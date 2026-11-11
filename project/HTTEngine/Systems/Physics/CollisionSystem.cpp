#include "CollisionSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Components/Rendering3D/PolygonShapeComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Physics/MotionQuery.h"
#include "HTTEngine/Systems/Rendering3D/PolygonShapeSystem.h"

namespace {
constexpr uint32_t kCircleDivision = 24;       // 球の円を何本の線で描くか
constexpr Vector4 kIdleColor = {0.4f, 1.0f, 0.4f, 1.0f};    // 当たっていない Collider の線の色（緑）
constexpr Vector4 kTriggerColor = {0.4f, 0.8f, 1.0f, 1.0f}; // 当たっていない Is Trigger の Collider の線の色（水色。すり抜ける物と見分ける）
constexpr Vector4 kHitColor = {1.0f, 0.267f, 0.267f, 1.0f}; // 当たっている Collider の線の色（赤）
constexpr float kNoFadeStart = 10000.0f;       // 距離でフェードさせないための値（LightGizmoと同じ）
constexpr float kNoFadeEnd = 20000.0f;

// ===== 判定に使う形 =====

/// <summary>
/// 重なっている2つ。first が小さい方（同じ組を「AとB」「BとA」の2回数えないように、並びをそろえる）
/// </summary>
struct HitPair {
	Handle<Entity> first;
	Handle<Entity> second;
	bool isTrigger = false; // どちらかが Is Trigger（途中で切り替えたら、前の当たりは Exit・新しい当たりは Enter になる）
	Vector3 normal = {0.0f, 0.0f, 0.0f}; // second から first へ押し出す向き（長さ1）。並び順（PairLess）には使わない
	Vector3 firstVelocity = {0.0f, 0.0f, 0.0f};  // first の速さ（物理でぶつかったときは、ぶつかる直前の速さ）
	Vector3 secondVelocity = {0.0f, 0.0f, 0.0f}; // second の速さ（同じ）
};

/// <summary>
/// CollisionSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct CollisionState {
	std::vector<WorldCollider> colliders; // このフレームに判定した形（ギズモもこれを描く＝見えている線と判定が同じ）
	std::vector<HitPair> currentPairs;    // このフレームに重なっていた組（並べてある）
	std::vector<HitPair> previousPairs;   // 前のフレームに重なっていた組（並べてある）
	std::vector<CollisionHit> hits;       // 当たりの一覧（self の順）
	std::vector<HitPair> impacts;         // このフレームに物理でぶつかった組（ReportImpact が足し、Update で混ぜて空にする）
	Renderer::LineListConfig gizmoLines;  // ギズモの線の置き場（配列のメモリを使い回す）
	bool showGizmos = true;               // 線を出すか
};
std::unique_ptr<CollisionState> state;

// Handle の並び順（番号 → 世代）。組の並びをそろえるのと、二分探索に使う
bool HandleLess(const Handle<Entity>& a, const Handle<Entity>& b) { return std::tie(a.index, a.generation) < std::tie(b.index, b.generation); }

// 組の並び順（first → second → isTrigger）
bool PairLess(const HitPair& a, const HitPair& b) {
	if (a.first != b.first) {
		return HandleLess(a.first, b.first);
	}
	if (a.second != b.second) {
		return HandleLess(a.second, b.second);
	}
	return a.isTrigger < b.isTrigger;
}

// 2つのEntityから、並びをそろえた組を作る。normal は b から a へ押し出す向き（入れ替えたら、向きも逆にして、速さも入れ替える）
HitPair MakePair(Handle<Entity> a, Handle<Entity> b, bool isTrigger, const Vector3& normal, const Vector3& velocityA, const Vector3& velocityB) {
	return HandleLess(a, b) ? HitPair{a, b, isTrigger, normal, velocityA, velocityB} : HitPair{b, a, isTrigger, -normal, velocityB, velocityA};
}

// 同じ2つの組か（isTrigger は見ない）
bool IsSamePair(const HitPair& a, const HitPair& b) { return a.first == b.first && a.second == b.second; }

// 今の速さ（Rigidbody・Character Controller など、動き方の Component が無ければ0。動かない壁・床）
// どの動き方かは知らずに、窓口（MotionQuery）に聞く
Vector3 VelocityOf(Handle<Entity> entity) { return MotionQuery::GetVelocity(entity); }

// Entityの大きさ（ワールド行列の1〜3行目の長さ＝ローカルのX・Y・Z軸がどれだけ伸びているか。親の大きさも入っている）
Vector3 GetWorldScale(const Matrix4x4& m) {
	return {
	    Length(Vector3{m.m[0][0], m.m[0][1], m.m[0][2]}),
	    Length(Vector3{m.m[1][0], m.m[1][1], m.m[1][2]}),
	    Length(Vector3{m.m[2][0], m.m[2][1], m.m[2][2]}),
	};
}

// 形を distance だけ太らせる（球・カプセルは半径を、箱は6つの面を外へ。多角形は太らせた量を覚える＝角の丸い形になる）
WorldCollider Inflate(const WorldCollider& collider, float distance) {
	WorldCollider result = collider;
	const Vector3 grow = {distance, distance, distance};
	result.sphere.radius += distance;
	result.box = {collider.box.min - grow, collider.box.max + grow};
	result.capsule.radius += distance;
	result.polygon.radius += distance;
	return result;
}

