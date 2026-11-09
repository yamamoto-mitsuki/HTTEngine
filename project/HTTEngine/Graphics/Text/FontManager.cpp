#include "FontManager.h"

#include <array>
#include <format>
#include <memory>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <unordered_map>

#include <wrl.h>
#include <d3d12.h>
#include <ShlObj.h>

// stb_truetype（ImGui に入っている物を、ImGui とは別に、このファイルの中だけで使う。ヘッダ1つなので Release でも使える）
// STB_TRUETYPE_IMPLEMENTATION：関数の中身もここで作る / STBTT_STATIC：このファイルの中だけの関数にする（ImGui の中にも同じ名前の関数があるので、ぶつからないように）
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <externals/imgui/imstb_truetype.h>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/UploadContext.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr int kAtlasSize = 2048;      // アトラスの幅・高さ（ピクセル。1文字が 64 ピクセル四方くらいなので、1000文字ほど入る）
constexpr int kGlyphGap = 1;          // アトラスの中で、文字と文字の間をあける（隣の文字が混ざって見えないように）
constexpr uint8_t kOnEdgeValue = 128; // ちょうど縁のピクセルに入れる値（0〜255 の真ん中。シェーダーでは 0.5 に見える）
// 縁から1ピクセル離れるごとに変える値（kSdfPadding 離れると 0（外）か 255（内）になる）
constexpr float kPixelDistScale = static_cast<float>(kOnEdgeValue) / static_cast<float>(FontManager::kSdfPadding);
constexpr size_t kMinFontFileSize = 12; // フォントのファイルの一番小さい大きさ（最初の表の見出し。これより小さい物は読まない）
// 既定のフォント（Windows のフォントのフォルダのファイル名）。上から順に、入っている物を使う
constexpr std::array<std::string_view, 4> kDefaultFontFiles = {"meiryo.ttc", "YuGothM.ttc", "msgothic.ttc", "arial.ttf"};

/// <summary>
/// 読み込んだフォント1つ
/// </summary>
struct Font {
	std::string path;                           // 読んだファイル（ToPathKey の形）
	std::vector<uint8_t> fileData;              // フォントのファイルの中身（info がこの中を指しているので、Font が消えるまで持つ）
	stbtt_fontinfo info{};                      // stb_truetype がフォントを読むための情報
	float scale = 0.0f;                         // フォントの中の長さ → 基準の大きさ（kBaseSize）のピクセル
	FontMetrics metrics;                        // 縦の寸法（基準の大きさのピクセル）
	std::unordered_map<char32_t, Glyph> glyphs; // 作った文字（文字の番号 → 形）。要素の場所は動かないので、参照を返してよい
	// --- アトラス（CPU）。文字を左から右へ並べ、行がいっぱいになったら下の行へ ---
	std::vector<uint8_t> atlasPixels; // 1ピクセル1バイト（kAtlasSize × kAtlasSize）
	int cursorX = 0;                  // 次の文字を置く場所の左
	int cursorY = 0;                  // 今の行の上の端
	int rowHeight = 0;                // 今の行で一番高い文字の高さ（次の行はこの下）
	bool isAtlasFull = false;         // アトラスがいっぱいになった（警告を1回だけ出す）
	// --- アトラス（GPU）---
	Microsoft::WRL::ComPtr<ID3D12Resource> texture; // 1ピクセル1バイトの絵（R8_UNORM）
	uint32_t srvIndex = 0;                          // SRV の番号（シェーダーの gTextures の何番目か）
	bool isDirty = true;                            // GPU へ送っていない変更がある（作った直後も、1回送って状態をそろえる）
	bool isUploaded = false;                        // 1回送った（状態が PIXEL_SHADER_RESOURCE になっている）
};

/// <summary>
/// FontManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct FontState {
	std::vector<std::unique_ptr<Font>> fonts;               // 番号 - 1 番目のフォント（Font の場所が動かないように unique_ptr で持つ）
	std::unordered_map<std::string, uint32_t> handleByPath; // パス → 番号（同じファイルを2回読まない）
	uint32_t defaultFont = 0;                               // 既定のフォントの番号（無ければ 0）
	bool isDefaultSearched = false;                         // 既定のフォントを探した（無かったときに、毎回探さないように）
};
std::unique_ptr<FontState> state;

const Glyph kEmptyGlyph{};         // 知らないフォントの文字（絵も幅も無い）
const FontMetrics kEmptyMetrics{}; // 知らないフォントの寸法
const std::string kEmptyPath;      // 知らない番号のパス

