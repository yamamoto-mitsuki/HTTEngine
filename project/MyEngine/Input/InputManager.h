#pragma once
#include <cstdint>
#include <optional>

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
	/// 今 OS のカーソルを隠す所か（SetCursorVisible(false) を頼まれていて、マウスがゲーム画面の上か、カーソルを閉じ込めている。Play 中かは見ない）
	/// <para>WindowManager が毎フレーム（UpdateAll の最後）見て、メインのウィンドウに渡す</para>
	/// </summary>
	static bool ShouldHideCursor();

	/// <summary>
	/// OS のカーソルをゲーム画面の中に閉じ込めるか。true で頼む（外へ出られなくなり、画面の端で止まる。真ん中へ戻し続けるのではない＝Unity の CursorLockMode.Confined）
	/// <para>閉じ込めている間も GetMouseDeltaX/Y は動いた分を返す（カーソルが画面の端で止まっていても）。エディタの Game ビューの上で ImGui に取られて 0 にもならない</para>
	/// <para>本当に閉じ込めるのは、Play 中（一時停止中は放す）で、メインのウィンドウが手前の間だけ（Alt+Tab などで手前でなくなったら放す。製品版は手前に戻るとまた閉じ込める）</para>
	/// <para>エディタ：閉じ込めている間は、エディタのパネルをマウスで触れない。Shift+F1 で放す（手前でなくなったときも放す）。Game ビューの絵を左クリックすると、また閉じ込める（そのクリックはゲームに渡さない）</para>
	/// <para>シーンを変えると（Stop も）false に戻る。閉じ込めたいシーンが、自分で頼む。隠すのは SetCursorVisible（別に頼む）</para>
	/// </summary>
	static void SetCursorConfined(bool confined) { GetInstance()->isCursorConfineRequested_ = confined; }

	/// <summary>SetCursorConfined で頼んだ値（今本当に閉じ込めているかではない。それは IsCursorConfineActive）</summary>
	static bool IsCursorConfined() { return GetInstance()->isCursorConfineRequested_; }

	/// <summary>今本当に OS のカーソルをゲーム画面に閉じ込めているか（前のフレームの終わりに WindowManager が決めた値。一時停止中・手前でない・エディタで放した間は false）</summary>
	static bool IsCursorConfineActive() { return GetInstance()->isCursorConfineActive_; }

	/// <summary>
	/// 今カーソルを閉じ込める四角（メインのウィンドウのクライアント座標。ゲーム画面の内側に丸めた物）。閉じ込めない所なら空（Play 中かは見ない）
	/// <para>閉じ込めるのは：SetCursorConfined(true) を頼まれていて、メインのウィンドウが手前で、ゲーム画面がある（エディタは Game ビューの絵が見えていて、Shift+F1 で放していない）とき</para>
	/// <para>WindowManager が毎フレーム（UpdateAll の最後）見て、メインのウィンドウに渡し、SetCursorConfineActive で結果を返す</para>
	/// </summary>
	static std::optional<RECT> GetCursorConfineRect();

	/// <summary>今本当にカーソルを閉じ込めたか。WindowManager が毎フレーム（UpdateAll の最後）渡す</summary>
	static void SetCursorConfineActive(bool active) { GetInstance()->isCursorConfineActive_ = active; }

	/// <summary>
	/// 隠す・閉じ込める頼みを戻す（SetCursorVisible(true)・SetCursorConfined(false)。エディタで Shift+F1 で放したのも戻す）
	/// <para>シーンを変えたとき（Stop も）に SceneManager が呼ぶ。前のシーンの頼みを持ち越さない</para>
	/// </summary>
	static void ResetCursorRequests();

	/// <summary>
	/// 前フレームからのX移動量 (右が正)。
	/// <para>単位はマウスが数えた数（mickey。DirectInput の相対の値）。ピクセルではない。コントロールパネルのポインターの速さ・加速は掛からない</para>
	/// <para>どれだけ動かすと 1 になるかはマウス（DPI）で変わる。ふつうの設定（ポインターの速さが真ん中・加速なし）なら、Windows の矢印の 1 ピクセルとおよそ同じ</para>
	/// <para>ゲームで使うときは「1 カウントを何ピクセルにするか」の倍率（感度）を掛けてピクセルの量にし、GameScreen::PixelToNdc で画面の位置の量にする</para>
	/// <para>カーソルを閉じ込めている間（SetCursorConfined）は、画面の端でカーソルが止まっていても動いた分を返す</para>
	/// </summary>
	static long GetMouseDeltaX();

	/// <summary>前フレームからのY移動量 (下が正)。単位は GetMouseDeltaX と同じ（マウスが数えた数）</summary>
	static long GetMouseDeltaY();

	/// <summary>ホイール移動量 (上スクロールが正)。</summary>
	static long GetMouseDeltaWheel();

	//============================================================
	// Xbox コントローラー
	// キーボードと同じく、ウィンドウが手前に無いとき・ImGui に文字を打っているときは何も押していない扱い
	//============================================================

	/// <summary>指定番号のコントローラーが接続されているか。</summary>
	static bool IsGamepadConnected(int index = 0);

	/// <summary>
	/// 指定ボタンが押されているか。button は GamepadButton::***
	/// <para>GamepadButton::LeftTrigger・RightTrigger で、トリガーをしきい値（SetTriggerPressThreshold / SetTriggerReleaseThreshold）で判定したボタンとして読める</para>
	/// </summary>
	static bool IsButtonPressed(uint32_t button, int index = 0);

	/// <summary>指定ボタンを押した瞬間か。トリガーは、押すしきい値まで引いたフレーム</summary>
	static bool IsButtonTriggered(uint32_t button, int index = 0);

	/// <summary>指定ボタンを離した瞬間か。トリガーは、離すしきい値より戻したフレーム</summary>
	static bool IsButtonReleased(uint32_t button, int index = 0);

	/// <summary>左スティックX軸 (-1.0f=左, 1.0f=右, デッドゾーン処理済み)。</summary>
	static float GetLeftStickX(int index = 0);

	/// <summary>左スティックY軸 (-1.0f=下, 1.0f=上, デッドゾーン処理済み)。</summary>
	static float GetLeftStickY(int index = 0);

	/// <summary>右スティックX軸 (-1.0f=左, 1.0f=右, デッドゾーン処理済み)。</summary>
	static float GetRightStickX(int index = 0);

	/// <summary>右スティックY軸 (-1.0f=下, 1.0f=上, デッドゾーン処理済み)。</summary>
	static float GetRightStickY(int index = 0);

	/// <summary>左トリガー (0.0f=未押し, 1.0f=最大)。押した瞬間を読むなら IsButtonTriggered(GamepadButton::LeftTrigger)</summary>
	static float GetLeftTrigger(int index = 0);

	/// <summary>右トリガー (0.0f=未押し, 1.0f=最大)。押した瞬間を読むなら IsButtonTriggered(GamepadButton::RightTrigger)</summary>
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

	// ---- トリガーをボタンとして読むときのしきい値 (0.0f〜1.0f。デッドゾーン処理後の量。既定は押す 0.5・離す 0.4) ----
	// 2つに分けるのは、しきい値の近くで指を止めたときに、押した・離したが何度も出ないように（離す方を小さくする）
	static void SetTriggerPressThreshold(float threshold);   // この量以上引いたら押した
	static void SetTriggerReleaseThreshold(float threshold); // 押している間は、この量より戻したら離した
	static float GetTriggerPressThreshold();
	static float GetTriggerReleaseThreshold();

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
	// Game ビューの絵がウィンドウのどこにあるか（左上・大きさ。クライアント座標）。EditorViewport が毎フレーム渡す（見えていない＝ほかのタブの裏なら、大きさ 0）
	static void SetGameViewRect(const Vector2& topLeft, const Vector2& size) {
		GetInstance()->gameViewTopLeft_ = topLeft;
		GetInstance()->gameViewSize_ = size;
	}
	// マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）。EditorViewport が毎フレーム渡す（OS のカーソルを隠すかに使う）
	static void SetGameViewHovered(bool hovered) { GetInstance()->isGameViewHovered_ = hovered; }
	// カーソルを閉じ込めるのを、エディタで放しているか（Shift+F1 を押した・手前でなくなった。Game ビューを左クリックするか、シーンを変えると戻る）。Game ビューに出す案内に使う
	static bool IsCursorConfineReleasedInEditor() { return GetInstance()->isCursorConfineReleasedInEditor_; }
