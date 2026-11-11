#include "CameraOrbit.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Camera/CameraLookAt.h"

namespace {
constexpr float kMaxPitch = 80.0f;        // 見下ろす・見上げる角度の上限（度）。真上・真下から見ると、横の向きが画面の回転になってしまう
constexpr float kMinDistance = 0.01f;     // 回る中心からカメラまでの長さの下限（m。0 だとカメラが回る中心に重なる）
constexpr float kMinFlatLengthSq = 1e-4f; // 地面に寝かせた向きの長さの2乗がこれより小さければ、横の向きが決まらない（真上・真下を向いている・2つの点が重なっている）

// ===== カメラを置く位置と向き =====
struct OrbitPose {
	Vector3 position;    // 位置（ワールド）
	Quaternion rotation; // 向き（ワールド。回る中心を見る）
};

//=============================================================================
// 向きの計算
//=============================================================================
// ===== 地面に寝かせた向きの、横の角度（度。0 が +Z・90 が +X。真上・真下を向いていて決まらなければ無し）=====
std::optional<float> CalcFlatYaw(const Vector3& direction) {
	if (direction.x * direction.x + direction.z * direction.z < kMinFlatLengthSq) {
		return std::nullopt;
	}
	return MathUtility::DirectionToYaw(direction);
}

// ===== Heading で決まる横の向き（度。Yaw を足す前。決まらなければ無し）=====
// pivot は今の回る中心（LookAt は、回る中心から見る点への向き。カメラはその反対の側に付く）
std::optional<float> CalcHeadingYaw(Handle<Entity> entity, const CameraOrbitComponent& orbit, const TransformComponent& target, const Vector3& pivot) {
	switch (orbit.heading) {
	case CameraOrbitHeading::World:
		return 0.0f; // Yaw がそのまま向き
	case CameraOrbitHeading::TargetForward:
		return CalcFlatYaw(GetWorldForward(target));
	case CameraOrbitHeading::LookAt: {
		// 見る点はなめらかにする前の点（Camera Look At の Smooth Time は写す向きにだけ効く。回り込みの遅れは Heading Smooth Time で決める）
		const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity);
		if (lookAt == nullptr || !lookAt->enabled) {
			return std::nullopt;
		}
		const std::optional<Vector3> point = CameraLookAt::FindWantedPoint(*lookAt);
		return point.has_value() ? CalcFlatYaw(*point - pivot) : std::nullopt;
	}
	}
	HTT_ASSERT_MSG(false, "知らない CameraOrbitHeading です");
	return std::nullopt;
}

// ===== 回り始めの横の向き（度）=====
// Heading で決まればそれ。決まらなければ、置いたカメラから回る中心への向き（置いた側から回り始める）。それも決まらなければ Yaw
float CalcStartYaw(Handle<Entity> entity, const CameraOrbitComponent& orbit, const TransformComponent& camera, const TransformComponent& target, const Vector3& pivot) {
	if (const std::optional<float> heading = CalcHeadingYaw(entity, orbit, target, pivot)) {
		return *heading + orbit.yaw;
	}
	return CalcFlatYaw(pivot - GetWorldPosition(camera)).value_or(orbit.yaw);
}

// ===== 回る中心と横の向きから、カメラを置く位置と向き =====
// 向きは X（見下ろす）→ Y（横）の順に回す（Inspector の Rotation と同じ Z → X → Y の順で、Z は 0＝傾けない）
// 位置は、回る中心からその向きの後ろへ Distance。なのでカメラはいつも回る中心を真ん中に見る
OrbitPose CalcPose(const CameraOrbitComponent& orbit, const Vector3& pivot, float yaw) {
	const float pitch = std::clamp(orbit.pitch, -kMaxPitch, kMaxPitch);
	const Quaternion rotation = MakeQuaternionFromEuler({pitch, yaw, 0.0f});
	const Vector3 forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	return {pivot - forward * (std::max)(orbit.distance, kMinDistance), rotation};
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraOrbit::Describe(ComponentUI& ui, CameraOrbitComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと動かさない（置いた位置・向きのまま）。同じ Entity の Camera Follow と両方入っているときは、こちらだけが動かす"));
	ui.Field("Target###target", value.target, Tip("追う Entity（Hierarchy からドラッグ）。再生中だけ、このまわりを回る位置へカメラを動かし、回る中心を向かせる"));
	ui.Field("Pivot Offset###pivotOffset", value.pivotOffset, Tip("追う物の位置から、回る中心へのずれ（m。ワールドの向き）。足元が原点のキャラなら、Y を胸・頭の高さにする"));
	ui.Field("Distance###distance", value.distance, AtLeast(kMinDistance).Tip("回る中心からカメラまでの長さ（m）"));
	ui.Field("Pitch###pitch", value.pitch, Range(-kMaxPitch, kMaxPitch).Tip("見下ろす角度（度）。＋ で上から見下ろす、− で下から見上げる"));
	ui.Separator("Heading");
	ui.Field(
	    "Heading###heading", value.heading,
	    Tip("カメラが、追う物のどちら側に付くか（Yaw の角度を足した向き）"));
	ui.Field(
	    "Yaw###yaw", value.yaw,
	    Tip("横の角度（度。＋ で上から見て時計回り）。World：向きそのもの / TargetForward・LookAt：決まった向きに足す角度（20 くらいずらすと、斜め後ろから肩越しに見る）"));
	ui.Field(
	    "Heading Smooth Time###headingSmoothTime", value.headingSmoothTime,
	    AtLeast(0.0f).Tip("横の向きが追いつくまでの時間の目安（秒）。大きいほどゆっくり回り込む（相手が追う物の上を飛び越えても、画面がガクッと回らない）。0 ならすぐ合わせる"));
	ui.Separator("Follow");
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("回る中心が、追う物に追いつくまでの時間の目安（秒）。走っている間は「速さ × Smooth Time」くらい遅れる。0 ならすぐ合わせる"));
	ui.Field(
	    "Show Gizmo###showGizmo", value.showGizmo,
	    Tip("Scene ビューに線を出す（エディタだけ）。黄：回る中心の十字と、追う物からの線 / 紫：回る輪と、回る中心からカメラを置く位置への線 / 赤：LookAt の見る点から回る中心への線。View メニューの Camera Orbits で全部まとめて消せる"));
}

