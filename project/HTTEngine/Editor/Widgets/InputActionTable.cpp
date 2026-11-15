#ifdef USE_IMGUI
#include "InputActionTable.h"

#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>

namespace {
constexpr float kLabelWidth = 200.0f;                               // Action の名前の列の幅
constexpr float kRowLabelWidth = 56.0f;                             // 行の名前（上・下・横の軸など）の幅
constexpr ImVec4 kConflictButtonColor = {0.62f, 0.20f, 0.20f, 1.0f}; // ぶつかっている割り当てのボタン
constexpr ImVec4 kConflictTextColor = {1.0f, 0.50f, 0.45f, 1.0f};    // ぶつかりの説明の文字
constexpr const char* kListeningPopupId = "割り当てを拾う";          // 拾っている間のモーダルの名前（ID にもなる）
constexpr float kSmallButtonScale = 0.75f;                          // ＋ の文字の大きさ（ふつうの文字の何倍か）
constexpr const char* kBindingMenuId = "##bindingMenu";             // 割り当てを右クリックしたメニュー
constexpr const char* kRowMenuId = "##rowMenu";                     // 行の空いている所を右クリックしたメニュー
constexpr const char* kLabelMenuId = "##labelMenu";                 // Action の名前を右クリックしたメニュー

// 小さいボタン（＋）。文字を小さくし、同じ行の割り当てのボタンと上下の真ん中をそろえる
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

// 割り当ての一覧を "A / B" の文字にする（「既定に戻す」の横に出す）
std::string JoinBindings(const std::vector<InputBinding>& bindings) {
	std::string text;
	for (const InputBinding& binding : bindings) {
		text += text.empty() ? "" : " / ";
		text += InputBindingText::ToDisplayString(binding);
	}
	return text.empty() ? "（なし）" : text;
}

// 行の名前（All は出さない）
const char* RowLabel(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::All:
		return "";
	case InputBindingRow::Positive:
		return "＋";
	case InputBindingRow::Negative:
		return "−";
	case InputBindingRow::Axis:
		return "軸";
	case InputBindingRow::Up:
		return "上";
	case InputBindingRow::Down:
		return "下";
	case InputBindingRow::Left:
		return "左";
	case InputBindingRow::Right:
		return "右";
	case InputBindingRow::Horizontal:
		return "横の軸";
	case InputBindingRow::Vertical:
		return "縦の軸";
	}
	return "";
}

// 行の説明（行の名前の吹き出し）
const char* RowTooltip(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::Positive:
	case InputBindingRow::Negative:
		return "押している間、この向きに 1 を足すキー・ボタン";
	case InputBindingRow::Axis:
		return "量をそのまま足す軸（トリガー・ホイール・スティック）。（反対）は、引いて足す";
	case InputBindingRow::Up:
	case InputBindingRow::Down:
	case InputBindingRow::Left:
	case InputBindingRow::Right:
		return "押している間、この向きに 1 を足すキー・ボタン";
	case InputBindingRow::Horizontal:
		return "横の量をそのまま足す軸（スティックの横・マウスの横の動き）。両側に効く（＋に倒すと右）。（反対）は左右が逆";
	case InputBindingRow::Vertical:
		return "縦の量をそのまま足す軸（スティックの縦・マウスの縦の動き）。両側に効く（＋に倒すと上）。（反対）は上下が逆";
	default:
		return "";
	}
}

// 行に何か出すか（入れられる入力があるか、もう割り当てが入っている）
bool HasRow(const InputActionMap& map, InputActionId id, InputBindingRow row) {
	if ((map.GetAcceptedDevices(id) & InputBindingRows::DevicesOf(row)) != InputDeviceMask::None) {
		return true;
	}
	const InputActionType type = map.GetDesc(id).type;
	for (const InputBinding& binding : map.GetBindings(id)) {
		if (InputBindingRows::Of(type, binding) == row) {
			return true;
		}
	}
	return false;
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
			isTableOpen = ImGui::BeginTable(std::format("##group{}", i).c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
			if (isTableOpen) {
				ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
				ImGui::TableSetupColumn("bindings", ImGuiTableColumnFlags_WidthStretch);
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
// Action 1つ
//=============================================================================
void InputActionTable::DrawAction(InputActionMap& map, InputActionId id) {
	const InputActionDesc& desc = map.GetDesc(id);
	bool isFirstRow = true;
	for (const InputBindingRow row : InputBindingRows::Get(desc.type)) {
		if (!HasRow(map, id, row)) {
			continue; // 入れられる入力が無い行（ホイールだけの Value の ＋・− など）
		}
		ImGui::TableNextRow();
		ImGui::PushID(static_cast<int>(row));

		// --- 名前（1行目だけ。既定から変えていれば * を付ける。右クリックで「足す」「既定に戻す」）---
		ImGui::TableNextColumn();
		if (isFirstRow) {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s%s", desc.label.c_str(), (!desc.isFixed && !map.IsDefault(id)) ? " *" : "");
			if (!desc.tooltip.empty()) {
				ImGui::SetItemTooltip("%s", desc.tooltip.c_str());
			}
			DrawActionMenu(map, id, row, kLabelMenuId);
		}

		// --- 割り当て ---
		ImGui::TableNextColumn();
		DrawRow(map, id, row);
		ImGui::PopID();
		isFirstRow = false;
	}
}

// ===== 割り当ての1行 =====
void InputActionTable::DrawRow(InputActionMap& map, InputActionId id, InputBindingRow row) {
	const InputActionDesc& desc = map.GetDesc(id);
	// --- 行の名前（Button は出さない）---
	if (row != InputBindingRow::All) {
		const float startX = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", RowLabel(row));
		ImGui::SetItemTooltip("%s", RowTooltip(row));
		ImGui::SameLine();
		ImGui::SetCursorPosX(startX + kRowLabelWidth); // 行の名前の幅をそろえる（割り当てのボタンが縦に並ぶように）
	}

	// --- 足す（行の左端。並びが変わっても同じ所にあるように）---
	if (!desc.isFixed) {
		if (SmallRowButton("+")) {
			StartAdd(map, id, row);
		}
		ImGui::SetItemTooltip("%s", InputBindingRows::IsAxisRow(row) ? "軸を足す（押してから、スティックを ＋ の向き（右・上）へ倒す。反対へ倒すと、反対に足す）" : "割り当てを足す（押してから、足したいキー・ボタンを押す）");
		ImGui::SameLine();
	}

	// --- この行の割り当て（ボタンを横に並べる）---
	bool hasAny = false;
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		if (InputBindingRows::Of(desc.type, map.GetBindings(id)[index]) != row) {
			continue;
		}
		hasAny = true;
		if (DrawBinding(map, id, index, row)) {
			return; // 消したので、後ろが詰まった（次のフレームで描き直す）
		}
		ImGui::SameLine();
	}
	if (!hasAny) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("（なし）");
		ImGui::SameLine();
	}
	if (desc.isFixed) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("変えられません");
	} else {
		// --- 空いている所（右クリックで「足す」「既定に戻す」）---
		// 見えないボタンで行の右端まで埋め、そこへの右クリックを受ける
		const float width = ImGui::GetContentRegionAvail().x;
		if (width > 1.0f) {
			ImGui::InvisibleButton("##rowSpace", ImVec2(width, ImGui::GetFrameHeight()));
			DrawActionMenu(map, id, row, kRowMenuId);
		} else {
			ImGui::NewLine();
		}
	}

	// --- ぶつかり（この行の割り当てごとに、並びの下に1行ずつ）---
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		if (InputBindingRows::Of(desc.type, map.GetBindings(id)[index]) == row) {
			DrawConflicts(map, id, index);
		}
	}
}

