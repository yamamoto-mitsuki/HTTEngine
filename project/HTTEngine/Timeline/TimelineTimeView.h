#pragma once
#include <cmath>
#include <algorithm>

#include "HTTEngine/Math/MathUtility.h"

/// <summary>
/// Timeline ウィンドウの見ている範囲（時間 ↔ 横のピクセル）。ImGui を知らない計算だけ
/// </summary>
struct TimelineTimeView {
	static constexpr float kMinPixelsPerSecond = 8.0f;    // 一番縮めたとき
	static constexpr float kMaxPixelsPerSecond = 4000.0f; // 一番広げたとき

	float pixelsPerSecond = 120.0f; // 1 秒の幅（ピクセル）
	float scrollTime = 0.0f;        // 左端の時間（秒）

	// 時間 → 左端からのピクセル
	float ToPixel(float time) const { return (time - scrollTime) * pixelsPerSecond; }
	// 左端からのピクセル → 時間
	float ToTime(float pixel) const { return scrollTime + pixel / pixelsPerSecond; }

	// pixel の下の時間が動かないように拡大縮小する
	void ZoomAt(float pixel, float factor) {
		const float time = ToTime(pixel);
		pixelsPerSecond = std::clamp(pixelsPerSecond * factor, kMinPixelsPerSecond, kMaxPixelsPerSecond);
		scrollTime = (std::max)(time - pixel / pixelsPerSecond, 0.0f);
	}

	// pixels の分だけ見ている範囲を動かす（左端は 0 秒まで）
	void Pan(float pixels) { scrollTime = (std::max)(scrollTime + pixels / pixelsPerSecond, 0.0f); }

	// duration 秒全部が width ピクセルに入るようにする
	void Fit(float duration, float width) {
		constexpr float kMargin = 1.1f; // 右端に少し余白を残す
		const float span = (std::max)(duration, 0.1f) * kMargin;
		pixelsPerSecond = std::clamp(width / span, kMinPixelsPerSecond, kMaxPixelsPerSecond);
		scrollTime = 0.0f;
	}

	// 目盛りの間隔（秒）。線の間が minPixels 以上あく、切りの良い値
	float CalcTickStep(float minPixels) const { return MathUtility::CalcNiceStep(minPixels / pixelsPerSecond); }
};

/// <summary>
/// 時間を目盛りへそろえる
/// </summary>
/// <param name="time">時間（秒）</param>
/// <param name="step">目盛りの間隔（秒）。0 以下ならそろえない</param>
inline float SnapTime(float time, float step) { return (step > 0.0f) ? std::round(time / step) * step : time; }