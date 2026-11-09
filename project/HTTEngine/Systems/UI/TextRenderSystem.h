#pragma once
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"


/// <summary>
/// Text（画面に重ねる文字）を描く
/// <para>文字を UTF-8 から1文字ずつに分け、FontManager から形（アトラスの場所・幅）をもらって左から並べ、行ごとにそろえる。
/// 置き方（Anchor・Pivot・Transform）は Image と同じ（SpriteRenderSystem::CalcScreenPoint）</para>
/// <para>停止中も描く（置いた場所をエディタで見られるように）。Image と同じく Scene ビューにも重なる</para>
/// </summary>
class TextRenderSystem {
public:
	// Text Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 描画リクエストを積む（WindowManager::DrawAll から）
	static void Draw();

	// ===== ゲームから使う =====

	/// <summary>
	/// 文字のかたまりの大きさ（ピクセル。Transform の大きさは掛けない）。背景の Image の大きさを文字に合わせるのに使う
	/// <para>Text が無い・フォントが無ければ 0,0</para>
	/// </summary>
	static Vector2 CalcTextSize(Handle<Entity> entity);
};