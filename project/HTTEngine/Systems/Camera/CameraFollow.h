#pragma once
#include "HTTEngine/Components/Camera/CameraFollowComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Follow の中身（CameraSystem から使う）
/// </summary>
namespace CameraFollow {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraFollowComponent& value);

// カメラ1つ分の追いかけ。Transform を動かしたら true
bool Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime);
} // namespace CameraFollow