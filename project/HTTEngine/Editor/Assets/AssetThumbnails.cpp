#ifdef USE_IMGUI
#include "AssetThumbnails.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <wincodec.h>
#include <wrl.h>

#include <externals/DirectXTex/DirectXTex.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/UploadContext.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr size_t kMaxThumbnails = 48; // 覚えておく数の上限（1枚で SRV のスロットを1つ使う。超えたら、しばらく描いていない物から捨てる）
constexpr int kLoadsPerFrame = 1;     // 1フレームに作る数

// 1枚分
struct Thumbnail {
	Microsoft::WRL::ComPtr<ID3D12Resource> resource; // 小さい絵（作れなかったら空）
	uint32_t srvSlot = 0;                            // SRV のスロット（0 なら作れなかった）
	ImVec2 size;                                     // 絵のピクセル数
	std::filesystem::file_time_type writeTime;       // 作ったときのファイルの更新時刻
	int lastUsedFrame = 0;                           // 最後に描いたフレーム（ImGui のフレーム番号）
};
std::unordered_map<std::string, Thumbnail> thumbnails; // パス → 小さい絵
int loadFrame = -1;                                    // loadCount を数えているフレーム
int loadCount = 0;                                     // そのフレームに作った数

// 長い辺が kThumbnailSize になるように縮めた大きさ（元の方が小さければそのまま）
std::pair<uint32_t, uint32_t> FitSize(size_t width, size_t height) {
	const size_t longSide = (std::max)(width, height);
	if (longSide <= AssetThumbnails::kThumbnailSize) {
		return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
	}
	const double scale = static_cast<double>(AssetThumbnails::kThumbnailSize) / static_cast<double>(longSide);
	return {(std::max)(1u, static_cast<uint32_t>(width * scale)), (std::max)(1u, static_cast<uint32_t>(height * scale))};
}

//=============================================================================
// 読む（CPU）。どちらも R8G8B8A8_UNORM_SRGB・長い辺 kThumbnailSize 以下の絵を image に作る
//=============================================================================
// png・jpg・bmp：WIC（Windows の画像の読み込み）で、縮めながら読む
// JPEG は読みながら 1/2・1/4・1/8 に縮められるので、4K の画像でも元の大きさで読むより速い
bool LoadWic(const std::wstring& path, DirectX::ScratchImage& image) {
	bool isWic2 = false;
	IWICImagingFactory* factory = DirectX::GetWICFactory(isWic2); // DirectXTex が作った物を借りる（こちらでは解放しない）
	if (factory == nullptr) {
		return false;
	}
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame))) {
		return false;
	}
	UINT width = 0;
	UINT height = 0;
	frame->GetSize(&width, &height);
	const auto [thumbnailWidth, thumbnailHeight] = FitSize(width, height);

	// 縮める → 1ピクセル RGBA 8bit ずつにそろえる
	Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
	Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), thumbnailWidth, thumbnailHeight, WICBitmapInterpolationModeFant))) {
		return false;
	}
	if (FAILED(factory->CreateFormatConverter(&converter)) ||
	    FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
		return false;
	}
	// png・jpg の色は sRGB で書かれているので、_SRGB の形として受け取る（TextureManager と同じ）
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, thumbnailWidth, thumbnailHeight, 1, 1))) {
		return false;
	}
	const DirectX::Image* pixels = image.GetImage(0, 0, 0);
	return SUCCEEDED(converter->CopyPixels(nullptr, static_cast<UINT>(pixels->rowPitch), static_cast<UINT>(pixels->slicePitch), pixels->pixels));
}

// hdr：DirectXTex で元の大きさのまま読んでから縮める（WIC は .hdr を読めない。大きい空の画像は少し時間がかかる）
bool LoadHdr(const std::wstring& path, DirectX::ScratchImage& image) {
	DirectX::ScratchImage hdr;
	if (FAILED(DirectX::LoadFromHDRFile(path.c_str(), nullptr, hdr))) {
		return false;
	}
	const auto [thumbnailWidth, thumbnailHeight] = FitSize(hdr.GetMetadata().width, hdr.GetMetadata().height);
	DirectX::ScratchImage resized;
	if (FAILED(DirectX::Resize(*hdr.GetImage(0, 0, 0), thumbnailWidth, thumbnailHeight, DirectX::TEX_FILTER_DEFAULT, resized))) {
		return false;
	}
	// 明るさ 1 を超える所は白になる（小さい絵で見分けがつけばよいので、明るさの調整はしない）
	return SUCCEEDED(DirectX::Convert(*resized.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, image));
}

//=============================================================================
// GPU に置く
//=============================================================================
bool Upload(const DirectX::ScratchImage& image, Thumbnail& thumbnail) {
	const DirectX::TexMetadata& metadata = image.GetMetadata();
	ID3D12Device* device = DirectXCommon::GetDevice();

	// --- 置き場所（DEFAULT ヒープ。コピーを受ける状態で作る）---
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = metadata.width;
	desc.Height = static_cast<UINT>(metadata.height);
	desc.DepthOrArraySize = 1;
	desc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
	desc.Format = metadata.format;
	desc.SampleDesc.Count = 1;
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&thumbnail.resource)))) {
		return false;
	}

	// --- ミップマップの段ごとの中身を送る（描画とは別のコマンドリストなので、ImGui を描いている途中でもよい）---
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	for (size_t i = 0; i < image.GetImageCount(); ++i) {
		const DirectX::Image& level = image.GetImages()[i];
		subresources.push_back({level.pixels, static_cast<LONG_PTR>(level.rowPitch), static_cast<LONG_PTR>(level.slicePitch)});
	}
	UploadContext::QueueTextureUpload(thumbnail.resource.Get(), subresources, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	UploadContext::Flush(); // 送り終わるまで待つ（この後すぐ ImGui が描くので）

	// --- SRV（ImGui::Image に渡す番号）---
	thumbnail.srvSlot = DirectXCommon::AllocateSRVSlot();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	ID3D12DescriptorHeap* srvHeap = DirectXCommon::GetSRVDescriptorHeap();
	device->CreateShaderResourceView(thumbnail.resource.Get(), &srvDesc, DirectXCommon::GetCPUDescriptorHandle(srvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, thumbnail.srvSlot));
	thumbnail.size = ImVec2(static_cast<float>(metadata.width), static_cast<float>(metadata.height));
	return true;
}

