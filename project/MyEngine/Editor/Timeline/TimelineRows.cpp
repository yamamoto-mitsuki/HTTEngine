#ifdef USE_IMGUI
#include <cmath>
#include <format>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>

#include <externals/imgui/imgui.h>

#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/Camera/TimelineCameraTrack.h"

// Timeline ウィンドウの真ん中（時間の目盛り・トラックの一覧・時間の列・スクロールバー）

namespace {
constexpr const char* kTrackDragType = "TIMELINE_TRACK"; // トラックの行をドラッグで運ぶときの種類の名前
constexpr float kClipLabelPadding = 6.0f;                // クリップの左端と名前のすき間
constexpr float kRulerHeight = 24.0f;                    // 時間の目盛りの高さ
constexpr float kTickMinPixels = 60.0f;                  // 目盛りの間隔の下限（ピクセル）
constexpr int kMaxTicks = 256;                           // 目盛りの線の数の上限
constexpr float kMuteButtonWidth = 26.0f;                // M ボタンの幅
constexpr float kClipPadding = 4.0f;                     // 行の上下とクリップのすき間
constexpr float kClipRounding = 3.0f;                    // クリップの角の丸み
constexpr float kKeySize = 5.0f;                         // キーの印の大きさ
constexpr float kEventSize = 7.0f;                       // イベントの印の大きさ
constexpr float kKeyHitDistance = 7.0f;                  // キー・イベントをつかめる距離
constexpr float kEndHitWidth = 6.0f;                     // クリップの右端をつかめる幅
constexpr float kPlayheadWidth = 6.0f;                   // 目盛りの上の再生位置の三角の幅
constexpr float kZoomStep = 1.15f;                       // ホイール1目盛りの拡大率
constexpr float kWheelPanPixels = 40.0f;                 // Shift + ホイール1目盛りで動かすピクセル
constexpr float kScrollMargin = 1.1f;                    // スクロールできる長さ（全体に対する割合。右に少し余白を残す）
constexpr float kMinThumbWidth = 20.0f;                  // スクロールバーのつまみの幅の下限
constexpr float kGridAlpha = 0.18f;                      // 目盛りの線の濃さ（灰色の文字の色に掛ける）
constexpr float kMutedAlpha = 0.35f;                     // Mute のトラックの濃さ

// 背景・目盛りはテーマの色を使う（エディタの色はリニアなので、暗い色を IM_COL32 で書くと明るく出る）
constexpr ImU32 kClipColor = IM_COL32(60, 130, 170, 255);     // クリップ
constexpr ImU32 kSelectedColor = IM_COL32(255, 160, 40, 255); // 選んでいる物（選んだ物の枠と同じ橙）
constexpr ImU32 kPlayheadColor = IM_COL32(240, 90, 90, 255);  // 再生位置
constexpr ImU32 kProblemColor = IM_COL32(240, 110, 110, 255); // 結び付けの問題
constexpr ImU32 kFadeColor = IM_COL32(0, 0, 0, 90);           // クリップの補間の部分（斜めの線の外を暗くする）
constexpr ImU32 kFadeOutColor = IM_COL32(60, 130, 170, 110);  // クリップの終わった後の補間

/// <summary>
/// 列の上のマウスの下にある物
/// </summary>
struct LaneHit {
	int track = -1;       // 行（無ければ -1）
	int clip = -1;        // クリップ（無ければ -1）
	bool isKey = false;   // キーの印
	float keyTime = 0.0f; // キーの時間（クリップの始まりからの秒）
	bool isEnd = false;   // クリップの右端
};

// 右クリックのメニューが相手にする物（開いた後はマウスが動くので、開いたときに覚える）
LaneHit menuHit;
float menuTime = 0.0f;

// ===== ひし形を描く =====
void DrawDiamond(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color) {
	drawList->AddQuadFilled(ImVec2(center.x, center.y - size), ImVec2(center.x + size, center.y), ImVec2(center.x, center.y + size), ImVec2(center.x - size, center.y), color);
}

// ===== 見えている時間の目盛りを順に渡す =====
// float の足し算で進めると大きな場所で止まらなくなるので、番号から計算する
template<class Function> void ForEachTick(const TimelineTimeView& view, float width, Function&& function) {
	const float step = view.CalcTickStep(kTickMinPixels);
	const float first = std::ceil(view.scrollTime / step);
	for (int i = 0; i < kMaxTicks; ++i) {
		const float time = (first + static_cast<float>(i)) * step;
		const float x = view.ToPixel(time);
		if (x > width) {
			break;
		}
		function(time, x);
	}
}

// ===== クリップを足す（対象の今の値で平らなキーも入れる）=====
void AddClipAt(TimelineEditorState& state, int trackIndex, float time) {
	TimelineTrack& track = *state.GetTimeline()->tracks[trackIndex];
	const float start = (std::max)(state.Snap(time), 0.0f);
	const size_t clip = track.AddClip(start, TimelineEdit::CalcNewClipDuration(track, start));
	FillNewClipCurves(state, track, clip);
	state.SelectClip(trackIndex, static_cast<int>(clip));
	state.MarkEdited();
}

// ===== キーを足す（形は変えない）=====
void AddKeyAt(TimelineEditorState& state, int trackIndex, int clipIndex, float time) {
	TimelineTrack& track = *state.GetTimeline()->tracks[trackIndex];
	const float start = track.GetClipStart(clipIndex);
	const float keyTime = std::clamp(state.Snap(time) - start, 0.0f, track.GetClipDuration(clipIndex));
	TimelineEdit::AddKeys(track.GetClipCurves(clipIndex), keyTime);
	state.SelectKey(trackIndex, clipIndex, keyTime);
	state.MarkEdited();
}

// ===== カメラのクリップを足す（Hierarchy からドラッグした Camera。カメラのトラックでなければ何もしない）=====
void AddCameraClipAt(TimelineEditorState& state, int trackIndex, float time, Handle<Entity> camera) {
	auto* track = dynamic_cast<TimelineCameraTrack*>(state.GetTimeline()->tracks[trackIndex].get());
	if (track == nullptr || EntityManager::Get<CameraComponent>(camera) == nullptr) {
		return;
	}
	AddClipAt(state, trackIndex, time);
	track->clips[state.selection.clip].camera = EntityManager::MakeBindingPath(state.owner, camera);
}

// ===== 選んでいる物を消す（キー → クリップ → トラックの順に、一番細かい物）=====
void DeleteSelected(TimelineEditorState& state) {
	Timeline* timeline = state.GetTimeline();
	TimelineTrack* track = state.GetSelectedTrack();
	if (track == nullptr) {
		return;
	}
	TimelineSelection& selection = state.selection;
	if (selection.clip >= 0 && selection.hasKey) {
		TimelineEdit::RemoveKeys(track->GetClipCurves(selection.clip), selection.keyTime);
		selection.hasKey = false;
	} else if (selection.clip >= 0) {
		track->RemoveClip(selection.clip);
		state.SelectTrack(selection.track);
	} else {
		TimelineEdit::RemoveTrack(*timeline, selection.track);
		state.SelectTrack(-1);
	}
	state.MarkEdited();
}

//=============================================================================
// トラックの一覧（左）
//=============================================================================
// ===== 行のドロップ（トラックの並べ替え・Hierarchy の Entity を対象にする）=====
void AcceptRowDrop(TimelineEditorState& state, int index, int& moveFrom, int& moveTo) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTrackDragType)) {
		moveFrom = *static_cast<const int*>(payload->Data);
		moveTo = index;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
		const Handle<Entity> entity = *static_cast<const Handle<Entity>*>(payload->Data);
		TimelineTrack& track = *state.GetTimeline()->tracks[index];
		if (track.UsesTarget()) {
			SetTimelineTarget(state, track, entity);
		} else {
			AddCameraClipAt(state, index, state.time, entity);
		}
	}
	ImGui::EndDragDropTarget();
}

