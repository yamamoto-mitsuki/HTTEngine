#include <format>
#include <string>

#include <MyEngine/Components/Animation/TimelineComponent.h>
#include <MyEngine/ECS/Script.h>
#include <MyEngine/Input/InputManager.h>
#include <MyEngine/Scene/PrefabManager.h>
#include <MyEngine/Systems/Animation/TimelineSystem.h>
#include <MyEngine/Timeline/TimelineTrack.h>
#include <MyEngine/UI/UIScreen.h>

// Timeline を確かめるための Script。Game Panel に付ける
// 1：Play / 2：Pause / 3：Stop / 4：1 秒へシーク / 5：Loop の切り替え / 6：速さの切り替え
// 7：画面を開く・閉じる（ゲームを止める） / 8：Prefab を2つ作る / H：1秒のヒットストップ

namespace {
constexpr const char* kScreenName = "TimelineTestScreen"; // 開く画面の名前

// 状態の名前
const char* NameOf(TimelinePlayState state) {
	switch (state) {
	case TimelinePlayState::Stopped:
		return "Stopped";
	case TimelinePlayState::Playing:
		return "Playing";
	case TimelinePlayState::Paused:
		return "Paused";
	case TimelinePlayState::Finished:
		return "Finished";
	}
	return "";
}

// 7 のキーで画面を閉じる（画面が開いている間はゲームが止まり、Game Panel の Script は呼ばれないので、画面の中の Script で読む）
void CheckCloseKey() {
	if (InputManager::IsKeyTriggered(DIK_7)) {
		UIScreen::Close(kScreenName);
	}
}
} // namespace


class TimelineTester : public Script {
public:
	void Update() override {
		const Handle<Entity> self = gameObject.handle;
		if (InputManager::IsKeyTriggered(DIK_1)) {
			TimelineSystem::Play(self);
		}
		if (InputManager::IsKeyTriggered(DIK_2)) {
			TimelineSystem::Pause(self);
		}
		if (InputManager::IsKeyTriggered(DIK_3)) {
			TimelineSystem::Stop(self);
		}
		if (InputManager::IsKeyTriggered(DIK_4)) {
			TimelineSystem::Seek(self, 1.0f);
		}
		TimelineComponent* timeline = GetComponent<TimelineComponent>();
		if (InputManager::IsKeyTriggered(DIK_5)) {
			timeline->loop = !timeline->loop;
		}
		if (InputManager::IsKeyTriggered(DIK_6)) {
			timeline->speed = (timeline->speed == 1.0f) ? 0.25f : 1.0f;
		}
		if (InputManager::IsKeyTriggered(DIK_7)) {
			UIScreen::Open(kScreenName);
		}
		if (InputManager::IsKeyTriggered(DIK_8)) {
			const uint32_t prefab = PrefabManager::Load("resources/prefabs/TimelineTestPanel.prefab");
			PrefabManager::Instantiate(prefab, {-350.0f, nextPrefabY_, 0.0f});
			PrefabManager::Instantiate(prefab, {350.0f, nextPrefabY_, 0.0f});
			nextPrefabY_ = (nextPrefabY_ > 0.0f) ? -150.0f : 150.0f;
		}
		if (InputManager::IsKeyTriggered(DIK_H)) {
			Time::HitStop(1.0f);
		}
		DrawStatus(*timeline);
	}

	// イベントは全部ログと画面に出す
	void OnTimelineEvent(const TimelineEvent& event) override {
		lastEvent_ = std::format("{}（{:.2f} 秒）{}", event.name, event.time, event.value);
		++eventCount_;
		LogManager::Log(std::format("Timeline のイベント：{}", lastEvent_));
	}

private:
	// 今の様子を画面の左上に出す
	void DrawStatus(const TimelineComponent& timeline) {
		TextComponent* text = EntityManager::Get<TextComponent>(EntityManager::FindByName("Status"));
		if (text == nullptr) {
			return;
		}
		const TimelineStatus game = TimelineSystem::GetStatus(gameObject.handle);
		const TimelineStatus real = TimelineSystem::GetStatus(EntityManager::FindByName("Real Panel"));
		text->text = std::format("Game {} {:.2f}/{:.2f} 周 {} Loop {} 速さ {:.2f}\nReal {} {:.2f} 周 {}\nイベント {} 回：{}", NameOf(game.state), game.time, game.duration, game.loopCount, timeline.loop, timeline.speed, NameOf(real.state), real.time, real.loopCount, eventCount_, lastEvent_);
	}

	float nextPrefabY_ = 150.0f; // 次に作る Prefab の高さ
	int eventCount_ = 0;         // 受け取ったイベントの数
	std::string lastEvent_;      // 最後に受け取ったイベント
};
SCRIPT(TimelineTester);


// 画面の一番上の Entity に付ける（ポーズ中は画面の中の Script だけが呼ばれる）
class TimelineTesterScreen : public Script {
public:
	void Update() override { CheckCloseKey(); }
};
SCRIPT(TimelineTesterScreen);
