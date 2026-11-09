#include "DepthOfFieldPass.h"

#include <algorithm>
#include <cstring>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/Compute/ComputePipeline.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"
#include "HTTEngine/Graphics/Pipeline/PSOManager.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"

using namespace Microsoft::WRL;

namespace {
constexpr const char* kDownsampleCS = "DepthOfFieldDownsampleCS"; // 縮める CS（Post/DepthOfFieldDownsample.CS.hlsl）
constexpr const char* kBlurCS = "DepthOfFieldBlurCS";             // ぼかす CS（Post/DepthOfFieldBlur.CS.hlsl）
constexpr const char* kCompositePS = "DepthOfFieldCompositePS";   // 混ぜる PS（Post/DepthOfFieldComposite.PS.hlsl）
constexpr float kRadiusStep = 0.5f;                               // ぼかすときに読む点の間隔（半分の大きさのピクセル。小さいほどなめらかで重い）
constexpr float kReferenceHeight = 720.0f;                        // Max Blur（ピクセル）を決めた画面の高さ。大きい画面では、同じ見た目になるよう広げる

// ===== ピントの合う奥行き（m）=====
// 点に合わせるときは、その点をカメラの前の向きに測った長さ（真ん中から外れた所でも、同じ奥行きの面にピントが合う）
float CalcFocusDistance(const Camera& camera, const DepthOfFieldSettings& settings) {
	if (!settings.hasFocusPoint) {
		return settings.focusOverride.Apply(settings.focusDistance);
	}
	// ビュー行列の3列目が、カメラの前の向き（SceneRenderer::UploadCameraCB の right・up と同じ取り方）
	const Matrix4x4& view = camera.GetViewMatrix();
	const Vector3 forward = {view.m[0][2], view.m[1][2], view.m[2][2]};
	return settings.focusOverride.Apply((std::max)(Dot(settings.focusPoint - camera.GetTranslation(), forward), 0.0f));
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void DepthOfFieldPass::Initialize(uint32_t width, uint32_t height) {
	// 半分の大きさ（ぼかす量が同じなら、読む点の数が 4 分の 1 で済む。ぼけた絵なので細かさは要らない）
	const uint32_t halfWidth = (std::max)((width + 1) / 2, 1u);
	const uint32_t halfHeight = (std::max)((height + 1) / 2, 1u);
	// 色を足し込むので HDR（16bit の小数）にする。A にぼけの大きさ（マイナスもある）を入れる
	half_ = std::make_unique<ComputeTexture>(halfWidth, halfHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, L"DepthOfField_Half");
	blur_ = std::make_unique<ComputeTexture>(halfWidth, halfHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, L"DepthOfField_Blur");
	// ビューごとに別スロットへ書く。コマンドの実行はフレーム末なので使い回すと壊れる
	cb_ = GpuResources::CreateMappedUploadBuffer(kSlotSize * kMaxPassPerFrame, reinterpret_cast<void**>(&cbMapped_));
	cb_->SetName(L"DepthOfFieldCB");

	// CS は初めて使うときに作られるので、ここで作っておく（遊んでいる途中で、作る分だけ止まらないように）
	PSOManager::GetComputePipeline(kDownsampleCS);
	PSOManager::GetComputePipeline(kBlurCS);
	CreateRootSignature();
	CreatePSO();
	LogManager::Log("Initialized");
}

//=============================================================================
// RootSignature / PSO（合成の PS）
//=============================================================================
void DepthOfFieldPass::CreateRootSignature() {
	const ShaderReflection& vs = ShaderPackageLoader::GetShaderReflection("FullscreenVS");
	const ShaderReflection& ps = ShaderPackageLoader::GetShaderReflection(kCompositePS);
	auto resources = RootSignatureManager::MergeStages(vs.resources, ps.resources);
	rsInfo_ = RootSignatureManager::MakeRootSignatureInfo(resources);
}

void DepthOfFieldPass::CreatePSO() {
	IDxcBlob* vs = ShaderPackageLoader::GetShaderReflection("FullscreenVS").blob.Get();
	IDxcBlob* ps = ShaderPackageLoader::GetShaderReflection(kCompositePS).blob.Get();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rsInfo_.rootSignature.Get();
	desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
	desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
	desc.InputLayout = {nullptr, 0};
	desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.DepthStencilState.DepthEnable = FALSE;
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = RenderTexture::ToDXGIFormat(RenderTextureFormat::HDR); // 書く先は HDR の RenderTexture（PostProcess の depthOfFieldRT_。この後にブルーム・レンズ
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	HRESULT hr = DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&compositePSO_));
	HTT_ASSERT_MSG(SUCCEEDED(hr), "DepthOfField PSO作成失敗");
}

