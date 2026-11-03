#include "SceneSerializer.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"
#include "MyEngine/Scene/Prefab/PrefabOverrides.h"
#include "MyEngine/Scene/Prefab/PrefabRecords.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr const char* kTransformName = "Transform"; // Transform の保存の名前（読めない Prefab から置いた物は、これだけ読む）

// 集めた警告を1回ずつ出す（同じComponentを100個持っていても、同じ文は1回だけ）
void FlushWarnings(const std::set<std::string>& warnings) {
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

//=============================================================================
// Entity → JSON
//=============================================================================
// ===== Entity 1つ分（{"id", "name", "parent", "active", "components"}）。id・parentId は書く番号（0は親なし）=====
// idMap を渡すと、Component の中の Entity への参照を表で付け替えて書く（Prefab に書くとき）
SceneJson SaveEntity(const Entity& entity, EntityId id, EntityId parentId, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr) {
	SceneJson components = SceneJson::object();
	ComponentSerializer::Save(entity.self, components, warnings, idMap);

	// 1つのEntityの中身は手元で組み立ててから入れる（ordered_map は途中で参照を持つと壊れるため）
	SceneJson item = SceneJson::object();
	item["id"] = id;
	item["name"] = entity.name;
	item["parent"] = parentId; // 親は EntityId で書く（0は親なし）
	item["active"] = entity.isActive;
	item["components"] = std::move(components);
	return item;
}

// ===== ファイルの一番外側（{"version", "entities"}）=====
SceneJson MakeRoot(SceneJson entities) {
	SceneJson root = SceneJson::object();
	root["version"] = SceneSerializer::kVersion;
	root["entities"] = std::move(entities);
	return root;
}

// ===== シーンに保存しない物か（自分か親のどれかに isDontSave の印がある）=====
bool IsDontSave(const Entity& entity) {
	for (const Entity* current = &entity; current != nullptr; current = EntityManager::Get(current->parent)) {
		if (current->isDontSave) {
			return true;
		}
	}
	return false;
}

// ===== Prefab から置いた物 1つ分（{"id", "name", "parent", "active", "prefab", "components", "overrides", "removed"}）=====
// 中身は書かない（読むときに Prefab のファイルから作り直す）。置いた所ごとに変えた所（上書き）だけを書く（PrefabOverrides::MakeInstanceEntry）
// shift・idMap を渡すと、番号を Prefab のファイルの番号にして書く（Prefab の中の入れ子。シーンなら 0 と nullptr）
SceneJson SaveInstance(const Entity& entity, EntityId shift, const EntityIdMap* idMap, std::set<std::string>& warnings) {
	SceneJson item = PrefabOverrides::MakeInstanceEntry(entity.self, warnings);
	return (idMap != nullptr) ? PrefabRecords::ShiftEntry(std::move(item), shift, *idMap) : item;
}

// ===== 全Entity。isFile なら、Prefab から置いた物は参照だけを書く（Play の退避は、シーンで直した所も戻せるよう中身も全部書く）=====
SceneJson SaveAll(bool isFile) {
	std::set<std::string> warnings;
	SceneJson entities = SceneJson::array();
	// 並んでいる順（Hierarchyに出る順）のまま書く。読むときも同じ順に作るので、並びが変わらない
	for (const Entity& entity : EntityManager::GetAll()) {
		if (IsDontSave(entity)) {
			continue; // エディタで編集中の画面の中身（画面のファイルに書く）
		}
		const Entity* parent = EntityManager::Get(entity.parent);
		const EntityId parentId = parent ? parent->id : EntityId{0};
		if (isFile) {
			if (PrefabManager::FindOwner(entity.self).IsValid()) {
				continue; // Prefab の中身（読むときに Prefab から作られる）
			}
			if (EntityManager::Get<PrefabInstanceComponent>(entity.self) != nullptr) {
				entities.push_back(SaveInstance(entity, 0, nullptr, warnings));
				continue;
			}
		}
		entities.push_back(SaveEntity(entity, entity.id, parentId, warnings));
	}
	FlushWarnings(warnings);
	return MakeRoot(std::move(entities));
}

//=============================================================================
// JSON → Entity（型が違う値を value() で読むと例外が出るので、呼ぶ側で受ける）
//=============================================================================
// ===== Prefab から置いた物を作る（中身は Prefab のファイルから。Prefab が読めなければ、印だけ付けた空の Entity）=====
// 一番上の名前・有効・Transform は、item（シーンファイルの1項目）のとおりになる
Handle<Entity> LoadInstance(const SceneJson& item, const std::string& path, std::set<std::string>& warnings) {
	const uint32_t prefab = PrefabManager::Load(ToPath(path));
	const Handle<Entity> root = PrefabManager::InstantiateNow(prefab, item, warnings);
	if (root.IsValid()) {
		return root;
	}
	// --- ファイルが無い（まだ pull していない など）。印と上書きは残すので、このまま保存しても参照・上書きは消えない ---
	warnings.insert(std::format("Prefab が読めないので、空の Entity にしました（ファイルが届いてからシーンを開き直すと、中身ができます）: {}", path));
	PrefabOverrides::KeepUnreadEntry(item);
	const Handle<Entity> empty = EntityManager::CreateWithId(item.value("id", EntityId{0}), item.value("name", std::string("Entity")), {});
	PrefabInstanceComponent link;
	link.path = path;
	EntityManager::RestoreComponents(empty, {ComponentSnapshot::Make(link)});
	// 置いた位置は残す（Transform だけ読む。ほかの Component は Prefab の中身なので、ここには無い）
	if (const auto components = item.find("components"); components != item.end() && components->is_object() && components->contains(kTransformName)) {
		SceneJson transform = SceneJson::object();
		transform[kTransformName] = components->at(kTransformName);
		ComponentSerializer::Load(empty, transform, warnings);
	}
	return empty;
}

bool LoadAll(const SceneJson& root) {
	const SceneJson* found = SceneSerializer::FindEntities(root);
	if (found == nullptr) {
		return false;
	}
	const SceneJson& entities = *found;
	std::set<std::string> warnings;

	// ===== 1周目：Entityを作る（親はまだ付けない。ファイルの並びが崩れていても困らないように、全部そろってから付ける）=====
	std::vector<Handle<Entity>> created(entities.size()); // 作れなかった所は無効なHandleのまま
	std::vector<bool> isInstance(entities.size());        // Prefab から置いた物か（Component はもう付いているので、3周目で読まない）
	for (size_t i = 0; i < entities.size(); ++i) {
		const SceneJson& item = entities[i];
		const EntityId id = item.value("id", EntityId{0});
		const std::string name = item.value("name", std::string("Entity"));
		if (id == 0 || EntityManager::FindById(id).IsValid()) {
			warnings.insert(std::format("EntityIdが0か、2つ目の同じ番号なので飛ばしました: {}", name));
			continue;
		}
		// "prefab" があれば Prefab から置いた物（中身も一緒にできる。一番上の Transform などもファイルの値になる）
		const auto prefab = item.find(kPrefabEntryKey);
		isInstance[i] = (prefab != item.end() && prefab->is_string());
		created[i] = isInstance[i] ? LoadInstance(item, prefab->get<std::string>(), warnings) : EntityManager::CreateWithId(id, name, {});
		EntityManager::Get(created[i])->isActive = item.value("active", true);
	}

	// ===== 2周目：親子をつなぐ =====
	for (size_t i = 0; i < entities.size(); ++i) {
		const EntityId parentId = entities[i].value("parent", EntityId{0});
		if (!created[i].IsValid() || parentId == 0) {
			continue;
		}
		const Handle<Entity> parent = EntityManager::FindById(parentId);
		if (!parent.IsValid()) {
			warnings.insert(std::format("親の番号 {} が見つからないので、一番上に置きました", parentId));
			continue;
		}
		EntityManager::SetParent(created[i], parent); // 輪になる親子は SetParent が断る
	}

	// ===== 3周目：Componentを付ける（全Entityがそろった後なので、Componentの中のEntityへの参照も引ける）=====
	for (size_t i = 0; i < entities.size(); ++i) {
		const auto componentsIt = entities[i].find("components");
		if (!created[i].IsValid() || isInstance[i] || componentsIt == entities[i].end() || !componentsIt->is_object()) {
			continue;
		}
		ComponentSerializer::Load(created[i], *componentsIt, warnings);
	}
	FlushWarnings(warnings);
	return true;
}

// 読み込みの入口。失敗したら作りかけのEntityを消して、何も無かったことにする
bool LoadSafely(const SceneJson& root) {
	try {
		if (LoadAll(root)) {
			return true;
		}
	} catch (const SceneJson::exception& exception) {
		// "id": "abc" のように、形の違う値があったとき
		LogManager::Error(std::format("シーンの読み込みに失敗しました: {}", exception.what()));
	}
	SceneSerializer::DestroyAllNow();
	return false;
}

// 1つの値を1行の文字列にする（名前に壊れた文字が混じっていても、例外で落とさず置き換える）
std::string ToLine(const SceneJson& json) { return json.dump(-1, ' ', false, SceneJson::error_handler_t::replace); }

// ファイル用に、人が読める形で書く。タブで字下げし、数だけの配列（Vector3 など）は1行にまとめる
// （nlohmann の dump で字下げすると1要素1行になり、Transform1つで15行になってしまう）
void WriteReadable(const SceneJson& json, int depth, std::string& out) {
	const std::string indent(static_cast<size_t>(depth) + 1, '\t');  // 中身の字下げ
	const std::string closeIndent(static_cast<size_t>(depth), '\t'); // 閉じかっこの字下げ
	const bool isNumberArray = json.is_array() && std::all_of(json.begin(), json.end(), [](const SceneJson& element) { return element.is_number(); });

	if (json.is_object() && !json.empty()) {
		// --- { "キー": 値, ... } は1項目1行 ---
		out += "{\n";
		for (auto it = json.begin(); it != json.end(); ++it) {
			out += indent + ToLine(SceneJson(it.key())) + ": ";
			WriteReadable(*it, depth + 1, out);
			out += (std::next(it) == json.end()) ? "\n" : ",\n";
		}
		out += closeIndent + "}";
	} else if (isNumberArray) {
		// --- [1.0, 2.0, 3.0] は1行 ---
		out += "[";
		for (auto it = json.begin(); it != json.end(); ++it) {
			out += ToLine(*it);
			out += (std::next(it) == json.end()) ? "" : ", ";
		}
		out += "]";
	} else if (json.is_array() && !json.empty()) {
		// --- それ以外の配列（Entityの並びなど）は1要素ずつ ---
		out += "[\n";
		for (auto it = json.begin(); it != json.end(); ++it) {
			out += indent;
			WriteReadable(*it, depth + 1, out);
			out += (std::next(it) == json.end()) ? "\n" : ",\n";
		}
		out += closeIndent + "]";
	} else {
		out += ToLine(json); // 数・文字・true/false・空の {} []
	}
}
} // namespace

