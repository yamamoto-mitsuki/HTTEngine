#pragma once
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// 3D の絵の背景（物が描かれていない所）の塗り方（Unity の Camera の Background Type）
/// <para>番号は Skybox.PS の kBackground〜 と同じ（シェーダーへ番号のまま送る）</para>
/// </summary>
enum class BackgroundType {
	Skybox,     // 天球（SceneRenderer の Skybox の絵）
	SolidColor, // 1色で塗る（霧は掛けない）
	Gradient,   // 上（Sky）・地平線（Horizon）・下（Ground）の3色を、見る向きの上下で混ぜる（霧は掛けない）
};

/// <summary>
/// 背景の塗り方。SceneRenderer が Skybox::Draw に渡す
/// <para>メインカメラの Camera Component から CameraSystem が作る。Scene ビューは EditorViewport が View メニューから作る</para>
/// <para>色は全部リニア（1 を超えるとブルームで光る）。使うのは type で選んだ物だけ</para>
/// </summary>
struct SceneBackground {
	BackgroundType type = BackgroundType::Skybox;
	Vector3 color = {0.0f, 0.0f, 0.0f}; // SolidColor のときの色
	// --- Gradient ---
	Vector3 skyColor = {0.0f, 0.0f, 0.0f};     // 真上の色
	Vector3 horizonColor = {0.0f, 0.0f, 0.0f}; // 地平線（真横）の色
	Vector3 groundColor = {0.0f, 0.0f, 0.0f};  // 真下の色
	float horizonWidth = 0.15f;                // 地平線の色が残る幅（見る向きの上下 y の値。0.15 なら上下に約 9°。その間をなめらかに Sky・Ground へ移る）
};
