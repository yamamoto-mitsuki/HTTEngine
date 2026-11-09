#include "GameScreen.h"

#include <algorithm>
#include <cmath>

#include "HTTEngine/Diagnostics/HTTAssert.h"

//=============================================================================
// 初期化
//=============================================================================
void GameScreen::Initialize(int width, int height) {
	HTT_ASSERT_MSG(width > 0 && height > 0, "WindowConfig の gameWidth・gameHeight は 1 以上にしてください");
	width_ = static_cast<float>(width);
	height_ = static_cast<float>(height);
}

//=============================================================================
// 大きさの計算
//=============================================================================
// ===== 描画先のテクスチャの大きさ =====
RenderSize GameScreen::CalcRenderSize(uint32_t longSide) {
	const float scale = static_cast<float>(longSide) / (std::max)(width_, height_);
	return {static_cast<uint32_t>(std::lround(width_ * scale)), static_cast<uint32_t>(std::lround(height_ * scale))};
}

// ===== 黒帯の内側 =====
ViewRect GameScreen::CalcFitRect(float outerWidth, float outerHeight) {
	// 幅でそろえる倍率と、高さでそろえる倍率の小さい方なら、どちらの辺もはみ出さない
	const float scale = (std::min)(outerWidth / width_, outerHeight / height_);
	const float width = std::round(width_ * scale);
	const float height = std::round(height_ * scale);
	return {std::floor((outerWidth - width) * 0.5f), std::floor((outerHeight - height) * 0.5f), width, height};
}