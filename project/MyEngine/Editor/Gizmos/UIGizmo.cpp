#ifdef USE_IMGUI
#include "UIGizmo.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>
#include <algorithm>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/ImageComponent.h"
#include "MyEngine/Components/UI/PolylineComponent.h"
#include "MyEngine/Components/UI/TextComponent.h"
#include "MyEngine/Components/UI/VectorShapeComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Viewport/EditorViewport.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "MyEngine/Systems/UI/PolylineSystem.h"
#include "MyEngine/Systems/UI/TextRenderSystem.h"
#include "MyEngine/Systems/UI/UIMaskSystem.h"

namespace {
constexpr ImU32 kSelectedColor = IM_COL32(255, 160, 40, 255); // 選んだ UI の枠（3D の物の枠と同じ色）
constexpr ImU32 kHoveredColor = IM_COL32(255, 160, 40, 110);  // マウスが乗っている UI の枠（うすく。クリックすると選べる物）
constexpr float kOutlineThickness = 1.5f;                     // 枠の線の太さ（画面のピクセル）
constexpr float kPivotRadius = 3.5f;                          // 位置（Pivot の所）の印の半径（画面のピクセル）
constexpr float kHandleHalfSize = 4.0f;                       // 大きさを変える印（四角）の半分の大きさ（画面のピクセル）
constexpr float kHandleHitRadius = 8.0f;                      // 印をつかめる距離（画面のピクセル）
constexpr float kMinAxisLength = 1.0e-4f;                     // これより小さい Scale の軸は 0 で割らないように止める

// 大きさを変える印の場所（x：-1=左 0=真ん中 1=右 / y：-1=下 0=真ん中 1=上）
constexpr std::array<Vector2, 8> kHandles = {
    {{-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}
};

/// <summary>
/// 画面に重ねる UI の置いた形（正規化座標）
/// </summary>
struct UIShape {
	ScreenQuad quad;  // 四角の4つの角
	Vector2 position; // Entity の位置（Pivot の所）
};

/// <summary>
/// UI を押してから離すまでの状態（離したときに、押す前と後を1件の履歴にする）
/// </summary>
struct UIDragState {
	bool isPressed = false;        // UI の上で押して、まだ離していないか
	bool isDragging = false;       // 押したまま少し動かしたか（動かすまでは位置を変えない。クリックで選ぶだけのとき、ずれないように）
	Handle<Entity> entity;         // 動かしている物
	std::vector<std::byte> before; // 押したときの Transform（ComponentEditor::Save の写し）
	Vector3 startTranslation;      // 押したときの Translation
	Matrix4x4 parentInverse;       // 親のワールド行列の逆行列（画面で動かした量を、親から見た量に直す）
	ImVec2 startMouse;             // 押したときのマウスの位置（画面の座標）
	// --- 大きさを変えるときだけ使う ---
	Vector2 handle = {0.0f, 0.0f};      // つかんだ印（kHandles の値。0,0 なら位置を動かす）
	std::vector<std::byte> beforeShape; // 押したときの Vector Shape（ComponentEditor::Save の写し）
	Vector2 startSize;                  // 押したときの Size
};
UIDragState drag;

//=============================================================================
// UI の形
//=============================================================================
// 画面に重ねる UI の形（Image・Vector Shape があれば Size の四角、無ければ Text の文字のかたまり）
// どれも無い・描いていない（無効・目で隠した）・選べない（画面の編集中の、画面の外の物）物は false
bool CalcShape(Handle<Entity> entity, UIShape& shape) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity) || !SceneVisibility::IsPickable(entity)) {
		return false;
	}
	const Matrix4x4& m = transform->worldMatrix;
	if (const ImageComponent* image = EntityManager::Get<ImageComponent>(entity); image != nullptr && image->enabled) {
		shape.quad = SpriteRenderSystem::CalcScreenQuad(m, image->anchor, image->pivot, image->size); // Fill は掛けない（Image の大きさ全部）
		shape.position = SpriteRenderSystem::CalcScreenPoint(m, image->anchor, {0.0f, 0.0f});
		return true;
	}
	if (const VectorShapeComponent* vectorShape = EntityManager::Get<VectorShapeComponent>(entity); vectorShape != nullptr && vectorShape->enabled) {
		shape.quad = SpriteRenderSystem::CalcScreenQuad(m, vectorShape->anchor, vectorShape->pivot, vectorShape->size);
		shape.position = SpriteRenderSystem::CalcScreenPoint(m, vectorShape->anchor, {0.0f, 0.0f});
		return true;
	}
	// 折れ線は、点を囲む四角（線の太さも入れる）
	if (const PolylineComponent* polyline = EntityManager::Get<PolylineComponent>(entity); polyline != nullptr && polyline->enabled) {
		Vector2 min;
		Vector2 max;
		if (PolylineSystem::CalcLocalBounds(*polyline, min, max)) {
			const auto toScreen = [&m, polyline](float x, float y) { return SpriteRenderSystem::CalcScreenPoint(m, polyline->anchor, {x, y}); };
			shape.quad = {toScreen(min.x, min.y), toScreen(min.x, max.y), toScreen(max.x, min.y), toScreen(max.x, max.y)};
			shape.position = toScreen(0.0f, 0.0f);
			return true;
		}
	}
	if (const TextComponent* text = EntityManager::Get<TextComponent>(entity); text != nullptr && text->enabled) {
		shape.quad = SpriteRenderSystem::CalcScreenQuad(m, text->anchor, text->pivot, TextRenderSystem::CalcTextSize(entity));
		shape.position = SpriteRenderSystem::CalcScreenPoint(m, text->anchor, {0.0f, 0.0f});
		return true;
	}
	return false;
}

