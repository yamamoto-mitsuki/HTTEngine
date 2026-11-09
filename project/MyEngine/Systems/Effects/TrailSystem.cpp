#include "TrailSystem.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>
#include <optional>
#include <algorithm>

#include "MyEngine/Camera/Camera.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Math/Color.h"

namespace {
constexpr int kMinPoints = 2;                         // 帯を描くのに要る点の数（Max Points の下限にも使う）
constexpr float kSamePointDistance = 1e-4f;           // これより近い点は同じ点とみなす（向きが計算できないので）
constexpr Vector3 kGroundNormal = {0.0f, 1.0f, 0.0f}; // Ground の帯の面の向き（真上）

/// <summary>
/// Trail 1つ分の、実行中だけの状態（保存しない。Play / Stop で作り直すと消える）
/// <para>持ち主（Entity か TrailComponent）が消えたら「離れた尾」になる：entity を無効にして、写しておいた設定の After Owner で消えきるまで描く</para>
/// </summary>
struct TrailState {
	Handle<Entity> entity;                  // 持ち主（離れた尾は無効な Handle。同じ Entity に Trail を付け直しても、新しい尾は別に始まる）
	std::vector<TrailPoint> points;         // 点の列（古い方 → 新しい方）
	std::optional<TrailComponent> settings; // 描く設定の写し（毎フレーム持ち主の Component から写す。離れた後はこれで描く。まだ一度も写していなければ空）
	std::optional<Vector3> head;            // 帯の先（Emitting の持ち主の今の位置。離れるときに最後の点にする。前のフレームに描いた帯の先と同じ所）
	bool isShown = false;                   // 今描いているか（Enabled で、持ち主が有効）。描いていなかった尾は、離れるときに捨てる（急に出てこないように）
	float detachedSeconds = 0.0f;           // 持ち主から離れてからの秒（After Owner が Fade のとき、薄くするのに使う）
};

/// <summary>
/// TrailSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct TrailSystemState {
	std::vector<TrailState> trails; // 点を持っている Trail の一覧（離れた尾も入る。Trail の数しか無いので、順に見れば足りる）
	std::vector<Vector3> path;      // 描くときの点の置き場（Draw の中だけで使う。配列のメモリを使い回す）
};
std::unique_ptr<TrailSystemState> state;

// 持ち主から離れた尾か
bool IsDetached(const TrailState& trail) { return !trail.entity.IsValid(); }

// Entityの状態を探す（無ければ nullptr。離れた尾は持ち主がいないので、探しても見つからない）
TrailState* FindTrail(Handle<Entity> entity) {
	if (!entity.IsValid()) {
		return nullptr;
	}
	const auto it = std::ranges::find(state->trails, entity, &TrailState::entity);
	return (it != state->trails.end()) ? &*it : nullptr;
}

// Entityの状態を探し、無ければ足す
TrailState& FindOrAddTrail(Handle<Entity> entity) {
	if (TrailState* trail = FindTrail(entity)) {
		return *trail;
	}
	state->trails.push_back({.entity = entity});
	return state->trails.back();
}

// 持ち主がまだいるか（Entity が生きていて、TrailComponent が付いているか、付ける予約がある）
// 予約を見るのは、RequestAdd したばかり（次のフレームに付く）の Trail に、同じフレームに AddPoint した点を捨てないため
bool HasOwner(const TrailState& trail) {
	if (!EntityManager::IsAlive(trail.entity)) {
		return false;
	}
	return EntityManager::Get<TrailComponent>(trail.entity) != nullptr || EntityManager::IsAddPending<TrailComponent>(trail.entity);
}

// 持ち主から離れた後も残す設定か（いつかは消えきる物だけ残す）
// Shrink は Life Time 0 だと点が消えないので残さない。Fade は秒で消えるので Life Time 0 でも残せる（Fade Seconds 0 は残さない）
bool CanKeepAfterOwner(const TrailComponent& settings) {
	switch (settings.afterOwner) {
	case TrailAfterOwner::Shrink:
		return settings.lifeTime > 0.0f;
	case TrailAfterOwner::Fade:
		return settings.fadeSeconds > 0.0f;
	case TrailAfterOwner::Discard:
	default:
		return false;
	}
}

