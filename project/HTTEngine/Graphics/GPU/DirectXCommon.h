#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>

#include "HTTEngine/Graphics/GPU/DescriptorAllocator.h"


/// <summary>
/// DirectX12の基盤クラス
/// <para>PSO・RootSignatureの管理はPSOManagerが担当する</para>
/// </summary>
class DirectXCommon {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class DirectXCommon;
	};
	explicit DirectXCommon(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	static constexpr UINT kSwapChainBufferCount = 2; // スワップチェーンのバッファ数
	static constexpr UINT kSRVDescriptorHeap = 256;  // SRVDescriptorHeapのスロット数

	/// <summary>
	/// DX12基盤の初期化。Engine::Initialize()内で1度だけ呼ぶ。
	/// </summary>
	static void Initialize();

	/// <summary>
	/// DX12基盤の解放。Engine::Finalize()内で最後に呼ぶ。
	/// <para>TextureManager・RenderContext等の自身を使っているものの解放が全て終わった後に呼ぶこと。</para>
	/// </summary>
	static void Release();

	/// <summary>
	/// デバイスが削除されたかどうかをチェックする
	/// <para> 削除されていたら、GraphicsDebug::ReportDeviceRemoved で DRED の記録をログに出す</ para>
	/// </summary>
	static bool CheckDeviceRemoved();

	/// <summary>
	/// GPUの処理完了を待つ
	/// <para>コマンドキューに積んだ全コマンドの実行が完了するまでCPUをブロックする</para>
	/// </summary>
	static void WaitForGPU();

	/// <summary>
	/// 描画中のコマンドリストを閉じて実行し、終わるまで待ってから、また詰める状態に戻す
	/// <para>初期化中など、フレームの外で「GPUにやらせて結果を待つ」ときに使う（IBLの焼きこみ・CSの動作確認など）</para>
	/// </summary>
	static void ExecuteAndWait();

	/// <summary>
	/// SRVスロット番号を1つ発行する。
	/// <para>TextureManager・RenderTextureなどSRVを必要とする全クラスがこれを使う。</para>
	/// <para>スロット0はImGui予約済みのため1から開始する。</para>
	/// <para>中身は DescriptorAllocator（番号の配り係）に任せている。</para>
	/// </summary>
	/// <returns>確保したスロット番号</returns>
	static uint32_t AllocateSRVSlot();

	/// <summary>
	/// SRVスロット番号を返す。RenderTextureなど、スロットをもらった物を消すときに呼ぶ
	/// <para>このフレームのコマンドがまだそのスロットを読むかもしれないので、すぐには配り直さない。
	/// フレームの終わりにGPUを待った後（RecycleFreedSRVSlots）から、また配られる。</para>
	/// <para>0（まだもらっていない）を渡したときと、DirectXCommonが無いときは何もしない。</para>
	/// </summary>
	static void FreeSRVSlot(uint32_t slot);

	/// <summary>
	/// このフレームに返してもらったスロットを、次から配れるようにする
	/// <para>フレームの終わりに WaitForGPU した後に1回呼ぶ（GPUがもう読まないと確かなときだけ）</para>
	/// </summary>
	static void RecycleFreedSRVSlots();

	// 今使っているSRVスロットの数（Profilerの表示用。スロット0のImGui用は数えない）
	static uint32_t GetUsedSRVSlotCount();

	/// <summary>
	/// リソースのトランジションバリアを1つ発行する（描画のコマンドリストへ積む）
	/// </summary>
	/// <param name="resource">状態を変えるリソース</param>
	/// <param name="before">変更前の状態</param>
	/// <param name="after">変更後の状態</param>
	static void TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	/// <summary>
	/// リソースのトランジションバリアを1つ、渡したコマンドリストへ発行する
	/// <para>描画とは別のコマンドリストを持っている所（UploadContext など）から使う。バリアは、コピーと同じリストに積まないと順番が守られない</para>
	/// </summary>
	/// <param name="commandList">バリアを積むコマンドリスト</param>
	/// <param name="resource">状態を変えるリソース</param>
	/// <param name="before">変更前の状態</param>
	/// <param name="after">変更後の状態</param>
	static void TransitionBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	/// <summary>
	/// リソースの状態遷移バリアを複数まとめて発行する
	/// </summary>
	/// <param name="resources">
	/// {resource, before, after} のリスト。
	/// 例: { {gameRT, RT, SRV}, {bloomRT, SRV, RT} }
	/// </param>
	static void TransitionBarriers(std::initializer_list<std::tuple<ID3D12Resource*, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES>> resources);

	/// <summary>
	/// 指定インデックスのCPUDescriptorHandleを取得する
	/// </summary>
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t index);

	/// <summary>
	/// 指定インデックスのGPUDescriptorHandleを取得する
	/// </summary>
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t index);

	/// <summary>
	/// SRVのヒープをコマンドリストに見せる（SetDescriptorHeaps）
	/// </summary>
	/// <returns>ヒープの先頭のGPUハンドル。SetGraphicsRootDescriptorTable（Bindless のテクスチャ）にそのまま渡せる</returns>
	static D3D12_GPU_DESCRIPTOR_HANDLE BindSRVHeap(ID3D12GraphicsCommandList* cmdList);

	// DrawCallをリセット、増加
	static void ResetDrawCallCount() { instance_->drawCallCount_ = 0; }
	static void IncrementDrawCallCount() { instance_->drawCallCount_++; }


	//==========================================
	// ゲッター
	//==========================================
	static IDXGIFactory7* GetFactory();
	static IDXGIAdapter4* GetAdapter4();
	static ID3D12Device* GetDevice();
	static ID3D12CommandQueue* GetCommandQueue();
	static ID3D12CommandAllocator* GetCommandAllocator();
	static ID3D12GraphicsCommandList* GetCommandList();
	static ID3D12DescriptorHeap* GetSRVDescriptorHeap();
	static IDxcUtils* GetDxcUtils();
	static IDxcCompiler3* GetDxcCompiler();
	static IDxcIncludeHandler* GetIncludeHandler();
	static uint32_t GetDescriptorSizeSRV();
	static uint32_t GetDescriptorSizeRTV();
	static uint32_t GetDescriptorSizeDSV();
	static uint32_t GetDrawCallCount();

