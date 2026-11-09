#include "HTTEngine/Input/InputManager.h"

#include <cmath>
#include <format>

#ifdef USE_IMGUI
#include <externals/imgui/imgui.h>
#endif

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


// ============================================================
// シングルトン
// ============================================================
InputManager* InputManager::GetInstance() {
	static InputManager instance;
	return &instance;
}

// ============================================================
// static 窓口 (外部から呼ぶインターフェース)
// 実処理はすべて GetInstance() 経由でインスタンスメソッドに委譲する
// ============================================================
void InputManager::Initialize(HINSTANCE hInstance, HWND hwnd) { GetInstance()->InitInternal(hInstance, hwnd); }
void InputManager::Release() { GetInstance()->FinalizeInternal(); }
void InputManager::Update() { GetInstance()->UpdateInternal(); }

// ---- キーボード ----
bool InputManager::IsKeyPressed(uint8_t key) {
	if (!IsActive()) return false;
	if (IsKeyboardCapturedByImGui()) return false;
	return GetInstance()->keyboard_.IsPressed(key);
}
bool InputManager::IsKeyTriggered(uint8_t key) {
	if (!IsActive()) return false;
	if (IsKeyboardCapturedByImGui()) return false;
	return GetInstance()->keyboard_.IsTriggered(key);
}
bool InputManager::IsKeyReleased(uint8_t key) {
	if (!IsActive()) return false;
	if (IsKeyboardCapturedByImGui()) return false;
	return GetInstance()->keyboard_.IsReleased(key);
}

// ---- マウス ----
bool InputManager::IsMouseButtonPressed(int b) {
	if (!IsActive()) return false;
	if (IsMouseCapturedByImGui()) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonPressed(b);
}
bool InputManager::IsMouseButtonTriggered(int b) {
	if (!IsActive()) return false;
	if (IsMouseCapturedByImGui()) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonTriggered(b);
}
bool InputManager::IsMouseButtonReleased(int b) {
	if (!IsActive()) return false;
	if (IsMouseCapturedByImGui()) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonReleased(b);
}

Vector2 InputManager::GetMousePositionInWindow() {
	const MouseInput& mouse = GetInstance()->mouse_;
	return {static_cast<float>(mouse.GetPositionX()), static_cast<float>(mouse.GetPositionY())};
}

Vector2 InputManager::GetMousePositionInGame() {
	Vector2 topLeft;
	Vector2 size;
	GetGameViewRect(topLeft, size);
	if (size.x < 1.0f || size.y < 1.0f) {
		return {0.0f, 0.0f}; // ゲーム画面がまだ無い（最初のフレーム・Game ビューを閉じている）
	}
	// 画面の中の割合（左上が0、右下が1）
	const Vector2 mouse = GetMousePositionInWindow();
	const float u = (mouse.x - topLeft.x) / size.x;
	const float v = (mouse.y - topLeft.y) / size.y;
	// スプライトと同じ座標へ（真ん中が0、右と上が+、端が±1。ウィンドウは下が+なので上下を返す）
	return {u * 2.0f - 1.0f, 1.0f - v * 2.0f};
}

bool InputManager::IsMouseInGame() {
	Vector2 topLeft;
	Vector2 size;
	GetGameViewRect(topLeft, size);
	if (size.x < 1.0f || size.y < 1.0f) {
		return false;
	}
	const Vector2 position = GetMousePositionInGame();
	return position.x >= -1.0f && position.x <= 1.0f && position.y >= -1.0f && position.y <= 1.0f;
}

bool InputManager::ShouldHideCursor() {
	if (GetInstance()->isCursorVisible_) {
		return false;
	}
	// 閉じ込めている間は、カーソルはいつもゲーム画面の中（エディタは ImGui がマウスを見ないので、Game ビューの「上か」が取れない。WindowManager が先に閉じ込めるかを決めて渡している）
	if (GetInstance()->isCursorConfineActive_) {
		return true;
	}
#ifdef USE_IMGUI
	// エディタ：Game ビューの絵の上（ほかの窓が重なっている所は出す）
	return GetInstance()->isGameViewHovered_;
#else
	// 製品版：黒帯の内側
	return IsMouseInGame();
#endif
}

