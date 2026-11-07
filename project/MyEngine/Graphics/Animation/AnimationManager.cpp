#include "AnimationManager.h"

#include <format>
#include <algorithm>
#include <unordered_set>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/String/ConvertString.h"

// 静的メンバ変数
std::unique_ptr<AnimationManager> AnimationManager::instance_ = nullptr;

namespace {
// mTicksPerSecond が 0 のファイル（書き出したソフトが入れていない）のときに使う値。assimp の決まりで 25
constexpr double kDefaultTicksPerSecond = 25.0;
} // namespace


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void AnimationManager::Initialize() { 
	MY_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼び出されています");
	instance_ = std::make_unique<AnimationManager>(ConstructorKey());
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	LogManager::Log("Initialized");
}

// ===== 名前を変えた・動かしたファイル =====
void AnimationManager::OnAssetMoved(const MovedPath& moved) {
	if (instance_ == nullptr) {
		return;
	}
	moved.ApplyToKeys(instance_->pathToHandle_);
	for (auto& [handle, file] : instance_->files_) {
		moved.Apply(file.path);
	}
}

// ===== 解放 =====
void AnimationManager::Release() {
	MY_ASSERT_MSG(instance_ != nullptr, "Initialize()より先にRelease()が呼ばれています");
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// 読み込み
//=============================================================================
uint32_t AnimationManager::Load(const std::filesystem::path& filePath) { 
	MY_ASSERT_MSG(instance_ != nullptr, "Initialize()を先に読んでください");
	// --- 読み込み済みなら対応した番号を返す ---
	const std::string key = ToPathKey(filePath);
	auto cached = instance_->pathToHandle_.find(key);
	if (cached != instance_->pathToHandle_.end()) {
		return cached->second;
	}

	// --- ファイルから読む ---
	std::vector<Animation> animations = LoadAnimationFile(filePath);
	if (animations.empty()) {
		return kInvalidHandle; // 止めずに失敗の番号を返す
	}

	// --- 番号を登録 ---
	const uint32_t handle = instance_->nextHandle_++;
	AnimationFile& file = instance_->files_[handle];
	file.path = key;
	file.animations = std::move(animations);
	instance_->pathToHandle_[key] = handle;
	LogManager::Log(std::format("読み込み完了 \"{}\" -> handle={} アニメーション数={}", key, handle, file.animations.size()));
	return handle;
}


//=============================================================================
// assimp → Animation
//=============================================================================
std::vector<Animation> AnimationManager::LoadAnimationFile(const std::filesystem::path& filePath) { 
	Assimp::Importer importer;
	// assimpには UTF-8の文字で渡す（Windowsの assimpは UTF-8として読む。path::string()は Shift-JISになるので使わない）
	const std::string utf8Path = ToUtf8(filePath);
	// アニメーションだけ使うので、頂点を直すオプションはいらない
	const aiScene* scene = importer.ReadFile(utf8Path.c_str(), 0);
	if (scene == nullptr) {
		LogManager::Error(std::format("読み込み失敗 \"{}\": {}", utf8Path, importer.GetErrorString()));
		return {};
	}
	if (scene->mNumAnimations == 0) {
		LogManager::Error(std::format("アニメーションが入っていません \"{}\"", utf8Path));
		return {};
	}

	std::vector<Animation> animations;
	animations.reserve(scene->mNumAnimations);
	for (uint32_t animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
		const aiAnimation* source = scene->mAnimations[animationIndex];
		// assimp の時間（tick）を秒に直す
		const double ticksPerSecond = (source->mTicksPerSecond != 0.0) ? source->mTicksPerSecond : kDefaultTicksPerSecond;
		
		Animation animation;
		animation.name = source->mName.C_Str();
		// 名前の無いアニメーションは名前で選べないので、番号から名前を作る（「#2」）。並びが変わると別のアニメになるので警告する
		if (animation.name.empty()) {
			animation.name = std::format("#{}", animationIndex);
			LogManager::Warning(std::format("名前の無いアニメーションがあります \"{}\"（{} 番目）。「{}」で選べますが、並びが変わると別のアニメになるので、名前を付けてください", utf8Path, animationIndex, animation.name));
		}
		animation.duration = static_cast<float>(source->mDuration / ticksPerSecond);

		// --- チャンネル（ノード一つ分の動き） ---
		for (uint32_t channelIndex = 0; channelIndex < source->mNumChannels; ++channelIndex) {
			const aiNodeAnim* channel = source->mChannels[channelIndex];
			// ノード名で引けるように
			NodeAnimation& nodeAnimation = animation.nodeAnimations[channel->mNodeName.C_Str()];

			// 位置: ModelManagerが頂点・行列の xを反転しているので、ここでも xを反転する
			nodeAnimation.translate.keyframes.reserve(channel->mNumPositionKeys);
			for (uint32_t keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex) {
				const aiVectorKey& key = channel->mPositionKeys[keyIndex];
				Keyframe<Vector3> keyframe;
				keyframe.time = static_cast<float>(key.mTime / ticksPerSecond);
				keyframe.value = {-key.mValue.x, key.mValue.y, key.mValue.z};
				nodeAnimation.translate.keyframes.push_back(keyframe);
			}

			// 回転：x を反転した世界では、回転の向きが変わるので y と z を反転する（w と x はそのまま）
			nodeAnimation.rotate.keyframes.reserve(channel->mNumRotationKeys);
			for (uint32_t keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex) {
				const aiQuatKey& key = channel->mRotationKeys[keyIndex];
				Keyframe<Quaternion> keyframe;
				keyframe.time = static_cast<float>(key.mTime / ticksPerSecond);
				keyframe.value = {key.mValue.x, -key.mValue.y, -key.mValue.z, key.mValue.w};
				nodeAnimation.rotate.keyframes.push_back(keyframe);
			}

			// 大きさ：向きに関係ないのでそのまま
			nodeAnimation.scale.keyframes.reserve(channel->mNumScalingKeys);
			for (uint32_t keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex) {
				const aiVectorKey& key = channel->mScalingKeys[keyIndex];
				Keyframe<Vector3> keyframe;
				keyframe.time = static_cast<float>(key.mTime / ticksPerSecond);
				keyframe.value = {key.mValue.x, key.mValue.y, key.mValue.z};
				nodeAnimation.scale.keyframes.push_back(keyframe);
			}
		}
		animations.push_back(std::move(animation));
	}

	// --- 同じ名前が2つあると、名前で選ぶときに後の方を選べない（先の方になる）---
	// unordered_set::insert は、もう入っていたら .second が false
	std::unordered_set<std::string_view> names;
	for (const Animation& animation : animations) {
		if (!names.insert(animation.name).second) {
			LogManager::Warning(std::format("同じ名前のアニメーションが2つあります \"{}\": 「{}」（名前で選ぶと先の方になります）", utf8Path, animation.name));
		}
	}
	return animations;
}


//=============================================================================
// 取得
//=============================================================================
const AnimationManager::AnimationFile* AnimationManager::Find(uint32_t handle) { 
	MY_ASSERT_MSG(instance_ != nullptr, "Initialize()を先に読んでください");
	auto it = instance_->files_.find(handle);
	if (it == instance_->files_.end()) {
		return nullptr;
	}
	return &it->second;
}

const Animation* AnimationManager::GetAnimation(uint32_t handle, uint32_t index) { 
	const AnimationFile* file = Find(handle); 
	if (file == nullptr || index >= file->animations.size()) {
		LogManager::Warning(std::format("アニメーションが見つかりません handle={} index={}", handle, index));
		return nullptr;
	}
	return &file->animations[index];
}

std::optional<uint32_t> AnimationManager::FindAnimationIndex(uint32_t handle, std::string_view name) {
	const AnimationFile* file = Find(handle);
	if (file == nullptr) {
		return std::nullopt;
	}
	// ranges::find の3つ目（&Animation::name）は「何と比べるか」。各アニメーションの name を name と比べる
	const auto it = std::ranges::find(file->animations, name, &Animation::name);
	if (it == file->animations.end()) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(it - file->animations.begin());
}

const Animation* AnimationManager::FindAnimation(uint32_t handle, std::string_view name) {
	const std::optional<uint32_t> index = FindAnimationIndex(handle, name);
	return index.has_value() ? &Find(handle)->animations[*index] : nullptr;
}

uint32_t AnimationManager::GetAnimationCount(uint32_t handle) {
	const AnimationFile* file = Find(handle);
	return file ? static_cast<uint32_t>(file->animations.size()) : 0;
}

const std::string& AnimationManager::GetPath(uint32_t handle) {
	const AnimationFile* file = Find(handle);
	if (file == nullptr) {
		static const std::string emptyString;
		return emptyString;
	}
	return file->path;
}