#include "CharacterControllerSystem.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/Physics/ColliderComponent.h"
#include "HTTEngine/Components/Physics/RigidbodyComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Physics/MotionQuery.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/Systems/Physics/PhysicsSystem.h"

namespace {
constexpr float kSkinWidth = CollisionSystem::kContactOffset * 0.5f; // 周りの物と空けておく隙間（m）。CollisionSystem の「触れている」距離より狭くして、乗っている床との当たり（OnCollisionStay）が切れないように
constexpr int kMaxSlideCount = 4;         // 1フレームに「当たって向きを変えて、残りを進む」回数の上限（角で何回も当たっても、ここで止める）
constexpr int kMaxDepenetrationCount = 4; // 入り込まれたときに外へ出す回数の上限（深い物から1つずつ出す）
constexpr float kMinMoveSq = 1e-10f;      // これより短い動きは動かない（長さの2乗。m²）
constexpr float kMinJumpSpeed = 0.001f;   // 地面にいるとき、上向きの速さがこれより大きければ「跳んだ」（m/s。計算の誤差で跳ばないように）
constexpr float kMinNormalUp = 1e-4f;     // 面の向きの「上」の成分の下限（0 で割らないように）
constexpr float kMinMass = 0.01f;         // 重さの下限（0で割らないように。Rigidbody と同じ）
constexpr float kMaxSlopeLimit = 89.0f;   // Slope Limit の上限（度。90 だと壁にも立ててしまう）
constexpr Vector4 kStepColor = {1.0f, 0.8f, 0.2f, 1.0f};   // 上れる段の高さの輪の色（黄）
constexpr Vector4 kGroundColor = {0.3f, 0.6f, 1.0f, 1.0f}; // 地面の向きの線の色（青）
constexpr float kGroundLineLength = 0.5f; // 地面の向きの線の長さ（m）
constexpr float kNoFadeStart = 10000.0f;  // 距離でフェードさせないための値（Collider のギズモと同じ）
constexpr float kNoFadeEnd = 20000.0f;

/// <summary>
/// CharacterControllerSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct ControllerSystemState {
	std::vector<ShapeOverlap> overlaps;  // Overlap の結果の置き場（配列のメモリを使い回す）
	Renderer::LineListConfig gizmoLines; // ギズモの線の置き場（同上）
};
std::unique_ptr<ControllerSystemState> state;

/// <summary>
/// 1体を動かす間だけ使う物
/// </summary>
struct Mover {
	Handle<Entity> entity;                              // 持ち主
	CharacterControllerComponent* controller = nullptr; // 動かしている間だけ持つ（その間、Component の配列は増えも減りもしない）
	WorldCollider shape;                                // 今の形（動かすたびに一緒にずらす）
	Vector3 moved = {0.0f, 0.0f, 0.0f};                 // このフレームに動いた量（ワールド座標）
	Vector3 up = {0.0f, 1.0f, 0.0f};                    // 上（重力の反対。長さ1）
	float walkableCos = 0.0f;                           // 面の向きと上の間の cos がこれ以上なら立てる（cos(Slope Limit)）
	ShapeQuery query;                                   // 調べる相手の決まり（Is Trigger は相手にしない。止めた軸へは押さない）
};

//=============================================================================
// 小さな道具
//=============================================================================
// 上の向きの分を抜く（地面に平らにする）
Vector3 Flatten(const Vector3& vector, const Vector3& up) { return vector - up * Dot(vector, up); }

// 面へ近づく分を抜く（離れる向きなら、そのまま）。面に沿った分だけが残る
Vector3 RemoveApproach(const Vector3& vector, const Vector3& normal) {
	const float approach = Dot(vector, normal);
	return (approach < 0.0f) ? vector - normal * approach : vector;
}

