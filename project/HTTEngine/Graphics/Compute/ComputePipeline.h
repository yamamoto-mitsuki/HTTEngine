#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <unordered_map>

#include <d3d12.h>
#include <wrl.h>

#include "HTTEngine/Graphics/Pipeline/RootSignatureManager.h"


/// <summary>
/// CS 1本動かすのに要る物をまとめたもの（RootSignature・PSO・numthreads・変数名 → ルートパラメータ）
/// <para>作るのはPSOManager::GetComputePipeline。同じCSは1つだけ作って使い回す</para>
/// <para>使い方: Bind → SetConstantBuffer・SetShaderResource・SetUnorderedAccess	→ Dispatch</para>
/// </summary>
class ComputePipeline {
public:

	/// <summary>
	/// CSの名前から作る
	/// </summary>
	/// <param name="csName">.shaderのpathから作った名前（"Compute/Skinning.CS.hlsl" なら "SkinningCS"）</param>
	explicit ComputePipeline(const std::string& csName);

	// コピー禁止（PSOManagerの中に1つだけ置く）
	ComputePipeline(const ComputePipeline&) = delete;
	ComputePipeline& operator=(const ComputePipeline&) = delete;

	/// <summary>
	/// このCSを使うと決める（RootSignatureとPSOを積む）。Set～とDispatchより先に呼ぶ
	/// </summary>
	void Bind(ID3D12GraphicsCommandList* cmdList) const;

	/// <summary>
	/// ConstantBuffer（bレジスタ）に、定数バッファのGPUアドレスを結ぶ
	/// <para>アドレスは256バイトの倍数にする</para>
	/// </summary>
	/// <param name="name">HLSLの変数名（"gTestInformation" など）</param>
	void SetConstantBuffer(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address) const;

	/// <summary>
	/// StructuredBuffer（tレジスタ・読むだけ）に、バッファのGPUアドレスを結ぶ
	/// </summary>
	/// <param name="name">HLSLの変数名（"gInput" など）</param>
	void SetShaderResource(ID3D12GraphicsCommandList* cmdLsit, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address) const;

	/// <summary>
	/// RWStructuredBuffer（uレジスタ・読み書き）に、バッファのGPUアドレスを結ぶ
	/// <para>バッファは、ALLOW_UNORDERED_ACCESS 付きで作り、UNORDERED_ACCESS の状態にしておく</para>
	/// </summary>
	/// <param name="name">HLSLの変数名（"gOutput" など）</param>
	void SetUnorderedAccess(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_VIRTUAL_ADDRESS address) const;

	/// <summary>
	/// Texture2D の配列（tレジスタ・読むだけ。gTextures[] のバインドレス）に、SRVヒープの先頭を結ぶ
	/// <para>どのテクスチャを読むかは、定数バッファに入れた番号（GetSRVSlot）で、CS の中で選ぶ。先に SetDescriptorHeaps でヒープを見せておく</para>
	/// </summary>
	/// <param name="name">HLSLの変数名（"gTextures"）</param>
	/// <param name="heapStart">SRVヒープの先頭（GetSRVDescriptorHeap()->GetGPUDescriptorHandleForHeapStart()）</param>
	void SetTextureTable(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE heapStart) const;

	/// <summary>
	/// RWTexture2D（uレジスタ・読み書き）に、テクスチャの書く窓口（UAV）を結ぶ
	/// <para>テクスチャは ALLOW_UNORDERED_ACCESS 付きで作り、UNORDERED_ACCESS の状態にしておく（ComputeTexture::BeginWrite）</para>
	/// </summary>
	/// <param name="name">HLSLの変数名（"gOutput" など）</param>
	/// <param name="uavHandle">書く窓口のGPUの場所（ComputeTexture::GetUAVGPUHandle）</param>
	void SetUnorderedAccessTexture(ID3D12GraphicsCommandList* cmdList, const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE uavHandle) const;

	/// <summary>
	/// 動かしたいスレッド数から、グループの数を計算してDispatchする
	/// <para>例：[numthreads(64, 1, 1)] で 1000 個 → 16 グループ（1024 スレッド。はみ出た 24 はCSの中で何もせず帰らせる）</para>
	/// </summary>
	void Dispatch(ID3D12GraphicsCommandList* cmdList, uint32_t threadCountX, uint32_t threadCountY = 1, uint32_t threadCountZ = 1) const;

private:
	/// <summary>
	/// HLSLの変数名からルートパラメータ番号を探す
	/// <para>名前の打ち間違い・結ぶ関数の間違い（SRVの変数に SetUnorderedAccess など）は、ここで止める</para>
	/// </summary>
	/// <param name="expectedType">その Set～ 関数で結べる種類</param>
	UINT FindSlot(const std::string& name, D3D_SHADER_INPUT_TYPE expectedType) const;

	std::string name_;                                               // CSの名前（エラーの表示用）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;      // CS用のRootSignature
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;      // CS用のPSO
	std::array<UINT, 3> threadGroupSize_{};                          // [numthreads(x, y, z)]
	std::unordered_map<std::string, RootParameterInfo> parameterOf_; // HLSLの変数名 → ルートパラメータ
};