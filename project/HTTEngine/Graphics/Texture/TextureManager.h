#pragma once
#include <span>
#include <optional>
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

	/// <summary>
	/// 画像ファイルの中身（png・jpg など。メモリにある物）からテクスチャを作って SRV を登録する
	/// <para>ファイルとして無い画像（モデルファイルに埋め込まれた画像など）に使う。読めない形なら、ログに出して 0 を返す（止めない）</para>
	/// </summary>
	/// <param name="key">表の名前（ファイルのパスと同じ形。埋め込みの画像は「モデルのパス/*番号」）。同じ名前はもう読んだ物を返す</param>
	/// <param name="fileData">画像ファイルの中身</param>
	/// <returns>登録したテクスチャのSRVスロット番号。読めなければ 0</returns>
	static uint32_t LoadFromMemory(const std::string& key, std::span<const uint8_t> fileData);

	/// <summary>
	/// 並んだ画素（B・G・R・A 各8ビット、上の行から）からテクスチャを作って SRV を登録する。色は sRGB として扱う
	/// </summary>
	/// <param name="key">表の名前（LoadFromMemory と同じ）</param>
	/// <returns>登録したテクスチャのSRVスロット番号。画素の数が width × height と合わなければ 0</returns>
	static uint32_t LoadFromPixels(const std::string& key, uint32_t width, uint32_t height, std::span<const uint8_t> bgraPixels);

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
	/// ミップマップを作る（1x1 の画像はそのまま返す）
	/// </summary>
	/// <param name="name">ログ用の名前</param>
	static DirectX::ScratchImage MakeMipMaps(DirectX::ScratchImage image, const std::string& name);

	/// <summary>
	/// 読み込み済みなら、その SRV スロット番号を返す（無ければ空。SRV スロットは 0 番もあり得るので、0 を「無い」に使わない）
	/// </summary>
	static std::optional<uint32_t> FindLoaded(const std::string& key);

	/// <summary>
	/// 画像を GPU に送って SRV を作り、key の名前で表に足す（Load・LoadFromMemory・LoadFromPixels の共通の後半）
	/// </summary>
	/// <returns>登録したテクスチャのSRVスロット番号</returns>
	static uint32_t Register(const std::string& key, const DirectX::ScratchImage& mipImages);

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