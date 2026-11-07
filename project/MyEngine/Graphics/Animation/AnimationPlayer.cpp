#include "AnimationPlayer.h"

#include <array>
#include <cmath>
#include <algorithm>

#include "MyEngine/Graphics/Animation/AnimationManager.h"
#include "MyEngine/Graphics/Model/ModelManager.h"

namespace {
constexpr float kMinScale = 1.0e-6f; // 大きさで割るときに、0で割らないための下限

Quaternion ExtractRotation(const Matrix4x4& matrix, const Vector3& scale) {
	const std::array<float, 3> scales = {
	    std::abs(scale.x) < kMinScale ? 1.0f : scale.x,
	    std::abs(scale.y) < kMinScale ? 1.0f : scale.y,
	    std::abs(scale.z) < kMinScale ? 1.0f : scale.z,
	};
	Matrix4x4 rotateMatrix = MakeIdentity4x4();
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			rotateMatrix.m[row][column] = matrix.m[row][column] / scales[row];
		}
	}
	return MakeQuaternionFromMatrix(rotateMatrix);
}
} // namespace


//=============================================================================
// 再生の操作
//=============================================================================
// ===== 最初から再生 =====
void AnimationPlayer::Play(uint32_t animationHandle, uint32_t index, bool loop) {
	// 見つからなければ nullptr
	animation_ = AnimationManager::GetAnimation(animationHandle, index);
	// 流す向きの始まりから（前に流すなら 0 秒、逆に流す（speed が負）なら終わりから。逆に流すのに 0 秒から始めると、すぐ終わってしまう）
	time_ = (animation_ != nullptr && speed_ < 0.0f) ? animation_->duration : 0.0f;
	loop_ = loop;
	isPaused_ = false;
	isPlaying_ = (animation_ != nullptr);
}

// ===== 停止 =====
void AnimationPlayer::Stop() {
	time_ = 0.0f;
	isPlaying_ = false;
	isPaused_ = false;
}

// ===== 時刻を直接決める =====
void AnimationPlayer::SetTime(float time) { 
	const float duration = animation_ ? animation_->duration : 0.0f; 
	time_ = std::clamp(time, 0.0f, duration);
}


//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 時刻を進める =====
void AnimationPlayer::Update(float deltaTime) {
	// 早期リターン
	if (animation_ == nullptr || !isPlaying_ || isPaused_) {
		return;
	}
	const float duration = animation_->duration;
	if (duration <= 0.0f) {
		time_ = 0.0f; // 長さ0のアニメ（キーが1つだけ）は、ずっと最初の形
		return;
	}

	const float step = deltaTime * speed_; // このフレームに進める秒（負なら戻る。一時停止・ヒットストップで deltaTime が 0 なら 0）
	time_ += step;

	if (loop_) {
		// --- ループ ---
		time_ = std::fmod(time_, duration);
		if (time_ < 0.0f) {
			time_ += duration; // 逆再生（speedが負）で0を下回ったときは後ろへ回す
		}
		return;
	} else {
		// --- ループしない ---
		// 進む向きの終わりに着いたときだけ止める（前に流すなら duration、逆に流すなら 0）
		// 進めない（step が 0）ときは止めない。流し始め（0 秒）に 0 秒進めても「0 以下」になるので、向きを見ないと止まってしまう
		if (step > 0.0f && time_ >= duration) {
			time_ = duration;
			isPlaying_ = false;
		} else if (step < 0.0f && time_ <= 0.0f) {
			time_ = 0.0f;
			isPlaying_ = false;
		}
	}
}

// ===== ノードの行列を作り直す =====
void AnimationPlayer::ApplyToModel(uint32_t modelHandle) { 
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(modelHandle);
	if (asset == nullptr) {
		nodeMatrices_.clear(); // 空なら DrawModelはモデルに入っている行列を使う
		return;
	}
	// モデルかアニメーションが変わったときだけ、ノード名でつなぎ直す
	if (modelHandle != boundModelHandle_ || animation_ != boundAnimation_ || bindings_.size() != asset->nodes.size()) {
		Bind(modelHandle);
	}

	const std::vector<ModelManager::Node>& nodes = asset->nodes;
	nodeMatrices_.resize(nodes.size());
	// 親の行列を使うので、番号（nodeIndex）で回す
	// LoadNodeは「親を足してから子を回す」ので、親は必ず自分より前にいる 
	for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
		const ModelManager::Node& node = nodes[nodeIndex];
		const NodeBinding& binding = bindings_[nodeIndex];

		// --- 親から見た行列 ---
		Matrix4x4 localMatrix = node.localMatrix; // 動かないノードは、モデルに入ったまま
		if (binding.animation != nullptr) {
			const NodeAnimation& nodeAnimation = *binding.animation;
			// キーがないカーブは、モデルに入っていた最初の形を使う
			const Vector3 scale = nodeAnimation.scale.keyframes.empty() ? binding.scale : CalculateValue(nodeAnimation.scale, time_);
			const Quaternion rotate = nodeAnimation.rotate.keyframes.empty() ? binding.rotate : CalculateValue(nodeAnimation.rotate, time_);
			const Vector3 translate = nodeAnimation.translate.keyframes.empty() ? binding.translate : CalculateValue(nodeAnimation.translate, time_);
			localMatrix = MakeAffineMatrix(scale, rotate, translate);
		}
		// --- 根からの行列（行ベクトルなので「自分 × 親」の順。LoadNodeと同じ）---
		nodeMatrices_[nodeIndex] = (node.parent == ModelManager::kInvalidNode) ? localMatrix : localMatrix * nodeMatrices_[node.parent];
	}
}


//=============================================================================
// ノードとカーブをつなぐ
//=============================================================================
void AnimationPlayer::Bind(uint32_t modelHandle) {
	boundModelHandle_ = modelHandle;
	boundAnimation_ = animation_;
	bindings_.clear();
	// 早期リターン
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(modelHandle);
	if (asset == nullptr) {
		return;
	}
	bindings_.resize(asset->nodes.size());
	if (animation_ == nullptr) {
		return; // 全部「動かないノード」のまま
	}

	for (size_t nodeIndex = 0; nodeIndex < asset->nodes.size(); ++nodeIndex) {
		const ModelManager::Node& node = asset->nodes[nodeIndex];
		NodeBinding& binding = bindings_[nodeIndex];
		// ノード名でカーブを探す（無ければ動かないノード）
		auto found = animation_->nodeAnimations.find(node.name);
		if (found == animation_->nodeAnimations.end()) {
			continue;
		}
		binding.animation = &found->second;
		// キーが無いカーブのために、最初の形を S・R・Tに分けて覚えておく
		const Transform initial = DecomposeAffineMatrix(node.localMatrix);
		binding.scale = initial.scale;
		binding.rotate = ExtractRotation(node.localMatrix, initial.scale);
		binding.translate = initial.translation;
	}
}