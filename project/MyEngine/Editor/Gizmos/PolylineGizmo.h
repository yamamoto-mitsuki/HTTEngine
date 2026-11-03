#pragma once
#include <externals/imgui/imgui.h>


/// <summary>
/// Scene ビューで、Polyline の点を動かす・足す・消す（Inspector の Edit Points を押している間だけ）
/// <para>動かすのは、選んでいる Entity の点。点は画面のピクセルで動かす（1 ピクセルに吸い付く。Ctrl で吸い付かない）</para>
/// <para>操作・履歴は PointListGizmo（Polygon Shape・Arrow Strip と共通）。ここは Polyline から「動かす点」と「画面の写し方」を作って渡すだけ</para>
/// <para>点を足すと、角の半径はとなりの2つの真ん中の値になる。2 点より少なくはできない</para>
/// </summary>
class PolylineGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に描いて、マウスで編集する。SceneGizmo::Draw から毎フレーム1回
	/// </summary>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	/// <returns>点を編集しているか（true なら、SceneGizmo は移動のギズモ・UI の移動・クリックで選び直すのをしない）</returns>
	static bool Draw(const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);
};
