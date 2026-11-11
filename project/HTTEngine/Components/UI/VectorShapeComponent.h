#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// 画像を使わず、シェーダーで計算して描く UI の図形。Entityに付けるComponent（カテゴリ：UI）
// 置き方は Image と同じ：Transform の translation の x・y が画面のピクセル（ゲームの画面のつもり。Y は上が＋）、
// 回転は Inspector の Rotation の z。Anchor・Pivot・Order・Blend も Image と同じ（Order で Image・Text とも重なる）
// 値は毎フレーム読むだけなので、Script や UI のアニメーションから自由に書き換えてよい（バッファを作り直さない）
//   例：GetComponent<VectorShapeComponent>()->gaugeAmount = value / maxValue;
// Transform の scale を掛けると：Size は x・y それぞれ、角の丸み・縁取り・リングの太さは x・y の小さい方の倍率が掛かる（横に伸ばしても形がゆがまない）

/// <summary>
/// 図形の種類
/// </summary>
enum class VectorShapeType {
	Rectangle,        // 四角形
	RoundedRectangle, // 角丸四角形
	Circle,           // 円（Size の短い方が直径）
	Ellipse,          // 楕円（Size がそのまま横と縦の直径）
	Ring,             // リング（Size の短い方が外の直径）
	Parallelogram,    // 平行四辺形（Size の四角にちょうど入る形）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(VectorShapeType) {
	using enum VectorShapeType;
	return MakeEnumTips<VectorShapeType>({
	    {Rectangle, "四角形"},
	    {RoundedRectangle, "角丸四角形"},
	    {Circle, "円（Size の短い方が直径）"},
	    {Ellipse, "楕円（Size がそのまま横と縦の直径）"},
	    {Ring, "リング（Size の短い方が外の直径）"},
	    {Parallelogram, "平行四辺形（Size の四角にちょうど入る形）"},
	});
}

