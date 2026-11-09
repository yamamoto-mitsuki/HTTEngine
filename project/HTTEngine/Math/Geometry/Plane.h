#pragma once
#include "HTTEngine/Math/Vector3.h"

// 当たり判定に使う平面
struct Plane {
	Vector3 normal; // 法線
	float distance; // 距離
};