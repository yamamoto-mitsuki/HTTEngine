#include "BloomPass.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"

using namespace Microsoft::WRL;

//=============================================================================
// 初期化
//=============================================================================
void BloomPass::Initialize(uint32_t width, uint32_t height) {
	// 縮小テクスチャを2列作る。加算合成を使わずに済むよう、行きと帰りで別々に持つ
	uint32_t w = width;
	uint32_t h = height;
	for (uint32_t i = 0; i < kLevelCount; ++i) {
		w = std::max(w / 2u, 1u);
		h = std::max(h / 2u, 1u);
		// 足し込みで飽和しないようHDRにする。深度は使わない
		down_[i] = std::make_unique<RenderTexture>(w, h, RenderTextureFormat::HDR, false);
		up_[i] = std::make_unique<RenderTexture>(w, h, RenderTextureFormat::HDR, false);
	}
	// 川瀬式の段（1/4・1/8・1/16・1/32）。段ごとに「縮めた絵・縦にぼかした結果」と「横にぼかした結果」の2枚
	w = std::max(width / 2u, 1u);
	h = std::max(height / 2u, 1u);
	for (uint32_t i = 0; i < kLevelCount; ++i) {
		w = std::max(w / 2u, 1u);
		h = std::max(h / 2u, 1u);
		kawaseLevels_[i] = std::make_unique<RenderTexture>(w, h, RenderTextureFormat::HDR, false);
		kawaseTemp_[i] = std::make_unique<RenderTexture>(w, h, RenderTextureFormat::HDR, false);
	}
	// パスごとに別スロットへ書く。コマンドの実行はフレーム末なので使い回すと壊れる
	cb_ = GpuResources::CreateMappedUploadBuffer(kSlotSize * kMaxPassPerFrame, reinterpret_cast<void**>(&cbMapped_));
	cb_->SetName(L"BloomCB");

	CreateRootSignature();
	brightPSO_ = CreatePSO("BloomBrightPS");
	downPSO_ = CreatePSO("BloomDownPS");
	upPSO_ = CreatePSO("BloomUpPS");
	compositePSO_ = CreatePSO("BloomCompositePS");
	gaussianPSO_ = CreatePSO("BloomGaussianPS");
	kawaseCompositePSO_ = CreatePSO("BloomKawaseCompositePS");
	LogManager::Log("Initialized");
}

//=============================================================================
// RootSignature / PSO
//=============================================================================
void BloomPass::CreateRootSignature() {
	const ShaderReflection& vs = ShaderPackageLoader::GetShaderReflection("FullscreenVS");
	const ShaderReflection& ps = ShaderPackageLoader::GetShaderReflection("BloomCompositePS");
	auto resources = RootSignatureManager::MergeStages(vs.resources, ps.resources);
	rsInfo_ = RootSignatureManager::MakeRootSignatureInfo(resources);
}

ComPtr<ID3D12PipelineState> BloomPass::CreatePSO(const char* psName) {
	IDxcBlob* vs = ShaderPackageLoader::GetShaderReflection("FullscreenVS").blob.Get();
	IDxcBlob* ps = ShaderPackageLoader::GetShaderReflection(psName).blob.Get();

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
	// 中間の段も、合成先（PostProcess の work_）も HDR（合成した絵は、この後のレンズのパスでトーンマップして画面へ）
	desc.RTVFormats[0] = RenderTexture::ToDXGIFormat(RenderTextureFormat::HDR);
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ComPtr<ID3D12PipelineState> pso;
	HRESULT hr = DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
	HTT_ASSERT_MSG(SUCCEEDED(hr), "Bloom PSO作成失敗");
	return pso;
}

//=============================================================================
// 1パス分のバインドと描画
//=============================================================================
void BloomPass::BindAndDraw(ID3D12PipelineState* pso, const Param& param) {
	HTT_ASSERT_MSG(slot_ < kMaxPassPerFrame, "1フレームのブルームパス数が上限を超えました");
	auto* cmdList = DirectXCommon::GetCommandList();

	size_t offset = slot_ * kSlotSize;
	std::memcpy(cbMapped_ + offset, &param, sizeof(Param));
	++slot_;

	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	cmdList->SetGraphicsRootSignature(rsInfo_.rootSignature.Get());
	cmdList->SetPipelineState(pso);
	cmdList->SetGraphicsRootConstantBufferView(rsInfo_.slotOf.at(RootBind::Bloom), cb_->GetGPUVirtualAddress() + offset);
	cmdList->SetGraphicsRootDescriptorTable(rsInfo_.slotOf.at(RootBind::BindlessTexture), heapStart);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0); // フルスクリーン三角形
}

// 描画先ごとにビューポートを張り直す必要がある
void BloomPass::SetTarget(RenderTexture& target) {
	auto* cmdList = DirectXCommon::GetCommandList();
	target.PreDraw();
	D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(target.GetWidth()), static_cast<float>(target.GetHeight()), 0.0f, 1.0f};
	D3D12_RECT sc{0, 0, static_cast<LONG>(target.GetWidth()), static_cast<LONG>(target.GetHeight())};
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sc);
}

