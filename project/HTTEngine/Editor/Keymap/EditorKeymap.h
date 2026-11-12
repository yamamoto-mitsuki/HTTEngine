#pragma once
#include <cstdint>
#include <string>
#include <string_view>

/// <summary>
/// エディタの操作（Settings → Editor → Keymap で、割り当てを人ごとに変えられる物）
/// <para>足すときは、EditorKeymap.cpp の MakeActionDescs にも同じ順番で足す</para>
/// </summary>
enum class EditorAction : uint8_t {
	// --- Scene ビューのカメラ（Scene ビューの上でだけ効く）---
	SceneRotate,     // 押している間、マウスの動きで向きを変える（その場で回る）
	ScenePan,        // 押している間、マウスの動きで平行に動く
	SceneDollyDrag,  // 押している間、マウスの上下で前後に動く
	SceneDollyWheel, // ホイールで前後に動く
	// --- Scene ビュー ---
	SceneSelect, // クリックで選ぶ（ギズモ・UI のドラッグも左クリックなので、変えられない）
	ToolMove,    // 移動のギズモにする
	ToolRotate,  // 回転のギズモにする
	ToolScale,   // 拡縮のギズモにする
	// --- 編集（どのウィンドウでも。何に効くかは、触っているウィンドウが決める）---
	Undo,
	Redo,
	Delete,    // 選んだ物を消す（Hierarchy・Project・Timeline・Curve・点の編集）
	Rename,    // 名前を変える（Hierarchy・Project）
	Copy,      // Hierarchy
	Paste,     // Hierarchy
	Duplicate, // Hierarchy
	Frame,     // 選んだ物・全体を画面に入れる（Scene ビュー・Timeline・Curve）
	Find,      // 検索欄へ（Code ウィンドウ）
	// --- シーンのファイル ---
	NewScene,
	SaveScene,
	SaveSceneAs,
};

/// <summary>
/// エディタの操作の割り当て（Unity の Shortcuts・Blender の Keymap と同じ役目）
/// <para>キー・ボタンを直接読まず、EditorAction で聞く。割り当ては Settings → Editor → Keymap で変え、Parameters/Editor/Keymap.json に保存する（git に入れないので、人ごとに違ってよい）</para>
/// <para>読むのは InputGate::Editor（ImGui のパネルを触っていても読める。文字を打っている間は読まない）。修飾キーはちょうど同じときだけ（Ctrl+Z は Ctrl+Shift+Z では効かない）</para>
/// <para>中身は Input Action の表（InputActionMap）。ゲームのキーコンフィグも同じ表で作る</para>
/// </summary>
class EditorKeymap {
public:
	// 操作を登録し、保存した割り当てを読み、Settings にページを足す（ImGuiManager::Initialize から1回）
	static void Initialize();
	// 入力を読む（毎フレーム1回。WindowManager::UpdateAll で、Scene ビューのカメラを動かす前）
	static void Update();

	// --- 聞く ---
	static bool IsPressed(EditorAction action);   // 押している間（ドラッグの操作）
	static bool IsTriggered(EditorAction action); // 押した瞬間（ショートカット）
	static float GetValue(EditorAction action);   // 量（ホイールの目盛り）

	// --- 画面に出す文字 ---
	// 割り当て（"Ctrl+Z"。割り当てが無ければ空）。メニューのショートカットの欄に
	static std::string GetShortcutText(EditorAction action);
	// "移動（W）" のように、説明の後ろに割り当てを付ける（割り当てが無ければ説明だけ）。ボタンの吹き出しに
	static std::string WithShortcut(std::string_view text, EditorAction action);

private:
	// Settings の Keymap ページの中身（プリセットと表）
	static void DrawPage();
};
