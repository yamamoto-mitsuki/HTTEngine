#include <HTTEngine/AI/AttackPart.h>
#include <HTTEngine/Math/MathUtility.h>

//=============================================================================
// 相手の方を向く（テスト用の部品。Runs In を Whole Attack にするか、待ちの部品に置く）
//=============================================================================
// ステップをまたいで動く例。Runs In を Whole Attack にすると、走る・掴む・跳ぶ のどのステップの間も相手を向き続ける

class FaceTarget : public AttackPart {
public:
	// ===== 始まった：今の向きから回し始める =====
	void OnBegin(const AttackPartContext& context) override { yaw_ = GetEulerAngles(context.self.transform).y; }

	// ===== 毎フレーム：相手の方へ、なめらかに回す =====
	void OnUpdate(const AttackPartContext& context) override {
		if (!context.target) {
			return;
		}
		TransformComponent& transform = context.self.transform;
		const Vector3 toTarget = GetWorldPosition(context.target.transform) - GetWorldPosition(transform);
		const float targetYaw = MathUtility::DirectionToYaw(toTarget);
		yaw_ += MathUtility::DeltaAngle(yaw_, targetYaw) * MathUtility::CalcSmoothRate(context.deltaTime, turnSmoothTime_);
		SetEulerAngles(transform, {0.0f, yaw_, 0.0f});
	}

	void Fields(ComponentUI& ui) override { ui.Field("Turn Smooth Time", turnSmoothTime_, AtLeast(0.0f).Tip("向きが追いつくまでの秒（0 ならすぐ向く）")); }

private:
	float turnSmoothTime_ = 0.15f; // 向きが追いつくまでの秒
	float yaw_ = 0.0f;             // 今の向き（度）
};
ATTACK_PART(FaceTarget, "相手の方をなめらかに向く（Runs In を Whole Attack にするか、待ちの部品に置く）");
