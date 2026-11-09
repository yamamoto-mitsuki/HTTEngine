#include <HTTEngine/ECS/EntityManager.h>
#include <HTTEngine/ECS/Script.h>
#include <HTTEngine/Input/InputManager.h>
#include <HTTEngine/Systems/Animation/TimelineSystem.h>
#include <HTTEngine/UI/UIScreen.h>

// UI アニメーションの見本を、キーで動かす Script（UIAnimationTest シーンの Tester に付ける）
// 1 / 2 / 3：HP のゲージを 100% / 50% / 15% へ動かす（Tween の Parameter "hp" に渡す）
// 4：Pulse（HUD の Gauge を一度ふくらませる）/ 5：Pulse の対象を Gauge ⇔ Ring に替える / M：メニューを出す
// ゲージ・Pulse の動きは .timeline に書いてあり、ここは「いつ・どの値で」再生するかだけ

namespace {
constexpr const char* kMenuScreen = "UIAnimMenu"; // M で出す画面
constexpr const char* kPulseTarget = "Gauge";     // Pulse.timeline のトラックの Target に書いてある文字
} // namespace

class UIAnimationTester : public Script {
public:
	void Update() override {
		// --- HP：Parameter に渡してから再生する（同じ層で再生し直すと、今の値から続けて動く）---
		if (InputManager::IsKeyTriggered(DIK_1)) {
			PlayGauge(1.0f);
		}
		if (InputManager::IsKeyTriggered(DIK_2)) {
			PlayGauge(0.5f);
		}
		if (InputManager::IsKeyTriggered(DIK_3)) {
			PlayGauge(0.15f);
		}
		// --- Pulse：HUD を再生する Entity にして、Script の層で再生する ---
		const Handle<Entity> hud = EntityManager::FindByName("HUD");
		if (InputManager::IsKeyTriggered(DIK_4)) {
			TimelineSystem::PlayLayer(hud, TimelineLayer::Script, {.timeline = pulseTimeline_});
		}
		// --- 対象の差し替え：Target が "Gauge" のトラックに、Ring を動かさせる ---
		if (InputManager::IsKeyTriggered(DIK_5)) {
			isPulseOnRing_ = !isPulseOnRing_;
			TimelineSystem::SetTargetOverride(hud, kPulseTarget, isPulseOnRing_ ? EntityManager::FindByName("Ring") : Handle<Entity>{});
		}
		// --- メニュー（MENU のボタンと同じ。開いている間はゲームが止まり、この Script は呼ばれない）---
		if (InputManager::IsKeyTriggered(DIK_M)) {
			UIScreen::Open(kMenuScreen);
		}
	}

	void Fields(ComponentUI& ui) override {
		ui.Label("1/2/3：HP / 4：Pulse / 5：Pulse の対象を替える / M：メニュー");
		ui.AssetField("Gauge Timeline", gaugeTimeline_, AssetType::Timeline, Tip("ゲージを動かす Timeline（Tween の To が Parameter \"hp\"）"));
		ui.AssetField("Pulse Timeline", pulseTimeline_, AssetType::Timeline, Tip("HUD の Gauge をふくらませる Timeline"));
	}

private:
	// ゲージを value へ動かす
	void PlayGauge(float value) {
		const Handle<Entity> gauge = EntityManager::FindByName("GaugeFill");
		TimelineSystem::SetParameter(gauge, "hp", value);
		TimelineSystem::PlayLayer(gauge, TimelineLayer::Script, {.timeline = gaugeTimeline_});
	}

	uint32_t gaugeTimeline_ = 0; // ゲージの Timeline
	uint32_t pulseTimeline_ = 0; // Pulse の Timeline
	bool isPulseOnRing_ = false; // Pulse の対象を Ring に替えている
};
SCRIPT(UIAnimationTester, "UI");
