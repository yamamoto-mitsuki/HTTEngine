#pragma once
#include <string>

#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// イベントのクリップ（長さは無い。start の時間ちょうどの1点）
/// </summary>
struct TimelineEventClip : TimelineClip {
	std::string name;  // イベントの名前
	std::string value; // イベントに付ける文字

	void Fields(ComponentUI& ui);
};

/// <summary>
/// 指定の時間に、対象の Script の OnTimelineEvent と TimelineSystem::AddEventListener の関数を呼ぶトラック
/// <para>呼ばれるのは、時間がそのイベントを通ったとき（通った時間の幅の決まりは TimelinePlayhead）。シークでは呼ばない</para>
/// </summary>
class TimelineEventTrack : public TimelineTrackOf<TimelineEventClip> {
public:
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
};