// 多角形を囲む箱（ワールド座標）。早く「当たらない」と分かる所を見るのと、カプセルを球に置き換えるときに使う
AABB CalcPolygonBounds(const ExtrudedPolygon& polygon) {
	const Vector3& origin = polygon.origin;
	AABB box = {
	    {origin.x, origin.y, origin.z - polygon.halfDepth},
	    {origin.x, origin.y, origin.z + polygon.halfDepth},
	};
	for (const Vector2& point : polygon.GetPoints()) {
		box.min.x = (std::min)(box.min.x, origin.x + point.x);
		box.min.y = (std::min)(box.min.y, origin.y + point.y);
		box.max.x = (std::max)(box.max.x, origin.x + point.x);
		box.max.y = (std::max)(box.max.y, origin.y + point.y);
	}
	return box;
}

// カプセルを線分の上のpointに置いた同じ半径の球にする
WorldCollider ToSphere(const WorldCollider& capsule, const Vector3& point) {
	WorldCollider result = capsule;
	result.shape = ColliderShape::Sphere;
	result.sphere = {point, capsule.capsule.radius};
	return result;
}

// カプセルの線分の上で、相手（球・箱）に一番近い点。箱に入っていれば、一番深く入っている点
// 多角形は、囲む箱（box）で同じように選ぶ（押し出し・重なりの判定では使わない。CalcPushOut が多角形用の関数で、線分の上の候補から選ぶ）
Vector3 NearestPointOnCapsule(const Capsule& capsule, const WorldCollider& other) {
	if (other.shape == ColliderShape::Sphere) {
		return Collision::ClosestPointOnSegment(other.sphere.center, capsule.start, capsule.end);
	}
	return Collision::DeepestPointOnSegmentInAABB(capsule.start, capsule.end, other.box);
}

// 2つが重なっているか（形の組み合わせで、使う関数を選ぶ）
bool IsOverlapping(const WorldCollider& a, const WorldCollider& b) {
	Vector3 push;
	return CollisionSystem::CalcPushOut(a, b, {}, &push);
}

// 2つが kContactOffset より近いか（触れているだけの物も「当たっている」にする）。近ければ、b から a へ押し出す向き（長さ1）を normal に入れる
// Rigid body は壁や床から押し戻されて「ちょうど触れた所」で止まるので、重なりだけを見ると、乗っている床との当たりが毎フレーム切れたりつながったりする
bool CalcContact(const WorldCollider& a, const WorldCollider& b, Vector3* normal) {
	const float half = CollisionSystem::kContactOffset * 0.5f; // 両方を半分ずつ太らせる
	Vector3 push;
	if (!CollisionSystem::CalcPushOut(Inflate(a, half), Inflate(b, half), {}, &push)) {
		return false;
	}
	*normal = Normalize(push);
	return true;
}

// 1組を「両方から見た2件」にして足す。消えたEntityから見た分は足さない（読む人がいない）
// normal は「相手から自分へ」なので、second から見た分は逆向き。速さも「自分・相手」を入れ替える
void AddHits(std::vector<CollisionHit>& hits, const HitPair& pair, HitPhase phase) {
	if (EntityManager::IsAlive(pair.first)) {
		hits.push_back({pair.first, pair.second, phase, pair.isTrigger, pair.normal, pair.firstVelocity, pair.secondVelocity});
	}
	if (EntityManager::IsAlive(pair.second)) {
		hits.push_back({pair.second, pair.first, phase, pair.isTrigger, -pair.normal, pair.secondVelocity, pair.firstVelocity});
	}
}

// 判定に使う形を集め直す（有効なEntityの、有効な Collider だけ）。Update と SyncWorld で共通
void CollectColliders() {
	state->colliders.clear();
	EntityManager::ForEach<ColliderComponent>([](Handle<Entity> entity, const ColliderComponent& collider) {
		if (!collider.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		state->colliders.push_back(CollisionSystem::CalcWorldCollider(entity, collider, *transform));
	});
}

// このEntityが、今重なっているか（当たり始め・当たっている。離れた＝Exit は入れない）
bool IsTouching(Handle<Entity> entity) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		if (hit.phase != HitPhase::Exit) {
			return true;
		}
	}
	return false;
}

// ===== 線（ギズモ）=====
void PushBox(std::vector<Renderer::LineSegment>& lines, const AABB& box, const Vector4& color) {
	const std::array<Vector3, 8> corners = box.GetCorners();
	for (const auto& [from, to] : kBoxEdges) {
		lines.push_back({corners[from], corners[to], color});
	}
}

