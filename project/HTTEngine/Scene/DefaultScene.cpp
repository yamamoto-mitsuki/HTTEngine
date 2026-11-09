#include "DefaultScene.h"

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Lighting/LightComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Scene/Skybox.h"
#include "HTTEngine/Systems/Rendering3D/ModelRenderSystem.h"


//=============================================================================
// 初期化（Entity がそろった後に呼ばれる）
//=============================================================================
void DefaultScene::Initialize() {
	// 環境光（IBL）。天球と同じ既定の画像から作る（シーンごとに空を変えるのは SceneFlow.md の SF4）
	environment_.MakeFromHDR(TextureManager::Load(Skybox::kDefaultSkyPath));
}

//=============================================================================
// 描画
//=============================================================================
void DefaultScene::Draw() {
	// 全部の Entity のモデルを描く（カメラは Hierarchy の Camera Component のカメラ）
	ModelRenderSystem::Draw({}, GetCamera().get(), &environment_, GetWindowTitle());
}

//=============================================================================
// 新しいシーンの最初の配置（シーンファイルがまだ無いときだけ呼ばれる）
//=============================================================================
void DefaultScene::CreateDefaultEntities() {
	// カメラ（Unity の新しいシーンと同じ位置。見た目は Inspector で直して Save する）
	const Handle<Entity> camera = EntityManager::Create("Main Camera");
	EntityManager::Get<TransformComponent>(camera)->translation = {0.0f, 1.0f, -10.0f};
	EntityManager::RequestAdd<CameraComponent>(camera);

	// 平行光源（+Z の向きに照らす。向きは GameScene の最初の配置と同じ）
	const Handle<Entity> light = EntityManager::Create("Directional Light");
	TransformComponent* lightTransform = EntityManager::Get<TransformComponent>(light);
	lightTransform->translation = {0.0f, 3.0f, 0.0f};
	SetEulerAngles(*lightTransform, {50.0f, -30.0f, 0.0f}); // 回転はクォータニオンなので、角度で決めるときは SetEulerAngles
	EntityManager::RequestAdd<DirectionalLightComponent>(light);
}