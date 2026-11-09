#include "HTTEngine/Graphics/GPU/GpuResources.h"

#include <format>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"

namespace {
// バッファを作る（Upload・Default・Readback で共通）。大きさはそのまま使う（256にそろえない）
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(D3D12_HEAP_TYPE heapType, size_t sizeInBytes, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState) {
	// --- どのヒープに置くか ---
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = heapType;
	// --- バッファの形（1列に並んだバイト）---
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = flags; // UAVとして使う（CSから書く）なら ALLOW_UNORDERED_ACCESS が要る
	// --- 作る ---
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = DirectXCommon::GetDevice()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&resource));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, std::format("バッファの生成に失敗しました（ヒープの種類 = {}, {} バイト）", static_cast<int>(heapType), sizeInBytes));
	}
	return resource;
}
} // namespace


//=============================================================================
// バッファ
//=============================================================================
// ===== Uploadヒープ（CPU → GPU）=====
// 定数バッファにも使うので、大きさを256バイトの倍数にそろえる
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateUploadBuffer(size_t sizeInBytes) {
	const size_t alignedSize = (sizeInBytes + 255) & ~size_t(255);
	return CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, alignedSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

// ===== Defaultヒープ（GPU専用・高速）=====
// バッファは作るときの状態の指定が無視されて COMMON から始まるので、COMMON を渡す
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateDefaultBuffer(size_t sizeInBytes, D3D12_RESOURCE_FLAGS flags) {
	const size_t alignedSize = (sizeInBytes + 255) & ~size_t(255);
	return CreateBuffer(D3D12_HEAP_TYPE_DEFAULT, alignedSize, flags, D3D12_RESOURCE_STATE_COMMON);
}

// ===== Readbackヒープ（GPU → CPU）=====
// 読み戻しは定数バッファにしないので、そろえない。Readbackヒープはいつも COPY_DEST（コピーされる先）
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateReadbackBuffer(size_t sizeInBytes) {
	return CreateBuffer(D3D12_HEAP_TYPE_READBACK, sizeInBytes, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
}

// ===== Uploadバッファを生成し、そのまま永続Mapして先頭ポインタを返す =====
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateMappedUploadBuffer(size_t sizeInBytes, void** outMappedPtr) {
	auto resource = CreateUploadBuffer(sizeInBytes);
	resource->Map(0, nullptr, outMappedPtr);
	return resource;
}


//=============================================================================
// テクスチャ
//=============================================================================
// ===== 2DTextureResourceの生成 =====
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateTextureResource(
    uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, uint16_t arraySize, uint16_t mipLevels) {
	// 設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2DTextureとして扱う
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.DepthOrArraySize = arraySize;
	resourceDesc.MipLevels = mipLevels;
	resourceDesc.Format = format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Flags = flags;
	// ヒープ
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	// 作成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = DirectXCommon::GetDevice()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState, clearValue, IID_PPV_ARGS(&resource));
	// エラーチェック
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "TextureResourceの生成に失敗しました");
	}

	return resource;
}

// ===== RenderTargetTextureResourceの生成 =====
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateRenderTargetTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, const float clearColor[4]) {
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;
	clearValue.Color[0] = clearColor[0];
	clearValue.Color[1] = clearColor[1];
	clearValue.Color[2] = clearColor[2];
	clearValue.Color[3] = clearColor[3];

	return CreateTextureResource(width, height, format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue);
}

// ===== DepthStencilTextureResourceの生成 =====
Microsoft::WRL::ComPtr<ID3D12Resource> GpuResources::CreateDepthStencilTextureResource(int32_t width, int32_t height, bool isShaderReadable) {
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // クリアは深度として書く形（DSV と同じ形）

	// D24_UNORM_S8_UINT で作ると、深度としてしか使えない（読む窓口 SRV を作れない）
	// 型なし（TYPELESS）で作ると、窓口ごとに形を決められる（DSV は D24_UNORM_S8_UINT、SRV は R24_UNORM_X8_TYPELESS）
	const DXGI_FORMAT format = isShaderReadable ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT;
	return CreateTextureResource(static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue);
}


//=============================================================================
// ディスクリプタヒープ
//=============================================================================
// ===== DescriptorHeapの生成 =====
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {

	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = heapType;
	desc.NumDescriptors = numDescriptors;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	HRESULT hr = DirectXCommon::GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "DescriptorHeapの生成に失敗しました");
	}
	return descriptorHeap;
}
