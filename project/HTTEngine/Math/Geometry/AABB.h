#pragma once
#include <array>
#include <utility>

#include "HTTEngine/Math/Vector3.h"

// 当たり判定に使うAABB
struct AABB {
	Vector3 min; // 最小点
	Vector3 max; // 最大点

	/// <summary>
	/// 8つの角を返す。番号 i の 1・2・4 のビットで、x・y・z に min と max のどちらを使うかを決める
	/// </summary>
	std::array<Vector3, 8> GetCorners() const;
};

// 箱の12本の辺（GetCorners の番号の組）。AABB・OBB どちらの角にも使える
// ビットが1つだけ違う角どうしを結ぶと辺になる（1つの軸に4本 × 3軸 ＝ 12本）
inline constexpr std::array<std::pair<int, int>, 12> kBoxEdges = {{
    {0, 1}, {0, 2}, {0, 4},
    {1, 3}, {1, 5},
    {2, 3}, {2, 6},
    {3, 7},
    {4, 5}, {4, 6},
    {5, 7},
    {6, 7},
}};
