#include "PolylineSystem.h"

#include <cmath>
#include <format>
#include <memory>
#include <algorithm>
#include <unordered_map>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/DrawRequest.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/UI/PolylineMesh.h"
#include "HTTEngine/Systems/UI/UIMaskSystem.h"

namespace {
constexpr float kMinScale = 1.0e-4f; // これより小さい Scale は描かない（0 で割らないように）

/// <summary>
/// Entity 1つ分のメッシュ。作ったときの形を覚えておき、変わった時だけ作り直す
/// </summary>
struct PolylineCache {
	PolylineShape shape;                    // 作ったときの形
	std::shared_ptr<PolylineMeshData> mesh; // 作ったメッシュ（描き終わるまで、RenderQueue のリクエストも同じ物を指す）
	PolylineMeshInfo info;                  // 塗り・線の番号の数
	bool isUsed = false;                    // このフレームの Draw で使ったか（使わなかった物は捨てる）
};

/// <summary>
/// PolylineSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PolylineState {
	std::unordered_map<EntityId, PolylineCache> caches; // Entity の番号 → メッシュ（折れ線が多くても1回で探せる）
	bool isEditing = false;                             // Scene ビューで点を編集しているか
};
std::unique_ptr<PolylineState> state;

//=============================================================================
// Component の値 → メッシュ・シェーダーの値
//=============================================================================
// ===== メッシュを作る元の値（Scale を掛けたピクセル）。hasFill は塗りの部分を作るか（閉じた形のマスクは、塗らなくても作る）=====
// 横だけ伸ばしても角の丸み・太さがゆがまないように、半径・太さには小さい方の倍率を掛ける（Vector Shape と同じ）
PolylineShape MakeShape(const PolylineComponent& polyline, const Vector2& scale, bool hasFill) {
	PolylineShape shape;
	const float uniformScale = (std::min)(scale.x, scale.y);
	shape.count = polyline.GetCount();
	for (int i = 0; i < shape.count; ++i) {
		shape.points[i] = {polyline.points[i].x * scale.x, polyline.points[i].y * scale.y};
		shape.radii[i] = (std::max)(polyline.radii[i], 0.0f) * uniformScale;
	}
	shape.isClosed = polyline.closed;
	shape.hasFill = hasFill;
	shape.halfWidth = polyline.HasStroke() ? polyline.strokeWidth * uniformScale * 0.5f : 0.0f;
	shape.cap = polyline.cap;
	return shape;
}

// ===== 頂点の位置（Scale を掛けたピクセル）→ 画面の正規化座標 =====
// SpriteRenderSystem::CalcScreenPoint と同じ写し方（回転・移動・アンカー・ピクセル → 正規化座標）を、行列1つにまとめる
// メッシュには Scale を掛けてあるので、行列の x 軸・y 軸は Scale で割って、回転と大きさの向きだけにする
Matrix4x4 MakeToScreen(const Matrix4x4& world, ScreenAnchor anchor, const Vector2& scale) {
	const Vector2 toNdc = GameScreen::PixelToNdc({1.0f, 1.0f}); // 1 ピクセルの量が、画面の位置でいくつか（横・縦）
	const Vector2 origin = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 0.0f});
	Matrix4x4 m = MakeIdentity4x4();
	m.m[0][0] = world.m[0][0] / scale.x * toNdc.x;
	m.m[0][1] = world.m[0][1] / scale.x * toNdc.y;
	m.m[1][0] = world.m[1][0] / scale.y * toNdc.x;
	m.m[1][1] = world.m[1][1] / scale.y * toNdc.y;
	m.m[3][0] = origin.x;
	m.m[3][1] = origin.y;
	return m;
}

// ===== Entity のメッシュを、今の形にそろえる（形が前と同じなら何もしない）=====
PolylineCache& UpdateCache(Handle<Entity> entity, const PolylineShape& shape) {
	const Entity* owner = EntityManager::Get(entity);
	// try_emplace：その番号がまだ無いときだけ、空の PolylineCache を作って入れる（あれば、あった物を返す）
	auto [it, isAdded] = state->caches.try_emplace((owner != nullptr) ? owner->id : 0);
	PolylineCache& cache = it->second;
	cache.isUsed = true;
	if (!isAdded && cache.mesh != nullptr && cache.shape == shape) {
		return cache; // 形が同じ：作り直さない
	}
	// まだ RenderQueue が前のメッシュを持っていたら（use_count が 2 以上）、新しく作る。持っていなければ、配列を使い回して作り直す
	if (cache.mesh == nullptr || cache.mesh.use_count() > 1) {
		cache.mesh = std::make_shared<PolylineMeshData>();
	}
	cache.shape = shape;
	cache.info = PolylineMeshBuilder::Build(shape, *cache.mesh);
	return cache;
}

