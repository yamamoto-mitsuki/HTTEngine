#include "SkinnedMeshSampler.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Pipeline/VertexFormat.h"
#include "HTTEngine/Math/RandomEngine.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"

namespace SkinnedMeshSampler {
/// <summary>
/// 三角形1つの場所
/// </summary>
struct TriangleRef {
	uint32_t meshIndex = 0;  // ModelAsset::meshes の番号
	uint32_t firstIndex = 0; // SubMesh::indices の何番目から3つが、この三角形の頂点の番号か
};

/// <summary>
/// モデル1つ分の、選ぶための表（初めて使ったときに1回だけ作る）
/// </summary>
struct MeshTable {
	const ModelManager::ModelAsset* asset = nullptr; // 作ったときのモデル（アドレスが変わったら作り直す）
	std::vector<TriangleRef> triangles;              // 骨で曲がるメッシュの三角形（面積 0 の物は入れない）
	std::vector<float> cumulativeAreas;              // triangles と同じ並び。その三角形までの面積の合計（小さい順。二分探索で面積に比例して選ぶ）
	std::vector<uint32_t> bones;                     // 親も骨の骨のノードの番号（Bone で、骨と親の骨を結ぶ線を選ぶ）
};
} // namespace SkinnedMeshSampler

namespace {
using SkinnedMeshSampler::MeshTable;
using SkinnedMeshSampler::SamplePoint;

constexpr uint32_t kMaxModelSearchDepth = 64; // モデルを探して親をたどる回数の上限（万一輪になっても止まるように。EntityManager と同じ数）
constexpr float kMinLengthSq = 1e-8f;         // これより短い向きは、向きが決まらないとみなす

std::unordered_map<uint32_t, MeshTable> tables; // モデルの番号（ModelRendererComponent::modelHandle）→ 表
std::vector<WellForGPU> palette;                // 行列パレット（MakeContext で作り、Sample で読む。配列のメモリを使い回す。出すのはメインのスレッドだけなので1つ）


//=============================================================================
// 小さな道具
//=============================================================================
// 頂点の位置（w は 1）の xyz だけ
Vector3 ToVector3(const Vector4& v) { return {v.x, v.y, v.z}; }

// 長さ1にする。短すぎて向きが決まらないときは fallback（それも短ければ、ランダムな向き）
Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
	if (LengthSq(v) > kMinLengthSq) {
		return Normalize(v);
	}
	if (LengthSq(fallback) > kMinLengthSq) {
		return Normalize(fallback);
	}
	return RandomEngine::GetDirection();
}

// ノードの、今のポーズのモデルの空間の位置（ノードの行列の原点。AnimationSystem::DrawGizmos と同じ）
Vector3 NodePosition(const SkinnedMeshSampler::Context& context, uint32_t nodeIndex) {
	const Matrix4x4& nodeMatrix = context.nodeMatrices ? (*context.nodeMatrices)[nodeIndex] : context.asset->nodes[nodeIndex].worldMatrix;
	return TransformPoint({0.0f, 0.0f, 0.0f}, nodeMatrix);
}


//=============================================================================
// 表を作る
//=============================================================================
// ===== モデル1つ分の表を作る（バインドポーズの面積で）=====
MeshTable BuildTable(const ModelManager::ModelAsset& asset) {
	MeshTable table;
	table.asset = &asset;
	// --- 骨で曲がるメッシュの三角形と、面積の累積 ---
	float totalArea = 0.0f;
	for (uint32_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex) {
		const ModelManager::SubMesh& mesh = asset.meshes[meshIndex];
		// 骨で曲がらないメッシュ（手に持った物など）はノードの行列で動くので、計算が違う。今回は出さない
		// 骨と重みが頂点と同じ数だけ無いメッシュも、曲げられないので出さない
		if (!mesh.isSkinned || mesh.influences.size() != mesh.vertices.size()) {
			continue;
		}
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			const Vector3 a = ToVector3(mesh.vertices[mesh.indices[i]].position);
			const Vector3 b = ToVector3(mesh.vertices[mesh.indices[i + 1]].position);
			const Vector3 c = ToVector3(mesh.vertices[mesh.indices[i + 2]].position);
			// 2辺の外積の長さ＝2辺が作る平行四辺形の面積。三角形はその半分
			const float area = Length(Cross(b - a, c - a)) * 0.5f;
			if (area <= 0.0f) {
				continue; // つぶれた三角形（当たる幅が無い）
			}
			totalArea += area;
			table.triangles.push_back({meshIndex, static_cast<uint32_t>(i)});
			table.cumulativeAreas.push_back(totalArea);
		}
	}
	// --- 親も骨の骨（根の骨は線が作れないので入れない）---
	for (uint32_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
		const ModelManager::Node& node = asset.nodes[nodeIndex];
		if (node.isJoint && node.parent != ModelManager::kInvalidNode && asset.nodes[node.parent].isJoint) {
			table.bones.push_back(nodeIndex);
		}
	}
	return table;
}

