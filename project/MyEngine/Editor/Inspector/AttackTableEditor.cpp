#ifdef USE_IMGUI
#include "AttackTableEditor.h"

#include <set>
#include <string>
#include <vector>
#include <format>
#include <memory>
#include <algorithm>
#include <string_view>

#include <externals/imgui/imgui.h>
#include <externals/nlohmann/json.hpp>

#include "MyEngine/AI/AttackPart.h"
#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Systems/Gameplay/AttackTableSystem.h"

namespace {
constexpr std::string_view kTypeName = "Attack Table"; // Inspector の見出しに出す名前
constexpr const char* kNoPartDescription = "部品を使わない（中身は動かず、時間だけ進む）";         // Part が (none) のときの説明
constexpr const char* kNoIdlePartDescription = "部品を使わない（攻撃を選ぶまで、何もせずに待つ）"; // Idle Part が (none) のときの説明

//=============================================================================
// 攻撃部品
//=============================================================================
// ===== Part の選択欄 =====
// 選択肢は ATTACK_PART(...) で登録した部品。選んでいる部品の説明を、欄の下に出す
// 知らない名前（別のゲームで作った部品など）は、消さずに赤で出す
// 攻撃の行の Part と、フェーズの Idle Part の両方がこれを使う（label は欄の名前、noneDescription は (none) の説明）
void DrawPartCombo(const char* label, AttackPartSlot& slot, const std::vector<std::string>& names, const char* noneDescription) {
	const bool isUnknown = !slot.name.empty() && std::ranges::find(names, slot.name) == names.end();
	if (isUnknown) {
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
	}
	const std::string preview = slot.name.empty() ? "(none)" : slot.name;
	if (ImGui::BeginCombo(label, preview.c_str())) {
		if (ImGui::Selectable("(none)", slot.name.empty())) {
			slot = AttackPartSlot{};
		}
		ImGui::SetItemTooltip("%s", noneDescription);
		for (const std::string& name : names) {
			if (ImGui::Selectable(name.c_str(), name == slot.name) && name != slot.name) {
				slot = AttackPartSlot{name}; // 別の部品の値は使えないので、値は空に戻す
			}
			const std::string& description = AttackPartRegistry::GetDescription(name);
			if (!description.empty()) {
				ImGui::SetItemTooltip("%s", description.c_str());
			}
		}
		ImGui::EndCombo();
	}
	if (isUnknown) {
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("この名前の攻撃部品がありません（ATTACK_PART(...) を確かめてください）");
	} else {
		ImGui::SetItemTooltip("ATTACK_PART(...) で登録した攻撃部品から選ぶ");
	}
	// --- 選んでいる部品の説明（長い文でも欄の幅で折り返す）---
	const std::string& description = AttackPartRegistry::GetDescription(slot.name);
	const char* text = slot.name.empty() ? noneDescription : description.c_str();
	if (text[0] != '\0') {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}
}

// ===== 部品の値 =====
// 部品を1つ作って、表の値を読み、Fields を描いて、書き戻す（Fields の実体はゲームのクラスの中にあるので、表だけでは描けない）
// label は警告に出す名前（攻撃の名前・フェーズの名前）
// sampleAnimation は表の Sample Animation（部品の Clip の欄に、このファイルの中の名前を並べる）
void DrawPartFields(ComponentUI& ui, AttackPartSlot& slot, std::string_view label, uint32_t sampleAnimation) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
	if (part == nullptr) {
		return;
	}
	std::set<std::string> warnings;
	const auto describe = [&part](ComponentUI& fields) { part->Fields(fields); };
	ComponentSerializer::ReadFields(label, describe, SceneJson::parse(slot.fields, nullptr, false), warnings);
	// 部品の欄の名前は部品が決めるので、表の欄（Name など）とぶつからないように ID の枠を分ける
	ImGui::PushID("partFields");
	{
		// 部品は表のファイルを知らないので、Clip の候補はここで決める（ClipSourceScope を抜けると元に戻る）
		const ClipSourceScope clipSource(ui, sampleAnimation);
		part->Fields(ui);
	}
	ImGui::PopID();
	SceneJson json;
	ComponentSerializer::WriteFields(label, describe, json, warnings);
	slot.fields = json.dump();
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

//=============================================================================
// 行を増やす・消す
//=============================================================================
// ===== フェーズ =====
void DrawPhases(ComponentUI& ui, AttackTable& table, bool isLocked) {
	const std::vector<std::string> partNames = AttackPartRegistry::GetNames();
	int removePhase = -1;
	for (int i = 0; i < static_cast<int>(table.phases.size()); ++i) {
		AttackPhaseData& phase = table.phases[i];
		ImGui::PushID(i);
		ImGui::SeparatorText(std::format("{}. {}", i + 1, phase.name).c_str());
		AttackTableManager::DescribePhase(ui, phase, i == 0);
		// --- 待ちの部品（攻撃の Part と同じ一覧から選ぶ。再生中は替えられない）---
		ImGui::BeginDisabled(isLocked);
		DrawPartCombo("Idle Part###idlePart", phase.idlePart, partNames, kNoIdlePartDescription);
		ImGui::EndDisabled();
		DrawPartFields(ui, phase.idlePart, phase.name, table.sampleAnimation);
		if (i != 0) {
			ImGui::BeginDisabled(isLocked);
			if (ImGui::SmallButton("Remove Phase")) {
				removePhase = i;
			}
			ImGui::EndDisabled();
		}
		ImGui::PopID();
	}
	if (removePhase >= 0) {
		table.phases.erase(table.phases.begin() + removePhase);
		// 攻撃の「使えるフェーズ」も1つずつ詰める（後ろのフェーズの設定が、1つ前へずれないように）
		for (AttackData& attack : table.attacks) {
			std::ranges::shift_left(attack.usablePhases.begin() + removePhase, attack.usablePhases.end(), 1);
			attack.usablePhases.back() = true;
		}
	}
	ImGui::BeginDisabled(isLocked || table.phases.size() >= kMaxAttackPhases);
	if (ImGui::Button("Add Phase")) {
		AttackPhaseData phase;
		phase.name = std::format("Phase {}", table.phases.size() + 1);
		table.phases.push_back(std::move(phase));
	}
	ImGui::EndDisabled();
}

// ===== 攻撃 =====
void DrawAttacks(ComponentUI& ui, AttackTable& table, bool isLocked) {
	const std::vector<std::string> partNames = AttackPartRegistry::GetNames();
	int removeAttack = -1;
	int copyAttack = -1;
	for (int i = 0; i < static_cast<int>(table.attacks.size()); ++i) {
		AttackData& attack = table.attacks[i];
		ImGui::PushID(i);
		// ### の後ろを番号にする（名前を打ち換えても、開いたままにする）
		if (ImGui::CollapsingHeader(std::format("{}###attack", attack.name).c_str())) {
			AttackTableManager::DescribeAttack(ui, attack, table.phases);
			ImGui::SeparatorText("Part");
			ImGui::BeginDisabled(isLocked);
			DrawPartCombo("Part###part", attack.part, partNames, kNoPartDescription);
			ImGui::EndDisabled();
			DrawPartFields(ui, attack.part, attack.name, table.sampleAnimation);
			ImGui::Spacing();
			ImGui::BeginDisabled(isLocked);
			if (ImGui::SmallButton("Duplicate")) {
				copyAttack = i;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete")) {
				removeAttack = i;
			}
			ImGui::EndDisabled();
			ImGui::Spacing();
		}
		ImGui::PopID();
	}
	// 配列を動かすのは、全部描いた後（描いている途中で動かすと、attack の参照が壊れる）
	if (copyAttack >= 0) {
		AttackData copy = table.attacks[copyAttack];
		copy.name += " Copy";
		table.attacks.insert(table.attacks.begin() + copyAttack + 1, std::move(copy));
	}
	if (removeAttack >= 0) {
		table.attacks.erase(table.attacks.begin() + removeAttack);
	}
	ImGui::BeginDisabled(isLocked);
	if (ImGui::Button("Add Attack")) {
		AttackData attack;
		attack.name = std::format("Attack {}", table.attacks.size() + 1);
		table.attacks.push_back(std::move(attack));
	}
	ImGui::EndDisabled();
}

//=============================================================================
// Inspector
//=============================================================================
/// <summary>
/// .attacktable のファイルを Inspector で直す係
/// </summary>
class AttackTableAssetEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return AttackTableManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return AttackTableManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = AttackTableManager::Load(filePath);
		const AttackTable* current = AttackTableManager::Get(handle);
		if (current == nullptr) {
			return false;
		}
		// 写しを直して、変わっていたら書き戻す（値は、再生中でも次のフレームからすぐ効く）
		AttackTable table = *current;
		const bool isLocked = AttackTableSystem::IsAnyRunning(); // 再生中は行を増やす・消す・部品を替えるのを止める（敵が行の番号で覚えているので）
		AttackTableManager::DescribeTable(ui, table);
		ImGui::SeparatorText("Phases");
		// フェーズと攻撃は、どちらも行の番号を ID にするので、区画ごとに枠を分ける（分けないと同じ番号の Name 欄がぶつかる）
		ImGui::PushID("Phases");
		DrawPhases(ui, table, isLocked);
		ImGui::PopID();
		ImGui::Spacing();
		ImGui::SeparatorText("Attacks");
		ImGui::PushID("Attacks");
		DrawAttacks(ui, table, isLocked);
		ImGui::PopID();
		ImGui::Spacing();
		ui.Label(
		    isLocked ? "再生中は値だけ直せる（行の足し引きと部品の変更は Stop してから）" : "このファイルを選んだ敵みんなに効く（つまむ・打つのが終わると保存。Undo は無い。1つ前は .bak に残る）");
		if (table == *current) {
			return false;
		}
		AttackTableManager::Set(handle, table);
		return true;
	}

	void Save(const std::filesystem::path& filePath) override { AttackTableManager::Save(AttackTableManager::Load(filePath)); }
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void AttackTableEditor::Initialize() { AssetInspector::Register(std::make_unique<AttackTableAssetEditor>()); }
#endif