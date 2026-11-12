#pragma once
#include <cstdint>
#include <functional>
#include <string>

/// <summary>
/// シーンのファイルの操作で出す確認のウィンドウと、File メニューの中身
/// <para>Save は「開いているシーン」と「エディタの設定（Settings → Editor の全ページ・ウィンドウの配置）」をまとめて保存する（RequestSave。Control ウィンドウの Save・Ctrl+S・File → Save が全部ここを通る）</para>
/// <para>保存していない変更があるまま別のシーンを開く・エディタを閉じるときは「保存しますか？（Save / Don't Save / Cancel）」を聞く</para>
/// <para>New Scene・Save As は名前を聞く（使える文字は英数字と _ -。FileNameRule.h）</para>
/// <para>シーンを開く・作る・保存するのは停止中だけ（再生中はメニューが押せない。エディタの設定は再生中でも保存できる）</para>
/// </summary>
class SceneDialogs {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）。確認のウィンドウとショートカット（Ctrl+N・Ctrl+Shift+S）
	static void Draw();
	// File メニューの中身（ImGui::BeginMenu("File") の中から呼ぶ）
	static void DrawFileMenuItems();

	// --- 保存 ---
	// 保存する物があるか（シーンを保存できる・エディタの設定に保存していない変更がある）。Save のボタン・メニューを押せるか
	static bool CanSave();
	// 開いているシーン（保存できるときだけ。書くのは次のフレームの頭）と、エディタの設定（すぐ書く）を保存する
	static void RequestSave();

	// --- シーンを開く・作る ---
	// name のシーンを開く。保存していない変更があれば、先に聞く（Project ウィンドウのダブルクリックからも）
	static void RequestOpen(const std::string& name);
	// 名前を聞いて、新しいシーンを作って開く
	static void RequestNew();
	// 名前を聞いて、今のシーンをその名前で保存し、開き直す
	static void RequestSaveAs();

	// --- エディタを閉じる（ウィンドウの ×・Alt+F4。ImGuiManager がウィンドウにつなぐ）---
	// もう閉じてよいか（保存していない変更が無い・「保存しますか？」に答え終わった）
	static bool CanQuit();
	// 閉じてよいか聞く。答えが出て閉じてよくなったら close を呼ぶ（close はウィンドウにもう一度「閉じて」と送る）
	static void RequestQuit(std::function<void()> close);

private:
	// 確認のウィンドウの種類
	enum class Dialog {
		None,
		Unsaved,  // 保存しますか？（別のシーンを開く前）
		Quit,     // 保存しますか？（エディタを閉じる前）
		NewScene, // 新しいシーンの名前
		SaveAs,   // 名前を付けて保存
	};
	// エディタを閉じる流れ
	enum class QuitState : uint8_t {
		None,        // 閉じようとしていない
		WaitingSave, // 「保存しますか？」で Save を押した。シーンの保存（次のフレームの頭）が済むのを待っている
		Approved,    // 閉じてよい（ウィンドウに「閉じて」を送った）
	};
	static const char* TitleOf(Dialog dialog);
	static void DrawUnsavedDialog();
	static void DrawQuitDialog();
	// 名前を聞くウィンドウ（NewScene・SaveAs で共通）
	static void DrawNameDialog(Dialog dialog);
	static void HandleShortcuts();

	// --- 保存の中身 ---
	// 開いているシーンを保存できるか（停止中で、シーンファイルを使っている）
	static bool CanSaveScene();
	// 開いているシーンに保存していない変更があるか
	static bool IsSceneDirty();
	// 保存していない物が何かあるか（シーン・エディタの設定）
	static bool HasUnsavedChanges();

	// --- 閉じる ---
	// Save を押した後、保存が済んだかを見る（済んで、保存していない物が無くなったら閉じる）
	static void UpdateQuit();
	// 閉じてよいことにして、ウィンドウを閉じる
	static void ApproveQuit();

	static Dialog openRequest_;                // 次のフレームに開く確認のウィンドウ（ほかのポップアップの中から開くと入れ子になるので、1フレーム預かる）
	static std::string pendingOpen_;           // 「保存しますか？」に答えた後に開くシーン
	static std::string nameBuffer_;            // 名前の入力欄の中身
	static bool nameFocusRequest_;             // 名前の入力欄を出した最初のフレームだけ true（すぐに打てるように）
	static QuitState quitState_;               // エディタを閉じる流れのどこか
	static std::function<void()> closeWindow_; // 閉じてよくなったら呼ぶ（RequestQuit でもらう）
};
