#pragma once
#include <cstdint>

#include <Windows.h>
#include <wrl.h>
#include <dinput.h>

#include "MyEngine/Math/Vector2.h"
#include "MyEngine/Input/MouseInput.h"
#include "MyEngine/Input/KeyboardInput.h"
#include "MyEngine/Input/XInputGamepad.h"


/// <summary>
/// すべての入力処理を管理するクラス
/// </summary>
class InputManager {
public:
	// コピー・ムーブ禁止
	InputManager() = default;
	~InputManager() = default;
	InputManager(const InputManager&) = delete;
	InputManager& operator=(const InputManager&) = delete;

	/// <summary>
	/// 初期化。ウィンドウ生成後に1度だけ呼ぶ。
	/// </summary>
	static void Initialize(HINSTANCE hInstance, HWND hwnd);

	/// <summary>
	/// 終了処理。プログラム終了時に1度だけ呼ぶ。
	/// </summary>
	static void Release();

	/// <summary>
	/// 毎フレームメインループの先頭で呼ぶ。
	/// </summary>
	static void Update();

	//============================================================
	// キーボード
	//============================================================

	/// <summary>指定したキーが押されているか。</summary>
	static bool IsKeyPressed(uint8_t key);

	/// <summary>指定したキーを押した瞬間か。</summary>
	static bool IsKeyTriggered(uint8_t key);

	/// <summary>指定したキーを離した瞬間か。</summary>
	static bool IsKeyReleased(uint8_t key);

	//============================================================
	// マウス ボタン番号: 0=左, 1=右, 2=中
	//============================================================

	/// <summary>指定ボタンが押されているか。</summary>
	static bool IsMouseButtonPressed(int button);

	/// <summary>指定ボタンを押した瞬間か。</summary>
	static bool IsMouseButtonTriggered(int button);

	/// <summary>指定ボタンを離した瞬間か。</summary>
	static bool IsMouseButtonReleased(int button);

	/// <summary>
	/// マウスの位置を、ウィンドウの中のピクセルで（左上が 0,0。右と下が +）。エディタ・エンジン用
	/// <para>エディタでは Hierarchy などのパネルの分だけずれる（Game ビューの中の位置ではない）。ゲームは GetMousePositionInGame を使う</para>
	/// </summary>
	static Vector2 GetMousePositionInWindow();

	/// <summary>
	/// マウスの位置を、ゲーム画面の座標で（スプライトの position と同じ：画面の真ん中が0、右と上が+、端が±1）
	/// <para>エディタでは Game ビューの絵の中、製品版ではウィンドウの黒帯の内側が画面。画面の外（黒帯の上も）なら ±1 を超える</para>
	/// </summary>
	static Vector2 GetMousePositionInGame();

	/// <summary>マウスがゲーム画面の上にあるか（エディタでは Game ビューの絵の上）</summary>
	static bool IsMouseInGame();

	/// <summary>
	/// ゲーム画面の上で OS のカーソル（Windows の矢印）を出すか。false で隠す（照準などの UI をカーソルの代わりに出すとき）
	/// <para>隠れるのは、マウスがゲーム画面の上にいる間だけ（エディタは Play 中に Game ビューの絵の上。一時停止中は出す。製品版は黒帯の内側）。ほかの窓・ゲームの外では出す</para>
	/// <para>シーンを変えると（Stop も）true に戻る。隠したいシーンが、自分で頼む</para>
	/// </summary>
	static void SetCursorVisible(bool visible) { GetInstance()->isCursorVisible_ = visible; }

	/// <summary>SetCursorVisible で頼んだ値（今本当に隠れているかではない。ゲーム画面の外では false でも出ている）</summary>
	static bool IsCursorVisible() { return GetInstance()->isCursorVisible_; }

	/// <summary>
	/// 今 OS のカーソルを隠す所か（SetCursorVisible(false) を頼まれていて、マウスがゲーム画面の上。Play 中かは見ない）
	/// <para>WindowManager が毎フレーム（UpdateAll の最後）見て、メインのウィンドウに渡す</para>
	/// </summary>
	static bool ShouldHideCursor();

	/// <summary>前フレームからのX移動量 (右が正)。</summary>
	static long GetMouseDeltaX();

	/// <summary>前フレームからのY移動量 (下が正)。</summary>
	static long GetMouseDeltaY();

	/// <summary>ホイール移動量 (上スクロールが正)。</summary>
	static long GetMouseDeltaWheel();

	//============================================================
	// Xbox コントローラー
	// キーボードと同じく、ウィンドウが手前に無いとき・ImGui に文字を打っているときは何も押していない扱い
	//============================================================

	/// <summary>指定番号のコントローラーが接続されているか。</summary>
	static bool IsGamepadConnected(int index = 0);

	/// <summary>指定ボタンが押されているか。</summary>
	static bool IsButtonPressed(uint16_t button, int index = 0);

	/// <summary>指定ボタンを押した瞬間か。</summary>
	static bool IsButtonTriggered(uint16_t button, int index = 0);

	/// <summary>指定ボタンを離した瞬間か。</summary>
	static bool IsButtonReleased(uint16_t button, int index = 0);

	/// <summary>左スティックX軸 (-1.0f=左, 1.0f=右, デッドゾーン処理済み)。</summary>
	static float GetLeftStickX(int index = 0);

