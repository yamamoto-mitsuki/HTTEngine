#pragma once
#include "MyEngine/Components/Camera/CameraPathComponent.h"
#include "MyEngine/Math/Geometry/PathCurve.h"
#include "MyEngine/Math/Quaternion.h"
#include "MyEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Path の中身（CameraSystem から使う）
/// </summary>
namespace CameraPath {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraPathComponent& value);

// 線の Entity の子から線を作る（点が 2 つより少なければ空の線）。座標はワールド
void BuildLine(const CameraPathComponent& path, PathCurve& out);

// position・rotation を、線の上の位置と向きへ置き換える（線が無ければ変えない）
void Apply(const CameraPathComponent& path, Vector3& position, Quaternion& rotation);
} // namespace CameraPath