// 画面の座標（Scene ビューの絵の上。Y は下向き）→ 正規化座標（真ん中が0、右と上が＋、端が±1）
Vector2 ToNdc(const ImVec2& point, const ImVec2& imageMin, const ImVec2& imageSize) { return {(point.x - imageMin.x) / imageSize.x * 2.0f - 1.0f, 1.0f - (point.y - imageMin.y) / imageSize.y * 2.0f}; }

// 正規化座標 → 画面の座標（ToNdc の逆）
ImVec2 ToImage(const Vector2& ndc, const ImVec2& imageMin, const ImVec2& imageSize) { return {imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x, imageMin.y + (0.5f - ndc.y * 0.5f) * imageSize.y}; }

//=============================================================================
// クリックで選ぶ
//=============================================================================
// point の下にある UI のうち、一番手前に描いてある物（描く順と同じ：UIMaskSystem が決める重ねる順のキーが大きい物）
// マスクの子は、マスクの図形の Order の所にまとまって描かれるので、子の Order だけでは比べられない
Handle<Entity> PickUI(const Vector2& point) {
	Handle<Entity> best;
	UIDrawKey bestKey;
	const auto consider = [&](Handle<Entity> entity, int order, UIDrawKind kind) {
		UIShape shape;
		if (!CalcShape(entity, shape) || !SpriteRenderSystem::IsInsideQuad(point, shape.quad)) {
			return;
		}
		const UIDrawKey key = UIMaskSystem::MakeStep(entity, order, kind).key;
		if (!best.IsValid() || bestKey < key) {
			best = entity;
			bestKey = key;
		}
	};
	EntityManager::ForEach<ImageComponent>([&consider](Handle<Entity> entity, const ImageComponent& image) { consider(entity, image.order, UIDrawKind::Image); });
	EntityManager::ForEach<VectorShapeComponent>([&consider](Handle<Entity> entity, const VectorShapeComponent& shape) { consider(entity, shape.order, UIDrawKind::VectorShape); });
	EntityManager::ForEach<PolylineComponent>([&consider](Handle<Entity> entity, const PolylineComponent& polyline) { consider(entity, polyline.order, UIDrawKind::Polyline); });
	EntityManager::ForEach<TextComponent>([&consider](Handle<Entity> entity, const TextComponent& text) { consider(entity, text.order, UIDrawKind::Text); });
	return best;
}

