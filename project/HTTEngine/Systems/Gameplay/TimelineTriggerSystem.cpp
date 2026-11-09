#include "TimelineTriggerSystem.h"

#include <memory>

#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"
#include "HTTEngine/Systems/Gameplay/TriggerActivatorSystem.h"

namespace {
//=============================================================================
// 再生する
//=============================================================================
// 入ったとき・出たときの再生。Once なら、それぞれ1回だけ（isUsed に、したことを覚える）。timeline が 0 なら何もしない
void React(uint32_t timeline, bool& isUsed, const TimelineTriggerComponent& trigger, Handle<Entity> owner) {
	if (timeline == 0 || (trigger.once && isUsed)) {
		return;
	}
	isUsed = true;
	TimelineSystem::PlayLayer(owner, TimelineLayer::Trigger, {.timeline = timeline, .timeMode = trigger.timeMode});
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeTimelineTrigger(ComponentUI& ui, TimelineTriggerComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと反応しない（再生中の Timeline は最後まで流れる）"));
	ui.Label("範囲は同じ Entity の Collider（Is Trigger にする）。再生中だけ動く");
	ui.Field("Target###target", value.target, Tip("Timeline を再生する Entity（Hierarchy からドラッグ）。トラックの対象は、ここからのパス。空なら自分"));
	// ===== 誰が入ったら =====
	ui.Separator("Source");
	ui.Field(
	    "Source###source", value.source,
	    Tip("誰が入ったら反応するか。FollowTarget：カメラが追っている物（プレイヤー） / Entity：Source Entity に入れた物 / AnyRigidbody：Rigidbody を付けた物なら、どれでも / AnyMover：Rigidbody か Character Controller を付けた物なら、どれでも"));
	// --- 選んだ相手の値だけを見せる（Trigger Activator と同じ）---
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
	// ===== 再生する Timeline =====
	ui.Separator("Timeline");
	ui.AssetField("On Enter###enterTimeline", value.enterTimeline, AssetType::Timeline, Tip("入ったときに再生する Timeline（選ばなければ何もしない）"));
	ui.AssetField(
	    "On Exit###exitTimeline", value.exitTimeline, AssetType::Timeline,
	    Tip("出たときに再生する Timeline（選ばなければ何もしない。入ったときの再生は最後まで流れる）。Tween の From を Current にすると、途中で出てもその値から戻る"));
	ui.Field("Once###once", value.once, Tip("入ったとき・出たときの再生を、それぞれ1回だけにする（一度見せた演出は、もう流さない）"));
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("Game：ポーズ・スロー・ヒットストップで止まる / Real：ゲームの時間が止まっても動く"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void TimelineTriggerSystem::Initialize() {
	// Timeline Trigger の置き場所・保存・Inspector を登録する（見せ方は DescribeTimelineTrigger 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<TimelineTriggerComponent>();
	ComponentSerializer::Register<TimelineTriggerComponent>("Timeline Trigger", &DescribeTimelineTrigger);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TimelineTriggerComponent>>("Timeline Trigger", "Gameplay", &DescribeTimelineTrigger));
}

void TimelineTriggerSystem::Release() {} // 持っている物は無い（実行中の値は、全部 Component の中。再生は TimelineSystem が持つ）

//=============================================================================
// 毎フレーム
//=============================================================================
void TimelineTriggerSystem::Update(bool isGameRunning) {
	if (!isGameRunning) {
		return; // 停止中は何もしない（Stop で、Play を押した瞬間の状態に戻る）
	}
	EntityManager::ForEach<TimelineTriggerComponent>([](Handle<Entity> entity, TimelineTriggerComponent& trigger) {
		// --- 入った・出たを見る（Enabled を外した・Entity が無効の間は見ない）---
		if (!trigger.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const Handle<Entity> target = EntityManager::Find(trigger.target);
		const Handle<Entity> owner = target.IsValid() ? target : entity; // 空なら自分
		const bool isInside = TriggerActivatorSystem::IsSourceInside(entity, trigger.source, trigger.sourceEntity);
		if (isInside && !trigger.isInside) {
			React(trigger.enterTimeline, trigger.isEnterUsed, trigger, owner);
		} else if (!isInside && trigger.isInside) {
			React(trigger.exitTimeline, trigger.isExitUsed, trigger, owner);
		}
		trigger.isInside = isInside;
	});
}