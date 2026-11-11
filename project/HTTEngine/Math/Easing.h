#pragma once
#include "HTTEngine/Core/EnumTips.h"

namespace Ease {

// ===== ユーティリティ =====

/// <summary>
/// イージング種別。SoundManagerのFade等で使用
/// </summary>
enum class Type {
	Linear,  // 一定速度
	InQuad,  // 二次関数で加速
	OutQuad, // 二次関数で減速
	InCubic,
	OutCubic,
	InOutCubic,
	InSine,
	OutSine,
	InOutSine,
	InBack,
	OutBack,
	OutBounce,
	InBounce,
	OutElastic,
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(Type) {
	using enum Type;
	return MakeEnumTips<Type>({
	    {Linear, "一定の速さ"},
	    {InQuad, "だんだん速く（ゆっくり始まる）"},
	    {OutQuad, "だんだん遅く（ゆっくり止まる）"},
	    {InCubic, "だんだん速く（InQuad より強く）"},
	    {OutCubic, "だんだん遅く（OutQuad より強く）"},
	    {InOutCubic, "ゆっくり始まって、ゆっくり止まる"},
	    {InSine, "だんだん速く（弱め）"},
	    {OutSine, "だんだん遅く（弱め）"},
	    {InOutSine, "ゆっくり始まって、ゆっくり止まる（弱め）"},
	    {InBack, "一度少し戻ってから進む"},
	    {OutBack, "少し行き過ぎてから戻って止まる"},
	    {OutBounce, "終わりで跳ねる（ボールが落ちて弾む）"},
	    {InBounce, "始めで跳ねる"},
	    {OutElastic, "終わりでばねのように揺れて止まる"},
	});
}

/// <summary>
/// tを0〜1にクランプする
/// </summary>
inline float Clamp(float t) {
	if (t < 0.0f)
		t = 0.0f;
	if (t > 1.0f)
		t = 1.0f;
	return t;
}

/// <summary>
/// 線形補間
/// </summary>
inline float Lerp(float start, float end, float t) { return start + (end - start) * t; }

// ===== Quad =====
float InQuad(float t, float start, float end);
float OutQuad(float t, float start, float end);
float InOutQuad(float t, float start, float end);

// ===== Cubic =====
float InCubic(float t, float start, float end);
float OutCubic(float t, float start, float end);
float InOutCubic(float t, float start, float end);

// ===== Quart =====
float InQuart(float t, float start, float end);
float OutQuart(float t, float start, float end);
float InOutQuart(float t, float start, float end);

// ===== Quint =====
float InQuint(float t, float start, float end);
float OutQuint(float t, float start, float end);
float InOutQuint(float t, float start, float end);

// ===== Sine =====
float InSine(float t, float start, float end);
float OutSine(float t, float start, float end);
float InOutSine(float t, float start, float end);

// ===== Expo =====
float InExpo(float t, float start, float end);
float OutExpo(float t, float start, float end);
float InOutExpo(float t, float start, float end);

// ===== Circ =====
float InCirc(float t, float start, float end);
float OutCirc(float t, float start, float end);
float InOutCirc(float t, float start, float end);

// ===== Back =====
float InBack(float t, float start, float end, float s = 1.70158f);
float OutBack(float t, float start, float end, float s = 1.70158f);
float InOutBack(float t, float start, float end, float s = 1.70158f);

// ===== Bounce =====
float OutBounce(float t, float start, float end);
float InBounce(float t, float start, float end);
float InOutBounce(float t, float start, float end);

// ===== Elastic =====
float InElastic(float t, float start, float end, float amplitude = 1.0f, float period = 0.3f);
float OutElastic(float t, float start, float end, float amplitude = 1.0f, float period = 0.3f);
float InOutElastic(float t, float start, float end, float amplitude = 1.0f, float period = 0.45f);

/// <summary>
/// Ease::Type 列挙型からイージング関数を適用する（0〜1の正規化された値を返す）
/// SoundManagerのFade処理など、型で分岐したいときに使う
/// </summary>
float Apply(float t, Type type);

}