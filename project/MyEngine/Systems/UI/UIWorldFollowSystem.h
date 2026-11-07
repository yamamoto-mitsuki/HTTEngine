#pragma once
#include "MyEngine/Components/UI/UIWorldFollowComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"

// 前方宣言
class Camera;

/// <summary>
/// UI World Follow（UI をワールドの物・点に重ね続ける）を動かす
/// <para>Update はカメラの後（EngineSystems::Update の CameraSystem::Update の後）。カメラを動かした後の絵で写すので、カメラを追いかけても揺れない</para>
/// <para>置いた所は、EntityManager::UpdateTransforms がワールド行列を作るたびに入れる（AddTransformAdjuster）。同じフレームの後で UpdateTransforms が呼ばれても、置いた所が消えない</para>
/// </summary>
class UIWorldFollowSystem {
public:
	// Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 重ねる点を camera で画面に写して、置く所を決め、ワールド行列を計算し直す（停止中も動く）
	/// </summary>
	/// <param name="camera">ゲームのメインのカメラ（CameraSystem::Update で、このフレームの位置・向きにした物）。nullptr なら何もしない</param>
	static void Update(const Camera* camera);

	// その Entity か親のどれかが、UI World Follow に隠されているか（描かない・マウスを当てない）
	static bool IsHiddenInHierarchy(Handle<Entity> entity);
};
