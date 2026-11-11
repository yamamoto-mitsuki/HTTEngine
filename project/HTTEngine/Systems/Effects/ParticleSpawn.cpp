#include "ParticleSpawn.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
#include "HTTEngine/Math/Geometry/AABB.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Math/RandomEngine.h"

namespace {
constexpr float kMinDirectionLengthSq = 1e-4f; // これより短い向きは、向きが決まらないとみなす
constexpr float kPointMarkSize = 0.1f;         // Point の印（3つの軸の十字）の半分の長さ（m）
constexpr float kConeGizmoLength = 1.0f;       // Cone の線で、開く向きを見せる長さ（m。粒が飛ぶ距離ではない）
constexpr int kConeSideLines = 4;              // Cone の、底の輪から開く向きへ引く線の数

// 長さ1のランダムな向き（球の中の点を長さ1に伸ばす。真ん中に近すぎて向きが決まらない点は選び直す）
Vector3 RandomDirection() {
	while (true) {
		const Vector3 point = RandomEngine::GetInsideUnitSphere();
		const float lengthSq = LengthSq(point);
		if (lengthSq > kMinDirectionLengthSq) {
			return point * (1.0f / std::sqrt(lengthSq));
		}
	}
}

// Box の半分の大きさ（負の値は正にする）
Vector3 HalfBoxSize(const ParticleSystemComponent& component) {
	return {std::abs(component.boxSize.x) * 0.5f, std::abs(component.boxSize.y) * 0.5f, std::abs(component.boxSize.z) * 0.5f};
}

// Cone の、縁（真ん中から outward へ radius 離れた所）から出た粒の飛ぶ向き。縁で Cone Angle だけ外へ傾く
Vector3 ConeRimDirection(const ParticleSystemComponent& component, const Vector3& outward) {
	const float tilt = component.coneAngle * MathUtility::kDegToRad;
	return outward * std::sin(tilt) + Vector3{0.0f, std::cos(tilt), 0.0f};
}
} // namespace

//=============================================================================
// 粒の出る場所と向き
//=============================================================================
// 中に均等に散らすため、真ん中からの距離は「0〜1 の乱数」をそのまま使わず、球なら3乗根・円なら平方根にする
// （球の中の体積は半径の3乗、円の面積は半径の2乗に比例するので、そのままだと真ん中に集まって見える）
ParticleSpawn::SpawnPoint ParticleSpawn::Sample(const ParticleSystemComponent& component) {
	switch (component.shape) {
	case ParticleShape::Point:
		return {
		    {0.0f, 0.0f, 0.0f},
            RandomDirection()
        };

	case ParticleShape::Sphere: {
		const Vector3 direction = RandomDirection();
		const float distance = component.fromSurface ? 1.0f : std::cbrt(RandomEngine::GetFloat(0.0f, 1.0f));
		return {direction * (component.radius * distance), direction};
	}

	case ParticleShape::Circle: {
		// XY の面（横から見るゲームで、画面と同じ向きの円）
		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
		const Vector3 direction = {std::cos(angle), std::sin(angle), 0.0f};
		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
		return {direction * (component.radius * distance), direction};
	}

	case ParticleShape::Cone: {
		// XZ の面の円から出て、上（+Y）へ。縁から出た粒ほど外へ傾ける（縁で Cone Angle。真ん中はまっすぐ上）
		// → 粒どうしが交差せず、ラッパの形に広がる。Radius 0 でも、傾きは乱数で決まるので広がる
		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
		const Vector3 outward = {std::cos(angle), 0.0f, std::sin(angle)};
		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
		const float tilt = component.coneAngle * MathUtility::kDegToRad * distance;
		const Vector3 direction = outward * std::sin(tilt) + Vector3{0.0f, std::cos(tilt), 0.0f};
		return {outward * (component.radius * distance), direction};
	}

	case ParticleShape::Box: {
		const Vector3 half = HalfBoxSize(component);
		Vector3 position = RandomEngine::GetVector3({-half.x, -half.y, -half.z}, half);
		if (component.fromSurface) {
			// 面を1つ選んで（広い面ほど選ばれやすく）、その軸だけ端へ寄せる
			const float areaX = half.y * half.z; // 左右の面の広さ（の 1/4。比べるだけなので、そろっていればよい）
			const float areaY = half.x * half.z; // 上下の面
			const float areaZ = half.x * half.y; // 前後の面
			const float pick = RandomEngine::GetFloat(0.0f, areaX + areaY + areaZ);
			const float side = (RandomEngine::GetInt(0, 1) == 0) ? -1.0f : 1.0f;
			if (pick < areaX) {
				position.x = half.x * side;
			} else if (pick < areaX + areaY) {
				position.y = half.y * side;
			} else {
				position.z = half.z * side;
			}
		}
		// 向きは箱の真ん中から外へ（ちょうど真ん中に出た粒は、どの向きでも）
		const Vector3 direction = (LengthSq(position) > kMinDirectionLengthSq) ? Normalize(position) : RandomDirection();
		return {position, direction};
	}
	}
	return {
	    {0.0f, 0.0f, 0.0f},
        RandomDirection()
    };
}

