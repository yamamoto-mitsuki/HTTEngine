#include "PolygonShapeSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <vector>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Rendering3D/PrimitiveRenderSystem.h"

namespace {
constexpr float kMinPointCount = 3.0f; // 点の数の下限（三角形）

/// <summary>
/// 形から作ったメッシュ（Entity 1つ分）。作ったときの形・行列を覚えておき、変わった時だけ作り直す
/// </summary>
struct MeshCache {
	Handle<Entity> entity;                   // 持ち主
	PolygonShapeComponent shape;             // 作ったときの形
	Matrix4x4 worldMatrix;                   // 作ったときのワールド行列
	bool worldSpaceUV = true;                // 作ったときの UV の作り方
	bool isValid = false;                    // 三角形に分けられたか（辺が交差しているなどで分けられなければ、描かない）
	bool isUsed = false;                     // このフレームの Draw で使ったか（使わなかった物は捨てる）
	std::vector<Vertex3dData> frontVertices; // 前の面
	std::vector<uint32_t> frontIndices;
	std::vector<Vertex3dData> sideVertices; // 横の面（少し暗く描くので、前の面と分ける）
	std::vector<uint32_t> sideIndices;
};

/// <summary>
/// PolygonShapeSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PolygonShapeState {
	std::vector<MeshCache> caches; // 描いている Entity のメッシュ（足場の数しか無いので、順に探せば足りる）
	bool isEditing = false;        // Scene ビューで形を編集しているか
};
std::unique_ptr<PolygonShapeState> state;

// 2つの値がバイト単位で同じか（Component・行列は memcpy で写せる型なので、中身をそのまま比べられる）
template<class T> bool IsSameBytes(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }

// ワールド行列の Z 軸の伸び（3行目の長さ）。奥行きに掛ける
float GetScaleZ(const Matrix4x4& m) { return Length(Vector3{m.m[2][0], m.m[2][1], m.m[2][2]}); }

//=============================================================================
// メッシュを作る
//=============================================================================
//
//   横から見た形（前の面）         斜めから見た形
//     3───────2                      ┌───────┐  ← 横の面（辺ごとに四角1枚）
//     │       │                     ┌┼──────┐│
//     0───────1                     │└──────┼┘
//   耳切り法で三角形に分ける        └───────┘  ← 前の面（後ろの面は見えないので作らない）
//
void BuildMesh(MeshCache& cache) {
	cache.frontVertices.clear();
	cache.frontIndices.clear();
	cache.sideVertices.clear();
	cache.sideIndices.clear();
	const std::span<const Vector2> points = cache.shape.GetPoints();
	// --- 前の面を三角形に分ける（形がおかしければ描かない）---
	cache.isValid = Polygon2D::IsSimple(points) && Polygon2D::Triangulate(points, cache.frontIndices);
	if (!cache.isValid) {
		cache.frontIndices.clear();
		return;
	}
	const size_t count = points.size();
	const float halfDepth = std::abs(cache.shape.depth) * 0.5f;

	// --- 前と後ろの外周（ワールド座標）---
	std::vector<Vector3> front(count);
	std::vector<Vector3> back(count);
	std::vector<Vector2> frontXY(count); // 前の外周の X・Y（横の面の外向きを決める）
	for (size_t i = 0; i < count; ++i) {
		front[i] = TransformPoint({points[i].x, points[i].y, -halfDepth}, cache.worldMatrix);
		back[i] = TransformPoint({points[i].x, points[i].y, halfDepth}, cache.worldMatrix);
		frontXY[i] = {front[i].x, front[i].y};
	}

	// ===== 前の面（法線はカメラの方 = -Z。UV はワールドの X・Y を m 単位で。V は下向きなので Y を逆にする）=====
	Vector3 frontNormal = Normalize(TransformNormal({0.0f, 0.0f, -1.0f}, cache.worldMatrix));
	if (LengthSq(frontNormal) == 0.0f) {
		frontNormal = {0.0f, 0.0f, -1.0f}; // Z の大きさが 0（向きが決まらない）
	}
	cache.frontVertices.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		const Vector2 uv = cache.worldSpaceUV ? Vector2{front[i].x, -front[i].y} : Vector2{points[i].x, -points[i].y};
		cache.frontVertices.push_back({
		    {front[i].x, front[i].y, front[i].z, 1.0f},
            uv, frontNormal
        });
	}

	// ===== 横の面（辺ごとに四角1枚。U は外周に沿った長さ、V は前からの奥行き。法線は辺の外向き）=====
	// 外向きは「反時計回りなら辺の右側」。ワールドで裏返っている（X の大きさがマイナス）と回り方が逆になるので、ワールドの面積で決める
	const bool isCounterClockwise = Polygon2D::SignedArea(frontXY) > 0.0f;
	cache.sideVertices.reserve(count * 4);
	cache.sideIndices.reserve(count * 6);
	float u = 0.0f; // 外周に沿った長さ（m）
	for (size_t i = 0; i < count; ++i) {
		const size_t next = (i + 1) % count;
		const Vector2 outward = Polygon2D::EdgeOutwardNormal(frontXY, i) * (isCounterClockwise ? 1.0f : -1.0f);
		const Vector3 normal = {outward.x, outward.y, 0.0f};
		const float length = Length(front[next] - front[i]);
		const float depth = Length(back[i] - front[i]);
		const uint32_t base = static_cast<uint32_t>(cache.sideVertices.size());
		cache.sideVertices.push_back({
		    {front[i].x, front[i].y, front[i].z, 1.0f},
            {u, 0.0f},
            normal
        });
		cache.sideVertices.push_back({
		    {front[next].x, front[next].y, front[next].z, 1.0f},
            {u + length, 0.0f},
            normal
        });
		cache.sideVertices.push_back({
		    {back[i].x, back[i].y, back[i].z, 1.0f},
            {u, depth},
            normal
        });
		cache.sideVertices.push_back({
		    {back[next].x, back[next].y, back[next].z, 1.0f},
            {u + length, depth},
            normal
        });
		cache.sideIndices.insert(cache.sideIndices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
		u += length;
	}
}

