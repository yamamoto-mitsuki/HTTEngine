#include "ComputePipeline.h"

#include <format>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "HTTEngine/String/ConvertString.h"


//=============================================================================
// 作成
//=============================================================================
ComputePipeline::ComputePipeline(const std::string& csName) : name_(csName) {
	// ===== CSをコンパイルして、中身を調べる =====
	const ShaderReflection& cs = ShaderPackageLoader::GetShaderReflection(csName);
	HTT_ASSERT_MSG(cs.threadGroupSize[0] != 0, std::format("'{}' はCSではありません（[numthreads] がありません）", csName));
	threadGroupSize_ = cs.threadGroupSize;

	// ===== RootSignature（リフレクションから自動で作る）=====
	ComputeRootSignatureInfo rootSignatureInfo = RootSignatureManager::MakeComputeRootSignatureInfo(cs.resources);
	rootSignature_ = std::move(rootSignatureInfo.rootSignature);
	parameterOf_ = std::move(rootSignatureInfo.parameterOf);
	rootSignature_->SetName(ConvertString("RootSig_" + csName).c_str());

	// ===== PSO =====
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.CS = {cs.blob->GetBufferPointer(), cs.blob->GetBufferSize()};
	HRESULT hr = DirectXCommon::GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", static_cast<uint32_t>(hr)));
		HTT_ASSERT_MSG(false, std::format("'{}' のComputePipelineStateの作成に失敗しました", csName));
	}
	pipelineState_->SetName(ConvertString(csName).c_str());

	LogManager::Log(std::format("Create ComputePipelineState: {} numthreads({}, {}, {})", csName, threadGroupSize_[0], threadGroupSize_[1], threadGroupSize_[2]));
}


//=============================================================================
// コマンドを積む
//=============================================================================
// ===== このCSを使う =====
void ComputePipeline::Bind(ID3D12GraphicsCommandList* cmdList) const { 
	// 描画用（SetGraphicsRootSignature）とは別の結び先。CSを積んでも、描画用のRootSignatureは消えない
	cmdList->SetComputeRootSignature(rootSignature_.Get());
	// PSOは描画用と同じ1つの枠。この後で描くときは、描画側が SetPipelineState で積み直す
	cmdList->SetPipelineState(pipelineState_.Get());
}

// ===== 定数（b）=====
void ComputePipeline::SetConstantBuffer(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address) const {
	HTT_ASSERT_MSG(address % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0, 
		std::format("'{}' の '{}'：定数バッファのアドレスが256バイトの倍数ではありません", name_, name));
	cmdList->SetComputeRootConstantBufferView(FindSlot(name, D3D_SIT_CBUFFER), address);
}

// ===== 読むだけ（t）=====
void ComputePipeline::SetShaderResource(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address) const {
	cmdList->SetComputeRootShaderResourceView(FindSlot(name, D3D_SIT_STRUCTURED), address);
}

// ===== 読み書き（u）=====
void ComputePipeline::SetUnorderedAccess(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address) const {
	cmdList->SetComputeRootUnorderedAccessView(FindSlot(name, D3D_SIT_UAV_RWSTRUCTURED), address);
}

// ===== テクスチャを読む（t。バインドレスの配列）=====
void ComputePipeline::SetTextureTable(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE heapStart) const {
	cmdList->SetComputeRootDescriptorTable(FindSlot(name, D3D_SIT_TEXTURE), heapStart);
}

// ===== テクスチャへ書く（u）=====
void ComputePipeline::SetUnorderedAccessTexture(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE uavHandle) const {
	cmdList->SetComputeRootDescriptorTable(FindSlot(name, D3D_SIT_UAV_RWTYPED), uavHandle);
}

// ===== 動かす =====
void ComputePipeline::Dispatch(ID3D12GraphicsCommandList* cmdList, uint32_t threadCountX, uint32_t threadCountY, uint32_t threadCountZ) const {
	// --- グループの数 = スレッドの数 ÷ 1グループのスレッドの数（切り上げ）---
	// 切り捨てだと、1000個・64スレッドのとき 15グループ=960スレッドになり、最後の40個が計算されない
	const auto groupCountOf = [](uint32_t threadCount, uint32_t groupSize) { return (threadCount + groupSize - 1) / groupSize; };
	const std::array<uint32_t, 3> groupCount = {
	    groupCountOf(threadCountX, threadGroupSize_[0]),
	    groupCountOf(threadCountY, threadGroupSize_[1]),
	    groupCountOf(threadCountZ, threadGroupSize_[2]),
	};
	// --- 1つの向きに並べられるグループは 65535個まで ---
	for (uint32_t count : groupCount) {
		HTT_ASSERT_MSG(count <= D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION, std::format("'{}'：グループの数が多すぎます（{}）", name_, count));
	}
	cmdList->Dispatch(groupCount[0], groupCount[1], groupCount[2]);
}


//=============================================================================
// HLSLの変数名 → ルートパラメータ番号
//=============================================================================
UINT ComputePipeline::FindSlot(const std::string& name, D3D_SHADER_INPUT_TYPE expectedType) const {
	auto it = parameterOf_.find(name);
	// CSの中で使っていない変数は、コンパイルで消えるので見つからない
	HTT_ASSERT_MSG(it != parameterOf_.end(), std::format("'{}' に '{}' という変数がありません（名前の打ち間違いか、CSの中で使っていない）", name_, name));
	HTT_ASSERT_MSG(it->second.type == expectedType, std::format("'{}' の '{}' は、この関数では結べない種類です（Set～ の関数を確かめる）", name_, name));
	return it->second.slot;
}