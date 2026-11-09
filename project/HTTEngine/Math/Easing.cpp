#define _USE_MATH_DEFINES
#include "HTTEngine/Math/Easing.h"
#include <math.h>

namespace Ease {

// ===== Quad =====

float InQuad(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (t * t);
}

float OutQuad(float t, float start, float end) {
	t = Clamp(t);
	float inv = 1.0f - t;
	return start + (end - start) * (1.0f - inv * inv);
}

float InOutQuad(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t < 0.5f) ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Cubic =====

float InCubic(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (t * t * t);
}

float OutCubic(float t, float start, float end) {
	t = Clamp(t);
	float inv = 1.0f - t;
	return start + (end - start) * (1.0f - inv * inv * inv);
}

float InOutCubic(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t < 0.5f) ? 4.0f * t * t * t : 0.5f * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
	return start + (end - start) * easedT;
}

// ===== Quart =====

float InQuart(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (t * t * t * t);
}

float OutQuart(float t, float start, float end) {
	t = Clamp(t);
	float inv = 1.0f - t;
	return start + (end - start) * (1.0f - inv * inv * inv * inv);
}

float InOutQuart(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t < 0.5f) ? 8.0f * t * t * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Quint =====

float InQuint(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (t * t * t * t * t);
}

float OutQuint(float t, float start, float end) {
	t = Clamp(t);
	float inv = 1.0f - t;
	return start + (end - start) * (1.0f - powf(inv, 5.0f));
}

float InOutQuint(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t < 0.5f) ? 16.0f * t * t * t * t * t : 1.0f - powf(fabsf(-2.0f * t + 2.0f), 5.0f) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Sine =====

float InSine(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (1.0f - cosf((t * (float)M_PI) / 2.0f));
}

float OutSine(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * sinf((t * (float)M_PI) / 2.0f);
}

float InOutSine(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (-(cosf((float)M_PI * t) - 1.0f) / 2.0f);
}

// ===== Expo =====

float InExpo(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t == 0.0f) ? 0.0f : powf(2.0f, 10.0f * (t - 1.0f));
	return start + (end - start) * easedT;
}

float OutExpo(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t == 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
	return start + (end - start) * easedT;
}

float InOutExpo(float t, float start, float end) {
	t = Clamp(t);
	float easedT;
	if (t == 0.0f)
		easedT = 0.0f;
	else if (t == 1.0f)
		easedT = 1.0f;
	else if (t < 0.5f)
		easedT = powf(2.0f, 10.0f * (2.0f * t - 1.0f)) / 2.0f;
	else
		easedT = (2.0f - powf(2.0f, -10.0f * (2.0f * t - 1.0f))) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Circ =====

float InCirc(float t, float start, float end) {
	t = Clamp(t);
	return start + (end - start) * (1.0f - sqrtf(1.0f - t * t));
}

float OutCirc(float t, float start, float end) {
	t = Clamp(t);
	float inv = t - 1.0f;
	return start + (end - start) * sqrtf(1.0f - inv * inv);
}

float InOutCirc(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t < 0.5f) ? (1.0f - sqrtf(1.0f - 4.0f * t * t)) / 2.0f : (sqrtf(1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f)) + 1.0f) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Back =====

float InBack(float t, float start, float end, float s) {
	t = Clamp(t);
	return start + (end - start) * (t * t * ((s + 1.0f) * t - s));
}

float OutBack(float t, float start, float end, float s) {
	t = Clamp(t);
	float inv = t - 1.0f;
	return start + (end - start) * (inv * inv * ((s + 1.0f) * inv + s) + 1.0f);
}