//=============================================================================
// ドラッグで動かす
//=============================================================================
// 押した：今の Transform を覚える（動かし始めるのは、少し動かしてから）。handle は大きさを変える印（0,0 なら位置を動かす）
void BeginPress(Handle<Entity> entity, const Vector2& handle = {0.0f, 0.0f}) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const ComponentEditor* editor = ComponentEditorRegistry::Find<TransformComponent>();
	if (transform == nullptr || editor == nullptr) {
		return;
	}
	drag.isPressed = true;
	drag.isDragging = false;
	drag.entity = entity;
	drag.before = editor->Save(transform);
	drag.startTranslation = transform->translation;
	// 親のワールド行列（親がいなければ単位行列）の逆行列
	const Entity* owner = EntityManager::Get(entity);
	const TransformComponent* parent = (owner != nullptr) ? EntityManager::Get<TransformComponent>(owner->parent) : nullptr;
	drag.parentInverse = (parent != nullptr) ? Inverse(parent->worldMatrix) : MakeIdentity4x4();
	drag.startMouse = ImGui::GetMousePos();
	// --- 大きさを変えるときは、Vector Shape も覚える ---
	const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(entity);
	const ComponentEditor* shapeEditor = ComponentEditorRegistry::Find<VectorShapeComponent>();
	const bool isResize = (handle.x != 0.0f) || (handle.y != 0.0f);
	if (isResize && shape != nullptr && shapeEditor != nullptr) {
		drag.handle = handle;
		drag.beforeShape = shapeEditor->Save(shape);
		drag.startSize = shape->size;
	}
}

// 画面の上で動かした量（Y は上が＋）を、親から見た量に直す（親が回っている・大きくしてある物も、マウスについてくるように）
// 動かした量は「向き」なので、点の変換の差（移動の分が引かれて消える）で、回転と大きさだけを戻す
Vector3 ToParentMove(const Vector3& worldMove) { return TransformPoint(worldMove, drag.parentInverse) - TransformPoint({0.0f, 0.0f, 0.0f}, drag.parentInverse); }

// 大きさを変える：つかんだ辺だけがマウスについてきて、反対の辺は動かない
void UpdateResize(TransformComponent& transform, VectorShapeComponent& shape, const Vector3& worldMove) {
	// --- Entity の横・縦の軸（画面の上の向き。長さが scale）に、マウスの動きを分ける（回っていても、つかんだ辺の向きに伸びる）---
	const Matrix4x4& m = transform.worldMatrix;
	const Vector2 axisX = {m.m[0][0], m.m[0][1]};
	const Vector2 axisY = {m.m[1][0], m.m[1][1]};
	const float lengthSqX = (std::max)(axisX.x * axisX.x + axisX.y * axisX.y, kMinAxisLength);
	const float lengthSqY = (std::max)(axisY.x * axisY.x + axisY.y * axisY.y, kMinAxisLength);
	const float moveX = (worldMove.x * axisX.x + worldMove.y * axisX.y) / lengthSqX; // Size の単位（scale を掛ける前のピクセル）
	const float moveY = (worldMove.x * axisY.x + worldMove.y * axisY.y) / lengthSqY;
	// --- 1 ピクセルずつ。つかんだ辺の外向きに動かすと大きくなる ---
	shape.size = {
	    (std::max)(std::round(drag.startSize.x + drag.handle.x * moveX), 0.0f),
	    (std::max)(std::round(drag.startSize.y + drag.handle.y * moveY), 0.0f),
	};
	// --- 位置は Pivot の所なので、反対の辺が動かないように、伸びた分のうち Pivot の割合だけ動かす ---
	// 右（上）の辺をつかんだ：伸びた分 × pivot / 左（下）の辺をつかんだ：伸びた分 × (pivot − 1)
	const auto calcShift = [](float grown, float handle, float pivot) { return (handle == 0.0f) ? 0.0f : grown * ((handle > 0.0f) ? pivot : pivot - 1.0f); };
	const float shiftX = calcShift(shape.size.x - drag.startSize.x, drag.handle.x, shape.pivot.x);
	const float shiftY = calcShift(shape.size.y - drag.startSize.y, drag.handle.y, shape.pivot.y);
	const Vector3 worldShift = {axisX.x * shiftX + axisY.x * shiftY, axisX.y * shiftX + axisY.y * shiftY, 0.0f};
	transform.translation = drag.startTranslation + ToParentMove(worldShift);
}

