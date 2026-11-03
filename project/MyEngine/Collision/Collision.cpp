#include "MyEngine/Collision/Collision.h"

#include <array>
#include <cmath>
#include <cfloat>
#include <chrono>
#include <span>
#include <utility>
#include <algorithm>

#include "MyEngine/Collision/Profiling/CollisionProfiler.h"



// COLLISION_PROFILING を定義している間だけ計測する
#ifdef _DEBUG
struct ScopeTimer_ {
	const char* name_;
	std::chrono::high_resolution_clock::time_point start_;
	explicit ScopeTimer_(const char* n) : name_(n), start_(std::chrono::high_resolution_clock::now()) {}
	~ScopeTimer_() {
		double ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start_).count();
		CollisionProfiler::Record(name_, ms);
	}
};
#define COLLISION_PROFILE() ScopeTimer_ _t_(__FUNCTION__)
#endif

namespace Collision {
//=============================================================================
// 点の判定
//=============================================================================
bool IsPointInSphere(const Vector3& point, const Sphere& sphere) { return LengthSq(point - sphere.center) <= sphere.radius * sphere.radius; }

bool IsPointInAABB(const Vector3& point, const AABB& aabb) {
	return point.x >= aabb.min.x && point.x <= aabb.max.x && // x
		   point.y >= aabb.min.y && point.y <= aabb.max.y && // y
		   point.z >= aabb.min.z && point.z <= aabb.max.z;   // z
}

//=============================================================================
// 形状同士
//=============================================================================
// ===== 球同士 =====
bool SphereSphere(const Sphere& a, const Sphere& b) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	float r = a.radius + b.radius;
	return LengthSq(a.center - b.center) <= r * r;
}

// ===== AABB同士 =====
bool AABBAABB(const AABB& a, const AABB& b) { 
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	return a.min.x <= b.max.x && a.max.x >= b.min.x && 
		   a.min.y <= b.max.y && a.max.y >= b.min.y && 
		   a.min.z <= b.max.z && a.max.z >= b.min.z; 
}

// ===== 球とAABB =====
bool SphereAABB(const Sphere& sphere, const AABB& aabb) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	Vector3 closest = ClosestPointOnAABB(sphere.center, aabb);
	return LengthSq(sphere.center - closest) <= sphere.radius * sphere.radius;
}

// ===== カプセルと球 =====
bool CapsuleSphere(const Capsule& capsule, const Sphere& sphere) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	const Vector3 closest = ClosestPointOnSegment(sphere.center, capsule.start, capsule.end);
	const float radiusSum = capsule.radius + sphere.radius;
	return LengthSq(sphere.center - closest) <= radiusSum * radiusSum;
}

// ===== カプセルと箱 =====
bool CapsuleAABB(const Capsule& capsule, const AABB& aabb) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	// 箱を半径ぶん広げ、線分（start → end）がその箱を通るかで調べる
	// 本当は角が丸い形（角では少し小さい）だが、広げた箱で近似する。角の近くだけ少し大きめに当たる
	const Vector3 grow = {capsule.radius, capsule.radius, capsule.radius};
	const AABB expanded = {aabb.min - grow, aabb.max + grow};
	// 向きを長さ1にしないレイにすると、t が 0〜1 の間＝線分の上で当たったことになる
	// start が箱の中なら RayAABB は t=0 で当たりを返す。start と end が同じ点（動いていない）でも、箱の中なら当たり
	const Ray ray = {capsule.start, capsule.end - capsule.start};
	float t = 0.0f;
	return RayAABB(ray, expanded, &t) && t <= 1.0f;
}


