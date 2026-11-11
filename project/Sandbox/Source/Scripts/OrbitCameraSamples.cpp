#include "Source/Scripts/OrbitCameraSamples.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Components/Camera/CameraOrbitComponent.h"
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Time/EngineTime.h"

SCRIPT(OrbitCameraSwitcher, "Test");
SCRIPT(OrbitYawInput, "Test");

namespace {
// 1〜9 キー（DirectInput のキーの番号。数字の順に並んでいる）
constexpr std::array<uint8_t, 9> kNumberKeys = {DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8, DIK_9};
} // namespace


//=============================================================================
// OrbitCameraSwitcher
//=============================================================================
// ===== 初期化 =====
void OrbitCameraSwitcher::Start() { Select(0); }

// ===== 更新 =====
void OrbitCameraSwitcher::Update() {
	const int count = static_cast<int>(cameras_.size());
	if (count == 0) {
		return;
	}
	// --- 番号のキー ---
	for (int i = 0; i < (std::min)(count, static_cast<int>(kNumberKeys.size())); ++i) {
		if (InputManager::IsKeyTriggered(kNumberKeys[static_cast<size_t>(i)])) {
			Select(i);
			return;
		}
	}
	// --- 次・前（端まで行ったら反対の端へ）---
	if (InputManager::IsKeyTriggered(DIK_TAB) || InputManager::IsButtonTriggered(GamepadButton::RightShoulder)) {
		Select((selected_ + 1) % count);
	} else if (InputManager::IsButtonTriggered(GamepadButton::LeftShoulder)) {
		Select((selected_ + count - 1) % count);
	}
	// --- 選んだカメラの値は Inspector・OrbitYawInput で変わるので、毎フレーム書き直す ---
	if (TextComponent* text = label_.GetComponent<TextComponent>()) {
		text->text = MakeLabel();
	}
}

// ===== 選ぶ =====
void OrbitCameraSwitcher::Select(int index) {
	if (index < 0 || index >= static_cast<int>(cameras_.size())) {
		return;
	}
	selected_ = index;
	for (size_t i = 0; i < cameras_.size(); ++i) {
		if (CameraComponent* camera = cameras_[i].GetComponent<CameraComponent>()) {
			camera->priority = (static_cast<int>(i) == selected_) ? activePriority_ : 0;
		}
	}
	LogManager::Log(std::format("OrbitCameraSwitcher：{} を写す", cameras_[static_cast<size_t>(index)].name));
}

// ===== Label の文字 =====
std::string OrbitCameraSwitcher::MakeLabel() const {
	std::string label;
	for (size_t i = 0; i < cameras_.size(); ++i) {
		label += std::format("{} [{}] {}\n", (static_cast<int>(i) == selected_) ? ">" : " ", i + 1, cameras_[i].name);
	}
	if (selected_ < 0) {
		return label;
	}
	const GameObject& camera = cameras_[static_cast<size_t>(selected_)];
	if (const CameraOrbitComponent* orbit = camera.GetComponent<CameraOrbitComponent>()) {
		label += std::format("Heading {}  Yaw {:.0f}  Pitch {:.0f}  Distance {:.1f}\n", magic_enum::enum_name(orbit->heading), orbit->yaw, orbit->pitch, orbit->distance);
		const CameraLookAtComponent* lookAt = camera.GetComponent<CameraLookAtComponent>();
		label += (lookAt != nullptr && lookAt->enabled) ? "Camera Look At あり（写す向きは見る点へ）" : "Camera Look At なし（回る中心を見る）";
	}
	return label;
}

// ===== Inspector に出す・保存する値 =====
void OrbitCameraSwitcher::Fields(ComponentUI& ui) {
	ui.ListField("Cameras###cameras", cameras_, [](ComponentUI& ui, GameObject& camera) { ui.Field("Camera###camera", camera, Tip("選べるカメラ（Camera Component を付けた Entity）")); }, Tip("選べるカメラ。上から 1・2・3…のキーで選ぶ"));
	ui.Field("Label###label", label_, Tip("カメラの一覧を出す Text の Entity（空なら出さない）"));
	ui.Field("Active Priority###activePriority", activePriority_, Tip("選んだカメラの Priority（ほかのカメラは 0 にする）"));
}


//=============================================================================
// OrbitYawInput
//=============================================================================
// ===== 更新 =====
void OrbitYawInput::Update() {
	CameraOrbitComponent* orbit = GetComponent<CameraOrbitComponent>();
	const float deltaTime = Time::GetDeltaTime();
	if (orbit == nullptr || deltaTime <= 0.0f) {
		return;
	}
	// --- 入力（キーは -1・0・1、スティックは -1〜1。両方を足して -1〜1 に収める）---
	const float keyYaw = (InputManager::IsKeyPressed(DIK_E) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_Q) ? 1.0f : 0.0f);
	const float keyPitch = (InputManager::IsKeyPressed(DIK_R) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_F) ? 1.0f : 0.0f);
	const float yawInput = std::clamp(keyYaw + InputManager::GetRightStickX(), -1.0f, 1.0f);
	const float pitchInput = std::clamp(keyPitch - InputManager::GetRightStickY(), -1.0f, 1.0f); // スティックを上へ倒すと見上げる
	// --- Camera Orbit の値を書き換える（回り込みは Heading Smooth Time でなめらかになる）---
	orbit->yaw += yawInput * yawSpeed_ * deltaTime;
	orbit->pitch = std::clamp(orbit->pitch + pitchInput * pitchSpeed_ * deltaTime, minPitch_, maxPitch_);
}

// ===== Inspector に出す・保存する値 =====
void OrbitYawInput::Fields(ComponentUI& ui) {
	ui.Field("Yaw Speed###yawSpeed", yawSpeed_, AtLeast(0.0f).Tip("横に回す速さ（度/秒）"));
	ui.Field("Pitch Speed###pitchSpeed", pitchSpeed_, AtLeast(0.0f).Tip("縦に回す速さ（度/秒）"));
	ui.Field("Min Pitch###minPitch", minPitch_, Range(-80.0f, 80.0f).Tip("Pitch の下限（度。− で見上げる）"));
	ui.Field("Max Pitch###maxPitch", maxPitch_, Range(-80.0f, 80.0f).Tip("Pitch の上限（度。＋ で見下ろす）"));
}
