#include "HitboxTelegraphSystem.h"

#include <algorithm>
#include <memory>
#include <span>

#include "MyEngine/Components/Combat/HitboxComponent.h"
#include "MyEngine/Components/Combat/HitboxTelegraphComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Systems/Combat/HitSystem.h"

namespace {
constexpr const char* kDefaultMaterialPath = "MyEngine/Resources/Materials/HitboxTelegraph.material"; // マテリアルを選んでいないときに使う（エンジンの Resources）
constexpr float kEdgeMargin = 0.1f;      // 箱を形より横に広げる長さ（m。縁をなめらかにする 1 ピクセルの分が、箱で切れないように）
constexpr float kBelowBaseDepth = 0.05f; // 箱を形の底より下へ伸ばす長さ（m。底と同じ高さの床が、深度の誤差で箱の外にならないように）

/// <summary>
/// HitboxTelegraphSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct HitboxTelegraphState {
	uint32_t defaultMaterial = 0;         // 既定のマテリアルの番号（初めて使うときに読む）
	bool isDefaultMaterialLoaded = false; // 読みに行ったか（無いファイルを毎フレーム読みに行かないように）
};
std::unique_ptr<HitboxTelegraphState> state;

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeHitboxTelegraph(ComponentUI& ui, HitboxTelegraphComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	ui.AssetField("Material###material", value.material, AssetType::Material,
	    Tip("予告のマテリアル（.material。Shader は Decal/HitboxTelegraph.PS.shader のように、gDrawData で予告の値を読む物）。(none) なら赤い塗り（エンジンの既定）"));
	ui.Field("Show While Active###showWhileActive", value.showWhileActive, Tip("入れると、Active（当たっている）間も出す。外すと Warning の間だけ"));
	ui.Field("Order###order", value.order, Tip("ほかのデカールと重なったときの順（大きい方が上に塗られる）"));
}

//=============================================================================
// 描く値
//=============================================================================
// ===== 予告を出す段階か =====
bool IsShown(const HitboxComponent& hitbox, const HitboxTelegraphComponent& telegraph) {
	switch (hitbox.phase) {
	case HitboxPhase::Off:
		return false;
	case HitboxPhase::Warning:
		return true;
	case HitboxPhase::Active:
		return telegraph.showWhileActive;
	}
	return false;
}

// ===== Warning の進み具合（0→1。Active は 1。Warning の長さが 0＝ずっと Warning なら 0 のまま）=====
float CalcProgress(const HitboxComponent& hitbox) {
	if (hitbox.phase == HitboxPhase::Active) {
		return 1.0f;
	}
	return (hitbox.warningSeconds > 0.0f) ? std::clamp(hitbox.phaseTime / hitbox.warningSeconds, 0.0f, 1.0f) : 0.0f;
}

// ===== Shader に送る値 =====
HitboxTelegraphData MakeData(const WorldHitShape& shape, const HitboxComponent& hitbox, float progress) {
	HitboxTelegraphData data;
	data.origin = shape.origin;
	data.radius = shape.radius;
	data.right = shape.right;
	data.innerRadius = shape.innerRadius;
	data.up = shape.up;
	data.halfAngle = shape.halfAngle;
	data.forward = shape.forward;
	data.length = shape.length;
	data.width = shape.width;
	data.height = shape.height;
	data.shapeType = static_cast<uint32_t>(shape.type);
	data.phase = static_cast<uint32_t>(hitbox.phase);
	data.progress = progress;
	data.remaining = hitbox.GetWarningRemaining();
	data.phaseTime = hitbox.phaseTime;
	return data;
}

