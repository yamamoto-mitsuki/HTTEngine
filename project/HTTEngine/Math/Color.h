#pragma once
#include <algorithm>
#include <cmath>

#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"

// 色の書き方の変換
// Component・Inspector・Renderer の Config は、どれも float の Vector4（RGBA）で持つ（1色8ビットの 0xRRGGBBAA はやめた。1 を超える明るさを運べないので）
// 色の空間：Inspector・コードで書く色は sRGB（色見本・画像と同じ、見た目の値）。シェーダーへ送るときにリニア（光の強さに比例する値）へ直す
// シェーダーはリニアで計算して、3D は float の絵（HDR）に書く。最後にトーンマップで 0〜1 に収め、画面（_SRGB の形）へ書くときに GPU が sRGB へ戻す
// 画像も _SRGB の形で読むので、読むときにリニアになる
// 色だけ直さないと、色見本で選んだ色より明るく・薄く出る（0.2 の暗い灰色が、画面では 0.48 の灰色になる）
// 明るさ：色見本は 0〜1。1 より明るくする（光らせる）ときは、色はそのままで Intensity（倍率）を掛ける（ApplyIntensity。Unity の HDR カラーと同じ）

// A（透け方）にだけ倍率を掛ける（Alpha Group でまとめて透明にする。RGB はそのまま）
inline Vector4 MultiplyAlpha(const Vector4& color, float alpha) { return {color.x, color.y, color.z, color.w * alpha}; }

// 色と色を掛ける（RGBA をそれぞれ掛ける。白 (1,1,1,1) を掛けても変わらない。パーティクルの「初めの色 × 最後の色」）
inline Vector4 MultiplyColor(const Vector4& a, const Vector4& b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }

// RGBA を 0〜1 に収める（色見本の範囲。行き過ぎると A が負になり、Add で暗くなるので）
inline Vector4 ClampColor(const Vector4& color) { return {std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f), std::clamp(color.z, 0.0f, 1.0f), std::clamp(color.w, 0.0f, 1.0f)}; }

// ===== sRGB（見た目の値）→ リニア（シェーダーで計算する値）=====
// GPU が _SRGB の形の画像を読むときと同じ式（暗い所は直線、それより上は 2.4 乗の曲線）。0 と 1 は変わらない
// 1 を超える値（HDR の色）も、同じ曲線をそのまま伸ばして直す

// 1色分
inline float SrgbToLinear(float value) {
	if (value <= 0.04045f) {
		return value / 12.92f;
	}
	return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

// RGB（光の色・自己発光・霧の色など、A の無い色）
inline Vector3 SrgbToLinear(const Vector3& color) { return {SrgbToLinear(color.x), SrgbToLinear(color.y), SrgbToLinear(color.z)}; }

// RGBA（A は透け方の割合なので、直さない）
inline Vector4 SrgbToLinear(const Vector4& color) { return {SrgbToLinear(color.x), SrgbToLinear(color.y), SrgbToLinear(color.z), color.w}; }

// ===== リニア → sRGB（SrgbToLinear の逆。1 を超える値も同じ曲線で）=====
inline float LinearToSrgb(float value) {
	if (value <= 0.0031308f) {
		return value * 12.92f;
	}
	return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

// RGB（読み込んだマテリアルの色などを、Inspector の sRGB の欄へ写すとき）
inline Vector3 LinearToSrgb(const Vector3& color) { return {LinearToSrgb(color.x), LinearToSrgb(color.y), LinearToSrgb(color.z)}; }

// RGBA（A は透け方の割合なので、直さない）
inline Vector4 LinearToSrgb(const Vector4& color) { return {LinearToSrgb(color.x), LinearToSrgb(color.y), LinearToSrgb(color.z), color.w}; }

// ===== 明るさの倍率（Intensity）を掛けた色（HDR の色）=====
// 色は 0〜1 に収めてから、リニア（光の強さ）で intensity 倍にして、sRGB の曲線で書き直す
// Renderer は Config の色を SrgbToLinear で直すので、シェーダーに届くのはちょうど「色のリニア × intensity」
//   例：白 × Intensity 3 → Config の色は (1.61, 1.61, 1.61)、シェーダーでは (3, 3, 3)。1 を超えた分が、ブルームの Threshold 1 で光る
//   sRGB のまま 3 を掛けてはいけない（(3, 3, 3) を SrgbToLinear に通すと 12.8 になる。曲線は 1 の上でも 2.4 乗なので）
// A（透け方）は倍率を掛けず、0〜1 に収めるだけ
inline Vector4 ApplyIntensity(const Vector4& color, float intensity) {
	const Vector4 clamped = ClampColor(color);
	// 1 倍なら曲線を通さない（行き来すると、小数の誤差で値が少しずれるので）
	if (intensity == 1.0f) {
		return clamped;
	}
	// 0 以下は真っ黒（負の明るさは無い）
	if (intensity <= 0.0f) {
		return {0.0f, 0.0f, 0.0f, clamped.w};
	}
	return {
	    LinearToSrgb(SrgbToLinear(clamped.x) * intensity),
	    LinearToSrgb(SrgbToLinear(clamped.y) * intensity),
	    LinearToSrgb(SrgbToLinear(clamped.z) * intensity),
	    clamped.w,
	};
}