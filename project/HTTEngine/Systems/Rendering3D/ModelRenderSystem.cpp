#include "ModelRenderSystem.h"

#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/IBL/IBLEnvironment.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"


//=============================================================================
// ModelRendererComponentの描画
//=============================================================================
void ModelRenderSystem::Draw(Handle<Entity> root, Camera* camera, IBLEnvironment* environment, const std::wstring& windowTitle) {
	if (!camera || windowTitle.empty()) {
		return;
	}

	// ModelRendererComponentを持つEntityだけを回す（全Entityの中から探さない）
	EntityManager::ForEach<ModelRendererComponent>([&](Handle<Entity> entity, const ModelRendererComponent& render) {
		if (!render.enabled || render.modelHandle == 0) {
			return;
		}
		// 自分か親のどれかが無効なら描かない
		if (!EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		// rootを指定したときは、root自身とその子孫だけ（rootが無効なHandleなら全部が対象）
		if (root.IsValid() && entity != root && !EntityManager::IsDescendantOf(entity, root)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (!transform || !ModelManager::GetModelAsset(render.modelHandle)) {
			return;
		}
		if (render.shadingType == ShadingType::PBR && (!environment || environment->GetParametersAddress() == 0)) {
			return;
		}

		Renderer::ModelConfig config{};
		config.modelHandle = render.modelHandle;
		config.textureHandle = render.textureHandle;
		config.color = ApplyIntensity(render.color, render.intensity); // 物ごとの色。明るさの倍率はリニアで掛ける（Color.h）
		config.uvTransform = render.uvTransform;
		config.material = render.material;
		config.overrideMaterial = render.overrideMaterial;
		config.shadingType = render.shadingType;
		config.blendMode = render.blendMode;
		config.rasterizerType = render.rasterizerType;
		config.depthMode = render.depthMode;
		config.billboard = render.billboard;
		config.receivesDecals = render.receivesDecals;
		config.worldMatrix = &transform->worldMatrix;
		config.nodeMatrices = AnimationSystem::GetNodeMatrices(entity);
		config.entity = entity;
		config.camera = camera;
		config.env = environment;
		config.windowTitle = windowTitle;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		Renderer::DrawModel(config);
	});
}