#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Math/Vector2.h"

// 「> > > >」のように、向きを示す矢印（山形）を線に沿って並べて流す見た目。Entity に付ける Component（カテゴリ：Rendering3D）
// 線は、点を順に通るなめらかな曲線（Catmull-Rom。点が 2 つならまっすぐ）。点は Entity のローカル座標の X・Y（Z は 0 の面に描く）
// 吹っ飛ばす円の行き先（緑の円は、この線に沿ってキューブを飛ばす）・ベルトコンベア・風の向きなど、「こっちへ進む」を見せたい所に使う
// 描くのは ArrowStripSystem。停止中も描いて流すので、Inspector・Scene ビュー（Edit Path）で変えると、すぐ見られる
// 矢印の絵（テクスチャ）は無くてよい（形は ArrowStripSystem が作る）。Texture を選ぶと、矢印1つの四角に1枚貼る
//
//   点0 ●── Start Offset ──▶ >  >  >  >  ╮        矢印は Start Offset から線の終わりまでに、Spacing ずつ並ぶ
//   （線の始まり）                         >       （Spacing に一番近い、ちょうど割り切れる間に直す）
//                                     点2 ● >     矢印の向きは、その場所の線の向き
//              ↑ 点1 はこのあたり（線は点を必ず通る）

// 線の点の数の上限（Component は memcpy で写すので、決まった大きさの配列で持つ）
constexpr uint32_t kMaxArrowPathPoints = 16;

/// <summary>
/// 向きを示す矢印の列
/// </summary>
struct ArrowStripComponent {
	bool enabled = true; // false の間は描かない
	// 線の点（Entity から見た X・Y。m）。使うのは先頭から pointCount 個。初めは右へまっすぐ 3.4m（G7b の Start Offset 1・4個・0.6 と同じ並び）
	std::array<Vector2, kMaxArrowPathPoints> points = {
	    {{0.0f, 0.0f}, {3.4f, 0.0f}}
    };
	int pointCount = 2;           // 使っている点の数（2〜kMaxArrowPathPoints）
	float startOffset = 1.0f;     // 線の始まりから、矢印を並べ始めるまでの長さ（m。線に沿って測る。円の外から並べたいときなど）
	float spacing = 0.6f;         // 矢印の間（m。線に沿って測る。1つの矢印の先から、次の矢印の先まで）
	Vector2 size = {0.35f, 0.6f}; // 矢印1つの大きさ（X：先までの長さ、Y：高さ。m）
	float thickness = 0.12f;      // 矢印の線の太さ（X の向きで測る。m）
	float scrollSpeed = 1.5f;     // 流れる速さ（m/秒。0 で止まる。マイナスで逆へ流れる）
	float fade = 0.3f;            // 両端で消していく長さ（並べた長さに対する割合。0〜0.5。0 なら消さない。Opaque は小さくして、それ以外は薄くして消す）
	PrimitiveMaterial material = {
	    .color = {0.4f, 1.0f, 0.5f, 1.0f},
	    .surface = SurfaceType::Additive,
	    .shadingType = ShadingType::Unlit,
	}; // 見た目（色・透け方・テクスチャ。Primitive Renderer と同じ。初めは加算で光る緑）

	// 使っている点だけ（pointCount が範囲の外でも、配列の外は読まない）
	std::span<const Vector2> GetPoints() const { return {points.data(), static_cast<size_t>(std::clamp(pointCount, 0, static_cast<int>(kMaxArrowPathPoints)))}; }
};

static_assert(std::is_trivially_copyable_v<ArrowStripComponent>);