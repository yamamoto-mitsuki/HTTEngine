#include "CameraRelativeInput.h"

#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"


//=============================================================================
// 入力 → ワールドの向き
//=============================================================================
// ===== 2D の入力をワールドの地面の向きへ =====
Vector3 CameraRelativeInput::ToWorld(const Vector2& input) {
	const Handle<Entity> camera = CameraSystem::GetMainCameraEntity();
	const bool isPressed = (input.x != 0.0f || input.y != 0.0f);

	// --- どの向きを使うか ---
	// 離したら、使い続けるのをやめる（次に押したときは、今のカメラの向き）
	// 押し続けている間にカメラが変わったら、前に使った向き（切り替わる前のカメラの向き）を使い続ける
	if (!isPressed) {
		isHolding_ = false;
	} else if (wasPressed_ && camera != camera_) {
		isHolding_ = true;
	}
	if (!isHolding_) {
		yaw_ = CameraSystem::GetMainCameraYaw();
	}
	camera_ = camera;
	wasPressed_ = isPressed;

	// --- 奥の向き × 上 ＋ 右の向き × 右（右の向きは、奥の向きを上から見て時計回りに 90° 回した向き）---
	const Vector3 forward = MathUtility::YawToDirection(yaw_);
	const Vector3 right = MathUtility::YawToDirection(yaw_ + 90.0f);
	return {right.x * input.x + forward.x * input.y, 0.0f, right.z * input.x + forward.z * input.y};
}

// ===== 忘れる =====
void CameraRelativeInput::Reset() {
	camera_ = {};
	yaw_ = 0.0f;
	wasPressed_ = false;
	isHolding_ = false;
}
