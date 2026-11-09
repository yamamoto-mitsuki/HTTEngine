#include "Source/Scripts/HitTestPlayer.h"

#include <format>

#include "HTTEngine/Components/Combat/HealthComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/InputManager.h"
#include "Source/Scripts/TestBullet.h"

SCRIPT(HitTestPlayer, "Test");

namespace {
constexpr int kMaxJustCount = 3;                       // ジャストの連続数の上限（3 の次は 0 に戻る）
constexpr Vector3 kMuzzleOffset = {0.0f, 1.0f, 0.6f};  // 弾を出す所（足元からのずれ）
} // namespace


//=============================================================================
// 毎フレーム
//=============================================================================
void HitTestPlayer::Update() {
	// --- 無敵（I で切り替えて、Health に書く。エンジンはここを見て断る）---
	if (InputManager::IsKeyTriggered(DIK_I)) {
		isInvincible_ = !isInvincible_;
		LogManager::Log(std::format("無敵 {}", isInvincible_ ? "ON" : "OFF"));
	}
	if (HealthComponent* health = GetComponent<HealthComponent>()) {
		health->invincible = isInvincible_;
	}

	// --- ジャスト判定（J を押した瞬間に、ジャスト判定の範囲に入っているか）---
	if (InputManager::IsKeyTriggered(DIK_J)) {
		JustCandidate just;
		if (HitSystem::TryConsumeJust(gameObject.handle, just)) {
			justCount_ = (justCount_ >= kMaxJustCount) ? 0 : justCount_ + 1;
			LogManager::Log(std::format("ジャスト！ {}（連続 {}・Active まで {:.2f} 秒）", GameObject{just.hitbox}.GetName(), justCount_, just.timeToActive));
		} else {
			LogManager::Log("ジャストではない");
		}
	}

	// --- 撃つ（前 +Z へ。当てる相手は Enemy レイヤー）---
	if (InputManager::IsKeyTriggered(DIK_F)) {
		Shoot();
	}
}

// ===== 前へ弾を撃つ =====
void HitTestPlayer::Shoot() {
	const Vector3 position = transform.translation + kMuzzleOffset;
	SpawnTestBullet(position, {0.0f, 0.0f, bulletSpeed_}, gameObject.handle, Layers::GetMask({"Enemy"}), bulletDamage_);
}


//=============================================================================
// 攻撃の当たり
//=============================================================================
// ===== 受ける前（K を押している間はガード）=====
void HitTestPlayer::OnBeforeHitTaken(HitInfo& hit) {
	if (InputManager::IsKeyPressed(DIK_K)) {
		hit.result = HitResult::Guarded;
		hit.damage *= guardRate_;
	}
}

// ===== 受けた =====
void HitTestPlayer::OnHitTaken(const HitInfo& hit) {
	justCount_ = 0; // 被弾で連続数は 0 に戻る
	LogManager::Log(std::format("被弾 {}：{} {:.1f}（残り {:.1f}）位置({:.2f}, {:.2f}, {:.2f}) 向き({:.2f}, {:.2f}, {:.2f})", GameObject{hit.hitbox}.GetName(),
	                            hit.result == HitResult::Guarded ? "ガード" : "ダメージ", hit.damage, hit.healthAfter, hit.point.x, hit.point.y, hit.point.z, hit.direction.x,
	                            hit.direction.y, hit.direction.z));
}

// ===== 当てた =====
void HitTestPlayer::OnHitDealt(const HitInfo& hit) {
	LogManager::Log(std::format("命中 {}：{:.1f}（倍率 {:.1f}・残り {:.1f}{}）", GameObject{hit.receiver}.GetName(), hit.damage, hit.hurtboxMultiplier, hit.healthAfter, hit.isDefeated ? "・撃破" : ""));
}


//=============================================================================
// Inspector と保存
//=============================================================================
void HitTestPlayer::Fields(ComponentUI& ui) {
	ui.Field("Guard Rate###guardRate", guardRate_, Range(0.0f, 1.0f).Tip("K を押している間に受けたダメージの倍率"));
	ui.Field("Bullet Speed###bulletSpeed", bulletSpeed_, AtLeast(0.0f).Tip("F で撃つ弾の速さ（m/s）"));
	ui.Field("Bullet Damage###bulletDamage", bulletDamage_, AtLeast(0.0f).Tip("F で撃つ弾のダメージ"));
}