// 地面の上で横に horizontal だけ進む動き。坂なら、坂に沿うように上下を足す（横に進む量は坂でも同じ。Unreal と同じ）
//   地面を「groundNormal を向いた平面」とみて、horizontal ＋ 上 × k が平面に乗る k を解く：Dot(horizontal ＋ 上 × k, n) = 0 → k = −Dot(horizontal, n) ÷ Dot(上, n)
Vector3 AlongGround(const Vector3& horizontal, const Vector3& groundNormal, const Vector3& up) {
	const float normalUp = Dot(groundNormal, up);
	if (normalUp < kMinNormalUp) {
		return horizontal;
	}
	return horizontal - up * (Dot(horizontal, groundNormal) / normalUp);
}

// 立てる面から隙間（kSkinWidth）を空けるずれ。真上へ空ける（面に垂直に空けると、坂では下り側へずれる＝止まっていても少しずつ滑る）
// 面に垂直に測って kSkinWidth になるよう、上へ kSkinWidth ÷ cos（面の向きと上の間）だけ
Vector3 CalcLift(const Mover& mover, const Vector3& normal) { return mover.up * (kSkinWidth / (std::max)(Dot(normal, mover.up), kMinNormalUp)); }

// その Entity（か親）が Character Controller を持つか（体の Collider を子に付けたキャラも、キャラとして数える）
bool IsCharacter(Handle<Entity> entity) {
	for (Handle<Entity> current = entity; EntityManager::IsAlive(current); current = EntityManager::Get(current)->parent) {
		if (EntityManager::Get<CharacterControllerComponent>(current) != nullptr) {
			return true;
		}
	}
	return false;
}

// 押される Rigidbody（Is Kinematic でない物）。無ければ nullptr
RigidbodyComponent* FindPushable(Handle<Entity> entity) {
	RigidbodyComponent* rigidbody = EntityManager::Get<RigidbodyComponent>(entity);
	return (rigidbody != nullptr && !rigidbody->isKinematic) ? rigidbody : nullptr;
}

// 立てる面か（緩い坂・床。ほかのキャラの頭には立たない＝滑り落ちる。Unreal と同じ）
bool IsWalkable(const Mover& mover, const Vector3& normal, Handle<Entity> entity) { return Dot(normal, mover.up) >= mover.walkableCos && !IsCharacter(entity); }

// キャラを move だけ動かす（形も一緒に。止めた軸へは動かさない）
void Translate(Mover& mover, const Vector3& move) {
	const Vector3 free = Collision::RemoveLockedAxes(move, mover.query.lock);
	mover.shape.Translate(free);
	mover.moved += free;
}

// 立った地面を覚える
void SetGround(Mover& mover, const ShapeCastHit& hit) {
	CharacterControllerComponent& controller = *mover.controller;
	controller.movementMode = CharacterMovementMode::Walking;
	controller.groundNormal = hit.normal;
	controller.groundPoint = hit.point;
	controller.groundEntity = hit.entity;
}

// 同じ Entity の Collider から、動かす形を作る（球・カプセルだけ。無い・無効・ほかの形なら false）
// 球・カプセルでないと、足元（一番下の点）が決まらず、段・坂の上り方が決まらない
bool MakeShape(Handle<Entity> entity, WorldCollider& out) {
	const ColliderComponent* collider = EntityManager::Get<ColliderComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (collider == nullptr || transform == nullptr || !collider->enabled) {
		return false;
	}
	if (collider->shape != ColliderShape::Sphere && collider->shape != ColliderShape::Capsule) {
		return false;
	}
	out = CollisionSystem::CalcWorldCollider(entity, *collider, *transform);
	return true;
}

