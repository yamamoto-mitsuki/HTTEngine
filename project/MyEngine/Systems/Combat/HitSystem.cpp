#include "HitSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/ECS/Layers.h"
#include "MyEngine/Editor/Gizmos/GizmoLines.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"

namespace {
// ===== すり抜けない判定 =====
constexpr float kMinMotion = 1.0e-4f;   // これより短い動きは、動いていないとみなす（m）
constexpr float kMinSweepStep = 0.01f;  // 動いた跡を調べる間隔の下限（m）
constexpr int kMaxSweepSteps = 32;      // 動いた跡を何か所まで調べるか（これより速いと、間隔が Hurtbox の半径より広くなる）
constexpr float kMaxHurtboxMotion = 5.0f; // 1フレームにこれより大きく動いた Hurtbox は、ワープ（置き直し）とみなして跡を調べない（m。60fps で 300m/s）

// ===== ギズモ =====
constexpr int kContourCells = 40;         // 床に置く形の縁を探す格子の、1辺のます目の数
constexpr float kContourPadding = 0.05f;  // 格子を形より少し広くする割合（縁が格子の端にかからないように）
constexpr int kVerticalLineEvery = 6;     // 縁の線分いくつごとに、縦の線（柱の高さ）を1本引くか
constexpr float kNoFadeStart = 10000.0f;  // 距離でフェードさせないための値（Collider と同じ）
constexpr float kNoFadeEnd = 20000.0f;
constexpr Vector4 kOffColor = {0.6f, 0.6f, 0.6f, 0.5f};       // Off の Hitbox（灰色）
constexpr Vector4 kWarningColor = {1.0f, 0.85f, 0.1f, 1.0f};  // Warning の Hitbox（黄色）
constexpr Vector4 kActiveColor = {1.0f, 0.2f, 0.2f, 1.0f};    // Active の Hitbox（赤）
constexpr Vector4 kJustColor = {0.2f, 0.9f, 1.0f, 1.0f};      // ジャスト判定の範囲（水色）
constexpr Vector4 kJustOffColor = {0.2f, 0.9f, 1.0f, 0.35f};  // Off の間のジャスト判定の範囲（うすい水色）
constexpr Vector4 kHurtboxColor = {0.4f, 1.0f, 0.5f, 1.0f};   // Hurtbox（緑）

//=============================================================================
// 実行中の値
//=============================================================================
/// <summary>
/// 攻撃の回1つの中で、相手1体に何をしたかの記録
/// </summary>
struct HitRecord {
	Handle<Entity> receiver;  // 受ける本体
	uint32_t serial = 0;      // どの攻撃の回の記録か（Warning の間にジャストでよけた物は、これから始まる回の番号）
	float lastHitTime = 0.0f; // 最後に当てた時刻（攻撃の回が始まってからの秒。多段ヒットの間隔に使う）
	bool isHit = false;       // 当てた
	bool isDodged = false;    // ジャストでよけられた（もう当てない・もうジャストにならない）
};

/// <summary>
/// Hitbox 1つの実行中の値（数が変わる記録を持つので、バイト列で写す Component には入れない）
/// </summary>
struct HitboxRuntime {
	HitboxPhase lastPhase = HitboxPhase::Off;   // 前の Update の段階（Off → Active を見つけて、新しい攻撃の回にする）
	uint32_t serial = 0;                        // 記録が今どの攻撃の回か
	float attackTime = 0.0f;                    // 攻撃の回が始まってからの秒
	Vector3 activationOrigin = {0.0f, 0.0f, 0.0f}; // Active になった所（距離の倍率に使う）
	Vector3 previousOrigin = {0.0f, 0.0f, 0.0f};   // 前のフレームの形の中心（動いた跡を調べる）
	bool hasPrevious = false;                   // previousOrigin が使える（同じ攻撃の回で、前のフレームも Active だった）
	std::vector<HitRecord> records;             // 相手ごとの記録
	uint32_t seenFrame = 0;                     // 最後に見つけたフレーム（外された Hitbox を片付ける）
};

/// <summary>
/// 集めたやられ判定1つ（Update の初めに、今の Transform で作る）
/// </summary>
struct WorldHurtbox {
	Handle<Entity> entity;   // Hurtbox の Entity
	Handle<Entity> receiver; // 受ける本体
	Capsule capsule{};       // 形（ワールド座標）
	int layer = 0;           // Entity のレイヤー
	float multiplier = 1.0f; // ダメージの倍率
	Vector3 motion = {0.0f, 0.0f, 0.0f}; // 前のフレームからの動き（すり抜けない判定で、相手の動きを引く）
};

/// <summary>
/// 前のフレームのやられ判定の位置
/// </summary>
struct HurtboxHistory {
	Vector3 center = {0.0f, 0.0f, 0.0f};
	uint32_t seenFrame = 0;
};

/// <summary>
/// HitSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct HitState {
	std::unordered_map<uint64_t, HitboxRuntime> hitboxes;   // Hitbox ごとの実行中の値（キーは Handle）
	std::unordered_map<uint64_t, HurtboxHistory> histories; // Hurtbox ごとの前のフレームの位置
	std::vector<WorldHurtbox> hurtboxes; // このフレームのやられ判定
	std::vector<HitInfo> hits;           // このフレームの当たり
	uint32_t frame = 0;                  // Update を呼んだ回数（片付けに使う）
	HitSystem::ReceiverFinder receiverFinder = nullptr; // 受ける本体を決める係（ダメージの層）
	HitSystem::HitResolver hitResolver = nullptr;       // 受けるか決める係（ダメージの層）
	bool showGizmos = true;              // ギズモを描くか
	Renderer::LineListConfig gizmoLines; // ギズモの線（使い回す）
};
std::unique_ptr<HitState> state;

