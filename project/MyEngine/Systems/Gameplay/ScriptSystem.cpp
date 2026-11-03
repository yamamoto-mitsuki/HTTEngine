#include "ScriptSystem.h"

#include <algorithm>
#include <format>
#include <set>
#include <span>
#include <utility>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/Script.h"
#include "MyEngine/Editor/Inspector/ScriptEditor.h"
#include "MyEngine/Systems/Physics/CollisionSystem.h"

namespace {
//=============================================================================
// 種類と実体
//=============================================================================
// 種類（SCRIPT(...) 1つにつき1つ）
struct ScriptType {
	std::string name;                    // SCRIPT(...) に書いた型の名前（Inspector の見出し・シーンファイルの名前）
	std::type_index type;                // 型（GetComponent・Add で探すときに使う）
	ScriptSystem::CreateFunction create; // 作り方
};
std::vector<ScriptType> types;

// 付いている Script 全部（付けた順。Update もこの順に呼ぶ）
// unique_ptr なので、配列が伸びて場所が動いても、Script そのものの場所は動かない
std::vector<std::unique_ptr<Script>> scripts;

const ScriptType* FindType(std::type_index type) {
	for (const ScriptType& info : types) {
		if (info.type == type) {
			return &info;
		}
	}
	return nullptr;
}

const ScriptType* FindTypeByName(std::string_view name) {
	for (const ScriptType& info : types) {
		if (info.name == name) {
			return &info;
		}
	}
	return nullptr;
}

// 写しの文字（Capture・SaveFields）の形が壊れていたら警告して読まない
SceneJson ParseText(std::string_view text) {
	SceneJson json = SceneJson::parse(text.begin(), text.end(), nullptr, false); // false：壊れていても例外を投げない
	if (!json.is_object()) {
		LogManager::Error("Script の写しが読めませんでした");
		return SceneJson::object();
	}
	return json;
}

// 集めた警告をログに出す（同じ文は1回だけ）
void PrintWarnings(const std::set<std::string>& warnings) {
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

// 当たり1件を、Script の関数に渡す（すり抜ける当たりは OnTrigger〜、ぶつかる当たりは OnCollision〜）
void CallHit(Script& script, const CollisionHit& hit) {
	const GameObject other{hit.other};
	if (hit.isTrigger) {
		switch (hit.phase) {
		case HitPhase::Enter:
			script.OnTriggerEnter(other);
			break;
		case HitPhase::Stay:
			script.OnTriggerStay(other);
			break;
		case HitPhase::Exit:
			script.OnTriggerExit(other);
			break;
		}
		return;
	}
	const CollisionInfo collision{other, hit.normal, hit.selfVelocity, hit.otherVelocity}; // ぶつかる当たりは、向きと速さも渡す
	switch (hit.phase) {
	case HitPhase::Enter:
		script.OnCollisionEnter(collision);
		break;
	case HitPhase::Stay:
		script.OnCollisionStay(collision);
		break;
	case HitPhase::Exit:
		script.OnCollisionExit(collision);
		break;
	}
}
} // namespace

//=============================================================================
// 初期化・終了
//=============================================================================
void ScriptSystem::Initialize() {
	// Entity が消える直前に、その Entity の Script の OnDestroy を呼べるようにする
	EntityManager::AddDestroyListener(&ScriptSystem::OnEntityDestroyed);
	LogManager::Log("Initialized");
}

void ScriptSystem::Release() {
	// 取り出してから呼ぶ（OnDestroy の中で Script が増えても、回している配列が動かないように）
	std::vector<std::unique_ptr<Script>> all = std::move(scripts);
	scripts.clear();
	for (const std::unique_ptr<Script>& script : all) {
		CallDestroyCallbacks(*script);
	}
	all.clear();
	scripts.clear(); // OnDestroy の中で付けられた物（もう呼ばない）
	LogManager::Log("Released");
}

//=============================================================================
// 種類の登録
//=============================================================================
void ScriptSystem::RegisterType(const char* name, std::type_index type, CreateFunction create, const char* sourceFile, std::string category) {
	if (FindType(type) != nullptr || FindTypeByName(name) != nullptr) {
		LogManager::Error(std::format("Script \"{}\" が2つ登録されました。後の方は使えません", name));
		return;
	}
	types.push_back({name, type, create});

	// --- 保存（シーンファイル・Play を押した瞬間の退避）。データの Component と同じ表に、同じ形で入る ---
	// 書く：付いていれば、見せ方の関数に係を渡す（書く係は値を変えないので、本物に呼んでよい。SetValue の係もそのまま使える）
	const auto visit = [type](Handle<Entity> entity, ComponentUI& ui) {
		Script* script = Get(entity, type);
		if (script == nullptr) {
			return false; // 付いていない
		}
		Describe(*script, ui);
		return true;
	};
	ComponentSerializer::RegisterErased(
	    name, visit,
	    // 読む：付けてから、ファイルにある項目だけ上書きする
	    [type](Handle<Entity> entity, ComponentUI& reader) {
		    if (Script* script = Add(entity, type)) {
			    Describe(*script, reader);
		    }
	    },
	    visit, {},
	    // 参照を付け替える（Prefab の入れ子・置いた所ごとの上書き）：作りたての写しに見せ方の関数を呼ぶ（Entity には付けないので、Awake なども呼ばれない）
	    [create](SceneJson& fields, const EntityIdMap& map) {
		    const std::unique_ptr<Script> temporary = create();
		    ComponentSerializer::RemapFields([&temporary](ComponentUI& ui) { Describe(*temporary, ui); }, fields, map);
	    });

	// --- Inspector の区画・Add Component のメニュー（COMPONENT(...) と同じ Gameplay に並ぶ）---
	ComponentEditorRegistry::Register(std::make_unique<ScriptEditor>(name, type, sourceFile, std::move(category)));
}

//=============================================================================
// 毎フレーム（再生中だけ）
//=============================================================================
void ScriptSystem::Update(EntityFilter filter) {
	// 途中で Script が増えても大丈夫なように、添字で回す。数は最初に決める（このフレームに付いた物は、次のフレームから）
	// 回している間に配列から消すことはない（消すのは FlushDestroy・Entity の破棄＝フレームの終わりか境目だけ）
	const size_t count = scripts.size();
	// filter で選ばなかった Script は、このフレームは何も呼ばない（ポーズ中に、開いている画面の中の Script だけ動かす）
	const auto isSkipped = [filter](const Script& script) { return filter != nullptr && !filter(script.gameObject_.handle); };

	// --- 1. 起こす：Awake → OnEnable（Script ごとに続けて呼ぶ。Unity と同じ）---
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (script.isDestroyRequested_ || isSkipped(script)) {
			continue;
		}
		// 無効な Entity に付いている物は、有効になるまで Awake を待つ（Unity と同じ）
		if (!script.isAwake_ && script.gameObject_.IsActiveInHierarchy()) {
			script.isAwake_ = true;
			script.Awake();
		}
		SyncEnabled(script);
	}

