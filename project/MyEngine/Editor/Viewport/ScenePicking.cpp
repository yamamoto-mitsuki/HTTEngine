#ifdef USE_IMGUI
#include "ScenePicking.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>

#include "MyEngine/Camera/Camera.h"
#include "MyEngine/Collision/Collision.h"
#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/Components/Lighting/LightComponent.h"
#include "MyEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "MyEngine/Components/Rendering3D/PolygonRendererComponent.h"
#include "MyEngine/Components/Rendering3D/PolygonShapeComponent.h"
#include "MyEngine/Components/Rendering2D/SpriteRendererComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Gizmos/LightGizmo.h"
#include "MyEngine/Editor/Gizmos/CameraGizmo.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Systems/Physics/CollisionSystem.h"
#include "MyEngine/Systems/Rendering3D/PolygonShapeSystem.h"
#include "MyEngine/Systems/Rendering2D/SpriteRenderSystem.h"

namespace {
constexpr float kIconRadius = 0.5f;        // ライト・カメラを指せる球の半径
constexpr float kParallerEpsilon = 1e-12f; // レイと三角形がこれより平行に近ければ、当たらなかったことにする

/// <summary>
/// 一番手前の当たりを覚えておく入れ物
/// </summary>
struct ClosestHit {
	Handle<Entity> entity;    // 当たった物（まだなければ無効）
	float distance = FLT_MAX; // レイの始点からの距離

	// もっと手前なら入れ替える。Hierarchy の目で隠した物は選ばない（見えない物をクリックで選ばないように）
	// 画面（UI Screen）の編集中は、画面の中の物だけ選ぶ（後ろのシーンは見えているが、触らない）
	void Consider(Handle<Entity> candidate, float candidateDistance) {
		if (!SceneVisibility::IsPickable(candidate)) {
			return;
		}
		if (SceneVisibility::IsHidden(candidate)) {
			return;
		}
		if (candidateDistance >= 0.0f && candidateDistance < distance) {
			entity = candidate;
			distance = candidateDistance;
		}
	}
};

//=============================================================================
// レイと三角形（Möller–Trumbore法）
//=============================================================================
// 三角形の中の点は a + u(b - a) + v(c - a) （u ≥ 0、v ≥ 0、u + v ≤ 1）と書ける
// それがレイの上の点 origin + t * direction と同じになる t・u・vを、外積と内積だけで解く（連立方程式を）
// 表・裏のどちらから当たっても当たりにする（裏を描かないモデルでも、中から見ている時に選べるように）
bool RayTriangle(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c, float& t) { 
	// 頂点aから頂点b,頂点cへ向かう2本の辺ベクトルを求める
	const Vector3 edge1 = b - a;
	const Vector3 edge2 = c - a;

	// レイの方向とedge2の外積を求める
	const Vector3 p = Cross(ray.direction, edge2);
	// edge1とpの内積を計算する。三角形の向きとレイの方向の関係を表す値
	// 0に近い場合、レイは三角形の面とほぼ平行。正負によって表側・裏側からの交差も区別できる
	const float determinant = Dot(edge1, p);
	
	// レイが三角形の面と平行なら抜ける
	if (std::abs(determinant) < kParallerEpsilon) {
		return false;
	}
	const float inverse = 1.0f / determinant; // 逆数
	// レイの始点から三角形の頂点aまでのベクトルを求める
	const Vector3 s = ray.origin - a;
	// 三角形上の位置を表す係数uを求める。u < 0、1 < u なら三角形の範囲外
	const float u = Dot(s, p) * inverse;
	if (u < 0.0f || u > 1.0f) {
		return false;
	}
	// sとedge1の外積を求める
	const Vector3 q = Cross(s, edge1);
	// 三角形上の位置を表す係数vを求める。vが0未満、またはu + vが1を超える場合は三角形の外側。
	const float v = Dot(ray.direction, q) * inverse;
	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}

