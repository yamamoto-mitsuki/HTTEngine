#include "TimelinePlayhead.h"

#include <algorithm>
#include <cmath>

//=============================================================================
// 操作
//=============================================================================
void TimelinePlayhead::Play() {
	switch (state_) {
	case TimelinePlayState::Playing:
		return;
	case TimelinePlayState::Paused:
		break; // 続きから
	case TimelinePlayState::Stopped:
	case TimelinePlayState::Finished:
		time_ = 0.0f;
		includesStart_ = true;
		loopCount_ = 0;
		break;
	}
	state_ = TimelinePlayState::Playing;
}

void TimelinePlayhead::Pause() {
	if (state_ == TimelinePlayState::Playing) {
		state_ = TimelinePlayState::Paused;
	}
}

void TimelinePlayhead::Stop() {
	state_ = TimelinePlayState::Stopped;
	time_ = 0.0f;
	includesStart_ = true;
	loopCount_ = 0;
}

void TimelinePlayhead::Seek(float time, float duration) {
	time_ = std::isnan(time) ? 0.0f : std::clamp(time, 0.0f, (std::max)(duration, 0.0f));
	includesStart_ = false; // シークした先のイベントは呼ばない
	if (state_ != TimelinePlayState::Playing) {
		state_ = TimelinePlayState::Paused;
	}
}

//=============================================================================
// 進める
//=============================================================================
void TimelinePlayhead::Advance(float deltaTime, float duration, bool isLoop, std::vector<TimelineRange>& out) {
	out.clear();
	if (state_ != TimelinePlayState::Playing || !(deltaTime > 0.0f)) {
		return; // !(> 0) なので NaN もここで止まる
	}
	// --- 長さ 0 の Timeline：0 ちょうどの物だけ呼んで終わる（割り算を避ける）---
	if (duration <= 0.0f) {
		if (includesStart_) {
			out.push_back({0.0f, 0.0f, true});
		}
		time_ = 0.0f;
		includesStart_ = false;
		state_ = TimelinePlayState::Finished;
		return;
	}
	// --- 最後まで届かない ---
	const float end = time_ + deltaTime;
	if (end < duration) {
		out.push_back({time_, end, includesStart_});
		time_ = end;
		includesStart_ = false;
		return;
	}
	// --- 最後まで届いた ---
	out.push_back({time_, duration, includesStart_});
	includesStart_ = false;
	if (!isLoop) {
		time_ = duration;
		state_ = TimelinePlayState::Finished;
		return;
	}
	// --- ループ：丸ごと通った周の分だけ全体の幅を足して、最後に余りの幅を足す ---
	const float remaining = end - duration;
	const int wholeLoops = (std::min)(static_cast<int>(std::floor(remaining / duration)), kMaxLoopsPerFrame);
	for (int i = 0; i < wholeLoops; ++i) {
		out.push_back({0.0f, duration, true});
		++loopCount_;
	}
	++loopCount_;
	time_ = std::fmod(remaining, duration);
	out.push_back({0.0f, time_, true});
}