#ifdef USE_IMGUI
#include "EditorKeymap.h"

#include <array>
#include <format>
#include <utility>
#include <vector>

#include <Windows.h>
#include <dinput.h>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Editor/Widgets/InputActionTable.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
constexpr const char* kSavePath = "Parameters/Editor/Keymap.json"; // 人ごとの割り当て（Parameters/Editor/ は git に入れない）
constexpr const char* kPageName = "Keymap";                        // Settings の一覧の名前

// --- 効く場所（ぶつかりを調べる範囲）---
constexpr const char* kContextSceneView = "SceneView"; // Scene ビューの上でだけ
constexpr const char* kContextAnywhere = "";            // どこでも（どの場所の割り当てともぶつかる）

// --- 割り当てられる入力 ---
// ドラッグの操作：押している間マウスを動かす物なので、マウスのボタンか、キー（押しながらマウスを動かす）
constexpr InputDeviceMask kDragDevices = InputDeviceMask::Key | InputDeviceMask::MouseButton;
// エディタで読む入力（パッド・マウスの動きは割り当てない）
constexpr InputDeviceMask kEditorDevices = InputDeviceMask::Key | InputDeviceMask::MouseButton | InputDeviceMask::MouseWheel;

//=============================================================================
// 操作の一覧（EditorAction と同じ順番）
//=============================================================================
struct ActionDef {
	EditorAction action; // 順番を確かめるため
	InputActionDesc desc;
};

