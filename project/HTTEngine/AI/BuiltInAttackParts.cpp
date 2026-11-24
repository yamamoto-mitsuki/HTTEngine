#include "BuiltInAttackParts.h"

#include <format>
#include <string>
#include <optional>

#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/Components/Animation/AnimatorComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"

namespace {
//=============================================================================
// HitboxWindow（ステップの間だけ、子のHitboxの窓を開ける）
//=============================================================================
// ===== 空ける窓の種類 =====
enum class HitboxWindowKind {
	Warning, // 予告
	Active,  // 当たる
};
// 選択肢ごとの説明
constexpr auto EnumTips(HitboxWindowKind) {
	using enum HitboxWindowKind;
	return MakeEnumTips<HitboxWindowKind>({
	    {Warning, "予告（Hitbox Telegraphがあれば床に形を塗る）"             },
	    {Active,  "当たる"},
	});
}

class HitboxWindow : public AttackPart {
public:
	// ステップに入った: 窓を空ける
	void OnBegin(const AttackPartContext& context) override { 
		hitbox_ = context.self.FindChild(child_);
		if (!hitbox_) {
			LogManager::Warning(std::format("HitboxWindow：子に \"{}\" がありません（ステップ \"{}\"）", child_, context.stepName));
			return;
		}
		switch (kind_) {
		case HitboxWindowKind::Warning:
			HitSystem::Warn(hitbox_.handle, context.stepDuration);
			break;
		case HitboxWindowKind::Active:
			HitSystem::Activate(hitbox_.handle, context.stepDuration);
			break;
		}
	}

	// ステップが終わった
	void OnEnd(const AttackPartContext& /*context*/, AttackPartEnd /*reason*/) override {
		if (hitbox_) {
			HitSystem::Deactivate(hitbox_.handle);
		}
		hitbox_ = GameObject{};
	}

	void Fields(ComponentUI& ui) override { 
		ui.Field("Child", child_, Tip("窓を開けるHitboxのEntityの名前"));
		ui.Field("Window", kind_, Tip("開ける窓の種類。予告のステップと当たるステップに1つずつ置くと、予告 → 当たり の順になる"));
	}

private:
	// ===== 攻撃表で変える値 =====
	std::string child_ = "Hitbox";                     // HitboxのEntity名
	HitboxWindowKind kind_ = HitboxWindowKind::Active; // 空ける窓の種類

	// ===== 実行中だけの値 =====
	GameObject hitbox_;
};

//=============================================================================
// PlayAnimation（ステップに入ったら、アニメーションを名前で流す）
//=============================================================================
class PlayAnimation : public AttackPart {
public:
	// ステップに入った
	void OnBegin(const AttackPartContext& context) override {
		if (clip_.Empty()) {
			return;
		}
		const Handle<Entity> self = context.self.handle;
		const std::string_view clip = clip_.View();
		const float speed = fitToStep_ ? AnimationSystem::CalcSpeedToFit(self, clip, context.stepDuration) : speed_;
		// 上書きしないなら空（Animator の Blend Time を使う）
		const std::optional<float> blendTime = overrideBlendTime_ ? std::optional<float>(blendTime_) : std::nullopt;
		AnimationSystem::Play(self, clip, loop_, speed, blendTime);
		// ループしないアニメは、前のステップ・前の攻撃と同じ名前でも頭から流す（Play は同じ名前が流れていると、速さしか変えない）
		// 同じフレームに Play と2回切り替えても、混ぜる元は前のステップのアニメのまま（AnimationSystem が見分ける）
		if (!loop_) {
			AnimationSystem::Restart(self, blendTime);
		}
	}

	void Fields(ComponentUI& ui) override {
		ui.ClipField("Clip", clip_, Tip("流すアニメーションの名前"));
		ui.Field("Loop", loop_, Tip("ループする"));
		ui.Field("Fit To Step", fitToStep_, Tip("ステップの秒数でちょうど1回流しきる速さにする"));
		ui.BeginDisabled(fitToStep_);
		ui.Field("Speed", speed_, AtLeast(0.0f).Tip("流す速さ（1で等速）"));
		ui.EndDisabled();
		ui.Field("Override Blend Time", overrideBlendTime_, Tip("オンなら下の Blend Time で、前のアニメの形から混ぜる。オフなら Animator の Blend Time"));
		ui.BeginDisabled(!overrideBlendTime_);
		ui.Field("Blend Time", blendTime_, AtLeast(0.0f).Drag(0.01f).Tip("前のアニメの形から混ぜていく秒（0 ならすぐ切り替える）。ステップの秒の中に入る"));
		ui.EndDisabled();
	}


private:
	// ===== 攻撃表で変える値 =====
	AnimationClipName clip_;         // 流すアニメーションの名前
	bool loop_ = false;              // ループするか
	bool fitToStep_ = true;          // ステップの長さに合わせるか
	float speed_ = 1.0f;             // 合わせないときの速さ
	bool overrideBlendTime_ = false; // 混ぜる秒を、Animator の Blend Time ではなく blendTime_ にするか
	float blendTime_ = 0.1f;         // 上書きするときの、混ぜる秒
};
} // namespace


//=============================================================================
// 登録
//=============================================================================
void BuiltInAttackParts::Register() {
	AttackPartRegistry::Register("HitboxWindow", &AttackPartRegistrar<HitboxWindow>::Create, "ステップの間だけ、子の Hitbox の窓を開ける（予告か当たり。ステップが終わったら閉じる）");
	AttackPartRegistry::Register("PlayAnimation", &AttackPartRegistrar<PlayAnimation>::Create, "ステップに入ったら、アニメーションを名前で流す（ステップの長さに合わせられる）");
}