//=============================================================================
// 当たった物への働きかけ（当たった向きの印・当たりの知らせ・Rigidbody を押す）
//=============================================================================
// ===== Rigidbody を押す =====
// ぶつかった向き（水平）の相手の速さを「キャラの速さ × 重さの割合」まで上げる。キャラはこのフレームはそこで止まり、相手はこの後の物理が動かす
// 次のフレームは、相手が動いた分だけキャラも進める（押している間は、キャラも相手と同じ速さになる）
void PushRigidbody(const Mover& mover, const ShapeCastHit& hit) {
	const CharacterControllerComponent& controller = *mover.controller;
	RigidbodyComponent* rigidbody = controller.pushRigidbodies ? FindPushable(hit.entity) : nullptr;
	if (rigidbody == nullptr) {
		return;
	}
	// 押す向き：当たった面の向きの反対（キャラから相手へ）を水平にした物。上に乗っても、床へ押し付けない
	const Vector3 flat = Flatten(-hit.normal, mover.up);
	if (LengthSq(flat) <= kMinMoveSq) {
		return;
	}
	const Vector3 direction = Normalize(flat);
	// 押す速さ ＝ キャラのその向きの速さ × 重さの割合（PhysicsSystem の Rigidbody 同士と同じ形。重い物ほどゆっくり）
	const float pushMass = (std::max)(controller.pushMass, kMinMass);
	const float share = pushMass / (pushMass + (std::max)(rigidbody->mass, kMinMass));
	const float wanted = Dot(controller.velocity, direction) * share;
	const float current = Dot(rigidbody->velocity, direction);
	if (wanted <= current) {
		return; // もう同じか、それより速く離れている
	}
	const Collision::AxisLock lock = {rigidbody->freezePositionX, rigidbody->freezePositionY, rigidbody->freezePositionZ};
	rigidbody->velocity += Collision::RemoveLockedAxes(direction * (wanted - current), lock);
}

// ===== 当たった1件 =====
void OnHit(const Mover& mover, const ShapeCastHit& hit) {
	CharacterControllerComponent& controller = *mover.controller;
	// --- 当たった向きの印（面の向きで分ける。立てない急な坂は横）---
	const float normalUp = Dot(hit.normal, mover.up);
	if (normalUp >= mover.walkableCos) {
		controller.hitBelow = true;
	} else if (normalUp <= -mover.walkableCos) {
		controller.hitAbove = true;
	} else {
		controller.hitSides = true;
	}
	// --- 当たりの知らせ（Script の OnCollision〜）。速さを変える前の速さで知らせる。同じ相手は、そのフレームの最初の1回だけ覚えられる ---
	CollisionSystem::ReportImpact(mover.entity, hit.entity, hit.normal, controller.velocity, MotionQuery::GetVelocity(hit.entity));
	PushRigidbody(mover, hit);
}

//=============================================================================
// 1. 入り込まれていたら外へ出す（動く物・ほかのキャラに入り込まれた・置いた位置が重なっていた）
//=============================================================================
void Depenetrate(Mover& mover) {
	for (int i = 0; i < kMaxDepenetrationCount; ++i) {
		CollisionSystem::Overlap(mover.shape, state->overlaps, mover.query);
		// 一番深く入っている物から出す。押される Rigidbody からは出ない（相手の方が、物理でキャラ（壁）から押し出される）
		const ShapeOverlap* deepest = nullptr;
		for (const ShapeOverlap& overlap : state->overlaps) {
			if (FindPushable(overlap.entity) != nullptr) {
				continue;
			}
			if (deepest == nullptr || LengthSq(overlap.push) > LengthSq(deepest->push)) {
				deepest = &overlap;
			}
		}
		if (deepest == nullptr) {
			return; // 重なっていない
		}
		const Vector3 normal = Normalize(deepest->push);
		const float depth = Length(deepest->push) + kSkinWidth; // 出した後に隙間も空ける
		if (IsWalkable(mover, normal, deepest->entity)) {
			// 立てる面からは真上へ（面に垂直に出すと、坂では下り側へずれる）。面に垂直に測って depth 出るだけ上げる
			Translate(mover, mover.up * (depth / Dot(normal, mover.up)));
		} else {
			Translate(mover, normal * depth);
		}
	}
}

