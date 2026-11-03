#include "SceneTransition.h"

#include <algorithm>
#include <format>
#include <limits>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Math/Color.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/Time/EngineTime.h"

namespace {
/// <summary>
/// 幕が今どうしているか
/// </summary>
enum class FadeState {
	None,      // 何もしていない（幕は見えない）
	Closing,   // 閉じている途中（だんだん暗くする）
	Switching, // 閉じきって、切り替えを頼んだ（切り替わるのは次のフレームの頭。そのフレームは暗いまま待つ）
	Opening,   // 開いている途中（切り替わった後、だんだん明るくする）
};

/// <summary>
/// 閉じきったら何をするか
/// </summary>
enum class SwitchType {
	Load,   // sceneName のシーンへ（SceneManager::LoadScene）
	Reload, // 今のシーンを最初から（SceneManager::ReloadScene）
};

/// <summary>
/// 幕の状態（Entity ではないので、シーンを切り替えても消えない）
/// </summary>
struct TransitionState {
	FadeState state = FadeState::None;
	SwitchType switchType = SwitchType::Load;
	std::string sceneName;                              // switchType が Load のとき：切り替えるシーン
	float fadeTime = SceneTransition::kDefaultFadeTime; // 閉じる・開くのにかける秒数（それぞれ）
	float alpha = 0.0f;                                 // 幕の濃さ（0 で見えない、1 で真っ暗）
	Vector4 color = {0.0f, 0.0f, 0.0f, 1.0f};           // 幕の色（a は使わない。alpha を使う）
};
TransitionState transition;

// 幕を閉じ始めてよいか（再生中で、幕が動いていない）。what はログに出す名前
bool CanBegin(const std::string& what) {
	const SceneManager* main = SceneManager::GetMain();
	// 停止中はゲームのコードが動かないので、ふつうは来ない（エディタで開いているシーンを、勝手に替えないように）
	if (main == nullptr || main->GetPlayState() == PlayState::Editing) {
		LogManager::Log(std::format("SceneTransition::{}：停止中なので切り替えません（Play 中だけ効きます）", what));
		return false;
	}
	// 幕が動いている間は、後から頼まれた物を断る（ゴールした後に落ちて「やり直し」が重なる、など）
	return transition.state == FadeState::None;
}

// 幕を閉じ始める
void Begin(SwitchType switchType, const std::string& sceneName, float fadeTime) {
	transition.state = FadeState::Closing;
	transition.switchType = switchType;
	transition.sceneName = sceneName;
	transition.fadeTime = (std::max)(fadeTime, 0.0f);
}

// 閉じきった：切り替えを頼む（切り替わるのは、次のフレームの頭）
void RequestSwitch() {
	switch (transition.switchType) {
	case SwitchType::Load:
		SceneManager::LoadScene(transition.sceneName);
		break;
	case SwitchType::Reload:
		SceneManager::ReloadScene();
		break;
	}
}
} // namespace

//=============================================================================
// ゲームから使う
//=============================================================================
bool SceneTransition::LoadScene(const std::string& name, float fadeTime) {
	if (!CanBegin(std::format("LoadScene(\"{}\")", name))) {
		return false;
	}
	// シーンが無いのに閉じると、暗くなってから何も起きない（名前の打ち間違いは、閉じる前に断る）
	if (!SceneManager::HasScene(name)) {
		LogManager::Warning(std::format("SceneTransition::LoadScene(\"{}\")：シーンがありません", name));
		return false;
	}
	Begin(SwitchType::Load, name, fadeTime);
	return true;
}

bool SceneTransition::ReloadScene(float fadeTime) {
	if (!CanBegin("ReloadScene()")) {
		return false;
	}
	Begin(SwitchType::Reload, "", fadeTime);
	return true;
}

bool SceneTransition::IsBusy() { return transition.state != FadeState::None; }

void SceneTransition::SetColor(const Vector4& color) { transition.color = color; }

//=============================================================================
// 毎フレーム
//=============================================================================
void SceneTransition::Update(bool isGameRunning) {
	// 停止中：幕を片付ける（再生中に Stop を押したら、暗いまま残さない）
	if (!isGameRunning) {
		transition.state = FadeState::None;
		transition.alpha = 0.0f;
		return;
	}
	// エディタの一時停止中は、幕も止める
	const SceneManager* main = SceneManager::GetMain();
	if (main != nullptr && main->GetPlayState() == PlayState::Paused) {
		return;
	}
	// 1フレームに変える濃さ（濃さ 1 を fadeTime 秒で変えきる。0 秒なら、すぐ変えきる）
	// 止めない時間で数える（ポーズ中に「やり直す」を選んだときも、幕は動く）
	const float step = (transition.fadeTime > 0.0f) ? Time::GetUnscaleDeltaTime() / transition.fadeTime : 1.0f;
	switch (transition.state) {
	case FadeState::None:
		break;
	case FadeState::Closing:
		transition.alpha = (std::min)(transition.alpha + step, 1.0f);
		if (transition.alpha >= 1.0f) {
			RequestSwitch();
			transition.state = FadeState::Switching;
		}
		break;
	case FadeState::Switching:
		// このフレームの頭で切り替わった。読み込みで長くなったフレームの時間で一気に明るくしないよう、ここでは進めない
		transition.state = FadeState::Opening;
		break;
	case FadeState::Opening:
		transition.alpha = (std::max)(transition.alpha - step, 0.0f);
		if (transition.alpha <= 0.0f) {
			transition.state = FadeState::None;
		}
		break;
	}
}

//=============================================================================
// 描く
//=============================================================================
void SceneTransition::Draw() {
	if (transition.alpha <= 0.0f) {
		return;
	}
	// 画面全体の四角（正規化座標の -1〜1）。画像は無し＝白に色を掛ける
	Renderer::Quad2dConfig config;
	config.lb = {-1.0f, -1.0f};
	config.lt = {-1.0f, 1.0f};
	config.rb = {1.0f, -1.0f};
	config.rt = {1.0f, 1.0f};
	config.color = ClampColor({transition.color.x, transition.color.y, transition.color.z, transition.alpha});
	config.blendMode = BlendMode::Normal;
	config.rasterizerType = RasterizerType::SolidNone;
	config.drawKey = UIDrawKey::FromOrder((std::numeric_limits<int>::max)()); // 一番手前（どの UI の Order よりも大きい）
	config.isSceneHidden = true;                      // Scene ビューには描かない（再生中でも、置いた物が見えるように。Game ビューだけ暗くなる）
	Renderer::DrawQuad2d(config);
}