	/// <summary>左スティックY軸 (-1.0f=下, 1.0f=上, デッドゾーン処理済み)。</summary>
	static float GetLeftStickY(int index = 0);

	/// <summary>右スティックX軸 (-1.0f=左, 1.0f=右, デッドゾーン処理済み)。</summary>
	static float GetRightStickX(int index = 0);

	/// <summary>右スティックY軸 (-1.0f=下, 1.0f=上, デッドゾーン処理済み)。</summary>
	static float GetRightStickY(int index = 0);

	/// <summary>左トリガー (0.0f=未押し, 1.0f=最大)。</summary>
	static float GetLeftTrigger(int index = 0);

	/// <summary>右トリガー (0.0f=未押し, 1.0f=最大)。</summary>
	static float GetRightTrigger(int index = 0);

	/// <summary>
	/// 振動をセットする。
	/// <para>leftMotor  : 低周波モーター(ドスン系) 0.0f〜1.0f</para>
	/// <para>rightMotor : 高周波モーター(ブルブル系) 0.0f〜1.0f</para>
	/// </summary>
	static void SetVibration(float leftMotor, float rightMotor, int index = 0);

	/// <summary>
	/// 振動を止める。
	/// </summary>
	static void StopVibration(int index = 0);

	// ---- デッドゾーン設定 (0.0f〜1.0f, デフォルトは Microsoft 推奨値) ----
	static void SetLeftStickDeadzone(float deadzone);
	static void SetRightStickDeadzone(float deadzone);
	static void SetTriggerDeadzone(float deadzone);
	static float GetLeftStickDeadzone();
	static float GetRightStickDeadzone();
	static float GetTriggerDeadzone();

	//============================================================
	// まとめて読む
	//============================================================

	/// <summary>
	/// 移動の入力（WASD・矢印キー・左スティックをまとめた物）。x は右が+、y は上（奥）が+
	/// <para>-1〜1。斜め（キー2つ・キーとスティックを一緒）でも長さは1まで（斜めに速くならない）</para>
	/// </summary>
	static Vector2 GetMoveAxis(bool useController = true);

	// 入力を検知するウィンドウをセット
	static void SetActiveWindow(HWND hwnd) { GetInstance()->activeHwnd_ = hwnd; }

#ifdef USE_IMGUI
	// マウスがViewportにあるか
	static bool IsMouseInViewport() { return GetInstance()->mouseInViewport_; }
	static void SetMouseInViewport(bool inViewport) { GetInstance()->mouseInViewport_ = inViewport; }
	// Viewportがフォーカスされてるか
	static void SetViewportFocused(bool focused) { GetInstance()->viewportFocused_ = focused; }
	static bool IsViewportFocused() { return GetInstance()->viewportFocused_; }
	// Game ビューの絵がウィンドウのどこにあるか（左上・大きさ。クライアント座標）。EditorViewport が毎フレーム渡す
	static void SetGameViewRect(const Vector2& topLeft, const Vector2& size) {
		GetInstance()->gameViewTopLeft_ = topLeft;
		GetInstance()->gameViewSize_ = size;
	}
	// マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）。EditorViewport が毎フレーム渡す（OS のカーソルを隠すかに使う）
	static void SetGameViewHovered(bool hovered) { GetInstance()->isGameViewHovered_ = hovered; }
#endif

private:
	static InputManager* GetInstance();

	// 内部ヘルパー
	void initInternal(HINSTANCE hInstance, HWND hwnd);
	void finalizeInternal();
	void updateInternal();
	static bool IsMouseCapturedByImGui();
	static bool IsKeyboardCapturedByImGui();
	static bool IsTypingInImGui(); // ImGui の入力欄に文字を打っている最中か
	static bool IsActive() { return GetInstance()->activeHwnd_ != nullptr; }
	static bool IsGamepadBlocked() { return !IsActive() || IsTypingInImGui(); } // パッドを「何も押していない」扱いにするか
	// ゲーム画面がウィンドウのどこにあるか（左上・大きさ。クライアント座標）
	static void GetGameViewRect(Vector2& topLeft, Vector2& size);

	HWND activeHwnd_ = nullptr;
	HWND mainHwnd_ = nullptr; // Initialize で受け取ったウィンドウ（製品版のゲーム画面＝このウィンドウの中）
	bool isCursorVisible_ = true; // ゲーム画面の上で OS のカーソルを出すか（SetCursorVisible で頼んだ値）

#ifdef USE_IMGUI
	bool mouseInViewport_ = false;           // マウスがViewportにあるか
	bool viewportFocused_ = false;           // Viewportがフォーカスされてるか
	Vector2 gameViewTopLeft_ = {0.0f, 0.0f}; // Game ビューの絵の左上（クライアント座標）
	Vector2 gameViewSize_ = {0.0f, 0.0f};    // Game ビューの絵の大きさ（0なら、まだ描いていない）
	bool isGameViewHovered_ = false;         // マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）
#endif

	// DirectInput8 インターフェース。キーボードとマウスで共有する。
	Microsoft::WRL::ComPtr<IDirectInput8> directInput_ = nullptr;

	KeyboardInput keyboard_; // DirectInput キーボード
	MouseInput mouse_;       // DirectInput マウス
	XInputGamepad gamepad_;  // XInput Xbox コントローラー
};