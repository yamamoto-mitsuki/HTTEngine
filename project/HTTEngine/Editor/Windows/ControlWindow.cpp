#ifdef USE_IMGUI
#include "ControlWindow.h"

#include <array>
#include <string>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Editor/Gizmos/SceneGizmo.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Widgets/ToolIcons.h"
#include "HTTEngine/Editor/Windows/SceneDialogs.h"
#include "HTTEngine/Scene/IScene.h"
#include "HTTEngine/Scene/SceneManager.h"

namespace {
constexpr ImU32 kPlayColor = IM_COL32(110, 210, 120, 255);             // 再生の三角（緑）
constexpr ImU32 kStopColor = IM_COL32(235, 110, 100, 255);             // 停止の四角（赤）
constexpr ImVec4 kPlayingTextColor = ImVec4(0.45f, 0.85f, 0.5f, 1.0f); // 「Playing」の文字の色
constexpr ImVec4 kPausedTextColor = ImVec4(0.95f, 0.8f, 0.35f, 1.0f);  // 「Paused」の文字の色
constexpr float kGroupSpacing = 14.0f;   // ボタンのまとまりと、まとまりの間
constexpr float kToolSpacing = 2.0f;     // 同じまとまりの中のボタンの間
constexpr float kSnapFieldWidth = 72.0f; // スナップの量の入力欄の幅

/// <summary>
/// ギズモの道具のボタン1つ分
/// </summary>
struct ToolButton {
	GizmoOperation operation; // 押すと、この操作になる
	ToolIcon icon;            // 絵
	const char* id;           // ボタンの ID
	const char* name;         // 説明（後ろに、今の割り当てのキーを付ける）
	EditorAction shortcut;    // Scene ビューで切り替えるキー
};
constexpr auto kToolButtons = std::to_array<ToolButton>({
    {GizmoOperation::Move,   ToolIcon::Move,   "move",   "移動", EditorAction::ToolMove},
    {GizmoOperation::Rotate, ToolIcon::Rotate, "rotate", "回転", EditorAction::ToolRotate},
    {GizmoOperation::Scale,  ToolIcon::Scale,  "scale",  "拡縮", EditorAction::ToolScale},
});
} // namespace


//=============================================================================
// 描画
//=============================================================================
void ControlWindow::Draw(SceneManager* sceneManager) {
	if (sceneManager == nullptr) {
		return;
	}
	ImGui::Begin("Control");
	// 1行目：左に道具、真ん中に再生、右に保存
	DrawGizmoTools();
	DrawPlayButtons(*sceneManager);
	DrawSaveButton();
	// 2行目：状態とシーン
	DrawStatus(*sceneManager);
	ImGui::End();
}

