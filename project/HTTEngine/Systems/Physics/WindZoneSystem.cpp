#include "WindZoneSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include "HTTEngine/Components/Physics/RigidbodyComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
#include "HTTEngine/Math/Geometry/AABB.h"
#include "HTTEngine/Math/Geometry/OBB.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"

namespace {
constexpr float kLineFadeInTime = 0.3f;      // 範囲に入ってから、画面の風の線が出きるまでの秒数
constexpr float kLineFadeOutTime = 0.5f;     // 範囲から出てから、画面の風の線が消えるまでの秒数
constexpr Vector4 kGizmoColor = {1.0f, 0.502f, 1.0f, 1.0f}; // ギズモの線の色（うすい赤紫。空・Collider の緑・水色・赤と見分けられるように）
constexpr int kGizmoArrowRows = 3;           // ギズモの矢印を、範囲の中に縦に何本並べるか

/// <summary>
/// 集めた範囲1つ（ワールド座標。毎フレーム作り直す）
/// </summary>
struct WorldWindZone {
	OBB box;                                // 範囲（向きのある箱。size は中心から面までの長さ）
	Vector3 direction = {1.0f, 0.0f, 0.0f}; // 風の向き（長さ1。Entity の右）
	float speed = 0.0f;                     // 風の速さ（m/s。ゆれの前）
	float strength = 0.0f;                  // 風の速さまで速くする強さ（m/s²）
	float gust = 0.0f;                      // ゆれ（0〜1）
	float gustFrequency = 0.0f;             // ゆれる速さ（1秒に何回くらい）
	float gustPhase = 0.0f;                 // ゆれの始まりのずれ（範囲ごとに、ゆれがそろわないように）
	float lineAmount = 0.0f;                // 画面の風の線の濃さ
};

/// <summary>
/// WindZoneSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct WindZoneState {
	std::vector<WorldWindZone> zones;     // 今の範囲（Update で集め直す）
	float time = 0.0f;                    // ゆれと風の線の時間（秒。再生中に足していく。止めると 0 に戻す）
	float lineAmount = 0.0f;              // 今の、画面の風の線の濃さ（出る・消えるを、なめらかにする）
	Vector2 lineDirection = {1.0f, 0.0f}; // 今の、画面の風の線の向き（消えていく間は、最後の向きのまま）
	Renderer::LineListConfig gizmoLines;  // ギズモの線（使い回す）
};
std::unique_ptr<WindZoneState> state;

//=============================================================================
// ゆれ
//=============================================================================
// -1〜1 の間をゆっくり行き来する値。速さの違う sin を2つ重ねて、決まった周期に見えないようにする
// （2.3 倍の方は 1 つめと周期がそろわないので、同じゆれ方がすぐにはくり返さない）
float CalcGustNoise(float time, float frequency, float phase) {
	const float angle = 2.0f * std::numbers::pi_v<float> * frequency * time + phase;
	return 0.6f * std::sin(angle) + 0.4f * std::sin(angle * 2.3f + phase * 1.7f);
}

// その範囲の、今の「ゆれの倍率」（1 ± gust）
float CalcGustScale(const WorldWindZone& zone) { return 1.0f + zone.gust * CalcGustNoise(state->time, zone.gustFrequency, zone.gustPhase); }

//=============================================================================
// 範囲
//=============================================================================
// 箱の中か（中心からのずれを、箱の3つの軸の向きに測って、どれも半分の長さ以内なら中）
bool Contains(const OBB& box, const Vector3& position) {
	const Vector3 offset = position - box.center;
	return std::abs(Dot(offset, box.axes[0])) <= box.size.x && std::abs(Dot(offset, box.axes[1])) <= box.size.y && std::abs(Dot(offset, box.axes[2])) <= box.size.z;
}

// Entity のワールド行列から、範囲の箱を作る
// 行列の1〜3行目は、ローカルの X・Y・Z の向きに Scale を掛けた物。長さが Scale、向きが軸になる
OBB MakeBox(const WindZoneComponent& zone, const Matrix4x4& world) {
	OBB box;
	box.center = {world.m[3][0], world.m[3][1], world.m[3][2]};
	const std::array<float, 3> halfSize = {zone.size.x * 0.5f, zone.size.y * 0.5f, zone.size.z * 0.5f};
	std::array<float, 3> scaled{};
	for (int i = 0; i < 3; ++i) {
		const Vector3 row = {world.m[i][0], world.m[i][1], world.m[i][2]};
		const float length = Length(row);
		box.axes[i] = (length > 0.0f) ? row / length : Vector3{0.0f, 0.0f, 0.0f}; // Scale が 0 の軸は、向きが決まらない（その軸の長さも 0 になる）
		scaled[i] = std::abs(halfSize[i]) * length;
	}
	box.size = {scaled[0], scaled[1], scaled[2]};
	return box;
}