// ===== 多角形：前の面と後ろの面の外周、点ごとに前から後ろへの線 =====
void PushPolygon(std::vector<Renderer::LineSegment>& lines, const ExtrudedPolygon& polygon, const Vector4& color) {
	const std::span<const Vector2> points = polygon.GetPoints();
	const Vector3& origin = polygon.origin;
	const float frontZ = origin.z - polygon.halfDepth;
	const float backZ = origin.z + polygon.halfDepth;
	for (size_t i = 0; i < points.size(); ++i) {
		const Vector2& a = points[i];
		const Vector2& b = points[(i + 1) % points.size()];
		for (const float z : {frontZ, backZ}) {
			lines.push_back({
			    {origin.x + a.x, origin.y + a.y, z},
                {origin.x + b.x, origin.y + b.y, z},
                color
            });
		}
		lines.push_back({
		    {origin.x + a.x, origin.y + a.y, frontZ},
            {origin.x + a.x, origin.y + a.y, backZ },
            color
        });
	}
}

// ===== Inspector と 保存 =====
// Collider の見せ方。Inspector とシーンファイルの両方がこの並びを使う（COMPONENT(...) の中身と同じ書き方）
// 保存の名前はエンジンの他の型にそろえてメンバ名にし、Inspector には「表示###保存の名前」で読みやすい名前を出す
void DescribeCollider(ComponentUI& ui, ColliderComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Is Trigger###isTrigger", value.isTrigger, Tip("すり抜ける（Rigidbody を押し戻さない）。Script には OnTrigger〜、外すと OnCollision〜"));
	ui.Field("Shape###shape", value.shape);
	ui.Field("Center###center", value.center, Tip("Entityの位置からのずれ（Entityの回転と大きさも掛かる）"));
	ui.AssetField("Physic Material###physicMaterial", value.physicMaterial, AssetType::PhysicMaterial, Tip("跳ね返り・摩擦（.physmat のファイル。Project の Create → Physic Material で作る）。(none) なら跳ね返らず、摩擦は 0.6（Unity の既定と同じ）"));
	// 形に合う大きさだけを見せる（保存も、使っている方だけが書かれる）
	switch (value.shape) {
	case ColliderShape::Sphere:
		ui.Field("Radius###radius", value.radius, AtLeast(0.0f).Tip("Entityの大きさの、一番大きい軸を掛けて判定する"));
		break;
	case ColliderShape::Box:
		ui.Field("Size###size", value.size, AtLeast(0.0f).Tip("幅・高さ・奥行き。Entityを回しても、箱は回らない"));
		break;
	case ColliderShape::Capsule:
		ui.Field("Radius###radius", value.radius, AtLeast(0.0f).Tip("Entityの大きさの、X と Z の大きい方を掛けて判定する"));
		ui.Field("Height###height ", value.height, AtLeast(0.0f).Tip("下の端から上の端まで（半球も入れる）。Entity の Y の大きさを掛ける。半径の2倍より小さいと球になる。Entityを回しても、カプセルは立ったまま"));
		break;
	case ColliderShape::Polygon:
		ui.Label("形は、同じ Entity の Polygon Shape の点を使う（無ければ当たらない）。当たるのは球・カプセルだけ（箱・多角形どうしは当たらない）");
		break;
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void CollisionSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<CollisionState>();
	// 線は距離でフェードさせない
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;
	state->gizmoLines.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く

	// Collider の置き場所・保存・Inspector を登録する。見せ方は DescribeCollider 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<ColliderComponent>();
	ComponentSerializer::Register<ColliderComponent>("Collider", &DescribeCollider);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ColliderComponent>>("Collider", "Physics", &DescribeCollider, "当たり判定の形と大きさ。重なった・ぶつかったは Script の OnTrigger〜・OnCollision〜 で受ける。止まるのは Rigidbody・Character Controller を付けた物"));
}

void CollisionSystem::Release() { state.reset(); }