//=============================================================================
// 左：ギズモの道具
//=============================================================================
void ControlWindow::DrawGizmoTools() {
	GizmoSettings& settings = SceneGizmo::GetSettings();

	// --- 移動・回転・拡縮（Scene ビューで W・E・R（EditorKeymap の割り当て）を押しても切り替わる）---
	for (const ToolButton& tool : kToolButtons) {
		const std::string tooltip = EditorKeymap::WithShortcut(tool.name, tool.shortcut); // "移動（W）"
		if (ToolIcons::Button(tool.id, tool.icon, settings.operation == tool.operation, tooltip.c_str())) {
			settings.operation = tool.operation;
		}
		ImGui::SameLine(0.0f, kToolSpacing);
	}

	// --- 軸の向き（押すたびに切り替わる。絵は今の向き）---
	ImGui::SameLine(0.0f, kGroupSpacing);
	const bool isLocal = (settings.space == GizmoSpace::Local);
	const char* spaceTooltip = isLocal ? "軸の向き：物の向き（押すとワールドの向き）。拡縮はいつも物の向き" : "軸の向き：ワールドの向き（押すと物の向き）。拡縮はいつも物の向き";
	if (ToolIcons::Button("space", isLocal ? ToolIcon::Local : ToolIcon::World, false, spaceTooltip)) {
		settings.space = isLocal ? GizmoSpace::World : GizmoSpace::Local;
	}

	// --- スナップ（オン・オフと、今の道具の刻み）---
	ImGui::SameLine(0.0f, kToolSpacing);
	if (ToolIcons::Button("snap", ToolIcon::Snap, settings.snap, "スナップ：決まった量ずつ動かす（Ctrl を押している間は、オン・オフが逆になる）")) {
		settings.snap = !settings.snap;
	}
	ImGui::SameLine(0.0f, kToolSpacing);
	ImGui::SetNextItemWidth(kSnapFieldWidth);
	switch (settings.operation) {
	case GizmoOperation::Move:
		ImGui::DragFloat("##moveSnap", &settings.moveSnap, 0.01f, 0.01f, 100.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("移動の刻み（長さ）");
		break;
	case GizmoOperation::Rotate:
		ImGui::DragFloat("##rotateSnap", &settings.rotateSnap, 0.5f, 1.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("回転の刻み（度）");
		break;
	case GizmoOperation::Scale:
		ImGui::DragFloat("##scaleSnap", &settings.scaleSnap, 0.01f, 0.01f, 10.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("拡縮の刻み（倍）");
		break;
	}
}

//=============================================================================
// 真ん中：再生
//=============================================================================
void ControlWindow::DrawPlayButtons(SceneManager& sceneManager) {
	const PlayState state = sceneManager.GetPlayState();
	const bool isEditing = (state == PlayState::Editing);

	// --- ウィンドウの真ん中にそろえる（左の道具とぶつかるなら、道具のすぐ右）---
	ImGui::SameLine(0.0f, kGroupSpacing);
	const float groupWidth = ToolIcons::GetButtonWidth() * 3.0f + kToolSpacing * 2.0f;
	const float lineLeft = ImGui::GetCursorStartPos().x;                               // この行の左端
	const float lineRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端（スクロールバーの分は除かれる）
	const float centerX = lineLeft + (lineRight - lineLeft - groupWidth) * 0.5f;
	if (ImGui::GetCursorPosX() < centerX) {
		ImGui::SetCursorPosX(centerX);
	}

	// --- 再生・停止（停止中は ▶、再生中・一時停止中は ■）---
	if (isEditing) {
		if (ToolIcons::Button("play", ToolIcon::Play, false, "再生", kPlayColor)) {
			sceneManager.Play();
		}
	} else {
		if (ToolIcons::Button("stop", ToolIcon::Stop, true, "停止（Play を押した時の状態に戻す）", kStopColor)) {
			sceneManager.Stop();
		}
	}

	// --- 一時停止・再開（停止中は押せない）---
	ImGui::SameLine(0.0f, kToolSpacing);
	const bool isPaused = (state == PlayState::Paused);
	ImGui::BeginDisabled(isEditing);
	if (ToolIcons::Button("pause", ToolIcon::Pause, isPaused, isPaused ? "再開" : "一時停止")) {
		sceneManager.Pause();
	}
	ImGui::EndDisabled();

	// --- やり直し（停止中は押せない。停止中の作り直しは「シーンファイルから読み直す」になり、保存していない編集が消えるので）---
	ImGui::SameLine(0.0f, kToolSpacing);
	ImGui::BeginDisabled(isEditing);
	if (ToolIcons::Button("restart", ToolIcon::Restart, false, "やり直し（Play を押した瞬間の状態から）")) {
		sceneManager.RequestReload();
	}
	ImGui::EndDisabled();
}

//=============================================================================
// 右：保存
//=============================================================================
void ControlWindow::DrawSaveButton() {
	// 保存の中身（シーン＋エディタの設定）と、押せるかは SceneDialogs が決める（File → Save と同じ）
	const bool canSave = SceneDialogs::CanSave();

	// --- 右端に寄せる ---
	const char* label = "Save";
	const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SameLine(0.0f, kGroupSpacing);
	const float available = ImGui::GetContentRegionAvail().x;
	if (available > width) {
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
	}
	ImGui::BeginDisabled(!canSave);
	if (ImGui::Button(label)) {
		SceneDialogs::RequestSave();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("%s。シーンは停止中だけ", EditorKeymap::WithShortcut("シーンとエディタの設定（Scene ビューのカメラ・View・Keymap など）を保存", EditorAction::SaveScene).c_str());

	// --- Ctrl+S（文字の入力中は、入力欄のほうを優先する。名前の変更中に Ctrl+S を押しても保存しない）---
	const bool isTyping = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
	if (canSave && !isTyping && EditorKeymap::IsTriggered(EditorAction::SaveScene)) {
		SceneDialogs::RequestSave();
	}
}

//=============================================================================
// 下の行：状態とシーン
//=============================================================================
void ControlWindow::DrawStatus(SceneManager& sceneManager) {
	// --- 今の状態 ---
	switch (sceneManager.GetPlayState()) {
	case PlayState::Playing:
		ImGui::TextColored(kPlayingTextColor, "[Playing]");
		break;
	case PlayState::Paused:
		ImGui::TextColored(kPausedTextColor, "[Paused]");
		break;
	case PlayState::Editing:
		ImGui::TextDisabled("[Editing]");
		break;
	}
	ImGui::SameLine();

	// --- 開いているシーン（再生中に LoadScene で別のシーンへ進んだら、今のシーンも出す）---
	const char* sceneFile = sceneManager.GetSceneFile();
	const std::string& openName = sceneManager.GetOpenSceneName();
	const IScene* scene = sceneManager.GetCurrentScene();
	const std::string currentName = scene ? scene->GetSceneName() : std::string();
	const char* dirtyMark = sceneManager.IsDirty() ? " *" : ""; // 保存していない変更がある印
	if (currentName == openName) {
		ImGui::TextDisabled("Scene: %s%s  (%s)", openName.c_str(), dirtyMark, sceneFile ? sceneFile : "シーンファイルを使っていない"); // どのファイルに保存されるか
	} else {
		ImGui::TextDisabled("Scene: %s%s → 今は %s（Stop で %s に戻ります）", openName.c_str(), dirtyMark, currentName.c_str(), openName.c_str());
	}
}
#endif