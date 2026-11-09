#include "ComputeTexture.h"

#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"

//=============================================================================
// 作成・破棄
//=============================================================================
// ===== 作成 =====
ComputeTexture::ComputeTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, const wchar_t* name) : width_(width), height_(height) {
	// --- 中身（ALLOW_UNORDERED_ACCESS が無いと UAV にできない）---
	// 初めは読む状態（ALL_SHADER_RESOURCE ＝ CS からも PS からも読める状態）
	resource_ = GpuResources::CreateTextureResource(width, height, format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	resource_->SetName(name);

	ID3D12Device* device = DirectXCommon::GetDevice();
	ID3D12DescriptorHeap* heap = DirectXCommon::GetSRVDescriptorHeap();

	// --- 読む窓口（SRV）---
	srvSlot_ = DirectXCommon::AllocateSRVSlot();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(resource_.Get(), &srvDesc, DirectXCommon::GetCPUDescriptorHandle(heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_));

	// --- 書く窓口（UAV）---
	// SRV と同じヒープに置く（描くとき・CS を動かすときに SetDescriptorHeaps で見せるのは、このヒープ1つだけ）
	uavSlot_ = DirectXCommon::AllocateSRVSlot();
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	device->CreateUnorderedAccessView(resource_.Get(), nullptr, &uavDesc, DirectXCommon::GetCPUDescriptorHandle(heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, uavSlot_));
	uavHandleGPU_ = DirectXCommon::GetGPUDescriptorHandle(heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, uavSlot_);
}

// ===== 破棄 =====
// スロットを返す（返さないと、作り直すたびにヒープの空きが減っていく）
ComputeTexture::~ComputeTexture() {
	DirectXCommon::FreeSRVSlot(srvSlot_);
	DirectXCommon::FreeSRVSlot(uavSlot_);
}

//=============================================================================
// 状態の切り替え
//=============================================================================
// ===== 読む → 書く =====
void ComputeTexture::BeginWrite() { DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); }

// ===== 書く → 読む =====
void ComputeTexture::EndWrite() { DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE); }