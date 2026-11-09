#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"

// 前方宣言
struct Entity;

/// <summary>
/// Component1つ分の写し（型を知らずに持てる形。Undo・コピー・「Componentを付けて作る」用）
/// <para>同じ実行の中だけで使う。type_index やバイト列をファイルに保存しない（型の番号も並びも実行ごとに変わりうる）</para>
/// </summary>
struct ComponentSnapshot {
	std::type_index type = typeid(void); // どの型か
	std::vector<std::byte> bytes;        // 中身をそのまま写したもの

	// 値から写しを作る
	template<class T> static ComponentSnapshot Make(const T& value) {
		static_assert(std::is_trivially_copyable_v<T>, "Componentはmemcpyで写せる型にしてください（ARCHITECTURE.md 原則2）");
		const auto* begin = reinterpret_cast<const std::byte*>(&value);
		return {typeid(T), std::vector<std::byte>(begin, begin + sizeof(T))};
	}
};

/// <summary>
/// 型を知らなくても呼べる窓口。EntityManagerは全部の型をこれでまとめて扱う
/// </summary>
class IComponentStorage {
public:
	virtual ~IComponentStorage() = default;
	virtual void Flush(bool (*isAlive)(Handle<Entity>)) = 0; // 予約を反映する（フレームの境目）
	virtual void DestroyEntity(Handle<Entity> entity) = 0;   // そのEntityの実体と予約を消す（フレームの境目）

	// ===== 型を知らずに中身を写す・戻す（Undo・コピー用）=====
	virtual size_t GetSize() const = 0;
	virtual const void* Find(Handle<Entity> entity) const = 0;              // 持っていなければnullptr
	virtual void SetNowBytes(Handle<Entity> entity, const void* bytes) = 0; // 無ければ作り、あれば上書き（フレームの境目）
};

/// <summary>
/// 型Tのところを、型別の連続した配列で持つ（ARCHITECTURE.md 段階3の「型別の連続配列」）
/// <para>追加・取り外しは予約して、Flushでまとめて反映する</para>
/// </summary>
template<class T> class ComponentStorage final : public IComponentStorage {
	static_assert(std::is_same_v<T, std::remove_cvref_t<T>>);
	static_assert(std::is_class_v<T> && std::is_trivially_copyable_v<T>);
	static_assert(std::is_default_constructible_v<T> && std::is_copy_assignable_v<T>);

public:
	struct Entry {
		Handle<Entity> entity;
		T value;
	};

	T* Get(Handle<Entity> entity) {
		const auto it = indices_.find(Key(entity));
		return it == indices_.end() ? nullptr : &entries_[it->second].value;
	}

	bool RequestAdd(Handle<Entity> entity, const T& initial) {
		const uint64_t key = Key(entity);
		const auto pending = pending_.find(key);
		// 予約後の状態で判断する。削除予約の後なら、現在実体があっても追加できる。
		const bool willExist = pending != pending_.end() ? pending->second.has_value() : indices_.contains(key);
		if (willExist) {
			return false;
		}
		pending_.insert_or_assign(key, initial);
		return true;
	}

	bool RequestRemove(Handle<Entity> entity) {
		const uint64_t key = Key(entity);
		const auto pending = pending_.find(key);
		const bool willExist = pending != pending_.end() ? pending->second.has_value() : indices_.contains(key);
		if (!willExist) {
			return false;
		}
		pending_.insert_or_assign(key, std::nullopt);
		return true;
	}

	bool IsAddPending(Handle<Entity> entity) const {
		const auto it = pending_.find(Key(entity));
		return it != pending_.end() && it->second.has_value();
	}

	void Flush(bool (*isAlive)(Handle<Entity>)) override {
		AssertNotIterating();
		for (const auto& [key, value] : pending_) {
			const Handle<Entity> entity = FromKey(key);
			if (!isAlive(entity)) {
				continue; // 削除済み・世代が違う予約は捨てる
			}
			if (value) {
				SetNow(entity, *value);
			} else {
				RemoveNow(entity);
			}
		}
		pending_.clear();
	}

	void DestroyEntity(Handle<Entity> entity) override {
		AssertNotIterating();
		pending_.erase(Key(entity)); // 実体がまだ無い追加予約も消す
		RemoveNow(entity);
	}

	size_t GetSize() const override { return sizeof(T); }

	const void* Find(Handle<Entity> entity) const override {
		const auto it = indices_.find(Key(entity));
		return it == indices_.end() ? nullptr : &entries_[it->second].value;
	}

	void SetNowBytes(Handle<Entity> entity, const void* bytes) override {
		T value{};
		std::memcpy(&value, bytes, sizeof(T)); // バイト列からTへ戻す
		SetNow(entity, value);
	}

	// 以下はEntityManagerから、フレームの境目でのみ使う。
	// Entity生成時の必須Transformと、予約反映の共通処理。
	void SetNow(Handle<Entity> entity, const T& value) {
		AssertNotIterating();
		if (T* existing = Get(entity)) {
			*existing = value;
			return;
		}
		const size_t index = entries_.size();
		entries_.push_back({entity, value});
		try {
			indices_.emplace(Key(entity), index);
		} catch (...) {
			entries_.pop_back(); // 索引の確保に失敗しても片方だけ増やさない
			throw;
		}
	}

	// 持っている物を全部回す。回している間、この型の配列は増減させない（予約はしてよい）
	template<class F> void ForEach(F&& function) {
		IterationScope scope(iterating_);
		for (Entry& entry : entries_) {
			function(entry.entity, entry.value);
		}
	}

private:
	// ForEachの間だけ数を増やす。例外で抜けても必ず戻す
	class IterationScope {
	public:
		explicit IterationScope(uint32_t& count) : count_(count) { ++count_; }
		~IterationScope() { --count_; }
		IterationScope(const IterationScope&) = delete;
		IterationScope& operator=(const IterationScope&) = delete;

	private:
		uint32_t& count_;
	};

	static uint64_t Key(Handle<Entity> entity) { return (static_cast<uint64_t>(entity.generation) << 32) | entity.index; }
	static Handle<Entity> FromKey(uint64_t key) { return {static_cast<uint32_t>(key), static_cast<uint32_t>(key >> 32)}; }

	// 回している配列を増減させると、回している途中の要素がずれる・消える
	void AssertNotIterating() const { HTT_ASSERT_MSG(iterating_ == 0, "ForEachで回している型の配列は、回し終わるまで増減できません（RequestAdd / RequestRemove で予約してください）"); }

	void RemoveNow(Handle<Entity> entity) {
		const auto it = indices_.find(Key(entity));
		if (it == indices_.end()) {
			return;
		}
		const size_t index = it->second;
		const size_t last = entries_.size() - 1;
		if (index != last) {
			entries_[index] = std::move(entries_[last]);
			indices_.at(Key(entries_[index].entity)) = index;
		}
		entries_.pop_back();
		indices_.erase(it);
	}

	std::vector<Entry> entries_;                   // 生きている実体だけ。削除は末尾と交換するので順序は不定。
	std::unordered_map<uint64_t, size_t> indices_; // Entityの世代込みキー → 配列位置
	// valueあり＝追加（削除→追加なら置換）、nullopt＝削除。
	std::unordered_map<uint64_t, std::optional<T>> pending_;
	uint32_t iterating_ = 0; // ForEachの途中なら1以上
};