#include "GizmoLines.h"

#include <array>
#include <cmath>

#include "HTTEngine/Math/MathUtility.h"

//=============================================================================
// 円・弧
//=============================================================================
void GizmoLines::PushCircle(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, 
	const Vector3& axisA, const Vector3& axisB, const Vector4& color, uint32_t division) {
	PushArc(lines, center, radius, axisA, axisB, 0.0f, MathUtility::kTwoPi, color, division); // 円＝1周（0〜2π）の弧
}

void GizmoLines::PushArc(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, 
	const Vector3& axisA, const Vector3& axisB, float startAngle, float endAngle, const Vector4& color,
    uint32_t division) {
	// 角度 angle の所の点（axisA と axisB が作る平面の上）
	const auto pointAt = [&](float angle) { return center + axisA * (std::cos(angle) * radius) + axisB * (std::sin(angle) * radius); };
	Vector3 prev = pointAt(startAngle);
	for (uint32_t i = 1; i <= division; ++i) {
		const float angle = startAngle + (endAngle - startAngle) * static_cast<float>(i) / static_cast<float>(division);
		const Vector3 current = pointAt(angle);
		lines.push_back({prev, current, color});
		prev = current; // 次の線の始点にする
	}
}

//=============================================================================
// 球
//=============================================================================
void GizmoLines::PushWireSphere(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, const Vector4& color, uint32_t division) {
	const Vector3 axisX = {1.0f, 0.0f, 0.0f};
	const Vector3 axisY = {0.0f, 1.0f, 0.0f};
	const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
	PushCircle(lines, center, radius, axisX, axisY, color, division); // XY平面
	PushCircle(lines, center, radius, axisY, axisZ, color, division); // YZ平面
	PushCircle(lines, center, radius, axisZ, axisX, color, division); // ZX平面
}

//=============================================================================
// カプセル
//=============================================================================
void GizmoLines::PushWireCapsule(std::vector<Renderer::LineSegment>& lines, const Vector3& start, const Vector3& end, 
	float radius, const Vector4& color, uint32_t division) {
	const Vector3 segment = end - start;
	const float length = Length(segment);
	if (length < 1e-6f) {
		PushWireSphere(lines, start, radius, color, division); // 線分が無い＝球
		return;
	}
	// ===== 線分の向きと、それに垂直な2つの向き =====
	// 垂直な向きは、線分の向きと「線分と平行でない向き（Y に近ければ X、そうでなければ Y）」の外積で作る
	const Vector3 axis = segment / length;
	const Vector3 notParallel = (std::abs(axis.y) < 0.99f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	const Vector3 side = Normalize(Cross(notParallel, axis));
	const Vector3 front = Cross(axis, side);                             // axis と side が長さ1で垂直なので、これも長さ1
	const uint32_t halfDivision = (division / 2 > 0) ? division / 2 : 1; // 半球の弧は輪の半分の本数

	// ===== 両端の輪 =====
	PushCircle(lines, start, radius, side, front, color, division);
	PushCircle(lines, end, radius, side, front, color, division);

	// ===== 横の線（輪の4か所を結ぶ）=====
	const std::array<Vector3, 4> offsets = {side, -side, front, -front};
	for (const Vector3& offset : offsets) {
		lines.push_back({start + offset * radius, end + offset * radius, color});
	}

	// ===== 両端の半球（角度0〜π で side → 外向き → -side と半周する弧を、2つの向きで）=====
	// 上の端（end）は線分の向きへ、下の端（start）は逆向きへふくらむ
	PushArc(lines, end, radius, side, axis, 0.0f, MathUtility::kPi, color, halfDivision);
	PushArc(lines, end, radius, front, axis, 0.0f, MathUtility::kPi, color, halfDivision);
	PushArc(lines, start, radius, side, -axis, 0.0f, MathUtility::kPi, color, halfDivision);
	PushArc(lines, start, radius, front, -axis, 0.0f, MathUtility::kPi, color, halfDivision);
}