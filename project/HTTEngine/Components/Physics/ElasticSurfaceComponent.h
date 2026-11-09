#pragma once
#include <type_traits>

#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Physics/ElasticSurfaceSimulation.h"

// 弾む面（トランポリン）。Entityに付けるComponent（カテゴリ：Physics）
// 面の真ん中は、付けた Entity のワールドの位置（回転・大きさは使わない）。横（X）に Width だけ広がり、奥行き（Z）に Depth だけ広がる
// 乗った物を押し返すのは PhysicsSystem（Rigidbody と、Is Trigger でない Collider を付けた物が乗れる）
// 点の高さ（揺れている形）は ElasticSurfaceSystem が持つ。Component に可変長の配列は入れられない（Trail と同じ）
// 見た目（色・テクスチャ・光沢）は Primitive Renderer と同じ PrimitiveMaterial。テクスチャは m 単位で貼る（UV Scale が1なら、1m に1枚）

/// <summary>
/// 弾む面の設定
/// </summary>
struct ElasticSurfaceComponent {
	bool enabled = true;            // falseの間は乗れない・描かない
	ElasticSurfaceSettings surface; // 膜の張り方（幅・点の数・張り・重さ・減衰）
	ElasticContactSettings contact; // 物とのぶつかり方
	int stepsPerSecond = 1200;      // 1秒間に何歩計算するか（PhysicsSystem の1歩がこれより細かくなる）。張りを強くして暴れたら増やす

	// --- 見た目（当たりには関係しない。上の面だけが当たる）---
	float thickness = 0.3f; // 厚さ（m）。上の面から下へこの分の厚みを付ける
	float depth = 6.0f;     // 奥行き（m）
	PrimitiveMaterial material = {
	    .color = {0.35f, 0.75f, 0.7f, 1.0f}
    }; // 板の見た目
	float postHeight = 3.0f;                  // 両端の支柱の高さ（ｍ。上の面から下へ）。0で描かない
	float postWidth = 0.4f;                   // 支柱の幅（m）
	PrimitiveMaterial postMaterial = {
	    .color = {0.45f, 0.45f, 0.5f, 1.0f}
    }; // 支柱の見た目
	bool showLines = false;                   // 点をつないだ線・平らな位置を描く（確かめる用）
};

static_assert(std::is_trivially_copyable_v<ElasticSurfaceComponent>);