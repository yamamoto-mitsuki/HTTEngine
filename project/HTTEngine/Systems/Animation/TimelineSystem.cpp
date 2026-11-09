#include "TimelineSystem.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "HTTEngine/Components/Animation/TimelineComponent.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
#include "HTTEngine/Timeline/Camera/TimelineCameraShakeTrack.h"
#include "HTTEngine/Timeline/Camera/TimelineCameraTrack.h"
#include "HTTEngine/Timeline/TimelineEventTrack.h"
#include "HTTEngine/Timeline/TimelineManager.h"
#include "HTTEngine/Timeline/TimelineValueTrack.h"
#include "HTTEngine/Timeline/Tween/TimelinePresetTrack.h"
#include "HTTEngine/Timeline/Tween/TimelineTweenTrack.h"

namespace {
//=============================================================================
// 再生1つ分の実行中の値
//=============================================================================
struct TimelineRuntime {
	Handle<Entity> entity;                          // 再生する Entity
	TimelineLayer layer = TimelineLayer::Component; // どの層の再生か
	TimelinePlaySettings settings;                  // 再生の設定（Component の層は、毎フレーム Component から作り直す）
	TimelineStates states;                          // トラックの Entity ごとの値
	TimelinePlayhead playhead;                      // 時間の位置
	bool needsApply = false;                        // 時間が飛んだ（シーク・停止・再生のし直し）。次の Update で値を書く
};

std::vector<TimelineRuntime> runtimes;                // 再生（層の順に並べる。同じ層の中は、後から再生を始めた物が後ろ）
std::vector<TimelineOverrides> overrides;             // Entity ごとの、実行中に渡した値・差し替えた対象
std::vector<TimelineRange> passedRanges;              // このフレームに通った時間の幅（毎回確保しないように使い回す）
std::vector<TimelineEvent> pendingEvents;             // 全部の値を書いた後に呼ぶイベント
std::vector<TimelineSystem::EventListener> listeners; // AddEventListener の関数
bool isRunning = false;                               // 再生中（エディタの停止中は false）

/// <summary>
/// 頼まれたプレビュー
/// </summary>
struct PreviewRequest {
	Handle<Entity> owner;  // 再生する Entity
	uint32_t timeline = 0; // 見る Timeline
	float time = 0.0f;     // 見る時間（秒）
};
std::optional<PreviewRequest> previewRequest; // 次の Update で見る物（毎フレーム頼まれる）
Handle<Entity> previewOwner;                  // プレビューのトラックの値を作ったときの、再生する Entity
TimelineStates previewStates;                 // プレビューのトラックの値（毎フレーム作らないように持っておく）
bool isPreviewApplied = false;                // 値を仮に書いている（FinishPreview で戻す）

// ===== その Entity・層の実行中の値（まだ作っていなければ nullptr）=====
TimelineRuntime* FindRuntime(Handle<Entity> entity, TimelineLayer layer) {
	const auto found = std::ranges::find_if(runtimes, [&](const TimelineRuntime& runtime) { return runtime.entity == entity && runtime.layer == layer; });
	return (found != runtimes.end()) ? &*found : nullptr;
}

// ===== 実行中の値を足す（同じ層の一番後ろ。値を書く順は、この並びの順）=====
TimelineRuntime& AddRuntime(Handle<Entity> entity, TimelineLayer layer) {
	const auto position = std::ranges::upper_bound(runtimes, layer, {}, &TimelineRuntime::layer);
	return *runtimes.insert(position, TimelineRuntime{.entity = entity, .layer = layer});
}

// ===== Entity に渡した値・差し替えた対象（無ければ nullptr）=====
TimelineOverrides* FindOverrides(Handle<Entity> owner) {
	const auto found = std::ranges::find(overrides, owner, &TimelineOverrides::owner);
	return (found != overrides.end()) ? &*found : nullptr;
}

// ===== Component の設定から、再生の設定を作る =====
TimelinePlaySettings SettingsOf(const TimelineComponent& component) { return {.timeline = component.timeline, .loop = component.loop, .speed = component.speed, .timeMode = component.timeMode}; }

// ===== Timeline Component の再生の実行中の値を用意する（Component・ファイルが無ければ nullptr）=====
TimelineRuntime* PrepareRuntime(Handle<Entity> entity) {
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
	const Timeline* timeline = (component != nullptr) ? TimelineManager::Get(component->timeline) : nullptr;
	if (!isRunning || timeline == nullptr) {
		return nullptr;
	}
	TimelineRuntime* runtime = FindRuntime(entity, TimelineLayer::Component);
	if (runtime == nullptr) {
		runtime = &AddRuntime(entity, TimelineLayer::Component);
		runtime->states.Prepare(component->timeline);
		if (component->playOnStart) {
			runtime->playhead.Play();
		}
	} else if (runtime->states.Prepare(component->timeline)) {
		runtime->needsApply = true; // ファイルを替えた・直した
	}
	runtime->settings = SettingsOf(*component);
	return runtime;
}

//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 再生1つを1フレーム進める =====
void UpdateRuntime(TimelineRuntime& runtime) {
	const Timeline* timeline = TimelineManager::Get(runtime.settings.timeline);
	if (timeline == nullptr || !EntityManager::IsActiveInHierarchy(runtime.entity)) {
		return; // ファイルが無い・無効な Entity の間は時間も進めない
	}
	if (runtime.states.Prepare(runtime.settings.timeline)) {
		runtime.needsApply = true; // ファイルを直した
	}
	// --- 時間を進める。通った幅があるか、時間が飛んだときだけ書く（止まっている間は、ゲームが書いた値を上書きしない）---
	const float speed = (runtime.settings.speed > 0.0f) ? runtime.settings.speed : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
	runtime.playhead.Advance(Time::GetDeltaTime(runtime.settings.timeMode) * speed, timeline->GetDuration(), runtime.settings.loop, passedRanges);
	TimelineFrame frame;
	frame.owner = runtime.entity;
	frame.time = runtime.playhead.GetTime();
	frame.ranges = passedRanges;
	frame.events = &pendingEvents;
	frame.timeMode = runtime.settings.timeMode;
	frame.overrides = FindOverrides(runtime.entity);
	timeline->Evaluate(frame, runtime.states, !passedRanges.empty() || runtime.needsApply, runtime.playhead.GetState() == TimelinePlayState::Stopped);
	runtime.needsApply = false;
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

// ===== プレビュー：今の値を写して、見る時間の値を書く =====
void ApplyPreview(const PreviewRequest& request) {
	const Timeline* timeline = TimelineManager::Get(request.timeline);
	if (timeline == nullptr || !EntityManager::IsAlive(request.owner)) {
		return;
	}
	// --- トラックの値を用意する（警告は出さない。キーを動かすたびに作り直すのでログが埋まる。問題は Timeline ウィンドウの行に出す）---
	if (previewOwner != request.owner) {
		previewOwner = request.owner;
		previewStates = {}; // 別の Entity の対象を覚えているので、作り直す
	}
	if (previewStates.Prepare(request.timeline)) {
		for (const std::unique_ptr<TimelineTrackState>& state : previewStates.tracks) {
			state->isWarned = true;
		}
	}
	// --- 書く（イベントは呼ばない。ranges が空なので、通った時間は無い）---
	TimelineFrame frame;
	frame.owner = request.owner;
	frame.time = request.time;
	frame.events = &pendingEvents;
	frame.isPreview = true;
	ComponentSerializer::BeginValueCapture();
	isPreviewApplied = true;
	CameraSystem::BeginPreviewShake(request.time);
	timeline->Evaluate(frame, previewStates, true, false);
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
	TimelineTrackRegistrar<TimelineCameraTrack>("TimelineCameraTrack", "クリップごとに写すカメラを決める。クリップを重ねると補間する");
	TimelineTrackRegistrar<TimelineCameraShakeTrack>("TimelineCameraShakeTrack", "指定の時間に、カメラへ衝撃を送って揺らす");
	TimelineTrackRegistrar<TimelineTweenTrack>("TimelineTweenTrack", "Component の1つの項目を From → To で動かす（今の値から・元の値からの差で書ける）");
	TimelineTrackRegistrar<TimelinePresetTrack>("TimelinePresetTrack", "別の Timeline（プリセット）を、対象の上で再生する。ずらして並べると、順に動く");
}

void TimelineSystem::Release() {
	runtimes.clear();
	overrides.clear();
	pendingEvents.clear();
	listeners.clear();
	previewRequest.reset();
	previewOwner = {};
	previewStates = {};
	isRunning = false;
	TimelineTweenTrack::ClearBaseValues();
}

//=============================================================================
// 毎フレーム
//=============================================================================
void TimelineSystem::Update(bool isGameRunning) {
	isRunning = isGameRunning;
	const std::optional<PreviewRequest> preview = std::exchange(previewRequest, std::nullopt); // 頼まれた1フレーム分だけ
	if (!isGameRunning) {
		// 停止中は動かさない（Play を押すと、また最初から）
		runtimes.clear();
		overrides.clear();
		TimelineTweenTrack::ClearBaseValues();
		if (preview.has_value()) {
			ApplyPreview(*preview);
		}
		return;
	}
	// --- Component を外された Entity を片付ける ---
	std::erase_if(runtimes, [](const TimelineRuntime& runtime) { return runtime.layer == TimelineLayer::Component && EntityManager::Get<TimelineComponent>(runtime.entity) == nullptr; });
	// --- Timeline Component の層（今までと同じ、Entity の順）→ ほかの層（並びの順。後の層ほど後に書くので勝つ）---
	EntityManager::ForEach<TimelineComponent>([](Handle<Entity> entity, TimelineComponent&) {
		if (TimelineRuntime* runtime = PrepareRuntime(entity)) {
			UpdateRuntime(*runtime);
		}
	});
	for (TimelineRuntime& runtime : runtimes) {
		if (runtime.layer != TimelineLayer::Component) {
			UpdateRuntime(runtime);
		}
	}
	SendEvents();
}

//=============================================================================
// Timeline Component の再生を指示する
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
		runtime->playhead.Seek(time, TimelineManager::Get(runtime->settings.timeline)->GetDuration());
		runtime->needsApply = true;
	}
	return runtime != nullptr;
}

