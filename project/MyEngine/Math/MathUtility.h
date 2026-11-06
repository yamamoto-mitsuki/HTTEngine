#pragma once
#include <cmath>
#include <numbers>

#include "MyEngine/Math/Vector2.h"
#include "MyEngine/Math/Vector3.h"
#include "MyEngine/Math/Vector4.h"
#include "MyEngine/Math/Matrix3x3.h"
#include "MyEngine/Math/Matrix4x4.h"
#include "MyEngine/Math/Transform.h"


/// <summary>
/// 数学系ユーティリティ。行列・ベクトル演算をまとめたnamespace。
/// </summary>
namespace MathUtility {
// ===== 定数 =====
// 円周率と角度の換算
inline constexpr float kPi = std::numbers::pi_v<float>; // 円周率
inline constexpr float kTwoPi = 2.0f * kPi;              // 1周（ラジアン）
inline constexpr float kDegToRad = kPi / 180.0f;         // 度 → ラジアン（std::sin・std::cos に渡すとき）
inline constexpr float kRadToDeg = 180.0f / kPi;         // ラジアン → 度（std::atan2 の答えを度にするとき）

// ===== 目盛り =====
// 目盛りの間隔。1・2・5 × 10 の n 乗 の中で、minStep 以上の一番小さい値（切りの良い数字が並ぶ）
inline float CalcNiceStep(float minStep) {
	const float base = std::pow(10.0f, std::floor(std::log10(minStep)));
	for (const float multiplier : {1.0f, 2.0f, 5.0f}) {
		if (base * multiplier >= minStep) {
			return base * multiplier;
		}
	}
	return base * 10.0f;
}

// ===== 行列生成 =====
inline Matrix4x4 MakeIdentity4x4() { return ::MakeIdentity4x4(); }
inline Matrix3x3 MakeIdentity3x3() { return ::MakeIdentity3x3(); }
inline Matrix4x4 MakeTranslateMatrix(const Vector3& translate) { return ::MakeTranslateMatrix(translate); }
inline Matrix4x4 MakeScaleMatrix(const Vector3& scale) { return ::MakeScaleMatrix(scale); }
inline Matrix4x4 MakeRotateXMatrix(float radian) { return ::MakeRotateXMatrix(radian); }
inline Matrix4x4 MakeRotateYMatrix(float radian) { return ::MakeRotateYMatrix(radian); }
inline Matrix4x4 MakeRotateZMatrix(float radian) { return ::MakeRotateZMatrix(radian); }
inline Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) { return ::MakeAffineMatrix(scale, rotate, translate); }
inline Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) { return ::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip, farClip); }
inline Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	return ::MakeOrthographicMatrix(left, top, right, bottom, nearClip, farClip);
}
inline Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) { return ::MakeViewportMatrix(left, top, width, height, minDepth, maxDepth); }
inline Matrix4x4 MakeUVTransformMatrix(const Transform& uvTransform) { return ::MakeUVTransformMatrix(uvTransform); }

// ===== 行列演算 =====
inline Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) { return ::Add(m1, m2); }
inline Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) { return ::Subtract(m1, m2); }
inline Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) { return ::Multiply(m1, m2); }
inline Matrix4x4 Inverse(const Matrix4x4& m) { return ::Inverse(m); }
inline Matrix4x4 Transpose(const Matrix4x4& m) { return ::Transpose(m); }

// ===== ベクトル変換 =====
inline Vector3 TransformPoint(const Vector3& v, const Matrix4x4& m) { return ::TransformPoint(v, m); }   // 点（位置も足す）
inline Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) { return ::TransformNormal(v, m); } // 向き（位置は足さない）

// ===== 汎用 =====
/// 値を min〜max の範囲にクランプする
inline float Clamp(float value, float min, float max) {
	if (value < min) {
		return min;
	}

	if (value > max) {
		return max;
	}

	return value;
}

/// float の線形補間
inline float Lerp(float start, float end, float t) { return start + (end - start) * t; }
/// Vector3 の線形補間
inline Vector3 Lerp(const Vector3& start, const Vector3& end, float t) {
	return {
	    start.x + (end.x - start.x) * t,
	    start.y + (end.y - start.y) * t,
	    start.z + (end.z - start.z) * t,
	};
}
/// Vector4 の線形補間（色を混ぜるときなど）
inline Vector4 Lerp(const Vector4& start, const Vector4& end, float t) { return start + (end - start) * t; }

/// なめらかに近づけるときの、1フレームに進む割合（残りの差 × これ だけ進む）。smoothTime が 0 以下なら 1（すぐ合わせる）
/// 1 - e^(-deltaTime / smoothTime)。毎フレーム決まった割合（0.2 など）にすると、フレームが多いほど速く近づいてしまう。これならフレームの速さに左右されない
/// smoothTime 秒たつと、残りの差が約 37%（e の -1 乗）になる。使い方：value = Lerp(value, target, CalcSmoothRate(deltaTime, smoothTime))
inline float CalcSmoothRate(float deltaTime, float smoothTime) { return (smoothTime > 0.0f) ? 1.0f - std::exp(-deltaTime / smoothTime) : 1.0f; }

/// current を target へ、maxDelta だけ近づける
/// 速さを「1秒に accel ずつ」目標の速さへ変えるとき：velocity = MoveTowards(velocity, target, accel * deltaTime)
inline float MoveTowards(float current, float target, float maxDelta) {
	if (target - current > maxDelta) {
		return current + maxDelta; // まだ遠い（上へ）
	}
	if (current - target > maxDelta) {
		return current - maxDelta; // まだ遠い（下へ）
	}
	return target; // 1回で届く
}

// ===== 角度（度） =====
/// from から to へ、一番近い回り方で回るときの差（度。-180〜180）。350 度から 10 度へは +20 度（-340 度ではない）
/// std::remainder(a, b) は「a から、b の一番近い整数倍を引いた余り」なので -b/2〜b/2 に収まる（std::fmod は a と同じ符号の側に収まる）
/// 向きをなめらかに回すとき：yaw += DeltaAngle(yaw, targetYaw) * CalcSmoothRate(deltaTime, smoothTime)
inline float DeltaAngle(float from, float to) { return std::remainder(to - from, 360.0f); }

/// 地面に平らな向きから、Y のまわりの角度（度）。+Z で 0、+X で 90（SetEulerAngles の Y と同じ測り方）
/// 向きの Y は使わない。X と Z が両方 0 なら 0
inline float DirectionToYaw(const Vector3& direction) { return std::atan2(direction.x, direction.z) * kRadToDeg; }

/// Y のまわりの角度（度）から、地面に平らな長さ 1 の向き（DirectionToYaw の逆）
inline Vector3 YawToDirection(float yaw) {
	const float radian = yaw * kDegToRad;
	return {std::sin(radian), 0.0f, std::cos(radian)};
}
}