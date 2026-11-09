#include "FontPreview.h"

#ifdef USE_IMGUI
#include <vector>
#include <cstdint>
#include <unordered_map>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Graphics/Text/FontManager.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr size_t kMaxFonts = 24;     // 覚えておくフォントの数の上限（超えたら、しばらく使っていない物から捨てる）
constexpr int kMaxLoadsPerFrame = 2; // 1フレームに読むフォントの数（一度にたくさん読むと、一覧を開いた瞬間に止まって見える）
constexpr int kKeepFrames = 2;       // 最後に使ってから、このフレーム数より前の物は捨ててよい（今のフレームと前のフレームで描いた物は捨てない）

/// <summary>
/// 見本のフォント1つ
/// </summary>
struct PreviewFont {
	std::vector<uint8_t> data; // フォントのファイルの中身（ImGui は写さずに中を見るので、フォントを捨てるまで持つ）
	ImFont* font = nullptr;    // ImGui のフォント（読めなかったら nullptr）
	int lastUsedFrame = 0;     // 最後に使ったフレーム（ImGui::GetFrameCount）
};

std::unordered_map<std::string, PreviewFont> fonts; // パス → 見本のフォント
int loadFrame = -1;                                 // loadsThisFrame を数えているフレーム
int loadsThisFrame = 0;                             // このフレームに読んだ数

// しばらく使っていないフォントを、数が上限より少なくなるまで捨てる（古い順）
void RemoveOldFonts(int frame) {
	while (fonts.size() >= kMaxFonts) {
		auto oldest = fonts.end();
		for (auto it = fonts.begin(); it != fonts.end(); ++it) {
			if (it->second.lastUsedFrame <= frame - kKeepFrames && (oldest == fonts.end() || it->second.lastUsedFrame < oldest->second.lastUsedFrame)) {
				oldest = it;
			}
		}
		if (oldest == fonts.end()) {
			return; // 全部、今描いている（捨てられる物が無い）
		}
		if (oldest->second.font != nullptr) {
			ImGui::GetIO().Fonts->RemoveFont(oldest->second.font);
		}
		fonts.erase(oldest);
	}
}
} // namespace

ImFont* FontPreview::Get(const std::string& path) {
	const int frame = ImGui::GetFrameCount();
	if (auto it = fonts.find(path); it != fonts.end()) {
		it->second.lastUsedFrame = frame;
		return it->second.font;
	}
	// --- 初めてのフォント：1フレームに読む数を守る（足りなければ次のフレームに）---
	if (loadFrame != frame) {
		loadFrame = frame;
		loadsThisFrame = 0;
	}
	if (loadsThisFrame >= kMaxLoadsPerFrame) {
		return nullptr;
	}
	RemoveOldFonts(frame);
	if (fonts.size() >= kMaxFonts) {
		return nullptr; // 見えている行が多すぎる（全部描いている途中）
	}
	++loadsThisFrame;

	// --- 読む。ImGui に読めないフォントを渡すと assert で止まるので、先に stb_truetype で読めるか確かめる ---
	PreviewFont& preview = fonts[path];
	preview.lastUsedFrame = frame;
	preview.data = FontManager::ReadFontFile(ToPath(path));
	if (!FontManager::IsFontData(preview.data)) {
		preview.data.clear();
		return nullptr;
	}
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false; // 中身は preview.data が持つ（ImGui に消させない）
	preview.font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(preview.data.data(), static_cast<int>(preview.data.size()), 0.0f, &config);
	return preview.font;
}

bool FontPreview::IsFailed(const std::string& path) {
	auto it = fonts.find(path);
	return it != fonts.end() && it->second.font == nullptr;
}

void FontPreview::Clear() {
	for (auto& [path, preview] : fonts) {
		if (preview.font != nullptr) {
			ImGui::GetIO().Fonts->RemoveFont(preview.font);
		}
	}
	fonts.clear();
}

#endif // USE_IMGUI