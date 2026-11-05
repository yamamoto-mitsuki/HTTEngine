#include "MyEngine/Graphics/RenderTarget/RenderTexture.h"

#include <format>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"
#include "MyEngine/Graphics/GPU/GpuResources.h"
#include "MyEngine/Graphics/RenderTarget/RenderWindow.h"

using namespace Microsoft::WRL;

//=============================================================================
// enum format → DXGI_FORMAT変換
//=============================================================================
DXGI_FORMAT RenderTexture::ToDXGIFormat(RenderTextureFormat format) {
	switch (format) {
	case RenderTextureFormat::SDR:
		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // SDR
	case RenderTextureFormat::HDR:
		return DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR
	case RenderTextureFormat::RG16F:
		return DXGI_FORMAT_R16G16_FLOAT; // RG16F
	}
	MY_ASSERT_MSG(false, "未対応のRenderTextureFormatです");
	return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

//=============================================================================
// コンストラクタ・デストラクタ
//=============================================================================
// ===== コンストラクタ =====
RenderTexture::RenderTexture(uint32_t width, uint32_t height, RenderTextureFormat format, bool useDepth, bool isDepthReadable) {
	MY_ASSERT_MSG(useDepth || !isDepthReadable, "深度を読むには、深度あり（useDepth）で作ってください");
	// これから作るTextureの設定
	width_ = width;
	height_ = height;
	format_ = ToDXGIFormat(format);
	useDepth_ = useDepth;
	isDepthReadable_ = isDepthReadable;

	// RTV, SRV作成
	CreateResource();
	// DSV作成するか
	if (useDepth_) {
		CreateDSVResource();
	}

	LogManager::Log(std::format("RenderTexture created {}x{} SRVSlot={}", width, height, srvSlot_));
}

// ===== デストラクタ =====
// SRVスロットを返す（返さないと、作り直すたびに空きが減り続け、256個を使い切ったところで止まる）
RenderTexture::~RenderTexture() {
	DirectXCommon::FreeSRVSlot(srvSlot_);
	if (isDepthReadable_) {
		DirectXCommon::FreeSRVSlot(depthSrvSlot_);
	}
}

//=============================================================================
// 描画開始
//=============================================================================
void RenderTexture::PreDraw() {
	// ===== ResourceStateを切り替える =====
	// PIXEL_SHADER_RESOURCE（ImGuiが読める状態）→ RENDER_TARGET（描画先として使える状態）
	DirectXCommon::TransitionBarrier(
	    resource_.Get(),
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // ImGuiが読んでいた状態
	    D3D12_RESOURCE_STATE_RENDER_TARGET);        // 描画先として使える状態

	// ===== RTVとDSVをセットしてクリア =====
	auto* cmdList = DirectXCommon::GetCommandList();
	if (useDepth_) {
		cmdList->OMSetRenderTargets(1, &rtvHandle_, false, &dsvHandle_);
		// 深度と一緒にステンシルも 0 にする（UI のマスクは、外にあるマスクの数をステンシルに数える）
		cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	} else {
		// 深度なし（IBL畳み込みなどフルスクリーンパス用）
		cmdList->OMSetRenderTargets(1, &rtvHandle_, false, nullptr);
	}
	cmdList->ClearRenderTargetView(rtvHandle_, RenderWindow::kClearColor.data(), 0, nullptr);
}

//=============================================================================
// 描画終了
//=============================================================================
void RenderTexture::PostDraw() {
	// ===== ResourceStateを元に戻す =====
	// RENDER_TARGET（描画先として使える状態）→ PIXEL_SHADER_RESOURCE（ImGuiが読める状態）
	DirectXCommon::TransitionBarrier(
	    resource_.Get(),
	    D3D12_RESOURCE_STATE_RENDER_TARGET,          // 描画先として使っていた状態
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); // ImGuiが読める状態
}

//=============================================================================
// ほかの RenderTexture の深度で描く
//=============================================================================
void RenderTexture::BindWithDepthOf(RenderTexture& depthSource) {
	MY_ASSERT_MSG(depthSource.useDepth_, "depthSource は深度あり（useDepth）で作ってください");
	// 大きさが違うと、深度と色のピクセルが合わない
	MY_ASSERT_MSG(depthSource.width_ == width_ && depthSource.height_ == height_, "depthSource と大きさが違います");
	DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandle_, false, &depthSource.dsvHandle_);
}

