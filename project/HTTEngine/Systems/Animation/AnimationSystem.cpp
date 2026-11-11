#include "AnimationSystem.h"

#include <set>
#include <cfloat>
#include <format>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>
#include <string_view>

#include "HTTEngine/Components/Core/TransformComponent.h"  
#include "HTTEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Editor/Gizmos/GizmoLines.h" 
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Animation/AnimationManager.h"
#include "HTTEngine/Graphics/Animation/AnimationPlayer.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h" 

namespace {
// --- 骨のギズモ ---
constexpr Vector4 kJointColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 骨（Joint）の球の色（白）
constexpr Vector4 kBoneColor = {1.0f, 0.8f, 0.2f, 1.0f};  // 親子をつなぐ線の色（黄色）
constexpr float kJointRadiusRate = 0.003f;   // 球の半径＝骨全体の大きさ（対角線）× これ（モデルの大きさが違っても同じくらいに見える）
constexpr float kMinJointRadius = 0.001f;    // 球の半径の下限（骨が1本だけ・全部同じ位置のとき）
constexpr uint32_t kJointCircleDivision = 8; // 球の円を何本の線で描くか（骨は数が多いので粗くする）
constexpr float kNoFadeStart = 10000.0f;     // 距離でフェードさせないための値（Collider・ライトのギズモと同じ）
constexpr float kNoFadeEnd = 20000.0f;

/// <summary>
/// Animator1つ分の実行中だけの状態（保存しない。Play / Stop で作り直すと消える）
/// </summary>
struct AnimatorState {
	Handle<Entity> entity;        // 持ち主
	uint32_t animationHandle = 0; // 再生しているアニメーションの番号
	AnimationClipName clip;       // 再生しているアニメーションの名前（Component の Clip が変わったかを見る）
	AnimationPlayer player;       // 今の時刻・ノードの行列
	bool isJustStarted = false;   // 流し始めたばかり（次の Update は時刻を進めず、0 秒の形を見せる）
};

/// <summary>
/// AnimationSystem が持つもの（Initializeで作り、Releaseで消す）
/// </summary>
struct AnimationState {
	std::vector<AnimatorState> animators; // 見つけたAnimatorの一覧。止まっていても持つ
	bool wasGameRunning = false;          // ゲームが動いているとして一覧を持っているか（Play / Stop の切り替わりで、一覧を捨てる。SwitchGameRunning）
	std::set<std::pair<uint32_t, std::string>> warnedClips; // 「ファイルに無い」と警告した（ファイルの番号, 名前）。同じ物は1回だけ出す（毎フレーム出さない）

