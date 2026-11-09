#include "PathCurve.h"

#include <algorithm>

namespace {
constexpr int kSegmentsPerSpan = 16; // 点と点の間（1区間）を、何本の短い直線に分けるか（多いほどなめらか・重い）

//=============================================================================
// Catmull-Rom スプライン
//=============================================================================
//
//   p0 ・          ・ p3        p1 から p2 までの曲線を、前後の点 p0・p3 も使って作る
//        ╲  p1 ●━━━━● p2 ╱     ・p1 では「p0 → p2 の向き」、p2 では「p1 → p3 の向き」に進む
//                               ・なので、区間のつなぎ目で向きが急に変わらない（なめらか）
//   t は 0（p1）〜 1（p2）。式は、この2つの決まりを満たす3次式（Catmull-Rom の式そのまま）
//
Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
	const float t2 = t * t;
	const float t3 = t2 * t;
	return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}
} // namespace

//=============================================================================
// 作る
//=============================================================================
void PathCurve::Build(std::span<const Vector3> points) {
	positions_.clear();
	distances_.clear();
	// --- 続けて同じ位置の点をまとめる（長さ 0 の区間があると、向きが決まらない）---
	controls_.clear();
	for (const Vector3& point : points) {
		if (controls_.empty() || LengthSq(point - controls_.back()) > 0.0f) {
			controls_.push_back(point);
		}
	}
	const size_t count = controls_.size();
	if (count < 2) {
		return;
	}

	// --- 区間ごとに、細かい折れ線にする ---
	// 端の区間には「前の点」「後ろの点」が無いので、端の点で折り返した位置を使う（2P0 − P1）。端では、となりの点の方へまっすぐ進む
	const Vector3 before = controls_[0] * 2.0f - controls_[1];
	const Vector3 after = controls_[count - 1] * 2.0f - controls_[count - 2];
	positions_.push_back(controls_[0]);
	for (size_t i = 0; i + 1 < count; ++i) {
		const Vector3& p0 = (i == 0) ? before : controls_[i - 1];
		const Vector3& p3 = (i + 2 < count) ? controls_[i + 2] : after;
		// 区間の始まり（s = 0）は、1つ前の区間の終わりと同じ点なので、s = 1 から足す
		for (int s = 1; s <= kSegmentsPerSpan; ++s) {
			const float t = static_cast<float>(s) / static_cast<float>(kSegmentsPerSpan);
			positions_.push_back(CatmullRom(p0, controls_[i], controls_[i + 1], p3, t));
		}
	}

	// --- 始まりから各点までの長さ（短い直線の長さを足していく）---
	distances_.reserve(positions_.size());
	float total = 0.0f;
	distances_.push_back(total);
	for (size_t i = 1; i < positions_.size(); ++i) {
		total += Length(positions_[i] - positions_[i - 1]);
		distances_.push_back(total);
	}
}

//=============================================================================
// 長さで聞く
//=============================================================================
PathCurve::Sample PathCurve::Evaluate(float distance) const {
	if (IsEmpty()) {
		return {};
	}
	const float clamped = std::clamp(distance, 0.0f, GetLength());
	// --- distance が入っている短い直線を探す（distances_ は小さい順なので、二分探索）---
	const auto found = std::upper_bound(distances_.begin(), distances_.end(), clamped);
	const size_t end = std::clamp(static_cast<size_t>(found - distances_.begin()), size_t{1}, positions_.size() - 1);
	const size_t start = end - 1;
	// --- 直線の上で、長さの割合で位置を決める。向きはその直線の向き ---
	const float segmentLength = distances_[end] - distances_[start];
	const float t = (segmentLength > 0.0f) ? (clamped - distances_[start]) / segmentLength : 0.0f;
	Sample sample;
	sample.position = positions_[start] + (positions_[end] - positions_[start]) * t;
	if (segmentLength > 0.0f) {
		sample.tangent = (positions_[end] - positions_[start]) / segmentLength;
	}
	return sample;
}