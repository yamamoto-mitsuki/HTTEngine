#include "CameraFollow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Physics/MotionQuery.h"

namespace {
constexpr float kMinFlatLength = 0.0001f;     // 地面に寝かせた向きの長さの2乗がこれより小さければ、向きが決まらない
constexpr float kMinDepth = 0.001f;           // カメラの前の向きに測った長さがこれより短い点は、画面に写らない（カメラの後ろ・真横）
constexpr float kMinCastAlignment = 0.0001f;  // 線の向きと面に垂直な向きの内積の大きさがこれより小さければ、線は面に沿っていて交わらない
constexpr float kGuideMarkScale = 0.03f;      // focus の印の大きさ（カメラから focus までの長さに掛ける。画面の上でいつも同じくらいに見える）

/// <summary>
/// カメラを動かす面の、横と縦の向き（どちらも長さ1）
/// </summary>
struct FollowAxes {
	Vector3 horizontal; // 横（画面の右に当たる向き）
	Vector3 vertical;   // 縦（画面の上に当たる向き）

	// ワールドの点（向き）を、面の上の座標にする
	Vector2 ToPlane(const Vector3& point) const { return {Dot(point, horizontal), Dot(point, vertical)}; }
	// 面の上の動き → ワールドの動き
	Vector3 ToWorld(const Vector2& move) const { return horizontal * move.x + vertical * move.y; }
};

/// <summary>
/// 画面の四隅が、真ん中の点からどれだけ離れた所を写すか（ワールドの2つの軸で測る。ApplyBounds に渡す）
/// </summary>
struct ViewExtent {
	Vector2 min; // 真ん中より小さい方へのずれ（マイナス）
	Vector2 max; // 真ん中より大きい方へのずれ（プラス）
};

/// <summary>
/// Bounds の Min・Max の X・Y が、ワールドのどの向きか（長さ1）
/// </summary>
struct BoundsAxes {
	Vector3 a; // Min・Max の X
	Vector3 b; // Min・Max の Y
};

/// <summary>
/// カメラから見た画面の測り方（Transform の向きと Camera Component の視野角。Camera Look At・揺れを入れる前の向き）
/// <para>画面の位置は、真ん中が 0・右と上が＋・端が ±1（Camera Look At の Screen Position と同じ）</para>
/// </summary>
struct ViewFrustum {
	Vector3 position;      // カメラの位置
	Vector3 right;         // カメラの右（長さ1）
	Vector3 up;            // カメラの上（長さ1）
	Vector3 forward;       // カメラの前（長さ1）
	float tanHalfX = 1.0f; // 画面の右の端へ向かう線の、前に対する傾き（tan(横の視野角の半分)）
	float tanHalfY = 1.0f; // 画面の上の端へ向かう線の、前に対する傾き（tan(縦の視野角の半分)）

	// 点が写る画面の位置（カメラの後ろ・真横なら無し）
	// カメラから見た点を「前に1進んだ所での横・縦のずれ」にして、端までのずれ（tanHalf）で割る
	std::optional<Vector2> ToScreen(const Vector3& point) const {
		const Vector3 local = point - position;
		const float depth = Dot(local, forward);
		if (depth < kMinDepth) {
			return std::nullopt;
		}
		return Vector2{Dot(local, right) / (depth * tanHalfX), Dot(local, up) / (depth * tanHalfY)};
	}

	// 画面の位置 screen に写る点を、planePoint を通り normal に垂直な面の上で探す（線が面に沿っている・面がカメラの後ろなら無し）
	std::optional<Vector3> CastToPlane(const Vector2& screen, const Vector3& planePoint, const Vector3& normal) const {
		const Vector3 direction = forward + right * (screen.x * tanHalfX) + up * (screen.y * tanHalfY); // ToScreen の逆（前に1進んだ所）
		const float alignment = Dot(direction, normal);
		if (std::abs(alignment) < kMinCastAlignment) {
			return std::nullopt;
		}
		const float distance = Dot(planePoint - position, normal) / alignment; // 線の上の点 position + direction × distance が面に乗る distance
		if (distance <= 0.0f) {
			return std::nullopt;
		}
		return position + direction * distance;
	}