// 押している間：押した所からマウスが動いた分だけ、Translation を動かす（印をつかんでいたら、大きさを変える）
void UpdateDrag(const ImVec2& imageSize) {
	TransformComponent* transform = EntityManager::Get<TransformComponent>(drag.entity);
	if (transform == nullptr) {
		drag = {}; // ドラッグ中に消された（Undo など）
		return;
	}
	// 少し動かすまでは動かさない（IsMouseDragging：押した所から決まった距離より動いたか。ImGui の io.MouseDragThreshold）
	if (!drag.isDragging && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		return;
	}
	drag.isDragging = true;
	// --- マウスが動いた量（画面のピクセル）→ ゲームの画面のピクセル（UI の位置の単位。Y は上が＋）---
	const ImVec2 mouse = ImGui::GetMousePos();
	const Vector3 worldMove = {
	    (mouse.x - drag.startMouse.x) / imageSize.x * GameScreen::GetWidth(),
	    -(mouse.y - drag.startMouse.y) / imageSize.y * GameScreen::GetHeight(),
	    0.0f,
	};
	// --- 大きさを変える ---
	if (drag.handle.x != 0.0f || drag.handle.y != 0.0f) {
		if (VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(drag.entity)) {
			UpdateResize(*transform, *shape, worldMove);
		}
		return;
	}
	// --- 位置を動かす ---
	const Vector3 localMove = ToParentMove(worldMove);
	// 1 ピクセルずつ（Inspector の数字が 150.37 のようにならないように）。Z はそのまま
	transform->translation = {
	    std::round(drag.startTranslation.x + localMove.x),
	    std::round(drag.startTranslation.y + localMove.y),
	    drag.startTranslation.z,
	};
}

// 離した：動かしていたら、押す前と後を1件の履歴にする（Inspector で値を変えたときと同じ入口）
void EndPress() {
	const ComponentEditor* editor = ComponentEditorRegistry::Find<TransformComponent>();
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(drag.entity);
	if (drag.isDragging && editor != nullptr && transform != nullptr) {
		EditorHistory::RecordComponentChange(drag.entity, *editor, drag.before, editor->Save(transform));
		// 大きさを変えたときは Vector Shape も（Commit までの変更が1件の履歴になる）
		const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(drag.entity);
		const ComponentEditor* shapeEditor = ComponentEditorRegistry::Find<VectorShapeComponent>();
		if (!drag.beforeShape.empty() && shape != nullptr && shapeEditor != nullptr) {
			EditorHistory::RecordComponentChange(drag.entity, *shapeEditor, drag.beforeShape, shapeEditor->Save(shape));
		}
		EditorHistory::CommitComponentChanges();
	}
	drag = {};
}

//=============================================================================
// 大きさを変える印
//=============================================================================
// 印の場所（正規化座標）。四角の4つの角を混ぜて出す（回っていても、そのままの形で出る）
Vector2 CalcHandlePosition(const ScreenQuad& quad, const Vector2& handle) {
	const float u = (handle.x + 1.0f) * 0.5f;
	const float v = (handle.y + 1.0f) * 0.5f;
	const Vector2 bottom = quad.lb * (1.0f - u) + quad.rb * u;
	const Vector2 top = quad.lt * (1.0f - u) + quad.rt * u;
	return bottom * (1.0f - v) + top * v;
}

// マウスの下の印（無ければ 0,0）
Vector2 FindHandle(const ScreenQuad& quad, const ImVec2& imageMin, const ImVec2& imageSize) {
	const ImVec2 mouse = ImGui::GetMousePos();
	for (const Vector2& handle : kHandles) {
		const ImVec2 point = ToImage(CalcHandlePosition(quad, handle), imageMin, imageSize);
		if (std::hypot(point.x - mouse.x, point.y - mouse.y) <= kHandleHitRadius) {
			return handle;
		}
	}
	return {0.0f, 0.0f};
}

