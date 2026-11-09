#pragma once
#include <array>
#include <cstdint>

#include <Windows.h>
#include <Xinput.h>

// ============================================================
// ボタン定数
// 下の 16 ビットは XInput のボタンの印（wButtons）そのまま。トリガーはその上のビットに置く
// （XInput の 16 ビットの中の空き 0x0400・0x0800 は、公開されていない Guide ボタンなどが使うので避ける）
// ============================================================
namespace GamepadButton {
constexpr uint32_t DpadUp = XINPUT_GAMEPAD_DPAD_UP;
constexpr uint32_t DpadDown = XINPUT_GAMEPAD_DPAD_DOWN;
constexpr uint32_t DpadLeft = XINPUT_GAMEPAD_DPAD_LEFT;
constexpr uint32_t DpadRight = XINPUT_GAMEPAD_DPAD_RIGHT;
constexpr uint32_t Start = XINPUT_GAMEPAD_START;
constexpr uint32_t Back = XINPUT_GAMEPAD_BACK;
constexpr uint32_t LeftThumb = XINPUT_GAMEPAD_LEFT_THUMB;   // 左スティック押し込み
constexpr uint32_t RightThumb = XINPUT_GAMEPAD_RIGHT_THUMB; // 右スティック押し込み
constexpr uint32_t LeftShoulder = XINPUT_GAMEPAD_LEFT_SHOULDER;
constexpr uint32_t RightShoulder = XINPUT_GAMEPAD_RIGHT_SHOULDER;
constexpr uint32_t A = XINPUT_GAMEPAD_A;
constexpr uint32_t B = XINPUT_GAMEPAD_B;
constexpr uint32_t X = XINPUT_GAMEPAD_X;
constexpr uint32_t Y = XINPUT_GAMEPAD_Y;
// --- トリガー（LT・RT）をボタンとして読む。引いた量がしきい値を越えたら「押した」（SetTriggerPressThreshold / SetTriggerReleaseThreshold）---
constexpr uint32_t LeftTrigger = 1u << 16;
constexpr uint32_t RightTrigger = 1u << 17;
} // namespace GamepadButton

class XInputGamepad {
public:
	// コントローラーの最大台数
	static constexpr int kMaxControllers = 4;

	/// <summary>初期化。接続済みコントローラーをログに出す。</summary>
	void Init();

	/// <summary>終了処理。全コントローラーの振動を止める。</summary>
	void Finalize();

	/// <summary>毎フレーム呼ぶ。接続状態と入力状態を更新する。</summary>
	void Update();

	// ---- 接続確認 ----

	/// <summary>指定番号のコントローラーが接続されているか。</summary>
	bool IsConnected(int index = 0) const;

	// ---- ボタン (GamepadButton::*** を使う。LeftTrigger・RightTrigger も読める) ----

	/// <summary>指定ボタンが押されているか。</summary>
	bool IsButtonPressed(uint32_t button, int index = 0) const;

	/// <summary>指定ボタンを押した瞬間か。</summary>
	bool IsButtonTriggered(uint32_t button, int index = 0) const;

	/// <summary>指定ボタンを離した瞬間か。</summary>
	bool IsButtonReleased(uint32_t button, int index = 0) const;

	// ---- スティック (-1.0f〜1.0f) ----

	/// <summary>左スティックX軸 (-1.0f=左, 1.0f=右)。</summary>
	float GetLeftStickX(int index = 0) const;

	/// <summary>左スティックY軸 (-1.0f=下, 1.0f=上)。</summary>
	float GetLeftStickY(int index = 0) const;

	/// <summary>右スティックX軸 (-1.0f=左, 1.0f=右)。</summary>
	float GetRightStickX(int index = 0) const;

	/// <summary>右スティックY軸 (-1.0f=下, 1.0f=上)。</summary>
	float GetRightStickY(int index = 0) const;

	// ---- トリガー (0.0f〜1.0f) ----

	/// <summary>左トリガー (0.0f=未押し, 1.0f=最大)。</summary>
	float GetLeftTrigger(int index = 0) const;

