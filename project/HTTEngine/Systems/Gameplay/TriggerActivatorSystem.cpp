#include "TriggerActivatorSystem.h"

#include <algorithm>
#include <memory>

#include "HTTEngine/Components/UI/AlphaGroupComponent.h"
#include "HTTEngine/Components/Physics/RigidbodyComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Physics/MotionQuery.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"

namespace {
//=============================================================================
// 誰が入ったか
//=============================================================================
// other が who 自身か、who の子孫か（プレイヤーの子に付けた Collider も、プレイヤーとして数える）
bool IsSelfOrDescendant(Handle<Entity> other, Handle<Entity> who) { return who.IsValid() && (other == who || EntityManager::IsDescendantOf(other, who)); }

// other が、反応する相手か
bool IsSource(TriggerSource source, EntityRef sourceEntity, Handle<Entity> other) {
	switch (source) {
	case TriggerSource::FollowTarget:
		return IsSelfOrDescendant(other, CameraSystem::GetFollowTargetEntity());
	case TriggerSource::Entity:
		return IsSelfOrDescendant(other, EntityManager::Find(sourceEntity));
	case TriggerSource::AnyRigidbody:
		return EntityManager::Get<RigidbodyComponent>(other) != nullptr;
	case TriggerSource::AnyMover:
		return MotionQuery::Has(other); // どの動き方かは知らずに、窓口に聞く
	}
	return false;
}

//=============================================================================
// 出す・消す
//=============================================================================
// フェードを使うか（使うときだけ、Target の Alpha Group を付ける・書く）
bool UsesFade(const TriggerActivatorComponent& activator) { return activator.fadeInTime > 0.0f || activator.fadeOutTime > 0.0f; }

// 再生を始めた最初のフレームに1回：Hide On Start と、フェードに使う Alpha Group の用意
void Start(TriggerActivatorComponent& activator, Handle<Entity> targetHandle, Entity& target) {
	activator.isStarted = true;
	// --- Alpha Group：あれば今の Alpha を覚える（そこまで出す）。フェードするのに無ければ付ける（付くのは次のフレームの頭。Stop で外れる）---
	if (const AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(targetHandle)) {
		activator.baseAlpha = group->alpha;
	} else if (UsesFade(activator) && !EntityManager::IsAddPending<AlphaGroupComponent>(targetHandle)) {
		EntityManager::RequestAdd<AlphaGroupComponent>(targetHandle); // 同じ Target を指す Trigger Activator が2つあっても、1つだけ付ける
	}
	if (activator.hideOnStart) {
		target.isActive = false;
	}
	activator.fadeAlpha = target.isActive ? 1.0f : 0.0f;
}

// 出す：有効にして、Fade In Time でだんだん見せる（消している途中なら、今の濃さから戻す）
void Show(TriggerActivatorComponent& activator, Entity& target) {
	if (!target.isActive) {
		target.isActive = true;
		activator.fadeAlpha = 0.0f; // 消えていた所から
	}
	if (activator.fadeInTime > 0.0f) {
		activator.fade = TriggerFade::FadingIn;
	} else {
		activator.fadeAlpha = 1.0f;
		activator.fade = TriggerFade::None;
	}
}

// 消す：Fade Out Time でだんだん消して、消えきったら無効にする（StepFade）。0 秒なら、すぐ無効にする
void Hide(TriggerActivatorComponent& activator, Entity& target) {
	if (activator.fadeOutTime > 0.0f && target.isActive) {
		activator.fade = TriggerFade::FadingOut;
		return;
	}
	target.isActive = false;
	activator.fadeAlpha = 0.0f;
	activator.fade = TriggerFade::None;
}

// 入ったとき・出たときの動き。Once なら、それぞれ1回だけ（isUsed に、したことを覚える）
void React(TriggerAction action, bool& isUsed, TriggerActivatorComponent& activator, Entity& target) {
	if (activator.once && isUsed) {
		return;
	}
	isUsed = true;
	switch (action) {
	case TriggerAction::None:
		break;
	case TriggerAction::Show:
		Show(activator, target);
		break;
	case TriggerAction::Hide:
		Hide(activator, target);
		break;
	}
}

// 1フレームに変える濃さ（濃さ 1 を time 秒で変えきる速さ。time が 0 なら、すぐ変えきる）
// フェードの途中で Inspector の秒数を 0 にされても、0 で割らないように
float CalcFadeStep(float time, float deltaTime) { return (time > 0.0f) ? deltaTime / time : 1.0f; }

// フェードを1フレーム進める（消えきったら Target を無効にする）
void StepFade(TriggerActivatorComponent& activator, Entity& target, float deltaTime) {
	switch (activator.fade) {
	case TriggerFade::None:
		break;
	case TriggerFade::FadingIn:
		activator.fadeAlpha = MathUtility::MoveTowards(activator.fadeAlpha, 1.0f, CalcFadeStep(activator.fadeInTime, deltaTime));
		if (activator.fadeAlpha >= 1.0f) {
			activator.fade = TriggerFade::None;
		}
		break;
	case TriggerFade::FadingOut:
		activator.fadeAlpha = MathUtility::MoveTowards(activator.fadeAlpha, 0.0f, CalcFadeStep(activator.fadeOutTime, deltaTime));
		if (activator.fadeAlpha <= 0.0f) {
			target.isActive = false;
			activator.fade = TriggerFade::None;
		}
		break;
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeTriggerActivator(ComponentUI& ui, TriggerActivatorComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと反応しない（フェードの途中なら、最後まで続ける）"));
	ui.Label("範囲は同じ Entity の Collider（Is Trigger にする）。再生中だけ動く");
	ui.Field("Target###target", value.target, Tip("出す・消す Entity（Hierarchy からドラッグ）。文字と背景をまとめた親を入れると、子も一緒に出る・消える。自分と自分の親は選ばない"));
	// ===== 誰が入ったら =====
	ui.Separator("Source");
	ui.Field(
	    "Source###source", value.source,
	    Tip("誰が入ったら反応するか。FollowTarget：カメラが追っている物（プレイヤー） / Entity：Source Entity に入れた物 / AnyRigidbody：Rigidbody を付けた物なら、どれでも / AnyMover：Rigidbody か Character Controller を付けた物なら、どれでも"));
	// --- 選んだ相手の値だけを見せる（保存も、使っている方だけが書かれる。Camera Follow の Mode と同じ）---
	switch (value.source) {
	case TriggerSource::FollowTarget:
		break; // 選ぶ物は無い（メインカメラの Camera Follow の Target を使う）
	case TriggerSource::Entity:
		ui.Field("Source Entity###sourceEntity", value.sourceEntity, Tip("反応する相手（Hierarchy からドラッグ）。子に付けた Collider も、この Entity として数える"));
		break;
	case TriggerSource::AnyRigidbody:
	case TriggerSource::AnyMover:
		break; // 選ぶ物は無い
	}
	// ===== どうするか =====
	ui.Separator("Action");
	ui.Field("On Enter###onEnter", value.onEnter, Tip("入ったとき。Show：出す / Hide：消す / None：何もしない"));
	ui.Field("On Exit###onExit", value.onExit, Tip("出たとき。Show：出す / Hide：消す / None：何もしない（出したままにする）"));
	ui.Field("Once###once", value.once, Tip("入ったとき・出たときの動きを、それぞれ1回だけにする（一度読んだ説明は、もう出さない）"));
	ui.Field("Hide On Start###hideOnStart", value.hideOnStart, Tip("再生を始めたら、すぐ Target を消す（エディタでは見えたまま置ける）。最初から出しておいて、出たら消すなら外す"));
	// ===== フェード =====
	ui.Separator("Fade");
	ui.Field("Fade In Time###fadeInTime", value.fadeInTime, AtLeast(0.0f).Tip("出すときに、見えきるまでの秒数（0 ならすぐ）"));
	ui.Field("Fade Out Time###fadeOutTime", value.fadeOutTime, AtLeast(0.0f).Tip("消すときに、消えきるまでの秒数（0 ならすぐ）"));
	ui.Label("フェードは Target の Alpha Group の Alpha を動かす（無ければ再生中だけ付く）。効くのは Image・Text・Sprite Renderer");
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void TriggerActivatorSystem::Initialize() {
	// Trigger Activator の置き場所・保存・Inspector を登録する（見せ方は DescribeTriggerActivator 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<TriggerActivatorComponent>();
	ComponentSerializer::Register<TriggerActivatorComponent>("Trigger Activator", &DescribeTriggerActivator);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TriggerActivatorComponent>>("Trigger Activator", "Gameplay", &DescribeTriggerActivator));
}

void TriggerActivatorSystem::Release() {} // 持っている物は無い（実行中の値は、全部 Component の中）

//=============================================================================
// 範囲に入ったか
//=============================================================================
bool TriggerActivatorSystem::IsSourceInside(Handle<Entity> entity, TriggerSource source, EntityRef sourceEntity) {
	// Exit の知らせを待たずに毎フレーム数え直すので、中にいた相手が消えても（Exit が来なくても）、次のフレームには「出た」になる
	return std::ranges::any_of(CollisionSystem::GetHits(entity), [&](const CollisionHit& hit) { return hit.phase != HitPhase::Exit && IsSource(source, sourceEntity, hit.other); });
}

//=============================================================================
// 毎フレーム
//=============================================================================
void TriggerActivatorSystem::Update(bool isGameRunning, float deltaTime) {
	if (!isGameRunning) {
		return; // 停止中は何もしない（Target を消したままにしない。Stop で、Play を押した瞬間の状態に戻る）
	}
	EntityManager::ForEach<TriggerActivatorComponent>([deltaTime](Handle<Entity> entity, TriggerActivatorComponent& activator) {
		const Handle<Entity> targetHandle = EntityManager::Find(activator.target);
		Entity* target = EntityManager::Get(targetHandle);
		if (target == nullptr || IsSelfOrDescendant(entity, targetHandle)) {
			return; // Target が無い・自分か親（消すと、範囲の Collider も一緒に消えて、もう入れない）
		}
		if (!activator.isStarted) {
			Start(activator, targetHandle, *target);
		}
		AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(targetHandle);
		if (UsesFade(activator) && group == nullptr) {
			return; // Start で付けた Alpha Group が、まだ付いていない（付く前に出すと、1フレームだけ濃く見える。次のフレームから見る）
		}
		// --- 入った・出たを見る（Enabled を外した・Entity が無効の間は見ない。フェードは続ける）---
		if (activator.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			const bool isInside = IsSourceInside(entity, activator.source, activator.sourceEntity);
			if (isInside && !activator.isInside) {
				React(activator.onEnter, activator.isEnterUsed, activator, *target);
			} else if (!isInside && activator.isInside) {
				React(activator.onExit, activator.isExitUsed, activator, *target);
			}
			activator.isInside = isInside;
		}
		// --- フェードを進めて、Target の Alpha Group へ書く（フェードを使わないなら書かない。Script が動かす Alpha を邪魔しない）---
		StepFade(activator, *target, deltaTime);
		if (UsesFade(activator)) {
			group->alpha = activator.baseAlpha * activator.fadeAlpha;
		}
	});
}