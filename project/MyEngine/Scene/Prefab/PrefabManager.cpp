#include "PrefabManager.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Scene/Prefab/PrefabRecords.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"


namespace {
constexpr const char* kTransformName = "Transform"; // Transform の保存の名前（作るときに、これだけ先に付ける）

/// <summary>
/// 読み込んだ Prefab 1つ分
/// </summary>
struct PrefabAsset {
	std::string path;                          // パス（UTF-8。ToPathKey の形。読み込み済みかを調べるキーにもなる）
	std::shared_ptr<const SceneJson> entities; // Entity の並び（ファイルの "entities"）。保存し直すと、新しい物に差し替わる
	std::filesystem::file_time_type writeTime; // 読んだ・書いたときのファイルの時刻（ReloadChangedFiles で比べる）
	// 中身の番号（この Prefab のファイルの番号。入れ子の Prefab の中身も、この Prefab の番号にして入れる）。置いた物の中身かを調べる
	// 入れ子の Prefab が書き換わると変わるので、使うときに作る（contentGeneration が PrefabState::generation と違えば作り直す）
	std::unordered_set<EntityId> contentIds;
	uint64_t contentGeneration = 0;
};

/// <summary>
/// 作ったけれど、まだ Transform 以外の Component を付けていない物（次のフレームの頭の Flush で付ける）
/// </summary>
struct PendingLoad {
	uint32_t prefab = 0;                          // どの Prefab から作ったか（印に書く）
	std::shared_ptr<const EntityRecords> records; // 作ったときの中身（置いた番号。途中で Prefab を保存し直しても、作ったときの中身で付ける）
	std::vector<Handle<Entity>> created;          // records の i 番目から作った Entity
};

/// <summary>
/// PrefabManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PrefabState {
	std::vector<PrefabAsset> assets;       // 読み込んだ Prefab（番号 - 1 が添字。番号は1から、0は未選択）
	std::vector<PendingLoad> pendingLoads; // Component を付ける予約
	std::vector<uint32_t> placeRequests;   // エディタから置く予約
	uint64_t generation = 1;               // どれかの Prefab の中身が変わるたびに増える（PrefabAsset::contentIds を作り直す合図）
};
std::unique_ptr<PrefabState> state;

//=============================================================================
// 小さな道具
//=============================================================================
// 番号から Prefab（知らない番号なら nullptr）
// 返したポインタは、次に Load するまでに使い終える（Load が assets を増やすと、指している場所が変わる）
PrefabAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// 集めた警告を1回ずつ出す（同じ Prefab を何個作っても、同じ文は1回だけ）
void FlushWarnings(const std::set<std::string>& warnings) {
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

// Entity 1つ分の形が正しいか（読むときに1回だけ確かめる。作るたびに value() で形の違う値を読むと例外が出るので）
bool IsValidItem(const SceneJson& item) {
	if (!item.is_object()) {
		return false;
	}
	// --- 番号は必ず要る（0は「無し」なので使えない）---
	const auto id = item.find("id");
	if (id == item.end() || !id->is_number_unsigned() || id->get<EntityId>() == 0) {
		return false;
	}
	// --- ほかは、あれば形を確かめる（無ければ作るときに初期値）---
	const auto isOptional = [&item](const char* key, bool (SceneJson::*isExpected)() const noexcept) {
		const auto it = item.find(key);
		return it == item.end() || ((*it).*isExpected)();
	};
	return isOptional("name", &SceneJson::is_string) && isOptional("parent", &SceneJson::is_number_unsigned) && isOptional("active", &SceneJson::is_boolean) &&
	       isOptional("components", &SceneJson::is_object) && isOptional(kPrefabEntryKey, &SceneJson::is_string);
}

// ファイルを読んで、Entity の並びを取り出す（読めなければ nullptr）
std::shared_ptr<const SceneJson> ReadEntities(const std::filesystem::path& filePath) {
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Prefab のファイルを開けませんでした: {}", ToUtf8(filePath)));
		return nullptr;
	}
	const SceneJson root = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error(std::format("Prefab のファイルが壊れています: {}", ToUtf8(filePath)));
		return nullptr;
	}
	const SceneJson* entities = SceneSerializer::FindEntities(root);
	if (entities == nullptr || entities->empty()) {
		LogManager::Error(std::format("Prefab に Entity がありません: {}", ToUtf8(filePath)));
		return nullptr;
	}
	if (!std::all_of(entities->begin(), entities->end(), &IsValidItem)) {
		LogManager::Error(std::format("Prefab の Entity の形が違います（\"id\" が無い・数でない など）: {}", ToUtf8(filePath)));
		return nullptr;
	}
	return std::make_shared<const SceneJson>(*entities);
}