//=============================================================================
// Component を使わずに再生する
//=============================================================================
bool TimelineSystem::PlayLayer(Handle<Entity> owner, TimelineLayer layer, const TimelinePlaySettings& settings) {
	const Timeline* timeline = TimelineManager::Get(settings.timeline);
	if (layer == TimelineLayer::Component || timeline == nullptr || !EntityManager::IsAlive(owner)) {
		return false;
	}
	// 前の再生を消して、同じ層の一番後ろに足し直す（後から始めた方が勝つ）。停止中に頼まれても、次の Update で消える
	StopLayer(owner, layer);
	TimelineRuntime& runtime = AddRuntime(owner, layer);
	runtime.settings = settings;
	runtime.states.Prepare(settings.timeline);
	runtime.playhead.Play();
	if (settings.jumpToEnd) {
		runtime.playhead.Seek(timeline->GetDuration(), timeline->GetDuration()); // 再生中のまま最後へ（次の Update で最後の値を書いて、終わる）
	}
	runtime.needsApply = true; // 時間が進まないフレーム（Game の時間でポーズ中）でも、最初の値を書く
	return true;
}

void TimelineSystem::StopLayer(Handle<Entity> owner, TimelineLayer layer) {
	if (layer == TimelineLayer::Component) {
		return; // Component の層は Stop で止める
	}
	std::erase_if(runtimes, [&](const TimelineRuntime& runtime) { return runtime.entity == owner && runtime.layer == layer; });
}

