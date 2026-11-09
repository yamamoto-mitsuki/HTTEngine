#pragma once
#include <cstdint>

#include <wrl.h>
#include <dinput.h>
#include <Windows.h>

/// <summary>
/// マウスの入力処理を管理するクラス
/// </summary>
class MouseInput {
public:
	/// <summary>初期化。DirectInput8インターフェースとHWNDを渡す。</summary>
	void Init(IDirectInput8* directInput, HWND hwnd);

	/// <summary>終了処理。</summary>
	void Finalize();

	/// <summary>毎フレーム呼ぶ。移動量・現在位置・ボタン状態を更新する。</summary>
	void Update();

	// ---- ボタン (0=左, 1=右, 2=中ボタン) ----

	/// <summary>指定ボタンが押されているか。</summary>
	bool IsButtonPressed(int button) const;

	/// <summary>指定ボタンを押した瞬間か。</summary>
	bool IsButtonTriggered(int button) const;

	/// <summary>指定ボタンを離した瞬間か。</summary>
	bool IsButtonReleased(int button) const;

	/// <summary>どれかのボタン（8個のどれか）が押されているか。</summary>
	bool IsAnyButtonPressed() const;

	/// <summary>どれかのボタンを、このフレームに離したか。</summary>
	bool IsAnyButtonReleased() const;

	// ---- 現在位置 (クライアント座標) ----

	/// <summary>マウスX座標(クライアント座標、左端が0)。</summary>
	long GetPositionX() const { return posX_; }

	/// <summary>マウスY座標(クライアント座標、上端が0)。</summary>
	long GetPositionY() const { return posY_; }

	// ---- 移動量 (前フレームからの相対値) ----

	/// <summary>X軸移動量(右が正)。</summary>
	long GetDeltaX() const { return state_.lX; }

	/// <summary>Y軸移動量(下が正)。</summary>
	long GetDeltaY() const { return state_.lY; }

	/// <summary>ホイール移動量(上スクロールが正)。</summary>
	long GetDeltaWheel() const { return state_.lZ; }

	// 入力をリセット
	void Clear() { memset(&state_, 0, sizeof(state_)); }

private:
	Microsoft::WRL::ComPtr<IDirectInputDevice8> device_ = nullptr;
	HWND hwnd_ = nullptr;
	DIMOUSESTATE2 state_ = {};
	DIMOUSESTATE2 statePrev_ = {};

	// クライアント座標でのマウス現在位置
	long posX_ = 0;
	long posY_ = 0;
};