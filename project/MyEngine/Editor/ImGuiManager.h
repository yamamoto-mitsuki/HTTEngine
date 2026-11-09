#pragma once
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <utility>
#include <functional>

#include <wrl.h>
#include <d3d12.h>

#include "MyEngine/Math/Vector2.h"
#include "MyEngine/Window/Win32Window.h"

#ifdef USE_IMGUI


/// <summary>
/// ImGuiの描画を管理するクラス。
/// </summary>
class ImGuiManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class ImGuiManager;
	};
	explicit ImGuiManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	ImGuiManager(const ImGuiManager&) = delete;
	ImGuiManager& operator=(const ImGuiManager&) = delete;

	/// <summary>
	/// 初期化。Engine::Initialize() 内で1度だけ呼ぶ。
	/// </summary>
	static void Initialize(Win32Window* window);

	/// <summary>
	/// 解放
	/// </summary>
	static void Release();

	/// <summary>
	/// フレームの開始処理。毎フレーム最初に呼ぶ。
	/// </summary>
	static void Begin();

	/// <summary>
	/// ImGuiウィンドウの描画リクエストを追加する。
	/// </summary>
	/// <param name="drawFunc">ImGui::Begin / End を含むラムダ式</param>
	static void AddDrawRequest(std::function<void()> drawFunc);

	/// <summary>
	/// AddDrawRequest() で積まれた描画リクエストを全て実行する。
	/// </summary>
	static void ProcessRequests();

	/// <summary>
	/// 描画データを確定させる（ImGui::Render() のラッパー）。
	/// </summary>
	static void Render();

	/// <summary>
	/// 確定した描画データをコマンドリストに積む。
	/// </summary>
	static void RenderDrawData();

	/// <summary>
	/// 全リクエストをクリアする。
	/// </summary>
	static void ClearRequests();

	/// <summary>
	/// エディタ（ImGui）がマウスを見るか。false の間は、どのパネルもマウスで触れない（ホバーもしない）
	/// <para>ゲームが OS のカーソルを Game ビューに閉じ込めている間、WindowManager が false にする（ImGuiConfigFlags_NoMouse）</para>
	/// </summary>
	static void SetMouseEnabled(bool enabled);

private:
	// シングルトン
	friend struct std::default_delete<ImGuiManager>;
	static std::unique_ptr<ImGuiManager> instance_;

	~ImGuiManager() = default;

	// 画面全体のウィンドウ（メニューバーとドッキングの土台）を描く
	void DrawDockSpace();
	// メニューバー（File・Edit・View・Setting）を描く
	void DrawMenuBar();
	void DrawSettingMenu();

	/// <summary>
	/// 現在のImGuiStyleを imgui_style.ini に書き出す。
	/// <para>色・Alpha・サイズ系パラメータを全て保存する。</para>
	/// </summary>
	/// <param name="path">保存先ファイルパス</param>
	void SaveStyle(const std::string& path);

	/// <summary>
	/// imgui_style.ini からImGuiStyleを読み込む。
	/// <para>ファイルが存在しない場合はスキップする（初回起動時は正常動作）</para>
	/// </summary>
	/// <param name="path">読み込み元ファイルパス</param>
	void LoadStyle(const std::string& path);

	// 描画リクエストのキュー
	std::vector<std::function<void()>> requests_;
};

#else

/// <summary>
/// USE_IMGUI が定義されていない場合の何もしないスタブ。
/// </summary>
class ImGuiManager {
public:
	static void Initialize(Win32Window*, const WindowConfig&) {}
	static void Release() {}
	static void Begin() {}
	static void Render() {}
	static void RenderDrawData() {}
	static void ClearRequests() {}
	static void ProcessRequests() {}
	static void AddDrawRequest(std::function<void()>) {}
	static void SetMouseEnabled(bool) {}
};

#endif