#pragma once
#include <string>

/// <summary>
/// Settings ウィンドウ（ゲーム全体・シーン・エディタの設定を1か所に。Unity の Project Settings と同じ形）
/// <para>左に一覧（Project／Scene／Editor の見出し）、右に選んだページ。1ページ＝ GlobalVariables のグループ1つ</para>
/// <para>保存していない変更があるページには * が付く。Save でファイルに書き、Revert で保存した値に戻す</para>
/// </summary>
class SettingsWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();
	// ウィンドウを前に出す（Edit → Settings）
	static void Focus() { focusRequest_ = true; }

private:
	// 左の一覧
	static void DrawPageList();
	// 一覧の1行（押すとそのページを選ぶ）
	static void DrawPageItem(const std::string& groupName);
	// 右のページ
	static void DrawPage();

	static std::string selected_; // 選んでいるページ（グループの名前）
	static bool focusRequest_;    // 次のフレームでウィンドウを前に出す
};