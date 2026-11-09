#pragma once
#include "HTTEngine/Components/Camera/CameraShakeComponent.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// 揺れのずれ（カメラの向きから見た値）
/// </summary>
struct CameraShakeOffset {
	Vector3 position = {0.0f, 0.0f, 0.0f}; // 位置のずれ（m。カメラの右・上・前）
	Vector3 rotation = {0.0f, 0.0f, 0.0f}; // 向きのずれ（度。オイラー角）
};

/// <summary>
/// Camera Shake の中身（CameraSystem から使う）
/// </summary>
namespace CameraShake {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraShakeComponent& value);

// 揺れを deltaTime 秒進める
void Update(CameraShakeComponent& shake, float deltaTime);

// 今のずれ（読むだけ。Transform には足さず、写す値にだけ足す）
CameraShakeOffset CalcOffset(const CameraShakeComponent& shake);

/// <summary>
/// 衝撃を受ける
/// </summary>
/// <param name="strength">最初に振れる幅（m）。止まっているカメラなら、ちょうどこの幅まで振れる</param>
/// <param name="direction">最初に振れる向き（カメラの右・上・前）。長さは1でなくてよい</param>
void AddImpulse(CameraShakeComponent& shake, float strength, const Vector3& direction);
} // namespace CameraShake