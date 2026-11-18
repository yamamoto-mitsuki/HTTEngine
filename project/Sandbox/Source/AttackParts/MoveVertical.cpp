#include <HTTEngine/AI/AttackPart.h>

//=============================================================================
// 上下に動く（テスト用のステップの部品）
//=============================================================================
// 跳ぶ・浮く・落ちる のステップに使う。下へ動いて Ground Height に着いたら FinishStep()（「着地まで」のステップ）


class MoveVertical : public AttackPart {
public:
	// ===== 毎フレーム：上下に動かす =====
	void OnUpdate(const AttackPartContext& context) override {
		Vector3& position = context.self.transform.translation;
		position.y += speed_ * context.deltaTime;
		// 下へ動いていて、床に着いた
		if (speed_ < 0.0f && position.y <= groundHeight_) {
			position.y = groundHeight_;
			FinishStep();
		}
	}

	void Fields(ComponentUI& ui) override {
		ui.Field("Speed", speed_, Tip("上下の速さ（m/秒。上が +、下が -）"));
		ui.Field("Ground Height", groundHeight_, Tip("下へ動くとき、この高さ（m）に着いたら止まってステップを終える"));
	}

private:
	float speed_ = 8.0f;        // 上下の速さ
	float groundHeight_ = 0.0f; // 床の高さ
};
ATTACK_PART(MoveVertical, "上下に動く。下へ動いて床の高さに着いたらステップを終える");
