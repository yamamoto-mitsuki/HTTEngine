#include "HTTEngine/Graphics/Model/ModelMaterialImporter.h"

#include <cctype>
#include <format>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <unordered_set>

#include <assimp/scene.h>
#include <assimp/GltfMaterial.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
using ImportedMaterial = ModelManager::ImportedMaterial;
using AlphaMode = ModelManager::AlphaMode;
using EmbeddedImage = ModelMaterialImporter::EmbeddedImage;

// ===== glTF の alphaMode の文字（"OPAQUE"・"MASK"・"BLEND"）→ AlphaMode =====
AlphaMode ParseAlphaMode(std::string_view text, const std::string& materialName) {
	if (text == "MASK") {
		return AlphaMode::Mask;
	}
	if (text == "BLEND") {
		return AlphaMode::Blend;
	}
	if (text != "OPAQUE") {
		LogManager::Warning(std::format("{}: 知らない alphaMode \"{}\" です（OPAQUE として読みます）", materialName, text));
	}
	return AlphaMode::Opaque;
}

// ===== OBJ か（拡張子を小文字にして比べる）=====
bool IsObjFile(const std::filesystem::path& filePath) { return ToLowerAscii(ToUtf8(filePath.extension())) == ".obj"; }

// ===== OBJ・MTL の1行が key（"mtllib"・"newmtl"・"Kd" など）で始まっていれば、その後ろ（前後の空白を除いた物）を返す。違えば空 =====
// 行の頭の空白は飛ばす。key の直後は空白でないといけない（"Kd" が "Kdx" に当たらないように）
std::string_view ReadKeyword(std::string_view line, std::string_view key) {
	constexpr std::string_view kSpaces = " \t\r";
	const size_t begin = line.find_first_not_of(kSpaces);
	if (begin == std::string_view::npos) {
		return {};
	}
	line.remove_prefix(begin);
	if (!line.starts_with(key) || line.size() <= key.size() || kSpaces.find(line[key.size()]) == std::string_view::npos) {
		return {};
	}
	line.remove_prefix(key.size());
	const size_t valueBegin = line.find_first_not_of(kSpaces);
	if (valueBegin == std::string_view::npos) {
		return {};
	}
	const size_t valueEnd = line.find_last_not_of(kSpaces);
	return line.substr(valueBegin, valueEnd - valueBegin + 1);
}

// ===== OBJ の .mtl を読み、Kd（物の色）が書かれているマテリアルの名前を集める =====
// assimp は Kd が無いと 0.6 を入れ、書いてあったかを外に出さないので、ここで自分で調べる
std::unordered_set<std::string> FindMaterialsWithDiffuse(const std::filesystem::path& objPath) {
	std::unordered_set<std::string> materialNames;
	std::string line;
	// --- .obj の mtllib の行から .mtl を見つける（.obj からの相対。行の残りを1つのファイル名として読む。assimp と同じ）---
	std::vector<std::filesystem::path> mtlPaths;
	std::ifstream obj(objPath);
	while (std::getline(obj, line)) {
		const std::string_view fileName = ReadKeyword(line, "mtllib");
		if (!fileName.empty()) {
			mtlPaths.push_back(objPath.parent_path() / ToPath(std::string(fileName))); // 文字は UTF-8 なので ToPath を通す
		}
	}
	// --- .mtl の newmtl ごとに、Kd の行があるかを見る ---
	for (const std::filesystem::path& mtlPath : mtlPaths) {
		std::ifstream mtl(mtlPath);
		std::string currentName; // 今読んでいるマテリアルの名前（newmtl の後ろ）
		while (std::getline(mtl, line)) {
			if (const std::string_view name = ReadKeyword(line, "newmtl"); !name.empty()) {
				currentName = name;
				continue;
			}
			if (!ReadKeyword(line, "Kd").empty()) {
				materialNames.insert(currentName);
			}
		}
	}
	return materialNames;
}

// ===== 埋め込みの画像の中身を写す =====
// mHeight が 0 なら pcData は画像ファイルの中身（mWidth バイト）。そうでなければ mWidth × mHeight の画素（aiTexel ＝ B・G・R・A の順）
EmbeddedImage CopyEmbeddedImage(const aiTexture& texture, const std::string& key) {
	EmbeddedImage image;
	image.key = key;
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(texture.pcData);
	if (texture.mHeight == 0) {
		image.fileData.assign(bytes, bytes + texture.mWidth);
	} else {
		image.width = texture.mWidth;
		image.height = texture.mHeight;
		image.bgraPixels.assign(bytes, bytes + static_cast<size_t>(texture.mWidth) * texture.mHeight * sizeof(aiTexel));
	}
	return image;
}

