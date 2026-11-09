#pragma once
#include "HTTEngine/Math/Matrix4x4.h"

struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

// ===== 二項演算子 =====
inline Vector4 operator+(const Vector4& a, const Vector4& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline Vector4 operator-(const Vector4& a, const Vector4& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline Vector4 operator*(const Vector4& v, float s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }
inline Vector4 operator*(float s, const Vector4& v) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }
inline Vector4 operator/(const Vector4& v, float s) { return {v.x / s, v.y / s, v.z / s, v.w / s}; }
inline Vector4 operator-(const Vector4& v) { return {-v.x, -v.y, -v.z, -v.w}; }

// ===== 複合代入演算子 =====
inline Vector4& operator+=(Vector4& a, const Vector4& b) {
	a.x += b.x;
	a.y += b.y;
	a.z += b.z;
	a.w += b.w;
	return a;
}
inline Vector4& operator-=(Vector4& a, const Vector4& b) {
	a.x -= b.x;
	a.y -= b.y;
	a.z -= b.z;
	a.w -= b.w;
	return a;
}
inline Vector4& operator*=(Vector4& v, float s) {
	v.x *= s;
	v.y *= s;
	v.z *= s;
	v.w *= s;
	return v;
}
inline Vector4& operator/=(Vector4& v, float s) {
	v.x /= s;
	v.y /= s;
	v.z /= s;
	v.w /= s;
	return v;
}

// 比較
inline bool operator==(const Vector4& a, const Vector4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
inline bool operator!=(const Vector4& a, const Vector4& b) { return !(a == b); }

// 内積
inline float Dot(const Vector4& a, const Vector4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
// 長さ
inline float Length(const Vector4& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w); }
inline float LengthSq(const Vector4& v) { return v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w; }
// 2点間の距離
inline float Distance(const Vector4& a, const Vector4& b) { return Length(b - a); }
// 正規化
inline Vector4 Normalize(const Vector4& v) {
	float len = Length(v);
	if (len < 1e-6f) {
		return {0.0f, 0.0f, 0.0f, 0.0f};
	}
	return {v.x / len, v.y / len, v.z / len, v.w / len};
}

// 線形補間
inline Vector4 Lerp(const Vector4& start, const Vector4& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return start + (end - start) * t;
}

// 行列乗算
Vector4 operator*(const Vector4& m1, const Matrix4x4& m2);