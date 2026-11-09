#pragma once
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"

// カメラを線に沿って動かす。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// 線は Path の Entity の子を上から順に通る、なめらかな線（PathCurve）。点が Entity なので、Scene ビューのギズモで動かせて、Prefab にもそのまま入る
// Position を Timeline のキーで動かすと、決めた時間で線の上を動く
// 変えるのは写す位置だけで、Transform は書き換えない（Camera Look At・Camera Shake は線の上の位置から掛かる）

/// <summary>
/// 線の上でのカメラの向き
/// </summary>
enum class CameraPathRotation {
	Keep,      // 変えない（Transform の向き。Camera Look At があれば注視点を見る）
	AlongPath, // 線の進む向きを見る
};

/// <summary>
/// 線に沿って動くカメラ（Unity の Cinemachine の Spline Dolly の小さい版）
/// </summary>
struct CameraPathComponent {
	bool enabled = true;                                    // false の間は Transform の位置のまま
	EntityRef path;                                         // 線の Entity（Hierarchy からドラッグ）。子の Entity が点になる
	float position = 0.0f;                                  // 線の上の位置（0：始まり 〜 1：終わり。長さに対する割合）
	CameraPathRotation rotation = CameraPathRotation::Keep; // 向きの決め方
};

static_assert(std::is_trivially_copyable_v<CameraPathComponent>);