// ファイルの時刻（読めなければ、いちばん古い時刻）
std::filesystem::file_time_type WriteTimeOf(const std::string& path) {
	std::error_code error; // 例外ではなくエラーコードで受ける
	const std::filesystem::file_time_type time = std::filesystem::last_write_time(ToPath(path), error);
	return error ? std::filesystem::file_time_type{} : time;
}

// 中身を差し替える（入れ子にしている Prefab の中身の番号も変わるので、全部の Prefab の contentIds を作り直させる）
void SetEntities(PrefabAsset& asset, std::shared_ptr<const SceneJson> entities) {
	asset.entities = std::move(entities);
	++state->generation;
}

//=============================================================================
// 番号のずれ（置いた物の番号 = ファイルの番号 XOR ずれ）
//=============================================================================
// ファイルの一番上の Entity の番号
EntityId RootFileId(const SceneJson& entities) { return entities.front().at("id").get<EntityId>(); }

// 一番上を rootId の番号で置いたときの、ずれ（一番上は ファイルの番号 XOR ずれ = rootId になる）
EntityId ShiftOf(const PrefabAsset& asset, EntityId rootId) { return rootId ^ RootFileId(*asset.entities); }

//=============================================================================
// 中身の番号（置いた物の中身かを調べる）
//=============================================================================
const std::unordered_set<EntityId>& ContentIdsOf(uint32_t handle, std::vector<uint32_t>& stack);

// ===== Prefab の中身の番号（ファイルの番号）を集める。入れ子の Prefab は、その中身をこの Prefab の番号にして入れる =====
// stack は集めている途中の Prefab（自分の中に自分が入っていたら、そこで止める）
std::unordered_set<EntityId> CollectContentIds(uint32_t handle, std::vector<uint32_t>& stack) {
	std::unordered_set<EntityId> ids;
	const PrefabAsset* asset = Find(handle);
	if (asset == nullptr || asset->entities == nullptr || std::ranges::find(stack, handle) != stack.end() || stack.size() >= kMaxPrefabNestDepth) {
		return ids;
	}
	const std::shared_ptr<const SceneJson> entities = asset->entities; // 入れ子の Load で asset の場所が変わるので、中身を持っておく
	stack.push_back(handle);
	for (const SceneJson& item : *entities) {
		const EntityId id = item.at("id").get<EntityId>();
		ids.insert(id);
		const auto prefab = item.find(kPrefabEntryKey);
		if (prefab == item.end()) {
			continue;
		}
		// --- 入れ子：中身の番号 = 入れ子のファイルの番号 XOR（この項目の番号 XOR 入れ子のファイルの一番上の番号）---
		const uint32_t child = PrefabManager::Load(ToPath(prefab->get<std::string>()));
		const PrefabAsset* childAsset = Find(child);
		if (childAsset == nullptr || childAsset->entities == nullptr) {
			continue; // 読めない（ファイルがまだ無い）。一番上だけが中身
		}
		const EntityId shift = id ^ RootFileId(*childAsset->entities);
		for (const EntityId childId : ContentIdsOf(child, stack)) {
			ids.insert(childId ^ shift);
		}
	}
	stack.pop_back();
	return ids;
}

// ===== 中身の番号（前に集めた物があれば、それを使う）=====
// 返した参照は、次に Load するまでに使い終える
const std::unordered_set<EntityId>& ContentIdsOf(uint32_t handle, std::vector<uint32_t>& stack) {
	static const std::unordered_set<EntityId> kEmpty; // 知らない番号のときに返す空の集まり（参照で返すので static にする）
	PrefabAsset* asset = Find(handle);
	if (asset == nullptr) {
		return kEmpty;
	}
	if (asset->contentGeneration != state->generation) {
		std::unordered_set<EntityId> ids = CollectContentIds(handle, stack);
		asset = Find(handle); // 入れ子の Load で場所が変わったかもしれないので、引き直す
		asset->contentIds = std::move(ids);
		asset->contentGeneration = state->generation;
	}
	return asset->contentIds;
}

