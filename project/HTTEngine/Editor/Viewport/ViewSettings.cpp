#ifdef USE_IMGUI
#include "ViewSettings.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Gizmos/CameraGizmo.h"
#include "HTTEngine/Editor/Gizmos/LightGizmo.h"
#include "HTTEngine/Editor/Viewport/EditorViewport.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
using json = nlohmann::json;

constexpr const char* kSavePath = "Parameters/Editor/View.json"; // 人ごとの見え方（Parameters/Editor/ は git に入れない）
constexpr const char* kPageName = "View";                        // Settings の一覧の名前（GlobalVariables のグループの名前とかぶらないように）
} // namespace


//=============================================================================
// 初期化
//=============================================================================
void ViewSettings::Initialize() {
	LightGizmoFlags& lightGizmo = LightGizmo::GetGlobalFlags();
	EditorViewport::ViewFlags& viewFlags = EditorViewport::GetViewFlags();

	// --- 切り替えの一覧（ここに足すと、メニュー・保存・Settings のページに全部出る）---
	// 初めの値は持ち主の値のまま（保存したファイルに値があれば、Load でその値になる）
	Toggles() = {
	    // Gizmos（1つずつは、Inspector のライトの区画・Show Gizmo で）
	    {.section = ViewSection::Gizmos, .name = "Light Icons", .value = &lightGizmo.showIcon, .tooltip = "ライトのアイコン（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Light Ranges", .value = &lightGizmo.showRange, .tooltip = "ライトの届く範囲（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Icons", .value = &CameraGizmo::GetShowIcons(), .tooltip = "カメラのアイコンと、写す範囲の線（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Paths", .value = &CameraGizmo::GetShowPaths(), .tooltip = "カメラが動く道の線（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Follow Zones", .value = &CameraGizmo::GetShowFollowZones(), .tooltip = "Camera Follow の Dead Zone・Soft Zone（Game ビューにも出る。1つずつは Inspector の Show Gizmo）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Orbits", .value = &CameraGizmo::GetShowOrbits(), .tooltip = "Camera Orbit の回る輪・回る中心（Scene ビューだけ。1つずつは Inspector の Show Gizmo）"},
	    {.section = ViewSection::Gizmos, .name = "Particle Shapes", .value = &ParticleSystem::GetShowGizmos(), .tooltip = "粒の出る形（Scene ビューだけ。1つずつは Inspector の Show Gizmo）"},
	    {.section = ViewSection::Gizmos, .name = "Colliders", .value = &CollisionSystem::GetShowGizmos(), .tooltip = "Collider の線（Game ビューにも出る）"},
	    {.section = ViewSection::Gizmos, .name = "Hit Shapes", .value = &HitSystem::GetShowGizmos(), .tooltip = "攻撃の形・ジャスト判定の範囲・Hurtbox（Game ビューにも出る）"},
	    {.section = ViewSection::Gizmos, .name = "Skeletons", .value = &AnimationSystem::GetShowGizmos(), .tooltip = "骨（Game ビューにも出る）"},
	    // Scene View（レンズは Scene では掛けない）
	    {.section = ViewSection::SceneView, .name = "Post Effects", .value = &viewFlags.sceneEffects, .tooltip = "ブルームなどのポストエフェクト（レンズは掛けない。ギズモ・クリックの位置がずれるので）"},
	    {.section = ViewSection::SceneView, .name = "Fog", .value = &viewFlags.sceneFog, .tooltip = "霧（Fog Component）"},
	    {.section = ViewSection::SceneView, .name = "Skybox", .value = &viewFlags.sceneSkybox, .tooltip = "天球（外すと、ギズモが見やすい落ち着いたグラデーション）"},
	    {.section = ViewSection::SceneView, .name = "Depth of Field", .value = &viewFlags.sceneDepthOfField, .enabledBy = &viewFlags.sceneEffects, .tooltip = "被写界深度（Post Effects が入っているときだけ）"},
	    {.section = ViewSection::SceneView, .name = "Grid", .value = &viewFlags.sceneGrid, .tooltip = "グリッド"},
	    {.section = ViewSection::SceneView, .name = "Pick UI", .value = &viewFlags.scenePickUI, .tooltip = "Image・Text をクリックで選ぶ・ドラッグで動かす（外すと、UI の後ろの 3D の物を選べる）"},
	    // Game View
	    {.section = ViewSection::GameView, .name = "Grid", .value = &viewFlags.gameGrid, .tooltip = "グリッド（遊ぶときと同じ見た目にしたいので、初めは描かない）"},
	};
	for (Toggle& toggle : Toggles()) {
		toggle.saved = *toggle.value;
	}

	Load();

	// --- Settings → Editor → View ---
	SettingsWindow::AddPage({
	    .name = kPageName,
	    .section = SettingsSection::Editor,
	    .draw = &ViewSettings::DrawPage,
	    .isDirty = &ViewSettings::IsDirty,
	    .revert = &ViewSettings::Revert,
	    .save = &ViewSettings::Save,
	    .saveLocation = kSavePath,
	    .revertTooltip = "保存した見え方に戻します",
	});
}

