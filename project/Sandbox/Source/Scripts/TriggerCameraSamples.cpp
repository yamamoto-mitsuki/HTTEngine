#include "Source/Scripts/TriggerCameraSamples.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>

#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Input/InputManager.h"
#include "MyEngine/Time/EngineTime.h"

SCRIPT(TriggerCameraSwitcher, "Test");
SCRIPT(SideSwing, "Test");

namespace {
// ===== トリガーを押した瞬間・離した瞬間を Console に出す =====
void LogTrigger(const char* name, uint32_t button, float amount) {
	if (InputManager::IsButtonTriggered(button)) {
		LogManager::Log(std::format("{} を押した（量 {:.2f}）", name, amount));
	}
	if (InputManager::IsButtonReleased(button)) {
		LogManager::Log(std::format("{} を離した（量 {:.2f}）", name, amount));
	}
}
} // namespace


//=============================================================================
// TriggerCameraSwitcher
//=============================================================================
// ===== 初期化 =====
void TriggerCameraSwitcher::Start() {
	if (const CameraComponent* camera = otherCamera_.GetComponent<CameraComponent>()) {
		basePriority_ = camera->priority;
	}
}

// ===== 更新 =====
void TriggerCameraSwitcher::Update() {
	// --- LT・RT：押した瞬間・離した瞬間（量も出して、しきい値で判定しているのを見る）---
	LogTrigger("LT", GamepadButton::LeftTrigger, InputManager::GetLeftTrigger());
	LogTrigger("RT", GamepadButton::RightTrigger, InputManager::GetRightTrigger());

	// --- RT か Enter を押した瞬間：Other Camera の Priority を上げ下げして切り替える ---
	if (!InputManager::IsButtonTriggered(GamepadButton::RightTrigger) && !InputManager::IsKeyTriggered(DIK_RETURN)) {
		return;
	}
	CameraComponent* camera = otherCamera_.GetComponent<CameraComponent>();
	if (camera == nullptr) {
		LogManager::Log("TriggerCameraSwitcher：Other Camera に Camera Component が付いた Entity を入れてください");
		return;
	}
	isSwitched_ = !isSwitched_;
	camera->priority = isSwitched_ ? switchedPriority_ : basePriority_;
}

// ===== Inspector に出す・保存する値 =====
void TriggerCameraSwitcher::Fields(ComponentUI& ui) {
	ui.Field("Other Camera###otherCamera", otherCamera_, Tip("切り替える先のカメラ（Camera Component を付けた Entity。Hierarchy からドラッグ）"));
	ui.Field("Switched Priority###switchedPriority", switchedPriority_, Tip("切り替えている間の Other Camera の Priority（今のカメラより大きくする）"));
}


//=============================================================================
// SideSwing
//=============================================================================
// ===== 初期化 =====
void SideSwing::Start() {
	center_ = transform.translation;
}

// ===== 更新 =====
void SideSwing::Update() {
	time_ += Time::GetDeltaTime();
	// 1往復で sin が1回まわる（2π）。period_ が 0 に近いと割れないので、少しだけ下を止める
	const float phase = 2.0f * std::numbers::pi_v<float> * time_ / (std::max)(period_, 0.01f);
	transform.translation.x = center_.x + width_ * std::sin(phase);
}

// ===== Inspector に出す・保存する値 =====
void SideSwing::Fields(ComponentUI& ui) {
	ui.Field("Width###width", width_, AtLeast(0.0f).Tip("真ん中から左右へ動く幅（m）"));
	ui.Field("Period###period", period_, AtLeast(0.01f).Tip("1往復の秒"));
}
