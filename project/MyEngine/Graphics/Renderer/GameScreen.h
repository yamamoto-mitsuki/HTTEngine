#pragma once
#include <cstdint>

#include "MyEngine/Math/Vector2.h"

// ゲームの画面の大きさ。縦横比と、UI の位置・大きさのピクセルの基準を決める
// main.cpp の WindowConfig（gameWidth・gameHeight）で決めて、起動の後は変えない（描画先のテクスチャの大きさがこの比率で決まるので）
//   横長（既定）：config.gameWidth = 1280; config.gameHeight = 720;
//   縦長　　　　：config.gameWidth = 720;  config.gameHeight = 1280;
// ウィンドウの大きさ（width・height）とは別の値。比が違うウィンドウでは、製品版は上下か左右に黒帯が出る

/// <summary>
/// 画面の中の四角（左上の位置と大きさ。ピクセル。Y は下が＋）
/// </summary>
struct ViewRect {
	float x = 0.0f;      // 左の端
	float y = 0.0f;      // 上の端
	float width = 0.0f;  // 幅
	float height = 0.0f; // 高さ
};

/// <summary>
/// 描画先のテクスチャの大きさ（ピクセル）
/// </summary>
struct RenderSize {
	uint32_t width = 0;  // 幅
	uint32_t height = 0; // 高さ
};


/// <summary>
/// ゲームの画面の大きさ（ゲーム全体で1つ）
/// </summary>
class GameScreen {
public:
	// 画面の大きさを決める（Engine::Initialize の頭で1回。width・height は 1 以上）
	static void Initialize(int width, int height);

	static float GetWidth() { return width_; }                 // 幅（UI のピクセル）
	static float GetHeight() { return height_; }               // 高さ（UI のピクセル）
	static float GetAspectRatio() { return width_ / height_; } // 縦横比（幅 ÷ 高さ）
	static Vector2 GetHalfSize() { return {width_ * 0.5f, height_ * 0.5f}; } // 幅・高さの半分（UI のピクセル。画面の真ん中から端まで）

	// ===== 画面の位置 ↔ UI のピクセル =====
	// 画面の位置：真ん中が 0、右と上が＋、端が ±1（InputManager::GetMousePositionInGame・Camera::ScreenPointToRay と同じ数え方）
	// UI のピクセル：真ん中が 0、右と上が＋（Anchor が Center の UI の Translation と同じ数え方）
	// どちらも真ん中が 0 なので、位置にも量（動いた分・大きさ）にもそのまま使える

	/// <summary>
	/// 画面の位置（端が ±1）→ UI のピクセル（真ん中が 0、上が＋）。量にも使える
	/// </summary>
	static Vector2 NdcToPixel(const Vector2& ndc) { return {ndc.x * width_ * 0.5f, ndc.y * height_ * 0.5f}; }

	/// <summary>
	/// UI のピクセル（真ん中が 0、上が＋）→ 画面の位置（端が ±1）。量にも使える
	/// </summary>
	static Vector2 PixelToNdc(const Vector2& pixel) { return {pixel.x / (width_ * 0.5f), pixel.y / (height_ * 0.5f)}; }

	/// <summary>
	/// 長い方の辺を longSide ピクセルにした、画面と同じ比率の大きさ（ポストエフェクトの描画先に使う）
	/// </summary>
	static RenderSize CalcRenderSize(uint32_t longSide);

	/// <summary>
	/// 幅 outerWidth・高さ outerHeight の四角の中に、画面の比率を保って一番大きく入れた四角（真ん中に寄せる。残りが黒帯）
	/// <para>比が同じなら、外の四角と同じ大きさになる。ピクセルの境目にそろえる</para>
	/// </summary>
	static ViewRect CalcFitRect(float outerWidth, float outerHeight);

private:
	static inline float width_ = 1280.0f;
	static inline float height_ = 720.0f;
};