// ===== 表を探し、無ければ作る =====
const MeshTable& FindOrMakeTable(uint32_t modelHandle, const ModelManager::ModelAsset& asset) {
	auto it = tables.find(modelHandle);
	// 無い・前と違うモデル（アドレスが変わった）なら作る
	// insert_or_assign：無ければ足す・あれば上書きする（返すのは「入れた所」と「足したか」の組）
	if (it == tables.end() || it->second.asset != &asset) {
		it = tables.insert_or_assign(modelHandle, BuildTable(asset)).first;
	}
	return it->second;
}

// ===== 粒の Entity から親へたどって、最初の Model Renderer の Entity を探す =====
Handle<Entity> FindModelOwner(Handle<Entity> emitter) {
	Handle<Entity> current = emitter;
	for (uint32_t i = 0; i < kMaxModelSearchDepth; ++i) {
		const ModelRendererComponent* render = EntityManager::Get<ModelRendererComponent>(current);
		if (render != nullptr && render->enabled && render->modelHandle != 0) {
			return current;
		}
		const Entity* entity = EntityManager::Get(current);
		if (entity == nullptr || !entity->parent.IsValid()) {
			return {}; // 根まで見たが無かった
		}
		current = entity->parent;
	}
	return {};
}


//=============================================================================
// 1点選ぶ
//=============================================================================
// ===== 1頂点だけ曲げる（Skinning.CS の main と同じ計算。結果はモデルの空間）=====
SamplePoint SkinVertex(const ModelManager::SubMesh& mesh, uint32_t vertexIndex) {
	const Vector3 position = ToVector3(mesh.vertices[vertexIndex].position);
	const Vector3& normal = mesh.vertices[vertexIndex].normal;
	const VertexInfluence& influence = mesh.influences[vertexIndex];
	SamplePoint result = {
	    {0.0f, 0.0f, 0.0f},
	    {0.0f, 0.0f, 0.0f}
    };
	// 骨4本それぞれで動かした位置・法線を、重みで混ぜる
	for (size_t k = 0; k < influence.weights.size(); ++k) {
		const float weight = influence.weights[k];
		if (weight <= 0.0f) {
			continue; // 使わない枠（CS は 0 を掛けているが、CPU では飛ばした方が速い）
		}
		const WellForGPU& well = palette[static_cast<size_t>(influence.jointIndices[k])];
		result.position += TransformPoint(position, well.skeletonSpaceMatrix) * weight;
		result.normal += TransformNormal(normal, well.skeletonSpaceInverseTransposeMatrix) * weight; // 法線は逆転置で（伸び縮みしても面に垂直なまま）
	}
	return result;
}

// ===== 表面から（Surface）=====
SamplePoint SampleSurface(const SkinnedMeshSampler::Context& context) {
	const MeshTable& table = *context.table;
	// --- 面積に比例して三角形を選ぶ ---
	// 0〜合計の乱数を引き、累積の表で「それより大きい最初の所」を探す。大きい三角形ほど幅が広いので当たりやすい
	// std::upper_bound：小さい順に並んだ範囲から「値より大きい最初の要素」を二分探索で探す（三角形が何万あっても速い）
	const float pick = RandomEngine::GetFloat(0.0f, table.cumulativeAreas.back());
	const auto found = std::upper_bound(table.cumulativeAreas.begin(), table.cumulativeAreas.end(), pick);
	// 乱数がちょうど合計だと表の外（end）を指すので、最後の三角形に収める
	const size_t triangleIndex = (std::min)(static_cast<size_t>(found - table.cumulativeAreas.begin()), table.triangles.size() - 1);
	const SkinnedMeshSampler::TriangleRef& triangle = table.triangles[triangleIndex];
	const ModelManager::SubMesh& mesh = context.asset->meshes[triangle.meshIndex];

	// --- 3頂点だけ、今のポーズに曲げる ---
	const SamplePoint a = SkinVertex(mesh, mesh.indices[triangle.firstIndex]);
	const SamplePoint b = SkinVertex(mesh, mesh.indices[triangle.firstIndex + 1]);
	const SamplePoint c = SkinVertex(mesh, mesh.indices[triangle.firstIndex + 2]);

	// --- 三角形の中で均等に1点（重心座標）---
	// A からの割合 s は、そのままだと A の角に寄る（A に近いほど幅が狭いのに、同じ数だけ選ばれる）。平方根で、面積に対して均等にする
	const float s = std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
	const float r = RandomEngine::GetFloat(0.0f, 1.0f);
	const float weightA = 1.0f - s;
	const float weightB = s * (1.0f - r);
	const float weightC = s * r;
	const Vector3 position = a.position * weightA + b.position * weightB + c.position * weightC;
	// 法線も同じ重みで混ぜる。ほとんど 0 になったら（逆を向いた頂点が混ざった）、面の向き（2辺の外積）
	const Vector3 faceNormal = Cross(b.position - a.position, c.position - a.position);
	const Vector3 normal = NormalizeOr(a.normal * weightA + b.normal * weightB + c.normal * weightC, faceNormal);

	// --- モデルの空間 → ワールド ---
	return {TransformPoint(position, context.modelWorld), NormalizeOr(TransformNormal(normal, context.modelWorld), faceNormal)};
}

