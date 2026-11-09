#pragma once
#include <string>

#include "HTTEngine/Math/Vector4.h"

/// <summary>
/// シーンを切り替えるときの幕（画面をだんだん暗くする → 切り替える → だんだん明るくする）
/// <para>ゲームのコードは SceneManager::LoadScene・ReloadScene の代わりにこれを呼ぶ（呼び方は同じ。幕が付くだけ）</para>
/// <para>幕は Entity ではなく、ここが持つ（Entity にすると、シーンを切り替えたときに幕も消えてしまう）</para>
/// <para>時間は止めない時間（GetUnscaleDeltaTime）で数える（ポーズ中に「やり直す」を選んでも、幕は動く）</para>
/// </summary>
class SceneTransition {
public:
	static constexpr float kDefaultFadeTime = 0.4f; // 幕を閉じる・開くのにかける秒数（それぞれ）

	// ===== ゲームから使う =====
	// 幕を閉じてから、name のシーンへ切り替える（SceneManager::LoadScene と同じ。シーンが無い・停止中・幕が動いている間は false）
	static bool LoadScene(const std::string& name, float fadeTime = kDefaultFadeTime);
	// 幕を閉じてから、今のシーンを最初からやり直す（SceneManager::ReloadScene と同じ。停止中・幕が動いている間は false）
	static bool ReloadScene(float fadeTime = kDefaultFadeTime);
	// 幕が動いているか（閉じている・開いている途中）。ゴールした後にもう一度頼まない・入力を受けないのに使う
	static bool IsBusy();
	// 幕の色（初めは黒。a は使わない）
	static void SetColor(const Vector4& color);

	// ===== エンジンから使う =====
	// 毎フレーム（WindowManager::UpdateAll）。幕を進めて、閉じきったら切り替えを頼む。停止中は幕を片付ける
	static void Update(bool isGameRunning);
	// 幕を積む（WindowManager::DrawAll。UI より手前。Scene ビューには描かない）
	static void Draw();
};