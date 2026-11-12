#ifdef USE_IMGUI
#include "InputActionTable.h"

#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>

namespace {
constexpr float kLabelWidth = 200.0f;                               // Action の名前の列の幅
constexpr ImVec4 kConflictButtonColor = {0.62f, 0.20f, 0.20f, 1.0f}; // ぶつかっている割り当てのボタン
constexpr ImVec4 kConflictTextColor = {1.0f, 0.50f, 0.45f, 1.0f};    // ぶつかりの説明の文字
constexpr const char* kListeningPopupId = "割り当てを拾う";          // 拾っている間のモーダルの名前（ID にもなる）
constexpr float kSmallButtonScale = 0.75f;                          // × と ＋ の文字の大きさ（ふつうの文字の何倍か）
constexpr float kRemoveButtonSpacing = 4.0f;                        // 割り当てのボタンと × の間（ピクセル）

// 小さいボタン（× と ＋）。文字を小さくし、同じ行の割り当てのボタンと上下の真ん中をそろえる
bool SmallRowButton(const char* label) {
	const float rowHeight = ImGui::GetFrameHeight(); // 割り当てのボタン（ふつうの文字）の高さ
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kSmallButtonScale);
	// 小さいボタンの高さ（小さい文字の高さ）との差の半分だけ下げる
	// SmallButton は、同じ行の文字の高さに合わせて自分でも FramePadding.y だけ下がるので、その分は引く
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f - ImGui::GetStyle().FramePadding.y);
	const bool isPressed = ImGui::SmallButton(label);
	ImGui::PopFont();
	return isPressed;
}

// 割り当ての一覧を "A / B" の文字にする（既定に戻すボタンの説明）
std::string JoinBindings(const std::vector<InputBinding>& bindings) {
	std::string text;
	for (const InputBinding& binding : bindings) {
		text += text.empty() ? "" : " / ";
		text += InputBindingText::ToDisplayString(binding);
	}
	return text.empty() ? "（なし）" : text;
}
} // namespace


//=============================================================================
// 表
//=============================================================================
// ===== 全部 =====
void InputActionTable::Draw(InputActionMap& map) {
	lastDrawFrame_ = ImGui::GetFrameCount();

	// --- group ごとに見出しを付け、表を分ける（同じ group が続く間は、同じ表）---
	std::string currentGroup;
	bool isTableOpen = false;
	for (uint32_t i = 0; i < map.GetActionCount(); ++i) {
		const InputActionId id = {i};
		const InputActionDesc& desc = map.GetDesc(id);
		if (i == 0 || desc.group != currentGroup) {
			if (isTableOpen) {
				ImGui::EndTable();
			}
			currentGroup = desc.group;
			ImGui::SeparatorText(currentGroup.c_str());
			isTableOpen = ImGui::BeginTable(std::format("##group{}", i).c_str(), 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
			if (isTableOpen) {
				ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
				ImGui::TableSetupColumn("bindings", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed);
			}
		}
		if (isTableOpen) {
			ImGui::PushID(static_cast<int>(i));
			DrawAction(map, id);
			ImGui::PopID();
		}
	}
	if (isTableOpen) {
		ImGui::EndTable();
	}

	DrawListening(map);
}

// ===== 拾う係を動かす（フレームの頭）=====
void InputActionTable::UpdateListening(InputActionMap& map) {
	if (!rebinder_.IsListening()) {
		return;
	}
	// --- 前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、やめる ---
	// フレームの頭（ImGui::NewFrame の前）では、GetFrameCount は前のフレームの番号。前のフレームに描いていれば同じ
	if (lastDrawFrame_ < ImGui::GetFrameCount()) {
		rebinder_.Cancel();
		return;
	}
	// --- 拾えたら、表に入れる ---
	const InputActionId id = rebinder_.GetAction();
	const int index = rebinder_.GetIndex();
	if (rebinder_.Update() == InputRebinder::Result::Captured) {
		map.SetBinding(id, index, *rebinder_.GetCaptured());
	}
}


//=============================================================================
// Action 1行
//=============================================================================
void InputActionTable::DrawAction(InputActionMap& map, InputActionId id) {
	const InputActionDesc& desc = map.GetDesc(id);
	ImGui::TableNextRow();

	// --- 名前 ---
	ImGui::TableNextColumn();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(desc.label.c_str());
	if (!desc.tooltip.empty()) {
		ImGui::SetItemTooltip("%s", desc.tooltip.c_str());
	}

	// --- 割り当て（ボタンを横に並べる）と、足すボタン ---
	ImGui::TableNextColumn();
	const int count = static_cast<int>(map.GetBindings(id).size());
	for (int index = 0; index < count; ++index) {
		if (DrawBinding(map, id, index)) {
			break; // 外したので、後ろが詰まった（次のフレームで描き直す）
		}
		ImGui::SameLine();
	}
	if (map.GetBindings(id).empty()) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("（なし）");
		ImGui::SameLine();
	}
	if (desc.isFixed) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("変えられません");
	} else {
		if (SmallRowButton("+")) {
			rebinder_.Start(map, id, static_cast<int>(map.GetBindings(id).size()));
		}
		ImGui::SetItemTooltip("割り当てを足す（押してから、足したいキー・ボタンを押す）");
	}
	// ぶつかりは、割り当ての並びの下に1行ずつ
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		DrawConflicts(map, id, index);
	}

	// --- 既定に戻す ---
	ImGui::TableNextColumn();
	if (!desc.isFixed) {
		ImGui::BeginDisabled(map.IsDefault(id));
		if (ImGui::Button("既定")) {
			map.ResetToDefault(id);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("既定の割り当てに戻す（%s）", JoinBindings(desc.defaults).c_str());
		}
	}
}

