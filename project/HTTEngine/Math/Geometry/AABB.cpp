#include "HTTEngine/Math/Geometry/AABB.h"

// ===== 8つの角 =====
std::array<Vector3, 8> AABB::GetCorners() const {
	std::array<Vector3, 8> corners{};
	for (int i = 0; i < 8; ++i) {
		corners[i] = {(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
	}
	return corners;
}
