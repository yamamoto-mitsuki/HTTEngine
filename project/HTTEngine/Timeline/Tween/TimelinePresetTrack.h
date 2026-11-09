#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "HTTEngine/Timeline/TimelineTrack.h"

/// <summary>
/// Preset のクリップ。別の Timeline のファイル（プリセット）を、トラックの対象の上で再生する
/// </summary>
struct TimelinePresetClip : TimelineClip {
	uint32_t timeline = 0; // 再生するファイル（AssetType::Timeline の番号）

	void Fields(ComponentUI& ui);
};


/// <summary>
/// プリセット（別の .timeline）を、ずらして並べて再生するトラック（UIPopIn.timeline を、要素ごとに少しずつ遅らせて並べる、など）
/// <para>プリセットの中のトラックは、このトラックの対象を「再生する Entity」として動く。プリセットは、対象を空（自分）にして作ると何にでも使える</para>
/// <para>時間：クリップの始まりがプリセットの 0 秒。最初のクリップの前はプリセットの 0 秒、クリップの後はクリップの終わりの値のまま</para>
/// <para>イベントも呼ぶ。プリセットの中にプリセットを入れてよい（4 段まで。自分を入れたときは、そこで止まる）</para>
/// </summary>
class TimelinePresetTrack : public TimelineTrackOf<TimelinePresetClip> {
public:
	static constexpr int kMaxDepth = 4; // プリセットの中のプリセットの段の上限

	std::string GetClipLabel(size_t index) const override;
	bool CanPreview() const override { return true; }
	std::unique_ptr<TimelineTrackState> CreateState() const override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
};