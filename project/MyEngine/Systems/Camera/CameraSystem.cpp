#include "CameraSystem.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "MyEngine/Camera/Camera.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Graphics/PostEffect/PostOverride.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Graphics/Renderer/SceneRenderer.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Math/RandomEngine.h"
#include "MyEngine/Systems/Camera/CameraFollow.h"
#include "MyEngine/Systems/Camera/CameraShake.h"
#include "MyEngine/Systems/Camera/CameraVolume.h"
#include "MyEngine/Time/EngineTime.h"

namespace {
const Vector3 kDefaultPosition = {0.0f, 0.0f, -10.0f}; // Camera Component が無いときの位置（原点を正面から見る）
constexpr float kMinLookDistanceSq = 1e-6f;            // 見る点がカメラとほとんど同じ位置なら、向きが決まらないので Transform の向きのままにする

/// <summary>
/// 1フレーム分の、カメラが写す値
/// </summary>
struct CameraOutput {
	Vector3 position = kDefaultPosition;            // 位置（ワールド）
	Quaternion rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 向き（ワールド）
	float fovY = CameraComponent{}.fovY;            // 縦の視野角（度）
	float nearZ = CameraComponent{}.nearZ;          // 近い方の端
	float farZ = CameraComponent{}.farZ;            // 遠い方の端
	PostOverrides post;                             // ポストエフェクトの上書き
};

/// <summary>
/// カメラの切り替えの補間
/// </summary>
struct CameraBlend {
	bool isActive = false;                          // 補間の途中か
	Handle<Entity> fromEntity;                      // 切り替え前のカメラ（無効なら from は止まった値のまま）
	CameraOutput from;                              // 補間の始まりの値（fromEntity が有効な間は毎フレーム取り直す）
	float elapsed = 0.0f;                           // 経った秒
	float duration = 0.0f;                          // 補間にかける秒
	Ease::Type ease = Ease::Type::Linear;           // 速さの変え方
	TimeMode timeMode = TimeMode::Game;             // 使う時間
};

/// <summary>
/// 送られた衝撃（次の Update で Camera Shake へ渡す）
/// </summary>
struct PendingImpulse {
	float strength = 0.0f;                  // 最初に振れる幅（m）
	Vector3 direction = {0.0f, 0.0f, 0.0f}; // 最初に振れる向き
	bool isWorldDirection = false;          // true：direction はワールドの向き false：カメラの右・上・前の向き
};

/// <summary>
/// CameraSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct CameraState {
	// ゲームの絵を撮るカメラ（GetMainCamera はいつもこれを返す）
	// shared_ptr にするのは、パーティクル（ParticleManager）が weak_ptr で「見ているだけ」にできるように
	std::shared_ptr<Camera> camera = std::make_shared<Camera>();
	Handle<Entity> mainEntity;            // 今使っている Camera Component の Entity
	bool hasLoggedNoCamera = false;       // 「Camera Component が無い」を知らせたか（続けて何度も出さない）
	bool hasOutput = false;               // output に前のフレームの値が入っているか（false なら、次の切り替えは補間しない）
	CameraOutput output;                  // 前のフレームに写した値（切り替えの途中でさらに切り替えるときは、ここから始める）
	CameraBlend blend;                    // 切り替えの補間
	std::vector<PendingImpulse> impulses; // 送られた衝撃
};
std::unique_ptr<CameraState> state;

