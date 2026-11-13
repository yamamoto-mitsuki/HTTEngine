#include "HTTEngine/Graphics/Model/ModelImporter.h"

#include <string>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Graphics/Model/ModelNodeImporter.h"

namespace {
// ===== メッシュ1つを読む =====
ModelManager::SubMesh ParseMesh(const aiMesh* mesh, const std::string& filename) {
	// --- 法線・UVが無いモデルもあるので、無ければ既定値で補う ---
	const bool hasNormals = mesh->HasNormals();
	const bool hasTexcoords = mesh->HasTextureCoords(0);
	if (!hasNormals) {
		LogManager::Warning(filename + ": 法線がないMeshです。(0,1,0)で補完します");
	}
	if (!hasTexcoords) {
		LogManager::Warning(filename + ": TexcoordがないMeshです。(0,0)で補完します");
	}
	// --- SubMesh ---
	ModelManager::SubMesh subMesh;
	subMesh.materialIndex = mesh->mMaterialIndex; // assimp のマテリアルの番号（使わないマテリアルを外したあと、ModelMaterialImporter が付け直す）
	subMesh.isSkinned = mesh->HasBones();         // 骨を持つメッシュ（gltfのskin）
	subMesh.vertices.reserve(mesh->mNumVertices);
	for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
		aiVector3D& position = mesh->mVertices[vertexIndex];
		Vertex3dData vertex;
		// 右手系→左手系はx反転で対処（FlipWindingOrderとセット）
		vertex.position = {-position.x, position.y, position.z, 1.0f};
		// 法線
		if (hasNormals) {
			aiVector3D& normal = mesh->mNormals[vertexIndex];
			vertex.normal = {-normal.x, normal.y, normal.z};
		} else {
			vertex.normal = {0.0f, 1.0f, 0.0f};
		}
		// UV
		if (hasTexcoords) {
			aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];
			vertex.texcoord = {texcoord.x, texcoord.y};
		} else {
			vertex.texcoord = {0.0f, 0.0f};
		}
		subMesh.vertices.push_back(vertex);
	}
	// 頂点インデックス。Faceの中身をそのまま積む
	subMesh.indices.reserve(mesh->mNumFaces * 3);
	for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
		aiFace& face = mesh->mFaces[faceIndex];
		HTT_ASSERT_MSG(face.mNumIndices == 3, "ポリゴンは三角形のみサポートしています");
		for (uint32_t element = 0; element < face.mNumIndices; ++element) {
			subMesh.indices.push_back(face.mIndices[element]);
		}
	}
	return subMesh;
}
} // namespace


//======================================================================================================
// モデルファイルを読む
//======================================================================================================
ModelImporter::Result ModelImporter::Import(const std::filesystem::path& filePath) {
	Assimp::Importer importer;
	const std::string filename = ToUtf8(filePath.filename()); // ログ用
	// --- 読み込み ---
	// assimp には UTF-8 の文字で渡す（path::string() は日本語の Windows では Shift-JIS になって読めない）
	const aiScene* scene = importer.ReadFile(
	    ToUtf8(filePath).c_str(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals);
	HTT_ASSERT_MSG(scene != nullptr, std::string("読み込み失敗: ") + importer.GetErrorString());
	HTT_ASSERT_MSG(scene->HasMeshes(), "メッシュを見つけられませんでした");

	Result result;
	ModelManager::ModelAsset& asset = result.asset;
	asset.name = ToUtf8(filePath.stem());

	// --- メッシュ ---
	// assimpは「1メッシュ=1マテリアル」に分割済みなので、aiMeshをそのままSubMeshにする（番号も同じ。ノードと骨が番号で指す）
	asset.meshes.reserve(scene->mNumMeshes);
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		asset.meshes.push_back(ParseMesh(scene->mMeshes[meshIndex], filename));
	}
	// --- マテリアル（使わない物を外してメッシュの番号を付け直すので、メッシュの後）---
	ModelMaterialImporter::Import(scene, filePath, asset, result.embeddedImages);
	// --- ノードの木・骨・ノードの名前の表（骨を選ぶときにメッシュの isSkinned を見るので、メッシュの後）---
	ModelNodeImporter::Import(scene, asset, filename);
	return result;
}
