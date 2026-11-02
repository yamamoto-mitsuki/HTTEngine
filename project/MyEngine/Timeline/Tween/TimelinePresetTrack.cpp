#include "TimelinePresetTrack.h"

#include <algorithm>
#include <vector>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/Timeline/TimelineManager.h"

namespace {
int depth = 0; // 今、何段目のプリセットの中を動かしているか（自分を入れたときに止めるため）

/// <summary>
/// Preset のトラックの、Entity ごとの値
/// </summary>
class TimelinePresetState : public TimelineTrackState {
public:
	std::vector<TimelineStates> clips; // [クリップの番号] プリセットのトラックの値
	std::vector<TimelineRange> ranges; // プリセットの時間に直した、このフレームに通った幅（毎回確保しないように使い回す）
};
} // namespace

//=============================================================================
// クリップ
//=============================================================================
void TimelinePresetClip::Fields(ComponentUI& ui) {
	TimelineClip::Fields(ui);
	ui.AssetField("Timeline###timeline", timeline, AssetType::Timeline, Tip("再生するプリセット（.timeline）。中のトラックは、このトラックの対象を「再生する Entity」として動く"));
}

//=============================================================================
// トラック
//=============================================================================
// ===== クリップの上の名前（ファイルの名前）=====
std::string TimelinePresetTrack::GetClipLabel(size_t index) const {
	const std::string& path = TimelineManager::GetPath(clips[index].timeline);
	return path.empty() ? std::string("(none)") : ToUtf8(ToPath(path).stem());
}

std::unique_ptr<TimelineTrackState> TimelinePresetTrack::CreateState() const { return std::make_unique<TimelinePresetState>(); }

// ===== 動かす =====
void TimelinePresetTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (clips.empty()) {
		return;
	}
	if (depth >= kMaxDepth) {
		WarnOnce(state, "プリセットの中のプリセットが深すぎます（プリセットが自分を入れていないか確かめてください）");
		return;
	}
	// --- 今のクリップ（最初のクリップの前は、最初のクリップの 0 秒）---
	const TimelinePresetClip* found = FindClip(frame.time);
	const TimelinePresetClip& clip = (found != nullptr) ? *found : clips.front();
	const Timeline* preset = TimelineManager::Get(clip.timeline);
	if (preset == nullptr) {
		return;
	}
	const Handle<Entity> entity = FindTarget(frame, state);
	if (!entity.IsValid()) {
		return;
	}
	// --- プリセットのトラックの値（プリセットのファイルを直したら作り直す）---
	auto& presetState = static_cast<TimelinePresetState&>(state);
	presetState.clips.resize(clips.size());
	TimelineStates& states = presetState.clips[static_cast<size_t>(&clip - clips.data())];
	if (states.Prepare(clip.timeline) && frame.isPreview) {
		// プレビューは警告を出さない（直すたびに作り直すのでログが埋まる。問題は Timeline ウィンドウの行に出す）
		for (const std::unique_ptr<TimelineTrackState>& track : states.tracks) {
			track->isWarned = true;
		}
	}
	// --- 通った幅を、プリセットの時間に直す（クリップの外の部分は切る。クリップの始まりをまたいだら 0 ちょうども通った）---
	presetState.ranges.clear();
	for (const TimelineRange& range : frame.ranges) {
		const float start = range.start - clip.start;
		const float end = range.end - clip.start;
		if (end < 0.0f || start > clip.duration) {
			continue;
		}
		presetState.ranges.push_back({std::clamp(start, 0.0f, clip.duration), std::clamp(end, 0.0f, clip.duration), range.includesStart || start < 0.0f});
	}
	// --- プリセットを、対象を再生する Entity にして動かす ---
	TimelineFrame local = frame;
	local.owner = entity;
	local.time = std::clamp(frame.time - clip.start, 0.0f, clip.duration);
	local.ranges = presetState.ranges;
	++depth;
	preset->Evaluate(local, states, true, false);
	--depth;
}