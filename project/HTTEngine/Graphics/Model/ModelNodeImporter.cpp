#include "HTTEngine/Graphics/Model/ModelNodeImporter.h"

#include <format>
#include <numeric>
#include <iterator>
#include <algorithm>
#include <unordered_map>

#include <assimp/scene.h>

#include "HTTEngine/Diagnostics/LogManager.h"

namespace {
using Node = ModelManager::Node;
using SubMesh = ModelManager::SubMesh;
using ModelAsset = ModelManager::ModelAsset;
constexpr uint32_t kInvalidNode = ModelManager::kInvalidNode;

// ===== assimpの行列 → エンジンの行列 =====
// ・assimpは「列ベクトル（変換後 = 行列 × 頂点）」で行優先。エンジンは「行ベクトル（変換後 = 頂点 × 行列）」なので転置する
// ・頂点を読むときにxを反転している（右手系→左手系）ので、行列も x を反転した空間へ直す
//   （x反転の行列で前後から挟む形。結果として「片方の添字だけがx」の成分の符号が反転する）
Matrix4x4 ConvertAssimpMatrix(const aiMatrix4x4& matrix) {
	Matrix4x4 result{};
	// 転置しながら入れる
	result.m[0][0] = matrix.a1;
	result.m[0][1] = matrix.b1;
	result.m[0][2] = matrix.c1;
	result.m[0][3] = matrix.d1;
	result.m[1][0] = matrix.a2;
	result.m[1][1] = matrix.b2;
	result.m[1][2] = matrix.c2;
	result.m[1][3] = matrix.d2;
	result.m[2][0] = matrix.a3;
	result.m[2][1] = matrix.b3;
	result.m[2][2] = matrix.c3;
	result.m[2][3] = matrix.d3;
	result.m[3][0] = matrix.a4;
	result.m[3][1] = matrix.b4;
	result.m[3][2] = matrix.c4;
	result.m[3][3] = matrix.d4;
	// x反転（位置のxと、xが混ざる回転成分の符号が変わる）
	result.m[0][1] = -result.m[0][1];
	result.m[0][2] = -result.m[0][2];
	result.m[0][3] = -result.m[0][3];
	result.m[1][0] = -result.m[1][0];
	result.m[2][0] = -result.m[2][0];
	result.m[3][0] = -result.m[3][0];
	return result;
}

// ===== aiNodeの木を ModelAsset::nodes（配列）へ写す。子を再帰でたどる =====
// 戻り値は足したノードの番号
uint32_t LoadNode(const aiNode* node, uint32_t parentIndex, const Matrix4x4& parentWorld, ModelAsset& modelAsset) {
	// 先に自分の場所を作る（子を足すと配列が伸びるので、参照ではなくインデックスで触る）
	uint32_t index = static_cast<uint32_t>(modelAsset.nodes.size());
	modelAsset.nodes.emplace_back();
	{
		Node& self = modelAsset.nodes[index];
		self.name = node->mName.C_Str();
		self.localMatrix = ConvertAssimpMatrix(node->mTransformation);
		self.worldMatrix = self.localMatrix * parentWorld; // 行ベクトルなので「自分 × 親」の順
		self.parent = parentIndex;
		// このノードが持つメッシュ（aiMeshの番号は ModelAsset::meshes の番号と同じ）
		self.meshIndices.assign(node->mMeshes, node->mMeshes + node->mNumMeshes);
	}
	Matrix4x4 world = modelAsset.nodes[index].worldMatrix;

	// 子をたどる
	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
		uint32_t added = LoadNode(node->mChildren[childIndex], index, world, modelAsset);
		modelAsset.nodes[index].children.push_back(added);
	}
	return index;
}

