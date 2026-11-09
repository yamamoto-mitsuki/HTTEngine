#pragma once
#include <cstdint>
#include <type_traits>

// HP と、攻撃を断る設定（受ける本体）。Entity に付ける Component（カテゴリ：Combat。ダメージの層）
// 同じ Entity か子の Hurtbox に当たった攻撃を、DamageSystem がここで受ける（無敵なら断る → 減らす → 0 になったら倒れた）
// 同じ Entity に Attack Table があれば、攻撃表は HP の割合をここから読む（Health Ratio を書かなくてよい）

/// <summary>
/// HP と、攻撃を断る設定
/// </summary>
struct HealthComponent {
	float maxHealth = 100.0f;        // 最大の HP
	float health = 100.0f;           // 今の HP（保存した値から始まる。ふつうは最大と同じ）。0 以下なら倒れている（もう当たらない）
	bool invincible = false;         // 無敵（ゲームが書く。true の間は攻撃を断る＝記録しないので、切れた後に同じ攻撃の範囲にいれば当たる）
	float invincibleAfterHit = 0.0f; // 当たった後、この秒だけ攻撃を断る（被弾の後の無敵）
	uint32_t immuneTypes = 0;        // 効かない攻撃の種類（Settings の Damage Types。断る）

	// --- 実行中だけの値（保存しない。DamageSystem が書く）---
	float invincibleTimer = 0.0f; // 被弾の後の無敵の残りの秒

	// HP の割合（0〜1。最大が 0 なら 0）
	float GetRatio() const { return (maxHealth > 0.0f) ? health / maxHealth : 0.0f; }
	// 倒れている（HP が 0 以下）
	bool IsDefeated() const { return health <= 0.0f; }
	// 今、攻撃を断るか（無敵か、被弾の後の無敵の間）
	bool IsInvincible() const { return invincible || invincibleTimer > 0.0f; }
};

static_assert(std::is_trivially_copyable_v<HealthComponent>);
