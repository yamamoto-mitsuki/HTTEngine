#pragma once
#include <vector>
#include <filesystem>

#include "HTTEngine/Graphics/Model/ModelManager.h"
#include "HTTEngine/Graphics/Model/ModelMaterialImporter.h"

/// <summary>
/// モデルファイル（OBJ・glTF・glb・FBX）を assimp で読み、ModelManager::ModelAsset（CPU の形）にする
/// <para>GPU（バッファ・テクスチャ）には触らない。それは ModelManager::Load がする</para>
/// <para>メッシュはここ、マテリアルは ModelMaterialImporter、ノードの木と骨は ModelNodeImporter</para>
/// </summary>
namespace ModelImporter {

// ===== 読み込んだ物 =====
struct Result {
	ModelManager::ModelAsset asset;                                   // メッシュ・マテリアル・ノード（GPU のバッファとテクスチャの番号はまだ無い）
	std::vector<ModelMaterialImporter::EmbeddedImage> embeddedImages; // モデルファイルに埋め込まれた画像（GPU に送ったら捨てる）
};

/// <summary>
/// モデルファイルを読む。読めないファイル・メッシュの無いファイルは止める（HTT_ASSERT）
/// </summary>
/// <param name="filePath">モデルファイルのパス（UTF-8 の文字から作るときは ToPath を通す）</param>
Result Import(const std::filesystem::path& filePath);

} // namespace ModelImporter