// Handle を1つの数にする（実行中の値の表のキー）
uint64_t KeyOf(Handle<Entity> entity) { return (static_cast<uint64_t>(entity.index) << 32) | entity.generation; }

// 撃った本人（Instigator が空・居なければ、Hitbox の Entity）
Handle<Entity> FindInstigator(Handle<Entity> hitboxEntity, const HitboxComponent& hitbox) {
	const Handle<Entity> instigator = EntityManager::Find(hitbox.instigator);
	return instigator.IsValid() ? instigator : hitboxEntity;
}

// 自分・撃った本人・その親子の Hurtbox には当てない
bool IsOwnSide(const WorldHurtbox& hurtbox, Handle<Entity> hitboxEntity, Handle<Entity> instigator) {
	return EntityManager::IsRelated(hurtbox.entity, hitboxEntity) || EntityManager::IsRelated(hurtbox.entity, instigator) || EntityManager::IsRelated(hurtbox.receiver, instigator);
}

// 記録を探す（無ければ nullptr）
HitRecord* FindRecord(HitboxRuntime& runtime, Handle<Entity> receiver, uint32_t serial) {
	const auto it = std::ranges::find_if(runtime.records, [&](const HitRecord& record) { return record.receiver == receiver && record.serial == serial; });
	return (it != runtime.records.end()) ? &*it : nullptr;
}

//=============================================================================
// 窓
//=============================================================================
// ===== Active にする（新しい攻撃の回）=====
void BeginActive(HitboxComponent& hitbox, float activeSeconds) {
	hitbox.phase = HitboxPhase::Active;
	hitbox.activeSeconds = activeSeconds;
	hitbox.phaseTime = 0.0f;
	++hitbox.attackSerial;
	hitbox.hitCount = 0;
}

// ===== Warning にする（activateAfterWarning なら、warningSeconds 後に activeSeconds の Active へ）=====
void BeginWarning(HitboxComponent& hitbox, float warningSeconds, bool activateAfterWarning, float activeSeconds) {
	hitbox.phase = HitboxPhase::Warning;
	hitbox.warningSeconds = (std::max)(warningSeconds, 0.0f);
	hitbox.activateAfterWarning = activateAfterWarning;
	hitbox.activeSeconds = (std::max)(activeSeconds, 0.0f);
	hitbox.phaseTime = 0.0f;
}

// ===== 時間で Warning → Active に進める（Active → Off は、当たりを調べた後）=====
void AdvanceWarning(HitboxComponent& hitbox, float deltaTime) {
	hitbox.phaseTime += deltaTime;
	if (hitbox.phase == HitboxPhase::Warning && hitbox.activateAfterWarning && hitbox.warningSeconds > 0.0f && hitbox.phaseTime >= hitbox.warningSeconds) {
		const float over = hitbox.phaseTime - hitbox.warningSeconds; // 越えた分は、Active の時間に入れる
		BeginActive(hitbox, hitbox.activeSeconds);
		hitbox.phaseTime = over;
	}
}

// ===== 時間で Active → Off に進める（どんなに短い窓でも、1フレームは当たりを調べてから閉じる）=====
void AdvanceActive(HitboxComponent& hitbox) {
	if (hitbox.phase == HitboxPhase::Active && hitbox.activeSeconds > 0.0f && hitbox.phaseTime >= hitbox.activeSeconds) {
		hitbox.phase = HitboxPhase::Off;
		hitbox.phaseTime = 0.0f;
	}
}