//=============================================================================
// 回り込み
//=============================================================================
bool CameraOrbit::Update(Handle<Entity> entity, CameraOrbitComponent& orbit, float deltaTime) {
	// --- 早期リターン ---
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* target = EntityManager::Get<TransformComponent>(orbit.target);
	if (transform == nullptr || target == nullptr) {
		orbit.isOrbiting = false; // 追う物が入っていない・もう居ない（入ったら、そこから回り始める）
		return false;
	}
	const Vector3 wantedPivot = GetWorldPosition(*target) + orbit.pivotOffset;

	// --- 1. 回る中心を、追う物へなめらかに近づける ---
	if (!orbit.isOrbiting) {
		// 回り始め（Play・シーンを読んだ直後・有効にした直後）：置いた位置から流れてこないように、すぐ合わせる
		orbit.currentPivot = wantedPivot;
		orbit.currentYaw = CalcStartYaw(entity, orbit, *transform, *target, orbit.currentPivot);
		orbit.isOrbiting = true;
	} else {
		orbit.currentPivot += (wantedPivot - orbit.currentPivot) * MathUtility::CalcSmoothRate(deltaTime, orbit.smoothTime);
		// --- 2. 横の向きを、Heading で決まる向きへ一番近い回り方で近づける ---
		// 回る中心を動かした後に測る（LookAt は、見る点と今の回る中心を結んだ線の上にカメラが来る）。決まらないフレームは、向きをそのまま保つ
		if (const std::optional<float> heading = CalcHeadingYaw(entity, orbit, *target, orbit.currentPivot)) {
			orbit.currentYaw += MathUtility::DeltaAngle(orbit.currentYaw, *heading + orbit.yaw) * MathUtility::CalcSmoothRate(deltaTime, orbit.headingSmoothTime);
			orbit.currentYaw = std::remainder(orbit.currentYaw, 360.0f); // -180〜180 に収める（足し続けても大きくならないように）
		}
	}

	// --- 3. 位置と向きを Transform へ（親の下に置いたカメラでも、ワールドでその位置・向きになる）---
	const OrbitPose pose = CalcPose(orbit, orbit.currentPivot, orbit.currentYaw);
	EntityManager::TranslateWorld(entity, pose.position - GetWorldPosition(*transform));
	EntityManager::SetWorldRotation(entity, pose.rotation);
	return true;
}

//=============================================================================
// エディタの線
//=============================================================================
std::optional<CameraOrbit::Guide> CameraOrbit::CalcGuide(Handle<Entity> entity, const CameraOrbitComponent& orbit) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* target = EntityManager::Get<TransformComponent>(orbit.target);
	if (transform == nullptr || target == nullptr) {
		return std::nullopt;
	}
	Guide guide;
	guide.target = GetWorldPosition(*target);
	// 回っている間は今の値。停止中（回り始める前）は、回り始めに置かれる値（Update の回り始めと同じ）
	float yaw = orbit.currentYaw;
	guide.pivot = orbit.currentPivot;
	if (!orbit.isOrbiting) {
		guide.pivot = guide.target + orbit.pivotOffset;
		yaw = CalcStartYaw(entity, orbit, *transform, *target, guide.pivot);
	}
	guide.camera = CalcPose(orbit, guide.pivot, yaw).position;
	// 回る輪：Yaw だけを回したときにカメラが通る円（回る中心の真上・真下の、カメラの高さ）
	guide.ringRadius = (std::max)(orbit.distance, kMinDistance) * std::cos(std::clamp(orbit.pitch, -kMaxPitch, kMaxPitch) * MathUtility::kDegToRad);
	guide.ringHeight = guide.camera.y;
	if (orbit.heading == CameraOrbitHeading::LookAt) {
		const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity);
		if (lookAt != nullptr && lookAt->enabled) {
			guide.lookPoint = CameraLookAt::FindWantedPoint(*lookAt);
		}
	}
	return guide;
}
