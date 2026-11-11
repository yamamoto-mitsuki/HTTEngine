#include "ElasticSurfaceSystem.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/Systems/Rendering3D/PrimitiveRenderSystem.h"

namespace {
constexpr float kMinContactDepth = 0.5f;           // 面より何m下まで入った点を押し返すか（厚さがこれより薄いときの下限）。速く落ちた物が1歩で深く入っても抜けないように
constexpr size_t kMaxBoxPoints = 9;                // 箱の底に並べて調べる点の数の上限
constexpr float kNoFadeDistance = 10000.0f;        // 確かめる用の線が薄くなり始める距離（遠くても薄くしないように、大きくしておく）
constexpr Vector4 kSurfaceLineColor = {1.0f, 1.0f, 1.0f, 1.0f};    // 確かめる用の線：点をつないだ線（白）
constexpr Vector4 kRestLineColor = {0.502f, 0.502f, 0.502f, 1.0f}; // 確かめる用の線：平らな位置（灰色）

/// <summary>
/// 面1つ分の実行中だけの状態（保存しない）
/// </summary>
struct SurfaceState {
	Handle<Entity> entity;               // 持ち主
	ElasticSurfaceSimulation simulation; // 揺れている形
	Vector3 origin = {0.0f, 0.0f, 0.0f}; // 面の真ん中（ワールド）。BeginPhysicsで入れなおす
	float halfDepth = 0.0f;              // 奥行きの半分（この外の Z の物は乗らない）
	float maxPenetration = 0.0f;         // これより深く入った点は押さない
	ElasticContactSettings contact;      // ぶつかり方
	bool isActive = false;               // このフレームの物理で使うか（無効・Enabled が外れていれば false）
};

/// <summary>
/// ElasticSurfaceSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct ElasticSurfaceSystemState {
	std::vector<SurfaceState> surfaces; // 形を持っている面の一覧（面の数しか無いので、順に見れば足りる）
};
std::unique_ptr<ElasticSurfaceSystemState> state;

// Entityの状態を探す（無ければ nullptr）
SurfaceState* FindState(Handle<Entity> entity) {
	const auto it = std::ranges::find(state->surfaces, entity, &SurfaceState::entity);
	return (it != state->surfaces.end()) ? &*it : nullptr;
}

// Entityの状態を探し、無ければ足す。どちらでも、Inspector の張り方を入れる（幅・点の数が変わったときだけ作り直される）
SurfaceState& FindOrAddState(Handle<Entity> entity, const ElasticSurfaceComponent& component) {
	SurfaceState* found = FindState(entity);
	if (found == nullptr) {
		state->surfaces.push_back({});
		found = &state->surfaces.back();
		found->entity = entity;
	}
	found->simulation.SetSettings(component.surface);
	return *found;
}

// 傾き slope の面に垂直な、上向きの向き（長さ1）。右上がりの坂なら、左に傾く
Vector3 CalcSurfaceNormal(float slope) { return Normalize(Vector3{-slope, 1.0f, 0.0f}); }

//=============================================================================
// ぶつかり
//=============================================================================
// ===== 面から見た点 (x, y) を押し返す力（ワールドの向きの力。入っていなければ (0, 0, 0)）=====
Vector3 PushPoint(SurfaceState& surface, float x, float y, float velocityY, float radius, const ElasticContactSettings& contact) {
	const float force = surface.simulation.PushBack(x, y, velocityY, radius, surface.maxPenetration, contact);
	if (force <= 0.0f) {
		return {0.0f, 0.0f, 0.0f};
	}
	return CalcSurfaceNormal(surface.simulation.GetSlope(x)) * force;
}

// ===== 球: 面に一番近い点（中心から、面に垂直な向きの逆へ半径の所）で調べる =====
Vector3 PushSphere(SurfaceState& surface, const Sphere& sphere, float velocityY) {
	const Vector3 center = sphere.center - surface.origin; // 面から見た中心
	if (std::abs(center.z) > surface.halfDepth + sphere.radius) {
		return {0.0f, 0.0f, 0.0f}; // 奥行きの外
	}
	const Vector3 normal = CalcSurfaceNormal(surface.simulation.GetSlope(center.x));
	const Vector3 point = center - normal * sphere.radius;
	return PushPoint(surface, point.x, point.y, velocityY, sphere.radius, surface.contact);
}

