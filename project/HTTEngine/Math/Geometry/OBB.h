#pragma once
#include <array>

#include "HTTEngine/Math/Vector3.h"

// 当たり判定に使用するOBB
struct OBB {
	Vector3 center;  // 中心点
	std::array<Vector3, 3> axes = {{
	    {1.0f, 0.0f, 0.0f},
        {0.0f,1.0f,0.0f},
		{0.0f,0.0f,1.0f},
    }}; // 座標軸。正規化・直交必須
	Vector3 size;    // 座標方向の長さの半分。中心から面までの距離

	/// <summary>
	/// 8つの角を返す。番号 i の 1・2・4 のビットで、X・Y・Z の＋側か−側かを決める（AABB::GetCorners と同じ番号）
	/// </summary>
	std::array<Vector3, 8> GetCorners() const {
		std::array<Vector3, 8> corners{};
		for (int i = 0; i < 8; ++i) {
			const float x = (i & 1) ? size.x : -size.x;
			const float y = (i & 2) ? size.y : -size.y;
			const float z = (i & 4) ? size.z : -size.z;
			corners[i] = {
			    center.x + axes[0].x * x + axes[1].x * y + axes[2].x * z,
			    center.y + axes[0].y * x + axes[1].y * y + axes[2].y * z,
			    center.z + axes[0].z * x + axes[1].z * y + axes[2].z * z,
			};
		}
		return corners;
	}
};