//=============================================================================
// 判定
//=============================================================================
void CollisionSystem::Update() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");

	// ===== 1. 判定に使う形を集める（有効なEntityの、有効な Collider だけ）=====
	CollectColliders();

	// ===== 2. 全部の組を調べる（総当たり。n個なら n×(n-1)/2 回。数百個まではこれで足りる）=====
	// 触れているだけの物も当たっている扱い（IsInContact）。どちらかが Is Trigger なら、すり抜ける当たり
	// 親子の Collider 同士は当たらない（自分の子に付けた剣の Collider で、自分に当たり続けないように）
	std::vector<HitPair>& current = state->currentPairs;
	current.clear();
	const std::vector<WorldCollider>& colliders = state->colliders;
	for (size_t i = 0; i < colliders.size(); ++i) {
		for (size_t j = i + 1; j < colliders.size(); ++j) {
			if (EntityManager::IsRelated(colliders[i].entity, colliders[j].entity)) {
				continue;
			}
			Vector3 normal; // j から i へ押し出す向き
			if (CalcContact(colliders[i], colliders[j], &normal)) {
				const Handle<Entity> a = colliders[i].entity;
				const Handle<Entity> b = colliders[j].entity;
				current.push_back(MakePair(a, b, colliders[i].isTrigger || colliders[j].isTrigger, normal, VelocityOf(a), VelocityOf(b)));
			}
		}
	}

	// ===== 3. 物理でぶつかった組を混ぜる（PhysicsSystem が ReportImpact で知らせた物）=====
	// 重なっている組は、向き・速さを「ぶつかった瞬間の物」に置き換える（今の速さは、押し戻した後の速さなので）
	// 跳ね返って、このフレームのうちに離れた組も足す（足さないと、触れていたのが一瞬なので、当たり始めを知らせ損ねる）
	for (const HitPair& impact : state->impacts) {
		const auto found = std::ranges::find_if(current, [&impact](const HitPair& pair) { return IsSamePair(pair, impact) && !pair.isTrigger; });
		if (found != current.end()) {
			*found = impact;
		} else if (EntityManager::IsAlive(impact.first) && EntityManager::IsAlive(impact.second)) {
			current.push_back(impact); // 消えたEntityとの組は足さない（物理の後に消えた）
		}
	}
	state->impacts.clear();
	std::ranges::sort(current, PairLess);

	// ===== 4. 前のフレームの組と見比べて、当たり始め・当たっている・離れた に分ける =====
	// どちらの列も同じ順に並べてあるので、先頭から1つずつ見比べれば「両方にある」「片方にしか無い」が分かる
	const std::vector<HitPair>& previous = state->previousPairs;
	std::vector<CollisionHit>& hits = state->hits;
	hits.clear();
	size_t c = 0; // current の何番目を見ているか
	size_t p = 0; // previous の何番目を見ているか
	while (c < current.size() || p < previous.size()) {
		if (p == previous.size() || (c < current.size() && PairLess(current[c], previous[p]))) {
			// 今だけにある ＝ 当たり始めた
			AddHits(hits, current[c], HitPhase::Enter);
			++c;
		} else if (c == current.size() || PairLess(previous[p], current[c])) {
			// 前だけにある ＝ 離れた（Colliderを外した・無効にした・Entityを消した、も含む）
			AddHits(hits, previous[p], HitPhase::Exit);
			++p;
		} else {
			// 両方にある ＝ 当たったまま
			AddHits(hits, current[c], HitPhase::Stay);
			++c;
			++p;
		}
	}
	// 自分（self）の順に並べ直す。GetHits が、自分の分だけを二分探索で切り出せるように
	std::ranges::stable_sort(hits, HandleLess, &CollisionHit::self);

	// 今回の組を、次のフレームの「前」にする（入れ替えるだけ。配列のメモリはどちらも使い回す）
	std::swap(state->previousPairs, state->currentPairs);
}

//=============================================================================
// 当たりの一覧
//=============================================================================
std::span<const CollisionHit> CollisionSystem::GetHits(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	// self の順に並んでいるので、自分の分は1か所にまとまっている。その範囲（最初と最後）を二分探索で探す
	const auto range = std::ranges::equal_range(state->hits, entity, HandleLess, &CollisionHit::self);
	return {range.begin(), range.end()};
}

std::span<const CollisionHit> CollisionSystem::GetAllHits() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	return state->hits;
}

//=============================================================================
// 物理でぶつかった組（PhysicsSystem から）
//=============================================================================
void CollisionSystem::ReportImpact(Handle<Entity> a, Handle<Entity> b, const Vector3& normal, const Vector3& velocityA, const Vector3& velocityB) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	const HitPair impact = MakePair(a, b, false, normal, velocityA, velocityB);
	// 物理は小さな1歩ごとに押し戻すので、触れている間は何回も来る。最初の1回（ぶつかる直前の速さ）だけ覚える
	const bool isKnown = std::ranges::any_of(state->impacts, [&impact](const HitPair& known) { return IsSamePair(known, impact); });
	if (!isKnown) {
		state->impacts.push_back(impact);
	}
}

//=============================================================================
// Collider をワールド座標の形にする（判定・押し戻しで共通）
//=============================================================================
WorldCollider CollisionSystem::CalcWorldCollider(Handle<Entity> entity, const ColliderComponent& collider, const TransformComponent& transform) {
	WorldCollider result;
	result.entity = entity;
	result.shape = collider.shape;
	result.isTrigger = collider.isTrigger;
	// 中心：ずれはEntityのローカル座標なので、ワールド行列で変換する（回転と大きさが掛かってから、Entityの位置が足される）
	const Vector3 center = TransformPoint(collider.center, transform.worldMatrix);
	const Vector3 scale = GetWorldScale(transform.worldMatrix);
	switch (collider.shape) {
	case ColliderShape::Sphere: {
		// 球のまま大きくするために、一番大きい軸の大きさを掛ける（(std::max) と括弧で囲むと、windows.h の max マクロにならない）
		const float maxScale = (std::max)({scale.x, scale.y, scale.z});
		result.sphere = {center, std::abs(collider.radius) * maxScale};
		break;
	}
	case ColliderShape::Box: {
		// 箱は「中心から面までの距離（大きさの半分）」を軸ごとに出す。回転は使わない（軸にそろった箱）
		const Vector3 half = {
		    std::abs(collider.size.x) * scale.x * 0.5f,
		    std::abs(collider.size.y) * scale.y * 0.5f,
		    std::abs(collider.size.z) * scale.z * 0.5f,
		};
		result.box = {center - half, center + half};
		break;
	}
	case ColliderShape::Capsule: {
		// 半径には横（X と Z）の大きい方、高さには Y の大きさを掛ける。回転は使わない（いつも立ったまま）
		const float radius = std::abs(collider.radius) * (std::max)(scale.x, scale.z);
		const float height = std::abs(collider.height) * scale.y;
		// 線分の長さの半分 ＝ 高さの半分から、端の半球の分（半径）を引いた物。高さが半径の2倍より小さければ0（球になる）
		const float halfSegment = (std::max)(height * 0.5f - radius, 0.0f);
		const Vector3 offset = {0.0f, halfSegment, 0.0f};
		result.capsule = {center - offset, center + offset, radius};
		break;
	}
	case ColliderShape::Polygon: {
		// 形は同じ Entity の Polygon Shape から（無ければ点 0 個のまま＝何にも当たらない）。囲む箱も作っておく
		if (const PolygonShapeComponent* polygon = EntityManager::Get<PolygonShapeComponent>(entity)) {
			result.polygon = PolygonShapeSystem::CalcWorldPolygon(*polygon, collider.center, transform.worldMatrix);
		} else {
			result.polygon.origin = center;
		}
		result.box = CalcPolygonBounds(result.polygon);
		break;
	}
	}
	return result;
}