// ===== カプセル：下の端の球で調べる（立っているカプセルの足元。横の面は、面に当たらない）=====
Vector3 PushCapsule(SurfaceState& surface, const Capsule& capsule, float velocityY) {
	const Vector3& bottom = (capsule.start.y < capsule.end.y) ? capsule.start : capsule.end;
	return PushSphere(surface, {bottom, capsule.radius}, velocityY);
}

// ===== 箱：底の面に、左端から右端まで点を並べて調べる（点が多いほど硬くならないように、硬さは点の数で割る）=====
Vector3 PushBox(SurfaceState& surface, const AABB& box, float velocityY) {
	const Vector3 min = box.min - surface.origin; // 面から見た位置
	const Vector3 max = box.max - surface.origin;
	if (min.z > surface.halfDepth || max.z < -surface.halfDepth) {
		return {0.0f, 0.0f, 0.0f}; // 奥行きの外
	}
	const float width = max.x - min.x;
	// 面の点の間隔ごとに1つくらい（最低でも両端の2つ）
	const size_t count = std::clamp(static_cast<size_t>(std::ceil(width / surface.simulation.GetSpacing())) + 1, size_t{2}, kMaxBoxPoints);
	ElasticContactSettings contact = surface.contact;
	contact.stiffness /= static_cast<float>(count);
	contact.damping /= static_cast<float>(count);
	const float interval = width / static_cast<float>(count - 1); // 点と点の間
	Vector3 total = {0.0f, 0.0f, 0.0f};
	for (size_t i = 0; i < count; ++i) {
		total += PushPoint(surface, min.x + interval * static_cast<float>(i), min.y, velocityY, interval * 0.5f, contact);
	}
	return total;
}

//=============================================================================
// 描画（頂点を並べる道具）
//=============================================================================
// ===== 頂点1つ =====
Vertex3dData MakeVertex(const Vector3& position, const Vector2& uv, const Vector3& normal) {
	return {
	    {position.x, position.y, position.z, 1.0f},
        uv, normal
    };
}

// ===== 帯：count組の頂点2つを並べ、隣の組との間を三角形2つでつなぐ。makePair(i) が i 組目の2頂点を返す =====
template<class MakePair> 
void AppendStrip(std::vector<Vertex3dData>& vertices, std::vector<uint32_t>& indices, size_t count, MakePair makePair) {
	const uint32_t base = static_cast<uint32_t>(vertices.size());
	for (size_t i = 0; i < count; ++i) {
		const auto [first, second] = makePair(i);
		vertices.push_back(first);
		vertices.push_back(second);
	}
	for (size_t i = 0; i + 1 < count; ++i) {
		const uint32_t a = base + static_cast<uint32_t>(i * 2); // この組の1つ目
		indices.insert(indices.end(), {a, a + 1, a + 2, a + 2, a + 1, a + 3});
	}
}

// 四角1枚（4頂点。p0-p1-p3-p2 の順に回る四角）。法線は全部同じ
// UV は m 単位（p0 が (0, 0)、p0→p1 の向きに U、p0→p2 の向きに V が、辺の長さだけ増える）。テクスチャの大きさが、どの面でもそろう
void AppendQuad(std::vector<Vertex3dData>& vertices, std::vector<uint32_t>& indices, const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, const Vector3& normal) {
	const float width = Length(p1 - p0);  // U の向きの長さ（m）
	const float height = Length(p2 - p0); // V の向きの長さ（m）
	const uint32_t base = static_cast<uint32_t>(vertices.size());
	vertices.push_back(MakeVertex(p0, {0.0f, 0.0f}, normal));
	vertices.push_back(MakeVertex(p1, {width, 0.0f}, normal));
	vertices.push_back(MakeVertex(p2, {0.0f, height}, normal));
	vertices.push_back(MakeVertex(p3, {width, height}, normal));
	indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
}