// Entity のメッシュを探す（無ければ足す）
MeshCache& FindOrAddCache(Handle<Entity> entity) {
	const auto it = std::ranges::find(state->caches, entity, &MeshCache::entity);
	if (it != state->caches.end()) {
		return *it;
	}
	MeshCache& added = state->caches.emplace_back();
	added.entity = entity;
	added.shape.pointCount = 0; // どの形とも違うので、最初の Draw で必ず作る
	return added;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Polygon Shape の見せ方。Inspector とシーンファイルの両方がこの並びを使う
void DescribePolygonShape(ComponentUI& ui, PolygonShapeComponent& value) {
	// ===== 形の編集（Inspector だけ。保存・読み込みのときは Button がいつも false）=====
	if (ui.Button(state->isEditing ? "Finish Editing###editShape" : "Edit Shape###editShape")) {
		state->isEditing = !state->isEditing;
	}
	ui.Label("Scene ビューで ■ をドラッグ・＋ で足す・Delete で消す");
	ui.Field("Depth###depth", value.depth, AtLeast(0.0f).Tip("奥行き（m）。Entity の Z を真ん中に、前後へ半分ずつ。Entity の Z の大きさも掛かる"));
	// ===== 点（先に数を読むので、シーンファイルから読むときも、その数だけ点を読む）=====
	ui.Separator("Points");
	const int before = value.pointCount;
	ui.Field("Point Count###pointCount", value.pointCount,Range(kMinPointCount, static_cast<float>(kMaxPolygonPoints)).Tip("点の数。Scene ビューで足す・消すこともできる。点は Entity から見た X・Y（m）。辺どうしが交差すると描かない（Scene ビューで線が赤くなる）。Entity の回転は Z だけ使う"));
	value.pointCount = std::clamp(value.pointCount, static_cast<int>(kMinPointCount), static_cast<int>(kMaxPolygonPoints));
	// 増やした点は、最後の点と最初の点の真ん中に置く（前に使っていた所や原点に飛んでいかないように）
	for (int i = (std::max)(before, 1); i < value.pointCount; ++i) {
		value.points[i] = (value.points[i - 1] + value.points[0]) * 0.5f;
	}
	for (int i = 0; i < value.pointCount; ++i) {
		ui.Field(std::format("Point {}###point{}", i, i).c_str(), value.points[i]); // 保存の名前は point0, point1, …
	}
}

// Polygon Renderer の見せ方
void DescribePolygonRenderer(ComponentUI& ui, PolygonRendererComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	ui.Field("Side Shade###sideShade", value.sideShade, Range(0.0f, 1.0f).Tip("横の面の明るさ（前の面の色に掛ける。1 で同じ明るさ）"));
	ui.Field("World Space UV###worldSpaceUV", value.worldSpaceUV, Tip("付ける：テクスチャをワールドの位置で貼る（足場を動かしても伸ばしても絵は伸びず、隣の足場とつながる）／外す：足場と一緒に絵も動く"));
	ui.Separator("Material");
	PrimitiveRenderSystem::DescribeMaterial(ui, value.material, "");
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void PolygonShapeSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<PolygonShapeState>();

	// Polygon Shape・Polygon Renderer の置き場所・保存・Inspector を登録する（見せ方は Describe〇〇 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<PolygonShapeComponent>();
	ComponentSerializer::Register<PolygonShapeComponent>("Polygon Shape", &DescribePolygonShape);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<PolygonShapeComponent>>("Polygon Shape", "Rendering3D", &DescribePolygonShape, "横から見た多角形を、奥へ押し出した形を持つ（足場）。見た目は Polygon Renderer、当たりは Collider の Polygon が使う"));
	EntityManager::RegisterComponent<PolygonRendererComponent>();
	ComponentSerializer::Register<PolygonRendererComponent>("Polygon Renderer", &DescribePolygonRenderer);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<PolygonRendererComponent>>("Polygon Renderer", "Rendering3D", &DescribePolygonRenderer, "同じ Entity の Polygon Shape の形を描く。テクスチャはワールドの座標で貼る（伸ばしても絵が伸びない）"));
}

