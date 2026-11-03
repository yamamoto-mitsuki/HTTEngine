#include "EditorHistory.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"
#include "MyEngine/Scene/Prefab/PrefabOverrides.h"
#include "MyEngine/Systems/Gameplay/ScriptSystem.h"

namespace {
//=============================================================================
// 履歴
//=============================================================================
// 履歴1件＝「戻す処理」と「やり直す処理」の組。対象が見つからないなどで失敗したらfalseを返す
struct Entry {
	std::function<bool()> undo;
	std::function<bool()> redo;
};

constexpr size_t kMaxHistory = 128; // 覚えておく操作の数（超えたら古いものから捨てる）

std::vector<Entry> history;                  // 古い順に並ぶ
size_t cursor = 0;                           // 適用済みの件数。[0, cursor) がUndoでき、[cursor, size) がRedoできる
std::vector<std::function<void()>> requests; // 次のFlushで実行する操作（頼まれた順）
uint64_t changeCount = 0;                    // 履歴が動いた回数（積んだ・戻した・やり直した・捨てた）。GetChangeCount で渡す
// Suspend で脇へよけた履歴（Resume で戻す）
std::vector<Entry> suspendedHistory;
size_t suspendedCursor = 0;
bool isSuspended = false;

//=============================================================================
// Entityの写し（削除のUndo・コピー・貼り付け用）
//=============================================================================
struct EntityData {
	EntityId id = 0;
	EntityId parent = 0; // 0ならroot
	std::string name;
	bool isActive = true;
	std::vector<ComponentSnapshot> components; // 持っているComponent全部（Inspectorに出していない型も含む）
	std::string scripts;  // 付いている Script 全部（バイト列では写せないので、保存と同じJSON の文字で）
};
using EntityTree = std::vector<EntityData>; // [0]が根。親は必ず子より前に並ぶ

EntityTree clipboard; // コピーした物（空なら何も無い）

//=============================================================================
// Inspectorで編集中のComponent
//=============================================================================
// マウスを離すまでの変更を、Component1つにつき1個にまとめて持つ
struct ComponentEdit {
	EntityId entity = 0;
	const ComponentEditor* editor = nullptr;
	std::vector<std::byte> before; // 編集開始時のComponent全体
	std::vector<std::byte> after;  // 最後にInspectorで変更した時点のComponent全体
};
std::vector<ComponentEdit> pendingEdits;

//=============================================================================
// 履歴の出し入れ
//=============================================================================
EntityId IdOf(Handle<Entity> handle) {
	const Entity* entity = EntityManager::Get(handle);
	return entity ? entity->id : 0;
}

// ===== もう適用してある操作を、履歴の末尾に足す（Redoできる分は捨てる）=====
void Append(Entry entry) {
	history.erase(history.begin() + static_cast<std::ptrdiff_t>(cursor), history.end());
	history.push_back(std::move(entry));
	if (history.size() > kMaxHistory) {
		history.erase(history.begin());
	}
	cursor = history.size();
	++changeCount;
}

// ===== まだ適用していない操作を、実行してから履歴に足す（実行できなければ何もしない）=====
void Execute(Entry entry) {
	if (entry.redo()) {
		Append(std::move(entry));
	}
}

// ===== Undo・Redoが失敗した＝履歴と今のシーンが食い違った。続けると関係ない物を書き換えかねないので、履歴を捨てる =====
void Failed() {
	history.clear();
	cursor = 0;
	++changeCount;
	LogManager::Warning("Undo/Redoの対象が見つからなかったので、履歴を消しました");
}

//=============================================================================
// Entityの写しを作る・戻す・消す
//=============================================================================
// ===== Entity1つ分。EntityManagerに登録されている全種類のComponentを見て、持っている物を写す =====
EntityData CaptureOne(Handle<Entity> handle) {
	const Entity* entity = EntityManager::Get(handle);
	EntityData data;
	data.id = entity->id;
	data.parent = IdOf(entity->parent);
	data.name = entity->name;
	data.isActive = entity->isActive;
	EntityManager::CaptureComponents(handle, data.components);
	data.scripts = ScriptSystem::Capture(handle);
	return data;
}

// ===== rootと、その子孫をまとめて写す（幅優先なので、親が必ず子より前に並ぶ）=====
EntityTree Capture(Handle<Entity> root) {
	EntityTree tree;
	if (!EntityManager::IsAlive(root)) {
		return tree;
	}
	std::vector<Handle<Entity>> queue = {root};
	std::vector<Handle<Entity>> children;
	for (size_t i = 0; i < queue.size(); ++i) {
		tree.push_back(CaptureOne(queue[i]));
		EntityManager::GetChildren(queue[i], children);
		queue.insert(queue.end(), children.begin(), children.end());
	}
	return tree;
}

// ===== 写しからEntityを作り直す。同じEntityIdで作るので、ほかの履歴からも同じ相手として見つかる =====
bool Restore(const EntityTree& tree) {
	if (tree.empty()) {
		return false;
	}
	const EntityData& root = tree.front();
	if (root.parent != 0 && !EntityManager::FindById(root.parent).IsValid()) {
		return false; // 戻す先の親がもう無い
	}
	for (const EntityData& data : tree) {
		if (EntityManager::FindById(data.id).IsValid()) {
			return false; // すでに居る（二重に戻そうとしている）
		}
	}

	// 先に全員を作ってから、Component・Script を戻す（Script の中の参照は、相手が居ないと外れるので。親が子より前に並ぶので、親は先にできている）
	for (const EntityData& data : tree) {
		Handle<Entity> handle = EntityManager::CreateWithId(data.id, data.name, EntityManager::FindById(data.parent));
		EntityManager::Get(handle)->isActive = data.isActive;
	}
	for (const EntityData& data : tree) {
		const Handle<Entity> handle = EntityManager::FindById(data.id);
		// 予約せずその場で付ける（Flushの中＝フレームの境目なので安全）。Transformのように作った時点で持っている物は上書き
		EntityManager::RestoreComponents(handle, data.components);
		ScriptSystem::Restore(handle, data.scripts);
	}
	HierarchyWindow::SetSelected(EntityManager::FindById(root.id));
	return true;
}

// ===== 自分と子孫の数 =====
size_t CountTree(Handle<Entity> handle) {
	size_t count = 1;
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(handle, children);
	for (Handle<Entity> child : children) {
		count += CountTree(child);
	}
	return count;
}

// ===== 写しのEntityを消す（作成・貼り付けのUndo、削除のRedo）=====
bool Remove(const EntityTree& tree) {
	if (tree.empty()) {
		return false;
	}
	const Handle<Entity> root = EntityManager::FindById(tree.front().id);
	// 写した後でゲームが子を足していたら、その子まで消してしまうので戻さない
	if (!root.IsValid() || CountTree(root) != tree.size()) {
		return false;
	}
	EntityManager::Destroy(root);
	EntityManager::FlushDestroy(); // Flushの中＝フレームの境目なので、すぐ消してよい
	return true;
}

// ===== Entityを増やす操作（作成・貼り付け）を実行して、履歴に積む =====
void AddTree(EntityTree tree) {
	Execute({[tree] { return Remove(tree); }, [tree] { return Restore(tree); }});
}

//=============================================================================
// 貼り付け
//=============================================================================
// 同じ親の下に、その名前のEntityがいるか
bool HasSibling(EntityId parent, const std::string& name) {
	const Handle<Entity> parentHandle = EntityManager::FindById(parent); // 0なら無効なHandle＝root
	for (const Entity& entity : EntityManager::GetAll()) {
		if (entity.parent == parentHandle && entity.name == name) {
			return true;
		}
	}
	return false;
}

// 末尾の " (Copy3)" を外す。コピーのコピーが "Cube (Copy1) (Copy1)" のように伸びないように
std::string RemoveCopySuffix(const std::string& name) {
	const std::string mark = " (Copy";
	const size_t position = name.rfind(mark);
	if (position == std::string::npos || name.back() != ')') {
		return name;
	}
	const size_t digitsBegin = position + mark.size();
	const size_t digitsEnd = name.size() - 1; // 最後の ')' の位置
	if (digitsBegin >= digitsEnd) {
		return name; // "Cube (Copy)" のように番号が無い
	}
	for (size_t i = digitsBegin; i < digitsEnd; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return name;
		}
	}
	return name.substr(0, position);
}

