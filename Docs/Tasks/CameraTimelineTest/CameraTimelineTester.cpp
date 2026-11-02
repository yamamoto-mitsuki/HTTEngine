#include <format>
#include <string>

#include <MyEngine/Components/Animation/TimelineComponent.h>
#include <MyEngine/ECS/Script.h>
#include <MyEngine/Input/InputManager.h>
#include <MyEngine/Scene/PrefabManager.h>
#include <MyEngine/Systems/Animation/TimelineSystem.h>

// カメラの演出の見本を動かす Script。Timeline を再生する Director に付ける
// 演出の中身は resources/timelines/CameraTimelineTest.timeline にある（ここはヒットストップのイベントを受けるだけ）
// 1：最初から再生 / 2：一時停止 / 3：止める / 4：5.5 秒へシーク / 5：Prefab のカメラを置いて再生
// 6：Timeline の Time Mode の切り替え / H：1秒のヒットストップ
class CameraTimelineTester : public Script {
public:
	void Update() override {
		const Handle<Entity> self = gameObject.handle;
		if (InputManager::IsKeyTriggered(DIK_1)) {
			TimelineSystem::Stop(self);
			TimelineSystem::Play(self);
		}
		if (InputManager::IsKeyTriggered(DIK_2)) {
			TimelineSystem::Pause(self);
		}
		if (InputManager::IsKeyTriggered(DIK_3)) {
			TimelineSystem::Stop(self);
		}
		if (InputManager::IsKeyTriggered(DIK_4)) {
			TimelineSystem::Seek(self, kSeekTime);
		}
		if (InputManager::IsKeyTriggered(DIK_5)) {
			PrefabManager::Instantiate(PrefabManager::Load("resources/prefabs/CameraRig.prefab"), {0.0f, 0.0f, 0.0f});
		}
		TimelineComponent* timeline = GetComponent<TimelineComponent>();
		if (InputManager::IsKeyTriggered(DIK_6)) {
			timeline->timeMode = (timeline->timeMode == TimeMode::Real) ? TimeMode::Game : TimeMode::Real;
		}
		if (InputManager::IsKeyTriggered(DIK_H)) {
			Time::HitStop(1.0f);
		}
		SpinBoss();
		DrawStatus(*timeline);
	}

	// Timeline のイベント："HitStop" の値が止める秒
	void OnTimelineEvent(const TimelineEvent& event) override {
		if (event.name == "HitStop") {
			Time::HitStop(std::stof(event.value));
		}
	}

	void Fields(ComponentUI& ui) override { ui.Field("Boss", boss_, Tip("回す Entity（ゲームの時間で回るので、ヒットストップで止まる）")); }

private:
	// ゲームの時間で回す（ヒットストップの間も演出が進むのが見えるように）
	void SpinBoss() {
		if (TransformComponent* boss = boss_.GetComponent<TransformComponent>()) {
			Vector3 euler = GetEulerAngles(*boss);
			euler.y += kSpinSpeed * Time::GetDeltaTime();
			SetEulerAngles(*boss, euler);
		}
	}

	// 今の様子を画面の左上に出す
	void DrawStatus(const TimelineComponent& timeline) {
		TextComponent* text = EntityManager::Get<TextComponent>(EntityManager::FindByName("Status"));
		if (text == nullptr) {
			return;
		}
		const TimelineStatus status = TimelineSystem::GetStatus(gameObject.handle);
		const Entity* camera = EntityManager::Get(CameraSystem::GetMainCameraEntity());
		const char* timeMode = (timeline.timeMode == TimeMode::Real) ? "Real" : "Game";
		text->text = std::format("{:.2f} / {:.2f} 秒（{}）\nカメラ：{}{}\n{}", status.time, status.duration, timeMode, (camera != nullptr) ? camera->name : "なし", CameraSystem::IsBlending() ? "（補間中）" : "", Time::IsHitStopping() ? "ヒットストップ中" : "");
	}

	static constexpr float kSeekTime = 5.5f;   // 4 のキーで飛ぶ時間（秒）
	static constexpr float kSpinSpeed = 90.0f; // Boss の回る速さ（度/秒）

	GameObject boss_; // 回す Entity
};
SCRIPT(CameraTimelineTester, "CameraTest");