//=============================================================================
// 描く
//=============================================================================
// ===== 描く手順ごとの、三角形の番号の範囲（番号は 塗り → 線 の順に並んでいる）=====
// 形だけの手順（マスクの形）は、閉じた形なら塗りの部分、開いた線なら線の部分
void SelectRange(const PolylineComponent& polyline, const PolylineMeshInfo& info, UIDrawPart part, uint32_t& start, uint32_t& count) {
	const uint32_t total = info.fillIndexCount + info.strokeIndexCount;
	switch (part) {
	case UIDrawPart::All:
		start = polyline.HasFill() ? 0 : info.fillIndexCount; // 塗らない形は、マスクのために作った塗りの部分を飛ばす
		count = total - start;
		break;
	case UIDrawPart::Fill:
		start = 0;
		count = info.fillIndexCount;
		break;
	case UIDrawPart::Stroke:
		start = info.fillIndexCount;
		count = info.strokeIndexCount;
		break;
	case UIDrawPart::MaskRegion:
		start = polyline.closed ? 0 : info.fillIndexCount;
		count = polyline.closed ? info.fillIndexCount : info.strokeIndexCount;
		break;
	}
}

void DrawPolyline(Handle<Entity> entity, const PolylineComponent& polyline) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
	const Vector2 scale = SpriteRenderSystem::CalcScreenScale(transform->worldMatrix);
	if (groupAlpha <= 0.0f || scale.x < kMinScale || scale.y < kMinScale) {
		return;
	}
	// --- 描く手順（ふつうは1回で塗りと線。マスクの図形なら、塗り → 形を書く → 子 → 形を消す → 線）---
	const UIShapeSteps plan = UIMaskSystem::PlanShapeSteps(entity, polyline.order, UIDrawKind::Polyline, polyline.HasFill(), polyline.HasStroke());
	const bool isMask = plan.count > 1; // マスクの図形だけ、手順が2回以上ある
	// 閉じた形のマスクは、塗らない形（Stroke）でも、切り抜く形として塗りの部分を作る
	const PolylineShape shape = MakeShape(polyline, scale, polyline.HasFill() || (isMask && polyline.closed));
	const PolylineCache& cache = UpdateCache(entity, shape);

	Renderer::PolylineConfig config;
	config.mesh = cache.mesh;
	config.material.toScreen = MakeToScreen(transform->worldMatrix, polyline.anchor, scale);
	config.material.fillColor = ClampColor(MultiplyAlpha(polyline.color, groupAlpha));
	config.material.strokeColor = ClampColor(MultiplyAlpha(polyline.strokeColor, groupAlpha));
	config.material.halfWidth = shape.halfWidth;
	const bool isHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	for (uint32_t i = 0; i < plan.count; ++i) {
		const UIDrawStep& step = plan.steps[i];
		SelectRange(polyline, cache.info, step.part, config.indexStart, config.indexCount);
		if (config.indexCount == 0) {
			continue;
		}
		step.ApplyTo(config);
		config.material.drawPart = static_cast<uint32_t>(step.part);
		// 形だけの手順は、色を出さない（Normal なら透明で、背景を変えない）。目で隠していても、子を切り抜くために描く
		config.blendMode = step.isGraphic ? polyline.blend : BlendMode::Normal;
		config.material.blendMode = static_cast<uint32_t>(config.blendMode);
		config.isSceneHidden = isHidden && step.isGraphic;
		Renderer::DrawPolyline(config);
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Polyline の見せ方。Inspector とシーンファイルの両方がこの並びを使う
// 点は数を先に読むので、シーンファイルから読むときも、その数だけ点を読む（Polygon Shape と同じ）
void DescribePolyline(ComponentUI& ui, PolylineComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Anchor###anchor", value.anchor, Tip("画面のどこから位置を数えるか（位置は Transform の Translation の x・y。ピクセルで、上が＋）"));
	ui.Field("Order###order", value.order, Tip("重ねる順（大きいほど手前。Image・Vector Shape・Text の Order とも比べる）"));
	ui.Field("Blend###blend", value.blend, Tip("重ね方（Normal：透明な所が透ける / Add：足して光る）。None は縁のなめらかさが無くなる"));

	// ===== 点（Edit Points は Inspector だけ。保存・読み込みのときは Button がいつも false）=====
	ui.Separator("Points");
	if (ui.Button(state->isEditing ? "Finish Editing###editPoints" : "Edit Points###editPoints")) {
		state->isEditing = !state->isEditing;
	}
	ui.Label("Scene ビューで ■ をドラッグ・＋ で足す・Delete で消す");
	ui.Field("Closed###closed", value.closed, Tip("最後の点から最初の点へもつなぐ（閉じた形。塗れるのは閉じた形だけ）"));
	const int before = value.GetCount();
	ui.Field("Point Count###pointCount", value.pointCount, Range(static_cast<float>(kMinPolylinePoints), static_cast<float>(kMaxPolylinePoints)).Tip("点の数。Scene ビューで足す・消すこともできる"));
	value.pointCount = std::clamp(value.pointCount, kMinPolylinePoints, kMaxPolylinePoints);
	// 増やした点は、閉じた形なら最後と最初の真ん中、開いた線なら最後の辺を伸ばした所に置く（前に使っていた所や原点に飛んでいかないように）
	for (int i = (std::max)(before, 2); i < value.pointCount; ++i) {
		const Vector2& last = value.points[i - 1];
		value.points[i] = value.closed ? (last + value.points[0]) * 0.5f : last + (last - value.points[i - 2]);
		value.radii[i] = 0.0f;
	}
	for (int i = 0; i < value.pointCount; ++i) {
		ui.Field(std::format("Point {}###point{}", i, i).c_str(), value.points[i], Drag(1.0f)); // 保存の名前は point0, point1, …（Timeline の値のトラックで動かせる）
		// 開いた線の両端は角ではないので、半径は使わない（灰色にするだけで、保存はする）
		ui.BeginDisabled(!value.closed && (i == 0 || i == value.pointCount - 1));
		ui.Field(std::format("Radius {}###radius{}", i, i).c_str(), value.radii[i], AtLeast(0.0f).Drag(0.5f).Tip("角の半径（ピクセル）。となりの辺に収まらなければ、描くときに縮める"));
		ui.EndDisabled();
	}

	ui.Separator("Paint");
	ui.Field("Style###style", value.style, Tip("Fill：塗りだけ / Fill And Stroke：塗りと線 / Stroke：線だけ。塗れるのは閉じた形だけ"));
	ui.BeginDisabled(!value.HasFill());
	ui.ColorField("Color###color", value.color);
	ui.EndDisabled();

	ui.Separator("Stroke");
	ui.BeginDisabled(value.style == VectorShapeStyle::Fill);
	ui.Field("Stroke Width###strokeWidth", value.strokeWidth, AtLeast(0.0f).Drag(0.25f).Tip("線の太さ（ピクセル）。点の真ん中に、両側へ半分ずつ"));
	ui.ColorField("Stroke Color###strokeColor", value.strokeColor);
	ui.EndDisabled();
	ui.BeginDisabled(value.style == VectorShapeStyle::Fill || value.closed);
	ui.Field("Cap###cap", value.cap, Tip("開いた線の端の形（Butt：端でまっすぐ切る / Round：半円 / Square：太さの半分伸ばして切る）"));
	ui.EndDisabled();
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void PolylineSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<PolylineState>();

	// Polyline の置き場所・保存・Inspector を登録する。見せ方は DescribePolyline 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<PolylineComponent>();
	ComponentSerializer::Register<PolylineComponent>("Polyline", &DescribePolyline);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<PolylineComponent>>("Polyline", "UI", &DescribePolyline));
}