// 貼り付けた物の名前。同じ親の下で空いている一番小さい番号を付ける："Cube (Copy1)" "Cube (Copy2)" …
std::string MakeCopyName(const std::string& name, EntityId parent) {
	const std::string base = RemoveCopySuffix(name);
	for (int number = 1;; ++number) {
		std::string candidate = base + " (Copy" + std::to_string(number) + ")";
		if (!HasSibling(parent, candidate)) {
			return candidate;
		}
	}
}

// 貼る物の番号を全部 XOR で変える数（変えた後の番号が、どれも0でなく、まだ誰も使っていない数を選ぶ）
EntityId MakeIdShift(const EntityTree& tree) {
	for (;;) {
		const EntityId shift = EntityManager::NewId(); // でたらめな数がほしいだけ（番号としては使わない）
		const bool canUse = std::ranges::all_of(tree, [shift](const EntityData& data) {
			const EntityId newId = data.id ^ shift;
			return newId != 0 && !EntityManager::FindById(newId).IsValid();
		});
		if (canUse) {
			return shift;
		}
	}
}

// 写しに新しい番号を振り直して、parentの子として作る
void Paste(EntityTree tree, EntityId parent) {
	if (tree.empty() || (parent != 0 && !EntityManager::FindById(parent).IsValid())) {
		return;
	}
	// 番号を振り直す。全員を同じ数（shift）で XOR する（子が覚えている親の番号も同じ数で XOR すれば、そのまま新しい親を指す）
	// 1つずつ別の番号にしないのは、貼った物に Prefab から置いた物が入っていても、Prefab の中の番号の並び（PrefabManager.h）が崩れないように
	const EntityId shift = MakeIdShift(tree);
	EntityIdMap idMap; // 元の番号 → 貼った番号（貼った物の中どうしの参照を、貼った物に向け直すため）
	for (EntityData& data : tree) {
		idMap[data.id] = data.id ^ shift;
		data.id ^= shift;
	}
	for (size_t i = 1; i < tree.size(); ++i) {
		tree[i].parent ^= shift;
	}
	tree.front().parent = parent;
	tree.front().name = MakeCopyName(tree.front().name, parent);

	// --- 中の参照を、貼った物に向け直す（写しの Component はバイトのままなので番号を直せない。一度作って JSON で直し、写し直す）---
	if (!Restore(tree)) {
		return;
	}
	std::set<std::string> warnings;
	for (const EntityData& data : tree) {
		const Handle<Entity> handle = EntityManager::FindById(data.id);
		SceneJson components = SceneJson::object();
		ComponentSerializer::Save(handle, components, warnings);
		SceneJson remapped = components;
		ComponentSerializer::RemapRefs(remapped, idMap);
		// 番号が変わった Component だけ読み直す（読み直すと Transform の回転がわずかにずれるので、変わらない物は触らない）
		SceneJson changed = SceneJson::object();
		for (auto it = remapped.begin(); it != remapped.end(); ++it) {
			if (*it != components[it.key()]) {
				changed[it.key()] = *it;
			}
		}
		if (!changed.empty()) {
			ComponentSerializer::Load(handle, changed, warnings);
		}
	}
	EntityTree fixed = Capture(EntityManager::FindById(tree.front().id));
	Remove(fixed);
	AddTree(std::move(fixed));
}

