#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 画面に重ねる UI（Image・Text・Vector Shape・Polyline）を、ワールドの物・ワールドの点に重ね続ける Component（カテゴリ：UI）
// 例：敵に重ねるマーク・名札・当たった所に出す数字・画面の端に出す「あっちにいる」の矢印
//
// 置き方（Unreal の Widget Component の Screen と同じ考え方）
//   ・重ねる点を、ゲームのメインのカメラで画面に写す。写した所を、この Entity のワールド行列の位置にする（カメラが動いた後に置くので、カメラを追いかけても揺れない）
//   ・Transform の Translation は書き換えない。x・y は「写した点からのずれ（ピクセル。上が＋）」として足される
//     → Tween・Timeline が Translation を動かすと、写した点からのずれが動く（当たった所からポンと上へ浮く、を親子に分けずに書ける）
//   ・親の大きさ・回転は、ずれと見た目に掛かる。親の位置は使わない（位置は写した点で決まる）
//   ・子は、置いた後の位置について来る（マークの輪を子に何個も並べられる）
//   ・停止中も重ねる（エディタで、重なった見た目を見ながら置ける）
//
// 決まり：この Entity と子の Image などの Anchor は Center にする（Anchor の場所は、ワールド行列の後に足されるので、Center 以外はずれる。Inspector に警告が出る）
// Script から：GetComponent<UIWorldFollowComponent>() の target・worldOffset を書くと、その次の置き直しから相手が変わる。結果（isOnScreen など）を読める

/// <summary>
/// 重ねる点が画面の外・カメラの後ろにあるときの扱い
/// </summary>
enum class OffscreenMode {
	Keep,        // そのまま（画面の外に出る。カメラの後ろなら隠す）
	Hide,        // 隠す（画面の中に入ったら、また出る）
	ClampToEdge, // 画面の端（Edge Margin だけ内側）に寄せる。カメラの後ろなら、向きの合う端に寄せる
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(OffscreenMode) {
	using enum OffscreenMode;
	return MakeEnumTips<OffscreenMode>({
	    {Keep, "そのまま（画面の外に出る。カメラの後ろなら隠す）"},
	    {Hide, "隠す（画面の中に入ったら、また出る）"},
	    {ClampToEdge, "画面の端（Edge Margin だけ内側）に寄せる。カメラの後ろなら、向きの合う端に寄せる"},
	});
}

/// <summary>
/// UI をワールドの物・ワールドの点に重ね続ける
/// </summary>
struct UIWorldFollowComponent {
	bool enabled = true;                           // false の間は重ねない（Transform のとおりに置く）
	EntityRef target;                              // 重ねる相手（none なら World Offset をワールドの点として使う）
	Vector3 worldOffset = {0.0f, 0.0f, 0.0f};      // 相手の位置からのずれ（ワールドの向き。m）。相手が none なら、重ねるワールドの点
	OffscreenMode offscreen = OffscreenMode::Hide; // 重ねる点が画面の外・カメラの後ろのときの扱い
	float edgeMargin = 48.0f;                      // Clamp To Edge で、画面の端から内側へ空ける幅（ピクセル。マークが切れない大きさにする）

	// --- 結果（UIWorldFollowSystem が毎フレーム書く。保存しない。Script から読む）---
	bool isHidden = false;                     // 隠している（画面の外・カメラの後ろ・相手が居ない）。この Entity と子は描かず、マウスも当たらない
	bool isOnScreen = false;                   // 重ねる点が画面の中に写っている（カメラの前で、画面の端の内側）
	bool isClamped = false;                    // 画面の端に寄せた（Clamp To Edge のときだけ）
	Vector2 screenPosition = {0.0f, 0.0f};     // 置いた所（画面の真ん中から、ピクセル。上が＋。Transform のずれは入らない）
	float edgeAngle = 0.0f;                    // 画面の真ん中から重ねる点への向き（度。右が 0、上が 90。端の矢印を回すのに使う）
	uint32_t nonCenterAnchorCount = 0;         // エディタ用：この Entity と子の UI で、Anchor が Center でない物の数（Inspector の警告）
};

static_assert(std::is_trivially_copyable_v<UIWorldFollowComponent>);
