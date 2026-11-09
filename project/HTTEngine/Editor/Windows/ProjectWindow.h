#pragma once
#include <string>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Editor/Assets/AssetIcons.h"

/// <summary>
/// Project ウィンドウ（resources/ と Source/ のファイルを見る・作る・開く・名前を変える・消す。Unity の Project ウィンドウと同じ形）
/// <para>左に resources/（素材）と Source/（ゲームのコード）のフォルダの木、右に選んだフォルダの中身（アイコンの並び。画像ファイルはその画像の小さい絵）</para>
/// <para>シーンはダブルクリックで開く。コードはダブルクリックで Code ウィンドウに出す（Source/ の中は、名前を変える・消すは Visual Studio で）</para>
/// <para>Create → Scene は、どのフォルダで押しても resources/scenes/ に作る（シーンは名前で探すので、置き場所は1つ）</para>
/// <para>ファイルは1秒ごとに読み直すので、エクスプローラーで足した物もすぐに出る</para>
/// <para>読む・描く側は ProjectWindow.cpp、操作の側は ProjectWindowActions.cpp。ファイルの道具は ProjectFiles</para>
/// </summary>
class ProjectWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();

private:
	// 右に並べる1つ（ファイルかフォルダ）
	struct Item {
		std::filesystem::path path;                // resources/scenes/Title.scene.json
		std::string name;                          // 出す名前（シーン・モデル・画像・音は拡張子を除く。シーンは "Title"）
		AssetIcon icon = AssetIcon::File;          // 種類
		bool isFolder = false;                     // フォルダか
		std::filesystem::file_time_type writeTime; // 更新時刻（画像が描き直されたら、小さい絵を作り直す）
	};
	// 左の木の1つ
	struct Folder {
		std::filesystem::path path;
		std::string name;
		std::vector<Folder> children;
	};

	// ===== 読む =====
	// フォルダの木と、今のフォルダの中身を読み直す
	static void Rescan();
	static Folder ScanFolderTree(const std::filesystem::path& path);
	static std::vector<Item> ScanItems(const std::filesystem::path& folder);

	// ===== 描く =====
	// 今のフォルダの場所（押すとそこへ戻る）と、アイコンの大きさ
	static void DrawToolbar();
	// 左の木（フォルダ1つ分。子がいれば入れ子で描く）
	static void DrawFolderTree(const Folder& folder);
	// 右の中身（アイコンの並び、または1行ずつの並び）
	static void DrawItems();
	static void DrawGridItem(const Item& item, float cellWidth);
	static void DrawListItem(const Item& item);
	// アイコン（画像ファイルなら小さい絵）を min〜max に描く
	static void DrawItemIcon(ImDrawList* drawList, const Item& item, const ImVec2& min, const ImVec2& max);
	// 出す名前（開いているシーンで、保存していなければ * を付ける）
	static std::string LabelOf(const Item& item);

	// ===== 操作 =====
	// 項目の上でのクリック（選ぶ・ダブルクリックで開く・右クリックのメニュー）。項目を置いた直後に呼ぶ
	static void HandleItemInput(const Item& item);
	static void DrawItemMenu(const Item& item);
	static void DrawCreateMenu();
	// 何も無い所（クリックで選ぶのをやめる・右クリックで作成メニュー）
	static void HandleEmptySpace();
	static void HandleShortcuts();
	// ダブルクリック：フォルダは中へ、シーンはエディタで開く
	static void Open(const Item& item);
	static void OpenFolder(const std::filesystem::path& folder);
	static void CreateFolder();
	static void CreateScene();
	// 今のフォルダに、既定の中身のファイルを作る（Physic Material・攻撃表。create はファイルを作る関数）
	static void CreateAssetFile(std::string_view baseName, std::string_view extension, bool (*create)(const std::filesystem::path&));
	// resources/ui/ に、一番上の Entity だけの .uiscreen を作る
	static void CreateUIScreen();
	// 名前の変更
	static void StartRename(const Item& item);
	static void DrawRenameField(const Item& item);
	static void ApplyRename(const Item& item);
	// ごみ箱へ移すか聞くウィンドウ
	static void DrawDeleteDialog();
	// 名前を変える・消すことができない物か（開いているシーンと、それが入っているフォルダ。Source/ の中の物）
	static bool IsLocked(const Item& item);
	// 選んでいる項目（無ければ nullptr）
	static const Item* FindSelected();

	static std::filesystem::path currentFolder_; // 右に中身を出しているフォルダ
	static std::filesystem::path selected_;      // 選んでいるファイル・フォルダ（空なら無し）
	static std::filesystem::path renaming_;      // 名前を変えている物（空なら変えていない）
	static std::string renameBuffer_;            // 入力中の名前（確定するまでファイルの名前は変えない）
	static bool renameFocusRequest_;             // 入力欄を出した最初のフレームだけ true
	static std::filesystem::path deleteTarget_;  // ごみ箱へ移すか聞いている物
	static bool deleteRequest_;                  // 次に「ごみ箱へ移しますか？」を開く
	static float iconSize_;                      // 右のアイコンの大きさ（kListModeSize 以下なら1行ずつの並び）
	static std::vector<Folder> rootFolders_;     // 左の木（resources/ と Source/。無いフォルダは入れない）
	static std::vector<Item> items_;             // 右に出している中身
	static double lastScanTime_;                 // 最後に読み直した時刻（ImGui::GetTime）
	static bool rescanRequest_;                  // 次のフレームの頭で読み直す（作った・消した・名前を変えた・フォルダを移った）
	static bool revealRequest_;                  // 左の木で、今のフォルダまでの親を開く（右でフォルダの中へ入ったとき）
};