// ===== entity を中身として持つ、一番外側の置いた物（どの置いた物の中身でもなければ entity 自身）=====
Handle<Entity> TopOwnerOf(Handle<Entity> entity) {
	Handle<Entity> current = entity;
	for (Handle<Entity> owner = PrefabManager::FindOwner(current); owner.IsValid(); owner = PrefabManager::FindOwner(current)) {
		current = owner;
	}
	return current;
}

//=============================================================================
// Entityの項目・Component
//=============================================================================
// Entityの項目（名前・有効など）を書き換える操作。memberは「Entityのどの項目か」（メンバへのポインタ）
template<class T> void RequestSetEntityValue(Handle<Entity> handle, T Entity::* member, T value) {
	requests.push_back([id = IdOf(handle), member, value] {
		const Entity* entity = EntityManager::Get(EntityManager::FindById(id));
		if (!entity || entity->*member == value) {
			return;
		}
		const T oldValue = entity->*member;
		auto set = [id, member](const T& newValue) {
			Entity* target = EntityManager::Get(EntityManager::FindById(id));
			if (!target) {
				return false;
			}
			target->*member = newValue;
			return true;
		};
		Execute({[set, oldValue] { return set(oldValue); }, [set, value] { return set(value); }});
	});
}

// Componentを今すぐ足す・外す（Flushの中からだけ呼ぶ）
bool AddComponentNow(EntityId id, const ComponentEditor& editor, std::span<const std::byte> initial) {
	const Handle<Entity> handle = EntityManager::FindById(id);
	if (!handle.IsValid() || editor.Get(handle)) {
		return false;
	}
	editor.RequestAdd(handle, initial);
	EntityManager::FlushComponentChanges();
	return editor.Get(handle) != nullptr;
}

