#pragma once
#include <cstdint>
#include <type_traits>

// 攻撃の予告を床に描く。Hitbox と同じ Entity に付ける Component（カテゴリ：Combat。見た目の層）
// 形は Hitbox の Shape（床に置く形：円・扇・線）。当たりと同じ符号付き距離の式で、デカールとして床・坂・段差に塗る
// 出すのは Hitbox が Warning の間（Show While Active を入れたら Active の間も）。描くのは HitboxTelegraphSystem
// 見た目はマテリアル（Shader と値）で変える。Curve の横軸は Warning の進み具合（0→1）

/// <summary>
/// 床の予告の設定
/// </summary>
struct HitboxTelegraphComponent {
	bool enabled = true;          // false の間は描かない
	uint32_t material = 0;        // マテリアル（MaterialManager::Load の番号。Shader は category: Decal で gDrawData に HitboxTelegraph を読む物。0 ならエンジンの既定＝赤い塗り）
	bool showWhileActive = false; // true：Active の間も出す（当たっている間の範囲も見せる攻撃）
	int order = 0;                // ほかのデカールと重なったときの順（大きい方が上に塗られる）
};

static_assert(std::is_trivially_copyable_v<HitboxTelegraphComponent>);