// ===== マテリアルに書いてあるテクスチャの名前 → エンジンのパス =====
// ・外のファイル：モデルのフォルダにつないだパス（マテリアルに書いてあるのはモデルファイルからの相対）
// ・埋め込みの画像（.glb の "*0" など）：「モデルのパス/*番号」。ファイルとしては無い名前だが、TextureManager の表の名前になる。
//   モデルの下に付く形なので、エディタでモデルの名前を変えると、テクスチャの名前も一緒に付け替わる（MovedPath::Apply）
class TexturePathResolver {
public:
	TexturePathResolver(const aiScene* scene, const std::filesystem::path& modelPath, std::vector<EmbeddedImage>& embeddedImages)
	    : scene_(scene), modelPath_(modelPath), embeddedImages_(embeddedImages) {}

	std::string Resolve(const aiString& name) const {
		// GetEmbeddedTextureAndIndex：埋め込みの画像なら、その中身と番号を返す（"*0" の形も、FBX のファイル名の形も探す。無ければ nullptr）
		const auto [texture, index] = scene_->GetEmbeddedTextureAndIndex(name.C_Str());
		if (texture == nullptr) {
			// path の / でつなぐと、フォルダが空（"cube.obj" だけ渡した）でも先頭に / が付かない。assimp の文字は UTF-8 なので ToPath を通す
			return ToUtf8(modelPath_.parent_path() / ToPath(name.C_Str()));
		}
		const std::string key = ToPathKey(modelPath_ / std::format("*{}", index));
		// 同じ画像を2つのマテリアルが使うこともあるので、写すのは1回だけ
		if (std::ranges::none_of(embeddedImages_, [&key](const EmbeddedImage& image) { return image.key == key; })) {
			embeddedImages_.push_back(CopyEmbeddedImage(*texture, key));
		}
		return key;
	}

private:
	const aiScene* scene_;
	std::filesystem::path modelPath_;
	std::vector<EmbeddedImage>& embeddedImages_;
};

// ===== マテリアル1つを読む（ファイルの形ごとの違いは、ここで ImportedMaterial の1つの形にそろえる）=====
ImportedMaterial ParseMaterial(const aiMaterial* material, const TexturePathResolver& texturePaths) {
	ImportedMaterial mtl;
	// 名前（objのnewmtl名。ない場合assimpが"DefaultMaterial"を作る）
	aiString name;
	material->Get(AI_MATKEY_NAME, name);
	mtl.name = name.C_Str();

	// --- 色（どれもリニアのまま。glTF は決まりでリニア、OBJ の Kd なども Blender が書くリニアの値として扱う）---
	aiColor3D color;
	if (material->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
		mtl.ambient = {color.r, color.g, color.b};
	}
	// 物の色の RGB（OBJ の Kd・glTF の baseColorFactor。OBJ で Kd が書かれていないときは、assimp が 0.6 を入れるので、Import で 1 に直す）
	if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
		mtl.baseColor = {color.r, color.g, color.b, 1.0f};
	}
	if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
		mtl.specular = {color.r, color.g, color.b};
	}
	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
		mtl.emissive = {color.r, color.g, color.b};
	}

	// --- 物の色の A（OBJ の d・glTF の baseColorFactor の A）---
	// glTF は COLOR_DIFFUSE の4つ目にも OPACITY にも同じ A が入るので、OPACITY の方だけを読む（両方掛けると2回掛かる）
	material->Get(AI_MATKEY_OPACITY, mtl.baseColor.w);

	// --- 透け方（glTF だけ。OBJ にはキーが無いので Opaque のまま）---
	aiString alphaMode;
	if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
		mtl.alphaMode = ParseAlphaMode(alphaMode.C_Str(), mtl.name);
		material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, mtl.alphaCutoff);
		// glTF の決まり：OPAQUE のときは A を使わない（Blender で A を下げても、不透明のまま）
		if (mtl.alphaMode == AlphaMode::Opaque) {
			mtl.baseColor.w = 1.0f;
		}
	}

	// --- スカラー ---
	material->Get(AI_MATKEY_SHININESS, mtl.shininess);
	material->Get(AI_MATKEY_METALLIC_FACTOR, mtl.metallic);
	material->Get(AI_MATKEY_ROUGHNESS_FACTOR, mtl.roughness);

	// --- テクスチャ（名前は TexturePathResolver でエンジンのパスに直す）---
	aiString texPath;
	// glTFはbaseColor、OBJ / FBXはdiffuseに入る。あった方を使う
	if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS || material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
		mtl.baseColorTexturePath = texturePaths.Resolve(texPath);
	}
	if (material->GetTexture(aiTextureType_AMBIENT, 0, &texPath) == AI_SUCCESS) {
		mtl.ambientTexFilePath = texturePaths.Resolve(texPath);
	}
	if (material->GetTexture(aiTextureType_SPECULAR, 0, &texPath) == AI_SUCCESS) {
		mtl.specularTexFilePath = texturePaths.Resolve(texPath);
	}
	// OBJのmap_bump/bumpはassimpではHEIGHT、glTFはNORMALSに分類される
	if (material->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS || material->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS) {
		mtl.bumpTexFilePath = texturePaths.Resolve(texPath);
	}
	return mtl;
}

