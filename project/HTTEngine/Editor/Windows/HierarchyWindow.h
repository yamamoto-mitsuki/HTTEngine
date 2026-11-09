#pragma once
#include <string>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"


/// <summary>
/// ヒエラルキーウィンドウ（Entityの一覧・作成・削除・選択・親子の付け替え・名前の変更・コピー）
/// <para>選択中のEntityはここが持つ。InspectorWindowはこれを見る</para>
/// <para>作成・削除などは全部EditorHistoryに予約するだけ（Undoでき、描いている途中で一覧も変わらない）</para>
/// </summary>
class HierarchyWindow {
public:
	// Entityをドラッグで運ぶときの種類の名前（HierarchyからInspectorの参照欄へ落とすときも同じ名前で受け取る）
	static constexpr const char* kDragDropType = "ENTITY_HANDLE";

	/// <summary>
	/// ImGuiのフレームの中で毎フレーム呼ぶ
	/// </summary>
	static void Draw();

	// 選択中のEntity（何も選んでいなければ無効なHandle）
	static Handle<Entity> GetSelected() { return selected_; }
	static void SetSelected(Handle<Entity> handle) { selected_ = handle; }
	// 選んで、一覧でも見えるようにする（閉じている親を開いて、そこまで送る）。Scene ビューでクリックして選んだときに使う
	static void SelectAndReveal(Handle<Entity> handle) {
		selected_ = handle;
		revealRequest_ = handle.IsValid();
	}

private:
	// 見出し（Entityの数と、作成メニューを開く＋ボタン）
	static void DrawToolbar();
	// Entity1つ分を描く（子がいれば入れ子で描く）
	static void DrawEntityNode(Handle<Entity> handle);
	// 行の左の端の目（押すと Scene ビューだけで隠す・見せる）。描いた後は、ツリーの行を描き始める所へ戻す
	static void DrawVisibilityToggle(Handle<Entity> handle);
	// 一覧の下の何も無い所（クリックで選択解除・rootへのドロップ・右クリックで作成メニュー）
	static void DrawEmptySpace();
	// Entityの右クリックメニュー
	static void DrawEntityMenu(Handle<Entity> handle);
	// 作成メニュー（＋ボタンと、何も無い所の右クリックで共通）
	static void DrawCreateMenu();
	// F2・Delete・Ctrl+C / V / D
	static void HandleShortcuts();
	// 名前の変更を始める（そのEntityの行を入力欄にする）
	static void StartRename(Handle<Entity> handle);
	// 名前の入力欄（DrawEntityNodeの中、ツリーの矢印の横に出す）
	static void DrawRenameField(Handle<Entity> handle);

	static Handle<Entity> selected_;  // 選択中
	static Handle<Entity> renaming_;  // 名前を変更中のEntity（無効なら変更中ではない）
	static std::string renameBuffer_; // 入力中の名前（確定するまでEntityの名前は変えない）
	static bool renameFocusRequest_;  // 入力欄を出した最初のフレームだけtrue（フォーカスを移す）
	static bool revealRequest_;       // 次に描くとき、選択中の物が見えるように親を開いて、そこまで送る
	static float eyeColumnX_;         // 目を並べる列の左（ウィンドウの中での位置。親子の深さに関係なく、ここにそろえる）
};