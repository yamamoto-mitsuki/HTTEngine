#pragma once
#include <array>
#include <cstdint>
#include "HTTEngine/Math/MathIncludes.h"

namespace IBLConfig {
// IBLで焼くマップの解像度・段数を一元に管理
inline constexpr uint32_t kEnvironmentSize = 512; // Envキューブ1面
inline constexpr uint32_t kIrradianceSize = 32;   // 拡散は低解像度で十分
inline constexpr uint32_t kPrefilterSize = 128;   // 鏡面ベース解像度
inline constexpr uint32_t kPrefilterMipCount = 5; // ラフネス段階
inline constexpr uint32_t kBrdfLutSize = 512;     // 2D LUT
inline constexpr uint32_t kSkyboxSizeMin = 256;   // 天球キューブの下限
inline constexpr uint32_t kSkyboxSizeMax = 2048;  // 天球キューブの上限


inline constexpr std::array<Vector3, 6> kDirs = {{
    {1,  0,  0 },
    {-1, 0,  0 },
    {0,  1,  0 },
    {0,  -1, 0 },
    {0,  0,  1 },
    {0,  0,  -1}
}};
inline constexpr std::array<Vector3, 6> kUps = {{
    {0, 1, 0 },
    {0, 1, 0 },
    {0, 0, -1},
    {0, 0, 1 },
    {0, 1, 0 },
    {0, 1, 0 }
}};
inline Matrix4x4 MakeCubeView(const Vector3& f, const Vector3& up) { 
	Vector3 r = Normalize(Cross(up, f));
	Vector3 u = Cross(f, r);
	Matrix4x4 v = {};
	v.m[0][0] = r.x;
	v.m[0][1] = u.x;
	v.m[0][2] = f.x;
	v.m[1][0] = r.y;
	v.m[1][1] = u.y;
	v.m[1][2] = f.y;
	v.m[2][0] = r.z;
	v.m[2][1] = u.z;
	v.m[2][2] = f.z;
	v.m[3][3] = 1.0f;
	return v;
}
inline size_t AlignTo256(size_t s) { return (s + 255) & ~size_t(255); }
}