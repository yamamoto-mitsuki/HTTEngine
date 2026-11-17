#include "ShadowMap.h"

#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"

//=============================================================================
// 作成・破棄
//=============================================================================
// ===== 作成 =====
ShadowMap::ShadowMap(uint32_t size) : size_(size) { 
	ID3D12Device* device = DirectXCommon::GetDevice();
	// --- 中身 ---
	// 型なし（R32_TYPELESS）で作ると、窓口ごとに決められる（書くDSVは、D32_FLOAT。読むSRVは、R32_FLOAT）
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = kDepthFormat;
	clearValue.DepthStencil.Depth = 1.0f; // 一番奥で消す
	resource_ = GpuResources::CreateTextureResource(size, size, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clearValue);
	resource_->SetName(L"ShadowMap");
	// --- 書く窓口（DSV）---
	dsvHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	dsvHeap_->SetName(L"ShadowMap_DSVHeap");
	dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = kDepthFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(resource_.Get(), &dsvDesc, dsvHandle_);
	// --- 読む窓口（SRV）---
	srvSlot_ = DirectXCommon::AllocateSRVSlot();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(resource_.Get(), &srvDesc, DirectXCommon::GetCPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_));
}

// ===== 破棄 =====
ShadowMap::~ShadowMap() { DirectXCommon::FreeSRVSlot(srvSlot_); }


//=============================================================================
// 描く
//=============================================================================
// ===== 描き始める =====
void ShadowMap::BeginRender() {
	auto* cmdList = DirectXCommon::GetCommandList();
	// 読む状態 → 書く状態
	DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	// 深度だけ結ぶ（色は無い。PSO・RTVもなし）
	cmdList->OMSetRenderTargets(0, nullptr, false, &dsvHandle_);
	cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	// ビューポート・シザーは絵全体
	const float size = static_cast<float>(size_);
	const D3D12_VIEWPORT viewport{0.0f, 0.0f, size, size, 0.0f, 1.0f};
	const D3D12_RECT scissor{0, 0, static_cast<LONG>(size_), static_cast<LONG>(size_)};
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissor);
}

// ===== 描き終わる =====
// CS（ShadowMask.CS）が読むので、PS以外が読む状態にしておく
void ShadowMap::EndRender() { DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); }