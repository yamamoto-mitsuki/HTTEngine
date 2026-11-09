#pragma once
#include <externals/imgui/imgui.h>

// 前方宣言
class Camera;

/// <summary>
/// Scene ビューで、Arrow Strip の線の点を動かす・足す・消す（Inspector の Edit Path を押している間だけ）
/// <para>操作・履歴は PointListGizmo（Polygon Shape と共通）。ここは Arrow Strip から「動かす点」と「点を通る線」を作って渡すだけ</para>
/// <para>2 点より少なくはできない。点を結ぶ薄い線と、実際の線（なめらかな曲線）を描く</para>
/// </summary>
class ArrowStripGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に描いて、マウスで編集する。SceneGizmo::Draw から毎フレーム1回（選んだ物の枠の後）
	/// </summary>
	/// <param name="camera">Scene ビューのカメラ</param>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	/// <returns>線を編集しているか（true なら、SceneGizmo は移動のギズモ・クリックで選び直すのをしない）</returns>
	static bool Draw(const Camera& camera, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);
};