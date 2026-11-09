#include "UIWorldFollowSystem.h"

#include <cmath>
#include <cfloat>
#include <format>
#include <memory>
#include <string>
#include <algorithm>
#include <unordered_map>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Math/MathUtility.h"
#ifdef USE_IMGUI
#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#endif

namespace {
constexpr float kMinDirectionLength = 1.0e-4f; // 向きがこれより短ければ、向きが無いとみなす（真後ろ）。そのときは下の端に寄せる

/// <summary>
/// 置く所1つ（UpdateTransforms がワールド行列を作るたびに入れる）
/// </summary>
struct Placement {
	uint32_t generation = 0;        // Handle の generation（同じ番号を、消えた後の別の Entity が使ったときに間違えないように）
	Vector2 position = {0.0f, 0.0f}; // 置く所（画面の真ん中から、ピクセル。上が＋）
};

// Handle の index → 置く所。Update で作り直す
// UpdateTransforms は全部の Entity について聞きに来るので、探すのが速い unordered_map にする
std::unordered_map<uint32_t, Placement> placements;

/// <summary>
/// 重ねる点を、画面に写した結果
/// </summary>
struct ScreenProjection {
	bool isBehind = false;            // カメラの後ろ
	Vector2 pixel = {0.0f, 0.0f};     // 写った所（画面の真ん中から、ピクセル。上が＋。カメラの後ろなら使わない）
	Vector2 direction = {0.0f, 0.0f}; // 画面の真ん中から、点の方への向き（ピクセルの縦横の比で。後ろでも、振り向く向きが合う）
};

//=============================================================================
// 写す・寄せる
//=============================================================================
// ===== ワールドの点を画面に写す =====
ScreenProjection Project(const Camera& camera, const Vector3& world) {
	ScreenProjection result;
	Vector2 ndc;
	if (camera.WorldToScreenPoint(world, &ndc)) {
		// 画面の位置（端が ±1）→ UI のピクセル（画面の真ん中から。Image の Transform と同じ数え方）
		result.pixel = GameScreen::NdcToPixel(ndc);
		result.direction = result.pixel;
		return result;
	}
	// --- カメラの後ろ：w で割ると裏返るので、カメラから見た向き（ビュー空間の x・y）を使う ---
	// 投影行列の [0][0]・[1][1] は、横・縦の広がりの倍率（画角と縦横の比）。掛けておくと、画面の位置の量になる。それをピクセルの量に直すと、画面の上での向きと同じ比になる
	result.isBehind = true;
	const Vector3 view = TransformPoint(world, camera.GetViewMatrix());
	const Matrix4x4& projection = camera.GetProjectionMatrix();
	result.direction = GameScreen::NdcToPixel({view.x * projection.m[0][0], view.y * projection.m[1][1]});
	return result;
}

// ===== 点が、端から margin だけ内側の四角の中か =====
bool IsInside(const Vector2& pixel, float margin) {
	const Vector2 half = GameScreen::GetHalfSize();
	return std::abs(pixel.x) <= half.x - margin && std::abs(pixel.y) <= half.y - margin;
}

// ===== 向きの先の、端から margin だけ内側の四角の縁の点 =====
// 真ん中から direction の向きへ伸ばして、四角の縁に当たった所（横の縁と縦の縁の、先に当たる方）
Vector2 ClampToEdge(Vector2 direction, float margin) {
	const Vector2 half = GameScreen::GetHalfSize();
	const Vector2 inner = {(std::max)(half.x - margin, 0.0f), (std::max)(half.y - margin, 0.0f)};
	if (Length(direction) < kMinDirectionLength) {
		direction = {0.0f, -1.0f}; // 真後ろ：下の端
	}
	const float scaleX = (std::abs(direction.x) > 0.0f) ? inner.x / std::abs(direction.x) : FLT_MAX;
	const float scaleY = (std::abs(direction.y) > 0.0f) ? inner.y / std::abs(direction.y) : FLT_MAX;
	const float scale = (std::min)(scaleX, scaleY);
	return {direction.x * scale, direction.y * scale};
}

// ===== 1つ置く（結果を Component に書く。置くなら true）=====
bool Place(const Camera& camera, const Vector3& world, UIWorldFollowComponent& follow) {
	const ScreenProjection projection = Project(camera, world);
	follow.isOnScreen = !projection.isBehind && IsInside(projection.pixel, 0.0f);
	follow.edgeAngle = std::atan2(projection.direction.y, projection.direction.x) * MathUtility::kRadToDeg;

	switch (follow.offscreen) {
	case OffscreenMode::Keep:
		follow.isHidden = projection.isBehind; // 後ろの点は、写すと裏返った所に出るので隠す
		follow.screenPosition = projection.pixel;
		break;
	case OffscreenMode::Hide:
		follow.isHidden = !follow.isOnScreen;
		follow.screenPosition = projection.pixel;
		break;
	case OffscreenMode::ClampToEdge:
		// 端から Edge Margin の内側にあれば、そのまま。外か後ろなら、向きの先の縁に寄せる
		if (!projection.isBehind && IsInside(projection.pixel, follow.edgeMargin)) {
			follow.screenPosition = projection.pixel;
		} else {
			follow.screenPosition = ClampToEdge(projection.direction, follow.edgeMargin);
			follow.isClamped = true;
		}
		break;
	}
	return !follow.isHidden;
}

// ===== 結果を消す（重ねていない状態）=====
void ResetResults(UIWorldFollowComponent& follow) {
	follow.isHidden = false;
	follow.isOnScreen = false;
	follow.isClamped = false;
	follow.nonCenterAnchorCount = 0;
}

//=============================================================================
// ワールド行列に置く（EntityManager::UpdateTransforms から、Entity ごとに呼ばれる）
//=============================================================================
void ApplyPlacement(Handle<Entity> entity, TransformComponent& transform, const TransformComponent* parent) {
	if (placements.empty()) {
		return; // 重ねている物が無いフレームは、何もしない（探す手間も掛けない）
	}
	const auto found = placements.find(entity.index);
	if (found == placements.end() || found->second.generation != entity.generation) {
		return;
	}
	// 今のワールド行列の位置は「親の位置 ＋ 親の向き・大きさを掛けた Translation」
	// 親の位置だけを引いて、写した点に置き換える（ずれ・回転・大きさは、そのまま残る）
	Matrix4x4& world = transform.worldMatrix;
	const Vector3 parentPosition = (parent != nullptr) ? GetWorldPosition(*parent) : Vector3{0.0f, 0.0f, 0.0f};
	world.m[3][0] = found->second.position.x + (world.m[3][0] - parentPosition.x);
	world.m[3][1] = found->second.position.y + (world.m[3][1] - parentPosition.y);
}

//=============================================================================
// エディタ：Anchor が Center でない物を数える（Inspector の警告）
//=============================================================================
#ifdef USE_IMGUI
// 自分か親で、一番近い UI World Follow（無ければ nullptr）
UIWorldFollowComponent* FindFollower(Handle<Entity> entity) {
	for (Handle<Entity> current = entity; current.IsValid();) {
		if (UIWorldFollowComponent* follow = EntityManager::Get<UIWorldFollowComponent>(current); follow != nullptr && follow->enabled) {
			return follow;
		}
		const Entity* owner = EntityManager::Get(current);
		current = (owner != nullptr) ? owner->parent : Handle<Entity>{};
	}
	return nullptr;
}

// T（Anchor を持つ UI の Component）のうち、Center でない物を、一番近い UI World Follow に数える
// UI World Follow の数ではなく、Center でない UI の数だけ親をたどる（多くの UI は Center なので軽い）
template<class T> void CountNonCenterAnchors() {
	EntityManager::ForEach<T>([](Handle<Entity> entity, const T& component) {
		if (component.anchor == ScreenAnchor::Center) {
			return;
		}
		if (UIWorldFollowComponent* follow = FindFollower(entity)) {
			++follow->nonCenterAnchorCount;
		}
	});
}
#endif

//=============================================================================
// 見せ方（Inspector・保存）
//=============================================================================
// ===== 今の状態の文字（Inspector の灰色の説明）=====
std::string MakeStatusText(const UIWorldFollowComponent& follow) {
	if (!follow.enabled) {
		return "今：重ねていない";
	}
	const char* state = follow.isHidden ? "隠している" : (follow.isClamped ? "端に寄せた" : (follow.isOnScreen ? "画面の中" : "画面の外"));
	return std::format("今：{}  置いた所 ({:.0f}, {:.0f})  向き {:.0f} 度", state, follow.screenPosition.x, follow.screenPosition.y, follow.edgeAngle);
}

void DescribeUIWorldFollow(ComponentUI& ui, UIWorldFollowComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("false の間は重ねない（Transform のとおりに置く）"));
	ui.Field("Target###target", value.target, Tip("重ねる相手（none なら World Offset をワールドの点として使う）"));
	ui.Field("World Offset###worldOffset", value.worldOffset, Drag(0.01f).Tip("相手の位置からのずれ（ワールドの向き。m）。相手が none なら、重ねるワールドの点"));
	ui.Field("Offscreen###offscreen", value.offscreen, Tip("重ねる点が画面の外・カメラの後ろのときの扱い"));
	ui.BeginDisabled(value.offscreen != OffscreenMode::ClampToEdge);
	ui.Field("Edge Margin###edgeMargin", value.edgeMargin, AtLeast(0.0f).Drag(1.0f).Tip("Clamp To Edge で、画面の端から内側へ空ける幅（ピクセル。マークが切れない大きさにする）"));
	ui.EndDisabled();
	// --- 結果と警告（飾り。保存しない）---
	ui.Label(MakeStatusText(value).c_str());
	ui.Label("位置は写した点で決まる。Transform の Translation の x・y は、そこからのずれ（ピクセル）");
	if (value.nonCenterAnchorCount > 0) {
		ui.Label(std::format("注意：この Entity と子の UI のうち {} 個の Anchor が Center ではありません（Anchor の分だけずれます）", value.nonCenterAnchorCount).c_str());
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIWorldFollowSystem::Initialize() {
	// 置き場所・保存・Inspector を登録する。見せ方は DescribeUIWorldFollow 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<UIWorldFollowComponent>();
	ComponentSerializer::Register<UIWorldFollowComponent>("UI World Follow", &DescribeUIWorldFollow);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIWorldFollowComponent>>("UI World Follow", "UI", &DescribeUIWorldFollow));
	// ワールド行列を作るたびに、置いた所を入れてもらう
	EntityManager::AddTransformAdjuster(&ApplyPlacement);
}

void UIWorldFollowSystem::Release() { placements.clear(); }


//=============================================================================
// 更新
//=============================================================================
void UIWorldFollowSystem::Update(const Camera* camera) {
	const bool hadPlacements = !placements.empty();
	placements.clear();

	// --- 1. 重ねる点を写して、置く所を決める ---
	EntityManager::ForEach<UIWorldFollowComponent>([camera](Handle<Entity> entity, UIWorldFollowComponent& follow) {
		ResetResults(follow);
		if (camera == nullptr || !follow.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return; // 重ねない（Transform のとおり）
		}
		// 重ねるワールドの点（相手が居なくなった・無効なら隠す。消えた相手の所に残らないように）
		Vector3 world = follow.worldOffset;
		if (follow.target.IsSet()) {
			const Handle<Entity> target = EntityManager::Find(follow.target);
			const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(target);
			if (targetTransform == nullptr || !EntityManager::IsActiveInHierarchy(target)) {
				follow.isHidden = true;
				return;
			}
			world = GetWorldPosition(*targetTransform) + follow.worldOffset;
		}
		if (Place(*camera, world, follow)) {
			placements[entity.index] = {entity.generation, follow.screenPosition};
		}
	});

#ifdef USE_IMGUI
	// --- 2. Anchor の警告（エディタだけ）---
	CountNonCenterAnchors<ImageComponent>();
	CountNonCenterAnchors<TextComponent>();
	CountNonCenterAnchors<VectorShapeComponent>();
	CountNonCenterAnchors<PolylineComponent>();
#endif

	// --- 3. ワールド行列を計算し直す（置く物の子も、置いた所について来る）---
	// 前のフレームに置いていた物があれば、置かなくなった物を Transform のとおりに戻すためにも計算し直す
	if (hadPlacements || !placements.empty()) {
		EntityManager::UpdateTransforms();
	}
}

bool UIWorldFollowSystem::IsHiddenInHierarchy(Handle<Entity> entity) {
	for (Handle<Entity> current = entity; current.IsValid();) {
		if (const UIWorldFollowComponent* follow = EntityManager::Get<UIWorldFollowComponent>(current); follow != nullptr && follow->enabled && follow->isHidden) {
			return true;
		}
		const Entity* owner = EntityManager::Get(current);
		current = (owner != nullptr) ? owner->parent : Handle<Entity>{};
	}
	return false;
}