//=============================================================================
// やられ判定を集める
//=============================================================================
void CollectHurtboxes() {
	state->hurtboxes.clear();
	EntityManager::ForEach<HurtboxComponent>([](Handle<Entity> entity, HurtboxComponent& hurtbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const Entity* owner = EntityManager::Get(entity);
		if (!hurtbox.enabled || transform == nullptr || owner == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		WorldHurtbox world;
		world.entity = entity;
		world.receiver = HitSystem::FindReceiver(entity);
		world.capsule = HitSystem::CalcWorldHurtbox(hurtbox, *transform);
		world.layer = owner->layer;
		world.multiplier = hurtbox.damageMultiplier;
		// 前のフレームからの動き（初めて見つけた Hurtbox・ワープした Hurtbox は、動いていないとみなす。通っていない所の攻撃に当たらないように）
		const Vector3 center = (world.capsule.start + world.capsule.end) * 0.5f;
		HurtboxHistory& history = state->histories[KeyOf(entity)];
		const Vector3 motion = center - history.center;
		const bool isContinuous = (history.seenFrame + 1 == state->frame) && Length(motion) <= kMaxHurtboxMotion;
		world.motion = isContinuous ? motion : Vector3{0.0f, 0.0f, 0.0f};
		history.center = center;
		history.seenFrame = state->frame;
		state->hurtboxes.push_back(world);
	});
	// 見つからなかった Hurtbox の位置は捨てる（外された・無効になった）
	std::erase_if(state->histories, [](const auto& item) { return item.second.seenFrame != state->frame; });
}

//=============================================================================
// 当たりを調べる
//=============================================================================
/// <summary>
/// 動いた跡を調べた結果
/// </summary>
struct SweepResult {
	WorldHitShape shape;                  // 当たった時の形の位置
	Vector3 nearest = {0.0f, 0.0f, 0.0f}; // Hurtbox の線分の上で、形に一番近い点
	float fraction = 1.0f;                // 動きの何割で当たったか（0＝前のフレームの位置、1＝今の位置）
};

// ===== 形を、前のフレームの位置から今の位置まで少しずつ動かして、最初に重なる所を探す =====
// relativeMotion は Hurtbox から見た形の動き（形の動き − Hurtbox の動き）。Hurtbox は今の位置に置いたまま、形の方を戻して調べる
// 間隔は Hurtbox の半径まで（それより広いと、細い形がその間を飛び越える）
bool Sweep(const WorldHitShape& shape, const Capsule& capsule, const Vector3& relativeMotion, SweepResult& result) {
	const float motionLength = Length(relativeMotion);
	if (motionLength < kMinMotion) {
		result.shape = shape;
		result.fraction = 1.0f;
		return shape.Overlaps(capsule, 0.0f, &result.nearest);
	}
	const int steps = std::clamp(static_cast<int>(std::ceil(motionLength / (std::max)(capsule.radius, kMinSweepStep))), 1, kMaxSweepSteps);
	for (int i = 0; i <= steps; ++i) {
		const float fraction = static_cast<float>(i) / static_cast<float>(steps);
		WorldHitShape moved = shape;
		moved.origin = shape.origin - relativeMotion * (1.0f - fraction);
		if (moved.Overlaps(capsule, 0.0f, &result.nearest)) {
			result.shape = moved;
			result.fraction = fraction;
			return true;
		}
	}
	return false;
}

// ===== 当たり1件の知らせを作る（当たりの層の欄だけ）=====
HitInfo MakeHitInfo(Handle<Entity> hitboxEntity, Handle<Entity> instigator, const HitboxComponent& hitbox, const HitboxRuntime& runtime, const WorldHurtbox& hurtbox,
                    const SweepResult& sweep, const Vector3& relativeMotion) {
	HitInfo hit;
	hit.hitbox = hitboxEntity;
	hit.instigator = instigator;
	hit.receiver = hurtbox.receiver;
	hit.hurtbox = hurtbox.entity;
	hit.attackSerial = hitbox.attackSerial;
	hit.hurtboxMultiplier = hurtbox.multiplier;
	// --- 当たった位置：線分の点から、形の方へ半径だけ出した所（Hurtbox の表面）。線分が形の中なら、線分の点 ---
	const WorldHitShape& shape = sweep.shape;
	const float distance = shape.CalcSignedDistance(sweep.nearest);
	const Vector3 normal = shape.CalcNormal(sweep.nearest);
	hit.point = sweep.nearest - normal * std::clamp(distance, 0.0f, hurtbox.capsule.radius);
	// --- 向き：動いている攻撃は動いた向き。止まっている攻撃は、形の真ん中から当たった位置へ（床に置く形は床に平らに）---
	if (Length(relativeMotion) >= kMinMotion) {
		hit.direction = Normalize(relativeMotion);
	} else {
		Vector3 outward = hit.point - shape.origin;
		if (IsFloorShape(shape.type)) {
			outward -= shape.up * Dot(outward, shape.up);
		}
		hit.direction = (Length(outward) >= kMinMotion) ? Normalize(outward) : shape.forward;
	}
	hit.distance = Length(hit.point - runtime.activationOrigin);
	return hit;
}

/// <summary>
/// 当たりの候補（受ける本体1体につき1つ）
/// </summary>
struct HitCandidate {
	const WorldHurtbox* hurtbox = nullptr; // 一番倍率の大きい Hurtbox
	SweepResult sweep;                     // その Hurtbox との当たり
	Vector3 relativeMotion = {0.0f, 0.0f, 0.0f};
};

// ===== Active の Hitbox 1つの当たりを調べて、記録して知らせる =====
void DetectHits(Handle<Entity> entity, HitboxComponent& hitbox, HitboxRuntime& runtime, const WorldHitShape& shape) {
	const Handle<Entity> instigator = FindInstigator(entity, hitbox);
	// 窓が開いた最初のフレームは、跡を調べない（形も Hurtbox も。開く前の位置で当たらないように）
	const bool canSweep = runtime.hasPrevious;
	const Vector3 shapeMotion = canSweep ? shape.origin - runtime.previousOrigin : Vector3{0.0f, 0.0f, 0.0f};

	// --- 1. 重なっている Hurtbox を、受ける本体ごとに1つ選ぶ（頭と手の両方に入っても1回。倍率の大きい方）---
	static std::vector<HitCandidate> candidates; // 毎フレーム配列を作り直さないように使い回す
	candidates.clear();
	for (const WorldHurtbox& hurtbox : state->hurtboxes) {
		if (!hitbox.targets.Contains(hurtbox.layer) || IsOwnSide(hurtbox, entity, instigator)) {
			continue;
		}
		HitCandidate candidate;
		candidate.hurtbox = &hurtbox;
		candidate.relativeMotion = canSweep ? shapeMotion - hurtbox.motion : Vector3{0.0f, 0.0f, 0.0f};
		if (!Sweep(shape, hurtbox.capsule, candidate.relativeMotion, candidate.sweep)) {
			continue;
		}
		const auto same = std::ranges::find_if(candidates, [&](const HitCandidate& other) { return other.hurtbox->receiver == hurtbox.receiver; });
		if (same == candidates.end()) {
			candidates.push_back(candidate);
		} else if (hurtbox.multiplier > same->hurtbox->multiplier) {
			*same = candidate;
		}
	}
	// 動いた跡の、先に触れた相手から（弾が2体に重なっても、手前の1体に当たる）
	std::ranges::sort(candidates, {}, [](const HitCandidate& candidate) { return candidate.sweep.fraction; });

	// --- 2. 記録を見て、当てる ---
	for (const HitCandidate& candidate : candidates) {
		const Handle<Entity> receiver = candidate.hurtbox->receiver;
		HitRecord* record = FindRecord(runtime, receiver, runtime.serial);
		if (record != nullptr && record->isDodged) {
			continue; // ジャストでよけられた
		}
		const bool isFirstHit = (record == nullptr || !record->isHit);
		if (!isFirstHit && (hitbox.rehitInterval <= 0.0f || runtime.attackTime - record->lastHitTime < hitbox.rehitInterval)) {
			continue; // もう当てた（多段ヒットなら、間隔がたっていない）
		}
		if (isFirstHit && hitbox.IsSpent()) {
			continue; // 当てられる相手の数を使いきった
		}
		HitInfo hit = MakeHitInfo(entity, instigator, hitbox, runtime, *candidate.hurtbox, candidate.sweep, candidate.relativeMotion);
		// ダメージの層に聞く（断られたら記録しない＝無敵が切れた後に、まだ範囲にいれば当たる）
		if (state->hitResolver != nullptr && !state->hitResolver(hit)) {
			continue;
		}
		if (record == nullptr) {
			runtime.records.push_back({receiver, runtime.serial});
			record = &runtime.records.back();
		}
		record->isHit = true;
		record->lastHitTime = runtime.attackTime;
		if (isFirstHit) {
			++hitbox.hitCount;
		}
		state->hits.push_back(hit);
	}
	// 当てきったら閉じる（弾は Is Spent を見て消す）
	if (hitbox.IsSpent()) {
		hitbox.phase = HitboxPhase::Off;
		hitbox.phaseTime = 0.0f;
	}
}

// ===== Hitbox 1つを1フレーム進める =====
void UpdateHitbox(Handle<Entity> entity, HitboxComponent& hitbox, float deltaTime) {
	HitboxRuntime& runtime = state->hitboxes[KeyOf(entity)];
	runtime.seenFrame = state->frame;

	// --- 1. 時間で Warning → Active ---
	AdvanceWarning(hitbox, deltaTime);
	// phase を直接 Active に書いた（Timeline・Inspector）：新しい攻撃の回にする
	if (hitbox.phase == HitboxPhase::Active && runtime.lastPhase != HitboxPhase::Active && hitbox.attackSerial == runtime.serial) {
		BeginActive(hitbox, hitbox.activeSeconds);
		hitbox.phaseTime = deltaTime;
	}
	// 予告をやめた（Warning → Off）：その予告の間にジャストでよけた記録は捨てる（次の攻撃は別の攻撃）
	if (hitbox.phase == HitboxPhase::Off && runtime.lastPhase == HitboxPhase::Warning) {
		std::erase_if(runtime.records, [&](const HitRecord& record) { return record.serial == hitbox.attackSerial + 1; });
	}

	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		runtime.lastPhase = hitbox.phase;
		runtime.hasPrevious = false; // 無効の間に動いた分を、通り道とみなさない
		return;
	}
	const WorldHitShape shape = HitSystem::CalcWorldShape(hitbox, *transform);

	// --- 2. 攻撃の回が変わった：記録を空にする（Warning の間にジャストでよけた、この回の分だけ残す）---
	if (hitbox.attackSerial != runtime.serial) {
		runtime.serial = hitbox.attackSerial;
		runtime.attackTime = 0.0f;
		runtime.activationOrigin = shape.origin;
		runtime.hasPrevious = false; // 窓が開いた最初のフレームは、跡を調べない（出てきた瞬間を、通り道とみなさない）
		std::erase_if(runtime.records, [&](const HitRecord& record) { return record.serial != runtime.serial; });
	}

	// --- 3. 当たり ---
	if (hitbox.phase == HitboxPhase::Active) {
		runtime.attackTime += deltaTime;
		DetectHits(entity, hitbox, runtime, shape);
	}

	// --- 4. 時間で Active → Off ---
	AdvanceActive(hitbox);
	runtime.previousOrigin = shape.origin;
	runtime.hasPrevious = (hitbox.phase == HitboxPhase::Active);
	runtime.lastPhase = hitbox.phase;
}