// ===== 割り当て1つのボタン =====
bool InputActionTable::DrawBinding(InputActionMap& map, InputActionId id, int index) {
	const InputActionDesc& desc = map.GetDesc(id);
	const InputBinding binding = map.GetBindings(id)[index]; // 写しておく（外すと、元の物は消える）
	const bool isListeningHere = rebinder_.IsListening() && rebinder_.GetAction() == id && rebinder_.GetIndex() == index;
	const bool hasConflict = !map.FindConflicts(id, index).empty();

	ImGui::PushID(index);
	// --- ボタン（押すと拾い始める。ぶつかっていれば赤）---
	std::string text = isListeningHere ? std::string("押してください…") : InputBindingText::ToDisplayString(binding);
	if (text.empty()) {
		text = "?"; // 名前の無い入力（ふつうは無い）
	}
	if (hasConflict) {
		ImGui::PushStyleColor(ImGuiCol_Button, kConflictButtonColor);
	}
	ImGui::BeginDisabled(desc.isFixed);
	if (ImGui::Button(text.c_str())) {
		rebinder_.Start(map, id, index);
	}
	ImGui::EndDisabled();
	if (hasConflict) {
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", desc.isFixed ? "ほかの所でも、この入力を決め打ちで使っているので変えられません" : "押してから、新しいキー・ボタンを押すと入れ替わります");
	}

	// --- 外す ---
	bool isRemoved = false;
	if (!desc.isFixed) {
		ImGui::SameLine(0.0f, kRemoveButtonSpacing);
		if (SmallRowButton("×")) {
			map.RemoveBinding(id, index);
			isRemoved = true;
		}
		ImGui::SetItemTooltip("この割り当てを外す");
	}
	ImGui::PopID();
	return isRemoved;
}

// ===== ぶつかっている相手 =====
void InputActionTable::DrawConflicts(InputActionMap& map, InputActionId id, int index) {
	const std::vector<InputBindingRef> conflicts = map.FindConflicts(id, index);
	const std::string bindingText = InputBindingText::ToDisplayString(map.GetBindings(id)[index]);
	for (const InputBindingRef& other : conflicts) {
		const InputActionDesc& otherDesc = map.GetDesc(other.action);
		ImGui::PushID(index);
		ImGui::PushID(static_cast<int>(other.action.index));
		ImGui::PushID(other.index);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(kConflictTextColor, "! %s は「%s」とぶつかっています", bindingText.c_str(), otherDesc.label.c_str());
		bool isRemoved = false;
		if (!otherDesc.isFixed) {
			ImGui::SameLine();
			if (ImGui::SmallButton(std::format("「{}」の方を外す", otherDesc.label).c_str())) {
				map.RemoveBinding(other.action, other.index);
				isRemoved = true;
			}
		}
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();
		if (isRemoved) {
			break; // 相手の並びが詰まったので、残りの一覧は古い（次のフレームで描き直す）
		}
	}
}


//=============================================================================
// 拾っている間
//=============================================================================
void InputActionTable::DrawListening(const InputActionMap& map) {
	// 拾う係はここでは動かさない（UpdateListening）。ImGui を描く間は、InputManager がウィンドウを手前でない扱いにしていて、何も読めない
	// --- 案内のモーダル（拾い終えたら閉じる）---
	if (rebinder_.IsListening() && !ImGui::IsPopupOpen(kListeningPopupId)) {
		ImGui::OpenPopup(kListeningPopupId);
	}
	if (ImGui::BeginPopupModal(kListeningPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!rebinder_.IsListening()) {
			ImGui::CloseCurrentPopup();
		} else {
			ImGui::Text("「%s」に割り当てるキー・ボタンを押してください", map.GetDesc(rebinder_.GetAction()).label.c_str());
			ImGui::TextDisabled("Ctrl・Shift・Alt を押しながら押すと、組にして割り当てます（Ctrl+Z など）");
			ImGui::TextDisabled("Esc でやめる");
		}
		ImGui::EndPopup();
	}
}
#endif
