#pragma once
#include <cmath>
#include <algorithm>

struct Vector2 {
	float x;
	float y;
};

// 二項演算子
inline Vector2 operator+(const Vector2& a, const Vector2& b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 operator-(const Vector2& a, const Vector2& b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 operator*(const Vector2& a, float s) { return {a.x * s, a.y * s}; }
inline Vector2 operator*(float s, const Vector2& a) { return {a.x * s, a.y * s}; }
inline Vector2 operator/(const Vector2& a, float s) { return {a.x / s, a.y / s}; }
inline Vector2 operator-(const Vector2& a) { return {-a.x, -a.y}; }
// 複合演算子
inline Vector2& operator+=(Vector2& a, const Vector2& b) { 
	a.x += b.x;
	a.y += b.y;
	return a;
}
inline Vector2& operator-=(Vector2& a, const Vector2& b) { 
	a.x -= b.x;
	a.y -= b.y;
	return a;
}
inline Vector2& operator*=(Vector2& a, float b) { 
	a.x *= b;
	a.y *= b;
	return a;
}
inline Vector2& operator/=(Vector2& a, float b) { 
	a.x /= b;
	a.y /= b;
	return a;
}

// 比較
inline bool operator==(const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Vector2& a, const Vector2& b) { return !(a == b); }

// 内積
inline float Dot(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
// 外積
inline float Cros(const Vector2& a, const Vector2& b) { return a.x * b.y - a.y * b.x; }

// 長さ
inline float Length(const Vector2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline float LengthSq(const Vector2& v) { return v.x * v.x + v.y * v.y; }

// 2点間の距離
inline float Distance(const Vector2& a, const Vector2& b) { return Length(b - a); }

// 正規化(ゼロベクトルのときは {0,0} を返す)
inline Vector2 Normalize(const Vector2& v) {
	float len = Length(v);
	if (len < 1e-6f) {
		return {0.0f, 0.0f};
	}
	return {v.x / len, v.y / len};
}

// 線形補間
inline Vector2 Lerp(const Vector2& start, const Vector2& end, float t) { 
	t = std::clamp(t, 0.0f, 1.0f);
	return start + (end - start) * t; 
}