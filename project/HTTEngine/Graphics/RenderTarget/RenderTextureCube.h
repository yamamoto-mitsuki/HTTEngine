#pragma once
#include <cstdint>
#include <vector>
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h" 


/// <summary>
/// キューブマップ(6面)のRenderTexture。IBLのEnv/Irradiance/Prefilterの描画先に使う。
/// <para>書くときは面ごとのRTV、読むときはTextureCubeとして扱う。</para>
/// </summary>
class RenderTextureCube {
public:
	/// <param name="size">Cubeの1面の解像度</param>
	/// <param name="mipLevels">prefilter用に段階が要るなら2以上</param>
	RenderTextureCube(uint32_t size, RenderTextureFormat format, uint32_t mipLevels = 1);
	~RenderTextureCube();

	// コピー・ムーブ禁止
	RenderTextureCube(const RenderTextureCube&) = delete;
	RenderTextureCube& operator=(const RenderTextureCube&) = delete;

	// ImGui表示用
	D3D12_GPU_DESCRIPTOR_HANDLE GetFaceSRVGPUHandle(uint32_t face);

	// ===== ゲッター =====
	// Cubeのface(0..5), mip 指定でRTVを取得
	D3D12_CPU_DESCRIPTOR_HANDLE GetFaceRTV(uint32_t face, uint32_t mip = 0) const { return rtvHandles_[mip * 6 + face]; }
	// シェーダに渡すSRV
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUHandle() const { return srvHandleGPU_; }
	// バリア遷移用
	ID3D12Resource* GetResource() const { return resource_.Get(); }
	uint32_t GetSize() const { return size_; }
	uint32_t GetMipLevels() const { return mipLevels_; }
	// ヒープ上のスロット番号
	uint32_t GetSRVSlot() const { return srvSlot_; }

private:
	void CreateResource();
	void CreateFaceRTVs();
	static DXGI_FORMAT ToDXGIFormat(RenderTextureFormat format);

	Microsoft::WRL::ComPtr<ID3D12Resource> resource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_ = nullptr;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles_; // サイズ = 6 * mipLevels_

	uint32_t srvSlot_ = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU_ = {};
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU_ = {};

	DXGI_FORMAT format_ = DXGI_FORMAT_R16G16B16A16_FLOAT;
	uint32_t size_ = 0;
	uint32_t mipLevels_ = 1;


	// ImGui確認用
	void EnsureFaceSRVs();
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> faceSrvGPU_;
	std::vector<uint32_t> faceSrvSlots_; // 面ごとのSRVのスロット番号（消すときに返す）
};