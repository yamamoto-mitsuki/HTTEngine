#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/String/FixedString.h"

// 画面に重ねる文字。Entityに付けるComponent（カテゴリ：UI）
// 置き方は Image と同じ：Transform の translation の x・y が画面のピクセル（ゲームの画面のつもり。Y は上が＋）、
// 回転は Inspector の Rotation の z、大きさには scale の x・y も掛かる。Anchor・Pivot・Order も Image と同じ（Order で Image とも重なる）
// 文字は Script から書き換える：GetComponent<TextComponent>()->text = std::format("SCORE {}", score_);
// 文字の形は SDF（縁までの距離の画像）にして描くので、大きくしてもぼやけない。縁取り・影も付けられる
// 演出：光（グロー）・斜めに通る光（シェーダー）、文字1つずつの動き 波・跳ねる・震える（頂点を動かす）

/// <summary>
/// 行のそろえ方（文字が何行かあるとき、どちらの端にそろえるか）
/// </summary>
enum class TextAlign {
	Left,   // 左にそろえる
	Center, // 真ん中にそろえる
	Right,  // 右にそろえる
};

/// <summary>
/// 文字1つずつの動き（どれも、文字ごとに少しずつ時間をずらして動かす）
/// </summary>
enum class TextMotion {
	None,   // 動かない
	Wave,   // 波
	Bounce, // 跳ねる
	Shake,  // 震える
};

/// <summary>
/// 画面に重ねる文字（Unity の UI の Text）
/// </summary>
struct TextComponent {
	bool enabled = true;                             // false の間は描かない
	FixedString<256> text = "Text";                  // 出す文字（UTF-8。\n で改行。255 バイト＝日本語で85文字くらいまで）
	uint32_t fontHandle = 0;                         // フォント（FontManager::Load の番号。0 なら既定のフォント＝メイリオ）。保存はパスで
	float fontSize = 36.0f;                          // 文字の大きさ（ピクセル。全角の1文字がだいたいこの幅）
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};        // 文字の色（RGBA。0〜1）
	TextAlign align = TextAlign::Center;             // 行のそろえ方
	ScreenAnchor anchor = ScreenAnchor::Center;      // 画面のどこから位置を数えるか（Image と同じ）
	Vector2 pivot = {0.5f, 0.5f};                    // 文字のかたまりのどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）
	float lineSpacing = 1.0f;                        // 行の間の倍率（1 でフォントの決めた間。小さくすると詰まる）
	float letterSpacing = 0.0f;                      // 文字と文字の間に足す（ピクセル。マイナスで詰める）
	float outlineWidth = 0.0f;                       // 縁取りの太さ（ピクセル。0 なら無し。文字の大きさの 1/6 くらいまで）
	Vector4 outlineColor = {0.0f, 0.0f, 0.0f, 1.0f}; // 縁取りの色
	bool shadow = false;                             // 影を付ける（同じ文字を、ずらして先に描く）
	Vector2 shadowOffset = {3.0f, -3.0f};            // 影のずれ（ピクセル。右下なら x が＋、y が−）
	Vector4 shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};  // 影の色
	// --- 演出（TextEffects.md）---
	float glowWidth = 0.0f;                        // 光（グロー）の広さ（ピクセル。縁取りの外へ。0 なら無し。縁取りと合わせて、文字の大きさの 1/6 くらいまで）
	Vector4 glowColor = {1.0f, 0.8f, 0.3f, 0.8f};  // 光の色
	bool shine = false;                            // 斜めの光が、文字の上を左から右へ通る
	Vector4 shineColor = {1.0f, 1.0f, 1.0f, 0.9f}; // 斜めの光の色（a で強さ）
	float shineWidth = 24.0f;                      // 斜めの光の帯の半分の幅（ピクセル）
	float shineDuration = 0.6f;                    // 光が端から端まで通る秒数
	float shineInterval = 2.0f;                    // 光が通ってから、次に通るまでの秒数
	TextMotion motion = TextMotion::None;          // 文字1つずつの動き
	float motionAmount = 8.0f;                     // 動く大きさ（ピクセル）
	float motionSpeed = 2.0f;                      // 動く速さ（1秒に何回）
	float motionSpread = 0.1f;                     // 隣の文字との、動きのずれ（1回の動きの割合。0.1 なら隣は 1/10 回分遅れる。0 なら全部一緒）
	int order = 0;                                 // 重ねる順（大きいほど手前。Image の Order と比べる）
	BlendMode blend = BlendMode::Normal;           // 重ね方（Normal：透明な所が透ける / Add：足して光る）
};

static_assert(std::is_trivially_copyable_v<TextComponent>);