//=============================================================================
// 押し出し（重なった物を、一番短い距離で外へ出す）
//=============================================================================
namespace {
constexpr float kPushEpsilon = 1e-6f; // これより浅い重なりは、重なっていないことにする（触れているだけ）
// Vector3 の x・y・z を、番号（0・1・2）で読む・書く（軸ごとに同じ計算を3回書かないため）
float GetAxis(const Vector3& v, int axis) { return (axis == 0) ? v.x : ((axis == 1) ? v.y : v.z); }
void SetAxis(Vector3& v, int axis, float value) {
	if (axis == 0) {
		v.x = value;
	} else if (axis == 1) {
		v.y = value;
	} else {
		v.z = value;
	}
}
// その軸へ押してよいか
bool IsFreeAxis(AxisLock lock, int axis) { return !((axis == 0) ? lock.x : ((axis == 1) ? lock.y : lock.z)); }
// 箱の中にある点（半径 radius の球の中心）を、一番近い面から外へ出す。押してよい軸だけを見る
bool PushPointOutOfAABB(const Vector3& point, float radius, const AABB& box, Vector3* push, AxisLock lock) {
	int bestAxis = -1;
	float best = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		if (!IsFreeAxis(lock, axis)) {
			continue;
		}
		const float toMin = GetAxis(point, axis) - GetAxis(box.min, axis) + radius; // −側の面の外へ出すのに要る距離
		const float toMax = GetAxis(box.max, axis) - GetAxis(point, axis) + radius; // ＋側の面の外へ出すのに要る距離
		const float distance = (toMin < toMax) ? -toMin : toMax;                    // 近い方の面へ（向き付き）
		if (bestAxis < 0 || std::abs(distance) < std::abs(best)) {
			bestAxis = axis;
			best = distance;
		}
	}
	if (bestAxis < 0) {
		return false; // 全部の軸を止めてある
	}
	*push = {0.0f, 0.0f, 0.0f};
	SetAxis(*push, bestAxis, best);
	return true;
}
} // namespace

// ===== 球を球から =====
bool PushOutSphereSphere(const Sphere& a, const Sphere& b, Vector3* push, AxisLock lock) {
	const Vector3 difference = a.center - b.center; // b の中心から a の中心へ
	const float radiusSum = a.radius + b.radius;
	const float distanceSq = LengthSq(difference);
	if (distanceSq >= radiusSum * radiusSum) {
		return false;
	}
	const float depth = radiusSum - std::sqrt(distanceSq); // 重なっている深さ
	if (depth <= kPushEpsilon) {
		return false;
	}
	// 押す向き：b の中心から a の中心へ（止めた軸は使わない）
	Vector3 direction = RemoveLockedAxes(difference, lock);
	if (LengthSq(direction) < kPushEpsilon * kPushEpsilon) {
		// 中心が同じ（または止めた軸の向きにしかずれていない）：押してよい軸のどれかへ（上を先に）
		direction = {0.0f, 0.0f, 0.0f};
		for (const int axis : {1, 0, 2}) {
			if (IsFreeAxis(lock, axis)) {
				SetAxis(direction, axis, 1.0f);
				break;
			}
		}
		if (LengthSq(direction) == 0.0f) {
			return false; // 全部の軸を止めてある
		}
	}
	*push = Normalize(direction) * depth;
	return true;
}

// ===== 球を箱から =====
bool PushOutSphereAABB(const Sphere& a, const AABB& b, Vector3* push, AxisLock lock) {
	const Vector3 closest = ClosestPointOnAABB(a.center, b); // 箱の中で、球の中心に一番近い点
	const Vector3 difference = a.center - closest;           // その点から球の中心へ
	const float distanceSq = LengthSq(difference);
	if (distanceSq >= a.radius * a.radius) {
		return false;
	}
	// 中心が箱の中（一番近い点＝中心）：一番近い面から外へ
	const Vector3 direction = RemoveLockedAxes(difference, lock);
	if (distanceSq < kPushEpsilon * kPushEpsilon || LengthSq(direction) < kPushEpsilon * kPushEpsilon) {
		return PushPointOutOfAABB(a.center, a.radius, b, push, lock);
	}
	// 中心が箱の外：一番近い点から離れる向きへ、重なった深さだけ
	const float depth = a.radius - std::sqrt(distanceSq);
	if (depth <= kPushEpsilon) {
		return false;
	}
	*push = Normalize(direction) * depth;
	return true;
}

// ===== 箱を球から（球を箱から出す向きの、反対へ）=====
bool PushOutAABBSphere(const AABB& a, const Sphere& b, Vector3* push, AxisLock lock) {
	if (!PushOutSphereAABB(b, a, push, lock)) {
		return false;
	}
	*push = -*push;
	return true;
}

