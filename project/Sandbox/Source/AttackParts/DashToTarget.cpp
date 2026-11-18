#include <HTTEngine/AI/AttackPart.h>

//=============================================================================
// 相手へ走る（テスト用のステップの部品）
//=============================================================================
// 秒ではなく「着いたら」で終わるステップの例。Stop Distance まで近づいたら FinishStep() で次のステップへ
// ステップの Seconds までに着けなければ、そのまま次のステップへ（Seconds が長さの上限になる）

class DashToTarget : public AttackPart {
public:
	// 毎フレーム
	void OnUpdate(const AttackPartContext& context) override {
		if (!context.target) {
			FailStep(); // 相手がいない
			return;
		}
		TransformComponent& transform = context.self.transform;
		Vector3 toTarget = GetWorldPosition(context.target.transform) - transform.translation;
		toTarget.y = 0.0f; // 地面に平らに進む
		const float distance = Length(toTarget);
		if (distance <= stopDistance_) {
			FinishStep(); // 着いた
			return;
		}
		const float move = (std::min)(speed_ * context.deltaTime, distance - stopDistance_); // 行き過ぎない
		transform.translation += toTarget * (move / distance);
	}

	void Fields(ComponentUI& ui) override {
		ui.Field("Speed", speed_, AtLeast(0.0f).Tip("走る速さ（m/秒）"));
		ui.Field("Stop Distance", stopDistance_, AtLeast(0.0f).Tip("相手までこの距離（m）に近づいたら、ステップを終える"));
	}

private:
	float speed_ = 10.0f;       // 走る速さ
	float stopDistance_ = 1.5f; // 着いたとみなす距離
};
ATTACK_PART(DashToTarget, "相手へ走る。近づいたらステップを終える（相手が居なければ失敗）");
