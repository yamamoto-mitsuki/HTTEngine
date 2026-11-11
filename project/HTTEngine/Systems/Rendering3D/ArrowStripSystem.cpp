#include "ArrowStripSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <vector>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Rendering3D/PrimitiveRenderSystem.h"
#include "HTTEngine/Time/EngineTime.h"

namespace {
constexpr int kMaxArrows = 64;         // 1本の線に並べる矢印の数の上限（1つずつ描くので、多すぎないように）
constexpr float kMinPointCount = 2.0f; // 線の点の数の下限（まっすぐな線）
constexpr float kMinSpacing = 0.01f;   // 矢印の間の下限（m。0 で割らないように）

/// <summary>
/// 矢印1つの置き方
/// </summary>
struct ArrowPlacement {
	PathCurve::Sample sample; // 矢印の真ん中の位置と、線の向き（Entity から見た座標）
	float scale = 1.0f;       // 大きさの倍率（Opaque の Fade で小さくする）
};

/// <summary>
/// ArrowStripSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct ArrowStripState {
	float time = 0.0f;                  // 流れる時間（秒。何も掛けていない時間を足していく）
	PathCurve path;                     // 描いている線（Entity から見た座標。Entity ごとに作り直して使い回す）
	std::vector<Vertex3dData> vertices; // 矢印1つ分の頂点（使い回す）
	std::vector<uint32_t> indices;      // 矢印1つ分の番号（いつも同じ）
	bool isEditing = false;             // Scene ビューで線を編集しているか
};
std::unique_ptr<ArrowStripState> state;

// x を 0〜length の間に折り返す（length を超えたら 0 から、マイナスなら length の方から）
// std::fmod はマイナスの数だとマイナスの余りを返すので、floor（小さい方の整数）で割った回数を引く
float Repeat(float x, float length) { return x - std::floor(x / length) * length; }

// 並べた中の位置 t（0〜1）での濃さ。両端の fade の長さで 0 まで下げる（真ん中は 1）
float CalcFade(float t, float fade) {
	if (fade <= 0.0f) {
		return 1.0f;
	}
	const float edge = (std::min)(t, 1.0f - t); // 近い方の端までの割合
	return std::clamp(edge / fade, 0.0f, 1.0f);
}

//=============================================================================
// 端で消す（透け方ごとに、消し方を変える）
//=============================================================================
// Opaque（不透明）は透けない（A を使わない）ので、色を暗くすると黒い矢印になる → 小さくして消す
// Transparent は A だけ、Additive は RGB だけを下げる（両方に掛けると、濃さの2乗で消えて、端の手前で急に薄くなる）
void ApplyFade(SurfaceType surface, float fade, Vector4* color, float* scale) {
	switch (surface) {
	case SurfaceType::Opaque:
		*scale = fade;
		break;
	case SurfaceType::Transparent:
		color->w *= fade;
		break;
	case SurfaceType::Additive:
		color->x *= fade;
		color->y *= fade;
		color->z *= fade;
		break;
	}
}