//=============================================================================
// ジャスト判定
//=============================================================================
// ===== 受ける本体の Hurtbox が、有効なジャスト判定の範囲に入っている攻撃を探す =====
bool FindJustCandidate(Handle<Entity> receiver, JustCandidate& out, Handle<Entity>& foundEntity) {
	// --- 受ける本体の Hurtbox（今の Transform で）---
	struct OwnHurtbox {
		Capsule capsule;
		int layer = 0;
	};
	std::vector<OwnHurtbox> own;
	EntityManager::ForEach<HurtboxComponent>([&](Handle<Entity> entity, HurtboxComponent& hurtbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const Entity* owner = EntityManager::Get(entity);
		if (hurtbox.enabled && transform != nullptr && owner != nullptr && EntityManager::IsActiveInHierarchy(entity) && HitSystem::FindReceiver(entity) == receiver) {
			own.push_back({HitSystem::CalcWorldHurtbox(hurtbox, *transform), owner->layer});
		}
	});
	if (own.empty()) {
		return false;
	}

	// --- 有効な Hitbox の中で、Active に一番近い物 ---
	bool isFound = false;
	EntityManager::ForEach<HitboxComponent>([&](Handle<Entity> entity, HitboxComponent& hitbox) {
		if (!hitbox.justEnabled || hitbox.phase == HitboxPhase::Off || hitbox.IsSpent()) {
			return;
		}
		// Warning は、Active になる Just Lead Time 秒前から（Warning の長さが 0 なら、いつ Active になるか分からないので、ずっと）
		const bool isWarning = (hitbox.phase == HitboxPhase::Warning);
		const float timeToActive = isWarning ? hitbox.GetWarningRemaining() : 0.0f;
		if (isWarning && hitbox.warningSeconds > 0.0f && timeToActive > hitbox.justLeadTime) {
			return;
		}
		if (isFound && timeToActive >= out.timeToActive) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const Handle<Entity> instigator = FindInstigator(entity, hitbox);
		if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity) || EntityManager::IsRelated(receiver, instigator) || EntityManager::IsRelated(receiver, entity)) {
			return;
		}
		// その攻撃の回で、もう当たった・よけた相手はジャストにならない（Warning の間は、これから始まる回）
		const uint32_t serial = isWarning ? hitbox.attackSerial + 1 : hitbox.attackSerial;
		if (const auto runtime = state->hitboxes.find(KeyOf(entity)); runtime != state->hitboxes.end()) {
			if (FindRecord(runtime->second, receiver, serial) != nullptr) {
				return;
			}
		}
		const WorldHitShape shape = HitSystem::CalcWorldShape(hitbox, *transform);
		const bool isInside = std::ranges::any_of(own, [&](const OwnHurtbox& hurtbox) { return hitbox.targets.Contains(hurtbox.layer) && shape.Overlaps(hurtbox.capsule, hitbox.justMargin); });
		if (!isInside) {
			return;
		}
		out = {entity, instigator, serial, timeToActive};
		foundEntity = entity;
		isFound = true;
	});
	return isFound;
}

