#pragma once
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

/// <summary>
/// GPU のリソース（バッファ・テクスチャ・ディスクリプタヒープ）を作る関数
/// <para>デバイスしか使わず、状態を持たない。DirectXCommon::Initialize の後に呼ぶ</para>
/// </summary>
namespace GpuResources {

/// <summary>
/// Uploadヒープ（CPU → GPU）にバッファを生成する
/// <para>頂点バッファ・定数バッファ・インデックスバッファ等に使う</para>
/// </summary>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(size_t sizeInBytes);

/// <summary>
/// Defaultヒープ（GPU専用・高速）にバッファを生成する
/// <para>CPUから直接書けないので、UploadToDefaultBuffer で転送して使う。</para>
/// <para>一度書いたら変わらない頂点バッファ等に使う。</para>
/// </summary>
/// <param name="flags">CSから書き込む（RWStructuredBuffer）なら D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS</param>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(size_t sizeInBytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

/// <summary>
/// Readbackヒープ（GPU → CPU）にバッファを生成する
/// </summary>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateReadbackBuffer(size_t sizeInBytes);

/// <summary>
/// Uploadバッファを生成し、そのまま永続Mapして先頭ポインタを返す
/// </summary>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateMappedUploadBuffer(size_t sizeInBytes, void** outMappedPtr);

/// <summary>
/// 2DTextureResource（Defaultヒープ）を生成する
/// <para>RenderTarget・DepthStencil・通常テクスチャなど、2DテクスチャのCreateCommittedResourceを共通化したもの</para>
/// </summary>
/// <param name="width">幅（ピクセル）</param>
/// <param name="height">高さ（ピクセル）</param>
/// <param name="format">ピクセルフォーマット</param>
/// <param name="flags">用途フラグ（RTVなら ALLOW_RENDER_TARGET、DSVなら ALLOW_DEPTH_STENCIL、通常テクスチャなら NONE）</param>
/// <param name="initialState">生成時のResourceState</param>
/// <param name="clearValue">最適化クリア値。RenderTarget/DepthStencilは渡す、通常テクスチャはnullptr</param>
/// <param name="arraySize">配列枚数（キューブマップ・テクスチャ配列用。通常1）</param>
/// <param name="mipLevels">ミップマップ段数（通常1）</param>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, 
	D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE*clearValue = nullptr, 
	uint16_t arraySize = 1, uint16_t mipLevels = 1);

/// <summary>
/// 深度StencilTextureResourceを生成する
/// </summary>
/// <param name="isShaderReadable">true：シェーダーからも読めるように、型なし（R24G8_TYPELESS）で作る</param>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height, bool isShaderReadable = false);

/// <summary>
/// RenderTargetTextureResourceを生成する
/// </summary>
/// <param name="width">幅（ピクセル）</param>
/// <param name="height">高さ（ピクセル）</param>
/// <param name="format">ピクセルフォーマット（通常は_SRGB系）</param>
/// <param name="clearColor">クリア色。RGBAの順で4要素</param>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTargetTextureResource(uint32_t width, uint32_t height,
	DXGI_FORMAT format, const float clearColor[4]);

/// <summary>
/// DescriptorHeapを生成する
/// </summary>
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

} // namespace GpuResources
