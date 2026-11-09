#pragma once
#include "HTTEngine/Components/Rendering3D/ArrowStripComponent.h"
#include "HTTEngine/Math/Geometry/PathCurve.h"
#include "HTTEngine/Math/Matrix4x4.h"

// 前方宣言
class Camera;
class IBLEnvironment;

/// <summary>
/// Arrow Strip（向きを示す矢印の列）を登録し、描く
/// <para>線は点を順に通る曲線（PathCurve）。矢印は「>」の形を、太さのある2本の棒（平行四辺形2枚）で作り、線に沿って並べて、時間で流す</para>
/// <para>流れる時間は、何も掛けていない時間（一時停止・スローでも、停止中のエディタでも流れる）</para>
/// <para>Scene ビューでの線の編集は ArrowStripGizmo（エディタ）。編集中かどうかは、Inspector のボタンから変えるので、ここに置く（Polygon Shape と同じ）</para>
/// </summary>
class ArrowStripSystem {
public:
	// ArrowStripComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 描画リクエストを積む（WindowManager::DrawAll から。停止中も描く）
	/// </summary>
	/// <param name="environment">環境光（PBR に要る。nullptr なら PBR の形は Half Lambert で描かれる）</param>
	static void Draw(Camera* camera, IBLEnvironment* environment);

	/// <summary>
	/// 線を作る（点にワールド行列を掛けてから曲線にする。worldMatrix に単位行列を渡すと、Entity から見た位置のまま）
	/// <para>緑の円が、キューブを線に沿って飛ばすのに使う。点が 2 つより少なければ、空の線になる</para>
	/// </summary>
	static void BuildPath(const ArrowStripComponent& strip, const Matrix4x4& worldMatrix, PathCurve& path);

	// ===== 線の編集（エディタ）=====
	// Scene ビューで、選んでいる Entity の線の点を動かしているか（Inspector の Edit Path で切り替える）
	static bool IsEditing();
	static void SetEditing(bool isEditing);
};