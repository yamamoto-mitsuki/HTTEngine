#include <HTTEngine/AI/AttackPart.h>

//=============================================================================
// 相手を掴んで持ち上げる（テスト用のステップの部品）
//=============================================================================
// 分かれ道の例。ステップに入ったときに相手が Range より遠ければ FailStep()（ステップの On Fail の行き先＝空振りのステップへ）
// 掴めたら、ステップの間ずっと相手を頭の上に持ち上げ、ステップが終わったら放す


class GrabTarget : public AttackPart {
public:
	// ===== ステップに入った：届くか調べる =====
	void OnBegin(const AttackPartContext& context) override {
		isHolding_ = false;
		if (!context.target) {
			FailStep();
			return;
		}
		Vector3 toTarget = GetWorldPosition(context.target.transform) - GetWorldPosition(context.self.transform);
		toTarget.y = 0.0f;
		if (Length(toTarget) > range_) {
			FailStep(); // 届かない（効くのは最初の OnUpdate の後）
			return;
		}
		isHolding_ = true;
	}

	// ===== 毎フレーム：掴んでいれば、相手を頭の上へ =====
	void OnUpdate(const AttackPartContext& context) override {
		if (!isHolding_ || !context.target) {
			return;
		}
		context.target.transform.translation = GetWorldPosition(context.self.transform) + Vector3{0.0f, holdHeight_, 0.0f};
	}

	// ===== ステップが終わった（止められたときも）：放す =====
	void OnEnd(const AttackPartContext& /*context*/, AttackPartEnd /*reason*/) override { isHolding_ = false; }

	void Fields(ComponentUI& ui) override {
		ui.Field("Range", range_, AtLeast(0.0f).Tip("掴める距離（m）。ステップに入ったときにこれより遠ければ失敗"));
		ui.Field("Hold Height", holdHeight_, Tip("持ち上げる高さ（自分の足元から m）"));
	}

private:
	float range_ = 2.0f;      // 掴める距離
	float holdHeight_ = 3.0f; // 持ち上げる高さ
	bool isHolding_ = false;  // 掴んでいるか
};
ATTACK_PART(GrabTarget, "近くの相手を掴んで持ち上げる。届かなければ失敗");