	// --- 骨のギズモ ---
	Renderer::LineListConfig gizmoLines; // ギズモの線の置き場（配列のメモリを使い回す）
	std::vector<Vector3> nodePositions;  // ノードごとのワールドの位置（1体分）
	bool showGizmos = true;              // 線を出すか
};
std::unique_ptr<AnimationState> state;

// ===== ゲームが動き出した・止まったら、一覧を捨てる（停止中の Preview Time のポーズをゲームに持ち込まない。Play On Start も、また最初から見る）=====
// 切り替わったときだけ捨てる（同じ値で何回呼んでもよい）
void SwitchGameRunning(bool isGameRunning) {
	if (isGameRunning == state->wasGameRunning) {
		return;
	}
	state->animators.clear();
	state->wasGameRunning = isGameRunning;
}

// ===== Entityの状態を探す（無ければ nullptr）=====
AnimatorState* FindAnimator(Handle<Entity> entity) { 
	const auto it = std::ranges::find(state->animators, entity, &AnimatorState::entity);
	return (it != state->animators.end()) ? &*it : nullptr;
}

// ===== Entityの状態を探す（無ければ nullptr）=====
AnimatorState& FindOrAddAnimator(Handle<Entity> entity) { 
	if (AnimatorState* animator = FindAnimator(entity)) {
		return *animator;
	}
	state->animators.push_back({entity});
	return state->animators.back();
}

// ===== 名前から、ファイルの中の何番目かを探す =====
// ファイルに無い名前なら、ログに警告して空を返す（同じファイル・同じ名前の警告は1回だけ）
// ファイル・名前が未選択（0・空）なら、警告せずに空を返す
std::optional<uint32_t> FindClip(uint32_t animationHandle, std::string_view clip) {
	if (animationHandle == 0 || clip.empty()) {
		return std::nullopt;
	}
	const std::optional<uint32_t> index = AnimationManager::FindAnimationIndex(animationHandle, clip);
	// set::emplace は、もう入っていたら .second が false（＝前に警告した）
	if (!index.has_value() && state->warnedClips.emplace(animationHandle, std::string(clip)).second) {
		LogManager::Warning(std::format("アニメーション「{}」が {} にありません（流さずに、モデルに入っている形で描きます）", clip, AnimationManager::GetPath(animationHandle)));
	}
	return index;
}

// ===== Componentで選んでいるアニメーションを最初の時刻から持ち直す =====
// AnimationSystem::Restart（ゲームから呼ぶ方）と名前がぶつかるので、こちらは ResetPlayer
void ResetPlayer(AnimatorState& animator, const AnimatorComponent& component, bool play) {
	animator.animationHandle = component.animationHandle;
	animator.clip = component.clip;
	animator.isJustStarted = false;
	// 未選択・ファイルに無い名前: 空の AnimationPlayerにする（ノードの行列が空 → モデルに入っている形で描く）
	// 名前は Component に残るので、Blender で名前を戻す・選び直すと、また流れる
	const std::optional<uint32_t> index = FindClip(component.animationHandle, component.clip.View());
	if (!index.has_value()) {
		animator.player = AnimationPlayer{};
		return;
	}
	animator.player.SetSpeed(component.speed); // 先に速さを渡す（Play は速さの向きで、流し始める時刻を決める）
	animator.player.Play(component.animationHandle, *index, component.loop);
	if (!play) {
		animator.player.Stop(); // アニメーションは覚えたまま、最初の時刻で止まる
		return;
	}
	// 流し始めたフレームは始まりの形を見せる（Update で進めると、最初の形が1フレームも見えず、呼んだ側の「段階に入ってからの秒」とも1フレームずれる）
	animator.isJustStarted = true;
}

// ===== Inspector と 保存 =====
// Animatorの見せ方。Inspectorとシーンファイルの両方がこの並びを使う
// 保存の名前はメンバ名にし、Inspector には「表示###保存の名前」で読みやすい名前を出す
void DescribeAnimator(ComponentUI& ui, AnimatorComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("オフにすると動かさない（モデルに入っている形で描く）"));
	const uint32_t previousAnimation = value.animationHandle;
	ui.AssetField("Animation###animation", value.animationHandle, AssetType::Animation, Tip("resources の下の .gltf / .glb / .fbx（アニメーションが入っている物）"));
	// ファイルを選んだとき、まだ Clip を選んでいなければ、先頭のアニメーションを選んでおく（選び直さなくても、すぐ動いて見える）
	// 読み込みのときも通るが、すぐ下の Clip の欄が JSON の名前で上書きする（JSON に "clip" が無いときだけ、先頭の名前が残る）
	if (value.animationHandle != previousAnimation && value.clip.Empty() && AnimationManager::GetAnimationCount(value.animationHandle) > 0) {
		value.clip = AnimationManager::GetAnimation(value.animationHandle, 0)->name;
	}

	// --- クリップ：名前で持つ・名前で保存する（候補はこの Animator のファイルの中の名前）---
	// ファイルを差し替えても名前はそのまま。新しいファイルに同じ名前があれば、そのアニメが流れる
	{
		const ClipSourceScope clipSource(ui, value.animationHandle);
		ui.ClipField("Clip###clip", value.clip, Tip("流すアニメーションの名前（Blender ならアクションの名前）。名前で持つので、ファイルの中で並びが変わっても同じアニメが流れる"));
	}

