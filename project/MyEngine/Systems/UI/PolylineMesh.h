#pragma once
#include <array>
#include <cstdint>

#include "MyEngine/Components/UI/PolylineComponent.h"
#include "MyEngine/Math/Vector2.h"

// 前方宣言
struct PolylineMeshData;

/// <summary>
/// 折れ線のメッシュの部分の種類（Polyline.PS の kPiece〜 と同じ番号。PS は種類ごとに距離の測り方を変える）
/// </summary>
enum class PolylinePiece : uint32_t {
	Segment,    // 線の直線の部分（線からの距離）
	Corner,     // 線の角の部分（角の円からの距離）
	Cap,        // 線の端（端の形の距離）
	FillInside, // 塗りの内側（いつも全部塗る）
	FillEdge,   // 塗りの縁の直線の部分（縁からの符号付きの距離）
	FillCorner, // 塗りの縁の角の部分（角の円からの符号付きの距離）
};

/// <summary>
/// メッシュを作る元の値。PolylineSystem は、これが前のフレームと同じならメッシュを作り直さない
/// <para>位置・回転・色・透明度は入れない（VS と定数で掛けるので、変わってもメッシュは同じ）</para>
/// </summary>
struct PolylineShape {
	std::array<Vector2, kMaxPolylinePoints> points{}; // 点（Scale を掛けたピクセル。使わない所は 0 のまま）
	std::array<float, kMaxPolylinePoints> radii{};    // 角の半径（Scale を掛けたピクセル）
	int count = 0;                                    // 使っている点の数
	bool isClosed = false;                            // 閉じた形
	bool hasFill = false;                             // 塗りの部分を作る（閉じた形だけ）
	float halfWidth = 0.0f;                           // 線の太さの半分（0 なら線の部分を作らない）
	PolylineCap cap = PolylineCap::Round;             // 開いた線の端の形

	// 全部の値を比べる（= default：メンバーを順に == で比べる関数を、コンパイラに作らせる）
	bool operator==(const PolylineShape&) const = default;
};

/// <summary>
/// 作ったメッシュの、番号の並び（塗りの部分が先、線の部分が後。1回の描画で全部描けば、線が塗りの上に重なる）
/// </summary>
struct PolylineMeshInfo {
	uint32_t fillIndexCount = 0;   // 番号の先頭から、塗りの部分の数
	uint32_t strokeIndexCount = 0; // その後ろの、線の部分の数
	bool isFillValid = true;       // 塗りを三角形に分けられたか（辺どうしが交差していると false。塗りは作らない）
};

/// <summary>
/// 折れ線のメッシュを作る（直線の部分・角の部分・端に分けて、それぞれを囲む三角形を作る。縁の形は Polyline.PS が距離で決める）
/// </summary>
namespace PolylineMeshBuilder {

/// <summary>
/// shape からメッシュを作る。mesh の配列は clear してから足すので、前の中身の大きさ（capacity）を使い回す
/// </summary>
PolylineMeshInfo Build(const PolylineShape& shape, PolylineMeshData& mesh);
} // namespace PolylineMeshBuilder