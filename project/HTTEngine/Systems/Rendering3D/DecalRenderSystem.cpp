#include "DecalRenderSystem.h"

#include <array>
#include <memory>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/Rendering3D/DecalRendererComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"

namespace {
constexpr const char* kDefaultMaterialPath = "HTTEngine/Resources/Materials/Decal.material"; // マテリアルを選んでいないときに使う（エンジンの Resources。白い画像を貼る）
constexpr Vector4 kGizmoColor = {0.4f, 0.8f, 1.0f, 0.8f}; // 箱のギズモの色（水色）
constexpr float kNoFadeStart = 10000.0f;                   // 距離でフェードさせないための値（Collider のギズモと同じ）
constexpr float kNoFadeEnd = 20000.0f;
constexpr float kUnitHalfSize = 0.5f;                      // Scale が 1 のときの、箱の中心から面までの長さ

/// <summary>
/// DecalRenderSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct DecalRenderState {
	uint32_t defaultMaterial = 0;         // 既定のマテリアルの番号（初めて使うときに読む）
	bool isDefaultMaterialLoaded = false; // 読みに行ったか（無いファイルを毎フレーム読みに行かないように）
	Renderer::LineListConfig gizmoLines;  // 箱のギズモ（毎フレーム作り直す。入れ物は使い回す）
};
std::unique_ptr<DecalRenderState> state;

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeDecalRenderer(ComponentUI& ui, DecalRendererComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	ui.AssetField("Material###material", value.material, AssetType::Material,
	    Tip("塗るマテリアル（.material。Shader は #PROGRAM が category: Decal の物。Project の Create → Material で作る）。(none) なら白い画像を貼る"));
	ui.Field("Progress###progress", value.progress, Range(0.0f, 1.0f).Tip("マテリアルの Curve の横軸（0〜1。Script・Timeline から動かす）"));
	ui.Field("Order###order", value.order, Tip("重なったときの順（大きい方が上に塗られる）"));
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void DecalRenderSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<DecalRenderState>();
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;
	state->gizmoLines.layer = DrawLayer::EditorGizmo; // Scene ビューだけ（ゲームの絵に入れない）

	// Decal Renderer の置き場所・保存・Inspector を登録する（見せ方は DescribeDecalRenderer 1つ）
	EntityManager::RegisterComponent<DecalRendererComponent>();
	ComponentSerializer::Register<DecalRendererComponent>("Decal Renderer", &DescribeDecalRenderer);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<DecalRendererComponent>>("Decal Renderer", "Rendering3D", &DescribeDecalRenderer));
}

void DecalRenderSystem::Release() { state.reset(); }


//=============================================================================
// 既定のマテリアル
//=============================================================================
uint32_t DecalRenderSystem::GetDefaultMaterial() {
	HTT_ASSERT_MSG(state != nullptr, "DecalRenderSystem::Initialize()を先に呼んでください");
	if (!state->isDefaultMaterialLoaded) {
		state->isDefaultMaterialLoaded = true;
		state->defaultMaterial = LoadAsset(AssetType::Material, kDefaultMaterialPath); // 無ければ警告が出て 0（描かない）
	}
	return state->defaultMaterial;
}


//=============================================================================
// 描画
//=============================================================================
void DecalRenderSystem::Draw() {
	EntityManager::ForEach<DecalRendererComponent>([](Handle<Entity> entity, const DecalRendererComponent& decal) {
		if (!decal.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		Renderer::DecalConfig config;
		config.material = (decal.material != 0) ? decal.material : GetDefaultMaterial();
		config.progress = decal.progress;
		config.worldMatrix = transform->worldMatrix; // 箱は大きさ 1 で 1辺 1 なので、ワールド行列がそのまま箱になる
		config.order = decal.order;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		Renderer::DrawDecal(config);
	});
}


//=============================================================================
// ギズモ（箱の 12 本の辺）
//=============================================================================
void DecalRenderSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "DecalRenderSystem::Initialize()を先に呼んでください");
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	EntityManager::ForEach<DecalRendererComponent>([&gizmo](Handle<Entity> entity, const DecalRendererComponent& decal) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (!decal.enabled || transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		// 角 i の x・y・z は、i のビット 1・2・4 が立っていれば +0.5、無ければ -0.5
		std::array<Vector3, 8> corners{};
		for (size_t i = 0; i < corners.size(); ++i) {
			const Vector3 local = {(i & 1) ? kUnitHalfSize : -kUnitHalfSize, (i & 2) ? kUnitHalfSize : -kUnitHalfSize, (i & 4) ? kUnitHalfSize : -kUnitHalfSize};
			corners[i] = TransformPoint(local, transform->worldMatrix);
		}
		// 辺は、1つのビットだけが違う2つの角を結ぶ（角ごとに、立っていないビットを立てた角へ）
		for (size_t i = 0; i < corners.size(); ++i) {
			for (size_t bit : {1u, 2u, 4u}) {
				if ((i & bit) == 0) {
					gizmo.lines.push_back({corners[i], corners[i | bit], kGizmoColor});
				}
			}
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}
