#include "CameraFollow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Physics/MotionQuery.h"

namespace {
constexpr float kMinVerticalForward = 0.001f; // 前の向きの Y の大きさがこれより小さければ、カメラは水平（地面と交わらない）
constexpr float kMinFlatLength = 0.0001f;     // 地面に寝かせた向きの長さの2乗がこれより小さければ、向きが決まらない

/// <summary>
/// カメラを動かす面の、横と縦の向き（どちらも長さ1）
/// </summary>
struct FollowAxes {
	Vector3 horizontal; // 横（画面の右に当たる向き）
	Vector3 vertical;   // 縦（画面の上に当たる向き）

	// ワールドの点を、面の上の座標にする
	Vector2 ToPlane(const Vector3& point) const { return {Dot(point, horizontal), Dot(point, vertical)}; }
};

/// <summary>
/// 画面の四隅が、真ん中の点からどれだけ離れた所を写すか（ワールドの2つの軸で測る。ApplyBounds に渡す）
/// </summary>
struct ViewExtent {
	Vector2 min; // 真ん中より小さい方へのずれ（マイナス）
	Vector2 max; // 真ん中より大きい方へのずれ（プラス）
};

//=============================================================================
// 面の計算
//=============================================================================
// ===== 面の、横と縦の向き =====
FollowAxes MakeFollowAxes(CameraFollowPlane plane, const Quaternion& rotation) {
	const Vector3 right = RotateVector({1.0f, 0.0f, 0.0f}, rotation);
	switch (plane) {
	case CameraFollowPlane::XY:
		return {
		    {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}
        };
	case CameraFollowPlane::XZ: {
		// カメラの右を地面に寝かせる。右は真下を見ても水平のままなので、見下ろしのカメラでも向きが決まる
		const Vector3 flatRight = {right.x, 0.0f, right.z};
		const Vector3 horizontal = (LengthSq(flatRight) > kMinFlatLength) ? Normalize(flatRight) : Vector3{1.0f, 0.0f, 0.0f};
		// 右 × 上 ＝ 前（Quaternion.cpp の MakeLookRotation と同じ左手座標系の決まり）
		return {horizontal, Cross(horizontal, {0.0f, 1.0f, 0.0f})};
	}
	case CameraFollowPlane::View:
		return {right, RotateVector({0.0f, 1.0f, 0.0f}, rotation)};
	}
	MY_ASSERT_MSG(false, "知らない CameraFollowPlane です");
	return {
	    {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    };
}

// ===== 画面の真ん中が写している、高さ height の点 =====
// 傾けて見下ろすカメラでは、カメラの真下は画面の真ん中ではない。カメラの前の向きへ伸ばした線と、高さ height の面が交わる点が真ん中
Vector3 CalcGroundCenter(const Vector3& cameraPosition, const Vector3& forward, float height) {
	if (std::abs(forward.y) < kMinVerticalForward) {
		return cameraPosition; // 水平のカメラは地面と交わらないので、カメラの位置をそのまま使う
	}
	const float distance = (height - cameraPosition.y) / forward.y;
	return (distance > 0.0f) ? cameraPosition + forward * distance : cameraPosition;
}

// ===== 地面で画面の四隅が写す範囲（X・Z）=====
// 傾いたカメラの写す範囲は台形なので、四隅の線を1本ずつ地面と交わらせて測る。地平線より上を写す隅は地面と交わらないので数えない
std::optional<ViewExtent> CalcGroundExtent(const Vector3& cameraPosition, const Quaternion& rotation, const Vector3& center, float fovY) {
	const float halfHeight = std::tan(fovY * 0.5f);
	const float halfWidth = halfHeight * GameScreen::GetAspectRatio();
	const std::array<Vector3, 4> corners = {
	    {
         {-halfWidth, -halfHeight, 1.0f},
         {halfWidth, -halfHeight, 1.0f},
         {-halfWidth, halfHeight, 1.0f},
         {halfWidth, halfHeight, 1.0f},
	     }
    };
	std::optional<ViewExtent> extent;
	for (const Vector3& corner : corners) {
		const Vector3 direction = RotateVector(corner, rotation);
		if (direction.y >= -kMinVerticalForward && cameraPosition.y >= center.y) {
			continue; // 上から見ていて、この隅は地平線より上を向いている
		}
		if (direction.y <= kMinVerticalForward && cameraPosition.y < center.y) {
			continue; // 下から見ていて、この隅は地平線より下を向いている
		}
		const Vector3 hit = cameraPosition + direction * ((center.y - cameraPosition.y) / direction.y);
		const Vector2 offset = {hit.x - center.x, hit.z - center.z};
		if (!extent.has_value()) {
			extent = ViewExtent{offset, offset};
		}
		extent->min = {(std::min)(extent->min.x, offset.x), (std::min)(extent->min.y, offset.y)};
		extent->max = {(std::max)(extent->max.x, offset.x), (std::max)(extent->max.y, offset.y)};
	}
	return extent;
}

//=============================================================================
// 追いかけの計算
//=============================================================================
// ===== 追い方で決まる、focus のずれ（先読み）の目標 =====
Vector2 CalcWantedLookAhead(const CameraFollowComponent& follow, const Vector2& velocity) {
	switch (follow.mode) {
	case CameraFollowMode::Center:
		return {0.0f, 0.0f}; // ずらさない（LookAhead から切り替えたときは、0 へなめらかに戻る）
	case CameraFollowMode::LookAhead:
		// 速さ × 何秒先か（t = p + α・v の α・v）。速いほど先を見せ、止まると 0（真ん中に戻る）。上限で縛る
		return {
		    std::clamp(velocity.x * follow.lookAheadTime.x, -follow.lookAheadMax.x, follow.lookAheadMax.x),
		    std::clamp(velocity.y * follow.lookAheadTime.y, -follow.lookAheadMax.y, follow.lookAheadMax.y),
		};
	}
	MY_ASSERT_MSG(false, "知らない CameraFollowMode です");
	return {0.0f, 0.0f};
}

// ===== 1つの軸で、遊びの外へ出た分だけ真ん中を動かす（focus が「真ん中 ± 遊び」の中なら、真ん中のまま）=====
float ApplyDeadZone(float center, float focus, float deadZone) {
	if (focus > center + deadZone) {
		return focus - deadZone; // 右（上）へはみ出した：はみ出した分だけ追う
	}
	if (focus < center - deadZone) {
		return focus + deadZone; // 左（下）へはみ出した
	}
	return center; // 遊びの中：動かない
}

// ===== 1つの軸で、画面の端が min〜max の外を写さないように、真ん中を寄せる =====
// viewMin・viewMax は、真ん中から画面の端までのずれ。範囲が画面より狭いときは、どこに置いてもはみ出すので、画面と範囲の真ん中をそろえる
float ApplyBounds(float center, float viewMin, float viewMax, float min, float max) {
	const float low = min - viewMin;  // 真ん中がこれより小さいと、画面の左（下）の端が min の外を写す
	const float high = max - viewMax; // 真ん中がこれより大きいと、画面の右（上）の端が max の外を写す
	if (low > high) {
		return ((min + max) - (viewMin + viewMax)) * 0.5f;
	}
	return std::clamp(center, low, high);
}

// ===== カメラの視野角（ラジアン）=====
float GetFovY(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return std::clamp(camera ? camera->fovY : CameraComponent{}.fovY, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraFollow::Describe(ComponentUI& ui, CameraFollowComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと追わない（置いた位置のまま）"));
	ui.Field("Target###target", value.target, Tip("追う Entity（Hierarchy からドラッグ）。再生中だけ追う。動かすのは Plane の面の上だけ（カメラの向きは置いたまま）"));
	ui.Field(
	    "Plane###plane", value.plane,
	    Tip("カメラを動かす面。XY：ワールドの X・Y（横から見る）/ XZ：地面（見下ろし。カメラの向きが変わると横・縦も回る）/ View：カメラの右・上（どの向きのカメラでも。奥行きも保つ）"));
	ui.Field("Mode###mode", value.mode, Tip("追い方。Center：追う物を真ん中に / LookAhead：進む向きの先を見せる（速さ × Time。止まると真ん中に戻る）"));
	// --- 選んだ追い方の値だけを見せる（保存も、使っている方だけが書かれる。Collider の Shape と同じ）---
	switch (value.mode) {
	case CameraFollowMode::Center:
		break; // Center だけの値は無い
	case CameraFollowMode::LookAhead:
		ui.Field(
		    "Look Ahead Time###lookAheadTime", value.lookAheadTime,
		    AtLeast(0.0f).Tip("何秒先を見せるか（先読み ＝ 追う物の速さ × これ）。X 0.4 なら 7m/s で 2.8m 先。Y を 0 にすると、縦には先読みしない"));
		ui.Field("Look Ahead Max###lookAheadMax", value.lookAheadMax, AtLeast(0.0f).Tip("先読みの上限（m）。吹っ飛びのような速い動きで、先へ行きすぎないように"));
		ui.Field(
		    "Look Ahead Smooth Time###lookAheadSmoothTime", value.lookAheadSmoothTime,
		    AtLeast(0.0f).Tip("先読みが変わるまでの時間の目安（秒）。速さが急に変わっても（振り返る・止まる）、画面がガクッと動かない。0 ならすぐ変わる"));
		break;
	}
	ui.Separator("Follow");
	ui.Field("Offset###offset", value.offset, Tip("追う物から、画面の真ん中をどれだけずらすか（m）。X が横、Y が縦。XY で Y を上げると、足元が広く見える"));
	ui.Field("Dead Zone###deadZone", value.deadZone, AtLeast(0.0f).Tip("遊び（m）。画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない（跳ぶたびに画面が上下しないように）"));
	ui.BeginDisabled(value.plane != CameraFollowPlane::XY);
	ui.Field(
	    "Ground Snap Y###groundSnapY", value.groundSnapY,
	    Tip("追う物が地面にいる間は、縦の遊びを使わずに合わせる（跳んでも画面は揺れず、段に乗ると上がる・降りると下がる）。追う物に Rigidbody か Character Controller が要る。Plane が XY のときだけ"));
	ui.EndDisabled();
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる"));
	ui.Field("Max Lag###maxLag", value.maxLag, AtLeast(0.0f).Tip("遅れの上限（m）。速く動いても、追う物が画面の真ん中からこれより離れない"));
	ui.Separator("Bounds");
	ui.BeginDisabled(value.plane == CameraFollowPlane::View); // View の面はカメラと一緒に回るので、決まった範囲を持てない
	ui.Field("Use Bounds###useBounds", value.useBounds, Tip("画面の端が、Min〜Max の外を写さない（ステージの端でカメラが止まる）。Plane が XY・XZ のとき"));
	ui.Field("Min###boundsMin", value.boundsMin, Tip("写してよい範囲の小さい方の端。XY はワールドの X・Y（追う物の奥行きで測る）、XZ はワールドの X・Z（追う物の高さで測る）"));
	ui.Field("Max###boundsMax", value.boundsMax, Tip("写してよい範囲の大きい方の端（XY はワールドの X・Y、XZ はワールドの X・Z）"));
	ui.EndDisabled();
}

//=============================================================================
// 追いかけ
//=============================================================================
bool CameraFollow::Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime) {
	// --- 早期リターン ---
	const Handle<Entity> targetEntity = EntityManager::Find(follow.target);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(targetEntity);
	if (transform == nullptr || targetTransform == nullptr) {
		return false; // 追う物が入っていない・もう居ない
	}
	const Vector3 cameraPosition = GetWorldPosition(*transform);
	const Vector3 targetPosition = GetWorldPosition(*targetTransform);
	// 向きは毎フレーム Transform から読む（カメラを回しても、横・縦が画面の向きに付いてくる）
	const Quaternion rotation = GetWorldRotation(*transform);
	const FollowAxes axes = MakeFollowAxes(follow.plane, rotation);
	const Vector2 target = axes.ToPlane(targetPosition);

	// --- 1. 先読み（追い方で決まる focus のずれ）---
	// 速さは位置の変化から出す（Rigidbody が無い物・スクリプトで動かす物でも使える。壁に止められている間は 0）
	// 目標の先読みへ、指数でなめらかに近づける（速さはフレームごとにガタつくので、そのままだと画面が震える）
	if (follow.isFollowing) {
		const Vector2 velocity = (target - axes.ToPlane(follow.lastTargetPosition)) / deltaTime;
		const Vector2 wanted = CalcWantedLookAhead(follow, velocity);
		follow.currentLookAhead += (wanted - follow.currentLookAhead) * MathUtility::CalcSmoothRate(deltaTime, follow.lookAheadSmoothTime);
	}
	follow.lastTargetPosition = targetPosition;

	// --- 2. 画面の真ん中に置きたい点（focus）---
	const Vector2 focus = target + follow.offset + follow.currentLookAhead;

	// --- 3. 新しい真ん中 ---
	// XY・View はカメラの位置がそのまま真ん中。XZ は、カメラが写している地面の点を真ん中にする
	const Vector3 forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	const float depth = Dot(targetPosition - cameraPosition, forward); // 追う物の奥行き（カメラの前の向きに測った長さ）
	const bool isGround = (follow.plane == CameraFollowPlane::XZ);
	const Vector3 viewCenter = isGround ? CalcGroundCenter(cameraPosition, forward, targetPosition.y) : cameraPosition;
	const Vector2 oldCenter = axes.ToPlane(viewCenter);
	Vector2 center = oldCenter;
	if (!follow.isFollowing) {
		// 追いかけ始め（Play・シーンを読んだ直後）：置いた位置から流れてこないように、すぐ focus へ
		center = focus;
		follow.viewDepth = depth;
		follow.isFollowing = true;
	} else {
		// 遊びの外へ出た分だけ追う。Y は、追う物が地面にいる間は遊びを使わずに合わせる（Ground Snap Y）
		// 遊びだけだと、跳んで遊びの上へはみ出した分カメラが上がり、着地しても遊びの中なので戻らない（跳ぶたびに少しずつずれていく）
		// 地面にいるかは動き方の窓口に聞く（Rigidbody でも Character Controller でも同じ。どちらも無ければ地面にいない扱い）
		const bool isSnapY = (follow.plane == CameraFollowPlane::XY) && follow.groundSnapY && MotionQuery::IsGrounded(targetEntity);
		const Vector2 wanted = {ApplyDeadZone(center.x, focus.x, follow.deadZone.x), isSnapY ? focus.y : ApplyDeadZone(center.y, focus.y, follow.deadZone.y)};
		// なめらかに近づける（指数補間。1フレームに「残りの差 × t」だけ進む）
		center += (wanted - center) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime);
		// 遅れすぎないように縛る（吹っ飛びのような速い動きでも、追う物が画面から出ない）
		center.x = std::clamp(center.x, focus.x - follow.maxLag.x, focus.x + follow.maxLag.x);
		center.y = std::clamp(center.y, focus.y - follow.maxLag.y, focus.y + follow.maxLag.y);
	}

	// --- 4. ステージの端で止める（画面の端が、範囲の外を写さない）---
	if (follow.useBounds) {
		switch (follow.plane) {
		case CameraFollowPlane::XY: {
			// 追う物の奥行きでの、画面の半分の高さ・幅（カメラは +Z を向いている前提）。半分の高さ ＝ 距離 × tan(縦の視野角の半分)
			const float halfHeight = std::abs(targetPosition.z - cameraPosition.z) * std::tan(GetFovY(entity) * 0.5f);
			const float halfWidth = halfHeight * GameScreen::GetAspectRatio();
			center.x = ApplyBounds(center.x, -halfWidth, halfWidth, follow.boundsMin.x, follow.boundsMax.x);
			center.y = ApplyBounds(center.y, -halfHeight, halfHeight, follow.boundsMin.y, follow.boundsMax.y);
			break;
		}
		case CameraFollowPlane::XZ:
			// 範囲はワールドの X・Z で決まっているので、いったんワールドへ戻して縛り、面の座標へ戻す（カメラが回っていても、範囲は回らない）
			if (const std::optional<ViewExtent> extent = CalcGroundExtent(cameraPosition, rotation, viewCenter, GetFovY(entity))) {
				Vector3 moved = viewCenter + axes.horizontal * (center.x - oldCenter.x) + axes.vertical * (center.y - oldCenter.y);
				moved.x = ApplyBounds(moved.x, extent->min.x, extent->max.x, follow.boundsMin.x, follow.boundsMax.x);
				moved.z = ApplyBounds(moved.z, extent->min.y, extent->max.y, follow.boundsMin.y, follow.boundsMax.y);
				center = axes.ToPlane(moved);
			}
			break;
		case CameraFollowPlane::View:
			break; // 面がカメラと一緒に回るので、範囲を持たない
		}
	}

	// --- 5. 動いた量を Transform へ（親の下に置いたカメラでも、ワールドで動いた量だけ動く）---
	Vector3 move = axes.horizontal * (center.x - oldCenter.x) + axes.vertical * (center.y - oldCenter.y);
	// View の面はカメラと一緒に回るので、横・縦だけでは奥行きがずれていく（90 度回ると、奥にいた物が真横になる）。保つ奥行きへ戻す
	if (follow.plane == CameraFollowPlane::View) {
		move += forward * ((depth - follow.viewDepth) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime));
	}
	if (LengthSq(move) == 0.0f) {
		return false;
	}
	EntityManager::TranslateWorld(entity, move);
	return true;
}