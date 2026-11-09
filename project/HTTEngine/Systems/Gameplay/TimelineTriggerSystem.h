#pragma once
#include "HTTEngine/Components/Gameplay/TimelineTriggerComponent.h"

/// <summary>
/// Timeline Trigger を登録し、範囲に入った・出たを見て、Timeline の再生を頼む
/// <para>入ったかどうかは Trigger Activator と同じ（TriggerActivatorSystem::IsSourceInside）。再生は TimelineLayer::Trigger なので、入る・出るをくり返すと前の再生は置き換わる</para>
/// <para>出たときの Timeline の Tween を From = Current にしておけば、入ったときの動きの途中で出ても、その値から戻る</para>
/// </summary>
class TimelineTriggerSystem {
public:
	// TimelineTriggerComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 入った・出たを見て、Timeline の再生を頼む（再生中だけ。停止中は何もしない）
	/// <para>CollisionSystem::Update の後に呼ぶ（そのフレームの当たりで決める。値が動くのは、次のフレームの TimelineSystem::Update から）</para>
	/// </summary>
	/// <param name="isGameRunning">再生中か</param>
	static void Update(bool isGameRunning);
};