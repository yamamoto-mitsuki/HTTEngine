#include "HTTEngine/Graphics/Model/ModelManager.h"

#include <format>
#include <unordered_map>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Graphics/GPU/UploadContext.h"
#include "HTTEngine/Graphics/Model/ModelImporter.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"


// ===== インスタンス取得 =====
ModelManager& ModelManager::GetInstance() {
	static ModelManager instance;
	return instance;
}

//======================================================================================================
// 初期化 / 解放
//======================================================================================================
// ===== 初期化 =====
void ModelManager::Initialize() { AssetMoveRegistry::AddListener(&OnAssetMoved); }

// ===== 名前を変えた・動かしたファイル =====
// モデルのファイルの中（.mtl・.gltf）に書いてあるテクスチャの名前は、外のツールの形なので書き換えない（読み直すと、前の名前を探す）
void ModelManager::OnAssetMoved(const MovedPath& moved) {
	ModelManager& inst = GetInstance();
	moved.ApplyToKeys(inst.pathToHandle_);
	for (auto& [handle, model] : inst.models_) {
		for (ImportedMaterial& material : model.materials) {
			moved.Apply(material.baseColorTexturePath);
			moved.Apply(material.ambientTexFilePath);
			moved.Apply(material.specularTexFilePath);
			moved.Apply(material.bumpTexFilePath);
		}
	}
}

// ===== 解放 =====
void ModelManager::Release() {
	auto& inst = GetInstance();
	inst.models_.clear();
	inst.pathToHandle_.clear();
	inst.modelsKey_ = 1;
}

// ===== 取得 =====
// モデル
const ModelManager::ModelAsset* ModelManager::GetModelAsset(uint32_t handle) {
	auto it = GetInstance().models_.find(handle);
	if (it != GetInstance().models_.end()) {
		return &it->second;
	}

	LogManager::Warning("ModelAssetが見つかりませんでした");
	return nullptr;
}

// ===== ノードを名前で探す =====
uint32_t ModelManager::FindNodeIndex(const ModelAsset& asset, const std::string& name) {
	const auto found = asset.nodeIndexByName.find(name);
	return (found != asset.nodeIndexByName.end()) ? found->second : kInvalidNode;
}

// ===== 読み込んだマテリアル（番号で引く）=====
const ModelManager::ImportedMaterial* ModelManager::GetImportedMaterial(uint32_t handle, uint32_t materialIndex) {
	// --- 早期リターン（無いモデル）---
	const ModelAsset* model = GetModelAsset(handle);
	if (model == nullptr) {
		return nullptr;
	}
	// --- 番号が範囲の外（SubMesh::materialIndex は読み込みで assimp の番号のまま入るので、外れるのは呼ぶ側の間違い）---
	if (materialIndex >= model->materials.size()) {
		HTT_ASSERT_MSG(false, std::format("{}: マテリアルの番号 {} は範囲の外です（{} 個）", model->name, materialIndex, model->materials.size()));
		return nullptr;
	}
	return &model->materials[materialIndex];
}

// ===== モデル名取得 =====
const std::string& ModelManager::GetModelName(uint32_t modelHandle) {
	auto it = GetInstance().models_.find(modelHandle);
	if (it != GetInstance().models_.end()) {
		return it->second.name;
	}
	static const std::string emptyString;
	return emptyString;
}

// ===== 読み込んだときのパス取得（番号 → パスの逆引き。モデルの数は多くないので、全部を見て探す）=====
const std::string& ModelManager::GetModelPath(uint32_t modelHandle) {
	for (const auto& [path, handle] : GetInstance().pathToHandle_) {
		if (handle == modelHandle) {
			return path;
		}
	}
	static const std::string emptyString;
	return emptyString;
}


