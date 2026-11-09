#pragma once
#include <cstdint>
#include <type_traits>

// 攻撃が当たったときのダメージと手応え。Hitbox と同じ Entity に付ける Component（カテゴリ：Combat。ダメージの層）
// 量 ＝ Damage × 当たった Hurtbox の倍率 × 距離の倍率（Falloff Curve）。受ける側の OnBeforeHitTaken で書き換えられる
// 手応え（ヒットストップ・カメラの揺れ）は、受けた側が断らずに Hit になったときだけ出る

/// <summary>
/// 攻撃のダメージと手応え
/// </summary>
struct DamageSourceComponent {
	float damage = 10.0f; // ダメージの量（倍率を掛ける前）
	int damageType = 0;   // 攻撃の種類（Settings の Damage Types の番号）

	// --- 距離で変わる倍率（Hitbox が Active になった所から、当たった所までの距離。弾なら撃った所から）---
	uint32_t falloffCurve = 0; // 倍率のカーブ（.curve。横軸＝距離を Near〜Far で 0〜1 に直した値、縦軸＝倍率）。未選択なら倍率は 1
	float falloffNear = 0.0f;  // この距離で、カーブの横軸が 0
	float falloffFar = 10.0f;  // この距離で、カーブの横軸が 1（それより遠くても 1）

	// --- 手応え（0 なら無し）---
	float hitStop = 0.0f; // ヒットストップの秒（Time::HitStop）
	float shake = 0.0f;   // カメラの揺れの強さ（CameraSystem::AddImpulse。向きは攻撃の向き）
};

static_assert(std::is_trivially_copyable_v<DamageSourceComponent>);