//=============================================================================
// CS・PS から読む
//=============================================================================
// ===== 読める状態にする =====
void RenderTexture::BeginShaderRead() {
	// 色：PS だけが読める状態 → CS からも PS からも読める状態（ALL_SHADER_RESOURCE ＝ NON_PIXEL ＋ PIXEL）
	// PIXEL_SHADER_RESOURCE のまま CS で読むと、デバッグレイヤーに止められる
	DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	// 深度：書く状態 → 読む状態
	if (isDepthReadable_) {
		DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	}
}

// ===== 元に戻す =====
void RenderTexture::EndShaderRead() {
	DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	if (isDepthReadable_) {
		DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	}
}

//=============================================================================
// 深度を読みながら描く
//=============================================================================
// ===== 読むだけで結ぶ =====
void RenderTexture::BeginDepthRead() {
	MY_ASSERT_MSG(isDepthReadable_, "深度を読むには、isDepthReadable で作ってください");
	// 書く状態 → 深度テストで読む ＋ PS で読む（DEPTH_READ と PIXEL_SHADER_RESOURCE は一緒にできる）
	DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandle_, false, &readOnlyDsvHandle_);
}

// ===== 書く状態に戻す =====
void RenderTexture::EndDepthRead() {
	DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandle_, false, &dsvHandle_);
}

//=============================================================================
// RTVとSRV共通のリソースを生成してRTV/SRVを登録する
//=============================================================================
void RenderTexture::CreateResource() {
	// Resource
	resource_ = GpuResources::CreateRenderTargetTextureResource(width_, height_, format_, RenderWindow::kClearColor.data());
	resource_->SetName(L"RenderTexture");

	// ===== RTV登録 =====
	// rtvがどこのアドレスにあるか登録
	rtvDescriptorHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
	rtvHandle_ = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	rtvDescriptorHeap_->SetName(L"RenderTexture_RTVHeap");
	// view情報
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = format_;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	DirectXCommon::GetDevice()->CreateRenderTargetView(resource_.Get(), &rtvDesc, rtvHandle_);

	// ===== SRV登録 =====
	// srvがどこのアドレスにあるか登録
	srvSlot_ = DirectXCommon::AllocateSRVSlot(); // DescriptorHeapからスロット番号取得
	srvHandleCPU_ = DirectXCommon::GetCPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_);
	srvHandleGPU_ = DirectXCommon::GetGPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_);
	// view情報
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format_;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	DirectXCommon::GetDevice()->CreateShaderResourceView(resource_.Get(), &srvDesc, srvHandleCPU_);
}

//=============================================================================
// 深度バッファを生成する
//=============================================================================
void RenderTexture::CreateDSVResource() {
	// Resource（読めるようにするときは、型なしで作る）
	depthResource_ = GpuResources::CreateDepthStencilTextureResource(static_cast<int32_t>(width_), static_cast<int32_t>(height_), isDepthReadable_);
	// DSVDescriptorHeap（深度を読めるときは、読むだけの窓口の分も入れて2つ）
	dsvDescriptorHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, isDepthReadable_ ? 2 : 1, false);
	dsvHandle_ = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	// DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	DirectXCommon::GetDevice()->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsvHandle_);
	dsvDescriptorHeap_->SetName(L"RenderTexture_DSVHeap");

	// ===== 深度を読むだけの窓口（DSV）=====
	// READ_ONLY を付けた DSV は、深度テスト・ステンシルテストには使えるが、書けない。その代わり、同じ深度を SRV で読みながら結んでおける
	if (isDepthReadable_) {
		readOnlyDsvHandle_ = DirectXCommon::GetCPUDescriptorHandle(dsvDescriptorHeap_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
		D3D12_DEPTH_STENCIL_VIEW_DESC readOnlyDesc = dsvDesc;
		readOnlyDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		DirectXCommon::GetDevice()->CreateDepthStencilView(depthResource_.Get(), &readOnlyDesc, readOnlyDsvHandle_);
	}

	// ===== 深度を読む窓口（SRV）=====
	// 同じ中身を、書くとき（DSV）は D24_UNORM_S8_UINT、読むとき（SRV）は R24_UNORM_X8_TYPELESS として見る
	// R24_UNORM_X8：上の 24bit（深度）を 0〜1 の数として読み、残りの 8bit（ステンシル）は読まない
	if (isDepthReadable_) {
		depthSrvSlot_ = DirectXCommon::AllocateSRVSlot();
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = DirectXCommon::GetCPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, depthSrvSlot_);
		DirectXCommon::GetDevice()->CreateShaderResourceView(depthResource_.Get(), &srvDesc, srvHandle);
	}
}