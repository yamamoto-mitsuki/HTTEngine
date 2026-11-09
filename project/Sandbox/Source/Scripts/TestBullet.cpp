#include "Source/Scripts/TestBullet.h"

#include "HTTEngine/Components/Combat/DamageSourceComponent.h"
#include "HTTEngine/Components/Combat/HitboxComponent.h"
#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Core/NameList.h"

namespace {
constexpr float kBulletRadius = 0.15f; // 弾の当たる形の半径（m）
constexpr float kBulletScale = 0.3f;   // 弾の見た目の球の大きさ
} // namespace

//=============================================================================
// 弾
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TestBullet, "Test") {
	ui.Field("Velocity###velocity", value.velocity, Tip("速さ（m/s。ワールド座標）"));
	ui.Field("Life Time###lifeTime", value.lifeTime, AtLeast(0.0f).Tip("残りの寿命（秒）"));
}

// ===== 動かす・消す =====
void TestBulletUpdate(Handle<Entity> entity, TestBullet& bullet, TransformComponent& transform, HitboxComponent& hitbox, float deltaTime) {
	// 当てきった（前のフレームの終わりに HitSystem が数えた）か、寿命が尽きたら消える
	bullet.lifeTime -= deltaTime;
	if (hitbox.IsSpent() || bullet.lifeTime <= 0.0f) {
		EntityManager::Destroy(entity);
		return;
	}
	transform.translation += bullet.velocity * deltaTime;
}
SYSTEM(TestBulletUpdate);

// ===== 弾を1つ作る =====
GameObject SpawnTestBullet(const Vector3& position, const Vector3& velocity, Handle<Entity> shooter, LayerMask targets, float damage) {
	const GameObject bullet = GameObject::Create("Bullet");
	bullet.transform.translation = position;
	bullet.transform.scale = {kBulletScale, kBulletScale, kBulletScale};
	bullet.layer = GameObject{shooter}.layer;

	// --- 当たる形：出たときから Active、1体に当たったら Off（Is Spent）---
	HitboxComponent hitbox;
	hitbox.shape.type = HitShapeType::Sphere;
	hitbox.shape.radius = kBulletRadius / kBulletScale; // 大きさを掛けると kBulletRadius になる
	hitbox.targets = targets;
	hitbox.instigator = EntityManager::RefOf(shooter);
	hitbox.phase = HitboxPhase::Active;
	hitbox.maxTargets = 1;
	hitbox.justEnabled = false; // 弾はジャスト判定に使わない
	bullet.AddComponent(hitbox);

	DamageSourceComponent source;
	source.damage = damage;
	bullet.AddComponent(source);

	TestBullet move;
	move.velocity = velocity;
	bullet.AddComponent(move);

	PrimitiveRendererComponent look;
	look.shape = PrimitiveShape::Sphere;
	look.material.color = {1.0f, 0.6f, 0.2f, 1.0f};
	bullet.AddComponent(look);
	return bullet;
}


//=============================================================================
// 弾を撃つ係
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TestBulletSpawner, "Test") {
	ui.Field("Interval###interval", value.interval, AtLeast(0.01f).Tip("撃つ間隔（秒）"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("弾の速さ（m/s）。速くてもすり抜けないかを見る"));
	ui.Field("Damage###damage", value.damage, AtLeast(0.0f).Tip("弾のダメージ"));
	ui.NameMaskField("Targets###targets", value.targets.bits, Layers::GetNameList(), Tip("弾が当たるレイヤー"));
}

// ===== 撃つ =====
void TestBulletSpawnerUpdate(Handle<Entity> entity, TestBulletSpawner& spawner, TransformComponent& transform, float deltaTime) {
	spawner.timer -= deltaTime;
	if (spawner.timer > 0.0f) {
		return;
	}
	spawner.timer += spawner.interval;
	SpawnTestBullet(GetWorldPosition(transform), GetWorldForward(transform) * spawner.speed, entity, spawner.targets, spawner.damage);
}
SYSTEM(TestBulletSpawnerUpdate);