// ===== 番号のフォント（知らない番号なら nullptr）=====
Font* FindFont(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->fonts.size()) {
		return nullptr;
	}
	return state->fonts[handle - 1].get();
}

// ===== Windows が決めているフォルダ（フォントの置き場など）。取れなければ空 =====
std::filesystem::path GetKnownFolder(const KNOWNFOLDERID& id) {
	PWSTR folder = nullptr;
	std::filesystem::path result;
	if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder))) {
		result = folder;
	}
	CoTaskMemFree(folder); // 失敗したときも呼ぶ決まり（nullptr なら何もしない）
	return result;
}

//=============================================================================
// アトラス
//=============================================================================
// ===== アトラスの GPU の絵と SRV を作る（コピーを受ける状態で作り、最初の UploadAtlases で読める状態にする）=====
bool CreateAtlasTexture(Font& font) {
	ID3D12Device* device = DirectXCommon::GetDevice();
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = kAtlasSize;
	desc.Height = kAtlasSize;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R8_UNORM; // 1ピクセル1バイト（距離だけなので色は要らない。シェーダーでは .r で読む）
	desc.SampleDesc.Count = 1;
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&font.texture)))) {
		return false;
	}
	font.texture->SetName(ConvertString("FontAtlas: " + font.path).c_str());

	// --- SRV（シェーダーの gTextures の font.srvIndex 番目に、この絵を置く）---
	font.srvIndex = DirectXCommon::AllocateSRVSlot();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	ID3D12DescriptorHeap* srvHeap = DirectXCommon::GetSRVDescriptorHeap();
	device->CreateShaderResourceView(font.texture.Get(), &srvDesc, DirectXCommon::GetCPUDescriptorHandle(srvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, font.srvIndex));
	return true;
}

// ===== アトラスに width × height の場所を取る（今の行の右に置き、入らなければ次の行の頭へ）。入りきらなければ false =====
bool AllocateAtlasRect(Font& font, int width, int height, int& x, int& y) {
	if (font.cursorX + width > kAtlasSize) {
		font.cursorX = 0;
		font.cursorY += font.rowHeight + kGlyphGap;
		font.rowHeight = 0;
	}
	if (font.cursorY + height > kAtlasSize) {
		return false; // 下まで使い切った
	}
	x = font.cursorX;
	y = font.cursorY;
	font.cursorX += width + kGlyphGap;
	font.rowHeight = (std::max)(font.rowHeight, height);
	return true;
}

// ===== 文字1つの形を作る（SDF の絵をアトラスに写し、置き方を Glyph にまとめる）=====
Glyph MakeGlyph(Font& font, char32_t codepoint) {
	Glyph glyph;
	// フォントの中の番号（フォントに無い文字は 0。0 の形は、たいてい豆腐の四角）
	glyph.index = stbtt_FindGlyphIndex(&font.info, static_cast<int>(codepoint));
	int advance = 0;
	int leftSideBearing = 0; // 使わない（SDF の絵のずれに入っている）
	stbtt_GetGlyphHMetrics(&font.info, glyph.index, &advance, &leftSideBearing);
	glyph.advance = static_cast<float>(advance) * font.scale;

	// --- SDF の絵を作る（各ピクセルに「縁までの距離」。縁が 128、内側ほど大きく、外側ほど小さい）---
	// 空白など形の無い文字は nullptr（幅だけ進める）
	int width = 0;
	int height = 0;
	int offsetX = 0;
	int offsetY = 0;
	unsigned char* sdf = stbtt_GetGlyphSDF(&font.info, font.scale, glyph.index, FontManager::kSdfPadding, kOnEdgeValue, kPixelDistScale, &width, &height, &offsetX, &offsetY);
	if (sdf == nullptr) {
		return glyph;
	}

	// --- アトラスに場所を取って、1行ずつ写す ---
	int x = 0;
	int y = 0;
	if (AllocateAtlasRect(font, width, height, x, y)) {
		for (int row = 0; row < height; ++row) {
			std::copy_n(sdf + static_cast<size_t>(row) * width, width, font.atlasPixels.begin() + static_cast<size_t>(y + row) * kAtlasSize + x);
		}
		const float atlasSize = static_cast<float>(kAtlasSize);
		glyph.uvMin = {static_cast<float>(x) / atlasSize, static_cast<float>(y) / atlasSize};
		glyph.uvMax = {static_cast<float>(x + width) / atlasSize, static_cast<float>(y + height) / atlasSize};
		glyph.size = {static_cast<float>(width), static_cast<float>(height)};
		glyph.offset = {static_cast<float>(offsetX), static_cast<float>(offsetY)};
		font.isDirty = true;
	} else if (!font.isAtlasFull) {
		font.isAtlasFull = true;
		LogManager::Warning(std::format("フォントのアトラスがいっぱいです（これより後の新しい文字は出ません）: {}", font.path));
	}
	stbtt_FreeSDF(sdf, nullptr); // stb_truetype が作った絵を返す
	return glyph;
}

