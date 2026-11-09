#include "LensPass.h"
#include <cstring>
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"
#include "MyEngine/Graphics/GPU/GpuResources.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"

using namespace Microsoft::WRL;

//=============================================================================
// 初期化
//=============================================================================
void LensPass::Initialize() {
	// ビューごとに別スロットへ書く。コマンドの実行はフレーム末なので、
	// 1つのバッファを使い回すと後のビューの値で上書きされてしまう
	cb_ = GpuResources::CreateMappedUploadBuffer(kSlotSize * kMaxPassPerFrame, reinterpret_cast<void**>(&cbMapped_));
	cb_->SetName(L"LensCB");

	CreateRootSignature();
	CreatePSO();
	LogManager::Log("Initialized");
}

//=============================================================================
// RootSignature作成
//=============================================================================
void LensPass::CreateRootSignature() {
	// シェーダーの反射情報から作る。gLens と gTextures が拾われる
	const ShaderReflection& vs = ShaderPackageLoader::GetShaderReflection("FullscreenVS");
	const ShaderReflection& ps = ShaderPackageLoader::GetShaderReflection("LensPS");
	auto resources = RootSignatureManager::MergeStages(vs.resources, ps.resources);
	rsInfo_ = RootSignatureManager::MakeRootSignatureInfo(resources);
}

//=============================================================================
// PSO作成
//=============================================================================
void LensPass::CreatePSO() {
	IDxcBlob* vs = ShaderPackageLoader::GetShaderReflection("FullscreenVS").blob.Get();
	IDxcBlob* ps = ShaderPackageLoader::GetShaderReflection("LensPS").blob.Get();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rsInfo_.rootSignature.Get();
	desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
	desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
	desc.InputLayout = {nullptr, 0}; // 頂点バッファを使わない
	desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.NumRenderTargets = 1;
	// 描画先はエディタのRenderTexture(SDR)とスワップチェーンの両方。どちらも同じ形式
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	HRESULT hr = DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_));
	MY_ASSERT_MSG(SUCCEEDED(hr), "Lens PSO作成失敗");
}

//=============================================================================
// 描画（描画先は呼び出し側が設定済み）
//=============================================================================
void LensPass::Render(RenderTexture& source, const ViewRect& rect, bool useLens, bool useColorAdjust) {
	MY_ASSERT_MSG(slot_ < kMaxPassPerFrame, "1フレームのレンズパス数が上限を超えました");
	auto* cmdList = DirectXCommon::GetCommandList();

	// --- パラメータを作る。切ってあるときはレンズの値が全部0・彩度が1＝露出・トーンマップだけ掛けたコピーになる ---
	Param param{};
	param.srcIndex = source.GetSRVSlot();
	param.exposure = kExposure;
	param.toneMap = static_cast<uint32_t>(toneMap);
	param.saturation = useColorAdjust ? kSaturation : 1.0f;
	param.dither = dither ? 1u : 0u; // 画面へ書くときの事なので、レンズ・色の調整を切ったビューでも掛ける
	if (useLens) {
		param.distortion = kDistortion + addDistortion;
		param.aberration = kAberration + addAberration;
		param.radialBlur = kRadialBlur + addRadialBlur;
		param.vignette = kVignette + addVignette;
		param.windLines = windLines;
		param.windLineTime = windLineTime;
		param.windLineDirection = windLineDirection;
	}
	param.aspect = (rect.height > 0.0f) ? rect.width / rect.height : 1.0f;
	// 上乗せ分は使ったら消す。毎フレーム書かないと自然に元へ戻る
	addDistortion = 0.0f;
	addAberration = 0.0f;
	addRadialBlur = 0.0f;
	addVignette = 0.0f;

	size_t offset = slot_ * kSlotSize;
	std::memcpy(cbMapped_ + offset, &param, sizeof(Param));
	++slot_;

	// --- ビューポートは描く四角に合わせる（黒帯がある画面では、ウィンドウの一部）---
	D3D12_VIEWPORT viewport{rect.x, rect.y, rect.width, rect.height, 0.0f, 1.0f};
	D3D12_RECT scissor{static_cast<LONG>(rect.x), static_cast<LONG>(rect.y), static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height)};
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissor);

	// --- バインド ---
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	cmdList->SetGraphicsRootSignature(rsInfo_.rootSignature.Get());
	cmdList->SetPipelineState(pso_.Get());
	cmdList->SetGraphicsRootConstantBufferView(rsInfo_.slotOf.at(RootBind::Lens), cb_->GetGPUVirtualAddress() + offset);
	cmdList->SetGraphicsRootDescriptorTable(rsInfo_.slotOf.at(RootBind::BindlessTexture), heapStart);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 頂点バッファなしで画面全体を覆う三角形を1枚
	cmdList->DrawInstanced(3, 1, 0, 0);
}