// ===== 軸にそろった箱（6面。面ごとに頂点を分けて、角がくっきり見えるようにする）=====
void AppendBox(std::vector<Vertex3dData>& vertices, std::vector<uint32_t>& indices, const Vector3& min, const Vector3& max) {
	AppendQuad(vertices, indices, {min.x, max.y, min.z}, {max.x, max.y, min.z}, {min.x, min.y, min.z}, {max.x, min.y, min.z}, {0.0f, 0.0f, -1.0f}); // 手前
	AppendQuad(vertices, indices, {max.x, max.y, max.z}, {min.x, max.y, max.z}, {max.x, min.y, max.z}, {min.x, min.y, max.z}, {0.0f, 0.0f, 1.0f});  // 奥
	AppendQuad(vertices, indices, {min.x, max.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z}, {0.0f, 1.0f, 0.0f});  // 上
	AppendQuad(vertices, indices, {min.x, min.y, min.z}, {max.x, min.y, min.z}, {min.x, min.y, max.z}, {max.x, min.y, max.z}, {0.0f, -1.0f, 0.0f}); // 下
	AppendQuad(vertices, indices, {min.x, max.y, max.z}, {min.x, max.y, min.z}, {min.x, min.y, max.z}, {min.x, min.y, min.z}, {-1.0f, 0.0f, 0.0f}); // 左
	AppendQuad(vertices, indices, {max.x, max.y, min.z}, {max.x, max.y, max.z}, {max.x, min.y, min.z}, {max.x, min.y, max.z}, {1.0f, 0.0f, 0.0f});  // 右
}


//=============================================================================
// 描画
//=============================================================================
// ===== 厚みのある板：上の面（揺れている形）・下の面（上の面を厚さの分だけ下げた形）・手前と奥の壁・左右の端 =====
//
//   横から見た形（手前の壁）            上の面の頂点 i は、下の面の頂点 i の真上
//   ●───●───●───●───●   ← 上の面
//   │   │   │   │   │   ← 厚さ
//   ●───●───●───●───●   ← 下の面
void DrawSlab(const ElasticSurfaceSimulation& simulation, const ElasticSurfaceComponent& component, const Vector3& origin, 
	Camera* camera, IBLEnvironment* environment) {
	const size_t count = simulation.GetPointCount();
	if (count < 2) {
		return;
	}
	const float depth = (std::max)(component.depth, 0.0f);
	const float frontZ = origin.z - depth * 0.5f;
	const float backZ = origin.z + depth * 0.5f;
	const float thickness = (std::max)(component.thickness, 0.0f);

	// --- 点iの上の面の位置・法線・横のUV ---
	const auto topPoint = [&](size_t i) { return Vector3{origin.x + simulation.GetPointX(i), origin.y + simulation.GetPointHeight(i), 0.0f}; };
	const auto topNormal = [&](size_t i) {
		// 左右の点の高さの差から傾きを出す（端は自分と隣）
		const size_t left = (i == 0) ? 0 : i - 1;
		const size_t right = (std::min)(i + 1, count - 1);
		const float slope = (simulation.GetPointHeight(right) - simulation.GetPointHeight(left)) / (simulation.GetSpacing() * static_cast<float>(right - left));
		return CalcSurfaceNormal(slope);
	};
	const auto u = [&simulation](size_t i) { return simulation.GetPointX(i) - simulation.GetPointX(0); }; // 左端からの距離（m）

	std::vector<Vertex3dData> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve(count * 8 + 8);
	indices.reserve((count - 1) * 24 + 12);

	// ===== 上の面（乗る面。当たるのはここだけ）=====
	AppendStrip(vertices, indices, count, [&](size_t i) {
		const Vector3 p = topPoint(i);
		const Vector3 n = topNormal(i);
		return std::pair{MakeVertex({p.x, p.y, frontZ}, {u(i), depth}, n), MakeVertex({p.x, p.y, backZ}, {u(i), 0.0f}, n)};
	});

	if (thickness > 0.0f) {
		// ===== 下の面（上の面と同じ形で、厚さの分だけ下。法線は下向き）=====
		AppendStrip(vertices, indices, count, [&](size_t i) {
			const Vector3 p = topPoint(i);
			const Vector3 n = -topNormal(i);
			return std::pair{MakeVertex({p.x, p.y - thickness, frontZ}, {u(i), depth}, n), MakeVertex({p.x, p.y - thickness, backZ}, {u(i), 0.0f}, n)};
		});
		// ===== 手前の壁・奥の壁（上の面と下の面の縁をつなぐ）=====
		for (const float z : {frontZ, backZ}) {
			const Vector3 n = {0.0f, 0.0f, (z == frontZ) ? -1.0f : 1.0f};
			AppendStrip(vertices, indices, count, [&](size_t i) {
				const Vector3 p = topPoint(i);
				return std::pair{MakeVertex({p.x, p.y, z}, {u(i), 0.0f}, n), MakeVertex({p.x, p.y - thickness, z}, {u(i), thickness}, n)};
			});
		}
		// ===== 左右の端（支柱に付いている所）=====
		const Vector3 left = topPoint(0);
		const Vector3 right = topPoint(count - 1);
		AppendQuad(vertices, indices, {left.x, left.y, backZ}, {left.x, left.y, frontZ}, {left.x, left.y - thickness, backZ}, {left.x, left.y - thickness, frontZ}, {-1.0f, 0.0f, 0.0f});
		AppendQuad(vertices, indices, {right.x, right.y, frontZ}, {right.x, right.y, backZ}, {right.x, right.y - thickness, frontZ}, {right.x, right.y - thickness, backZ}, {1.0f, 0.0f, 0.0f});
	}

	Renderer::MeshConfig config;
	PrimitiveRenderSystem::ApplyMaterial(config, component.material, environment); // 色・テクスチャ・陰の付け方・光沢（Primitive Renderer と同じ）
	config.camera = camera;
	Renderer::DrawMesh(config, std::move(vertices), std::move(indices)); // move で渡すと、配列を写さない
}

