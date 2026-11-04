#include "PrefabOverrides.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Scene/Prefab/PrefabDiff.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"
#include "MyEngine/Scene/Prefab/PrefabRecords.h"


namespace {
constexpr const char* kTransformName = "Transform"; // Transform の保存の名前（一番上の Transform は、いつも丸ごと書く）

// Prefab が読めなかった置いた物の、ファイルの項目（一番上の番号 → 項目）。保存するときに上書きをそのまま書き戻す
std::unordered_map<EntityId, SceneJson> unreadEntries;

// ===== Entity 1つ分の今の Component（Save と同じ形）=====
SceneJson SaveComponents(Handle<Entity> handle, std::set<std::string>& warnings) {
	SceneJson components = SceneJson::object();
	ComponentSerializer::Save(handle, components, warnings);
	return components;
}
} // namespace


//=============================================================================
// 上書きを作る
//=============================================================================
SceneJson PrefabOverrides::MakeInstanceEntry(Handle<Entity> root, std::set<std::string>& warnings) {
	const Entity* entity = EntityManager::Get(root);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	if (entity == nullptr || link == nullptr) {
		return {};
	}
	// ===== 置いた所ごとに必ず書く物（一番上の番号・名前・親・有効・レイヤー・Prefab のパス・Transform）=====
	const Entity* parent = EntityManager::Get(entity->parent);
	SceneJson entry = SceneJson::object();
	entry["id"] = entity->id;
	entry["name"] = entity->name;
	entry["parent"] = (parent != nullptr) ? parent->id : EntityId{0};
	entry["active"] = entity->isActive;
	WriteEntityLayer(entry, entity->layer); // Default なら書かない（読むときも、無ければ Default）
	entry[kPrefabEntryKey] = link->path.ToString();
	SceneJson nowRoot = SaveComponents(root, warnings);
	nowRoot.erase(kPrefabInstanceComponentName); // 印は書かない（"prefab" で分かる）
	SceneJson components = SceneJson::object();
	if (const auto transform = nowRoot.find(kTransformName); transform != nowRoot.end()) {
		components[kTransformName] = *transform;
	}
	nowRoot.erase(kTransformName);

	// ===== Prefab が読めない：読んだときの上書きをそのまま残す（Prefab をまだ pull していない人が保存しても、上書きが消えないように）=====
	if (PrefabManager::GetEntities(link->prefab) == nullptr) {
		const auto kept = unreadEntries.find(entity->id);
		if (kept == unreadEntries.end()) {
			entry["components"] = std::move(components);
			return entry;
		}
		SceneJson keptEntry = kept->second;
		for (const char* key : {"id", "name", "parent", "active"}) {
			keptEntry[key] = entry[key];
		}
		WriteEntityLayer(keptEntry, entity->layer);
		if (components.contains(kTransformName)) {
			keptEntry["components"][kTransformName] = components[kTransformName];
		}
		return keptEntry;
	}

	// ===== Prefab のとおりに並べた物（上書きの基準）と比べる =====
	const EntityRecords records = PrefabRecords::Expand(link->prefab, entity->id, warnings);
	// --- 一番上の Component（Transform は上で丸ごと書いたので比べない）---
	const SceneJson* baseComponents = records.empty() ? nullptr : PrefabRecords::FindComponents(records.front());
	SceneJson baseRoot = (baseComponents != nullptr) ? *baseComponents : SceneJson::object();
	baseRoot.erase(kPrefabInstanceComponentName);
	baseRoot.erase(kTransformName);
	SceneJson removedComponents = SceneJson::array();
	PrefabDiff::DiffComponents(baseRoot, nowRoot, components, removedComponents);
	entry["components"] = std::move(components);
	if (!removedComponents.empty()) {
		entry["removedComponents"] = std::move(removedComponents);
	}

	// --- 中身1つずつ（名前・有効・親・Component）。居なくなった物は「消した」 ---
	SceneJson overrides = SceneJson::array();
	SceneJson removed = SceneJson::array();
	std::unordered_set<EntityId> removedIds;
	for (size_t i = 1; i < records.size(); ++i) {
		const SceneJson& record = records[i];
		const EntityId id = record.at("id").get<EntityId>();
		const Handle<Entity> handle = EntityManager::FindById(id);
		const Entity* content = EntityManager::Get(handle);
		if (content == nullptr) {
			// 親も消していれば、親だけ書く（子は読むときに一緒に消える）
			if (!removedIds.contains(record.value("parent", EntityId{0}))) {
				removed.push_back(id);
			}
			removedIds.insert(id);
			continue;
		}
		SceneJson override = SceneJson::object();
		override["id"] = id;
		if (content->name != record.value("name", std::string("Entity"))) {
			override["name"] = content->name;
		}
		if (content->isActive != record.value("active", true)) {
			override["active"] = content->isActive;
		}
		// レイヤーは違うときだけ書く。Default に戻したときも 0 と書く（書かないと Prefab の値に戻ってしまう）
		if (content->layer != ReadEntityLayer(record)) {
			override[kLayerKey] = content->layer;
		}
		const Entity* contentParent = EntityManager::Get(content->parent);
		const EntityId parentId = (contentParent != nullptr) ? contentParent->id : EntityId{0};
		if (parentId != record.value("parent", EntityId{0})) {
			override["parent"] = parentId;
		}
		const SceneJson* base = PrefabRecords::FindComponents(record);
		SceneJson patches = SceneJson::object();
		SceneJson removedOne = SceneJson::array();
		PrefabDiff::DiffComponents((base != nullptr) ? *base : SceneJson::object(), SaveComponents(handle, warnings), patches, removedOne);
		if (!patches.empty()) {
			override["components"] = std::move(patches);
		}
		if (!removedOne.empty()) {
			override["removedComponents"] = std::move(removedOne);
		}
		if (override.size() > 1) {
			overrides.push_back(std::move(override)); // "id" だけなら、何も変えていない
		}
	}
	if (!overrides.empty()) {
		entry["overrides"] = std::move(overrides);
	}
	if (!removed.empty()) {
		entry["removed"] = std::move(removed);
	}
	return entry;
}

bool PrefabOverrides::HasOverrides(Handle<Entity> root) {
	std::set<std::string> warnings; // 調べるだけなので、警告は出さない（保存するときに出る）
	const SceneJson entry = MakeInstanceEntry(root, warnings);
	if (!entry.is_object()) {
		return false;
	}
	const SceneJson* components = PrefabRecords::FindComponents(entry);
	const bool hasRootOverride = (components != nullptr) && (components->size() > (components->contains(kTransformName) ? 1u : 0u));
	return hasRootOverride || entry.contains("removedComponents") || entry.contains("overrides") || entry.contains("removed");
}

void PrefabOverrides::KeepUnreadEntry(const SceneJson& entry) { unreadEntries[entry.value("id", EntityId{0})] = entry; }
