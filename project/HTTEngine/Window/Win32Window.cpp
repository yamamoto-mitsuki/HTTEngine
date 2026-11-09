#include "HTTEngine/Window/Win32Window.h"
#include <format>
#include <wrl.h>
#include <d3d12.h>
#include <externals/imgui/imgui.h>
#include <externals/imgui/imgui_impl_dx12.h>
#include <externals/imgui/imgui_impl_win32.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Widgets/ImeInput.h"

#pragma comment(lib, "windowsapp.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::Data::Xml::Dom;

void Win32Window::Init() {
	WindowConfig config;
	Init(config);
}

void Win32Window::Init(const WindowConfig& config) {
	config_ = config;
	width_ = config.width;
	height_ = config.height;
	title_ = config.title;

	// 高DPIディスプレイでビットマップ拡大されないようにする（全プロジェクト共通で1度だけ）
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// ===== ウィンドウクラスを登録する =====
	// ウィンドウプロシージャ
	wc_.lpfnWndProc = WindowProc;
	// ウィンドウクラス名(なんでも良い)
	wc_.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc_.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
	// ウィンドウクラスを登録
	RegisterClass(&wc_);

	// ===== ウィンドウサイズを決める =====
	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, config.width, config.height };
	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ===== ウィンドウの生成 =====
	hwnd_ = CreateWindow(
		wc_.lpszClassName,    // 利用するクラス名
		config.title.c_str(), // タイトルバーの文字
		config.style,         // よく見るウィンドウスタイル
		config.x,             // 表示X座標
		config.y,             // 表示Y座標
		wrc.right - wrc.left, // ウィンドウ縦幅
		wrc.bottom - wrc.top, // ウィンドウ横幅
		nullptr,              // 親ウィンドウハンドル
		nullptr,              // メニューハンドル
		wc_.hInstance,        // インスタンスハンドル
		nullptr);             // オプション

	SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	// ウィンドウを表示する
	ShowWindow(hwnd_, SW_SHOW);
	LogManager::Log(std::format("Complete create Window!"));
}

bool Win32Window::ProcessMessage() {
	MSG msg{};
	// このスレッドに届いたメッセージを全部処理する。
	// hwnd_を指定すると自分宛てしか取り出さず、IMEの変換窓などWindowsが作ったウィンドウ宛てのメッセージが処理されないまま残る
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return IsWindow(hwnd_) != 0;
}

