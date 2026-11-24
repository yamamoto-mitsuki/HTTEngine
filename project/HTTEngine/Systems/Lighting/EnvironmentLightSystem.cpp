#include "EnvironmentLightSystem.h"

#include <algorithm>
#include <memory>

#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
#include "HTTEngine/Scene/Skybox.h"

namespace {
//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeEnvironmentLight(ComponentUI& ui, EnvironmentLightComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと全部 1（空の画像の明るさのまま）"));
	ui.Label("シーンに1つ。空の画像から来る光（PBR の物）と、空の絵の明るさを決める");
	// ===== 物を照らす光 =====
	ui.Separator("Lighting");
	ui.Field("Intensity###intensity", value.intensity, AtLeast(0.0f).Tip("空から来る、ぼんやりした光の強さ。全部の面に当たるので、下げると平行光源の当たる面と影の差が出る（1 で空の画像のまま）"));
	ui.Field("Reflection Intensity###reflectionIntensity", value.reflectionIntensity, AtLeast(0.0f).Tip("空の映り込みの強さ。つるつるした物・金属に写る空の明るさ（1 で空の画像のまま）"));
	// ===== 空の絵 =====
	ui.Separator("Sky");
	ui.Field("Sky Intensity###skyIntensity", value.skyIntensity, AtLeast(0.0f).Tip("空の絵の明るさ（Camera の Background が Skybox のとき）。物を照らす光は変わらない。下げるとブルームで光る所が減る"));
}

// Environment Light Component の値を、シェーダーへ送る形にする（マイナスは 0 にする）
EnvironmentLightData ToEnvironmentLightData(const EnvironmentLightComponent& environment) {
	EnvironmentLightData data;
	data.intensity = (std::max)(environment.intensity, 0.0f);
	data.reflectionIntensity = (std::max)(environment.reflectionIntensity, 0.0f);
	return data;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void EnvironmentLightSystem::Initialize() {
	// Environment Light の置き場所・保存・Inspector を登録する（見せ方は DescribeEnvironmentLight 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<EnvironmentLightComponent>();
	ComponentSerializer::Register<EnvironmentLightComponent>("EnvironmentLight", &DescribeEnvironmentLight);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<EnvironmentLightComponent>>(
	    "Environment Light", "Lighting", &DescribeEnvironmentLight, "環境光（空の画像から来る光）と空の絵の明るさ。シーンに1つ、どの Entity に付けてもよい"));
}

void EnvironmentLightSystem::Release() {} // 持っている物は無い（SceneRenderer は先に消えていることがあるので、ここでは触らない）

//=============================================================================
// 毎フレーム
//=============================================================================
void EnvironmentLightSystem::Update() {
	// 有効な Environment Light を1つ探す（先に見つかった物。無ければ全部 1）
	// ForEach は途中で止められないので、見つけたら印を付けて、後の物は飛ばす
	EnvironmentLightData data;
	float skyIntensity = 1.0f;
	bool isFound = false;
	EntityManager::ForEach<EnvironmentLightComponent>([&data, &skyIntensity, &isFound](Handle<Entity> entity, const EnvironmentLightComponent& environment) {
		if (isFound || !environment.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		data = ToEnvironmentLightData(environment);
		skyIntensity = (std::max)(environment.skyIntensity, 0.0f);
		isFound = true;
	});
	SceneRenderer::SetEnvironmentLight(data); // 見つからなければ、初期値（全部 1）のまま
	// 空の絵の明るさ（Skybox は SceneRenderer が持っている。無ければ 1 に戻す＝付けた物を消したときに元へ戻る）
	if (Skybox* skybox = SceneRenderer::GetSkybox()) {
		skybox->SetIntensity(skyIntensity);
	}
}
