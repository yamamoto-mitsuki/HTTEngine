#ifdef USE_IMGUI
#include "SceneGizmo.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <externals/imgui/ImGuizmo.h>

#include "HTTEngine/Camera/DebugCamera.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/Core/FolderComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Gizmos/ArrowStripGizmo.h"
#include "HTTEngine/Editor/Gizmos/PolygonShapeGizmo.h"
#include "HTTEngine/Editor/Gizmos/PolylineGizmo.h"
#include "HTTEngine/Editor/Gizmos/UIGizmo.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Viewport/ScenePicking.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
#include "HTTEngine/Math/Vector4.h"

namespace {
constexpr ImU32 kOutlineColor = IM_COL32(255, 160, 40, 255); // 選んだ物の枠の色
constexpr float kOutlineThickness = 1.5f;                    // 枠の線の大きさ
constexpr float kNearW = 1e-3f;                              // カメラのすぐ前で線を切る（これより後ろの点は、画面に出せない）
constexpr float kMinScale = 1e-6f;                           // これより小さい大きさは0とみなす
constexpr float kFocusMinDistance = 2.0f;                    // F で寄るとき、これより近づかない
constexpr float kFocusMargin = 1.0f;                         // F で寄るとき、物が画面にちょうど入る距離の何倍まで離れるか

/// <summary>
/// ギズモをドラッグしている間の乗法（離したときに、ドラッグの前と後を1件の履歴にする）
/// </summary>
struct DragState {
	bool isDragging = false;       // ドラッグ中か
	Handle<Entity> entity;         // 動かしている物
	std::vector<std::byte> before; // ドラッグを始めつ前の Transform（ComponentEditor::Save の写し）
};

GizmoSettings settings;               // ギズモの設定
DragState drag;                       // ドラッグ中の物
std::vector<PickBounds> boundsBuffer; // 見ための箱を集める入れ物（毎フレーム中身だけ入れなおす。メモリは使いまわす）

// ===== 行列の i行目 =====
Vector3 RowOf(const Matrix4x4& m, int row) { return {m.m[row][0], m.m[row][1], m.m[row][2]}; }

// ===== 行列が和人して正しいか =====
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

// ===== 親のワールド行列（親がいなければ単位行列）=====
Matrix4x4 ParentWorldOf(Handle<Entity> handle) {
	const Entity* entity = EntityManager::Get(handle);
	const TransformComponent* parent = entity ? EntityManager::Get<TransformComponent>(entity->parent) : nullptr;
	return parent ? parent->worldMatrix : MakeIdentity4x4();
}

//=============================================================================
// ドラッグの終わり（履歴に積む）
//=============================================================================
void EndDrag() {
	// ドラッグしているか
	if (!drag.isDragging) {
		return;
	}
	const ComponentEditor* editor = ComponentEditorRegistry::Find<TransformComponent>();
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(drag.entity);
	if (editor != nullptr && transform != nullptr) {
		// Inspector で値を変えたときと同じ入口。ドラッグの前と後を、1件の履歴にまとめる
		EditorHistory::RecordComponentChange(drag.entity, *editor, drag.before, editor->Save(transform));
		EditorHistory::CommitComponentChanges();
	}
	drag = {};
}

//=============================================================================
// ギズモで動かした行列を、Transform の値へ戻す
//=============================================================================
// ===== localは「親から見た行列」。今の道具分だけ書き戻す（他の値まで行列から取り出し直すと、動かすたびに少しずつずれるので）=====
void ApplyToTransform(TransformComponent& transform, const Matrix4x4 local) {
	switch (settings.operation) { 
	case GizmoOperation::Move: {
		transform.translation = RowOf(local, 3);
		break;
	}
	
	case GizmoOperation::Rotate: {
		// 1～3行目を大きさで割ると、回転の行列が残る
		const std::array<float, 3> scales = {transform.scale.x, transform.scale.y, transform.scale.z};
		Matrix4x4 rotateMatrix = MakeIdentity4x4();
		for (int row = 0; row < 3; ++row) {
			if (std::abs(scales[row]) < kMinScale) {
				return; // 大きさが0の軸は、向きが分からない
			}
			const Vector3 axis = RowOf(local, row) / scales[row];
			rotateMatrix.m[row][0] = axis.x;
			rotateMatrix.m[row][1] = axis.y;
			rotateMatrix.m[row][2] = axis.z;
		}
		transform.rotation = MakeQuaternionFromMatrix(rotateMatrix);
		// Inspector に出すオイラー角は、ドラッグする前の角度に一番近い表し方にする（数字が 180 度飛んだりしない）
		transform.eulerHint = ToEulerNearest(transform.rotation, transform.eulerHint);
		break;
	}

	case GizmoOperation::Scale: {
		// 回転は変わらないので、新しい行を「今の回転の行（長さ1）」の向きに測った長さが、新しい大きさ（向きが逆ならマイナス）
		const Matrix4x4 rotateMatrix = MakeRotateMatrix(transform.rotation);
		transform.scale = {
		    Dot(RowOf(local, 0), RowOf(rotateMatrix, 0)),
		    Dot(RowOf(local, 1), RowOf(rotateMatrix, 1)),
		    Dot(RowOf(local, 2), RowOf(rotateMatrix, 2)),
		};
		break;
	}
	}
}
//=============================================================================
// ギズモ
//=============================================================================
// 選んだ物にギズモを出して、ドラッグされたら Transform を書き換える。ギズモを出したら true
bool Manipulate(Handle<Entity> selected, const Camera& camera) {
	TransformComponent* transform = EntityManager::Get<TransformComponent>(selected);
	// 画面に重ねるUIは出さない（位置が画面のピクセルなので、3D の矢印では動かせない。UIGizmo がドラッグで動かす）
	if (transform == nullptr || EntityManager::Get<FolderComponent>(selected) != nullptr || UIGizmo::IsScreenUI(selected)) {
		EndDrag(); // ドラッグ中に消された・選ぶのをやめた
		return false;
	}

	// --- 今のワールド行列（Inspector で変えたばかりでも合うように、Transform の値から作り直す）---
	const Matrix4x4 parentWorld = ParentWorldOf(selected);
	Matrix4x4 world = MakeAffineMatrix(transform->scale, transform->rotation, transform->translation) * parentWorld;

	// --- ImGuizmo に渡す設定 ---
	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	float snapValue = settings.moveSnap;
	switch (settings.operation) {
	case GizmoOperation::Move:
		break; // 上の初期値（TRANSLATE・moveSnap）のまま
	case GizmoOperation::Rotate:
		operation = ImGuizmo::ROTATE;
		snapValue = settings.rotateSnap; // 回転の刻みは度で渡す
		break;
	case GizmoOperation::Scale:
		operation = ImGuizmo::SCALE;
		snapValue = settings.scaleSnap;
		break;
	}
	// 拡縮はいつも物の向き（ワールドの向きで拡縮すると、回っている物がゆがむので）
	const bool isWorld = (settings.space == GizmoSpace::World) && (settings.operation != GizmoOperation::Scale);
	const ImGuizmo::MODE mode = isWorld ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	const bool useSnap = (settings.snap != ImGui::GetIO().KeyCtrl); // Ctrl を押している間は、オン・オフを逆にする
	const std::array<float, 3> snap = {snapValue, snapValue, snapValue};

	// 行列は、そのまま渡せる（ImGuizmo もエンジンと同じ「v × 行列」で、4行目が位置の並び）
	ImGuizmo::SetOrthographic(false);
	const bool isChanged = ImGuizmo::Manipulate(&camera.GetViewMatrix().m[0][0], &camera.GetProjectionMatrix().m[0][0], operation, mode, &world.m[0][0], nullptr, useSnap ? snap.data() : nullptr);

	// --- ドラッグの始まり：今の中身を覚えておく ---
	if (ImGuizmo::IsUsing() && !drag.isDragging) {
		if (const ComponentEditor* editor = ComponentEditorRegistry::Find<TransformComponent>()) {
			drag.isDragging = true;
			drag.entity = selected;
			drag.before = editor->Save(transform);
		}
	}
	// --- 動かした分を Transform へ（親から見た行列に直してから）---
	if (isChanged) {
		const Matrix4x4 local = world * Inverse(parentWorld);
		if (IsFinite(local)) {
			ApplyToTransform(*transform, local);
		}
	}
	// --- ドラッグの終わり：履歴に積む ---
	if (drag.isDragging && !ImGuizmo::IsUsing()) {
		EndDrag();
	}
	return true;
}

//=============================================================================
// 選んだ物の枠
//=============================================================================
// ワールド座標の点 → 同次座標（w で割る前。w はカメラからの奥行き）
Vector4 ToClip(const Vector3& p, const Matrix4x4& m) {
	return {
	    p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
	    p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
	    p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2],
	    p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3],
	};
}

