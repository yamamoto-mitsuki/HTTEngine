#include "HTTEngine/Graphics/GPU/DirectXCommon.h"

#include <array>
#include <cassert>
#include <format>
#include <string>
#include <string_view>

#include <externals/DirectXTex/d3dx12.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/Diagnostics/GraphicsDebug.h"
#include "HTTEngine/String/ConvertString.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

// staticメンバの定義
std::unique_ptr<DirectXCommon> DirectXCommon::instance_ = nullptr;

namespace {
// D3D12 の機能レベルと、ログに出す名前
struct FeatureLevel {
	D3D_FEATURE_LEVEL level;
	std::string_view name;
};
// 高い順に試す
constexpr auto kFeatureLevels = std::to_array<FeatureLevel>({
    {D3D_FEATURE_LEVEL_12_2, "12.2"},
    {D3D_FEATURE_LEVEL_12_1, "12.1"},
    {D3D_FEATURE_LEVEL_12_0, "12.0"},
});

// 初期化の中の生成の確認。失敗なら Error Code をログに出して止まる
void CheckCreated(HRESULT hr, const std::string& message) {
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", static_cast<uint32_t>(hr)));
		HTT_ASSERT_MSG(false, message);
	}
}
} // namespace


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
// PSO/RootSignatureはPSOManager::Initialize()で行うため、ここでは生成しない
void DirectXCommon::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()を2回以上呼んでいます");
	instance_ = std::make_unique<DirectXCommon>(ConstructorKey());

	// デバッグレイヤー・DRED（Debugだけ。デバイスより先）
	// Graphics Toolsがある場合はDXGIのデバッグを有効化するため、フラグが返ってくる
	const UINT dxgiFactoryFlags = GraphicsDebug::EnableDebugLayer();

	instance_->CreateFactory(dxgiFactoryFlags);
	instance_->SelectAdapter();
	instance_->CreateDevice();
	// エラー・警告で止まる・ログに出す（Debugだけ。デバイスの後）
	GraphicsDebug::SetupInfoQueue(instance_->device_.Get());
	instance_->CreateCommandObjects();
	instance_->CreateFence();
	instance_->CreateDxc();
	instance_->CreateDescriptorObjects();

	LogManager::Log("Initialized");
}

// ===== 解放 =====
// Engine::Finalize()内でTextureManager等の解放が全て終わった後に呼ぶこと
void DirectXCommon::Release() {
	HTT_ASSERT_MSG(instance_ != nullptr, "DirectXCommon::Release()がInitialize()より先に呼ばれています");
	// フェンス
	CloseHandle(instance_->fenceEvent_);
	instance_->fenceEvent_ = nullptr;
	// インスタンス
	instance_.reset();
}


//=============================================================================
// 初期化の段階
//=============================================================================
// ===== DXGIファクトリーの生成 =====
void DirectXCommon::CreateFactory(UINT dxgiFactoryFlags) {
	const HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory_));
	CheckCreated(hr, "DXGIファクトリーを生成できませんでした");
}

// ===== アダプタの選択 =====
void DirectXCommon::SelectAdapter() {
	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {
		DXGI_ADAPTER_DESC3 adapterDesc{};
		const HRESULT hr = adapter_->GetDesc3(&adapterDesc);
		CheckCreated(hr, "アダプターの情報を取得できませんでした");
		// ソフトウェアアダプタ（GPUではなく、CPUで描画するエミュレータ）は除外する
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			LogManager::Log(std::format("Use Adapter:{}", ConvertString(adapterDesc.Description)));
			break;
		}
		adapter_ = nullptr;
	}
	HTT_ASSERT_MSG(adapter_ != nullptr, "適切なアダプタが見つかりませんでした");
}

// ===== D3D12Deviceの生成（12.2→12.1→12.0の順で試みる）=====
void DirectXCommon::CreateDevice() {
	for (const FeatureLevel& featureLevel : kFeatureLevels) {
		const HRESULT hr = D3D12CreateDevice(adapter_.Get(), featureLevel.level, IID_PPV_ARGS(&device_));
		if (SUCCEEDED(hr)) {
			LogManager::Log(std::format("FeatureLevel : {}", featureLevel.name));
			break;
		}
	}
	HTT_ASSERT_MSG(device_ != nullptr, "デバイスの生成に失敗しました");
	LogManager::Log("Complete create D3D12Device!!!");
#ifdef _DEBUG
	device_->SetName(L"MainDevice");
#endif
}