	// --- 選んでいるアニメーションの長さ（飾りなので保存されない） ---
	const Animation* clip = AnimationManager::FindAnimation(value.animationHandle, value.clip.View());
	if (clip != nullptr) {
		const std::string info = std::format("{:.2f} 秒 ／ このファイルに {} 個", clip->duration, AnimationManager::GetAnimationCount(value.animationHandle));
		ui.Label(info.c_str());
	}

	ui.Field("Speed###speed", value.speed, Drag(0.01f).Tip("1で等速、2で倍速、負で逆再生"));
	ui.Field("Loop###loop", value.loop, Tip("ループするか"));
	ui.Field("Play On Start###playOnStart", value.playOnStart, Tip("Play を押したとき（シーンが始まったとき）に再生する。オフならゲームから AnimationSystem::Play・Restart"));

	// --- 停止中のプレビュー ---
	ui.Separator("Preview");
	const float duration = (clip != nullptr) ? clip->duration : 0.0f;
	const FieldStyle timeStyle = (duration > 0.0f) ? Range(0.0f, duration) : AtLeast(0.0f);
	ui.Field("Preview Time###previewTime", value.previewTime, timeStyle.Tip("停止中に見せる時刻（秒）。つまんで動かすとポーズが変わる"));
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void AnimationSystem::Initialize() { 
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<AnimationState>();
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;
	state->gizmoLines.depthMode = DepthMode::Disable;
	state->gizmoLines.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く

	// Animatorの置き場所・保存・保存・Inspectorを登録
	EntityManager::RegisterComponent<AnimatorComponent>();
	ComponentSerializer::Register<AnimatorComponent>("Animator", &DescribeAnimator);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<AnimatorComponent>>("Animator", "Animation", &DescribeAnimator, "同じ Entity の Model Renderer のモデルを、アニメーションのファイルのクリップで動かす（再生するクリップは名前で選ぶ）"));
}

void AnimationSystem::Release() { state.reset(); }


//=============================================================================
// 毎フレーム
//=============================================================================
void AnimationSystem::Update(bool isGameRunning, float deltaTime) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	std::vector<AnimatorState>& animators = state->animators;

	// ===== Play / Stop が切り替わったら、一覧を捨てる =====
	// ゲームが動き出した最初のフレームは、ここより先に Script が Play を呼んでいて、そこで切り替え済み（その分は捨てない）
	SwitchGameRunning(isGameRunning);

	// ===== 消えた・外した Animatorを一覧から外す =====
	std::erase_if(animators, [](const AnimatorState& animator) { 
		return EntityManager::Get<AnimatorComponent>(animator.entity) == nullptr; 
	});

	// ===== 初めて見つけた Animatorは一覧に足す。時刻を進めて、ノードの行列を作り直す =====
	EntityManager::ForEach<AnimatorComponent>([&animators, isGameRunning, deltaTime](Handle<Entity> entity, const AnimatorComponent& component) {
		if (!component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return; // 状態は残す（有効に戻したら続きから）
		}

		// --- 一覧から探す。無ければ足す（再生中なら Play on Start）---
		AnimatorState* animator = FindAnimator(entity);
		if (animator == nullptr) {
			animators.push_back({entity});
			animator = &animators.back();
			ResetPlayer(*animator, component, isGameRunning && component.playOnStart);
		} else if (component.animationHandle != animator->animationHandle || component.clip != animator->clip) {
			// Inspector・ゲームからアニメーションから選び直した → 新しい方を最初から
			// 再生中のゲームなら、再生していた・Play On Start のときは再生する（未選択から選んでときも動き出すように）
			ResetPlayer(*animator, component, isGameRunning && (animator->player.IsPlaying() || component.playOnStart));
		}

		// --- 時刻 ---
		AnimationPlayer& player = animator->player;
		player.SetSpeed(component.speed);
		player.SetLoop(component.loop);
		if (isGameRunning) {
			// 流し始めたフレーム（Play・Restart・Play On Start・Clip の選び直し）は進めない。0 秒の形を見せて、次のフレームから進める
			if (animator->isJustStarted) {
				animator->isJustStarted = false;
			} else {
				player.Update(deltaTime); // 一時停止中は deltaTime が0なので進まない
			}
		} else {
			player.SetTime(component.previewTime); // 停止中はInspectorの時刻のポーズ
		}

		// --- ノードの行列（同じEntityのModelRendererのモデル）---
		// Model Renderer が無い・モデルが未選択なら作らない
		const ModelRendererComponent* render = EntityManager::Get<ModelRendererComponent>(entity);
		if (render != nullptr && render->modelHandle != 0) {
			player.ApplyToModel(render->modelHandle);
		}
	});
}