//=============================================================================
// 2. 速さ（跳んだら空中へ・空中なら重力・地面なら上下の速さは0）
//=============================================================================
void UpdateVelocity(Mover& mover, const Vector3& gravity, float deltaTime) {
	CharacterControllerComponent& controller = *mover.controller;
	if (controller.IsGrounded() && Dot(controller.velocity, mover.up) > kMinJumpSpeed) {
		controller.movementMode = CharacterMovementMode::Falling; // ゲームが上向きの速さを書いた＝跳んだ
	}
	switch (controller.movementMode) {
	case CharacterMovementMode::Walking:
		// 地面に沿って動くので、上下の速さは使わない（下向きを書いても地面へは潜らない）
		controller.velocity = Flatten(controller.velocity, mover.up);
		break;
	case CharacterMovementMode::Falling:
		if (controller.useGravity) {
			controller.velocity += gravity * (controller.gravityScale * deltaTime);
		}
		break;
	}
	controller.velocity = Collision::RemoveLockedAxes(controller.velocity, mover.query.lock);
}

//=============================================================================
// 3. 動かす
//=============================================================================
// ===== 段を上る =====
// 上へ Step Offset → 前へ → 下へ。下りた所が立てる面で、上った高さが Step Offset 以下なら、そこへ動かす（Unreal の StepUp と同じ手順）
// 上れたら motion を残りの横の動きにして true
bool TryStepUp(Mover& mover, Vector3& motion) {
	const CharacterControllerComponent& controller = *mover.controller;
	const Vector3 forward = Flatten(motion, mover.up);
	if (controller.stepOffset <= 0.0f || LengthSq(forward) <= kMinMoveSq) {
		return false; // 上る段が無い・横に進もうとしていない
	}
	WorldCollider probe = mover.shape; // 試しに動かす形（上れると決まるまで、キャラは動かさない）
	ShapeCastHit hit;
	// --- 1. 上へ（天井があれば、その手前まで）---
	float rise = controller.stepOffset;
	if (CollisionSystem::Cast(probe, mover.up * rise, hit, mover.query)) {
		rise = hit.distance - kSkinWidth;
	}
	if (rise <= kSkinWidth) {
		return false; // 上に余裕が無い
	}
	probe.Translate(mover.up * rise);
	// --- 2. 前へ（上った高さで、残りの横の動きだけ。途中で当たったら、そこまで）---
	float advance = 1.0f;
	if (CollisionSystem::Cast(probe, forward, hit, mover.query)) {
		advance = hit.fraction;
	}
	if (LengthSq(forward * advance) <= kMinMoveSq) {
		return false; // 上っても前へ進めない（段ではなく、高い壁）
	}
	probe.Translate(forward * advance);
	// --- 3. 下へ（上った分と隙間の分。その間に立てる面が無ければ、段ではない）---
	const float drop = rise + kSkinWidth * 2.0f;
	if (!CollisionSystem::Cast(probe, -mover.up * drop, hit, mover.query) || !IsWalkable(mover, hit.normal, hit.entity)) {
		return false;
	}
	// --- 4. 上った高さが段になっているか ---
	//   Step Offset を超えた：段の角に、形の丸みで乗り上げただけ
	//   隙間の分も上っていない：下りた所が元の床（当たったのは段ではなく壁。斜めに当たった壁で「高さ 0 の段」を上り続けて、沿って進めなくなる）
	const float climbed = rise - hit.distance + Dot(CalcLift(mover, hit.normal), mover.up);
	if (climbed > controller.stepOffset + kSkinWidth || climbed <= kSkinWidth) {
		return false;
	}
	// --- 5. 上る ---
	Translate(mover, mover.up * climbed + forward * advance);
	motion = forward * (1.0f - advance); // 残りの横の動き
	SetGround(mover, hit);
	return true;
}

