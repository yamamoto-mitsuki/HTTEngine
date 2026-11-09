#pragma once
#include "HTTEngine/Math/Vector3.h"

// Inspectorで編集するライトのデータ。Entityに付けるComponent（カテゴリ：Lighting）
// GPUへ送る構造体（ShaderConstants.h）とは分けて、padding・ポインタ・GPUリソースを持たせない
// 位置と向きは持たない。付けたEntityのTransformから取る（位置＝ワールド座標、向き＝ローカルの+Z）

/// <summary>
/// ライトのエディタ表示のON / OFF。ゲームの絵には影響しない
/// </summary>
struct LightGizmoFlags {
	bool showIcon = true;  // アイコン
	bool showRange = true; // 光の届く範囲（平行光源は向き）のワイヤー
};

/// <summary>
/// 平行光源。GPUに送るのは1つだけ（有効な物のうち最初に見つかった物）
/// </summary>
struct DirectionalLightComponent {
	Vector3 color = {1.0f, 1.0f, 1.0f}; // 色
	float intensity = 1.0f;             // 強さ
	LightGizmoFlags gizmo;              // ギズモの表示設定（アイコンはまだ無いので、向きの線だけ）
};

/// <summary>
/// ポイントライト
/// </summary>
struct PointLightComponent {
	Vector3 color = {1.0f, 1.0f, 1.0f}; // 色
	float intensity = 1.0f;             // 強さ
	float radius = 10.0f;               // ライトの届く最大距離
	float decay = 1.0f;                 // 減衰率（-にはしない）
	LightGizmoFlags gizmo;              // ギズモの表示設定
};

/// <summary>
/// スポットライト。Transformの前（+Z）を照らす
/// </summary>
struct SpotLightComponent {
	static constexpr float kMaxAngle = 89.0f; // 角度の条件（度）。90度で円錐が平らになる

	Vector3 color = {1.0f, 1.0f, 1.0f}; // 色
	float intensity = 1.0f;             // 強さ
	float range = 10.0f;                // ライトの届く最大距離
	float decay = 1.0f;                 // 減衰率（-にはしない）
	float outerAngle = 30.0f;           // 外側の角度（度）。これより外は照らさない
	float innerAngle = 20.0f;           // 内側の角度（度）。これより内は100%で照らす。外側より大きくしない
	LightGizmoFlags gizmo;              // ギズモの表示設定
};