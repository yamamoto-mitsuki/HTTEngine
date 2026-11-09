#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/Components/Physics/CharacterControllerComponent.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"


/// <summary>
/// 相手の方を向き、遠ければ Character Controller で近づき、近ければ止まる（Sandbox のテスト用。攻撃表のフェーズの Idle Part に選ぶ）
/// <para>同じ Entity に Character Controller と Animator を付ける。アニメは歩いている間は等速、止まっている間はゆっくり流す</para>
/// </summary>
class ChaseTarget : public AttackPart {
public:
	void OnUpdate(const AttackPartContext& context) override;
	void OnEnd(const AttackPartContext& context, AttackPartEnd reason) override;
	void Fields(ComponentUI& ui) override;

private:
	float moveSpeed_ = 3.0f;       // 近づく速さ（m/s）
	float stopDistance_ = 2.5f;    // 相手とこの距離より近ければ止まる（m。地面に平らに測る）
	float turnSmoothTime_ = 0.15f; // 向きを合わせる速さ（秒。小さいほど速く向く）
	AnimationClipName clip_;       // 流すアニメの名前（攻撃表の Sample Animation の中の名前から選ぶ）
	float stopAnimSpeed_ = 0.25f;  // 止まっている間のアニメの速さ
};
ATTACK_PART(ChaseTarget, "相手の方を向き、遠ければ Character Controller で近づく（Sandbox のテスト用。Idle Part に選ぶ）");


//=============================================================================
// 毎フレーム
//=============================================================================
void ChaseTarget::OnUpdate(const AttackPartContext& context) {
	CharacterControllerComponent* controller = context.self.GetComponent<CharacterControllerComponent>();
	if (controller == nullptr || !context.target) {
		return; // Character Controller が無い・相手が居ない
	}

	// --- 相手への向き（地面に平ら）---
	TransformComponent& transform = context.self.transform;
	Vector3 toTarget = GetWorldPosition(context.target.transform) - GetWorldPosition(transform);
	toTarget.y = 0.0f;
	const float distance = Length(toTarget);

	// --- 向く：今の角度から一番近い回り方で、なめらかに（フレームの速さに左右されない）---
	if (distance > 0.0f) {
		const float yaw = GetEulerAngles(transform).y;
		const float targetYaw = MathUtility::DirectionToYaw(toTarget);
		const float rate = MathUtility::CalcSmoothRate(context.deltaTime, turnSmoothTime_);
		SetEulerAngles(transform, {0.0f, yaw + MathUtility::DeltaAngle(yaw, targetYaw) * rate, 0.0f});
	}

	// --- 近づく・止まる（X・Z だけ書く。Y は Character Controller の重力に任せる）---
	const bool isMoving = (distance > stopDistance_);
	const Vector3 velocity = isMoving ? Normalize(toTarget) * moveSpeed_ : Vector3{0.0f, 0.0f, 0.0f};
	controller->velocity.x = velocity.x;
	controller->velocity.z = velocity.z;

	// --- アニメ：同じ名前を毎フレーム渡しても、最初には戻らない（速さだけ変わる）---
	AnimationSystem::Play(context.self.handle, clip_.View(), true, isMoving ? 1.0f : stopAnimSpeed_);
}

//=============================================================================
// 終わり
//=============================================================================
// 攻撃を選んだときは、その攻撃の OnBegin より先に呼ばれる。ここで速さを消すので、攻撃が始まったフレームに歩きの速さが残らない
void ChaseTarget::OnEnd(const AttackPartContext& context, AttackPartEnd /*reason*/) {
	if (CharacterControllerComponent* controller = context.self.GetComponent<CharacterControllerComponent>()) {
		controller->velocity.x = 0.0f;
		controller->velocity.z = 0.0f;
	}
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void ChaseTarget::Fields(ComponentUI& ui) {
	ui.Field("Move Speed###moveSpeed", moveSpeed_, AtLeast(0.0f).Tip("近づく速さ（m/s）"));
	ui.Field("Stop Distance###stopDistance", stopDistance_, AtLeast(0.0f).Tip("相手とこの距離より近ければ止まる（m。地面に平らに測る）"));
	ui.Field("Turn Smooth Time###turnSmoothTime", turnSmoothTime_, AtLeast(0.0f).Tip("向きを合わせる速さ（秒。小さいほど速く向く。0 ならすぐ向く）"));
	ui.ClipField("Clip###clip", clip_, Tip("流すアニメの名前（敵の Animator のファイルに、同じ名前のアニメーションが要る）"));
	ui.Field("Stop Anim Speed###stopAnimSpeed", stopAnimSpeed_, AtLeast(0.0f).Tip("止まっている間のアニメの速さ（歩いている間は 1）"));
}
