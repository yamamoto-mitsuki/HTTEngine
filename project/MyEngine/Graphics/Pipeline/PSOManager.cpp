#include "PSOManager.h"
#include <format>
#include <cassert>
#include <externals/magic_enum/magic_enum.hpp>
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"
#include "MyEngine/Graphics/Pipeline/RootSignatureManager.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/RenderTarget/RenderTexture.h"

using namespace Microsoft::WRL;
std::unique_ptr<PSOManager> PSOManager::instance_ = nullptr;

namespace {
// ===== 描く形 → 描画先の形式 =====
// 3D は float の絵（HDR。1 を超える明るさを残す）に描き、2D はトーンマップの後の画面（SDR）に重ねる（SceneRenderer の描く順）
RenderTextureFormat GetTargetFormat(DrawCategory category) {
	switch (category) {
	case DrawCategory::Model:
	case DrawCategory::Particle:
	case DrawCategory::Line:
	case DrawCategory::Decal:
		return RenderTextureFormat::HDR;
	case DrawCategory::Sprite:
	case DrawCategory::Text:
	case DrawCategory::VectorShape:
	case DrawCategory::Polyline:
		return RenderTextureFormat::SDR;
	}
	return RenderTextureFormat::SDR;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
//  ===== 初期化 =====
void PSOManager::Initialize() {
	MY_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼び出されています");
	instance_ = std::make_unique<PSOManager>(ConstructorKey());
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void PSOManager::Release() {
	instance_.reset();
	instance_ = nullptr;
	LogManager::Log("Released");
}


//=============================================================================
// SortKey
//=============================================================================
uint64_t PSOManager::GetSortKey(const PSOKey& key) {
	// PSOKey の全部を入れる（入れていない物だけが違う2つが同じキーになると、PSO を切り替えずに描いてしまう）
	return (static_cast<uint64_t>(key.shaderProgramID) << 32) // Shader
	     | (static_cast<uint64_t>(key.stencilMode)     << 24) // StencilMode（デカールを受けない印）
	     | (static_cast<uint64_t>(key.blendMode)       << 16) // BlendMode
	     | (static_cast<uint64_t>(key.rasterizerType)  << 8)  // Rasterizer
	     |  static_cast<uint64_t>(key.depthMode);              // DepthMode
}


//=============================================================================
// RootSignatureInfoを取得
//=============================================================================
const RootSignatureInfo& PSOManager::GetRootSignatureInfo(uint32_t id) {
	auto& cache = instance_->rootSignatureCache_;
	// キャッシュを確認
	if (id >= cache.size()) {
		cache.resize(ShaderPackageLoader::GetProgramCount());
	}
	
	RootSignatureInfo& slot = cache[id]; // RootSignature情報を取得
	// 取得できなかったら、新しく作って登録
	if (!slot.rootSignature) {
		const ProgramEntry& p = ShaderPackageLoader::GetProgramAt(id);
		const ShaderReflection& vs = ShaderPackageLoader::GetShaderReflection(p.vsName);
		const ShaderReflection& ps = ShaderPackageLoader::GetShaderReflection(p.psName);
		auto merged = RootSignatureManager::MergeStages(vs.resources, ps.resources);
		slot = RootSignatureManager::MakeRootSignatureInfo(merged);
		slot.rootSignature->SetName(ConvertString("RootSig_" + p.name).c_str());
	}
	return slot;
}



//=============================================================================
// 描画情報 → ID・PSOKey
//=============================================================================
// ===== ShaderProgramID =====
uint32_t PSOManager::GetShaderProgramID(DrawCategory category, ShadingType type) { 
	return static_cast<uint32_t>(ShaderPackageLoader::GetProgramIndex(category, type));
}

// ===== TopologyID =====
TopologyID PSOManager::GetTopologyID(DrawCategory category) {
	switch (category) {
	case DrawCategory::Line: // 線の描画のみIDがLine
		return TopologyID::Line;
	default:
		return TopologyID::Triangle;
	}
	return TopologyID::Triangle;
}

// ===== PSOKey =====
PSOKey PSOManager::GetPSOKey(DrawCategory category, ShadingType shading, BlendMode blend, RasterizerType raster, DepthMode depth, StencilMode stencil) {
	return PSOKey{GetShaderProgramID(category, shading), blend, raster, depth, stencil};
}

//=============================================================================
// ID → D3D12・Shader
//=============================================================================
D3D12_PRIMITIVE_TOPOLOGY_TYPE PSOManager::GetTopologyType(TopologyID id) {
	switch (id) {
	case TopologyID::Triangle: // Line以外
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	case TopologyID::Line: // Line
		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	}
	return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}


//=============================================================================
// Key → PSO
//=============================================================================
// ===== PSOName =====
const char* PSOManager::GetStateName(const PSOKey& key) { return ShaderPackageLoader::GetProgramAt(key.shaderProgramID).name.c_str(); }

// ===== PipelineState =====
Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::GetPSO(const PSOKey& key) {
	auto& map = instance_->psoMap_;
	if (auto it = map.find(key); it != map.end()) {
		return it->second.Get();
	}

	std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = MakePSO(key, elems);
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso = CreatePSO(psoDesc);
	pso->SetName(ConvertString(ShaderPackageLoader::GetProgramAt(key.shaderProgramID).name).c_str());
	map.emplace(key, pso);
	return pso;
}


//=============================================================================
// CSの名前 → ComputePipeline
//=============================================================================
const ComputePipeline& PSOManager::GetComputePipeline(const std::string& csName) {
	// try_emplace：その名前がまだ無いときだけ ComputePipeline(csName) を作って入れる。あれば何も作らず、あった物を返す
	auto [it, inserted] = instance_->computePipelines_.try_emplace(csName, csName);
	return it->second;
}


//=============================================================================
// PSO作成
//=============================================================================
// ===== PSOKey → PipelineStateDesc =====
D3D12_GRAPHICS_PIPELINE_STATE_DESC PSOManager::MakePSO(const PSOKey& key, std::vector<D3D12_INPUT_ELEMENT_DESC>& outElems) {
	// Shaderファイル
	const ProgramEntry& p = ShaderPackageLoader::GetProgramAt(key.shaderProgramID);
	const ShaderReflection& vs = ShaderPackageLoader::GetShaderReflection(p.vsName);
	const ShaderReflection& ps = ShaderPackageLoader::GetShaderReflection(p.psName);
	// RootSignature
	const RootSignatureInfo& rs = GetRootSignatureInfo(key.shaderProgramID);
	// 入力レイアウト
	outElems = VertexFormat::MakeInputLayout(vs.inputs);
	D3D12_INPUT_LAYOUT_DESC inputLayout{outElems.data(), static_cast<UINT>(outElems.size())};
	// トポロジ
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = GetTopologyType(GetTopologyID(p.category));

	// PipelineStateDesc
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rs.rootSignature.Get();
	desc.InputLayout = inputLayout;
	desc.VS = {vs.blob->GetBufferPointer(), vs.blob->GetBufferSize()};
	desc.PS = {ps.blob->GetBufferPointer(), ps.blob->GetBufferSize()};
	desc.BlendState = RenderStates::MakeBlendDesc(key.blendMode);
	desc.RasterizerState = RenderStates::MakeRasterizerDesc(key.rasterizerType);
	desc.DepthStencilState = RenderStates::MakeDepthStencilDesc(key.depthMode, key.stencilMode);
	desc.PrimitiveTopologyType = topology;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = RenderTexture::ToDXGIFormat(GetTargetFormat(p.category)); // 3D は HDR、2D は SDR
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	return desc;
}

// ===== PipelineStateDesc → CreatePSO =====
Microsoft::WRL::ComPtr<ID3D12PipelineState> PSOManager::CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	HRESULT hr = DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		MY_ASSERT_MSG(false, "GraphicsPipelineStateの作成に失敗しました");
	}

	LogManager::Log("Create GraphicsPipelineState");
	return pipelineState;
}