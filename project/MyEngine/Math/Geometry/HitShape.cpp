#include "HitShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
constexpr float kMinAxisLength = 1.0e-6f;  // これより短い軸は、大きさ0とみなす（向きが決まらない）
constexpr float kNormalEpsilon = 1.0e-3f;  // 傾き（向き）を差で求めるときにずらす距離
constexpr int kMaxSegmentSamples = 64;     // 線分の上に並べる点の数の上限
constexpr int kGoldenIterations = 24;      // 谷を細かく探す回数（1回で区間が 0.618 倍になる。24 回で 1/10000 より細かい）
constexpr float kInvGoldenRatio = 0.6180339887f; // 黄金比の逆数（(√5 − 1) / 2）

// 行列の1行（ローカルの軸がワールドでどちらを向き、どれだけの長さか）
Vector3 RowOf(const Matrix4x4& m, int row) { return {m.m[row][0], m.m[row][1], m.m[row][2]}; }

// 長さ1にする。短すぎれば fallback
Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
	const float length = Length(v);
	return (length > kMinAxisLength) ? v / length : fallback;
}

//=============================================================================
// 床の面の形（2D の符号付き距離。p はローカルの (x, z)）
//=============================================================================
// ===== 円・ドーナツ・扇・扇のドーナツ（どれも「半径 ri〜R、前 +Z から ±a の範囲」の形）=====
// 左右が同じ形なので、x を正の側にそろえて考える。q＝(|x|, z)、φ＝q の +Z からの角度
//   φ ≦ a（開きの中）：外側の弧・内側の弧・横の辺のうち、一番近い物までの距離
//   φ ＞ a（開きの外）：横の辺（ri・e 〜 R・e の線分。e は開きの端の向き）までの距離
float CalcSectorDistance(const Vector2& p, float outerRadius, float innerRadius, float halfAngle) {
	const Vector2 q = {std::abs(p.x), p.y};
	const float radial = Length(q);
	// 弧だけを見た距離（中ならマイナス）。内側の弧は、内側の半径があるときだけ（0 なら真ん中の1点で、縁ではない）
	const float radialDistance = (innerRadius > 0.0f) ? (std::max)(radial - outerRadius, innerRadius - radial) : radial - outerRadius;
	if (halfAngle >= std::numbers::pi_v<float>) {
		return radialDistance; // 円・ドーナツ（横の辺が無い）
	}
	// 横の辺（開きの端の向き e に、内側の半径から外側の半径までの線分）までの距離
	const Vector2 edge = {std::sin(halfAngle), std::cos(halfAngle)};
	const float along = std::clamp(Dot(q, edge), innerRadius, outerRadius);
	const float edgeDistance = Length(q - edge * along);
	const float phi = std::atan2(q.x, q.y); // +Z からの角度（0〜π）
	if (phi > halfAngle) {
		return edgeDistance; // 開きの外：一番近いのは横の辺（弧の端も辺の端なので含まれる）
	}
	if (radialDistance > 0.0f) {
		return radialDistance; // 開きの中で、弧の外（外側の弧の外・内側の弧の内側）
	}
	return -(std::min)(-radialDistance, edgeDistance); // 中：一番近い縁までの距離をマイナスで
}

// ===== 線（幅 width の長方形。z = 0 から length まで）=====
float CalcLineDistance(const Vector2& p, float width, float length) {
	const Vector2 halfSize = {width * 0.5f, length * 0.5f};
	const Vector2 d = {std::abs(p.x) - halfSize.x, std::abs(p.y - halfSize.y) - halfSize.y};
	const Vector2 outside = {(std::max)(d.x, 0.0f), (std::max)(d.y, 0.0f)};
	return Length(outside) + (std::min)((std::max)(d.x, d.y), 0.0f);
}

// ===== 2D の形を、高さ height の柱にする（y = 0〜height）=====
// 横の距離 d2 と、上下の距離 dy を組み合わせる。角の外では2つの距離の三平方、中では近い方
float Extrude(float d2, float y, float height) {
	const float halfHeight = height * 0.5f;
	const float dy = std::abs(y - halfHeight) - halfHeight;
	const Vector2 outside = {(std::max)(d2, 0.0f), (std::max)(dy, 0.0f)};
	return (std::min)((std::max)(d2, dy), 0.0f) + Length(outside);
}
} // namespace