#endif

private:
	static InputManager* GetInstance();

	// 内部ヘルパー
	void InitInternal(HINSTANCE hInstance, HWND hwnd);
	void FinalizeInternal();
	void UpdateInternal();
	static bool IsMouseCapturedByImGui();
	static bool IsKeyboardCapturedByImGui();
	static bool IsTypingInImGui(); // ImGui の入力欄に文字を打っている最中か
	static bool IsActive() { return GetInstance()->activeHwnd_ != nullptr; }
	static bool IsGamepadBlocked() { return !IsActive() || IsTypingInImGui(); } // パッドを「何も押していない」扱いにするか
	// ゲーム画面がウィンドウのどこにあるか（左上・大きさ。クライアント座標）
	static void GetGameViewRect(Vector2& topLeft, Vector2& size);
	// メインのウィンドウが手前か（カーソルを閉じ込めるのは、手前の間だけ）
	bool IsMainWindowForeground() const { return mainHwnd_ != nullptr && GetForegroundWindow() == mainHwnd_; }
	// また閉じ込めたクリックを、ボタンを全部離すまでゲームに渡さない（フレームの頭。UpdateInternal から）
	void UpdateMouseSwallow();
#ifdef USE_IMGUI
	// エディタで閉じ込めるのを放す・また閉じ込める（フレームの頭、ゲームが入力を読む前。UpdateInternal から）
	void UpdateEditorCursorConfine();
