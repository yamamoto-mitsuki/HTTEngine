#pragma once
#include <array>
#include <vector>

#include "HTTEngine/Graphics/GPU/DirectXCommon.h"

// 前方宣言
class Win32Window;
struct ViewRect;


/// <summary> 
/// Windowに描画する準備（バリア変更、リセット）を行うクラス
/// </summary>
class RenderWindow {
public:
	// Windowの画面クリアの色（RGBAの順）
	static constexpr std::array<float, 4> kClearColor = {0.1f, 0.25f, 0.5f, 1.0f};
	// ゲームの比率と違うウィンドウに出る帯の色（RGBAの順）
	static constexpr std::array<float, 4> kBarColor = {0.0f, 0.0f, 0.0f, 1.0f};

public:
	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="window">描画したいWindow</param>
	void Initialize(Win32Window* window);

	/// <summary>
	/// 描画開始処理（バリア設定・RTVとDSVのクリア）
	/// </summary>
	void PreDraw();

	/// <summary>
	/// 描画後転送処理（バリアを戻す）
	/// </summary>
	void PostDraw();

	/// <summary>
	/// クリアせずにRTV/DSVを張り直す（ポストエフェクトの後に使う）
	/// </summary>
	void BindRenderTarget();

	/// <summary>
	/// ウィンドウのうち、inner の四角の外だけを帯の色でクリアする
	/// </summary>
	/// <param name="inner">ゲームの絵を描く四角（ピクセル）</param>
	/// <param name="windowWidth">ウィンドウの幅（ピクセル）</param>
	/// <param name="windowHeight">ウィンドウの高さ（ピクセル）</param>
	void ClearBars(const ViewRect& inner, float windowWidth, float windowHeight);

	/// <summary>
	/// ウィンドウサイズ変更時の描画位置調整
	/// </summary>
	void Resize(int width, int height, bool needWait = true);

	// ゲッター
	IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVHandle() const { return rtvHandles_[currentBackBufferIndex_]; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return dsvHandle_; }
	ID3D12Resource* GetWindowSizeBuffer() const { return windowSizeBuffer_.Get(); }

private:
	// 包含
	Win32Window* window_ = nullptr;
	
	// スワップチェーン
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_ = nullptr;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, DirectXCommon::kSwapChainBufferCount> swapChainResources_;
	// RTV
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_ = nullptr;
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, DirectXCommon::kSwapChainBufferCount> rtvHandles_{};
	// DSV
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_ = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_ = {};
	// ウィンドウサイズ定数バッファ（2DシェーダーのNDC変換用）
	Microsoft::WRL::ComPtr<ID3D12Resource> windowSizeBuffer_ = nullptr;
	float* windowSizeMappedPtr_ = nullptr;

	UINT currentBackBufferIndex_ = 0;
};