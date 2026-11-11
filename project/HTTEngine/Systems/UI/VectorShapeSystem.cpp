#include "VectorShapeSystem.h"

#include <array>
#include <cmath>
#include <memory>
#include <algorithm>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/UI/UIMaskSystem.h"
#include "HTTEngine/Systems/UI/UIWorldFollowSystem.h"

namespace {
constexpr float kEdgeMargin = 2.0f;      // 四角の板を図形より広げる量（ピクセル。縁のなめらかな所が板の外で切れないように）
constexpr float kMinScale = 1.0e-4f;     // これより小さい Scale は描かない（0 で割らないように）
constexpr float kNoStrokeEdge = 1.0e5f;  // 縁取りが無いときの、塗りの内側の線（図形の外まで全部塗りにする）
constexpr float kMaxSkewDegrees = 80.0f; // 平行四辺形の傾きの上限（度。90 に近づくと横の長さが無限になる）
constexpr float kMaxTopShift = 0.9f;     // 平行四辺形の上の辺が横へずれる上限（幅の半分に対して。これで上の辺が幅の 1 割は残り、図形が消えない）

//=============================================================================
// Component の値 → シェーダーの値
//=============================================================================
// ===== 2Dの向き（x・y）を、左へ 90 度回す =====
Vector2 RotateLeft(const Vector2& v) { return {-v.y, v.x}; }

// ===== 図形の範囲の半分（ピクセル。ゲージが端から端まで動く範囲）=====
Vector2 CalcShapeExtent(VectorShapeType shape, const Vector2& half) {
	switch (shape) {
	case VectorShapeType::Circle:
	case VectorShapeType::Ring: {
		const float radius = (std::min)(half.x, half.y);
		return {radius, radius};
	}
	default:
		return half;
	}
}

// ===== 形 =====
// --- 角の半径を、シェーダーが読める値に直す（どの角も、短い方の辺の半分まで）---
Vector4 ClampCornerRadius(const Vector4& radius, float scale, float maxRadius) {
	const auto clampOne = [scale, maxRadius](float value) { return std::clamp(value * scale, 0.0f, maxRadius); };
	return {clampOne(radius.x), clampOne(radius.y), clampOne(radius.z), clampOne(radius.w)};
}

// --- 図形の形の値を、シェーダーが読む shapeHalf・shapeParam・cornerRadius に入れる ---
// half は図形の Size の半分、uniformScale は角の丸み・太さに掛ける倍率。戻り値は平行四辺形の傾き（横のずれ ÷ 縦。ほかの形は 0）
float WriteShapeParams(const VectorShapeComponent& shape, const Vector2& half, float uniformScale, VectorShapeMaterialData& material) {
	const float shortHalf = (std::min)(half.x, half.y);
	material.shapeHalf = half;
	switch (shape.shape) {
	case VectorShapeType::Rectangle:
		material.cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f}; // 角丸四角形の計算を半径 0 で使う
		break;
	case VectorShapeType::RoundedRectangle:
		material.cornerRadius = ClampCornerRadius(shape.cornerRadius, uniformScale, shortHalf);
		break;
	case VectorShapeType::Circle:
		material.shapeParam.x = shortHalf;
		break;
	case VectorShapeType::Ellipse:
		break; // shapeHalf だけ使う
	case VectorShapeType::Ring: {
		// x：太さの真ん中の半径 / y：太さの半分
		const float thickness = std::clamp(shape.ringThickness * uniformScale, 0.0f, shortHalf);
		material.shapeParam = {shortHalf - thickness * 0.5f, thickness * 0.5f};
		break;
	}
	case VectorShapeType::Parallelogram: {
		// 上の辺の真ん中が、図形の真ん中から横へ topShift だけずれる。Size の四角にちょうど入るように、真ん中の高さの半分の幅 middleHalf を決める
		const float angle = std::clamp(shape.skewAngle, -kMaxSkewDegrees, kMaxSkewDegrees) * MathUtility::kDegToRad;
		const float slope = std::tan(angle);
		const float maxShift = half.x * kMaxTopShift;
		const float topShift = std::clamp(half.y * slope, -maxShift, maxShift);
		const float clampedSlope = (half.y > 0.0f) ? topShift / half.y : 0.0f;
		const float middleHalf = half.x - std::abs(topShift);
		// 角を丸める：形を半径の分だけ内側へ縮めて、外へ半径の分だけ太らせる（縮めるとき、斜めの辺は 半径 ÷ cos だけ横へ動く）
		const float cosAngle = 1.0f / std::hypot(1.0f, clampedSlope);
		const float radius = std::clamp(shape.cornerRadius.x * uniformScale, 0.0f, (std::min)(half.y, middleHalf * cosAngle));
		const float insetHalfY = half.y - radius;
		material.shapeHalf = {middleHalf - radius / cosAngle, insetHalfY};
		material.shapeParam = {insetHalfY * clampedSlope, radius};
		return clampedSlope;
	}
	}
	return 0.0f;
}

