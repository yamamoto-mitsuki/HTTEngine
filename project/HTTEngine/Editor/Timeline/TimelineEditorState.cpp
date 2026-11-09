#ifdef USE_IMGUI
#include "TimelineEditorState.h"

#include <algorithm>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Systems/Animation/TimelineSystem.h"

//=============================================================================
// 調べる
//=============================================================================
TimelineTrack* TimelineEditorState::GetSelectedTrack() const {
	const Timeline* timeline = TimelineManager::Get(handle);
	if (timeline == nullptr || selection.track < 0 || selection.track >= static_cast<int>(timeline->tracks.size())) {
		return nullptr;
	}
	return timeline->tracks[selection.track].get();
}

std::span<Curve> TimelineEditorState::GetSelectedCurves() const {
	TimelineTrack* track = GetSelectedTrack();
	return (track != nullptr && selection.clip >= 0) ? track->GetClipCurves(static_cast<size_t>(selection.clip)) : std::span<Curve>{};
}

float TimelineEditorState::Snap(float value) const { return ImGui::GetIO().KeyCtrl ? value : SnapTime(value, snapStep); }

//=============================================================================
// 変える
//=============================================================================
void TimelineEditorState::MarkEdited() {
	TimelineManager::MarkChanged(handle);
	seenRevision = TimelineManager::GetRevision(handle); // 自分で増やした版（Undo と区別する）
	hasEdit = true;
}

void TimelineEditorState::SeekTo(float value) {
	time = std::clamp(value, 0.0f, duration);
	if (isGameRunning) {
		// Play 中に動かせるのは Timeline Component の再生だけ（UI State・UI Screen の再生は、その係が時間を決める）
		if (GetLayer() == TimelineLayer::Component) {
			TimelineSystem::Seek(owner, time);
		}
		return;
	}
	playhead.Seek(time, duration);
	isPreview = owner.IsValid();
}

void TimelineEditorState::SelectTrack(int track) { SelectClip(track, -1); }

void TimelineEditorState::SelectClip(int track, int clip) {
	if (selection.track != track || selection.clip != clip) {
		curveEditor = CurveEditor{}; // 別のカーブになるので、見ている範囲と選択をやり直す
	}
	selection.track = track;
	selection.clip = clip;
	selection.hasKey = false;
}

void TimelineEditorState::SelectKey(int track, int clip, float keyTime) {
	SelectClip(track, clip);
	selection.hasKey = true;
	selection.keyTime = keyTime;
}

void TimelineEditorState::ClampSelection() {
	const TimelineTrack* track = GetSelectedTrack();
	if (track == nullptr) {
		selection = {};
		curveEditor = CurveEditor{};
		return;
	}
	if (selection.clip >= static_cast<int>(track->GetClipCount())) {
		SelectClip(selection.track, -1);
	}
}
#endif