//=============================================================================
// カプセルを球に置き換える（判定・押し戻しで共通）
//=============================================================================
void CollisionSystem::ReplaceCapsulesWithSpheres(WorldCollider& a, WorldCollider& b) {
	const bool aIsCapsule = (a.shape == ColliderShape::Capsule);
	const bool bIsCapsule = (b.shape == ColliderShape::Capsule);
	// --- 両方カプセル：2本の線分の、お互いに一番近い点 ---
	if (aIsCapsule && bIsCapsule) {
		Vector3 onA;
		Vector3 onB;
		Collision::ClosestPointsBetweenSegments(a.capsule.start, a.capsule.end, b.capsule.start, b.capsule.end, &onA, &onB);
		a = ToSphere(a, onA);
		b = ToSphere(b, onB);
		return;
	}
	// --- 片方だけ：相手（球・箱）に一番近い点 ---
	if (aIsCapsule) {
		a = ToSphere(a, NearestPointOnCapsule(a.capsule, b));
	}
	if (bIsCapsule) {
		b = ToSphere(b, NearestPointOnCapsule(b.capsule, a));
	}
}

//=============================================================================
// 押し出すずれ（判定・当たりの向き・押し戻しで共通）
//=============================================================================
bool CollisionSystem::CalcPushOut(WorldCollider a, WorldCollider b, Collision::AxisLock lock, Vector3* push) {
	// カプセル × 多角形：線分の上の候補から一番深い所を探す（囲む箱で1点だけ選ぶと、坂に立つカプセルの足元を見落とす）
	// 囲む箱にも触れていなければ、辺を1本ずつ調べなくても当たらない
	if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::Polygon) {
		return Collision::CapsuleAABB(a.capsule, b.box) && Collision::PushOutCapsulePolygon(a.capsule, b.polygon, push, lock);
	}
	if (a.shape == ColliderShape::Polygon && b.shape == ColliderShape::Capsule) {
		return Collision::CapsuleAABB(b.capsule, a.box) && Collision::PushOutPolygonCapsule(a.polygon, b.capsule, push, lock);
	}
	// カプセルは、相手に一番近い所の球に置き換える（その球を出す量だけ、カプセルも出す）。ここから下は球・箱・多角形だけ
	ReplaceCapsulesWithSpheres(a, b);
	// 形の組み合わせ（a × b）で、使う関数を選ぶ
	switch (a.shape) {
	case ColliderShape::Sphere:
		switch (b.shape) {
		case ColliderShape::Sphere:
			return Collision::PushOutSphereSphere(a.sphere, b.sphere, push, lock);
		case ColliderShape::Box:
			return Collision::PushOutSphereAABB(a.sphere, b.box, push, lock);
		case ColliderShape::Polygon:
			// 囲む箱にも触れていなければ、辺を1本ずつ調べなくても当たらない（足場が多くても軽く済む）
			return Collision::SphereAABB(a.sphere, b.box) && Collision::PushOutSpherePolygon(a.sphere, b.polygon, push, lock);
		case ColliderShape::Capsule:
			break; // 置き換えたので来ない
		}
		break;
	case ColliderShape::Box:
		switch (b.shape) {
		case ColliderShape::Sphere:
			return Collision::PushOutAABBSphere(a.box, b.sphere, push, lock);
		case ColliderShape::Box:
			return Collision::PushOutAABBAABB(a.box, b.box, push, lock);
		case ColliderShape::Polygon:
			return false; // 箱 × 多角形は作っていない（当たらない）
		case ColliderShape::Capsule:
			break; // 置き換えたので来ない
		}
		break;
	case ColliderShape::Polygon:
		switch (b.shape) {
		case ColliderShape::Sphere:
			return Collision::SphereAABB(b.sphere, a.box) && Collision::PushOutPolygonSphere(a.polygon, b.sphere, push, lock);
		case ColliderShape::Box:
		case ColliderShape::Polygon:
			return false; // 多角形 × 箱・多角形 × 多角形は作っていない（当たらない）
		case ColliderShape::Capsule:
			break; // 置き換えたので来ない
		}
		break;
	case ColliderShape::Capsule:
		break; // 置き換えたので来ない
	}
	HTT_ASSERT_MSG(false, "カプセルが球に置き換わっていません");
	return false;
}

