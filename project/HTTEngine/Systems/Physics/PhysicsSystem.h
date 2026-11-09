#pragma once
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Components/Physics/RigidbodyComponent.h"


/// <summary>
/// Rigidbody を重力と速さで動かして、ぶつかった Collider から押し戻す（Unity の物理の、ずっと小さい版）
/// <para>押し戻す相手は Is Trigger でない Collider。Rigidbody を付けた Entity には、自分の Collider（Is Trigger でない物）も付ける</para>
/// <para>回転はしない。ぶつかった向きの速さが0になる（Collider の Physic Material に跳ね返り・摩擦があれば、その分だけ跳ね返る・面に沿った速さが減る）。押し戻されるのは Rigidbody の側だけ
/// （Rigidbody 同士なら重さの逆の割合で。Collide With Rigidbodies を外した方は、ほかの Rigidbody とすり抜ける）</para>
/// <para>ぶつかった組は、ぶつかる直前の速さと一緒に CollisionSystem へ知らせる（Script の OnCollision〜 の CollisionInfo に入る）</para>
/// <para>Elastic Surface（弾む面）があれば、同じ小さな1歩の中で面も進め、乗った Rigidbody を面の力で押し返す（こちらは跳ねる）</para>
/// <para>動かすのは再生中だけ。一時停止・ヒットストップ中は時間が0なので止まる。スロー（SetGameTimeScale）ではゆっくり動く</para>
/// </summary>
class PhysicsSystem {
public:
	// RigidbodyComponent を登録する（置き場所・保存・Inspector）と、重力を Settings に出す。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// Rigidbody を動かして、ぶつかった物から押し戻す。ワールド行列の計算（UpdateTransforms）の後に呼ぶ
	/// <para>速い物が薄い壁をすり抜けないように、1フレームを細かく分けて動かす</para>
	/// </summary>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。0 なら何もしない）</param>
	/// <returns>動かした Rigidbody があったか（あれば、呼んだ側がワールド行列を計算し直す）</returns>
	static bool Update(float deltaTime);

	// 重力（Settings の Physics → World → Gravity。既定は (0, -9.8, 0)）
	static Vector3 GetGravity();
	static void SetGravity(const Vector3& gravity);
	// 「上」の向き（長さ1。重力の反対。重力が 0 なら +Y）。地面に乗っているか・立てる坂かを決めるのに使う（Character Controller も同じ向きを使う）
	static Vector3 GetUp();
};