//=============================================================================
// ギズモ
//=============================================================================
// ===== 床に置く形の縁（符号付き距離 ＝ level の線）を、格子で探して線にする（marching squares）=====
// 格子の点ごとに距離を測り、ます目の4つの角で符号が変わる辺に、縁が通る点を置いてつなぐ（形の種類を知らなくても、同じ式から描ける）
void PushFloorContour(std::vector<Renderer::LineSegment>& lines, const WorldHitShape& shape, float level, float y, float pillarHeight, const Vector4& color) {
	// --- 格子の範囲（形の縁 ＋ level が入る広さ）---
	float minX = 0.0f;
	float maxX = 0.0f;
	float minZ = 0.0f;
	float maxZ = 0.0f;
	if (shape.type == HitShapeType::Line) {
		const float halfWidth = shape.width * 0.5f + level;
		minX = -halfWidth;
		maxX = halfWidth;
		minZ = -level;
		maxZ = shape.length + level;
	} else {
		const float extent = shape.radius + level;
		minX = -extent;
		maxX = extent;
		minZ = -extent;
		maxZ = extent;
	}
	const float padX = (maxX - minX) * kContourPadding + 0.01f;
	const float padZ = (maxZ - minZ) * kContourPadding + 0.01f;
	minX -= padX;
	maxX += padX;
	minZ -= padZ;
	maxZ += padZ;

	// --- 格子の点ごとの距離（縁の上が 0）---
	constexpr int kPoints = kContourCells + 1; // 1辺の点の数
	constexpr int kCorners = 4;                // ます目の角の数
	static std::vector<float> values(kPoints * kPoints);
	const float stepX = (maxX - minX) / kContourCells;
	const float stepZ = (maxZ - minZ) / kContourCells;
	for (int j = 0; j < kPoints; ++j) {
		for (int i = 0; i < kPoints; ++i) {
			values[j * kPoints + i] = shape.CalcSignedDistance2D({minX + stepX * i, minZ + stepZ * j}) - level;
		}
	}

	// --- ます目ごとに、縁が通る辺を探してつなぐ ---
	int segmentCount = 0;
	const auto pushSegment = [&](const Vector2& a, const Vector2& b) {
		lines.push_back({shape.ToWorld({a.x, y, a.y}), shape.ToWorld({b.x, y, b.y}), color});
		// ところどころに縦の線（柱の高さが分かるように）
		if (pillarHeight > 0.0f && (segmentCount++ % kVerticalLineEvery) == 0) {
			lines.push_back({shape.ToWorld({a.x, 0.0f, a.y}), shape.ToWorld({a.x, pillarHeight, a.y}), color});
		}
	};
	for (int j = 0; j < kContourCells; ++j) {
		for (int i = 0; i < kContourCells; ++i) {
			// 角：0＝左下、1＝右下、2＝右上、3＝左上（反時計回り）
			const std::array<Vector2, kCorners> corners = {
			    Vector2{minX + stepX * i,       minZ + stepZ * j      },
			    Vector2{minX + stepX * (i + 1), minZ + stepZ * j      },
			    Vector2{minX + stepX * (i + 1), minZ + stepZ * (j + 1)},
			    Vector2{minX + stepX * i,       minZ + stepZ * (j + 1)},
			};
			const std::array<float, kCorners> v = {values[j * kPoints + i], values[j * kPoints + i + 1], values[(j + 1) * kPoints + i + 1], values[(j + 1) * kPoints + i]};
			// 辺 k（角 k → 角 k+1）で符号が変われば、縁が通る点を、距離の比で置く
			// 4つとも符号が変わる（鞍の形）なら、crossings[k] がそのまま辺 k の点になる
			std::array<Vector2, kCorners> crossings{};
			int count = 0;
			for (int k = 0; k < kCorners; ++k) {
				const int next = (k + 1) % kCorners;
				if ((v[k] < 0.0f) != (v[next] < 0.0f)) {
					const float t = v[k] / (v[k] - v[next]);
					crossings[count] = corners[k] + (corners[next] - corners[k]) * t;
					++count;
				}
			}
			if (count == 2) {
				pushSegment(crossings[0], crossings[1]);
			} else if (count == kCorners) {
				// 向かい合う角だけが中（鞍の形）：ます目の真ん中が中なら、中の2つの角はつながっている
				const bool isCenterInside = (v[0] + v[1] + v[2] + v[3]) * 0.25f < 0.0f;
				const bool isFirstInside = v[0] < 0.0f;
				if (isCenterInside == isFirstInside) {
					pushSegment(crossings[0], crossings[1]); // 角 1 を切り取る
					pushSegment(crossings[2], crossings[3]); // 角 3 を切り取る
				} else {
					pushSegment(crossings[3], crossings[0]); // 角 0 を切り取る
					pushSegment(crossings[1], crossings[2]); // 角 2 を切り取る
				}
			}
		}
	}
}

