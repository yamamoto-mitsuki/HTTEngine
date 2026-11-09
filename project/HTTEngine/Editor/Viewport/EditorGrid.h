#pragma once
#include <cstdint>
#include "HTTEngine/Graphics/Renderer/Renderer.h"


/// <summary>
/// Editorのグリッド表示クラス
/// </summary>
class EditorGrid {
public:
	void Initialize();
	void Draw();

private:
	uint32_t modelHandle_;
	uint32_t textureHandle_;
	Renderer::ModelConfig model_;
};