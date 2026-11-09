#include "SkinCache.h"

#include <format>

#include <externals/DirectXTex/d3dx12.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"
#include "HTTEngine/Graphics/Pipeline/PSOManager.h"
#include "HTTEngine/Graphics/Profiling/GPUScope.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"

// 静的メンバ変数
std::unique_ptr<SkinCache> SkinCache::instance_ = nullptr;

namespace {
// Skinning.CS の SkinningInformation と同じ並び
struct SkinningInformation {
	uint32_t numVertices; // 頂点の数（はみ出たスレッドを止めるのに使う）
};
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void SkinCache::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼び出されています");
	instance_ = std::make_unique<SkinCache>(ConstructorKey());
	LogManager::Log("Initialized");
}

// ===== 解放 =====
// 出力バッファも一緒に消える。GPUを待った後（WindowManager::Finalize の後）に呼ぶ
void SkinCache::Release() {
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// 仕事を積む
//=============================================================================
D3D12_VERTEX_BUFFER_VIEW SkinCache::Request(Handle<Entity> entity, uint32_t meshIndex, const ModelManager::SubMesh& mesh, 
	D3D12_GPU_VIRTUAL_ADDRESS paletteAddress) {
	HTT_ASSERT_MSG(instance_, "SkinCache::Initialize()を先に呼んでください");
	HTT_ASSERT_MSG(mesh.isSkinned && mesh.influenceBuffer, "スキンのないメッシュは曲げられません");
	SkinCache& self = *instance_;
	const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());

	// ===== 出力バッファを探す（無ければ作る）=====
	Output& output = self.outputs_[Key{entity, meshIndex}]; // []は、無ければ空のOutputを作って入れる
	// --- 同じフレームに2回目: もう仕事を積んであるので、VBVだけ返す ---
	if (output.lastRequestedFrame == self.frame_) {
		return output.vbv;
	}
	// --- 初めて・モデルが差し替わって頂点の数が変わった: 作り直す ---
	if (output.vertexCount != vertexCount) {
		CreateOutput(output, vertexCount);
	}
	output.lastRequestedFrame = self.frame_;

	// ===== 仕事を積む（Dispatchはまだしない）=====
	Job job;
	job.paletteAddress = paletteAddress;
	job.inputAddress = mesh.vertexBuffer->GetGPUVirtualAddress();
	job.influenceAddress = mesh.influenceBuffer->GetGPUVirtualAddress();
	job.infoAddress = RenderContext::PushFrameConstant(SkinningInformation{vertexCount});
	job.output = output.buffer.Get();
	job.vertexCount = vertexCount;
	self.jobs_.push_back(job);
	return output.vbv;
}


//=============================================================================
// 出力バッファを作る
//=============================================================================
void SkinCache::CreateOutput(Output& output, uint32_t vertexCount) { 
	const size_t size = sizeof(Vertex3dData) * vertexCount;
	// CS が書く（UAV）ので ALLOW_UNORDERED_ACCESS。描くときは同じバッファを頂点バッファとして読む
	output.buffer = GpuResources::CreateDefaultBuffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	output.buffer->SetName(L"SkinCache_SkinnedVertex");
	output.vbv.BufferLocation = output.buffer->GetGPUVirtualAddress();
	output.vbv.SizeInBytes = static_cast<UINT>(size);
	output.vbv.StrideInBytes = sizeof(Vertex3dData); // 元の頂点と同じ形（Skinning.CSのVertex）
	output.vertexCount = vertexCount;
}


//=============================================================================
// 積んだ仕事を全部 Dispatch する
//=============================================================================
void SkinCache::Dispatch(ID3D12GraphicsCommandList* cmdList) {
	HTT_ASSERT_MSG(instance_, "SkinCache::Initialize()を先に呼んでください");
	SkinCache& self = *instance_;

	// ===== 1. このフレームにRequestされなかった出力を捨てる =====
	// 消えた・無効にしたEntity、シーンの切り替え、Stopなど描かれなくなった物
	std::erase_if(self.outputs_, [&](const auto& pair) { return pair.second.lastRequestedFrame != self.frame_; });

	// ===== 2. 仕事ががあれば、まとめて曲げる =====
	if (!self.jobs_.empty()) {
		GPU_SCOPE(cmdList, "Skinning");

		// --- 出力を「CS書ける状態」に（まとめて1回）---
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		barriers.reserve(self.jobs_.size());
		for (const Job& job : self.jobs_) {
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(job.output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		}
		cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

		// --- 1メッシュにつき1回 Dispatch（1スレッド = 1頂点）---
		const ComputePipeline& pipeline = PSOManager::GetComputePipeline("SkinningCS");
		pipeline.Bind(cmdList);
		for (const Job& job : self.jobs_) {
			pipeline.SetShaderResource(cmdList, "gMatrixPalette", job.paletteAddress);
			pipeline.SetShaderResource(cmdList, "gInputVertices", job.inputAddress);
			pipeline.SetShaderResource(cmdList, "gInfluences", job.influenceAddress);
			pipeline.SetUnorderedAccess(cmdList, "gOutputVertices", job.output->GetGPUVirtualAddress());
			pipeline.SetConstantBuffer(cmdList, "gSkinningInformation", job.infoAddress);
			pipeline.Dispatch(cmdList, job.vertexCount);
		}

		// --- 書き終わった出力を、頂点バッファとして読む状態に（まとめて1回）---
		barriers.clear();
		for (const Job& job : self.jobs_) {
			barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(job.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
				D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
		}
		cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
	}
	
	// ===== 3. 次のフレームへ =====
	self.jobs_.clear();
	++self.frame_;
}