// ===== 立てない面に沿うときに使う向き =====
//   地面にいる間：面の向きを水平にする（急な坂を上らない・壁に沿って上下へずれない）
//   空中でほかのキャラの頭に当たった：Slope Limit ちょうどの坂とみなす（真上に乗ったままにならず、滑り落ちる）
//   それ以外（空中で壁・急な坂・天井）：面の向きのまま（急な坂なら、沿って滑り落ちる）
Vector3 CalcSlideNormal(const Mover& mover, const ShapeCastHit& hit) {
	const Vector3 flat = Flatten(hit.normal, mover.up);
	if (mover.controller->IsGrounded()) {
		return (LengthSq(flat) > kMinMoveSq) ? Normalize(flat) : hit.normal;
	}
	if (Dot(hit.normal, mover.up) < mover.walkableCos) {
		return hit.normal;
	}
	// ここに来るのは「立てる角度だが、ほかのキャラ」だけ（立てる面は SlideMove で先に扱う）
	// 滑り落ちる向き：面の向きの水平の分。真上から乗った（水平の分が無い）なら、動いていた向き。それも無ければ X、Z の向き
	Vector3 away = flat;
	for (const Vector3& fallback : {Flatten(mover.controller->velocity, mover.up), Flatten({1.0f, 0.0f, 0.0f}, mover.up), Flatten({0.0f, 0.0f, 1.0f}, mover.up)}) {
		if (LengthSq(away) > kMinMoveSq) {
			break;
		}
		away = fallback;
	}
	const float sinLimit = std::sqrt((std::max)(1.0f - mover.walkableCos * mover.walkableCos, 0.0f));
	return Normalize(Normalize(away) * sinLimit + mover.up * mover.walkableCos);
}

// ===== 立てない面に当たったとき、速さから消す向き =====
//   壁・天井：面の向きのまま（壁へ向かう速さ・天井へ向かう上向きの速さを消す。飛びかかり・吹っ飛びのように、1回書いて放っておく動きも、壁で止まる）
//   上を向いた面（急な坂・ほかのキャラの頭）：面の向きを水平にした物（横に入り込む速さだけ消し、落ちる速さは残す）
//     落ちる速さまで消すと、毎フレーム X・Z を書くゲームでは、坂を滑る速さが毎フレーム消えて、ゆっくりしか滑り落ちない
//     落ちる速さを残して、動き（motion）の方を面に沿わせると、滑る速さが重力で増えていく（摩擦の無い坂と同じ、下向きに g × sin² の加速）
Vector3 CalcVelocityNormal(const Mover& mover, const Vector3& normal) {
	if (Dot(normal, mover.up) <= 0.0f) {
		return normal;
	}
	const Vector3 flat = Flatten(normal, mover.up);
	return (LengthSq(flat) > kMinMoveSq) ? Normalize(flat) : Vector3{0.0f, 0.0f, 0.0f}; // 真上を向いた面は、横の速さを消さない（RemoveApproach が何もしない）
}

// ===== 当たる所まで進んで、当たった面に沿って向きを変え、残りを進む（collide and slide）=====
void SlideMove(Mover& mover, Vector3 motion) {
	CharacterControllerComponent& controller = *mover.controller;
	for (int i = 0; i < kMaxSlideCount && LengthSq(motion) > kMinMoveSq; ++i) {
		ShapeCastHit hit;
		if (!CollisionSystem::Cast(mover.shape, motion, hit, mover.query)) {
			Translate(mover, motion);
			return; // 何にも当たらない
		}
		// --- 当たる所まで進む ---
		Translate(mover, motion * hit.fraction);
		motion *= 1.0f - hit.fraction; // 残り
		OnHit(mover, hit);

		// --- 立てる面（床・緩い坂）：真上へ隙間を空けて、残りを地面に沿わせる ---
		if (IsWalkable(mover, hit.normal, hit.entity)) {
			Translate(mover, CalcLift(mover, hit.normal));
			if (controller.IsGrounded() || Dot(controller.velocity, mover.up) <= 0.0f) {
				// 歩いて別の坂へ乗り移った・落ちてきて着地した：その面に立つ（下へ向かう速さは消す）
				controller.velocity = Flatten(controller.velocity, mover.up);
				SetGround(mover, hit);
				motion = AlongGround(Flatten(motion, mover.up), hit.normal, mover.up);
			} else {
				// 跳んで上っている途中に、坂の下側から当たった：沿って進む（まだ着地しない）
				motion = RemoveApproach(motion, hit.normal);
				controller.velocity = RemoveApproach(controller.velocity, CalcVelocityNormal(mover, hit.normal));
			}
			continue;
		}

		// --- 立てない面（壁・急な坂・天井・ほかのキャラ）：面から隙間を空ける ---
		Translate(mover, hit.normal * kSkinWidth);
		// 段を上る（地面にいて、横に進もうとしていたときだけ）
		if (controller.IsGrounded() && TryStepUp(mover, motion)) {
			continue;
		}
		// 面に沿う
		const Vector3 normal = CalcSlideNormal(mover, hit);
		motion = RemoveApproach(motion, normal);
		controller.velocity = RemoveApproach(controller.velocity, CalcVelocityNormal(mover, normal));
	}
}