std::vector<ActionDef> MakeActionDescs() {
	using Mod = InputModifiers;
	using Type = InputActionType;
	return {
	    // ===== Scene ビューのカメラ =====
	    {EditorAction::SceneRotate, {.name = "SceneView.Rotate", .label = "回転（ドラッグ）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                                 .tooltip = "押している間、マウスの動きでカメラの向きを変える（その場で回る）", .type = Type::Button, .accepts = kDragDevices,
	                                 .defaults = {InputBinding::Mouse(2)}}},
	    {EditorAction::ScenePan, {.name = "SceneView.Pan", .label = "平行移動（ドラッグ）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                              .tooltip = "押している間、マウスの動きでカメラを上下左右に動かす", .type = Type::Button, .accepts = kDragDevices,
	                              .defaults = {InputBinding::Mouse(1)}}},
	    {EditorAction::SceneDollyDrag, {.name = "SceneView.DollyDrag", .label = "前後に動く（ドラッグ）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                                    .tooltip = "押している間、マウスを上へ動かすと前へ、下へ動かすと後ろへ動く", .type = Type::Button, .accepts = kDragDevices,
	                                    .defaults = {}}},
	    {EditorAction::SceneDollyWheel, {.name = "SceneView.DollyWheel", .label = "前後に動く（ホイール）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                                     .tooltip = "ホイールを奥へ回すと前へ、手前へ回すと後ろへ動く", .type = Type::Value, .accepts = InputDeviceMask::MouseWheel,
	                                     .defaults = {InputBinding::Wheel()}}},
	    // ===== Scene ビュー =====
	    {EditorAction::SceneSelect, {.name = "SceneView.Select", .label = "選ぶ（クリック）", .group = "Scene View", .context = kContextSceneView,
	                                 .tooltip = "クリックで選ぶ。ギズモ・UI のドラッグも左クリックなので変えられない（ほかの操作とぶつかっていないかを見るために出している）",
	                                 .type = Type::Button, .accepts = InputDeviceMask::MouseButton, .isFixed = true, .defaults = {InputBinding::Mouse(0)}}},
	    {EditorAction::ToolMove, {.name = "SceneView.ToolMove", .label = "移動のギズモ", .group = "Scene View", .context = kContextSceneView,
	                              .tooltip = "ギズモを移動にする", .defaults = {InputBinding::Key(DIK_W)}}},
	    {EditorAction::ToolRotate, {.name = "SceneView.ToolRotate", .label = "回転のギズモ", .group = "Scene View", .context = kContextSceneView,
	                                .tooltip = "ギズモを回転にする", .defaults = {InputBinding::Key(DIK_E)}}},
	    {EditorAction::ToolScale, {.name = "SceneView.ToolScale", .label = "拡縮のギズモ", .group = "Scene View", .context = kContextSceneView,
	                               .tooltip = "ギズモを拡縮にする", .defaults = {InputBinding::Key(DIK_R)}}},
	    // ===== 編集 =====
	    {EditorAction::Undo, {.name = "Edit.Undo", .label = "元に戻す", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "Undo（文字の入力中は、入力欄の文字を戻す）", .defaults = {InputBinding::Key(DIK_Z, Mod::Ctrl)}}},
	    {EditorAction::Redo, {.name = "Edit.Redo", .label = "やり直す", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "Redo", .defaults = {InputBinding::Key(DIK_Y, Mod::Ctrl), InputBinding::Key(DIK_Z, Mod::Ctrl | Mod::Shift)}}},
	    {EditorAction::Delete, {.name = "Edit.Delete", .label = "消す", .group = "編集", .context = kContextAnywhere,
	                            .tooltip = "選んだ物を消す（Hierarchy・Project・Timeline・Curve・点の編集）", .defaults = {InputBinding::Key(DIK_DELETE)}}},
	    {EditorAction::Rename, {.name = "Edit.Rename", .label = "名前を変える", .group = "編集", .context = kContextAnywhere,
	                            .tooltip = "選んだ物の名前を変える（Hierarchy・Project）", .defaults = {InputBinding::Key(DIK_F2)}}},
	    {EditorAction::Copy, {.name = "Edit.Copy", .label = "コピー", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "選んだ Entity をコピーする（Hierarchy）", .defaults = {InputBinding::Key(DIK_C, Mod::Ctrl)}}},
	    {EditorAction::Paste, {.name = "Edit.Paste", .label = "貼り付け", .group = "編集", .context = kContextAnywhere,
	                           .tooltip = "コピーした Entity を貼る（Hierarchy）", .defaults = {InputBinding::Key(DIK_V, Mod::Ctrl)}}},
	    {EditorAction::Duplicate, {.name = "Edit.Duplicate", .label = "複製", .group = "編集", .context = kContextAnywhere,
	                               .tooltip = "選んだ Entity を複製する（Hierarchy）", .defaults = {InputBinding::Key(DIK_D, Mod::Ctrl)}}},
	    {EditorAction::Frame, {.name = "Edit.Frame", .label = "画面に入れる", .group = "編集", .context = kContextAnywhere,
	                           .tooltip = "選んだ物へ寄る（Scene ビュー）・全体が見える範囲にする（Timeline・Curve）", .defaults = {InputBinding::Key(DIK_F)}}},
	    {EditorAction::Find, {.name = "Edit.Find", .label = "検索", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "検索欄へ（Code ウィンドウ）", .defaults = {InputBinding::Key(DIK_F, Mod::Ctrl)}}},
	    // ===== シーンのファイル =====
	    {EditorAction::NewScene, {.name = "File.NewScene", .label = "新しいシーン", .group = "シーンのファイル", .context = kContextAnywhere,
	                              .tooltip = "新しいシーンを作る", .defaults = {InputBinding::Key(DIK_N, Mod::Ctrl)}}},
	    {EditorAction::SaveScene, {.name = "File.SaveScene", .label = "保存", .group = "シーンのファイル", .context = kContextAnywhere,
	                               .tooltip = "開いているシーンを保存する（停止中だけ）", .defaults = {InputBinding::Key(DIK_S, Mod::Ctrl)}}},
	    {EditorAction::SaveSceneAs, {.name = "File.SaveSceneAs", .label = "名前を付けて保存", .group = "シーンのファイル", .context = kContextAnywhere,
	                                 .tooltip = "開いているシーンを、別の名前で保存する", .defaults = {InputBinding::Key(DIK_S, Mod::Ctrl | Mod::Shift)}}},
	};
}

//=============================================================================
// カメラの操作のプリセット（ほかのツールに慣れている人向け。Scene ビューのカメラの3つだけを入れ替える）
//=============================================================================
struct CameraPreset {
	const char* name;
	const char* tooltip;
	InputBinding rotate;    // 回転（ドラッグ）
	InputBinding pan;       // 平行移動（ドラッグ）
	InputBinding dollyDrag; // 前後に動く（ドラッグ）。空なら割り当てなし
};
constexpr auto kCameraPresets = std::to_array<CameraPreset>({
    {"HTTEngine", "ホイールボタンで回転、右ドラッグで平行移動（既定）", InputBinding::Mouse(2), InputBinding::Mouse(1), {}},
    {"Unity 風", "右ドラッグで回転、ホイールボタンで平行移動、Alt+右ドラッグで前後", InputBinding::Mouse(1), InputBinding::Mouse(2), InputBinding::Mouse(1, InputModifiers::Alt)},
    {"Blender 風", "ホイールボタンで回転、Shift+ホイールボタンで平行移動、Ctrl+ホイールボタンで前後（Blender と違い、注視点のまわりではなく、その場で回る）",
     InputBinding::Mouse(2), InputBinding::Mouse(2, InputModifiers::Shift), InputBinding::Mouse(2, InputModifiers::Ctrl)},
});