// ===== 両端の支柱（面の外側に、上の面の高さから下へ） =====
void DrawPosts(const ElasticSurfaceComponent& component, const Vector3& origin, Camera* camera, IBLEnvironment* environment) {
	if (component.postHeight <= 0.0f || component.postWidth <= 0.0f) {
		return;
	}
	const float halfWidth = component.surface.width * 0.5f;
	const float frontZ = origin.z - component.depth * 0.5f;
	const float backZ = origin.z + component.depth * 0.5f;
	const float bottomY = origin.y - component.postHeight;

	std::vector<Vertex3dData> vertices;
	std::vector<uint32_t> indices;
	AppendBox(vertices, indices, {origin.x - halfWidth - component.postWidth, bottomY, frontZ}, {origin.x - halfWidth, origin.y, backZ}); // 左
	AppendBox(vertices, indices, {origin.x + halfWidth, bottomY, frontZ}, {origin.x + halfWidth + component.postWidth, origin.y, backZ}); // 右

	Renderer::MeshConfig config;
	PrimitiveRenderSystem::ApplyMaterial(config, component.postMaterial, environment); // 支柱の見た目（Inspector の Posts）
	config.camera = camera;
	Renderer::DrawMesh(config, std::move(vertices), std::move(indices));
}

// ===== 確かめる用の線：点をつないだ線（白）と平らな位置（灰色）。手前の縁に、奥行きを見ずに描く（板に隠れない）=====
void DrawDebugLines(const ElasticSurfaceSimulation& simulation, const ElasticSurfaceComponent& component, const Vector3& origin, Camera* camera) {
	const size_t count = simulation.GetPointCount();
	if (count < 2) {
		return;
	}
	const float z = origin.z - component.depth * 0.5f;
	const float halfWidth = component.surface.width * 0.5f;

	Renderer::LineListConfig config;
	config.camera = camera;
	config.depthMode = DepthMode::Disable;
	config.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く
	config.fadeStartDistance = kNoFadeDistance;
	config.fadeEndDistance = kNoFadeDistance * 2.0f;
	config.lines.reserve(count);
	config.lines.push_back({
	    {origin.x - halfWidth, origin.y, z},
        {origin.x + halfWidth, origin.y, z},
        kRestLineColor
    });
	for (size_t i = 0; i + 1 < count; ++i) {
		const Vector3 start = {origin.x + simulation.GetPointX(i), origin.y + simulation.GetPointHeight(i), z};
		const Vector3 end = {origin.x + simulation.GetPointX(i + 1), origin.y + simulation.GetPointHeight(i + 1), z};
		config.lines.push_back({start, end, kSurfaceLineColor});
	}
	Renderer::DrawLines(config);
}


