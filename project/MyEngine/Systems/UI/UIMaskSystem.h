#pragma once
#include <array>
#include <cstdint>

#include "MyEngine/Components/UI/UIMaskComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Graphics/Pipeline/RenderStates.h"

/// <summary>
/// 画面に重ねる UI の種類（同じ Order のときに重ねる順。UIDrawKeyPart::kind に入れる）
/// </summary>
enum class UIDrawKind : uint32_t {
	Image,       // Image（一番奥）
	VectorShape, // Vector Shape
	Polyline,    // Polyline
	Text,        // Text（一番手前）
};

/// <summary>
/// UI を1回描く分の、重ねる順・ステンシル・図形のどこを描くか
/// </summary>
struct UIDrawStep {
	UIDrawKey key;                           // 重ねる順
	StencilMode stencil = StencilMode::None; // ステンシルの使い方
	uint32_t stencilRef = 0;                 // ステンシルの参照値（外にあるマスクの数）
	UIDrawPart part = UIDrawPart::All;       // 図形のどこを描くか（Image・Text は All のまま）
	bool isGraphic = true;                   // 色を出す手順か（false はステンシルだけ。Hierarchy の目で隠しても、子を切り抜くために描く）

	// Config の重ねる順とステンシルに写す
	void ApplyTo(Draw2dTarget& target) const {
		target.drawKey = key;
		target.stencil = stencil;
		target.stencilRef = stencilRef;
	}
};

/// <summary>
/// 図形（Vector Shape・Polyline）を描く手順（マスクの図形なら最大 4 回、そうでなければ 1 回）
/// </summary>
struct UIShapeSteps {
	std::array<UIDrawStep, 4> steps{}; // 手順（積む順）
	uint32_t count = 0;                // 使っている数
};


/// <summary>
/// UI Mask（子の UI を図形の形で切り抜く）と、画面に重ねる UI の描く順を決める
/// <para>マスクの図形は、塗り → 形をステンシルに書く（1 増やす）→ 子 → 形を消す（1 減らす）→ 縁取り の順に描く</para>
/// <para>子は「ステンシルの値が、外にあるマスクの数と同じ所」だけに描く。入れ子なら内のマスクは外のマスクの中だけを増やすので、重なった所だけになる</para>
/// <para>描く順は、外のマスクの Order から順に比べるキー（UIDrawKey）で決める。Image・Vector Shape・Polyline・Text の System がここで聞いてから積む</para>
/// </summary>
class UIMaskSystem {
public:
	// UI Mask を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// UI の1つの Component の描き方（マスクの中ならステンシルで切り抜く）
	/// <para>order・kind はその Component の Order と種類。切り抜きに使う図形なら、キーは図形の「始まり」の位置（UI ギズモのクリックで選ぶ順に使う）</para>
	/// </summary>
	static UIDrawStep MakeStep(Handle<Entity> entity, int order, UIDrawKind kind);

	/// <summary>
	/// 図形（Vector Shape・Polyline）を描く手順。マスクの図形なら、塗り・形を書く・形を消す・縁取りの最大 4 回
	/// </summary>
	/// <param name="hasFill">塗りがあるか（無ければ塗りの手順を作らない）</param>
	/// <param name="hasStroke">縁取り・線があるか（無ければ縁取りの手順を作らない）</param>
	static UIShapeSteps PlanShapeSteps(Handle<Entity> entity, int order, UIDrawKind kind, bool hasFill, bool hasStroke);
};