void PolygonShapeSystem::Release() { state.reset(); }

//=============================================================================
// 描画
//=============================================================================
void PolygonShapeSystem::Draw(Camera* camera, IBLEnvironment* environment) {
	HTT_ASSERT_MSG(state != nullptr, "PolygonShapeSystem::Initialize()を先に呼んでください");
	for (MeshCache& cache : state->caches) {
		cache.isUsed = false;
	}
	EntityManager::ForEach<PolygonRendererComponent>([camera, environment](Handle<Entity> entity, const PolygonRendererComponent& renderer) {
		if (!renderer.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const PolygonShapeComponent* shape = EntityManager::Get<PolygonShapeComponent>(entity);
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (shape == nullptr || transform == nullptr) {
			return; // 形が無い（Polygon Shape を外した）
		}
		// --- 形・置き場所・UV の作り方が、前に作ったときと違えば作り直す ---
		MeshCache& cache = FindOrAddCache(entity);
		cache.isUsed = true;
		if (!IsSameBytes(cache.shape, *shape) || !IsSameBytes(cache.worldMatrix, transform->worldMatrix) || cache.worldSpaceUV != renderer.worldSpaceUV) {
			cache.shape = *shape;
			cache.worldMatrix = transform->worldMatrix;
			cache.worldSpaceUV = renderer.worldSpaceUV;
			BuildMesh(cache);
		}
		if (!cache.isValid) {
			return;
		}
		// --- 前の面・横の面（横は色を暗くする）。覚えているメッシュは写して渡す（Renderer がフレームの終わりまで持つので）---
		Renderer::MeshConfig config;
		PrimitiveRenderSystem::ApplyMaterial(config, renderer.material, environment);
		config.camera = camera;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		Renderer::DrawMesh(config, cache.frontVertices, cache.frontIndices);
		const Vector4& color = renderer.material.color;
		const float shade = std::clamp(renderer.sideShade, 0.0f, 1.0f);
		config.color = ApplyIntensity({color.x * shade, color.y * shade, color.z * shade, color.w}, renderer.material.intensity);
		Renderer::DrawMesh(config, cache.sideVertices, cache.sideIndices);
	});
	// 描かなかった物（消えた・外した・無効にした）のメッシュを捨てる
	std::erase_if(state->caches, [](const MeshCache& cache) { return !cache.isUsed; });
}

//=============================================================================
// 当たり判定用の形
//=============================================================================
ExtrudedPolygon PolygonShapeSystem::CalcWorldPolygon(const PolygonShapeComponent& shape, const Vector3& offset, const Matrix4x4& worldMatrix) {
	ExtrudedPolygon result;
	result.origin = TransformPoint(offset, worldMatrix);
	const std::span<const Vector2> points = shape.GetPoints();
	result.count = static_cast<uint32_t>(points.size());
	for (size_t i = 0; i < points.size(); ++i) {
		const Vector3 world = TransformPoint({points[i].x + offset.x, points[i].y + offset.y, offset.z}, worldMatrix);
		result.points[i] = {world.x - result.origin.x, world.y - result.origin.y};
	}
	// 当たり判定は反時計回りの並びで考えるので、時計回りなら逆に並べる（Inspector の並び・X の大きさがマイナスで裏返した物）
	if (Polygon2D::SignedArea(result.GetPoints()) < 0.0f) {
		std::reverse(result.points.begin(), result.points.begin() + result.count);
	}
	result.halfDepth = std::abs(shape.depth) * 0.5f * GetScaleZ(worldMatrix);
	return result;
}

//=============================================================================
// 形の編集
//=============================================================================
bool PolygonShapeSystem::IsEditing() { return state != nullptr && state->isEditing; }

void PolygonShapeSystem::SetEditing(bool isEditing) {
	HTT_ASSERT_MSG(state != nullptr, "PolygonShapeSystem::Initialize()を先に呼んでください");
	state->isEditing = isEditing;
}