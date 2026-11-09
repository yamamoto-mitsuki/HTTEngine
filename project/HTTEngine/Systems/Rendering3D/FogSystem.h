#pragma once
#include "HTTEngine/Components/Rendering3D/FogComponent.h"

/// <summary>
/// Fog（霧）を登録し、シーンの Fog Component の値を、毎フレーム SceneRenderer へ渡す
/// <para>色に混ぜるのはシェーダー（Fog.hlsli の ApplyFog）。値はカメラの定数バッファ（b0）に入る</para>
/// </summary>
class FogSystem {
public:
	// FogComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 有効な Fog Component を探して、SceneRenderer::SetFog で渡す（無ければ霧なし）。停止中も呼ぶ（エディタで値を変えて、すぐ見られるように）
	/// </summary>
	static void Update();
};