//=============================================================================
// 写す
//=============================================================================
// 写り方をカメラへ（視野角は度で持っているので、ラジアンに直して渡す）
void ApplyProjection(float fovYDegrees, float nearZValue, float farZValue) {
	const float fovY = std::clamp(fovYDegrees, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
	const float nearZ = (std::max)(nearZValue, 0.001f);
	const float farZ = (std::max)(farZValue, nearZ + 0.01f);                    // far が near より手前だと、何も映らない
	state->camera->Initialize(fovY, GameScreen::GetAspectRatio(), nearZ, farZ); // Initialize の中で行列も作り直す
}

// 1フレーム分の値をカメラへ
void ApplyOutput(const CameraOutput& output) {
	// Camera クラスの回転は X → Y → Z の順の角度（ラジアン）なので、行列にしてから取り出す
	const Transform pose = DecomposeAffineMatrix(MakeAffineMatrix({1.0f, 1.0f, 1.0f}, output.rotation, output.position));
	state->camera->SetTranslation(pose.translation);
	state->camera->SetRotation(pose.rotation);
	state->camera->SetScale({1.0f, 1.0f, 1.0f});
	ApplyProjection(output.fovY, output.nearZ, output.farZ);
	SceneRenderer::SetCameraPostOverrides(output.post);
}

// Camera Component が無いときの、既定のカメラ
void UseDefaultCamera() {
	state->camera->SetTranslation(kDefaultPosition);
	state->camera->SetRotation({0.0f, 0.0f, 0.0f});
	state->camera->SetScale({1.0f, 1.0f, 1.0f});
	ApplyProjection(CameraComponent{}.fovY, CameraComponent{}.nearZ, CameraComponent{}.farZ);
}

//=============================================================================
// 時間
//=============================================================================
// ===== その Entity のカメラが使う時間の秒（Camera Component が無ければゲームの時間）=====
float GetCameraDeltaTime(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return Time::GetDeltaTime(camera ? camera->timeMode : TimeMode::Game);
}

//=============================================================================
// Inspector と保存
//=============================================================================
// ===== Camera =====
// Inspector とシーンファイルの両方がこの並びを使う（保存の名前はメンバ名）
void DescribeCamera(ComponentUI& ui, CameraComponent& value) {
	ui.Field("Field Of View###fovY", value.fovY, Range(kMinCameraFovY, kMaxCameraFovY).Tip("縦の視野角（度）。小さいほど望遠"));
	ui.Field("Near###nearZ", value.nearZ, AtLeast(0.001f).Tip("これより近い物は映らない"));
	ui.Field("Far###farZ", value.farZ, AtLeast(0.01f).Tip("これより遠い物は映らない"));
	ui.Field("Priority###priority", value.priority, Tip("有効な Camera が複数あるとき、大きい方を使う（切り替えはこの値か、Entity の有効・無効で）"));
	ui.Separator("Blend");
	ui.Field("Style###blendStyle", value.blendStyle, Tip("このカメラへ切り替わるときの動き方。Cut：すぐ切り替える / Ease：Time の秒をかけて補間する（Camera Volume の値も一緒に）"));
	ui.BeginDisabled(value.blendStyle != CameraBlendStyle::Ease);
	ui.Field("Time###blendTime", value.blendTime, AtLeast(0.0f).Tip("補間にかける秒。0 ならすぐ切り替える"));
	ui.Field("Ease###blendEase", value.blendEase, Tip("補間の速さの変え方"));
	ui.EndDisabled();
	ui.Separator("Time");
	ui.Field(
	    "Time Mode###timeMode", value.timeMode,
	    Tip("このカメラの切り替え・Camera Follow・Camera Look At・Camera Shake が使う時間。Game：スローで遅くなり、ヒットストップ・ポーズで止まる / Real：止まらない"));
}

// ===== Camera Look At =====
// 実行中だけの値は並べない＝保存しない
void DescribeCameraLookAt(ComponentUI& ui, CameraLookAtComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと Transform の向きのまま"));
	ui.Field("Target###target", value.target, Tip("見る Entity（Hierarchy からドラッグ）。空なら Offset をワールドの座標として見る"));
	ui.Field("Offset###offset", value.offset, Tip("見る点のずれ（m。ワールドの向き）"));
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("見る点が追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる（再生中だけ効く）"));
}

//=============================================================================
// 注視点
//=============================================================================
// ===== 見たい点（Target が居なくなったら nullopt）=====
std::optional<Vector3> FindLookAtPoint(const CameraLookAtComponent& lookAt) {
	if (!lookAt.target.IsSet()) {
		return lookAt.offset;
	}
	const TransformComponent* target = EntityManager::Get<TransformComponent>(lookAt.target);
	if (target == nullptr) {
		return std::nullopt;
	}
	return GetWorldPosition(*target) + lookAt.offset;
}

// ===== 今見ている点を、見たい点へ近づける =====
void UpdateLookAt(CameraLookAtComponent& lookAt, float deltaTime) {
	const std::optional<Vector3> point = FindLookAtPoint(lookAt);
	if (!lookAt.enabled || !point.has_value()) {
		lookAt.isAiming = false;
		return;
	}
	if (!lookAt.isAiming) {
		lookAt.currentPoint = *point; // 見始め：カメラの向きが置いた向きから流れないように、すぐ合わせる
		lookAt.isAiming = true;
		return;
	}
	lookAt.currentPoint += (*point - lookAt.currentPoint) * CameraFollow::CalcSmoothRate(deltaTime, lookAt.smoothTime);
}

// ===== 今見る点。再生中はなめらかにした点、停止中は見たい点そのもの（Inspector で変えた値がすぐ見えるように）=====
std::optional<Vector3> GetLookAtPoint(const CameraLookAtComponent& lookAt, bool isGameRunning) {
	if (!lookAt.enabled) {
		return std::nullopt;
	}
	if (isGameRunning && lookAt.isAiming) {
		return lookAt.currentPoint;
	}
	return FindLookAtPoint(lookAt);
}