// 線を描く。カメラの後ろへはみ出した分は、カメラのすぐ前で切る（後ろの点を w で割ると、裏返って変な所に出るので）
void DrawClippedLine(ImDrawList* drawList, Vector4 a, Vector4 b, const ImVec2& imageMin, const ImVec2& imageSize) {
	if (a.w < kNearW && b.w < kNearW) {
		return; // 両方ともカメラの後ろ
	}
	if (a.w < kNearW) {
		a = a + (b - a) * ((kNearW - a.w) / (b.w - a.w));
	} else if (b.w < kNearW) {
		b = b + (a - b) * ((kNearW - b.w) / (a.w - b.w));
	}
	// 正規化座標（真ん中0、右と上が＋、端が±1）→ 絵の上のピクセル（Y は下向き）
	const auto toScreen = [&imageMin, &imageSize](const Vector4& p) { return ImVec2(imageMin.x + (p.x / p.w * 0.5f + 0.5f) * imageSize.x, imageMin.y + (0.5f - p.y / p.w * 0.5f) * imageSize.y); };
	drawList->AddLine(toScreen(a), toScreen(b), kOutlineColor, kOutlineThickness);
}

void DrawSelectionOutline(Handle<Entity> selected, const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize) {
	if (!EntityManager::IsAlive(selected)) {
		return;
	}
	boundsBuffer.clear();
	ScenePicking::CollectBounds(selected, camera, boundsBuffer);
	if (boundsBuffer.empty()) {
		return; // 見た目の無い物（ライト・空の Entity）は、ギズモだけ
	}
	const Matrix4x4 viewProjection = camera.GetViewProjectionMatrix();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true); // 絵の外（黒い余白）には描かない
	for (const PickBounds& bounds : boundsBuffer) {
		const std::array<Vector3, 8> worldCorners = bounds.GetWorldCorners();
		std::array<Vector4, 8> corners{};
		for (size_t i = 0; i < corners.size(); ++i) {
			corners[i] = ToClip(worldCorners[i], viewProjection);
		}
		for (const auto& [from, to] : kBoxEdges) {
			DrawClippedLine(drawList, corners[from], corners[to], imageMin, imageSize);
		}
	}
	drawList->PopClipRect();
}