// ===== 攻撃の形1つ（margin だけ太らせた形）=====
void PushHitShape(std::vector<Renderer::LineSegment>& lines, const WorldHitShape& shape, float margin, const Vector4& color, bool drawPillars) {
	switch (shape.type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
	case HitShapeType::Line:
		// 底と上の縁（太らせた形は、上下にも margin だけ広い）
		PushFloorContour(lines, shape, margin, -margin, drawPillars ? shape.height : 0.0f, color);
		PushFloorContour(lines, shape, margin, shape.height + margin, 0.0f, color);
		break;
	case HitShapeType::Sphere:
		GizmoLines::PushWireSphere(lines, shape.origin, shape.radius + margin, color);
		break;
	case HitShapeType::Capsule:
		GizmoLines::PushWireCapsule(lines, shape.origin, shape.origin + shape.forward * shape.length, shape.radius + margin, color);
		break;
	}
}

//=============================================================================
// 見せ方（Inspector と保存の両方に使う）
//=============================================================================
// ===== 攻撃の形 =====
void DescribeHitShape(ComponentUI& ui, HitShape& shape) {
	ui.Field("Type###shapeType", shape.type, Tip("Circle・Sector・Line は床に置く形（ローカルの XZ に描いて、上 +Y へ Height だけ伸ばした柱）。Sphere・Capsule は空中の形"));
	ui.Field("Center###shapeCenter", shape.center, Tip("Entity の位置からのずれ（ローカル。床に置く形は底の真ん中）"));
	switch (shape.type) {
	case HitShapeType::Circle:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径"));
		ui.Field("Inner Radius###shapeInnerRadius", shape.innerRadius, AtLeast(0.0f).Tip("内側の半径（0 なら穴なし。入れるとドーナツ）"));
		ui.Field("Height###shapeHeight", shape.height, AtLeast(0.0f).Tip("底から上への高さ（これより高く跳べば当たらない）"));
		break;
	case HitShapeType::Sector:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径"));
		ui.Field("Inner Radius###shapeInnerRadius", shape.innerRadius, AtLeast(0.0f).Tip("内側の半径（0 なら穴なし）"));
		ui.Field("Angle###shapeAngle", shape.angle, Range(0.0f, 360.0f).Tip("開き（度）。前 +Z を真ん中に、左右に半分ずつ"));
		ui.Field("Height###shapeHeight", shape.height, AtLeast(0.0f).Tip("底から上への高さ"));
		break;
	case HitShapeType::Line:
		ui.Field("Width###shapeWidth", shape.width, AtLeast(0.0f).Tip("幅"));
		ui.Field("Length###shapeLength", shape.length, AtLeast(0.0f).Tip("長さ（Center から前 +Z へ）"));
		ui.Field("Height###shapeHeight", shape.height, AtLeast(0.0f).Tip("底から上への高さ"));
		break;
	case HitShapeType::Sphere:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径（Entity の大きさの一番大きい軸を掛ける）"));
		break;
	case HitShapeType::Capsule:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径"));
		ui.Field("Length###shapeLength", shape.length, AtLeast(0.0f).Tip("線分の長さ（Center から前 +Z へ。両端の半球は入れない）"));
		break;
	}
}

