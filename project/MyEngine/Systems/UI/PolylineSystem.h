#pragma once
#include "MyEngine/Components/UI/PolylineComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Vector2.h"


/// <summary>
/// Polyline（点をつないだ UI の線・形）を描く
/// <para>停止中も描く（置いた場所をエディタで見られるように）。Scene ビューにも Image と同じ位置に重なる</para>
/// <para>メッシュ（直線の部分・角の部分・端）は Entity ごとに覚えておき、点・半径・太さ・端の形・Scale が変わった時だけ作り直す。
/// 位置・回転・色・透明度は定数で渡すので、変えても作り直さない</para>
/// </summary>
class PolylineSystem {
public:
	// Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 描画リクエストを積む（EngineSystems から。Vector Shape の後、Text の前）
	static void Draw();

	// ===== エディタから使う =====

	// Scene ビューで、選んでいる Entity の点を動かしているか（Inspector の Edit Points で切り替える）
	static bool IsEditing();
	static void SetEditing(bool isEditing);

	/// <summary>
	/// 点を囲む四角（Entity から見たピクセル。線の太さの半分も含める。Scale は掛けない）
	/// <para>UI ギズモの枠とクリックで選ぶのに使う。点が無ければ false</para>
	/// </summary>
	static bool CalcLocalBounds(const PolylineComponent& polyline, Vector2& min, Vector2& max);
};
