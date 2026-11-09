#pragma once
#include <array>
#include <cstddef>
#include <algorithm>
#include <initializer_list>

#include <externals/magic_enum/magic_enum.hpp>

// カメラごとのポストエフェクトの値を、全体の設定へ混ぜるための値
// CameraSystem が毎フレーム作って SceneRenderer へ渡す。全体の設定の値（GlobalVariables・Depth Of Field Component）を読まずに補間できる

/// <summary>
/// 1つの項目の上書き
/// <para>value は「カメラの値 × weight」。掛けた後の値で持つと、2つの上書きを補間するときに value と weight を別々に Lerp するだけで済む（色の premultiplied alpha と同じ）</para>
/// </summary>
struct PostOverride {
	float value = 0.0f;  // カメラの値 × weight
	float weight = 0.0f; // 効き具合（0：全体の設定のまま 〜 1：カメラの値）

	// 全体の設定の値 base に混ぜた値
	float Apply(float base) const { return base * (1.0f - weight) + value; }
};

/// <summary>
/// カメラごとに持てる項目
/// </summary>
enum class PostParam {
	FocusDistance,  // 被写界深度：ピントの合う奥行き
	FocusRange,     // 被写界深度：ぼけない幅
	FarTransition,  // 被写界深度：奥のぼけ始めから一番ぼけるまで
	NearTransition, // 被写界深度：手前のぼけ始めから一番ぼけるまで
	MaxBlur,        // 被写界深度：一番ぼけた所の半径
	BloomThreshold, // ブルーム：しきい値
	BloomKnee,      // ブルーム：しきい値付近の柔らかさ
	BloomIntensity, // ブルーム：強さ
	LensDistortion, // レンズ：歪み
	LensAberration, // レンズ：色収差
	LensRadialBlur, // レンズ：放射ブラー
	LensVignette,   // レンズ：端の暗さ
	Exposure,       // 露出
	Saturation,     // 色の調整：彩度（1 でそのまま・0 で白黒）
};

/// <summary>
/// 全部の項目の上書き（初期値は「どれも上書きしない」）
/// </summary>
struct PostOverrides {
	std::array<PostOverride, static_cast<size_t>(magic_enum::enum_count<PostParam>())> items;

	PostOverride& operator[](PostParam param) { return items[static_cast<size_t>(param)]; }
	const PostOverride& operator[](PostParam param) const { return items[static_cast<size_t>(param)]; }
	// 全体の設定の値 base に、その項目の上書きを混ぜた値
	float Apply(PostParam param, float base) const { return (*this)[param].Apply(base); }
	// その項目のどれかが効いているか
	bool HasAny(std::initializer_list<PostParam> params) const {
		return std::any_of(params.begin(), params.end(), [this](PostParam param) { return (*this)[param].weight > 0.0f; });
	}
};

/// <summary>
/// 2つの上書きの補間。t は 0〜1 に収める（行き過ぎるイージングでも、効き具合が 0〜1 の外へ出ないように）
/// </summary>
inline PostOverrides LerpPostOverrides(const PostOverrides& from, const PostOverrides& to, float t) {
	const float rate = std::clamp(t, 0.0f, 1.0f);
	PostOverrides result;
	for (size_t i = 0; i < result.items.size(); ++i) {
		result.items[i].value = from.items[i].value + (to.items[i].value - from.items[i].value) * rate;
		result.items[i].weight = from.items[i].weight + (to.items[i].weight - from.items[i].weight) * rate;
	}
	return result;
}