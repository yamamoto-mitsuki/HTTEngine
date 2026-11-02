#pragma once
#include <set>
#include <string>

#include "MyEngine/Math/Easing.h"
#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// クリップが終わった後のカメラの戻し方
/// </summary>
enum class TimelineCameraExit {
	Cut,  // すぐ通常のカメラへ戻す
	Ease, // Exit Time の秒をかけて、通常のカメラへ補間する（Timeline の長さに入る）
	Hold, // 次のクリップまで、このカメラのまま。後ろにクリップが無ければ TimelineSystem::Stop まで
};

/// <summary>
/// カメラのクリップ。この時間の間に写すカメラを持つ
/// <para>camera は再生する Entity からのパス（Prefab の中のカメラ）か、/ で始まるシーンの一番上からのパス（シーンのカメラ）</para>
/// </summary>
struct TimelineCameraClip : TimelineClip {
	std::string camera;                                // 写すカメラのパス（空なら再生する Entity 自身）
	float blendIn = 0.0f;                              // 前のクリップと重なっていないとき、この秒をかけて補間する（0 ならすぐ切り替える）
	Ease::Type blendEase = Ease::Type::InOutCubic;     // 始まりの補間と、前のクリップとの重なりの補間の速さの変え方
	TimelineCameraExit exit = TimelineCameraExit::Cut; // 終わった後の戻し方
	float exitTime = 1.0f;                             // 戻す補間の秒（Ease のとき。Hold は TimelineSystem::Stop で止めたときに使う）
	Ease::Type exitEase = Ease::Type::InOutCubic;      // 戻す補間の速さの変え方

	void Fields(ComponentUI& ui);
	void WriteExtra(SceneJson& out) const;
	void ReadExtra(SceneJson& in, std::set<std::string>& warnings);
};

/// <summary>
/// ある時間のカメラの選び方（Entity を知らない計算の結果）
/// </summary>
struct TimelineCameraSelection {
	static constexpr int kNormalCamera = -1; // 通常のカメラ（クリップの番号の代わり）

	bool isActive = false;                    // false なら何も頼まない（通常のカメラのまま）
	int from = kNormalCamera;                 // 混ぜる前のクリップの番号
	int to = kNormalCamera;                   // 混ぜた先のクリップの番号
	float weight = 1.0f;                      // to の割合（イージングを掛けた後の値）
	float exitTime = 0.0f;                    // ここで止められたときに、通常のカメラへ戻す秒
	Ease::Type exitEase = Ease::Type::Linear; // 戻す速さの変え方
};


/// <summary>
/// クリップごとに写すカメラを決めるトラック
/// <para>通常のカメラ（Priority で選ぶカメラ）の代わりに写すカメラを、CameraSystem::RequestOverride で毎フレーム頼む。頼むのをやめれば通常のカメラへ戻る</para>
/// <para>重なったクリップは、後のクリップの始まりから前のクリップの終わりまでの間に混ぜる。時間から決まる値なので、シークしてもプレビューでも同じ絵になる</para>
/// </summary>
class TimelineCameraTrack : public TimelineTrackOf<TimelineCameraClip> {
public:
	TimelineCameraTrack() { name = "Camera"; }

	// time 秒のカメラの選び方
	TimelineCameraSelection Select(float time) const;

	float GetEndTime() const override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
	std::unique_ptr<TimelineTrackState> CreateState() const override;
	std::string GetClipLabel(size_t index) const override;
	TimelineClipFade GetClipFade(size_t index) const override;
	bool UsesTarget() const override { return false; }
	std::string FindClipProblem(Handle<Entity> owner) const override;
	bool CanPreview() const override { return true; }
	bool EvaluatesEveryFrame() const override { return true; }
};