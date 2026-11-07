#pragma once
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>
#include "MyEngine/Editor/ImGuiManager.h"
#include "MyEngine/Editor/Viewport/EditorViewport.h"
#include "MyEngine/Camera/DebugCamera.h"
#include "MyEngine/Graphics/RenderTarget/RenderWindow.h"
#include "MyEngine/Scene/IScene.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/Window/Win32Window.h"

// 前方宣言
class RenderWindow;
class DirectXCommon;


/// <summary>
/// 全ウィンドウの管理クラス
/// </summary>
class WindowManager {
public:
	/// <summary>
	/// 終了処理。whileループを抜けた後、return 0の前に必ず呼ぶ
	/// </summary>
	void Finalize();

	/// <summary>
	/// ウィンドウを追加
	/// </summary>
	/// <param name = "config">ウィンドウ設定</param>
	/// <param name> = "initialScene">設定したいシーン</param>
	void AddWindow(const WindowConfig& config, SceneFactory initialScene = nullptr);

	/// <summary>
	/// 全ウィンドウのメッセージ処理
	/// </summary>
	bool ProcessMessage();

	/// <summary>
	/// 全ウィンドウの更新処理
	/// </summary>
	void UpdateAll();

	/// <summary>
	/// 全ウィンドウの描画
	/// </summary>
	void DrawAll();

	/// <summary>
	/// 全ウィンドウの描画開始
	/// </summary>
	void PreRenderAll();

	/// <summary>
	/// 全ウィンドウの描画命令
	/// </summary>
	void PostRenderAll();

	/// <summary>
	/// コマンドリストの実行
	/// </summary>
	void ExecuteOnly();

	/// <summary>
	/// PostRenderAll()内に呼ぶ処理を登録する
	/// <para>用途: RaymarchRenderer::ClearRequests() など、プロジェクト固有のクリア処理</para>
	/// </summary>
	/// <param name="callback">登録したい処理</param>
	/// <param name="tarhetTitle">描画したいウィンドウ（指定しない場合、全てのウィンドウに実行）</param>
	int AddFrameEndCallback(std::function<void()> callback, const std::wstring& targetTitle = L"");

	/// <summary>
	/// AddFrameEndCallback()で登録したコールバックを解除
	/// <param name="id">AddFrameEndCallback()の戻り値のid</param>
	void RemoveFrameEndCallback(int id);

	// ゲッター
	HWND GetMainHWND() const { return windows_.empty() ? nullptr : windows_[0].window->GetHWND(); }
	Win32Window* GetWindowByTitle(const std::wstring& title);
	Win32Window* GetMainWindow() const { return windows_.empty() ? nullptr : windows_[0].window.get(); }
	IScene* GetSceneByTitle(const std::wstring& title);

private:
	// コールバックをIDと一緒に管理する
	struct CallbackEntry {
		int id;
		std::wstring targetWindowTitle;
		std::function<void()> func;
	};
	// ウィンドウとウィンドウに描画するクラスをまとめた構造体
	struct WindowSet {
		std::unique_ptr<Win32Window> window;
		std::unique_ptr<RenderWindow> renderer;
		std::unique_ptr<SceneManager> sceneManager;
		std::unique_ptr<EditorViewport> editor;
	};

	std::vector<WindowSet> windows_; // 全ウィンドウ
	std::vector<CallbackEntry> framEndCallbacks_; // プロジェクト側から登録する追加の描画処理

	int nextCallbackId_ = 0; // コールバックIDの管理

	// カメラを持っている最初のシーン（無ければ nullptr）
	IScene* FindGameScene() const;

	// OS のカーソルを隠すかを、メインのウィンドウへ渡す（UpdateAll の最後に呼ぶ）
	void UpdateCursor();

#ifdef USE_IMGUI
	std::wstring imguiTargetWindow_;
	void RenderImGui();
#endif
};
