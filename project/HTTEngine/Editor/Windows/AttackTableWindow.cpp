#ifdef USE_IMGUI
#include "AttackTableWindow.h"

#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"

namespace {
constexpr ImU32 kOkColor = IM_COL32(120, 220, 120, 255); // 条件を満たしている
constexpr ImU32 kNgColor = IM_COL32(240, 110, 110, 255); // 条件を満たしていない
constexpr float kStaggerTestSeconds = 1.0f;              // Stagger ボタンで怯ませる秒

// 条件の表の1マス（満たしていれば緑、いなければ赤）
void DrawCondition(bool isOk, const std::string& text) {
	ImGui::PushStyleColor(ImGuiCol_Text, isOk ? kOkColor : kNgColor);
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopStyleColor();
}
} // namespace

//=============================================================================
// 描画
//=============================================================================
void AttackTableWindow::Draw() {
	if (ImGui::Begin("Attack Table")) {
		const Handle<Entity> entity = FindEntity();
		if (entity.IsValid()) {
			DrawStatus(entity);
			DrawAttacks(entity);
		} else {
			ImGui::TextDisabled("Attack Table の付いた Entity がありません");
		}
	}
	ImGui::End();
}

// ===== 出す敵を決める =====
Handle<Entity> AttackTableWindow::FindEntity() {
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	if (EntityManager::Get<AttackTableComponent>(selected) != nullptr) {
		return selected;
	}
	Handle<Entity> first;
	EntityManager::ForEach<AttackTableComponent>([&first](Handle<Entity> entity, AttackTableComponent&) {
		if (!first.IsValid()) {
			first = entity;
		}
	});
	return first;
}

// ===== 上の段 =====
void AttackTableWindow::DrawStatus(Handle<Entity> entity) {
	const Entity* info = EntityManager::Get(entity);
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTableStatus status = AttackTableSystem::GetStatus(entity);
	ImGui::Text("Entity: %s", info->name.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%s", AttackTableManager::GetPath(component->table).c_str());
	if (!status.isRunning) {
		ImGui::TextDisabled(component->table == 0 ? "Table が選ばれていません" : "再生すると様子が出ます");
		return;
	}
	const std::string state(magic_enum::enum_name(status.state));
	const std::string stage(magic_enum::enum_name(status.stage));
	ImGui::Text("Phase: %d %s", status.phase + 1, status.phaseName.c_str());
	ImGui::Text("State: %s", state.c_str());
	if (status.stage != AttackStage::None) {
		ImGui::SameLine();
		ImGui::Text("/ Stage: %s", stage.c_str());
	}
	if (!status.isEnabled) {
		ImGui::SameLine();
		ImGui::TextColored(ImColor(kNgColor), "（AI 停止中）");
	}
	ImGui::Text("Attack: %s", status.attackName.empty() ? "-" : status.attackName.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("（最後に選んだ：%s）", status.lastAttackName.empty() ? "-" : status.lastAttackName.c_str());
	// --- 待ちの部品（Selecting の間だけ動く。残りの秒は下の棒が Min Idle Seconds の残り）---
	const AttackTable* table = AttackTableManager::Get(component->table);
	const std::string& idlePart = table->phases[status.phase].idlePart.name;
	ImGui::Text("Idle Part: %s", idlePart.empty() ? "-" : idlePart.c_str());
	if (status.state == AttackTableState::Selecting) {
		ImGui::SameLine();
		ImGui::TextDisabled(status.GetRemaining() > 0.0f ? "（Min Idle Seconds の待ち）" : "（使える攻撃を待っている）");
	}
	// --- 残り時間 ---
	const float progress = (status.duration > 0.0f) ? status.time / status.duration : 0.0f;
	ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), std::format("残り {:.2f} / {:.2f} 秒", status.GetRemaining(), status.duration).c_str());
	// --- 距離・HP ---
	const float distance = AttackTableSystem::CalcTargetDistance(entity);
	ImGui::Text("Distance: %s", std::isinf(distance) ? "(Target なし)" : std::format("{:.2f}", distance).c_str());
	ImGui::SameLine();
	ImGui::Text("/ HP: %.0f%%", component->healthRatio * 100.0f);
	// --- 試す（ゲームを書かなくても怯み・停止を確かめられるように）---
	if (ImGui::SmallButton("Stagger")) {
		AttackTableSystem::Stagger(entity, kStaggerTestSeconds);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(status.isEnabled ? "Stop AI" : "Resume AI")) {
		AttackTableSystem::SetEnabled(entity, !status.isEnabled);
	}
}

// ===== 下の段 =====
void AttackTableWindow::DrawAttacks(Handle<Entity> entity) {
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTable* table = AttackTableManager::Get(component->table);
	std::vector<AttackCheck> checks;
	AttackTableSystem::GetAttackChecks(entity, checks);
	if (table == nullptr || checks.size() != table->attacks.size()) {
		return;
	}
	ImGui::SeparatorText("Attacks");
	constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##attackTable", 7, kFlags)) {
		return;
	}
	ImGui::TableSetupColumn("Attack");
	ImGui::TableSetupColumn("Chance");
	ImGui::TableSetupColumn("Phase");
	ImGui::TableSetupColumn("Distance");
	ImGui::TableSetupColumn("Cooldown");
	ImGui::TableSetupColumn("Repeat");
	ImGui::TableSetupColumn("");
	ImGui::TableHeadersRow();
	for (size_t i = 0; i < table->attacks.size(); ++i) {
		const AttackData& attack = table->attacks[i];
		const AttackCheck& check = checks[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(attack.name.c_str());
		ImGui::TableNextColumn();
		DrawCondition(check.probability > 0.0f, std::format("{:.1f}%", check.probability * 100.0f));
		ImGui::TableNextColumn();
		DrawCondition(check.isPhaseOk, check.isPhaseOk ? "OK" : "NG");
		ImGui::TableNextColumn();
		const std::string maxDistance = (attack.maxDistance > 0.0f) ? std::format("{:.1f}", attack.maxDistance) : "∞";
		DrawCondition(check.isDistanceOk, std::format("{:.1f}〜{}", attack.minDistance, maxDistance));
		ImGui::TableNextColumn();
		DrawCondition(check.isCooldownOk, check.isCooldownOk ? "OK" : std::format("{:.2f}", check.cooldownRemaining));
		ImGui::TableNextColumn();
		DrawCondition(check.isRepeatOk, (table->maxRepeat > 0) ? std::format("{}/{}", check.repeatCount, table->maxRepeat) : std::to_string(check.repeatCount));
		ImGui::TableNextColumn();
		if (ImGui::SmallButton("Next")) {
			AttackTableSystem::RequestAttack(entity, attack.name); // 次にこの攻撃を出す（同じ名前の行があると、先の行になる）
		}
		ImGui::SetItemTooltip("次に選ぶときに、この攻撃を出す");
		ImGui::PopID();
	}
	ImGui::EndTable();
}
#endif