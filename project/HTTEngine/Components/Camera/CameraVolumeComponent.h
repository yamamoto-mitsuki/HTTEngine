#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"

// カメラごとのポストエフェクトの値。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// チェックを入れた項目だけ、このカメラの値を使う。外した項目は全体の設定（Settings → PostEffect・Depth Of Field Component）のまま
// カメラが切り替わるときは、位置・向きと一緒に同じ速さで値も補間する
// Bloom の Mode・ToneMap の Mode のように途中の値が無い項目は持たない（全体の設定のまま）

/// <summary>
/// 上書きできる値1つ
/// </summary>
struct CameraVolumeValue {
	bool isOverride = false; // true なら、このカメラの値を使う
	float value = 0.0f;      // このカメラの値
};

/// <summary>
/// ピントの合わせ方
/// </summary>
enum class CameraVolumeFocus {
	Global,       // 全体の設定（Depth Of Field Component の Focus）のまま
	Distance,     // カメラから Focus Distance（m）の奥行き
	LookAtTarget, // Camera Look At が見ている点の奥行き
	FollowTarget, // このカメラの Camera Follow・Camera Orbit の Target の奥行き
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(CameraVolumeFocus) {
	using enum CameraVolumeFocus;
	return MakeEnumTips<CameraVolumeFocus>({
	    {Global, "全体の設定（Depth Of Field Component の Focus）のまま"},
	    {Distance, "カメラから Focus Distance（m）の奥行きに合わせる"},
	    {LookAtTarget, "Camera Look At が見ている点の奥行きに合わせる"},
	    {FollowTarget, "このカメラの Camera Follow・Camera Orbit の Target の奥行きに合わせる"},
	});
}

/// <summary>
/// カメラごとのポストエフェクトの値（Unity の Cinemachine の Volume Settings の小さい版）
/// </summary>
struct CameraVolumeComponent {
	bool enabled = true; // false の間は全部全体の設定のまま
	float weight = 1.0f; // 全部の項目の効き具合（0：全体の設定のまま 〜 1：このカメラの値）
	// --- Depth Of Field ---
	CameraVolumeFocus focus = CameraVolumeFocus::Global; // ピントの合わせ方
	float focusDistance = 10.0f;                         // カメラからピントの合う所までの奥行き（m。Distance のとき）
	CameraVolumeValue focusRange = {false, 3.0f};        // ピントの前後この奥行き（m）までは、ぼけない
	CameraVolumeValue farTransition = {false, 40.0f};    // その奥から、一番ぼけるまでの奥行き（m）
	CameraVolumeValue nearTransition = {false, 4.0f};    // その手前から、一番ぼけるまでの奥行き（m）
	CameraVolumeValue maxBlur = {false, 8.0f};           // 一番ぼけた所の、ぼけの半径（ピクセル。0 ならぼかさない）
	// --- Bloom ---
	CameraVolumeValue bloomThreshold = {false, 1.0f}; // これより明るい所だけ光る
	CameraVolumeValue bloomKnee = {false, 0.25f};     // しきい値付近の柔らかさ
	CameraVolumeValue bloomIntensity = {false, 0.9f}; // 合成の強さ（0 なら光らない）
	// --- Lens ---
	CameraVolumeValue lensDistortion = {false, 0.0f}; // 画面端の歪み
	CameraVolumeValue lensAberration = {false, 0.0f}; // 色収差
	CameraVolumeValue lensRadialBlur = {false, 0.0f}; // 中心へ向かって流れるブラー
	CameraVolumeValue lensVignette = {false, 0.35f};  // 端の暗さ
	// --- ToneMap ---
	CameraVolumeValue exposure = {false, 1.0f}; // 露出（2 で1段明るい・0.5 で1段暗い）
	// --- Color Adjust ---
	CameraVolumeValue saturation = {false, 1.0f}; // 彩度（1 でそのまま・0 で白黒・1 より大きいと色が濃くなる）
};

static_assert(std::is_trivially_copyable_v<CameraVolumeComponent>);