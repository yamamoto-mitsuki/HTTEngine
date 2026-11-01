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

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"

// Timeline ウィンドウの下の詳細（トラックの結び付け・クリップ・キー・カーブ）

namespace {
constexpr float kFieldsWidth = 280.0f;             // 項目の列（トラック・クリップ）1つの幅
constexpr float kItemLabelWidth = 90.0f;           // 項目の右の名前に残す幅
constexpr float kCurveMinHeight = 80.0f;           // カーブの絵の高さの下限
constexpr float kCurveFooterHeight = 120.0f;       // 欄が狭いとき、カーブの絵の下の数値の欄に残す高さ
constexpr float kCurveWideWidth = 640.0f;          // 欄の幅がこれ以上なら、数値の欄を絵の右に置く
constexpr float kCurveFieldsWidth = 280.0f;        // カーブの絵の右の数値の欄の幅
constexpr const char* kDefaultTrackName = "Track"; // トラックの名前の既定の値（結び付けを選ぶと自動の名前に替える）
constexpr std::array<const char*, ComponentValue::kChannelCount> kVectorNames = {"x", "y", "z", "w"}; // 成分の名前
constexpr std::array<const char*, ComponentValue::kChannelCount> kColorNames = {"r", "g", "b", "a"};  // 色の成分の名前

// ===== パスの一番下の名前 =====
std::string_view LeafName(std::string_view path) {
	const size_t slash = path.rfind('/');
	return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

// ===== 自動の名前（Title.color）=====
std::string MakeAutoName(const TimelineValueTrack& track) { return track.target.empty() ? track.component + "." + track.property : std::string(LeafName(track.target)) + "." + track.property; }

// ===== 結び付けを変えた後、名前が自動のままなら付け直す =====
void UpdateAutoName(TimelineValueTrack& track, const std::string& oldAutoName) {
	if (track.name == kDefaultTrackName || track.name == oldAutoName) {
		track.name = MakeAutoName(track);
	}
}

// ===== 対象の Entity の項目の形（分からなければ空）=====
std::optional<ComponentValueField> FindField(const TimelineEditorState& state, const TimelineValueTrack& track) {
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		return std::nullopt;
	}
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, track.component);
	const auto found = std::ranges::find(fields, track.property, &ComponentValueField::key);
	return (found != fields.end()) ? std::optional(*found) : std::nullopt;
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
		GetInspectorUI().Field("Target###target", track.target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く。空なら再生する Entity 自身"));
		return;
	}
	if (ImGui::BeginCombo("Target###target", track.target.empty() ? "(self)" : track.target.c_str())) {
		if (ImGui::Selectable("(self)", track.target.empty())) {
			SetTimelineTarget(state, track, state.owner);
		}
		int id = 0;
		DrawTargetChoices(state, track, state.owner, id);
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす相手（再生する Entity とその子孫）。Hierarchy の Entity をトラックの行へドラッグしても選べる");
}

// ===== Component と項目 =====
void DrawValueBinding(TimelineEditorState& state, TimelineValueTrack& track) {
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		track.Fields(GetInspectorUI()); // 対象が無い間は、文字で直せるようにする
		return;
	}
	const std::string oldAutoName = MakeAutoName(track);
	// --- Component（値を動かせる項目がある物だけ）---
	if (ImGui::BeginCombo("Component###component", track.component.c_str())) {
		for (const std::string& name : ComponentSerializer::GetComponentNames(target)) {
			if (ComponentSerializer::GetValueFields(target, name).empty()) {
				continue;
			}
			if (ImGui::Selectable(name.c_str(), name == track.component)) {
				track.component = name;
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす Component");
	// --- 項目 ---
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, track.component);
	if (ImGui::BeginCombo("Property###property", track.property.c_str())) {
		for (const ComponentValueField& field : fields) {
			if (ImGui::Selectable(std::format("{}###{}", field.label, field.key).c_str(), field.key == track.property)) {
				track.property = field.key;
				UpdateAutoName(track, oldAutoName);
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす項目（数で表せる物：float・int・bool・Vector・色）");
}

// ===== トラック全体 =====
void DrawTrackFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText(track.typeName.c_str());
	ImGui::PushID("track"); // クリップの項目と名前が重なる（Name###name）ので分ける
	GetInspectorUI().Field("Name###name", track.name, Tip("トラックの名前（一覧と警告に出す）"));
	DrawTarget(state, track);
	if (auto* valueTrack = dynamic_cast<TimelineValueTrack*>(&track)) {
		DrawValueBinding(state, *valueTrack);
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
	const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track);
	const std::optional<ComponentValueField> field = (valueTrack != nullptr) ? FindField(state, *valueTrack) : std::nullopt;
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
	ImGui::PushID("channels"); // 下のキーの値の欄と同じ名前（a など）になるので分ける
	for (size_t i = 0; i < count; ++i) {
		ImGui::PushID(static_cast<int>(i));
		bool isUsed = !curves[i].GetKeys().empty();
		if (ImGui::Checkbox("##used", &isUsed)) {
			if (isUsed) {
				TimelineEdit::FillFlat(curves[i], 0.0f, track.GetClipDuration(state.selection.clip));
			} else {
				curves[i] = Curve();
			}
			state.curveEditor = CurveEditor{};
			state.MarkEdited();
		}
		ImGui::SetItemTooltip("この成分を動かす（外すとキーを全部消す）");
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
	const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track);
	const std::optional<ComponentValueField> field = (valueTrack != nullptr) ? FindField(state, *valueTrack) : std::nullopt;
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

// ===== クリップ全体 =====
void DrawClipFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText("Clip");
	ImGui::PushID("clip");
	state.selection.clip = static_cast<int>(track.DescribeClip(state.selection.clip, GetInspectorUI()));
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

// ===== カーブの絵 =====
void DrawCurvePanel(TimelineEditorState& state, std::span<Curve> curves) {
	if (curves.empty()) {
		ImGui::TextDisabled("このクリップにカーブはありません");
		return;
	}
	if (curves[state.selection.channel].GetKeys().empty()) {
		ImGui::TextDisabled("動かす成分にチェックを入れると、ここでカーブを直せる");
		return;
	}
	// 広ければ数値の欄を右に置いて、上の並びを除いた高さを全部カーブの絵に使う
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const bool isWide = available.x >= kCurveWideWidth;
	const float footerHeight = isWide ? 0.0f : kCurveFooterHeight;
	const float canvasHeight = (std::max)(available.y - ImGui::GetFrameHeightWithSpacing() - footerHeight, kCurveMinHeight);
	if (state.curveEditor.Draw("curve", curves[state.selection.channel], canvasHeight, GetInspectorUI(), isWide ? kCurveFieldsWidth : 0.0f)) {
		state.MarkEdited();
	}
	state.curveEditor.TakeFinishedEdit(); // Undo は TimelineWindow がファイル全体でまとめて積むので、使わず受け取る
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
	// --- 右：カーブ ---
	ImGui::SameLine();
	ImGui::BeginChild("##curve", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
	if (state.selection.clip >= 0) {
		DrawCurvePanel(state, state.GetSelectedCurves());
	} else {
		ImGui::TextDisabled("クリップを選ぶと、ここでカーブを直せる（列の何も無い所をダブルクリックでクリップを足す）");
	}
	ImGui::EndChild();
}

//=============================================================================
// 部品の間で使う
//=============================================================================
void FillNewClipCurves(const TimelineEditorState& state, TimelineTrack& track, size_t clip) {
	const std::span<Curve> curves = track.GetClipCurves(clip);
	if (curves.empty()) {
		return;
	}
	// --- 対象の今の値を読む（読めなければ最初の成分を 0 で）---
	ComponentValue value;
	value.isUsed[0] = true;
	if (const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track)) {
		const Handle<Entity> target = EntityManager::FindByPath(state.owner, valueTrack->target);
		if (target.IsValid()) {
			ComponentSerializer::GetValue(target, valueTrack->component, valueTrack->property, value);
		}
	}
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
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		return std::format("対象が見つかりません: \"{}\"", track.target);
	}
	const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track);
	if (valueTrack == nullptr) {
		return {};
	}
	ComponentValue value;
	switch (ComponentSerializer::GetValue(target, valueTrack->component, valueTrack->property, value)) {
	case ComponentValueResult::Written:
		return {};
	case ComponentValueResult::NoComponent:
		return std::format("\"{}\" に Component \"{}\" がありません", track.target, valueTrack->component);
	case ComponentValueResult::NoField:
		return std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", valueTrack->component, valueTrack->property);
	}
	return {};
}

void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity) {
	const std::optional<std::string> path = EntityManager::MakePath(state.owner, entity);
	if (!path.has_value()) {
		LogManager::Warning("Timeline の対象にできるのは、再生する Entity とその子孫だけです");
		return;
	}
	auto* valueTrack = dynamic_cast<TimelineValueTrack*>(&track);
	const std::string oldAutoName = (valueTrack != nullptr) ? MakeAutoName(*valueTrack) : std::string();
	track.target = *path;
	if (valueTrack != nullptr) {
		UpdateAutoName(*valueTrack, oldAutoName);
	}
	state.MarkEdited();
}
#endif