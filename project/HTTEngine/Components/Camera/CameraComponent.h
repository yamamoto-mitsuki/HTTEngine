#pragma once
#include <type_traits>

#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Time/EngineTime.h"

// ゲームの絵を撮るカメラ。Entityに付けるComponent（カテゴリ：Camera）
// 位置・向きは Transform（親の下に置けば一緒に動く）。ここに持つのは写り方と、このカメラへ切り替わるときの動き方
// 実際のカメラ（Camera クラス）は CameraSystem が1つ持ち、毎フレーム一番 priority の高い Camera の値を写す
// Transform は「カメラの本来の位置・向き」。注視点・揺れ・切り替えの補間は写す値にだけ入れて、Transform は書き換えない

inline constexpr float kMinCameraFovY = 1.0f;   // 視野角の下限（度）
inline constexpr float kMaxCameraFovY = 179.0f; // 視野角の上限（度）。180度以上は透視投影で作れない

/// <summary>
/// このカメラへの切り替え方
/// </summary>
enum class CameraBlendStyle {
	Cut,  // すぐ切り替える
	Ease, // Blend Time の秒をかけて、イージングで補間する
};


/// <summary>
/// カメラ
/// </summary>
struct CameraComponent {
	float fovY = 45.0f;   // 縦の視野角（度）。小さいほど望遠
	float nearZ = 0.1f;   // これより近い物は映らない
	float farZ = 1000.0f; // これより遠い物は映らない
	int priority = 0;     // 有効な Camera が複数あるとき、大きい方を使う（同じなら先に見つけた方）
	// --- このカメラへ切り替わるときの動き方 ---
	CameraBlendStyle blendStyle = CameraBlendStyle::Cut; // 切り替え方
	float blendTime = 1.0f;                              // 補間にかける秒（Ease のとき）
	Ease::Type blendEase = Ease::Type::InOutCubic;       // 補間の速さの変え方（Ease のとき）
	// --- 時間 ---
	TimeMode timeMode = TimeMode::Game; // 切り替え・追いかけ・注視点・揺れに使う時間
};

static_assert(std::is_trivially_copyable_v<CameraComponent>);