#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// カメラが Entity を追いかける。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// どの面の上で動かすかは Plane で決める。カメラの向きは変えない
// 動かすのは CameraSystem::UpdateFollow（再生中だけ。物理で動いた後・カメラの絵を決める前）。停止中は置いた位置のまま
// 画面の真ん中に置きたい点（focus）＝ 追う物の位置 ＋ Offset ＋ 追い方（Mode）で決まるずれ
// カメラは、focus が Dead Zone（遊び）の外へ出た分だけなめらかに追い、Soft Zone の外へは出さない（Cinemachine の Dead Zone・Soft Zone と同じ）
// 2つの範囲は、ワールドの長さ（m）か画面の割合で測る（Zone Space）

/// <summary>
/// 追い方（focus をどこに置くか）。Inspector では、選んだ追い方の値だけが出る
/// </summary>
enum class CameraFollowMode {
	Center,    // 追う物を真ん中に（Offset の分だけずらす）
	LookAhead, // 進む向きの先を見せる（追う物の速さ × Look Ahead Time だけ先を真ん中に。止まると真ん中に戻る）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(CameraFollowMode) {
	using enum CameraFollowMode;
	return MakeEnumTips<CameraFollowMode>({
	    {Center, "追う物を画面の真ん中に置く（Offset の分だけずらせる）"},
	    {LookAhead, "進む向きの先を見せる（速さ × Look Ahead Time だけ先を真ん中に。止まると真ん中に戻る）"},
	});
}

