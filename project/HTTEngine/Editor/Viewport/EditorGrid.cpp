#include "EditorGrid.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"

void EditorGrid::Initialize() { 
	modelHandle_ = ModelManager::Load("HTTEngine/Resources/Model/Grid/grid.obj");
	textureHandle_ = TextureManager::Load("HTTEngine/Resources/Textures/grid.png");
	model_.modelHandle = modelHandle_;
	model_.textureHandle = textureHandle_;
	model_.shadingType = ShadingType::Unlit;
	model_.rasterizerType = RasterizerType::SolidNone;
	model_.layer = DrawLayer::EditorGrid; // Scene・Game ビューで別々に切れるように（View メニュー）
}

void EditorGrid::Draw() { 
	Renderer::DrawModel(model_);
}