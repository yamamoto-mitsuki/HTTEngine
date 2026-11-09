#ifdef USE_IMGUI
#include "PointListGizmo.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <externals/imgui/ImGuizmo.h>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"

namespace {
constexpr float kHandleHalfSize = 5.0f;                     // 点の印（■）の大きさの半分（ピクセル）
constexpr float kPlusHalfSize = 4.0f;                       // 辺の真ん中の印（＋）の大きさの半分（ピクセル）
constexpr float kPickRadius = 9.0f;                         // マウスがこれより近ければ、点・＋を掴める（ピクセル）
constexpr float kLineThickness = 2.0f;                      // 辺・線の太さ（ピクセル）
constexpr float kGuideThickness = 1.0f;                     // 線があるときの、辺（点と点を結ぶ補助線）の太さ（ピクセル）
constexpr float kMinDeterminant = 1.0e-12f;                 // 画面の軸が潰れている（大きさ 0）と見なす行列式の大きさ
constexpr ImU32 kEdgeColor = IM_COL32(80, 220, 255, 255);   // 辺・線（水色）
constexpr ImU32 kGuideColor = IM_COL32(80, 220, 255, 110);  // 線があるときの辺（薄い水色）
constexpr ImU32 kInvalidColor = IM_COL32(255, 70, 70, 255); // 形がおかしい（辺が交差している）ときの辺（赤）
constexpr ImU32 kPointColor = IM_COL32(255, 255, 255, 255); // 点（白）
constexpr ImU32 kActiveColor = IM_COL32(255, 160, 40, 255); // 選んだ点・マウスが乗っている点と ＋（選んだ物の枠と同じオレンジ）
constexpr ImU32 kPlusColor = IM_COL32(80, 220, 255, 200);   // 辺の真ん中の ＋（水色）

/// <summary>
/// 編集の状態（エディタに1つだけ）
/// </summary>
struct EditState {
	Handle<Entity> entity;           // 編集している Entity
	const void* component = nullptr; // 編集している Component（同じ Entity の別の Component を渡されたら、状態を捨てる）
	int lastFrame = -1;              // 最後に Draw が呼ばれた ImGui のフレーム（とんでいたら、編集をやめていたので状態を捨てる）
	int selectedPoint = -1;          // 選んでいる点（Delete で消す。-1 なら無し）
	int dragPoint = -1;              // ドラッグしている点（-1 ならしていない）
	std::vector<std::byte> before;   // ドラッグ・足す・消すの前の Component（ComponentEditor::Save の写し。Undo の「前」）
};

/// <summary>
/// 点の、Scene ビューの絵の上の位置
/// </summary>
struct ScreenPoints {
	std::vector<ImVec2> pixels;  // 絵の上のピクセル
	std::vector<bool> isVisible; // 写っているか（カメラの後ろの点は false）
	int count = 0;               // 点の数
};

EditState edit;

// ===== 画面の位置（真ん中0、右と上が＋、端が±1）↔ Scene ビューの絵の上のピクセル（Y は下向き）=====
ImVec2 ToPixel(const Vector2& ndc, const ImVec2& imageMin, const ImVec2& imageSize) {
	return ImVec2(imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x, imageMin.y + (0.5f - ndc.y * 0.5f) * imageSize.y);
}
Vector2 ToNdc(const ImVec2& pixel, const ImVec2& imageMin, const ImVec2& imageSize) { return {(pixel.x - imageMin.x) / imageSize.x * 2.0f - 1.0f, 1.0f - (pixel.y - imageMin.y) / imageSize.y * 2.0f}; }

// 2点の距離の2乗（ピクセル）
float DistanceSq(const ImVec2& a, const ImVec2& b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

// 2点の真ん中（ピクセル）
ImVec2 Midpoint(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

// 辺の数（多角形は点の数と同じ。線は1つ少ない）。辺 i は 点 i → 点 (i + 1) % count
int EdgeCount(const PointList& list, int count) { return list.isClosed ? count : (std::max)(count - 1, 0); }

// 足せる余りがあるか
bool CanInsert(const PointList& list) { return *list.count < static_cast<int>(list.storage.size()); }

//=============================================================================
// 履歴（Undo）
//=============================================================================
// ===== 変える前の中身を覚える =====
void Remember(const PointList& list) {
	if (list.editor != nullptr) {
		edit.before = list.editor->Save(list.component);
	}
}

// ===== 覚えた中身と今の中身を、1件の履歴にする（Inspector で値を変えたときと同じ入口。変わっていなければ積まれない）=====
void Commit(const PointList& list) {
	if (list.editor != nullptr && !edit.before.empty()) {
		EditorHistory::RecordComponentChange(edit.entity, *list.editor, edit.before, list.editor->Save(list.component));
		EditorHistory::CommitComponentChanges();
	}
	edit.before.clear();
}

//=============================================================================
// 位置の変換
//=============================================================================
// ===== 点の、絵の上の位置 =====
ScreenPoints CalcScreenPoints(const PointList& list, const PointMapping& mapping) {
	ScreenPoints result;
	result.count = *list.count;
	result.pixels.resize(result.count);
	result.isVisible.resize(result.count);
	for (int i = 0; i < result.count; ++i) {
		result.isVisible[i] = mapping.toPixel(list.storage[i], result.pixels[i]);
	}
	return result;
}

// ===== グリッドに吸い付ける（Ctrl を押している間はしない）=====
Vector2 Snap(const Vector2& point, float gridSize) {
	if (ImGui::GetIO().KeyCtrl || gridSize <= 0.0f) {
		return point;
	}
	return {std::round(point.x / gridSize) * gridSize, std::round(point.y / gridSize) * gridSize};
}

//=============================================================================
// 操作
//=============================================================================
// ===== マウスの近くの点（無ければ -1）=====
int FindHoveredPoint(const ScreenPoints& screen, const ImVec2& mouse) {
	int found = -1;
	float best = kPickRadius * kPickRadius;
	for (int i = 0; i < screen.count; ++i) {
		const float distanceSq = DistanceSq(mouse, screen.pixels[i]);
		if (screen.isVisible[i] && distanceSq < best) {
			found = i;
			best = distanceSq;
		}
	}
	return found;
}

// ===== マウスの近くの ＋（辺 i の真ん中。無ければ -1）=====
int FindHoveredPlus(const PointList& list, const ScreenPoints& screen, const ImVec2& mouse) {
	int found = -1;
	float best = kPickRadius * kPickRadius;
	for (int i = 0; i < EdgeCount(list, screen.count); ++i) {
		const int next = (i + 1) % screen.count;
		if (!screen.isVisible[i] || !screen.isVisible[next]) {
			continue;
		}
		const float distanceSq = DistanceSq(mouse, Midpoint(screen.pixels[i], screen.pixels[next]));
		if (distanceSq < best) {
			found = i;
			best = distanceSq;
		}
	}
	return found;
}

// ===== 辺 edge の真ん中に点を足す。足した点の番号を返す =====
int InsertPoint(const PointList& list, int edge) {
	const int count = *list.count;
	const int insert = edge + 1;
	const int next = (edge + 1) % count;
	const Vector2 middle = (list.storage[edge] + list.storage[next]) * 0.5f;
	// insert 番から後ろを1つずつ後ろへずらして、空いた所に入れる（copy_backward：後ろから写すので、重なっていても壊れない）
	std::copy_backward(list.storage.begin() + insert, list.storage.begin() + count, list.storage.begin() + count + 1);
	list.storage[insert] = middle;
	// 点と一緒の値（角の半径）も同じようにずらす。足した点は、となりの2つの真ん中の値
	if (!list.values.empty()) {
		const float middleValue = (list.values[edge] + list.values[next]) * 0.5f;
		std::copy_backward(list.values.begin() + insert, list.values.begin() + count, list.values.begin() + count + 1);
		list.values[insert] = middleValue;
	}
	*list.count = count + 1;
	return insert;
}

// ===== 点 index を消す（後ろの点を1つずつ前へ詰める）=====
void ErasePoint(const PointList& list, int index) {
	std::copy(list.storage.begin() + index + 1, list.storage.begin() + *list.count, list.storage.begin() + index);
	if (!list.values.empty()) {
		std::copy(list.values.begin() + index + 1, list.values.begin() + *list.count, list.values.begin() + index);
	}
	--*list.count;
}

// ===== マウス：押す（点を掴む・＋で足す・何も無い所で選ぶのをやめる）・ドラッグ・離す =====
void HandleMouse(const PointList& list, const PointMapping& mapping, const ScreenPoints& screen, bool isHovered) {
	// --- 押した（右上の軸の上は、そちらの操作）---
	const bool canGrab = isHovered && edit.dragPoint < 0 && !ImGuizmo::IsViewManipulateHovered();
	if (canGrab && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const ImVec2 mouse = ImGui::GetMousePos();
		const int hoveredPoint = FindHoveredPoint(screen, mouse);
		const int hoveredPlus = CanInsert(list) ? FindHoveredPlus(list, screen, mouse) : -1;
		if (hoveredPoint >= 0) {
			Remember(list);
			edit.selectedPoint = edit.dragPoint = hoveredPoint;
		} else if (hoveredPlus >= 0) {
			Remember(list); // 足す前を覚える（足してドラッグして離すまでが1件）
			edit.selectedPoint = edit.dragPoint = InsertPoint(list, hoveredPlus);
		} else {
			edit.selectedPoint = -1;
		}
	}
	if (edit.dragPoint < 0) {
		return;
	}
	// --- ドラッグ中：マウスの所へ（グリッドに吸い付く）。離したら1件の履歴にする ---
	// 少し動かすまで（IsMouseDragging：押した所から数ピクセル）は点を動かさない。押しただけで、グリッドに吸い付いて位置が変わらないように
	if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		Vector2 local;
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && mapping.toLocal(ImGui::GetMousePos(), local)) {
			list.storage[edit.dragPoint] = Snap(local, list.gridSize);
		}
	} else {
		Commit(list);
		edit.dragPoint = -1;
	}
}

// ===== キー：Delete で選んだ点を消す（minCount より少なくはしない）=====
void HandleDelete(const PointList& list) {
	if (edit.dragPoint >= 0 || edit.selectedPoint < 0 || edit.selectedPoint >= *list.count || !ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		return;
	}
	if (*list.count <= list.minCount) {
		return;
	}
	Remember(list);
	ErasePoint(list, edit.selectedPoint);
	Commit(list);
	edit.selectedPoint = -1;
}

//=============================================================================
// 描く
//=============================================================================
// ===== 点の間を通る線（Entity から見た座標の折れ線を、絵の上へ）=====
void DrawCurve(ImDrawList* drawList, const PointList& list, const PointMapping& mapping) {
	ImVec2 previous;
	bool hasPrevious = false;
	for (const Vector3& point : list.curve) {
		ImVec2 pixel;
		const bool isVisible = mapping.toPixel({point.x, point.y}, pixel);
		if (isVisible && hasPrevious) {
			drawList->AddLine(previous, pixel, kEdgeColor, kLineThickness);
		}
		previous = pixel;
		hasPrevious = isVisible;
	}
}

void DrawPoints(const PointList& list, const PointMapping& mapping, const ScreenPoints& screen, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true); // 絵の外（黒い余白）には描かない
	const ImVec2 mouse = ImGui::GetMousePos();
	const int count = screen.count;
	const int edgeCount = EdgeCount(list, count);
	// --- 辺（形がおかしいと赤）。線があるときは、辺は薄い補助線にして、線を太く描く ---
	const bool hasCurve = !list.curve.empty();
	const ImU32 edgeColor = !list.isValid ? kInvalidColor : (hasCurve ? kGuideColor : kEdgeColor);
	const float edgeThickness = hasCurve ? kGuideThickness : kLineThickness;
	for (int i = 0; i < edgeCount; ++i) {
		const int next = (i + 1) % count;
		if (screen.isVisible[i] && screen.isVisible[next]) {
			drawList->AddLine(screen.pixels[i], screen.pixels[next], edgeColor, edgeThickness);
		}
	}
	if (hasCurve) {
		DrawCurve(drawList, list, mapping);
	}
	// --- 辺の真ん中の ＋（点を足せる間だけ。ドラッグ中は出さない）---
	if (CanInsert(list) && edit.dragPoint < 0) {
		const int hoveredPlus = isHovered ? FindHoveredPlus(list, screen, mouse) : -1;
		for (int i = 0; i < edgeCount; ++i) {
			const int next = (i + 1) % count;
			if (!screen.isVisible[i] || !screen.isVisible[next]) {
				continue;
			}
			const ImVec2 center = Midpoint(screen.pixels[i], screen.pixels[next]);
			const ImU32 color = (i == hoveredPlus) ? kActiveColor : kPlusColor;
			drawList->AddLine(ImVec2(center.x - kPlusHalfSize, center.y), ImVec2(center.x + kPlusHalfSize, center.y), color, kLineThickness);
			drawList->AddLine(ImVec2(center.x, center.y - kPlusHalfSize), ImVec2(center.x, center.y + kPlusHalfSize), color, kLineThickness);
		}
	}
	// --- 点（■。選んだ点・マウスが乗っている点はオレンジ）---
	const int hoveredPoint = (isHovered && edit.dragPoint < 0) ? FindHoveredPoint(screen, mouse) : -1;
	for (int i = 0; i < count; ++i) {
		if (!screen.isVisible[i]) {
			continue;
		}
		const ImVec2& p = screen.pixels[i];
		const bool isActive = (i == edit.selectedPoint || i == hoveredPoint);
		drawList->AddRectFilled(ImVec2(p.x - kHandleHalfSize, p.y - kHandleHalfSize), ImVec2(p.x + kHandleHalfSize, p.y + kHandleHalfSize), isActive ? kActiveColor : kPointColor);
	}
	drawList->PopClipRect();
}
} // namespace

