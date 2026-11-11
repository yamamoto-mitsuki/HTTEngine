#include "FogSystem.h"

#include <algorithm>
#include <memory>

#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
#include "HTTEngine/Math/Color.h"

namespace {
//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeFog(ComponentUI& ui, FogComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと霧なし"));
	ui.Label("シーンに1つ。3D の物の色に、距離と高さで霧を混ぜる（Scene ビューは View → Scene View → Fog で切れる）");
	// ===== 距離の霧 =====
	ui.Separator("Distance");
	ui.ColorField("Color###color", value.color);
	ui.Field("Start Distance###startDistance", value.startDistance, AtLeast(0.0f).Tip("カメラからこの距離（m）までは霧が無い。遊ぶ面より少し奥にすると、遊ぶ面ははっきり見える"));
	ui.Field("Density###density", value.density, AtLeast(0.0f).Tip("濃さ（1m あたり）。大きいほど近くから濃くなる。0.02 なら、始まりから 35m 奥で半分くらい"));
	ui.Field("Max Opacity###maxOpacity", value.maxOpacity, Range(0.0f, 1.0f).Tip("一番濃い所（0〜1）。1 にすると、一番奥の空の絵まで霧の色だけになる"));
	// ===== 高さの霧 =====
	ui.Separator("Height");
	ui.ColorField("Height Color###heightColor", value.heightColor);
	ui.Field("Height Base###heightBase", value.heightBase, Tip("一番濃くなる高さ（ワールドの Y）。これより下はずっと一番濃い。穴の底より少し上にする"));
	ui.Field("Height Falloff###heightFalloff", value.heightFalloff, AtLeast(0.0f).Tip("上へ薄くなる速さ（1m あたり）。大きいほど、すぐ薄くなる（0.35 なら、3m 上で約 3 分の 1）"));
	ui.Field("Height Max Opacity###heightMaxOpacity", value.heightMaxOpacity, Range(0.0f, 1.0f).Tip("一番濃い所（0〜1）。0 なら高さの霧なし"));
}

// Fog Component の値を、シェーダーへ送る形にする
FogData ToFogData(const FogComponent& fog) {
	FogData data;
	data.color = SrgbToLinear(fog.color); // 色は Inspector で選んだ sRGB → シェーダーで混ぜるリニアへ
	data.startDistance = (std::max)(fog.startDistance, 0.0f);
	data.density = (std::max)(fog.density, 0.0f);
	data.maxOpacity = std::clamp(fog.maxOpacity, 0.0f, 1.0f);
	data.heightBase = fog.heightBase;
	data.heightFalloff = (std::max)(fog.heightFalloff, 0.0f);
	data.heightColor = SrgbToLinear(fog.heightColor);
	data.heightMaxOpacity = std::clamp(fog.heightMaxOpacity, 0.0f, 1.0f);
	return data;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void FogSystem::Initialize() {
	// Fog の置き場所・保存・Inspector を登録する（見せ方は DescribeFog 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<FogComponent>();
	ComponentSerializer::Register<FogComponent>("Fog", &DescribeFog);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<FogComponent>>("Fog", "Rendering3D", &DescribeFog, "霧（遠いほど・低いほど色が混ざる）。シーンに1つ、どの Entity に付けてもよい"));
}

void FogSystem::Release() {} // 持っている物は無い（SceneRenderer は先に消えていることがあるので、ここでは触らない）

//=============================================================================
// 毎フレーム
//=============================================================================
void FogSystem::Update() {
	// 有効な Fog を1つ探す（先に見つかった物。無ければ霧なし）
	// ForEach は途中で止められないので、見つけたら印を付けて、後の物は飛ばす
	FogData data;
	bool isFound = false;
	EntityManager::ForEach<FogComponent>([&data, &isFound](Handle<Entity> entity, const FogComponent& fog) {
		if (isFound || !fog.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		data = ToFogData(fog);
		isFound = true;
	});
	SceneRenderer::SetFog(data); // 見つからなければ、初期値（霧なし）のまま
}