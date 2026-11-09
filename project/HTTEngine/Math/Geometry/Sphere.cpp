#include "HTTEngine/Math/Geometry/Sphere.h"

// ===== 球同士の判定 =====
bool IsCollidingSphereSphere(const Sphere& a, const Sphere& b) {
	// 2つの球の中心間の座標を求める
	float distanceSq = LengthSq(a.center - b.center);
	// 判定
	if (distanceSq <= (a.radius + b.radius) * (a.radius + b.radius)) {
		return true;
	}

	return false;
}