//=============================================================================
// OS のカーソルを閉じ込める
//=============================================================================
// ===== 閉じ込める四角 =====
std::optional<RECT> InputManager::GetCursorConfineRect() {
	InputManager* self = GetInstance();
	if (!self->isCursorConfineRequested_ || !self->IsMainWindowForeground()) {
		return std::nullopt;
	}
#ifdef USE_IMGUI
	if (self->isCursorConfineReleasedInEditor_) {
		return std::nullopt; // Shift+F1 で放している（Game ビューをクリックするまで）
	}
#endif
	// --- ゲーム画面（エディタは Game ビューの絵、製品版は黒帯の内側）を、内側へ丸めた整数の四角にする ---
	// 外側へ丸めると、端の 1 ピクセルが絵の外（エディタではパネルの縁）になる
	Vector2 topLeft;
	Vector2 size;
	GetGameViewRect(topLeft, size);
	const RECT rect = {
	    static_cast<LONG>(std::ceil(topLeft.x)),
	    static_cast<LONG>(std::ceil(topLeft.y)),
	    static_cast<LONG>(std::floor(topLeft.x + size.x)),
	    static_cast<LONG>(std::floor(topLeft.y + size.y)),
	};
	if (rect.right - rect.left < 1 || rect.bottom - rect.top < 1) {
		return std::nullopt; // ゲーム画面がまだ無い（最初のフレーム・エディタで Game ビューがほかのタブの裏）
	}
	return rect;
}

// ===== シーンを変えたとき =====
void InputManager::ResetCursorRequests() {
	InputManager* self = GetInstance();
	self->isCursorVisible_ = true;
	self->isCursorConfineRequested_ = false;
#ifdef USE_IMGUI
	self->isCursorConfineReleasedInEditor_ = false; // 次に閉じ込めるシーンは、Shift+F1 で放す前から始まる
#endif
}

// ===== また閉じ込めたクリックを、ゲームに渡さない =====
// ボタンを全部離して、離した瞬間（Released）も過ぎたフレームに戻す（押した・離したの片方だけがゲームに届かないように）
void InputManager::UpdateMouseSwallow() {
	if (isMouseButtonSwallowed_ && !mouse_.IsAnyButtonPressed() && !mouse_.IsAnyButtonReleased()) {
		isMouseButtonSwallowed_ = false;
	}
}

#ifdef USE_IMGUI
// ===== エディタで放す・また閉じ込める =====
// フレームの頭（ゲームが入力を読む前）に決めるので、また閉じ込めたクリックは、このフレームからゲームに渡さない
// 「閉じ込めているか」は前のフレームの終わりに WindowManager が決めた値（isCursorConfineActive_）
void InputManager::UpdateEditorCursorConfine() {
	const bool isForeground = IsMainWindowForeground();
	if (isCursorConfineActive_) {
		// --- 閉じ込めている：Shift+F1 か、手前でなくなったら放す（DirectInput のキーボードはエディタの ImGui に関係なく読める）---
		const bool isShift = keyboard_.IsPressed(DIK_LSHIFT) || keyboard_.IsPressed(DIK_RSHIFT);
		if (!isForeground || (isShift && keyboard_.IsTriggered(DIK_F1))) {
			isCursorConfineReleasedInEditor_ = true;
		}
		return;
	}
	// --- 放している：Game ビューの絵を左クリックしたら、また閉じ込める。そのクリックはゲームに渡さない（撃つなどが起きないように）---
	// 「絵の上か」は前のフレームの描画で決めた値（ほかの窓に隠れていない所だけ）
	if (isCursorConfineReleasedInEditor_ && isCursorConfineRequested_ && isForeground && isGameViewHovered_ && mouse_.IsButtonTriggered(0)) {
		isCursorConfineReleasedInEditor_ = false;
		isMouseButtonSwallowed_ = true;
	}
}
#endif

