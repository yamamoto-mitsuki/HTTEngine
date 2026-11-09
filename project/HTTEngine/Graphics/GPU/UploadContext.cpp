#include "HTTEngine/Graphics/GPU/UploadContext.h"

#include <array>
#include <vector>
#include <cassert>
#include <cstring>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"

// 静的メンバ変数
std::unique_ptr<UploadContext> UploadContext::instance_ = nullptr;

//=============================================================================
// 初期化 / 解放
//=============================================================================
void UploadContext::Initialize(size_t stagingBytes) { 
	HTT_ASSERT_MSG(instance_ == nullptr, "Initializeが2回呼ばれています");
	instance_ = std::make_unique<UploadContext>(ConstructorKey());
	instance_->Init(stagingBytes);
}

void UploadContext::Init(size_t stagingBytes) { 
	auto* device = DirectXCommon::GetDevice(); 
	// 転送専用のCommandAllocator, List, Fenceを作成
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_));
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	allocator_->SetName(L"UploadContext_CommandAllocator");
	commandList_->SetName(L"UploadContext_CommandList");
	fence_->SetName(L"UploadContext_Fence");

	// Bufferを使いまわす（永続Map）
	staging_ = GpuResources::CreateUploadBuffer(stagingBytes);
	staging_->SetName(L"UploadContext_StagingBuffer");
	staging_->Map(0, nullptr, reinterpret_cast<void**>(&stagingMapped_));
	stagingCapacity_ = stagingBytes;
	stagingOffset_ = 0;

	LogManager::Log("Initialized");
}

void UploadContext::Release() {
	if (!instance_) {
		return;
	}

	if (instance_->stagingMapped_) {
		instance_->staging_->Unmap(0, nullptr);
	}

	if (instance_->fenceEvent_) {
		CloseHandle(instance_->fenceEvent_);
	}
	instance_.reset();
}


//=============================================================================
// 転送の予約：stagingへ書き、staging→dest のコピーを記録する
//=============================================================================
void UploadContext::QueueUpload(ID3D12Resource* dest, const void* data, size_t size, D3D12_RESOURCE_STATES finalState) { 
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	HTT_ASSERT_MSG(size <= instance_->stagingCapacity_, "1回の転送サイズがstaging容量を超えています。容量を増やしてください。");

	// stagingに入りきらない場合、いったんFlush()して空に
	if (instance_->stagingOffset_ + size > instance_->stagingCapacity_) {
		Flush();
	}

	// staging（永続Map）へCPUコピー
	std::memcpy(instance_->stagingMapped_ + instance_->stagingOffset_, data, size);
	// stagingの該当するOffsetを dest へコピー
	instance_->commandList_->CopyBufferRegion(dest, 0, instance_->staging_.Get(), instance_->stagingOffset_, size);
	
	// Resourceの状態を変更（コピーと同じ、転送用のコマンドリストに積む）
	DirectXCommon::TransitionBarrier(instance_->commandList_.Get(), dest, D3D12_RESOURCE_STATE_COPY_DEST, finalState);

	// Offsetを進める
	instance_->stagingOffset_ = (instance_->stagingOffset_ + size + 255) & ~size_t(255);
	instance_->hasPending_ = true;
}


//=============================================================================
// 中身付きの GPU 常駐バッファを作る
//=============================================================================
Microsoft::WRL::ComPtr<ID3D12Resource> UploadContext::CreateBufferWithData(const void* data, size_t size, 
	D3D12_RESOURCE_STATES finalState, const std::wstring& name) {
	HTT_ASSERT_MSG(size > 0, "大きさ0のバッファは作れません（中身が空）");
	// CPUから直接書けない DEFAULTヒープに作り、staging経由で送る予約をする
	Microsoft::WRL::ComPtr<ID3D12Resource> buffer = GpuResources::CreateDefaultBuffer(size);
	buffer->SetName(name.c_str());
	QueueUpload(buffer.Get(), data, size, finalState);
	return buffer;
}


