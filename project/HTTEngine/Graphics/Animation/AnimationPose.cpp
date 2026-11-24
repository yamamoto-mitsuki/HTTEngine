#include "AnimationPose.h"

#include <algorithm>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"


//=============================================================================
// 混ぜる
//=============================================================================
void BlendPose(const AnimationPose& from, const AnimationPose& to, float weight, AnimationPose& out) {
	HTT_ASSERT_MSG(&out != &from && &out != &to, "BlendPose の out に、from・to と同じ物は渡せません");
	// 違うモデルのポーズは混ぜられない（モデルを途中で差し替えた）。新しい方をそのまま
	if (from.nodes.size() != to.nodes.size()) {
		out = to;
		return;
	}

	const float t = std::clamp(weight, 0.0f, 1.0f);
	out.nodes.resize(to.nodes.size());
	// 3つの並びを同じ番号で見るので、添字で回す
	for (size_t nodeIndex = 0; nodeIndex < to.nodes.size(); ++nodeIndex) {
		const NodePose& a = from.nodes[nodeIndex];
		const NodePose& b = to.nodes[nodeIndex];
		NodePose& result = out.nodes[nodeIndex];
		// 片方だけが動かすノードは、もう片方の「最初の形」（SamplePose が入れておく）から混ぜる
		result.scale = Lerp(a.scale, b.scale, t);
		result.rotate = Slerp(a.rotate, b.rotate, t); // 遠回りしないように、Slerp の中で向きをそろえる
		result.translate = Lerp(a.translate, b.translate, t);
		result.isAnimated = a.isAnimated || b.isAnimated;
	}
}


//=============================================================================
// 行列にする
//=============================================================================
void BuildNodeMatrices(uint32_t modelHandle, const AnimationPose& pose, std::vector<Matrix4x4>& out) {
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(modelHandle);
	if (asset == nullptr || pose.nodes.size() != asset->nodes.size()) {
		out.clear(); // 空なら DrawModel はモデルに入っている行列を使う
		return;
	}

	const std::vector<ModelManager::Node>& nodes = asset->nodes;
	out.resize(nodes.size());
	// 親の行列を使うので、番号（nodeIndex）で回す
	// LoadNode は「親を足してから子を回す」ので、親は必ず自分より前にいる
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
		const ModelManager::Node& node = nodes[nodeIndex];
		const NodePose& nodePose = pose.nodes[nodeIndex];
		// --- 親から見た行列（動かないノードは、モデルに入ったまま。分解して戻すと誤差が出るので）---
		const Matrix4x4 localMatrix = nodePose.isAnimated ? MakeAffineMatrix(nodePose.scale, nodePose.rotate, nodePose.translate) : node.localMatrix;
		// --- 根からの行列（行ベクトルなので「自分 × 親」の順。LoadNode と同じ）---
		out[nodeIndex] = (node.parent == ModelManager::kInvalidNode) ? localMatrix : localMatrix * out[node.parent];
	}
}