// ===== 切り替えの一覧 =====
std::vector<ViewSettings::Toggle>& ViewSettings::Toggles() {
	static std::vector<Toggle> toggles;
	return toggles;
}

// ===== 見出しの名前 =====
const char* ViewSettings::SectionName(ViewSection section) {
	switch (section) {
	case ViewSection::Gizmos:
		return "Gizmos";
	case ViewSection::SceneView:
		return "Scene View";
	case ViewSection::GameView:
		return "Game View";
	}
	return "";
}


//=============================================================================
// View メニュー
//=============================================================================
// ===== メニュー =====
void ViewSettings::DrawMenu() {
	if (!ImGui::BeginMenu("View")) {
		return;
	}
	// 見出しごとの子メニュー（カーソルを合わせると横に出る）
	for (const ViewSection section : magic_enum::enum_values<ViewSection>()) {
		if (!ImGui::BeginMenu(SectionName(section))) {
			continue;
		}
		// 押してもメニューを閉じない（いくつも続けて切り替えられるように。外をクリックか Esc で閉じる）
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
		for (const Toggle& toggle : Toggles()) {
			if (toggle.section == section) {
				DrawMenuItem(toggle);
			}
		}
		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}
	ImGui::EndMenu();
}

// ===== メニューの1行 =====
void ViewSettings::DrawMenuItem(const Toggle& toggle) {
	const bool enabled = (toggle.enabledBy == nullptr) || *toggle.enabledBy;
	ImGui::MenuItem(toggle.name.c_str(), nullptr, toggle.value, enabled);
	if (!toggle.tooltip.empty()) {
		ImGui::SetItemTooltip("%s", toggle.tooltip.c_str());
	}
}


//=============================================================================
// 保存
//=============================================================================
// ===== 読む（無い・読めない項目は、今の値のまま）=====
void ViewSettings::Load() {
	std::ifstream stream(kSavePath);
	if (!stream) {
		return; // まだ保存していない
	}
	const json root = json::parse(stream, nullptr, false); // false: 壊れていても例外を投げない
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error(std::string("[ViewSettings] 見え方のファイルを読めません（初めの値のままにします）: ") + kSavePath);
		return;
	}
	for (Toggle& toggle : Toggles()) {
		const auto section = root.find(SectionName(toggle.section));
		if (section == root.end() || !section->is_object()) {
			continue;
		}
		const auto value = section->find(toggle.name);
		if (value != section->end() && value->is_boolean()) {
			*toggle.value = value->get<bool>();
			toggle.saved = *toggle.value;
		}
	}
}

// ===== 書く（全部の切り替えを、見出しごとに）=====
void ViewSettings::Save() {
	json root = json::object();
	for (const Toggle& toggle : Toggles()) {
		root[SectionName(toggle.section)][toggle.name] = *toggle.value;
	}
	std::error_code error;
	std::filesystem::create_directories(std::filesystem::path(kSavePath).parent_path(), error);
	std::ofstream stream(kSavePath);
	if (!stream) {
		LogManager::Error(std::string("[ViewSettings] 見え方のファイルを書けません: ") + kSavePath);
		return;
	}
	stream << std::setw(4) << root << std::endl;
	for (Toggle& toggle : Toggles()) {
		toggle.saved = *toggle.value;
	}
}

// ===== 保存していない変更があるか =====
bool ViewSettings::IsDirty() {
	for (const Toggle& toggle : Toggles()) {
		if (*toggle.value != toggle.saved) {
			return true;
		}
	}
	return false;
}

// ===== 保存した値に戻す =====
void ViewSettings::Revert() {
	for (const Toggle& toggle : Toggles()) {
		*toggle.value = toggle.saved;
	}
}


//=============================================================================
// Settings → Editor → View
//=============================================================================
void ViewSettings::DrawPage() {
	for (const ViewSection section : magic_enum::enum_values<ViewSection>()) {
		ImGui::SeparatorText(SectionName(section));
		ImGui::PushID(static_cast<int>(section)); // Scene View と Game View の Grid が同じ ID にならないように
		for (const Toggle& toggle : Toggles()) {
			if (toggle.section != section) {
				continue;
			}
			const bool enabled = (toggle.enabledBy == nullptr) || *toggle.enabledBy;
			ImGui::BeginDisabled(!enabled);
			ImGui::Checkbox(toggle.name.c_str(), toggle.value);
			ImGui::EndDisabled();
			if (!toggle.tooltip.empty()) {
				ImGui::SetItemTooltip("%s", toggle.tooltip.c_str());
			}
		}
		ImGui::PopID();
	}
}
#endif