//=============================================================================
// F：選んだ物へ寄る
//=============================================================================
void FocusSelected(DebugCamera& camera) {
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(selected);
	if (transform == nullptr) {
		return;
	}
	// --- 物が全部入る球（見た目が無ければ、置いてある所の小さい球）---
	Vector3 center = GetWorldPosition(*transform);
	float radius = 0.5f;
	boundsBuffer.clear();
	ScenePicking::CollectBounds(selected, camera, boundsBuffer);
	if (!boundsBuffer.empty()) {
		Vector3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
		Vector3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		for (const PickBounds& bounds : boundsBuffer) {
			for (const Vector3& corner : bounds.GetWorldCorners()) {
				min = {(std::min)(min.x, corner.x), (std::min)(min.y, corner.y), (std::min)(min.z, corner.z)};
				max = {(std::max)(max.x, corner.x), (std::max)(max.y, corner.y), (std::max)(max.z, corner.z)};
			}
		}
		center = (min + max) * 0.5f;
		radius = Length(max - min) * 0.5f;
	}
	// --- 向きはそのままで、球が画面の縦にちょうど入る距離まで下がった所へ ---
	// 球が縦の視野角 fovY にちょうど入る距離は、radius ÷ sin(fovY / 2)
	const float distance = (std::max)(kFocusMinDistance, radius / std::sin(camera.GetFovY() * 0.5f) * kFocusMargin);
	const Matrix4x4 cameraWorld = MakeAffineMatrix(camera.GetScale(), camera.GetRotation(), camera.GetTranslation());
	const Vector3 forward = Normalize(RowOf(cameraWorld, 2)); // カメラの前（ワールド行列の3行目）
	camera.SetTranslation(center - forward * distance);
	camera.ReCalcViewMatrix();
}

