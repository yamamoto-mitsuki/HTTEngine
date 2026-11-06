#include "AttackTableManager.h"

#include <set>
#include <memory>
#include <format>
#include <fstream>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Attack Table"; // 警告に出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct AttackTableAsset {
	std::string path;      // ファイル（ToPathKey の形）
	AttackTable table;     // 中身
	uint32_t revision = 0; // 中身の版（Set のたびに増える）
};

/// <summary>
/// AttackTableManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct AttackTableStorage {
	std::vector<AttackTableAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<AttackTableStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
AttackTableAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

//=============================================================================
// JSON にする
//=============================================================================
// ファイルの形（"phases"・"attacks" は [ ] の並び。1つの中身は Describe〜 の項目）
//   { "settings": {...}, "phases": [{...}], "attacks": [{...}] }

/// <summary>
/// 部品（名前と値）を書くときのキーの名前
/// </summary>
struct PartKeys {
	const char* name;   // 部品の名前
	const char* fields; // 部品の値
};
constexpr PartKeys kAttackPartKeys = {"part", "partFields"};         // 攻撃の行の部品
constexpr PartKeys kIdlePartKeys = {"idlePart", "idlePartFields"}; // フェーズの待ちの部品

// ===== 部品（名前と値）=====
// 部品は Describe の並びに入れない（選択肢が実行中に決まり、値の形も部品ごとに違う）ので、決めたキーで別に書く
void WritePartSlot(SceneJson& json, const AttackPartSlot& slot, const PartKeys& keys) {
	json[keys.name] = slot.name;
	json[keys.fields] = SceneJson::parse(slot.fields, nullptr, false);
}

// ===== 攻撃1行 =====
// Describe の項目のほかに、Part と部品の値を書く
SceneJson AttackToJson(AttackData attack, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) {
	SceneJson json;
	ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, json, warnings);
	WritePartSlot(json, attack.part, kAttackPartKeys);
	return json;
}

SceneJson ToJson(AttackTable table) {
	std::set<std::string> warnings;
	SceneJson settings;
	ComponentSerializer::WriteFields(kTypeName, [&table](ComponentUI& ui) { AttackTableManager::DescribeTable(ui, table); }, settings, warnings);

	SceneJson phases = SceneJson::array();
	for (size_t i = 0; i < table.phases.size(); ++i) {
		AttackPhaseData phase = table.phases[i];
		SceneJson item;
		ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, i == 0); }, item, warnings);
		WritePartSlot(item, phase.idlePart, kIdlePartKeys);
		phases.push_back(std::move(item));
	}

	SceneJson attacks = SceneJson::array();
	for (const AttackData& attack : table.attacks) {
		attacks.push_back(AttackToJson(attack, table.phases, warnings));
	}

	SceneJson root = SceneJson::object();
	root["settings"] = std::move(settings);
	root["phases"] = std::move(phases);
	root["attacks"] = std::move(attacks);
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
	return root;
}

//=============================================================================
// JSON から読む
//=============================================================================
// ===== 部品（名前と値）=====
// 読んだキーは json から消す（部品を除いた残りを Describe の並びで読むため。残すと「読む所がない」と警告が出る）
AttackPartSlot TakePartSlot(SceneJson& json, const PartKeys& keys) {
	AttackPartSlot slot;
	if (const auto name = json.find(keys.name); name != json.end()) {
		if (name->is_string()) {
			slot.name = name->get<std::string>();
		}
		json.erase(name);
	}
	if (const auto fields = json.find(keys.fields); fields != json.end()) {
		if (fields->is_object()) {
			slot.fields = fields->dump();
		}
		json.erase(fields);
	}
	return slot;
}

// ===== 攻撃1行 =====
AttackData AttackFromJson(const SceneJson& json, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) {
	AttackData attack;
	SceneJson fields = json; // 部品を除いた残りを、Describe の並びで読む
	attack.part = TakePartSlot(fields, kAttackPartKeys);
	ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, fields, warnings);
	return attack;
}

AttackTable FromJson(const SceneJson& root, std::set<std::string>& warnings) {
	AttackTable table;
	if (const auto settings = root.find("settings"); settings != root.end()) {
		ComponentSerializer::ReadFields(kTypeName, [&table](ComponentUI& ui) { AttackTableManager::DescribeTable(ui, table); }, *settings, warnings);
	}
	// --- フェーズ（攻撃の「使えるフェーズ」が名前を見るので、先に読む）---
	if (const auto phases = root.find("phases"); phases != root.end() && phases->is_array() && !phases->empty()) {
		table.phases.clear();
		for (const SceneJson& item : *phases) {
			if (table.phases.size() >= kMaxAttackPhases) {
				warnings.insert(std::format("フェーズは {} 個までです。後ろは読みません", kMaxAttackPhases));
				break;
			}
			const bool isFirst = table.phases.empty();
			AttackPhaseData phase = isFirst ? AttackTable{}.phases.front() : AttackPhaseData{}; // 一番上のフェーズはしきい値を書かないので、表の既定の値から始める
			SceneJson fields = item; // 待ちの部品を除いた残りを、Describe の並びで読む
			phase.idlePart = TakePartSlot(fields, kIdlePartKeys);
			ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, isFirst); }, fields, warnings);
			table.phases.push_back(std::move(phase));
		}
	}
	// --- 攻撃 ---
	if (const auto attacks = root.find("attacks"); attacks != root.end() && attacks->is_array()) {
		for (const SceneJson& item : *attacks) {
			table.attacks.push_back(AttackFromJson(item, table.phases, warnings));
		}
	}
	return table;
}