// ===== 球を、多角形を押し出した形から =====
//   中心が形の外：形の表面で一番近い点から、中心へ向かう向きに「半径 − 距離」だけ（箱の PushOutSphereAABB と同じ考え）
//                 表面で一番近い点 ＝ X・Y は「中なら中心のまま／外なら一番近い辺の点」、Z は奥行きの範囲に収めた所
//   中心が形の中：横の辺・前の面・後ろの面のうち、一番近い所から外へ「そこまでの距離 ＋ 半径」だけ（止めた軸の向きは使わない）
//
//   三角形に分けずに、外周の辺と「中か外か」だけで調べるので、へこんだ形でも、分けた所の線（本当は無い辺）で引っかからない
bool PushOutSpherePolygon(const Sphere& a, const ExtrudedPolygon& b, Vector3* push, AxisLock lock) {
	if (b.count < 3) {
		return false; // 形が無い
	}
	const float radius = a.radius + b.radius; // 多角形を太らせた分も、球の半径に足して調べる（同じこと）
	// --- 多角形の点は origin から見た位置なので、球の中心も origin から見る ---
	const std::span<const Vector2> points = b.GetPoints();
	const Vector2 center = {a.center.x - b.origin.x, a.center.y - b.origin.y};
	const float z = a.center.z - b.origin.z; // 奥行きの真ん中から見た Z（−halfDepth が前の面、＋halfDepth が後ろの面）
	size_t edge = 0;                         // 一番近い辺の番号
	const Vector2 closest = Polygon2D::ClosestPointOnEdges(center, points, &edge);
	const bool isInsideXY = Polygon2D::IsPointInside(center, points);
	const bool isInsideZ = std::abs(z) <= b.halfDepth;

	// ===== 中心が形の外 =====
	if (!isInsideXY || !isInsideZ) {
		const Vector2 nearXY = isInsideXY ? center : closest;
		const float nearZ = std::clamp(z, -b.halfDepth, b.halfDepth);
		const Vector3 difference = {center.x - nearXY.x, center.y - nearXY.y, z - nearZ}; // 表面の点から中心へ
		const float distanceSq = LengthSq(difference);
		if (distanceSq >= radius * radius) {
			return false; // 半径より離れている
		}
		const float distance = std::sqrt(distanceSq);
		const Vector3 direction = RemoveLockedAxes(difference, lock); // 止めた軸へは押さない
		if (distance > kPushEpsilon && LengthSq(direction) >= kPushEpsilon * kPushEpsilon) {
			const float depth = radius - distance; // 重なっている深さ
			if (depth <= kPushEpsilon) {
				return false;
			}
			*push = Normalize(direction) * depth;
			return true;
		}
		// 中心がちょうど表面の上（向きが決まらない）・押せない向きだった：下の「中」と同じく、一番近い所から出す
	}

	// ===== 中心が形の中：横の辺・前の面・後ろの面のうち、押してよい向きで一番近い所から =====
	Vector3 bestDirection = {0.0f, 0.0f, 0.0f};
	float bestDepth = -1.0f; // まだ無い
	const auto consider = [&](const Vector3& direction, float depth) {
		const Vector3 free = RemoveLockedAxes(direction, lock);
		if (LengthSq(free) < kPushEpsilon * kPushEpsilon) {
			return; // 止めた軸の向きしか無い（押せない）
		}
		if (bestDepth < 0.0f || depth < bestDepth) {
			bestDirection = Normalize(free);
			bestDepth = depth;
		}
	};
	// 横の辺：中心から一番近い辺の点へ。中心がちょうど辺の上（距離 0）なら、その辺の外向き
	const Vector2 toEdge = closest - center;
	const float edgeDistance = Length(toEdge);
	const Vector2 edgeDirection = (edgeDistance > kPushEpsilon) ? toEdge / edgeDistance : Polygon2D::EdgeOutwardNormal(points, edge);
	consider({edgeDirection.x, edgeDirection.y, 0.0f}, edgeDistance + radius);
	consider({0.0f, 0.0f, -1.0f}, z + b.halfDepth + radius); // 前の面（−Z）から
	consider({0.0f, 0.0f, 1.0f}, b.halfDepth - z + radius);  // 後ろの面（＋Z）から
	if (bestDepth <= kPushEpsilon) {
		return false; // 全部の軸を止めてある
	}
	*push = bestDirection * bestDepth;
	return true;
}

