#include "TimelineEventTrack.h"

#include "HTTEngine/ECS/ComponentUI.h"

//=============================================================================
// クリップ
//=============================================================================
void TimelineEventClip::Fields(ComponentUI& ui) {
	ui.Field("Time###start", start, AtLeast(0.0f).Tip("呼ぶ時間（秒）"));
	ui.Field("Name###name", name, Tip("イベントの名前"));
	ui.Field("Value###value", value, Tip("イベントに付ける文字（使い方は受け取る側が決める）"));
}

//=============================================================================
// トラック
//=============================================================================
void TimelineEventTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (frame.ranges.empty()) {
		return; // シーク・停止では呼ばない（対象も探さない）
	}
	ForEachStarted(frame.ranges, [&](const TimelineEventClip& clip) {
		const Handle<Entity> entity = FindTarget(frame, state);
		if (entity.IsValid()) {
			frame.events->push_back({frame.owner, entity, clip.name, clip.value, clip.start});
		}
	});
}