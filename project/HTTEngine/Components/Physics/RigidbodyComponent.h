#pragma once
#include <type_traits>

#include "HTTEngine/Math/Vector3.h"

// 重力と速さで動き、ぶつかったら止まる（跳ね返り・摩擦は Collider の Physic Material）。Entityに付けるComponent（カテゴリ：Physics）
// 動かすのは PhysicsSystem。押し戻す相手は Is Trigger でない Collider（自分にも Collider を付ける。無ければ落ちるだけ）
// Elastic Surface（弾む面）の上では、面に押し返されて跳ねる（重いほど深く沈める）
// ゲームは velocity を書いて動かす（transform.translation を直接動かしても、壁からは押し戻される）


/// <summary>
/// 物理で動く物（Unity の Rigidbody の小さい版。回転は無い）
/// </summary>
struct RigidbodyComponent {
	Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 速さ（1秒に進む距離。ワールド座標）。ジャンプなら y に上向きの速さを入れる
	float mass = 1.0f;                     // 重さ（kg）
	bool useGravity = true;                // 重力で落ちる（重力の向き・強さは Settings の Physics → World → Gravity）
	float gravityScale = 1.0f;             // 重力の倍率（2 で2倍速く落ちる。ジャンプの手触りの調整に）
	float drag = 0.0f;                     // 空気抵抗（大きいほど早く止まる。0 なら止まらない）
	float windScale = 1.0f;                // 風（Wind Zone）に押される強さの倍率（0 なら押されない。自分で風を読んで動く物は 0 にする）
	bool isKinematic = false;              // true：物理で動かさない（スクリプトで動かす床など）。ほかの Rigidbody からは壁に見える
	bool collideWithRigidbodies = true;    // true：ほかの Rigidbody と押し合う（重さの逆の割合で）。false：ほかの Rigidbody とはすり抜ける（壁・床にはぶつかる。当たりの知らせ OnCollision〜 は来る）
	bool freezePositionX = false;          // その軸には動かさない（押し戻しも、その軸ではしない）
	bool freezePositionY = false;          // 見下ろしのゲームなら Y を止める
	bool freezePositionZ = false;          // 横から見るゲームなら Z を止める

	// --- 実行中だけの値（PhysicsSystem が書く。保存しない）---
	bool isGrounded = false; // 前のフレームに、足元の物（重力と反対の向きへ押し返してきた物）に乗っていた
};

static_assert(std::is_trivially_copyable_v<RigidbodyComponent>);