//=============================================================================
// 定数
//=============================================================================
DepthOfFieldPass::Param DepthOfFieldPass::MakeParam(RenderTexture& scene, const Camera& camera, const DepthOfFieldSettings& settings) const {
	Param param{};
	// --- 大きさ ---
	param.fullWidth = scene.GetWidth();
	param.fullHeight = scene.GetHeight();
	param.halfWidth = half_->GetWidth();
	param.halfHeight = half_->GetHeight();
	param.halfTexelSize = {1.0f / static_cast<float>(param.halfWidth), 1.0f / static_cast<float>(param.halfHeight)};
	// --- 深度（0〜1）→ カメラからの奥行き（m）の戻し方 ---
	// 透視投影は 深度 = A + B / 奥行き（A = m[2][2]、B = m[3][2]）なので、奥行き = B / (深度 − A)
	const Matrix4x4& projection = camera.GetProjectionMatrix();
	param.projectionA = projection.m[2][2];
	param.projectionB = projection.m[3][2];
	// --- ピント ---
	param.focusDistance = CalcFocusDistance(camera, settings);
	param.focusRange = (std::max)(settings.focusRange, 0.0f);
	param.farTransition = (std::max)(settings.farTransition, 0.01f); // 0 で割らないように
	param.nearTransition = (std::max)(settings.nearTransition, 0.0f);
	// --- ぼけの半径：高さ 720 の画面のピクセル → この絵のピクセル → 半分の大きさのピクセル ---
	const float blur = std::clamp(settings.maxBlur, 0.0f, kMaxBlurLimit);
	param.maxRadius = blur * (static_cast<float>(param.fullHeight) / kReferenceHeight) * 0.5f;
	param.radiusStep = kRadiusStep;
	// --- 読むテクスチャの番号 ---
	param.colorIndex = scene.GetSRVSlot();
	param.depthIndex = scene.GetDepthSRVSlot();
	param.halfIndex = half_->GetSRVSlot();
	param.blurIndex = blur_->GetSRVSlot();
	return param;
}

//=============================================================================
// ぼかした絵を作る
//=============================================================================
void DepthOfFieldPass::Record(RenderTexture& scene, const Camera& camera, const DepthOfFieldSettings& settings) {
	HTT_ASSERT_MSG(slot_ < kMaxPassPerFrame, "1フレームの被写界深度のパス数が上限を超えました");
	auto* cmdList = DirectXCommon::GetCommandList();

	// --- 定数（CS 2本と合成の PS で、同じ物を見る）---
	const Param param = MakeParam(scene, camera, settings);
	const size_t offset = slot_ * kSlotSize;
	std::memcpy(cbMapped_ + offset, &param, sizeof(Param));
	++slot_;
	currentCB_ = cb_->GetGPUVirtualAddress() + offset;

	// --- 元の絵の色と深度を、CS からも読める状態にする（Composite の最後に戻す）---
	scene_ = &scene;
	scene.BeginShaderRead();

	// --- CS が読み書きする窓口のヒープを見せる ---
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);

	// --- 1. 半分の大きさに縮めて、ぼけの大きさを作る ---
	Dispatch(kDownsampleCS, *half_, heapStart);
	// --- 2. ぼかす ---
	Dispatch(kBlurCS, *blur_, heapStart);
}

// ===== CS を1本動かす =====
void DepthOfFieldPass::Dispatch(const char* csName, ComputeTexture& output, D3D12_GPU_DESCRIPTOR_HANDLE heapStart) const {
	auto* cmdList = DirectXCommon::GetCommandList();
	const ComputePipeline& pipeline = PSOManager::GetComputePipeline(csName);
	output.BeginWrite();
	pipeline.Bind(cmdList);
	pipeline.SetConstantBuffer(cmdList, "gDof", currentCB_);
	pipeline.SetTextureTable(cmdList, "gTextures", heapStart);
	pipeline.SetUnorderedAccessTexture(cmdList, "gOutput", output.GetUAVGPUHandle());
	pipeline.Dispatch(cmdList, output.GetWidth(), output.GetHeight()); // 1スレッド ＝ 半分の大きさの1ピクセル
	output.EndWrite();                                                 // 書き終わってから、次の CS・PS が読む
}

//=============================================================================
// 合成（描画先は呼び出し側が設定済み）
//=============================================================================
void DepthOfFieldPass::Composite(float width, float height) {
	HTT_ASSERT_MSG(scene_ != nullptr, "Record を先に呼んでください");
	auto* cmdList = DirectXCommon::GetCommandList();
	D3D12_VIEWPORT vp{0.0f, 0.0f, width, height, 0.0f, 1.0f};
	D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sc);

	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	cmdList->SetGraphicsRootSignature(rsInfo_.rootSignature.Get());
	cmdList->SetPipelineState(compositePSO_.Get());
	cmdList->SetGraphicsRootConstantBufferView(rsInfo_.slotOf.at(RootBind::DepthOfField), currentCB_);
	cmdList->SetGraphicsRootDescriptorTable(rsInfo_.slotOf.at(RootBind::BindlessTexture), heapStart);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0); // フルスクリーン三角形

	// 読み終わったので、元の絵を元の状態へ（次のビューで、また描画先になる）
	scene_->EndShaderRead();
	scene_ = nullptr;
}