//=============================================================================
// 実行中に、値・対象を変える
//=============================================================================
void TimelineSystem::SetParameter(Handle<Entity> owner, std::string_view name, const Vector4& value) {
	TimelineOverrides* found = FindOverrides(owner);
	if (found == nullptr) {
		found = &overrides.emplace_back(TimelineOverrides{.owner = owner});
	}
	const auto parameter = std::ranges::find(found->parameters, name, &TimelineParameter::name);
	if (parameter != found->parameters.end()) {
		parameter->value = value;
	} else {
		found->parameters.push_back({std::string(name), value});
	}
}

void TimelineSystem::SetParameter(Handle<Entity> owner, std::string_view name, float value) { SetParameter(owner, name, Vector4{value, value, value, value}); }

void TimelineSystem::SetTargetOverride(Handle<Entity> owner, std::string_view path, Handle<Entity> target) {
	TimelineOverrides* found = FindOverrides(owner);
	if (found == nullptr) {
		if (!target.IsValid()) {
			return; // 元へ戻すだけなので、作らない
		}
		found = &overrides.emplace_back(TimelineOverrides{.owner = owner});
	}
	std::erase_if(found->targets, [path](const TimelineTargetOverride& item) { return item.path == path; });
	if (target.IsValid()) {
		found->targets.push_back({std::string(path), target});
	}
}

void TimelineSystem::ClearOverrides(Handle<Entity> owner) {
	std::erase_if(overrides, [owner](const TimelineOverrides& item) { return item.owner == owner; });
}

void TimelineSystem::ResetBaseValues(Handle<Entity> entity) { TimelineTweenTrack::ResetBaseValues(entity); }

//=============================================================================
// プレビュー
//=============================================================================
void TimelineSystem::RequestPreview(Handle<Entity> owner, uint32_t timeline, float time) { previewRequest = PreviewRequest{owner, timeline, time}; }

void TimelineSystem::FinishPreview() {
	if (!isPreviewApplied) {
		return;
	}
	isPreviewApplied = false;
	ComponentSerializer::RestoreCapturedValues();
	EntityManager::UpdateTransforms(); // 動かした物の子のワールド行列も元へ戻す（この後の ImGui のギズモが読む）
}

//=============================================================================
// 外から読む
//=============================================================================
TimelineStatus TimelineSystem::GetStatus(Handle<Entity> entity, TimelineLayer layer) {
	// --- 再生しているファイル（Component の層は、まだ再生していなくても Component のファイル）---
	const TimelineRuntime* runtime = FindRuntime(entity, layer);
	uint32_t handle = (runtime != nullptr) ? runtime->settings.timeline : 0;
	if (layer == TimelineLayer::Component) {
		const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
		handle = (component != nullptr) ? component->timeline : 0;
	}
	const Timeline* timeline = TimelineManager::Get(handle);
	if (timeline == nullptr) {
		return {};
	}
	TimelineStatus status;
	status.isValid = true;
	status.timeline = handle;
	status.duration = timeline->GetDuration();
	if (runtime != nullptr) {
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
	ClearOverrides(entity);
	TimelineTweenTrack::ResetBaseValues(entity);
}