// 1枚作る。作れなければ false（thumbnail は空のまま）
bool Create(const std::string& path, Thumbnail& thumbnail) {
	const std::wstring widePath = ConvertString(path);
	DirectX::ScratchImage image;
	const bool isHdr = path.ends_with(".hdr") || path.ends_with(".HDR");
	if (!(isHdr ? LoadHdr(widePath, image) : LoadWic(widePath, image))) {
		return false;
	}
	// ミップマップ（小さく描いてもチラつかないように）。1x1 の画像は作れないので、そのまま使う
	DirectX::ScratchImage mipImages;
	const bool hasMips = SUCCEEDED(DirectX::GenerateMipMaps(*image.GetImage(0, 0, 0), DirectX::TEX_FILTER_SRGB, 0, mipImages));
	return Upload(hasMips ? mipImages : image, thumbnail);
}

// SRV のスロットを返して、GPU の絵を捨てる
// 捨てるのは「このフレームにまだ描いていない物」だけなので、前のフレームの描画は（フレームの終わりに GPU を待っているので）終わっている
void Free(Thumbnail& thumbnail) {
	if (thumbnail.srvSlot != 0) {
		DirectXCommon::FreeSRVSlot(thumbnail.srvSlot); // スロットは、このフレームの GPU を待った後から配り直される
		thumbnail.srvSlot = 0;
	}
	thumbnail.resource.Reset();
}

// 覚えている数が上限なら、いちばん長く描いていない物を1つ捨てる
// このフレームに描いた物しか無ければ捨てずに false（今のフレームの ImGui がまだ使うので）
bool MakeRoom(int frame) {
	if (thumbnails.size() < kMaxThumbnails) {
		return true;
	}
	auto oldest = thumbnails.end();
	for (auto it = thumbnails.begin(); it != thumbnails.end(); ++it) {
		if (it->second.lastUsedFrame < frame && (oldest == thumbnails.end() || it->second.lastUsedFrame < oldest->second.lastUsedFrame)) {
			oldest = it;
		}
	}
	if (oldest == thumbnails.end()) {
		return false;
	}
	Free(oldest->second);
	thumbnails.erase(oldest);
	return true;
}

// SRV のスロット → ImGui に渡す番号（GPU から見たハンドル）
ImTextureID TextureIdOf(uint32_t srvSlot) {
	return static_cast<ImTextureID>(DirectXCommon::GetGPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot).ptr);
}
} // namespace

//=============================================================================
// 小さい絵を取る（無ければ作る）
//=============================================================================
bool AssetThumbnails::Get(const std::string& path, std::filesystem::file_time_type writeTime, ImTextureID& texture, ImVec2& size) {
	// フレームが変わったら、作った数を数え直す
	const int frame = ImGui::GetFrameCount();
	if (frame != loadFrame) {
		loadFrame = frame;
		loadCount = 0;
	}

	auto it = thumbnails.find(path);
	// --- 作ってあって、ファイルも変わっていない ---
	if (it != thumbnails.end() && it->second.writeTime == writeTime) {
		it->second.lastUsedFrame = frame;
		if (it->second.srvSlot == 0) {
			return false; // 作れなかった画像（ファイルが変わるまで作り直さない）
		}
		texture = TextureIdOf(it->second.srvSlot);
		size = it->second.size;
		return true;
	}

	// --- 作る（このフレームにもう作っていたら、次のフレームに回す）---
	if (loadCount >= kLoadsPerFrame) {
		return false;
	}
	// ファイルが描き直されていたら、前の絵を捨てる（このフレームにもう描いてしまった物は、次のフレームで）
	if (it != thumbnails.end()) {
		if (it->second.lastUsedFrame == frame) {
			return false;
		}
		Free(it->second);
		thumbnails.erase(it);
	}
	if (!MakeRoom(frame)) {
		return false;
	}
	++loadCount;
	Thumbnail& thumbnail = thumbnails[path];
	thumbnail.writeTime = writeTime;
	thumbnail.lastUsedFrame = frame;
	if (!Create(path, thumbnail)) {
		Free(thumbnail); // 途中まで作った物を捨てる（空のまま覚えておき、作り直さない）
		LogManager::Warning(std::format("小さい絵を作れませんでした: {}", path));
		return false;
	}
	texture = TextureIdOf(thumbnail.srvSlot);
	size = thumbnail.size;
	return true;
}

//=============================================================================
// 全部捨てる
//=============================================================================
void AssetThumbnails::Release() {
	for (auto& [path, thumbnail] : thumbnails) {
		Free(thumbnail);
	}
	thumbnails.clear();
}
#endif