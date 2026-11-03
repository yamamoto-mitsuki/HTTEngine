#ifdef USE_IMGUI
#include "PolylineGizmo.h"

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/PolylineComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Gizmos/PointListGizmo.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Math/Geometry/Polygon.h"
#include "MyEngine/Systems/UI/PolylineSystem.h"

namespace {
constexpr float kGridSize = 1.0f; // 点が吸い付く間隔（ピクセル。Inspector の数字が 150.37 のようにならないように）
} // namespace

//=============================================================================
// 毎フレーム
//=============================================================================
bool PolylineGizmo::Draw(const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	if (!PolylineSystem::IsEditing()) {
		return false;
	}
	// --- 選んでいる物に Polyline が無ければ、編集を終わる（選ぶのをやめた・ほかの物を選んだ）---
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	PolylineComponent* polyline = EntityManager::Get<PolylineComponent>(selected);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(selected);
	if (polyline == nullptr || transform == nullptr) {
		PolylineSystem::SetEditing(false);
		return false;
	}
	// --- 点と角の半径（足す・消すと、半径も一緒にずれる）。塗る形で辺が交差していたら赤（塗りが描かれない形）---
	PointList list;
	list.storage = polyline->points;
	list.values = polyline->radii;
	list.count = &polyline->pointCount;
	list.minCount = kMinPolylinePoints;
	list.isClosed = polyline->closed;
	list.gridSize = kGridSize;
	list.isValid = !polyline->HasFill() || Polygon2D::IsSimple({polyline->points.data(), static_cast<size_t>(polyline->pointCount)});
	list.editor = ComponentEditorRegistry::Find<PolylineComponent>();
	list.component = polyline;
	const PointMapping mapping = PointListGizmo::MakeScreenMapping(transform->worldMatrix, polyline->anchor, imageMin, imageSize);
	if (!PointListGizmo::Draw(selected, list, mapping, imageMin, imageSize, isHovered)) {
		PolylineSystem::SetEditing(false); // Esc で終わった
		return false;
	}
	return true;
}
#endif
