#pragma once
#include <array>
#include <format>
#include <vector>
#include <cstdint>
#include <string_view>
#include <d3d12.h>
#include <externals/magic_enum/magic_enum.hpp>
#include "MyEngine/Math/MathIncludes.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Graphics/Pipeline/ShaderCompiler.h"

//=============================================================================
// 頂点データ構造体
//=============================================================================
// 2D頂点データ構造
struct Vertex2dData {
	Vector4 position;
	Vector2 texcoord;
};
// 3D頂点データ構造
struct Vertex3dData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};
// パーティクルデータ構造体
struct VertexParticleData {
	Vector4 position;
	Vector2 texcoord;
};
// Line3D頂点データ構造
struct VertexLineData {
	Vector4 position;
	Vector4 color;
};
// UIの折れ線の頂点（Polyline.VS が読む。部分の種類ごとに、PS が距離の測り方を変える）
struct VertexPolylineData {
	Vector2 position; // 位置（折れ線の Entity から見たピクセル。Scale は掛けてある。VS が画面へ写す）
	Vector2 local;    // その部分から見た位置（直線・端：x=進む向き y=左へ / 角：円の中心から）
	Vector2 param;    // 部分ごとの値（角：x=半径 y=内外の向き / 端：x=端の形 / 塗りの縁：y=内外の向き）
	uint32_t piece;   // 部分の種類（PolylinePiece の番号）
};
static_assert(sizeof(VertexPolylineData) == 28, "Polyline.VS の VertexShaderInput と大きさが違います");

//　スキンの頂点データ（どの骨にどれだけ動かされるか）
// Vertex3dDataとは別の頂点バッファにして、Skinning の CS が StructuredBuffer<VertexInfluence> gInfluences として読む
constexpr uint32_t kNumMaxInfluence = 4; // 1つの頂点を動かす骨の最大数
struct VertexInfluence {
	std::array<float, kNumMaxInfluence> weights{};        // 骨ごとの重み（合計1。使わない枠は0）
	std::array<int32_t, kNumMaxInfluence> jointIndices{}; // 骨の番号（ModelAsset::nodes の番号＝行列パレットの番号）
};
static_assert(sizeof(VertexInfluence) == 32, "Skinning.CS の VertexInfluence（float4 + int4）と大きさが違います");


/// <summary>
/// 頂点の入力の形（InputLayout）を作る
/// </summary>
class VertexFormat {
public:
	static constexpr UINT kInfluenceInputSlot = 1; // スキンの頂点（VertexInfluence）を結ぶスロット

	/// <summary>
	/// シェーダーリフレクション情報からInputLayoutを生成
	/// </summary>
	static std::vector<D3D12_INPUT_ELEMENT_DESC> MakeInputLayout(const std::vector<ShaderInputParameter>& inputs);

private:
	/// <summary>
	/// シェーダー入力パラメータから対応するDXGI_FORMATを取得
	/// </summary>
	static DXGI_FORMAT FormatOf(D3D_REGISTER_COMPONENT_TYPE type, BYTE mask);
};