// 有効な Wind Zone を集め直す（停止中も集める。ギズモと、Script の Sample に使う）
void CollectZones() {
	state->zones.clear(); // 中身だけ消す（確保したメモリは残る）
	EntityManager::ForEach<WindZoneComponent>([](Handle<Entity> entity, const WindZoneComponent& zone) {
		if (!zone.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		WorldWindZone world;
		world.box = MakeBox(zone, transform->worldMatrix);
		world.direction = world.box.axes[0]; // 右（ローカルの +X）
		world.speed = zone.speed;
		world.strength = (std::max)(zone.strength, 0.0f);
		world.gust = std::clamp(zone.gust, 0.0f, 1.0f);
		world.gustFrequency = (std::max)(zone.gustFrequency, 0.0f);
		world.gustPhase = world.box.center.x * 0.37f + world.box.center.y * 0.71f; // 置いた場所で決まる、ばらばらな値
		world.lineAmount = std::clamp(zone.lineAmount, 0.0f, 1.0f);
		state->zones.push_back(world);
	});
}

//=============================================================================
// 押す
//=============================================================================
// 再生中：Wind Scale が 0 でない Rigidbody を、その位置の風で押す
void PushRigidbodies(float deltaTime) {
	EntityManager::ForEach<RigidbodyComponent>([deltaTime](Handle<Entity> entity, RigidbodyComponent& rigidbody) {
		if (rigidbody.isKinematic || rigidbody.windScale == 0.0f || !EntityManager::IsActiveInHierarchy(entity)) {
			return; // 物理で動かさない・風で押さない（自分で風を読む）
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		WindZoneSystem::WindSample wind = WindZoneSystem::Sample(GetWorldPosition(*transform));
		if (!wind.IsBlowing()) {
			return;
		}
		wind.strength *= rigidbody.windScale; // 軽い物・重い物で、押され方を変える
		rigidbody.velocity = WindZoneSystem::Push(wind, rigidbody.velocity, deltaTime);
	});
}

//=============================================================================
// 画面の風の線
//=============================================================================
// カメラが追っている物（Camera Follow の Target）の所の風で、線の濃さと向きを決めて、LensPass へ書く
void UpdateScreenLines(bool isGameRunning, float deltaTime) {
	float target = 0.0f; // 出したい濃さ
	if (isGameRunning) {
		if (const TransformComponent* followed = CameraSystem::GetFollowTargetTransform()) {
			const WindZoneSystem::WindSample wind = WindZoneSystem::Sample(GetWorldPosition(*followed));
			target = std::clamp(wind.lineAmount, 0.0f, 1.0f);
			const Vector2 direction = {wind.velocity.x, wind.velocity.y};
			if (Length(direction) > 0.0f) {
				state->lineDirection = Normalize(direction); // 風が無くなったら、最後の向きのまま消えていく
			}
		}
	}
	// 出るときは Fade In、消えるときは Fade Out の時間で、まっすぐ近づける（濃さ 1 を、その秒数で変えきる速さ）
	const float time = (target > state->lineAmount) ? kLineFadeInTime : kLineFadeOutTime;
	state->lineAmount = MathUtility::MoveTowards(state->lineAmount, target, deltaTime / time);
	if (!isGameRunning) {
		state->lineAmount = 0.0f; // 止めたらすぐ消す（エディタの停止中に残らないように）
	}
	// --- LensPass へ書く（毎フレーム書くので、上乗せの値と違って自分では消えない）---
	if (PostProcess* post = SceneRenderer::GetPost()) {
		LensPass& lens = post->GetLensPass();
		lens.windLines = state->lineAmount;
		lens.windLineDirection = state->lineDirection;
		lens.windLineTime = state->time;
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeWindZone(ComponentUI& ui, WindZoneComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと吹かない"));
	ui.Label("風は Entity の右（ローカルの +X）へ吹く。Rotation の Z で向きを変える（90 で上へ）");
	ui.Field("Size###size", value.size, AtLeast(0.0f).Tip("範囲の大きさ（m。Entity から見た箱の X・Y・Z の長さ。Scale も掛かる）。真ん中は Entity の位置"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("風の速さ（m/s）。物はこの速さまで流される（それより速い物は、それ以上押さない）"));
	ui.Field("Strength###strength", value.strength, AtLeast(0.0f).Tip("風の速さまで速くする強さ（m/s²）。上向きの風で浮かせるなら、重力（Settings の Physics → Gravity × 物の Gravity Scale）より大きくする"));
	ui.Field("Gust###gust", value.gust, Range(0.0f, 1.0f).Tip("ゆれ（0.3 なら、風の速さが ±30% の間でゆれる）。0 ならゆれない"));
	ui.Field("Gust Frequency###gustFrequency", value.gustFrequency, AtLeast(0.0f).Tip("ゆれる速さ（1秒に何回くらい。大きいほど細かくゆれる）"));
	ui.Field("Line Amount###lineAmount", value.lineAmount, Range(0.0f, 1.0f).Tip("画面の風の線の濃さ（0 なら出さない）。カメラが追っている物（Camera Follow の Target）が範囲の中にいる間だけ出る"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void WindZoneSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<WindZoneState>();

	// Wind Zone の置き場所・保存・Inspector を登録する（見せ方は DescribeWindZone 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<WindZoneComponent>();
	ComponentSerializer::Register<WindZoneComponent>("Wind Zone", &DescribeWindZone);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<WindZoneComponent>>("Wind Zone", "Physics", &DescribeWindZone));
}

void WindZoneSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void WindZoneSystem::Update(bool isGameRunning, float deltaTime) {
	HTT_ASSERT_MSG(state != nullptr, "WindZoneSystem::Initialize()を先に呼んでください");
	// 時間：再生中だけ進める（止めたら 0 に戻すので、Play を押すたびに同じゆれ方から始まる）
	state->time = isGameRunning ? state->time + deltaTime : 0.0f;
	CollectZones();
	if (isGameRunning && deltaTime > 0.0f) {
		PushRigidbodies(deltaTime);
	}
	UpdateScreenLines(isGameRunning, deltaTime);
}

//=============================================================================
// 風を読む・押す
//=============================================================================
WindZoneSystem::WindSample WindZoneSystem::Sample(const Vector3& position) {
	WindSample sample;
	if (state == nullptr) {
		return sample;
	}
	for (const WorldWindZone& zone : state->zones) {
		if (!Contains(zone.box, position)) {
			continue;
		}
		// 重なった範囲は、速さと線の濃さを足す。強さは一番強い物（足すと、重ねるほど急に速くなるので）
		const float gustScale = CalcGustScale(zone);
		sample.velocity += zone.direction * (zone.speed * gustScale);
		sample.strength = (std::max)(sample.strength, zone.strength);
		sample.lineAmount += zone.lineAmount * gustScale;
	}
	return sample;
}

Vector3 WindZoneSystem::Push(const WindSample& wind, const Vector3& velocity, float deltaTime) {
	const float windSpeed = Length(wind.velocity);
	if (!wind.IsBlowing() || windSpeed <= 0.0f || deltaTime <= 0.0f) {
		return velocity;
	}
	// 風の向きの速さ（along）が、風の速さより遅い間だけ速くする。1フレームに足すのは strength × deltaTime まで（風の速さを超えない）
	// 風の向きと直角の速さ（重力で落ちる・横へ走る）は、そのまま残る
	const Vector3 direction = wind.velocity / windSpeed;
	const float along = Dot(velocity, direction);
	if (along >= windSpeed) {
		return velocity; // もう風より速い（押さない。風で止めることもしない）
	}
	const float add = (std::min)(wind.strength * deltaTime, windSpeed - along);
	return velocity + direction * add;
}

//=============================================================================
// ギズモ
//=============================================================================
void WindZoneSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "WindZoneSystem::Initialize()を先に呼んでください");
	if (!CollisionSystem::GetShowGizmos() || state->zones.empty()) {
		return; // Collider のギズモと一緒に出す・消す
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	gizmo.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く
	for (const WorldWindZone& zone : state->zones) {
		const OBB& box = zone.box;
		// --- 箱：8つの角を、12本の辺で結ぶ ---
		const std::array<Vector3, 8> corners = box.GetCorners();
		for (const auto& [from, to] : kBoxEdges) {
			gizmo.lines.push_back({corners[from], corners[to], kGizmoColor});
		}
		// --- 風の向きの矢印：前の面（Z の−側）に、縦に何本か。左の端から右の端まで、先に「>」---
		const Vector3 front = box.center - box.axes[2] * box.size.z;
		const float head = (std::min)(box.size.x, box.size.y) * 0.3f; // 矢印の先の大きさ
		for (int row = 0; row < kGizmoArrowRows; ++row) {
			const float t = (static_cast<float>(row) + 0.5f) / static_cast<float>(kGizmoArrowRows) * 2.0f - 1.0f; // -1〜1 に等しく並べる
			const Vector3 middle = front + box.axes[1] * (box.size.y * t * 0.8f);
			const Vector3 tail = middle - zone.direction * box.size.x * 0.8f;
			const Vector3 tip = middle + zone.direction * box.size.x * 0.8f;
			gizmo.lines.push_back({tail, tip, kGizmoColor});
			gizmo.lines.push_back({tip, tip - zone.direction * head + box.axes[1] * head * 0.6f, kGizmoColor});
			gizmo.lines.push_back({tip, tip - zone.direction * head - box.axes[1] * head * 0.6f, kGizmoColor});
		}
	}
	Renderer::DrawLines(gizmo);
	gizmo.camera = nullptr; // フレームをまたいで持たない
}