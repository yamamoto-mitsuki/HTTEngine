#pragma once
#include "HTTEngine/Math/Vector3.h"

struct Transform {
	Vector3 scale = {1.0f, 1.0f, 1.0f};       // 拡大率
	Vector3 rotation = {0.0f, 0.0f, 0.0f};    // 回転角（ラジアン。TransformComponent の rotation とは別物で、こちらはラジアンのまま）
	Vector3 translation = {0.0f, 0.0f, 0.0f}; // 平行移動量
};