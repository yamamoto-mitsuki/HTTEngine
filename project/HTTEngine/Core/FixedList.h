#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <type_traits>
#include <initializer_list>


/// <summary>
/// 上限の数が決まった一覧。データの Component に一覧を持たせるときに使う（Script は std::vector でよい）
/// <para>Component は memcpy で写せる型しか持てない。std::vector は中で別の場所のメモリを指しているので持てない。
/// これは要素を自分の中の配列に入れるので、写すだけで中身ごと写る（Undo・Prefab・Duplicate がそのまま使える）。FixedString と同じ考え方</para>
/// <para>Inspector・保存は ui.ListField(...) で std::vector と同じに書ける（ファイルの形も同じ JSON の配列）</para>
/// <para>使っていない所（Size() から後ろ）は、いつも初期値（T{}）にしておく。同じ一覧なら配列の中身も全部同じになるので、Undo が「変わったか」をバイトで比べても正しく見える</para>
/// </summary>
template<class T, size_t N>
class FixedList {
	static_assert(std::is_trivially_copyable_v<T>, "FixedList の要素は、memcpy で写せる型にしてください（std::string・std::vector を持たない struct）");
	static_assert(std::is_default_constructible_v<T>, "FixedList の要素は、引数の無いコンストラクタで作れるようにしてください（足したときの初期値）");
	static_assert(N >= 1, "FixedList は1つ以上入る大きさにしてください");

public:
	FixedList() = default;
	// { {20.0f, 1.0f}, {40.0f, 1.5f} } のように、最初の中身を書ける（入りきらない分は捨てる）
	FixedList(std::initializer_list<T> items) {
		for (const T& item : items) {
			if (!PushBack(item)) {
				break;
			}
		}
	}

	// ===== 数 =====
	size_t Size() const { return size_; }
	bool Empty() const { return size_ == 0; }
	bool IsFull() const { return size_ == N; }
	// 入る数の上限
	static constexpr size_t Capacity() { return N; }

	// ===== 要素（index は Size() より小さいこと。範囲の外は読まない・書かない）=====
	T& operator[](size_t index) { return items_[index]; }
	const T& operator[](size_t index) const { return items_[index]; }
	T& Front() { return items_[0]; }
	const T& Front() const { return items_[0]; }
	T& Back() { return items_[size_ - 1]; }
	const T& Back() const { return items_[size_ - 1]; }

	// range-based for で回せるように（for (Stage& stage : stages)）。名前は C++ の決まりで小文字
	T* begin() { return items_.data(); }
	T* end() { return items_.data() + size_; }
	const T* begin() const { return items_.data(); }
	const T* end() const { return items_.data() + size_; }

	// ===== 足す・消す =====
	// 最後に足す（一杯なら足さずに false）
	bool PushBack(const T& item) {
		if (IsFull()) {
			return false;
		}
		items_[size_] = item;
		++size_;
		return true;
	}

	// 最後を消す（空なら何もしない）
	void PopBack() {
		if (size_ > 0) {
			--size_;
			items_[size_] = T{};
		}
	}

	// index の所に入れて、後ろを1つずつずらす（一杯・index が Size() より大きいなら、入れずに false）
	bool Insert(size_t index, const T& item) {
		if (IsFull() || index > size_) {
			return false;
		}
		// std::move_backward：[index, size_) を、後ろの端から1つ後ろへずらす（前から写すと、写す前の要素を上書きしてしまう）
		std::move_backward(items_.begin() + index, items_.begin() + size_, items_.begin() + size_ + 1);
		items_[index] = item;
		++size_;
		return true;
	}

	// index を消して、後ろを1つずつ詰める（範囲の外なら何もしない）
	void Erase(size_t index) {
		if (index >= size_) {
			return;
		}
		std::move(items_.begin() + index + 1, items_.begin() + size_, items_.begin() + index);
		PopBack(); // 詰めた後の最後（前の最後の写し）を初期値に戻す
	}

	// 数を変える（増えた分は初期値。上限より多くは増やさない）
	void Resize(size_t count) {
		count = (std::min)(count, N);
		// 減った分は初期値に戻す（使っていない所はいつも初期値なので、増えた分はもう初期値になっている）
		if (count < size_) {
			std::fill(items_.begin() + count, items_.begin() + size_, T{});
		}
		size_ = static_cast<uint32_t>(count);
	}

	void Clear() { Resize(0); }

private:
	std::array<T, N> items_{}; // 要素（使うのは先頭から size_ 個）
	uint32_t size_ = 0;         // 入っている数
};