//=============================================================================
// 毎フレーム
//=============================================================================
bool PointListGizmo::Draw(Handle<Entity> entity, const PointList& list, const PointMapping& mapping, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	// --- 別の物を渡された・前のフレームに呼ばれなかった（編集をやめていた）：選んだ点などは捨てる ---
	const int frame = ImGui::GetFrameCount();
	if (edit.entity != entity || edit.component != list.component || edit.lastFrame != frame - 1) {
		edit = {};
		edit.entity = entity;
		edit.component = list.component;
	}
	edit.lastFrame = frame;
	// --- キー（Scene ビューを触っている時だけ。文字の入力中は何もしない）---
	const bool isKeyTarget = (isHovered || ImGui::IsWindowFocused()) && !ImGui::GetIO().WantTextInput;
	if (isKeyTarget && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (edit.dragPoint >= 0) {
			Commit(list); // ドラッグ中なら、そこまでを1件にする
		}
		edit = {};
		return false;
	}
	if (isKeyTarget) {
		HandleDelete(list);
	}
	// --- マウス（変えた後の点で描くように、描くための位置は後でもう一度出す）---
	HandleMouse(list, mapping, CalcScreenPoints(list, mapping), isHovered);
	DrawPoints(list, mapping, CalcScreenPoints(list, mapping), imageMin, imageSize, isHovered);
	return true;
}