void PolylineSystem::Release() { state.reset(); }

//=============================================================================
// 描く（停止中も）
//=============================================================================
void PolylineSystem::Draw() {
	HTT_ASSERT_MSG(state != nullptr, "PolylineSystem::Initialize()を先に呼んでください");
	for (auto& [id, cache] : state->caches) {
		cache.isUsed = false;
	}
	// 重ねる順は RenderQueue が Order で並べるので、ここは見つけた順に積む
	EntityManager::ForEach<PolylineComponent>([](Handle<Entity> entity, const PolylineComponent& polyline) {
		if (polyline.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			DrawPolyline(entity, polyline);
		}
	});
	// 描かなかった物（消えた・外した・無効にした）のメッシュを捨てる
	// std::erase_if：条件に合う物を全部消す（map でも使える。C++20）
	std::erase_if(state->caches, [](const auto& pair) { return !pair.second.isUsed; });
}

//=============================================================================
// エディタから使う
//=============================================================================
bool PolylineSystem::IsEditing() { return state != nullptr && state->isEditing; }

void PolylineSystem::SetEditing(bool isEditing) {
	HTT_ASSERT_MSG(state != nullptr, "PolylineSystem::Initialize()を先に呼んでください");
	state->isEditing = isEditing;
}

bool PolylineSystem::CalcLocalBounds(const PolylineComponent& polyline, Vector2& min, Vector2& max) {
	const int count = polyline.GetCount();
	if (count == 0) {
		return false;
	}
	min = max = polyline.points[0];
	for (int i = 1; i < count; ++i) {
		min = {(std::min)(min.x, polyline.points[i].x), (std::min)(min.y, polyline.points[i].y)};
		max = {(std::max)(max.x, polyline.points[i].x), (std::max)(max.y, polyline.points[i].y)};
	}
	// 線は点の外へ太さの半分はみ出す
	const float margin = polyline.HasStroke() ? polyline.strokeWidth * 0.5f : 0.0f;
	min -= Vector2{margin, margin};
	max += Vector2{margin, margin};
	return true;
}