//=============================================================================
// テクスチャの転送の予約：段ごとに1行ずつ staging へ書き、staging→dest のコピーを記録する
//=============================================================================
void UploadContext::QueueTextureUpload(ID3D12Resource* dest, std::span<const D3D12_SUBRESOURCE_DATA> subresources, 
	D3D12_RESOURCE_STATES finalState, D3D12_RESOURCE_STATES currentState) {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	const UINT count = static_cast<UINT>(subresources.size());

	// --- staging の中での置き方を GPU に聞く ---
	// GPU がテクスチャをコピーするときは「1行の幅が256バイトの倍数」の決まりがあるので、画像の1行の幅とは違うことがある
	const D3D12_RESOURCE_DESC desc = dest->GetDesc();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count); // 段ごとの置き場所（先頭からの位置・1行の幅）
	std::vector<UINT> rowCounts(count);                             // 段ごとの行の数
	std::vector<UINT64> rowSizes(count);                            // 段ごとの1行の中身のバイト数
	UINT64 totalBytes = 0;
	DirectXCommon::GetDevice()->GetCopyableFootprints(&desc, 0, count, 0, layouts.data(), rowCounts.data(), rowSizes.data(), &totalBytes);
	HTT_ASSERT_MSG(totalBytes <= instance_->stagingCapacity_, "1回の転送サイズがstaging容量を超えています。容量を増やしてください。");

	// --- テクスチャは512バイトの倍数の位置から置く。入りきらなければ、いったん Flush して空にする ---
	constexpr size_t kAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
	size_t base = (instance_->stagingOffset_ + kAlignment - 1) & ~(kAlignment - 1);
	if (base + totalBytes > instance_->stagingCapacity_) {
		Flush();
		base = 0;
	}

	// --- 描画で読んでいた絵を書き直すときは、先にコピーを受ける状態へ戻す（作ったばかりの絵は、もう COPY_DEST）---
	// Flush の後に積む（Flush はコマンドリストを空にするので、前に積むと消える）
	if (currentState != D3D12_RESOURCE_STATE_COPY_DEST) {
		DirectXCommon::TransitionBarrier(instance_->commandList_.Get(), dest, currentState, D3D12_RESOURCE_STATE_COPY_DEST);
	}

	for (UINT i = 0; i < count; ++i) {
		// 1行ずつ写す（staging の1行の幅は layouts[i].Footprint.RowPitch、画像の1行の幅は subresources[i].RowPitch）
		uint8_t* destRows = instance_->stagingMapped_ + base + layouts[i].Offset;
		const auto* sourceRows = static_cast<const uint8_t*>(subresources[i].pData);
		for (UINT row = 0; row < rowCounts[i]; ++row) {
			std::memcpy(destRows + static_cast<size_t>(row) * layouts[i].Footprint.RowPitch, sourceRows + static_cast<size_t>(row) * subresources[i].RowPitch, static_cast<size_t>(rowSizes[i]));
		}

		// staging の i 段目 → dest の i 段目
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = dest;
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = i;
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = instance_->staging_.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = layouts[i];
		source.PlacedFootprint.Offset += base; // GetCopyableFootprints は0から数えた位置を返すので、staging の中の位置に直す
		instance_->commandList_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	}

	// Resourceの状態を変更（コピーと同じ、転送用のコマンドリストに積む）
	DirectXCommon::TransitionBarrier(instance_->commandList_.Get(), dest, D3D12_RESOURCE_STATE_COPY_DEST, finalState);

	// Offsetを進める
	instance_->stagingOffset_ = (base + static_cast<size_t>(totalBytes) + 255) & ~size_t(255);
	instance_->hasPending_ = true;
}


//=============================================================================
// 予約した全コピーを実行してGPU完了を待つ
//=============================================================================
void UploadContext::Flush() { 
	// 予約しているものがあるか確認
	if (!instance_->hasPending_) {
		return;
	}
	// 記録を確定して実行
	instance_->commandList_->Close();
	std::array<ID3D12CommandList*, 1> lists = {instance_->commandList_.Get()};
	DirectXCommon::GetCommandQueue()->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
	// 専用のフェンスで完了を待つ
	++instance_->fenceValue_;
	DirectXCommon::GetCommandQueue()->Signal(instance_->fence_.Get(), instance_->fenceValue_);

	if (instance_->fence_->GetCompletedValue() < instance_->fenceValue_) {
		instance_->fence_->SetEventOnCompletion(instance_->fenceValue_, instance_->fenceEvent_);
		WaitForSingleObject(instance_->fenceEvent_, INFINITE);
	}
	instance_->allocator_->Reset();
	instance_->commandList_->Reset(instance_->allocator_.Get(), nullptr);
	instance_->stagingOffset_ = 0;
	instance_->hasPending_ = false;
}