/// <summary>
/// カメラを動かす面。Offset・Look Ahead と、World の Dead Zone・Soft Zone の X・Y は、下の「横」と「縦」の向きになる
/// </summary>
enum class CameraFollowPlane {
	XY,   // ワールドの X・Y（横から見るゲーム）。横 = X・縦 = Y
	XZ,   // 地面の X・Z（見下ろしのゲーム）。横 = カメラの右、縦 = カメラの前を地面に寝かせた向き（カメラが回ると一緒に回る）
	View, // カメラから見た位置を保つ（どの向きのカメラでも）。横 = カメラの右、縦 = カメラの上。奥行きも、追いかけ始めの長さに保つ
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(CameraFollowPlane) {
	using enum CameraFollowPlane;
	return MakeEnumTips<CameraFollowPlane>({
	    {XY, "ワールドの X・Y の面で動かす（横から見るゲーム）。横 = X、縦 = Y"},
	    {XZ, "地面の X・Z の面で動かす（見下ろしのゲーム）。横 = カメラの右、縦 = カメラの前を地面に寝かせた向き（カメラが回ると一緒に回る）"},
	    {View, "カメラから見た位置を保つ（どの向きのカメラでも）。横 = カメラの右、縦 = カメラの上。奥行きも、追い始めの長さのまま"},
	});
}

/// <summary>
/// Dead Zone・Soft Zone を何で測るか。Inspector では、選んだ方の値だけが出る
/// </summary>
enum class CameraFollowZoneSpace {
	World,  // ワールドの長さ（m）。Plane の面の上で、画面の真ん中から横・縦に何 m か（カメラが寄っても引いても、追い始める距離は同じ）
	Screen, // 画面の割合。真ん中が 0、端が ±1（Camera Look At の Screen Position と同じ）。カメラが引くと、ワールドでは広がる
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(CameraFollowZoneSpace) {
	using enum CameraFollowZoneSpace;
	return MakeEnumTips<CameraFollowZoneSpace>({
	    {World, "ワールドの長さ（m）で測る。Plane の面の上で、画面の真ん中から横・縦に何 m か（カメラが寄っても引いても、追い始める距離は同じ）"},
	    {Screen, "画面の割合で測る。真ん中が 0、端が 1（カメラが引くと、ワールドでは広がる）"},
	});
}

/// <summary>
/// カメラの追いかけ（Unity の Cinemachine の Position Composer の小さい版）
/// </summary>
struct CameraFollowComponent {
	// --- どの追い方でも使う ---
	bool enabled = true;                                 // false の間は追わない（置いた位置のまま）
	EntityRef target;                                    // 追う Entity（Hierarchy からドラッグ。保存は EntityId で）
	CameraFollowPlane plane = CameraFollowPlane::XY;     // カメラを動かす面
	CameraFollowMode mode = CameraFollowMode::LookAhead; // 追い方
	Vector2 offset = {0.0f, 1.5f};                       // 追う物から、画面の真ん中をどれだけずらすか（m。XY で Y を上げると、足元が広く見える）
	// --- 遊び（Dead Zone）と、focus を出さない範囲（Soft Zone）。どちらも画面の真ん中から横・縦への半分の幅 ---
	CameraFollowZoneSpace zoneSpace = CameraFollowZoneSpace::World; // 2つの範囲を何で測るか
	Vector2 deadZone = {0.5f, 1.5f};                                // World の遊び（m）。focus がこの中で動いても、カメラは動かない
	Vector2 softZone = {6.0f, 4.0f};                                // World の Soft Zone（m）。focus はこの外へ出ない（速く動いても画面から出ないように）。Dead Zone より小さい軸は Dead Zone と同じ
	Vector2 screenDeadZone = {0.1f, 0.1f};                          // Screen の遊び（画面の割合。端が 1）
	Vector2 screenSoftZone = {0.8f, 0.8f};                          // Screen の Soft Zone（画面の割合。1 より小さいと、focus はいつも画面の中）
	bool groundSnapY = true;  // true：追う物が地面にいる間は、縦の遊びを使わずに合わせる（XY のとき。跳んでも画面は揺れず、段に乗ると上がる）。追う物の Rigidbody・Character Controller が地面にいるかを見る（MotionQuery::IsGrounded）
	float smoothTime = 0.15f; // 遊びの外へ出たとき、追いつくまでの時間の目安（秒。0 ならすぐ合わせる）
	bool showGizmo = true;    // Dead Zone・Soft Zone・focus・Bounds の線を出す（エディタだけ。Screen の範囲は Game ビューだけ。View メニューの Camera Follow Zones で全部まとめて消せる）
	// --- ステージの端 ---
	bool useBounds = false;               // true：画面の端が boundsMin〜boundsMax の外を写さない（ステージの端でカメラが止まる。XY・XZ のとき）
	Vector2 boundsMin = {-10.0f, -10.0f}; // 写してよい範囲の左下（XY はワールドの X・Y、XZ はワールドの X・Z。追う物の奥行き・高さで測る）
	Vector2 boundsMax = {100.0f, 30.0f};  // 写してよい範囲の右上
	// --- Look Ahead のときだけ使う ---
	Vector2 lookAheadTime = {0.4f, 0.0f}; // 何秒先を見せるか（先読み ＝ 追う物の速さ × これ。Y を 0 にすると、縦には先読みしない）
	Vector2 lookAheadMax = {3.0f, 2.0f};  // 先読みの上限（m）。吹っ飛びのような速い動きで、先へ行きすぎないように
	float lookAheadSmoothTime = 0.3f;     // 先読みが変わるまでの時間の目安（秒。速さが急に変わっても、画面がガクッと動かないように）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isFollowing = false;                        // 追いかけ始めたか（false なら、次のフレームで focus へすぐ合わせる）
	float viewDepth = 0.0f;                          // View のとき、保つ奥行き（m。追いかけ始めの、カメラの前の向きに測った長さ）
	Vector2 currentLookAhead = {0.0f, 0.0f};         // 今の先読み（m。横・縦が＋。Center の間は 0 へ戻っていく）
	Vector3 lastTargetPosition = {0.0f, 0.0f, 0.0f}; // 前のフレームの、追う物の位置（速さを出すのに使う。カメラが回ると横・縦の向きが変わるので、ワールドの位置で持つ）
};

static_assert(std::is_trivially_copyable_v<CameraFollowComponent>);