long InputManager::GetMouseDeltaX() {
	if (!IsActive()) return 0;
	if (IsMouseCapturedByImGui()) return 0;
	return GetInstance()->mouse_.GetDeltaX();
}
long InputManager::GetMouseDeltaY() {
	if (!IsActive()) return 0;
	if (IsMouseCapturedByImGui()) return 0;
	return GetInstance()->mouse_.GetDeltaY();
}
long InputManager::GetMouseDeltaWheel() {
	if (!IsActive()) return 0;
	if (IsMouseCapturedByImGui()) return 0;
	return GetInstance()->mouse_.GetDeltaWheel();
}

// ---- Xbox コントローラー ----
// キーボードと同じく、ウィンドウが手前に無いとき・ImGui に文字を打っているときは、何も押していない扱いにする
// （XInput はウィンドウに関係なく読めるので、見ないと、別のアプリを触っている間もゲームが動いてしまう）
bool InputManager::IsGamepadConnected(int i) { return GetInstance()->gamepad_.IsConnected(i); }
bool InputManager::IsButtonPressed(uint32_t btn, int i) { return !IsGamepadBlocked() && GetInstance()->gamepad_.IsButtonPressed(btn, i); }
bool InputManager::IsButtonTriggered(uint32_t btn, int i) { return !IsGamepadBlocked() && GetInstance()->gamepad_.IsButtonTriggered(btn, i); }
bool InputManager::IsButtonReleased(uint32_t btn, int i) { return !IsGamepadBlocked() && GetInstance()->gamepad_.IsButtonReleased(btn, i); }
float InputManager::GetLeftStickX(int i) { return IsGamepadBlocked() ? 0.0f : GetInstance()->gamepad_.GetLeftStickX(i); }
float InputManager::GetLeftStickY(int i) { return IsGamepadBlocked() ? 0.0f : GetInstance()->gamepad_.GetLeftStickY(i); }
float InputManager::GetRightStickX(int i) { return IsGamepadBlocked() ? 0.0f : GetInstance()->gamepad_.GetRightStickX(i); }
float InputManager::GetRightStickY(int i) { return IsGamepadBlocked() ? 0.0f : GetInstance()->gamepad_.GetRightStickY(i); }
float InputManager::GetLeftTrigger(int i) { return IsGamepadBlocked() ? 0.0f : GetInstance()->gamepad_.GetLeftTrigger(i); }
float InputManager::GetRightTrigger(int i) { return IsGamepadBlocked() ? 0.0f : GetInstance()->gamepad_.GetRightTrigger(i); }
void InputManager::SetVibration(float l, float r, int i) { GetInstance()->gamepad_.SetVibration(l, r, i); }
void InputManager::StopVibration(int i) { GetInstance()->gamepad_.StopVibration(i); }

// ---- デッドゾーン ----
void InputManager::SetLeftStickDeadzone(float d) { GetInstance()->gamepad_.SetLeftStickDeadzone(d); }
void InputManager::SetRightStickDeadzone(float d) { GetInstance()->gamepad_.SetRightStickDeadzone(d); }
void InputManager::SetTriggerDeadzone(float d) { GetInstance()->gamepad_.SetTriggerDeadzone(d); }
float InputManager::GetLeftStickDeadzone() { return GetInstance()->gamepad_.GetLeftStickDeadzone(); }
float InputManager::GetRightStickDeadzone() { return GetInstance()->gamepad_.GetRightStickDeadzone(); }
float InputManager::GetTriggerDeadzone() { return GetInstance()->gamepad_.GetTriggerDeadzone(); }

// ---- トリガーのしきい値 ----
void InputManager::SetTriggerPressThreshold(float t) { GetInstance()->gamepad_.SetTriggerPressThreshold(t); }
void InputManager::SetTriggerReleaseThreshold(float t) { GetInstance()->gamepad_.SetTriggerReleaseThreshold(t); }
float InputManager::GetTriggerPressThreshold() { return GetInstance()->gamepad_.GetTriggerPressThreshold(); }
float InputManager::GetTriggerReleaseThreshold() { return GetInstance()->gamepad_.GetTriggerReleaseThreshold(); }

