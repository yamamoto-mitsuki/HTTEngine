#pragma once
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/GameObject.h"

/// <summary>
/// まっすぐ飛ぶ弾（Sandbox のテスト用。数が多い物は COMPONENT / SYSTEM で回す形の見本）
/// <para>同じ Entity に Hitbox（Phase を Active、Max Targets を 1）と Damage Source を付ける。当てきった（Is Spent）か、寿命が尽きたら消える</para>
/// </summary>
struct TestBullet {
	Vector3 velocity = {0.0f, 0.0f, 10.0f}; // 速さ（m/s。ワールド座標）
	float lifeTime = 3.0f;                  // 残りの寿命（秒）
};

/// <summary>
/// 弾を1つ作る（Hitbox・Damage Source・TestBullet・見た目の球を付ける。付くのは次のフレームの頭）
/// </summary>
/// <param name="position">出す位置（ワールド）</param>
/// <param name="velocity">速さ（m/s。ワールド）</param>
/// <param name="shooter">撃った物（弾のレイヤーはこれと同じ。Instigator にも入れる＝撃った物には当たらない）</param>
/// <param name="targets">当たるレイヤー</param>
/// <param name="damage">ダメージ</param>
GameObject SpawnTestBullet(const Vector3& position, const Vector3& velocity, Handle<Entity> shooter, LayerMask targets, float damage);

/// <summary>
/// 決まった間隔で弾を撃つ（Sandbox のテスト用。Entity の前 +Z へ）
/// </summary>
struct TestBulletSpawner {
	float interval = 1.0f;     // 撃つ間隔（秒）
	float speed = 40.0f;       // 弾の速さ（m/s。速くても Hurtbox をすり抜けないかを見る）
	float damage = 5.0f;       // 弾のダメージ
	LayerMask targets = LayerMask::Everything(); // 弾が当たるレイヤー
	float timer = 0.0f;        // 次に撃つまでの秒（実行中だけ）
};