private:
	// Initialize()で作り、Release()で消す（unique_ptr）。
	// 解放順序をEngine::Finalize()で明示的に制御するためMeyersシングルトンを使わない。
	// シングルトン
	friend struct std::default_delete<DirectXCommon>;
	~DirectXCommon() = default;
	DirectXCommon(const DirectXCommon&) = delete;
	DirectXCommon& operator=(const DirectXCommon&) = delete;
	static std::unique_ptr<DirectXCommon> instance_;

	// ===== 内部ヘルパー =====
	// 初期化の段階（Initialize が順に呼ぶ）
	void CreateFactory(UINT dxgiFactoryFlags);
	void SelectAdapter();
	void CreateDevice();
	void CreateCommandObjects(); // コマンドキュー・アロケータ・リスト
	void CreateFence();          // フェンスと、待つためのイベント
	void CreateDxc();            // シェーダーのコンパイラ
	void CreateDescriptorObjects(); // ディスクリプタの大きさと SRV のヒープ
	// ディスクリプタ1個分の大きさ（ヒープの種類ごと）
	static uint32_t DescriptorSizeOf(D3D12_DESCRIPTOR_HEAP_TYPE heapType);
	
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_ = nullptr;
	Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	// DrawCallカウンター
	uint32_t drawCallCount_ = 0;
	// シェーダーから見えるヒープと、その番号の配り係（0番はImGui用に取っておく）
	std::unique_ptr<DescriptorAllocator> srvAllocator_;
	// DescriptorSizeのキャッシュ
	uint32_t descriptorSizeSRV_ = 0;
	uint32_t descriptorSizeRTV_ = 0;
	uint32_t descriptorSizeDSV_ = 0;
};