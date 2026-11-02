#include "CameraPath.h"

#include <algorithm>
#include <vector>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"

namespace {
constexpr float kDirectionSpan = 0.25f; // 向きを測る前後の長さ（m）。細かい折れ線の1本で測ると、線の継ぎ目で向きがカクカク変わる

PathCurve line;                       // 今のカメラの線（毎回確保しないように使い回す）
std::vector<Handle<Entity>> children; // 線の Entity の子（使い回す）
std::vector<Vector3> points;          // 線の点（使い回す）
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
void CameraPath::Describe(ComponentUI& ui, CameraPathComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと Transform の位置のまま"));
	ui.Field("Path###path", value.path, Tip("線の Entity（Hierarchy からドラッグ）。子の Entity を上から順に通る。子が 2 つ以上要る"));
	ui.Field("Position###position", value.position, Range(0.0f, 1.0f).Tip("線の上の位置。0：始まり 〜 1：終わり（長さに対する割合）。Timeline のキーで動かす"));
	ui.Field("Rotation###rotation", value.rotation, Tip("向きの決め方。Keep：Transform の向き（Camera Look At があれば注視点を見る） / AlongPath：線の進む向きを見る"));
}

//=============================================================================
// 線
//=============================================================================
void CameraPath::BuildLine(const CameraPathComponent& path, PathCurve& out) {
	points.clear();
	const Handle<Entity> root = EntityManager::Find(path.path);
	if (root.IsValid()) {
		EntityManager::GetChildren(root, children);
		for (const Handle<Entity> child : children) {
			if (const TransformComponent* transform = EntityManager::Get<TransformComponent>(child)) {
				points.push_back(GetWorldPosition(*transform));
			}
		}
	}
	out.Build(points);
}

void CameraPath::Apply(const CameraPathComponent& path, Vector3& position, Quaternion& rotation) {
	if (!path.enabled) {
		return;
	}
	BuildLine(path, line);
	if (line.IsEmpty()) {
		return;
	}
	const float distance = std::clamp(path.position, 0.0f, 1.0f) * line.GetLength();
	position = line.Evaluate(distance).position;
	if (path.rotation == CameraPathRotation::AlongPath) {
		const Vector3 direction = line.Evaluate(distance + kDirectionSpan).position - line.Evaluate(distance - kDirectionSpan).position;
		if (LengthSq(direction) > 0.0f) {
			rotation = MakeLookRotation(direction);
		}
	}
}