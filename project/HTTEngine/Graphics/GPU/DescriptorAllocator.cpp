#include "HTTEngine/Graphics/GPU/DescriptorAllocator.h"

#include <algorithm>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"


//=============================================================================
// 作成
//=============================================================================
DescriptorAllocator::DescriptorAllocator(uint32_t capacity, uint32_t reservedCount, const std::wstring& name) : capacity_(capacity), reservedCount_(reservedCount), next_(reservedCount) {
	HTT_ASSERT_MSG(reservedCount < capacity, "取っておく番号の数が、ヒープの大きさ以上です");
	// シェーダーから見えるヒープ（SetDescriptorHeaps で積めるのはこの種類だけ）
	heap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, capacity, true);
	heap_->SetName(name.c_str());
}


//=============================================================================
// 配る・返す
//=============================================================================
// ===== 配る =====
// 返してもらった番号があれば、それを先に使う（無ければ新しい番号）
uint32_t DescriptorAllocator::Allocate() {
	// --- 返してもらった番号を使い回す ---
	if (!free_.empty()) {
		const uint32_t slot = free_.back();
		free_.pop_back();
		return slot;
	}
	// --- 無ければ新しい番号 ---
	HTT_ASSERT_MSG(next_ < capacity_, "DescriptorHeapのスロットが不足しています");
	return next_++;
}

// ===== 返す =====
// すぐには配り直さず、フレームの終わり（GPUを待った後）まで取っておく
void DescriptorAllocator::Free(uint32_t slot) {
	// 取っておいた番号は、ここからは配っていない
	if (slot < reservedCount_) {
		return;
	}
	HTT_ASSERT_MSG(slot < next_, "配っていないスロットが返されました");
	// 同じ番号を2回返すと、別々の2つに同じ番号が配られてしまう
	const auto contains = [slot](const std::vector<uint32_t>& slots) { return std::find(slots.begin(), slots.end(), slot) != slots.end(); };
	HTT_ASSERT_MSG(!contains(free_) && !contains(pendingFree_), "同じスロットが2回返されました");
	// このフレームのコマンドリストが、まだこの番号を読むかもしれない。
	// すぐに配り直して別のテクスチャのSRVで上書きすると、GPUが実行するときには別の絵を読んでしまう
	pendingFree_.push_back(slot);
}

// ===== 返してもらった番号を、また配れるようにする =====
// フレームの終わりに WaitForGPU した後に呼ぶ（このフレームのコマンドは、もう全部終わっている）
void DescriptorAllocator::RecycleFreed() {
	free_.insert(free_.end(), pendingFree_.begin(), pendingFree_.end());
	pendingFree_.clear();
}

//=============================================================================
// 今配っている番号の数
//=============================================================================
uint32_t DescriptorAllocator::GetUsedCount() const {
	// 配った数（reservedCount_番〜next_-1番）から、返してもらった数を引く
	const uint32_t issued = next_ - reservedCount_;
	const size_t returned = free_.size() + pendingFree_.size();
	return issued - static_cast<uint32_t>(returned);
}