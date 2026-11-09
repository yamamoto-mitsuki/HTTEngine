#ifdef USE_IMGUI
#include "AssetIcons.h"

#include <array>
#include <cfloat>
#include <filesystem>
#include <format>
#include <string>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Graphics/Texture/TextureManager.h"

namespace {
// アイコンの画像の置き場所（エンジンの Resources/Editor/Icons/ が、ビルドのたびにゲームへ写される）
constexpr const char* kIconFolder = "HTTEngine/Resources/Editor/Icons/";
constexpr size_t kIconCount = magic_enum::enum_count<AssetIcon>(); // アイコンの種類の数

// 種類ごとの画像（ImTextureID_Invalid なら画像なし。添え字は AssetIcon の並びの番号）
std::array<ImTextureID, kIconCount> iconTextures{};

// 画像が無いときの見た目
struct Fallback {
	ImU32 color;        // 形の色
	const char* letter; // 真ん中に出す文字（空なら出さない）
};
Fallback FallbackOf(AssetIcon icon) {
	switch (icon) {
	case AssetIcon::Folder:
	case AssetIcon::FolderOpen:
		return {IM_COL32(222, 184, 92, 255), ""};
	case AssetIcon::Scene:
		return {IM_COL32(80, 130, 210, 255), "S"};
	case AssetIcon::Model:
		return {IM_COL32(60, 160, 170, 255), "M"};
	case AssetIcon::Texture:
		return {IM_COL32(170, 90, 170, 255), "T"};
	case AssetIcon::Sound:
		return {IM_COL32(90, 160, 80, 255), "♪"};
	case AssetIcon::Code:
		return {IM_COL32(70, 110, 190, 255), "C"};
	case AssetIcon::File:
		return {IM_COL32(120, 120, 120, 255), ""};
	}
	return {IM_COL32(120, 120, 120, 255), ""};
}

// フォルダの形（左上の出っぱり＋本体）
void DrawFolderShape(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 color) {
	const float width = max.x - min.x;
	const float height = max.y - min.y;
	const float rounding = width * 0.08f;
	drawList->AddRectFilled(ImVec2(min.x + width * 0.08f, min.y + height * 0.14f), ImVec2(min.x + width * 0.46f, min.y + height * 0.34f), color, rounding);
	drawList->AddRectFilled(ImVec2(min.x + width * 0.08f, min.y + height * 0.24f), ImVec2(max.x - width * 0.08f, max.y - height * 0.14f), color, rounding);
}

// ファイルの形（縦長の四角＋真ん中に文字）
void DrawFileShape(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const Fallback& fallback) {
	const float width = max.x - min.x;
	const float height = max.y - min.y;
	const ImVec2 paperMin(min.x + width * 0.18f, min.y + height * 0.08f);
	const ImVec2 paperMax(max.x - width * 0.18f, max.y - height * 0.08f);
	drawList->AddRectFilled(paperMin, paperMax, fallback.color, width * 0.08f);
	// 文字は、小さすぎて読めないときは出さない
	const float fontSize = height * 0.45f;
	if (fallback.letter[0] == '\0' || fontSize < 8.0f) {
		return;
	}
	const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fallback.letter);
	const ImVec2 textPos((paperMin.x + paperMax.x - textSize.x) * 0.5f, (paperMin.y + paperMax.y - textSize.y) * 0.5f);
	drawList->AddText(nullptr, fontSize, textPos, IM_COL32_WHITE, fallback.letter);
}
} // namespace

//=============================================================================
// 画像を読む
//=============================================================================
void AssetIcons::Initialize() {
	for (const AssetIcon icon : magic_enum::enum_values<AssetIcon>()) {
		const std::string path = std::format("{}{}.png", kIconFolder, magic_enum::enum_name(icon));
		std::error_code error;
		if (!std::filesystem::exists(path, error)) {
			continue; // まだ置いていない（形で代わりに描く）
		}
		// 小さい画像なので、ふつうのテクスチャとして読む（ミップマップもできるので、小さく描いてもきれい）
		const uint32_t srvIndex = TextureManager::Load(path);
		if (const TextureManager::TextureData* data = TextureManager::GetTextureData(srvIndex)) {
			iconTextures[static_cast<size_t>(icon)] = static_cast<ImTextureID>(data->srvHandleGPU.ptr);
		}
	}
	// 開いたフォルダの画像が無ければ、閉じたフォルダの画像を使う
	ImTextureID& folderOpen = iconTextures[static_cast<size_t>(AssetIcon::FolderOpen)];
	if (folderOpen == ImTextureID_Invalid) {
		folderOpen = iconTextures[static_cast<size_t>(AssetIcon::Folder)];
	}
}

//=============================================================================
// 描く
//=============================================================================
void AssetIcons::Draw(ImDrawList* drawList, AssetIcon icon, const ImVec2& min, const ImVec2& max) {
	// --- 画像があれば、それを貼る ---
	const ImTextureID texture = iconTextures[static_cast<size_t>(icon)];
	if (texture != ImTextureID_Invalid) {
		drawList->AddImage(texture, min, max);
		return;
	}
	// --- 無ければ、形で代わりに描く ---
	const Fallback fallback = FallbackOf(icon);
	if (icon == AssetIcon::Folder || icon == AssetIcon::FolderOpen) {
		DrawFolderShape(drawList, min, max, fallback.color);
	} else {
		DrawFileShape(drawList, min, max, fallback);
	}
}
#endif