	// --- 2. Start（全部の Awake の後。有効になっている物だけ）---
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (script.isEnableCalled_ && !script.isStarted_ && !script.isDestroyRequested_ && !isSkipped(script)) {
			script.isStarted_ = true;
			script.Start();
		}
	}

	// --- 3. 当たり（前のフレームの終わりに CollisionSystem が作った一覧）---
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (!IsRunning(script)) {
			continue;
		}
		for (const CollisionHit& hit : CollisionSystem::GetHits(script.gameObject_.handle)) {
			if (!IsRunning(script) || isSkipped(script)) {
				break; // 当たりの途中で自分を無効にした・消した
			}
			CallHit(script, hit);
		}
	}

	// --- 4. Update ---
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (IsRunning(script) && !isSkipped(script)) {
			script.Update();
		}
	}
}

void ScriptSystem::LateUpdate(EntityFilter filter) {
	const size_t count = scripts.size();
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (IsRunning(script) && (filter == nullptr || filter(script.gameObject_.handle))) {
			script.LateUpdate();
		}
	}
}

// ===== entity に付いている動いている Script 全部の OnTimelineEvent を呼ぶ（TimelineSystem から）=====
void ScriptSystem::SendTimelineEvent(Handle<Entity> entity, const TimelineEvent& event) {
	// 呼んだ先で Script が増えても大丈夫なように、添字で回す（Update と同じ）
	const size_t count = scripts.size();
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (script.gameObject_.handle == entity && IsRunning(script)) {
			script.OnTimelineEvent(event);
		}
	}
}