// ===== コマンドキュー・アロケータ・リストの生成 =====
void DirectXCommon::CreateCommandObjects() {
	// --- コマンドキュー ---
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	HRESULT hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	CheckCreated(hr, "コマンドキューの生成に失敗しました");
	commandQueue_->SetName(L"MainCommandQueue");

	// --- コマンドアロケータ ---
	hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	CheckCreated(hr, "コマンドアロケータを生成できませんでした");
	commandAllocator_->SetName(L"MainCommandAllocator");

	// --- コマンドリスト ---
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	CheckCreated(hr, "コマンドリストを生成できませんでした");
	commandList_->SetName(L"MainCommandList");
}

// ===== フェンス・イベントの生成 =====
void DirectXCommon::CreateFence() {
	const HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	CheckCreated(hr, "フェンスの生成に失敗しました");
	fence_->SetName(L"MainFence");
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	HTT_ASSERT_MSG(fenceEvent_ != nullptr, "フェンスイベントの生成に失敗しました");
}

// ===== DXCコンパイラの初期化 =====
void DirectXCommon::CreateDxc() {
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	CheckCreated(hr, "DxcUtilsを生成できませんでした");
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	CheckCreated(hr, "DxcCompilerを生成できませんでした");
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	CheckCreated(hr, "DXC IncludeHandlerを生成できませんでした");
}

// ===== ディスクリプタの大きさとヒープ =====
void DirectXCommon::CreateDescriptorObjects() {
	// --- DescriptorSizeをキャッシュ ---
	descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// --- SRVDescriptorHeapの生成 ---
	// スロット0: ImGui用（取っておく）、スロット1〜: AllocateSRVSlot()で発行
	srvAllocator_ = std::make_unique<DescriptorAllocator>(kSRVDescriptorHeap, 1, L"SRVDescriptorHeap");
}

//=============================================================================
// GPUの処理完了待ち
//=============================================================================
void DirectXCommon::WaitForGPU() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	// 目標値を一つ決める
	instance_->fenceValue_++;
	instance_->commandQueue_->Signal(instance_->fence_.Get(), instance_->fenceValue_);

	if (instance_->fence_->GetCompletedValue() < instance_->fenceValue_) {
		// イベントを設定
		instance_->fence_->SetEventOnCompletion(instance_->fenceValue_, instance_->fenceEvent_);
		// イベントを待つ
		WaitForSingleObject(instance_->fenceEvent_, INFINITE);
	}
}

//=============================================================================
// 描画のコマンドリストを実行して待ち、また積める状態に戻す
//=============================================================================
void DirectXCommon::ExecuteAndWait() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	ID3D12GraphicsCommandList* commandList = instance_->commandList_.Get();
	// 積んだコマンドを確定して、GPUに渡す
	HRESULT hr = commandList->Close();
	HTT_ASSERT_MSG(SUCCEEDED(hr), "コマンドリストのクローズに失敗しました");
	std::array<ID3D12CommandList*, 1> commandLists = {commandList};
	instance_->commandQueue_->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());
	// 終わるまで待つ（待たずにResetすると、GPUが読んでいる途中のコマンドを消してしまう）
	WaitForGPU();
	// また積めるようにする
	instance_->commandAllocator_->Reset();
	commandList->Reset(instance_->commandAllocator_.Get(), nullptr);
}

//=============================================================================
// SRVスロット（中身は DescriptorAllocator）
//=============================================================================
// ===== 発行 =====
uint32_t DirectXCommon::AllocateSRVSlot() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	return instance_->srvAllocator_->Allocate();
}

// ===== 返却 =====
void DirectXCommon::FreeSRVSlot(uint32_t slot) {
	// 終了のとき、DirectXCommonより後にRenderTextureが消えることがある（ヒープごと無いので、返す先も無い）
	if (instance_ == nullptr) {
		return;
	}
	instance_->srvAllocator_->Free(slot);
}

// ===== 返してもらったスロットを、また配れるようにする =====
void DirectXCommon::RecycleFreedSRVSlots() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	instance_->srvAllocator_->RecycleFreed();
}

// ===== 今使っている数 =====
uint32_t DirectXCommon::GetUsedSRVSlotCount() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	return instance_->srvAllocator_->GetUsedCount();
}

//=============================================================================
// トランジションバリアを1つ発行する
//=============================================================================
// ===== 描画のコマンドリストへ =====
void DirectXCommon::TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	TransitionBarrier(instance_->commandList_.Get(), resource, before, after);
}