//=============================================================================
// 4. 地面を探す（地面にいた間だけ。下り坂・段を下りるときに浮かないよう、下へ吸い付ける）
//=============================================================================
void FindGround(Mover& mover, float horizontalDistance) {
	CharacterControllerComponent& controller = *mover.controller;
	if (!controller.IsGrounded()) {
		return; // 空中（跳んだ・落ちている）なら、着地は SlideMove で見る
	}
	// 探す深さ：段の高さ ＋ 横に進んだ分だけ一番急な坂を下りた高さ ＋ 隙間（Unreal と同じ考え。項目は増やさない）
	const float tanLimit = std::sqrt((std::max)(1.0f - mover.walkableCos * mover.walkableCos, 0.0f)) / (std::max)(mover.walkableCos, kMinNormalUp);
	const float depth = controller.stepOffset + horizontalDistance * tanLimit + kSkinWidth * 2.0f;
	ShapeCastHit hit;
	if (CollisionSystem::Cast(mover.shape, -mover.up * depth, hit, mover.query) && IsWalkable(mover, hit.normal, hit.entity)) {
		// 隙間を残して下ろす（真上の向きに測る。面に垂直に下ろすと、坂では下り側へずれる）
		Translate(mover, -mover.up * hit.distance + CalcLift(mover, hit.normal));
		SetGround(mover, hit);
		OnHit(mover, hit); // 乗っている間も、毎フレーム当たりを知らせる（OnCollisionStay が切れないように）
		return;
	}
	controller.movementMode = CharacterMovementMode::Falling; // 足元に立てる面が無い（崖から出た・急な坂へ出た・ほかのキャラの頭）
}

//=============================================================================
// 1体を動かす
//=============================================================================
bool MoveController(Handle<Entity> entity, CharacterControllerComponent& controller, const Vector3& gravity, const Vector3& up, float deltaTime) {
	// --- 早期リターン ---
	if (!controller.enabled || !EntityManager::IsActiveInHierarchy(entity) || EntityManager::Get<RigidbodyComponent>(entity) != nullptr) {
		return false; // 止めてある・無効・Rigidbody が動かす
	}
	Mover mover;
	if (!MakeShape(entity, mover.shape)) {
		return false; // 形が無い
	}

	// --- 準備 ---
	mover.entity = entity;
	mover.controller = &controller;
	mover.up = up;
	mover.walkableCos = std::cos(std::clamp(controller.slopeLimit, 0.0f, kMaxSlopeLimit) * MathUtility::kDegToRad);
	mover.query.lock = {controller.freezePositionX, controller.freezePositionY, controller.freezePositionZ};
	controller.hitSides = false; // 当たった向きは毎フレーム作り直す
	controller.hitAbove = false;
	controller.hitBelow = false;

	// --- 1. 入り込まれていたら外へ → 2. 速さ ---
	Depenetrate(mover);
	UpdateVelocity(mover, gravity, deltaTime);

	// --- 3. 動かす。地面にいれば地面に沿って、空中なら速さのまま ---
	Vector3 motion = controller.velocity * deltaTime;
	float horizontalDistance = 0.0f;
	if (controller.IsGrounded()) {
		horizontalDistance = Length(motion); // 地面にいる間の velocity は水平
		motion = AlongGround(motion, controller.groundNormal, up);
	}
	SlideMove(mover, Collision::RemoveLockedAxes(motion, mover.query.lock));

	// --- 4. 地面を探す ---
	FindGround(mover, horizontalDistance);

	// --- 5. 動いた量を Transform へ。調べる相手の形も動かす（次のキャラが、動いた後の位置で調べる）---
	if (LengthSq(mover.moved) == 0.0f) {
		return false;
	}
	EntityManager::TranslateWorld(entity, mover.moved);
	CollisionSystem::MoveCollider(entity, mover.moved);
	return true;
}

