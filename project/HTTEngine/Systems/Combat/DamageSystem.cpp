#include "DamageSystem.h"

#include <algorithm>
#include <memory>

#include "HTTEngine/Combat/DamageTypes.h"
#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
#include "HTTEngine/Time/EngineTime.h"

namespace {
constexpr uint32_t kMaxReceiverDepth = 64; // 受ける本体を探して、親を何段までたどるか（EntityManager の親子の深さの上限と同じ）

//=============================================================================
// 見せ方（Inspector と保存の両方に使う）
//=============================================================================
// ===== Health =====
void DescribeHealth(ComponentUI& ui, HealthComponent& value) {
	ui.Field("Max Health###maxHealth", value.maxHealth, AtLeast(0.0f).Tip("最大の HP"));
	ui.Field("Health###health", value.health, AtLeast(0.0f).Tip("今の HP（保存した値から始まる）。0 以下なら倒れている（もう当たらない）"));
	ui.Field("Invincible###invincible", value.invincible, Tip("無敵（ゲームが書く）。攻撃を断る＝記録しないので、切れた後に同じ攻撃の範囲にいれば当たる"));
	ui.Field("Invincible After Hit###invincibleAfterHit", value.invincibleAfterHit, AtLeast(0.0f).Tip("当たった後、この秒だけ攻撃を断る（被弾の後の無敵）"));
	ui.NameMaskField("Immune Types###immuneTypes", value.immuneTypes, DamageTypes::GetNameList(), Tip("効かない攻撃の種類（断る）。名前は Settings の DamageTypes"));
}

// ===== Damage Source =====
void DescribeDamageSource(ComponentUI& ui, DamageSourceComponent& value) {
	ui.Field("Damage###damage", value.damage, AtLeast(0.0f).Tip("ダメージの量（Hurtbox の倍率・距離の倍率を掛ける前）"));
	ui.NameField("Damage Type###damageType", value.damageType, DamageTypes::GetNameList(), Tip("攻撃の種類（受ける側の Immune Types・OnBeforeHitTaken で使う）。名前は Settings の DamageTypes"));
	ui.Separator("Falloff");
	ui.AssetField("Falloff Curve###falloffCurve", value.falloffCurve, AssetType::Curve, Tip("距離で変わる倍率（横軸＝距離を Near〜Far で 0〜1 に直した値、縦軸＝倍率）。(none) なら倍率は 1"));
	ui.BeginDisabled(value.falloffCurve == 0);
	ui.Field("Falloff Near###falloffNear", value.falloffNear, AtLeast(0.0f).Tip("この距離（m）で、カーブの横軸が 0。距離は Hitbox が Active になった所から（弾なら撃った所から）"));
	ui.Field("Falloff Far###falloffFar", value.falloffFar, AtLeast(0.0f).Tip("この距離（m）で、カーブの横軸が 1（それより遠くても 1）"));
	ui.EndDisabled();
	ui.Separator("Feedback");
	ui.Field("Hit Stop###hitStop", value.hitStop, AtLeast(0.0f).Tip("当たったときのヒットストップの秒（0 なら無し）。断られた・防がれたときは出ない"));
	ui.Field("Shake###shake", value.shake, AtLeast(0.0f).Tip("当たったときのカメラの揺れの強さ（0 なら無し）。向きは攻撃の向き"));
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void DamageSystem::Initialize() {
	// Health・Damage Source の置き場所・保存・Inspector を登録する
	EntityManager::RegisterComponent<HealthComponent>();
	ComponentSerializer::Register<HealthComponent>("Health", &DescribeHealth);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HealthComponent>>("Health", "Combat", &DescribeHealth));
	EntityManager::RegisterComponent<DamageSourceComponent>();
	ComponentSerializer::Register<DamageSourceComponent>("Damage Source", &DescribeDamageSource);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<DamageSourceComponent>>("Damage Source", "Combat", &DescribeDamageSource));