//=============================================================================
// 点の写し方
//=============================================================================
// ===== 3D の物：Entity の面の上の点を、カメラで写す =====
PointMapping PointListGizmo::MakeCameraMapping(const Matrix4x4& world, const Camera& camera, float planeZ, const ImVec2& imageMin, const ImVec2& imageSize) {
	PointMapping mapping;
	// --- 点 → ワールド（点を動かす面の上）→ カメラで正規化座標 → 絵の上のピクセル ---
	mapping.toPixel = [world, &camera, planeZ, imageMin, imageSize](const Vector2& local, ImVec2& pixel) {
		Vector2 ndc;
		if (!camera.WorldToScreenPoint(TransformPoint({local.x, local.y, planeZ}, world), &ndc)) {
			return false;
		}
		pixel = ToPixel(ndc, imageMin, imageSize);
		return true;
	};
	// --- ピクセルの所から奥へ向かうレイと、点を動かす面が交わる所 → Entity から見た位置 ---
	mapping.toLocal = [world, &camera, planeZ, imageMin, imageSize](const ImVec2& pixel, Vector2& local) {
		const Ray ray = camera.ScreenPointToRay(ToNdc(pixel, imageMin, imageSize));
		// 点を動かす面（向きは Entity の Z 軸。Plane は「法線 · 点 ＝ distance」で表す）
		const Vector3 normal = Normalize(TransformNormal({0.0f, 0.0f, 1.0f}, world));
		if (LengthSq(normal) == 0.0f) {
			return false; // Z の大きさが 0（面の向きが決まらない）
		}
		const Vector3 onPlane = TransformPoint({0.0f, 0.0f, planeZ}, world);
		float t = 0.0f;
		if (!Collision::RayPlane(ray, {normal, Dot(normal, onPlane)}, &t)) {
			return false; // 面と平行・面がカメラの後ろ
		}
		const Vector3 hit = TransformPoint(Collision::RayPoint(ray, t), Inverse(world));
		if (!std::isfinite(hit.x) || !std::isfinite(hit.y)) {
			return false; // 大きさが 0 の物（逆行列が作れない）
		}
		local = {hit.x, hit.y};
		return true;
	};
	return mapping;
}