// ===== 骨の線から（Bone）=====
SamplePoint SampleBone(const SkinnedMeshSampler::Context& context) {
	const MeshTable& table = *context.table;
	// --- 骨を1つ選び、親の骨 → その骨の線の上に点を置く ---
	const uint32_t bone = table.bones[static_cast<size_t>(RandomEngine::GetInt(0, static_cast<int32_t>(table.bones.size()) - 1))];
	const Vector3 head = NodePosition(context, context.asset->nodes[bone].parent);
	const Vector3 tail = NodePosition(context, bone);
	const Vector3 position = Lerp(head, tail, RandomEngine::GetFloat(0.0f, 1.0f));

	// --- 向き：ランダムな向きから、骨の向きの成分を引いて、線に垂直にする ---
	Vector3 direction = RandomEngine::GetDirection();
	const Vector3 axis = tail - head;
	if (LengthSq(axis) > kMinLengthSq) {
		const Vector3 unitAxis = Normalize(axis);
		direction = NormalizeOr(direction - unitAxis * Dot(direction, unitAxis), direction); // 骨とほぼ同じ向きを引いたら、そのまま
	}

	// --- モデルの空間 → ワールド ---
	return {TransformPoint(position, context.modelWorld), NormalizeOr(TransformNormal(direction, context.modelWorld), direction)};
}
} // namespace


//=============================================================================
// 外から使う
//=============================================================================
// ===== 材料を作る =====
bool SkinnedMeshSampler::MakeContext(Handle<Entity> emitter, ParticleMeshSource source, Context& context) {
	// --- 早期リターン ---
	// モデルを持つ Entity を探す
	const Handle<Entity> owner = FindModelOwner(emitter);
	const ModelRendererComponent* render = EntityManager::Get<ModelRendererComponent>(owner);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(owner);
	if (render == nullptr || transform == nullptr) {
		return false;
	}
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(render->modelHandle);
	if (asset == nullptr) {
		return false;
	}
	// 表があるか（無ければ作る）。出す物が1つも無ければ出せない
	const MeshTable& table = FindOrMakeTable(render->modelHandle, *asset);
	const bool isSurface = (source == ParticleMeshSource::Surface);
	if (isSurface ? table.triangles.empty() : table.bones.empty()) {
		return false;
	}

	// --- 今のポーズ ---
	// Animator が無い・違うモデルのアニメ（ノードの数が違う）なら、モデルに入っている形
	const std::vector<Matrix4x4>* nodeMatrices = AnimationSystem::GetNodeMatrices(owner);
	if (nodeMatrices != nullptr && nodeMatrices->size() != asset->nodes.size()) {
		nodeMatrices = nullptr;
	}

	// --- 書き込む ---
	context.asset = asset;
	context.table = &table;
	context.nodeMatrices = nodeMatrices;
	context.modelWorld = transform->worldMatrix;
	context.source = source;
	// 行列パレットは表面だけが使う（骨の数だけ逆行列。粒ごとではなく、ここで1回）
	if (isSurface) {
		ModelManager::MakeMatrixPalette(*asset, nodeMatrices, palette);
	}
	return true;
}

// ===== 1点選ぶ =====
SkinnedMeshSampler::SamplePoint SkinnedMeshSampler::Sample(const Context& context) {
	return (context.source == ParticleMeshSource::Surface) ? SampleSurface(context) : SampleBone(context);
}

// ===== 表を捨てる =====
void SkinnedMeshSampler::Clear() {
	tables.clear();
	palette.clear();
}
