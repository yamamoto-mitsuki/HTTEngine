#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// モデルのファイルを使わずに、決まった形（球・箱）を描く。Entityに付けるComponent（カテゴリ：Rendering3D）
/// モデルのファイルを使わない形の見た目（Primitive Renderer の球・箱、Elastic Surface の板・支柱、Polygon Renderer の足場で共通）
// 大きさは Transform の Scale（Scale が 1 なら、球は直径1・箱は1辺1。Collider の初期値と同じ大きさ）
// 描くのは PrimitiveRenderSystem。Unity の「3D Object → Sphere / Cube」の見た目の所と同じ

/// <summary>
/// 透け方（描く順番と、奥行きを書くか）
/// </summary>
enum class SurfaceType {
	Opaque,      // 不透明（奥行きを書く。先に描く）
	Transparent, // 半透明（色の A の分だけ透ける。奥行きを書かずに、不透明な物の後で、カメラから遠い順に描く）
	Additive,    // 加算（色を足して光らせる。奥行きを書かずに、遠い順に描く）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(SurfaceType) {
	using enum SurfaceType;
	return MakeEnumTips<SurfaceType>({
	    {Opaque, "不透明"},
	    {Transparent, "Color の A の分だけ透ける（氷・水）。不透明な物の後に、カメラから遠い順に描く"},
	    {Additive, "色を足して光る（光の玉）。不透明な物の後に、カメラから遠い順に描く"},
	});
}

/// <summary>
/// モデルのファイルを使わない形の見た目（Primitive Renderer の球・箱、Elastic Surface の板・支柱で共通）
/// <para>モデル（Model Renderer）と違って mtl が無いので、値はいつもここから使う</para>
/// <para>Inspector と保存の並びは PrimitiveRenderSystem::DescribeMaterial（持っている Component の見せ方から呼ぶ）</para>
/// </summary>
struct PrimitiveMaterial {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};           // 色（RGBA。0〜1）。テクスチャがあれば掛け合わせる
	float intensity = 1.0f;                             // 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る）
	SurfaceType surface = SurfaceType::Opaque;          // 透け方（Transparent にすると、color の A の分だけ透ける）
	uint32_t textureHandle = 0;                         // テクスチャ（0 なら白＝色だけ）
	Vector2 uvScale = {1.0f, 1.0f};                     // テクスチャ1枚の大きさ（Elastic Surface は m。Primitive Renderer は1面を1として、0.5 なら1面に2回くり返す）
	ShadingType shadingType = ShadingType::HalfLambert; // 陰の付け方（Unlit：色そのまま／BlinnPhong：ハイライト／PBR：金属感・粗さ）
	MaterialParams params;                              // 光沢・金属感・自己発光など（どの値を使うかは shadingType で変わる）
	bool receivesDecals = true;                         // false：デカールを受けない（箱の中にあっても塗られない。Opaque のときだけ効く）
};

static_assert(std::is_trivially_copyable_v<PrimitiveMaterial>);

/// <summary>
/// 形
/// </summary>
enum class PrimitiveShape {
	Sphere, // 球
	Box,    // 箱
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(PrimitiveShape) {
	using enum PrimitiveShape;
	return MakeEnumTips<PrimitiveShape>({
	    {Sphere, "球（Scale 1 で直径 1）"},
	    {Box, "箱（Scale 1 で 1 辺 1）"},
	});
}

/// <summary>
/// 決まった形を描く設定
/// </summary>
struct PrimitiveRendererComponent {
	bool enabled = true;                        // false の間は描かない
	PrimitiveShape shape = PrimitiveShape::Box; // 形
	PrimitiveMaterial material;                 // 見た目（色・テクスチャ・陰の付け方・光沢など）
};

static_assert(std::is_trivially_copyable_v<PrimitiveRendererComponent>);