#pragma once
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Components/Lighting/LightComponent.h"

// ライトのInspector（3種類。どれもカテゴリはLighting）
// 位置と向きはComponentに無いので、TransformのPosition / Rotationで動かす

/// <summary>
/// 平行光源（Transformの前＝+Zの向きに照らす）
/// </summary>
class DirectionalLightEditor : public TypedComponentEditor<DirectionalLightComponent> {
public:
	const char* GetName() const override { return "Directional Light"; }
	const char* GetCategory() const override { return "Lighting"; }

protected:
	void DrawComponent(DirectionalLightComponent& light) const override;
};

/// <summary>
/// ポイントライト（Transformの位置から全方向を照らす）
/// </summary>
class PointLightEditor : public TypedComponentEditor<PointLightComponent> {
public:
	const char* GetName() const override { return "Point Light"; }
	const char* GetCategory() const override { return "Lighting"; }

protected:
	void DrawComponent(PointLightComponent& light) const override;
};

/// <summary>
/// スポットライト（Transformの位置から、前＝+Zの向きを照らす）
/// </summary>
class SpotLightEditor : public TypedComponentEditor<SpotLightComponent> {
public:
	const char* GetName() const override { return "Spot Light"; }
	const char* GetCategory() const override { return "Lighting"; }

protected:
	void DrawComponent(SpotLightComponent& light) const override;
};