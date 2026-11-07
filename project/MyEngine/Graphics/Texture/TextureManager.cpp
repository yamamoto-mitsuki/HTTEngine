#include "MyEngine/Graphics/Texture/TextureManager.h"

#include <format>

#include <externals/DirectXTex/d3dx12.h>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"

// 静的メンバ変数
std::unique_ptr<TextureManager> TextureManager::instance_ = nullptr;


//=============================================================================
// 初期化
//=============================================================================
void TextureManager::Initialize() { 
	MY_ASSERT_MSG(instance_ == nullptr, "Initialize()を2回以上呼び出しています");
	instance_ = std::make_unique<TextureManager>(ConstructorKey());
	instance_->whiteTextureHandle_ = Load("MyEngine/Resources/Textures/white.png");
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	LogManager::Log("Initialized");
}

// ===== 名前を変えた・動かしたファイル =====
void TextureManager::OnAssetMoved(const MovedPath& moved) {
	if (instance_ != nullptr) {
		moved.ApplyToKeys(instance_->textures_);
	}
}

//=============================================================================
// 解放
//=============================================================================
void TextureManager::Release() {
	instance_->textures_.clear();
	instance_.reset();
}

//=============================================================================
// 読み込み
//=============================================================================
uint32_t TextureManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(instance_, "Initializeを先に呼んでください");
	// すでに読み込まれているか確認
	const std::string key = ToPathKey(filePath);
	auto it = instance_->textures_.find(key);
	if (it != instance_->textures_.end()) {
		return it->second.srvIndex;
	}
	// ファイルを読む
	DirectX::ScratchImage mipImages = LoadTextureFromFile(filePath);
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	// TextureResourceを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = CreateTextureResource(metadata);
	resource->SetName(filePath.c_str()); // 名前をつける
	// IntermediateResourceを作り、コマンドを積む
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource.Get(), mipImages);
	// CommandQueueでコピーコマンドを実行する
	DirectXCommon::ExecuteAndWait();
	
	// SRVを登録
	TextureData textureData;
	textureData.resource = std::move(resource);
	textureData.srvIndex = DirectXCommon::AllocateSRVSlot();
	RegisterSRV(DirectXCommon::GetSRVDescriptorHeap(), textureData, metadata);
	// マップに登録
	uint32_t index = textureData.srvIndex;
	instance_->textures_[key] = std::move(textureData);

	LogManager::Log(std::format("Loaded: {} -> SRVSlot {}", key, index));
	LogManager::Log(std::format("filePath: {} format: {}", key, (uint32_t)metadata.format));
	return index;
}

//=============================================================================
// テクスチャがHDRかSDRか判断する
//=============================================================================
DirectX::ScratchImage TextureManager::LoadTextureFromFile(const std::filesystem::path& filePath) {
	// テクスチャファイルを読み込む
	DirectX::ScratchImage image{};
	HRESULT hr{};
	
	// HDR画像
	if (filePath.extension() == ".hdr") {
		hr = DirectX::LoadFromHDRFile(filePath.c_str(), nullptr, image);
		MY_ASSERT_MSG(SUCCEEDED(hr), "HDR画像の読み込みに失敗しました");
		return image; // R32G32B32A32_FLOAT・1mip のまま返す
	}

	// 通常テクスチャ（従来通り：sRGB強制＋mip生成）
	hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);

	if (FAILED(hr)) {
		LogManager::Error(std::format("Failed to load texture from file: {}", ToUtf8(filePath)));
		MY_ASSERT_MSG(SUCCEEDED(hr), "テクスチャの読み込みに失敗しました");
	}

	// 1x1 の画像はこれ以上小さくできない（GenerateMipMaps は E_INVALIDARG を返す）ので、ミップマップを作らずにそのまま使う
	if (image.GetMetadata().width == 1 && image.GetMetadata().height == 1) {
		return image;
	}
	// ミップマップの生成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	if (FAILED(hr)) {
		LogManager::Error(std::format("Failed to load texture from file: {}", ToUtf8(filePath)));
		MY_ASSERT_MSG(false, "ミップマップの生成に失敗しました");
	}

	return mipImages;
}