//=============================================================================
// フルスクリーン切り替え
//=============================================================================
void Win32Window::SetFullscreen(bool enable) {
	if (enable == isFullscreen_) {
		return;
	}
	isFullscreen_ = enable;

	if (enable) {
		// 戻すときのために、今の見た目と位置を控えておく
		windowedStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
		GetWindowPlacement(hwnd_, &windowedPlacement_);

		// ウィンドウが今いるモニタの範囲を取る。マルチモニタでも正しい方に広がる
		MONITORINFO info = {sizeof(MONITORINFO)};
		GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info);
		const RECT& area = info.rcMonitor;

		SetWindowLongPtr(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(hwnd_, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
	} else {
		SetWindowLongPtr(hwnd_, GWL_STYLE, windowedStyle_);
		SetWindowPlacement(hwnd_, &windowedPlacement_);
		SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
	}
}

void Win32Window::SetCursorHidden(bool hidden) {
	if (hidden == isCursorHidden_) {
		return;
	}
	isCursorHidden_ = hidden;
	// WM_SETCURSOR はマウスが動くまで来ないので、マウスがこのウィンドウの上なら、すぐ形を変える
	// （ほかのアプリの窓の上にいるときに変えると、その窓のカーソルを消してしまうので、上にいるか確かめる）
	POINT point{};
	if (GetCursorPos(&point) && WindowFromPoint(point) == hwnd_) {
		SetCursor(hidden ? nullptr : wc_.hCursor);
	}
}

//=============================================================================
// OS のカーソルを閉じ込める
//=============================================================================
// ===== 閉じ込める・放す =====
void Win32Window::SetCursorClip(const std::optional<RECT>& clientRect) {
	if (!clientRect.has_value()) {
		ReleaseCursorClip();
		return;
	}
	// ClipCursor はスクリーン座標（モニターの左上が 0,0）なので、クライアント座標から直す
	POINT topLeft = {clientRect->left, clientRect->top};
	POINT bottomRight = {clientRect->right, clientRect->bottom};
	ClientToScreen(hwnd_, &topLeft);
	ClientToScreen(hwnd_, &bottomRight);
	const RECT screenRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
	// 毎フレーム頼み直す（同じ四角でも。ほかのアプリ・Windows が外していることがある）
	ClipCursor(&screenRect);
	isCursorClipped_ = true;
}

// ===== 放す =====
void Win32Window::ReleaseCursorClip() {
	if (!isCursorClipped_) {
		return;
	}
	ClipCursor(nullptr);
	isCursorClipped_ = false;
}

LRESULT CALLBACK Win32Window::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	// SetWindowLongPtrで保存した値を取り出す（ウィンドウを作っている途中はまだnullptr）
	Win32Window* self = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

#ifdef USE_IMGUI
	// 変換中の文字はImGuiの入力欄の中に自分で描く（ImeInput）ので、Windowsの小さな変換窓は出さない。
	// 最初のこのメッセージは、ウィンドウを表示した瞬間（SetImGuiTargetより前）に届くので、isImGuiTarget_を見ずに外す
	if (msg == WM_IME_SETCONTEXT) {
		lparam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
	}
	if (self && self->isImGuiTarget_) {
		// IMEが変換の確定・取り消しに使ったEnter / Escは、ImGuiに渡さない（渡すと名前の確定・取り消しまで一緒に起きる）
		if (ImeInput::HandleMessage(msg, wparam)) {
			return DefWindowProc(hwnd, msg, wparam, lparam);
		}
		const LRESULT result = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
		// WM_IME_COMPOSITIONは、ImGuiの中でDefWindowProcWまで済ませている（下のDefWindowProcで2回目を呼ばない）
		if (result != 0 || msg == WM_IME_COMPOSITION) {
			return result;
		}
	}
#endif

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {

	// Alt + Enter でフルスクリーン切り替え
	case WM_SYSKEYDOWN: {
		// lparam の bit29 = Altが押されている、bit30 = 直前も押されていた（キーリピート）
		bool isAlt = (lparam & (1 << 29)) != 0;
		bool isRepeat = (lparam & (1 << 30)) != 0;
		if (self && wparam == VK_RETURN && isAlt && !isRepeat) {
			self->ToggleFullscreen();
			return 0; // DefWindowProc に渡すとシステムメニューが開いてしまう
		}
		break;
	}

	// マウスが動いた（カーソルの形を決める）。クライアント領域の上で、隠すと決まっていれば隠す
	// ImGui は NoMouseCursorChange でカーソルを触らないので、ここまで届く
	case WM_SETCURSOR: {
		if (self && self->isCursorHidden_ && LOWORD(lparam) == HTCLIENT) {
			SetCursor(nullptr);
			return TRUE; // DefWindowProc に渡すと、ウィンドウクラスの矢印に戻される
		}
		break;
	}

	// 手前になった・手前でなくなった。手前でなくなったら、閉じ込めているカーソルをすぐ放す
	// （Alt+Tab・Windows キー・assert のメッセージボックス。メッセージボックスの間はフレームが回らないので、次のフレームを待つとカーソルがボタンへ届かない）
	case WM_ACTIVATE: {
		if (self && LOWORD(wparam) == WA_INACTIVE) {
			self->ReleaseCursorClip();
		}
		break;
	}

	// ウィンドウのサイズが変わったとき
	case WM_SIZE: {
		if (wparam == SIZE_MINIMIZED || !self) {
			break;
		}
		int w = static_cast<int>(LOWORD(lparam));
		int h = static_cast<int>(HIWORD(lparam));
		// 最大化・復元時は即時リサイズ
		if (wparam == SIZE_MAXIMIZED || wparam == SIZE_RESTORED) {
			if (self->onResize_) {
				self->onResize_(w, h);
			}
		} else {
			// ドラッグ中などは保留
			self->pendingResize_ = true;
			self->pendingWidth_ = w;
			self->pendingHeight_ = h;
		}
		break;
	}

#ifdef USE_IMGUI
	// ウィンドウのサイズ制限
	case WM_GETMINMAXINFO: {
		if (self && self->isImGuiTarget_) {
			MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
			info->ptMinTrackSize.x = 800;
			info->ptMinTrackSize.y = 500;
			info->ptMaxTrackSize.x = 1920;
			info->ptMaxTrackSize.y = 1080;
		}
		break;
	}
#endif

	case WM_SYSCOMMAND: {
		// ウィンドウの移動制限がかかっているとき
		UINT command = static_cast<UINT>(wparam & 0xFFF0);
		if (self && self->isPositionLocked_ && (command == SC_MOVE || command == SC_SIZE)) {
			return 0;
		}
		break;
	}

	// ウィンドウの×ボタン
	case WM_CLOSE: {
		if (self && self->onCanClose_) {
			if (!self->onCanClose_()) {
				// プロジェクト側で閉じれなかった処理を書いてから呼ぶ
				if (self->onTryClose_) {
					self->onTryClose_();
				}
				return 0;
			}
		}
		DestroyWindow(hwnd);
		return 0;
	}

	// ウィンドウが破棄された（閉じ込めていたカーソルを放して終わる。ClipCursor はほかのアプリと共有なので、残さない）
	case WM_DESTROY: {
		if (self) {
			self->ReleaseCursorClip();
		}
		return 0;
	}
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}