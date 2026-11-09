#include "TimelineCameraShakeTrack.h"

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
const Vector3 kPreviewDirection = {1.0f, 1.0f, 0.0f}; // プレビューでの、ランダムの代わりの向き（カメラの右上。動かすたびに絵が変わらないように）
} // namespace

//=============================================================================
// クリップ
//=============================================================================
void TimelineCameraShakeClip::Fields(ComponentUI& ui) {
	ui.Field("Time###start", start, AtLeast(0.0f).Tip("衝撃を送る時間（秒）"));
	ui.Field("Strength###strength", strength, AtLeast(0.0f).Tip("最初に振れる幅（m）。Camera Shake の Gain が掛かる"));
	ui.Field("Use Direction###useDirection", useDirection, Tip("外すと、向きは画面の面の中でランダム"));
	ui.BeginDisabled(!useDirection);
	ui.Field("Direction###direction", direction, Tip("最初に振れる向き（ワールド）。長さは1でなくてよい"));
	ui.EndDisabled();
}

//=============================================================================
// トラック
//=============================================================================
void TimelineCameraShakeTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& /*state*/) const {
	// --- プレビュー：今の時間までに始まった衝撃を、受けてからの秒と一緒に渡す ---
	if (frame.isPreview) {
		for (const TimelineCameraShakeClip& clip : clips) {
			if (clip.start <= frame.time) {
				CameraSystem::AddPreviewImpulse(clip.strength, clip.useDirection ? clip.direction : kPreviewDirection, clip.useDirection, frame.time - clip.start);
			}
		}
		return;
	}
	// --- 再生：このフレームに通った衝撃を送る ---
	ForEachStarted(frame.ranges, [](const TimelineCameraShakeClip& clip) {
		if (clip.useDirection) {
			CameraSystem::AddImpulse(clip.strength, clip.direction);
		} else {
			CameraSystem::AddImpulse(clip.strength);
		}
	});
}