//=============================================================================
// ファイル
//=============================================================================
bool SceneSerializer::SaveFile(const std::string& path) {
	if (!WriteFile(path, SaveAll(true))) {
		return false;
	}
	LogManager::Log(std::format("シーンを保存しました: {}", path));
	return true;
}

bool SceneSerializer::WriteFile(const std::string& path, const SceneJson& root) {
	const std::filesystem::path filePath = ToPath(path); // UTF-8 の文字から（日本語のフォルダでも化けないように）
	std::error_code error;                               // 例外ではなくエラーコードで受ける
	std::filesystem::create_directories(filePath.parent_path(), error);
	// 前のファイルを1つだけ残す（書いている途中で落ちても、1つ前には戻れるように）
	if (std::filesystem::exists(filePath, error)) {
		std::filesystem::path backupPath = filePath;
		backupPath += ".bak"; // += は、パスの最後にそのまま文字を足す（/ で区切らない）
		std::filesystem::copy_file(filePath, backupPath, std::filesystem::copy_options::overwrite_existing, error);
	}
	std::ofstream stream(filePath);
	if (!stream) {
		LogManager::Error(std::format("ファイルを開けませんでした: {}", path));
		return false;
	}
	std::string text;
	WriteReadable(root, 0, text); // gitの差分で見やすいように、タブで字下げして書く
	stream << text << '\n';
	stream.close();
	if (stream.fail()) {
		LogManager::Error(std::format("ファイルを書き切れませんでした（1つ前は .bak にあります）: {}", path));
		return false;
	}
	return true;
}

