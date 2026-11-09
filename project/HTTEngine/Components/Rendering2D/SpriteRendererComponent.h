#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// 3D空間に画像の板を置く。Entityに付けるComponent（カテゴリ：Rendering2D）
// 板は Entity の XY 平面に立つ（表は -Z 側。Camera を作ったときの向きのまま見える）。裏からも見える
// 大きさは「使う範囲（コマ・Rect）のピクセル数 ÷ Pixels Per Unit」×「Transform の scale」（Unity と同じ）
// 画面に重ねる絵（HUD・ゲージ・ボタン）は Image（ImageComponent）を使う


/// <summary>
/// 画像の板（Unity の Sprite Renderer）
/// </summary>
struct SpriteRendererComponent {
	bool enabled = true;                           // false の間は描かない
	uint32_t textureHandle = 0;                    // 画像（TextureManager::Load の番号。0 なら白い 1×1 の板）。保存はパスで
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};      // 掛ける色（RGBA。0〜1。a を下げると透ける）
	float intensity = 1.0f;                        // 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る）
	float pixelsPerUnit = 100.0f;                  // 画像の何ピクセルを長さ1にするか（100 なら 256 ピクセルの画像が 2.56）
	Vector2 pivot = {0.5f, 0.5f};                  // 板のどこを Entity の位置に合わせるか（0,0=左下 0.5,0=足元 1,1=右上）
	int columns = 1;                               // 画像を横に何コマに分けるか（コマを並べた画像＝スプライトシート）
	int rows = 1;                                  // 縦に何コマに分けるか
	int frame = 0;                                 // 何コマ目を出すか（左上が 0。右へ 1, 2 …、行の終わりで次の行へ）
	bool useRect = false;                          // true: 画像のピクセルの範囲（rectPosition・rectSize）を切り取って使う（columns・rows・frameは使わない）
	Vector2 rectPosition = {0.0f, 0.0f};           // 切り取る範囲の左上（ピクセル。画像の左上が 0,0、右・下が＋）
	Vector2 rectSize = {0.0f, 0.0f};               // 切り取る範囲の大きさ（ピクセル。0 の向きは画像の端まで）
	bool flipX = false;                            // 左右反転（右向きの絵を左向きに）
	bool flipY = false;                            // 上下反転
	BillboardMode billboard = BillboardMode::None; // カメラの方を向く（Full：いつも正面 / AxisY：縦に立てたまま回る）
	BlendMode blend = BlendMode::Normal;           // 重ね方（Normal：透明な所が透ける / Add：足して光る）
	bool alphaClip = false;                        // true：半分より透明な所を捨てて、奥行きを書く（重なった板の前後が崩れない。縁はギザギザ）
};

static_assert(std::is_trivially_copyable_v<SpriteRendererComponent>);