// ===== 画面に重ねる UI：Image と同じ置き方（SpriteRenderSystem::CalcScreenPoint）で写す =====
PointMapping PointListGizmo::MakeScreenMapping(const Matrix4x4& world, ScreenAnchor anchor, const ImVec2& imageMin, const ImVec2& imageSize) {
	PointMapping mapping;
	mapping.toPixel = [world, anchor, imageMin, imageSize](const Vector2& local, ImVec2& pixel) {
		pixel = ToPixel(SpriteRenderSystem::CalcScreenPoint(world, anchor, local), imageMin, imageSize);
		return true;
	};
	// --- 逆向き：点 (1,0)・(0,1) が画面のどこへ動くか（軸）から、2×2 の連立方程式を解く ---
	// 画面の位置 = 原点 + x × 横の軸 + y × 縦の軸 なので、外積（Cros）で x・y を取り出す（クラメルの公式）
	mapping.toLocal = [world, anchor, imageMin, imageSize](const ImVec2& pixel, Vector2& local) {
		const Vector2 origin = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 0.0f});
		const Vector2 axisX = SpriteRenderSystem::CalcScreenPoint(world, anchor, {1.0f, 0.0f}) - origin;
		const Vector2 axisY = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 1.0f}) - origin;
		const float determinant = Cros(axisX, axisY);
		if (std::abs(determinant) < kMinDeterminant) {
			return false; // 大きさが 0（向きが潰れている）
		}
		const Vector2 offset = ToNdc(pixel, imageMin, imageSize) - origin;
		local = {Cros(offset, axisY) / determinant, Cros(axisX, offset) / determinant};
		return true;
	};
	return mapping;
}
#endif
