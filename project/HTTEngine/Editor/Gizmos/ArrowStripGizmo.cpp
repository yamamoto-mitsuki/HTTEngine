#ifdef USE_IMGUI
#include "ArrowStripGizmo.h"

#include "HTTEngine/Components/Rendering3D/ArrowStripComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Gizmos/PointListGizmo.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Math/Geometry/PathCurve.h"
#include "HTTEngine/Systems/Rendering3D/ArrowStripSystem.h"

namespace {
constexpr int kMinPoints = 2; // 点の数の下限（まっすぐな線）
PathCurve preview;            // 描く線（Entity から見た座標。毎フレーム作り直して、配列は使い回す）
} // namespace

//=============================================================================
// 毎フレーム
//=============================================================================
bool ArrowStripGizmo::Draw(const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	if (!ArrowStripSystem::IsEditing()) {
		return false;
	}
	// --- 選んでいる物に Arrow Strip が無ければ、編集を終わる（選ぶのをやめた・ほかの物を選んだ）---
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	ArrowStripComponent* strip = EntityManager::Get<ArrowStripComponent>(selected);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(selected);
	if (strip == nullptr || transform == nullptr) {
		ArrowStripSystem::SetEditing(false);
		return false;
	}
	// --- 点は矢印を描く面（Z が 0）の上で動かす。開いた線で、点を通る曲線も描く ---
	ArrowStripSystem::BuildPath(*strip, MakeIdentity4x4(), preview);
	PointList list;
	list.storage = strip->points;
	list.count = &strip->pointCount;
	list.minCount = kMinPoints;
	list.isClosed = false;
	list.curve = preview.GetPolyline();
	list.editor = ComponentEditorRegistry::Find<ArrowStripComponent>();
	list.component = strip;
	const PointMapping mapping = PointListGizmo::MakeCameraMapping(transform->worldMatrix, camera, 0.0f, imageMin, imageSize);
	if (!PointListGizmo::Draw(selected, list, mapping, imageMin, imageSize, isHovered)) {
		ArrowStripSystem::SetEditing(false); // Esc で終わった
		return false;
	}
	return true;
}
#endif