// ===== どのメッシュも使わないマテリアルを外す =====
// assimp は、どのメッシュも使わない既定のマテリアルを足すことがある（OBJ の "DefaultMaterial"・glTF の名前の無い物）
// マテリアルの番号は、そのままマテリアルの枠（Inspector の一覧）になるので、使う物だけをファイルの並びのまま詰めて、メッシュの番号を付け直す
void RemoveUnusedMaterials(ModelManager::ModelAsset& asset) {
	// --- どれが使われているか ---
	std::vector<bool> isUsed(asset.materials.size(), false);
	for (const ModelManager::SubMesh& mesh : asset.meshes) {
		isUsed[mesh.materialIndex] = true;
	}
	// --- 使う物だけを並びのまま詰め、古い番号 → 新しい番号の表を作る ---
	std::vector<ImportedMaterial> usedMaterials;
	std::vector<uint32_t> newIndices(asset.materials.size(), 0);
	for (size_t oldIndex = 0; oldIndex < asset.materials.size(); ++oldIndex) {
		if (isUsed[oldIndex]) {
			newIndices[oldIndex] = static_cast<uint32_t>(usedMaterials.size());
			usedMaterials.push_back(std::move(asset.materials[oldIndex]));
		}
	}
	// --- メッシュの番号を付け直す ---
	for (ModelManager::SubMesh& mesh : asset.meshes) {
		mesh.materialIndex = newIndices[mesh.materialIndex];
	}
	asset.materials = std::move(usedMaterials);
}
} // namespace


//======================================================================================================
// マテリアルを読む
//======================================================================================================
void ModelMaterialImporter::Import(const aiScene* scene, const std::filesystem::path& modelPath, ModelManager::ModelAsset& asset,
	std::vector<EmbeddedImage>& embeddedImages) {
	// --- 読む ---
	// assimp と同じ並びで読む（メッシュは番号 mMaterialIndex でマテリアルを指すので、そのまま SubMesh::materialIndex になる）
	// 名前で持たないのは、glTF は同じ名前のマテリアルを許すから（名前の表だと、後から読んだ物が前の物を消す）
	const TexturePathResolver texturePaths(scene, modelPath, embeddedImages);
	asset.materials.reserve(scene->mNumMaterials);
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		asset.materials.push_back(ParseMaterial(scene->mMaterials[materialIndex], texturePaths));
	}

	// --- OBJ：Kd が書かれていないマテリアルは、物の色を白にする（書いていない＝色を掛けない。glTF の baseColorFactor の既定 1 と同じ）---
	// Blender はテクスチャを使うマテリアルに Kd を書かないので、assimp の 0.6 のままだと、テクスチャが暗くなる
	if (IsObjFile(modelPath)) {
		const std::unordered_set<std::string> withDiffuse = FindMaterialsWithDiffuse(modelPath);
		for (ImportedMaterial& material : asset.materials) {
			if (!withDiffuse.contains(material.name)) {
				material.baseColor = {1.0f, 1.0f, 1.0f, material.baseColor.w}; // A（OBJ の d）はそのまま
			}
		}
	}

	// --- どのメッシュも使わないマテリアルを外す（番号はファイルの並びのまま詰める）---
	RemoveUnusedMaterials(asset);
}
