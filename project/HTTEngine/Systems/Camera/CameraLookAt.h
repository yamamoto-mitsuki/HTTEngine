#pragma once
#include <optional>

#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Math/Quaternion.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Look At の中身（CameraSystem から使う）
/// </summary>
namespace CameraLookAt {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraLookAtComponent& value);

// 今見ている点を、見たい点へ deltaTime 秒ぶん近づける（再生中だけ呼ぶ）
void Update(CameraLookAtComponent& lookAt, float deltaTime);

// 今見る点（ワールド）。再生中はなめらかにした点、停止中は見たい点そのもの（Inspector で変えた値がすぐ見えるように）。見ないなら nullopt
std::optional<Vector3> GetPoint(const CameraLookAtComponent& lookAt, bool isGameRunning);

/// <summary>
/// 見る点が、画面の screenPosition に写るカメラの向きを作る。カメラは横に傾けない（画面の上がワールドの上の側）
/// </summary>
/// <param name="position">カメラの位置（ワールド）</param>
/// <param name="point">見る点（ワールド）</param>
/// <param name="screenPosition">写す画面の位置（真ん中が 0、右と上が＋、端が ±1）</param>
/// <param name="fovYDegrees">縦の視野角（度）</param>
/// <param name="aspect">画面の横 ÷ 縦</param>
/// <returns>見る点がカメラとほとんど同じ位置で、向きが決まらないなら nullopt</returns>
std::optional<Quaternion> CalcRotation(const Vector3& position, const Vector3& point, const Vector2& screenPosition, float fovYDegrees, float aspect);
} // namespace CameraLookAt