// ===== 多角形を球から（球を多角形から出す向きの、反対へ）=====
bool PushOutPolygonSphere(const ExtrudedPolygon& a, const Sphere& b, Vector3* push, AxisLock lock) {
	if (!PushOutSpherePolygon(b, a, push, lock)) {
		return false;
	}
	*push = -*push;
	return true;
}

// ===== カプセルを、多角形を押し出した形から =====
//   カプセルは線分を半径で太らせた形なので、線分の上の「一番深く入っている点」に置いた球を出せばよい
//   その点の候補（線分の上の位置 t。0 が start、1 が end）：
//     ・線分の両端
//     ・多角形の点ごとに、線分の上で一番近い所（外にあるとき、角に一番近づく所）
//     ・線分が辺をまたぐ所（X・Y で見て。表面にちょうど触れる所）
//   外にある間の距離は、どれかの候補で一番短くなる。候補ごとに球を出してみて、一番大きく出す物を使う
//   （囲む箱で1点だけ選ぶと、坂に立つカプセルでは足元より上の点を選んでしまい、足が坂に沈む）
bool PushOutCapsulePolygon(const Capsule& a, const ExtrudedPolygon& b, Vector3* push, AxisLock lock) {
	if (b.count < 3) {
		return false; // 形が無い
	}
	const std::span<const Vector2> points = b.GetPoints();
	const Vector3 direction = a.end - a.start;
	const float lengthSq = LengthSq(direction);

	// --- 候補の t を集める（両端・点の数・辺の数。kMaxPolygonPoints が上限なので配列で足りる）---
	std::array<float, kMaxPolygonPoints * 2 + 2> candidates{};
	size_t count = 0;
	candidates[count++] = 0.0f;
	candidates[count++] = 1.0f;
	if (lengthSq > kPushEpsilon * kPushEpsilon) {
		// 点に一番近い所：点を線分へ落とした位置（線分の外なら端）
		const float nearZ = std::clamp(a.start.z, b.origin.z - b.halfDepth, b.origin.z + b.halfDepth);
		for (const Vector2& point : points) {
			const Vector3 vertex = {b.origin.x + point.x, b.origin.y + point.y, nearZ};
			candidates[count++] = std::clamp(Dot(vertex - a.start, direction) / lengthSq, 0.0f, 1.0f);
		}
		// 辺をまたぐ所：線分と辺を X・Y で見た、2本の線分の交わる所（2次元の外積で解く）
		//   線分 P(t) = s + d·t、辺 Q(u) = e0 + e·u。P(t) = Q(u) を解くと t = (e0 − s)×e ÷ (d×e)、u = (e0 − s)×d ÷ (d×e)
		const Vector2 start = {a.start.x - b.origin.x, a.start.y - b.origin.y};
		const Vector2 segment = {direction.x, direction.y};
		const auto cross = [](const Vector2& p, const Vector2& q) { return p.x * q.y - p.y * q.x; };
		for (size_t i = 0; i < points.size(); ++i) {
			const Vector2& edgeStart = points[i];
			const Vector2 edge = points[(i + 1) % points.size()] - edgeStart;
			const float denominator = cross(segment, edge);
			if (std::abs(denominator) < kPushEpsilon) {
				continue; // 平行（またがない。近い所は点の候補で見ている）
			}
			const Vector2 toEdge = edgeStart - start;
			const float t = cross(toEdge, edge) / denominator;
			const float u = cross(toEdge, segment) / denominator;
			if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
				candidates[count++] = t;
			}
		}
	}

	// --- 候補ごとに球を出してみて、一番大きく出す物を使う ---
	bool isHit = false;
	float bestSq = 0.0f;
	for (size_t i = 0; i < count; ++i) {
		const Sphere sphere = {a.start + direction * candidates[i], a.radius};
		Vector3 candidatePush;
		if (!PushOutSpherePolygon(sphere, b, &candidatePush, lock)) {
			continue;
		}
		const float pushSq = LengthSq(candidatePush);
		if (!isHit || pushSq > bestSq) {
			*push = candidatePush;
			bestSq = pushSq;
			isHit = true;
		}
	}
	return isHit;
}