// ---- まとめて読む ----
Vector2 InputManager::GetMoveAxis(bool useController) {
	// キーボード：押している向きを足す（反対の向きを同時に押すと、打ち消し合って0）
	Vector2 move = {0.0f, 0.0f};
	if (IsKeyPressed(DIK_D) || IsKeyPressed(DIK_RIGHT)) {
		move.x += 1.0f;
	}
	if (IsKeyPressed(DIK_A) || IsKeyPressed(DIK_LEFT)) {
		move.x -= 1.0f;
	}
	if (IsKeyPressed(DIK_W) || IsKeyPressed(DIK_UP)) {
		move.y += 1.0f;
	}
	if (IsKeyPressed(DIK_S) || IsKeyPressed(DIK_DOWN)) {
		move.y -= 1.0f;
	}

	// パッド：左スティック（1つ目のコントローラー。デッドゾーン処理済み）
	if (useController) {
		move.x += GetLeftStickX();
		move.y += GetLeftStickY();
	}

	// 斜めでも速くならないように、長さを1までにする（キー2つだと長さが√2＝約1.41になる）
	const float length = Length(move);
	if (length > 1.0f) {
		move /= length;
	}
	return move;
}

// ============================================================
// 内部ヘルパー
// ============================================================
void InputManager::InitInternal(HINSTANCE hInstance, HWND hwnd) {
	mainHwnd_ = hwnd;
	// --- DirectInput8 インターフェースの生成 ---
	HRESULT hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput_, nullptr);
	if (FAILED(hr)) {
		LogManager::Error(std::format("HRESULT=0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "DirectInput8の初期化に失敗しました");
	}
	LogManager::Log("DirectInput8Create Success");

	// --- 各デバイスの初期化 ---
	keyboard_.Init(directInput_.Get(), hwnd);
	mouse_.Init(directInput_.Get(), hwnd);
	gamepad_.Init();
	LogManager::Log("Initialized");
}

void InputManager::FinalizeInternal() {
	gamepad_.Finalize();
	mouse_.Finalize();
	keyboard_.Finalize();
	directInput_.Reset();

	LogManager::Log("Finalized");
}

void InputManager::UpdateInternal() {
	keyboard_.Update();
	mouse_.Update();
	gamepad_.Update();
	// --- OS のカーソルを閉じ込めるの（ゲームが入力を読む前に決める）---
	UpdateMouseSwallow();
#ifdef USE_IMGUI
	UpdateEditorCursorConfine();
#endif
}

bool InputManager::IsMouseCapturedByImGui() {
#ifdef USE_IMGUI
	// カーソルを閉じ込めている間は、マウスはゲームの物（カーソルは Game ビューの中にいて、ImGui はマウスを見ていない）
	if (GetInstance()->isCursorConfineActive_) return false;
	if (GetInstance()->mouseInViewport_) return false;
	return ImGui::GetIO().WantCaptureMouse;
#else
	return false;
#endif
}

bool InputManager::IsKeyboardCapturedByImGui() {
#ifdef USE_IMGUI
	// Viewportがフォーカスされているときはゲームに通す
	if (GetInstance()->viewportFocused_) return false;
	return ImGui::GetIO().WantCaptureKeyboard;
#else
	return false;
#endif
}

bool InputManager::IsTypingInImGui() {
#ifdef USE_IMGUI
	return ImGui::GetIO().WantTextInput;
#else
	return false;
#endif
}

void InputManager::GetGameViewRect(Vector2& topLeft, Vector2& size) {
#ifdef USE_IMGUI
	// エディタ：Game ビューの絵（EditorViewport が毎フレーム教えてくれる）
	topLeft = GetInstance()->gameViewTopLeft_;
	size = GetInstance()->gameViewSize_;
#else
	// 製品版：ウィンドウの中の、黒帯の内側（SceneRenderer::RenderToWindow が描く四角と同じ）
	RECT client{};
	GetClientRect(GetInstance()->mainHwnd_, &client);
	const ViewRect rect = GameScreen::CalcFitRect(static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top));
	topLeft = {rect.x, rect.y};
	size = {rect.width, rect.height};
#endif
}