//=============================================================================
// その場で調べる（レイキャスト・スイープ）
//=============================================================================
namespace {
// 調べる相手か（生きていて、絞り込みに合う）
// state->colliders は前のフレームに集めた物なので、作り直しで消えたEntityが残っていることがある
bool IsQueryTarget(const WorldCollider& collider, const CollisionFilter& filter) {
	if (!EntityManager::IsAlive(collider.entity)) {
		return false;
	}
	return !filter || filter(collider.entity);
}

// ===== レイ（向きは長さ1）が Collider に当たるか。当たったら、始点からの距離を t に入れる =====
bool RayCollider(const Ray& ray, const WorldCollider& collider, float& t) {
	switch (collider.shape) {
	case ColliderShape::Sphere:
		// RaySphere は「近い方の交点」を返すので、始点が中のとき・球が後ろにあるときは t がマイナスになる
		if (Collision::IsPointInSphere(ray.origin, collider.sphere)) {
			t = 0.0f;
			return true;
		}
		return Collision::RaySphere(ray, collider.sphere, &t) && t >= 0.0f;
	case ColliderShape::Box:
		return Collision::RayAABB(ray, collider.box, &t); // 始点が箱の中なら t=0
	case ColliderShape::Capsule:
		return Collision::RayCapsule(ray, collider.capsule, &t); // 始点がカプセルの中なら t=0
	case ColliderShape::Polygon:
		return Collision::RayPolygon(ray, collider.polygon, &t); // 始点が中なら t=0
	}
	return false;
}

// ===== 形を動かして調べる（Cast・Overlap）の道具 =====
constexpr float kMinCastLength = 1e-6f; // これより短い動きは調べない（m）
constexpr float kCastStepRatio = 0.5f;  // 1回に進める長さ ＝ 動かす形の一番細い所の半分の太さ × これ（細かいほど、かすっただけの物も見つける）
constexpr float kMinCastStep = 0.01f;   // 1回に進める長さの下限（m。とても細い形でも、回数が増えすぎないように）
constexpr int kCastBisectCount = 12;    // 当たった区間を半分にする回数（12 回で、1回に進める長さの 4096 分の1まで狭まる）

// 形を囲む箱（調べる前に「近くにも無い物」を外すのに使う）
AABB CalcBounds(const WorldCollider& collider) {
	switch (collider.shape) {
	case ColliderShape::Sphere: {
		const Vector3 half = {collider.sphere.radius, collider.sphere.radius, collider.sphere.radius};
		return {collider.sphere.center - half, collider.sphere.center + half};
	}
	case ColliderShape::Box:
		return collider.box;
	case ColliderShape::Capsule: {
		const Capsule& capsule = collider.capsule;
		const Vector3 half = {capsule.radius, capsule.radius, capsule.radius};
		const Vector3 low = {(std::min)(capsule.start.x, capsule.end.x), (std::min)(capsule.start.y, capsule.end.y), (std::min)(capsule.start.z, capsule.end.z)};
		const Vector3 high = {(std::max)(capsule.start.x, capsule.end.x), (std::max)(capsule.start.y, capsule.end.y), (std::max)(capsule.start.z, capsule.end.z)};
		return {low - half, high + half};
	}
	case ColliderShape::Polygon: {
		// box は点だけを囲む箱なので、太らせた量（radius）を足す
		const Vector3 half = {collider.polygon.radius, collider.polygon.radius, collider.polygon.radius};
		return {collider.box.min - half, collider.box.max + half};
	}
	}
	return collider.box;
}

// 2つの箱を両方とも囲む箱
AABB MergeBounds(const AABB& a, const AABB& b) {
	return {
	    {(std::min)(a.min.x, b.min.x), (std::min)(a.min.y, b.min.y), (std::min)(a.min.z, b.min.z)},
	    {(std::max)(a.max.x, b.max.x), (std::max)(a.max.y, b.max.y), (std::max)(a.max.z, b.max.z)},
	};
}

// 形の一番細い所の、半分の太さ（Cast で1回に進める長さを決める。これより短く進めれば、壁を飛び越えない）
float CalcHalfThickness(const WorldCollider& collider) {
	switch (collider.shape) {
	case ColliderShape::Sphere:
		return collider.sphere.radius;
	case ColliderShape::Capsule:
		return collider.capsule.radius;
	case ColliderShape::Box:
	case ColliderShape::Polygon: {
		const AABB bounds = CalcBounds(collider);
		const Vector3 size = bounds.max - bounds.min;
		return (std::min)({size.x, size.y, size.z}) * 0.5f;
	}
	}
	return 0.0f;
}

// 形の表面で、direction（長さ1）の向きに一番出ている所（当たった所のだいたいの位置に使う）
Vector3 CalcFarthestPoint(const WorldCollider& collider, const Vector3& direction) {
	switch (collider.shape) {
	case ColliderShape::Sphere:
		return collider.sphere.center + direction * collider.sphere.radius;
	case ColliderShape::Capsule: {
		// 線分の端のうち、その向きに出ている方（同じくらいなら真ん中）から、半径だけ先
		const Capsule& capsule = collider.capsule;
		const float startSide = Dot(capsule.start, direction);
		const float endSide = Dot(capsule.end, direction);
		const Vector3 base = (std::abs(startSide - endSide) < 1e-4f) ? (capsule.start + capsule.end) * 0.5f : ((startSide > endSide) ? capsule.start : capsule.end);
		return base + direction * capsule.radius;
	}
	case ColliderShape::Box:
	case ColliderShape::Polygon: {
		// 箱の角のうち、その向きに一番出ている物（多角形は囲む箱で、だいたい）
		const AABB bounds = CalcBounds(collider);
		return {
		    (direction.x >= 0.0f) ? bounds.max.x : bounds.min.x,
		    (direction.y >= 0.0f) ? bounds.max.y : bounds.min.y,
		    (direction.z >= 0.0f) ? bounds.max.z : bounds.min.z,
		};
	}
	}
	return collider.sphere.center;
}

// 形を動かして調べる相手か（生きている・Is Trigger の扱い・自分と親子でない・絞り込みに合う）
bool IsShapeQueryTarget(const WorldCollider& collider, const WorldCollider& shape, const ShapeQuery& query) {
	if (!EntityManager::IsAlive(collider.entity) || (collider.isTrigger && !query.includeTriggers)) {
		return false;
	}
	if (shape.entity.IsValid() && EntityManager::IsRelated(shape.entity, collider.entity)) {
		return false; // 自分の形・自分の子に付けた形（持っている剣など）
	}
	return !query.filter || query.filter(collider.entity);
}

// shape を motion だけ動かしたとき、other に最初に触れる割合（0〜limit）を探す。limit までに触れなければ false
//   1. stepFraction ずつ進めて、重なったら止める（1回に進める長さは形の半分の太さより短いので、壁を飛び越えない）
//   2. 最後に重ならなかった割合と、重なった割合の間を、半分ずつ狭める（二分探索）
//   3. 向きは、重なった所で外へ出すずれの向き（CalcPushOut の向き＝当たった面の向き）
bool CastAgainst(const WorldCollider& shape, const Vector3& motion, const WorldCollider& other, float stepFraction, float limit, Collision::AxisLock lock, float* fraction,
                 Vector3* normal) {
	Vector3 push;
	// --- 動かす前から重なっている：近づく向きなら、そこで止める。離れる向きなら止めない（中から出られるように）---
	if (CollisionSystem::CalcPushOut(shape, other, lock, &push)) {
		if (Dot(push, motion) >= 0.0f) {
			return false;
		}
		*fraction = 0.0f;
		*normal = Normalize(push);
		return true;
	}
	// --- 1. 少しずつ進めて、重なる所を探す ---
	float freeFraction = 0.0f; // ここまでは重ならない
	while (freeFraction < limit) {
		const float next = (std::min)(freeFraction + stepFraction, limit);
		WorldCollider moved = shape;
		moved.Translate(motion * next);
		if (!CollisionSystem::CalcPushOut(moved, other, lock, &push)) {
			freeFraction = next;
			continue;
		}
		// --- 2. 重ならない所と重なる所の間を、半分ずつ狭める ---
		float hitFraction = next;
		for (int i = 0; i < kCastBisectCount; ++i) {
			const float middle = (freeFraction + hitFraction) * 0.5f;
			WorldCollider probe = shape;
			probe.Translate(motion * middle);
			Vector3 middlePush;
			if (CollisionSystem::CalcPushOut(probe, other, lock, &middlePush)) {
				hitFraction = middle;
				push = middlePush;
			} else {
				freeFraction = middle;
			}
		}
		// --- 3. 重ならない方の割合まで進めてよい。向きは重なった所の押し出す向き ---
		*fraction = freeFraction;
		*normal = Normalize(push);
		return true;
	}
	return false;
}
} // namespace


