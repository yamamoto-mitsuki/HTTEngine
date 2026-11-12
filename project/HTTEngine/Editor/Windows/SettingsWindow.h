#pragma once
#include <functional>
#include <string>
#include <vector>

// 前方宣言（GlobalVariables.h。json まで読まないように）
enum class SettingsSection;

/// <summary>
/// GlobalVariables のグループではない、Settings のページ1つ（Keymap など。中身と保存は持ち主が受け持つ）
/// </summary>
struct SettingsPage {
	std::string name;              // 一覧に出す名前（GlobalVariables のグループの名前とかぶらないように）
	SettingsSection section;       // 一覧のどの見出しに出すか
	std::function<void()> draw;    // 中身を描く（名前・Revert・Save の見出しは Settings ウィンドウが描く）
	std::function<bool()> isDirty; // 保存していない変更があるか（* を付ける・Revert と Save を押せるようにする）
	std::function<void()> revert;  // 保存した状態に戻す
	std::function<void()> save;    // 保存する
	std::string saveLocation;      // 保存先（見出しの下に出す）
	std::string revertTooltip;     // Revert の説明
};

/// <summary>
/// Settings ウィンドウ（ゲーム全体・シーン・エディタの設定を1か所に。Unity の Project Settings と同じ形）
/// <para>左に一覧（Project／Scene／Editor の見出し）、右に選んだページ。1ページ＝ GlobalVariables のグループ1つか、AddPage で足したページ1つ</para>
/// <para>保存していない変更があるページには * が付く。Save でファイルに書き、Revert で保存した値に戻す</para>
/// </summary>
class SettingsWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();
	// ウィンドウを前に出す（Edit → Settings）
	static void Focus() { focusRequest_ = true; }
	// GlobalVariables のグループではないページを足す（起動のときに1回。同じ見出しの、GlobalVariables のグループの後に出る）
	static void AddPage(SettingsPage page);

private:
	// 見出しのボタンで押された物
	enum class HeaderAction {
		None,
		Revert,
		Save,
	};

	// 左の一覧
	static void DrawPageList();
	// 一覧の1行（押すとそのページを選ぶ）
	static void DrawPageItem(const std::string& name, bool isDirty);
	// 右のページ
	static void DrawPage();
	// ページの見出し（名前（* 付き）と、右端に Revert・Save。その下に保存先）
	static HeaderAction DrawPageHeader(const std::string& name, bool isDirty, const std::string& saveLocation, const char* revertTooltip);
	// AddPage で足したページ（無ければ nullptr）
	static const SettingsPage* FindPage(const std::string& name);
	// そのページ（グループか、足したページ）があるか
	static bool HasPage(const std::string& name);

	static std::string selected_;            // 選んでいるページ（グループの名前）
	static bool focusRequest_;               // 次のフレームでウィンドウを前に出す
	static std::vector<SettingsPage> pages_; // AddPage で足したページ
};
