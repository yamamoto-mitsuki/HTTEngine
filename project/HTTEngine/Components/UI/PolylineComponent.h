#pragma once
#include <algorithm>
#include <array>
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// 点をつないだ UI の線（閉じれば塗った形）。Entityに付けるComponent（カテゴリ：UI）
// 点は Entity の位置を原点にしたピクセル（Y は上が＋）。Transform の回転 z・大きさ・親子が掛かり、Anchor の所から数える（Image と同じ）
// 点ごとに角の半径を持ち、角を円弧で曲げる（0 ならとがった角。線が太いと、外側は太さの半分の丸になる）
// 値は毎フレーム読むだけ。点・半径・太さが変わった時だけメッシュを作り直す（位置・回転・色・透明度を変えても作り直さない）
//   例：GetComponent<PolylineComponent>()->points[2].y = std::sin(time) * 40.0f;

// 点の数の上限（Component は memcpy で写すので、決まった大きさの配列で持つ）
constexpr int kMaxPolylinePoints = 32;
// 点の数の下限（線は 2 点から）
constexpr int kMinPolylinePoints = 2;

/// <summary>
/// 開いた線の端の形
/// </summary>
enum class PolylineCap {
	Butt,   // 端の点でまっすぐ切る
	Round,  // 端の点を中心に、太さの半分の半円
	Square, // 端の点から、太さの半分だけ伸ばして切る
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(PolylineCap) {
	using enum PolylineCap;
	return MakeEnumTips<PolylineCap>({
	    {Butt, "端の点でまっすぐ切る"},
	    {Round, "端の点を中心に、太さの半分の半円"},
	    {Square, "端の点から、太さの半分だけ伸ばして切る"},
	});
}

/// <summary>
/// 点をつないだ UI の線・形
/// </summary>
struct PolylineComponent {
	bool enabled = true;                        // false の間は描かない
	ScreenAnchor anchor = ScreenAnchor::Center; // 画面のどこから位置を数えるか（Image と同じ）
	int order = 0;                              // 重ねる順（大きいほど手前。Image・Vector Shape・Text の Order と比べる）
	BlendMode blend = BlendMode::Normal;        // 重ね方（Normal・Add など。Image と同じ）
	// --- 形 ---
	bool closed = false; // 最後の点から最初の点へもつなぐ（閉じた形。塗れるのは閉じた形だけ）
	int pointCount = 4;  // 使っている点の数（kMinPolylinePoints〜kMaxPolylinePoints）
	std::array<Vector2, kMaxPolylinePoints> points = {
	    {{-150.0f, 0.0f}, {-50.0f, 60.0f}, {50.0f, -60.0f}, {150.0f, 0.0f}}
    }; // 点（ピクセル。使うのは先頭から pointCount 個）
	std::array<float, kMaxPolylinePoints> radii = {0.0f, 40.0f, 40.0f, 0.0f}; // 点ごとの角の半径（ピクセル。開いた線の両端の点は使わない）
	// --- 塗り・線 ---
	VectorShapeStyle style = VectorShapeStyle::Stroke; // 塗り方（Fill：塗りだけ / Fill And Stroke：塗りと線 / Stroke：線だけ）
	Vector4 color = {0.3f, 0.6f, 1.0f, 1.0f};          // 塗りの色（閉じた形だけ）
	float strokeWidth = 8.0f;                          // 線の太さ（ピクセル）
	Vector4 strokeColor = {1.0f, 1.0f, 1.0f, 1.0f};    // 線の色
	PolylineCap cap = PolylineCap::Round;              // 開いた線の端の形

	// 使っている点の数（pointCount が範囲の外でも、配列の外は読まない）
	int GetCount() const { return std::clamp(pointCount, 0, kMaxPolylinePoints); }
	// 塗るか（閉じた形で、Style が Fill・Fill And Stroke）
	bool HasFill() const { return closed && style != VectorShapeStyle::Stroke; }
	// 線を描くか（Style が Stroke・Fill And Stroke で、太さがある）
	bool HasStroke() const { return style != VectorShapeStyle::Fill && strokeWidth > 0.0f; }
};

static_assert(std::is_trivially_copyable_v<PolylineComponent>);