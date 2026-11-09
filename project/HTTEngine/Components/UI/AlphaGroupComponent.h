#pragma once
#include <type_traits>

// 自分と子の Image・Text・Sprite Renderer を、まとめて透明にする。Entityに付けるComponent（カテゴリ：UI）
// 描くときに、自分と親を全部たどって、見つかった Alpha Group の Alpha を全部掛ける（親の親に付けた物も効く）
// 説明の文字と背景の板を1つの親にまとめて、親の Alpha を動かせば、全部いっしょにフェードする（Unity の Canvas Group と同じ考え）
// Trigger Activator のフェードは、Target の Alpha Group の Alpha を動かす（Target に無ければ、再生を始めたときに付ける）


/// <summary>
/// まとめて透明にする（Unity の Canvas Group の、透明度だけの版）
/// </summary>
struct AlphaGroupComponent {
	bool enabled = true; // false の間は掛けない（Alpha が 1 と同じ）
	float alpha = 1.0f;  // 透明度の倍率（0〜1。0 なら見えない）。自分と子の Image・Text・Sprite Renderer の色の a に掛かる
};

static_assert(std::is_trivially_copyable_v<AlphaGroupComponent>);