//=============================================================================
// 動き方の窓口・Inspector と 保存
//=============================================================================
// 動き方の窓口（MotionQuery）への答え方：Character Controller が付いていれば、その速さと「地面にいるか」
bool GetControllerMotion(Handle<Entity> entity, MotionState& out) {
	const CharacterControllerComponent* controller = EntityManager::Get<CharacterControllerComponent>(entity);
	if (controller == nullptr) {
		return false;
	}
	out.velocity = controller->velocity;
	out.isGrounded = controller->IsGrounded();
	return true;
}

// Character Controller の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Rigidbody と同じ書き方）
void DescribeCharacterController(ComponentUI& ui, CharacterControllerComponent& value) {
	ui.Label("形は同じ Entity の Collider（Sphere か Capsule）。Rigidbody とは一緒に付けない");
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと動かさない（その場に止まる）"));
	ui.Field("Slope Limit###slopeLimit", value.slopeLimit, Range(0.0f, kMaxSlopeLimit).Tip("立てる坂の角度（度）。これより緩い面は地面（止まっても滑らない）。急な面は壁と同じく、沿って滑る"));
	ui.Field("Step Offset###stepOffset", value.stepOffset, AtLeast(0.0f).Tip("そのまま上れる段の高さ（m）。下り坂・段を下りるときに、地面へ吸い付く距離にも使う"));
	ui.Field("Use Gravity###useGravity", value.useGravity, Tip("空中で重力で落ちる（向きと強さは Settings → Physics。Rigidbody と同じ値）"));
	ui.Field("Gravity Scale###gravityScale", value.gravityScale, Tip("重力の倍率（2 で2倍速く落ちる。ジャンプの手触りの調整に）"));
	ui.Separator("Push");
	ui.Field("Push Rigidbodies###pushRigidbodies", value.pushRigidbodies, Tip("ぶつかった Rigidbody（Is Kinematic でない物）を押す。外すと止まるだけ（押したければ Script の OnCollision〜 で）"));
	ui.BeginDisabled(!value.pushRigidbodies);
	ui.Field("Push Mass###pushMass", value.pushMass, AtLeast(kMinMass).Tip("押すときのキャラの重さ（kg）。押す速さ ＝ キャラの速さ × Push Mass ÷ (Push Mass ＋ 相手の Mass)。重い物ほどゆっくり動く"));
	ui.EndDisabled();
	ui.Separator("Constraints");
	ui.Field("Freeze Position X###freezePositionX", value.freezePositionX);
	ui.Field("Freeze Position Y###freezePositionY", value.freezePositionY);
	ui.Field("Freeze Position Z###freezePositionZ", value.freezePositionZ, Tip("横から見るゲームなら止める"));
	ui.Separator("Info");
	ui.Field("Velocity###velocity", value.velocity,
	         Tip("今の速さ。ゲームは X・Z を書く（跳ぶなら Y に上向きの速さも）。Y は空中で重力が足され、地面にいる間は 0。壁に当たると壁へ向かう分が消える。停止中に入れた値は、Play を押したときの速さになる"));
	ui.Label(value.IsGrounded() ? "Is Grounded: 地面に乗っている" : "Is Grounded: 空中");
	const bool hasHit = value.hitBelow || value.hitSides || value.hitAbove;
	const std::string hits = std::format("Hit: {}{}{}{}", value.hitBelow ? "下 " : "", value.hitSides ? "横 " : "", value.hitAbove ? "上 " : "", hasHit ? "" : "なし");
	ui.Label(hits.c_str());
}