bool RemoveComponentNow(EntityId id, const ComponentEditor& editor) {
	const Handle<Entity> handle = EntityManager::FindById(id);
	if (!editor.Get(handle)) {
		return false;
	}
	editor.RequestRemove(handle);
	EntityManager::FlushComponentChanges();
	return editor.Get(handle) == nullptr;
}

// Component全体を書き戻す（undoならbefore、redoならafter）
bool ApplyEdits(const std::vector<ComponentEdit>& edits, bool undo) {
	// 一部だけ復元して失敗しないよう、先に全対象を確認する。
	for (const ComponentEdit& edit : edits) {
		if (!edit.editor->Get(EntityManager::FindById(edit.entity))) {
			return false;
		}
	}
	for (const ComponentEdit& edit : edits) {
		void* target = edit.editor->Get(EntityManager::FindById(edit.entity));
		edit.editor->Load(target, undo ? edit.before : edit.after); // 書き戻し方は editor が知っている
	}
	return true;
}
} // namespace

//=============================================================================
// 毎フレーム
//=============================================================================
void EditorHistory::Flush() {
	// 実行中に新しい予約が足されても壊れないように、取り出してから回す
	std::vector<std::function<void()>> current = std::move(requests);
	requests.clear();
	for (const std::function<void()>& request : current) {
		request();
	}
}

void EditorHistory::Clear() {
	history.clear();
	cursor = 0;
	requests.clear();
	pendingEdits.clear();
	suspendedHistory.clear(); // 脇へよけていた物も、もう戻せない（相手が作り直される）
	isSuspended = false;
	++changeCount;
}

void EditorHistory::Suspend() {
	if (isSuspended) {
		return;
	}
	suspendedHistory = std::exchange(history, {});
	suspendedCursor = std::exchange(cursor, size_t{0});
	isSuspended = true;
	pendingEdits.clear();
	++changeCount;
}

void EditorHistory::Resume() {
	if (!isSuspended) {
		return;
	}
	// よけている間の履歴（画面の編集）は捨てて、よける前の履歴に戻す
	history = std::exchange(suspendedHistory, {});
	cursor = suspendedCursor;
	isSuspended = false;
	pendingEdits.clear();
	++changeCount;
}

uint64_t EditorHistory::GetChangeCount() { return changeCount; }

//=============================================================================
// Undo / Redo
//=============================================================================
bool EditorHistory::CanUndo() { return cursor > 0; }
bool EditorHistory::CanRedo() { return cursor < history.size(); }

void EditorHistory::RequestUndo() {
	CommitComponentChanges(); // 編集中の物があれば、先に履歴へ入れてから戻す
	requests.push_back([] {
		if (cursor == 0) {
			return;
		}
		// Failedでhistoryを消しても壊れないように、呼ぶ前に関数をコピーしておく
		const std::function<bool()> undo = history[cursor - 1].undo;
		if (undo()) {
			--cursor;
		} else {
			Failed();
			++changeCount;
		}
	});
}

void EditorHistory::RequestRedo() {
	CommitComponentChanges();
	requests.push_back([] {
		if (cursor >= history.size()) {
			return;
		}
		const std::function<bool()> redo = history[cursor].redo;
		if (redo()) {
			++cursor;
		} else {
			Failed();
			++changeCount;
		}
	});
}

//=============================================================================
// Entityの操作
//=============================================================================
void EditorHistory::RequestCreate(const std::string& name, Handle<Entity> parent, std::vector<ComponentSnapshot> components) {
	if (parent.IsValid() && !EntityManager::IsAlive(parent)) {
		return;
	}
	requests.push_back([name, parentId = IdOf(parent), components = std::move(components)] {
		EntityData data;
		data.id = EntityManager::NewId();
		data.parent = parentId;
		data.name = name;
		data.components = components; // 空なら、作った時点のTransform（初期値）だけ
		AddTree({data});
	});
}

void EditorHistory::RequestDestroy(Handle<Entity> handle) {
	requests.push_back([id = IdOf(handle)] {
		const EntityTree tree = Capture(EntityManager::FindById(id));
		if (tree.empty()) {
			return;
		}
		Execute({[tree] { return Restore(tree); }, [tree] { return Remove(tree); }});
	});
}

void EditorHistory::RequestRename(Handle<Entity> handle, const std::string& name) {
	if (name.empty()) {
		return; // 空の名前にはしない（元の名前のまま）
	}
	RequestSetEntityValue(handle, &Entity::name, name);
}