// ===== 名前を変えた・動かしたファイル：覚えているパスを付け替える（番号はそのまま。AssetMoveRegistry から呼ばれる）=====
void OnAssetMoved(const MovedPath& moved) {
	if (state == nullptr) {
		return;
	}
	moved.ApplyToKeys(state->handleByPath);
	for (const std::unique_ptr<Font>& font : state->fonts) {
		moved.Apply(font->path);
	}
}
}; // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void FontManager::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<FontState>();
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	LogManager::Log("Initialized");
}

void FontManager::Release() {
	if (state == nullptr) {
		return;
	}
	for (const std::unique_ptr<Font>& font : state->fonts) {
		DirectXCommon::FreeSRVSlot(font->srvIndex);
	}
	state.reset();
	LogManager::Log("Released");
}


//=============================================================================
// 読み込み
//=============================================================================
uint32_t FontManager::Load(const std::filesystem::path& filePath) {
	HTT_ASSERT_MSG(state != nullptr, "FontManager::Initialize()を先に呼んでください");
	const std::string key = ToPathKey(filePath);
	if (auto it = state->handleByPath.find(key); it != state->handleByPath.end()) {
		return it->second; // 読んだことがある
	}

	// --- ファイルを読んで、stb_truetype に渡す（.ttc は何個かのフォントが入っているので、1つ目の場所を探してから）---
	auto font = std::make_unique<Font>();
	font->path = key;
	font->fileData = ReadFontFile(filePath);
	if (!IsFontData(font->fileData)) {
		LogManager::Error(std::format("フォントを読めませんでした: {}", key));
		return 0;
	}
	stbtt_InitFont(&font->info, font->fileData.data(), stbtt_GetFontOffsetForIndex(font->fileData.data(), 0));

	// --- 大きさ：全角の1文字（em）が kBaseSize ピクセルになる倍率 ---
	font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, kBaseSize);
	int ascent = 0;
	int descent = 0;
	int lineGap = 0;
	stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &lineGap);
	font->metrics = {static_cast<float>(ascent) * font->scale, static_cast<float>(descent) * font->scale, static_cast<float>(lineGap) * font->scale};

	// --- アトラス（中身は文字を使ったときに書き足す）---
	font->atlasPixels.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
	if (!CreateAtlasTexture(*font)) {
		LogManager::Error(std::format("フォントのアトラスを作れませんでした: {}", key));
		return 0;
	}

	state->fonts.push_back(std::move(font));
	const uint32_t handle = static_cast<uint32_t>(state->fonts.size());
	state->handleByPath.emplace(key, handle);
	LogManager::Log(std::format("Loaded font: {} -> {}", key, handle));
	return handle;
}

// ===== フォントの番号からパスを引く（知らない番号なら空）=====
const std::string& FontManager::GetPath(uint32_t handle) {
	const Font* font = FindFont(handle);
	return (font != nullptr) ? font->path : kEmptyPath;
}

// ===== 既定のフォント（Text の Font が (none) のときに使う）。初めて呼ばれたときに、Windows のフォントから探して読む =====
uint32_t FontManager::GetDefaultFont() {
	HTT_ASSERT_MSG(state != nullptr, "FontManager::Initialize()を先に呼んでください");
	if (state->isDefaultSearched) {
		return state->defaultFont;
	}
	state->isDefaultSearched = true;
	// Windows のフォントのフォルダ（全員用。1つ目）から、kDefaultFontFiles の順に探す
	const std::vector<std::filesystem::path> folders = GetSystemFontFolders();
	if (!folders.empty()) {
		for (std::string_view fileName : kDefaultFontFiles) {
			const std::filesystem::path file = folders.front() / fileName;
			std::error_code error;
			if (std::filesystem::exists(file, error)) {
				state->defaultFont = Load(file);
			}
			if (state->defaultFont != 0) {
				break;
			}
		}
	}
	if (state->defaultFont == 0) {
		LogManager::Warning("既定のフォントが見つかりません（Text の Font を選ぶまで、文字は出ません）");
	}
	return state->defaultFont;
}

