#pragma once
#include "HTTEngine/Components/Lighting/EnvironmentLightComponent.h"

/// <summary>
/// Environment Light（環境光）を登録し、シーンの Environment Light Component の値を、毎フレーム SceneRenderer へ渡す
/// <para>光の強さはカメラの定数バッファ（b0）に入り、PBR.PS が IBL に掛ける。空の絵の明るさは Skybox へ渡す</para>
/// </summary>
class EnvironmentLightSystem {
public:
	// EnvironmentLightComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 有効な Environment Light Component を探して、SceneRenderer へ渡す（無ければ全部 1）。停止中も呼ぶ（エディタで値を変えて、すぐ見られるように）
	/// </summary>
	static void Update();
};