// --- 塗り方 ---
// 図形の輪郭からの距離で、どこまでを塗り・どこまでを図形全体にするかを決める（距離は内側がマイナス）
void WriteStroke(const VectorShapeComponent& shape, float uniformScale, VectorShapeMaterialData& material) {
	const float width = (std::max)(shape.strokeWidth * uniformScale, 0.0f);
	material.fillOpacity = (shape.style == VectorShapeStyle::Stroke) ? 0.0f : 1.0f;
	material.strokeUsesGradient = shape.strokeUsesGradient ? 1u : 0u;
	if (shape.style == VectorShapeStyle::Fill || width <= 0.0f) {
		material.strokeInner = kNoStrokeEdge;
		material.strokeOuter = 0.0f;
		return;
	}
	switch (shape.strokeAlign) {
	case VectorShapeStrokeAlign::Inside:
		material.strokeInner = -width;
		material.strokeOuter = 0.0f;
		break;
	case VectorShapeStrokeAlign::Center:
		material.strokeInner = -width * 0.5f;
		material.strokeOuter = width * 0.5f;
		break;
	case VectorShapeStrokeAlign::Outside:
		material.strokeInner = 0.0f;
		material.strokeOuter = width;
		break;
	}
}

// --- グラデーション ---
void WriteGradient(const VectorShapeComponent& shape, const Vector2& half, VectorShapeMaterialData& material) {
	material.gradientType = static_cast<uint32_t>(shape.gradient);
	material.gradientStart = shape.gradientRange.x;
	const float range = shape.gradientRange.y - shape.gradientRange.x;
	material.gradientScale = 1.0f / ((std::abs(range) > 1.0e-4f) ? range : 1.0e-4f); // 範囲が 0 でも、0 で割らずにパキッと分かれる
	switch (shape.gradient) {
	case VectorShapeGradient::None:
		break;
	case VectorShapeGradient::Linear: {
		// 図形の四角の、向きの方の端から端までを 0〜1 にする（dot(位置, 軸) + 0.5 で読む）
		const float angle = shape.gradientAngle * MathUtility::kDegToRad;
		const Vector2 direction = {std::cos(angle), std::sin(angle)};
		const float length = 2.0f * (std::abs(direction.x) * half.x + std::abs(direction.y) * half.y);
		material.gradientAxis = direction / (std::max)(length, 1.0e-4f);
		break;
	}
	case VectorShapeGradient::Radial:
		// 真ん中が 0、四角の辺の真ん中が 1（length(位置 × 軸) で読む）
		material.gradientAxis = {1.0f / (std::max)(half.x, 1.0e-4f), 1.0f / (std::max)(half.y, 1.0e-4f)};
		break;
	}
}

// --- ゲージ ---
// 直線：gaugeAxisA 方向に測って gaugeOffset より手前を見せる
void WriteLinearGauge(const VectorShapeComponent& shape, const Vector2& extent, float slope, float outerMargin, float amount, VectorShapeMaterialData& material) {
	const bool isHorizontal = (shape.gaugeDirection == VectorShapeGaugeDirection::LeftToRight) || (shape.gaugeDirection == VectorShapeGaugeDirection::RightToLeft);
	const bool isReversed = (shape.gaugeDirection == VectorShapeGaugeDirection::RightToLeft) || (shape.gaugeDirection == VectorShapeGaugeDirection::TopToBottom);
	// 横のゲージは、切り口も図形の傾きに合わせる（平行四辺形のゲージの先が、斜めの辺と平行になる）
	const Vector2 axis = isHorizontal ? Vector2{1.0f, -slope} : Vector2{0.0f, 1.0f};
	const float slopedExtent = isHorizontal ? extent.x - std::abs(extent.y * slope) : extent.y;
	const float range = slopedExtent + outerMargin;
	material.gaugeMode = 1;
	material.gaugeAxisA = isReversed ? axis * -1.0f : axis;
	material.gaugeOffset = -range + 2.0f * range * amount;
}

