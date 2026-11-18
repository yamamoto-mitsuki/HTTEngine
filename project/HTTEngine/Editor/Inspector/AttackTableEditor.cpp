#ifdef USE_IMGUI
#include "AttackTableEditor.h"

#include <algorithm>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Components/Animation/AnimatorComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Inspector/AssetInspector.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"

namespace {
constexpr std::string_view kTypeName = "Attack Table";     // Inspector の見出しに出す名前
constexpr ImU32 kErrorColor = IM_COL32(255, 96, 96, 255);  // 知らない名前（部品・行き先のステップ）の色
constexpr const char* kNoPartDescription = "部品を使わない"; // 部品が (none) のときの説明
constexpr const char* kAttackPartsTip = "攻撃の部品。それぞれ Runs In で、どのステップの間に動くかを選ぶ（Whole Attack なら攻撃の間ずっと）。同じステップの部品は上から順に呼ぶ";
constexpr const char* kWholeAttackLabel = "Whole Attack (攻撃の間ずっと)";
constexpr const char* kIdlePartsTip = "攻撃を選ぶまで（Selecting の間）動く部品。無ければ何もせずに待つ";

/// <summary>
/// 部品の欄を描くのに要る物
/// </summary>
struct PartDrawContext {
	ComponentUI& ui;                       // 部品の値の欄を描く係
	const std::vector<std::string>& names; // 部品の選択肢（ATTACK_PART(...) とエンジンの部品）
	uint32_t clipSource;                   // 部品のClipの候補を出すアニメーションのファイル
	bool isLocked;                         // 再生中（部品の足し引き・選び直しを止める）
};

//=============================================================================
// Clipの候補
//=============================================================================
// 開いているシーンの中で、この表を使っている敵の Animator のファイル（無ければ 0）
uint32_t FindClipSource(uint32_t table) {
	uint32_t found = 0;
	EntityManager::ForEach<AttackTableComponent>([&](Handle<Entity> entity, const AttackTableComponent& component) {
		if (found != 0 || component.table != table) {
			return;
		}
		if (const AnimatorComponent* animator = EntityManager::Get<AnimatorComponent>(entity)) {
			found = animator->animationHandle;
		}
	});
	return found;
}

//=============================================================================
// 攻撃部品
//=============================================================================
// ===== 部品の選択欄 =====
// 選択肢は登録した部品。選んでいる部品の説明を、欄の下に出す
void DrawPartCombo(const char* label, AttackPartSlot& slot, const std::vector<std::string>& names) {
	const bool isUnknown = !slot.name.empty() && std::ranges::find(names, slot.name) == names.end();
	if (isUnknown) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
	}
	const std::string preview = slot.name.empty() ? "(none)" : slot.name;
	if (ImGui::BeginCombo(label, preview.c_str())) {
		if (ImGui::Selectable("(none)", slot.name.empty())) {
			slot.name.clear(); // 動くステップは残す
			slot.fields = "{}";
		}
		ImGui::SetItemTooltip("%s", kNoPartDescription);
		for (const std::string& name : names) {
			if (ImGui::Selectable(name.c_str(), name == slot.name) && name != slot.name) {
				slot.name = name;
				slot.fields = "{}"; // 別の部品の値は使えないので、値は空に戻す（動くステップは残す）
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
		ImGui::SetItemTooltip("ATTACK_PART(...) で登録した攻撃部品とエンジンの部品から選ぶ");
	}
	// --- 選んでいる部品の説明（長い文でも欄の幅で折り返す）---
	const std::string& description = AttackPartRegistry::GetDescription(slot.name);
	const char* text = slot.name.empty() ? kNoPartDescription : description.c_str();
	if (text[0] != '\0') {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}
}

// ===== 部品の値 =====
// 部品を1つ作って、表の値を読み、Fields を描いて、書き戻す（Fieldsの実体はゲームのクラスの中にあるので、表だけでは描けない）
// label は警告に出す名前
void DrawPartFields(const PartDrawContext& draw, AttackPartSlot& slot, std::string_view label) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
	if (part == nullptr) {
		return;
	}
	std::set<std::string> warnings;
	const auto describe = [&part](ComponentUI& fields) { part->Fields(fields); };
	ComponentSerializer::ReadFields(label, describe, SceneJson::parse(slot.fields, nullptr, false), warnings);
	// 部品の欄の名前は部品が決めるので、表の欄（Name など）とぶつからないようにIDの枠を分ける
	ImGui::PushID("partFields");
	{
		// 部品は表のファイルを知らないので、Clipの候補はここで決める（ClipSourceScopeを抜けると元に戻る）
		const ClipSourceScope clipSource(draw.ui, draw.clipSource);
		part->Fields(draw.ui);
	}
	ImGui::PopID();
	SceneJson json;
	ComponentSerializer::WriteFields(label, describe, json, warnings);
	slot.fields = json.dump();
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

// ===== 部品の動くステップの選択欄（攻撃の部品だけ）=====
// 選択肢は「攻撃の間ずっと（Whole Attack）」と、この攻撃のステップ。名前のステップが無ければ赤で出す
void DrawRunsInCombo(AttackPartSlot& slot, const std::vector<AttackStepData>& steps) {
	const auto found = std::ranges::find(steps, slot.step, &AttackStepData::name);
	const bool isUnknown = !slot.step.empty() && found == steps.end();
	std::string preview = kWholeAttackLabel;
	if (!slot.step.empty()) {
		preview = isUnknown ? std::format("{} (無いステップ)", slot.step)
		                    : std::format("{}. {} ({})", found - steps.begin() + 1, slot.step, magic_enum::enum_name(found->stage));
	}
	if (isUnknown) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
	}
	if (ImGui::BeginCombo("Runs In###runsIn", preview.c_str())) {
		if (ImGui::Selectable(kWholeAttackLabel, slot.step.empty())) {
			slot.step.clear();
		}
		ImGui::SetItemTooltip("攻撃の始めから終わりまで、どのステップの間も動く（ステップに入るたびに OnStep が来る）");
		for (size_t i = 0; i < steps.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const std::string item = std::format("{}. {} ({})", i + 1, steps[i].name, magic_enum::enum_name(steps[i].stage));
			if (ImGui::Selectable(item.c_str(), slot.step == steps[i].name)) {
				slot.step = steps[i].name;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (isUnknown) {
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("この名前のステップがありません（ステップを消した）。このままだと、この部品は動かない");
	} else {
		ImGui::SetItemTooltip("この部品が動くステップ。そのステップの間だけ OnBegin → OnUpdate → OnEnd が来る");
	}
}

// ===== 部品の並び（攻撃の部品・待ちの部品）=====
// 部品ごとに開け閉めできる欄（見出しに番号・部品の名前・動く所）。中に「選択欄・動くステップ・値・Up・Down・Remove」、最後に Add Part
// steps は攻撃のステップ（攻撃の部品だけ。待ちの部品は nullptr で、動くステップの欄を出さない）。label は警告に出す名前
void DrawPartList(const PartDrawContext& draw, const char* title, const char* tip, std::vector<AttackPartSlot>& parts, std::string_view label,
                  const std::vector<AttackStepData>* steps) {
	ImGui::PushID(title);
	ImGui::SeparatorText(title);
	ImGui::SetItemTooltip("%s", tip);
	int removePart = -1;
	int moveUp = -1; // この部品を1つ上の部品と入れ替える
	for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
		AttackPartSlot& slot = parts[i];
		ImGui::PushID(i);
		// 見出し：番号・部品の名前・動く所。### の後ろを固定にする（部品を選び直しても、開いたままにする）
		std::string where;
		if (steps != nullptr) {
			where = slot.step.empty() ? std::string("  [Whole Attack]") : std::format("  [{}]", slot.step);
		}
		const std::string header = std::format("{}. {}{}###part", i + 1, slot.name.empty() ? "(none)" : slot.name, where);
		if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			// 再生中は選び直し・動く所の付け替えを止める（敵が部品の番号と動く所で実体を持っているので）
			ImGui::BeginDisabled(draw.isLocked);
			DrawPartCombo("Part###part", slot, draw.names);
			if (steps != nullptr) {
				DrawRunsInCombo(slot, *steps);
			}
			ImGui::EndDisabled();
			DrawPartFields(draw, slot, label);
			// --- 並べ替え・消す（呼ぶ順は上から）---
			ImGui::BeginDisabled(draw.isLocked);
			ImGui::BeginDisabled(i == 0);
			if (ImGui::SmallButton("Up")) {
				moveUp = i;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 >= static_cast<int>(parts.size()));
			if (ImGui::SmallButton("Down")) {
				moveUp = i + 1;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove Part")) {
				removePart = i;
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	// 配列を動かすのは、全部描いた後（描いている途中で動かすと、parts[i] の参照が壊れる）
	if (moveUp > 0) {
		std::swap(parts[moveUp - 1], parts[moveUp]);
	}
	if (removePart >= 0) {
		parts.erase(parts.begin() + removePart);
	}
	ImGui::BeginDisabled(draw.isLocked);
	if (ImGui::Button("Add Part")) {
		parts.emplace_back();
	}
	ImGui::EndDisabled();
	ImGui::PopID();
}

//=============================================================================
// ステップ
//=============================================================================
// ===== 行き先の選択欄 =====
// 選択肢は「1つ下のステップ（Next）」「攻撃を終える（End）」と、この攻撃のステップの名前。名前のステップが無ければ赤で出す
void DrawLinkCombo(const char* label, AttackStepLink& link, const std::vector<AttackStepData>& steps, const char* tip) {
	const bool isUnknown = (link.jump == AttackStepJump::Step) && std::ranges::find(steps, link.step, &AttackStepData::name) == steps.end();
	std::string preview;
	switch (link.jump) {
	case AttackStepJump::Next:
		preview = "Next (1つ下のステップ)";
		break;
	case AttackStepJump::End:
		preview = "End (攻撃を終える)";
		break;
	case AttackStepJump::Step:
		preview = std::format("Step: {}", link.step);
		break;
	}
	if (isUnknown) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
	}
	if (ImGui::BeginCombo(label, preview.c_str())) {
		if (ImGui::Selectable("Next (1つ下のステップ)", link.jump == AttackStepJump::Next)) {
			link = AttackStepLink{AttackStepJump::Next, ""};
		}
		ImGui::SetItemTooltip("1つ下のステップへ（一番下のステップなら、攻撃を終える）");
		if (ImGui::Selectable("End (攻撃を終える)", link.jump == AttackStepJump::End)) {
			link = AttackStepLink{AttackStepJump::End, ""};
		}
		ImGui::SetItemTooltip("攻撃を終えて、次の攻撃を選ぶ（クールタイムが始まる）");
		for (size_t i = 0; i < steps.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const bool isSelected = (link.jump == AttackStepJump::Step) && (link.step == steps[i].name);
			if (ImGui::Selectable(std::format("Step: {}", steps[i].name).c_str(), isSelected)) {
				link = AttackStepLink{AttackStepJump::Step, steps[i].name};
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (isUnknown) {
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("この名前のステップがありません（ステップの名前を変えた・消した）。このまま進むと攻撃を終える");
	} else {
		ImGui::SetItemTooltip("%s", tip);
	}
}

// ===== ステップの名前を変えたら、同じ攻撃の行き先と部品の動くステップも付け替える（名前を打ち換えただけで、外れないように）=====
void RenameStep(AttackData& attack, const std::string& before, const std::string& after) {
	for (AttackPartSlot& slot : attack.parts) {
		if (slot.step == before) {
			slot.step = after;
		}
	}
	for (AttackStepData& step : attack.steps) {
		for (AttackStepLink* link : {&step.next, &step.fail}) {
			if (link->jump == AttackStepJump::Step && link->step == before) {
				link->step = after;
			}
		}
	}
}

// ===== ステップの並び =====
void DrawSteps(const PartDrawContext& draw, AttackData& attack) {
	ImGui::PushID("Steps");
	ImGui::SeparatorText("Steps");
	ImGui::SetItemTooltip("攻撃のステップ。上から順に進める（Next・OnFailで、ほかのステップへ飛ばせる）");
	int removeStep = -1;
	int moveUp = -1; // このステップを1つ上のステップと入れ替える
	for (int i = 0; i < static_cast<int>(attack.steps.size()); ++i) {
		AttackStepData& step = attack.steps[i];
		ImGui::PushID(i);
		// ### の後ろを固定にする（名前を打ち換えても、開いたままにする）
		const std::string header = std::format("{}. {}  ({}, {:.2f}s)###step", i + 1, step.name, magic_enum::enum_name(step.stage), step.seconds);
		if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			const std::string nameBefore = step.name;
			AttackTableManager::DescribeStep(draw.ui, step);
			if (step.name != nameBefore) {
				RenameStep(attack, nameBefore, step.name);
			}
			DrawLinkCombo("Next###next", step.next, attack.steps, "時間が来た・部品が FinishStep を呼んだときの行き先");
			DrawLinkCombo("On Fail###fail", step.fail, attack.steps, "部品が FailStep を呼んだときの行き先（掴めなかった・着けなかった など）");
			// --- 並べ替え・消す（再生中は、敵がステップの番号で覚えているので止める）---
			ImGui::BeginDisabled(draw.isLocked);
			ImGui::BeginDisabled(i == 0);
			if (ImGui::SmallButton("Up")) {
				moveUp = i;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 >= static_cast<int>(attack.steps.size()));
			if (ImGui::SmallButton("Down")) {
				moveUp = i + 1;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove Step")) {
				removeStep = i;
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	// 配列を動かすのは、全部描いた後
	if (moveUp > 0) {
		std::swap(attack.steps[moveUp - 1], attack.steps[moveUp]);
	}
	if (removeStep >= 0) {
		attack.steps.erase(attack.steps.begin() + removeStep);
	}
	ImGui::BeginDisabled(draw.isLocked);
	if (ImGui::Button("Add Step")) {
		AttackStepData step;
		step.name = std::format("Step {}", attack.steps.size() + 1);
		attack.steps.push_back(std::move(step));
	}
	ImGui::EndDisabled();
	ImGui::PopID();
}

//=============================================================================
// 行を増やす・消す
//=============================================================================
// ===== フェーズ =====
void DrawPhases(const PartDrawContext& draw, AttackTable& table) {
	int removePhase = -1;
	for (int i = 0; i < static_cast<int>(table.phases.size()); ++i) {
		AttackPhaseData& phase = table.phases[i];
		ImGui::PushID(i);
		ImGui::SeparatorText(std::format("{}. {}", i + 1, phase.name).c_str());
		AttackTableManager::DescribePhase(draw.ui, phase, i == 0);
		DrawPartList(draw, "Idle Parts", kIdlePartsTip, phase.idleParts, phase.name, nullptr);
		if (i != 0) {
			ImGui::BeginDisabled(draw.isLocked);
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
	ImGui::BeginDisabled(draw.isLocked || table.phases.size() >= kMaxAttackPhases);
	if (ImGui::Button("Add Phase")) {
		AttackPhaseData phase;
		phase.name = std::format("Phase {}", table.phases.size() + 1);
		table.phases.push_back(std::move(phase));
	}
	ImGui::EndDisabled();
}

// ===== 攻撃 =====
void DrawAttacks(const PartDrawContext& draw, AttackTable& table) {
	int removeAttack = -1;
	int copyAttack = -1;
	for (int i = 0; i < static_cast<int>(table.attacks.size()); ++i) {
		AttackData& attack = table.attacks[i];
		ImGui::PushID(i);
		// ### の後ろを番号にする（名前を打ち換えても、開いたままにする）
		if (ImGui::CollapsingHeader(std::format("{}###attack", attack.name).c_str())) {
			ImGui::Indent();
			AttackTableManager::DescribeAttack(draw.ui, attack, table.phases);
			DrawSteps(draw, attack);
			DrawPartList(draw, "Parts", kAttackPartsTip, attack.parts, attack.name, &attack.steps);
			ImGui::Spacing();
			ImGui::BeginDisabled(draw.isLocked);
			if (ImGui::SmallButton("Duplicate")) {
				copyAttack = i;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete")) {
				removeAttack = i;
			}
			ImGui::EndDisabled();
			ImGui::Unindent();
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
	ImGui::BeginDisabled(draw.isLocked);
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
		const std::vector<std::string> partNames = AttackPartRegistry::GetNames();
		// 再生中は行・ステップ・部品を増やす・消す・選び直すのを止める（敵が行・ステップ・部品の番号で覚えているので）
		const PartDrawContext draw{ui, partNames, FindClipSource(handle), AttackTableSystem::IsAnyRunning()};
		AttackTableManager::DescribeTable(ui, table);
		if (draw.clipSource == 0) {
			ui.Label("部品のClipの欄は名前を打つ（この表を使う敵を Animator ごとシーンに置くと、そのAnimatorのファイルの名前から選べる）");
		}
		ImGui::SeparatorText("Phases");
		// フェーズと攻撃は、どちらも行の番号を ID にするので、区画ごとに枠を分ける（分けないと同じ番号の Name 欄がぶつかる）
		ImGui::PushID("Phases");
		DrawPhases(draw, table);
		ImGui::PopID();
		ImGui::Spacing();
		ImGui::SeparatorText("Attacks");
		ImGui::PushID("Attacks");
		DrawAttacks(draw, table);
		ImGui::PopID();
		ImGui::Spacing();
		ui.Label(
		    draw.isLocked ? "再生中は値だけ直せる（行・ステップ・部品の足し引きと部品の選び直しは Stop してから）"
		                  : "このファイルを選んだ敵みんなに効く（つまむ・打つのが終わると保存。Undoは無い。1つ前は.bakに残る）");
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
