#include "Source/Scripts/BurstEffectSamples.h"

#include "MyEngine/Components/Effects/ParticleSystemComponent.h"
#include "MyEngine/ECS/GameObject.h"
#include "MyEngine/Math/RandomEngine.h"

namespace {
// ===== コードで作るときの、飛び散る物の見た目 =====
constexpr int kBurstCount = 24;                           // 1回に出す粒の数
constexpr float kBurstDuration = 0.1f;                    // 1回の長さ（秒。Burst だけなので短くてよい）
constexpr Vector2 kBurstSpeed = {3.0f, 6.0f};             // 飛び散る速さ（m/s）
constexpr Vector2 kBurstLifetime = {0.3f, 0.6f};          // 粒が消えるまでの秒
constexpr Vector2 kBurstSize = {0.15f, 0.3f};             // 粒の大きさ（m）
constexpr float kBurstDrag = 3.0f;                        // すぐ止まる火花にする
constexpr float kBurstIntensity = 3.0f;                   // ブルームで光る明るさ
constexpr Vector4 kBurstColor = {1.0f, 0.6f, 0.2f, 1.0f}; // 橙の火花
} // namespace


//=============================================================================
// BurstSpawner
//=============================================================================
// ===== 見せ方 =====
COMPONENT(BurstSpawner, "Test") {
	ui.Field("Interval###interval", value.interval, AtLeast(0.01f).Tip("出す間隔（秒）"));
	ui.AssetField("Prefab###prefab", value.prefab, AssetType::Prefab, Tip("飛び散る物の Prefab（一番上の Particle System を Looping なし・Stop Action を Destroy に）。無ければコードで作る"));
	ui.Field("Spread###spread", value.spread, AtLeast(0.0f).Tip("出す所を左右（X）にずらす幅（m）"));
}

// ===== 出す =====
void BurstSpawnerUpdate(Handle<Entity>, BurstSpawner& spawner, TransformComponent& transform, float deltaTime) {
	spawner.timer -= deltaTime;
	if (spawner.timer > 0.0f) {
		return;
	}
	spawner.timer += spawner.interval;

	// --- 出す所（この Entity の位置から、左右にずらす）---
	Vector3 position = GetWorldPosition(transform);
	position.x += RandomEngine::GetFloat(-spawner.spread, spawner.spread);

	// --- Prefab：Instantiate（Component は次のフレームの頭に付く。そのフレームに Play On Start で出る）---
	if (spawner.prefab != 0) {
		GameObject::Instantiate(spawner.prefab, position);
		return;
	}

	// --- コード：Entity を作って Particle System を付ける（これも付くのは次のフレームの頭）---
	const GameObject burst = GameObject::Create("Burst");
	burst.transform.translation = position;

	ParticleSystemComponent particle;
	particle.playOnStart = true;                          // 付いたフレームから出す
	particle.looping = false;                             // 1回だけ
	particle.duration = kBurstDuration;
	particle.stopAction = ParticleStopAction::Destroy;    // 出し終わったら（粒が全部消えたら）Entity ごと消える
	particle.rateOverTime = 0.0f;                         // Burst だけ
	particle.burstCount = kBurstCount;
	particle.shape = ParticleShape::Point;
	particle.lifetime = kBurstLifetime;
	particle.speed = kBurstSpeed;
	particle.size = kBurstSize;
	particle.drag = kBurstDrag;
	particle.endSizeRate = 0.0f;                          // 縮んで消える
	particle.colorA = kBurstColor;
	particle.colorB = kBurstColor;
	particle.intensity = kBurstIntensity;
	burst.AddComponent(particle);
}
SYSTEM(BurstSpawnerUpdate);
