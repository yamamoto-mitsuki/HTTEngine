#include "TimelineSystem.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "MyEngine/Components/Animation/TimelineComponent.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Systems/Gameplay/ScriptSystem.h"
#include "MyEngine/Timeline/TimelineEventTrack.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"

namespace {
//=============================================================================
// Entity 1つ分の実行中の値
//=============================================================================
struct TimelineRuntime {
	Handle<Entity> entity;                                   // どの Entity か
	uint32_t timeline = 0;                                   // 作ったときのファイル（Component のファイルが替わったら作り直す）
	uint32_t revision = 0;                                   // 読んだファイルの版（変わったら作り直す）
	std::vector<std::unique_ptr<TimelineTrackState>> states; // [トラックの番号] トラックの Entity ごとの値
	TimelinePlayhead playhead;                               // 時間の位置
	bool needsApply = false;                                 // 時間が飛んだ（シーク・停止）。次の Update で値を書く
};

std::vector<TimelineRuntime> runtimes;                // 再生を使った Entity（数が少ないので、一列に並べて順に探す）
std::vector<TimelineRange> passedRanges;              // このフレームに通った時間の幅（毎回確保しないように使い回す）
std::vector<TimelineEvent> pendingEvents;             // 全部の値を書いた後に呼ぶイベント
std::vector<TimelineSystem::EventListener> listeners; // AddEventListener の関数
bool isRunning = false;                               // 再生中（エディタの停止中は false）

// ===== その Entity の実行中の値（まだ作っていなければ nullptr）=====
TimelineRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(runtimes, entity, &TimelineRuntime::entity);
	return (found != runtimes.end()) ? &*found : nullptr;
}

// ===== トラックの Entity ごとの値を作り直す（対象も探し直す）=====
void BuildStates(TimelineRuntime& runtime, uint32_t handle, const Timeline& timeline) {
	runtime.timeline = handle;
	runtime.revision = TimelineManager::GetRevision(handle);
	runtime.states.clear();
	for (const std::unique_ptr<TimelineTrack>& track : timeline.tracks) {
		runtime.states.push_back(track->CreateState());
	}
}

// ===== 実行中の値を用意する（Component・ファイルが無ければ nullptr）=====
TimelineRuntime* PrepareRuntime(Handle<Entity> entity) {
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
	const Timeline* timeline = (component != nullptr) ? TimelineManager::Get(component->timeline) : nullptr;
	if (!isRunning || timeline == nullptr) {
		return nullptr;
	}
	TimelineRuntime* runtime = FindRuntime(entity);
	if (runtime == nullptr) {
		runtimes.push_back({.entity = entity});
		runtime = &runtimes.back();
		BuildStates(*runtime, component->timeline, *timeline);
		if (component->playOnStart) {
			runtime->playhead.Play();
		}
	} else if (runtime->timeline != component->timeline || runtime->revision != TimelineManager::GetRevision(component->timeline)) {
		BuildStates(*runtime, component->timeline, *timeline);
		runtime->needsApply = true;
	}
	return runtime;
}

//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 全部のトラックを動かす =====
void EvaluateTracks(TimelineRuntime& runtime, const Timeline& timeline) {
	TimelineFrame frame;
	frame.owner = runtime.entity;
	frame.time = runtime.playhead.GetTime();
	frame.ranges = passedRanges;
	frame.events = &pendingEvents;
	for (size_t i = 0; i < timeline.tracks.size(); ++i) {
		const TimelineTrack& track = *timeline.tracks[i];
		if (!track.isMuted) {
			track.Evaluate(frame, *runtime.states[i]);
		}
	}
}

// ===== Entity 1つ =====
void UpdateEntity(Handle<Entity> entity, const TimelineComponent& component) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		return; // ファイルが無い・無効な Entity の間は時間も進めない
	}
	const Timeline& timeline = *TimelineManager::Get(runtime->timeline);
	// --- 時間を進める。通った幅があるか、時間が飛んだときだけ書く（止まっている間は、ゲームが書いた値を上書きしない）---
	const float speed = (component.speed > 0.0f) ? component.speed : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
	runtime->playhead.Advance(Time::GetDeltaTime(component.timeMode) * speed, timeline.GetDuration(), component.loop, passedRanges);
	if (!passedRanges.empty() || runtime->needsApply) {
		runtime->needsApply = false;
		EvaluateTracks(*runtime, timeline);
	}
}

// ===== 集めたイベントを呼ぶ =====
// 全部の Entity の値を書いた後に呼ぶ。中で Stop・Seek・Destroy をしても、回している途中の配列が動かない
void SendEvents() {
	for (const TimelineEvent& event : pendingEvents) {
		for (const TimelineSystem::EventListener listener : listeners) {
			listener(event);
		}
		ScriptSystem::SendTimelineEvent(event.target, event);
	}
	pendingEvents.clear();
}