// ===== 行の右クリックのメニュー =====
void DrawRowMenu(TimelineEditorState& state, int index, int& moveFrom, int& moveTo, int& deleteIndex) {
	if (!ImGui::BeginPopupContextItem("##rowMenu")) {
		return;
	}
	const int count = static_cast<int>(state.GetTimeline()->tracks.size());
	if (ImGui::MenuItem("Add Clip At Playhead")) {
		AddClipAt(state, index, state.time);
	}
	if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) {
		moveFrom = index;
		moveTo = index - 1;
	}
	if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < count)) {
		moveFrom = index;
		moveTo = index + 1;
	}
	if (ImGui::MenuItem("Delete Track")) {
		deleteIndex = index; // 行を描いている途中なので、ここでは消さない
	}
	ImGui::EndPopup();
}

// ===== 1行 =====
void DrawTrackRow(TimelineEditorState& state, int index, const ImVec2& rowMin, int& moveFrom, int& moveTo, int& deleteIndex) {
	Timeline& timeline = *state.GetTimeline();
	TimelineTrack& track = *timeline.tracks[index];
	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::PushID(index); // トラックは同じ名前でもよいので、番号で分ける
	ImGui::SetCursorScreenPos(rowMin);
	const float rowWidth = kTimelineListWidth - kMuteButtonWidth - style.ItemSpacing.x * 2.0f;
	if (ImGui::Selectable("##row", state.selection.track == index, ImGuiSelectableFlags_AllowOverlap, ImVec2(rowWidth, kTimelineRowHeight - style.ItemSpacing.y))) {
		state.SelectTrack(index);
	}
	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload(kTrackDragType, &index, sizeof(index));
		ImGui::TextUnformatted(track.name.c_str());
		ImGui::EndDragDropSource();
	}
	AcceptRowDrop(state, index, moveFrom, moveTo);
	DrawRowMenu(state, index, moveFrom, moveTo, deleteIndex);
	// --- 問題があれば、マウスを乗せると出す ---
	const std::string problem = FindTimelineBindingProblem(state, track);
	if (!problem.empty()) {
		ImGui::SetItemTooltip("%s", problem.c_str());
	}
	// --- 名前（Selectable の上に書く）---
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 textPosition(rowMin.x + style.ItemSpacing.x, rowMin.y + (kTimelineRowHeight - ImGui::GetTextLineHeight()) * 0.5f);
	const ImU32 textColor = !problem.empty() ? kProblemColor : ImGui::GetColorU32(track.isMuted ? ImGuiCol_TextDisabled : ImGuiCol_Text);
	drawList->PushClipRect(rowMin, ImVec2(rowMin.x + rowWidth, rowMin.y + kTimelineRowHeight), true);
	drawList->AddText(textPosition, textColor, track.name.c_str());
	drawList->PopClipRect();
	// --- Mute ---
	ImGui::SameLine(0.0f, style.ItemSpacing.x);
	const bool wasMuted = track.isMuted; // 押すと変わるので、Push と Pop の数がそろうように覚えておく
	if (wasMuted) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
	}
	if (ImGui::Button("M", ImVec2(kMuteButtonWidth, 0.0f))) {
		track.isMuted = !track.isMuted;
		state.MarkEdited();
	}
	if (wasMuted) {
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip("このトラックを動かさない");
	ImGui::PopID();
}