// ===== Hitbox =====
void DescribeHitbox(ComponentUI& ui, HitboxComponent& value) {
	ui.Separator("Shape");
	DescribeHitShape(ui, value.shape);
	ui.Separator("Targets");
	ui.NameMaskField("Targets###targets", value.targets.bits, Layers::GetNameList(), Tip("当てる相手のレイヤー（相手の Hurtbox の Entity のレイヤーで見る）"));
	ui.Field("Instigator###instigator", value.instigator, Tip("撃った本人（空ならこの Entity）。本人とその親子には当たらない。弾は撃った物が作るときに書く"));
	ui.Separator("Window");
	ui.Field("Phase###phase", value.phase, Tip("今の段階（Off・Warning＝予告・Active＝当たる）。保存した値が、Play したとき・Prefab から作ったときの段階になる"));
	ui.Field("Warning Seconds###warningSeconds", value.warningSeconds, AtLeast(0.0f).Tip("Warning の長さ（秒。0 ならずっと）"));
	ui.Field("Activate After Warning###activateAfterWarning", value.activateAfterWarning, Tip("Warning が終わったら Active にする"));
	ui.Field("Active Seconds###activeSeconds", value.activeSeconds, AtLeast(0.0f).Tip("Active の長さ（秒。0 ならずっと）"));
	ui.Separator("Hits");
	ui.Field("Rehit Interval###rehitInterval", value.rehitInterval, AtLeast(0.0f).Tip("同じ相手にもう一度当たるまでの秒（0 なら、1回の攻撃で1回だけ。正なら多段ヒット）"));
	ui.Field("Max Targets###maxTargets", value.maxTargets, AtLeast(0.0f).Tip("1回の攻撃で当てられる相手の数（0 なら何体でも。弾は 1。当てきったら Off になる）"));
	ui.Separator("Just");
	// Just Enabled がオフでも、下の2つは出したまま・保存したままにする（オンに戻したとき、前の値が残っているように）
	ui.Field("Just Enabled###justEnabled", value.justEnabled, Tip("ジャスト判定に使うか（オフならジャストにならない当たり。弾・ジャストを見せたくない攻撃）"));
	ui.Field("Just Margin###justMargin", value.justMargin, AtLeast(0.0f).Tip("ジャスト判定の範囲：形をこの距離だけ太らせた範囲（m。0 なら形そのものの中だけ。ギズモは形の線がそのままジャストの範囲になる）"));
	ui.Field("Just Lead Time###justLeadTime", value.justLeadTime, AtLeast(0.0f).Tip("Active になる何秒前から、ジャスト判定の範囲が有効か（Warning の長さが 0 なら、Warning の間ずっと）"));
}

// ===== Hurtbox =====
void DescribeHurtbox(ComponentUI& ui, HurtboxComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Shape###shape", value.shape);
	ui.Field("Center###center", value.center, Tip("Entity の位置からのずれ（ローカル。回転と大きさも掛かる）"));
	ui.Field("Radius###radius", value.radius, AtLeast(0.0f).Tip("半径（Entity の大きさの X と Z の大きい方を掛ける。球は一番大きい軸）"));
	if (value.shape == HurtboxShape::Capsule) {
		ui.Field("Height###height", value.height, AtLeast(0.0f).Tip("下の端から上の端まで（半球も入れる）。ローカルの上 +Y に立ち、Entity と一緒に回る"));
	}
	ui.Field("Damage Multiplier###damageMultiplier", value.damageMultiplier, AtLeast(0.0f).Tip("ダメージの倍率（弱点なら 1 より大きく）。1回の攻撃が同じ本体のいくつもの Hurtbox に入ったら、倍率の大きい方に当たる"));
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void HitSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<HitState>();
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;

	// Hitbox・Hurtbox の置き場所・保存・Inspector を登録する（見せ方は1つの関数で、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<HitboxComponent>();
	ComponentSerializer::Register<HitboxComponent>("Hitbox", &DescribeHitbox);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HitboxComponent>>("Hitbox", "Combat", &DescribeHitbox));
	EntityManager::RegisterComponent<HurtboxComponent>();
	ComponentSerializer::Register<HurtboxComponent>("Hurtbox", &DescribeHurtbox);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HurtboxComponent>>("Hurtbox", "Combat", &DescribeHurtbox));
	EntityManager::AddDestroyListener(&HitSystem::OnEntityDestroyed);
}

void HitSystem::Release() { state.reset(); }


//=============================================================================
// 更新
//=============================================================================
void HitSystem::Update(bool isGameRunning, float deltaTime) {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	state->hits.clear();
	if (!isGameRunning) {
		// 停止中：実行中の値を全部捨てる（次の Play は記録の無い状態から）
		state->hitboxes.clear();
		state->histories.clear();
		state->hurtboxes.clear();
		return;
	}
	if (deltaTime <= 0.0f) {
		return; // 時間が止まっている（一時停止・ヒットストップ）：窓も当たりも進めない
	}
	++state->frame;
	CollectHurtboxes();
	EntityManager::ForEach<HitboxComponent>([deltaTime](Handle<Entity> entity, HitboxComponent& hitbox) { UpdateHitbox(entity, hitbox, deltaTime); });
	// 見つからなかった Hitbox の実行中の値を捨てる（Component を外された）
	std::erase_if(state->hitboxes, [](const auto& item) { return item.second.seenFrame != state->frame; });
}

// ===== Entity が消える直前 =====
void HitSystem::OnEntityDestroyed(Handle<Entity> entity) {
	if (state != nullptr) {
		state->hitboxes.erase(KeyOf(entity));
		state->histories.erase(KeyOf(entity));
	}
}


//=============================================================================
// 窓を開け閉めする
//=============================================================================
void HitSystem::Warn(Handle<Entity> hitbox, float warningSeconds) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		BeginWarning(*component, warningSeconds, false, component->activeSeconds);
	}
}

void HitSystem::Warn(Handle<Entity> hitbox, float warningSeconds, float activeSeconds) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		BeginWarning(*component, warningSeconds, true, activeSeconds);
	}
}

void HitSystem::Activate(Handle<Entity> hitbox, float activeSeconds) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		BeginActive(*component, (std::max)(activeSeconds, 0.0f));
	}
}

void HitSystem::Deactivate(Handle<Entity> hitbox) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		component->phase = HitboxPhase::Off;
		component->phaseTime = 0.0f;
	}
}


//=============================================================================
// 当たりの知らせ
//=============================================================================
std::span<const HitInfo> HitSystem::GetAllHits() {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	return state->hits;
}

void HitSystem::GetHitsDealt(Handle<Entity> entity, std::vector<HitInfo>& out) {
	out.clear();
	for (const HitInfo& hit : GetAllHits()) {
		if (hit.hitbox == entity || hit.instigator == entity) {
			out.push_back(hit);
		}
	}
}

