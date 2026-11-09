#pragma once
#include <cmath>
#include <algorithm>

struct Vector3 {
	float x;
	float y;
	float z;
};

// ===== 二項演算子 =====
inline Vector3 operator+(const Vector3& a, const Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 operator-(const Vector3& a, const Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 operator*(const Vector3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline Vector3 operator*(float s, const Vector3& v) { return {v.x * s, v.y * s, v.z * s}; }
inline Vector3 operator/(const Vector3& v, float s) { return {v.x / s, v.y / s, v.z / s}; }
inline Vector3 operator-(const Vector3& v) { return {-v.x, -v.y, -v.z}; }

// ===== 複合代入演算子 =====
inline Vector3& operator+=(Vector3& a, const Vector3& b) {
	a.x += b.x;
	a.y += b.y;
	a.z += b.z;
	return a;
}
inline Vector3& operator-=(Vector3& a, const Vector3& b) {
	a.x -= b.x;
	a.y -= b.y;
	a.z -= b.z;
	return a;
}
inline Vector3& operator*=(Vector3& v, float s) {
	v.x *= s;
	v.y *= s;
	v.z *= s;
	return v;
}
inline Vector3& operator/=(Vector3& v, float s) {
	v.x /= s;
	v.y /= s;
	v.z /= s;
	return v;
}

// 比較
inline bool operator==(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!=(const Vector3& a, const Vector3& b) { return !(a == b); }

// 内積
inline float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// 外積
inline Vector3 Cross(const Vector3& a, const Vector3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// 長さ
inline float Length(const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline float LengthSq(const Vector3& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

// 2点間の距離
inline float Distance(const Vector3& a, const Vector3& b) { return Length(b - a); }

// 正規化
inline Vector3 Normalize(const Vector3& v) {
	float len = Length(v);
	if (len < 1e-6f) {
		return {0.0f, 0.0f, 0.0f};
	}
	return {v.x / len, v.y / len, v.z / len};
}

// 線形補間
inline Vector3 Lerp(const Vector3& start, const Vector3& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return start + (end - start) * t;
}
