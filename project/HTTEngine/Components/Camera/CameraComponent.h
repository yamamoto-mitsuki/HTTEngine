#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Graphics/Renderer/SceneBackground.h"
#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Math/Vector3.h"
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

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(CameraBlendStyle) {
	using enum CameraBlendStyle;
	return MakeEnumTips<CameraBlendStyle>({
	    {Cut, "すぐ切り替える"},
	    {Ease, "Time の秒をかけて、前のカメラから位置・向き・ポストエフェクトの値をなめらかに補間する"},
	});
}


/// <summary>
/// カメラ
/// </summary>
struct CameraComponent {
	float fovY = 45.0f;   // 縦の視野角（度）。小さいほど望遠
	float nearZ = 0.1f;   // これより近い物は映らない
	float farZ = 1000.0f; // これより遠い物は映らない
	int priority = 0;     // 有効な Camera が複数あるとき、大きい方を使う（同じなら先に見つけた方）
	// --- 背景（物が描かれていない所）---
	// 色は全部 sRGB（Inspector で選ぶ色）。CameraSystem がリニアに直して SceneRenderer へ渡す
	BackgroundType backgroundType = BackgroundType::Skybox; // Skybox：天球 / SolidColor：backgroundColor の1色 / Gradient：下の3色
	Vector3 backgroundColor = {0.192f, 0.302f, 0.475f};     // SolidColor の色（既定は Unity の新しいカメラと同じ青）
	Vector3 gradientSkyColor = {0.36f, 0.58f, 0.86f};       // Gradient の真上の色（既定は青空）
	Vector3 gradientHorizonColor = {0.86f, 0.90f, 0.95f};   // Gradient の地平線の色（既定は白っぽい空）
	Vector3 gradientGroundColor = {0.42f, 0.40f, 0.38f};    // Gradient の真下の色（既定は灰色の地面）
	float gradientHorizonWidth = 0.15f;                     // 地平線の色が残る幅（見る向きの上下 y。0.15 なら上下に約 9°。その間をなめらかに Sky・Ground へ移る）
	// --- このカメラへ切り替わるときの動き方 ---
	CameraBlendStyle blendStyle = CameraBlendStyle::Cut; // 切り替え方
	float blendTime = 1.0f;                              // 補間にかける秒（Ease のとき）
	Ease::Type blendEase = Ease::Type::InOutCubic;       // 補間の速さの変え方（Ease のとき）
	// --- 時間 ---
	TimeMode timeMode = TimeMode::Game; // 切り替え・追いかけ・注視点・揺れに使う時間
};

static_assert(std::is_trivially_copyable_v<CameraComponent>);