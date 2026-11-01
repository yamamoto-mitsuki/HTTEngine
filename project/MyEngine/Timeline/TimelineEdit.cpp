#include "TimelineEdit.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace {
// 同じ時間か
bool IsSameTime(float a, float b) { return std::abs(a - b) <= TimelineEdit::kSameKeyTime; }
} // namespace

//=============================================================================
// トラック
//=============================================================================
int TimelineEdit::AddTrack(Timeline& timeline, std::string_view typeName) {
	std::unique_ptr<TimelineTrack> track = TimelineTrackRegistry::Create(typeName);
	if (track == nullptr) {
		return -1;
	}
	timeline.tracks.push_back(std::move(track));
	return static_cast<int>(timeline.tracks.size()) - 1;
}

void TimelineEdit::RemoveTrack(Timeline& timeline, int index) {
	if (index >= 0 && index < static_cast<int>(timeline.tracks.size())) {
		timeline.tracks.erase(timeline.tracks.begin() + index);
	}
}

int TimelineEdit::MoveTrack(Timeline& timeline, int index, int newIndex) {
	const int count = static_cast<int>(timeline.tracks.size());
	if (index < 0 || index >= count) {
		return index;
	}
	const int destination = std::clamp(newIndex, 0, count - 1);
	// std::rotate は、範囲の中の1つを前へ持ってくる関数。動かす向きで、範囲の取り方を変える
	const auto begin = timeline.tracks.begin();
	if (destination < index) {
		std::rotate(begin + destination, begin + index, begin + index + 1);
	} else {
		std::rotate(begin + index, begin + index + 1, begin + destination + 1);
	}
	return destination;
}

//=============================================================================
// クリップ
//=============================================================================
float TimelineEdit::CalcNewClipDuration(const TimelineTrack& track, float start) {
	float duration = kDefaultClipDuration;
	for (size_t i = 0; i < track.GetClipCount(); ++i) {
		const float gap = track.GetClipStart(i) - start;
		if (gap > kMinClipDuration) {
			duration = (std::min)(duration, gap);
		}
	}
	return duration;
}

//=============================================================================
// キー
//=============================================================================
std::vector<float> TimelineEdit::CollectKeyTimes(std::span<const Curve> curves) {
	std::vector<float> times;
	for (const Curve& curve : curves) {
		for (const CurveKey& key : curve.GetKeys()) {
			times.push_back(key.time);
		}
	}
	std::ranges::sort(times);
	const auto removed = std::ranges::unique(times, IsSameTime); // 並んでいる同じ値を後ろへ寄せて、寄せた範囲を返す
	times.erase(removed.begin(), removed.end());
	return times;
}

void TimelineEdit::AddKeys(std::span<Curve> curves, float time) {
	for (Curve& curve : curves) {
		if (!curve.GetKeys().empty()) {
			curve.InsertKeyOnCurve(time);
		}
	}
}

void TimelineEdit::RemoveKeys(std::span<Curve> curves, float time) {
	for (Curve& curve : curves) {
		const std::vector<CurveKey>& keys = curve.GetKeys();
		for (int i = static_cast<int>(keys.size()) - 1; i >= 0; --i) { // 消すと後ろの番号がずれるので、後ろから
			if (IsSameTime(keys[i].time, time)) {
				curve.RemoveKey(i);
			}
		}
	}
}

void TimelineEdit::MoveKeys(std::span<Curve> curves, float from, float to) {
	for (Curve& curve : curves) {
		std::vector<CurveKey> keys = curve.GetKeys();
		for (CurveKey& key : keys) {
			if (IsSameTime(key.time, from)) {
				key.time = to;
			}
		}
		curve = Curve(std::move(keys)); // 並びとハンドルの長さは Curve が直す
	}
}

void TimelineEdit::FillFlat(Curve& curve, float value, float duration) {
	curve = Curve();
	curve.AddKey(0.0f, value);
	if (duration > kSameKeyTime) {
		curve.AddKey(duration, value);
	}
}