// 持ち主から離す。残す設定で、描いていた尾なら、帯の先を最後の点にして残す。残さないなら点を捨てる（点が無くなった尾は Update が捨てる）
// 残さないのは：After Owner が Discard・消えきらない設定（CanKeepAfterOwner）・描いていなかった（Enabled を外していた・持ち主が無効）・設定を一度も写していない
void Detach(TrailState& trail) {
	const bool canKeep = trail.settings.has_value() && CanKeepAfterOwner(*trail.settings) && trail.isShown;
	trail.detachedSeconds = 0.0f;
	if (!canKeep) {
		trail.points.clear();
	} else if (trail.head.has_value() && (trail.points.empty() || Distance(trail.points.back().position, *trail.head) > kSamePointDistance)) {
		trail.points.push_back({*trail.head, 0.0f}); // 前のフレームに描いた帯の先で終わらせる（Emitting は先を持ち主にくっつけて描いていたので）
	}
	trail.entity = {};
	trail.head.reset();
}

// 点を古くして、寿命が来た物を消す（Life Time 0 は消えない）
void AgePoints(std::vector<TrailPoint>& points, float lifeTime, float deltaTime) {
	for (TrailPoint& point : points) {
		point.age += deltaTime;
	}
	if (lifeTime > 0.0f) {
		std::erase_if(points, [lifeTime](const TrailPoint& point) { return point.age >= lifeTime; });
	}
}

// 離れた尾を1フレーム進める（写しておいた設定の After Owner で消していく。消えきったら点を空にする＝Update が捨てる）
void UpdateDetached(TrailState& trail, float deltaTime) {
	const TrailComponent& settings = *trail.settings;
	switch (settings.afterOwner) {
	// --- 縮む：点が古い方から寿命で消える ---
	case TrailAfterOwner::Shrink:
		AgePoints(trail.points, settings.lifeTime, deltaTime);
		break;
	// --- 薄くする：点は古くしない（形はそのまま）。秒を数えて、Fade Seconds が過ぎたら消す ---
	case TrailAfterOwner::Fade:
		trail.detachedSeconds += deltaTime;
		if (trail.detachedSeconds >= settings.fadeSeconds) {
			trail.points.clear();
		}
		break;
	// --- 残さない（Detach で点を捨てているので、ここには来ない）---
	case TrailAfterOwner::Discard:
	default:
		trail.points.clear();
		break;
	}
}

// 離れた尾の濃さ（1：そのまま → 0：消えた。尾全体の色の A に掛ける）。Fade 以外はいつも 1
float CalcDetachedAlpha(const TrailState& trail) {
	if (!trail.settings.has_value() || trail.settings->afterOwner != TrailAfterOwner::Fade || trail.settings->fadeSeconds <= 0.0f) {
		return 1.0f;
	}
	return std::clamp(1.0f - trail.detachedSeconds / trail.settings->fadeSeconds, 0.0f, 1.0f);
}

// 上限を超えた分を、古い方から消す
void TrimPoints(std::vector<TrailPoint>& points, int maxPoints) {
	const size_t limit = static_cast<size_t>((std::max)(maxPoints, kMinPoints));
	if (points.size() > limit) {
		points.erase(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(points.size() - limit));
	}
}

