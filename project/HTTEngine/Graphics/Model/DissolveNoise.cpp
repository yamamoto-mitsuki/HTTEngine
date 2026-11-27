#include "DissolveNoise.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include <externals/DirectXTex/DirectXTex.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
/// <summary>
/// CPU に持つ画像1枚（灰色。1画素 1 バイト、上の行から）
/// </summary>
struct GrayImage {
	std::vector<uint8_t> pixels; // 空なら読めなかった（もう一度は読みに行かない）
	int width = 0;
	int height = 0;
};

std::unordered_map<std::string, GrayImage> images; // 画像のパス → 画素（同じ画像は1回だけ読む）


//=============================================================================
// 読み込み
//=============================================================================
// ===== ファイルから灰色の画素を読む（読めなければ pixels が空）=====
// sRGB として扱わずに読む（ファイルの数そのまま。GPU 側は sRGB に戻してから比べるので、同じ数になる）
GrayImage LoadGray(const std::string& path) {
	GrayImage result;
	DirectX::ScratchImage loaded;
	if (FAILED(DirectX::LoadFromWICFile(ToPath(path).c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, loaded))) {
		LogManager::Warning(std::format("ディゾルブの画像を CPU で読めません（{}）。ファイルとして無い画像（モデルに埋め込みの画像など）は、境目から粒を出すのに使えません", path));
		return result;
	}
	// --- 赤だけ 8 ビットにそろえる（png が RGBA でも灰色でも、1画素 1 バイトに）---
	const DirectX::Image& source = *loaded.GetImage(0, 0, 0);
	DirectX::ScratchImage gray;
	if (source.format == DXGI_FORMAT_R8_UNORM) {
		gray = std::move(loaded); // もう灰色なら直さない
	} else if (FAILED(DirectX::Convert(source, DXGI_FORMAT_R8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, gray))) {
		LogManager::Warning(std::format("ディゾルブの画像を灰色に直せません（{}）", path));
		return result;
	}
	const DirectX::Image& image = *gray.GetImage(0, 0, 0);
	result.width = static_cast<int>(image.width);
	result.height = static_cast<int>(image.height);
	result.pixels.resize(image.width * image.height);
	// 1行ずつ写す（行の後ろに詰め物が付くことがあるので、rowPitch ずつ進める）
	for (size_t y = 0; y < image.height; ++y) {
		std::memcpy(result.pixels.data() + y * image.width, image.pixels + y * image.rowPitch, image.width);
	}
	return result;
}

// ===== 画像を探し、無ければ読む =====
const GrayImage& FindOrLoad(uint32_t textureHandle) {
	const std::string path = (textureHandle != 0) ? TextureManager::GetTexturePath(textureHandle) : std::string(kDefaultDissolveNoisePath);
	auto it = images.find(path);
	if (it == images.end()) {
		it = images.emplace(path, LoadGray(path)).first; // 読めなくても空のまま入れる（毎回ファイルを開きに行かない・警告も1回）
	}
	return it->second;
}

// ===== 1画素（WRAP：はみ出したら反対側。0〜1）=====
float Texel(const GrayImage& image, int x, int y) {
	x = ((x % image.width) + image.width) % image.width; // 負も 0〜幅-1 に（% は負なら負を返すので、幅を足してもう一度）
	y = ((y % image.height) + image.height) % image.height;
	return static_cast<float>(image.pixels[static_cast<size_t>(y) * image.width + x]) / 255.0f;
}
} // namespace


//=============================================================================
// 外から使う
//=============================================================================
// ===== 画像の値（GPU の Linear フィルター・WRAP と同じ読み方）=====
bool DissolveNoise::Evaluate(uint32_t textureHandle, const Vector2& uv, int scale, float& value) {
	const GrayImage& image = FindOrLoad(textureHandle);
	if (image.pixels.empty()) {
		return false;
	}
	const float repeat = static_cast<float>((std::max)(scale, 1));
	// 画素の真ん中が整数になるようにずらす（GPU は、画素 x の真ん中を UV で (x + 0.5) ÷ 幅 の所とする）
	const float px = uv.x * repeat * static_cast<float>(image.width) - 0.5f;
	const float py = uv.y * repeat * static_cast<float>(image.height) - 0.5f;
	const float cellX = std::floor(px);
	const float cellY = std::floor(py);
	const float fx = px - cellX; // 右の画素へどれだけ寄っているか
	const float fy = py - cellY; // 下の画素へどれだけ寄っているか
	const int x = static_cast<int>(cellX);
	const int y = static_cast<int>(cellY);
	const float top = MathUtility::Lerp(Texel(image, x, y), Texel(image, x + 1, y), fx);
	const float bottom = MathUtility::Lerp(Texel(image, x, y + 1), Texel(image, x + 1, y + 1), fx);
	value = MathUtility::Lerp(top, bottom, fy);
	return true;
}

float DissolveNoise::Cut(float amount, float edgeWidth) { return amount * (1.0f + edgeWidth) - edgeWidth; }

void DissolveNoise::Clear() { images.clear(); }
