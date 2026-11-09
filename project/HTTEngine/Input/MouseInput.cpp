#include "HTTEngine/Input/MouseInput.h"

#include <format>
#include <cstring>
#include <iterator>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"


void MouseInput::Init(IDirectInput8* directInput, HWND hwnd) {
	HTT_ASSERT_MSG(directInput, "DirectInput8インターフェースがnullです");
	HTT_ASSERT_MSG(hwnd, "HWNDがnullです");

	hwnd_ = hwnd;
	HRESULT hr;

	// --- マウスデバイスの生成 ---
	hr = directInput->CreateDevice(GUID_SysMouse, &device_, nullptr);
	if (FAILED(hr)) {
		LogManager::Error(std::format("HRESULT=0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "マウスデバイスの生成に失敗しました");
	}
	LogManager::Log("CreateDevice Success");

	// --- 入力データ形式のセット ---
	hr = device_->SetDataFormat(&c_dfDIMouse2);
	if (FAILED(hr)) {
		LogManager::Error(std::format("HRESULT=0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "マウスのデータフォーマット設定に失敗しました");
	}

	// --- 協調レベルのセット ---
	hr = device_->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
	if (FAILED(hr)) {
		LogManager::Error(std::format("HRESULT=0x{:08X}", (uint32_t)hr));
		HTT_ASSERT_MSG(false, "マウスの協調レベル設定に失敗しました");
	}

	LogManager::Log("Initialized");
}

void MouseInput::Finalize() {
	if (device_) {
		device_->Unacquire();
		device_.Reset();
		LogManager::Log("Finalized");
	}
}

void MouseInput::Update() {
	statePrev_ = state_;

	HRESULT hr = device_->Acquire();
	if (SUCCEEDED(hr)) {
		hr = device_->GetDeviceState(sizeof(DIMOUSESTATE2), &state_);
		if (FAILED(hr)) {
			memset(&state_, 0, sizeof(state_));
		}
	} else {
		memset(&state_, 0, sizeof(state_));
	}

	// --- クライアント座標でのマウス現在位置を取得 ---
	// DirectInput は相対値(移動量)しか返さないため Win32 API を使う。
	// GetCursorPos  : スクリーン座標(モニター左上が原点)でカーソル位置を取得
	// ScreenToClient: スクリーン座標 → 指定ウィンドウのクライアント座標に変換
	POINT p{};
	if (GetCursorPos(&p) && ScreenToClient(hwnd_, &p)) {
		posX_ = p.x;
		posY_ = p.y;
	}
}

bool MouseInput::IsButtonPressed(int button) const {
	if (button < 0 || button >= 8) {
		return false;
	}

	return (state_.rgbButtons[button] & 0x80) != 0;
}

bool MouseInput::IsButtonTriggered(int button) const {
	if (button < 0 || button >= 8) {
		return false;
	}
	return (state_.rgbButtons[button] & 0x80) != 0 && (statePrev_.rgbButtons[button] & 0x80) == 0;
}

bool MouseInput::IsButtonReleased(int button) const {
	if (button < 0 || button >= 8) {
		return false;
	}
	return (state_.rgbButtons[button] & 0x80) == 0 && (statePrev_.rgbButtons[button] & 0x80) != 0;
}
bool MouseInput::IsAnyButtonPressed() const {
	// rgbButtons は DIMOUSESTATE2 の 8 個のボタン（上の 0x80 が押している印）
	for (const BYTE button : state_.rgbButtons) {
		if ((button & 0x80) != 0) {
			return true;
		}
	}
	return false;
}

bool MouseInput::IsAnyButtonReleased() const {
	for (int i = 0; i < static_cast<int>(std::size(state_.rgbButtons)); ++i) {
		if (IsButtonReleased(i)) {
			return true;
		}
	}
	return false;
}