// ===== 多角形をカプセルから（カプセルを多角形から出す向きの、反対へ）=====
bool PushOutPolygonCapsule(const ExtrudedPolygon& a, const Capsule& b, Vector3* push, AxisLock lock) {
	if (!PushOutCapsulePolygon(b, a, push, lock)) {
		return false;
	}
	*push = -*push;
	return true;
}

// ===== 箱を箱から =====
bool PushOutAABBAABB(const AABB& a, const AABB& b, Vector3* push, AxisLock lock) {
	// 軸ごとに「−側へ出す距離」「＋側へ出す距離」を出し、押してよい軸の中で一番短い物を使う
	int bestAxis = -1;
	float best = 0.0f;
	for (int axis = 0; axis < 3; ++axis) {
		const float toNegative = GetAxis(a.max, axis) - GetAxis(b.min, axis); // a を −側へ出すのに要る距離
		const float toPositive = GetAxis(b.max, axis) - GetAxis(a.min, axis); // a を ＋側へ出すのに要る距離
		if (toNegative <= kPushEpsilon || toPositive <= kPushEpsilon) {
			return false; // 1つの軸でも離れていれば（触れているだけでも）、重なっていない
		}
		if (!IsFreeAxis(lock, axis)) {
			continue;
		}
		const float distance = (toNegative < toPositive) ? -toNegative : toPositive;
		if (bestAxis < 0 || std::abs(distance) < std::abs(best)) {
			bestAxis = axis;
			best = distance;
		}
	}
	if (bestAxis < 0) {
		return false; // 全部の軸を止めてある
	}
	*push = {0.0f, 0.0f, 0.0f};
	SetAxis(*push, bestAxis, best);
	return true;
}


//=============================================================================
// レイキャスト
//=============================================================================
// ===== 球 =====
bool RaySphere(const Ray& ray, const Sphere& sphere, float* t) {
	// 直線（Rayの軌跡）の式 StartPos + dist * Direction と 球の式 X^2 + Y^2 + Z^2 = r^2 を変化させる
	// 
	// r^2を左辺に移動させると
	// X^2 + Y^2 + Z^2 - r^2 = 0
	// 球の式に直線の式を合わせると
	// X: StartPos.x + dist * Direction.x
	// Y: StartPos.y + dist * Direction.y
	// Z: StartPos.z + dist * Direction.z
	// (StartPos.x + dist * Direction.x)^2 + (StartPos.y + dist * Direction.y)^2 + (StartPos.z + dist * Direction.z)^2 - r^2 = 0
	// 
	// ここでStartPos...P, dist...t, Direction...D とする
	// (P.x + t * D.x)^2を展開すると
	// (P.x)^2 + (2 * P.x * t * D.x) + (t * D.x)^2 のようになる。y,zも展開する。
	// すると t^2 , t が発生するので
	// (D.x^2 + D.y^2 + D.z^2) * t^2 + 2 * (P.x * D.x + P.y * D.y + P.z * D.z) * t + (P.x^2 + P.y^2 + P.z^2 - r^2) = 0
	// 
	// よって ax^2 + bx + c = 0 という二次方程式になる。
	// そして二次方程式の解 √b^2 - 4ac という中身を求めて - なら √ が成立しないので false を返す
	//
	// t (dist) は「当たるまでの時間(距離)」で、まだ未知数だから前半の係数計算には登場しない。
	// 光線は前方へ無限の長さで伸びている前提（後からtの長さで制限可能）。
	//
	// a = dirの長さ^2。バグで単位ベクトル以外が来ても壊れないための安全弁。
	// b = 2 * Dot(oc, dir)。(P + tD)^2 を展開した時の公式の「2ab」の2。
	// c = ocの長さ^2 - 半径^2。
	//
	// disc (判別式: b^2 - 4ac) は解の公式の「ルートの中身」。
	// マイナスなら現実世界に答えがない（＝外れた）ので、std::sqrtを計算する前に秒でreturnする。

#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	Vector3 oc = ray.origin - sphere.center; 
	float a = Dot(ray.direction, ray.direction);
	float b = 2.0f * Dot(oc, ray.direction);
	float c = Dot(oc, oc) - sphere.radius * sphere.radius;
	float disc = b * b - 4.0f * a * c;
	if (disc < 0.0f) { return false; }
	if (t) { *t = (-b - std::sqrtf(disc)) / (2.0f * a); }
	return true;
}