// 足元（形の一番下の点）と、形の太さ（ギズモの輪に使う）
void CalcFoot(const WorldCollider& shape, const Vector3& up, Vector3* foot, float* radius) {
	if (shape.shape == ColliderShape::Sphere) {
		*foot = shape.sphere.center - up * shape.sphere.radius;
		*radius = shape.sphere.radius;
		return;
	}
	// カプセル：線分の端のうち、下にある方の球の一番下
	const Capsule& capsule = shape.capsule;
	const Vector3& lower = (Dot(capsule.start, up) < Dot(capsule.end, up)) ? capsule.start : capsule.end;
	*foot = lower - up * capsule.radius;
	*radius = capsule.radius;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void CharacterControllerSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<ControllerSystemState>();
	// 線は距離でフェードさせない
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;
	state->gizmoLines.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く

	// Character Controller の置き場所・保存・Inspector を登録する。見せ方は DescribeCharacterController 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<CharacterControllerComponent>();
	ComponentSerializer::Register<CharacterControllerComponent>("Character Controller", &DescribeCharacterController);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CharacterControllerComponent>>("Character Controller", "Physics", &DescribeCharacterController));
	// 速さ・地面にいるかを、動き方の窓口から聞けるようにする（当たりの知らせ・カメラの追いかけ・Trigger が使う）
	MotionQuery::Register(&GetControllerMotion);
}

void CharacterControllerSystem::Release() { state.reset(); }


//=============================================================================
// 毎フレーム（再生中だけ）
//=============================================================================
bool CharacterControllerSystem::Update(float deltaTime) {
	HTT_ASSERT_MSG(state != nullptr, "CharacterControllerSystem::Initialize()を先に呼んでください");
	if (deltaTime <= 0.0f) {
		return false; // 一時停止・ヒットストップ中（速さも重力も変えない）
	}
	const Vector3 gravity = PhysicsSystem::GetGravity();
	const Vector3 up = PhysicsSystem::GetUp();
	bool isMoved = false;
	EntityManager::ForEach<CharacterControllerComponent>([&](Handle<Entity> entity, CharacterControllerComponent& controller) {
		if (MoveController(entity, controller, gravity, up, deltaTime)) {
			isMoved = true;
		}
	});
	return isMoved;
}


//=============================================================================
// ギズモ
//=============================================================================
void CharacterControllerSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "CharacterControllerSystem::Initialize()を先に呼んでください");
	if (!CollisionSystem::GetShowGizmos()) {
		return; // Collider のギズモと一緒に出す・消す
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	// 輪を描く面：上と直交する2本の向き（上が X に近ければ Z を使って、外積が 0 にならないように）
	const Vector3 up = PhysicsSystem::GetUp();
	const Vector3 side = Normalize(Cross(up, (std::abs(up.x) < 0.9f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 0.0f, 1.0f}));
	const Vector3 front = Cross(side, up);
	EntityManager::ForEach<CharacterControllerComponent>([&](Handle<Entity> entity, const CharacterControllerComponent& controller) {
		WorldCollider shape;
		if (!controller.enabled || !EntityManager::IsActiveInHierarchy(entity) || !MakeShape(entity, shape)) {
			return;
		}
		// --- 上れる段の高さ：足元から Step Offset の高さに、形と同じ太さの輪 ---
		Vector3 foot;
		float radius = 0.0f;
		CalcFoot(shape, up, &foot, &radius);
		GizmoLines::PushCircle(gizmo.lines, foot + up * controller.stepOffset, radius, side, front, kStepColor);
		// --- 地面の向き：地面に触れている所から、面の向きへ（地面にいる間だけ）---
		if (controller.IsGrounded()) {
			gizmo.lines.push_back({controller.groundPoint, controller.groundPoint + controller.groundNormal * kGroundLineLength, kGroundColor});
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}
