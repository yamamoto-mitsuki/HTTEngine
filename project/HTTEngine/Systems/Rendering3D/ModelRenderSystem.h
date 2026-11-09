#pragma once
#include <string>

#include "HTTEngine/Core/Handle.h"

// 前方宣言
struct Entity;
class Camera;
class IBLEnvironment;


/// <summary>
/// ModelRendererComponent を持つEntityを描くシステム（カテゴリ：Rendering3D）
/// </summary>
class ModelRenderSystem {
public:
	// root自身と子孫を描く。camera / environment は保持しない。
	// windowTitleにはIScene::GetWindowTitle()を渡す。
	static void Draw(Handle<Entity> root, Camera* camera, IBLEnvironment* environment, const std::wstring& windowTitle);
};