// ===== AABB =====
bool RayAABB(const Ray& ray, const AABB& aabb, float* t) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	// スラブ法
	float tmin = 0.0f;
	float tmax = 1e30f;

	const float* org  = &ray.origin.x;
	const float* dir  = &ray.direction.x;
	const float* bmin = &aabb.min.x;
	const float* bmax = &aabb.max.x;

	for (int i = 0; i < 3; ++i) {
		if (std::abs(dir[i]) < 1e-8f) {
			// rayの速度（単位ベクトルの長さ）が0に近く、AABBの幅にないなら終了
			if (org[i] < bmin[i] || org[i] > bmax[i]) {
				return false;
			}
		} else {
			// 距離 / 速さ = 時間
			float t1 = (bmin[i] - org[i]) / dir[i];
			float t2 = (bmax[i] - org[i]) / dir[i];
			// 逆方向から飛んできてたら入れ替える
			if (t1 > t2) {
				std::swap(t1, t2);
			}
			// 入口: 一番遅い時間に更新
			// 出口: 一番速い時間に更新
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax) {
				return false;
			}
		}
	}
	if (t) {
		*t = tmin;
	}
	return true;
}

// ===== 平面 =====
bool RayPlane(const Ray& ray, const Plane& plane, float* t) { 
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	float denom = Dot(plane.normal, ray.direction); 
	if (std::abs(denom) < 1e-8f) {
		return false;
	}
	float tVal = (plane.distance - Dot(plane.normal, ray.origin)) / denom;
	if (tVal < 0.0f) {
		return false;
	}
	if (t) {
		*t = tVal;
	}
	return true;
}

// ===== カプセル =====
// カプセル＝「横の面（円柱）」＋「上下の端の球」。それぞれに当てて、一番手前の当たりを使う
bool RayCapsule(const Ray& ray, const Capsule& capsule, float* t) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	// --- 始点がカプセルの中（線分までの距離が半径以下）なら、距離0で当たり ---
	const Vector3 nearest = ClosestPointOnSegment(ray.origin, capsule.start, capsule.end);
	if (LengthSq(ray.origin - nearest) <= capsule.radius * capsule.radius) {
		if (t) {
			*t = 0.0f;
		}
		return true;
	}

	bool isHit = false;
	float best = 0.0f; // 今までで一番手前の当たり
	// 前（0以上）にあって、今までより手前なら残す
	const auto consider = [&isHit, &best](float candidate) {
		if (candidate >= 0.0f && (!isHit || candidate < best)) {
			best = candidate;
			isHit = true;
		}
	};

	// --- 上下の端の球（RaySphere は近い方の交点を返す。始点は外なので、後ろの球ならマイナスになって consider で外れる）---
	float sphereT = 0.0f;
	if (RaySphere(ray, {capsule.start, capsule.radius}, &sphereT)) {
		consider(sphereT);
	}
	if (RaySphere(ray, {capsule.end, capsule.radius}, &sphereT)) {
		consider(sphereT);
	}

	// --- 横の面（円柱）：線分の向きの成分を取り除くと「円と直線」の交わりになる（RaySphere と同じ2次方程式）---
	const Vector3 axis = capsule.end - capsule.start;
	const float axisLength = Length(axis);
	if (axisLength > 1e-6f) {
		const Vector3 unitAxis = axis / axisLength;
		const Vector3 toOrigin = ray.origin - capsule.start;
		const Vector3 side = toOrigin - unitAxis * Dot(toOrigin, unitAxis);                    // 始点の、軸からの横のずれ
		const Vector3 sideDirection = ray.direction - unitAxis * Dot(ray.direction, unitAxis); // レイの向きの、横の成分
		const float a = Dot(sideDirection, sideDirection);
		if (a > 1e-12f) { // 軸と平行なレイは横の面に当たらない（端の球で当たる）
			const float b = 2.0f * Dot(side, sideDirection);
			const float c = Dot(side, side) - capsule.radius * capsule.radius;
			const float disc = b * b - 4.0f * a * c;
			if (disc >= 0.0f) {
				const float cylinderT = (-b - std::sqrt(disc)) / (2.0f * a);
				// 当たった所が、線分の長さの中（0〜長さ）なら横の面。外なら端の球の所なので、上で調べた
				const float along = Dot(toOrigin + ray.direction * cylinderT, unitAxis);
				if (along >= 0.0f && along <= axisLength) {
					consider(cylinderT);
				}
			}
		}
	}

	if (isHit && t) {
		*t = best;
	}
	return isHit;
}

