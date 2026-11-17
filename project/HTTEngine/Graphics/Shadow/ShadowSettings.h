#pragma once
#include <cstdint>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Math/Vector3.h"


// ===== 影の深度の絵の1辺の大きさ =====
enum class ShadowMapSize {
	Size1024, // 1024*1024
	Size2048, // 2048*2048
	Size4096, // 4096*4096
};
// 選択肢ごとの説明
constexpr auto EnumTips(ShadowMapSize) {
	using enum ShadowMapSize;
	return MakeEnumTips<ShadowMapSize>({
	    {Size1024, "軽い。影の縁がガタガタしやすい"     },
	    {Size2048, "ふつう"	                       },
	    {Size4096, "細かいが重い"                    },
	});
}

// ===== 1辺のピクセル数 =====
constexpr uint32_t GetShadowMapPixels(ShadowMapSize size) {
	switch (size) {
	case ShadowMapSize::Size1024:
		return 1024;
	case ShadowMapSize::Size2048:
		return 2048;
	case ShadowMapSize::Size4096:
		return 4096;
	}
	return 2048;
}

// ===== 平行光源の影の調整（Inspectorで変える値） =====
struct DirectionalShadowParams {
	bool castsShadows = true;                        // 影を出す
	ShadowMapSize mapSize = ShadowMapSize::Size2048; // 影の深度の絵の大きさ
	float distance = 30.0f;                          // カメラからこの距離まで影を出す
	float strength = 1.0f;                           // 影の濃さ（0: 影なし 1: 平行光源の光が全く届かない）
	float bias = 0.05f;                              // 深度を比べる前に引く距離
};

// ===== このフレームの平行光源の影 =====
struct DirectionalShadowSettings {
	bool enabled = false;                    // false: 影を出さない
	Vector3 direction = {0.0f, -1.0f, 0.0f}; // 光が進む向き
	DirectionalShadowParams params;          // 調整の値
};