//=============================================================================
// エディタの線
//=============================================================================
// Entity から見た座標で線を足してから、足した分だけワールドへ写す（Transform の大きさで形が伸びるのも、粒を出すときと同じ）
void ParticleSpawn::PushShapeGizmo(std::vector<Renderer::LineSegment>& out, const ParticleSystemComponent& component, const Matrix4x4& worldMatrix, const Vector4& color) {
	const size_t first = out.size();
	const Vector3 origin = {0.0f, 0.0f, 0.0f};
	const Vector3 axisX = {1.0f, 0.0f, 0.0f};
	const Vector3 axisY = {0.0f, 1.0f, 0.0f};
	const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
	switch (component.shape) {
	case ParticleShape::Point:
		// 点は大きさが無いので、3つの軸の小さな十字
		out.push_back({axisX * -kPointMarkSize, axisX * kPointMarkSize, color});
		out.push_back({axisY * -kPointMarkSize, axisY * kPointMarkSize, color});
		out.push_back({axisZ * -kPointMarkSize, axisZ * kPointMarkSize, color});
		break;

	case ParticleShape::Sphere:
		GizmoLines::PushWireSphere(out, origin, component.radius, color);
		break;

	case ParticleShape::Circle:
		GizmoLines::PushCircle(out, origin, component.radius, axisX, axisY, color); // XY の面
		break;

	case ParticleShape::Cone: {
		// 底の輪（XZ の面）と、縁から出た粒の向きへ kConeGizmoLength 進んだ所の輪。間を何本かの線で結ぶ（ラッパの形）
		const float tilt = component.coneAngle * MathUtility::kDegToRad;
		const Vector3 topCenter = axisY * (kConeGizmoLength * std::cos(tilt));
		const float topRadius = component.radius + kConeGizmoLength * std::sin(tilt);
		GizmoLines::PushCircle(out, origin, component.radius, axisX, axisZ, color);
		GizmoLines::PushCircle(out, topCenter, topRadius, axisX, axisZ, color);
		for (int i = 0; i < kConeSideLines; ++i) {
			const float angle = MathUtility::kTwoPi * static_cast<float>(i) / static_cast<float>(kConeSideLines);
			const Vector3 outward = {std::cos(angle), 0.0f, std::sin(angle)};
			const Vector3 rim = outward * component.radius;
			out.push_back({rim, rim + ConeRimDirection(component, outward) * kConeGizmoLength, color});
		}
		break;
	}

	case ParticleShape::Box: {
		const Vector3 half = HalfBoxSize(component);
		const std::array<Vector3, 8> corners = AABB{-half, half}.GetCorners();
		for (const auto& [from, to] : kBoxEdges) {
			out.push_back({corners[from], corners[to], color});
		}
		break;
	}
	}
	for (size_t i = first; i < out.size(); ++i) {
		out[i].start = TransformPoint(out[i].start, worldMatrix);
		out[i].end = TransformPoint(out[i].end, worldMatrix);
	}
}