void EditorHistory::RequestSetActive(Handle<Entity> handle, bool isActive) { RequestSetEntityValue(handle, &Entity::isActive, isActive); }

void EditorHistory::RequestReparent(Handle<Entity> child, Handle<Entity> parent) {
	if (parent.IsValid() && !EntityManager::IsAlive(parent)) {
		return;
	}
	requests.push_back([childId = IdOf(child), parentId = IdOf(parent)] {
		const Handle<Entity> childHandle = EntityManager::FindById(childId);
		const Entity* entity = EntityManager::Get(childHandle);
		if (!entity) {
			return;
		}
		const EntityId oldParentId = IdOf(entity->parent);
		if (oldParentId == parentId) {
			return;
		}
		// Prefab の中身は、同じ置いた物の中身の間でだけ動かせる（親を変えたことは、置いた所ごとの上書きとして保存される）
		// 外へ出すと、開き直したときに Prefab から同じ番号でもう1つ作られてしまう。
		// あとから足した子の下にも入れない（開き直すと、中身は足した子より先に作られるので、親が見つからない）
		const Handle<Entity> owner = PrefabManager::FindOwner(childHandle);
		const Handle<Entity> newParent = EntityManager::FindById(parentId);
		if (owner.IsValid() && newParent != owner && TopOwnerOf(newParent) != TopOwnerOf(childHandle)) {
			LogManager::Warning(std::format("「{}」は Prefab の中身なので、同じ置いた物の中身の間でだけ動かせます（外へ出すときは、先に Inspector の Unpack）", entity->name));
			return;
		}
		// 見た目の位置を変えずに付け替える。Transform の値（親から見た位置）が変わるので、前と後の両方を覚えておく
		const TransformComponent oldTransform = *EntityManager::Get<TransformComponent>(childHandle);
		EntityManager::SetParentKeepWorld(childHandle, EntityManager::FindById(parentId));
		if (IdOf(EntityManager::Get(childHandle)->parent) != parentId) {
			return; // 輪になる付け替えはSetParentが断る（何も変わっていないので、履歴にも積まない）
		}
		const TransformComponent newTransform = *EntityManager::Get<TransformComponent>(childHandle);

		// Undo・Redo は計算し直さず、覚えた値をそのまま戻す（何度行き来しても値がずれない）
		auto set = [childId](EntityId newParentId, const TransformComponent& transform) {
			const Handle<Entity> target = EntityManager::FindById(childId);
			const Handle<Entity> parentHandle = EntityManager::FindById(newParentId);
			if (!target.IsValid() || (newParentId != 0 && !parentHandle.IsValid())) {
				return false;
			}
			EntityManager::SetParent(target, parentHandle);
			if (EntityManager::Get(target)->parent != parentHandle) {
				return false; // 輪になる付け替えはSetParentが断る
			}
			*EntityManager::Get<TransformComponent>(target) = transform;
			return true;
		};
		Append({[set, oldParentId, oldTransform] { return set(oldParentId, oldTransform); }, [set, parentId, newTransform] { return set(parentId, newTransform); }});
	});
}

//=============================================================================
// コピー・貼り付け
//=============================================================================
bool EditorHistory::CanPaste() { return !clipboard.empty(); }

void EditorHistory::Copy(Handle<Entity> handle) {
	EntityTree tree = Capture(handle);
	if (!tree.empty()) {
		clipboard = std::move(tree);
	}
}

void EditorHistory::RequestPaste(Handle<Entity> parent) {
	if (parent.IsValid() && !EntityManager::IsAlive(parent)) {
		return;
	}
	requests.push_back([parentId = IdOf(parent)] { Paste(clipboard, parentId); });
}

void EditorHistory::RequestDuplicate(Handle<Entity> handle) {
	requests.push_back([id = IdOf(handle)] {
		const Entity* entity = EntityManager::Get(EntityManager::FindById(id));
		if (!entity) {
			return;
		}
		// Pasteの中でEntityが増えるとentityのポインタが使えなくなるので、先に値を取り出しておく
		EntityTree tree = Capture(entity->self);
		const EntityId parent = IdOf(entity->parent);
		Paste(std::move(tree), parent);
	});
}