//=============================================================================
// 置く
//=============================================================================
WorldHitShape WorldHitShape::Make(const HitShape& shape, const Matrix4x4& worldMatrix) {
	WorldHitShape world;
	world.type = shape.type;
	world.origin = TransformPoint(shape.center, worldMatrix);

	// --- 向き：前を先に決めて、右・上を直角にそろえる（親の大きさが軸ごとに違って、少しゆがんでいても直角にする）---
	const Vector3 rowX = RowOf(worldMatrix, 0);
	const Vector3 rowY = RowOf(worldMatrix, 1);
	const Vector3 rowZ = RowOf(worldMatrix, 2);
	world.forward = NormalizeOr(rowZ, {0.0f, 0.0f, 1.0f});
	world.right = NormalizeOr(rowX - world.forward * Dot(rowX, world.forward), {1.0f, 0.0f, 0.0f});
	world.up = Cross(world.forward, world.right); // 前 × 右 ＝ 上

	// --- 大きさ（Entity の大きさを、軸ごとに掛ける）---
	const float scaleX = Length(rowX);
	const float scaleY = Length(rowY);
	const float scaleZ = Length(rowZ);
	const float scaleXZ = (std::max)(scaleX, scaleZ);
	switch (shape.type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
		world.radius = shape.radius * scaleXZ;
		world.innerRadius = std::clamp(shape.innerRadius, 0.0f, shape.radius) * scaleXZ;
		break;
	case HitShapeType::Line:
		world.width = shape.width * scaleX;
		world.length = shape.length * scaleZ;
		break;
	case HitShapeType::Sphere:
		world.radius = shape.radius * (std::max)(scaleXZ, scaleY);
		break;
	case HitShapeType::Capsule:
		world.radius = shape.radius * (std::max)(scaleX, scaleY);
		world.length = shape.length * scaleZ;
		break;
	}
	world.height = shape.height * scaleY;
	// 円は「開き 360 度の扇」として同じ式で扱う
	const float angle = (shape.type == HitShapeType::Sector) ? std::clamp(shape.angle, 0.0f, 360.0f) : 360.0f;
	world.halfAngle = angle * 0.5f * (std::numbers::pi_v<float> / 180.0f);
	return world;
}


//=============================================================================
// 座標
//=============================================================================
Vector3 WorldHitShape::ToLocal(const Vector3& point) const {
	const Vector3 offset = point - origin;
	return {Dot(offset, right), Dot(offset, up), Dot(offset, forward)};
}

Vector3 WorldHitShape::ToWorld(const Vector3& local) const { return origin + right * local.x + up * local.y + forward * local.z; }


//=============================================================================
// 符号付き距離
//=============================================================================
// ===== 床の面の中 =====
float WorldHitShape::CalcSignedDistance2D(const Vector2& p) const {
	switch (type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
		return CalcSectorDistance(p, radius, innerRadius, halfAngle);
	case HitShapeType::Line:
		return CalcLineDistance(p, width, length);
	case HitShapeType::Sphere:
		return Length(p) - radius; // y = 0 で切った断面
	case HitShapeType::Capsule:
		return Length(Vector2{p.x, p.y - std::clamp(p.y, 0.0f, length)}) - radius;
	}
	return 0.0f;
}

// ===== 3D =====
float WorldHitShape::CalcSignedDistance(const Vector3& world) const {
	const Vector3 p = ToLocal(world);
	switch (type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
	case HitShapeType::Line:
		return Extrude(CalcSignedDistance2D({p.x, p.z}), p.y, height);
	case HitShapeType::Sphere:
		return Length(p) - radius;
	case HitShapeType::Capsule:
		return Length(Vector3{p.x, p.y, p.z - std::clamp(p.z, 0.0f, length)}) - radius;
	}
	return 0.0f;
}

