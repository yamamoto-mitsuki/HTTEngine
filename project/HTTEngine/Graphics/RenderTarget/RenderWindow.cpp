#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"

#include <format>

#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/Window/Win32Window.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"


//=============================================================================
// 初期化
//=============================================================================
void RenderWindow::Initialize(Win32Window* window) {
	// 対象情報の保持
	window_ = window;
	RECT wr;
	GetClientRect(window_->GetHWND(), &wr);

	// ===== スワップチェーンの生成 =====
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = wr.right - wr.left;                         // 画面の幅。ウィンドウのクライアント領域と同じにしておく
	swapChainDesc.Height = wr.bottom - wr.top;                        // 画面の高さ。ウィンドウのクライアント領域と同じにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;                // 色の形式（1ピクセルのビット構成）
	swapChainDesc.SampleDesc.Count = 1;                               // マルチサンプル設定
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;      // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = DirectXCommon::kSwapChainBufferCount; // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;         // モニタにうつしたら、中身を破棄
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;            // バックバッファのアルファ値をDWM（デスクトップ合成）時にどう扱うか
	                                                                  // ウィンドウ自体を透過させたいときとかに使う
	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	HRESULT hr = DirectXCommon::GetFactory()->CreateSwapChainForHwnd(
	   DirectXCommon::GetCommandQueue(), window_->GetHWND(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "スワップチェーンが生成できませんでした");
	}
	// Alt+Enter の既定動作（DXGIの排他フルスクリーン）を止める。切り替えは Win32Window が行う
	DirectXCommon::GetFactory()->MakeWindowAssociation(window_->GetHWND(), DXGI_MWA_NO_ALT_ENTER);

	// ===== スワップチェーンからResourceを取得 =====
	hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapChainResources_[0]));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "スワップチェーンが持っている1つ目のResourceを取得できませんでした");
	}
	hr = swapChain_->GetBuffer(1, IID_PPV_ARGS(&swapChainResources_[1]));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "スワップチェーンが持っている2つ目のResourceを取得できませんでした");
	}
	// デバッグレイヤー/PIXでリソースを識別できるように名前を付ける
	swapChainResources_[0]->SetName(L"SwapChainBackBuffer0");
	swapChainResources_[1]->SetName(L"SwapChainBackBuffer1");

	// ===== RTVDescriptorHeapを作成 =====
	rtvDescriptorHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, DirectXCommon::kSwapChainBufferCount, false);

	// ===== RTVの作成 =====
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;      // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
	// 1つ目
	rtvHandles_[0] = DirectXCommon::GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 0);
	DirectXCommon::GetDevice()->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc, rtvHandles_[0]);
	// 2つ目
	rtvHandles_[1] = DirectXCommon::GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	DirectXCommon::GetDevice()->CreateRenderTargetView(swapChainResources_[1].Get(), &rtvDesc, rtvHandles_[1]);

	// ===== DepthStencilResourceの生成 =====
	depthStencilResource_ = GpuResources::CreateDepthStencilTextureResource(wr.right - wr.left, wr.bottom - wr.top);
	depthStencilResource_->SetName(L"RenderWindow_DepthStencil");

	// ===== DSVDescriptorHeapの生成 =====
	dsvDescriptorHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// ===== DSVの生成 =====
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;        // Resourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
	dsvHandle_ = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	DirectXCommon::GetDevice()->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvHandle_);

	// ===== ウィンドウサイズ定数バッファの生成（2DシェーダーのNDC変換用）=====
	windowSizeBuffer_ = GpuResources::CreateUploadBuffer(sizeof(float) * 4);
	windowSizeBuffer_->SetName(L"RenderWindow_WindowSizeCB");
	windowSizeBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&windowSizeMappedPtr_));

	// ウィンドウサイズ保存
	windowSizeMappedPtr_[0] = static_cast<float>(wr.right - wr.left);
	windowSizeMappedPtr_[1] = static_cast<float>(wr.bottom - wr.top);
	windowSizeMappedPtr_[2] = 0.0f;
	windowSizeMappedPtr_[3] = 0.0f;

	LogManager::Log("Initialized");
}

//=============================================================================
// 描画開始
//=============================================================================
void RenderWindow::PreDraw() {
	// これから書き込むバックバッファのインデックスを取得
	currentBackBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// ===== トランジションバリアを張る（PresentからRenderTargetへ）=====
	DirectXCommon::TransitionBarrier(swapChainResources_[currentBackBufferIndex_].Get(),
	    D3D12_RESOURCE_STATE_PRESENT, 
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	// ===== RTVとDSVをセットしてクリア =====
	DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandles_[currentBackBufferIndex_], false, &dsvHandle_);
	DirectXCommon::GetCommandList()->ClearRenderTargetView(rtvHandles_[currentBackBufferIndex_], kClearColor.data(), 0, nullptr);
	// 深度と一緒にステンシルも 0 にする（UI のマスクは、外にあるマスクの数をステンシルに数える）
	DirectXCommon::GetCommandList()->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}