//=============================================================================
// Prefab
//=============================================================================
void EditorHistory::RequestRevertPrefab(Handle<Entity> root) {
	requests.push_back([id = IdOf(root)] {
		const Handle<Entity> handle = EntityManager::FindById(id);
		const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(handle);
		if (link == nullptr || link->prefab == 0) {
			return; // 置いた物でない・Prefab が読めていない
		}
		const uint32_t prefab = link->prefab;
		// --- 戻した後の項目：一番上の番号・名前・親・有効・Transform だけ（上書きは書かない）---
		std::set<std::string> warnings;
		SceneJson entry = PrefabOverrides::MakeInstanceEntry(handle, warnings);
		SceneJson components = SceneJson::object();
		if (entry["components"].contains("Transform")) {
			components["Transform"] = entry["components"]["Transform"];
		}
		entry["components"] = std::move(components);
		for (const char* key : {"removedComponents", "overrides", "removed"}) {
			entry.erase(key);
		}
		// --- 今の物を写してから消し、Prefab から作り直す（同じ番号でできるので、ほかの物からの参照もそのまま）---
		const EntityTree before = Capture(handle);
		EntityManager::Destroy(handle);
		EntityManager::FlushDestroy(); // Flushの中＝フレームの境目なので、すぐ消してよい
		const Handle<Entity> created = PrefabManager::InstantiateNow(prefab, entry, warnings);
		if (!created.IsValid()) {
			Restore(before);
			return;
		}
		if (const Handle<Entity> parent = EntityManager::FindById(entry.value("parent", EntityId{0})); parent.IsValid()) {
			EntityManager::SetParent(created, parent);
		}
		const EntityTree after = Capture(created);
		// Undo は作り直した物を消して前の写しを戻す。Redo はその逆
		Append({[before, after] { return Remove(after) && Restore(before); }, [before, after] { return Remove(before) && Restore(after); }});
		HierarchyWindow::SetSelected(created);
		LogManager::Log(std::format("「{}」を Prefab のとおりに戻しました: {}", entry.value("name", std::string()), PrefabManager::GetPath(prefab)));
	});
}

//=============================================================================
// Component
//=============================================================================
void EditorHistory::RequestAddComponent(Handle<Entity> handle, const ComponentEditor& editor) {
	CommitComponentChanges();
	requests.push_back([id = IdOf(handle), editor = &editor] { Execute({[id, editor] { return RemoveComponentNow(id, *editor); }, [id, editor] { return AddComponentNow(id, *editor, {}); }}); });
}

void EditorHistory::RequestRemoveComponent(Handle<Entity> handle, const ComponentEditor& editor) {
	CommitComponentChanges();
	requests.push_back([id = IdOf(handle), editor = &editor] {
		const void* component = editor->Get(EntityManager::FindById(id));
		if (!component) {
			return;
		}
		// 外す前の中身を写しておき、Undoではその値のまま戻す
		const std::vector<std::byte> saved = editor->Save(component);
		Execute({[id, editor, saved] { return AddComponentNow(id, *editor, saved); }, [id, editor] { return RemoveComponentNow(id, *editor); }});
	});
}

void EditorHistory::RecordComponentChange(Handle<Entity> handle, const ComponentEditor& editor, const std::vector<std::byte>& before, const std::vector<std::byte>& after) {
	if (before == after) {
		return;
	}
	const EntityId id = IdOf(handle);
	auto edit = std::find_if(pendingEdits.begin(), pendingEdits.end(), [&](const ComponentEdit& e) { return e.entity == id && e.editor == &editor; });
	if (edit == pendingEdits.end()) {
		pendingEdits.push_back({id, &editor, before, after});
	} else {
		edit->after = after; // beforeは操作開始時のまま
	}
}

void EditorHistory::CommitComponentChanges() {
	if (pendingEdits.empty()) {
		return;
	}
	std::vector<ComponentEdit> edits = std::move(pendingEdits);
	pendingEdits.clear();
	std::erase_if(edits, [](const ComponentEdit& edit) { return edit.before == edit.after; });
	if (!edits.empty()) {
		requests.push_back([edits] { Append({[edits] { return ApplyEdits(edits, true); }, [edits] { return ApplyEdits(edits, false); }}); });
	}
}

//=============================================================================
// Entity の外の値
//=============================================================================
void EditorHistory::Record(std::function<bool()> undo, std::function<bool()> redo) {
	// 値はもう変わっているので、実行はせずに積むだけ。ほかの予約と順番が入れ替わらないよう、予約の列を通す
	requests.push_back([undo = std::move(undo), redo = std::move(redo)] { Append({undo, redo}); });
}