// ===== Destroy(Script) の予約を反映する（OnDisable・OnDestroy を呼んでから消す）。EntityManager::FlushDestroy の前に呼ぶ =====
void ScriptSystem::FlushDestroy() {
	// 消す物を先に取り出してから呼ぶ（OnDestroy の中で Script が増えても、回している配列が動かないように）
	std::vector<std::unique_ptr<Script>> removed;
	for (std::unique_ptr<Script>& script : scripts) {
		if (script->isDestroyRequested_) {
			removed.push_back(std::move(script)); // 抜けた所は nullptr になる
		}
	}
	if (removed.empty()) {
		return;
	}
	std::erase(scripts, nullptr);
	for (const std::unique_ptr<Script>& script : removed) {
		CallDestroyCallbacks(*script);
	}
}

//=============================================================================
// 付ける・探す・外す
//=============================================================================
Script* ScriptSystem::Add(Handle<Entity> entity, std::type_index type) {
	if (!EntityManager::IsAlive(entity)) {
		return nullptr;
	}
	if (Script* existing = Get(entity, type)) {
		return existing; // 1つの Entity に同じ種類は1つまで（シーンファイルが型の名前で書くため）
	}
	const ScriptType* info = FindType(type);
	if (info == nullptr) {
		LogManager::Error(std::format("{} は SCRIPT(...) で登録されていないので付けられません（.cpp に SCRIPT(クラス名); を書いてください）", type.name()));
		return nullptr;
	}
	std::unique_ptr<Script> script = info->create();
	script->gameObject_.handle = entity;
	Script* result = script.get();
	scripts.push_back(std::move(script));
	return result;
}

Script* ScriptSystem::Get(Handle<Entity> entity, std::type_index type) {
	for (const std::unique_ptr<Script>& script : scripts) {
		// typeid(*script) は「実際に作った型」（Script ではなく PlayerController など）
		if (script->gameObject_.handle == entity && !script->isDestroyRequested_ && std::type_index(typeid(*script)) == type) {
			return script.get();
		}
	}
	return nullptr;
}

Script* ScriptSystem::Find(Handle<Entity> entity, bool (*match)(const Script& script)) {
	for (const std::unique_ptr<Script>& script : scripts) {
		if (script->gameObject_.handle == entity && !script->isDestroyRequested_ && match(*script)) {
			return script.get();
		}
	}
	return nullptr;
}

void ScriptSystem::FindAll(bool (*match)(const Script& script), std::vector<Script*>& out) {
	out.clear();
	for (const std::unique_ptr<Script>& script : scripts) {
		if (!script->isDestroyRequested_ && script->gameObject_.IsActiveInHierarchy() && match(*script)) {
			out.push_back(script.get());
		}
	}
}

void ScriptSystem::RequestDestroy(Script* script) {
	if (script != nullptr) {
		script->isDestroyRequested_ = true; // 消すのは FlushDestroy（フレームの終わり）
	}
}

void ScriptSystem::RemoveNow(Handle<Entity> entity, std::type_index type) {
	const auto found = std::find_if(
	    scripts.begin(), scripts.end(), [entity, type](const std::unique_ptr<Script>& script) { return script->gameObject_.handle == entity && std::type_index(typeid(*script)) == type; });
	if (found == scripts.end()) {
		return;
	}
	// 配列から外してから呼ぶ（OnDestroy の中で Script が増えても大丈夫なように）
	const std::unique_ptr<Script> removed = std::move(*found);
	scripts.erase(found);
	CallDestroyCallbacks(*removed);
}

