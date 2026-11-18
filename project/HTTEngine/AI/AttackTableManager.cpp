#include "AttackTableManager.h"

#include <set>
#include <memory>
#include <format>
#include <fstream>
#include <algorithm>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Scene/SceneSerializer.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Attack Table"; // 警告に出す名前

// ===== 読み込んだファイルを1つ =====
struct AttackTableAsset {
	std::string path;      // ファイル
	AttackTable table;     // 中身
	uint32_t revision = 0; // 中身の版（Setのたび増える）
};

// ===== AttackTableManagerが持つ物 =====
struct AttackTableStorage {
	std::vector<AttackTableAsset> assets; // 番号-1番目のファイル
};
std::unique_ptr<AttackTableStorage> state;

// 知らない番号のパス
const std::string kEmptyPath;

// ===== 番号のファイル =====
AttackTableAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

//=============================================================================
// JSONにする
//=============================================================================
// ===== 部品の並び（hasStep なら、動くステップの名前も書く。攻撃の部品だけ）=====
SceneJson PartsToJson(const std::vector<AttackPartSlot>& parts, bool hasStep) {
	SceneJson json = SceneJson::array();
	for (const AttackPartSlot& slot : parts) {
		SceneJson item;
		item["name"] = slot.name;
		if (hasStep) {
			item["step"] = slot.step;
		}
		item["fields"] = SceneJson::parse(slot.fields, nullptr, false);
		json.push_back(std::move(item));
 	}
	return json;
}
// ===== 行き先 =====
SceneJson LinkToJson(const AttackStepLink& link) { 
	SceneJson json;
	json["jump"] = std::string(magic_enum::enum_name(link.jump));
	json["step"] = link.step;
	return json;
}
// ===== ステップ1つ =====
SceneJson StepToJson(AttackStepData step, std::set<std::string>& warnings) { 
	SceneJson json;
	ComponentSerializer::WriteFields(kTypeName, [&step](ComponentUI& ui) { AttackTableManager::DescribeStep(ui, step); }, json, warnings);
	json["next"] = LinkToJson(step.next);
	json["fail"] = LinkToJson(step.fail);
	return json;
}
// ===== 攻撃1行 =====
SceneJson AttackToJson(AttackData attack, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) { 
	SceneJson json;
	ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, json, warnings);
	json["parts"] = PartsToJson(attack.parts, true);
	SceneJson steps = SceneJson::array();
	for (const AttackStepData& step : attack.steps) {
		steps.push_back(StepToJson(step, warnings));
	}
	json["steps"] = std::move(steps);
	return json;
}

