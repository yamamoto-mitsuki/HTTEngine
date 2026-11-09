#pragma once
#include <type_traits>

#include "HTTEngine/Math/Vector3.h"

// カメラの揺れ。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// ノイズ：ずっと続く小さな揺れ（手持ちカメラ・地鳴りなど）
// 衝撃：CameraSystem::AddImpulse で受けて、ばねのように揺れながら収まる揺れ
// どちらも写す値にだけ足して、Transform は書き換えない（揺れが無くなれば、必ず元の位置・向きに戻る）
// 動くのは再生中だけ。時間は Camera の Time Mode に従う

/// <summary>
/// カメラの揺れ（Unity の Cinemachine の Noise と Impulse Listener の小さい版）
/// </summary>
struct CameraShakeComponent {
	bool enabled = true; // false の間は揺らさない
	// --- ノイズ ---
	Vector3 noisePosition = {0.0f, 0.0f, 0.0f}; // 位置の揺れ幅（m。カメラの右・上・前）
	Vector3 noiseRotation = {0.0f, 0.0f, 0.0f}; // 向きの揺れ幅（度。X：上下の首振り Y：左右の首振り Z：傾き）
	float noiseFrequency = 1.0f;                // 1秒に揺れる回数の目安
	float noiseGain = 1.0f;                     // 揺れ幅に掛ける倍率（0 で止まる。少しずつ変えると、揺れを強めたり弱めたりできる）
	// --- 衝撃 ---
	float impulseGain = 1.0f;      // 受けた強さに掛ける倍率（0 なら受けない）
	float impulseFrequency = 8.0f; // 1秒に揺れる回数
	float impulseDamping = 0.2f;   // 収まる速さ（減衰比。0 に近いほど長く揺れ、1 に近いほどすぐ止まる）
	float impulseRotation = 2.0f;  // 1m ずれたときに一緒に回す角度（度）。遠くの景色も揺れて見えるように

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	float noiseTime = 0.0f;                       // ノイズを読む位置（秒 × 回数）
	Vector3 impulseOffset = {0.0f, 0.0f, 0.0f};   // 衝撃の今のずれ（m。カメラの右・上・前）
	Vector3 impulseVelocity = {0.0f, 0.0f, 0.0f}; // 衝撃の今の速さ（m/秒）
};

static_assert(std::is_trivially_copyable_v<CameraShakeComponent>);