// ===== ノードの行列 =====
const std::vector<Matrix4x4>* AnimationSystem::GetNodeMatrices(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	// --- 早期リターン ---
	// コンポーネントを持っているか
	const AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr || !component->enabled) {
		return nullptr;
	}
	// アニメーションを持っているか
	const AnimatorState* animator = FindAnimator(entity);
	if (animator == nullptr || animator->player.GetNodeMatrices().empty()) {
		return nullptr;
	}
	return &animator->player.GetNodeMatrices();
}


//=============================================================================
// ゲームから
//=============================================================================
// ===== 名前で流す =====
bool AnimationSystem::Play(Handle<Entity> entity, std::string_view clip, bool loop, float speed) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr) {
		return false; // Animator が無い（RequestAdd した直後は、次のフレームまで付いていない）
	}
	// --- Component に書く ---
	// Update は毎フレーム、Component の Speed・Loop を AnimationPlayer へ写し、Clip が変わっていたら最初から流す。ここに書かないと Update に戻されてしまう
	component->clip = clip;
	component->loop = loop;
	component->speed = speed;
	if (!EntityManager::IsActiveInHierarchy(entity)) {
		return false; // 無効な Entity（値だけ書いておく）
	}
	// --- 同じアニメが流れているなら、ここまで（速さと loop は Update が写す）---
	// 一覧を見る前に、ゲームが動いている側へ切り替えておく（理由は Restart と同じ）
	SwitchGameRunning(true);
	const AnimatorState* animator = FindAnimator(entity);
	if (animator != nullptr && animator->animationHandle == component->animationHandle && animator->clip == component->clip && animator->player.IsPlaying()) {
		return true;
	}
	// --- 名前が違う・止まっている：最初から（ファイルに無い名前なら、Restart が警告して false）---
	return Restart(entity);
}

// ===== 最初から流し直す =====
bool AnimationSystem::Restart(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	// Restart を呼ぶのはゲーム（Script・SYSTEM。ゲームが動いている間だけ回る）なので、ここで切り替えておく
	// ゲームが動き出した最初のフレームは、Script が AnimationSystem::Update より先に回るので、切り替えないと後の Update がこの Restart を捨てる
	SwitchGameRunning(true);
	const AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	// Animatorが無い（RequestAdd した直後は、次のフレームまで付いていない）・無効なEntity
	if (component == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		return false;
	}
	// 一覧に入れるので、この後のUpdateがPlay On Startでもう一度からにすることはない
	AnimatorState& animator = FindOrAddAnimator(entity);
	ResetPlayer(animator, *component, true);
	return animator.player.IsPlaying();
}

// ===== アニメーションの長さ =====
float AnimationSystem::GetClipDuration(Handle<Entity> entity, std::string_view clip) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	const AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr) {
		return 0.0f;
	}
	// 名前は、この Entity の Animator のファイルから探す（ファイルに無ければ警告して 0）
	const std::optional<uint32_t> index = FindClip(component->animationHandle, clip);
	return index.has_value() ? AnimationManager::GetAnimation(component->animationHandle, *index)->duration : 0.0f;
}

// ===== N 秒で流しきる速さ =====
float AnimationSystem::CalcSpeedToFit(Handle<Entity> entity, std::string_view clip, float seconds) {
	const float duration = GetClipDuration(entity, clip);
	// 長さ 0 のアニメ（1枚の形だけ）・アニメが無いときは、どの速さでも同じなので等速
	return (seconds > 0.0f && duration > 0.0f) ? duration / seconds : 1.0f;
}