SceneJson ToJson(AttackTable table) { 
	std::set<std::string> warnings;
	SceneJson settings;
	ComponentSerializer::WriteFields(kTypeName, [&table](ComponentUI& ui) { AttackTableManager::DescribeTable(ui, table); }, settings, warnings);
	// フェーズ分ループ
	SceneJson phases = SceneJson::array();
	for (size_t i = 0; i < table.phases.size(); ++i) {
		AttackPhaseData phase = table.phases[i];
		SceneJson item;
		ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, i == 0); }, item, warnings);
		item["idleParts"] = PartsToJson(phase.idleParts, false);
		phases.push_back(std::move(item));
	}
	// 攻撃分ループ
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
// JSONから読む
//=============================================================================
// ===== キーを1つ取り出す =====
SceneJson TakeKey(SceneJson& json, const char* key) { 
	const auto found = json.find(key);
	if (found == json.end()) {
		return nullptr;
	}
	SceneJson value = std::move(*found);
	json.erase(found);
	return value;
}
// ===== 部品の並び =====
std::vector<AttackPartSlot> TakeParts(SceneJson& json, const char* key) {
	std::vector<AttackPartSlot> parts;
	const SceneJson items = TakeKey(json, key);
	if (!items.is_array()) {
		return parts;
	}
	for (const SceneJson& item : items) {
		if (!item.is_object()) {
			continue;
		}
		AttackPartSlot slot;
		if (const auto name = item.find("name"); name != item.end() && name->is_string()) {
			slot.name = name->get<std::string>();
		}
		if (const auto step = item.find("step"); step != item.end() && step->is_string()) {
			slot.step = step->get<std::string>();
		}
		if (const auto fields = item.find("fields"); fields != item.end() && fields->is_object()) {
			slot.fields = fields->dump();
		}
		parts.push_back(std::move(slot));
	}
	return parts;
}
// ===== 行き先 =====
AttackStepLink TakeLink(SceneJson& json, const char* key, const AttackStepLink& fallback, std::set<std::string>& warnings) { 
	AttackStepLink link = fallback; 
	const SceneJson item = TakeKey(json, key);
	if (!item.is_object()) {
		return link;
	}
	if (const auto jump = item.find("jump"); jump != item.end() && jump->is_string()) {
		const std::string text = jump->get<std::string>();
		if (const auto value = magic_enum::enum_cast<AttackStepJump>(text)) {
			link.jump = *value;
		} else {
			warnings.insert(std::format("ステップの行き先\"{}\"を知りません。{}として読みます", text, magic_enum::enum_name(link.jump)));
		}
	}
	if (const auto step = item.find("step"); step != item.end() && step->is_string()) {
		link.step = step->get<std::string>();
	}
	return link;
}
// ===== ステップ1つ =====
AttackStepData StepFromJson(const SceneJson& json, std::set<std::string>& warnings) { 
	AttackStepData step;
	SceneJson fields = json; // 行き先を除いた残りをDescribeの並びで読む
	step.next = TakeLink(fields, "next", step.next, warnings);
	step.fail = TakeLink(fields, "fail", step.fail, warnings);
	ComponentSerializer::ReadFields(kTypeName, [&step](ComponentUI& ui) { AttackTableManager::DescribeStep(ui, step); }, fields, warnings);
	return step;
}
// ===== 攻撃1行 =====
AttackData AttackFromJson(const SceneJson& json, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) { 
	AttackData attack;
	SceneJson fields = json; // 部品・行き先を除いた残りをDescribeの並びで読む
	attack.parts = TakeParts(fields, "parts");
	if (const SceneJson steps = TakeKey(fields, "steps"); steps.is_array()) {
		attack.steps.clear();
		for (const SceneJson& item : steps) {
			if (item.is_object()) {
				attack.steps.push_back(StepFromJson(item, warnings));
			}
		}
	}
	ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, fields, warnings);
	return attack;
}

