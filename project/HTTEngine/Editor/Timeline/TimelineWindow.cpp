#ifdef USE_IMGUI
#include "TimelineWindow.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Components/Animation/TimelineComponent.h"
#include "HTTEngine/Components/UI/UIScreenComponent.h"
#include "HTTEngine/Components/UI/UIStateComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Timeline/TimelineEditorState.h"
#include "HTTEngine/Editor/Widgets/ToolIcons.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"

namespace {
constexpr float kFirstWidth = 960.0f;                             // 最初のウィンドウの幅
constexpr float kFirstHeight = 520.0f;                            // 最初のウィンドウの高さ
constexpr float kMinRowsHeight = 60.0f;                           // トラックの一覧の高さの下限
constexpr float kMinDetailsHeight = 120.0f;                       // 詳細の高さの下限
constexpr float kSplitterHeight = 8.0f;                           // 詳細との境目の高さ
constexpr float kTimeFieldWidth = 80.0f;                          // 再生位置の欄の幅
constexpr float kLengthFieldWidth = 70.0f;                        // 長さの欄の幅
constexpr float kSnapFieldWidth = 80.0f;                          // スナップの欄の幅
constexpr float kSourceFieldWidth = 260.0f;                       // 欄を選ぶ一覧の幅
constexpr const char* kNewTimelineFolder = "resources/timelines"; // New で作る Timeline の置き場所

/// <summary>
/// スナップの選択肢1つ
/// </summary>
struct SnapChoice {
	const char* name;
	float step;
};
constexpr std::array<SnapChoice, 6> kSnapChoices = {
    {{"Off", 0.0f}, {"1 s", 1.0f}, {"0.5 s", 0.5f}, {"0.1 s", 0.1f}, {"1/30 s", 1.0f / 30.0f}, {"1/60 s", 1.0f / 60.0f}}
};

TimelineEditorState state;               // ウィンドウの状態
Handle<Entity> lastSelected;             // 前に見た Hierarchy の選択（選び直したときだけファイルを開き直す）
Handle<Entity> lastScreenRoot;           // 前に見た編集中の画面（UI Screen）
float rowsLimit = 180.0f;                // トラックの一覧の高さの上限（境目をつまんで変える。トラックが少なければ低く出して、詳細に譲る）
bool needsFocus = false;                 // 次に描くとき、ウィンドウを前に出す
std::vector<TimelineRange> passedRanges; // プレビューで通った時間（使わないが、Advance が書く場所）

//=============================================================================
// 欄（再生する Entity の Timeline を選ぶ所）
//=============================================================================
// ===== Component を Inspector で変えたときと同じく、前後を履歴に積んで変える =====
template<class T, class Edit> void EditComponent(Handle<Entity> entity, Edit&& edit) {
	T* component = EntityManager::Get<T>(entity);
	const ComponentEditor* editor = ComponentEditorRegistry::Find<T>();
	if (component == nullptr || editor == nullptr) {
		return;
	}
	const std::vector<std::byte> before = editor->Save(component);
	edit(*component);
	EditorHistory::RecordComponentChange(entity, *editor, before, editor->Save(component));
	EditorHistory::CommitComponentChanges();
}

// ===== Entity の、Timeline のファイルを選ぶ欄を全部集める =====
std::vector<TimelineSource> CollectSources(Handle<Entity> entity) {
	std::vector<TimelineSource> sources;
	if (const TimelineComponent* timeline = EntityManager::Get<TimelineComponent>(entity)) {
		sources.push_back({"Timeline", timeline->timeline, TimelineLayer::Component, [entity](uint32_t handle) {
			                   EditComponent<TimelineComponent>(entity, [handle](TimelineComponent& value) { value.timeline = handle; });
		                   }});
	}
	if (const UIScreenComponent* screen = EntityManager::Get<UIScreenComponent>(entity)) {
		sources.push_back({"UI Screen / Show", screen->showTimeline, TimelineLayer::UIScreen, [entity](uint32_t handle) {
			                   EditComponent<UIScreenComponent>(entity, [handle](UIScreenComponent& value) { value.showTimeline = handle; });
		                   }});
		sources.push_back({"UI Screen / Hide", screen->hideTimeline, TimelineLayer::UIScreen, [entity](uint32_t handle) {
			                   EditComponent<UIScreenComponent>(entity, [handle](UIScreenComponent& value) { value.hideTimeline = handle; });
		                   }});
	}
	if (const UIStateComponent* ui = EntityManager::Get<UIStateComponent>(entity)) {
		for (size_t i = 0; i < kUIElementStateCount; ++i) {
			const std::string label = std::format("UI State / {}", magic_enum::enum_name(static_cast<UIElementState>(i)));
			sources.push_back({label, ui->stateTimelines[i], TimelineLayer::UIState, [entity, i](uint32_t handle) {
				                   EditComponent<UIStateComponent>(entity, [handle, i](UIStateComponent& value) { value.stateTimelines[i] = handle; });
			                   }});
		}
		sources.push_back({"UI State / Show", ui->showTimeline, TimelineLayer::UIVisibility, [entity](uint32_t handle) {
			                   EditComponent<UIStateComponent>(entity, [handle](UIStateComponent& value) { value.showTimeline = handle; });
		                   }});
		sources.push_back({"UI State / Hide", ui->hideTimeline, TimelineLayer::UIVisibility, [entity](uint32_t handle) {
			                   EditComponent<UIStateComponent>(entity, [handle](UIStateComponent& value) { value.hideTimeline = handle; });
		                   }});
	}
	return sources;
}

// ===== New で作るファイルの場所（Entity の名前_欄の最後の名前.timeline。"UI Screen / Show" なら Show。使われていれば後ろに番号）=====
std::filesystem::path MakeNewTimelinePath(const std::string& ownerName, const std::string& label) {
	constexpr std::string_view kSeparator = " / "; // 欄の名前の区切り（"UI Screen / Show"）
	const size_t separator = label.rfind(kSeparator);
	const std::string_view leaf = (separator == std::string::npos) ? std::string_view(label) : std::string_view(label).substr(separator + kSeparator.size());
	std::string base = std::format("{}_{}", ownerName, leaf);
	// ファイルの名前に使えない文字と空白は _ にする
	std::ranges::replace_if(base, [](char c) { return std::string_view(" /\\:*?\"<>|").find(c) != std::string_view::npos; }, '_');
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::string name = (number == 0) ? base : std::format("{}{}", base, number);
		const std::filesystem::path path = std::filesystem::path(kNewTimelineFolder) / ToPath(name + std::string(TimelineManager::kFileExtension));
		if (!std::filesystem::exists(path, error)) {
			return path;
		}
	}
}

