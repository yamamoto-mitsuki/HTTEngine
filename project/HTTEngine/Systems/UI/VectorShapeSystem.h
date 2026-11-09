#pragma once
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"


/// <summary>
/// Vector Shape（画像を使わない UI の図形）を描く
/// <para>停止中も描く（置いた場所をエディタで見られるように）。Scene ビューにも Image と同じ位置に重なる</para>
/// <para>図形の形はシェーダーが1ピクセルずつ計算する（VectorShape.PS）。ここは Component の値を、シェーダーの単位（画面のピクセル）に直して渡すだけ</para>
/// </summary>
class VectorShapeSystem {
public:
	// Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 描画リクエストを積む（EngineSystems から。Image の後、Text の前）
	static void Draw();

	// ===== ゲームから使う =====

	/// <summary>
	/// マウスがその図形の Size の四角の上にあるか（ボタンに使う。円でも角の所は上と数える）
	/// <para>図形が無い・無効・マウスがゲーム画面の外なら false</para>
	/// </summary>
	static bool IsMouseOver(Handle<Entity> entity);
};