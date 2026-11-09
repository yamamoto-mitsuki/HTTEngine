#pragma once
#include <type_traits>

#include "HTTEngine/Math/Vector3.h"

// 範囲の中の Rigidbody を、風の向きへ押す。Entityに付けるComponent（カテゴリ：Physics）
// 範囲は Entity の位置を真ん中にした箱（Size × Scale。Entity と一緒に回る）。風は Entity の右（ローカルの +X）へ吹く。Rotation の Z で向きを変える（90 で上へ）
// 押すのは WindZoneSystem（再生中だけ）。風の速さまでは Strength で速くして、それより速い物はそれ以上押さない（いつまでも速くならない）
// 画面の風の線は、カメラが追っている物（Camera Follow の Target）が範囲の中にいる間だけ出る


/// <summary>
/// 風の吹く範囲（Unity の Wind Zone・Area Effector の、横から見るゲーム用の小さい版）
/// </summary>
struct WindZoneComponent {
	bool enabled = true;               // false の間は吹かない
	Vector3 size = {8.0f, 4.0f, 4.0f}; // 範囲の大きさ（m。Entity から見た箱の、X・Y・Z の長さ。Scale も掛かる）
	float speed = 5.0f;                // 風の速さ（m/s）。物はこの速さまで流される（キューブの走る速さ 7 より遅いと、向かい風でも少し進める）
	float strength = 40.0f;            // 風の速さまで速くする強さ（m/s²）。上向きの風で浮かせるなら、重力より大きくする
	float gust = 0.3f;                 // ゆれ（0〜1。0.3 なら、速さが ±30% の間でゆれる）
	float gustFrequency = 0.5f;        // ゆれる速さ（1秒に何回くらい。大きいほど細かくゆれる）
	float lineAmount = 0.6f;           // 画面の風の線の濃さ（0〜1。0 なら線を出さない）
};

static_assert(std::is_trivially_copyable_v<WindZoneComponent>);