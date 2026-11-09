#pragma once
#include <type_traits>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// キャラクターの歩き方。Entityに付けるComponent（カテゴリ：Physics）
// 形は同じ Entity の Collider（Sphere か Capsule。無い・無効・Box・Polygon なら動かさない）。形を2か所に持たない（Rigidbody と同じ決まり）
// 動かすのは CharacterControllerSystem（再生中だけ。Script の後・物理の前）。ゲームは velocity の X・Z を書く（跳ぶなら Y に上向きの速さも）
// Rigidbody と違い、跳ね返らず・押されず・坂で滑らない。立てる坂の上では止まり、小さな段は上り、下り坂・段を下りるときは地面に吸い付く
// ほかの Rigidbody からは動く壁に見える。Rigidbody と一緒に付けない（付けたら Rigidbody が動かし、こちらは何もしない）


/// <summary>
/// 動きの状態（Unreal の Movement Mode と同じ考え。泳ぐ・飛ぶなどは、ここに足す）
/// </summary>
enum class CharacterMovementMode {
	Walking, // 地面に乗っている（重力をかけず、地面に沿って横に動く）
	Falling, // 空中（重力で落ちる。跳んだ・段から落ちた・立てない急な坂の上）
};

/// <summary>
/// キャラクターの歩き方（Unity の CharacterController・Unreal の CharacterMovementComponent の小さい版。回転は無い）
/// </summary>
struct CharacterControllerComponent {
	bool enabled = true;                   // false の間は動かさない（その場に止まる）
	float slopeLimit = 45.0f;              // 立てる坂の角度（度）。これより緩い面は地面（止まっても滑らない）。急な面は壁と同じく、沿って滑る
	float stepOffset = 0.3f;               // そのまま上れる段の高さ（m）。下り坂・段を下りるときに地面へ吸い付く距離にも使う
	bool useGravity = true;                // 空中で重力で落ちる（重力の向き・強さは Settings の Physics → World → Gravity。Rigidbody と同じ値）
	float gravityScale = 1.0f;             // 重力の倍率（2 で2倍速く落ちる。ジャンプの手触りの調整に）
	bool pushRigidbodies = true;           // true：ぶつかった Rigidbody（Is Kinematic でない物）を押す。false：止まるだけ
	float pushMass = 80.0f;                // 押すときのキャラの重さ（kg）。押す速さ ＝ キャラの速さ × pushMass ÷ (pushMass ＋ 相手の重さ)
	bool freezePositionX = false;          // その軸には動かさない（押し出しも、その軸ではしない）
	bool freezePositionY = false;
	bool freezePositionZ = false;          // 横から見るゲームなら Z を止める
	Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 速さ（1秒に進む距離。ワールド座標）。ゲームは X・Z を書く。Y は空中で重力が足され、地面にいる間は 0。壁に当たると壁へ向かう分が消える

	// --- 実行中だけの値（CharacterControllerSystem が書く。保存しない）---
	CharacterMovementMode movementMode = CharacterMovementMode::Falling; // 今の動きの状態（Play の直後は空中から始めて、最初のフレームで地面を見つける）
	Vector3 groundNormal = {0.0f, 1.0f, 0.0f}; // 乗っている地面の向き（長さ1。Walking の間だけ意味がある）
	Vector3 groundPoint = {0.0f, 0.0f, 0.0f};  // 地面に触れている所（ワールド座標。Walking の間だけ意味がある）
	Handle<Entity> groundEntity;               // 乗っている地面の Entity（Walking の間だけ意味がある。後で「動く床と一緒に動く」に使う）
	bool hitSides = false;                     // このフレームに横（壁・立てない急な坂）に当たった
	bool hitAbove = false;                     // このフレームに上（天井）に当たった
	bool hitBelow = false;                     // このフレームに下（床・立てる坂・ほかのキャラの頭）に当たった

	// 地面に乗っているか（前のフレームの終わりに）
	bool IsGrounded() const { return movementMode == CharacterMovementMode::Walking; }
};

static_assert(std::is_trivially_copyable_v<CharacterControllerComponent>);