//=============================================================================
// 文字
//=============================================================================
// ===== 文字1つの形。初めての文字なら、SDF を作ってアトラスに足す（アトラスがいっぱいなら、絵の無い形を返す）=====
const Glyph& FontManager::GetGlyph(uint32_t handle, char32_t codepoint) {
	Font* font = FindFont(handle);
	if (font == nullptr) {
		return kEmptyGlyph;
	}
	if (auto it = font->glyphs.find(codepoint); it != font->glyphs.end()) {
		return it->second; // 作ったことがある
	}
	return font->glyphs.emplace(codepoint, MakeGlyph(*font, codepoint)).first->second;
}

// ===== 2つの文字の間を詰める量（基準の大きさのピクセル。詰めるならマイナス。「AV」「To」など）=====
float FontManager::GetKerning(uint32_t handle, const Glyph& left, const Glyph& right) {
	const Font* font = FindFont(handle);
	if (font == nullptr) {
		return 0.0f;
	}
	return static_cast<float>(stbtt_GetGlyphKernAdvance(&font->info, left.index, right.index)) * font->scale;
}

// ===== フォントの縦の寸法（行の高さを決めるのに使う）=====
const FontMetrics& FontManager::GetMetrics(uint32_t handle) {
	const Font* font = FindFont(handle);
	return (font != nullptr) ? font->metrics : kEmptyMetrics;
}

// ===== アトラスの SRV の番号（シェーダーの gTextures の何番目か）=====
uint32_t FontManager::GetAtlasTextureIndex(uint32_t handle) {
	const Font* font = FindFont(handle);
	return (font != nullptr) ? font->srvIndex : 0;
}


//=============================================================================
// GPU へ送る
//=============================================================================
void FontManager::UploadAtlases() {
	if (state == nullptr) {
		return;
	}
	bool hasUpload = false;
	for (const std::unique_ptr<Font>& font : state->fonts) {
		if (!font->isDirty) {
			continue;
		}
		// アトラス全部を送る（文字が増えるのは初めて使ったときだけなので、毎フレームではない）
		const D3D12_SUBRESOURCE_DATA pixels = {font->atlasPixels.data(), kAtlasSize, static_cast<LONG_PTR>(kAtlasSize) * kAtlasSize};
		// 2回目からは、描画で読んでいる状態（PIXEL_SHADER_RESOURCE）から、コピーを受ける状態に戻してから送る
		const D3D12_RESOURCE_STATES currentState = font->isUploaded ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
		UploadContext::QueueTextureUpload(font->texture.Get(), {&pixels, 1}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, currentState);
		font->isDirty = false;
		font->isUploaded = true;
		hasUpload = true;
	}
	if (hasUpload) {
		UploadContext::Flush(); // 送り終わるまで待つ（この後すぐ描くので）
	}
}


//=============================================================================
// フォントのファイル
//=============================================================================
// ===== ファイルの中身を全部読む（読めなければ空）=====
std::vector<uint8_t> FontManager::ReadFontFile(const std::filesystem::path& filePath) {
	std::error_code error;
	const uintmax_t size = std::filesystem::file_size(filePath, error);
	if (error || size < kMinFontFileSize) {
		return {};
	}
	std::vector<uint8_t> data(static_cast<size_t>(size));
	std::ifstream file(filePath, std::ios::binary);
	if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
		return {};
	}
	return data;
}

// ===== stb_truetype で読めるフォントのデータか（.ttc は1つ目のフォントを見る）=====
bool FontManager::IsFontData(std::span<const uint8_t> data) {
	if (data.size() < kMinFontFileSize) {
		return false;
	}
	// 1つ目のフォントの場所（.ttf・.otf は 0。.ttc は見出しの後ろ）。フォントでなければマイナス
	const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
	stbtt_fontinfo info{};
	return offset >= 0 && stbtt_InitFont(&info, data.data(), offset) != 0;
}

// ===== Windows のフォントの置き場所（全員用と、自分だけに入れた物）。無いフォルダは入れない =====
std::vector<std::filesystem::path> FontManager::GetSystemFontFolders() {
	std::vector<std::filesystem::path> folders;
	const auto addIfExists = [&folders](const std::filesystem::path& folder) {
		std::error_code error;
		if (!folder.empty() && std::filesystem::is_directory(folder, error)) {
			folders.push_back(folder);
		}
	};
	// 全員用（C:/Windows/Fonts）
	addIfExists(GetKnownFolder(FOLDERID_Fonts));
	// 自分だけに入れたフォント（フォントのファイルを右クリック →「インストール」で入る所）
	const std::filesystem::path localAppData = GetKnownFolder(FOLDERID_LocalAppData);
	if (!localAppData.empty()) {
		addIfExists(localAppData / "Microsoft" / "Windows" / "Fonts");
	}
	return folders;
}