//=============================================================================
// 時間の列（右）
//=============================================================================
/// <summary>
/// 列の場所（ボタンの左上と、行の数）
/// </summary>
struct LaneArea {
	ImVec2 min;
	ImVec2 size;
};

// ===== クリップの横の範囲 =====
float ClipLeft(const TimelineEditorState& state, const LaneArea& area, const TimelineTrack& track, size_t clip) { return area.min.x + state.view.ToPixel(track.GetClipStart(clip)); }
float ClipRight(const TimelineEditorState& state, const LaneArea& area, const TimelineTrack& track, size_t clip) {
	return area.min.x + state.view.ToPixel(track.GetClipStart(clip) + track.GetClipDuration(clip));
}

// ===== マウスの下にある物 =====
LaneHit FindHit(const TimelineEditorState& state, const LaneArea& area, const ImVec2& mouse) {
	const Timeline& timeline = *state.GetTimeline();
	LaneHit hit;
	const int row = static_cast<int>(std::floor((mouse.y - area.min.y) / kTimelineRowHeight));
	if (row < 0 || row >= static_cast<int>(timeline.tracks.size())) {
		return hit;
	}
	hit.track = row;
	TimelineTrack& track = *timeline.tracks[row];
	// --- 右端のすぐ外は長さを変える（終わりにキーがあっても、後ろのクリップの下に隠れていても、ここならつかめる）---
	for (int clip = static_cast<int>(track.GetClipCount()) - 1; clip >= 0; --clip) {
		const float right = ClipRight(state, area, track, clip);
		if (track.HasClipDuration() && mouse.x > right && mouse.x <= right + kEndHitWidth) {
			hit.clip = clip;
			hit.isEnd = true;
			return hit;
		}
	}
	// --- 上に描かれている物（後ろのクリップ）から調べる ---
	for (int clip = static_cast<int>(track.GetClipCount()) - 1; clip >= 0; --clip) {
		const float left = ClipLeft(state, area, track, clip);
		const float right = ClipRight(state, area, track, clip);
		// キーはクリップの外へはみ出して描くことがあるので、範囲の前に調べる
		const std::span<Curve> curves = track.GetClipCurves(clip);
		for (const float keyTime : TimelineEdit::CollectKeyTimes(curves)) {
			if (std::abs(left + keyTime * state.view.pixelsPerSecond - mouse.x) <= kKeyHitDistance) {
				hit.clip = clip;
				hit.isKey = true;
				hit.keyTime = keyTime;
				return hit;
			}
		}
		if (!track.HasClipDuration()) {
			if (std::abs(left - mouse.x) <= kKeyHitDistance) {
				hit.clip = clip;
				return hit;
			}
			continue;
		}
		if (mouse.x >= left && mouse.x <= right) {
			hit.clip = clip;
			hit.isEnd = mouse.x >= right - kEndHitWidth;
			return hit;
		}
	}
	return hit;
}