// 扇形：始まりの線と終わりの線の間を見せる（180 度より狭ければ2つの半平面の重なり、広ければ合わせた所）
void WriteRadialGauge(const VectorShapeComponent& shape, float amount, VectorShapeMaterialData& material) {
	const float sweep = std::clamp(shape.gaugeSweep * MathUtility::kDegToRad, 0.0f, MathUtility::kTwoPi) * amount;
	if (sweep >= MathUtility::kTwoPi) {
		return; // 1周なら全部見せる
	}
	const float start = shape.gaugeStartAngle * MathUtility::kDegToRad;
	const float end = start + (shape.gaugeClockwise ? sweep : -sweep);
	// 角度は真上が 0・時計回りが＋なので、向きは (sin, cos)
	const Vector2 startDirection = {std::sin(start), std::cos(start)};
	const Vector2 endDirection = {std::sin(end), std::cos(end)};
	// 線の法線は「見せない側」を向ける（シェーダーは 法線・位置 が負の所を見せる）
	const float side = shape.gaugeClockwise ? 1.0f : -1.0f;
	material.gaugeAxisA = RotateLeft(startDirection) * side;
	material.gaugeAxisB = RotateLeft(endDirection) * -side;
	material.gaugeMode = (sweep < MathUtility::kPi) ? 2u : 3u;
}

// Amount が 0 で何も見えないときは false
bool WriteGauge(const VectorShapeComponent& shape, const Vector2& half, float slope, VectorShapeMaterialData& material) {
	if (shape.gauge == VectorShapeGauge::None) {
		return true;
	}
	const float amount = std::clamp(shape.gaugeAmount, 0.0f, 1.0f);
	if (amount <= 0.0f) {
		return false;
	}
	switch (shape.gauge) {
	case VectorShapeGauge::None:
		break;
	case VectorShapeGauge::Linear:
		if (amount < 1.0f) {
			const float outerMargin = (std::max)(material.strokeOuter, 0.0f); // 外へ出た縁取りも含めて、端から端まで動かす
			WriteLinearGauge(shape, CalcShapeExtent(shape.shape, half), slope, outerMargin, amount, material);
		}
		break;
	case VectorShapeGauge::Radial:
		WriteRadialGauge(shape, amount, material);
		break;
	}
	return true;
}

// Component の値をシェーダーの値にする。値が変わっても、ここを毎フレーム通るだけで、作り直す物は無い。何も見えないときは false
bool MakeMaterial(const VectorShapeComponent& shape, const Vector2& scale, float groupAlpha, VectorShapeMaterialData& material) {
	if (shape.shape == VectorShapeType::Ring && shape.ringThickness <= 0.0f) {
		return false; // 太さ 0 のリングは何も見えない
	}
	const Vector2 half = {shape.size.x * scale.x * 0.5f, shape.size.y * scale.y * 0.5f};
	const float uniformScale = (std::min)(scale.x, scale.y); // 横だけ伸ばしても、丸み・太さがゆがまないように、小さい方の倍率を掛ける
	material.shape = static_cast<uint32_t>(shape.shape);
	material.blendMode = static_cast<uint32_t>(shape.blend);
	material.color = ClampColor(MultiplyAlpha(shape.color, groupAlpha));
	material.gradientColor = ClampColor(MultiplyAlpha(shape.gradientColor, groupAlpha));
	material.strokeColor = ClampColor(MultiplyAlpha(shape.strokeColor, groupAlpha));
	const float slope = WriteShapeParams(shape, half, uniformScale, material);
	WriteStroke(shape, uniformScale, material);
	WriteGradient(shape, half, material);
	return WriteGauge(shape, half, slope, material);
}

