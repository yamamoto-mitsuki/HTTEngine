#include "PrefabDiff.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <externals/nlohmann/json.hpp>


//=============================================================================
// 比べる
//=============================================================================
bool PrefabDiff::IsSameValue(const SceneJson& a, const SceneJson& b) {
	// --- 小数：値の大きさに対して、とても小さな違いなら同じ ---
	// Transform の回転は「度 → クォータニオン → 度」と変換するので、読み直すと最後の桁が変わることがある
	// 整数（Entity の番号など）は下の == で比べる（float にすると、大きな番号の違いが消える）
	if (a.is_number_float() || b.is_number_float()) {
		if (!a.is_number() || !b.is_number()) {
			return false;
		}
		constexpr float kTolerance = 1.0e-5f; // 値の大きさに対して、これくらいの違いまでは同じ
		const float x = a.get<float>();
		const float y = b.get<float>();
		// (std::max) と ( ) で囲むのは、Windows.h の max マクロとぶつからないように。{...} で3つ以上を渡せる
		return std::abs(x - y) <= kTolerance * (std::max)({1.0f, std::abs(x), std::abs(y)});
	}
	// --- 配列：同じ数で、1つずつ同じ ---
	if (a.is_array() && b.is_array()) {
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), &IsSameValue);
	}
	// --- {...}：同じ項目があって、項目ごとに同じ ---
	if (a.is_object() && b.is_object()) {
		if (a.size() != b.size()) {
			return false;
		}
		for (auto it = a.begin(); it != a.end(); ++it) {
			const auto found = b.find(it.key());
			if (found == b.end() || !IsSameValue(*it, *found)) {
				return false;
			}
		}
		return true;
	}
	return a == b;
}

//=============================================================================
// 差分
//=============================================================================
SceneJson PrefabDiff::MakePatch(const SceneJson& before, const SceneJson& after) {
	SceneJson patch = SceneJson::object();
	for (auto it = after.begin(); it != after.end(); ++it) {
		const auto found = before.find(it.key());
		if (found == before.end()) {
			patch[it.key()] = *it;
		} else if (!IsSameValue(*found, *it)) {
			patch[it.key()] = (found->is_object() && it->is_object()) ? MakePatch(*found, *it) : *it;
		}
	}
	// before にしか無い項目は null（merge_patch で「消す」の印）
	for (auto it = before.begin(); it != before.end(); ++it) {
		if (!after.contains(it.key())) {
			patch[it.key()] = nullptr;
		}
	}
	return patch;
}

void PrefabDiff::DiffComponents(const SceneJson& before, const SceneJson& after, SceneJson& patches, SceneJson& removed) {
	for (auto it = after.begin(); it != after.end(); ++it) {
		const auto found = before.find(it.key());
		if (found == before.end()) {
			patches[it.key()] = *it; // 足した Component
			continue;
		}
		SceneJson patch = MakePatch(*found, *it);
		if (!patch.empty()) {
			patches[it.key()] = std::move(patch);
		}
	}
	for (auto it = before.begin(); it != before.end(); ++it) {
		if (!after.contains(it.key())) {
			removed.push_back(it.key()); // 外した Component
		}
	}
}
