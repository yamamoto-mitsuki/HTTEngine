#include "PrefabRecords.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"
#include "MyEngine/String/ConvertString.h"


namespace {
// ===== 読めない入れ子の代わりの、空の Entity 1つ分 =====
SceneJson MakeEmptyRecord(EntityId id) {
	SceneJson record = SceneJson::object();
	record["id"] = id;
	record["name"] = "Entity";
	record["parent"] = EntityId{0};
	record["active"] = true;
	record["components"] = SceneJson::object();
	return record;
}

// ===== Entity 1つ分に、上書き（書いてある名前・有効・親・Component と、外した Component）を当てる =====
// Component は書いてある項目だけを上書きする（merge_patch。{"translation": [...]} なら translation だけ変わる）。Prefab に無い型は足す
void ApplyOverride(SceneJson& record, const SceneJson& override) {
	for (const char* key : {"name", "active", "parent"}) {
		if (const auto it = override.find(key); it != override.end()) {
			record[key] = *it;
		}
	}
	if (!record.contains("components") || !record["components"].is_object()) {
		record["components"] = SceneJson::object();
	}
	SceneJson& target = record["components"];
	if (const SceneJson* components = PrefabRecords::FindComponents(override)) {
		for (auto it = components->begin(); it != components->end(); ++it) {
			if (target.contains(it.key()) && target[it.key()].is_object() && it->is_object()) {
				target[it.key()].merge_patch(*it);
			} else {
				target[it.key()] = *it;
			}
		}
	}
	if (const auto removed = override.find("removedComponents"); removed != override.end() && removed->is_array()) {
		for (const SceneJson& name : *removed) {
			if (name.is_string()) {
				target.erase(name.get<std::string>());
			}
		}
	}
}
} // namespace


//=============================================================================
// 並べる
//=============================================================================
EntityRecords PrefabRecords::Expand(uint32_t prefab, EntityId rootId, std::set<std::string>& warnings) {
	std::vector<uint32_t> stack;
	return ExpandNested(prefab, rootId, stack, warnings);
}

