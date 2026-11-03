#pragma once
#include <cstdint>

#include "MyEngine/Components/UI/ImageComponent.h"
#include "MyEngine/Components/Rendering2D/SpriteRendererComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Matrix4x4.h"
#include "MyEngine/Math/Vector2.h"
#include "MyEngine/Math/Geometry/AABB.h"

// 前方宣言
class Camera;

/// <summary>
/// 画面の上の四角の4つの角（正規化座標：画面の真ん中が0、右と上が＋、端が±1）。Image・Text を置いた形
/// </summary>
struct ScreenQuad {
	Vector2 lb; // 左下
	Vector2 lt; // 左上
	Vector2 rb; // 右下
	Vector2 rt; // 右上
};


/// <summary>
/// Sprite Renderer（3D空間に置く画像の板）と Image（画面に重ねる絵）を描く
/// <para>どちらも停止中も描く（置いた場所をエディタで見られるように）。Image は Scene ビューにも同じ位置に重なる</para>
/// <para>一覧などの中身は SpriteRenderSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class SpriteRenderSystem {
public:
	// 2つの Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 描画リクエストを積む（WindowManager::DrawAll から）。camera は半透明の板を奥から並べるのに使う
	static void Draw(Camera* camera);

	// ===== ゲームから使う =====

	/// <summary>
	/// マウスがその Image の上にあるか（ボタンに使う）
	/// <para>Image が無い・無効・マウスがゲーム画面の外なら false。回した・Fill で縮めた Image も、描いた形のとおりに調べる</para>
	/// </summary>
	static bool IsMouseOver(Handle<Entity> entity);

	// 画像の1コマの大きさ（ピクセル）。columns・rows は横・縦のコマの数（1 より小さければ 1）
	static Vector2 CalcFrameSize(uint32_t textureHandle, int columns, int rows);

	/// <summary>
	/// 画面に重ねる物（Image・Text）の点を、画面の正規化座標にする（真ん中が0、右と上が＋、端が±1）
	/// <para>local は Entity の位置を原点にしたピクセル（Y は上が＋）。Transform（回転 z・大きさ・親子）を掛けてから、アンカーの場所を足す</para>
	/// </summary>
	static Vector2 CalcScreenPoint(const Matrix4x4& worldMatrix, ScreenAnchor anchor, const Vector2& local);

	/// <summary>
	/// 画面に重ねる四角（Image の絵・Text の文字のかたまり）の4つの角
	/// <para>size はピクセル、pivot は四角のどこを Entity の位置に合わせるか（0,0=左下 1,1=右上）。fill は右の端を縮める割合（Image の Fill。1 なら縮めない）</para>
	/// </summary>
	static ScreenQuad CalcScreenQuad(const Matrix4x4& worldMatrix, ScreenAnchor anchor, const Vector2& pivot, const Vector2& size, float fill = 1.0f);

	// 画面に重ねる物の、横・縦の大きさの倍率（ワールド行列の x 軸・y 軸の長さ。Transform の scale に親の scale も掛かった値。回っていても測れる）
	static Vector2 CalcScreenScale(const Matrix4x4& worldMatrix);

	// 点が四角の中か（point も正規化座標。4つの辺をぐるっと回って、どの辺から見ても同じ側なら中。回っていても・裏返っていても使える）
	static bool IsInsideQuad(const Vector2& point, const ScreenQuad& quad);

	/// <summary>
	/// 自分と親を全部たどって、有効な Alpha Group の Alpha を掛けた値（0〜1。どこにも無ければ 1）
	/// <para>Image・Text・Sprite Renderer が、描く色の a に掛ける（親の Alpha Group を動かすと、子がまとめてフェードする）</para>
	/// </summary>
	static float CalcGroupAlpha(Handle<Entity> entity);


	// ===== エディタから使う =====

	/// <summary>
	/// Sprite Renderer の板の形（ローカルの XY 平面の四角 localBox。z は 0）と、それをワールドへ置く行列
	/// <para>ビルボードの板は、camera の方へ向けた行列にする（描くときにシェーダーがするのと同じ計算）。camera が nullptr なら向けない</para>
	/// <para>Scene ビューのクリックで選ぶ・選んだ物の枠を描くのに使う。描いていない物（無効・Sprite Renderer が無い）は false</para>
	/// </summary>
	static bool CalcSpritePlacement(Handle<Entity> entity, const Camera* camera, AABB& localBox, Matrix4x4& matrix);
};