	// 当たりの層に答え方を登録する（当たりの層は、Health・Damage Source を知らない）
	HitSystem::SetReceiverFinder(&DamageSystem::FindReceiver);
	HitSystem::SetHitResolver(&DamageSystem::Resolve);
}

void DamageSystem::Release() {}


//=============================================================================
// 更新
//=============================================================================
void DamageSystem::Update(bool isGameRunning, float deltaTime) {
	if (!isGameRunning || deltaTime <= 0.0f) {
		return;
	}
	// 被弾の後の無敵の残りを減らす
	EntityManager::ForEach<HealthComponent>([deltaTime](Handle<Entity>, HealthComponent& health) { health.invincibleTimer = (std::max)(health.invincibleTimer - deltaTime, 0.0f); });
}


//=============================================================================
// 受ける本体
//=============================================================================
Handle<Entity> DamageSystem::FindReceiver(Handle<Entity> hurtbox) {
	// 自分から親へたどって、Health の付いた一番近い物（輪になっていても止まるように、回数に上限）
	Handle<Entity> current = hurtbox;
	for (uint32_t i = 0; i < kMaxReceiverDepth && current.IsValid(); ++i) {
		if (EntityManager::Get<HealthComponent>(current) != nullptr) {
			return current;
		}
		const Entity* entity = EntityManager::Get(current);
		current = (entity != nullptr) ? entity->parent : Handle<Entity>{};
	}
	return hurtbox;
}


//=============================================================================
// 量
//=============================================================================
float DamageSystem::CalcFalloff(const DamageSourceComponent& source, float distance) {
	const Curve* curve = CurveManager::Get(source.falloffCurve);
	if (curve == nullptr) {
		return 1.0f;
	}
	const float range = source.falloffFar - source.falloffNear;
	const float t = (range > 0.0f) ? std::clamp((distance - source.falloffNear) / range, 0.0f, 1.0f) : 0.0f;
	return curve->Evaluate(t);
}


//=============================================================================
// 当たり1件を受ける
//=============================================================================
bool DamageSystem::Resolve(HitInfo& hit) {
	HealthComponent* health = EntityManager::Get<HealthComponent>(hit.receiver);
	const DamageSourceComponent* source = EntityManager::Get<DamageSourceComponent>(hit.hitbox);
	hit.damageType = (source != nullptr) ? source->damageType : DamageTypes::kDefault;

	// --- 1. 断る（倒れている・無敵・効かない種類）---
	if (health != nullptr) {
		const bool isImmune = hit.damageType >= 0 && hit.damageType < 32 && ((health->immuneTypes >> hit.damageType) & 1u) != 0u;
		if (health->IsDefeated() || health->IsInvincible() || isImmune) {
			return false;
		}
	}

	// --- 2. 量（Damage × Hurtbox の倍率 × 距離の倍率）---
	hit.result = HitResult::Hit;
	hit.damage = (source != nullptr) ? source->damage * hit.hurtboxMultiplier * CalcFalloff(*source, hit.distance) : 0.0f;

	// --- 3. 受ける側の Script に聞く（ガード・条件付きの無敵・量の書き換え）---
	ScriptSystem::SendBeforeHitTaken(hit.receiver, hit);
	if (hit.result == HitResult::Ignored) {
		return false;
	}

	// --- 4. HP を減らす ---
	if (health != nullptr) {
		health->health = (std::max)(health->health - (std::max)(hit.damage, 0.0f), 0.0f);
		hit.isDefeated = health->IsDefeated();
		hit.healthAfter = health->health;
		if (hit.result == HitResult::Hit && health->invincibleAfterHit > 0.0f) {
			health->invincibleTimer = health->invincibleAfterHit;
		}
	}

	// --- 5. 手応え（当たったときだけ。防いだときは出さない）---
	if (source != nullptr && hit.result == HitResult::Hit) {
		if (source->hitStop > 0.0f) {
			Time::HitStop(source->hitStop);
		}
		if (source->shake > 0.0f) {
			CameraSystem::AddImpulse(source->shake, hit.direction);
		}
	}
	return true;
}
