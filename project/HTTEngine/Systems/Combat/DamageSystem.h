#pragma once
#include "HTTEngine/Combat/HitInfo.h"
#include "HTTEngine/Components/Combat/DamageSourceComponent.h"
#include "HTTEngine/Components/Combat/HealthComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// 攻撃のダメージ（ダメージの層）。当たりの層（HitSystem）から当たり1件を受け取り、受けるか決めて HP を減らす
/// <para>流れ：倒れている・無敵・効かない種類なら断る → 量を決める（Damage × Hurtbox の倍率 × 距離の倍率）→ 受ける側の Script の OnBeforeHitTaken
/// （Guarded・Ignored に変える・量を書き換える）→ HP を減らす → 被弾の後の無敵 → 手応え（ヒットストップ・カメラの揺れ）</para>
/// <para>Initialize で HitSystem に答え方を2つ登録する（受ける本体＝Health の付いた一番近い親か自分／受けるか）。当たりの層はこの型を知らない</para>
/// </summary>
class DamageSystem {
public:
	// Health・Damage Source を登録する（置き場所・保存・Inspector）。HitSystem::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 被弾の後の無敵の残りを減らす（HitSystem::Update の前に呼ぶ）
	/// </summary>
	/// <param name="isGameRunning">再生中か</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。ヒットストップの間は 0 なので、無敵の残りも減らない）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// Hurtbox の受ける本体（Health の付いた一番近い親か自分。無ければ hurtbox 自身）
	static Handle<Entity> FindReceiver(Handle<Entity> hurtbox);

	// 距離の倍率（Damage Source の Falloff Curve。カーブが無ければ 1）
	static float CalcFalloff(const DamageSourceComponent& source, float distance);

private:
	// 当たり1件を受けるか決めて、ダメージの欄を書く（HitSystem から呼ばれる）。false なら断った
	static bool Resolve(HitInfo& hit);
};