//=============================================================================
// カメラ1台分の値
//=============================================================================
// ===== Camera Volume のピントの奥行き（m。全体の設定のままなら nullopt）=====
std::optional<float> CalcVolumeFocusDistance(Handle<Entity> entity, const CameraVolumeComponent& volume, const CameraOutput& output, const std::optional<Vector3>& lookAtPoint) {
	// カメラの前の向きに測った長さ（真ん中から外れた所でも、同じ奥行きの面にピントが合う）
	const auto depthOf = [&output](const Vector3& point) { return (std::max)(Dot(point - output.position, RotateVector({0.0f, 0.0f, 1.0f}, output.rotation)), 0.0f); };
	switch (volume.focus) {
	case CameraVolumeFocus::Global:
		return std::nullopt;
	case CameraVolumeFocus::Distance:
		return volume.focusDistance;
	case CameraVolumeFocus::LookAtTarget:
		return lookAtPoint.has_value() ? std::optional<float>(depthOf(*lookAtPoint)) : std::nullopt;
	case CameraVolumeFocus::FollowTarget: {
		const CameraFollowComponent* follow = EntityManager::Get<CameraFollowComponent>(entity);
		const TransformComponent* target = follow ? EntityManager::Get<TransformComponent>(follow->target) : nullptr;
		return target ? std::optional<float>(depthOf(GetWorldPosition(*target))) : std::nullopt;
	}
	}
	MY_ASSERT_MSG(false, "知らない CameraVolumeFocus です");
	return std::nullopt;
}

// ===== カメラ1台の、このフレームの値 =====
// Transform は読むだけ。注視点・揺れはここで足すので、Transform には残らない
CameraOutput EvaluateCamera(Handle<Entity> entity, const TransformComponent& transform, const CameraComponent& camera, bool isGameRunning) {
	CameraOutput output;
	output.position = GetWorldPosition(transform);
	output.rotation = GetWorldRotation(transform);
	output.fovY = camera.fovY;
	output.nearZ = camera.nearZ;
	output.farZ = camera.farZ;

	// --- 注視点：向きだけ置き換える ---
	std::optional<Vector3> lookAtPoint;
	if (const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity)) {
		lookAtPoint = GetLookAtPoint(*lookAt, isGameRunning);
	}
	if (lookAtPoint.has_value() && LengthSq(*lookAtPoint - output.position) > kMinLookDistanceSq) {
		output.rotation = MakeLookRotation(*lookAtPoint - output.position);
	}
	// --- ポストエフェクト（揺れを足す前の位置で測る。揺れでピントが動かないように）---
	if (const CameraVolumeComponent* volume = EntityManager::Get<CameraVolumeComponent>(entity)) {
		output.post = CameraVolume::MakeOverrides(*volume, CalcVolumeFocusDistance(entity, *volume, output, lookAtPoint));
	}
	// --- 揺れ：カメラの向きから見たずれを足す ---
	const CameraShakeComponent* shake = EntityManager::Get<CameraShakeComponent>(entity);
	if (isGameRunning && shake != nullptr && shake->enabled) {
		const CameraShakeOffset offset = CameraShake::CalcOffset(*shake);
		output.position += RotateVector(offset.position, output.rotation);
		output.rotation = Normalize(output.rotation * MakeQuaternionFromEuler(offset.rotation)); // 右に掛けた方が先に回る＝カメラの軸のまわりに回る
	}
	return output;
}

//=============================================================================
// 切り替えの補間
//=============================================================================
// ===== 2つの値の間 =====
CameraOutput LerpOutput(const CameraOutput& from, const CameraOutput& to, float t) {
	CameraOutput output;
	output.position = Lerp(from.position, to.position, t);
	output.rotation = Slerp(from.rotation, to.rotation, t);
	output.fovY = Ease::Lerp(from.fovY, to.fovY, t);
	output.nearZ = Ease::Lerp(from.nearZ, to.nearZ, t);
	output.farZ = Ease::Lerp(from.farZ, to.farZ, t);
	output.post = LerpPostOverrides(from.post, to.post, t);
	return output;
}

