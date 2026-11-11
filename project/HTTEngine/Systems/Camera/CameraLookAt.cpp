#include "CameraLookAt.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kMinLookDistanceSq = 1e-6f; // 見る点がカメラとほとんど同じ位置なら、向きが決まらないので Transform の向きのままにする
// 上下に向ける角の上限（ラジアン）。真上・真下まで向けると、左右の向きが決まらなくなる
constexpr float kMaxPitch = std::numbers::pi_v<float> * 0.5f - 0.01f;
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraLookAt::Describe(ComponentUI& ui, CameraLookAtComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと Transform の向きのまま"));
	ui.Field("Target###target", value.target, Tip("見る Entity（Hierarchy からドラッグ）。空なら Offset をワールドの座標として見る"));
	ui.Field("Offset###offset", value.offset, Tip("見る点のずれ（m。ワールドの向き）"));
	ui.Field(
	    "Screen Position###screenPosition", value.screenPosition,
	    Range(-1.0f, 1.0f).Tip("見る点を写す画面の位置。真ん中が 0、右と上が＋、端が ±1（スプライトの position と同じ）。横 1/3 の所なら X を -0.333（左から 1/3）か 0.333（右から 1/3）"));
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("見る点が追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる（再生中だけ効く）"));
}

//=============================================================================
// 見る点
//=============================================================================
// ===== 見たい点 =====
std::optional<Vector3> CameraLookAt::FindWantedPoint(const CameraLookAtComponent& lookAt) {
	if (!lookAt.target.IsSet()) {
		return lookAt.offset;
	}
	const TransformComponent* target = EntityManager::Get<TransformComponent>(lookAt.target);
	if (target == nullptr) {
		return std::nullopt;
	}
	return GetWorldPosition(*target) + lookAt.offset;
}

// ===== 今見ている点を、見たい点へ近づける =====
void CameraLookAt::Update(CameraLookAtComponent& lookAt, float deltaTime) {
	const std::optional<Vector3> point = FindWantedPoint(lookAt);
	if (!lookAt.enabled || !point.has_value()) {
		lookAt.isAiming = false;
		return;
	}
	if (!lookAt.isAiming) {
		lookAt.currentPoint = *point; // 見始め：カメラの向きが置いた向きから流れないように、すぐ合わせる
		lookAt.isAiming = true;
		return;
	}
	lookAt.currentPoint += (*point - lookAt.currentPoint) * MathUtility::CalcSmoothRate(deltaTime, lookAt.smoothTime);
}

// ===== 今見る点 =====
std::optional<Vector3> CameraLookAt::GetPoint(const CameraLookAtComponent& lookAt, bool isGameRunning) {
	if (!lookAt.enabled) {
		return std::nullopt;
	}
	if (isGameRunning && lookAt.isAiming) {
		return lookAt.currentPoint;
	}
	return FindWantedPoint(lookAt);
}

//=============================================================================
// 向き
//=============================================================================
// 考え方：カメラの向きを「左右（ヨー）」と「上下（ピッチ）」の2つの角に分けて、上下 → 左右の順に1つずつ合わせる（横の傾き＝ロールは 0）
// 1. カメラから見て、見る点がどの向きにあれば画面の screenPosition に写るか（カメラの右・上・前で表した向き u）を、視野角から出す
// 2. 上下：カメラを上下に向けても、u の左右の成分は変わらない。上下だけ回して、u のワールドの高さを、見る点の向き w の高さにそろえる
// 3. 左右：ワールドの上の軸のまわりに回しても、高さは変わらない。真上から見た角を、w の角にそろえる
// どちらの回転も、もう一方で合わせた物をくずさないので、1回ずつで合う
std::optional<Quaternion> CameraLookAt::CalcRotation(const Vector3& position, const Vector3& point, const Vector2& screenPosition, float fovYDegrees, float aspect) {
	const Vector3 toPoint = point - position;
	const float distanceSq = LengthSq(toPoint);
	if (distanceSq <= kMinLookDistanceSq) {
		return std::nullopt;
	}
	// 真ん中なら、見る向きをそのまま前にする（今までと同じ計算。誤差も出さない）
	if (screenPosition.x == 0.0f && screenPosition.y == 0.0f) {
		return MakeLookRotation(toPoint);
	}
	const Vector3 w = toPoint / std::sqrt(distanceSq); // 見る点の向き（ワールド。長さ1）

	// --- 1. 画面の位置 → カメラから見た向き u（右・上・前。前が 1 の所の、画面の端までの長さが tan(視野角の半分)）---
	// 写すときの視野角と同じ範囲に収める（CameraSystem の ApplyProjection と同じ）
	const float fovY = std::clamp(fovYDegrees, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
	const float tanHalfY = std::tan(fovY * 0.5f);
	const float tanHalfX = tanHalfY * aspect;
	const Vector3 u = Normalize(Vector3{screenPosition.x * tanHalfX, screenPosition.y * tanHalfY, 1.0f});

	// --- 2. 上下：u を上・前の面で見ると、長さ r・カメラの前から上へ alpha の角。ピッチ pitch だけ上へ向けると、ワールドの高さは r × sin(alpha + pitch) ---
	// これが w の高さ w.y になる pitch を求める（左右へ大きくずらした位置に、真上近くの点は写せないので、届かない分は止める）
	const float r = std::sqrt(u.y * u.y + u.z * u.z);
	const float alpha = std::atan2(u.y, u.z);
	const float elevation = std::asin(std::clamp(w.y / r, -1.0f, 1.0f)); // alpha + pitch（u のワールドでの仰角）
	const float pitch = std::clamp(elevation - alpha, -kMaxPitch, kMaxPitch);

	// --- 3. 左右：上下を合わせた u を真上から見た角（前から右へ）を、w を真上から見た角にそろえる ---
	// 真上から見た u は（右 u.x、前 r × cos(alpha + pitch)）。角は atan2(右, 前)
	const float pointYaw = std::atan2(w.x, w.z);
	const float offsetYaw = std::atan2(u.x, r * std::cos(alpha + pitch));
	const float yaw = pointYaw - offsetYaw;

	// --- カメラの前の向きにして、向きを作る（MakeLookRotation はワールドの上を上にする＝横に傾けない）---
	const Vector3 forward = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
	return MakeLookRotation(forward);
}
