#pragma once
#include <span>
#include <vector>

#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// 点を順に通る、なめらかな線（Catmull-Rom スプライン）。細かい折れ線にして持ち、線に沿った長さ（m）で位置と向きを聞ける
/// <para>Arrow Strip（矢印を線に沿って等間隔に並べる）のに使う</para>
/// <para>点が 2 つならまっすぐな線。点は Entity から見た位置でも、ワールド座標でもよい（入れた座標のまま返す）</para>
/// </summary>
class PathCurve {
public:
	/// <summary>
	/// 線の上の1点
	/// </summary>
	struct Sample {
		Vector3 position = {0.0f, 0.0f, 0.0f}; // 位置
		Vector3 tangent = {1.0f, 0.0f, 0.0f};  // 進む向き（長さ1）
	};

	/// <summary>
	/// 点を順に通る線を作り直す（前の中身は捨てる。配列の置き場所は使い回す）
	/// <para>同じ位置に続けて置いた点は1つにまとめる。まとめた後で点が 2 つより少なければ、空の線になる</para>
	/// </summary>
	void Build(std::span<const Vector3> points);

	// 線があるか（点が 2 つ以上で作れた）
	bool IsEmpty() const { return positions_.size() < 2; }
	// 線の長さ（m）。空なら 0
	float GetLength() const { return distances_.empty() ? 0.0f : distances_.back(); }
	// 細かい折れ線の点（エディタで線を描くのに使う）
	std::span<const Vector3> GetPolyline() const { return positions_; }

	/// <summary>
	/// 始まりから distance（m）進んだ所の位置と向き。0 より前は始まり、長さより先は終わりで止まる
	/// <para>空の線なら、原点と +X</para>
	/// </summary>
	Sample Evaluate(float distance) const;

private:
	std::vector<Vector3> positions_; // 細かい折れ線の点（始まりから終わりへ）
	std::vector<float> distances_;   // 始まりから、その点までの長さ（m。positions_ と同じ数。distances_[0] は 0）
	std::vector<Vector3> controls_;  // Build で、続けて同じ位置の点をまとめた後の点（使い回す）
};