// ===== 渡したコマンドリストへ =====
void DirectXCommon::TransitionBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
	// 中身の詰め方（全サブリソース・フラグなし）は d3dx12.h の CD3DX12_RESOURCE_BARRIER::Transition にまかせる
	const CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
	commandList->ResourceBarrier(1, &barrier);
}

//=============================================================================
// トランジションバリアを複数まとめて発行する
//=============================================================================
void DirectXCommon::TransitionBarriers(std::initializer_list<std::tuple<ID3D12Resource*, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES>> resources) {
	assert(instance_ && "[DirectXCommon::TransitionBarriers] Init()を先に呼んでください");
	assert(resources.size() > 0 && "[DirectXCommon::TransitionBarriers] resourcesが空です");

	// バリアを配列に詰める
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	barriers.reserve(resources.size());
	for (const auto& [resource, before, after] : resources) {
		barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after));
	}
	// 1回のAPIコールで全バリアを発行する
	instance_->commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

//=============================================================================
// DescriptorHandle取得
//=============================================================================
// ===== ディスクリプタ1個分の大きさ =====
uint32_t DirectXCommon::DescriptorSizeOf(D3D12_DESCRIPTOR_HEAP_TYPE heapType) {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	switch (heapType) {
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		return instance_->descriptorSizeSRV_;
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
		return instance_->descriptorSizeRTV_;
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
		return instance_->descriptorSizeDSV_;
	default:
		HTT_ASSERT_MSG(false, "未対応のDescriptorHeapTypeです");
		return 0;
	}
}

// ===== CPU =====
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += DescriptorSizeOf(heapType) * index;
	return handle;
}

// ===== GPU =====
D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += DescriptorSizeOf(heapType) * index;
	return handle;
}

// ===== SRV のヒープを見せる =====
D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::BindSRVHeap(ID3D12GraphicsCommandList* cmdList) {
	std::array<ID3D12DescriptorHeap*, 1> heaps = {GetSRVDescriptorHeap()};
	cmdList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
	return heaps[0]->GetGPUDescriptorHandleForHeapStart();
}

//=============================================================================
// デバイスが削除されたか確認
//=============================================================================
bool DirectXCommon::CheckDeviceRemoved() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	HRESULT reason = instance_->device_->GetDeviceRemovedReason();
	if (FAILED(reason)) {
		GraphicsDebug::ReportDeviceRemoved(instance_->device_.Get(), reason);
		return true;
	}
	return false;
}

//=============================================================================
// ゲッター
//=============================================================================
IDXGIFactory7* DirectXCommon::GetFactory() {
	assert(instance_);
	return instance_->dxgiFactory_.Get();
}
IDXGIAdapter4* DirectXCommon::GetAdapter4() {
	assert(instance_);
	return instance_->adapter_.Get();
}
ID3D12Device* DirectXCommon::GetDevice() {
	assert(instance_);
	return instance_->device_.Get();
}
ID3D12CommandQueue* DirectXCommon::GetCommandQueue() {
	assert(instance_);
	return instance_->commandQueue_.Get();
}
ID3D12CommandAllocator* DirectXCommon::GetCommandAllocator() {
	assert(instance_);
	return instance_->commandAllocator_.Get();
}
ID3D12GraphicsCommandList* DirectXCommon::GetCommandList() {
	assert(instance_);
	return instance_->commandList_.Get();
}
ID3D12DescriptorHeap* DirectXCommon::GetSRVDescriptorHeap() {
	assert(instance_);
	return instance_->srvAllocator_->GetHeap();
}
IDxcUtils* DirectXCommon::GetDxcUtils() {
	assert(instance_);
	return instance_->dxcUtils_.Get();
}
IDxcCompiler3* DirectXCommon::GetDxcCompiler() {
	assert(instance_);
	return instance_->dxcCompiler_.Get();
}
IDxcIncludeHandler* DirectXCommon::GetIncludeHandler() {
	assert(instance_);
	return instance_->includeHandler_.Get();
}
uint32_t DirectXCommon::GetDescriptorSizeSRV() {
	assert(instance_);
	return instance_->descriptorSizeSRV_;
}
uint32_t DirectXCommon::GetDescriptorSizeRTV() {
	assert(instance_);
	return instance_->descriptorSizeRTV_;
}
uint32_t DirectXCommon::GetDescriptorSizeDSV() {
	assert(instance_);
	return instance_->descriptorSizeDSV_;
}
uint32_t DirectXCommon::GetDrawCallCount() {
	assert(instance_);
	return instance_->drawCallCount_;
}