//=============================================================================
// 作る
//=============================================================================
// ===== 並べた中身（PrefabRecords::Expand）から Entity を作り、親子をつなぐ（Component はまだ。Transform は Entity と一緒にできている）=====
PendingLoad CreateEntities(uint32_t prefab, std::shared_ptr<const EntityRecords> records, Handle<Entity> parent) {
	PendingLoad pending;
	pending.prefab = prefab;
	pending.records = std::move(records);
	const EntityRecords& items = *pending.records;
	pending.created.reserve(items.size());

	// --- 1周目：Entity を作る（番号はもう置いた番号になっている）---
	for (const SceneJson& item : items) {
		const Handle<Entity> created = EntityManager::CreateWithId(item.at("id").get<EntityId>(), item.value("name", std::string("Entity")), {});
		EntityManager::Get(created)->isActive = item.value("active", true);
		pending.created.push_back(created);
	}

	// --- 2周目：親子をつなぐ ---
	// 先頭（一番上）は parent の子に。それ以外は、書いてある親（見つからなければ一番上）の子にする
	const Handle<Entity> root = pending.created.front();
	for (size_t i = 0; i < items.size(); ++i) {
		Handle<Entity> newParent = parent;
		if (i > 0) {
			const Handle<Entity> found = EntityManager::FindById(items[i].value("parent", EntityId{0}));
			newParent = found.IsValid() ? found : root;
		}
		if (newParent.IsValid()) {
			EntityManager::SetParent(pending.created[i], newParent);
		}
	}
	return pending;
}

// ===== Component を付ける。isTransform なら Transform だけ、そうでなければ Transform 以外を全部 =====
// Transform は Entity を作ったときから持っているので、上書きするだけ（Component の配列は増えない＝フレームの途中でも安全）
// ほかの Component は配列が増えるので、フレームの境目でだけ付ける
void LoadComponents(const PendingLoad& pending, bool isTransform, std::set<std::string>& warnings) {
	for (size_t i = 0; i < pending.created.size(); ++i) {
		if (!EntityManager::IsAlive(pending.created[i])) {
			continue; // 作ってから今までに消された（Destroy・Stop）
		}
		const SceneJson* components = PrefabRecords::FindComponents((*pending.records)[i]);
		if (components == nullptr) {
			continue;
		}
		SceneJson part = SceneJson::object();
		if (isTransform) {
			if (const auto transform = components->find(kTransformName); transform != components->end()) {
				part[kTransformName] = *transform;
			}
		} else {
			part = *components;
			part.erase(kTransformName);
		}
		// 番号はもう置いた番号なので、付け替えずに読む（中身どうしの参照も、相手はもう作ってある）
		ComponentSerializer::Load(pending.created[i], part, warnings);
	}
}

// ===== 一番上に「置いた物の印」を付ける（Component が増えるので、フレームの境目でだけ呼ぶ）=====
void Link(const PendingLoad& pending) {
	const Handle<Entity> root = pending.created.front();
	if (!EntityManager::IsAlive(root)) {
		return;
	}
	PrefabInstanceComponent link;
	link.path = PrefabManager::GetPath(pending.prefab);
	link.prefab = pending.prefab;
	EntityManager::RestoreComponents(root, {ComponentSnapshot::Make(link)});
}

// ===== 置いた物の一番上から、元の Prefab（印が無い・読めていない Prefab なら nullptr）=====
const PrefabAsset* FindAssetOf(Handle<Entity> root) {
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	const PrefabAsset* asset = (link != nullptr) ? Find(link->prefab) : nullptr;
	return (asset != nullptr && asset->entities != nullptr) ? asset : nullptr;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void PrefabManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<PrefabState>();
}

void PrefabManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t PrefabManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &PrefabAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む ---
	std::shared_ptr<const SceneJson> entities = ReadEntities(filePath);
	if (entities == nullptr) {
		return 0; // 止めずに未選択の番号を返す
	}
	const size_t count = entities->size();
	PrefabAsset asset;
	asset.path = key;
	asset.writeTime = WriteTimeOf(key);
	SetEntities(asset, std::move(entities));
	state->assets.push_back(std::move(asset));
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("読み込み完了 \"{}\" -> handle={} Entity数={}", key, handle, count));
	return handle;
}

const std::string& PrefabManager::GetPath(uint32_t handle) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	const PrefabAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmpty;
}

std::shared_ptr<const SceneJson> PrefabManager::GetEntities(uint32_t handle) {
	const PrefabAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->entities : nullptr;
}