	/// <summary>右トリガー (0.0f=未押し, 1.0f=最大)。</summary>
	float GetRightTrigger(int index = 0) const;

	// ---- デッドゾーン設定 (0.0f〜1.0f) ----
	/// <summary>左スティックのデッドゾーンを設定 (0.0f〜1.0f)。</summary>
	void SetLeftStickDeadzone(float deadzone);

	/// <summary>右スティックのデッドゾーンを設定 (0.0f〜1.0f)。</summary>
	void SetRightStickDeadzone(float deadzone);

	/// <summary>トリガーのデッドゾーンを設定 (0.0f〜1.0f)。</summary>
	void SetTriggerDeadzone(float deadzone);

	float GetLeftStickDeadzone() const { return leftStickDeadzone_; }
	float GetRightStickDeadzone() const { return rightStickDeadzone_; }
	float GetTriggerDeadzone() const { return triggerDeadzone_; }

	// ---- トリガーをボタンとして読むときのしきい値 (0.0f〜1.0f。デッドゾーンを引いた後の量で比べる) ----
	/// <summary>この量以上引いたら「押した」（GamepadButton::LeftTrigger・RightTrigger）。</summary>
	void SetTriggerPressThreshold(float threshold);

	/// <summary>押している間は、この量より戻したら「離した」。押すしきい値より大きくしても、押すしきい値として扱う。</summary>
	void SetTriggerReleaseThreshold(float threshold);

	float GetTriggerPressThreshold() const { return triggerPressThreshold_; }
	float GetTriggerReleaseThreshold() const { return triggerReleaseThreshold_; }

	// ---- 振動 ----

	/// <summary>
	/// 振動をセットする。
	/// leftMotor  : 低周波モーター(ドスン系) 0.0f〜1.0f
	/// rightMotor : 高周波モーター(ブルブル系) 0.0f〜1.0f
	/// </summary>
	void SetVibration(float leftMotor, float rightMotor, int index = 0);

	/// <summary>振動を止める。</summary>
	void StopVibration(int index = 0);

private:
	/// <summary>
	/// int16_t の生値をデッドゾーン処理して -1.0f〜1.0f に正規化する。
	/// デッドゾーン内はゼロを返す。
	/// </summary>
	float ApplyDeadzone(int16_t value, float deadzone) const;

	/// <summary>
	/// 今フレームのボタンの印（XInput のボタン ＋ トリガーをしきい値で判定した物）を作る。Update が前フレームの印を buttonsPrev へ移した後に呼ぶ
	/// </summary>
	uint32_t MakeButtons(int index) const;

	/// <summary>
	/// トリガー1本を押しているか。押す・離すのしきい値を分けて、間で止めても押し直しにならないようにする（ヒステリシス）
	/// </summary>
	/// <param name="amount">今の引いた量（0.0f〜1.0f。デッドゾーン処理済み）</param>
	/// <param name="wasPressed">前フレームに押していたか</param>
	bool IsTriggerHeld(float amount, bool wasPressed) const;

	// デッドゾーン (0.0f〜1.0f)。Microsoft 推奨値をデフォルトに使用。
	float leftStickDeadzone_ = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;   // ≒0.239f
	float rightStickDeadzone_ = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.0f; // ≒0.265f
	float triggerDeadzone_ = XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 255.0f;         // ≒0.118f

	// トリガーをボタンとして読むときのしきい値（Unity の Input System の Press Point と同じ考え）
	float triggerPressThreshold_ = 0.5f;   // この量以上引いたら押した
	float triggerReleaseThreshold_ = 0.4f; // 押している間は、この量より戻したら離した

	// コントローラー1台分の状態をまとめた構造体
	struct ControllerState {
		XINPUT_STATE state = {};  // 今フレームの入力状態（スティック・トリガーの量を読む）
		uint32_t buttons = 0;     // 今フレームのボタンの印（GamepadButton::***。トリガーも入る）
		uint32_t buttonsPrev = 0; // 前フレームのボタンの印（Triggered/Released判定に使う）
		bool connected = false;
	};
	std::array<ControllerState, kMaxControllers> controllers_{};
};