// ===== 多角形を押し出した形 =====
// 1. 奥行き（Z）の間にいる t の範囲（tEnter〜tExit）を出す（RayAABB のスラブ法の、Z だけ版）
// 2. 入った所（tEnter）が、横から見て多角形の中なら、そこで当たる（前の面・後ろの面。始点が中なら 0）
// 3. 中でなければ、横から見てレイが辺を横切る所のうち、tEnter〜tExit の間で一番手前（横の面）
bool RayPolygon(const Ray& ray, const ExtrudedPolygon& polygon, float* t) {
#ifdef _DEBUG
	COLLISION_PROFILE();
#endif
	if (polygon.count < 3) {
		return false;
	}
	// --- 1. 奥行きの間 ---
	const float minZ = polygon.origin.z - polygon.halfDepth;
	const float maxZ = polygon.origin.z + polygon.halfDepth;
	float tEnter = 0.0f;
	float tExit = FLT_MAX;
	if (std::abs(ray.direction.z) < 1e-8f) {
		if (ray.origin.z < minZ || ray.origin.z > maxZ) {
			return false; // Z に動かないレイが、奥行きの外を通る
		}
	} else {
		float t1 = (minZ - ray.origin.z) / ray.direction.z;
		float t2 = (maxZ - ray.origin.z) / ray.direction.z;
		if (t1 > t2) {
			std::swap(t1, t2); // 奥から手前へ向かうレイ
		}
		tEnter = (std::max)(t1, 0.0f);
		tExit = t2;
		if (tEnter > tExit) {
			return false; // 奥行きの間は、レイの後ろ
		}
	}
	// --- 2. 前の面・後ろの面（入った所が中か）---
	const std::span<const Vector2> points = polygon.GetPoints();
	const Vector2 origin = {ray.origin.x - polygon.origin.x, ray.origin.y - polygon.origin.y}; // 横から見た始点（多角形の origin から）
	const Vector2 direction = {ray.direction.x, ray.direction.y};                              // 横から見た向き
	if (Polygon2D::IsPointInside(origin + direction * tEnter, points)) {
		if (t) {
			*t = tEnter;
		}
		return true;
	}
	// --- 3. 横の面：origin + direction × t ＝ a + 辺 × s を解く（t がレイの上の位置、s が辺の上の位置 0〜1）---
	// 両辺に「辺との外積」を取ると s の項が消えて t が出る。「向きとの外積」を取ると t が消えて s が出る
	bool isHit = false;
	float nearest = tExit;
	for (size_t i = 0; i < points.size(); ++i) {
		const Vector2& a = points[i];
		const Vector2 edge = points[(i + 1) % points.size()] - a;
		const float denominator = Cros(direction, edge);
		if (std::abs(denominator) < 1e-12f) {
			continue; // 辺と平行（横から見て、交わらない）
		}
		const Vector2 toA = a - origin;
		const float edgeT = Cros(toA, edge) / denominator;
		const float s = Cros(toA, direction) / denominator;
		if (s < 0.0f || s > 1.0f || edgeT < tEnter || edgeT > nearest) {
			continue; // 辺の外・奥行きの外・もっと手前で当たっている
		}
		nearest = edgeT;
		isHit = true;
	}
	if (isHit && t) {
		*t = nearest;
	}
	return isHit;
}

//=============================================================================
// ユーティリティ
//=============================================================================
Vector3 RemoveLockedAxes(const Vector3& vector, AxisLock lock) {
	Vector3 result = vector;
	if (lock.x) {
		result.x = 0.0f;
	}
	if (lock.y) {
		result.y = 0.0f;
	}
	if (lock.z) {
		result.z = 0.0f;
	}
	return result;
}