//=============================================================================
// キー（Scene ビューを触っている時だけ）
//=============================================================================
void HandleShortcuts(DebugCamera& camera, bool isHovered) {
	// マウスが Scene ビューの上にあるか、Scene ビューを最後にクリックした時だけ（Game ビューでゲームの W を押しても、道具が変わらないように）
	if (!isHovered && !ImGui::IsWindowFocused()) {
		return;
	}
	// 文字の入力中・ギズモのドラッグ中は何もしない
	if (ImGui::GetIO().WantTextInput || ImGuizmo::IsUsing()) {
		return;
	}
	// キーは EditorKeymap（Settings → Editor → Keymap で変えられる）
	if (EditorKeymap::IsTriggered(EditorAction::ToolMove)) {
		settings.operation = GizmoOperation::Move;
	}
	if (EditorKeymap::IsTriggered(EditorAction::ToolRotate)) {
		settings.operation = GizmoOperation::Rotate;
	}
	if (EditorKeymap::IsTriggered(EditorAction::ToolScale)) {
		settings.operation = GizmoOperation::Scale;
	}
	if (EditorKeymap::IsTriggered(EditorAction::Frame)) {
		FocusSelected(camera);
	}
}

//=============================================================================
// クリックで選ぶ
//=============================================================================
void HandleClick(const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize, bool canUseMouse) {
	if (!canUseMouse || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		return;
	}
	// マウスの位置 → 正規化座標（真ん中0、右と上が＋、端が±1）→ カメラの奥へ向かうレイ
	const ImVec2 mouse = ImGui::GetMousePos();
	const Vector2 ndc = {(mouse.x - imageMin.x) / imageSize.x * 2.0f - 1.0f, 1.0f - (mouse.y - imageMin.y) / imageSize.y * 2.0f};
	// 何も無い所なら、無効な Handle が返る（選ぶのをやめる。Unity と同じ）。選んだ物は Hierarchy の一覧でも見えるようにする
	HierarchyWindow::SelectAndReveal(ScenePicking::Pick(camera.ScreenPointToRay(ndc), camera));
}
} // namespace


//=============================================================================
// 描画
//=============================================================================
void SceneGizmo::Draw(DebugCamera& camera, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
		return;
	}
	HandleShortcuts(camera, isHovered);
	UIScreenEditor::DrawFrame(imageMin, imageSize); // 画面（UI Screen）の編集中は、画面の枠（一番下に描く。選んだ物の枠・ギズモが上に重なる）
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	DrawSelectionOutline(selected, camera, imageMin, imageSize); // 枠を先に描く（ギズモが上に重なるように）
	// 形・線の編集中（Polygon Shape の Edit Shape・Arrow Strip の Edit Path・Polyline の Edit Points）は、点を動かすのがマウスの仕事。移動のギズモ・クリックで選び直すのはしない
	// || は左から調べて true で止まるので、2つの編集が同じフレームに動くことはない
	if (PolygonShapeGizmo::Draw(camera, imageMin, imageSize, isHovered) || ArrowStripGizmo::Draw(camera, imageMin, imageSize, isHovered) || PolylineGizmo::Draw(imageMin, imageSize, isHovered)) {
		return;
	}
	const bool isGizmoShown = Manipulate(selected, camera);
	// ギズモ・右上の軸の上は、そちらの操作（UI も 3D の物も選び直さない）。ギズモの上かは、Manipulate の後で分かる
	const bool isOnGizmo = (isGizmoShown && ImGuizmo::IsOver()) || ImGuizmo::IsUsingAny() || ImGuizmo::IsViewManipulateHovered();
	const bool canUseMouse = isHovered && !isOnGizmo;
	// 画面に重ねる UI が先（3D の物より手前に描いてあるので）。UI をクリックした・ドラッグ中なら、3D の物は選ばない
	if (UIGizmo::Draw(selected, imageMin, imageSize, canUseMouse)) {
		return;
	}
	HandleClick(camera, imageMin, imageSize, canUseMouse);
}

GizmoSettings& SceneGizmo::GetSettings() { return settings; }
#endif