//=============================================================================
// 写し（Undo・コピー）
//=============================================================================
std::string ScriptSystem::Capture(Handle<Entity> entity) {
	SceneJson root = SceneJson::object();
	std::set<std::string> warnings; // 写しでは警告を出さない（同じ物はシーンを保存するときに出る）
	for (const std::unique_ptr<Script>& script : scripts) {
		if (script->gameObject_.handle != entity || script->isDestroyRequested_) {
			continue;
		}
		SceneJson fields;
		ComponentSerializer::WriteFields(GetTypeName(*script), [&script](ComponentUI& ui) { Describe(*script, ui); }, fields, warnings);
		root[GetTypeName(*script)] = std::move(fields);
	}
	return root.empty() ? std::string() : root.dump();
}

void ScriptSystem::Restore(Handle<Entity> entity, std::string_view text) {
	if (text.empty()) {
		return; // Script が付いていなかった
	}
	const SceneJson root = ParseText(text);
	std::set<std::string> warnings;
	for (auto it = root.begin(); it != root.end(); ++it) {
		const ScriptType* info = FindTypeByName(it.key());
		if (info == nullptr) {
			warnings.insert(std::format("知らない Script \"{}\" を飛ばしました", it.key()));
			continue;
		}
		if (Script* script = Add(entity, info->type)) {
			ComponentSerializer::ReadFields(info->name, [script](ComponentUI& ui) { Describe(*script, ui); }, *it, warnings);
		}
	}
	PrintWarnings(warnings);
}

std::string ScriptSystem::SaveFields(Script& script) {
	SceneJson fields;
	std::set<std::string> warnings; // Inspector が毎フレーム呼ぶので、警告は出さない
	ComponentSerializer::WriteFields(GetTypeName(script), [&script](ComponentUI& ui) { Describe(script, ui); }, fields, warnings);
	return fields.dump();
}

void ScriptSystem::LoadFields(Script& script, std::string_view text) {
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(GetTypeName(script), [&script](ComponentUI& ui) { Describe(script, ui); }, ParseText(text), warnings);
	PrintWarnings(warnings);
}

//=============================================================================
// 見せ方・名前
//=============================================================================
void ScriptSystem::Describe(Script& script, ComponentUI& ui) {
	ui.Field("Enabled", script.enabled_); // Unity の見出しのチェックと同じ（false の間は Update などが呼ばれない）
	script.Fields(ui);
}

const char* ScriptSystem::GetTypeName(const Script& script) {
	const ScriptType* info = FindType(typeid(script));
	return info ? info->name.c_str() : "?";
}

size_t ScriptSystem::GetCount() { return scripts.size(); }

//=============================================================================
// 中で使う物
//=============================================================================
void ScriptSystem::OnEntityDestroyed(Handle<Entity> entity) {
	// その Entity の Script を取り出してから呼ぶ（OnDestroy の中で Script が増えても大丈夫なように）
	std::vector<std::unique_ptr<Script>> removed;
	for (std::unique_ptr<Script>& script : scripts) {
		if (script->gameObject_.handle == entity) {
			removed.push_back(std::move(script));
		}
	}
	if (removed.empty()) {
		return;
	}
	std::erase(scripts, nullptr);
	for (const std::unique_ptr<Script>& script : removed) {
		CallDestroyCallbacks(*script);
	}
}

void ScriptSystem::SyncEnabled(Script& script) {
	const bool shouldBeEnabled = script.isAwake_ && script.enabled_ && script.gameObject_.IsActiveInHierarchy();
	if (shouldBeEnabled && !script.isEnableCalled_) {
		script.isEnableCalled_ = true;
		script.OnEnable();
	} else if (!shouldBeEnabled && script.isEnableCalled_) {
		script.isEnableCalled_ = false;
		script.OnDisable();
	}
}

bool ScriptSystem::IsRunning(const Script& script) {
	// enabled・SetActive はその場で変わるので、OnDisable を呼ぶ前でも、ここで止める
	return script.isStarted_ && script.isEnableCalled_ && script.enabled_ && !script.isDestroyRequested_ && script.gameObject_.IsActiveInHierarchy();
}

void ScriptSystem::CallDestroyCallbacks(Script& script) {
	if (script.isEnableCalled_) {
		script.isEnableCalled_ = false;
		script.OnDisable();
	}
	if (script.isAwake_) {
		script.OnDestroy();
	}
}