// ===== 拡大縮小とスクロール =====
void HandleView(TimelineEditorState& state, const LaneArea& area) {
	const ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
		if (io.KeyCtrl) {
			ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * kTimelineRowHeight); // 縦に動かす
		} else if (io.KeyShift) {
			state.view.Pan(-io.MouseWheel * kWheelPanPixels); // 横に動かす
		} else {
			state.view.ZoomAt(io.MousePos.x - area.min.x, std::pow(kZoomStep, io.MouseWheel));
		}
	}
	// --- 中ボタンで動かす ---
	if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
		state.view.Pan(-io.MouseDelta.x);
		ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
	}
}

// ===== 押した物をつまむ =====
void BeginDrag(TimelineEditorState& state, const LaneHit& hit, float mouseTime) {
	state.drag = TimelineDrag::None;
	if (hit.clip < 0) {
		return;
	}
	TimelineTrack& track = *state.GetTimeline()->tracks[hit.track];
	const float start = track.GetClipStart(hit.clip);
	if (hit.isKey) {
		state.drag = TimelineDrag::Key;
		state.dragKeyTime = hit.keyTime;
		state.dragOffset = mouseTime - (start + hit.keyTime);
		const std::span<Curve> curves = track.GetClipCurves(hit.clip);
		state.dragCurves.assign(curves.begin(), curves.end());
	} else if (hit.isEnd) {
		state.drag = TimelineDrag::ClipEnd;
		state.dragOffset = mouseTime - (start + track.GetClipDuration(hit.clip));
	} else {
		state.drag = TimelineDrag::Clip;
		state.dragOffset = mouseTime - start;
	}
}

