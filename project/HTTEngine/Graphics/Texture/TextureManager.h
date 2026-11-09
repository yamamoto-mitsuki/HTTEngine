#pragma once
#include <memory>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <wrl.h>
#include <d3d12.h>
#include <externals/DirectXTex/DirectXTex.h>
#include <externals/DirectXTex/d3dx12.h>
#include "HTTEngine/Math/MathIncludes.h"
#include "HTTEngine/String/ConvertString.h"

// 前方宣言
struct MovedPath;

/// <summary>
/// テクスチャの読み込み、登録
/// </summary>
class TextureManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class TextureManager;
	};
	explicit TextureManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	struct TextureData {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; // TextureResource本体
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU{};      // SRVCPUハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};      // SRVGPUハンドル
		uint32_t srvIndex = 0;                           // SRVDescriptorHeap上のスロット番号
	};

public:
	// コピー・ムーブ禁止
	TextureManager(const TextureManager&) = delete;
	TextureManager& operator=(const TextureManager&) = delete;

	/// <summary>
	/// 初期化。DirectXCommon生成直後に1回だけ呼ぶ
	/// </summary>
	static void Initialize();

	/// <summary>
	/// 全テクスチャの解放。WinMainのreturn 0の前に呼ぶ
	/// </summary>
	static void Release();

	/// <summary>
	/// テクスチャを読み込んでSRVを登録する。
	/// </summary>
	/// <param name="filePath">読み込む画像ファイルのパス（UTF-8 の文字から作るときは ToPath を通す）</param>
	/// <returns>登録したテクスチャのSRVスロット番号</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// ゲッター
	static const TextureData* GetTextureData(uint32_t srvIndex);
	static const std::string& GetTexturePath(uint32_t srvIndex);
	static const uint32_t GetWhiteTextureHandle() { return instance_->whiteTextureHandle_; }
	static Vector2 GetTextureSize(uint32_t srvIndex);

private:
	~TextureManager() = default;

	// シングルトン
	friend struct std::default_delete<TextureManager>;
	static std::unique_ptr<TextureManager> instance_;

	/// <summary>
	/// Textureデータをファイルから読み込む
	/// </summary>
	static DirectX::ScratchImage LoadTextureFromFile(const std::filesystem::path& filePath);

	/// <summary>
	/// DirectX12のTextureResourceを作る
	/// </summary>
	static Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	/// <summary>
	/// TextureResourceにCPUからデータを転送する
	/// </summary>
	static Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);

	/// <summary>
	/// SRVをSRVDescriptorHeapに登録する
	/// </summary>
	static void RegisterSRV(ID3D12DescriptorHeap* srvHeap, TextureData& textureData, const DirectX::TexMetadata& metadata);

	// 名前を変えた・動かしたファイル：キーのパスを付け替える（番号＝SRV の場所はそのまま。AssetMoveRegistry から呼ばれる）
	static void OnAssetMoved(const MovedPath& moved);

	// 画像のファイルパス（ToPathKey でそろえた UTF-8 の文字）をキー、TextureDataを値とするマップ
	std::unordered_map<std::string, TextureData> textures_;

	uint32_t whiteTextureHandle_ = 0u; // エンジン組み込みのwhite
};