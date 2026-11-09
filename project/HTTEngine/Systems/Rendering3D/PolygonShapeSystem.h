#pragma once
#include "HTTEngine/Components/Rendering3D/PolygonRendererComponent.h"
#include "HTTEngine/Components/Rendering3D/PolygonShapeComponent.h"
#include "HTTEngine/Math/Geometry/Polygon.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;
class IBLEnvironment;


/// <summary>
/// Polygon Shape（多角形を押し出した形）と Polygon Renderer（その見た目）を登録し、描く
/// <para>形から作った三角形（メッシュ）は覚えておき、形・置き場所が変わった時だけ作り直す（耳切り法は点の数の3乗くらいかかるので）</para>
/// <para>当たりは CollisionSystem（Collider の Shape を Polygon にする）が CalcWorldPolygon で作る</para>
/// <para>Scene ビューでの形の編集は PolygonShapeGizmo（エディタ）。編集中かどうかは、Inspector のボタンから変えるので、ここに置く</para>
/// </summary>
class PolygonShapeSystem {
public:
	// PolygonShapeComponent・PolygonRendererComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 描画リクエストを積む（WindowManager::DrawAll から。停止中も描く）
	/// </summary>
	/// <param name="environment">環境光（PBR に要る。nullptr なら PBR の形は Half Lambert で描かれる）</param>
	static void Draw(Camera* camera, IBLEnvironment* environment);

	/// <summary>
	/// 形をワールド座標の「押し出した多角形」にする（当たり判定用）
	/// <para>点に offset（Collider の Center）を足してから、ワールド行列を掛ける。奥行きには Z の大きさを掛ける</para>
	/// </summary>
	static ExtrudedPolygon CalcWorldPolygon(const PolygonShapeComponent& shape, const Vector3& offset, const Matrix4x4& worldMatrix);

	// ===== 形の編集（エディタ）=====
	// Scene ビューで、選んでいる Entity の点を動かしているか（Inspector の Edit Shape で切り替える）
	static bool IsEditing();
	static void SetEditing(bool isEditing);
};