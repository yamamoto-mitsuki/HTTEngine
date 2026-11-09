#include "Source/Scripts/CharacterKeyboardMover.h"

#include <format>

#include "HTTEngine/Components/Physics/CharacterControllerComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/InputManager.h"

SCRIPT(CharacterKeyboardMover, "Test");


//=============================================================================
// 毎フレーム
//=============================================================================
void CharacterKeyboardMover::Update() {
	CharacterControllerComponent* controller = GetComponent<CharacterControllerComponent>();
	if (controller == nullptr) {
		return; // Character Controller を付けていない
	}

	// --- 歩く向き（キーを押している向きを足して、長さ1にする）---
	Vector3 input = {0.0f, 0.0f, 0.0f};
	if (InputManager::IsKeyPressed(DIK_W)) {
		input.z += 1.0f;
	}
	if (InputManager::IsKeyPressed(DIK_S)) {
		input.z -= 1.0f;
	}
	if (InputManager::IsKeyPressed(DIK_D)) {
		input.x += 1.0f;
	}
	if (InputManager::IsKeyPressed(DIK_A)) {
		input.x -= 1.0f;
	}
	if (LengthSq(input) > 0.0f) {
		input = Normalize(input);
		facing_ = input;
	}

	// --- ダッシュ（押した瞬間から Dash Time 秒、向きは押した瞬間の向き）---
	if (InputManager::IsKeyTriggered(DIK_LSHIFT)) {
		dashRemaining_ = dashTime_;
	}
	Vector3 horizontal = input * moveSpeed_;
	if (dashRemaining_ > 0.0f) {
		horizontal = facing_ * dashSpeed_;
		dashRemaining_ -= Time::GetDeltaTime();
	}

	// --- 速さを書く。X・Z は毎フレーム書き、Y は Character Controller に任せる（跳ぶときだけ書く）---
	controller->velocity.x = horizontal.x;
	controller->velocity.z = horizontal.z;
	if (controller->IsGrounded() && InputManager::IsKeyTriggered(DIK_SPACE)) {
		controller->velocity.y = jumpSpeed_;
	}
}

//=============================================================================
// 当たりの知らせ（Rigidbody と同じように来るかを見る）
//=============================================================================
void CharacterKeyboardMover::OnCollisionEnter(const CollisionInfo& collision) {
	LogManager::Log(std::format("Enter {} 向き({:.2f}, {:.2f}, {:.2f}) 強さ {:.2f}", collision.gameObject.GetName(), collision.normal.x, collision.normal.y, collision.normal.z, collision.GetImpactSpeed()));
}

void CharacterKeyboardMover::OnCollisionExit(const CollisionInfo& collision) {
	LogManager::Log(std::format("Exit {}", collision.gameObject ? collision.gameObject.GetName() : std::string("(消えた)")));
}

//=============================================================================
// Inspector と保存
//=============================================================================
void CharacterKeyboardMover::Fields(ComponentUI& ui) {
	ui.Field("Move Speed###moveSpeed", moveSpeed_, AtLeast(0.0f).Tip("歩く速さ（m/s）"));
	ui.Field("Jump Speed###jumpSpeed", jumpSpeed_, AtLeast(0.0f).Tip("跳んだ瞬間の上向きの速さ（m/s）"));
	ui.Field("Dash Speed###dashSpeed", dashSpeed_, AtLeast(0.0f).Tip("ダッシュの速さ（m/s）。速く動いても壁をすり抜けないかを見る"));
	ui.Field("Dash Time###dashTime", dashTime_, AtLeast(0.0f).Tip("ダッシュの長さ（秒）"));
}