bool CollisionSystem::Raycast(const Ray& ray, float maxDistance, RaycastHit& hit, const CollisionFilter& filter) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	// 向きを長さ1にする（t がそのまま距離になるように）
	const float length = Length(ray.direction);
	if (length < 1e-6f) {
		return false; // 向きが無い
	}
	const Ray unitRay = {ray.origin, ray.direction / length};

	// 全部の Collider を調べて、一番近い物を残す（総当たり。Update と同じく数百個まではこれで足りる）
	bool isHit = false;
	float nearest = maxDistance;
	for (const WorldCollider& collider : state->colliders) {
		if (!IsQueryTarget(collider, filter)) {
			continue;
		}
		float t = 0.0f;
		if (!RayCollider(unitRay, collider, t) || t > nearest) {
			continue;
		}
		nearest = t;
		isHit = true;
		hit.entity = collider.entity;
	}
	if (isHit) {
		hit.distance = nearest;
		hit.point = Collision::RayPoint(unitRay, nearest);
	}
	return isHit;
}

void CollisionSystem::SweepSphere(const Capsule& capsule, std::vector<Handle<Entity>>& out, const CollisionFilter& filter) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	out.clear();
	// 動いた跡をカプセルの Collider にして、Update と同じ IsOverlapping で調べる（相手が球・箱・カプセルのどれでも同じ書き方）
	WorldCollider sweep;
	sweep.shape = ColliderShape::Capsule;
	sweep.capsule = capsule;
	for (const WorldCollider& collider : state->colliders) {
		if (!IsQueryTarget(collider, filter)) {
			continue;
		}
		if (IsOverlapping(sweep, collider)) {
			out.push_back(collider.entity);
		}
	}
}