// ===== 割り当て1つのボタン =====
bool InputActionTable::DrawBinding(InputActionMap& map, InputActionId id, int index, InputBindingRow row) {
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
	// 軸の行で、反対に足す割り当て（-X・-Y）
	if (!isListeningHere && InputBindingRows::IsAxisRow(row) && InputBindingRows::DirectionOf(row) != binding.direction) {
		text += "（反対）";
	}
	if (hasConflict) {
		ImGui::PushStyleColor(ImGuiCol_Button, kConflictButtonColor);
	}
	ImGui::BeginDisabled(desc.isFixed);
	if (ImGui::Button(text.c_str())) {
		rebinder_.Start(map, id, index, row);
	}
	ImGui::EndDisabled();
	if (hasConflict) {
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", desc.isFixed ? "ほかの所でも、この入力を決め打ちで使っているので変えられません" : "押してから、新しいキー・ボタンを押すと入れ替わります（右クリックで 変更・消す）");
	}

	// --- 右クリックのメニュー（変更・消す）---
	bool isRemoved = false;
	if (!desc.isFixed && ImGui::BeginPopupContextItem(kBindingMenuId)) {
		if (ImGui::MenuItem("変更")) {
			rebinder_.Start(map, id, index, row);
		}
		ImGui::SetItemTooltip("押した後に、新しいキー・ボタンを押すと入れ替わる");
		if (ImGui::MenuItem("消す")) {
			map.RemoveBinding(id, index);
			isRemoved = true;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return isRemoved;
}

// ===== 右クリックのメニュー（行の空いている所・Action の名前）=====
void InputActionTable::DrawActionMenu(InputActionMap& map, InputActionId id, InputBindingRow row, const char* popupId) {
	const InputActionDesc& desc = map.GetDesc(id);
	if (desc.isFixed || !ImGui::BeginPopupContextItem(popupId)) {
		return;
	}
	// --- 足す（その行の向きで拾う）---
	const std::string addLabel = (row == InputBindingRow::All) ? std::string("足す") : std::format("「{}」に足す", RowLabel(row));
	if (ImGui::MenuItem(addLabel.c_str())) {
		StartAdd(map, id, row);
	}
	// --- 既定に戻す（Action の割り当てを全部。横に既定の割り当てを出す）---
	if (ImGui::MenuItem(resetLabel_.c_str(), JoinBindings(desc.defaults).c_str(), false, !map.IsDefault(id))) {
		map.ResetToDefault(id);
	}
	ImGui::EndPopup();
}

// ===== 行に割り当てを足し始める =====
void InputActionTable::StartAdd(const InputActionMap& map, InputActionId id, InputBindingRow row) {
	rebinder_.Start(map, id, static_cast<int>(map.GetBindings(id).size()), row);
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
			const InputBindingRow row = rebinder_.GetRow();
			const std::string& label = map.GetDesc(rebinder_.GetAction()).label;
			if (row == InputBindingRow::All) {
				ImGui::Text("「%s」に割り当てるキー・ボタンを押してください", label.c_str());
			} else if (InputBindingRows::IsAxisRow(row)) {
				ImGui::Text("「%s」の「%s」に割り当てる軸を、＋ の向き（右・上）へ動かしてください", label.c_str(), RowLabel(row));
				ImGui::TextDisabled("反対の向きへ動かすと、反対に足す割り当てになります");
			} else {
				ImGui::Text("「%s」の「%s」に割り当てるキー・ボタンを押してください", label.c_str(), RowLabel(row));
			}
			ImGui::TextDisabled("Ctrl・Shift・Alt を押しながら押すと、組にして割り当てます（Ctrl+Z など）");
			ImGui::TextDisabled("Esc でやめる");
		}
		ImGui::EndPopup();
	}
}
#endif