//=============================================================================
// Inspector と 保存
//=============================================================================
// Elastic Surface の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Trail・Rigidbody と同じ書き方）
void DescribeElasticSurface(ComponentUI& ui, ElasticSurfaceComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと乗れない・描かない"));
	// ===== 膜の張り方 =====
	ui.Separator("Surface");
	ui.Field("Width###width", value.surface.width, AtLeast(1.0f).Tip("横幅（m）。両端が支柱で、その外には乗れない"));
	ui.Field("Point Count###pointCount", value.surface.pointCount, Range(3.0f, 513.0f).Tip("点の数（両端の支柱を含む）。多いほど細かく曲がるが重くなる"));
	ui.Field("Tension###tension", value.surface.tension, AtLeast(0.0f).Drag(100.0f).Tip("張りの強さ（N）。強いほど沈みにくく、速く跳ね返す"));
	ui.Field("Mass Per Meter###massPerMeter", value.surface.massPerMeter, AtLeast(0.01f).Tip("1mあたりの膜の重さ（kg）。軽いほど、乗った物の勢いがそのまま跳ね返りになる"));
	ui.Field("Damping###damping", value.surface.damping, AtLeast(0.0f).Tip("揺れが収まる速さ。大きいほど跳ね返りが弱くなる"));
	ui.Field("Smoothing###smoothing", value.surface.smoothing, AtLeast(0.0f).Tip("細かいギザギザだけを消す強さ。大きい曲がりにはほとんど効かない"));
	// ===== 端の硬さ（どちらも0で今まで通り）=====
	ui.Separator("Edge");
	ui.Field("Edge Stiffness###edgeStiffness", value.surface.edgeStiffness, AtLeast(0.0f).Drag(100.0f).Tip("端を平らな位置へ引き戻す強さ（N/m²）。大きいほど端が沈みにくい。0 で無し"));
	ui.Field("Edge Damping###edgeDamping", value.surface.edgeDamping, AtLeast(0.0f).Tip("端の揺れを止める強さ（Damping に足す）。大きいほど端で跳ね返りが弱い。0 で無し"));
	ui.Field("Edge Falloff###edgeFalloff", value.surface.edgeFalloff, AtLeast(0.0f).Tip("端からどれだけ内側まで効かせるか。1 で真ん中から真っすぐ強く、2・4 と大きいほど端の近くだけ。0 で全体に同じだけ"));
	// ===== 物とのぶつかり方 =====
	ui.Separator("Contact");
	ui.Field("Contact Stiffness###contactStiffness", value.contact.stiffness, AtLeast(0.0f).Drag(100.0f).Tip("めり込んだ深さ1mあたりの押し返す力（N/m）。小さいと物が面に沈み込んで見える"));
	ui.Field("Contact Damping###contactDamping", value.contact.damping, AtLeast(0.0f).Tip("近づく速さ1m/sあたりの押し返す力（N・s/m）。止まっている物が震えるなら上げる"));
	ui.Field("Steps Per Second###stepsPerSecond", value.stepsPerSecond, Range(60.0f, 5000.0f).Tip("1秒に何歩計算するか。Tension を上げて暴れたら増やす"));
	// ===== 見た目 =====
	ui.Separator("Appearance");
	ui.Field("Thickness###thickness", value.thickness, AtLeast(0.0f).Tip("厚さ（m）。上の面から下へ。見た目だけ（当たるのは上の面）"));
	ui.Field("Depth###depth", value.depth, AtLeast(0.0f).Tip("奥行き（m）。この外の Z にいる物は乗らない"));
	ui.Field("Show Lines###showLines", value.showLines, Tip("点をつないだ線（白）と平らな位置（灰色）を描く。確かめる用"));
	// ===== 板の見た目（保存の名前は "color"・"texture" …。色は前と同じ名前なので、前のシーンの色も読める）=====
	ui.Separator("Surface Material");
	PrimitiveRenderSystem::DescribeMaterial(ui, value.material, "");
	// ===== 支柱（保存の名前は "postColor"・"postTexture" …）=====
	ui.Separator("Posts");
	ui.Field("Post Height###postHeight", value.postHeight, AtLeast(0.0f).Tip("両端の支柱の高さ（m。上の面から下へ）。0 で描かない"));
	ui.Field("Post Width###postWidth", value.postWidth, AtLeast(0.0f).Tip("支柱の幅（m）"));
	PrimitiveRenderSystem::DescribeMaterial(ui, value.postMaterial, "post");
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void ElasticSurfaceSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<ElasticSurfaceSystemState>();

	// Elastic Surface の置き場所・保存・Inspector を登録する。見せ方は DescribeElasticSurface 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<ElasticSurfaceComponent>();
	ComponentSerializer::Register<ElasticSurfaceComponent>("Elastic Surface", &DescribeElasticSurface);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ElasticSurfaceComponent>>("Elastic Surface", "Physics", &DescribeElasticSurface, "弾む面（トランポリン）。Rigidbody を付けた物が乗ると沈んで、押し返して跳ねさせる"));
}

