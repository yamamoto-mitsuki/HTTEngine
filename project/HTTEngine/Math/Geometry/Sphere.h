#pragma once
#include "HTTEngine/Math/Vector3.h"

// 当たり判定に使用する球
struct Sphere {
	Vector3 center; // 中心点
	float radius;   // 半径
};

// 球同士の判定
bool IsCollidingSphereSphere(const Sphere& a, const Sphere& b);
