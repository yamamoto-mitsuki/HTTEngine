#include "MyEngine/AI/AttackPart.h"
#include "MyEngine/Systems/Animation/AnimationSystem.h"


/// <summary>
/// 予備動作の間に、アニメをちょうど1回流しきる（AnimationSystem::CalcSpeedToFit の確かめ。Sandbox のテスト用）
/// <para>同じ Entity に Animator を付ける。動きは付けない（攻撃の間、敵が止まっているかを見る）</para>
/// </summary>
class SpinAttack : public AttackPart {
public:
	void OnStage(const AttackPartContext& context) override;
	void Fields(ComponentUI& ui) override;

private:
	int clipIndex_ = 0; // 流すアニメの番号
};
ATTACK_PART(SpinAttack, "予備動作の間に、アニメをちょうど1回流しきる（Sandbox のテスト用）");


//=============================================================================
// 段階に入った
//=============================================================================
void SpinAttack::OnStage(const AttackPartContext& context) {
	if (context.stage != AttackStage::Windup) {
		return;
	}
	// 予備動作の長さで、ちょうど1回（ループしない）
	const Handle<Entity> self = context.self.handle;
	AnimationSystem::Play(self, clipIndex_, false, AnimationSystem::CalcSpeedToFit(self, clipIndex_, context.stageDuration));
	// 待ちの部品が同じ番号のアニメを流していると、Play は速さと loop だけ変える（最初には戻らない）。最初から流したいので Restart
	AnimationSystem::Restart(self);
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void SpinAttack::Fields(ComponentUI& ui) { ui.Field("Clip Index###clipIndex", clipIndex_, AtLeast(0.0f).Tip("流すアニメの番号（Animator の Clip と同じ番号）")); }
