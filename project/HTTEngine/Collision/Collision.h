#pragma once
#include "HTTEngine/Math/Geometry/AABB.h"
#include "HTTEngine/Math/Geometry/Capsule.h"
#include "HTTEngine/Math/Geometry/Plane.h"
#include "HTTEngine/Math/Geometry/Polygon.h"
#include "HTTEngine/Math/Geometry/Ray.h"
#include "HTTEngine/Math/Geometry/Sphere.h"

// 当たり判定をまとめた名前空間
namespace Collision {

/// <summary>
/// 押し出しで動かさない軸を判定する（true の軸へは押さない。横から見るゲームで奥行き(Z)へ押し出されないように）
/// </summary>
struct AxisLock {
	bool x = false;
	bool y = false;
	bool z = false;
};

// ===== 点の判定 =====
bool IsPointInSphere(const Vector3& point, const Sphere& sphere);
bool IsPointInAABB(const Vector3& point, const AABB& aabb);

// ===== 形状同士 =====
bool SphereSphere(const Sphere& a, const Sphere& b);
bool AABBAABB(const AABB& a, const AABB& b);
bool SphereAABB(const Sphere& sphere, const AABB& aabb);
bool CapsuleSphere(const Capsule& capsule, const Sphere& sphere);
bool CapsuleAABB(const Capsule& capsule, const AABB& aabb);

// ===== 押し出し =====
// a に push を足すと、a と b がちょうど触れた所になる。lock の軸へは押さない（その分、ほかの軸で押す）
bool PushOutSphereSphere(const Sphere& a, const Sphere& b, Vector3* push, AxisLock lock = {});
bool PushOutSphereAABB(const Sphere& a, const AABB& b, Vector3* push, AxisLock lock = {});
bool PushOutAABBSphere(const AABB& a, const Sphere& b, Vector3* push, AxisLock lock = {});
bool PushOutAABBAABB(const AABB& a, const AABB& b, Vector3* push, AxisLock lock = {});
// 多角形を押し出した形と球。横の面・前の面・後ろの面の一番近い所から出す。へこんだ形も、三角形に分けずにそのまま調べる
bool PushOutSpherePolygon(const Sphere& a, const ExtrudedPolygon& b, Vector3* push, AxisLock lock = {});
bool PushOutPolygonSphere(const ExtrudedPolygon& a, const Sphere& b, Vector3* push, AxisLock lock = {});
// カプセルと、多角形を押し出した形。線分の上で一番深く入っている所の球を出す（坂に立つカプセルの足元も見落とさない）
bool PushOutCapsulePolygon(const Capsule& a, const ExtrudedPolygon& b, Vector3* push, AxisLock lock = {});
bool PushOutPolygonCapsule(const ExtrudedPolygon& a, const Capsule& b, Vector3* push, AxisLock lock = {});
bool RayCapsule(const Ray& ray, const Capsule& capsule, float* t = nullptr);
// 多角形を押し出した形（前の面・後ろの面・横の面のどこに当たっても。始点が中なら t=0）
bool RayPolygon(const Ray& ray, const ExtrudedPolygon& polygon, float* t = nullptr);

// ===== レイキャスト t: レイの原点からの距離（不要ならnullptr） =====
bool RaySphere(const Ray& ray, const Sphere& sphere, float* t = nullptr);
bool RayAABB(const Ray& ray, const AABB& aabb, float* t = nullptr);
bool RayPlane(const Ray& ray, const Plane& plane, float* t = nullptr);

// ===== ユーティリティ =====
Vector3 RemoveLockedAxes(const Vector3& vector, AxisLock lock); // lock の軸の成分を0にする（その軸へは動かさない）
Vector3 ClosestPointOnAABB(const Vector3& point, const AABB& aabb);
Vector3 RayPoint(const Ray& ray, float t); // レイ上の点を取得
Vector3 ClosestPointOnSegment(const Vector3& point, const Vector3& start, const Vector3& end); // 線分の上で point に一番近い点
// 2本の線分の、お互いに一番近い点（onA は線分 a の上、onB は線分 b の上）
void ClosestPointsBetweenSegments(const Vector3& aStart, const Vector3& aEnd, const Vector3& bStart, const Vector3& bEnd, Vector3* onA, Vector3* onB);
Vector3 DeepestPointOnSegmentInAABB(const Vector3& start, const Vector3& end, const AABB& aabb); // 線分の上で、箱に一番深く入っている点（線分が箱の外なら、箱に一番近い点）
}