//=============================================================================
// Textureを作るためのResourceを作成
//=============================================================================
Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateTextureResource(const DirectX::TexMetadata& metadata) {
	// metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width); // Textureの幅
	resourceDesc.Height = UINT(metadata.height); // Textureの高さ
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize); // 奥行き(3DTexture)または配列サイズ(TextureArray, CubeMap)
	resourceDesc.MipLevels = UINT16(metadata.mipLevels); // ミップマップの数
	resourceDesc.Format = DirectX::MakeSRGB(metadata.format);
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension); // Textureの次元数
	// 利用するヒープの設定
	 D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // GPUのみ高速で読み書きできる
	// Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	HRESULT hr = DirectXCommon::GetDevice()->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE, // Heapの設定
	    &resourceDesc,        // Resourceの設定
	    D3D12_RESOURCE_STATE_COPY_DEST, // CPUがロードしたデータを転送するためこれ
	    nullptr,   // Clear最適値。使わないのでnullptr
	    IID_PPV_ARGS(&resource));  // 作成するResourceポインタへのポインタ
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		MY_ASSERT_MSG(false, "TextureResourceの作成に失敗しました");
	}

	return resource;
}


//=============================================================================
// 作ったTextureResourceをUploadする
//=============================================================================
[[nodiscard]]
Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages) {
	std::vector<D3D12_SUBRESOURCE_DATA> subResources;
	HRESULT hr = DirectX::PrepareUpload(DirectXCommon::GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subResources);
	MY_ASSERT_MSG(SUCCEEDED(hr), "サブリソースの準備に失敗しました");

	// ===== IntermediateResource(UploadHeap上のBuffer)を作る =====
	// 必要なバッファサイズをDirectX12に計算させる
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, static_cast<UINT>(subResources.size()));
	
	// UploadHeapはCPUが書き込めてGPUが読めるヒープ。TextureResourceにデータを転送するための中継地点として使う。
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC uploadResourceDesc{};
	uploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadResourceDesc.Width = intermediateSize;
	uploadResourceDesc.Height = 1;
	uploadResourceDesc.DepthOrArraySize = 1;
	uploadResourceDesc.MipLevels = 1;
	uploadResourceDesc.SampleDesc.Count = 1;
	uploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
	hr = DirectXCommon::GetDevice()->CreateCommittedResource(
	    &uploadHeapProperties, D3D12_HEAP_FLAG_NONE, 
		&uploadResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&intermediateResource));
	MY_ASSERT_MSG(SUCCEEDED(hr), "中間Resourceの作成に失敗しました");

	// ===== コマンドを積む =====
	UpdateSubresources(DirectXCommon::GetCommandList(), texture, intermediateResource.Get(), 0, 0, static_cast<UINT>(subResources.size()), subResources.data());

	// ===== ResourceStateを変更 =====
	DirectXCommon::TransitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);

	// Load()側でWaitForGPUが完了するまで解放されないようにreturnで返す
	return intermediateResource;
}

//=============================================================================
// SRV
//=============================================================================
void TextureManager::RegisterSRV(ID3D12DescriptorHeap* srvHeap, TextureData& textureData, const DirectX::TexMetadata& metadata) {
	// ===== ハンドル取得 =====
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = DirectXCommon::GetCPUDescriptorHandle(srvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, textureData.srvIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = DirectXCommon::GetGPUDescriptorHandle(srvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, textureData.srvIndex);
	textureData.srvHandleCPU = cpuHandle;
	textureData.srvHandleGPU = gpuHandle;

	// ===== SRVDesc の設定 =====
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DirectX::MakeSRGB(metadata.format);
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// ===== SRVの生成 =====
	DirectXCommon::GetDevice()->CreateShaderResourceView(textureData.resource.Get(), &srvDesc, cpuHandle);
}


//=============================================================================
//　ゲッター
//=============================================================================
const TextureManager::TextureData* TextureManager::GetTextureData(uint32_t srvIndex) {
	for (auto& [path, data] : instance_->textures_) {
		if (data.srvIndex == srvIndex) {
			return &data;
		}
	}
	LogManager::Warning(std::format("{}: Not found", srvIndex));

	return nullptr;
}

const std::string& TextureManager::GetTexturePath(uint32_t srvIndex) {
	for (const auto& [path, data] : instance_->textures_) {
		if (data.srvIndex == srvIndex) {
			return path;
		}
	}
	static const std::string emptyString;
	return emptyString;
}

Vector2 TextureManager::GetTextureSize(uint32_t srvIndex) {
	const TextureData* data = GetTextureData(srvIndex);
	if (!data || !data->resource) {
		return {1.0f, 1.0f};
	}
	D3D12_RESOURCE_DESC desc = data->resource->GetDesc();
	return {static_cast<float>(desc.Width), static_cast<float>(desc.Height)};
}