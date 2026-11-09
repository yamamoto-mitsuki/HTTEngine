#pragma once
#include <type_traits>

#include "HTTEngine/Math/Vector3.h"

// 霧（距離の霧＋高さの霧）。シーンに1つ、どの Entity に付けてもよい Component（カテゴリ：Rendering3D）
// 値は FogSystem が毎フレーム SceneRenderer へ渡し、3D のシェーダー（モデル・Primitive・Polygon・Sprite の板・パーティクル）の最後で色に混ぜる
// 有効な Fog が2つ以上あれば、先に見つかった1つだけを使う。1つも無ければ霧なし。停止中のエディタでも掛かる（Scene ビューは View → Scene View → Fog で切れる）

/// <summary>
/// 霧（Unity の Lighting の Fog・Unreal の Exponential Height Fog の小さい版）
/// </summary>
struct FogComponent {
	bool enabled = true; // false の間は霧なし
	// --- 距離の霧（遠いほど、明るい霧の色に溶ける）---
	Vector3 color = {0.70f, 0.79f, 0.81f}; // 霧の色（sRGB。遠くの山・木が溶けていく色。空の地平線の色に近づける）
	float startDistance = 20.0f;           // カメラからこの距離（m）までは霧が無い（遊ぶ面より少し奥にすると、遊ぶ面ははっきり見える）
	float density = 0.02f;                 // 濃さ（1m あたり。大きいほど近くから濃くなる）
	float maxOpacity = 0.4f;               // 一番濃い所（0〜1。1 にすると、一番奥の空の絵まで霧の色だけになる）
	// --- 高さの霧（低いほど、暗い霧の色に沈む。穴の底を見えなくする）---
	Vector3 heightColor = {0.10f, 0.19f, 0.21f}; // 高さの霧の色（sRGB。暗い青緑）
	float heightBase = -4.0f;                    // 一番濃くなる高さ（ワールドの Y。これより下はずっと一番濃い）
	float heightFalloff = 0.4f;                  // 上へ薄くなる速さ（1m あたり。大きいほど、すぐ薄くなる）
	float heightMaxOpacity = 0.9f;               // 一番濃い所（0〜1。0 なら高さの霧なし）
};

static_assert(std::is_trivially_copyable_v<FogComponent>);