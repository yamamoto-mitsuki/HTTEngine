#pragma once
#include <vector>
#include <utility>
#include <cstdint>
#include <algorithm>

#include "HTTEngine/Core/Handle.h"


/// <summary>
/// Handleで要素を指す入れ物。生きている要素は隙間なく連続して並ぶ
/// <para>破棄済みのHandleで Get すると nullptr が返る</para>
/// <para>注意: Create / Destroy で要素の場所が動くので、Get で受け取ったポインタは使い捨てにする</para>
/// </summary>
template<class T> 
class SlotMap {
public:
	Handle<T> Create();        // 空きスロットを再利用して1個作る
	void Destroy(Handle<T> h); // 後ろの要素を前へ詰めて消し（並びは作った順のまま）、スロットを空きリストへ
	T* Get(Handle<T> h);       // 破棄済み・無効なら nullptr
	const T* Get(Handle<T> handle) const;
	bool IsAlive(Handle<T> h) const;

	size_t Size() const { return dense_.size(); }
	// 生きている要素だけが隙間なく並んでいるので、そのまま全部回せる
	auto begin() { return dense_.begin(); }
	auto end() { return dense_.end(); }
	auto begin() const { return dense_.begin(); }
	auto end() const { return dense_.end(); }

private:
	static constexpr uint32_t kInvalidIndex = 0xFFFFFFFF;

	// Handle.index が指す台帳の1桁
	struct Slot {
		uint32_t denseIndex = kInvalidIndex; // dense_の何番目にあるか。空きなら kInvalidIndex
		uint32_t generation = 1;             // 0は「無効なHandle」用に開けておく
	};

	std::vector<T> dense_;              // 実体。生きている要素だけが連続で並ぶ
	std::vector<uint32_t> denseToSlot_; // dense_[i] がどのスロットのものか（入れ替え時の付け替え用）
	std::vector<Slot> slots_;           // Handle.index → dense_ の位置と世代
	std::vector<uint32_t> freeList_;    // 空いているスロット番号
};



//=============================================================================
// 作成
//=============================================================================
template<class T> 
Handle<T> SlotMap<T>::Create() {
	// --- スロットを決める。空きがあれば再利用 ---
	uint32_t slotIndex = 0;
	if (!freeList_.empty()) {
		slotIndex = freeList_.back();
		freeList_.pop_back();
	} else {
		slotIndex = static_cast<uint32_t>(slots_.size());
		slots_.push_back(Slot{});
	}
	// --- 実体は常に末尾に足す ---
	Slot& slot = slots_[slotIndex];
	slot.denseIndex = static_cast<uint32_t>(dense_.size());
	dense_.emplace_back();
	denseToSlot_.push_back(slotIndex);
	return Handle<T>{slotIndex, slot.generation};
}


//=============================================================================
// 破棄
//=============================================================================
template<class T> 
void SlotMap<T>::Destroy(Handle<T> handle) {
	if (!IsAlive(handle)) {
		return; // 破棄済み・無効なら何もしない（2回消しても安全）
	}
	Slot& slot = slots_[handle.index];
	const uint32_t removeIndex = slot.denseIndex;

	// --- 後ろの要素を1つずつ前へ詰めて、末尾を消す（隙間を作らない・並びを保つ）---
	// 末尾と入れ替えると、消すたびに並び（Hierarchy の順・画面の要素の順）が変わってしまうので、詰める
	// std::move（3つの引数）：[first, last) の要素を、dest から順に移す（前へずらす）
	std::move(dense_.begin() + removeIndex + 1, dense_.end(), dense_.begin() + removeIndex);
	std::move(denseToSlot_.begin() + removeIndex + 1, denseToSlot_.end(), denseToSlot_.begin() + removeIndex);
	dense_.pop_back();
	denseToSlot_.pop_back();
	// 前へずれた要素のスロットに、新しい場所を教える
	for (uint32_t i = removeIndex; i < static_cast<uint32_t>(dense_.size()); ++i) {
		slots_[denseToSlot_[i]].denseIndex = i;
	}

	// --- スロットを空きにする。世代を進めて古いHandleを無効にする ---
	slot.denseIndex = kInvalidIndex;
	++slot.generation;
	if (slot.generation == 0) {
		slot.generation = 1; // 一周したら0（無効）を飛ばす
	}
	freeList_.push_back(handle.index);
}


//=============================================================================
// 取得
//=============================================================================
template<class T> 
T* SlotMap<T>::Get(Handle<T> handle) {
	if (!IsAlive(handle)) {
		return nullptr;
	}
	return &dense_[slots_[handle.index].denseIndex];
}

template<class T> 
const T* SlotMap<T>::Get(Handle<T> handle) const {
	if (!IsAlive(handle)) {
		return nullptr;
	}
	return &dense_[slots_[handle.index].denseIndex];
}

template<class T>
bool SlotMap<T>::IsAlive(Handle<T> handle) const {
	if (!handle.IsValid() || handle.index >= slots_.size()) {
		return false;
	}
	const Slot& slot = slots_[handle.index];
	return slot.generation == handle.generation && slot.denseIndex != kInvalidIndex;
}