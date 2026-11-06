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
	AnimationClipName clip_; // 流すアニメの名前（攻撃表の Sample Animation の中の名前から選ぶ）
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
	AnimationSystem::Play(self, clip_.View(), false, AnimationSystem::CalcSpeedToFit(self, clip_.View(), context.stageDuration));
	// 待ちの部品が同じ名前のアニメを流していると、Play は速さと loop だけ変える（最初には戻らない）。最初から流したいので Restart
	AnimationSystem::Restart(self);
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void SpinAttack::Fields(ComponentUI& ui) { ui.ClipField("Clip###clip", clip_, Tip("流すアニメの名前（敵の Animator のファイルに、同じ名前のアニメーションが要る）")); }