// ===== 解放 =====
void ElasticSurfaceSystem::Release() { state.reset(); }


//=============================================================================
// 毎フレーム
//=============================================================================
void ElasticSurfaceSystem::Update(bool isGameRunning) {
	HTT_ASSERT_MSG(state != nullptr, "ElasticSurfaceSystem::Initialize()を先に呼んでください");
	// ===== 消えた・外した面の形を捨てる =====
	std::erase_if(state->surfaces, [](const SurfaceState& surface) { return !EntityManager::IsAlive(surface.entity) || EntityManager::Get<ElasticSurfaceComponent>(surface.entity) == nullptr; });
	
	// ===== 停止中はいつも平ら（次に Play したときは、平らなところから）=====
	if (!isGameRunning) {
		for (SurfaceState& surface : state->surfaces) {
			surface.simulation.Flatten();
		}
	}
}


//=============================================================================
// 描画
//=============================================================================
void ElasticSurfaceSystem::Draw(Camera* camera, IBLEnvironment* environment) {
	HTT_ASSERT_MSG(state != nullptr, "ElasticSurfaceSystem::Initialize()を先に呼んでください");
	EntityManager::ForEach<ElasticSurfaceComponent>([camera, environment](Handle<Entity> entity, const ElasticSurfaceComponent& component) {
		if (!component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		// 停止中でも描けるように、ここでも状態を作る（停止中に Width を変えても、すぐ見た目に出る）
		const SurfaceState& surface = FindOrAddState(entity, component);
		const Vector3 origin = GetWorldPosition(*transform);
		DrawSlab(surface.simulation, component, origin, camera, environment);
		DrawPosts(component, origin, camera, environment);
		if (component.showLines) {
			DrawDebugLines(surface.simulation, component, origin, camera);
		}
	});
}


//=============================================================================
// PhysicsSystem から
//=============================================================================
// ===== 使う面を集めて、Inspector の値を入れる。ワールド行列の計算の後に呼ぶ =====
float ElasticSurfaceSystem::BeginPhysics() {
	HTT_ASSERT_MSG(state != nullptr, "ElasticSurfaceSystem::Initialize()を先に呼んでください");
	for (SurfaceState& surface : state->surfaces) {
		surface.isActive = false; // このフレームに使う物だけ、下で true にする
	}
	float stepTime = 0.0f;
	EntityManager::ForEach<ElasticSurfaceComponent>([&stepTime](Handle<Entity> entity, const ElasticSurfaceComponent& component) {
		if (!component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		// --- Inspector の値を入れる ---
		SurfaceState& surface = FindOrAddState(entity, component);
		surface.origin = GetWorldPosition(*transform);
		surface.halfDepth = component.depth * 0.5f;
		surface.maxPenetration = (std::max)(component.thickness, kMinContactDepth);
		surface.contact = component.contact;
		surface.isActive = true;
		// --- 1歩の長さ：面が何枚あっても、一番細かい物に合わせる ---
		const float surfaceStep = 1.0f / static_cast<float>((std::max)(component.stepsPerSecond, 1));
		stepTime = (stepTime == 0.0f) ? surfaceStep : (std::min)(stepTime, surfaceStep);
	});
	return stepTime;
}

// ===== 物の形（Collider）が面に入っていたら、面から押し返される力（N）を返す。面には同じ力が下向きにたまる（反作用）=====
Vector3 ElasticSurfaceSystem::CalcContactForce(const WorldCollider& shape, float velocityY) {
	Vector3 total = {0.0f, 0.0f, 0.0f};
	for (SurfaceState& surface : state->surfaces) {
		if (!surface.isActive) {
			continue;
		}
		switch (shape.shape) {
		case ColliderShape::Sphere:
			total += PushSphere(surface, shape.sphere, velocityY);
			break;
		case ColliderShape::Box:
			total += PushBox(surface, shape.box, velocityY);
			break;
		case ColliderShape::Capsule:
			total += PushCapsule(surface, shape.capsule, velocityY);
			break;
		case ColliderShape::Polygon:
			break;
		}
	}
	return total;
}

// ===== 全部の面を stepTime 秒進める（たまった力で沈み、張りで戻る）=====
void ElasticSurfaceSystem::Step(float stepTime) {
	for (SurfaceState& surface : state->surfaces) {
		if (surface.isActive) {
			surface.simulation.Step(stepTime);
		}
	}
}


//=============================================================================
// ゲームから使う
//=============================================================================
Handle<Entity> ElasticSurfaceSystem::FindSurface(float worldX) {
	Handle<Entity> found;
	EntityManager::ForEach<ElasticSurfaceComponent>([&found, worldX](Handle<Entity> entity, const ElasticSurfaceComponent& component) {
		if (EntityManager::IsAlive(found) || !component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return; // もう見つけた・使っていない面
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform != nullptr && std::abs(worldX - GetWorldPosition(*transform).x) <= component.surface.width * 0.5f) {
			found = entity;
		}
	});
	return found;
}

float ElasticSurfaceSystem::GetHeight(Handle<Entity> surface, float worldX) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(surface);
	if (transform == nullptr) {
		return 0.0f;
	}
	const Vector3 origin = GetWorldPosition(*transform);
	const SurfaceState* found = FindState(surface);
	return origin.y + ((found != nullptr) ? found->simulation.GetHeight(worldX - origin.x) : 0.0f); // 形がまだ無ければ平ら
}

float ElasticSurfaceSystem::GetVelocity(Handle<Entity> surface, float worldX) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(surface);
	const SurfaceState* found = FindState(surface);
	if (transform == nullptr || found == nullptr) {
		return 0.0f;
	}
	return found->simulation.GetVelocity(worldX - GetWorldPosition(*transform).x);
}

void ElasticSurfaceSystem::AddImpulse(Handle<Entity> surface, float worldX, float impulse, float radius) {
	const ElasticSurfaceComponent* component = EntityManager::Get<ElasticSurfaceComponent>(surface);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(surface);
	if (component == nullptr || transform == nullptr) {
		return;
	}
	FindOrAddState(surface, *component).simulation.AddImpulse(worldX - GetWorldPosition(*transform).x, impulse, radius);
}