#pragma once
#include <type_traits>

// 子の UI を、この Entity の図形（Vector Shape か Polyline）の形で切り抜く。Entityに付けるComponent（カテゴリ：UI）（Unity の uGUI の Mask）
// 切り抜く形：Vector Shape は輪郭の内側（ゲージで切った所も）、閉じた Polyline は塗りの内側、開いた Polyline は線の太さの所
// 両方ある Entity は Vector Shape を使う。どちらも無い・無効なら、何も切り抜かない（子はそのまま描く）
// 子（孫も）は、この図形の Order の所にまとめて描く（子どうしは子の Order の順）。同じ Entity の、ほかの UI（Text など）も中身になる
// 入れ子にすると、外のマスクと内のマスクの重なった所だけに描く（8 段まで。それより内のマスクは切り抜かない）
// 切り口はなめらかにならない（ステンシルで切るので、ピクセルごとに中か外か）。図形の縁取り（Stroke）は子より手前に描くので、切り口を隠せる

/// <summary>
/// 子の UI を図形の形で切り抜く
/// </summary>
struct UIMaskComponent {
	bool enabled = true;     // false の間は切り抜かない（図形はふつうに描く）
	bool showGraphic = true; // 切り抜く図形も描く（外すと、切り抜くだけで図形は見えない。縁取りも描かない）
};

static_assert(std::is_trivially_copyable_v<UIMaskComponent>);