void PrefabManager::ReloadChangedFiles() {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	for (PrefabAsset& asset : state->assets) {
		const std::filesystem::file_time_type time = WriteTimeOf(asset.path);
		if (time == asset.writeTime) {
			continue;
		}
		// 読めなかったら前の中身のまま（書いている途中のファイルを読んだときなど）。時刻は進めて、毎回読みに行かないようにする
		asset.writeTime = time;
		if (std::shared_ptr<const SceneJson> entities = ReadEntities(ToPath(asset.path))) {
			SetEntities(asset, std::move(entities));
			LogManager::Log(std::format("Prefab が書き換わっていたので読み直しました: {}", asset.path));
		}
	}
}

//=============================================================================
// 保存
//=============================================================================
bool PrefabManager::Save(Handle<Entity> root, const std::string& path) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	const Entity* rootEntity = EntityManager::Get(root);
	if (rootEntity == nullptr) {
		return false;
	}
	const std::string key = ToPathKey(ToPath(path));
	const auto cached = std::ranges::find(state->assets, key, &PrefabAsset::path);
	const uint32_t cachedHandle = (cached != state->assets.end()) ? static_cast<uint32_t>(cached - state->assets.begin()) + 1 : 0;

	// --- この Prefab から置いた物なら、ずれを戻して書く（ファイルの番号が前と同じになる）。ほかは今の番号のまま書く ---
	const PrefabAsset* source = FindAssetOf(root);
	const EntityId shift = (source != nullptr && source == Find(cachedHandle)) ? ShiftOf(*source, rootEntity->id) : EntityId{0};
	const SceneJson json = SceneSerializer::SaveTree(root, shift);
	if (!SceneSerializer::WriteFile(path, json)) {
		return false;
	}

	// --- もう読み込んでいる Prefab なら、中身を差し替える。まだなら読み込む ---
	// このあと作る物から新しい中身になる。作りかけの物（PendingLoad）は前の中身を shared_ptr で持っているので、そのまま付け終われる
	uint32_t handle = cachedHandle;
	if (PrefabAsset* asset = Find(handle)) {
		SetEntities(*asset, std::make_shared<const SceneJson>(json.at("entities")));
		asset->writeTime = WriteTimeOf(key); // 自分で書いた分は、ReloadChangedFiles で読み直さない
	} else {
		handle = Load(ToPath(path));
	}
	LogManager::Log(std::format("Prefab を保存しました: {}（Entity {}個）", path, json.at("entities").size()));

	// --- 保存した物を、この Prefab から置いた物にする（印が無ければ次のフレームの頭に付く。ImGui を描いている途中で呼ばれるので）---
	PrefabInstanceComponent link;
	link.path = GetPath(handle);
	link.prefab = handle;
	if (PrefabInstanceComponent* now = EntityManager::Get<PrefabInstanceComponent>(root)) {
		*now = link;
	} else {
		EntityManager::RequestAdd(root, link);
	}
	return true;
}

bool PrefabManager::IsOtherPrefabFile(Handle<Entity> root, const std::string& path) {
	std::error_code error; // 例外ではなくエラーコードで受ける
	if (!std::filesystem::exists(ToPath(path), error)) {
		return false; // まだ無いファイル（新しく作る）
	}
	// 同じファイルから置いた物なら、書いても Apply と同じ（自分の Prefab を書き直すだけ）
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link == nullptr) || (ToPathKey(ToPath(link->path.ToString())) != ToPathKey(ToPath(path)));
}

//=============================================================================
// 作る
//=============================================================================
Handle<Entity> PrefabManager::Instantiate(uint32_t prefab, const Vector3& position, Handle<Entity> parent) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	if (Find(prefab) == nullptr) {
		LogManager::Warning(std::format("読み込んでいない Prefab の番号 {} から作ろうとしました（Inspector で Prefab を選んでいるか確かめてください）", prefab));
		return {};
	}
	// ===== 中身を並べる（一番上の番号は新しくもらう。中の誰かがほかの Entity とぶつかったら、もらい直す）=====
	std::set<std::string> warnings;
	EntityRecords records;
	do {
		records = PrefabRecords::Expand(prefab, EntityManager::NewId(), warnings);
	} while (!records.empty() && !PrefabRecords::AreIdsFree(records));
	if (records.empty()) {
		FlushWarnings(warnings);
		return {};
	}

	// ===== Entity を作り、Transform だけ今すぐ付ける =====
	PendingLoad pending = CreateEntities(prefab, std::make_shared<const EntityRecords>(std::move(records)), parent);
	LoadComponents(pending, true, warnings);
	FlushWarnings(warnings);
	// 一番上の位置を、渡された位置にする（回転・大きさは Prefab のまま）
	const Handle<Entity> root = pending.created.front();
	if (TransformComponent* transform = EntityManager::Get<TransformComponent>(root)) {
		transform->translation = position;
	}

	// ===== ほかの Component・Script・印は、次のフレームの頭に付ける =====
	state->pendingLoads.push_back(std::move(pending));
	return root;
}

