#include "DepthOfFieldSystem.h"

#include <memory>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/PostEffect/DepthOfFieldPass.h"
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeDepthOfField(ComponentUI& ui, DepthOfFieldComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すとぼかさない"));
	ui.Label("シーンに1つ。ピントの合った所だけはっきり見え、手前と奥がぼける（Scene ビューは View → Scene View → Depth of Field で切れる）");
	// ===== ピント =====
	ui.Separator("Focus");
	ui.Field("Focus###focus", value.focus, Tip("ピントの合わせ方"));
	// --- 選んだ合わせ方の値だけを見せる（保存も、使っている方だけが書かれる。Camera Follow の Mode と同じ）---
	switch (value.focus) {
	case DepthOfFieldFocus::FollowTarget:
		break; // FollowTarget だけの値は無い（Target は Camera Follow・Camera Orbit の物を使う）
	case DepthOfFieldFocus::Distance:
		ui.Field("Focus Distance###focusDistance", value.focusDistance, AtLeast(0.0f).Tip("カメラからピントの合う所までの奥行き（m）"));
		break;
	}
	ui.Field("Focus Range###focusRange", value.focusRange, AtLeast(0.0f).Tip("ピントの前後この奥行き（m）までは、ぼけない。遊ぶ面の手前・奥の飾り（草・岩）もはっきり見せたいときは広げる"));
	// ===== ぼけ方 =====
	ui.Separator("Blur");
	ui.Field(
	    "Far Transition###farTransition", value.farTransition, AtLeast(0.01f).Tip("ピントの範囲の奥から、一番ぼけるまでの奥行き（m）。小さいほど、すぐ奥がぼける（奥の層ほど、ぼけが大きくなる）"));
	ui.Field("Near Transition###nearTransition", value.nearTransition, AtLeast(0.0f).Tip("ピントの範囲の手前から、一番ぼけるまでの奥行き（m）。0 なら手前はぼかさない"));
	ui.Field("Max Blur###maxBlur", value.maxBlur, Range(0.0f, DepthOfFieldPass::kMaxBlurLimit).Tip("一番ぼけた所の、ぼけの半径（ピクセル。高さ 720 の画面で）。大きいほど重い"));
}

// ===== Depth Of Field Component の値を、ポストエフェクトへ渡す形にする =====
DepthOfFieldSettings ToSettings(const DepthOfFieldComponent& component) {
	DepthOfFieldSettings settings;
	settings.enabled = true;
	settings.focusDistance = component.focusDistance;
	settings.focusRange = component.focusRange;
	settings.farTransition = component.farTransition;
	settings.nearTransition = component.nearTransition;
	settings.maxBlur = component.maxBlur;
	// --- ピントを合わせる点（奥行きはビューごとにカメラから測るので、ここでは点だけ渡す）---
	switch (component.focus) {
	case DepthOfFieldFocus::FollowTarget:
		if (const TransformComponent* target = CameraSystem::GetFollowTargetTransform()) {
			settings.hasFocusPoint = true;
			settings.focusPoint = GetWorldPosition(*target);
		}
		break; // Target が無ければ focusDistance のまま
	case DepthOfFieldFocus::Distance:
		break;
	}
	return settings;
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void DepthOfFieldSystem::Initialize() {
	// Depth Of Field の置き場所・保存・Inspector を登録する（見せ方は DescribeDepthOfField 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<DepthOfFieldComponent>();
	ComponentSerializer::Register<DepthOfFieldComponent>("Depth Of Field", &DescribeDepthOfField);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<DepthOfFieldComponent>>("Depth Of Field", "Rendering3D", &DescribeDepthOfField, "被写界深度。ピントの合った所だけはっきり見せ、手前と奥をぼかす。シーンに1つ、どの Entity に付けてもよい"));
}

void DepthOfFieldSystem::Release() {} // 持っている物は無い（SceneRenderer は先に消えていることがあるので、ここでは触らない）

//=============================================================================
// 毎フレーム
//=============================================================================
void DepthOfFieldSystem::Update() {
	// 有効な Depth Of Field を1つ探す（先に見つかった物。無ければぼかさない）
	// ForEach は途中で止められないので、見つけたら印を付けて、後の物は飛ばす（FogSystem と同じ）
	DepthOfFieldSettings settings;
	bool isFound = false;
	EntityManager::ForEach<DepthOfFieldComponent>([&settings, &isFound](Handle<Entity> entity, const DepthOfFieldComponent& component) {
		if (isFound || !component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		settings = ToSettings(component);
		isFound = true;
	});
	SceneRenderer::SetDepthOfField(settings); // 見つからなければ、初期値（ぼかさない）のまま
}