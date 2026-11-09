#pragma once
#include <externals/imgui/imgui.h>

// 前方宣言
class Camera;


/// <summary>
/// Scene ビューで、Polygon Shape の点を動かす・足す・消す（Inspector の Edit Shape を押している間だけ）
/// <para>動かすのは、選んでいる Entity の点。点は足場の前の面の上で動かす（マウスのレイと、前の面の平面が交わる所）</para>
/// <para>操作・履歴は PointListGizmo（Arrow Strip の線と共通）。ここは Polygon Shape から「動かす点」を作って渡すだけ</para>
/// <para>3 点より少なくはできない。辺が交差していると、線が赤くなる（描かれない形）</para>
/// </summary>
class PolygonShapeGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に描いて、マウスで編集する。SceneGizmo::Draw から毎フレーム1回（選んだ物の枠の後）
	/// </summary>
	/// <param name="camera">Scene ビューのカメラ</param>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	/// <returns>形を編集しているか（true なら、SceneGizmo は移動のギズモ・クリックで選び直すのをしない）</returns>
	static bool Draw(const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);
};