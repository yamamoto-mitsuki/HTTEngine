#pragma once
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Timeline/TimelineTrack.h"

/// <summary>
/// 衝撃のクリップ（長さは無い。start の時間ちょうどの1点）
/// </summary>
struct TimelineCameraShakeClip : TimelineClip {
	static constexpr bool kHasDuration = false; // 1点
	float strength = 0.3f;                      // 最初に振れる幅（m）
	bool useDirection = false;                  // false：向きは画面の面の中でランダム / true：Direction の向き
	Vector3 direction = {1.0f, 0.0f, 0.0f};     // 最初に振れる向き（ワールド）

	void Fields(ComponentUI& ui);
};


/// <summary>
/// 指定の時間に、カメラへ衝撃を送るトラック（CameraSystem::AddImpulse。有効な Camera Shake 全部が受ける）
/// <para>ずっと続く揺れは TimelineValueTrack で Camera Shake の Gain（noiseGain）を動かす</para>
/// <para>プレビューでは送らず、その時間の揺れの値を計算して見せる</para>
/// </summary>
class TimelineCameraShakeTrack : public TimelineTrackOf<TimelineCameraShakeClip> {
public:
	TimelineCameraShakeTrack() { name = "Camera Shake"; }

	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
	bool UsesTarget() const override { return false; }
	bool CanPreview() const override { return true; }
};