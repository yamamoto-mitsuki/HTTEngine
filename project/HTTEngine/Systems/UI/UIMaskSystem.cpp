#include "UIMaskSystem.h"

#include <algorithm>
#include <memory>

#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"

namespace {
constexpr uint32_t kMaxMaskDepth = static_cast<uint32_t>(UIDrawKey::kMaxDepth) - 1; // マスクの入れ子の上限（キーの段の1つは自分の分）
constexpr uint32_t kMaxSearchDepth = 64; // 親をたどって数えるマスクの上限（これより深い入れ子は数えない）

/// <summary>
/// 外にあるマスク（外から順）
/// </summary>
struct MaskChain {
	std::array<UIDrawKeyPart, kMaxMaskDepth> parts{}; // マスクの図形の段（Order・種類・番号）
	uint32_t count = 0;                               // マスクの数
};

//=============================================================================
// マスクを探す
//=============================================================================
// ===== Entity の切り抜きに使う図形（UI Mask が有効で、有効な Vector Shape か Polyline があれば true）=====
bool FindMaskShape(Handle<Entity> entity, UIDrawKind& kind, int& order) {
	const UIMaskComponent* mask = EntityManager::Get<UIMaskComponent>(entity);
	if (mask == nullptr || !mask->enabled) {
		return false;
	}
	if (const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(entity); shape != nullptr && shape->enabled) {
		kind = UIDrawKind::VectorShape;
		order = shape->order;
		return true;
	}
	if (const PolylineComponent* polyline = EntityManager::Get<PolylineComponent>(entity); polyline != nullptr && polyline->enabled) {
		kind = UIDrawKind::Polyline;
		order = polyline->order;
		return true;
	}
	return false;
}

// ===== Entity の番号（Handle が古ければ 0）=====
EntityId IdOf(Handle<Entity> entity) {
	const Entity* owner = EntityManager::Get(entity);
	return (owner != nullptr) ? owner->id : 0;
}

// ===== 親 =====
Handle<Entity> ParentOf(Handle<Entity> entity) {
	const Entity* owner = EntityManager::Get(entity);
	return (owner != nullptr) ? owner->parent : Handle<Entity>{};
}

// ===== 外にあるマスクを、外から順に集める（includeSelf なら entity 自身のマスクも入れる）=====
// 8 段より多ければ外の 8 段だけ（内のマスクは切り抜かない。そのマスクの図形も、ふつうの図形として描かれる）
MaskChain CollectMasks(Handle<Entity> entity, bool includeSelf) {
	// --- 内から外へたどって集める ---
	std::array<UIDrawKeyPart, kMaxSearchDepth> innerFirst{};
	uint32_t found = 0;
	for (Handle<Entity> current = includeSelf ? entity : ParentOf(entity); current.IsValid() && found < kMaxSearchDepth; current = ParentOf(current)) {
		UIDrawKind kind = UIDrawKind::Image;
		int order = 0;
		if (FindMaskShape(current, kind, order)) {
			innerFirst[found++] = {order, static_cast<uint32_t>(kind), IdOf(current)};
		}
	}
	// --- 外から順に並べ直す ---
	MaskChain chain;
	chain.count = (std::min)(found, kMaxMaskDepth);
	for (uint32_t i = 0; i < chain.count; ++i) {
		chain.parts[i] = innerFirst[found - 1 - i];
	}
	return chain;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeUIMask(ComponentUI& ui, UIMaskComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと切り抜かない（図形はふつうに描く）"));
	ui.Field("Show Graphic###showGraphic", value.showGraphic, Tip("切り抜く図形も描く。外すと、子を切り抜くだけで、図形（縁取りも）は見えない"));
	ui.Label("同じ Entity の Vector Shape（無ければ Polyline）の形で、子の UI を切り抜く。縁取りは子より手前に描く");
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIMaskSystem::Initialize() {
	// UI Mask の置き場所・保存・Inspector を登録する（見せ方は DescribeUIMask 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<UIMaskComponent>();
	ComponentSerializer::Register<UIMaskComponent>("UI Mask", &DescribeUIMask);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIMaskComponent>>("UI Mask", "UI", &DescribeUIMask));
}

void UIMaskSystem::Release() {} // 持っている物は無い（描く順は毎フレーム、Component から求める）

//=============================================================================
// 描き方
//=============================================================================
UIDrawStep UIMaskSystem::MakeStep(Handle<Entity> entity, int order, UIDrawKind kind) {
	// --- この Component が、自分の切り抜きに使う図形か（なら自分のマスクの外。違えば、同じ Entity の中身として自分のマスクの中）---
	UIDrawKind maskKind = UIDrawKind::Image;
	int maskOrder = 0;
	const bool isMaskShape = FindMaskShape(entity, maskKind, maskOrder) && maskKind == kind;
	const MaskChain chain = CollectMasks(entity, !isMaskShape);

	// --- キー：外のマスクの段 → 自分の段 ---
	UIDrawStep step;
	std::copy_n(chain.parts.begin(), chain.count, step.key.parts.begin());
	step.key.parts[chain.count] = {order, static_cast<uint32_t>(kind), IdOf(entity)};
	step.key.depth = chain.count + 1;
	if (isMaskShape && chain.count < kMaxMaskDepth) {
		step.key.phase = -1; // マスクの図形の位置は「始まり」（子より奥）
	}
	// --- マスクの中なら、ステンシルの値が外のマスクの数と同じ所だけに描く ---
	step.stencil = (chain.count > 0) ? StencilMode::Test : StencilMode::None;
	step.stencilRef = chain.count;
	return step;
}

UIShapeSteps UIMaskSystem::PlanShapeSteps(Handle<Entity> entity, int order, UIDrawKind kind, bool hasFill, bool hasStroke) {
	UIShapeSteps plan;
	UIDrawStep base = MakeStep(entity, order, kind);
	// --- マスクの図形でなければ1回（入れ子が深すぎるマスクも、ふつうの図形として描く）---
	const UIMaskComponent* mask = EntityManager::Get<UIMaskComponent>(entity);
	if (base.key.phase >= 0 || mask == nullptr) {
		base.key.phase = 0;
		plan.steps[plan.count++] = base;
		return plan;
	}
	// --- マスクの図形：子より奥に塗り → 形を書く → （子）→ 形を消す → 子より手前に縁取り ---
	const uint32_t level = base.stencilRef; // 外にあるマスクの数（この図形自身は、外のマスクの中に描く）
	const StencilMode outer = (level > 0) ? StencilMode::Test : StencilMode::None;
	const auto add = [&](int32_t phase, uint32_t stepIndex, StencilMode stencil, uint32_t stencilRef, UIDrawPart part, bool isGraphic) {
		UIDrawStep& step = plan.steps[plan.count++];
		step = base;
		step.key.phase = phase;
		step.key.step = stepIndex;
		step.stencil = stencil;
		step.stencilRef = stencilRef;
		step.part = part;
		step.isGraphic = isGraphic;
	};
	if (mask->showGraphic && hasFill) {
		add(-1, 0, outer, level, UIDrawPart::Fill, true);
	}
	add(-1, 1, StencilMode::Increment, level, UIDrawPart::MaskRegion, false);
	add(+1, 0, StencilMode::Decrement, level + 1, UIDrawPart::MaskRegion, false);
	if (mask->showGraphic && hasStroke) {
		add(+1, 1, outer, level, UIDrawPart::Stroke, true);
	}
	return plan;
}