//=============================================================================
// 矢印1つ分の形
//=============================================================================
//
//   F────E                  「>」を、太さ thickness の棒2本（平行四辺形2枚）で作る
//    ╲    ╲                   下の棒：A B C D　上の棒：D C E F
//     ╲    ╲                  先（C）は線の進む向き（tangent）
//      D────C   ← 先          矢印の真ん中（A と C の真ん中の X）を、線の上の位置に置く
//     ╱    ╱
//    ╱    ╱                  矢印の中の向き：along ＝ 線の向き、across ＝ 線の向きを左へ 90 度回した向き
//   A────B                   頂点は「線の位置 ＋ along × u ＋ across × v」を、ワールド座標にして渡す（Renderer::DrawMesh）
//                            UV は矢印1つの四角に、絵を1枚
//
void BuildArrow(const ArrowStripComponent& strip, const ArrowPlacement& placement, const Matrix4x4& world, const Vector3& normal) {
	const float width = (std::max)(strip.size.x, 0.0f) * placement.scale;
	const float halfHeight = (std::max)(strip.size.y, 0.0f) * 0.5f * placement.scale;
	const float thickness = std::clamp(strip.thickness * placement.scale, 0.0f, width); // 長さより太いと、形が裏返る
	const float left = -width * 0.5f;                                                   // 矢印の左端（真ん中から見た u）
	// --- 矢印の中の6点（u：線の向き、v：横）---
	const std::array<Vector2, 6> local = {
	    Vector2{left,                     -halfHeight}, // A
	    Vector2{left + thickness,         -halfHeight}, // B
	    Vector2{left + width,             0.0f       }, // C 先
	    Vector2{left + width - thickness, 0.0f       }, // D 内側のくぼみ
	    Vector2{left + thickness,         halfHeight }, // E
	    Vector2{left,                     halfHeight }, // F
	};
	const Vector3& along = placement.sample.tangent;
	const Vector3 across = {-along.y, along.x, 0.0f}; // X・Y の面で、左へ 90 度回した向き
	state->vertices.clear();
	for (const Vector2& point : local) {
		const Vector3 position = TransformPoint(placement.sample.position + along * point.x + across * point.y, world);
		// UV：矢印1つの四角（左端〜先、下〜上）を 0〜1 に。V は下向きなので上を 0 にする
		const Vector2 uv = {(width > 0.0f) ? (point.x - left) / width : 0.0f, (halfHeight > 0.0f) ? (halfHeight - point.y) / (halfHeight * 2.0f) : 0.0f};
		state->vertices.push_back({
		    {position.x, position.y, position.z, 1.0f},
            uv, normal
        });
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeArrowStrip(ComponentUI& ui, ArrowStripComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	// ===== 線の編集（Inspector だけ。保存・読み込みのときは Button がいつも false）=====
	if (ui.Button(state->isEditing ? "Finish Editing###editPath" : "Edit Path###editPath")) {
		state->isEditing = !state->isEditing;
	}
	ui.Label("Scene ビューで ■ をドラッグ・＋ で足す・Delete で消す。停止中も流れる");
	// ===== 並べ方 =====
	ui.Field("Start Offset###startOffset", value.startOffset, AtLeast(0.0f).Tip("線の始まりから、矢印を並べ始めるまでの長さ（m。線に沿って測る）。円の外から並べたいときなど"));
	ui.Field("Spacing###spacing", value.spacing, AtLeast(kMinSpacing).Tip("矢印の間（m。線に沿って測る）。並べる長さがちょうど割り切れるように、一番近い間に直して並べる"));
	ui.Field("Size###size", value.size, AtLeast(0.0f).Tip("矢印1つの大きさ（X：先までの長さ、Y：高さ。m）"));
	ui.Field("Thickness###thickness", value.thickness, AtLeast(0.0f).Tip("矢印の線の太さ（X の向きで測る。m）。Size の X より太くはならない"));
	ui.Field("Scroll Speed###scrollSpeed", value.scrollSpeed, Tip("流れる速さ（m/秒）。0 で止まる、マイナスで逆へ流れる。端から出た矢印は、反対の端から出てくる"));
	ui.Field("Fade###fade", value.fade, Range(0.0f, 0.5f).Tip("両端で消していく長さ（並べた長さに対する割合）。0 なら消さない。Opaque は小さくして、Transparent・Additive は薄くして消す"));
	// ===== 線の点（先に数を読むので、シーンファイルから読むときも、その数だけ点を読む）=====
	ui.Separator("Path");
	const int before = value.pointCount;
	ui.Field(
	    "Point Count###pointCount", value.pointCount,
	    Range(kMinPointCount, static_cast<float>(kMaxArrowPathPoints)).Tip("線の点の数。Scene ビューで足す・消すこともできる。点は Entity から見た X・Y（m）。線は点を順に、なめらかに通る"));
	value.pointCount = std::clamp(value.pointCount, static_cast<int>(kMinPointCount), static_cast<int>(kMaxArrowPathPoints));
	// 増やした点は、最後の点の先（最後の区間と同じ向き・長さ）に置く（前に使っていた所や原点に飛んでいかないように）
	for (int i = (std::max)(before, 2); i < value.pointCount; ++i) {
		value.points[i] = value.points[i - 1] * 2.0f - value.points[i - 2];
	}
	for (int i = 0; i < value.pointCount; ++i) {
		ui.Field(std::format("Point {}###point{}", i, i).c_str(), value.points[i]); // 保存の名前は point0, point1, …
	}
	ui.Separator("Material");
	PrimitiveRenderSystem::DescribeMaterial(ui, value.material, "");
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void ArrowStripSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<ArrowStripState>();
	// 番号は矢印の形が同じなので、最初に1回だけ作る（下の棒 A B C・A C D、上の棒 D C E・D E F）
	state->indices = {0, 1, 2, 0, 2, 3, 3, 2, 4, 3, 4, 5};

	// Arrow Strip の置き場所・保存・Inspector を登録する（見せ方は DescribeArrowStrip 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<ArrowStripComponent>();
	ComponentSerializer::Register<ArrowStripComponent>("Arrow Strip", &DescribeArrowStrip);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ArrowStripComponent>>("Arrow Strip", "Rendering3D", &DescribeArrowStrip, "「> > >」のような矢印を、線に沿って並べて流す（進む向き・行き先を見せる）"));
}

void ArrowStripSystem::Release() { state.reset(); }

//=============================================================================
// 線
//=============================================================================
void ArrowStripSystem::BuildPath(const ArrowStripComponent& strip, const Matrix4x4& worldMatrix, PathCurve& path) {
	// 点（X・Y）を Z が 0 の面に置いて、行列を掛ける（決まった大きさの配列なので、new しない）
	std::array<Vector3, kMaxArrowPathPoints> points{};
	const std::span<const Vector2> source = strip.GetPoints();
	for (size_t i = 0; i < source.size(); ++i) {
		points[i] = TransformPoint({source[i].x, source[i].y, 0.0f}, worldMatrix);
	}
	path.Build({points.data(), source.size()});
}

//=============================================================================
// 描画
//=============================================================================
void ArrowStripSystem::Draw(Camera* camera, IBLEnvironment* environment) {
	HTT_ASSERT_MSG(state != nullptr, "ArrowStripSystem::Initialize()を先に呼んでください");
	// 流れる時間：何も掛けていない時間（停止中のエディタでも流して、Inspector で変えた値をすぐ見られるように）
	state->time += Time::GetUnscaleDeltaTime();

	EntityManager::ForEach<ArrowStripComponent>([camera, environment](Handle<Entity> entity, const ArrowStripComponent& strip) {
		if (!strip.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		// --- 線は Entity から見た座標で作る（矢印の形も Entity から見た座標で作って、最後にワールド行列を掛ける）---
		BuildPath(strip, MakeIdentity4x4(), state->path);
		const float startOffset = (std::max)(strip.startOffset, 0.0f);
		const float length = state->path.GetLength() - startOffset; // 矢印を並べる長さ
		if (state->path.IsEmpty() || length <= 0.0f) {
			return;
		}
		// --- 数と間：Spacing に一番近い、ちょうど割り切れる間にする（端から出た矢印が反対の端から出てくるとき、間がずれないように）---
		const int count = std::clamp(static_cast<int>(std::round(length / (std::max)(strip.spacing, kMinSpacing))), 1, kMaxArrows);
		const float step = length / static_cast<float>(count);

		const Matrix4x4& world = transform->worldMatrix;
		// 法線はカメラの方（-Z）。Z の大きさが 0 で向きが決まらなければ、そのまま -Z
		Vector3 normal = Normalize(TransformNormal({0.0f, 0.0f, -1.0f}, world));
		if (LengthSq(normal) == 0.0f) {
			normal = {0.0f, 0.0f, -1.0f};
		}
		Renderer::MeshConfig config;
		PrimitiveRenderSystem::ApplyMaterial(config, strip.material, environment);
		config.camera = camera;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない

		// --- 矢印を1つずつ：並べた中の位置を時間で進めて、並べる長さで折り返す。両端は消していく ---
		for (int i = 0; i < count; ++i) {
			// (i + 0.5) × step：止めたとき、両端から半分ずつ空けて並ぶ（端の矢印が Fade で消えないように）
			const float position = Repeat((static_cast<float>(i) + 0.5f) * step + state->time * strip.scrollSpeed, length);
			const float fade = CalcFade(position / length, strip.fade);
			if (fade <= 0.0f) {
				continue;
			}
			Vector4 color = strip.material.color;
			ArrowPlacement placement;
			ApplyFade(strip.material.surface, fade, &color, &placement.scale);
			config.color = ApplyIntensity(color, strip.material.intensity);
			// 矢印の真ん中を、線の上の position の所に、線の向きで置く
			placement.sample = state->path.Evaluate(startOffset + position);
			BuildArrow(strip, placement, world, normal);
			Renderer::DrawMesh(config, state->vertices, state->indices);
		}
	});
}

//=============================================================================
// 線の編集
//=============================================================================
bool ArrowStripSystem::IsEditing() { return state != nullptr && state->isEditing; }

void ArrowStripSystem::SetEditing(bool isEditing) {
	HTT_ASSERT_MSG(state != nullptr, "ArrowStripSystem::Initialize()を先に呼んでください");
	state->isEditing = isEditing;
}