//=============================================================================
// 持ち物
//=============================================================================
// 割り当ての表（エディタの決まりで読む・修飾キーはちょうど同じときだけ）
InputActionMap& Map() {
	static InputActionMap map(InputGate::Editor, InputModifierMatch::Exact, kEditorDevices);
	return map;
}
// Keymap ページの表
InputActionTable& Table() {
	static InputActionTable table;
	return table;
}
// EditorAction → 表の番号（足した順番が EditorAction の順番）
InputActionId ToId(EditorAction action) { return {static_cast<uint32_t>(action)}; }
// 割り当て1つ → 割り当ての一覧（空なら割り当てなし）
std::vector<InputBinding> ToList(const InputBinding& binding) { return binding.IsEmpty() ? std::vector<InputBinding>{} : std::vector<InputBinding>{binding}; }
} // namespace


//=============================================================================
// 初期化・毎フレーム
//=============================================================================
// ===== 初期化 =====
void EditorKeymap::Initialize() {
	InputActionMap& map = Map();
	for (ActionDef& def : MakeActionDescs()) {
		const InputActionId id = map.AddAction(std::move(def.desc));
		HTT_ASSERT_MSG(id == ToId(def.action), "MakeActionDescs の順番が EditorAction と違います");
	}
	HTT_ASSERT_MSG(map.GetActionCount() == magic_enum::enum_count<EditorAction>(), "MakeActionDescs に足りない EditorAction があります");
	map.LoadOverrides(kSavePath);

	// --- Settings → Editor → Keymap ---
	SettingsWindow::AddPage({
	    .name = kPageName,
	    .section = SettingsSection::Editor,
	    .draw = &EditorKeymap::DrawPage,
	    .isDirty = [] { return Map().IsDirty(); },
	    .revert = [] { Map().RevertToSaved(); },
	    .save = [] { Map().SaveOverrides(kSavePath); },
	    .saveLocation = kSavePath,
	    .revertTooltip = "保存した割り当てに戻します",
	});
}

// ===== 毎フレーム =====
void EditorKeymap::Update() {
	// 割り当てを拾う（入力を読めるのはここだけ。Keymap ページを閉じていたら、拾うのをやめる）
	const bool wasListening = Table().IsListening();
	Table().UpdateListening(Map());
	// 拾っている間（拾えたフレームも）は、どの操作も効かせない（Ctrl+Z を拾うときに Undo が動かないように）
	Map().SetEnabled(!wasListening);
	Map().Update();
}


//=============================================================================
// 聞く
//=============================================================================
bool EditorKeymap::IsPressed(EditorAction action) { return Map().IsPressed(ToId(action)); }
bool EditorKeymap::IsTriggered(EditorAction action) { return Map().IsTriggered(ToId(action)); }
float EditorKeymap::GetValue(EditorAction action) { return Map().GetValue(ToId(action)); }

// ===== 画面に出す文字 =====
std::string EditorKeymap::GetShortcutText(EditorAction action) { return Map().GetDisplayText(ToId(action)); }

std::string EditorKeymap::WithShortcut(std::string_view text, EditorAction action) {
	const std::string shortcut = GetShortcutText(action);
	if (shortcut.empty()) {
		return std::string(text);
	}
	return std::format("{}（{}）", text, shortcut);
}


//=============================================================================
// Settings の Keymap ページ
//=============================================================================
void EditorKeymap::DrawPage() {
	InputActionMap& map = Map();

	// --- カメラの操作の型 ---
	ImGui::SeparatorText("カメラの操作の型");
	for (size_t i = 0; i < kCameraPresets.size(); ++i) {
		const CameraPreset& preset = kCameraPresets[i];
		if (i > 0) {
			ImGui::SameLine();
		}
		if (ImGui::Button(preset.name)) {
			map.SetBindings(ToId(EditorAction::SceneRotate), ToList(preset.rotate));
			map.SetBindings(ToId(EditorAction::ScenePan), ToList(preset.pan));
			map.SetBindings(ToId(EditorAction::SceneDollyDrag), ToList(preset.dollyDrag));
		}
		ImGui::SetItemTooltip("%s", preset.tooltip);
	}
	ImGui::TextDisabled("Scene View のカメラの回転・平行移動・前後（ドラッグ）だけを入れ替えます。残すなら Save");
	if (ImGui::Button("全部を既定に戻す")) {
		map.ResetAllToDefault();
	}
	ImGui::SetItemTooltip("全部の操作を、エンジンの既定の割り当てに戻す（残すなら Save）");

	// --- 全部の操作 ---
	Table().Draw(map);
}
#endif
