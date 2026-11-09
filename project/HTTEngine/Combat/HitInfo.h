#pragma once
#include <cstdint>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// 当たったときの扱い（受ける側の Script の OnBeforeHitTaken で変えられる）
/// </summary>
enum class HitResult {
	Hit,     // 当たった（HP が減る・ヒットストップと揺れが出る）
	Guarded, // 防いだ（当たりは記録する＝同じ攻撃の回ではもう当たらない。HP は damage だけ減る。ヒットストップと揺れは出ない）
	Ignored, // 断った（記録しない。無敵と同じで、同じ攻撃の回のうちに、後のフレームでもう一度当たることがある）
};


/// <summary>
/// 攻撃が当たった1件（当てた側の OnHitDealt・受けた側の OnHitTaken に渡す。SYSTEM は HitSystem::GetAllHits で読む）
/// <para>上半分は当たりの層（HitSystem）、下半分はダメージの層（DamageSystem）が書く</para>
/// </summary>
struct HitInfo {
	// ===== 当たりの層（HitSystem）=====
	Handle<Entity> hitbox;          // 当てた Hitbox の Entity
	Handle<Entity> instigator;      // 撃った本人（Hitbox の Instigator。空なら Hitbox の Entity と同じ）
	Handle<Entity> receiver;        // 受けた本体（Health の付いた一番近い親か自分。無ければ Hurtbox の Entity）
	Handle<Entity> hurtbox;         // 当たった Hurtbox の Entity
	uint32_t attackSerial = 0;      // 攻撃の回の番号（Hitbox ごとに、Active になるたびに 1 増える）
	Vector3 point = {0.0f, 0.0f, 0.0f};     // 当たった位置（Hurtbox の芯の、攻撃に一番近い点から、攻撃の形の縁まで寄せた所。芯が形の中なら芯の点。ダメージの数字を出す所）
	Vector3 direction = {0.0f, 0.0f, 1.0f}; // 攻撃の向き（長さ1。動いている攻撃は動いた向き、止まっている攻撃は形の真ん中から外へ）
	float distance = 0.0f;          // Active になった所から、当たった所までの距離（m。弾なら撃った所から）
	float hurtboxMultiplier = 1.0f; // 当たった Hurtbox の倍率（弱点なら 1 より大きい）

	// ===== ダメージの層（DamageSystem）=====
	HitResult result = HitResult::Hit; // 当たったときの扱い
	int damageType = 0;                // 攻撃の種類（Settings の Damage Types の番号。DamageSource が無ければ 0）
	float damage = 0.0f;               // ダメージの量（Damage × Hurtbox の倍率 × 距離の倍率。OnBeforeHitTaken で書き換えた後の値。残りの HP より大きくてもそのまま）
	bool isDefeated = false;           // この当たりで HP が 0 になった
	float healthAfter = 0.0f;          // 当たった後の HP（Health が無ければ 0）
};
