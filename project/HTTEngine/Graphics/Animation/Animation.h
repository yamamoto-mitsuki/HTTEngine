#pragma once
#include <map>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include "Vector3.h"
#include "Quaternion.h"
#include "HTTAssert.h"

// ===== キーフレーム =====
template <typename T> 
struct Keyframe {
	T value;    // 値
	float time; // 時刻（秒）
};
using KeyframeVector3 = Keyframe<Vector3>;
using KeyframeQuaternion = Keyframe<Quaternion>;


// ===== ノードごとのAnimation =====
// SRT1つあたりのテンプレート
template<typename T> 
struct AnimationCurve {
	std::vector<Keyframe<T>> keyframes;
};

struct NodeAnimation {
	AnimationCurve<Vector3> scale;
	AnimationCurve<Quaternion> rotate;
	AnimationCurve<Vector3> translate;
};


// ===== Animation =====
struct Animation {
	std::string name; // アニメーションの名前
	float duration;   // 全体の尺（秒）
	std::map<std::string, NodeAnimation> nodeAnimations; // NodeAnimationの集合
};



//=============================================================================
// Animation中の任意の時刻も値を取得する
//=============================================================================
// 位置・大きさ: まっすぐ混ざる
inline Vector3 Interpolate(const Vector3& start, const Vector3& end, float t) { return Lerp(start, end, t); }
// 回転: 球面線形補間（同じ大きさで回る）
inline Quaternion Interpolate(const Quaternion& start, const Quaternion& end, float t) { return Slerp(start, end, t); }

// ===== 時刻Timeの値を取得 =====
template<typename T> T CalculateValue(const AnimationCurve<T>& curve, float time) { 
	const std::vector<Keyframe<T>>& keyframes = curve.keyframes;
	HTT_ASSERT_MSG(!keyframes.empty(), "キーが1つも無いカーブは補間できません");

	// --- Timeより後ろにある最初のキーを探す ---
	// キーは時刻順で並んでいるので二分探索
	auto next = std::ranges::upper_bound(keyframes, time, {}, &Keyframe<T>::time);
	if (next == keyframes.begin()) {
		return keyframes.front().value; // 最初のキーより前
	}
	if (next == keyframes.end()) {
		return keyframes.back().value; // 最後のキーより後
	}
	// 前後のキーの間のどこか（0～1）を出して混ぜる
	auto prev = std::prev(next);
	const float t = (time - prev->time) / (next->time - prev->time);
	return Interpolate(prev->value, next->value, t);
}