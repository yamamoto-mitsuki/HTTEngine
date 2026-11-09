#pragma once
#include <span>
#include <functional>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;
class ComponentEditor;

/// <summary>
/// 点の写し方（Entity から見た X・Y ⇔ Scene ビューの絵の上のピクセル）
/// <para>3D の物はカメラで写し（MakeCameraMapping）、画面に重ねる UI は画面のピクセルで写す（MakeScreenMapping）</para>
/// <para>std::function：関数・ラムダを「呼べる物」として1つの型で持てる入れ物。写し方ごとに違うラムダを入れて、同じ編集の処理から呼ぶ</para>
/// </summary>
struct PointMapping {
	std::function<bool(const Vector2& local, ImVec2& pixel)> toPixel; // 点 → 絵の上のピクセル（写っていなければ false）
	std::function<bool(const ImVec2& pixel, Vector2& local)> toLocal; // 絵の上のピクセル → 点（マウスの所。決まらなければ false）
};

/// <summary>
/// Scene ビューで編集する点の並び（Polygon Shape の外周・Arrow Strip の線・Polyline の点）。点は Entity から見た X・Y
/// <para>持ち主（PolygonShapeGizmo・ArrowStripGizmo・PolylineGizmo）が、毎フレーム Component から作って渡す</para>
/// </summary>
struct PointList {
	std::span<Vector2> storage;              // 点の置き場所（全部。足すと、後ろの点を1つずつずらす）
	std::span<float> values;                 // 点と一緒にずらす値（Polyline の角の半径。無ければ空。足した点は、となりの2つの真ん中の値）
	int* count = nullptr;                    // 使っている点の数（足す・消すと変わる）
	int minCount = 2;                        // これより少なくは消せない
	bool isClosed = false;                   // 最後の点から最初の点へも辺がある（多角形）
	float gridSize = 0.25f;                  // 点が吸い付く間隔（Entity から見た長さ。Ctrl を押している間は吸い付かない）
	bool isValid = true;                     // false なら、辺を赤く描く（形がおかしい）
	std::span<const Vector3> curve;          // 点の間を通る線（Entity から見た座標。z は使わない。あれば、辺の代わりにこれを太く描く）
	const ComponentEditor* editor = nullptr; // Undo の写しを取る（Inspector と同じ ComponentEditor::Save）
	const void* component = nullptr;         // 写しを取る Component（点を持っている物）
};


/// <summary>
/// Scene ビューで、点を動かす・足す・消す（形・線の編集の共通部分）
/// <para>操作：■ をドラッグ（gridSize に吸い付く。Ctrl で吸い付かない）／辺の真ん中の ＋ を押すと点を足す（そのままドラッグできる）／
/// 点を選んで Delete で消す（minCount より少なくはできない）／Esc で編集を終わる</para>
/// <para>1回のドラッグ・足す・消すで、1件の履歴になる（Ctrl+Z で戻せる）</para>
/// <para>選んだ点などの状態は1つだけ持つ。別の物を渡した・前のフレームに呼ばれなかったら、状態を捨てる</para>
/// </summary>
class PointListGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に描いて、マウス・キーで編集する。持ち主の Draw から毎フレーム1回
	/// </summary>
	/// <param name="entity">点を持っている Entity（Undo の記録に使う）</param>
	/// <param name="list">編集する点</param>
	/// <param name="mapping">点の写し方（MakeCameraMapping・MakeScreenMapping で作る）</param>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	/// <returns>編集を続けるか（Esc を押したら false。持ち主は編集中の印を消す）</returns>
	static bool Draw(Handle<Entity> entity, const PointList& list, const PointMapping& mapping, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);

	/// <summary>
	/// 3D の物の点の写し方。点は Entity の面（Entity から見た Z が planeZ）の上で、カメラから見た絵の上へ写す
	/// <para>返した物は、camera を参照で持つ。そのフレームの Draw にだけ渡す</para>
	/// </summary>
	static PointMapping MakeCameraMapping(const Matrix4x4& world, const Camera& camera, float planeZ, const ImVec2& imageMin, const ImVec2& imageSize);

	/// <summary>
	/// 画面に重ねる UI の点の写し方。点は Entity の位置を原点にしたピクセル（Image と同じ置き方。anchor の所から数える）
	/// </summary>
	static PointMapping MakeScreenMapping(const Matrix4x4& world, ScreenAnchor anchor, const ImVec2& imageMin, const ImVec2& imageSize);
};
