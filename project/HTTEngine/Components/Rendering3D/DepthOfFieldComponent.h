#pragma once
#include <type_traits>

// 被写界深度（ピントの合った所だけはっきり見え、手前と奥がぼける）。シーンに1つ、どの Entity に付けてもよい Component（カテゴリ：Rendering3D）
// 値は DepthOfFieldSystem が毎フレーム SceneRenderer へ渡し、ポストエフェクト（DepthOfFieldPass）が深度でぼかす
// 有効な物が2つ以上あれば、先に見つかった1つだけを使う。1つも無ければぼかさない。停止中のエディタでも掛かる（Scene ビューは View → Scene View → Depth of Field で切れる）
// 奥行き（深度）を書かない物（Alpha Clip なしの Sprite の板・半透明・パーティクル）は、後ろにある物と同じだけぼける

/// <summary>
/// ピントをどこに合わせるか。Inspector では、選んだ合わせ方の値だけが出る
/// </summary>
enum class DepthOfFieldFocus {
	FollowTarget, // メインカメラの Camera Follow の Target（遊ぶキャラ）の奥行きに合わせる。Target が無ければ Focus Distance の初期値
	Distance,     // カメラから Focus Distance（m）の奥行きに合わせる
};

/// <summary>
/// 被写界深度（Unity の Post Processing の Depth of Field の小さい版）
/// </summary>
struct DepthOfFieldComponent {
	bool enabled = true;                                       // false の間はぼかさない
	DepthOfFieldFocus focus = DepthOfFieldFocus::FollowTarget; // ピントの合わせ方
	float focusDistance = 16.5f;                               // カメラからピントの合う所までの奥行き（m。Distance のとき）
	float focusRange = 3.0f;                                   // ピントの前後この奥行き（m）までは、ぼけない（遊ぶ面の手前・奥の飾りも、はっきり見せる幅）
	float farTransition = 40.0f;                               // その奥から、一番ぼけるまでの奥行き（m。小さいほど、すぐ奥がぼける）
	float nearTransition = 4.0f;                               // その手前から、一番ぼけるまでの奥行き（m。0 なら手前はぼかさない）
	float maxBlur = 8.0f;                                      // 一番ぼけた所の、ぼけの半径（ピクセル。高さ 720 の画面で。大きいほど重い）
};

static_assert(std::is_trivially_copyable_v<DepthOfFieldComponent>);