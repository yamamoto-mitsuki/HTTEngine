#include "PlayerController.h"

#include <HTTEngine/Components/Physics/CharacterControllerComponent.h>
#include <HTTEngine/Input/InputManager.h>
#include <HTTEngine/Math/MathUtility.h>

SCRIPT(PlayerController, "Player"); // クラス名が保存の名前なので後から変えない


//=============================================================================
// 初期化・更新
//=============================================================================
// ===== 初期化 =====
void PlayerController::Start() {
	// シーンに置いたときの向きから始める（Inspector の Rotation の Y）
	facingYaw_ = GetEulerAngles(transform).y;
}

// ===== 更新 =====
void PlayerController::Update() {
	const float dt = Time::GetDeltaTime();

	// --- 早期リターン ---
	if (dt <= 0.0f) {
		return; // ポーズ・ヒットストップ中は動かさない
	}
	CharacterControllerComponent* controller = GetComponent<CharacterControllerComponent>();
	if (controller == nullptr) {
		return; // Character Controller が付いていない
	}

	// --- 歩く：WASD・矢印キー・左スティック（-1〜1）を、カメラから見た地面の向きへ直す ---
	// velocity の X・Z は毎フレーム書く（離すと 0 になって止まる）
	const Vector3 move = moveInput_.ToWorld(InputManager::GetMoveAxis());
	controller->velocity.x = move.x * moveSpeed_;
	controller->velocity.z = move.z * moveSpeed_;

	// --- 跳ぶ：地面にいて、押した瞬間だけ ---
	// velocity の Y は跳ぶときだけ書く（あとは重力に任せる）
	const bool isJumpTriggered = InputManager::IsKeyTriggered(DIK_SPACE) || InputManager::IsButtonTriggered(GamepadButton::A);
	if (controller->IsGrounded() && isJumpTriggered) {
		controller->velocity.y = jumpSpeed_;
	}

	// --- 向く：進む向きへ、近い回り方でなめらかに ---
	if (move.x != 0.0f || move.z != 0.0f) {
		const float targetYaw = MathUtility::DirectionToYaw(move);
		const float rate = MathUtility::CalcSmoothRate(dt, turnSmoothTime_);
		facingYaw_ += MathUtility::DeltaAngle(facingYaw_, targetYaw) * rate;
	}
	SetEulerAngles(transform, {0.0f, facingYaw_, 0.0f});
}


//=============================================================================
// Inspector に出す・保存する値
//=============================================================================
void PlayerController::Fields(ComponentUI& ui) {
	ui.Field("Move Speed", moveSpeed_, AtLeast(0.0f).Tip("移動速度（m/秒）"));
	ui.Field("Jump Speed", jumpSpeed_, AtLeast(0.0f).Tip("跳ぶ瞬間の上向きの速さ（m/秒）"));
	ui.Field("Turn Smooth Time", turnSmoothTime_, AtLeast(0.0f).Tip("向きが追いつくまでの時間（秒）"));
}