Vector3 ClosestPointOnAABB(const Vector3& point, const AABB& aabb) {
	return {
		std::max(aabb.min.x, std::min(point.x, aabb.max.x)), 
		std::max(aabb.min.y, std::min(point.y, aabb.max.y)), 
		std::max(aabb.min.z, std::min(point.z, aabb.max.z))
	};
}

Vector3 RayPoint(const Ray& ray, float t) { return ray.origin + ray.direction * t; }

Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& start, const Vector3& end) {
	// 線分の向き ab に point を射影した長さの割合 t（0 で start、1 で end）を、0〜1 に収める
	const Vector3 ab = end - start;
	const float lengthSq = LengthSq(ab);
	if (lengthSq < 1e-12f) {
		return start; // 長さ0の線分（点）
	}
	float t = Dot(point - start, ab) / lengthSq;
	t = std::clamp(t, 0.0f, 1.0f);
	return start + ab * t;
}

void ClosestPointsBetweenSegments(const Vector3& aStart, const Vector3& aEnd, const Vector3& bStart, const Vector3& bEnd, Vector3* onA, Vector3* onB) {
	// 1. 2本を「両側に無限に伸びた直線」として、一番近づく所を a の割合 s（0 で aStart、1 で aEnd）で出し、0〜1 に収める
	const Vector3 da = aEnd - aStart;
	const Vector3 db = bEnd - bStart;
	const Vector3 r = aStart - bStart;
	const float aa = Dot(da, da);
	const float bb = Dot(db, db);
	const float ab = Dot(da, db);
	const float denom = aa * bb - ab * ab; // |da|²|db|²sin²θ。2本が平行（または点）に近いほど0に近い
	float s = 0.0f;                        // 平行なら、どこでも同じ近さなので a の始点から
	if (denom > 1e-6f * aa * bb) {
		s = std::clamp((ab * Dot(db, r) - Dot(da, r) * bb) / denom, 0.0f, 1.0f);
	}
	// 2. a のその点に一番近い b の点（b の端に収まることがある）
	*onB = ClosestPointOnSegment(aStart + da * s, bStart, bEnd);
	// 3. その b の点に一番近い a の点を探し直す（2 で b の端に収めたとき、a の一番近い点もずれるので）
	*onA = ClosestPointOnSegment(*onB, aStart, aEnd);
}

namespace {
constexpr int kSegmentSearchSteps = 32; // 三分探索の回数（1回で範囲が 2/3 になる。32回で 100万分の2くらい）

// 点から箱までの「符号付きの距離」。外なら箱までの距離（＋）、中なら一番近い面までの距離に − を付けた物
float SignedDistanceToAABB(const Vector3& point, const AABB& aabb) {
	const Vector3 closest = ClosestPointOnAABB(point, aabb);
	const float outsideSq = LengthSq(point - closest);
	if (outsideSq > 0.0f) {
		return std::sqrt(outsideSq); // 外
	}
	// 中：6つの面までの距離の一番短い物（深く入っているほど、大きいマイナスになる）
	const float toFace = std::min({
	    point.x - aabb.min.x,
	    aabb.max.x - point.x, // x の2面
	    point.y - aabb.min.y,
	    aabb.max.y - point.y, // y の2面
	    point.z - aabb.min.z,
	    aabb.max.z - point.z, // z の2面
	});
	return -toFace;
}
} // namespace

Vector3 DeepestPointOnSegmentInAABB(const Vector3& start, const Vector3& end, const AABB& aabb) {
	// 線分の上を動かしたときの「符号付きの距離」は、谷が1つだけの形（箱が凸な形なので）。三分探索で谷の底を探す
	//   範囲 [low, high] を3つに分け、2つの点 m1 < m2 で値を比べる。m1 の方が小さければ、谷の底は m2 より右には無い（逆も同じ）
	const Vector3 direction = end - start;
	float low = 0.0f;
	float high = 1.0f;
	for (int i = 0; i < kSegmentSearchSteps; ++i) {
		const float m1 = low + (high - low) / 3.0f;
		const float m2 = high - (high - low) / 3.0f;
		if (SignedDistanceToAABB(start + direction * m1, aabb) < SignedDistanceToAABB(start + direction * m2, aabb)) {
			high = m2;
		} else {
			low = m1;
		}
	}
	return start + direction * ((low + high) * 0.5f);
}

} // namespace Collision