//=============================================================================
// 開く
//=============================================================================
// ===== ファイルを開く =====
void OpenFile(uint32_t handle) {
	state.handle = handle;
	state.selection = {};
	state.curveEditor = CurveEditor{};
	state.playhead.Stop();
	state.time = 0.0f;
	state.isPreview = false;
	state.needsFit = true;
	state.hasEdit = false;
	state.committedText = TimelineManager::ToText(handle);
	state.seenRevision = TimelineManager::GetRevision(handle);
}

// ===== プレビューをやめる（次のフレームから要求を出さない＝値は元のまま）=====
void StopPreview() {
	state.playhead.Stop();
	state.time = 0.0f;
	state.isPreview = false;
}

// ===== 開く物と、再生する Entity を決める =====
void ResolveOwner() {
	// --- 詳細の Preset の Open：今の物を Back に積んで、プリセットを対象の上で開く ---
	if (state.openRequest != 0) {
		state.backStack.push_back({state.handle, state.ownerId, state.source, state.isExplicitOwner});
		OpenFile(std::exchange(state.openRequest, 0));
		state.ownerId = state.openOwnerId;
		state.source = -1;
		state.isExplicitOwner = true;
	}
	// --- Hierarchy で Timeline の欄のある Entity を選んだら、それを開く ---
	// Component が付くのを待てるように、付いていない間は覚えない（画面（UI Screen）の一番上は、選ばれた次のフレームに Component が付く）
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	const std::vector<TimelineSource> selectedSources = CollectSources(selected);
	if (selectedSources.empty()) {
		lastSelected = {};
	} else if (selected != lastSelected) {
		lastSelected = selected;
		state.ownerId = EntityManager::Get(selected)->id;
		state.isExplicitOwner = false;
		state.backStack.clear();
		// 開いているファイルが欄にあれば、そのまま。無ければ、ファイルの入っている最初の欄（全部空なら最初の欄）
		const auto isOpened = [](const TimelineSource& source) { return source.handle != 0 && source.handle == state.handle; };
		const auto hasFile = [](const TimelineSource& source) { return source.handle != 0; };
		auto chosen = std::ranges::find_if(selectedSources, isOpened);
		chosen = (chosen != selectedSources.end()) ? chosen : std::ranges::find_if(selectedSources, hasFile);
		chosen = (chosen != selectedSources.end()) ? chosen : selectedSources.begin();
		state.source = static_cast<int>(chosen - selectedSources.begin());
		if (chosen->handle != state.handle) {
			OpenFile(chosen->handle);
		}
	}
	// --- 番号から今の Handle を引いて、欄を集め直す ---
	const Handle<Entity> found = EntityManager::FindById(state.ownerId);
	state.sources = CollectSources(found);
	if (state.source >= static_cast<int>(state.sources.size())) {
		state.source = -1;
	}
	// --- 開いている欄のファイルが変わった（Inspector で選び直した・New で作った・Undo）：開き直す ---
	if (state.source >= 0 && state.sources[state.source].handle != state.handle) {
		OpenFile(state.sources[state.source].handle);
	}
	// --- 別のシーンの同じ番号の Entity と間違えないように、欄にそのファイルがあるかも見る（Preset は対象の上で開いたので見ない）---
	const bool isReferenced = std::ranges::any_of(state.sources, [](const TimelineSource& source) { return source.handle != 0 && source.handle == state.handle; });
	state.owner = (state.handle != 0 && (isReferenced || state.isExplicitOwner)) ? found : Handle<Entity>{};
	// --- 再生する Entity が居なくなった（シーンの切り替え・消した）---
	if (!state.owner.IsValid()) {
		state.isPreview = false;
	}
	// --- 画面（UI Screen）の編集に入る・出るときは切る（中身が入れ替わる。編集中は画面の中の Entity を選べばプレビューできる）---
	const Handle<Entity> screenRoot = UIScreenEditor::GetRoot();
	if (screenRoot != lastScreenRoot) {
		lastScreenRoot = screenRoot;
		StopPreview();
	}
}

