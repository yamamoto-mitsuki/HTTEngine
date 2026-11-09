#include "LightSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Components/Lighting/LightComponent.h"
#include "HTTEngine/Editor/Gizmos/LightGizmo.h"
#include "HTTEngine/Math/MathUtility.h"

// 静的メンバ変数
std::unique_ptr<LightSystem> LightSystem::instance_ = nullptr;

namespace {

/// <summary>
/// ライトの色（sRGB の見た目の色）→ シェーダーへ送るリニアの色（RGBA）。アルファはシェーダーで使わないので1固定
/// <para>sRGB → リニアの式は Color.h の SrgbToLinear（ほかの色と同じ物を使う）</para>
/// </summary>
Vector4 ToLightColor(const Vector3& color) {
	const Vector3 linear = SrgbToLinear(color);
	return {linear.x, linear.y, linear.z, 1.0f};
}

/// <summary>
/// 向きを正規化する。(0,0,0)だとシェーダーで壊れるので、そのときは真下にする（Scaleが0のときなど）
/// </summary>
Vector3 NormalizeDirection(const Vector3& direction) {
	if (LengthSq(direction) > 1e-6f) {
		return Normalize(direction);
	}
	return {0.0f, -1.0f, 0.0f};
}

/// <summary>
/// ライトとして使えるEntityなら、そのTransformを返す（自分か親が無効ならnullptr＝照らさない・ギズモも出さない）
/// </summary>
const TransformComponent* FindActiveTransform(Handle<Entity> entity) {
	if (!EntityManager::IsActiveInHierarchy(entity)) {
		return nullptr;
	}
	return EntityManager::Get<TransformComponent>(entity);
}

// ライトが照らす向き（Transformの前＝ローカルの+Z）
Vector3 LightDirection(const TransformComponent& transform) { return NormalizeDirection(GetWorldForward(transform)); }
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void LightSystem::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼ばれています");
	instance_ = std::make_unique<LightSystem>(ConstructorKey());
	// ライトもEntityに付けるComponentとして、EntityManagerに置き場所を作ってもらう
	EntityManager::RegisterComponent<DirectionalLightComponent>();
	EntityManager::RegisterComponent<PointLightComponent>();
	EntityManager::RegisterComponent<SpotLightComponent>();
}

void LightSystem::Release() {
	instance_.reset();
}


//=============================================================================
// 更新
//=============================================================================
void LightSystem::Update() { instance_->CollectForGPU(); }

// ====== Component → GPU用データにまとめてRendererへ渡す =====
void LightSystem::CollectForGPU() {
	// --- 平行光源（GPUには1つだけ。有効な物のうち最初に見つかった物を使う）---
	DirectionalLightData directional;
	directional.intensity = 0.0f; // 1つも無ければ照らさない
	uint32_t directionalCount = 0;
	EntityManager::ForEach<DirectionalLightComponent>([&](Handle<Entity> entity, const DirectionalLightComponent& light) {
		const TransformComponent* transform = FindActiveTransform(entity);
		if (!transform) {
			return;
		}
		if (directionalCount == 0) {
			directional.color = ToLightColor(light.color);
			directional.intensity = light.intensity;
			directional.direction = LightDirection(*transform);
		}
		++directionalCount;
	});
	if (directionalCount > 1 && !hasWarnedDirectionalLightLimit_) {
		LogManager::Warning(std::format("平行光源が{}個あります。使われるのは1つだけです", directionalCount));
		hasWarnedDirectionalLightLimit_ = true;
	}

	// --- ポイントライト（見つかった順に上限まで） ---
	PointLightListData pointList;
	uint32_t pointCount = 0;
	EntityManager::ForEach<PointLightComponent>([&](Handle<Entity> entity, const PointLightComponent& light) {
		const TransformComponent* transform = FindActiveTransform(entity);
		if (!transform) {
			return;
		}
		if (pointCount >= kMaxPointLights) {
			if (!hasWarnedPointLightLimit_) {
				LogManager::Warning(std::format("ポイントライトが上限({}個)を超えています。超えた分は描画に使われません", kMaxPointLights));
				hasWarnedPointLightLimit_ = true;
			}
			return;
		}
		PointLightData& data = pointList.lights[pointCount];
		data.color = ToLightColor(light.color);
		data.position = GetWorldPosition(*transform);
		data.intensity = light.intensity;
		data.radius = light.radius; // 0や負の値はシェーダー側で安全な値に丸めている
		data.decay = light.decay;
		++pointCount;
	});
	pointList.count = pointCount;

	// --- スポットライト（見つかった順に上限まで） ---
	SpotLightListData spotList;
	uint32_t spotCount = 0;
	EntityManager::ForEach<SpotLightComponent>([&](Handle<Entity> entity, const SpotLightComponent& light) {
		const TransformComponent* transform = FindActiveTransform(entity);
		if (!transform) {
			return;
		}
		if (spotCount >= kMaxSpotLights) {
			if (!hasWarnedSpotLightLimit_) {
				LogManager::Warning(std::format("スポットライトが上限({}個)を超えています。超えた分は描画に使われません", kMaxSpotLights));
				hasWarnedSpotLightLimit_ = true;
			}
			return;
		}
		SpotLightData& data = spotList.lights[spotCount];
		data.color = ToLightColor(light.color);
		data.position = GetWorldPosition(*transform);
		data.intensity = light.intensity;
		data.direction = LightDirection(*transform);
		data.range = light.range; // 0や負の値はシェーダー側で安全な値に丸めている
		data.decay = light.decay;
		// 角度（度）→ cos。内側が外側より大きいと明るさの向きが逆になるので、外側までに収める
		float outerAngle = std::clamp(light.outerAngle, 0.0f, SpotLightComponent::kMaxAngle);
		float innerAngle = std::clamp(light.innerAngle, 0.0f, outerAngle);
		data.cosOuter = std::cos(outerAngle * MathUtility::kDegToRad);
		data.cosInner = std::cos(innerAngle * MathUtility::kDegToRad);
		++spotCount;
	});
	spotList.count = spotCount;

	Renderer::SetFrameLights(directional, pointList, spotList);
}


//=============================================================================
// ギズモ
//=============================================================================
void LightSystem::DrawGizmos(Camera* camera) {
	LightGizmo::Begin(camera);
	EntityManager::ForEach<DirectionalLightComponent>([](Handle<Entity> entity, const DirectionalLightComponent& light) {
		if (const TransformComponent* transform = FindActiveTransform(entity)) {
			LightGizmo::AddDirectionalLight(GetWorldPosition(*transform), LightDirection(*transform), light);
		}
	});
	EntityManager::ForEach<PointLightComponent>([](Handle<Entity> entity, const PointLightComponent& light) {
		if (const TransformComponent* transform = FindActiveTransform(entity)) {
			LightGizmo::AddPointLight(GetWorldPosition(*transform), light);
		}
	});
	EntityManager::ForEach<SpotLightComponent>([](Handle<Entity> entity, const SpotLightComponent& light) {
		if (const TransformComponent* transform = FindActiveTransform(entity)) {
			LightGizmo::AddSpotLight(GetWorldPosition(*transform), LightDirection(*transform), light);
		}
	});
	LightGizmo::End();
}