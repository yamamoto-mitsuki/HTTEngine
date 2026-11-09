#pragma once
#include <optional>

#include "HTTEngine/Components/Camera/CameraVolumeComponent.h"
#include "HTTEngine/Graphics/PostEffect/PostOverride.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Volume の中身（CameraSystem から使う）
/// </summary>
namespace CameraVolume {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraVolumeComponent& value);

/// <summary>
/// このカメラの上書きを作る
/// </summary>
/// <param name="focusDistance">ピントの奥行き（m）。全体の設定のままなら nullopt</param>
PostOverrides MakeOverrides(const CameraVolumeComponent& volume, std::optional<float> focusDistance);
} // namespace CameraVolume