AttackTable FromJson(const SceneJson& root, std::set<std::string>& warnings) { 
	AttackTable table;
	if (const auto settings = root.find("settings"); settings != root.end()) {
		ComponentSerializer::ReadFields(kTypeName, [&table](ComponentUI& ui) { AttackTableManager::DescribeTable(ui, table); }, *settings, warnings);
	}
	// --- フェーズ ---
	if (const auto phases = root.find("phases"); phases != root.end() && phases->is_array() && !phases->empty()) {
		table.phases.clear();
		for (const SceneJson& item : *phases) {
			if (table.phases.size() >= kMaxAttackPhases) {
				warnings.insert(std::format("フェーズは {} 個までです。後ろは読みません", kMaxAttackPhases));
				break;
			}
			const bool isFirst = table.phases.empty();
			AttackPhaseData phase = isFirst ? AttackTable{}.phases.front() : AttackPhaseData{}; // 一番上のフェーズはしきい値を書かないので、表の既定の値から始める
			SceneJson fields = item;                                                            // 待ちの部品を除いた残りを、Describe の並びで読む
			phase.idleParts = TakeParts(fields, "idleParts");
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

// ファイルに書く
bool WriteFile(const std::string& path, const AttackTable& table) { 
	if (!SceneSerializer::WriteFile(path, ToJson(table))) {
		LogManager::Error(std::format("攻撃表を保存できませんでした: {}", path));
		return false;
	}
	return true;
}

//=============================================================================
// 名前を変えた・動かしたファイル
//=============================================================================
// ===== 部品の値の中のパス =====
bool RemapParts(std::vector<AttackPartSlot>& parts, const MovedPath& moved) { 
	bool isChanged = false;
	for (AttackPartSlot& slot : parts) {
		SceneJson fields = SceneJson::parse(slot.fields, nullptr, false);
		if (fields.is_discarded() || !SceneSerializer::RemapPaths(fields, moved)) {
			continue;
		}
		slot.fields = fields.dump();
		isChanged = true;
	}
	return isChanged;
}
// ===== 覚えているパスを付け替える =====
void OnAssetMoved(const MovedPath& moved) {
	if (state == nullptr) {
		return;
	}
	for (AttackTableAsset& asset : state->assets) {
		moved.Apply(asset.path);
		// --- 部品の値（フェーズの待ち・攻撃 の部品）---
		bool isChanged = false;
		for (AttackPhaseData& phase : asset.table.phases) {
			isChanged |= RemapParts(phase.idleParts, moved);
		}
		for (AttackData& attack : asset.table.attacks) {
			isChanged |= RemapParts(attack.parts, moved);
		}
		if (isChanged) {
			++asset.revision; // 版を上げて、AttackTableSystem に部品の値を読み直させる
		}
	}
}
} // namespace


//=============================================================================
// 新しい攻撃の行のステップ
//=============================================================================
std::vector<AttackStepData> MakeDefaultAttackSteps() { 
	std::vector<AttackStepData> steps(3);
	steps[0].name = "Windup";
	steps[0].stage = AttackStage::Windup;
	steps[0].seconds = 0.5f;
	steps[1].name = "Active";
	steps[1].stage = AttackStage::Active;
	steps[1].seconds = 0.5f;
	steps[2].name = "Recovery";
	steps[2].stage = AttackStage::Recovery;
	steps[2].seconds = 1.0f;
	return steps;
}


//=============================================================================
// 初期化 ・ 解放
//=============================================================================
// ===== 初期化 =====
void AttackTableManager::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています"); 
	state = std::make_unique<AttackTableStorage>();
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	AssetMoveRegistry::AddReferenceFileType(kFileExtension);
}

// ===== 解放 =====
void AttackTableManager::Release() { state.reset(); }


//=============================================================================
// 読み込み
//=============================================================================
uint32_t AttackTableManager::Load(const std::filesystem::path& filePath) { 
	HTT_ASSERT_MSG(state != nullptr, "Initialize()を先に呼んでください"); 
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &AttackTableAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む ---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("攻撃表のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false);
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
		ui.Field("HP Threshold###healthThreshold", value.healthThreshold, Range(0.0f, 1.0f).Tip("HPの割合がこの値以下になったら、このフェーズへ移る（0.5 で 50%）"));
		ui.Field("Transition Seconds###transitionSeconds", value.transitionSeconds, AtLeast(0.0f).Tip("このフェーズへ移るときの長さ。0なら移行の状態を飛ばす"));
	}
	ui.Field("Min Idle Seconds###minIdleSeconds", value.minIdleSeconds,
		AtLeast(0.0f).Tip("次の攻撃を選ぶまでの最短の秒。攻撃の後・怯みの後・フェーズの移行の後・始めに、Selectingに入るたびに0から数える（0ならすぐ選ぶ。RequestAttackは待たない）"));
}

// ===== 攻撃1行 =====
void AttackTableManager::DescribeAttack(ComponentUI& ui, AttackData& value, const std::vector<AttackPhaseData>& phases) {
	ui.Field("Name###name", value.name);
	ui.Field("Weight###weight", value.weight, AtLeast(0.0f).Tip("抽選の重み（大きいほど選ばれやすい。0ならRequestAttackでしか出ない）"));
	ui.Field("Min Distance###minDistance", value.minDistance, AtLeast(0.0f).Tip("相手との距離がこれより近いと使わない"));
	ui.Field("Max Distance###maxDistance", value.maxDistance, AtLeast(0.0f).Tip("相手との距離がこれより遠いと使わない（0なら上限なし）"));
	ui.Field("Cooldown###cooldown", value.cooldown, AtLeast(0.0f).Tip("攻撃が終わってから、次に使えるようになるまでの秒"));
	// --- 使えるフェーズ ---
	ui.Separator("Usable Phases");
	for (size_t i = 0; i < phases.size() && i < kMaxAttackPhases; ++i) {
		const std::string label = std::format("{}###phase{}", phases[i].name, i);
		ui.Field(label.c_str(), value.usablePhases[i]);
	}
}

// ===== ステップ1つ =====
void AttackTableManager::DescribeStep(ComponentUI& ui, AttackStepData& value) {
	ui.Field("Name###name", value.name, Tip("ステップの名前（行き先・確認の画面・context.stepNameに使う。同じ攻撃の中で同じ名前にしない）"));
	ui.Field("Stage###stage", value.stage, Tip("種類の目印（確認の画面・GetStatus・SyncHitbox が見る。並びの順番は決めない）"));
	ui.Field("Seconds###seconds", value.seconds, AtLeast(0.0f).Tip("ステップの長さ。部品が FinishStep・FailStep を呼べば、これより早く終わる（着くまで・着地までのステップも、ここが長さの上限）"));
}