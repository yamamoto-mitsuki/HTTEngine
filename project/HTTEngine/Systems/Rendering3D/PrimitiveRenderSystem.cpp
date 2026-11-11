#include "PrimitiveRenderSystem.h"

#include <array>
#include <cctype>
#include <memory>
#include <string>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"

namespace {
constexpr float kUnitHalfSize = 0.5f; // Scale が 1 のときの、球の半径・箱の1辺の半分（直径・1辺が1になるように）
constexpr float kMinUVScale = 0.01f;  // テクスチャ1枚の大きさの下限（0 にすると UV が無限大になるので）

// ===== 球：ワールド行列を 大きさ・回転・位置 に分けて、半径1の球を半分の大きさで描く =====
void DrawSphere(const TransformComponent& transform, const PrimitiveRendererComponent& component, Camera* camera, IBLEnvironment* environment, bool isSceneHidden) {
	Renderer::SphereConfig config;
	config.transform = DecomposeAffineMatrix(transform.worldMatrix);
	config.transform.scale = config.transform.scale * kUnitHalfSize;
	PrimitiveRenderSystem::ApplyMaterial(config, component.material, environment);
	config.camera = camera;
	config.isSceneHidden = isSceneHidden;
	Renderer::DrawSphere(config);
}

// ===== 箱：ワールド行列の1〜3行目が、それぞれ X・Y・Z 軸の向き × 大きさ。向きを軸に、長さの半分を大きさ（中心から面まで）にする =====
void DrawBox(const TransformComponent& transform, const PrimitiveRendererComponent& component, Camera* camera, IBLEnvironment* environment, bool isSceneHidden) {
	const Matrix4x4& m = transform.worldMatrix;
	Renderer::OBBConfig config;
	config.obb.center = GetWorldPosition(transform);
	std::array<float, 3> halfSizes = {}; // X・Y・Z の、中心から面までの長さ
	for (int axis = 0; axis < 3; ++axis) {
		const Vector3 row = {m.m[axis][0], m.m[axis][1], m.m[axis][2]};
		const float length = Length(row);
		if (length <= 0.0f) {
			return; // 大きさが0（向きが決まらない・見えない）
		}
		config.obb.axes[axis] = row / length;
		halfSizes[axis] = length * kUnitHalfSize;
	}
	config.obb.size = {halfSizes[0], halfSizes[1], halfSizes[2]};
	PrimitiveRenderSystem::ApplyMaterial(config, component.material, environment);
	config.camera = camera;
	config.isSceneHidden = isSceneHidden;
	Renderer::DrawOBB(config);
}

// ===== 保存の名前の付いたラベル（"Color" と "color" → "Color###color"。keyPrefix が "post" なら "Color###postColor"）=====
// ### の後ろが保存の名前（ComponentSerializer の KeyOf）。頭に言葉を付けるときは、つなぎ目を大文字にする（キャメルケース）
std::string MakeLabel(std::string_view display, std::string_view keyPrefix, std::string_view key) {
	std::string label = std::string(display) + "###";
	if (keyPrefix.empty()) {
		label += key;
		return label;
	}
	label += keyPrefix;
	label += static_cast<char>(std::toupper(static_cast<unsigned char>(key.front()))); // 頭の1文字だけ大文字に
	label += key.substr(1);
	return label;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribePrimitiveRenderer(ComponentUI& ui, PrimitiveRendererComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	ui.Field("Shape###shape", value.shape, Tip("形（大きさは Transform の Scale）"));
	ui.Separator("Material");
	PrimitiveRenderSystem::DescribeMaterial(ui, value.material, ""); // 色は前と同じ "color" で保存される
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void PrimitiveRenderSystem::Initialize() {
	// Primitive Renderer の置き場所・保存・Inspector を登録する（見せ方は DescribePrimitiveRenderer 1つ）
	EntityManager::RegisterComponent<PrimitiveRendererComponent>();
	ComponentSerializer::Register<PrimitiveRendererComponent>("Primitive Renderer", &DescribePrimitiveRenderer);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<PrimitiveRendererComponent>>("Primitive Renderer", "Rendering3D", &DescribePrimitiveRenderer, "モデルのファイルを使わずに、球・箱を描く（仮の見た目・簡単な足場）。大きさは Transform の Scale"));
}

//=============================================================================
// 描画
//=============================================================================
void PrimitiveRenderSystem::Draw(Camera* camera, IBLEnvironment* environment) {
	EntityManager::ForEach<PrimitiveRendererComponent>([camera, environment](Handle<Entity> entity, const PrimitiveRendererComponent& component) {
		if (!component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		const bool isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		if (component.shape == PrimitiveShape::Sphere) {
			DrawSphere(*transform, component, camera, environment, isSceneHidden);
		} else {
			DrawBox(*transform, component, camera, environment, isSceneHidden);
		}
	});
}

//=============================================================================
// 見た目（PrimitiveMaterial）の見せ方
//=============================================================================
void PrimitiveRenderSystem::DescribeMaterial(ComponentUI& ui, PrimitiveMaterial& material, std::string_view keyPrefix) {
	// ラベルの文字はこの行の終わりまで生きていればよい（ui は受け取ったその場でしか使わない）ので、作った std::string の c_str() を渡す
	const auto label = [keyPrefix](std::string_view display, std::string_view key) { return MakeLabel(display, keyPrefix, key); };

	// ===== 色とテクスチャ =====
	ui.ColorField(label("Color", "color").c_str(), material.color);
	ui.Field(label("Intensity", "intensity").c_str(), material.intensity, AtLeast(0.0f).Tip("明るさの倍率（色はそのまま）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る。Unlit・Additive だと色のまま光る"));
	ui.Field(label("Surface", "surface").c_str(), material.surface, Tip("透け方"));
	ui.AssetField(label("Texture", "texture").c_str(), material.textureHandle, AssetType::Texture, Tip("resources の下の画像。色と掛け合わせる（(none) なら色だけ）"));
	ui.Field(label("UV Scale", "uvScale").c_str(), material.uvScale, AtLeast(kMinUVScale).Tip("テクスチャ1枚の大きさ。Elastic Surface・Polygon Renderer は m、Primitive Renderer は1面を1として（0.5 なら1面に2回くり返す）"));
	// ===== 陰の付け方（どの値を使うかが変わる）=====
	ui.Field(label("Shading", "shadingType").c_str(), material.shadingType, Tip("陰の付け方"));
	ui.ColorField(label("Specular", "specular").c_str(), material.params.specular);
	ui.Field(label("Shininess", "shininess").c_str(), material.params.shininess, AtLeast(0.0f).Tip("ハイライトの鋭さ（Phong・BlinnPhong。大きいほど小さく鋭い）。Specular が黒ならハイライトは出ない"));
	ui.Field(label("Metallic", "metallic").c_str(), material.params.metallic, Range(0.0f, 1.0f).Tip("金属っぽさ（PBR。0=プラスチック・ゴム 1=金属）"));
	ui.Field(label("Roughness", "roughness").c_str(), material.params.roughness, Range(0.0f, 1.0f).Tip("表面の粗さ（PBR。0=鏡のよう 1=ざらざら）"));
	ui.ColorField(label("Emissive", "emissive").c_str(), material.params.emissive); // 自己発光（Unlit 以外。ライトが無くても光る）
	// ===== デカール =====
	ui.Field(label("Receive Decals", "receivesDecals").c_str(), material.receivesDecals, Tip("外すと、デカールの箱の中にあっても、この形には塗られない（Opaque のときだけ効く）"));
}