#endif

	HWND activeHwnd_ = nullptr;
	HWND mainHwnd_ = nullptr; // Initialize で受け取ったウィンドウ（製品版のゲーム画面＝このウィンドウの中）
	bool isCursorVisible_ = true;           // ゲーム画面の上で OS のカーソルを出すか（SetCursorVisible で頼んだ値）
	bool isCursorConfineRequested_ = false; // OS のカーソルをゲーム画面に閉じ込めるか（SetCursorConfined で頼んだ値）
	bool isCursorConfineActive_ = false;    // 今本当に閉じ込めているか（WindowManager が SetCursorConfineActive で渡す）
	bool isMouseButtonSwallowed_ = false;   // マウスのボタンを「何も押していない」扱いにしているか（また閉じ込めたクリック。ボタンを全部離した次のフレームに戻る）

#ifdef USE_IMGUI
	bool mouseInViewport_ = false;           // マウスがViewportにあるか
	bool viewportFocused_ = false;           // Viewportがフォーカスされてるか
	Vector2 gameViewTopLeft_ = {0.0f, 0.0f}; // Game ビューの絵の左上（クライアント座標）
	Vector2 gameViewSize_ = {0.0f, 0.0f};    // Game ビューの絵の大きさ（0なら、まだ描いていない）
	bool isGameViewHovered_ = false;         // マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）
	bool isCursorConfineReleasedInEditor_ = false; // カーソルを閉じ込めるのを、エディタで放しているか（Shift+F1・手前でなくなった）
#endif

	// DirectInput8 インターフェース。キーボードとマウスで共有する。
	Microsoft::WRL::ComPtr<IDirectInput8> directInput_ = nullptr;

	KeyboardInput keyboard_; // DirectInput キーボード
	MouseInput mouse_;       // DirectInput マウス
	XInputGamepad gamepad_;  // XInput Xbox コントローラー
};