float InOutBack(float t, float start, float end, float s) {
	t = Clamp(t);
	float s2 = s * 1.525f;
	float easedT = (t < 0.5f) ? (2.0f * t * 2.0f * t * ((s2 + 1.0f) * 2.0f * t - s2)) / 2.0f : ((2.0f * t - 2.0f) * (2.0f * t - 2.0f) * ((s2 + 1.0f) * (2.0f * t - 2.0f) + s2) + 2.0f) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Bounce =====

float OutBounce(float t, float start, float end) {
	t = Clamp(t);
	float easedT;
	if (t < 1.0f / 2.75f) {
		easedT = 7.5625f * t * t;
	} else if (t < 2.0f / 2.75f) {
		t -= 1.5f / 2.75f;
		easedT = 7.5625f * t * t + 0.75f;
	} else if (t < 2.5f / 2.75f) {
		t -= 2.25f / 2.75f;
		easedT = 7.5625f * t * t + 0.9375f;
	} else {
		t -= 2.625f / 2.75f;
		easedT = 7.5625f * t * t + 0.984375f;
	}
	return start + (end - start) * easedT;
}

float InBounce(float t, float start, float end) {
	t = Clamp(t);
	float easedT = 1.0f - OutBounce(1.0f - t, 0.0f, 1.0f);
	return start + (end - start) * easedT;
}

float InOutBounce(float t, float start, float end) {
	t = Clamp(t);
	float easedT = (t < 0.5f) ? (1.0f - OutBounce(1.0f - 2.0f * t, 0.0f, 1.0f)) / 2.0f : (1.0f + OutBounce(2.0f * t - 1.0f, 0.0f, 1.0f)) / 2.0f;
	return start + (end - start) * easedT;
}

// ===== Elastic =====

float InElastic(float t, float start, float end, float amplitude, float period) {
	t = Clamp(t);
	float c = end - start;
	if (t == 0.0f)
		return start;
	if (t == 1.0f)
		return end;
	float s = (amplitude < fabsf(c)) ? (amplitude = c, period / 4.0f) : period / (2.0f * (float)M_PI) * asinf(c / amplitude);
	t -= 1.0f;
	return start + c * (-(amplitude * powf(2.0f, 10.0f * t) * sinf((t - s) * (2.0f * (float)M_PI) / period)));
}

float OutElastic(float t, float start, float end, float amplitude, float period) {
	t = Clamp(t);
	float c = end - start;
	if (t == 0.0f)
		return start;
	if (t == 1.0f)
		return end;
	float s = (amplitude < fabsf(c)) ? (amplitude = c, period / 4.0f) : period / (2.0f * (float)M_PI) * asinf(c / amplitude);
	return start + c * (amplitude * powf(2.0f, -10.0f * t) * sinf((t - s) * (2.0f * (float)M_PI) / period) + 1.0f);
}

float InOutElastic(float t, float start, float end, float amplitude, float period) {
	t = Clamp(t);
	float c = end - start;
	if (t == 0.0f)
		return start;
	if (t == 1.0f)
		return end;
	float s = (amplitude < fabsf(c)) ? (amplitude = c, period / 4.0f) : period / (2.0f * (float)M_PI) * asinf(c / amplitude);
	float easedT;
	float t2 = t * 2.0f;
	if (t2 < 1.0f) {
		t2 -= 1.0f;
		easedT = -0.5f * (amplitude * powf(2.0f, 10.0f * t2) * sinf((t2 - s) * (2.0f * (float)M_PI) / period));
	} else {
		t2 -= 1.0f;
		easedT = amplitude * powf(2.0f, -10.0f * t2) * sinf((t2 - s) * (2.0f * (float)M_PI) / period) * 0.5f + 1.0f;
	}
	return start + c * easedT;
}

// ===== Apply（Type列挙型から関数を適用） =====

float Apply(float t, Type type) {
	switch (type) {
	case Type::Linear:
		return t;
	case Type::InQuad:
		return InQuad(t, 0.0f, 1.0f);
	case Type::OutQuad:
		return OutQuad(t, 0.0f, 1.0f);
	case Type::InCubic:
		return InCubic(t, 0.0f, 1.0f);
	case Type::OutCubic:
		return OutCubic(t, 0.0f, 1.0f);
	case Type::InOutCubic:
		return InOutCubic(t, 0.0f, 1.0f);
	case Type::InSine:
		return InSine(t, 0.0f, 1.0f);
	case Type::OutSine:
		return OutSine(t, 0.0f, 1.0f);
	case Type::InOutSine:
		return InOutSine(t, 0.0f, 1.0f);
	case Type::InBack:
		return InBack(t, 0.0f, 1.0f);
	case Type::OutBack:
		return OutBack(t, 0.0f, 1.0f);
	case Type::OutBounce:
		return OutBounce(t, 0.0f, 1.0f);
	case Type::InBounce:
		return InBounce(t, 0.0f, 1.0f);
	case Type::OutElastic:
		return OutElastic(t, 0.0f, 1.0f);
	default:
		return t;
	}
}

} // namespace Ease