SceneLoadResult SceneSerializer::LoadFile(const std::string& path) {
	std::error_code error;
	// 無い・中身が空（Project ウィンドウで作ったばかりの、まだ保存していないシーン）なら、最初の配置から作ってもらう
	if (!std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0) {
		return SceneLoadResult::NotFound;
	}
	// git pull で Prefab が届いていたら、新しい中身から作る
	PrefabManager::ReloadChangedFiles();
	std::ifstream stream(path);
	const SceneJson root = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	stream.close();
	const bool isLoaded = !root.is_discarded() && root.is_object() && LoadSafely(root);
	if (!isLoaded) {
		// 読めなかったファイルを別の名前で残す（このまま Save を2回押すと、本体も .bak も空のシーンで上書きされてしまうので）
		std::filesystem::copy_file(path, path + ".broken", std::filesystem::copy_options::overwrite_existing, error);
		LogManager::Error(std::format("シーンファイルを読めませんでした（壊れている・新しい版で保存された）。{}.broken に写しました", path));
		return SceneLoadResult::Failed;
	}
	LogManager::Log(std::format("シーンを読み込みました: {}（{}個）", path, EntityManager::GetCount()));
	return SceneLoadResult::Loaded;
}

//=============================================================================
// 文字列
//=============================================================================
std::string SceneSerializer::SaveToText() { return ToLine(SaveAll(false)); } // 1行に詰める（人が読まないので）

bool SceneSerializer::LoadFromText(const std::string& text) {
	const SceneJson root = SceneJson::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error("退避したシーンが読めませんでした");
		return false;
	}
	return LoadSafely(root);
}

