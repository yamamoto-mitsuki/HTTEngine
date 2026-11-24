#pragma once
#include <vector>
#include <cstdint>

#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Quaternion.h"
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// ノード1つ分の形（親から見た 大きさ・回転・位置）
/// <para>行列にする前の形で持つ。行列のまま混ぜると、回転が縮んだり歪んだりするので</para>
/// </summary>
struct NodePose {
	Vector3 scale = {1.0f, 1.0f, 1.0f};     // 大きさ
	Quaternion rotate;                      // 回転
	Vector3 translate = {0.0f, 0.0f, 0.0f}; // 位置
	bool isAnimated = false;                // アニメが動かすノードか（false なら、行列はモデルに入っている物をそのまま使う。S・R・T は最初の形）
};

/// <summary>
/// 1体分のポーズ（ノードごとの形）。ModelAsset::nodes と同じ並び
/// <para>AnimationPlayer::SamplePose で作り、BlendPose で混ぜ、BuildNodeMatrices で行列にする</para>
/// </summary>
struct AnimationPose {
	std::vector<NodePose> nodes; // ModelAsset::nodes と同じ並び（空ならまだ作っていない）
};

/// <summary>
/// 2つのポーズを混ぜる（回転は Slerp、大きさ・位置は Lerp）
/// <para>ノードの数が違う（違うモデルのポーズ）ときは、混ぜずに to をそのまま写す</para>
/// </summary>
/// <param name="weight">to の割合（0 で from そのまま、1 で to そのまま。外の値は 0〜1 に収める）</param>
/// <param name="out">混ぜた物を入れる所。from・to と同じ物は渡さない（メモリを使い回すため、呼ぶ側が持つ）</param>
void BlendPose(const AnimationPose& from, const AnimationPose& to, float weight, AnimationPose& out);

/// <summary>
/// ポーズから、ノードの行列（根からのワールド行列）を作る
/// <para>モデルが無い・ノードの数が合わないときは、out を空にする（空なら DrawModel はモデルに入っている行列を使う）</para>
/// </summary>
/// <param name="out">ModelAsset::nodes と同じ並びの行列（Renderer::ModelConfig::nodeMatrices に渡す物）</param>
void BuildNodeMatrices(uint32_t modelHandle, const AnimationPose& pose, std::vector<Matrix4x4>& out);
