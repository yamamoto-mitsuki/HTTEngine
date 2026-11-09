#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Math/Geometry/HitShape.h"

// 攻撃の当たる形と、当たる時間の窓。Entity に付ける Component（カテゴリ：Combat。当たりの層）
// 当たったら HitSystem が「当たりの知らせ」を作る（Script の OnHitDealt・OnHitTaken、SYSTEM は HitSystem::GetAllHits）
// ダメージの量・種類・手応えは同じ Entity の Damage Source（ダメージの層）。無ければ、当たりの知らせだけ来る
// 窓を開け閉めするのは呼ぶ側（攻撃部品・Timeline・Script・SYSTEM）。HitSystem::Warn・Activate・Deactivate か、phase を書く
// 形の値（Shape）は、当たり・ジャスト判定の範囲・床の予告の図形が同じ物を使う（ずれない）

/// <summary>
/// 当たる時間の窓の段階
/// </summary>
enum class HitboxPhase {
	Off,     // 何もしない（当たらない。ジャスト判定の範囲も無い）
	Warning, // 予告（当たらない。床の予告の図形を出す。Active になる Just Lead Time 秒前から、ジャスト判定の範囲が有効）
	Active,  // 当たる（Active になるたびに、新しい攻撃の回になる＝当てた相手の記録が空になる）
};


/// <summary>
/// 攻撃の当たる形と窓
/// </summary>
struct HitboxComponent {
	HitShape shape;                               // 当たる形（ジャスト判定の範囲・床の予告の図形も、これを使う）
	LayerMask targets = LayerMask::Everything();  // 当てる相手のレイヤー（相手の Hurtbox の Entity のレイヤーで見る）
	EntityRef instigator;                         // 撃った本人（空ならこの Entity）。本人とその親子には当たらない。弾は撃った物が作るときに書く

	// --- 窓（保存した値が、Play したとき・Prefab から作ったときの段階になる）---
	HitboxPhase phase = HitboxPhase::Off; // 今の段階
	float warningSeconds = 0.0f;          // Warning の長さ（秒。0 ならずっと Warning）
	bool activateAfterWarning = false;    // Warning が終わったら Active にする
	float activeSeconds = 0.0f;           // Active の長さ（秒。0 ならずっと Active）

	// --- 何回当たるか ---
	float rehitInterval = 0.0f; // 同じ相手にもう一度当たるまでの秒（0 なら、1回の攻撃で1回だけ。正なら多段ヒット）
	int maxTargets = 0;         // 1回の攻撃で当てられる相手の数（0 なら何体でも。弾は 1。当てきったら Off になる）

	// --- ジャスト判定（HitSystem::TryConsumeJust）---
	bool justEnabled = true;   // ジャスト判定に使うか（false ならジャストにならない当たり。弾・ジャストを見せたくない攻撃）
	float justMargin = 0.5f;   // ジャスト判定の範囲：形をこの距離だけ太らせた範囲（m。0 なら形そのものの中だけ）
	float justLeadTime = 0.3f; // Active になる何秒前から、ジャスト判定の範囲が有効か（Warning の長さが 0 なら、Warning の間ずっと）

	// --- 実行中だけの値（保存しない。HitSystem が書く）---
	float phaseTime = 0.0f;    // 今の段階に入ってからの秒
	uint32_t attackSerial = 0; // 攻撃の回の番号（Active になるたびに 1 増える）
	int hitCount = 0;          // この攻撃の回で当てた相手の数

	// 当てきった（Max Targets まで当てた。弾を消す合図）
	bool IsSpent() const { return maxTargets > 0 && hitCount >= maxTargets; }
	// Warning の残りの秒（Warning の長さが 0 なら 0）
	float GetWarningRemaining() const { return (phase == HitboxPhase::Warning && warningSeconds > phaseTime) ? warningSeconds - phaseTime : 0.0f; }
};

static_assert(std::is_trivially_copyable_v<HitboxComponent>);
