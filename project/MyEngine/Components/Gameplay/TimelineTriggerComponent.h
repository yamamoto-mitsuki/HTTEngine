#pragma once
#include <cstdint>
#include <type_traits>

#include "MyEngine/Components/Gameplay/TriggerActivatorComponent.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Time/EngineTime.h"

// 同じ Entity の Collider の範囲に入ったら・出たら、Timeline を再生する。Entityに付けるComponent（カテゴリ：Gameplay）
// 範囲・誰が入ったら反応するかは Trigger Activator と同じ（Collider は Is Trigger にする）。Script を書かずに、エディタだけで演出を置ける
// 再生は TimelineSystem::PlayLayer（TimelineLayer::Trigger）。Target を再生する Entity にするので、トラックの対象は Target からのパス
// 動かすのは TimelineTriggerSystem（再生中だけ）。停止中は何もしない

/// <summary>
/// 範囲に入ったら・出たら、Timeline を再生する（Unity なら、OnTriggerEnter で PlayableDirector.Play を呼ぶ Script を書く所）
/// </summary>
struct TimelineTriggerComponent {
	bool enabled = true;                                // false の間は反応しない（再生中の Timeline は最後まで流れる）
	EntityRef target;                                   // 再生する Entity（Hierarchy からドラッグ。空なら自分）
	TriggerSource source = TriggerSource::FollowTarget; // 誰が入ったら反応するか
	EntityRef sourceEntity;                             // source が Entity のとき：反応する相手
	uint32_t enterTimeline = 0;                         // 入ったときに再生する .timeline（0 なら何もしない）
	uint32_t exitTimeline = 0;                          // 出たときに再生する .timeline（0 なら何もしない。入ったときの再生は最後まで流れる）
	bool once = false;                                  // true：入ったとき・出たときの再生を、それぞれ1回だけにする
	TimeMode timeMode = TimeMode::Game;                 // 再生に使う時間（Real：ポーズ・スローでも動く）

	// --- 実行中だけの値（TimelineTriggerSystem が書く。保存しない）---
	bool isInside = false;    // 前のフレームに、Source が範囲の中にいたか（今と比べて、入った・出たを決める）
	bool isEnterUsed = false; // 入ったときの再生をしたか（Once のとき、2回目をしないため）
	bool isExitUsed = false;  // 出たときの再生をしたか（同じ）
};

static_assert(std::is_trivially_copyable_v<TimelineTriggerComponent>);