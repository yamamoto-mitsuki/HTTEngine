#pragma once
#include "HTTEngine/Components/Rendering3D/DepthOfFieldComponent.h"

/// <summary>
/// Depth Of Field（被写界深度）を登録し、シーンの Depth Of Field Component の値を、毎フレーム SceneRenderer へ渡す
/// <para>ぼかすのはポストエフェクト（DepthOfFieldPass。CS 2本＋合成の PS）</para>
/// </summary>
class DepthOfFieldSystem {
public:
	// DepthOfFieldComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 有効な Depth Of Field Component を探して、SceneRenderer::SetDepthOfField で渡す（無ければぼかさない）
	/// <para>停止中も呼ぶ（エディタで値を変えて、すぐ見られるように）。ピントを合わせる Target の位置を使うので、Transform を計算し終わった後に呼ぶ</para>
	/// </summary>
	static void Update();
};