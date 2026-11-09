#pragma once
#include "HTTEngine/ECS/Script.h"

/// <summary>
/// 攻撃の当たり・ダメージを手で試すプレイヤー役（Sandbox のテスト用）
/// <para>I：無敵の切り替え（Health の Invincible）／J：ジャスト判定（HitSystem::TryConsumeJust。入っていれば連続数 +1）／K を押している間：ガード（OnBeforeHitTaken で Guarded・量 × Guard Rate）／F：前（+Z）へ弾を撃つ</para>
/// <para>同じ Entity に Health・Hurtbox・Character Controller（CharacterKeyboardMover で歩く）を付ける。当たり・ジャストは Console に出す</para>
/// </summary>
class HitTestPlayer : public Script {
public:
	void Update() override;
	void OnBeforeHitTaken(HitInfo& hit) override;
	void OnHitTaken(const HitInfo& hit) override;
	void OnHitDealt(const HitInfo& hit) override;
	void Fields(ComponentUI& ui) override;

private:
	// 前へ弾を撃つ（Hitbox・Damage Source・TestBullet を付けた Entity を作る）
	void Shoot();

	float guardRate_ = 0.2f;      // ガードしたときのダメージの倍率
	float bulletSpeed_ = 30.0f;   // 撃つ弾の速さ（m/s）
	float bulletDamage_ = 20.0f;  // 撃つ弾のダメージ
	bool isInvincible_ = false;   // 無敵（I で切り替え）
	int justCount_ = 0;           // ジャストの連続数（被弾で 0 に戻る。3 の次も 0 に戻る）
};