//=============================================================================
// 描く
//=============================================================================
void DrawShape(Handle<Entity> entity, const VectorShapeComponent& shape) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr || shape.size.x <= 0.0f || shape.size.y <= 0.0f) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
	const Vector2 scale = SpriteRenderSystem::CalcScreenScale(transform->worldMatrix);
	if (groupAlpha <= 0.0f || scale.x < kMinScale || scale.y < kMinScale) {
		return;
	}
	Renderer::VectorShapeConfig config;
	if (!MakeMaterial(shape, scale, groupAlpha, config.material)) {
		return;
	}

	// --- 板の4つの角：Size の四角を、縁取りと縁のなめらかな所が入るだけ広げる ---
	// margin は描かれるピクセルで決めるので、Transform の scale が掛かる前の長さ（ローカル）へ割り戻す
	const float margin = (std::max)(config.material.strokeOuter, 0.0f) + kEdgeMargin;
	const Vector2 localMargin = {margin / scale.x, margin / scale.y};
	const Vector2 center = {(0.5f - shape.pivot.x) * shape.size.x, (0.5f - shape.pivot.y) * shape.size.y};
	const Vector2 localHalf = {shape.size.x * 0.5f + localMargin.x, shape.size.y * 0.5f + localMargin.y};
	const auto toScreen = [&](float signX, float signY) {
		return SpriteRenderSystem::CalcScreenPoint(transform->worldMatrix, shape.anchor, {center.x + signX * localHalf.x, center.y + signY * localHalf.y});
	};
	config.lb = toScreen(-1.0f, -1.0f);
	config.lt = toScreen(-1.0f, +1.0f);
	config.rb = toScreen(+1.0f, -1.0f);
	config.rt = toScreen(+1.0f, +1.0f);
	// シェーダーへは、図形の真ん中を原点にした描かれるピクセルの場所を渡す（UV の代わり。板の中でなめらかにつながる）
	config.localHalf = {localHalf.x * scale.x, localHalf.y * scale.y};

	// --- 描く手順（ふつうは1回。マスクの図形なら、塗り → 形を書く → 子 → 形を消す → 縁取り）---
	const bool hasFill = shape.style != VectorShapeStyle::Stroke;
	const bool hasStroke = shape.style != VectorShapeStyle::Fill && shape.strokeWidth > 0.0f;
	const bool isHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	const UIShapeSteps plan = UIMaskSystem::PlanShapeSteps(entity, shape.order, UIDrawKind::VectorShape, hasFill, hasStroke);
	for (uint32_t i = 0; i < plan.count; ++i) {
		const UIDrawStep& step = plan.steps[i];
		step.ApplyTo(config);
		config.material.drawPart = static_cast<uint32_t>(step.part);
		// 形だけの手順は、色を出さない（Normal なら透明で、背景を変えない）。目で隠していても、子を切り抜くために描く
		config.blendMode = step.isGraphic ? shape.blend : BlendMode::Normal;
		config.material.blendMode = static_cast<uint32_t>(config.blendMode);
		config.isSceneHidden = isHidden && step.isGraphic;
		Renderer::DrawVectorShape(config);
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Vector Shape の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Image と同じ書き方）
// 今の設定で効かない項目は隠さず灰色にする（隠すと保存からも落ちて、別の形へ切り替えたときに値が消えるので）
void DescribeVectorShape(ComponentUI& ui, VectorShapeComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Shape###shape", value.shape, Tip("図形の種類"));
	ui.Field("Size###size", value.size, AtLeast(0.0f).Drag(1.0f).Tip("大きさ（ピクセル。ゲームの画面のつもりで）。円とリングは短い方が直径"));
	ui.Field("Anchor###anchor", value.anchor, Tip("画面のどこから位置を数えるか（位置は Transform の Translation の x・y。ピクセルで、上が＋）"));
	ui.Field("Pivot###pivot", value.pivot, Tip("図形のどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）"));
	ui.Field("Order###order", value.order, Tip("重ねる順（大きいほど手前。Image・Text の Order とも比べる）"));
	ui.Field("Blend###blend", value.blend, Tip("重ね方"));

	ui.Separator("Shape");
	const bool usesCorner = (value.shape == VectorShapeType::RoundedRectangle) || (value.shape == VectorShapeType::Parallelogram);
	ui.BeginDisabled(!usesCorner);
	ui.Field("Corner Radius###cornerRadius", value.cornerRadius, AtLeast(0.0f).Drag(0.5f).Tip("角の半径（ピクセル）。左上・右上・右下・左下の順。平行四辺形は左の値を全部の角に使う"));
	ui.EndDisabled();
	ui.BeginDisabled(value.shape != VectorShapeType::Ring);
	ui.Field("Ring Thickness###ringThickness", value.ringThickness, AtLeast(0.0f).Drag(0.5f).Tip("リングの太さ（ピクセル）"));
	ui.EndDisabled();
	ui.BeginDisabled(value.shape != VectorShapeType::Parallelogram);
	ui.Field("Skew Angle###skewAngle", value.skewAngle, Range(-kMaxSkewDegrees, kMaxSkewDegrees).Tip("傾き（度）。正で上の辺が右へずれる。図形は Size の四角の中に収まる"));
	ui.EndDisabled();

	ui.Separator("Paint");
	ui.Field("Style###style", value.style, Tip("塗りと縁取りのどちらを描くか"));
	ui.ColorField("Color###color", value.color);

	ui.Separator("Stroke");
	ui.BeginDisabled(value.style == VectorShapeStyle::Fill);
	ui.Field("Stroke Width###strokeWidth", value.strokeWidth, AtLeast(0.0f).Drag(0.25f).Tip("縁取りの太さ（ピクセル）。Size を変えても変わらない"));
	ui.Field("Stroke Align###strokeAlign", value.strokeAlign, Tip("輪郭のどちら側に付けるか（Inside：図形の大きさが変わらない）"));
	ui.ColorField("Stroke Color###strokeColor", value.strokeColor);
	ui.Field("Stroke Uses Gradient###strokeUsesGradient", value.strokeUsesGradient, Tip("縁取りにも塗りのグラデーションを使う（Stroke Color を使わない）"));
	ui.EndDisabled();

	ui.Separator("Gradient");
	ui.Field("Gradient###gradient", value.gradient, Tip("色の付け方"));
	ui.BeginDisabled(value.gradient == VectorShapeGradient::None);
	ui.ColorField("Gradient Color###gradientColor", value.gradientColor);
	ui.Field("Gradient Range###gradientRange", value.gradientRange, Tip("色が変わる範囲（図形の中の場所。0〜1）。x までは Color、y から先は Gradient Color"));
	ui.EndDisabled();
	ui.BeginDisabled(value.gradient != VectorShapeGradient::Linear);
	ui.Field("Gradient Angle###gradientAngle", value.gradientAngle, Drag(1.0f).Tip("直線の向き（度。0 で左から右、90 で下から上）"));
	ui.EndDisabled();

	ui.Separator("Gauge");
	ui.Field("Gauge###gauge", value.gauge, Tip("図形のどこまで見せるか（ゲージ）。縁取りも一緒に切る"));
	ui.BeginDisabled(value.gauge == VectorShapeGauge::None);
	ui.Field("Amount###gaugeAmount", value.gaugeAmount, Range(0.0f, 1.0f).Tip("見せる割合（0〜1）"));
	ui.EndDisabled();
	ui.BeginDisabled(value.gauge != VectorShapeGauge::Linear);
	ui.Field("Direction###gaugeDirection", value.gaugeDirection, Tip("直線のゲージが伸びる向き"));
	ui.EndDisabled();
	ui.BeginDisabled(value.gauge != VectorShapeGauge::Radial);
	ui.Field("Start Angle###gaugeStartAngle", value.gaugeStartAngle, Drag(1.0f).Tip("扇形の始まりの角度（度。0 で真上、時計回りが＋）"));
	ui.Field("Sweep###gaugeSweep", value.gaugeSweep, Range(0.0f, 360.0f).Tip("Amount が 1 のときの扇形の広さ（度。270 なら下が開いたメーターになる）"));
	ui.Field("Clockwise###gaugeClockwise", value.gaugeClockwise, Tip("時計回りに伸びる"));
	ui.EndDisabled();
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void VectorShapeSystem::Initialize() {
	// Vector Shape の置き場所・保存・Inspector を登録する。見せ方は DescribeVectorShape 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<VectorShapeComponent>();
	ComponentSerializer::Register<VectorShapeComponent>("Vector Shape", &DescribeVectorShape);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<VectorShapeComponent>>("Vector Shape", "UI", &DescribeVectorShape, "画像を使わずに計算で描く UI の図形（四角・角丸・円・リング）。塗り・縁取り・グラデーション・ゲージ"));
}

void VectorShapeSystem::Release() {}

//=============================================================================
// 描く（停止中も）
//=============================================================================
void VectorShapeSystem::Draw() {
	// 見つけた順に積む（重ねる順は drawKey で RenderQueue が並べる）
	EntityManager::ForEach<VectorShapeComponent>([](Handle<Entity> entity, const VectorShapeComponent& shape) {
		if (shape.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			DrawShape(entity, shape);
		}
	});
}

//=============================================================================
// ゲームから使う
//=============================================================================
bool VectorShapeSystem::IsMouseOver(Handle<Entity> entity) {
	const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (shape == nullptr || transform == nullptr || !shape->enabled || !EntityManager::IsActiveInHierarchy(entity) || !InputManager::IsMouseInGame()) {
		return false;
	}
	// UI World Follow が隠している物（描いていない）には当たらない
	if (UIWorldFollowSystem::IsHiddenInHierarchy(entity)) {
		return false;
	}
	const ScreenQuad quad = SpriteRenderSystem::CalcScreenQuad(transform->worldMatrix, shape->anchor, shape->pivot, shape->size);
	return SpriteRenderSystem::IsInsideQuad(InputManager::GetMousePositionInGame(), quad);
}