// ===== Undo で中身が変わっていたら、知っている中身を合わせる =====
void SyncRevision() {
	const uint32_t revision = TimelineManager::GetRevision(state.handle);
	if (revision != state.seenRevision) {
		state.seenRevision = revision;
		state.committedText = TimelineManager::ToText(state.handle);
		state.hasEdit = false;
		state.ClampSelection();
		state.curveEditor = CurveEditor{}; // カーブの中身も入れ替わったので、見ている範囲を合わせ直す（前の範囲のままだと、戻ったキーが絵の外に出て見えない）
	}
}

// ===== 再生位置を進めて、プレビューを頼む =====
void UpdateTime(const Timeline& timeline) {
	const SceneManager* sceneManager = SceneManager::GetMain();
	const bool isGameRunning = sceneManager != nullptr && sceneManager->GetPlayState() != PlayState::Editing;
	if (isGameRunning && !state.isGameRunning) {
		StopPreview(); // Play を押した（このフレームから要求を出さないので、Play の退避は元の値を写す）
	}
	state.isGameRunning = isGameRunning;
	state.duration = timeline.GetDuration();

	// --- Play 中：TimelineSystem の時間を見せる（その層で別のファイルを再生していれば 0）---
	if (isGameRunning) {
		const TimelineStatus status = TimelineSystem::GetStatus(state.owner, state.GetLayer());
		state.time = (status.timeline == state.handle) ? status.time : 0.0f;
		return;
	}
	// --- 停止中：ウィンドウの中で時間を進める ---
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(state.owner);
	const bool usesComponentSpeed = component != nullptr && state.GetLayer() == TimelineLayer::Component;
	const float speed = (usesComponentSpeed && component->speed > 0.0f) ? component->speed : 1.0f;
	if (state.playhead.GetState() == TimelinePlayState::Playing) {
		state.playhead.Advance(ImGui::GetIO().DeltaTime * speed, state.duration, state.isLoop, passedRanges);
		state.time = state.playhead.GetTime();
	}
	state.time = (std::min)(state.time, state.duration); // 縮めた長さの外へ出ない
	if (state.isPreview && state.owner.IsValid()) {
		TimelineSystem::RequestPreview(state.owner, state.handle, state.time);
	}
}

