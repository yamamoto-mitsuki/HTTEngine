#pragma once
#include <string>

#include "HTTEngine/Graphics/Model/ModelManager.h"

// 前方宣言
struct aiScene;

/// <summary>
/// モデルファイルのノードの木と骨（スキン）を、assimp の形から ModelManager::ModelAsset へ写す（ModelImporter から呼ぶ。GPU には触らない）
/// </summary>
namespace ModelNodeImporter {

/// <summary>
/// ノードの木・骨・ノードの名前の表を読む。メッシュ（ModelAsset::meshes）を読んだ後に呼ぶ（骨を選ぶときにメッシュの isSkinned を見るので）
/// <para>・aiNode の木を ModelAsset::nodes（配列）へ写す</para>
/// <para>・メッシュの骨（aiBone）の名前と同じノードに isJoint、その祖先に isSkeleton の印を付け、逆バインド行列を入れる。
/// 同じ名前のノードが2つ以上あれば、スキンのあるメッシュを持つノードは骨にしない</para>
/// <para>・スキンのあるメッシュに、頂点ごとの骨と重み（SubMesh::influences）を作る</para>
/// <para>・ノードの名前 → 番号の表（ModelAsset::nodeIndexByName）を作る。同じ名前があればログに警告</para>
/// </summary>
/// <param name="filename">ログ用のファイル名</param>
void Import(const aiScene* scene, ModelManager::ModelAsset& asset, const std::string& filename);

} // namespace ModelNodeImporter