// ===== つまんでいる物を動かす =====
void UpdateDrag(TimelineEditorState& state, float mouseTime) {
	TimelineTrack* track = state.GetSelectedTrack();
	TimelineSelection& selection = state.selection;
	if (track == nullptr || selection.clip < 0) {
		state.drag = TimelineDrag::None;
		return;
	}
	const float target = state.Snap(mouseTime - state.dragOffset);
	const float start = track->GetClipStart(selection.clip);
	const float duration = track->GetClipDuration(selection.clip);
	switch (state.drag) {
	case TimelineDrag::None:
		return;
	case TimelineDrag::Clip:
		selection.clip = static_cast<int>(track->SetClipTime(selection.clip, (std::max)(target, 0.0f), duration));
		break;
	case TimelineDrag::ClipEnd:
		selection.clip = static_cast<int>(track->SetClipTime(selection.clip, start, (std::max)(target - start, TimelineEdit::kMinClipDuration)));
		break;
	case TimelineDrag::Key: {
		const std::span<Curve> curves = track->GetClipCurves(selection.clip);
		std::ranges::copy(state.dragCurves, curves.begin());
		selection.keyTime = std::clamp(target - start, 0.0f, duration);
		TimelineEdit::MoveKeys(curves, state.dragKeyTime, selection.keyTime);
		break;
	}
	}
	state.MarkEdited();
}

// ===== マウス（選ぶ・つまむ・足す）=====
void HandleMouse(TimelineEditorState& state, const LaneArea& area) {
	const ImGuiIO& io = ImGui::GetIO();
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		state.drag = TimelineDrag::None;
	}
	if (!ImGui::IsMousePosValid()) {
		return;
	}
	const float mouseTime = state.view.ToTime(io.MousePos.x - area.min.x);
	const bool isHovered = ImGui::IsItemHovered();
	// --- 右端の上では、マウスの形で長さを変えられると知らせる ---
	if (state.drag == TimelineDrag::ClipEnd || (isHovered && FindHit(state, area, io.MousePos).isEnd)) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	// --- 左：選ぶ・つまむ。ダブルクリックは足す ---
	if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const LaneHit hit = FindHit(state, area, io.MousePos);
		if (hit.isKey) {
			state.SelectKey(hit.track, hit.clip, hit.keyTime);
		} else {
			state.SelectClip(hit.track, hit.clip);
		}
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hit.track >= 0) {
			state.drag = TimelineDrag::None; // この操作は「足す」だけ
			if (hit.clip < 0) {
				AddClipAt(state, hit.track, mouseTime);
			} else if (!hit.isKey && !state.GetTimeline()->tracks[hit.track]->GetClipCurves(hit.clip).empty()) {
				AddKeyAt(state, hit.track, hit.clip, mouseTime);
			}
		} else {
			BeginDrag(state, hit, mouseTime);
		}
	}
	// --- 右：メニュー（押したときに開くと ImGui が閉じてしまうので、離したときに開く。BeginPopupContextItem と同じ）---
	if (isHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
		menuHit = FindHit(state, area, io.MousePos);
		menuTime = mouseTime;
		if (menuHit.isKey) {
			state.SelectKey(menuHit.track, menuHit.clip, menuHit.keyTime);
		} else {
			state.SelectClip(menuHit.track, menuHit.clip);
		}
		ImGui::OpenPopup("##laneMenu");
	}
	// --- つまんで動かす（押した場所から少し動いてから。クリックだけでは値を変えない）---
	if (state.drag != TimelineDrag::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		UpdateDrag(state, mouseTime);
	}
}

// ===== 列のドロップ：Hierarchy の Camera を、落とした時間のクリップにする =====
void AcceptLaneDrop(TimelineEditorState& state, const LaneArea& area) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int row = static_cast<int>(std::floor((mouse.y - area.min.y) / kTimelineRowHeight));
		if (row >= 0 && row < static_cast<int>(state.GetTimeline()->tracks.size())) {
			AddCameraClipAt(state, row, state.view.ToTime(mouse.x - area.min.x), *static_cast<const Handle<Entity>*>(payload->Data));
		}
	}
	ImGui::EndDragDropTarget();
}