//=============================================================================
// 光の層を作る
//=============================================================================
void BloomPass::Record(RenderTexture& scene) {
	// --- 明るい部分を抜き出して半分の解像度へ（Dual・Kawase で同じ）---
	{
		Param p{};
		p.texelSize[0] = 1.0f / scene.GetWidth();
		p.texelSize[1] = 1.0f / scene.GetHeight();
		p.threshold = std::max(kThreshold + addThreshold, 0.0f); // 負にすると全部真っ白になる
		p.knee = std::max(kKnee, 0.0001f);
		p.srcIndex = scene.GetSRVSlot();
		SetTarget(*down_[0]);
		BindAndDraw(brightPSO_.Get(), p);
		down_[0]->PostDraw();
	}

	// --- ぼかし方ごと ---
	if (mode == BloomMode::Kawase) {
		RecordKawase();
	} else {
		RecordDual();
	}
}

// ===== Dual：縮めながらぼかし、戻しながら足す =====
void BloomPass::RecordDual() {
	// --- 段々に縮めながらぼかす ---
	for (uint32_t i = 1; i < kLevelCount; ++i) {
		Param p{};
		p.texelSize[0] = 1.0f / down_[i - 1]->GetWidth();
		p.texelSize[1] = 1.0f / down_[i - 1]->GetHeight();
		p.srcIndex = down_[i - 1]->GetSRVSlot();
		SetTarget(*down_[i]);
		BindAndDraw(downPSO_.Get(), p);
		down_[i]->PostDraw();
	}

	// --- 一番小さい段から戻しながら、同じ大きさの縮小結果を足していく ---
	for (int i = static_cast<int>(kLevelCount) - 2; i >= 0; --i) {
		RenderTexture& src = (i == static_cast<int>(kLevelCount) - 2) ? *down_[kLevelCount - 1] : *up_[i + 1];
		Param p{};
		p.texelSize[0] = 1.0f / src.GetWidth();
		p.texelSize[1] = 1.0f / src.GetHeight();
		p.srcIndex = src.GetSRVSlot();
		p.addIndex = down_[i]->GetSRVSlot();
		SetTarget(*up_[i]);
		BindAndDraw(upPSO_.Get(), p);
		up_[i]->PostDraw();
	}
}

// ===== Kawase（MGF）：段ごとに縮めて、横 → 縦にガウスでぼかす =====
//   抜き出し(1/2) ─縮める→ 段0(1/4) ─横→ 作業0 ─縦→ 段0'
//   段0'          ─縮める→ 段1(1/8) ─横→ 作業1 ─縦→ 段1'  … 段3(1/32) まで
// 次の段は、ぼかした後の段から縮める（ぼかしが段ごとに重なって、小さい段ほど広く広がる）
// 合成（Composite）で 段0'〜段3' を重みを付けて全部足す
void BloomPass::RecordKawase() {
	RenderTexture* source = down_[0].get(); // 縮める元（初めは抜き出した絵）
	for (uint32_t i = 0; i < kLevelCount; ++i) {
		RenderTexture& level = *kawaseLevels_[i];
		RenderTexture& temp = *kawaseTemp_[i];
		const float texelX = 1.0f / level.GetWidth();
		const float texelY = 1.0f / level.GetHeight();

		// --- 縮める（今のテントをそのまま使う）---
		{
			Param p{};
			p.texelSize[0] = 1.0f / source->GetWidth();
			p.texelSize[1] = 1.0f / source->GetHeight();
			p.srcIndex = source->GetSRVSlot();
			SetTarget(level);
			BindAndDraw(downPSO_.Get(), p);
			level.PostDraw();
		}
		// --- 横にぼかす（段 → 作業用）---
		{
			Param p{};
			p.texelSize[0] = texelX;
			p.texelSize[1] = texelY;
			p.direction[0] = 1.0f;
			p.srcIndex = level.GetSRVSlot();
			SetTarget(temp);
			BindAndDraw(gaussianPSO_.Get(), p);
			temp.PostDraw();
		}
		// --- 縦にぼかす（作業用 → 段。読む絵と書く絵を同じにしない）---
		{
			Param p{};
			p.texelSize[0] = texelX;
			p.texelSize[1] = texelY;
			p.direction[1] = 1.0f;
			p.srcIndex = temp.GetSRVSlot();
			SetTarget(level);
			BindAndDraw(gaussianPSO_.Get(), p);
			level.PostDraw();
		}
		source = &level;
	}
}

//=============================================================================
// 合成（描画先は呼び出し側が設定済み）
//=============================================================================
void BloomPass::Composite(RenderTexture& scene, float width, float height) {
	auto* cmdList = DirectXCommon::GetCommandList();
	D3D12_VIEWPORT vp{0.0f, 0.0f, width, height, 0.0f, 1.0f};
	D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sc);

	Param p{};
	p.intensity = kIntensity + addIntensity;
	p.srcIndex = scene.GetSRVSlot();
	if (mode == BloomMode::Kawase) {
		// 川瀬式：段を全部、重みを付けて足す（1回の PS で4枚を読む。Bindless なので SRV の番号を渡すだけ）
		for (uint32_t i = 0; i < kLevelCount; ++i) {
			p.levelWeights[i] = kKawaseWeights[i];
			p.levelIndices[i] = kawaseLevels_[i]->GetSRVSlot();
		}
		BindAndDraw(kawaseCompositePSO_.Get(), p);
	} else {
		p.addIndex = up_[0]->GetSRVSlot();
		BindAndDraw(compositePSO_.Get(), p);
	}

	// 上乗せ分は使ったら消す
	addIntensity = 0.0f;
	addThreshold = 0.0f;
}