EntityRecords PrefabRecords::ExpandNested(uint32_t prefab, EntityId rootId, std::vector<uint32_t>& stack, std::set<std::string>& warnings) {
	// 中身は shared_ptr で持つ（入れ子の Load で Prefab が増えても、この中身はそのまま使える）
	const std::shared_ptr<const SceneJson> entities = PrefabManager::GetEntities(prefab);
	if (entities == nullptr) {
		return {};
	}
	if (std::ranges::find(stack, prefab) != stack.end() || stack.size() >= kMaxPrefabNestDepth) {
		warnings.insert(std::format("Prefab の中に自分が入っている（か、入れ子が {} 段より深い）ので、そこから先は作りません: {}", kMaxPrefabNestDepth, PrefabManager::GetPath(prefab)));
		return {};
	}
	const EntityId shift = rootId ^ entities->front().at("id").get<EntityId>(); // 一番上が rootId になるずれ
	stack.push_back(prefab);

	// --- 1周目：並べる（番号と親を、置いた番号に）。入れ子は、その Prefab の中身を開いて差し込む ---
	EntityRecords records;
	std::vector<size_t> plainIndices; // ファイルにそのまま書いてあった項目の、records での位置
	// 入れ子を差し込んだ所（先頭の位置・数・ファイルのその項目）
	struct NestedRange {
		size_t begin = 0;
		size_t count = 0;
		const SceneJson* item = nullptr;
	};
	std::vector<NestedRange> nested;
	for (const SceneJson& item : *entities) {
		const EntityId id = item.at("id").get<EntityId>() ^ shift;
		if (const auto path = item.find(kPrefabEntryKey); path != item.end()) {
			const std::string& nestedPath = path->get_ref<const std::string&>();
			EntityRecords sub = ExpandNested(PrefabManager::Load(ToPath(nestedPath)), id, stack, warnings);
			if (sub.empty()) {
				warnings.insert(std::format("入れ子の Prefab が読めないので、空の Entity にしました（ファイルが届いてから開き直すと、中身ができます）: {}", nestedPath));
				sub.push_back(MakeEmptyRecord(id));
			}
			// 入れ子の一番上に、置いた物の印を付ける（パスはファイルに書いてあるとおり）
			sub.front()["components"][kPrefabInstanceComponentName] = SceneJson{{"path", nestedPath}};
			nested.push_back({records.size(), sub.size(), &item});
			records.insert(records.end(), std::make_move_iterator(sub.begin()), std::make_move_iterator(sub.end()));
			continue;
		}
		SceneJson record = item;
		record["id"] = id;
		if (const EntityId parent = item.value("parent", EntityId{0}); parent != 0) {
			record["parent"] = parent ^ shift;
		}
		plainIndices.push_back(records.size());
		records.push_back(std::move(record));
	}

	// --- 2周目：ファイルの番号 → 置いた番号 の表（入れ子の中身も入る）で、Component の中の参照を付け替える ---
	EntityIdMap map;
	for (const SceneJson& record : records) {
		const EntityId id = record.at("id").get<EntityId>();
		map[id ^ shift] = id;
	}
	for (const size_t index : plainIndices) {
		if (const auto components = records[index].find("components"); components != records[index].end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
	}

	// --- 3周目：入れ子の項目（ファイルに書いてある名前・Transform・上書きなど）を、置いた番号にしてから当てる ---
	// 後ろから当てる（消した中身があると records が詰まるので、前から当てると、後の入れ子の位置がずれる）
	for (auto range = nested.rbegin(); range != nested.rend(); ++range) {
		ApplyEntry(records, range->begin, range->count, ShiftEntry(*range->item, shift, map), warnings);
	}
	records.front()["parent"] = EntityId{0}; // 一番上の親は、置く側が決める
	stack.pop_back();
	return records;
}

//=============================================================================
// 当てる
//=============================================================================
void PrefabRecords::ApplyEntry(EntityRecords& records, size_t begin, size_t count, const SceneJson& entry, std::set<std::string>& warnings) {
	// --- 一番上（名前・有効・親は、書いていなくても置いた所ごとの値にする）---
	SceneJson& root = records[begin];
	ApplyOverride(root, entry);
	root["active"] = entry.value("active", true);
	root["parent"] = entry.value("parent", EntityId{0});
	const EntityId rootId = root.at("id").get<EntityId>();

	// --- 中身の上書き（番号 → records の位置。相手が Prefab から消えていたら、その上書きは外す）---
	std::unordered_map<EntityId, size_t> positions;
	for (size_t i = begin; i < begin + count; ++i) {
		positions[records[i].at("id").get<EntityId>()] = i;
	}
	if (const auto overrides = entry.find("overrides"); overrides != entry.end() && overrides->is_array()) {
		for (const SceneJson& override : *overrides) {
			const EntityId id = override.is_object() ? override.value("id", EntityId{0}) : EntityId{0};
			const auto found = positions.find(id);
			if (found == positions.end()) {
				warnings.insert(std::format("上書きの相手（EntityId {}）が Prefab にありません（Prefab から消された？）。この上書きは外れます: {}", id, entry.value("name", std::string())));
				continue;
			}
			ApplyOverride(records[found->second], override);
		}
	}

	// --- 消した中身（子も一緒に。上書きで親を変えた後の親子で見る）---
	std::unordered_set<EntityId> removed;
	if (const auto list = entry.find("removed"); list != entry.end() && list->is_array()) {
		for (const SceneJson& id : *list) {
			if (id.is_number_unsigned() && id.get<EntityId>() != rootId) {
				removed.insert(id.get<EntityId>());
			}
		}
	}
	if (removed.empty()) {
		return;
	}
	// 子の子まで広げる（親を変えた物は、親が後ろに並ぶこともあるので、増えなくなるまで繰り返す）
	for (bool isGrown = true; isGrown;) {
		isGrown = false;
		for (size_t i = begin; i < begin + count; ++i) {
			const EntityId id = records[i].at("id").get<EntityId>();
			if (!removed.contains(id) && removed.contains(records[i].value("parent", EntityId{0}))) {
				removed.insert(id);
				isGrown = true;
			}
		}
	}
	// remove_if：消さない物を前へ詰めて、新しい終わりを返す。erase で、その後ろを本当に消す
	const auto first = records.begin() + static_cast<std::ptrdiff_t>(begin);
	const auto last = first + static_cast<std::ptrdiff_t>(count);
	records.erase(std::remove_if(first, last, [&removed](const SceneJson& record) { return removed.contains(record.at("id").get<EntityId>()); }), last);
}

//=============================================================================
// 番号をずらす
//=============================================================================
SceneJson PrefabRecords::ShiftEntry(SceneJson entry, EntityId shift, const EntityIdMap& map) {
	// Entity 1つ分（一番上・上書き1つ）の番号と参照
	const auto shiftOne = [shift, &map](SceneJson& item) {
		if (const auto id = item.find("id"); id != item.end() && id->is_number_unsigned()) {
			*id = id->get<EntityId>() ^ shift;
		}
		if (const auto parent = item.find("parent"); parent != item.end() && parent->is_number_unsigned() && parent->get<EntityId>() != 0) {
			*parent = parent->get<EntityId>() ^ shift;
		}
		if (const auto components = item.find("components"); components != item.end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
	};
	shiftOne(entry);
	if (const auto overrides = entry.find("overrides"); overrides != entry.end() && overrides->is_array()) {
		for (SceneJson& override : *overrides) {
			shiftOne(override);
		}
	}
	if (const auto removed = entry.find("removed"); removed != entry.end() && removed->is_array()) {
		for (SceneJson& id : *removed) {
			if (id.is_number_unsigned()) {
				id = id.get<EntityId>() ^ shift;
			}
		}
	}
	return entry;
}

bool PrefabRecords::AreIdsFree(const EntityRecords& records) {
	return std::ranges::all_of(records, [](const SceneJson& record) {
		const EntityId id = record.at("id").get<EntityId>();
		return id != 0 && !EntityManager::FindById(id).IsValid();
	});
}

void PrefabRecords::Relabel(EntityRecords& records, EntityId delta) {
	EntityIdMap map;
	for (const SceneJson& record : records) {
		const EntityId id = record.at("id").get<EntityId>();
		map[id] = id ^ delta;
	}
	for (SceneJson& record : records) {
		record["id"] = map.at(record.at("id").get<EntityId>());
		if (const auto parent = map.find(record.value("parent", EntityId{0})); parent != map.end()) {
			record["parent"] = parent->second;
		}
		if (const auto components = record.find("components"); components != record.end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
	}
}

//=============================================================================
// 小さな道具
//=============================================================================
const SceneJson* PrefabRecords::FindComponents(const SceneJson& item) {
	const auto it = item.find("components");
	return (it != item.end() && it->is_object()) ? &*it : nullptr;
}