//=============================================================================
// 描画終了
//=============================================================================
void RenderWindow::PostDraw() {
	// ===== トランジションバリアを戻す（RenderTargetからPresentへ）=====
	DirectXCommon::TransitionBarrier(
	    swapChainResources_[currentBackBufferIndex_].Get(),
	    D3D12_RESOURCE_STATE_RENDER_TARGET, 
		D3D12_RESOURCE_STATE_PRESENT);
}

//=============================================================================
// クリアせずにRTV/DSVを張り直す（ポストエフェクトの後に使う）
//=============================================================================
void RenderWindow::BindRenderTarget() {
	UINT index = swapChain_->GetCurrentBackBufferIndex();
	auto* cmdList = DirectXCommon::GetCommandList();
	cmdList->OMSetRenderTargets(1, &rtvHandles_[index], false, &dsvHandle_);
}


//=============================================================================
// 四角の外だけを帯の色でクリア
//=============================================================================
void RenderWindow::ClearBars(const ViewRect& inner, float windowWidth, float windowHeight) {
	const LONG outerRight = static_cast<LONG>(windowWidth);
	const LONG outerBottom = static_cast<LONG>(windowHeight);
	const LONG left = static_cast<LONG>(inner.x);
	const LONG top = static_cast<LONG>(inner.y);
	const LONG right = left + static_cast<LONG>(inner.width);
	const LONG bottom = top + static_cast<LONG>(inner.height);

	// --- 上・下・左・右の順に、四角の外を4枚に分ける（CalcFitRect の四角なら、大きさがあるのは2枚まで）---
	const std::array<D3D12_RECT, 4> candidates = {
	    {
         {0, 0, outerRight, top},
         {0, bottom, outerRight, outerBottom},
         {0, top, left, bottom},
         {right, top, outerRight, bottom},
	    }
    };

	// --- 大きさのある物だけ集める ---
	std::array<D3D12_RECT, 4> bars{};
	UINT barCount = 0;
	for (const D3D12_RECT& candidate : candidates) {
		if (candidate.left < candidate.right && candidate.top < candidate.bottom) {
			bars[barCount] = candidate;
			++barCount;
		}
	}

	// 数を 0 のまま ClearRenderTargetView を呼ぶと、ウィンドウ全部がクリアされてしまう
	if (barCount == 0) {
		return;
	}
	DirectXCommon::GetCommandList()->ClearRenderTargetView(rtvHandles_[currentBackBufferIndex_], kBarColor.data(), barCount, bars.data());
}


//=============================================================================
// ウィンドウサイズ調整
//=============================================================================
void RenderWindow::Resize(int width, int height, bool needWait) {
	if (width == 0 || height == 0) {
		return;
	}

	// 描画中の可能性があるためGPU完了を待つ
	if (needWait) {
		DirectXCommon::WaitForGPU();
	}

	// スワップチェーンリソース解放
	swapChainResources_[0].Reset();
	swapChainResources_[1].Reset();

	// バッファリサイズ
	HRESULT hr = swapChain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "ウィンドウサイズの変更でBufferSizeを変更できませんでした。");
	}

	// バッファ再取得
	swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapChainResources_[0]));
	swapChain_->GetBuffer(1, IID_PPV_ARGS(&swapChainResources_[1]));
	swapChainResources_[0]->SetName(L"SwapChainBackBuffer0");
	swapChainResources_[1]->SetName(L"SwapChainBackBuffer1");

	// RTV再生成
	std::wstring title = window_->GetTitle();
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvHandles_[0] = DirectXCommon::GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 0);
	DirectXCommon::GetDevice()->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc, rtvHandles_[0]);
	rtvHandles_[1] = DirectXCommon::GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	DirectXCommon::GetDevice()->CreateRenderTargetView(swapChainResources_[1].Get(), &rtvDesc, rtvHandles_[1]);
	rtvDescriptorHeap_->SetName((title + L"_RTVHeap").c_str());

	// DSV再生成
	depthStencilResource_ = GpuResources::CreateDepthStencilTextureResource(width, height);
	depthStencilResource_->SetName(L"RenderWindow_DepthStencil");
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvHandle_ = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	DirectXCommon::GetDevice()->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvHandle_);
	dsvDescriptorHeap_->SetName((title + L"_DSVHeap").c_str());

	// ウィンドウサイズバッファ更新
	if (windowSizeMappedPtr_) {
		windowSizeMappedPtr_[0] = static_cast<float>(width);
		windowSizeMappedPtr_[1] = static_cast<float>(height);
	}
}