Handle<Entity> PrefabManager::InstantiateNow(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// --- 中身を並べ、ファイルの項目（一番上の名前・有効・Transform・上書きなど）を当てる ---
	EntityRecords records = PrefabRecords::Expand(prefab, entry.value("id", EntityId{0}), warnings);
	if (records.empty()) {
		return {};
	}
	PrefabRecords::ApplyEntry(records, 0, records.size(), entry, warnings);
	// --- 中の誰かがほかの Entity とぶつかるなら、全員を新しい番号にずらす（シーンのほかの物からの参照は外れる）---
	if (!PrefabRecords::AreIdsFree(records)) {
		warnings.insert(std::format("Prefab の中の番号がほかの Entity とぶつかったので、新しい番号で作りました（この物への参照は外れます）: {}", GetPath(prefab)));
		const EntityId rootId = records.front().at("id").get<EntityId>();
		EntityRecords moved;
		do {
			moved = records;
			PrefabRecords::Relabel(moved, EntityManager::NewId() ^ rootId);
		} while (!PrefabRecords::AreIdsFree(moved));
		records = std::move(moved);
	}
	// --- 作って、Component・印までその場で付ける（フレームの境目なので安全）---
	const PendingLoad pending = CreateEntities(prefab, std::make_shared<const EntityRecords>(std::move(records)), {});
	LoadComponents(pending, true, warnings);
	LoadComponents(pending, false, warnings);
	Link(pending);
	return pending.created.front();
}

void PrefabManager::RequestPlace(uint32_t prefab) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	state->placeRequests.push_back(prefab);
}

//=============================================================================
// フレームの頭
//=============================================================================
void PrefabManager::Flush() {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// ===== エディタから置く予約（作るだけ。Component は下でまとめて付く）=====
	// 取り出してから回す（回している間に予約が足されても壊れないように）
	const std::vector<uint32_t> places = std::exchange(state->placeRequests, {});
	for (const uint32_t prefab : places) {
		Instantiate(prefab, {0.0f, 0.0f, 0.0f});
	}

	// ===== Component・Script・印を付ける（Transform は作ったときに付けたので飛ばす）=====
	// フレームの境目なので、予約せずその場で付けてよい（シーンを読むときと同じ）
	const std::vector<PendingLoad> loads = std::exchange(state->pendingLoads, {});
	std::set<std::string> warnings;
	for (const PendingLoad& pending : loads) {
		LoadComponents(pending, false, warnings);
		Link(pending);
	}
	FlushWarnings(warnings);
}

//=============================================================================
// 置いた物
//=============================================================================
Handle<Entity> PrefabManager::FindOwner(Handle<Entity> entity) {
	const Entity* target = EntityManager::Get(entity);
	if (target == nullptr) {
		return {};
	}
	// 親の方へたどり、印のある物の中身か（自分の番号 XOR ずれが、中身の番号にあるか）を調べる。入れ子なら、一番近い置いた物が持ち主
	std::vector<uint32_t> stack;
	Handle<Entity> current = target->parent;
	while (const Entity* ancestor = EntityManager::Get(current)) {
		const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(current);
		if (const PrefabAsset* asset = (link != nullptr) ? Find(link->prefab) : nullptr; asset != nullptr && asset->entities != nullptr) {
			const EntityId fileId = target->id ^ ShiftOf(*asset, ancestor->id); // asset は ContentIdsOf の中の Load で場所が変わるので、先に使う
			if (ContentIdsOf(link->prefab, stack).contains(fileId)) {
				return current;
			}
		}
		current = ancestor->parent;
	}
	return {};
}

Handle<Entity> PrefabManager::FindInstanceRoot(Handle<Entity> entity) {
	const Handle<Entity> root = (EntityManager::Get<PrefabInstanceComponent>(entity) != nullptr) ? entity : FindOwner(entity);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link != nullptr && IsAssetFile(link->path.View(), AssetType::Prefab)) ? root : Handle<Entity>{};
}

bool PrefabManager::Apply(Handle<Entity> root) {
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link != nullptr) && Save(root, link->path.ToString());
}

void PrefabManager::Unpack(Handle<Entity> root) {
	if (EntityManager::RequestRemove<PrefabInstanceComponent>(root)) {
		LogManager::Log("Prefab とのつながりを切りました（これからは中身もシーンファイルに書かれます）");
	}
}