//=============================================================================
// 描く
//=============================================================================
// ===== 押している間は色を変えるボタン =====
bool ToggleButton(const char* label, bool isOn, const char* tooltip) {
	if (isOn) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
	}
	const bool isPressed = ImGui::Button(label);
	if (isOn) {
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip("%s", tooltip);
	return isPressed;
}

// ===== 欄を選ぶ・空の欄にファイルを作る・Back =====
void DrawSourceBar() {
	// --- Back（Preset を開く前に戻る）---
	if (!state.backStack.empty()) {
		if (ImGui::Button("Back")) {
			const TimelineBackItem back = state.backStack.back();
			state.backStack.pop_back();
			OpenFile(back.handle);
			state.ownerId = back.ownerId;
			state.source = back.source;
			state.isExplicitOwner = back.isExplicitOwner;
		}
		ImGui::SetItemTooltip("Preset を開く前に開いていた Timeline に戻る");
		ImGui::SameLine();
	}
	const Entity* owner = EntityManager::Get(EntityManager::FindById(state.ownerId));
	if (state.sources.empty() || owner == nullptr) {
		return;
	}
	// --- 欄の一覧（欄の名前 と ファイルの名前）---
	const auto describe = [](const TimelineSource& source) {
		const std::string& path = TimelineManager::GetPath(source.handle);
		return std::format("{}  ({})", source.label, path.empty() ? std::string("none") : ToUtf8(ToPath(path).filename()));
	};
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", owner->name.c_str());
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kSourceFieldWidth);
	const std::string current = (state.source >= 0) ? describe(state.sources[state.source]) : std::string("(Project から開いたファイル)");
	if (ImGui::BeginCombo("##source", current.c_str())) {
		for (size_t i = 0; i < state.sources.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(describe(state.sources[i]).c_str(), static_cast<int>(i) == state.source)) {
				state.source = static_cast<int>(i);
				state.isExplicitOwner = false;
				OpenFile(state.sources[i].handle);
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("この Entity の、Timeline を選ぶ欄（Timeline Component・UI Screen・UI State）。選ぶと、その欄のファイルを開く");
	// --- 空の欄：New で作って入れる（停止中だけ。Play 中に入れても Stop で戻る）---
	if (state.source >= 0 && state.sources[state.source].handle == 0) {
		ImGui::SameLine();
		ImGui::BeginDisabled(state.isGameRunning);
		if (ImGui::Button("New")) {
			const std::filesystem::path path = MakeNewTimelinePath(owner->name, state.sources[state.source].label);
			if (TimelineManager::CreateNewFile(path)) {
				const uint32_t handle = TimelineManager::Load(path);
				state.sources[state.source].assign(handle); // Inspector で選んだのと同じ（Undo で外れる）
				OpenFile(handle);
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s に新しい Timeline を作って、この欄に入れる", kNewTimelineFolder);
	}
}

// ===== 再生・一時停止・停止・ループ =====
void DrawTransport() {
	// Play 中に動かせるのは Timeline Component の再生だけ（UI State・UI Screen は、その係が再生する）
	const bool isComponentLayer = state.GetLayer() == TimelineLayer::Component;
	const bool canPlay = state.owner.IsValid() && (!state.isGameRunning || isComponentLayer);
	const TimelineStatus status = TimelineSystem::GetStatus(state.owner, state.GetLayer());
	const bool isPlaying = state.isGameRunning ? (status.IsPlaying() && status.timeline == state.handle) : state.playhead.GetState() == TimelinePlayState::Playing;
	ImGui::BeginDisabled(!canPlay);
	if (ToolIcons::Button("play", ToolIcon::Play, isPlaying, state.isGameRunning ? "この Entity の Timeline を再生する" : "プレビューを再生する（イベントは呼ばない）")) {
		if (state.isGameRunning) {
			TimelineSystem::Play(state.owner);
		} else {
			state.playhead.Seek(state.time, state.duration);
			state.playhead.Play();
			state.isPreview = true;
		}
	}
	ImGui::SameLine();
	if (ToolIcons::Button("pause", ToolIcon::Pause, false, "一時停止")) {
		if (state.isGameRunning) {
			TimelineSystem::Pause(state.owner);
		} else {
			state.playhead.Pause();
		}
	}
	ImGui::SameLine();
	if (ToolIcons::Button("stop", ToolIcon::Stop, false, state.isGameRunning ? "止めて最初へ戻す" : "プレビューをやめて最初へ戻す（画面は元の値へ戻る）")) {
		if (state.isGameRunning) {
			TimelineSystem::Stop(state.owner);
		} else {
			StopPreview();
		}
	}
	ImGui::SameLine();
	// --- ループ：Play 中は Component の値（Stop で元へ戻る）、停止中はプレビューだけ ---
	TimelineComponent* component = isComponentLayer ? EntityManager::Get<TimelineComponent>(state.owner) : nullptr;
	const bool isLoop = (state.isGameRunning && component != nullptr) ? component->loop : state.isLoop;
	if (ToggleButton("Loop", isLoop, state.isGameRunning ? "最後まで行ったら最初へ戻す（Play の間だけ。Stop で元へ戻る）" : "プレビューをループする（保存しない）")) {
		if (state.isGameRunning && component != nullptr) {
			component->loop = !component->loop;
		} else {
			state.isLoop = !state.isLoop;
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(state.isGameRunning);
	if (ToggleButton("Preview", state.isPreview, "停止中に、再生位置の値を画面に出す（切ると元の値へ戻る。Inspector・保存・Undo はいつも元の値を見る）")) {
		state.isPreview = !state.isPreview;
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();
}

// ===== 上の並び =====
void DrawToolbar(Timeline& timeline) {
	DrawTransport();
	// --- 再生位置 ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kTimeFieldWidth);
	float time = state.time;
	if (ImGui::DragFloat("##time", &time, 0.01f, 0.0f, state.duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp)) {
		state.SeekTo(time);
	}
	ImGui::SetItemTooltip("再生位置（秒）");
	// --- 長さ ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kLengthFieldWidth);
	if (ImGui::DragFloat("##length", &timeline.length, 0.01f, 0.0f, Curve::kMaxValue, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("長さ（秒）。クリップがこれより長ければ、クリップの終わりまで再生する");
	// --- スナップ ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kSnapFieldWidth);
	const auto current = std::ranges::find(kSnapChoices, state.snapStep, &SnapChoice::step);
	if (ImGui::BeginCombo("##snap", (current != kSnapChoices.end()) ? current->name : "Off")) {
		for (const SnapChoice& choice : kSnapChoices) {
			if (ImGui::Selectable(choice.name, choice.step == state.snapStep)) {
				state.snapStep = choice.step;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かすときに目盛りへそろえる間隔（Ctrl を押している間はそろえない）");
	ImGui::SameLine();
	if (ImGui::Button("Fit")) {
		state.needsFit = true;
	}
	ImGui::SetItemTooltip("全体が見える範囲にする（F）");
	// --- 開いている物 ---
	ImGui::SameLine();
	const Entity* owner = EntityManager::Get(state.owner);
	const std::string& path = TimelineManager::GetPath(state.handle);
	ImGui::TextDisabled("%s  /  %s", ToUtf8(ToPath(path).filename()).c_str(), (owner != nullptr) ? owner->name.c_str() : "再生する Entity なし");
	ImGui::SetItemTooltip("%s", (owner != nullptr) ? path.c_str() : "Timeline の欄でこのファイルを選んだ Entity を Hierarchy で選ぶと、プレビューできる");
}

// ===== 詳細との境目（つまんで高さを変える）=====
void DrawSplitter(float rowsHeight, float maxRowsHeight) {
	ImGui::InvisibleButton("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, kSplitterHeight));
	const bool isHovered = ImGui::IsItemHovered();
	const bool isActive = ImGui::IsItemActive();
	if (isHovered || isActive) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (isActive) {
		// 見えている高さから動かす（トラックが少なくて上限より低く出ていても、すぐ動く）
		rowsLimit = std::clamp(rowsHeight + ImGui::GetIO().MouseDelta.y, kMinRowsHeight, maxRowsHeight);
	}
	// --- つまめる場所だと分かるように線を引く ---
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const float lineY = (min.y + max.y) * 0.5f;
	const ImU32 lineColor = ImGui::GetColorU32(isActive ? ImGuiCol_SeparatorActive : (isHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
	ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, lineY), ImVec2(max.x, lineY), lineColor, 2.0f);
}

// ===== 操作が終わったら、保存して Undo に積む =====
void CommitEdit() {
	if (!state.hasEdit || ImGui::IsAnyItemActive()) {
		return;
	}
	state.hasEdit = false;
	std::string after = TimelineManager::ToText(state.handle);
	if (after == state.committedText) {
		return;
	}
	TimelineManager::Save(state.handle);
	const auto apply = [handle = state.handle](const std::string& text) { return TimelineManager::ApplyText(handle, text) && TimelineManager::Save(handle); };
	EditorHistory::Record([apply, before = std::move(state.committedText)] { return apply(before); }, [apply, value = after] { return apply(value); });
	state.committedText = std::move(after);
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
void TimelineWindow::Draw() {
	ImGui::SetNextWindowSize(ImVec2(kFirstWidth, kFirstHeight), ImGuiCond_FirstUseEver);
	if (std::exchange(needsFocus, false)) {
		ImGui::SetNextWindowFocus();
	}
	// ウィンドウの中のスクロールは部品ごとに持つ。隠れていると Begin が false で何も頼まないので、プレビューは止まり値は元のまま
	if (ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		ResolveOwner();
		SyncRevision();
		DrawSourceBar();
		if (Timeline* timeline = state.GetTimeline()) {
			UpdateTime(*timeline);
			DrawToolbar(*timeline);
			DrawTimelineRuler(state);
			// --- 一覧は行の分だけ（1行は空けて、足す場所を残す）。残りは全部、詳細に使う ---
			const float available = ImGui::GetContentRegionAvail().y;
			const float maxRowsHeight = (std::max)(available - kMinDetailsHeight - kTimelineScrollbarHeight - kSplitterHeight, kMinRowsHeight);
			const float fitHeight = static_cast<float>(timeline->tracks.size() + 1) * kTimelineRowHeight;
			const float rowsHeight = std::clamp((std::min)(rowsLimit, fitHeight), kMinRowsHeight, maxRowsHeight);
			DrawTimelineRows(state, rowsHeight);
			DrawTimelineScrollbar(state);
			DrawSplitter(rowsHeight, maxRowsHeight);
			DrawTimelineDetails(state, ImGui::GetContentRegionAvail().y);
			CommitEdit();
		} else if (!state.sources.empty()) {
			ImGui::TextDisabled("この欄にはまだファイルがありません。New で作るか、Inspector で .timeline を選ぶ");
		} else {
			ImGui::TextDisabled("Hierarchy で Timeline の欄（Timeline・UI Screen・UI State）のある Entity を選ぶか、Project で .timeline をダブルクリックする");
		}
	}
	ImGui::End();
}

void TimelineWindow::Open(const std::filesystem::path& filePath) {
	const uint32_t handle = TimelineManager::Load(filePath);
	if (handle == 0) {
		return;
	}
	if (handle != state.handle) {
		OpenFile(handle);
	}
	// 同じファイルを欄で選んでいる Entity があれば、それをプレビューに使う
	state.ownerId = 0;
	state.source = -1;
	state.isExplicitOwner = false;
	state.backStack.clear();
	const auto findOwner = [handle](Handle<Entity> entity) {
		if (state.ownerId != 0) {
			return;
		}
		const std::vector<TimelineSource> sources = CollectSources(entity);
		const auto found = std::ranges::find(sources, handle, &TimelineSource::handle);
		if (found != sources.end()) {
			state.ownerId = EntityManager::Get(entity)->id;
			state.source = static_cast<int>(found - sources.begin());
		}
	};
	EntityManager::ForEach<TimelineComponent>([&](Handle<Entity> entity, TimelineComponent&) { findOwner(entity); });
	EntityManager::ForEach<UIScreenComponent>([&](Handle<Entity> entity, UIScreenComponent&) { findOwner(entity); });
	EntityManager::ForEach<UIStateComponent>([&](Handle<Entity> entity, UIStateComponent&) { findOwner(entity); });
	needsFocus = true;
}
#endif