#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// カメラを、追う物のまわりの球の上に置く。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// 位置 ＝ 回る中心（追う物 ＋ Pivot Offset）から、横の向き（Heading ＋ Yaw）の後ろへ Distance 離れ、Pitch の角度だけ上。向きは回る中心を見る
// Camera Look At を一緒に付けると、写す向きは Camera Look At が決める（Transform の向きは回る中心を見たまま）
//   Heading を LookAt にすると、見る点・回る中心・カメラが上から見て一直線になる（相手を見ながら、追う物の後ろに付くロックオン）
// 動かすのは CameraSystem::UpdateFollow（再生中だけ。物理で動いた後・カメラの絵を決める前）。停止中は置いた位置のまま
// 同じ Entity に Camera Follow も付いていて、どちらも有効なら、Camera Orbit だけが動かす（どちらもカメラの位置を決めるので）

// ===== 横の向き（カメラが追うもののどちら側に付くか）の決め方。どれもYawの角度を足す =====
enum class CameraOrbitHeading {
	World,         // ワールドの決まった向き（Yaw 0で +Zを向く=追う物の -zの側に付く。）ScriptでYawを書き換えると回せる
	TargetForward, // 追う物の前の向き（追うものの後ろにつく。追う物が振り変えると回り込む）
	LookAt,        // 同じ Entity の Camera Look At が見る点の向き（見る点と反対の側に付く）。Camera Look At が無い・見る点が無いときは、向きをそのまま保つ
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(CameraOrbitHeading) {
	using enum CameraOrbitHeading;
	return MakeEnumTips<CameraOrbitHeading>({
	    {World, "ワールドの決まった向き（Yaw 0 で +Z を向き、追う物の -Z の側に付く）。Script で Yaw を書き換えると回せる"},
	    {TargetForward, "追う物の前の向き（追う物の後ろに付く。追う物が振り向くと回り込む）"},
	    {LookAt, "同じ Entity の Camera Look At が見る点の向き（見る点と反対の側に付く＝相手を見ながら、追う物の後ろに付くロックオン）。見る点が無いときは向きをそのまま保つ"},
	});
}

// ===== 追う物のまわりを回るカメラ =====
struct CameraOrbitComponent {
	bool enabled = true;                                            // false の間は動かさない（置いた位置・向きのまま）
	EntityRef target;                                               // 追う Entity（Hierarchy からドラッグ。保存は EntityId で）
	Vector3 pivotOffset = {0.0f, 1.5f, 0.0f};                       // 追う物の位置から、回る中心へのずれ（m。ワールドの向き。足元の原点から胸・頭の高さへ上げる）
	float distance = 6.0f;                                          // 回る中心からカメラまでの長さ（m）
	float pitch = 15.0f;                                            // 見下ろす角度（度。＋ で上から見る。-80〜80）
	CameraOrbitHeading heading = CameraOrbitHeading::TargetForward; // 横の向きの決め方
	float yaw = 0.0f;                                               // 横の角度（度）。World：向きそのもの / ほか：決まった向きに足す角度（ずらすと斜め後ろ・肩越しになる）
	float smoothTime = 0.1f;                                        // 回る中心が、追う物に追いつくまでの時間の目安（秒。0 ならすぐ合わせる）
	float headingSmoothTime = 0.3f;                                 // 横の向きが、決まった向きに追いつくまでの時間の目安（秒。0 ならすぐ。相手が急に回り込んでも、画面がガクッと回らない）
	bool showGizmo = true;                                          // Scene ビューに、回る中心・回る輪・カメラを置く位置の線を出す（エディタだけ。View メニューの Camera Orbits で全部まとめて消せる）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isOrbiting = false;                   // 回り始めたか（false なら、次のフレームで決まった位置・向きへすぐ合わせる）
	Vector3 currentPivot = {0.0f, 0.0f, 0.0f}; // 今の回る中心（ワールド）
	float currentYaw = 0.0f;                   // 今の横の向き（度。ワールドで、0 が +Z・90 が +X）
};

static_assert(std::is_trivially_copyable_v<CameraOrbitComponent>);