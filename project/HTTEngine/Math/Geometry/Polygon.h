#pragma once
#include <span>
#include <array>
#include <vector>
#include <cstdint>

#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 多角形の点の数の上限（Polygon Shape の Component は memcpy で写すので、決まった大きさの配列で持つ）
constexpr uint32_t kMaxPolygonPoints = 32;

/// <summary>
/// 多角形を奥（Z）へ押し出した形（当たり判定に使う。ワールド座標）
/// <para>横から見るゲームの足場用。X・Y の形は自由（へこんでいてもよい）で、Z は奥行きの幅だけ</para>
/// </summary>
struct ExtrudedPolygon {
	std::array<Vector2, kMaxPolygonPoints> points{}; // 外周の点（origin から見た X・Y。反時計回りに並べ直してある）
	uint32_t count = 0;                              // 使っている点の数（3 より少なければ、何にも当たらない）
	Vector3 origin = {0.0f, 0.0f, 0.0f};             // 基準の位置（物理で動かすときは、ここだけずらせばよい）
	float halfDepth = 0.0f;                          // 奥行きの半分（Z が origin.z ± halfDepth の間だけ当たる）
	float radius = 0.0f;                             // 外へ太らせた量（角は丸くなる。「触れているか」を調べるときに少し太らせる）

	// 使っている点だけ
	std::span<const Vector2> GetPoints() const { return {points.data(), count}; }
};

/// <summary>
/// 平面の多角形（点を順につなぎ、最後の点から最初の点へ戻る形）の計算
/// <para>点 i → 点 i+1 を「辺 i」と呼ぶ（最後の辺は、最後の点 → 点 0）</para>
/// <para>Polygon Shape の見た目（三角形に分ける）・当たり（中か・一番近い辺）・形のエディタ（形がおかしくないか）で使う</para>
/// </summary>
namespace Polygon2D {

// 符号付きの面積。反時計回り（X 右・Y 上で見て左回り）なら＋、時計回りなら−
float SignedArea(std::span<const Vector2> points);

// point が多角形の中か（辺の上の点は、どちらになるか決まらない）
bool IsPointInside(const Vector2& point, std::span<const Vector2> points);

// 外周（辺）の上で point に一番近い点。edgeIndex には、それが何番の辺の上か
Vector2 ClosestPointOnEdges(const Vector2& point, std::span<const Vector2> points, size_t* edgeIndex = nullptr);

// 辺 edgeIndex の外向きの法線（長さ1）。points は反時計回り（反時計回りなら、辺の進む向きの右側が外）
Vector2 EdgeOutwardNormal(std::span<const Vector2> points, size_t edgeIndex);

// 形として正しいか（点が 3 つ以上・長さ 0 の辺が無い・面積が 0 でない・辺どうしが交差していない）
bool IsSimple(std::span<const Vector2> points);

/// <summary>
/// 三角形に分ける（耳切り法）。indices に点の番号を 3 つずつ足す（時計回りでもよい。中で並べ直す）
/// </summary>
/// <returns>分けられたか（形がおかしいと false）</returns>
bool Triangulate(std::span<const Vector2> points, std::vector<uint32_t>& indices);
} // namespace Polygon2D