/// <summary>
/// 塗り方
/// </summary>
enum class VectorShapeStyle {
	Fill,          // 塗りだけ
	FillAndStroke, // 塗りと縁取り
	Stroke,        // 縁取りの線だけ
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(VectorShapeStyle) {
	using enum VectorShapeStyle;
	return MakeEnumTips<VectorShapeStyle>({
	    {Fill, "塗りだけ"},
	    {FillAndStroke, "塗りと線（縁取り）"},
	    {Stroke, "線（縁取り）だけ"},
	});
}

/// <summary>
/// 縁取りを、図形の輪郭のどちら側に付けるか
/// </summary>
enum class VectorShapeStrokeAlign {
	Inside,  // 輪郭の内側（図形の大きさが変わらない）
	Center,  // 輪郭をまたぐ
	Outside, // 輪郭の外側
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(VectorShapeStrokeAlign) {
	using enum VectorShapeStrokeAlign;
	return MakeEnumTips<VectorShapeStrokeAlign>({
	    {Inside, "輪郭の内側（図形の大きさが変わらない）"},
	    {Center, "輪郭をまたぐ"},
	    {Outside, "輪郭の外側"},
	});
}

/// <summary>
/// グラデーションの種類
/// </summary>
enum class VectorShapeGradient {
	None,   // 無し（Color の1色）
	Linear, // 直線
	Radial, // 真ん中から外へ
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(VectorShapeGradient) {
	using enum VectorShapeGradient;
	return MakeEnumTips<VectorShapeGradient>({
	    {None, "無し（Color の1色）"},
	    {Linear, "直線のグラデーション"},
	    {Radial, "真ん中から外へのグラデーション"},
	});
}

/// <summary>
/// ゲージ（図形のどこまで見せるか）の形
/// </summary>
enum class VectorShapeGauge {
	None,   // 全部見せる
	Linear, // 直線で切る
	Radial, // 真ん中のまわりに扇形で切る
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(VectorShapeGauge) {
	using enum VectorShapeGauge;
	return MakeEnumTips<VectorShapeGauge>({
	    {None, "全部見せる"},
	    {Linear, "直線で切る（Direction の向きに伸びる）"},
	    {Radial, "真ん中のまわりに扇形で切る"},
	});
}

/// <summary>
/// 直線のゲージが伸びる向き
/// </summary>
enum class VectorShapeGaugeDirection {
	LeftToRight, // 左から右へ
	RightToLeft, // 右から左へ
	BottomToTop, // 下から上へ
	TopToBottom, // 上から下へ
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(VectorShapeGaugeDirection) {
	using enum VectorShapeGaugeDirection;
	return MakeEnumTips<VectorShapeGaugeDirection>({
	    {LeftToRight, "左から右へ伸びる"},
	    {RightToLeft, "右から左へ伸びる"},
	    {BottomToTop, "下から上へ伸びる"},
	    {TopToBottom, "上から下へ伸びる"},
	});
}

/// <summary>
/// 画像を使わない UI の図形
/// </summary>
struct VectorShapeComponent {
	bool enabled = true;                                       // false の間は描かない（マウスも当たらない）
	VectorShapeType shape = VectorShapeType::RoundedRectangle; // 図形の種類
	Vector2 size = {100.0f, 100.0f};                           // 大きさ（ピクセル）
	ScreenAnchor anchor = ScreenAnchor::Center;                // 画面のどこから位置を数えるか（Image と同じ）
	Vector2 pivot = {0.5f, 0.5f};                              // 図形のどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）
	int order = 0;                                             // 重ねる順（大きいほど手前。Image・Text の Order と比べる）
	BlendMode blend = BlendMode::Normal;                       // 重ね方（Normal・Add など。Image と同じ）
	// --- 形 ---
	Vector4 cornerRadius = {16.0f, 16.0f, 16.0f, 16.0f}; // 角の半径（ピクセル。x=左上 y=右上 z=右下 w=左下。平行四辺形は x だけ使う）
	float ringThickness = 16.0f;                         // リングの太さ（ピクセル）
	float skewAngle = 20.0f;                             // 平行四辺形の傾き（度。正で上の辺が右へずれる）
	// --- 塗り ---
	VectorShapeStyle style = VectorShapeStyle::Fill; // 塗り方
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};        // 塗りの色（グラデーションの始まりの色）
	// --- 縁取り ---
	float strokeWidth = 4.0f;                                            // 縁取りの太さ（ピクセル）
	VectorShapeStrokeAlign strokeAlign = VectorShapeStrokeAlign::Inside; // 縁取りを輪郭のどちら側に付けるか
	Vector4 strokeColor = {0.0f, 0.0f, 0.0f, 1.0f};                      // 縁取りの色
	bool strokeUsesGradient = false;                                     // 縁取りにも塗りのグラデーションを使う（Stroke Color を使わない）
	// --- グラデーション ---
	VectorShapeGradient gradient = VectorShapeGradient::None; // グラデーションの種類
	Vector4 gradientColor = {0.3f, 0.6f, 1.0f, 1.0f};         // 終わりの色
	float gradientAngle = 0.0f;                               // 直線の向き（度。0 で左から右、90 で下から上）
	Vector2 gradientRange = {0.0f, 1.0f};                     // 色が変わる範囲（図形の中の場所。0〜1。x までは始まりの色、y から先は終わりの色）
	// --- ゲージ ---
	VectorShapeGauge gauge = VectorShapeGauge::None;                                   // ゲージの形
	float gaugeAmount = 1.0f;                                                          // 見せる割合（0〜1）
	VectorShapeGaugeDirection gaugeDirection = VectorShapeGaugeDirection::LeftToRight; // 直線のゲージが伸びる向き
	float gaugeStartAngle = 0.0f;                                                      // 扇形の始まりの角度（度。0 で真上、時計回りが＋）
	float gaugeSweep = 360.0f;                                                         // Amount が 1 のときの扇形の広さ（度。270 なら下が開いたメーター）
	bool gaugeClockwise = true;                                                        // 扇形が時計回りに伸びる
};

static_assert(std::is_trivially_copyable_v<VectorShapeComponent>);