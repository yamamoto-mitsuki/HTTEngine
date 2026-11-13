#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

#include "HTTEngine/Graphics/Model/ModelManager.h"

// 前方宣言
struct aiScene;

/// <summary>
/// モデルファイルのマテリアルを、assimp の形から ModelManager::ImportedMaterial へそろえる（ModelImporter から呼ぶ。GPU には触らない）
/// </summary>
namespace ModelMaterialImporter {

// ===== モデルファイルに埋め込まれた画像（.glb・FBX の埋め込み・.gltf の data URI）=====
// 読み込みの間に assimp の中身を写しておき、ModelManager::Load が TextureManager に渡す（assimp の中身は読み込みが終わると消えるので）
struct EmbeddedImage {
	std::string key;                // TextureManager の表の名前（「モデルのパス/*番号」。ImportedMaterial のテクスチャのパスにも、この名前が入る）
	std::vector<uint8_t> fileData;  // 画像ファイルの中身（png・jpg など）。空なら bgraPixels を使う
	std::vector<uint8_t> bgraPixels; // 並んだ画素（B・G・R・A 各8ビット、上の行から）。assimp が画像を解いて持っているときだけ
	uint32_t width = 0;             // bgraPixels の幅
	uint32_t height = 0;            // bgraPixels の高さ
};

/// <summary>
/// マテリアルを読む。メッシュ（ModelAsset::meshes）を読んだ後に呼ぶ（どのメッシュも使わないマテリアルを外し、メッシュの番号を付け直すので）
/// <para>テクスチャのパスは、外のファイルならモデルのフォルダにつないだパス、埋め込みの画像なら「モデルのパス/*番号」。
/// 埋め込みの画像の中身は embeddedImages に1枚1回だけ足す</para>
/// </summary>
/// <param name="modelPath">モデルファイルのパス（ModelManager::Load に渡されたパス。テクスチャのパスの元になる）</param>
void Import(const aiScene* scene, const std::filesystem::path& modelPath, ModelManager::ModelAsset& asset, std::vector<EmbeddedImage>& embeddedImages);

} // namespace ModelMaterialImporter
