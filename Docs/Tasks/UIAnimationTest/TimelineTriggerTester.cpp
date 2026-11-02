#include <MyEngine/ECS/Script.h>
#include <MyEngine/Input/InputManager.h>
#include <MyEngine/Time/EngineTime.h>

// Timeline Trigger の見本（TimelineTriggerTest シーンの Player に付ける）。←→ で自分を横に動かすだけ
// 範囲に入った・出たときの演出（扉・パネル）は、SignArea の Timeline Trigger と .timeline に書いてあり、ここには書かない

class TimelineTriggerTester : public Script {
public:
	void Update() override {
		float direction = 0.0f;
		if (InputManager::IsKeyPressed(DIK_LEFT)) {
			direction -= 1.0f;
		}
		if (InputManager::IsKeyPressed(DIK_RIGHT)) {
			direction += 1.0f;
		}
		transform.translation.x += direction * moveSpeed_ * Time::GetDeltaTime();
	}

	void Fields(ComponentUI& ui) override {
		ui.Label("←→ で横に動く");
		ui.Field("Move Speed", moveSpeed_, AtLeast(0.0f).Tip("1秒に動く長さ（m）"));
	}

private:
	float moveSpeed_ = 5.0f; // 1秒に動く長さ（m）
};
SCRIPT(TimelineTriggerTester, "UI");
