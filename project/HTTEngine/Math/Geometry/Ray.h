#pragma once
#include "HTTEngine/Math/Vector3.h"

// 当たり判定に使用するRay（半直線）
struct Ray {
	Vector3 origin;    // 始点
	Vector3 direction; // 終点への差分ベクトル
};