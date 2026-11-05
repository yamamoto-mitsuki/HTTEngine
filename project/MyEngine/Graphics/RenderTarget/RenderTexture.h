#pragma once
#include <cstdint>
#include <vector>

#include "MyEngine/Graphics/GPU/DirectXCommon.h"

// IBL使用時にHDR画像として使いたいのでFormatを区別する
enum class RenderTextureFormat {
	SDR,   // R8G8B8A8_UNORM_SRGB：画面に出す絵（0〜1 で頭打ち。書くときに GPU が sRGB へ直す）
	HDR,   // R16G16B16A16_FLOAT：3D の絵・ブルームの段・IBL（1 を超える明るさを残す）
	RG16F, // R16G16_FLOAT：IBL の BRDF の表
};

/// <summary>
/// レンダリング結果を画像に焼き付けるクラス
/// </summary>
class RenderTexture {
public:
	// 1枚作る（isDepthReadable：深度をシェーダーから読めるようにする。被写界深度が奥行きを知る・デカールが塗る所を知るのに使う。useDepth が true のときだけ）
	RenderTexture(uint32_t width, uint32_t height, RenderTextureFormat format, bool useDepth = true, bool isDepthReadable = false);
	// SRVスロットの解放
	~RenderTexture();

	// コピー・ムーブ禁止
	RenderTexture(const RenderTexture&) = delete;
	RenderTexture& operator=(const RenderTexture&) = delete;

	/// <summary>
	/// 描画開始
	/// <para>リソースをPIXEL_SHADER_RESOURCE→RENDER_TARGETに切り替えてRTV/DSVをクリアする</para>
	/// </summary>
	void PreDraw();

	/// <summary>
	/// 描画終了
	/// <para>リソースをRENDER_TARGET→PIXEL_SHADER_RESOURCEに戻す</para>
	/// </summary>
	void PostDraw();

	/// <summary>
	/// 描画先にし直す（クリアしない）。深度は depthSource の物を使う
	/// <para>被写界深度でぼかした絵に、元の絵の深度で前後を比べながら半透明を描くときに使う。PreDraw の後に呼ぶ</para>
	/// </summary>
	void BindWithDepthOf(RenderTexture& depthSource);

	/// <summary>
	/// 色と深度（isDepthReadable のとき）を、CS からも PS からも読める状態にする
	/// <para>PostDraw の後に呼ぶ。読み終わったら EndShaderRead（次の PreDraw より前）</para>
	/// </summary>
	void BeginShaderRead();

	/// <summary>
	/// BeginShaderRead の前の状態（色は PS だけが読める状態、深度は書く状態）に戻す
	/// </summary>
	void EndShaderRead();

	/// <summary>
	/// 深度を読みながら描く（デカール）。深度は読むだけの窓口（読むだけの DSV）で結び直し、同じ深度を SRV でも読める状態にする
	/// <para>PreDraw の後に呼ぶ（isDepthReadable で作ったときだけ）。描き終わったら EndDepthRead（深度を書く状態に戻す）</para>
	/// <para>読むだけの間は、深度もステンシルも書けない（書く PSO で描くとデバッグレイヤーに止められる）</para>
	/// </summary>
	void BeginDepthRead();

	/// <summary>
	/// BeginDepthRead の前の状態（深度を書く窓口で結んだ、書く状態）に戻す
	/// </summary>
	void EndDepthRead();

	/// <summary>
	/// 形式 → DXGI_FORMAT（PSO の描画先の形式を、描く先の RenderTexture とそろえるのに使う）
	/// </summary>
	static DXGI_FORMAT ToDXGIFormat(RenderTextureFormat format);

	// ===== ゲッター =====
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUHandle() { return srvHandleGPU_; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() { return rtvHandle_; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() { return dsvHandle_; }
	uint32_t GetWidth() { return width_; }
	uint32_t GetHeight() { return height_; }
	// ヒープ上のスロット番号
	uint32_t GetSRVSlot() const { return srvSlot_; }
	// 深度を読む窓口のスロット番号（isDepthReadable で作ったときだけ使える）
	uint32_t GetDepthSRVSlot() const { return depthSrvSlot_; }

private:
	// ===== 内部ヘルパー =====
	void CreateResource();
	void CreateDSVResource();
	// RTV, SRV, DSV
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_ = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_ = {};
	uint32_t srvSlot_ = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU_ = {};
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_ = {};
	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_ = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_ = {};
	D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsvHandle_ = {}; // 深度を読むだけの窓口（isDepthReadable_ のときだけ。BeginDepthRead で結ぶ）
	uint32_t depthSrvSlot_ = 0; // 深度を読む窓口（isDepthReadable_ のときだけ）
	// Format
	DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// サイズ
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	// Flag
	bool useDepth_;
	bool isDepthReadable_ = false; // 深度をシェーダーから読めるか
};