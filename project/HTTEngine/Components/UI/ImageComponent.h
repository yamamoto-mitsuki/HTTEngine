#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// 画面に重ねる絵（HUD・ゲージ・ボタン・フェードの幕）。Entityに付けるComponent（カテゴリ：UI）
// 位置は Transform の translation の x・y を「画面のピクセル」として読む（ゲームの画面のつもりで置く。ウィンドウの大きさが変わっても同じ割合で出る）
// Y は上が＋（3D と同じ）。回転は Inspector の Rotation の z（Z 軸のまわり）、大きさには scale の x・y も掛かる。親子にすると一緒に動く（UI をまとめる親を作るとよい）
// 3D空間に置く板は Sprite Renderer（SpriteRendererComponent）を使う

/// <summary>
/// 画面のどこから位置を数えるか（Unity の Anchor）
/// </summary>
enum class ScreenAnchor {
	TopLeft,     // 左上
	Top,         // 上の真ん中
	TopRight,    // 右上
	Left,        // 左の真ん中
	Center,      // 真ん中
	Right,       // 右の真ん中
	BottomLeft,  // 左下
	Bottom,      // 下の真ん中
	BottomRight, // 右下
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ScreenAnchor) {
	using enum ScreenAnchor;
	return MakeEnumTips<ScreenAnchor>({
	    {TopLeft, "画面の左上から数える"},
	    {Top, "画面の上の真ん中から数える"},
	    {TopRight, "画面の右上から数える"},
	    {Left, "画面の左の真ん中から数える"},
	    {Center, "画面の真ん中から数える"},
	    {Right, "画面の右の真ん中から数える"},
	    {BottomLeft, "画面の左下から数える"},
	    {Bottom, "画面の下の真ん中から数える"},
	    {BottomRight, "画面の右下から数える"},
	});
}

/// <summary>
/// 画面に重ねる絵（Unity の UI の Image）
/// </summary>
struct ImageComponent {
	bool enabled = true;                        // false の間は描かない（マウスも当たらない）
	uint32_t textureHandle = 0;                 // 画像（TextureManager::Load の番号。0 なら白い四角。黒くすればフェードの幕）。保存はパスで
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};   // 掛ける色（RGBA。0〜1。a を動かすとフェード）
	Vector2 size = {100.0f, 100.0f};            // 大きさ（ピクセル）。Inspector の Set Native Size で、画像の使う範囲（1コマ・Rect）と同じ大きさになる
	ScreenAnchor anchor = ScreenAnchor::Center; // 画面のどこから位置を数えるか（TopLeft なら、左上の角から右へ x・上へ y）
	Vector2 pivot = {0.5f, 0.5f};               // 絵のどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）
	int columns = 1;                            // 画像を横に何コマに分けるか（コマを並べた画像）
	int rows = 1;                               // 縦に何コマに分けるか
	int frame = 0;                              // 何コマ目を出すか（左上が 0。右へ 1, 2 …、行の終わりで次の行へ）
	bool useRect = false;                       // true：画像のピクセルの範囲（rectPosition・rectSize）を切り取って使う（columns・rows・frame は使わない）
	Vector2 rectPosition = {0.0f, 0.0f};        // 切り取る範囲の左上（ピクセル。画像の左上が 0,0、右・下が＋）
	Vector2 rectSize = {0.0f, 0.0f};            // 切り取る範囲の大きさ（ピクセル。0 の向きは画像の端まで）
	bool flipX = false;                         // 左右反転
	bool flipY = false;                         // 上下反転
	float fill = 1.0f;                          // 左から何割まで出すか（0〜1。体力・インクのゲージに）
	int order = 0;                              // 重ねる順（大きいほど手前。同じなら先に作った物が奥）
	BlendMode blend = BlendMode::Normal;        // 重ね方（Normal：透明な所が透ける / Add：足して光る）
	// --- 繰り返し（模様）---
	bool tiled = false;                // true：画像全体を tileSize ごとに並べて繰り返す（Sheet・Rect は使わない）
	Vector2 tileSize = {0.0f, 0.0f};   // 1枚の大きさ（ピクセル。0 の向きは画像の大きさ）
	Vector2 tileOffset = {0.0f, 0.0f}; // 模様をずらす量（1 で1枚分。動かすと模様が流れる）
};

static_assert(std::is_trivially_copyable_v<ImageComponent>);