bool CollisionSystem::Cast(const WorldCollider& shape, const Vector3& motion, ShapeCastHit& hit, const ShapeQuery& query) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	const float length = Length(motion);
	if (length < kMinCastLength) {
		return false; // 動かない
	}
	// 動いた跡を囲む箱（動く前と後の形を両方囲む）。これに触れない物は調べない
	WorldCollider end = shape;
	end.Translate(motion);
	const AABB sweptBounds = MergeBounds(CalcBounds(shape), CalcBounds(end));
	// 1回に進める割合（形の半分の太さ × kCastStepRatio を、動きの長さで割る）
	const float stepFraction = (std::max)(CalcHalfThickness(shape) * kCastStepRatio, kMinCastStep) / length;

	// 全部の相手を調べて、一番早く触れる物を残す（総当たり。Update と同じく数百個まではこれで足りる）
	// 2つ目からは「今までで一番早い割合」までしか調べない（それより後に触れる物は要らない）
	bool isHit = false;
	float nearest = 1.0f;
	for (const WorldCollider& collider : state->colliders) {
		if (!IsShapeQueryTarget(collider, shape, query) || !Collision::AABBAABB(sweptBounds, CalcBounds(collider))) {
			continue;
		}
		float fraction = 0.0f;
		Vector3 normal;
		if (!CastAgainst(shape, motion, collider, stepFraction, nearest, query.lock, &fraction, &normal)) {
			continue;
		}
		if (isHit && fraction >= nearest) {
			continue; // 同じ割合なら、先に見つけた方
		}
		isHit = true;
		nearest = fraction;
		hit.entity = collider.entity;
		hit.normal = normal;
	}
	if (isHit) {
		hit.fraction = nearest;
		hit.distance = nearest * length;
		// 止まった所での、相手に向いた表面の点（相手は normal の反対の向きにある）
		WorldCollider stopped = shape;
		stopped.Translate(motion * nearest);
		hit.point = CalcFarthestPoint(stopped, -hit.normal);
	}
	return isHit;
}

void CollisionSystem::Overlap(const WorldCollider& shape, std::vector<ShapeOverlap>& out, const ShapeQuery& query) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	out.clear();
	const AABB bounds = CalcBounds(shape);
	for (const WorldCollider& collider : state->colliders) {
		if (!IsShapeQueryTarget(collider, shape, query) || !Collision::AABBAABB(bounds, CalcBounds(collider))) {
			continue;
		}
		Vector3 push;
		if (CalcPushOut(shape, collider, query.lock, &push)) {
			out.push_back({collider.entity, push});
		}
	}
}

//=============================================================================
// 調べる相手の形を、今の位置に合わせる（物を動かす係から）
//=============================================================================
void CollisionSystem::SyncWorld() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	CollectColliders();
}

void CollisionSystem::MoveCollider(Handle<Entity> entity, const Vector3& move) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	// 子に付けた形も、Transform では一緒に動くので、ここでも一緒にずらす
	for (WorldCollider& collider : state->colliders) {
		if (collider.entity == entity || EntityManager::IsDescendantOf(collider.entity, entity)) {
			collider.Translate(move);
		}
	}
}

//=============================================================================
// ギズモ
//=============================================================================
void CollisionSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	if (!state->showGizmos) {
		return;
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear(); // 中身だけ消す。確保済みのメモリは残るので、次から再確保が起きにくい
	gizmo.camera = camera;
	// 判定に使った形をそのまま描く（Update が集めた物。無効な Collider・無効なEntityは入っていない）
	for (const WorldCollider& collider : state->colliders) {
		const Vector4 idleColor = collider.isTrigger ? kTriggerColor : kIdleColor;
		const Vector4 color = IsTouching(collider.entity) ? kHitColor : idleColor;
		switch (collider.shape) {
		case ColliderShape::Sphere:
			GizmoLines::PushWireSphere(gizmo.lines, collider.sphere.center, collider.sphere.radius, color);
			break;
		case ColliderShape::Box:
			PushBox(gizmo.lines, collider.box, color);
			break;
		case ColliderShape::Capsule:
			GizmoLines::PushWireCapsule(gizmo.lines, collider.capsule.start, collider.capsule.end, collider.capsule.radius, color);
			break;
		case ColliderShape::Polygon:
			PushPolygon(gizmo.lines, collider.polygon, color);
			break;
		}
	}
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}

bool& CollisionSystem::GetShowGizmos() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	return state->showGizmos;
}