//=============================================================================
// 帯を作る
//=============================================================================
// 点の列から帯の頂点と番号を作る（点1つにつき、左右に2頂点。点と点の間を三角形2つでつなぐ）
//
//   古い方                  新しい方
//   0 ── 2 ── 4 ── ...   ← 左（side の向き）
//   │ ＼ │ ＼ │
//   1 ── 3 ── 5 ── ...   ← 右
//
// 左右の向き（side）は「進む向き × 面の向き」。面の向きは Ground なら真上、View ならその点からカメラへの向き
void BuildRibbon(const std::vector<Vector3>& path, const TrailComponent& trail, const Camera* camera, std::vector<Vertex3dData>& vertices, std::vector<uint32_t>& indices) {
	const size_t count = path.size();
	vertices.reserve(count * 2);
	indices.reserve((count - 1) * 6);

	Vector3 previousSide = {1.0f, 0.0f, 0.0f}; // 左右が計算できない点（進む向きと面の向きが同じ）は、1つ前の点と同じにする
	for (size_t i = 0; i < count; ++i) {
		// --- 進む向き（前後の点の差。端は自分と隣） ---
		const Vector3& previous = path[(i == 0) ? 0 : i - 1];
		const Vector3& next = path[(i + 1 < count) ? i + 1 : i];
		const Vector3 tangent = next - previous;

		// --- 面の向き ---
		Vector3 normal = kGroundNormal;
		if (trail.alignment == TrailAlignment::View && camera != nullptr) {
			const Vector3 toCamera = camera->GetTranslation() - path[i];
			if (LengthSq(toCamera) > kSamePointDistance * kSamePointDistance) {
				normal = Normalize(toCamera);
			}
		}

		// --- 左右の向き ---
		Vector3 side = Cross(tangent, normal);
		if (LengthSq(side) < kSamePointDistance * kSamePointDistance) {
			side = previousSide;
		} else {
			side = Normalize(side);
		}
		previousSide = side;

		// --- 太さ（古い方の端 endWidthRate → 新しい方の端 1） ---
		const float rate = static_cast<float>(i) / static_cast<float>(count - 1); // 0 が古い方、1 が新しい方
		const float halfWidth = trail.width * 0.5f * MathUtility::Lerp(trail.endWidthRate, 1.0f, rate);
		const Vector3 left = path[i] + side * halfWidth;
		const Vector3 right = path[i] - side * halfWidth;
		vertices.push_back({
		    {left.x, left.y, left.z, 1.0f},
            {rate, 0.0f},
            normal
        });
		vertices.push_back({
		    {right.x, right.y, right.z, 1.0f},
            {rate, 1.0f},
            normal
        });
	}

	// --- 点と点の間を三角形2つでつなぐ（両面で描くので、並べる向きは気にしなくてよい） ---
	for (size_t i = 0; i + 1 < count; ++i) {
		const uint32_t base = static_cast<uint32_t>(i * 2);
		indices.insert(indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Trail の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Collider・AudioSource と同じ書き方）
void DescribeTrail(ComponentUI& ui, TrailComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない（点は残る）"));
	ui.Field("Emitting###emitting", value.emitting, Tip("動いたら自動で点を足す。外すと TrailSystem::AddPoint で足した点だけ（描いた線）"));
	ui.Field("Min Vertex Distance###minVertexDistance", value.minVertexDistance, AtLeast(0.0f).Tip("自動で足すとき、この距離動くたびに1点"));
	ui.Field("Life Time###lifeTime", value.lifeTime, AtLeast(0.0f).Tip("点が消えるまでの秒。0 で消えない"));
	ui.Field("After Owner###afterOwner", value.afterOwner, Tip("持ち主（Entity かこの Component）が消えた後の消え方（新しい点は足さない）。Discard：すぐ捨てる／Shrink：点が寿命で消えて縮む（Life Time 0 なら捨てる）／Fade：形はそのままで、Fade Seconds かけて薄くして消す"));
	ui.Field("Fade Seconds###fadeSeconds", value.fadeSeconds, AtLeast(0.0f).Tip("After Owner が Fade のとき、薄くしきるまでの秒。0 ならすぐ捨てる"));
	ui.Field("Max Points###maxPoints", value.maxPoints, AtLeast(static_cast<float>(kMinPoints)).Tip("超えたら古い方から消す"));
	ui.Separator("Appearance");
	ui.Field("Width###width", value.width, AtLeast(0.0f).Tip("新しい方の端の太さ"));
	ui.Field("End Width Rate###endWidthRate", value.endWidthRate, Range(0.0f, 1.0f).Tip("古い方の端の太さ（Width に掛ける）。1 でずっと同じ太さ"));
	ui.Field("Alignment###alignment", value.alignment, Tip("View：カメラの方を向く／Ground：地面に寝かせる"));
	ui.ColorField("Color###color", value.color);
	ui.Field("Intensity###intensity", value.intensity, AtLeast(0.0f).Tip("明るさの倍率（色はそのまま）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る"));
	ui.AssetField("Texture###texture", value.textureHandle, AssetType::Texture, Tip("U が古い方 0 → 新しい方 1、V が幅の向き。無ければ白"));
	ui.Field("Blend###blend", value.blend, Tip("Add で光る線（重なるほど明るい）"));
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void TrailSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<TrailSystemState>();

	// Trail の置き場所・保存・Inspector を登録する。見せ方は DescribeTrail 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<TrailComponent>();
	ComponentSerializer::Register<TrailComponent>("Trail", &DescribeTrail);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TrailComponent>>("Trail", "Effects", &DescribeTrail));
}

void TrailSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void TrailSystem::Update(bool isGameRunning, float deltaTime) {
	MY_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	std::vector<TrailState>& trails = state->trails;

	// ===== 停止中は点を持たない（次に Play したときは、何も無いところから）=====
	if (!isGameRunning) {
		trails.clear();
		return;
	}

	// ===== 1. 持ち主（Entity か TrailComponent）が消えた尾を、持ち主から離す =====
	// 離れた尾は、写しておいた設定で点が寿命で消えきるまで描く（残さない設定なら、ここで点を捨てる）。新しい点は足さない
	for (TrailState& trail : trails) {
		if (!IsDetached(trail) && !HasOwner(trail)) {
			Detach(trail);
		}
	}

	// ===== 2. 離れた尾を After Owner で消していく（縮める・薄くする）。帯にならなくなった尾を捨てる =====
	for (TrailState& trail : trails) {
		if (IsDetached(trail) && trail.settings.has_value()) {
			UpdateDetached(trail, deltaTime);
		}
	}
	std::erase_if(trails, [](const TrailState& trail) { return IsDetached(trail) && trail.points.size() < static_cast<size_t>(kMinPoints); });

	// ===== 3. 持ち主のいる尾：設定を写し、点を古くして寿命が来た物を消す。Emitting なら今の位置を足す =====
	EntityManager::ForEach<TrailComponent>([deltaTime](Handle<Entity> entity, const TrailComponent& component) {
		TrailState* trail = FindTrail(entity);
		const bool isActive = EntityManager::IsActiveInHierarchy(entity);
		// --- 古くする・消す ---
		if (trail != nullptr) {
			AgePoints(trail->points, component.lifeTime, deltaTime);
		}
		// --- 動いた分だけ足す（無効な Entity は足さない）。今の位置は帯の先として覚えておく ---
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (component.emitting && isActive && transform != nullptr) {
			const Vector3 position = GetWorldPosition(*transform);
			TrailState& emitter = (trail != nullptr) ? *trail : FindOrAddTrail(entity);
			if (emitter.points.empty() || Distance(emitter.points.back().position, position) >= component.minVertexDistance) {
				emitter.points.push_back({position, 0.0f});
			}
			emitter.head = position;
			trail = &emitter;
		} else if (trail != nullptr) {
			trail->head.reset(); // 帯の先を持ち主にくっつけていない（Emitting でない・無効）
		}
		// --- 上限・設定の写し（持ち主が消えたら、この写しで描く）---
		if (trail != nullptr) {
			TrimPoints(trail->points, component.maxPoints);
			trail->settings = component;
			trail->isShown = component.enabled && isActive;
		}
	});
}

void TrailSystem::ClearAll() {
	MY_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	state->trails.clear();
}

//=============================================================================
// 描画
//=============================================================================
void TrailSystem::Draw(Camera* camera) {
	MY_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	std::vector<Vector3>& path = state->path;
	for (const TrailState& trail : state->trails) {
		// --- 描く設定（離れた尾は写しておいた設定。持ち主のいる尾は、Inspector で変えた値がすぐ出るように Component を読む） ---
		const bool isDetached = IsDetached(trail);
		const TrailComponent* component = isDetached ? (trail.settings.has_value() ? &*trail.settings : nullptr) : EntityManager::Get<TrailComponent>(trail.entity);
		if (component == nullptr) {
			continue;
		}
		if (!isDetached && (!component->enabled || !EntityManager::IsActiveInHierarchy(trail.entity))) {
			continue; // 離れた尾は、描いていた物だけ残しているので、ここは見ない
		}

		// --- 描く点を並べる（持ち主のいる Emitting の尾は、最後に今の位置を足して、帯の先を Entity にくっつける） ---
		path.clear();
		for (const TrailPoint& point : trail.points) {
			path.push_back(point.position);
		}
		if (!isDetached && component->emitting) {
			if (const TransformComponent* transform = EntityManager::Get<TransformComponent>(trail.entity)) {
				const Vector3 head = GetWorldPosition(*transform);
				if (path.empty() || Distance(path.back(), head) > kSamePointDistance) {
					path.push_back(head);
				}
			}
		}
		if (path.size() < static_cast<size_t>(kMinPoints)) {
			continue; // 点1つでは帯にならない
		}

		// --- 帯を作って描く ---
		std::vector<Vertex3dData> vertices;
		std::vector<uint32_t> indices;
		BuildRibbon(path, *component, camera, vertices, indices);

		Renderer::MeshConfig config;
		// 明るさの倍率はリニアで掛ける（Color.h）。離れて薄くしている尾は、A に濃さを掛ける
		// Normal も Add も「色 × A」を重ねる（RenderStates の SrcBlend が SRC_ALPHA）ので、A を 0 へ下げれば、どちらも色と明るさごと 0 になる
		config.color = MultiplyAlpha(ApplyIntensity(component->color, component->intensity), isDetached ? CalcDetachedAlpha(trail) : 1.0f);
		config.textureHandle = component->textureHandle;
		config.shadingType = ShadingType::Unlit; // 軌跡は光の当たり方を使わない（色そのまま）
		config.blendMode = (component->blend == TrailBlend::Add) ? BlendMode::Add : BlendMode::Normal;
		config.depthMode = DepthMode::TestNoWrite; // 奥の物には隠れるが、自分は奥行きを書かない（重なった帯・半透明が欠けないように）
		config.camera = camera;
		Renderer::DrawMesh(config, std::move(vertices), std::move(indices));
	}
}

//=============================================================================
// ゲームから使う
//=============================================================================
void TrailSystem::AddPoint(Handle<Entity> entity, const Vector3& position) {
	MY_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	if (!EntityManager::IsAlive(entity)) {
		return;
	}
	FindOrAddTrail(entity).points.push_back({position, 0.0f});
}

void TrailSystem::Clear(Handle<Entity> entity) {
	MY_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	if (TrailState* trail = FindTrail(entity)) {
		trail->points.clear();
	}
}

std::span<const TrailPoint> TrailSystem::GetPoints(Handle<Entity> entity) {
	MY_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	if (const TrailState* trail = FindTrail(entity)) {
		return trail->points;
	}
	return {};
}

float TrailSystem::CalcLength(Handle<Entity> entity) {
	const std::span<const TrailPoint> points = GetPoints(entity);
	float length = 0.0f;
	for (size_t i = 1; i < points.size(); ++i) {
		length += Distance(points[i - 1].position, points[i].position);
	}
	return length;
}