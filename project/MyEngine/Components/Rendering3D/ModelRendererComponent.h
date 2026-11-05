#pragma once
#include <cstdint>
#include <type_traits>

#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Math/Transform.h"

// 3Dモデルを描くためのデータだけを持つ（カテゴリ：Rendering3D）。処理とGPUリソースは持たない。
struct ModelRendererComponent {
	uint32_t modelHandle = 0;   // 0は未選択（ModelManagerの採番は1から）
	uint32_t textureHandle = 0; // 0ならモデルのマテリアルのテクスチャを使う
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色（sRGB。RGBA 0〜1。テクスチャの色に掛ける）
	float intensity = 1.0f;                   // 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る）
	Transform uvTransform;
	MaterialParams material;
	bool overrideMaterial = false; // true：material をモデルの mtl / glTF の値の代わりに使う（false なら、mtl に無いメッシュだけ material を使う）
	ShadingType shadingType = ShadingType::Unlit;
	BlendMode blendMode = BlendMode::Normal;
	RasterizerType rasterizerType = RasterizerType::SolidBack;
	DepthMode depthMode = DepthMode::TestWrite;
	BillboardMode billboard = BillboardMode::None; // カメラの方を向かせる（Full / AxisY）
	bool receivesDecals = true;                    // false：デカールを受けない（箱の中にあっても塗られない。不透明なときだけ効く）
	bool enabled = true;
};

static_assert(std::is_trivially_copyable_v<ModelRendererComponent>);