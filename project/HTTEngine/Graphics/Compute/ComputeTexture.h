#pragma once
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

/// <summary>
/// CS が書いて、CS・PS が読むテクスチャ（書く窓口 UAV と、読む窓口 SRV を両方持つ）
/// <para>使い方：BeginWrite（読む状態 → 書く状態）→ CS で書く → EndWrite（書く状態 → 読む状態）→ 別の CS・PS が gTextures[GetSRVSlot()] で読む</para>
/// </summary>
class ComputeTexture {
public:
	/// <summary>
	/// 1枚作る（初めは読む状態）
	/// </summary>
	/// <param name="format">中身の形（R16G16B16A16_FLOAT など。_SRGB の形は UAV にできないので使えない）</param>
	/// <param name="name">PIX・デバッグ表示用の名前</param>
	ComputeTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const wchar_t* name);
	// SRV・UAV のスロットを返す
	~ComputeTexture();

	// コピー禁止（スロットを2回返してしまう）
	ComputeTexture(const ComputeTexture&) = delete;
	ComputeTexture& operator=(const ComputeTexture&) = delete;

	// 読む状態 → CSが書ける状態（ComputePipeline::Dispatchの前に呼ぶ）
	void BeginWrite();
	// CS が書ける状態 → 読む状態（Dispatch の後に呼ぶ。書き終わるのを待ってから、次の CS・PS が読むようになる）
	void EndWrite();

	// ===== ゲッター =====
	uint32_t GetWidth() const { return width_; }
	uint32_t GetHeight() const { return height_; }
	// 読む窓口の番号（HLSL の gTextures[番号] で読む）
	uint32_t GetSRVSlot() const { return srvSlot_; }
	// 書く窓口（ComputePipeline::SetUnorderedAccessTexture に渡す）
	D3D12_GPU_DESCRIPTOR_HANDLE GetUAVGPUHandle() const { return uavHandleGPU_; }


private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_; // 中身
	uint32_t srvSlot_ = 0;                            // 読む窓口の、SRVヒープの番号
	uint32_t uavSlot_ = 0;                            // 書く窓口の、SRVヒープの番号（UAV も同じヒープに置く）
	D3D12_GPU_DESCRIPTOR_HANDLE uavHandleGPU_{};      // 書く窓口のGPUの場所
	uint32_t width_ = 0;
	uint32_t height_ = 0;
};