// ===== 右クリックのメニュー =====
void DrawLaneMenu(TimelineEditorState& state) {
	if (!ImGui::BeginPopup("##laneMenu")) {
		return;
	}
	if (menuHit.track < 0) {
		ImGui::TextDisabled("トラックの上で開く");
	} else if (menuHit.isKey) {
		if (ImGui::MenuItem("Delete Key")) {
			DeleteSelected(state);
		}
	} else if (menuHit.clip >= 0) {
		const bool hasCurves = !state.GetTimeline()->tracks[menuHit.track]->GetClipCurves(menuHit.clip).empty();
		if (ImGui::MenuItem("Add Key", nullptr, false, hasCurves)) {
			AddKeyAt(state, menuHit.track, menuHit.clip, menuTime);
		}
		if (ImGui::MenuItem("Delete Clip")) {
			DeleteSelected(state);
		}
	} else if (ImGui::MenuItem("Add Clip")) {
		AddClipAt(state, menuHit.track, menuTime);
	}
	ImGui::EndPopup();
}

// ===== キーボード =====
void HandleKeyboard(TimelineEditorState& state) {
	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		DeleteSelected(state);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		state.needsFit = true;
	}
}

// ===== クリップ1つを描く =====
void DrawClip(ImDrawList* drawList, const TimelineEditorState& state, const LaneArea& area, TimelineTrack& track, int trackIndex, int clip, float rowTop, ImU32 keyColor) {
	const TimelineSelection& selection = state.selection;
	const bool isSelected = selection.track == trackIndex && selection.clip == clip;
	const float left = ClipLeft(state, area, track, clip);
	const float centerY = rowTop + kTimelineRowHeight * 0.5f;
	// --- 1点のクリップ（イベント）---
	if (!track.HasClipDuration()) {
		DrawDiamond(drawList, ImVec2(left, centerY), kEventSize, isSelected ? kSelectedColor : kClipColor);
		return;
	}
	// --- 長さのあるクリップ ---
	const ImVec2 min(left, rowTop + kClipPadding);
	const ImVec2 max(ClipRight(state, area, track, clip), rowTop + kTimelineRowHeight - kClipPadding);
	drawList->AddRectFilled(min, max, kClipColor, kClipRounding);
	// --- 補間の部分：始まりは左上を斜めに暗くする。終わった後は、右へ細くなる三角 ---
	const TimelineClipFade fade = track.GetClipFade(clip);
	if (fade.in > 0.0f) {
		const float fadeX = (std::min)(min.x + fade.in * state.view.pixelsPerSecond, max.x);
		drawList->AddTriangleFilled(min, ImVec2(fadeX, min.y), ImVec2(min.x, max.y), kFadeColor);
	}
	if (fade.out > 0.0f) {
		drawList->AddTriangleFilled(ImVec2(max.x, min.y), ImVec2(max.x + fade.out * state.view.pixelsPerSecond, max.y), max, kFadeOutColor);
	}
	if (isSelected) {
		drawList->AddRect(min, max, kSelectedColor, kClipRounding, 0, 2.0f);
	}
	// --- 名前 ---
	const std::string label = track.GetClipLabel(clip);
	if (!label.empty()) {
		drawList->PushClipRect(min, max, true);
		drawList->AddText(ImVec2(min.x + kClipLabelPadding, centerY - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
		drawList->PopClipRect();
	}
	// --- キーの印（同じ時間のキーは1つに見せる）---
	for (const float keyTime : TimelineEdit::CollectKeyTimes(track.GetClipCurves(clip))) {
		const bool isSelectedKey = isSelected && selection.hasKey && std::abs(selection.keyTime - keyTime) <= TimelineEdit::kSameKeyTime;
		DrawDiamond(drawList, ImVec2(left + keyTime * state.view.pixelsPerSecond, centerY), kKeySize, isSelectedKey ? kSelectedColor : keyColor);
	}
}

// ===== 列を描く =====
void DrawLanes(const TimelineEditorState& state, const LaneArea& area) {
	const Timeline& timeline = *state.GetTimeline();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 max(area.min.x + area.size.x, area.min.y + area.size.y);
	drawList->PushClipRect(area.min, max, true);
	drawList->AddRectFilled(area.min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
	// --- 目盛りの線 ---
	const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, kGridAlpha);
	ForEachTick(state.view, area.size.x, [&](float /*time*/, float x) { drawList->AddLine(ImVec2(area.min.x + x, area.min.y), ImVec2(area.min.x + x, max.y), gridColor); });
	// --- 行とクリップ ---
	for (int i = 0; i < static_cast<int>(timeline.tracks.size()); ++i) {
		TimelineTrack& track = *timeline.tracks[i];
		const float rowTop = area.min.y + static_cast<float>(i) * kTimelineRowHeight;
		drawList->AddLine(ImVec2(area.min.x, rowTop + kTimelineRowHeight), ImVec2(max.x, rowTop + kTimelineRowHeight), gridColor);
		const ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_Text, track.isMuted ? kMutedAlpha : 1.0f);
		for (int clip = 0; clip < static_cast<int>(track.GetClipCount()); ++clip) {
			DrawClip(drawList, state, area, track, i, clip, rowTop, keyColor);
		}
	}
	// --- 終わりの線と再生位置 ---
	const float endX = area.min.x + state.view.ToPixel(state.duration);
	drawList->AddLine(ImVec2(endX, area.min.y), ImVec2(endX, max.y), ImGui::GetColorU32(ImGuiCol_TextDisabled));
	const float playheadX = area.min.x + state.view.ToPixel(state.time);
	drawList->AddLine(ImVec2(playheadX, area.min.y), ImVec2(playheadX, max.y), kPlayheadColor);
	drawList->PopClipRect();
}
} // namespace