// ===== 形を囲む箱（デカールの箱。大きさ 1 で 1辺 1 の箱を、形のローカルの軸で置く）=====
// 横（XZ）は形を囲む四角、縦（Y）は底の少し下から形の高さまで（高さの外は当たらないので塗らない）
bool MakeBox(const WorldHitShape& shape, Matrix4x4& outWorld) {
	// --- 形のローカルでの箱の範囲 ---
	Vector3 boxMin = {-shape.radius, -kBelowBaseDepth, -shape.radius}; // 円・扇：半径の四角
	Vector3 boxMax = {shape.radius, shape.height, shape.radius};
	if (shape.type == HitShapeType::Line) {
		boxMin.x = -shape.width * 0.5f; // 線：幅 × 長さ（中心から前 +Z へ）
		boxMax.x = shape.width * 0.5f;
		boxMin.z = 0.0f;
		boxMax.z = shape.length;
	}
	boxMin.x -= kEdgeMargin;
	boxMin.z -= kEdgeMargin;
	boxMax.x += kEdgeMargin;
	boxMax.z += kEdgeMargin;
	const Vector3 size = boxMax - boxMin;
	if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) {
		return false; // 大きさ 0（見えない）
	}
	// --- ワールド行列（1〜3行目が軸 × 大きさ、4行目が箱の中心）---
	const Vector3 center = (boxMin + boxMax) * 0.5f;
	const Vector3 axisX = shape.right * size.x;
	const Vector3 axisY = shape.up * size.y;
	const Vector3 axisZ = shape.forward * size.z;
	const Vector3 position = shape.ToWorld(center);
	outWorld = MakeIdentity4x4();
	outWorld.m[0][0] = axisX.x;
	outWorld.m[0][1] = axisX.y;
	outWorld.m[0][2] = axisX.z;
	outWorld.m[1][0] = axisY.x;
	outWorld.m[1][1] = axisY.y;
	outWorld.m[1][2] = axisY.z;
	outWorld.m[2][0] = axisZ.x;
	outWorld.m[2][1] = axisZ.y;
	outWorld.m[2][2] = axisZ.z;
	outWorld.m[3][0] = position.x;
	outWorld.m[3][1] = position.y;
	outWorld.m[3][2] = position.z;
	return true;
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void HitboxTelegraphSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<HitboxTelegraphState>();
	// Hitbox Telegraph の置き場所・保存・Inspector を登録する（見せ方は DescribeHitboxTelegraph 1つ）
	EntityManager::RegisterComponent<HitboxTelegraphComponent>();
	ComponentSerializer::Register<HitboxTelegraphComponent>("Hitbox Telegraph", &DescribeHitboxTelegraph);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HitboxTelegraphComponent>>("Hitbox Telegraph", "Combat", &DescribeHitboxTelegraph));
}

void HitboxTelegraphSystem::Release() { state.reset(); }


//=============================================================================
// 既定のマテリアル
//=============================================================================
uint32_t HitboxTelegraphSystem::GetDefaultMaterial() {
	MY_ASSERT_MSG(state != nullptr, "HitboxTelegraphSystem::Initialize()を先に呼んでください");
	if (!state->isDefaultMaterialLoaded) {
		state->isDefaultMaterialLoaded = true;
		state->defaultMaterial = LoadAsset(AssetType::Material, kDefaultMaterialPath); // 無ければ警告が出て 0（描かない）
	}
	return state->defaultMaterial;
}


//=============================================================================
// 描画
//=============================================================================
void HitboxTelegraphSystem::Draw() {
	EntityManager::ForEach<HitboxTelegraphComponent>([](Handle<Entity> entity, const HitboxTelegraphComponent& telegraph) {
		if (!telegraph.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		// --- 同じ Entity の Hitbox が、予告を出す段階で、床に置く形か ---
		const HitboxComponent* hitbox = EntityManager::Get<HitboxComponent>(entity);
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (hitbox == nullptr || transform == nullptr || !IsShown(*hitbox, telegraph) || !IsFloorShape(hitbox->shape.type)) {
			return;
		}
		// --- 当たりと同じ形（Entity の大きさを掛けた後）で、箱と Shader に送る値を作る ---
		const WorldHitShape shape = HitSystem::CalcWorldShape(*hitbox, *transform);
		Renderer::DecalConfig config;
		if (!MakeBox(shape, config.worldMatrix)) {
			return;
		}
		const float progress = CalcProgress(*hitbox);
		const HitboxTelegraphData data = MakeData(shape, *hitbox, progress);
		config.material = (telegraph.material != 0) ? telegraph.material : GetDefaultMaterial();
		config.progress = progress; // マテリアルの Curve の横軸も、Warning の進み具合
		config.order = telegraph.order;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		// std::as_bytes：構造体1つを、中身を変えずにバイトの並びとして見る（std::span で「data 1個分」を渡す）
		Renderer::DrawDecal(config, std::as_bytes(std::span(&data, 1)));
	});
}
