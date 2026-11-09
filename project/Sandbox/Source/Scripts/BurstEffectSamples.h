#pragma once
#include <cstdint>

#include "MyEngine/ECS/GameComponent.h"

//=============================================================================
// 出し終わったら消える、飛び散る物（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 決まった間隔で、飛び散る物を出す（Sandbox のテスト用。弾が当たった所に出すのと同じ形）
/// <para>Prefab を選んでいれば Instantiate、選んでいなければコードで Entity を作って Particle System を付ける</para>
/// <para>どちらも、作った同じフレームには Component がまだ付いていない（次のフレームの頭に付く）。付いたフレームに Play On Start で出て、出し終わったら Stop Action の Destroy で消える</para>
/// <para>Prefab を使うときは、一番上の Entity の Particle System を「Looping を外す・Play On Start・Stop Action を Destroy」にしておく</para>
/// </summary>
struct BurstSpawner {
	float interval = 0.5f;  // 出す間隔（秒）
	uint32_t prefab = 0;    // 飛び散る物の Prefab（0 ならコードで作る）
	float spread = 3.0f;    // 出す所を、この Entity の位置から左右（X）にずらす幅（m。この中でランダム）
	float timer = 0.0f;     // 次に出すまでの秒（実行中だけ）
};
