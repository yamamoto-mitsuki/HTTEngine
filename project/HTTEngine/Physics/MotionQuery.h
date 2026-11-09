#pragma once
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// 動き方の Component（Rigidbody・Character Controller）に共通して聞ける値
/// </summary>
struct MotionState {
	Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 今の速さ（1秒に進む距離。ワールド座標）
	bool isGrounded = false;               // 前のフレームに、地面（足元の物）に乗っていた
};

/// <summary>
/// 「この Entity の速さ・地面にいるか」を、どの動き方の Component かを知らずに聞く窓口
/// <para>動き方の System（PhysicsSystem・CharacterControllerSystem）が Initialize で「自分の Component ならこう答える」を登録する</para>
/// <para>聞く側（当たり判定・カメラの追いかけ・Trigger）は、動き方の種類を知らない。動き方を足しても、聞く側は直さなくてよい</para>
/// </summary>
class MotionQuery {
public:
	// 答え方。entity に自分の Component が付いていれば out に入れて true、付いていなければ false
	using Getter = bool (*)(Handle<Entity> entity, MotionState& out);

	// 答え方の表を作る・消す。動き方の System の Initialize より前に作り、Release より後に消す
	static void Initialize();
	static void Release();

	// 答え方を足す（動き方の System の Initialize から）。先に足した物から聞く（1つの Entity に2つの動き方を付けたら、先の物が答える）
	static void Register(Getter getter);

	// 動き方の Component が付いていれば out に入れて true。付いていなければ false（out は変えない）
	static bool TryGet(Handle<Entity> entity, MotionState& out);
	// 動き方の Component が付いているか
	static bool Has(Handle<Entity> entity);
	// 今の速さ（動き方の Component が無ければ0。動かない壁・床）
	static Vector3 GetVelocity(Handle<Entity> entity);
	// 地面に乗っているか（動き方の Component が無ければ false）
	static bool IsGrounded(Handle<Entity> entity);
};
