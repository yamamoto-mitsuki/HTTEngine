#include "HTTEngine/Graphics/RenderTarget/RenderTextureCube.h"

#include <format>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"

using namespace Microsoft::WRL;

//=============================================================================
// enum format → DXGI_FORMAT変換
//=============================================================================
DXGI_FORMAT RenderTextureCube::ToDXGIFormat(RenderTextureFormat format) {
	switch (format) {
	case RenderTextureFormat::SDR: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // SDR
	case RenderTextureFormat::HDR: return DXGI_FORMAT_R16G16B16A16_FLOAT;  // HDR
	}
	HTT_ASSERT_MSG(false, "未対応のRenderTextureFormatです");
	return DXGI_FORMAT_R16G16B16A16_FLOAT;
}


//=============================================================================
// コンストラクタ・デストラクタ
//=============================================================================
// ===== コンストラクタ =====
RenderTextureCube::RenderTextureCube(uint32_t size, RenderTextureFormat format, uint32_t mipLevels) {
	size_ = size;
	format_ = ToDXGIFormat(format);
	mipLevels_ = mipLevels;

	CreateResource();
	CreateFaceRTVs();

	LogManager::Log(std::format("RenderTextureCube created {}x{} mip={} SRVSlot={}", size, size, mipLevels_, srvSlot_));
}

// ===== デストラクタ =====
// SRVスロットを返す。ImGuiで面を見せるために作ったスロット（6つ）も返す
RenderTextureCube::~RenderTextureCube() {
	DirectXCommon::FreeSRVSlot(srvSlot_);
	for (uint32_t slot : faceSrvSlots_) {
		DirectXCommon::FreeSRVSlot(slot);
	}
}


//=============================================================================
// リソース＋キューブSRVを作る
//=============================================================================
void RenderTextureCube::CreateResource() {
	// ===== Texture =====
	// 設定
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format_;
	clearValue.Color[0] = RenderWindow::kClearColor[0];
	clearValue.Color[1] = RenderWindow::kClearColor[1];
	clearValue.Color[2] = RenderWindow::kClearColor[2];
	clearValue.Color[3] = RenderWindow::kClearColor[3];
	// 作成
	resource_ = GpuResources::CreateTextureResource(
	    size_, size_,                               // 大きさ
	    format_,                                    // SDR or HDR
	    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,    // RTVとして使用
	    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // 生成時の Resource State
	    &clearValue,                                // クリアカラー
	    6,                                          // Cubeの面の数
	    static_cast<uint16_t>(mipLevels_)           // mipLevel
	);
	resource_->SetName(L"RenderTextureCube");

	// ===== SRV =====
	// Slot, Handle
	srvSlot_ = DirectXCommon::AllocateSRVSlot();
	srvHandleCPU_ = DirectXCommon::GetCPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_);
	srvHandleGPU_ = DirectXCommon::GetGPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_);
	// 設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format_;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; // Cubeとして読む
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.TextureCube.MipLevels = mipLevels_;
	// 作成
	DirectXCommon::GetDevice()->CreateShaderResourceView(resource_.Get(), &srvDesc, srvHandleCPU_);
}

//=============================================================================
// 面ごと(×mip)のRTVを作る
//=============================================================================
void RenderTextureCube::CreateFaceRTVs() { 
	rtvHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6 * mipLevels_, false); 
	rtvHeap_->SetName(L"RenderTextureCube_RTVHeap");
	// 中間Resource
	UINT rtvSize = DirectXCommon::GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE start = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
	// 作成
	rtvHandles_.resize(6 * mipLevels_);
	for (uint32_t mip = 0; mip < mipLevels_; ++mip) {
		for (uint32_t face = 0; face < 6; ++face) {
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = format_;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; // 配列の1枚とする
			rtvDesc.Texture2DArray.MipSlice = mip;
			rtvDesc.Texture2DArray.FirstArraySlice = face;
			rtvDesc.Texture2DArray.ArraySize = 1;
			uint32_t i = mip * 6 + face;
			D3D12_CPU_DESCRIPTOR_HANDLE handle = start;
			handle.ptr += static_cast<SIZE_T>(i) * rtvSize;
			DirectXCommon::GetDevice()->CreateRenderTargetView(resource_.Get(), &rtvDesc, handle);
			rtvHandles_[i] = handle;
		}
	}
}


//=============================================================================
// Cubeの6面表示出来ているか確認用
//=============================================================================
D3D12_GPU_DESCRIPTOR_HANDLE RenderTextureCube::GetFaceSRVGPUHandle(uint32_t face) {
	if (faceSrvGPU_.empty()) {
		EnsureFaceSRVs();
	}
	return faceSrvGPU_[face];
}

void RenderTextureCube::EnsureFaceSRVs() {
	faceSrvGPU_.resize(6);
	faceSrvSlots_.resize(6);
	for (uint32_t face = 0; face < 6; ++face) {
		uint32_t slot = DirectXCommon::AllocateSRVSlot();
		faceSrvSlots_[face] = slot; // 消すときに返すので覚えておく
		auto cpu = DirectXCommon::GetCPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, slot);
		faceSrvGPU_[face] = DirectXCommon::GetGPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, slot);

		D3D12_SHADER_RESOURCE_VIEW_DESC d{};
		d.Format = format_;
		d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; // ← 1面を2Dとして
		d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		d.Texture2DArray.MostDetailedMip = 0;
		d.Texture2DArray.MipLevels = 1;
		d.Texture2DArray.FirstArraySlice = face; // この面だけ
		d.Texture2DArray.ArraySize = 1;
		DirectXCommon::GetDevice()->CreateShaderResourceView(resource_.Get(), &d, cpu);
	}
}