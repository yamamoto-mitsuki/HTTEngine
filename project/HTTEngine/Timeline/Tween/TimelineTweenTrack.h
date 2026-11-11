#pragma once
#include <memory>
#include <set>
#include <string>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Timeline/TimelineTrack.h"
#include "HTTEngine/Timeline/TimelineValueTrack.h"

/// <summary>
/// Tween の端（From・To）の値の決め方
/// </summary>
enum class TimelineTweenMode {
	Current,   // クリップが始まったときの今の値（途中から別の動きに替わっても、値が飛ばない）
	Value,     // Value に書いた値
	Add,       // 元の値 ＋ Value（元の位置からずらす）
	Multiply,  // 元の値 × Value（元の大きさ・透明度に対する割合）
	Parameter, // 実行中に TimelineSystem::SetParameter で渡した値（渡していなければ Value）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(TimelineTweenMode) {
	using enum TimelineTweenMode;
	return MakeEnumTips<TimelineTweenMode>({
	    {Current, "クリップが始まったときの今の値（途中から別の動きに替わっても、値が飛ばない）"},
	    {Value, "下の Value の値"},
	    {Add, "元の値 ＋ Value（元の位置からずらす）"},
	    {Multiply, "元の値 × Value（元の大きさ・透明度に対する割合）"},
	    {Parameter, "実行中に TimelineSystem::SetParameter で渡した値（渡していなければ Value）"},
	});
}

/// <summary>
/// Tween の端1つ（From か To）
/// </summary>
struct TimelineTweenPoint {
	TimelineTweenMode mode = TimelineTweenMode::Value;
	Vector4 value = {0.0f, 0.0f, 0.0f, 0.0f}; // Value・Add・Multiply・Parameter（渡していないとき）に使う値。1つの値の項目は x だけ使う。色は r, g, b, a の順
	std::string parameter;                    // Parameter のときの名前（TimelineSystem::SetParameter に渡す名前）
};

/// <summary>
/// Tween のクリップ。From から To へ、Ease のカーブで動かす
/// <para>Ease は 横：時間の割合 0〜1（クリップの始まり〜終わり）→ 縦：進んだ割合 0〜1（From〜To）。クリップの長さを変えても形はそのまま。1 を超えると To より行き過ぎる</para>
/// </summary>
struct TimelineTweenClip : TimelineClip {
	TimelineTweenPoint from = {.mode = TimelineTweenMode::Current};
	TimelineTweenPoint to = {.mode = TimelineTweenMode::Value};
	Curve ease = Curve::FromEase(Ease::Type::OutCubic);

	void WriteExtra(SceneJson& out) const;
	void ReadExtra(SceneJson& in, std::set<std::string>& warnings);
};

/// <summary>
/// Component の1つの項目を、From → To の Tween で動かすトラック（値のトラックのキーの代わりに、端の値の決め方で動きを書く）
/// <para>From を Current にすると、動いている途中の値から続けて動く（ホバーの途中で離した・出している途中で閉じた）</para>
/// <para>Add・Multiply は元の値からの差・割合なので、対象を空（再生する Entity 自身）にして作れば、同じファイルをどの要素にも使い回せる</para>
/// <para>元の値は、その Entity のその項目を Tween が初めて動かしたときの値（TimelineSystem::ResetBaseValues で取り直す）。プレビューでは置いてある値</para>
/// <para>最初のクリップの前：From が Current なら何も書かない。ほかは From の値を書く（遅れて出る要素を、出る前は隠しておける）</para>
/// </summary>
class TimelineTweenTrack : public TimelineTrackOf<TimelineTweenClip> {
public:
	TimelinePropertyBinding binding; // 動かす Component と項目

	void Fields(ComponentUI& ui) override { binding.Fields(ui); }
	TimelinePropertyBinding* GetPropertyBinding() override { return &binding; }
	const TimelinePropertyBinding* GetPropertyBinding() const override { return &binding; }
	std::string GetClipLabel(size_t index) const override;
	bool CanPreview() const override { return true; }
	std::unique_ptr<TimelineTrackState> CreateState() const override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;

	// ===== 元の値（Add・Multiply の基準。TimelineSystem から呼ぶ）=====
	// entity の覚えた元の値を忘れる（次に動かしたときの値が、新しい元の値になる）
	static void ResetBaseValues(Handle<Entity> entity);
	// 全部忘れる（エディタの停止）
	static void ClearBaseValues();
};