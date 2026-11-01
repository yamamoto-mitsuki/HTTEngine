#pragma once
#include <set>
#include <array>
#include <string>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// 値のクリップ。成分ごとのカーブを持つ（カーブの時間はクリップの始まりからの秒）
/// <para>キーの無い成分は書かない（color の a だけを動かすと、r・g・b はそのまま）</para>
/// </summary>
struct TimelineValueClip : TimelineClip {
	std::array<Curve, ComponentValue::kChannelCount> curves; // [x, y, z, w]。色は r, g, b, a の順

	void WriteExtra(SceneJson& out) const;
	void ReadExtra(SceneJson& in, std::set<std::string>& warnings);
};


/// <summary>
/// Component の1つの項目を、カーブで動かすトラック
/// <para>項目は保存の名前で指す（Describe 関数の "表示名###名前" の名前）。Transform の translation・rotation・scale、Image の color など</para>
/// <para>書けるのは float・int・bool・Vector2〜4・色</para>
/// <para>時間の全部を決める：最初のクリップの前はそのクリップの始まりの値、クリップの間は前のクリップの終わりの値（ループ・Stop で前の値が残らないように）</para>
/// </summary>
class TimelineValueTrack : public TimelineTrackOf<TimelineValueClip> {
public:
	std::string component = "Transform";  // Component の名前（Inspector の見出しと同じ。Script の名前でもよい）
	std::string property = "translation"; // 項目の名前

	void Fields(ComponentUI& ui) override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
};