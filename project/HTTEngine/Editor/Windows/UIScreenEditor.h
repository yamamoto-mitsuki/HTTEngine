#pragma once
#include <filesystem>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"


/// <summary>
/// 画面（UI Screen。resources/ui/〇〇.uiscreen）の編集モード（Polygon Shape の Edit Shape と同じ考えの、画面版）
/// <para>入る：Project ウィンドウでダブルクリック・Inspector の Edit UI Screen（停止中だけ）。画面の中身を、今のシーンに置く</para>
/// <para>編集中：シーンはそのまま見える（後ろにどう重なるか見ながら置ける）が、クリックで選べるのは画面の中の物だけ。
/// Hierarchy には画面の中身だけを出す。中身には「シーンに保存しない」印（Entity::isDontSave）を付ける。Undo は画面の編集の分だけ</para>
/// <para>出る：Done（ファイルに保存して閉じる）・Cancel（保存せずに閉じる）。Play・シーンを開くときは、先に Done と同じことをする</para>
/// </summary>
class UIScreenEditor {
public:
	// .uiscreen のファイルを Inspector に出す係（Edit UI Screen のボタン）を登録する
	static void Initialize();

	// ===== 入る・出る（Project・Inspector・Hierarchy から。実際に入る・出るのは次のフレームの頭）=====
	static void RequestOpen(const std::filesystem::path& filePath);
	// save が true なら Done（保存して閉じる）、false なら Cancel（保存せずに閉じる）
	static void RequestClose(bool save);

	// ===== エンジンから =====
	// フレームの頭（WindowManager::UpdateAll の EditorHistory::Flush の後）。頼まれていれば入る・出る
	static void Flush();
	// 今すぐ閉じる（SceneManager がシーンを作り直す前。フレームの境目で呼ぶ）。編集していなければ何もしない
	static void EndNow(bool save);
	// 編集中なら、画面のファイルに書く（Ctrl+S。シーンの保存と一緒に）
	static void Save();

	// ===== 調べる =====
	static bool IsEditing();
	// 編集中の画面の一番上の Entity（編集していなければ無効な Handle）
	static Handle<Entity> GetRoot();

	// ===== 描く（エディタのウィンドウから）=====
	// Hierarchy の一番上の帯（画面の名前・Done・Cancel）。編集していなければ何も描かない
	static void DrawBar();
	// Scene ビューの絵の上に、画面の枠（ゲームの画面＝絵の縁）と名前を描く
	static void DrawFrame(const ImVec2& imageMin, const ImVec2& imageSize);
};