//=============================================================================
// Inspector・保存
//=============================================================================
void DescribeTimelineComponent(ComponentUI& ui, TimelineComponent& value) {
	ui.AssetField("Timeline###timeline", value.timeline, AssetType::Timeline, Tip("再生するファイル（.timeline）。Project ウィンドウの Create → Timeline で作る"));
	ui.Field("Play On Start###playOnStart", value.playOnStart, Tip("再生が始まったら、すぐ再生する（外すと TimelineSystem::Play を呼ぶまで止まっている）"));
	ui.Field("Loop###loop", value.loop, Tip("最後まで行ったら最初へ戻る"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("再生速度（1 で普通）"));
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("Game：スロー・ヒットストップ・ポーズで止まる / Real：ゲームの時間が止まっても動く（エディタの一時停止では止まる）"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void TimelineSystem::Initialize() {
	// Timeline の置き場所・保存・Inspector を登録する（見せ方は DescribeTimelineComponent 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<TimelineComponent>();
	ComponentSerializer::Register<TimelineComponent>("Timeline", &DescribeTimelineComponent);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TimelineComponent>>("Timeline", "Animation", &DescribeTimelineComponent));
	EntityManager::AddDestroyListener(&TimelineSystem::OnEntityDestroyed);
	// エンジンのトラック（.lib の中の TIMELINE_TRACK はリンカに捨てられるので、ここで登録する）
	TimelineTrackRegistrar<TimelineValueTrack>("TimelineValueTrack", "Component の1つの項目をカーブで動かす");
	TimelineTrackRegistrar<TimelineEventTrack>("TimelineEventTrack", "指定の時間に Script の OnTimelineEvent を呼ぶ");
}

void TimelineSystem::Release() {
	runtimes.clear();
	pendingEvents.clear();
	listeners.clear();
	isRunning = false;
}

//=============================================================================
// 毎フレーム
//=============================================================================
void TimelineSystem::Update(bool isGameRunning) {
	isRunning = isGameRunning;
	if (!isGameRunning) {
		runtimes.clear(); // 停止中は動かさない（Play を押すと、また最初から）
		return;
	}
	// --- Component を外された Entity を片付ける ---
	std::erase_if(runtimes, [](const TimelineRuntime& runtime) { return EntityManager::Get<TimelineComponent>(runtime.entity) == nullptr; });
	EntityManager::ForEach<TimelineComponent>([](Handle<Entity> entity, TimelineComponent& component) { UpdateEntity(entity, component); });
	SendEvents();
}

//=============================================================================
// 外から指示する
//=============================================================================
bool TimelineSystem::Play(Handle<Entity> entity) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Play();
	}
	return runtime != nullptr;
}

bool TimelineSystem::Pause(Handle<Entity> entity) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Pause();
	}
	return runtime != nullptr;
}

bool TimelineSystem::Stop(Handle<Entity> entity) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Stop();
		runtime->needsApply = true;
	}
	return runtime != nullptr;
}

bool TimelineSystem::Seek(Handle<Entity> entity, float time) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Seek(time, TimelineManager::Get(runtime->timeline)->GetDuration());
		runtime->needsApply = true;
	}
	return runtime != nullptr;
}

//=============================================================================
// 外から読む
//=============================================================================
TimelineStatus TimelineSystem::GetStatus(Handle<Entity> entity) {
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
	const Timeline* timeline = (component != nullptr) ? TimelineManager::Get(component->timeline) : nullptr;
	if (timeline == nullptr) {
		return {};
	}
	TimelineStatus status;
	status.isValid = true;
	status.duration = timeline->GetDuration();
	if (const TimelineRuntime* runtime = FindRuntime(entity)) {
		status.state = runtime->playhead.GetState();
		status.time = runtime->playhead.GetTime();
		status.loopCount = runtime->playhead.GetLoopCount();
	}
	return status;
}

void TimelineSystem::AddEventListener(EventListener listener) {
	// 同じ関数は1回だけ（Script の Start から呼ぶと、Play のたびに呼ばれるので）
	if (std::ranges::find(listeners, listener) == listeners.end()) {
		listeners.push_back(listener);
	}
}

//=============================================================================
// Entity の破棄
//=============================================================================
void TimelineSystem::OnEntityDestroyed(Handle<Entity> entity) {
	std::erase_if(runtimes, [entity](const TimelineRuntime& runtime) { return runtime.entity == entity; });
}