// ファイルに書く（前のファイルは .bak として1つ残る）
bool WriteFile(const std::string& path, const AttackTable& table) {
	if (!SceneSerializer::WriteFile(path, ToJson(table))) {
		LogManager::Error(std::format("攻撃表を保存できませんでした: {}", path));
		return false;
	}
	return true;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void AttackTableManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<AttackTableStorage>();
}

void AttackTableManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t AttackTableManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "AttackTableManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &AttackTableAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("攻撃表のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded() || !json.is_object()) {
		LogManager::Error(std::format("攻撃表のファイルが壊れています: {}", key));
		return 0;
	}
	std::set<std::string> warnings;
	AttackTable table = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(table)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded attack table: {} -> {}", key, handle));
	return handle;
}

const std::string& AttackTableManager::GetPath(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const AttackTable* AttackTableManager::Get(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->table : nullptr;
}

//=============================================================================
// 書き換え・保存
//=============================================================================
void AttackTableManager::Set(uint32_t handle, const AttackTable& table) {
	AttackTableAsset* asset = Find(handle);
	if (asset == nullptr) {
		return;
	}
	asset->table = table;
	++asset->revision;
}

uint32_t AttackTableManager::GetRevision(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->revision : 0;
}

bool AttackTableManager::Save(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->table);
}

bool AttackTableManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), AttackTable{});
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
// ===== 表全体 =====
void AttackTableManager::DescribeTable(ComponentUI& ui, AttackTable& value) {
	ui.Field("Max Repeat###maxRepeat", value.maxRepeat, AtLeast(0.0f).Tip("同じ攻撃を続けてよい回数。これを超える攻撃は、ほかに使える攻撃がある間は選ばない（0 なら制限なし）"));
}

// ===== フェーズ1つ =====
void AttackTableManager::DescribePhase(ComponentUI& ui, AttackPhaseData& value, bool isFirst) {
	ui.Field("Name###name", value.name);
	// 一番上のフェーズは最初から入っているので、しきい値も移行の長さも使わない（保存もしない）
	if (!isFirst) {
		ui.Field("HP Threshold###healthThreshold", value.healthThreshold, Range(0.0f, 1.0f).Tip("HP の割合がこの値以下になったら、このフェーズへ移る（0.5 で 50%）"));
		ui.Field("Transition Seconds###transitionSeconds", value.transitionSeconds, AtLeast(0.0f).Tip("このフェーズへ移るときの長さ（秒）。0 なら移行の状態を飛ばす"));
	}
	ui.Field("Min Idle Seconds###minIdleSeconds", value.minIdleSeconds,
	         AtLeast(0.0f).Tip("次の攻撃を選ぶまでの最短の秒。攻撃の後・怯みの後・フェーズの移行の後・始めに、Selecting に入るたびに 0 から数える（0 ならすぐ選ぶ。RequestAttack は待たない）"));
}

// ===== 攻撃1行 =====
void AttackTableManager::DescribeAttack(ComponentUI& ui, AttackData& value, const std::vector<AttackPhaseData>& phases) {
	ui.Field("Name###name", value.name);
	ui.Field("Weight###weight", value.weight, AtLeast(0.0f).Tip("抽選の重み（大きいほど選ばれやすい。0 なら RequestAttack でしか出ない）"));
	ui.Field("Min Distance###minDistance", value.minDistance, AtLeast(0.0f).Tip("相手との距離がこれより近いと使わない"));
	ui.Field("Max Distance###maxDistance", value.maxDistance, AtLeast(0.0f).Tip("相手との距離がこれより遠いと使わない（0 なら上限なし）"));
	ui.Field("Cooldown###cooldown", value.cooldown, AtLeast(0.0f).Tip("攻撃が終わってから、次に使えるようになるまでの秒"));
	ui.Field("Windup Seconds###windupSeconds", value.windupSeconds, AtLeast(0.0f).Tip("予備動作の秒"));
	ui.Field("Active Seconds###activeSeconds", value.activeSeconds, AtLeast(0.0f).Tip("攻撃の秒"));
	ui.Field("Recovery Seconds###recoverySeconds", value.recoverySeconds, AtLeast(0.0f).Tip("硬直の秒（攻撃の後、次の攻撃を選ぶまでの間）"));
	// --- 使えるフェーズ（今あるフェーズの分だけ。ファイルの項目の名前は phase0, phase1, ... ）---
	ui.Separator("Usable Phases");
	for (size_t i = 0; i < phases.size() && i < kMaxAttackPhases; ++i) {
		const std::string label = std::format("{}###phase{}", phases[i].name, i);
		ui.Field(label.c_str(), value.usablePhases[i]);
	}
}