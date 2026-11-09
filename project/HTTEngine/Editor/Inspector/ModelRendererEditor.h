#pragma once
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Components/Rendering3D/ModelRendererComponent.h"

/// <summary>
/// ModelRendererComponentのInspector（どのモデルを描くか・描画設定・マテリアル）
/// </summary>
class ModelRendererEditor : public TypedComponentEditor<ModelRendererComponent> {
public:
	const char* GetName() const override { return "Model Renderer"; }
	const char* GetCategory() const override { return "Rendering3D"; }

protected:
	void DrawComponent(ModelRendererComponent& render) const override;
};