//=============================================================================
// Entity の一部分（Prefab）
//=============================================================================
SceneJson SceneSerializer::SaveTree(Handle<Entity> root, EntityId shift) {
	// ===== root から幅優先でたどる（先に見つけた物から並べるので、親が必ず子より前になる。EditorHistory の写しと同じ）=====
	std::vector<Handle<Entity>> queue = {root};
	std::vector<Handle<Entity>> children;
	for (size_t i = 0; i < queue.size(); ++i) {
		EntityManager::GetChildren(queue[i], children);
		queue.insert(queue.end(), children.begin(), children.end());
	}
	// ===== 番号の付け替え表（中の Entity の番号 → 書く番号）。中の相手への参照も、同じ番号に直して書く =====
	EntityIdMap idMap;
	for (const Handle<Entity> handle : queue) {
		if (const Entity* entity = EntityManager::Get(handle)) {
			idMap[entity->id] = entity->id ^ shift;
		}
	}
	// ===== 書く =====
	std::set<std::string> warnings;
	SceneJson entities = SceneJson::array();
	for (size_t i = 0; i < queue.size(); ++i) {
		const Entity* entity = EntityManager::Get(queue[i]);
		if (entity == nullptr) {
			continue;
		}
		// root の親は書かない（Prefab の外なので。作るときに Instantiate の parent を付ける）
		const Entity* parent = EntityManager::Get(entity->parent);
		const EntityId parentId = (i == 0 || parent == nullptr) ? EntityId{0} : (parent->id ^ shift);
		// --- 中の入れ子（別の Prefab から置いた物）は、シーンファイルと同じく参照だけを書く ---
		if (i > 0) {
			// 入れ子の中身は書かない（作るときに、その Prefab から作られる）。持ち主が root なら root の中身なので書く。
			// 持ち主が root より上（root 自身が入れ子のときの外側）なら、この Prefab の中身として書く
			const Entity* owner = EntityManager::Get(PrefabManager::FindOwner(queue[i]));
			if (owner != nullptr && owner->self != root && idMap.contains(owner->id)) {
				continue;
			}
			if (EntityManager::Get<PrefabInstanceComponent>(queue[i]) != nullptr) {
				entities.push_back(SaveInstance(*entity, shift, &idMap, warnings));
				continue;
			}
		}
		SceneJson item = SaveEntity(*entity, entity->id ^ shift, parentId, warnings, &idMap);
		if (i == 0) {
			item["components"].erase(kPrefabInstanceComponentName); // root の「置いた物の印」は書かない（Prefab の中に、自分を指す印が入ってしまう）
		}
		entities.push_back(std::move(item));
	}
	FlushWarnings(warnings);
	return MakeRoot(std::move(entities));
}

const SceneJson* SceneSerializer::FindEntities(const SceneJson& root) {
	// --- 版を確かめる（新しい版は、どこが変わったか分からないので読まない）---
	const auto versionIt = root.find("version");
	const int version = (versionIt != root.end() && versionIt->is_number_integer()) ? versionIt->get<int>() : 0;
	if (version < 1 || version > kVersion) {
		LogManager::Error(std::format("このエンジンでは読めない版です（ファイル: {} / エンジン: {}）", version, kVersion));
		return nullptr;
	}
	// 版を上げたら、ここで古い版のJSONを今の形に直してから読む（例: if (version < 2) { 回転をクオータニオンに直す }）
	const auto entitiesIt = root.find("entities");
	if (entitiesIt == root.end() || !entitiesIt->is_array()) {
		LogManager::Error("\"entities\" がありません");
		return nullptr;
	}
	return &*entitiesIt;
}

//=============================================================================
// 全Entityを今すぐ消す
//=============================================================================
void SceneSerializer::DestroyAllNow() {
	// Script の OnDestroy の中で Entity が作られることがある（消えるときに出す煙など）。
	// 残ると次のシーン（Stop なら編集中のシーン）に混ざって保存までされるので、空になるまで繰り返す
	// （作られたばかりの Entity の Script は Awake 前なので OnDestroy は呼ばれず、ふつうは2回目で空になる）
	constexpr int kMaxRounds = 8;
	std::vector<Handle<Entity>> roots;
	for (int round = 0; round < kMaxRounds; ++round) {
		EntityManager::GetRoots(roots);
		if (roots.empty()) {
			return;
		}
		for (Handle<Entity> root : roots) {
			EntityManager::Destroy(root); // 子も一緒に消える
		}
		EntityManager::FlushDestroy(); // 予約をその場で反映する（フレームの境目なので安全。EditorHistoryのUndoと同じ使い方）
	}
	LogManager::Warning("OnDestroy の中で Entity を作り続けているので、全部は消せませんでした");
}