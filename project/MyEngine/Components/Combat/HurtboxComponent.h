#pragma once
#include <type_traits>

#include "MyEngine/Math/Vector3.h"

// 攻撃を受ける形（やられ判定）。Entity に付ける Component（カテゴリ：Combat。当たりの層）
// 押し合う形（Collider）とは別（見た目より小さくする・体の部分ごとに分けるため）。部分ごとに分けるときは、子の Entity に1つずつ付ける
// どのチームかは、付けた Entity のレイヤー（Hitbox の Targets で絞られる）
// 受ける本体は「Health の付いた一番近い親か自分」（無ければ自分）。頭と手の両方に入っても、1回の攻撃で当たるのは1回

/// <summary>
/// やられ判定の形
/// </summary>
enum class HurtboxShape {
	Sphere,  // 球
	Capsule, // カプセル（ローカルの上 +Y に立つ。Entity と一緒に回る）
};


/// <summary>
/// 攻撃を受ける形
/// </summary>
struct HurtboxComponent {
	bool enabled = true;                         // false の間は当たらない（外さずに、一時的に止めたいとき）
	HurtboxShape shape = HurtboxShape::Capsule;  // 形
	Vector3 center = {0.0f, 0.9f, 0.0f};         // Entity の位置からのずれ（ローカル。回転と大きさも掛かる）
	float radius = 0.4f;                         // 半径（Entity の大きさの X と Z の大きい方を掛ける。球は一番大きい軸）
	float height = 1.8f;                         // カプセルの高さ（下の端から上の端まで。半球も入れる。Entity の Y の大きさを掛ける）
	float damageMultiplier = 1.0f;               // ダメージの倍率（弱点なら 1 より大きく、硬い所は小さく）
};

static_assert(std::is_trivially_copyable_v<HurtboxComponent>);
