#pragma once
#include <optional>

#include "HTTEngine/Components/Camera/CameraOrbitComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;


/// <summary>
/// Camera Orbitの中身（Camera Systemから使う）
/// </summary>
namespace CameraOrbit {
// ===== 今の回る中心・カメラを置く位置などを、ワールドの点で表したもの（エディタの線に使う。CalcGuideで作る）=====
struct Guide {
	Vector3 target; // 追う物の位置
	Vector3 pivot; // 回る中心
	Vector3 camera; // カメラを置く位置（停止中は、再生したら置かれる位置）
	float ringRadius = 0.0f;          // 回る輪の半径（m。カメラの高さでの、回る中心からの横の長さ）
	float ringHeight = 0.0f;          // 回る輪の高さ（ワールドの Y）
	std::optional<Vector3> lookPoint; // Heading が LookAt のとき、Camera Look At が見る点（見る点・回る中心・カメラが上から見て一直線）
};

// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraOrbitComponent& value);

// カメラ1つ分の回り込み。Transform を動かしたら true
bool Update(Handle<Entity> entity, CameraOrbitComponent& orbit, float deltaTime);

// 今の位置（停止中は、再生したら置かれる位置）。追う物が居なければ無し。回り込みと同じ計算で作る
std::optional<Guide> CalcGuide(Handle<Entity> entity, const CameraOrbitComponent& orbit);
} // namespace CameraOrbit