//======================================================================================================
// モデルファイルを読み込む
//======================================================================================================
uint32_t ModelManager::Load(const std::filesystem::path& filePath) {
	auto& inst = GetInstance();
	// 重複チェック（書き方をそろえたキーで探す）
	const std::string key = ToPathKey(filePath);
	auto cached = inst.pathToHandle_.find(key);
	if (cached != inst.pathToHandle_.end()) {
		return cached->second;
	}

	// ファイル名（ログ・バッファの名前用。UTF-8 の文字）
	const std::string filename = ToUtf8(filePath.filename());
	// --- 読み込み（CPU の形。assimp を使うのは ModelImporter の中だけ）---
	ModelImporter::Result imported = ModelImporter::Import(filePath);
	ModelAsset& modelAsset = imported.asset;

	// --- テクスチャ ---
	// 埋め込みの画像を先に TextureManager へ送る（マテリアルのテクスチャのパスが、その名前を指している）。読めなかった画像は 0（テクスチャ無し）
	std::unordered_map<std::string, uint32_t> embeddedHandles; // 埋め込みの画像の名前 → テクスチャの番号
	for (const ModelMaterialImporter::EmbeddedImage& image : imported.embeddedImages) {
		embeddedHandles[image.key] = image.fileData.empty() ? TextureManager::LoadFromPixels(image.key, image.width, image.height, image.bgraPixels)
		                                                    : TextureManager::LoadFromMemory(image.key, image.fileData);
	}
	// マテリアルのテクスチャ（埋め込みの画像はその番号、外のファイルは TextureManager で読む。パスは UTF-8 の文字で持っているので ToPath を通す）
	for (ImportedMaterial& mat : modelAsset.materials) {
		if (mat.baseColorTexturePath.empty()) {
			continue;
		}
		if (const auto embedded = embeddedHandles.find(mat.baseColorTexturePath); embedded != embeddedHandles.end()) {
			mat.baseColorTextureHandle = embedded->second;
		} else {
			mat.baseColorTextureHandle = TextureManager::Load(ToPath(mat.baseColorTexturePath));
		}
	}

	// --- GPU常駐バッファを構築（転送を予約。バッファの名前は「ファイル名/マテリアル名」）---
	for (SubMesh& mesh : modelAsset.meshes) {
		MakeMeshBuffer(mesh, filename + "/" + modelAsset.materials[mesh.materialIndex].name);
	}
	// 予約した転送をまとめて実行
	UploadContext::Flush();
	// --- ハンドル割り当てと登録 ---
	uint32_t handle = inst.modelsKey_++;
	inst.models_[handle] = std::move(modelAsset);
	inst.pathToHandle_[key] = handle;

	return handle;
}


//======================================================================================================
// スキンの行列パレット
//======================================================================================================
void ModelManager::MakeMatrixPalette(const ModelAsset& asset, const std::vector<Matrix4x4>* nodeMatrices, 
	std::vector<WellForGPU>& palette) {
	HTT_ASSERT_MSG(nodeMatrices == nullptr || nodeMatrices->size() == asset.nodes.size(), "ノードの行列の数がモデルと違います");
	palette.resize(asset.nodes.size()); // 番号はノードの番号（VertexInfluence::jointIndices と同じ）
	// ノード分回す
	for (size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
		const Node& node = asset.nodes[nodeIndex];
		WellForGPU& well = palette[nodeIndex];
		if (!node.isJoint) {
			// 骨のないノードは、どの頂点からも重みを付けて指されない（空き枠の0番が重みで0を指すだけ）
			// 0を掛けても NaN にならないように、単位行列にしておく
			well.skeletonSpaceMatrix = MakeIdentity4x4();
			well.skeletonSpaceInverseTransposeMatrix = MakeIdentity4x4();
			continue;
		}
		// 今の骨の行列（AnimationPlayer が作ったものか、モデルに入っている形）
		const Matrix4x4& jointMatrix = nodeMatrices ? (*nodeMatrices)[nodeIndex] : node.worldMatrix;
		// バインドポーズの頂点を一度骨から見た位置に戻して（逆バインド行列）、今の骨の行列で置きなおす
		well.skeletonSpaceMatrix = node.inverseBindPoseMatrix * jointMatrix;
		// 法線用 非均一スケールでも面に垂直なままにする
		well.skeletonSpaceInverseTransposeMatrix = Transpose(Inverse(well.skeletonSpaceMatrix));
	}
}


//======================================================================================================
// メッシュのCPU頂点を GPU常駐バッファへ転送する（ロード時1回だけ）
//======================================================================================================
void ModelManager::MakeMeshBuffer(SubMesh& mesh, const std::string& bufferName) {
	if (mesh.vertices.empty() || mesh.indices.empty()) {
		return;
	}
	// バッファの名前の頭
	const std::wstring base = ConvertString(bufferName);

	// 頂点バッファ（形。スキンのあるメッシュは、Skinning の CS も StructuredBuffer として読む）
	mesh.vbv = UploadContext::CreateVertexBuffer(mesh.vertices, mesh.vertexBuffer, base + L"_VB");
	// 骨と重みのバッファ。スキンのあるメッシュだけ（頂点バッファではなく、Skinning の CS が StructuredBuffer として読む）
	if (!mesh.influences.empty()) {
		HTT_ASSERT_MSG(mesh.influences.size() == mesh.vertices.size(), "骨と重みの数が頂点の数と違います（同じ頂点番号で2本のバッファを読むので、揃っている必要があります）");
		mesh.influenceBuffer = 
			UploadContext::CreateBufferWithData(mesh.influences.data(), sizeof(VertexInfluence) * mesh.influences.size(), 
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, base + L"_Influence");
	}
	// インデックスバッファ
	mesh.ibv = UploadContext::CreateIndexBuffer(mesh.indices, mesh.indexBuffer, base + L"_IB");
	mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
}