void HitSystem::GetHitsTaken(Handle<Entity> receiver, std::vector<HitInfo>& out) {
	out.clear();
	for (const HitInfo& hit : GetAllHits()) {
		if (hit.receiver == receiver) {
			out.push_back(hit);
		}
	}
}


//=============================================================================
// ジャスト判定
//=============================================================================
bool HitSystem::FindJust(Handle<Entity> receiver, JustCandidate& out) {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	Handle<Entity> found;
	return FindJustCandidate(receiver, out, found);
}

bool HitSystem::TryConsumeJust(Handle<Entity> receiver, JustCandidate& out) {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	Handle<Entity> found;
	if (!FindJustCandidate(receiver, out, found)) {
		return false;
	}
	// 「よけた」と記録する。まだ Update で一度も見ていない Hitbox（このフレームに作った・窓を開けた）なら、今の回から始める
	const auto [it, isNew] = state->hitboxes.try_emplace(KeyOf(found));
	HitboxRuntime& runtime = it->second;
	if (isNew) {
		const HitboxComponent* hitbox = EntityManager::Get<HitboxComponent>(found);
		runtime.serial = hitbox->attackSerial;
		runtime.lastPhase = hitbox->phase;
		runtime.seenFrame = state->frame;
	}
	HitRecord* record = FindRecord(runtime, receiver, out.attackSerial);
	if (record == nullptr) {
		runtime.records.push_back({receiver, out.attackSerial});
		record = &runtime.records.back();
	}
	record->isDodged = true;
	return true;
}


//=============================================================================
// 形
//=============================================================================
WorldHitShape HitSystem::CalcWorldShape(const HitboxComponent& hitbox, const TransformComponent& transform) { return WorldHitShape::Make(hitbox.shape, transform.worldMatrix); }

Capsule HitSystem::CalcWorldHurtbox(const HurtboxComponent& hurtbox, const TransformComponent& transform) {
	const Matrix4x4& m = transform.worldMatrix;
	const Vector3 rowX = {m.m[0][0], m.m[0][1], m.m[0][2]};
	const Vector3 rowY = {m.m[1][0], m.m[1][1], m.m[1][2]};
	const Vector3 rowZ = {m.m[2][0], m.m[2][1], m.m[2][2]};
	const float scaleXZ = (std::max)(Length(rowX), Length(rowZ));
	const float scaleY = Length(rowY);
	const Vector3 center = TransformPoint(hurtbox.center, m);
	if (hurtbox.shape == HurtboxShape::Sphere) {
		const float radius = hurtbox.radius * (std::max)(scaleXZ, scaleY);
		return {center, center, radius};
	}
	// カプセル：ローカルの上に立つ線分（高さが半径の2倍より小さければ、球と同じ）
	const float radius = hurtbox.radius * scaleXZ;
	const float halfSegment = (std::max)(hurtbox.height * scaleY * 0.5f - radius, 0.0f);
	const Vector3 up = (scaleY > 0.0f) ? rowY / scaleY : Vector3{0.0f, 1.0f, 0.0f};
	return {center - up * halfSegment, center + up * halfSegment, radius};
}

Handle<Entity> HitSystem::FindReceiver(Handle<Entity> hurtbox) {
	return (state != nullptr && state->receiverFinder != nullptr) ? state->receiverFinder(hurtbox) : hurtbox;
}


//=============================================================================
// 答える係
//=============================================================================
void HitSystem::SetReceiverFinder(ReceiverFinder finder) {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	state->receiverFinder = finder;
}

void HitSystem::SetHitResolver(HitResolver resolver) {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	state->hitResolver = resolver;
}


//=============================================================================
// ギズモ
//=============================================================================
void HitSystem::DrawGizmos(Camera* camera) {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	if (!state->showGizmos) {
		return;
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	// 停止中も、今の Transform で描く（置きながら形を合わせられるように）
	EntityManager::ForEach<HitboxComponent>([&](Handle<Entity> entity, HitboxComponent& hitbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const WorldHitShape shape = CalcWorldShape(hitbox, *transform);
		Vector4 color = kOffColor;
		switch (hitbox.phase) {
		case HitboxPhase::Off:
			color = kOffColor;
			break;
		case HitboxPhase::Warning:
			color = kWarningColor;
			break;
		case HitboxPhase::Active:
			color = kActiveColor;
			break;
		}
		PushHitShape(gizmo.lines, shape, 0.0f, color, true);
		// ジャスト判定の範囲（Just Margin が 0 なら形の線と重なるので描かない。形の線がそのままジャストの範囲）
		if (hitbox.justEnabled && hitbox.justMargin > 0.0f) {
			PushHitShape(gizmo.lines, shape, hitbox.justMargin, (hitbox.phase == HitboxPhase::Off) ? kJustOffColor : kJustColor, false);
		}
	});
	EntityManager::ForEach<HurtboxComponent>([&](Handle<Entity> entity, HurtboxComponent& hurtbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (!hurtbox.enabled || transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const Capsule capsule = CalcWorldHurtbox(hurtbox, *transform);
		if (hurtbox.shape == HurtboxShape::Sphere) {
			GizmoLines::PushWireSphere(gizmo.lines, capsule.start, capsule.radius, kHurtboxColor);
		} else {
			GizmoLines::PushWireCapsule(gizmo.lines, capsule.start, capsule.end, capsule.radius, kHurtboxColor);
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}

bool& HitSystem::GetShowGizmos() {
	MY_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	return state->showGizmos;
}