	// 向きはそのまま、位置だけ move 動かしたカメラ
	ViewFrustum Moved(const Vector3& move) const {
		ViewFrustum moved = *this;
		moved.position = position + move;
		return moved;
	}
};

/// <summary>
/// 1フレーム分の、追いかけの計算に使う値（Update と CalcGuide が同じ物を使う）
/// </summary>
struct FollowFrame {
	FollowAxes axes;        // 面の横・縦
	Vector3 normal;         // 面に垂直な向き（Screen で、画面の位置を面の上の点に直すのに使う）
	ViewFrustum frustum;    // 今のカメラ
	Vector3 targetPosition; // 追う物の位置
	Vector3 viewCenter;     // 画面の真ん中が写している点（カメラの前の向きが、追う物を通る面と交わる点）
	Vector2 center;         // viewCenter の面の上の座標（このフレームで追う前の真ん中）
	float depth = 0.0f;     // 追う物の奥行き（カメラの前の向きに測った長さ）
};

/// <summary>
/// Dead Zone と Soft Zone の大きさ（Zone Space で選んだ方。Soft Zone は Dead Zone より小さくしない）
/// </summary>
struct ZoneSizes {
	Vector2 dead; // focus がこの中なら、カメラは動かない
	Vector2 soft; // focus はこの外へ出ない
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
	HTT_ASSERT_MSG(false, "知らない CameraFollowPlane です");
	return {
	    {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    };
}

// ===== 面の上で、画面の四隅が写す範囲 =====
// 画面の四隅から線を伸ばして、planePoint を通り normal に垂直な面と交わる点を、真ん中 center からのずれ（axisA・axisB の向きに測る）にする
// 傾いたカメラの写す範囲は、真ん中から上下（左右）に同じ長さではない（XY を下へ傾けると、下の方が遠くまで写る。XZ は台形）ので、四隅を1本ずつ測る
// 面と交わらない隅（地平線より上を向いている）は数えない。1つも交わらなければ無し
std::optional<ViewExtent> CalcViewExtent(const ViewFrustum& frustum, const Vector3& planePoint, const Vector3& normal, const Vector3& center, const Vector3& axisA, const Vector3& axisB) {
	const std::array<Vector2, 4> corners = {
	    {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}}
    };
	std::optional<ViewExtent> extent;
	for (const Vector2& corner : corners) {
		const std::optional<Vector3> hit = frustum.CastToPlane(corner, planePoint, normal);
		if (!hit.has_value()) {
			continue;
		}
		const Vector2 offset = {Dot(*hit - center, axisA), Dot(*hit - center, axisB)};
		if (!extent.has_value()) {
			extent = ViewExtent{offset, offset};
		}
		extent->min = {(std::min)(extent->min.x, offset.x), (std::min)(extent->min.y, offset.y)};
		extent->max = {(std::max)(extent->max.x, offset.x), (std::max)(extent->max.y, offset.y)};
	}
	return extent;
}

// ===== Bounds の Min・Max の向き（View は面がカメラと一緒に回るので、決まった範囲を持てない＝無し）=====
std::optional<BoundsAxes> GetBoundsAxes(CameraFollowPlane plane) {
	switch (plane) {
	case CameraFollowPlane::XY:
		return BoundsAxes{
		    {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}
        };
	case CameraFollowPlane::XZ:
		return BoundsAxes{
		    {1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}
        };
	case CameraFollowPlane::View:
		return std::nullopt;
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowPlane です");
	return std::nullopt;
}

// ===== カメラの視野角（ラジアン）=====
float GetFovY(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return std::clamp(camera ? camera->fovY : CameraComponent{}.fovY, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
}

// ===== 1フレーム分の値を集める（カメラか追う物の Transform が無ければ無し）=====
std::optional<FollowFrame> MakeFollowFrame(Handle<Entity> entity, Handle<Entity> targetEntity, CameraFollowPlane plane) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(targetEntity);
	if (transform == nullptr || targetTransform == nullptr) {
		return std::nullopt; // 追う物が入っていない・もう居ない
	}
	FollowFrame frame;
	// 向きは毎フレーム Transform から読む（カメラを回しても、横・縦が画面の向きに付いてくる）
	const Quaternion rotation = GetWorldRotation(*transform);
	frame.axes = MakeFollowAxes(plane, rotation);
	frame.normal = Cross(frame.axes.horizontal, frame.axes.vertical); // 向き（表か裏か）は使わない。面と交わる所を探すだけ
	frame.frustum.position = GetWorldPosition(*transform);
	frame.frustum.right = RotateVector({1.0f, 0.0f, 0.0f}, rotation);
	frame.frustum.up = RotateVector({0.0f, 1.0f, 0.0f}, rotation);
	frame.frustum.forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	frame.frustum.tanHalfY = std::tan(GetFovY(entity) * 0.5f);
	frame.frustum.tanHalfX = frame.frustum.tanHalfY * GameScreen::GetAspectRatio();
	frame.targetPosition = GetWorldPosition(*targetTransform);
	frame.depth = Dot(frame.targetPosition - frame.frustum.position, frame.frustum.forward);
	// 画面の真ん中は、カメラの前の向きへ伸ばした線が、追う物を通る面（XY：追う物の奥行き、XZ：追う物の高さ、View：カメラの正面）と交わる点
	// 傾けたカメラでは、カメラの真横・真下は画面の真ん中ではない（XY を下へ傾けると、真ん中に写るのはカメラより低い所）
	// 面と交わらない（面に沿って見ている・面が後ろにある）ときは、カメラの位置を使う
	frame.viewCenter = frame.frustum.CastToPlane({0.0f, 0.0f}, frame.targetPosition, frame.normal).value_or(frame.frustum.position);
	frame.center = frame.axes.ToPlane(frame.viewCenter);
	return frame;
}

// ===== Zone Space で選んだ方の大きさ =====
ZoneSizes GetZoneSizes(const CameraFollowComponent& follow) {
	const auto make = [](const Vector2& dead, const Vector2& soft) {
		const Vector2 safeDead = {(std::max)(dead.x, 0.0f), (std::max)(dead.y, 0.0f)};
		return ZoneSizes{safeDead, {(std::max)(soft.x, safeDead.x), (std::max)(soft.y, safeDead.y)}};
	};
	switch (follow.zoneSpace) {
	case CameraFollowZoneSpace::World:
		return make(follow.deadZone, follow.softZone);
	case CameraFollowZoneSpace::Screen:
		return make(follow.screenDeadZone, follow.screenSoftZone);
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowZoneSpace です");
	return make(follow.deadZone, follow.softZone);
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
	HTT_ASSERT_MSG(false, "知らない CameraFollowMode です");
	return {0.0f, 0.0f};
}

// ===== 1つの軸で、focus が「真ん中 ± zone」の外へ出た分だけ真ん中を動かす（中なら、真ん中のまま）=====
float ApplyZone(float center, float focus, float zone) {
	if (focus > center + zone) {
		return focus - zone; // 右（上）へはみ出した：はみ出した分だけ追う
	}
	if (focus < center - zone) {
		return focus + zone; // 左（下）へはみ出した
	}
	return center; // 範囲の中：動かない
}

// ===== focus が「真ん中 ± zone」の中に来るように、真ん中を一番少なく動かす =====
// center・focus は面の上の座標、focusPoint は focus のワールドの点。Dead Zone（なめらかに追う目標）と Soft Zone（すぐ縛る）の両方に使う
// World：面の上の長さで比べる
// Screen：真ん中が center のときのカメラで focus を画面に写して比べ、はみ出した分を面の上の長さに直す
//   カメラを面に沿って m 動かすと、面の上のどの点も、画面の上では「m 動いた点」の写っていた所へ移る（面とカメラの動く向きが平行なので）
//   → 「focus を写したい画面の位置（範囲の端）に、今写っている面の点」から focus までが、動かす量（面が傾いた XZ の台形でもそのまま正しい）
Vector2 KeepInZone(const FollowFrame& frame, CameraFollowZoneSpace space, const Vector2& center, const Vector2& focus, const Vector3& focusPoint, const Vector2& zone) {
	switch (space) {
	case CameraFollowZoneSpace::World:
		return {ApplyZone(center.x, focus.x, zone.x), ApplyZone(center.y, focus.y, zone.y)};
	case CameraFollowZoneSpace::Screen: {
		const ViewFrustum camera = frame.frustum.Moved(frame.axes.ToWorld(center - frame.center));
		const std::optional<Vector2> screen = camera.ToScreen(focusPoint);
		if (!screen.has_value()) {
			return focus; // focus がカメラの後ろ：真ん中へ合わせる
		}
		const Vector2 kept = {std::clamp(screen->x, -zone.x, zone.x), std::clamp(screen->y, -zone.y, zone.y)};
		if (kept == *screen) {
			return center; // 範囲の中：動かない
		}
		const std::optional<Vector3> hit = camera.CastToPlane(kept, focusPoint, frame.normal);
		if (!hit.has_value()) {
			return focus; // 範囲の端が面に写らない（地平線より上など）：真ん中へ合わせる
		}
		return center + frame.axes.ToPlane(focusPoint - *hit);
	}
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowZoneSpace です");
	return center;
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

//=============================================================================
// エディタの線の形
//=============================================================================
// ===== World の範囲の四隅（focus を通る面の上で、真ん中 ± zone）=====
std::array<Vector3, 4> MakeWorldRect(const FollowFrame& frame, const Vector2& focus, const Vector3& focusPoint, const Vector2& zone) {
	const auto corner = [&](float signX, float signY) {
		const Vector2 point = {frame.center.x + zone.x * signX, frame.center.y + zone.y * signY};
		return focusPoint + frame.axes.ToWorld(point - focus);
	};
	return {corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(1.0f, 1.0f), corner(-1.0f, 1.0f)};
}

// ===== Screen の範囲の四隅（画面の ± zone に写る、focus を通る面の上の点。面に写らない隅があれば無し）=====
std::optional<std::array<Vector3, 4>> MakeScreenRect(const ViewFrustum& frustum, const Vector3& focusPoint, const Vector3& normal, const Vector2& zone) {
	const std::array<Vector2, 4> screens = {
	    {{-zone.x, -zone.y}, {zone.x, -zone.y}, {zone.x, zone.y}, {-zone.x, zone.y}}
    };
	std::array<Vector3, 4> corners;
	for (size_t i = 0; i < screens.size(); ++i) {
		const std::optional<Vector3> hit = frustum.CastToPlane(screens[i], focusPoint, normal);
		if (!hit.has_value()) {
			return std::nullopt;
		}
		corners[i] = *hit;
	}
	return corners;
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
	    Tip("カメラを動かす面"));
	ui.Field("Mode###mode", value.mode, Tip("追い方"));
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
	ui.Field(
	    "Zone Space###zoneSpace", value.zoneSpace,
	    Tip("Dead Zone・Soft Zone を何で測るか"));
	// --- 選んだ測り方の値だけを見せる（保存も、使っている方だけ）---
	switch (value.zoneSpace) {
	case CameraFollowZoneSpace::World:
		ui.Field("Dead Zone###deadZone", value.deadZone, AtLeast(0.0f).Tip("遊び（m）。focus が画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない（跳ぶたびに画面が上下しないように）"));
		ui.Field(
		    "Soft Zone###softZone", value.softZone,
		    AtLeast(0.0f).Tip("focus を出さない範囲（m。画面の真ん中から横・縦）。Dead Zone の外ではなめらかに追い、ここに着いたらすぐ付いていく（速く動いても画面から出ない）。Dead Zone より小さい軸は Dead Zone と同じ"));
		break;
	case CameraFollowZoneSpace::Screen:
		ui.Field("Dead Zone###screenDeadZone", value.screenDeadZone, AtLeast(0.0f).Tip("遊び（画面の割合。真ん中から端までが 1）。focus がこの中で動いても、カメラは動かない。0.1 なら、真ん中から左右へ画面の幅の 5% ずつ"));
		ui.Field(
		    "Soft Zone###screenSoftZone", value.screenSoftZone,
		    AtLeast(0.0f).Tip("focus を出さない範囲（画面の割合。真ん中から端までが 1）。Dead Zone の外ではなめらかに追い、ここに着いたらすぐ付いていく。1 より小さいと、focus はいつも画面の中。Dead Zone より小さい軸は Dead Zone と同じ"));
		break;
	}
	ui.BeginDisabled(value.plane != CameraFollowPlane::XY);
	ui.Field(
	    "Ground Snap Y###groundSnapY", value.groundSnapY,
	    Tip("追う物が地面にいる間は、縦の遊びを使わずに合わせる（跳んでも画面は揺れず、段に乗ると上がる・降りると下がる）。追う物に Rigidbody か Character Controller が要る。Plane が XY のときだけ"));
	ui.EndDisabled();
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("focus が Dead Zone の外へ出たとき、追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる"));
	ui.Field(
	    "Show Gizmo###showGizmo", value.showGizmo,
	    Tip("線を出す（エディタだけ）。緑：Dead Zone / 青：Soft Zone / 黄：focus（追う物から引いた線の先）/ 赤：Bounds。Screen の範囲は Game ビューだけ（今写しているカメラの物だけ）、ほかは Scene・Game ビュー。View メニューの Camera Follow Zones で全部まとめて消せる"));
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
	const std::optional<FollowFrame> frameValue = MakeFollowFrame(entity, targetEntity, follow.plane);
	if (!frameValue.has_value()) {
		return false; // 追う物が入っていない・もう居ない
	}
	FollowFrame frame = *frameValue;
	const FollowAxes& axes = frame.axes;
	const Vector2 target = axes.ToPlane(frame.targetPosition);

	// --- 0. View の奥行きを保つ前後の動き ---
	// View の面はカメラと一緒に回るので、横・縦だけでは奥行きがずれていく（90 度回ると、奥にいた物が真横になる）。保つ奥行きへ戻す
	// 範囲を測る前にカメラを前後へ動かしておく（後で足すと、Screen の範囲で縛った後に画面の位置が伸び縮みして、Soft Zone から少し出る）
	// 前の向きは面の横・縦と直角なので、面の上の座標（frame.center）は変わらない
	Vector3 depthMove = {0.0f, 0.0f, 0.0f};
	if (follow.plane == CameraFollowPlane::View && follow.isFollowing) {
		depthMove = frame.frustum.forward * ((frame.depth - follow.viewDepth) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime));
		frame.frustum.position += depthMove;
		frame.viewCenter += depthMove;
	}

	// --- 1. 先読み（追い方で決まる focus のずれ）---
	// 速さは位置の変化から出す（Rigidbody が無い物・スクリプトで動かす物でも使える。壁に止められている間は 0）
	// 目標の先読みへ、指数でなめらかに近づける（速さはフレームごとにガタつくので、そのままだと画面が震える）
	if (follow.isFollowing) {
		const Vector2 velocity = (target - axes.ToPlane(follow.lastTargetPosition)) / deltaTime;
		const Vector2 wanted = CalcWantedLookAhead(follow, velocity);
		follow.currentLookAhead += (wanted - follow.currentLookAhead) * MathUtility::CalcSmoothRate(deltaTime, follow.lookAheadSmoothTime);
	}
	follow.lastTargetPosition = frame.targetPosition;

	// --- 2. 画面の真ん中に置きたい点（focus）---
	const Vector2 shift = follow.offset + follow.currentLookAhead;
	const Vector2 focus = target + shift;
	const Vector3 focusPoint = frame.targetPosition + axes.ToWorld(shift); // ワールドの点（Screen で画面に写すのに使う）

	// --- 3. 新しい真ん中 ---
	Vector2 center = frame.center;
	if (!follow.isFollowing) {
		// 追いかけ始め（Play・シーンを読んだ直後）：置いた位置から流れてこないように、すぐ focus へ
		center = focus;
		follow.viewDepth = frame.depth;
		follow.isFollowing = true;
	} else {
		const ZoneSizes zones = GetZoneSizes(follow);
		// 3-1. Dead Zone の外へ出た分だけ追う。Y は、追う物が地面にいる間は遊びを使わずに合わせる（Ground Snap Y）
		// 遊びだけだと、跳んで遊びの上へはみ出した分カメラが上がり、着地しても遊びの中なので戻らない（跳ぶたびに少しずつずれていく）
		// 地面にいるかは動き方の窓口に聞く（Rigidbody でも Character Controller でも同じ。どちらも無ければ地面にいない扱い）
		const bool isSnapY = (follow.plane == CameraFollowPlane::XY) && follow.groundSnapY && MotionQuery::IsGrounded(targetEntity);
		Vector2 wanted = KeepInZone(frame, follow.zoneSpace, center, focus, focusPoint, zones.dead);
		if (isSnapY) {
			wanted.y = focus.y;
		}
		// なめらかに近づける（指数補間。1フレームに「残りの差 × t」だけ進む）
		center += (wanted - center) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime);
		// 3-2. Soft Zone の外へは出さない（吹っ飛びのような速い動きでも、追う物が画面から出ない）
		center = KeepInZone(frame, follow.zoneSpace, center, focus, focusPoint, zones.soft);
	}

	// --- 4. ステージの端で止める（画面の端が、範囲の外を写さない）---
	// 範囲はワールドの軸（XY：X・Y、XZ：X・Z）で決まっているので、いったんワールドへ戻して縛り、面の座標へ戻す（カメラが回っていても、範囲は回らない）
	// 真ん中から画面の端までのずれは、四隅を面に当てて測る。カメラは面と平行に動き、向きは変えないので、真ん中を動かしてもこのずれは変わらない
	if (follow.useBounds) {
		const std::optional<BoundsAxes> boundsAxes = GetBoundsAxes(follow.plane);
		const std::optional<ViewExtent> extent =
		    boundsAxes.has_value() ? CalcViewExtent(frame.frustum, frame.targetPosition, frame.normal, frame.viewCenter, boundsAxes->a, boundsAxes->b) : std::nullopt;
		if (extent.has_value()) {
			const Vector3 moved = frame.viewCenter + axes.ToWorld(center - frame.center);
			const float a = Dot(moved, boundsAxes->a);
			const float b = Dot(moved, boundsAxes->b);
			const float boundedA = ApplyBounds(a, extent->min.x, extent->max.x, follow.boundsMin.x, follow.boundsMax.x);
			const float boundedB = ApplyBounds(b, extent->min.y, extent->max.y, follow.boundsMin.y, follow.boundsMax.y);
			center = axes.ToPlane(moved + boundsAxes->a * (boundedA - a) + boundsAxes->b * (boundedB - b));
		}
	}

	// --- 5. 動いた量を Transform へ（親の下に置いたカメラでも、ワールドで動いた量だけ動く）---
	const Vector3 move = axes.ToWorld(center - frame.center) + depthMove; // 0 の前後の動きも足す
	if (LengthSq(move) == 0.0f) {
		return false;
	}
	EntityManager::TranslateWorld(entity, move);
	return true;
}

//=============================================================================
// エディタの線
//=============================================================================
std::optional<CameraFollow::Guide> CameraFollow::CalcGuide(Handle<Entity> entity, const CameraFollowComponent& follow) {
	const std::optional<FollowFrame> frameValue = MakeFollowFrame(entity, EntityManager::Find(follow.target), follow.plane);
	if (!frameValue.has_value()) {
		return std::nullopt;
	}
	const FollowFrame& frame = *frameValue;
	// --- focus（Update と同じ。停止中は先読みが 0 なので、追う物 ＋ Offset）---
	const Vector2 shift = follow.offset + follow.currentLookAhead;
	const Vector2 focus = frame.axes.ToPlane(frame.targetPosition) + shift;
	const Vector3 focusPoint = frame.targetPosition + frame.axes.ToWorld(shift);
	const Vector3 toFocus = focusPoint - frame.frustum.position;
	if (Dot(toFocus, frame.frustum.forward) < kMinDepth) {
		return std::nullopt; // focus がカメラの後ろ（範囲を画面に重ねられない）
	}

	Guide guide;
	guide.target = frame.targetPosition;
	guide.focus = focusPoint;
	guide.horizontal = frame.axes.horizontal;
	guide.vertical = frame.axes.vertical;
	guide.markSize = Length(toFocus) * kGuideMarkScale;

	// --- Dead Zone・Soft Zone ---
	const ZoneSizes zones = GetZoneSizes(follow);
	switch (follow.zoneSpace) {
	case CameraFollowZoneSpace::World:
		guide.deadZone = MakeWorldRect(frame, focus, focusPoint, zones.dead);
		guide.softZone = MakeWorldRect(frame, focus, focusPoint, zones.soft);
		break;
	case CameraFollowZoneSpace::Screen: {
		// 追いかけと同じ面に置く（XZ なら地面の台形）。隅が地平線より上で面に写らなければ、2つともカメラの正面の面に置く
		std::optional<std::array<Vector3, 4>> dead = MakeScreenRect(frame.frustum, focusPoint, frame.normal, zones.dead);
		std::optional<std::array<Vector3, 4>> soft = MakeScreenRect(frame.frustum, focusPoint, frame.normal, zones.soft);
		if (!dead.has_value() || !soft.has_value()) {
			dead = MakeScreenRect(frame.frustum, focusPoint, frame.frustum.forward, zones.dead);
			soft = MakeScreenRect(frame.frustum, focusPoint, frame.frustum.forward, zones.soft);
		}
		if (!dead.has_value() || !soft.has_value()) {
			return std::nullopt; // focus がカメラの前にあれば、正面の面にはいつも写る（ここへは来ない）
		}
		guide.deadZone = *dead;
		guide.softZone = *soft;
		break;
	}
	}

	// --- Bounds（追いかけと同じく、XY は追う物の奥行き、XZ は追う物の高さの面に置く）---
	// Min・Max の向き（a・b）以外の向きは、追う物の値のまま（XY なら Z、XZ なら Y）
	const std::optional<BoundsAxes> boundsAxes = GetBoundsAxes(follow.plane);
	if (follow.useBounds && boundsAxes.has_value()) {
		const Vector3& position = frame.targetPosition;
		const auto corner = [&](float a, float b) { return position + boundsAxes->a * (a - Dot(position, boundsAxes->a)) + boundsAxes->b * (b - Dot(position, boundsAxes->b)); };
		const Vector2& min = follow.boundsMin;
		const Vector2& max = follow.boundsMax;
		guide.bounds = std::array<Vector3, 4>{corner(min.x, min.y), corner(max.x, min.y), corner(max.x, max.y), corner(min.x, max.y)};
	}
	return guide;
}
