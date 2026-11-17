#pragma once
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>


/// <summary>
/// 影の深度の絵（シャドウマップ）。ライトから見た一番手前の物までの深度を書く
/// <para>使い方: BeginRender（読む状態 → 書く状態・消す）→ 深度だけのPSOで描く → EndRender（書く状態 → CSが読む状態）→ CSがgTexturesで読む</para>
/// <para>色は持たない（RTVなし）。深度は32bitの小数（ステンシルは使わない）</para>
/// </summary>
class ShadowMap {
public:
	// 深度を書く形
	static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

	/// <summary>
	/// 1枚つくる
	/// </summary>
	/// <param name="size">1辺のピクセル数（正方形）</param>
	explicit ShadowMap(uint32_t size);
	// 読む窓口のスロットを返す
	~ShadowMap();

	// コピー禁止
	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	/// <summary>
	/// 書き始める: 読む状態 → 書く状態にし、深度だけを描画先に結んで一番奥（1）で消す。ビューポートも絵全体にする
	/// </summary>
	void BeginRender();

	/// <summary>
	/// 書き終わる: 書く状態 → CSが読む状態
	/// </summary>
	void EndRender();

	// ===== ゲッター =====
	uint32_t GetSize() const { return size_; }
	uint32_t GetSRVSlot() const { return srvSlot_; } // 読む窓口の番号（HLSLのgTextures[番号]で読む。中身はRの0〜1の深度）

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;      // 中身（型なしで作り、書くときは D32_FLOAT、読むときは R32_FLOAT として見る）
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_; // 書く窓口（DSV）の置き場
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};              // 書く窓口
	uint32_t srvSlot_ = 0;                                 // 読む窓口のSRVヒープの番号
	uint32_t size_ = 0;                                    // 1辺のピクセル数
};