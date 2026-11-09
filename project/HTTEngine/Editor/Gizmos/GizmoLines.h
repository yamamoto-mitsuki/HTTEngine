#pragma once
#include <vector>
#include <cstdint>

#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"

/// <summary>
/// ギズモの線の形を作る（renderer::LineListConfig::linesに足していく）
/// </summary>
namespace GizmoLines {
constexpr uint32_t kDefaultCircleDivision = 24; // 円を何本の線で描くか

/// <summary>
/// axisA と axisB が作る平面に、中心center・半径radiusの円を線で並べる
/// </summary>
/// <param name="division">何本の線で描くか</param>
void PushCircle(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, const Vector3& axisA, const Vector3& axisB, 
	const Vector4& color, uint32_t division = kDefaultCircleDivision);

/// <summary>
/// 円の一部（弧）。角度 startAngle〜endAngle（ラジアン。0 が axisA の向き、π/2 が axisB の向き）
/// </summary>
/// <param name="division">何本の線で描くか</param>
void PushArc(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, const Vector3& axisA, const Vector3& axisB, 
	float startAngle, float endAngle, const Vector4& color,
    uint32_t division = kDefaultCircleDivision);

/// <summary>
/// 球: XY・YZ・ZX の3方向の円を重ねる
/// </summary>
void PushWireSphere(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, const Vector4& color,
	uint32_t division = kDefaultCircleDivision);

/// <summary>
/// カプセル（start → end の線分を半径 radius で太らせた形）：両端の輪・横の線4本・両端の半球の弧
/// </summary>
/// <param name="division">輪を何本の線で描くか（半球の弧はその半分）</param>
void PushWireCapsule(std::vector<Renderer::LineSegment>& lines, const Vector3& start, const Vector3& end, float radius, const Vector4& color, 
	uint32_t division = kDefaultCircleDivision);
} // namespace GizmoLines