// ===== 止めて最初の時刻に戻す =====
void AnimationSystem::Stop(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	if (AnimatorState* animator = FindAnimator(entity)) {
		animator->player.Stop();
	}
}

// ===== 再生中か（ループしないアニメが最後まで行ったら false）=====
bool AnimationSystem::IsPlaying(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	const AnimatorState* animator = FindAnimator(entity);
	return animator != nullptr && animator->player.IsPlaying();
}

// ===== 持っているAnimationPlayerを渡す =====
AnimationPlayer* AnimationSystem::GetPlayer(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	AnimatorState* animator = FindAnimator(entity);
	return (animator != nullptr) ? &animator->player : nullptr;
}


//=============================================================================
// ギズモ（骨）
//=============================================================================
void AnimationSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	if (!state->showGizmos) {
		return;
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear(); // 中身だけ消して、メモリを確保
	gizmo.camera = camera;
	std::vector<Vector3>& positions = state->nodePositions;

	EntityManager::ForEach<ModelRendererComponent>([&gizmo, &positions](Handle<Entity> entity, const ModelRendererComponent& render) {
		// --- 早期リターン ---
		if (!render.enabled || render.modelHandle == 0 || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(render.modelHandle);
		if (transform == nullptr || asset == nullptr) {
			return;
		}
		const std::vector<ModelManager::Node>& nodes = asset->nodes;
		// 骨のないモデルは書かない
		if (std::ranges::none_of(nodes, &ModelManager::Node::isJoint)) {
			return;
		}

		// --- ノードごとのワールドの位置 ---
		// Animator があれば動いているポーズ（ModelRenderSystemが描いている物と同じ行列）、無ければモデルに入っている形
		const std::vector<Matrix4x4>* nodeMatrices = AnimationSystem::GetNodeMatrices(entity);
		const bool useAnimated = (nodeMatrices != nullptr && nodeMatrices->size() == nodes.size());
		positions.resize(nodes.size());
		// 骨全体を囲む箱（箱の大きさを決める）
		Vector3 boundsMin = {FLT_MAX, FLT_MAX, FLT_MAX};
		Vector3 boundsMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
			const Matrix4x4& nodeMatrix = useAnimated ? (*nodeMatrices)[nodeIndex] : nodes[nodeIndex].worldMatrix;
			// ノードの原点（0, 0, 0）を、ノードの行列 × Entity のワールド行列で動かした所（行ベクトルなので「ノード × Entity」の順）
			positions[nodeIndex] = TransformPoint({0.0f, 0.0f, 0.0f}, nodeMatrix * transform->worldMatrix);
			if (nodes[nodeIndex].isJoint) {
				const Vector3& p = positions[nodeIndex];
				boundsMin = {(std::min)(boundsMin.x, p.x), (std::min)(boundsMin.y, p.y), (std::min)(boundsMin.z, p.z)};
				boundsMax = {(std::max)(boundsMax.x, p.x), (std::max)(boundsMax.y, p.y), (std::max)(boundsMax.z, p.z)};
			}
		}
		const float radius = (std::max)(Length(boundsMax - boundsMin) * kJointRadiusRate, kMinJointRadius);

		// --- 骨は球、親子は線 ---
		for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
			const ModelManager::Node& node = nodes[nodeIndex];
			if (node.isJoint) {
				GizmoLines::PushWireSphere(gizmo.lines, positions[nodeIndex], radius, kJointColor, kJointCircleDivision);
			}
			// 親も骨か、骨の祖先なら線でつなぐ（骨どうしが兄弟で、親が骨でない Armatureボようなノードでもつながるように）
			if (node.isSkeleton && node.parent != ModelManager::kInvalidNode && nodes[node.parent].isSkeleton) {
				gizmo.lines.push_back({positions[node.parent], positions[nodeIndex], kBoneColor});
			}
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}

bool& AnimationSystem::GetShowGizmos() {
	HTT_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	return state->showGizmos;
}