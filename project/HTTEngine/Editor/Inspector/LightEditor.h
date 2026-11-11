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
	const char* GetDescription() const override { return "太陽のように、どこでも同じ向きから当たる光。向きは Transform の回転（ローカルの +Z へ照らす）"; }

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
	const char* GetDescription() const override { return "電球のように、1点からまわりへ広がる光。位置は Transform、届く距離は Radius"; }

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
	const char* GetDescription() const override { return "懐中電灯のように、円すいの形に照らす光。位置と向きは Transform（ローカルの +Z へ照らす）"; }

protected:
	void DrawComponent(SpotLightComponent& light) const override;
};