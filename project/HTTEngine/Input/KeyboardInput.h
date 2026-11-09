#pragma once
#include <array>
#include <cstdint>

#include <wrl.h>
#include <dinput.h>
#include <Windows.h>


/// <summary>
/// キーボード入力を管理するクラス 
/// </summary>
class KeyboardInput {
public:
	/// <summary>初期化。DirectInput8インターフェースとHWNDを渡す。</summary>
	void Init(IDirectInput8* directInput, HWND hwnd);

	/// <summary>終了処理。デバイスのUnacquireとRelease。</summary>
	void Finalize();

	/// <summary>毎フレーム呼ぶ。前フレームを保存してから今フレームを取得する。</summary>
	void Update();

	/// <summary>指定したキーが押されているか。キー定数は DIK_*** を使う。</summary>
	bool IsPressed(uint8_t key) const;

	/// <summary>指定したキーを押した瞬間か。</summary>
	bool IsTriggered(uint8_t key) const;

	/// <summary>指定したキーを離した瞬間か。</summary>
	bool IsReleased(uint8_t key) const;

	// 入力をリセット
	void Clear() { state_.fill(0); }

private:
	Microsoft::WRL::ComPtr<IDirectInputDevice8> device_ = nullptr;

	std::array<BYTE, 256> state_{};
	std::array<BYTE, 256> statePrev_{};
};