//=============================================================================
// 時間の目盛り
//=============================================================================
void DrawTimelineRuler(TimelineEditorState& state) {
	Timeline& timeline = *state.GetTimeline();
	// --- トラックを足す ---
	ImGui::SetNextItemWidth(kTimelineListWidth - ImGui::GetStyle().ItemSpacing.x);
	if (ImGui::BeginCombo("##addTrack", "Add Track")) {
		for (const std::string& name : TimelineTrackRegistry::GetNames()) {
			// 見せるのは頭の Timeline を省いた名前。ID は元の名前（省いた名前がゲームの種類と同じになっても衝突しない）
			if (ImGui::Selectable(std::format("{}###{}", TimelineTrackRegistry::GetDisplayName(name), name).c_str())) {
				state.SelectTrack(TimelineEdit::AddTrack(timeline, name));
				state.MarkEdited();
			}
			const std::string& description = TimelineTrackRegistry::GetDescription(name);
			if (!description.empty()) {
				ImGui::SetItemTooltip("%s", description.c_str());
			}
		}
		ImGui::EndCombo();
	}
	// --- 目盛り（つまむと再生位置が動く）---
	ImGui::SameLine(kTimelineListWidth + ImGui::GetStyle().WindowPadding.x);
	const ImVec2 min = ImGui::GetCursorScreenPos();
	state.laneWidth = (std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
	if (std::exchange(state.needsFit, false)) {
		state.view.Fit(state.duration, state.laneWidth);
	}
	ImGui::InvisibleButton("##ruler", ImVec2(state.laneWidth, kRulerHeight));
	if (ImGui::IsItemActive()) {
		if (ImGui::IsItemActivated() && !state.isGameRunning) {
			state.playhead.Pause(); // つまんでいる間は進めない
		}
		state.SeekTo(state.Snap(state.view.ToTime(ImGui::GetIO().MousePos.x - min.x)));
	}
	if (!ImGui::IsItemActive()) {
		ImGui::SetItemTooltip("つまむと再生位置が動く。停止中はプレビューになる（Ctrl：目盛りにそろえない）");
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 max(min.x + state.laneWidth, min.y + kRulerHeight);
	drawList->PushClipRect(min, max, true);
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
	const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_Text);
	ForEachTick(state.view, state.laneWidth, [&](float time, float x) {
		drawList->AddLine(ImVec2(min.x + x, max.y - 6.0f), ImVec2(min.x + x, max.y), labelColor);
		drawList->AddText(ImVec2(min.x + x + 3.0f, min.y + 1.0f), labelColor, std::format("{:.6g}", time).c_str());
	});
	const float playheadX = min.x + state.view.ToPixel(state.time);
	drawList->AddTriangleFilled(ImVec2(playheadX - kPlayheadWidth, min.y), ImVec2(playheadX + kPlayheadWidth, min.y), ImVec2(playheadX, max.y), kPlayheadColor);
	drawList->PopClipRect();
}

//=============================================================================
// トラックの一覧と時間の列
//=============================================================================
void DrawTimelineRows(TimelineEditorState& state, float height) {
	// ホイールは列のボタンが受け取る。一覧の上のホイールは、この窓の縦のスクロールになる
	ImGui::BeginChild("##rows", ImVec2(0.0f, height));
	const Timeline& timeline = *state.GetTimeline();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const int count = static_cast<int>(timeline.tracks.size());

	// --- 左の一覧（並べ替えと消すのは回し終わってから。回している間に消すと、track の参照と count が古くなる）---
	int moveFrom = -1;
	int moveTo = -1;
	int deleteIndex = -1;
	for (int i = 0; i < count; ++i) {
		DrawTrackRow(state, i, ImVec2(origin.x, origin.y + static_cast<float>(i) * kTimelineRowHeight), moveFrom, moveTo, deleteIndex);
	}
	if (moveFrom >= 0 && moveFrom != moveTo) {
		state.SelectTrack(TimelineEdit::MoveTrack(*state.GetTimeline(), moveFrom, moveTo));
		state.MarkEdited();
	}
	if (deleteIndex >= 0) {
		state.SelectTrack(deleteIndex);
		DeleteSelected(state);
	}

	// --- 右の列（Add Track・消すで行の数が変わるので、ここで数え直す）---
	const float contentHeight = static_cast<float>(state.GetTimeline()->tracks.size()) * kTimelineRowHeight;
	const LaneArea area = {ImVec2(origin.x + kTimelineListWidth, origin.y), ImVec2(state.laneWidth, (std::max)(contentHeight, height))};
	ImGui::SetCursorScreenPos(area.min);
	ImGui::InvisibleButton("##lanes", area.size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // ホイールを、窓のスクロールより先に受け取る
	// IsItem〜 は直前の項目＝上のボタンを見るので、間に項目を置かない
	AcceptLaneDrop(state, area);
	HandleView(state, area);
	HandleMouse(state, area);
	HandleKeyboard(state);
	DrawLanes(state, area);
	DrawLaneMenu(state);
	ImGui::EndChild();
}

//=============================================================================
// 横のスクロールバー
//=============================================================================
void DrawTimelineScrollbar(TimelineEditorState& state) {
	const float width = state.laneWidth;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kTimelineListWidth);
	const ImVec2 min = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##hScroll", ImVec2(width, kTimelineScrollbarHeight));
	// --- 全体の長さと見えている範囲 ---
	const float visible = width / state.view.pixelsPerSecond;
	const float total = (std::max)(state.duration * kScrollMargin, state.view.scrollTime + visible);
	if (ImGui::IsItemActive()) {
		state.view.Pan(ImGui::GetIO().MouseDelta.x * total / width * state.view.pixelsPerSecond);
	}
	const float thumbWidth = (std::max)(visible / total * width, kMinThumbWidth);
	const float thumbLeft = min.x + state.view.scrollTime / total * width;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + kTimelineScrollbarHeight), ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
	const ImU32 thumbColor = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive : ImGuiCol_ScrollbarGrab);
	drawList->AddRectFilled(ImVec2(thumbLeft, min.y), ImVec2(thumbLeft + thumbWidth, min.y + kTimelineScrollbarHeight), thumbColor, kTimelineScrollbarHeight * 0.5f);
}
#endif