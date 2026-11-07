#pragma once
#include <string>
#include <functional>

#include <Windows.h>

#include "MyEngine/Math/Vector2.h"

// ウィンドウ設定用
struct WindowConfig {
	int width = 1280;      // 横の大きさ
	int height = 720;    // 縦の大きさ
	std::wstring title = L"Title"; // タイトルバー
	int x = CW_USEDEFAULT;         // 表示座標X
	int y = CW_USEDEFAULT;         // 表示座標Y
	DWORD style = WS_OVERLAPPEDWINDOW; // ウィンドウスタイル
	// ゲームの画面（縦横比と UI のピクセルの基準。ウィンドウの大きさとは別。比が違うウィンドウでは、製品版は黒帯が出る。GameScreen.h）
	int gameWidth = 1280; // 画面の幅（縦長のゲームなら 720 など）
	int gameHeight = 720; // 画面の高さ（縦長のゲームなら 1280 など）
	bool isImGui = false; // ImGuiを使うか
};

class Win32Window {
public:
	/// <summary>
	/// 初期化(標準)
	/// </summary>
	void Init();

	/// <summary>
	/// 初期化(細かい設定用)
	/// </summary>
	/// <param name = "config">ウィンドウ設定</param>
	void Init(const WindowConfig& config);

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// メッセージ処理
	bool ProcessMessage();
	// 画面最大化
	void ToggleFullscreen() { SetFullscreen(!isFullscreen_); }

	// ゲッター
	HWND GetHWND() const { return hwnd_; }
	const std::wstring& GetTitle() const { return title_; }
	bool GetIsImGuiTarget() const { return isImGuiTarget_; }
	bool GetPendingResize() const { return pendingResize_; }
	int GetPendingWidth() const { return pendingWidth_; }
	int GetPendingHeight() const { return pendingHeight_; }
	bool GetIsFullscreen() const { return isFullscreen_; }
	WindowConfig GetWindowConfig() const { return config_; }

	// セッター
	void SetFullscreen(bool enable);
	void SetImGuiTarget(bool isTarget) { isImGuiTarget_ = isTarget; }
	void SetOnResize(std::function<void(int, int)> callback) { onResize_ = std::move(callback); }
	void SetPositionLock(bool isLocked) { isPositionLocked_ = isLocked; }

	/// <summary>
	/// このウィンドウの中（クライアント領域）で、OS のカーソルを隠すか。WindowManager が毎フレーム渡す（隠す所かは InputManager::ShouldHideCursor が決める）
	/// <para>変わったとき、マウスがこのウィンドウの上なら、マウスが動くのを待たずにすぐ形を変える</para>
	/// </summary>
	void SetCursorHidden(bool hidden);

	void ClearPendingResize() { pendingResize_ = false; }

	// ウィンドウシステム
	std::function<bool()> onCanClose_;
	std::function<void()> onTryClose_;
	bool isPositionLocked_ = false;

private:

	HWND hwnd_ = nullptr;
	WNDCLASS wc_ = {};
	DWORD windowedStyle_ = 0;
	WINDOWPLACEMENT windowedPlacement_ = {sizeof(WINDOWPLACEMENT)};
	int width_ = 0;
	int height_ = 0;
	std::wstring title_;
	std::function<void(int, int)> onResize_;
	int pendingWidth_ = 0;
	int pendingHeight_ = 0;
	bool pendingResize_ = false;
	bool isImGuiTarget_ = false;
	bool isFullscreen_ = false;
	bool isCursorHidden_ = false; // クライアント領域で OS のカーソルを隠すか（WM_SETCURSOR で見る）
	WindowConfig config_;
};
