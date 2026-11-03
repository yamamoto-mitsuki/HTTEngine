#pragma once
#include <externals/imgui/imgui.h>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"

/// <summary>
/// Scene ビューで、画面に重ねる UI（Image・Vector Shape・Polyline・Text）をクリックで選ぶ・ドラッグで動かす（Unity の Rect Tool の小さい版）
/// <para>UI は 3D の物より手前に描いてあるので、3D の物より先に選ぶ。重なっていたら、手前に描いてある物（Order が大きい・後に作った物）</para>
/// <para>動かすのは Transform の Translation の x・y（画面のピクセル。1 ピクセルずつ）。1回のドラッグで1件の履歴になる（Ctrl+Z で戻せる）</para>
/// <para>Vector Shape を選ぶと、枠に大きさを変える印が8つ出る。印をドラッグすると Size が変わる（反対の辺は動かない）。Transform と Size をまとめて1件の履歴にする</para>
/// <para>View → Scene View → Pick UI を外すと、UI を選ばない（UI の後ろの 3D の物をクリックしたいとき）。Hierarchy の目で隠した UI も選ばない</para>
/// </summary>
class UIGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に、UI の枠を描いて、マウスで選ぶ・動かす。SceneGizmo::Draw から毎フレーム1回（移動のギズモの後）
	/// </summary>
	/// <param name="selected">選んでいる Entity（UI なら枠と位置の印を描く）</param>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="canUseMouse">マウスを使ってよいか（マウスが絵の上にあり、ギズモ・右上の軸の上ではない）</param>
	/// <returns>マウスを使ったか（UI をクリックした・ドラッグ中。true なら、SceneGizmo は 3D の物を選び直さない）</returns>
	static bool Draw(Handle<Entity> selected, const ImVec2& imageMin, const ImVec2& imageSize, bool canUseMouse);

	// 画面に重ねる UI か（Image・Vector Shape・Polyline・Text のどれかが付いている）。位置がピクセルなので、3D の移動のギズモは出さない
	static bool IsScreenUI(Handle<Entity> entity);
};