// ===== 外へ向かう向き（x・y・z をそれぞれ少しずらしたときの、距離の増え方）=====
Vector3 WorldHitShape::CalcNormal(const Vector3& world) const {
	const Vector3 dx = {kNormalEpsilon, 0.0f, 0.0f};
	const Vector3 dy = {0.0f, kNormalEpsilon, 0.0f};
	const Vector3 dz = {0.0f, 0.0f, kNormalEpsilon};
	const Vector3 gradient = {
	    CalcSignedDistance(world + dx) - CalcSignedDistance(world - dx),
	    CalcSignedDistance(world + dy) - CalcSignedDistance(world - dy),
	    CalcSignedDistance(world + dz) - CalcSignedDistance(world - dz),
	};
	return NormalizeOr(gradient, up);
}


//=============================================================================
// 重なり
//=============================================================================
// ===== 球 =====
bool WorldHitShape::Overlaps(const Sphere& sphere, float margin) const { return CalcSignedDistance(sphere.center) <= sphere.radius + margin; }

// ===== カプセル =====
bool WorldHitShape::Overlaps(const Capsule& capsule, float margin, Vector3* nearest) const {
	// 点を並べる間隔は半径まで（それより広いと、細い形の間を飛び越える）
	const Vector3 point = FindNearestOnSegment(capsule.start, capsule.end, (std::max)(capsule.radius, 0.01f));
	if (nearest != nullptr) {
		*nearest = point;
	}
	return CalcSignedDistance(point) <= capsule.radius + margin;
}

// ===== 線分の上で、形に一番近い点 =====
// 1. 線分の上に、間隔 sampleStep 以下で点を並べて距離を測る
// 2. 両隣より小さい所（谷）ごとに、両隣の間を黄金分割探索で細かく探す（形が凸なら、谷は1つで、ちょうどの答えになる）
Vector3 WorldHitShape::FindNearestOnSegment(const Vector3& start, const Vector3& end, float sampleStep) const {
	const float segmentLength = Length(end - start);
	if (segmentLength <= kMinAxisLength) {
		return start;
	}
	const int intervals = std::clamp(static_cast<int>(std::ceil(segmentLength / sampleStep)), 1, kMaxSegmentSamples - 1);
	const auto pointAt = [&](float t) { return start + (end - start) * t; };
	const auto distanceAt = [&](float t) { return CalcSignedDistance(pointAt(t)); };

	// --- 1. 並べて測る ---
	std::array<float, kMaxSegmentSamples> values{};
	for (int i = 0; i <= intervals; ++i) {
		values[i] = distanceAt(static_cast<float>(i) / static_cast<float>(intervals));
	}

	// --- 2. 谷ごとに細かく探す ---
	float bestT = 0.0f;
	float bestValue = values[0];
	for (int i = 0; i <= intervals; ++i) {
		const bool isValley = (i == 0 || values[i] <= values[i - 1]) && (i == intervals || values[i] <= values[i + 1]);
		if (!isValley) {
			continue;
		}
		// 黄金分割探索：区間 [low, high] の中の2点を比べて、大きい方の外側を捨てる。1回ごとに区間が 0.618 倍になる
		float low = static_cast<float>((std::max)(i - 1, 0)) / static_cast<float>(intervals);
		float high = static_cast<float>((std::min)(i + 1, intervals)) / static_cast<float>(intervals);
		float a = high - (high - low) * kInvGoldenRatio;
		float b = low + (high - low) * kInvGoldenRatio;
		float valueA = distanceAt(a);
		float valueB = distanceAt(b);
		for (int iteration = 0; iteration < kGoldenIterations; ++iteration) {
			if (valueA <= valueB) {
				high = b;
				b = a;
				valueB = valueA;
				a = high - (high - low) * kInvGoldenRatio;
				valueA = distanceAt(a);
			} else {
				low = a;
				a = b;
				valueA = valueB;
				b = low + (high - low) * kInvGoldenRatio;
				valueB = distanceAt(b);
			}
		}
		const float t = (valueA <= valueB) ? a : b;
		const float value = (std::min)(valueA, valueB);
		// 並べた点そのものの方が小さければ、そちら（端の点など）
		if (values[i] < value && values[i] < bestValue) {
			bestValue = values[i];
			bestT = static_cast<float>(i) / static_cast<float>(intervals);
		} else if (value < bestValue) {
			bestValue = value;
			bestT = t;
		}
	}
	return pointAt(bestT);
}