	// レイ上の位置を表す係数tを求める。
	t = Dot(edge2, q) * inverse;
	
	// tが0以上なら、交点はレイの始点より先にある。
	return t >= 0.0f;
}

// ===== 頂点の位置（Vertex3dDataは w付きの4つなので、x・y・zだけ取り出す）=====
Vector3 PositionOf(const Vertex3dData& vertex) { return {vertex.position.x, vertex.position.y, vertex.position.z}; }

// ===== レイ（メッシュのローカル座標）がメッシュに当たるか。当たった中で一番近い tを返す =====
bool RaycastMesh(const Ray& ray, const ModelManager::SubMesh& mesh, float& t) { 
	bool isHit = false;
	t = FLT_MAX;
	const auto testTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) { 
		float triangleT = 0.0f;
		if (RayTriangle(ray, PositionOf(mesh.vertices[i0]),PositionOf(mesh.vertices[i1]), PositionOf(mesh.vertices[i2]), triangleT) && 
			triangleT < t) {
			t = triangleT;
			isHit = true;
		}
	};
	// 番号（indices）があれば3つづつ、負ければ頂点を3つづつ
	if (!mesh.indices.empty()) {
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			testTriangle(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
		}
	} else {
		for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(mesh.vertices.size()); i += 3) {
			testTriangle(i, i + 1, i + 2);
		}
	}
	return isHit;
}

// ===== 行列が数として正しいか（大きさ0の物の逆行列は、数でなくなる）=====
bool IsFinite(const Matrix4x4& m) {
	for (const auto& row : m.m) {
		for (const float value : row) {
			if (!std::isfinite(value)) {
				return false;
			}
		}
	}
	return true;
}

//=============================================================================
// 種類ごとの当たり
//=============================================================================
// ===== Model Renderer: メッシュの三角形1枚づつ =====
void PickModels(const Ray& ray, ClosestHit& closest) { 
	EntityManager::ForEach<ModelRendererComponent>([&](Handle<Entity> entity, const ModelRendererComponent& renderer) { 
		// --- 早期リターン ---
		// ModelComponentを持っているか、モデルを持っているか
		if (!renderer.enabled || renderer.modelHandle == 0 || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);      // Transformを取得
		const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(renderer.modelHandle); // 参照しているモデル
		// Transform,modelHandleを取得出来ているか
		if (transform == nullptr || asset == nullptr) {
			return;
		}

		for (const ModelManager::Node& node : asset->nodes) {
			if (node.meshIndices.empty()) {
				continue; // このノードだけ飛ばす
			}
			for (const uint32_t meshIndex : node.meshIndices) {
				const ModelManager::SubMesh& mesh = asset->meshes[meshIndex];
				// メッシュを置く行列（スキンのあるメッシュはノードの行列を掛けない。描くときと同じ ModelManager::MeshMatrix）
				const Matrix4x4 inverse = Inverse(ModelManager::MeshMatrix(mesh, node.worldMatrix) * transform->worldMatrix);
				if (!IsFinite(inverse)) {
					continue; // 大きさが0（見えない）
				}
				// レイをメッシュのローカル座標へ直す。向きは長さ1に戻さない
				const Ray localRay = {TransformPoint(ray.origin, inverse), TransformNormal(ray.direction, inverse)};
				float t = 0.0f;
				if (RaycastMesh(localRay, mesh, t)) {
					closest.Consider(entity, t);
				}
			}
		}
	});
}

// ===== Sprite Renderer: 板（三角形2枚）=====
void PickSprites(const Ray& ray, const Camera& camera, ClosestHit& closest) {
	EntityManager::ForEach<SpriteRendererComponent>([&](Handle<Entity> entity, const SpriteRendererComponent&) {
		PickBounds bounds;
		if (!SpriteRenderSystem::CalcSpritePlacement(entity, &camera, bounds.local, bounds.matrix)) {
			return;
		}
		const std::array<Vector3, 8> corners = bounds.GetWorldCorners(); // z は min も max も0なので、0〜3 が板の4つの角（左下・右下・左上・右上）
		float t = 0.0f;
		if (RayTriangle(ray, corners[0], corners[1], corners[2], t) || RayTriangle(ray, corners[1], corners[3], corners[2], t)) {
			closest.Consider(entity, t);
		}
	});
}