// ===== 骨の名前のノードが2つ以上あるとき、骨にする1つを選ぶ =====
// モデルを作るソフト（Blender など）では、オブジェクトの名前と骨の名前を別々に付けられるので、
// 書き出すと「メッシュのオブジェクト」と「骨」が同じ名前のノードになることがある。assimp の骨（aiBone）は名前しか持たない
// スキンのあるメッシュは骨で置かれる（ノードの行列を掛けない。MeshMatrix）ので、そのメッシュを持つノードは骨の役をしない。それを外して選ぶ
uint32_t PickJointNode(const std::vector<uint32_t>& candidates, const ModelAsset& asset) {
	// スキンのあるメッシュを持つノードか
	const auto holdsSkinnedMesh = [&asset](uint32_t nodeIndex) {
		return std::ranges::any_of(asset.nodes[nodeIndex].meshIndices, [&asset](uint32_t meshIndex) { return asset.meshes[meshIndex].isSkinned; });
	};
	// find_if_not：条件に合わない最初の物を探す（＝スキンのあるメッシュを持たない最初のノード）
	const auto found = std::ranges::find_if_not(candidates, holdsSkinnedMesh);
	return (found != candidates.end()) ? *found : candidates.front(); // どれも持つなら先の方
}

// ===== 頂点に骨を1本足す =====
// 空き枠に入れ、4本とも埋まっていたら一番軽い骨と入れ替える
void AddInfluence(VertexInfluence& influence, int32_t jointIndex, float weight) {
	if (weight <= 0.0f) {
		return; // 動かさない骨
	}
	// --- 一番重みの小さい枠を探す。空いている枠（重み0）があれば、そこが選ばれる ---
	auto smallest = std::ranges::min_element(influence.weights); // 一番要素の小さい場所を返す
	if (weight <= *smallest) {
		return; // 4本とも埋まっていて、どれよりも軽い骨は捨てる
	}
	// 空き枠に入れる。4本とも埋まっているときは、一番軽い骨と入れ替える
	const size_t slot = static_cast<size_t>(std::distance(influence.weights.begin(), smallest));
	influence.weights[slot] = weight;
	influence.jointIndices[slot] = jointIndex;
}

// ===== 重みの合計を1にそろえる =====
void NormalizeInfluence(VertexInfluence& influence) {
	// begin(),end()まで足した合計
	const float sum = std::accumulate(influence.weights.begin(), influence.weights.end(), 0.0f);
	if (sum <= 0.0f) {
		return; // どの骨にも動かされない頂点
	}
	for (float& weight : influence.weights) {
		weight /= sum;
	}
}

// ===== スキン（骨）を読む =====
// LoadNode の後に呼ぶ（骨をノードの名前で探すので）
void LoadSkin(const aiScene* scene, ModelAsset& modelAsset) {
	// --- ノード名 → 番号の表（同じ名前のノードが2つ以上あることがあるので、全部持つ。選ぶのは PickJointNode）---
	std::unordered_map<std::string, std::vector<uint32_t>> nameToIndices;
	nameToIndices.reserve(modelAsset.nodes.size());
	for (uint32_t nodeIndex = 0; nodeIndex < modelAsset.nodes.size(); ++nodeIndex) {
		nameToIndices[modelAsset.nodes[nodeIndex].name].push_back(nodeIndex);
	}

	// --- メッシュごとの骨（aiBone）---
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh->HasBones()) {
			continue; // スキンのないメッシュ
		}
		// 頂点ごとの骨と重み（verticesと同じ数・順番。最初は全部重み0）
		SubMesh& subMesh = modelAsset.meshes[meshIndex]; // aiMeshの番号とSubMeshの番号は同じ
		subMesh.influences.assign(subMesh.vertices.size(), VertexInfluence{});

		for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
			const aiBone* bone = mesh->mBones[boneIndex];
			auto found = nameToIndices.find(bone->mName.C_Str());
			if (found == nameToIndices.end()) {
				continue; // 骨の名前のノードがない（壊れたファイル）。この骨は使わない
			}
			const uint32_t jointIndex = PickJointNode(found->second, modelAsset); // この骨の番号 = ノードの番号 = 行列パレットの番号
			Node& joint = modelAsset.nodes[jointIndex];

			// --- 骨の印 ---
			// 自分に骨の印、自分から根までに骨の線を通す印
			// 親は必ず自分より前の番号にいる。既に印のあるところまで来たら、その先も印が付いているので止める
			joint.isJoint = true;
			for (uint32_t index = jointIndex; index != kInvalidNode && !modelAsset.nodes[index].isSkeleton;
				index = modelAsset.nodes[index].parent) {
				modelAsset.nodes[index].isSkeleton = true;
			}

			// --- 逆バインド行列 ---
			// assimpでは mOffsetMatrix（gltfのinverseBindMatrices）。ノードの行列と同じく、転置とx反転で直す
			joint.inverseBindPoseMatrix = ConvertAssimpMatrix(bone->mOffsetMatrix);

			// --- 頂点ごとの重み ---
			// 骨が「どの頂点を、どれだけ動かすか」を持っているので、頂点の側（influences）へ写し替える
			for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
				const aiVertexWeight& vertexWeight = bone->mWeights[weightIndex];
				if (vertexWeight.mVertexId >= subMesh.influences.size()) {
					continue; // 頂点の番号が範囲外（壊れたファイル）
				}
				AddInfluence(subMesh.influences[vertexWeight.mVertexId], static_cast<int32_t>(jointIndex), vertexWeight.mWeight);
			}
		}
		// 重みの合計を1にそろえる
		for (VertexInfluence& influence : subMesh.influences) {
			NormalizeInfluence(influence);
		}
	}
}

