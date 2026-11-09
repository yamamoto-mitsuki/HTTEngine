#pragma once
#include <string>

/// <summary>
/// シーンのファイルの操作で出す確認のウィンドウと、File メニューの中身
/// <para>保存していない変更があるまま別のシーンを開くときは「保存しますか？（Save / Don't Save / Cancel）」を聞く</para>
/// <para>New Scene・Save As は名前を聞く（使える文字は英数字と _ -。FileNameRule.h）</para>
/// <para>シーンを開く・作る・名前を付けて保存するのは停止中だけ（再生中はメニューが押せない）</para>
/// </summary>
class SceneDialogs {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）。確認のウィンドウとショートカット（Ctrl+N・Ctrl+Shift+S）
	static void Draw();
	// File メニューの中身（ImGui::BeginMenu("File") の中から呼ぶ）
	static void DrawFileMenuItems();

	// name のシーンを開く。保存していない変更があれば、先に聞く（Project ウィンドウのダブルクリックからも）
	static void RequestOpen(const std::string& name);
	// 名前を聞いて、新しいシーンを作って開く
	static void RequestNew();
	// 名前を聞いて、今のシーンをその名前で保存し、開き直す
	static void RequestSaveAs();

private:
	// 確認のウィンドウの種類
	enum class Dialog {
		None,
		Unsaved,  // 保存しますか？
		NewScene, // 新しいシーンの名前
		SaveAs,   // 名前を付けて保存
	};
	static const char* TitleOf(Dialog dialog);
	static void DrawUnsavedDialog();
	// 名前を聞くウィンドウ（NewScene・SaveAs で共通）
	static void DrawNameDialog(Dialog dialog);
	static void HandleShortcuts();

	static Dialog openRequest_;      // 次のフレームに開く確認のウィンドウ（ほかのポップアップの中から開くと入れ子になるので、1フレーム預かる）
	static std::string pendingOpen_; // 「保存しますか？」に答えた後に開くシーン
	static std::string nameBuffer_;  // 名前の入力欄の中身
	static bool nameFocusRequest_;   // 名前の入力欄を出した最初のフレームだけ true（すぐに打てるように）
};