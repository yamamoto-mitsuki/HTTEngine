#pragma once
#include <array>
#include <optional>

#include "HTTEngine/Components/Camera/CameraFollowComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Follow の中身（CameraSystem から使う）
/// </summary>
namespace CameraFollow {
/// <summary>
/// 今の Dead Zone・Soft Zone などを、ワールドの点で表した物（エディタの線に使う。CalcGuide で作る）
/// <para>四角は左下・右下・右上・左上の順。focus を通る、Plane の面の上に置く（Screen で面が画面の外へ伸びるときは、カメラの正面の面）</para>
/// <para>どの面に置いても、このカメラから見ると画面の上の範囲にぴったり重なる（Game ビューでは、範囲が画面の四角に見える）</para>
/// </summary>
struct Guide {
	std::array<Vector3, 4> deadZone; // focus がこの中なら、カメラは動かない
	std::array<Vector3, 4> softZone; // focus はこの外へ出ない
	Vector3 target;                  // 追う物の位置
	Vector3 focus;                   // 画面の真ん中に置きたい点（追う物 ＋ Offset ＋ 先読み）
	Vector3 horizontal;              // focus の印を描く向き（面の横。長さ1）
	Vector3 vertical;                // focus の印を描く向き（面の縦。長さ1）
	float markSize = 0.0f;           // focus の印の大きさ（m。カメラからの距離に合わせて、画面の上でいつも同じくらいに見える）
	std::optional<std::array<Vector3, 4>> bounds; // Use Bounds の範囲（XY・XZ で Use Bounds のときだけ）
};

// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraFollowComponent& value);

// カメラ1つ分の追いかけ。Transform を動かしたら true
bool Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime);

// 今のカメラの位置での範囲（追う物が居ない・focus がカメラの後ろなら無し）。追いかけと同じ計算で作る
std::optional<Guide> CalcGuide(Handle<Entity> entity, const CameraFollowComponent& follow);
} // namespace CameraFollow