// ===== Polygon Renderer：見た目の形（多角形を押し出した形）。当たりの Collider と同じ計算（Collision::RayPolygon）を使う =====
void PickPolygons(const Ray& ray, ClosestHit& closest) {
	EntityManager::ForEach<PolygonRendererComponent>([&](Handle<Entity> entity, const PolygonRendererComponent& renderer) {
		if (!renderer.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const PolygonShapeComponent* shape = EntityManager::Get<PolygonShapeComponent>(entity);
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (shape == nullptr || transform == nullptr) {
			return;
		}
		float t = 0.0f;
		if (Collision::RayPolygon(ray, PolygonShapeSystem::CalcWorldPolygon(*shape, {0.0f, 0.0f, 0.0f}, transform->worldMatrix), &t)) {
			closest.Consider(entity, t);
		}
	});
}

// ===== ライト・カメラ：形が無いので、置いてある所（アイコンの所）の小さい球 =====
// その種類の Component を持つ Entity を、球で調べる。showIcon が false の物（アイコンを出していない物）は飛ばす
// カメラも Transform の位置（Camera Path で写す位置が変わっても、ギズモと同じ所）
template<class T>
void PickIcons(const Ray& ray, ClosestHit& closest, bool (*showIcon)(const T&)) {
	EntityManager::ForEach<T>([&](Handle<Entity> entity, const T& component) {
		if (!showIcon(component) || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		const Sphere sphere = {GetWorldPosition(*transform), kIconRadius};
		float t = 0.0f;
		if (Collision::RaySphere(ray, sphere, &t)) {
			closest.Consider(entity, t);
		}
	});
}

// ===== ライトのアイコンを出しているか =====
template<class T> bool IsLightIconShown(const T& light) { return LightGizmo::GetGlobalFlags().showIcon && light.gizmo.showIcon; }

// ===== カメラのアイコンを出しているか =====
bool IsCameraIconShown(const CameraComponent&) { return CameraGizmo::GetShowIcons(); }

// ===== Collider：線を出している時だけ。見た目の無い物（透明な壁など）を選べるように =====
void PickColliders(const Ray& ray, ClosestHit& closest) {
	if (!CollisionSystem::GetShowGizmos()) {
		return;
	}
	// モデル・板・多角形を持つ物は、見た目で調べ済み（大きな Collider が手前の物を隠さないように除く）
	const auto hasNoVisual = [](Handle<Entity> entity) {
		return EntityManager::Get<ModelRendererComponent>(entity) == nullptr && EntityManager::Get<SpriteRendererComponent>(entity) == nullptr &&
		       EntityManager::Get<PolygonRendererComponent>(entity) == nullptr;
	};
	RaycastHit hit;
	if (CollisionSystem::Raycast(ray, closest.distance, hit, hasNoVisual)) {
		closest.Consider(hit.entity, hit.distance);
	}
}
} // namespace

// ===== 箱の8つの角を、ワールド座標で返す =====
std::array<Vector3, 8> PickBounds::GetWorldCorners() const {
	std::array<Vector3, 8> corners = local.GetCorners();
	for (Vector3& corner : corners) {
		corner = TransformPoint(corner, matrix);
	}
	return corners;
}


//=============================================================================
// 一番手前の Entity
//=============================================================================
Handle<Entity> ScenePicking::Pick(const Ray& ray, const Camera& camera) {
	// 向きを長さ1にしておく（距離 t を、どの種類でも同じ「ワールドの長さ」で比べられるように）
	const Ray worldRay = {ray.origin, Normalize(ray.direction)};
	if (LengthSq(worldRay.direction) == 0.0f) {
		return {};
	}
	ClosestHit closest;
	PickModels(worldRay, closest);
	PickSprites(worldRay, camera, closest);
	PickPolygons(worldRay, closest);
	PickIcons<DirectionalLightComponent>(worldRay, closest, &IsLightIconShown<DirectionalLightComponent>);
	PickIcons<PointLightComponent>(worldRay, closest, &IsLightIconShown<PointLightComponent>);
	PickIcons<SpotLightComponent>(worldRay, closest, &IsLightIconShown<SpotLightComponent>);
	PickIcons<CameraComponent>(worldRay, closest, &IsCameraIconShown);
	PickColliders(worldRay, closest);
	return closest.entity;
}


//=============================================================================
// 見た目の箱
//=============================================================================
void ScenePicking::CollectBounds(Handle<Entity> entity, const Camera& camera, std::vector<PickBounds>& out) {
	// --- モデル：メッシュごとに、頂点が全部入る箱 ---
	const ModelRendererComponent* renderer = EntityManager::Get<ModelRendererComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	// モデルが未選択なら GetModelAsset を呼ばない（呼ぶと「見つかりません」の警告が、選んでいる間ずっと毎フレーム出る）
	const ModelManager::ModelAsset* asset = (renderer != nullptr && renderer->modelHandle != 0) ? ModelManager::GetModelAsset(renderer->modelHandle) : nullptr;
	
	// Transform,modelHandleを持っているか
	if (renderer != nullptr && renderer->enabled && transform != nullptr && asset != nullptr) {
		for (const ModelManager::Node& node : asset->nodes) {
			for (const uint32_t meshIndex : node.meshIndices) {
				const ModelManager::SubMesh& mesh = asset->meshes[meshIndex];
				// 頂点が空ではないか
				if (mesh.vertices.empty()) {
					continue;
				}
				PickBounds bounds;
				bounds.local = {PositionOf(mesh.vertices[0]), PositionOf(mesh.vertices[0])};
				for (const Vertex3dData& vertex : mesh.vertices) {
					const Vector3 p = PositionOf(vertex);
					bounds.local.min = {(std::min)(bounds.local.min.x, p.x), (std::min)(bounds.local.min.y, p.y), (std::min)(bounds.local.min.z, p.z)};
					bounds.local.max = {(std::max)(bounds.local.max.x, p.x), (std::max)(bounds.local.max.y, p.y), (std::max)(bounds.local.max.z, p.z)};
				}
				bounds.matrix = ModelManager::MeshMatrix(mesh, node.worldMatrix) * transform->worldMatrix;
				out.push_back(bounds);
			}
		}
	}
	// --- Sprite：板 ---
	PickBounds sprite;
	if (SpriteRenderSystem::CalcSpritePlacement(entity, &camera, sprite.local, sprite.matrix)) {
		out.push_back(sprite);
	}
	// --- Polygon Shape：点が全部入る箱（奥行きは Depth）---
	const PolygonShapeComponent* polygon = EntityManager::Get<PolygonShapeComponent>(entity);
	if (polygon != nullptr && transform != nullptr && polygon->pointCount > 0) {
		const float halfDepth = std::abs(polygon->depth) * 0.5f;
		PickBounds bounds;
		bounds.local = {
		    {polygon->points[0].x, polygon->points[0].y, -halfDepth},
            {polygon->points[0].x, polygon->points[0].y, halfDepth }
        };
		for (const Vector2& point : polygon->GetPoints()) {
			bounds.local.min = {(std::min)(bounds.local.min.x, point.x), (std::min)(bounds.local.min.y, point.y), -halfDepth};
			bounds.local.max = {(std::max)(bounds.local.max.x, point.x), (std::max)(bounds.local.max.y, point.y), halfDepth};
		}
		bounds.matrix = transform->worldMatrix;
		out.push_back(bounds);
	}
}
#endif