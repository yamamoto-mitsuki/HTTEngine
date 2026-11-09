#pragma once
#include "HTTEngine/Math/Vector3.h"

// 当たり判定に使うカプセル（線分 start → end を半径、radius で太らせた形）
struct Capsule {
	Vector3 start; // 線分の始点
	Vector3 end;   // 線分の終点
	float radius;  // 半径（0なら線分）
};