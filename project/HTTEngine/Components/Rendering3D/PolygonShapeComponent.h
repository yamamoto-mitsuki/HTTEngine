#pragma once
#include <span>
#include <array>
#include <algorithm>
#include <type_traits>

#include "HTTEngine/Math/Geometry/Polygon.h"
#include "HTTEngine/Math/Vector2.h"

// 横から見た形（多角形）を、奥（Z）へ押し出した形。Entityに付けるComponent（カテゴリ：Rendering3D）
// 形を持つだけ。見た目は Polygon Renderer、当たりは Collider（Shape を Polygon にする）が、同じ Entity のこの形を使う（Unity の MeshFilter と同じ考え）
// 点は Entity のローカル座標の X・Y（Entity の位置・Z の回転・大きさが掛かる）。奥行きは Entity の Z を真ん中に、前後へ Depth の半分ずつ
// 点は Scene ビューで動かせる（Inspector の Edit Shape）。Entity の回転は Z だけ使う（X・Y で回すと、見た目は回るが当たりは回らない）

/// <summary>
/// 多角形を押し出した形
/// </summary>
struct PolygonShapeComponent {
	// 外周の点（左回りでも右回りでもよい。辺どうしが交差してはいけない）。使うのは先頭から pointCount 個
	// Component は memcpy で写すので、std::vector ではなく決まった大きさの配列で持つ
	std::array<Vector2, kMaxPolygonPoints> points = {
	    {{-2.0f, -0.5f}, {2.0f, -0.5f}, {2.0f, 0.5f}, {-2.0f, 0.5f}}
    };
	int pointCount = 4; // 使っている点の数（3〜kMaxPolygonPoints）
	float depth = 4.0f; // 奥行き（m）

	// 使っている点だけ（pointCount が範囲の外でも、配列の外は読まない）
	std::span<const Vector2> GetPoints() const { return {points.data(), static_cast<size_t>(std::clamp(pointCount, 0, static_cast<int>(kMaxPolygonPoints)))}; }
};

static_assert(std::is_trivially_copyable_v<PolygonShapeComponent>);