// ===== ノードの名前 → 番号の表を作る =====
// LoadSkin の後に呼ぶ（同じ名前があるときに骨の印を見るので）
void BuildNodeNameTable(ModelAsset& asset, const std::string& filename) {
	asset.nodeIndexByName.clear();
	asset.nodeIndexByName.reserve(asset.nodes.size());
	std::vector<std::string> duplicatedNames; // 2つ以上あった名前（警告用。1つの名前は1回だけ入れる）
	for (uint32_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
		const Node& node = asset.nodes[nodeIndex];
		// try_emplace：まだ無い名前なら入れて inserted が true。もうあれば入れずに、入っている方を返す
		const auto [entry, inserted] = asset.nodeIndexByName.try_emplace(node.name, nodeIndex);
		if (inserted) {
			continue;
		}
		if (std::ranges::find(duplicatedNames, node.name) == duplicatedNames.end()) {
			duplicatedNames.push_back(node.name);
		}
		// 骨を優先する（アニメーションのカーブは、骨を動かす物なので）
		if (node.isJoint && !asset.nodes[entry->second].isJoint) {
			entry->second = nodeIndex;
		}
	}
	// --- 同じ名前のノードがあることを知らせる（どれを動かすかを決めたので止めはしない）---
	for (const std::string& name : duplicatedNames) {
		const uint32_t chosen = asset.nodeIndexByName.at(name);
		LogManager::Warning(std::format("{}: 同じ名前のノードが2つ以上あります「{}」。骨とアニメーションは {} 番のノード{}に付けます。"
		                                "モデルを作ったソフトで、オブジェクトと骨に同じ名前を付けていないか確かめてください",
		                                filename, name, chosen, asset.nodes[chosen].isJoint ? "（骨）" : ""));
	}
}
} // namespace


//======================================================================================================
// ノードの木と骨を読む
//======================================================================================================
void ModelNodeImporter::Import(const aiScene* scene, ModelAsset& asset, const std::string& filename) {
	// --- ノードの木（根からたどって、ノードごとの行列とメッシュ番号を配列に写す）---
	LoadNode(scene->mRootNode, kInvalidNode, MakeIdentity4x4(), asset);
	// --- スキン（骨の印・逆バインド行列・頂点ごとの骨と重み）---
	LoadSkin(scene, asset);
	// --- ノードの名前 → 番号の表（アニメーションのカーブを名前でつなぐ。骨の印を見るので LoadSkin の後）---
	BuildNodeNameTable(asset, filename);
}
