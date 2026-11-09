#pragma once
#include <cstdint>
#include <type_traits>

// 箱の中にある不透明な物（床・壁・段差）に、マテリアルで塗る。Entity に付ける Component（カテゴリ：Rendering3D）。Unity の Decal Projector に当たる
// 箱は Transform の大きさ（Scale 1 で 1辺 1。中心が Entity の位置）。上（ローカルの +Y）から下へ貼り、XZ がテクスチャの UV
// 描くのは DecalRenderSystem。デカールを受けない物は、Model Renderer・Primitive Renderer の Receive Decals を外す

/// <summary>
/// デカールの設定
/// </summary>
struct DecalRendererComponent {
	bool enabled = true;   // false の間は描かない
	uint32_t material = 0; // マテリアル（MaterialManager::Load の番号。Shader は category: Decal の物。0 ならエンジンの既定＝白い画像を貼る）
	float progress = 0.0f; // マテリアルの Curve の横軸（0〜1。Script・Timeline から動かす）
	int order = 0;         // 重なったときの順（大きい方が上に塗られる）
};

static_assert(std::is_trivially_copyable_v<DecalRendererComponent>);