//=============================================================================
// 枠を描く
//=============================================================================
void DrawShape(ImDrawList* drawList, const UIShape& shape, const ImVec2& imageMin, const ImVec2& imageSize, ImU32 color, bool drawPivot) {
	const auto toImage = [&imageMin, &imageSize](const Vector2& ndc) { return ToImage(ndc, imageMin, imageSize); };
	// 左下 → 右下 → 右上 → 左上 の順に回る四角（回っていても、そのままの形で描ける）
	drawList->AddQuad(toImage(shape.quad.lb), toImage(shape.quad.rb), toImage(shape.quad.rt), toImage(shape.quad.lt), color, kOutlineThickness);
	if (drawPivot) {
		drawList->AddCircle(toImage(shape.position), kPivotRadius, color, 0, kOutlineThickness);
	}
}

// 大きさを変える印を描く（白い四角。枠の色で縁取る）
void DrawHandles(ImDrawList* drawList, const ScreenQuad& quad, const ImVec2& imageMin, const ImVec2& imageSize) {
	for (const Vector2& handle : kHandles) {
		const ImVec2 point = ToImage(CalcHandlePosition(quad, handle), imageMin, imageSize);
		const ImVec2 min = {point.x - kHandleHalfSize, point.y - kHandleHalfSize};
		const ImVec2 max = {point.x + kHandleHalfSize, point.y + kHandleHalfSize};
		drawList->AddRectFilled(min, max, IM_COL32_WHITE);
		drawList->AddRect(min, max, kSelectedColor);
	}
}
} // namespace

//=============================================================================
// 描画
//=============================================================================
bool UIGizmo::Draw(Handle<Entity> selected, const ImVec2& imageMin, const ImVec2& imageSize, bool canUseMouse) {
	// --- 押している間は動かす。離したら履歴に積む（マウスが絵の外へ出ても、離すまで続ける）---
	if (drag.isPressed) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			UpdateDrag(imageSize);
		} else {
			EndPress();
		}
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true); // 絵の外（黒い余白）には描かない
	// --- マウスの下の UI（Pick UI がオンのときだけ。押している間は探さない）---
	const bool canPick = EditorViewport::GetViewFlags().scenePickUI && canUseMouse && !drag.isPressed;
	const Handle<Entity> hovered = canPick ? PickUI(ToNdc(ImGui::GetMousePos(), imageMin, imageSize)) : Handle<Entity>{};
	UIShape shape;
	if (hovered.IsValid() && hovered != selected && CalcShape(hovered, shape)) {
		DrawShape(drawList, shape, imageMin, imageSize, kHoveredColor, false);
	}
	// --- 選んでいる UI の枠と、位置（Pivot の所）の印。Vector Shape には大きさを変える印も出す ---
	Vector2 hoveredHandle = {0.0f, 0.0f};
	if (CalcShape(selected, shape)) {
		DrawShape(drawList, shape, imageMin, imageSize, kSelectedColor, true);
		if (EntityManager::Get<VectorShapeComponent>(selected) != nullptr) {
			DrawHandles(drawList, shape.quad, imageMin, imageSize);
			hoveredHandle = canPick ? FindHandle(shape.quad, imageMin, imageSize) : Vector2{0.0f, 0.0f};
		}
	}
	drawList->PopClipRect();

	// --- クリック：印の上なら大きさを変える（手前の UI より先に見る。印は枠の外へ少し出るので）---
	if ((hoveredHandle.x != 0.0f || hoveredHandle.y != 0.0f) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		BeginPress(selected, hoveredHandle);
		return true;
	}
	// --- クリック：UI の上なら、それを選んで、ドラッグで動かせるようにする ---
	if (hovered.IsValid() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		HierarchyWindow::SelectAndReveal(hovered); // Hierarchy の一覧でも見えるようにする
		BeginPress(hovered);
		return true;
	}
	return drag.isPressed;
}

bool UIGizmo::IsScreenUI(Handle<Entity> entity) {
	return EntityManager::Get<ImageComponent>(entity) != nullptr || EntityManager::Get<VectorShapeComponent>(entity) != nullptr || EntityManager::Get<PolylineComponent>(entity) != nullptr ||
	       EntityManager::Get<TextComponent>(entity) != nullptr;
}
#endif