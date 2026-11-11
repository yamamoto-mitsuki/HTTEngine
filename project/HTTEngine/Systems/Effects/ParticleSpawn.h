#pragma once
#include <vector>

#include "HTTEngine/Components/Effects/ParticleSystemComponent.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"

/// <summary>
/// Particle System の Shape（粒が出る場所と向き）。ParticleSystem から使う
/// <para>粒を出す所と、エディタの線を同じファイルに置く（形を足す・変えるときに、出し方と線がずれないように）</para>
/// </summary>
namespace ParticleSpawn {
/// <summary>
/// 粒が出る場所と、飛ぶ向き（どちらも Entity から見た座標）
/// </summary>
struct SpawnPoint {
	Vector3 position;  // 出る場所
	Vector3 direction; // 飛ぶ向き（長さ1）
};

// 形の中（From Surface なら表面）から、出る場所と向きを1つ選ぶ
SpawnPoint Sample(const ParticleSystemComponent& component);

// 形の線を out に足す（worldMatrix：Entity のワールド行列。粒を出すときと同じく、位置・回転・大きさが効く）
void PushShapeGizmo(std::vector<Renderer::LineSegment>& out, const ParticleSystemComponent& component, const Matrix4x4& worldMatrix, const Vector4& color);
} // namespace ParticleSpawn
