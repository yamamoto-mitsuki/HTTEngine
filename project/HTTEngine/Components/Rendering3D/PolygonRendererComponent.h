#pragma once
#include <type_traits>

#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"

// Polygon Shape（多角形を押し出した形）を描く。Polygon Shape と同じ Entity に付けるComponent（カテゴリ：Rendering3D）
// 描くのは前の面と横の面（後ろの面はカメラから見えないので描かない）。描くのは PolygonShapeSystem
// テクスチャはワールド座標で貼る（UV Scale が 2 なら 2m に1枚）。足場を動かしても伸ばしても絵は伸びず、隣の足場とつながる

/// <summary>
/// 多角形の見た目
/// </summary>
struct PolygonRendererComponent {
	bool enabled = true; // false の間は描かない
	PrimitiveMaterial material = {
	    .color = {0.55f, 0.5f, 0.45f, 1.0f},
	    .uvScale = {2.0f, 2.0f},
	}; // 見た目（色・テクスチャ・陰の付け方など。Primitive Renderer と同じ）
	float sideShade = 0.7f;   // 横の面の明るさ（前の面の色に掛ける。横から見るゲームで、形の縁が分かるように）
	bool worldSpaceUV = true; // true：UV をワールド座標で作る（隣の足場と絵がつながる）。false：Entity のローカル座標（足場と一緒に絵も動く）
};

static_assert(std::is_trivially_copyable_v<PolygonRendererComponent>);