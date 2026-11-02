#ifdef USE_IMGUI
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/imgui/imgui_internal.h>

#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Timeline/Camera/TimelineCameraTrack.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"
#include "MyEngine/Timeline/Tween/TimelinePresetTrack.h"
#include "MyEngine/Timeline/Tween/TimelineTweenTrack.h"

// Timeline ウィンドウの下の詳細（トラックの結び付け・クリップ・キー・カーブ）

namespace {
constexpr float kFieldsWidth = 280.0f;                                                                // 項目の列（トラック・クリップ）1つの幅
constexpr float kItemLabelWidth = 90.0f;                                                              // 項目の右の名前に残す幅
constexpr float kCurveMinHeight = 80.0f;                                                              // カーブの絵の高さの下限
constexpr float kCurveFooterHeight = 120.0f;                                                          // 欄が狭いとき、カーブの絵の下の数値の欄に残す高さ
constexpr float kCurveWideWidth = 640.0f;                                                             // 欄の幅がこれ以上なら、数値の欄を絵の右に置く
constexpr float kCurveFieldsWidth = 280.0f;                                                           // カーブの絵の右の数値の欄の幅
constexpr const char* kDefaultTrackName = "Track";                                                    // トラックの名前の既定の値（結び付けを選ぶと自動の名前に替える）
constexpr std::array<const char*, ComponentValue::kChannelCount> kVectorNames = {"x", "y", "z", "w"}; // 成分の名前
constexpr std::array<const char*, ComponentValue::kChannelCount> kColorNames = {"r", "g", "b", "a"};  // 色の成分の名前

// ===== パスの一番下の名前 =====
std::string_view LeafName(std::string_view path) {
	const size_t slash = path.rfind('/');
	return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

// ===== 自動の名前（Title.color）=====
std::string MakeAutoName(const TimelineTrack& track, const TimelinePropertyBinding& binding) {
	return track.target.empty() ? binding.component + "." + binding.property : std::string(LeafName(track.target)) + "." + binding.property;
}

// ===== 結び付けを変えた後、名前が自動のままなら付け直す =====
void UpdateAutoName(TimelineTrack& track, const TimelinePropertyBinding& binding, const std::string& oldAutoName) {
	if (track.name == kDefaultTrackName || track.name == oldAutoName) {
		track.name = MakeAutoName(track, binding);
	}
}

// ===== 対象の Entity の項目の形（項目を動かす種類でない・分からなければ空）=====
std::optional<ComponentValueField> FindField(const TimelineEditorState& state, const TimelineTrack& track) {
	const TimelinePropertyBinding* binding = track.GetPropertyBinding();
	const Handle<Entity> target = (binding != nullptr) ? EntityManager::FindByPath(state.owner, track.target) : Handle<Entity>{};
	if (!target.IsValid()) {
		return std::nullopt;
	}
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, binding->component);
	const auto found = std::ranges::find(fields, binding->property, &ComponentValueField::key);
	return (found != fields.end()) ? std::optional(*found) : std::nullopt;
}

// ===== 対象の今の値（読めなければ全部 0 で、最初の成分だけ使う）=====
ComponentValue ReadTargetValue(const TimelineEditorState& state, const TimelineTrack& track) {
	ComponentValue value;
	value.isUsed[0] = true;
	if (const TimelinePropertyBinding* binding = track.GetPropertyBinding()) {
		const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
		if (target.IsValid()) {
			ComponentSerializer::GetValue(target, binding->component, binding->property, value);
		}
	}
	return value;
}

// ===== 成分の名前 =====
std::string ChannelName(const std::optional<ComponentValueField>& field, size_t channel) {
	if (!field.has_value()) {
		return kVectorNames[channel];
	}
	if (field->channelCount == 1) {
		return "value";
	}
	return field->isColor ? kColorNames[channel] : kVectorNames[channel];
}

// ===== シーンのカメラを選択肢に出す（onSelect は選んだ Entity を受け取る。isOutsideOnly なら、再生する Entity の外のカメラだけ）=====
template<class Function> void DrawCameraChoices(const TimelineEditorState& state, const std::string& current, bool isOutsideOnly, Function&& onSelect) {
	int id = 0;
	EntityManager::ForEach<CameraComponent>([&](Handle<Entity> entity, const CameraComponent&) {
		const std::string path = EntityManager::MakeBindingPath(state.owner, entity);
		if (isOutsideOnly && !path.starts_with('/')) {
			return;
		}
		ImGui::PushID(id++); // 同じ名前のカメラがあっても項目が衝突しないように、数えた番号で分ける
		if (ImGui::Selectable(path.empty() ? "(self)" : path.c_str(), path == current)) {
			onSelect(entity);
		}
		ImGui::PopID();
	});
}

//=============================================================================
// トラック
//=============================================================================
// ===== 再生する Entity の子孫を選択肢に出す =====
void DrawTargetChoices(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> parent, int& id) {
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(parent, children);
	for (const Handle<Entity> child : children) {
		const std::string path = EntityManager::MakePath(state.owner, child).value_or("");
		ImGui::PushID(id++); // 同じ名前の兄弟があっても項目が衝突しないように、数えた番号で分ける
		if (ImGui::Selectable(path.c_str(), path == track.target)) {
			SetTimelineTarget(state, track, child);
		}
		ImGui::PopID();
		DrawTargetChoices(state, track, child, id);
	}
}

// ===== 対象 =====
void DrawTarget(TimelineEditorState& state, TimelineTrack& track) {
	if (!state.owner.IsValid()) {
		GetInspectorUI().Field("Target###target", track.target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く。空なら再生する Entity 自身。先頭が / ならシーンの一番上から"));
		return;
	}
	if (ImGui::BeginCombo("Target###target", track.target.empty() ? "(self)" : track.target.c_str())) {
		if (ImGui::Selectable("(self)", track.target.empty())) {
			SetTimelineTarget(state, track, state.owner);
		}
		int id = 0;
		DrawTargetChoices(state, track, state.owner, id);
		ImGui::SeparatorText("Scene Cameras");
		DrawCameraChoices(state, track.target, true, [&](Handle<Entity> entity) { SetTimelineTarget(state, track, entity); });
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす相手（再生する Entity とその子孫・シーンのカメラ）。Hierarchy の Entity をトラックの行へドラッグすると、シーンのどの Entity でも選べる");
}

// ===== Component と項目 =====
void DrawPropertyBinding(TimelineEditorState& state, TimelineTrack& track, TimelinePropertyBinding& binding) {
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		binding.Fields(GetInspectorUI()); // 対象が無い間は、文字で直せるようにする
		return;
	}
	const std::string oldAutoName = MakeAutoName(track, binding);
	// --- Component（値を動かせる項目がある物だけ）---
	if (ImGui::BeginCombo("Component###component", binding.component.c_str())) {
		for (const std::string& name : ComponentSerializer::GetComponentNames(target)) {
			if (ComponentSerializer::GetValueFields(target, name).empty()) {
				continue;
			}
			if (ImGui::Selectable(name.c_str(), name == binding.component)) {
				binding.component = name;
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす Component");
	// --- 項目 ---
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, binding.component);
	if (ImGui::BeginCombo("Property###property", binding.property.c_str())) {
		for (const ComponentValueField& field : fields) {
			if (ImGui::Selectable(std::format("{}###{}", field.label.empty() ? field.key : field.label, field.key).c_str(), field.key == binding.property)) {
				binding.property = field.key;
				UpdateAutoName(track, binding, oldAutoName);
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす項目（数で表せる物：float・int・bool・Vector・色）");
}

// ===== トラック全体 =====
void DrawTrackFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText(std::string(TimelineTrackRegistry::GetDisplayName(track.typeName)).c_str());
	ImGui::PushID("track"); // クリップの項目と名前が重なる（Name###name）ので分ける
	GetInspectorUI().Field("Name###name", track.name, Tip("トラックの名前（一覧と警告に出す）"));
	if (track.UsesTarget()) {
		DrawTarget(state, track);
	}
	if (TimelinePropertyBinding* binding = track.GetPropertyBinding()) {
		DrawPropertyBinding(state, track, *binding);
	} else {
		track.Fields(GetInspectorUI());
	}
	ImGui::PopID();
}

//=============================================================================
// クリップ
//=============================================================================
// ===== 成分（チェック：キーがある＝動かす / 名前：カーブの絵に出す）=====
void DrawChannels(TimelineEditorState& state, TimelineTrack& track, std::span<Curve> curves) {
	const std::optional<ComponentValueField> field = FindField(state, track);
	const size_t count = field.has_value() ? (std::min)(field->channelCount, curves.size()) : curves.size();
	state.selection.channel = (std::min)(state.selection.channel, static_cast<int>(count) - 1);
	// --- 選んでいる成分にキーが無ければ、キーのある最初の成分を絵に出す ---
	if (curves[state.selection.channel].GetKeys().empty()) {
		const auto used = std::ranges::find_if(curves, [](const Curve& curve) { return !curve.GetKeys().empty(); });
		if (used != curves.end()) {
			state.selection.channel = static_cast<int>(used - curves.begin());
			state.curveEditor = CurveEditor{};
		}
	}
	const auto hasKeys = [](const Curve& curve) { return !curve.GetKeys().empty(); };
	const ptrdiff_t usedCount = std::ranges::count_if(curves.first(count), hasKeys); // キーのある成分の数
	ImGui::PushID("channels");                                                       // 下のキーの値の欄と同じ名前（a など）になるので分ける
	for (size_t i = 0; i < count; ++i) {
		ImGui::PushID(static_cast<int>(i));
		bool isUsed = hasKeys(curves[i]);
		// 最後の1つは外せない（外すとクリップが何も動かさなくなる。要らなければクリップを消す）
		const bool isLastUsed = isUsed && usedCount == 1;
		ImGui::BeginDisabled(isLastUsed);
		if (ImGui::Checkbox("##used", &isUsed)) {
			if (isUsed) {
				// 入れ直したら、対象の今の値の平らな線から（0 から始めると、FOV などが一瞬で壊れた値になる）
				TimelineEdit::FillFlat(curves[i], ReadTargetValue(state, track).channels[i], track.GetClipDuration(state.selection.clip));
			} else {
				curves[i] = Curve();
			}
			state.curveEditor = CurveEditor{};
			state.MarkEdited();
		}
		ImGui::EndDisabled();
		// 押せないときも説明を出す（SetItemTooltip は押せない項目では出ない）
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", isLastUsed ? "最後の1つは外せない（動かさないなら、クリップを消す）" : "この成分を動かす（外すとキーを全部消す。入れ直すと今の値の平らな線になる）");
		}
		ImGui::SameLine();
		if (ImGui::Selectable(ChannelName(field, i).c_str(), state.selection.channel == static_cast<int>(i), 0, ImVec2(40.0f, 0.0f))) {
			state.selection.channel = static_cast<int>(i);
			state.curveEditor = CurveEditor{};
		}
		ImGui::PopID();
		if (i + 1 < count) {
			ImGui::SameLine();
		}
	}
	ImGui::PopID();
}

// ===== 選んでいるキーの値（全部の成分を並べて打てる）=====
void DrawKeyFields(TimelineEditorState& state, TimelineTrack& track, std::span<Curve> curves) {
	TimelineSelection& selection = state.selection;
	// --- カーブの絵で動かしたり消したりしたら、もう選んでいない ---
	const std::vector<float> times = TimelineEdit::CollectKeyTimes(curves);
	const bool isFound = std::ranges::any_of(times, [&selection](float time) { return std::abs(time - selection.keyTime) <= TimelineEdit::kSameKeyTime; });
	if (!selection.hasKey || !isFound) {
		selection.hasKey = false;
		return;
	}
	ImGui::SeparatorText("Key");
	const float duration = track.GetClipDuration(selection.clip);
	float keyTime = selection.keyTime;
	if (ImGui::DragFloat("Time###keyTime", &keyTime, 0.01f, 0.0f, duration, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
		TimelineEdit::MoveKeys(curves, selection.keyTime, keyTime);
		selection.keyTime = keyTime;
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("キーの時間（クリップの始まりからの秒）");
	// --- その時間にキーのある成分の値 ---
	const std::optional<ComponentValueField> field = FindField(state, track);
	ImGui::PushID("keyValues");
	for (size_t i = 0; i < curves.size(); ++i) {
		const std::vector<CurveKey>& keys = curves[i].GetKeys();
		const auto key = std::ranges::find_if(keys, [&selection](const CurveKey& item) { return std::abs(item.time - selection.keyTime) <= TimelineEdit::kSameKeyTime; });
		if (key == keys.end()) {
			continue;
		}
		ImGui::PushID(static_cast<int>(i));
		float value = key->value;
		if (ImGui::DragFloat(ChannelName(field, i).c_str(), &value, 0.01f)) {
			CurveKey edited = *key;
			edited.value = value;
			curves[i].SetKey(static_cast<int>(key - keys.begin()), edited);
			state.MarkEdited();
		}
		ImGui::PopID();
	}
	ImGui::PopID();
}

// ===== カメラのクリップの、写すカメラ =====
void DrawClipCamera(TimelineEditorState& state, TimelineCameraTrack& track) {
	TimelineCameraClip& clip = track.clips[state.selection.clip];
	const auto select = [&](Handle<Entity> entity) {
		clip.camera = EntityManager::MakeBindingPath(state.owner, entity);
		state.MarkEdited();
	};
	if (ImGui::BeginCombo("Camera###camera", clip.camera.empty() ? "(self)" : clip.camera.c_str())) {
		DrawCameraChoices(state, clip.camera, false, select);
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("このクリップの間に写すカメラ。Hierarchy の Camera をここへドラッグしても選べる。再生する Entity の外のカメラは / で始まるパスになる");
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
			select(*static_cast<const Handle<Entity>*>(payload->Data));
		}
		ImGui::EndDragDropTarget();
	}
}

// ===== Tween の端1つ（決め方・値・Parameter の名前）=====
void DrawTweenPoint(TimelineEditorState& state, const char* label, TimelineTweenPoint& point, const std::optional<ComponentValueField>& field) {
	ImGui::PushID(label);
	ImGui::SeparatorText(label);
	GetInspectorUI().Field(
	    "Mode###mode", point.mode, Tip("Current：始まったときの今の値 / Value：下の値 / Add：元の値 ＋ 下の値 / Multiply：元の値 × 下の値 / Parameter：TimelineSystem::SetParameter で渡す値"));
	if (point.mode == TimelineTweenMode::Parameter) {
		GetInspectorUI().Field("Name###parameter", point.parameter, Tip("TimelineSystem::SetParameter に渡す名前。渡していないときは下の値"));
	}
	// --- 値（項目の成分の数だけ。Current は使わない）---
	const size_t count = field.has_value() ? field->channelCount : ComponentValue::kChannelCount;
	std::array<float*, ComponentValue::kChannelCount> channels = {&point.value.x, &point.value.y, &point.value.z, &point.value.w};
	ImGui::BeginDisabled(point.mode == TimelineTweenMode::Current);
	for (size_t i = 0; i < count; ++i) {
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::DragFloat(ChannelName(field, i).c_str(), channels[i], 0.01f)) {
			state.MarkEdited();
		}
		ImGui::PopID();
	}
	ImGui::EndDisabled();
	ImGui::PopID();
}

// ===== Tween のクリップ（From・To）=====
void DrawClipTween(TimelineEditorState& state, TimelineTweenTrack& track) {
	TimelineTweenClip& clip = track.clips[state.selection.clip];
	const std::optional<ComponentValueField> field = FindField(state, track);
	DrawTweenPoint(state, "From", clip.from, field);
	DrawTweenPoint(state, "To", clip.to, field);
	ImGui::TextDisabled("動き方は右のカーブ（横：時間の割合 0〜1 / 縦：From〜To の割合）");
}

// ===== Preset のクリップ（長さを合わせる・プリセットを開く）=====
void DrawClipPreset(TimelineEditorState& state, TimelinePresetTrack& track, uint32_t before) {
	TimelinePresetClip& clip = track.clips[state.selection.clip];
	const Timeline* preset = TimelineManager::Get(clip.timeline);
	// --- ファイルを選び直したら、長さをプリセットに合わせる ---
	if (clip.timeline != before && preset != nullptr) {
		state.selection.clip = static_cast<int>(track.SetClipTime(state.selection.clip, clip.start, preset->GetDuration()));
		state.MarkEdited();
		return;
	}
	ImGui::BeginDisabled(preset == nullptr);
	if (ImGui::SmallButton("Fit Length")) {
		state.selection.clip = static_cast<int>(track.SetClipTime(state.selection.clip, clip.start, preset->GetDuration()));
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("クリップの長さを、プリセットの長さにする");
	ImGui::SameLine();
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	ImGui::BeginDisabled(!target.IsValid());
	if (ImGui::SmallButton("Open Preset")) {
		state.openRequest = clip.timeline;
		state.openOwnerId = EntityManager::Get(target)->id;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("プリセットを、このトラックの対象の上で開く（Back で戻る）。直したプリセットは、使っている全部の場所に効く");
	ImGui::EndDisabled();
}

// ===== クリップ全体 =====
void DrawClipFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText("Clip");
	ImGui::PushID("clip");
	if (auto* cameraTrack = dynamic_cast<TimelineCameraTrack*>(&track)) {
		DrawClipCamera(state, *cameraTrack);
	}
	auto* presetTrack = dynamic_cast<TimelinePresetTrack*>(&track);
	const uint32_t presetBefore = (presetTrack != nullptr) ? presetTrack->clips[state.selection.clip].timeline : 0;
	state.selection.clip = static_cast<int>(track.DescribeClip(state.selection.clip, GetInspectorUI()));
	if (presetTrack != nullptr) {
		DrawClipPreset(state, *presetTrack, presetBefore);
	}
	if (auto* tweenTrack = dynamic_cast<TimelineTweenTrack*>(&track)) {
		DrawClipTween(state, *tweenTrack);
	}
	const std::span<Curve> curves = track.GetClipCurves(state.selection.clip);
	if (!curves.empty()) {
		DrawChannels(state, track, curves);
		if (ImGui::SmallButton("Add Key At Playhead")) {
			const float keyTime = std::clamp(state.time - track.GetClipStart(state.selection.clip), 0.0f, track.GetClipDuration(state.selection.clip));
			TimelineEdit::AddKeys(curves, keyTime);
			state.SelectKey(state.selection.track, state.selection.clip, keyTime);
			state.MarkEdited();
		}
		ImGui::SetItemTooltip("再生位置に、形を変えずにキーを足す（列の上のクリップをダブルクリックでも足せる）");
		DrawKeyFields(state, track, curves);
	}
	ImGui::PopID();
}

// ===== カーブの絵（描く高さを決めて、絵を描く）=====
void DrawCurveCanvas(TimelineEditorState& state, Curve& curve) {
	// 広ければ数値の欄を右に置いて、上の並びを除いた高さを全部カーブの絵に使う
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const bool isWide = available.x >= kCurveWideWidth;
	const float footerHeight = isWide ? 0.0f : kCurveFooterHeight;
	const float canvasHeight = (std::max)(available.y - ImGui::GetFrameHeightWithSpacing() - footerHeight, kCurveMinHeight);
	if (state.curveEditor.Draw("curve", curve, canvasHeight, GetInspectorUI(), isWide ? kCurveFieldsWidth : 0.0f)) {
		state.MarkEdited();
	}
	state.curveEditor.TakeFinishedEdit(); // Undo は TimelineWindow がファイル全体でまとめて積むので、使わず受け取る
}

// ===== カーブの絵（値のトラック）=====
void DrawCurvePanel(TimelineEditorState& state, std::span<Curve> curves) {
	if (curves.empty()) {
		ImGui::TextDisabled("このクリップにカーブはありません");
		return;
	}
	if (curves[state.selection.channel].GetKeys().empty()) {
		ImGui::TextDisabled("動かす成分にチェックを入れると、ここでカーブを直せる");
		return;
	}
	DrawCurveCanvas(state, curves[state.selection.channel]);
}

// ===== Tween の動き方のカーブ（Ease の形は、カーブの絵の Ease Preset で作り直せる）=====
void DrawTweenEasePanel(TimelineEditorState& state, TimelineTweenClip& clip) {
	ImGui::TextDisabled("動き方（横：時間の割合 0〜1 / 縦：From〜To の割合。1 を超えると To より行き過ぎる）");
	DrawCurveCanvas(state, clip.ease);
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
void DrawTimelineDetails(TimelineEditorState& state, float height) {
	TimelineTrack* track = state.GetSelectedTrack();
	if (track == nullptr) {
		ImGui::TextDisabled("トラックを選ぶと、ここで結び付けとキーを直せる");
		const Timeline* timeline = state.GetTimeline();
		if (!timeline->unknownTracks.empty()) {
			ImGui::TextDisabled("知らない種類のトラックが %d 個あります（動かしません。保存しても消えません）", static_cast<int>(timeline->unknownTracks.size()));
		}
		return;
	}
	// --- 左：トラックの結び付け ---
	ImGui::BeginChild("##trackFields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	DrawTrackFields(state, *track);
	ImGui::PopItemWidth();
	ImGui::EndChild();
	// --- 真ん中：クリップとキー（1列にまとめると、キーの値がスクロールの下へ隠れる）---
	ImGui::SameLine();
	ImGui::BeginChild("##clipFields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	if (state.selection.clip >= 0) {
		DrawClipFields(state, *track);
	} else {
		ImGui::TextDisabled("クリップを選ぶと、ここで直せる");
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();
	// ComponentUI の項目は「変えたか」を返さないので、ImGui の「今操作している項目を変えた」印を見る（打っている文字にも効く）
	if (ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame) {
		state.MarkEdited();
	}
	// --- 右：カーブ（Tween は動き方のカーブ）---
	ImGui::SameLine();
	ImGui::BeginChild("##curve", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
	auto* tweenTrack = dynamic_cast<TimelineTweenTrack*>(track);
	if (state.selection.clip < 0) {
		ImGui::TextDisabled("クリップを選ぶと、ここでカーブを直せる（列の何も無い所をダブルクリックでクリップを足す）");
	} else if (tweenTrack != nullptr) {
		DrawTweenEasePanel(state, tweenTrack->clips[state.selection.clip]);
	} else {
		DrawCurvePanel(state, state.GetSelectedCurves());
	}
	ImGui::EndChild();
}

//=============================================================================
// 部品の間で使う
//=============================================================================
void FillNewClipCurves(const TimelineEditorState& state, TimelineTrack& track, size_t clip) {
	const ComponentValue value = ReadTargetValue(state, track);
	// --- Tween：今の値から、今の値へ（何も動かない形から直していく）---
	if (auto* tweenTrack = dynamic_cast<TimelineTweenTrack*>(&track)) {
		tweenTrack->clips[clip].to.value = {value.channels[0], value.channels[1], value.channels[2], value.channels[3]};
		tweenTrack->clips[clip].from.value = tweenTrack->clips[clip].to.value;
		return;
	}
	// --- 値のトラック：対象の今の値で平らなキー ---
	const std::span<Curve> curves = track.GetClipCurves(clip);
	for (size_t i = 0; i < curves.size(); ++i) {
		if (value.isUsed[i]) {
			TimelineEdit::FillFlat(curves[i], value.channels[i], track.GetClipDuration(clip));
		}
	}
}

std::string FindTimelineBindingProblem(const TimelineEditorState& state, const TimelineTrack& track) {
	if (!state.owner.IsValid()) {
		return {}; // 再生する Entity が分からないので、調べられない
	}
	if (!track.UsesTarget()) {
		return track.FindClipProblem(state.owner);
	}
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		return std::format("対象が見つかりません: \"{}\"", track.target);
	}
	const TimelinePropertyBinding* binding = track.GetPropertyBinding();
	if (binding == nullptr) {
		return {};
	}
	ComponentValue value;
	switch (ComponentSerializer::GetValue(target, binding->component, binding->property, value)) {
	case ComponentValueResult::Written:
		return {};
	case ComponentValueResult::NoComponent:
		return std::format("\"{}\" に Component \"{}\" がありません", track.target, binding->component);
	case ComponentValueResult::NoField:
		return std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", binding->component, binding->property);
	}
	return {};
}

void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity) {
	const TimelinePropertyBinding* binding = track.GetPropertyBinding();
	const std::string oldAutoName = (binding != nullptr) ? MakeAutoName(track, *binding) : std::string();
	track.target = EntityManager::MakeBindingPath(state.owner, entity);
	if (binding != nullptr) {
		UpdateAutoName(track, *binding, oldAutoName);
	}
	state.MarkEdited();
}
#endif