// ===== 切り替わったカメラへの補間を始める =====
void StartBlend(Handle<Entity> previous, const CameraComponent& next) {
	CameraBlend& blend = state->blend;
	const bool wasBlending = blend.isActive;
	blend.isActive = (next.blendStyle == CameraBlendStyle::Ease) && next.blendTime > 0.0f;
	// 始まりは「今見えている値」（前のフレームに写した値）。補間の途中でも、途中の値から始まるので絵が飛ばない
	blend.from = state->output;
	// 切り替え前のカメラが動いていれば、補間の間も一緒に動かす（追いかけているカメラの絵が置いていかれないように）
	// 補間の途中で切り替えたときは、2つのカメラの間の値なので、止まった値のまま使う
	blend.fromEntity = wasBlending ? Handle<Entity>{} : previous;
	blend.elapsed = 0.0f;
	blend.duration = next.blendTime;
	blend.ease = next.blendEase;
	blend.timeMode = next.timeMode;
}

// ===== 切り替え前のカメラの今の値を取り直す =====
void RefreshBlendSource(bool isGameRunning) {
	CameraBlend& blend = state->blend;
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(blend.fromEntity);
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(blend.fromEntity);
	if (transform == nullptr || camera == nullptr) {
		blend.fromEntity = {}; // 途中で消えた：最後に取った値のまま使う
		return;
	}
	blend.from = EvaluateCamera(blend.fromEntity, *transform, *camera, isGameRunning);
}

// ===== 補間を進めて、今写す値を返す =====
CameraOutput ApplyBlend(const CameraOutput& target, bool isGameRunning) {
	CameraBlend& blend = state->blend;
	if (!blend.isActive) {
		return target;
	}
	blend.elapsed += Time::GetDeltaTime(blend.timeMode);
	if (blend.elapsed >= blend.duration) {
		blend.isActive = false;
		return target;
	}
	RefreshBlendSource(isGameRunning);
	return LerpOutput(blend.from, target, Ease::Apply(blend.elapsed / blend.duration, blend.ease));
}

//=============================================================================
// 揺れ
//=============================================================================
// ===== 送られた衝撃を Camera Shake へ渡す =====
// AddImpulse を Inspector のボタンからも呼ぶので、そこでは Component を書き換えない（書き換えると Undo の履歴に積まれる）
void ApplyPendingImpulses() {
	for (const PendingImpulse& impulse : state->impulses) {
		EntityManager::ForEach<CameraShakeComponent>([&impulse](Handle<Entity> entity, CameraShakeComponent& shake) {
			const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
			if (!shake.enabled || !EntityManager::IsActiveInHierarchy(entity) || transform == nullptr) {
				return;
			}
			// ワールドの向きは、カメラごとの右・上・前の向きに直す（長さ1の回転の逆は共役）
			const Vector3 direction = impulse.isWorldDirection ? RotateVector(impulse.direction, Conjugate(GetWorldRotation(*transform))) : impulse.direction;
			CameraShake::AddImpulse(shake, impulse.strength, direction);
		});
	}
	state->impulses.clear();
}

// ===== 注視点と揺れを進める（どのカメラも。補間の間は切り替え前のカメラも写るので）=====
void UpdateCameraMotion() {
	ApplyPendingImpulses();
	EntityManager::ForEach<CameraLookAtComponent>([](Handle<Entity> entity, CameraLookAtComponent& lookAt) {
		if (EntityManager::IsActiveInHierarchy(entity)) {
			UpdateLookAt(lookAt, GetCameraDeltaTime(entity));
		}
	});
	EntityManager::ForEach<CameraShakeComponent>([](Handle<Entity> entity, CameraShakeComponent& shake) {
		if (shake.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			CameraShake::Update(shake, GetCameraDeltaTime(entity));
		}
	});
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void CameraSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<CameraState>();
	UseDefaultCamera(); // 最初の Update より前（シーンの Initialize）に使われても、ちゃんとしたカメラを返せるように

	EntityManager::RegisterComponent<CameraComponent>();
	ComponentSerializer::Register<CameraComponent>("Camera", &DescribeCamera);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraComponent>>("Camera", "Camera", &DescribeCamera));

	EntityManager::RegisterComponent<CameraFollowComponent>();
	ComponentSerializer::Register<CameraFollowComponent>("Camera Follow", &CameraFollow::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraFollowComponent>>("Camera Follow", "Camera", &CameraFollow::Describe));

	EntityManager::RegisterComponent<CameraLookAtComponent>();
	ComponentSerializer::Register<CameraLookAtComponent>("Camera Look At", &DescribeCameraLookAt);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraLookAtComponent>>("Camera Look At", "Camera", &DescribeCameraLookAt));

	EntityManager::RegisterComponent<CameraShakeComponent>();
	ComponentSerializer::Register<CameraShakeComponent>("Camera Shake", &CameraShake::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraShakeComponent>>("Camera Shake", "Camera", &CameraShake::Describe));

	EntityManager::RegisterComponent<CameraVolumeComponent>();
	ComponentSerializer::Register<CameraVolumeComponent>("Camera Volume", &CameraVolume::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraVolumeComponent>>("Camera Volume", "Camera", &CameraVolume::Describe));
}

void CameraSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
bool CameraSystem::UpdateFollow() {
	bool isMoved = false;
	EntityManager::ForEach<CameraFollowComponent>([&isMoved](Handle<Entity> entity, CameraFollowComponent& follow) {
		if (!follow.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const float deltaTime = GetCameraDeltaTime(entity);
		if (deltaTime <= 0.0f) {
			return; // このカメラの時間が止まっている（一時停止・ヒットストップ中のゲームの時間など）
		}
		if (CameraFollow::Update(entity, follow, deltaTime)) {
			isMoved = true;
		}
	});
	return isMoved;
}

void CameraSystem::Update(bool isGameRunning) {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");

	// ===== 1. 使う Camera を選ぶ（有効な物の中で priority が一番大きい物。同じなら先に見つけた物）=====
	Handle<Entity> best;
	CameraComponent bestComponent;
	EntityManager::ForEach<CameraComponent>([&](Handle<Entity> entity, const CameraComponent& component) {
		if (!EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		if (!best.IsValid() || component.priority > bestComponent.priority) {
			best = entity;
			bestComponent = component;
		}
	});
	const Handle<Entity> previous = state->mainEntity;
	state->mainEntity = best;

	// ===== 2. 無ければ既定のカメラ =====
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(best);
	if (transform == nullptr) {
		if (!state->hasLoggedNoCamera) {
			LogManager::Log("Camera Component が無いので、既定のカメラ（(0, 0, -10) から +Z を向く）で撮ります。Hierarchy の ＋ → Camera で置けます");
			state->hasLoggedNoCamera = true;
		}
		UseDefaultCamera();
		SceneRenderer::SetCameraPostOverrides({});
		ResetOutput();
		return;
	}
	state->hasLoggedNoCamera = false;

	// ===== 3. カメラが変わったら、補間を始める =====
	// 停止中と、シーンの最初のフレームは補間しない（エディタで値を変えた結果・置いた通りの絵をすぐ見せる）
	if (!isGameRunning) {
		state->blend.isActive = false;
		state->impulses.clear();
	} else if (best != previous && state->hasOutput) {
		StartBlend(previous, bestComponent);
	}

	// ===== 4. 注視点と揺れを進める =====
	if (isGameRunning) {
		UpdateCameraMotion();
	}

	// ===== 5. 写す値を作って、カメラへ写す =====
	// 大きさは写さない（ビュー行列に大きさが混ざると、絵が伸び縮みする）
	const CameraOutput output = ApplyBlend(EvaluateCamera(best, *transform, bestComponent, isGameRunning), isGameRunning);
	ApplyOutput(output);
	state->output = output;
	state->hasOutput = true;
}

void CameraSystem::ResetOutput() {
	state->hasOutput = false;
	state->blend.isActive = false;
}

//=============================================================================
// 取得
//=============================================================================
Camera* CameraSystem::GetMainCamera() {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->camera.get();
}

std::shared_ptr<Camera> CameraSystem::GetSharedMainCamera() {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->camera;
}

Handle<Entity> CameraSystem::GetMainCameraEntity() {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->mainEntity;
}

bool CameraSystem::IsBlending() {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->blend.isActive;
}

const TransformComponent* CameraSystem::GetFollowTargetTransform() {
	// Target の Entity → Transform（どこかで無ければ nullptr）
	return EntityManager::Get<TransformComponent>(GetFollowTargetEntity());
}

Handle<Entity> CameraSystem::GetFollowTargetEntity() {
	// メインカメラの Camera Follow → その Target の Entity（どこかで無ければ無効な Handle）
	const CameraFollowComponent* follow = EntityManager::Get<CameraFollowComponent>(GetMainCameraEntity());
	return (follow != nullptr) ? EntityManager::Find(follow->target) : Handle<Entity>{};
}

//=============================================================================
// 衝撃
//=============================================================================
void CameraSystem::AddImpulse(float strength) {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	// 画面の面の中で、向きをランダムに決める
	const float angle = RandomEngine::GetFloat(0.0f, 2.0f * std::numbers::pi_v<float>);
	state->impulses.push_back({
	    strength, {std::cos(angle), std::sin(angle), 0.0f},
         false
    });
}

void CameraSystem::AddImpulse(float strength, const Vector3& worldDirection) {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	state->impulses.push_back({strength, worldDirection, true});
}