#ifdef USE_IMGUI
#include "GameInputPage.h"

#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Editor/Widgets/InputActionTable.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Input/Action/GameInput.h"
#include "HTTEngine/Input/Action/InputActionFile.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
constexpr const char* kPageName = "Input";              // Settings の一覧の名前
constexpr const char* kNewActionName = "NewAction";      // 足した Action の名前（重なれば後ろに番号）
constexpr float kListHeight = 180.0f;                    // Action の一覧の高さ（ピクセル。多ければスクロール）
constexpr ImVec4 kErrorColor = {1.0f, 0.50f, 0.45f, 1.0f}; // 保存できない理由の文字

//=============================================================================
// 持ち物
//=============================================================================
struct PageState {
	std::vector<InputActionDesc> saved;  // ファイルの中身（Revert で戻る先。表の「既定」もこれ）
	std::vector<InputActionDesc> edited; // 直している中身（defaults が、今の割り当て）
	bool hasFile = false;                // ファイルがあるか（無ければ Save で作る）
	// 表に見せる表（edited から作る。Action の番号＝ edited の番号）
	// エディタの決まりで読む（このページを触っている間も拾える）。修飾キーの合わせ方はゲームと同じ（ぶつかりの見え方をゲームとそろえる）
	InputActionMap map{InputGate::Editor, InputModifierMatch::AtLeast};
	InputActionTable table{"保存した割り当てに戻す"};
	int selected = -1; // 詳しい所を出している Action（edited の番号。無ければ -1）
};
PageState& State() {
	static PageState state;
	return state;
}

//=============================================================================
// 表と、直している中身の行き来
//=============================================================================
// ===== 保存した中身から、同じ名前の Action を探す（無ければ nullptr）=====
const InputActionDesc* FindSaved(const std::string& name) {
	for (const InputActionDesc& desc : State().saved) {
		if (desc.name == name) {
			return &desc;
		}
	}
	return nullptr;
}

// ===== 直している中身から、表を作り直す（Action を足す・消す・名前や種類を変えたとき）=====
// 表の「既定」は保存した割り当て（右クリックの「保存した割り当てに戻す」）。今の割り当ては SetBindings で入れる
void RebuildMap() {
	PageState& state = State();
	state.table.CancelListening(); // 表の Action の番号が変わるかもしれないので
	state.map = InputActionMap(InputGate::Editor, InputModifierMatch::AtLeast);
	for (size_t i = 0; i < state.edited.size(); ++i) {
		const InputActionDesc& desc = state.edited[i];
		InputActionDesc view = desc;
		// 表の中の名前は番号にする（直している途中の名前は、空・重なりがありうる。表の名前は1つだけでないといけない）
		view.name = std::to_string(i);
		view.label = desc.label.empty() ? desc.name : desc.label;
		view.isFixed = false; // ページでは「変えられない」Action の既定も直す（変えられないのはプレイヤー）
		const InputActionDesc* saved = FindSaved(desc.name);
		view.defaults = saved ? saved->defaults : std::vector<InputBinding>{};
		const InputActionId id = state.map.AddAction(std::move(view));
		state.map.SetBindings(id, desc.defaults);
	}
}

// ===== 表で直した割り当てを、直している中身へ写す =====
void SyncFromMap() {
	PageState& state = State();
	for (size_t i = 0; i < state.edited.size() && i < state.map.GetActionCount(); ++i) {
		state.edited[i].defaults = state.map.GetBindings({static_cast<uint32_t>(i)});
	}
}

// ===== 保存できない理由（名前が空・重なっている。無ければ空）=====
std::string FindNameError() {
	std::unordered_set<std::string> names;
	for (const InputActionDesc& desc : State().edited) {
		if (desc.name.empty()) {
			return "名前が空の Action があります";
		}
		if (!names.insert(desc.name).second) {
			return std::format("同じ名前の Action が2つあります: {}", desc.name);
		}
	}
	return {};
}

//=============================================================================
// 読む・保存・戻す
//=============================================================================
// ===== ファイルを読む =====
void LoadFile() {
	PageState& state = State();
	const std::optional<std::vector<InputActionDesc>> loaded = InputActionFile::Load(GameInput::kActionsPath);
	state.hasFile = loaded.has_value();
	state.saved = loaded.value_or(std::vector<InputActionDesc>{});
	state.edited = state.saved;
	state.selected = state.edited.empty() ? -1 : 0;
	RebuildMap();
}

// ===== 保存（名前に問題があれば保存しない）=====
void Save() {
	PageState& state = State();
	SyncFromMap();
	const std::string error = FindNameError();
	if (!error.empty()) {
		LogManager::Error("[GameInputPage] 保存しませんでした: " + error);
		return;
	}
	if (!InputActionFile::Save(GameInput::kActionsPath, state.edited)) {
		return;
	}
	state.saved = state.edited;
	state.hasFile = true;
	RebuildMap();               // 表の「既定」（保存した割り当て）を新しくする
	GameInput::ReloadActions(); // ゲームの表へ（Play 中でも効く）
}

// ===== 保存した状態に戻す =====
void Revert() {
	PageState& state = State();
	state.edited = state.saved;
	if (state.selected >= static_cast<int>(state.edited.size())) {
		state.selected = static_cast<int>(state.edited.size()) - 1;
	}
	RebuildMap();
}

// ===== 保存していない変更があるか =====
bool IsDirty() {
	SyncFromMap();
	return State().edited != State().saved;
}

//=============================================================================
// Action を足す・消す・並べ替える
//=============================================================================
// ===== 重ならない名前（"NewAction"・"NewAction2"…）=====
std::string MakeUniqueName() {
	std::unordered_set<std::string> names;
	for (const InputActionDesc& desc : State().edited) {
		names.insert(desc.name);
	}
	std::string name = kNewActionName;
	for (int n = 2; names.contains(name); ++n) {
		name = std::format("{}{}", kNewActionName, n);
	}
	return name;
}

// ===== 足す（選んでいる Action の後ろ。同じ group にする）=====
void AddAction() {
	PageState& state = State();
	InputActionDesc desc;
	desc.name = MakeUniqueName();
	desc.label = desc.name;
	const int insertAt = (state.selected >= 0) ? state.selected + 1 : static_cast<int>(state.edited.size());
	if (state.selected >= 0) {
		desc.group = state.edited[state.selected].group;
	}
	state.edited.insert(state.edited.begin() + insertAt, std::move(desc));
	state.selected = insertAt;
	RebuildMap();
}

// ===== 消す =====
void RemoveAction(int index) {
	PageState& state = State();
	state.edited.erase(state.edited.begin() + index);
	if (state.selected >= static_cast<int>(state.edited.size())) {
		state.selected = static_cast<int>(state.edited.size()) - 1;
	}
	RebuildMap();
}

// ===== 1つ前・後ろと入れ替える（offset は -1 か 1）=====
void MoveAction(int index, int offset) {
	PageState& state = State();
	const int other = index + offset;
	if (other < 0 || other >= static_cast<int>(state.edited.size())) {
		return;
	}
	std::swap(state.edited[index], state.edited[other]);
	state.selected = other;
	RebuildMap();
}

//=============================================================================
// 描く
//=============================================================================
// ===== Action の一覧（押すと選ぶ）=====
void DrawActionList() {
	PageState& state = State();
	ImGui::SeparatorText("Action");
	if (ImGui::BeginTable("##actions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, kListHeight))) {
		ImGui::TableSetupScrollFreeze(0, 1); // 見出しの行はスクロールしない
		ImGui::TableSetupColumn("名前");
		ImGui::TableSetupColumn("種類");
		ImGui::TableSetupColumn("グループ");
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(state.edited.size()); ++i) {
			const InputActionDesc& desc = state.edited[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			const std::string name = desc.name.empty() ? std::string("（名前なし）") : desc.name;
			if (ImGui::Selectable(name.c_str(), state.selected == i, ImGuiSelectableFlags_SpanAllColumns)) {
				state.selected = i;
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(magic_enum::enum_name(desc.type).data());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(desc.group.c_str());
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (ImGui::Button("Action を足す")) {
		AddAction();
	}
	ImGui::SetItemTooltip("選んでいる Action の後ろに足す（同じグループになる）");
}

// ===== 選んだ Action の詳しい所（変えたら true＝表を作り直す）=====
bool DrawActionDetail(InputActionDesc& desc) {
	bool isChanged = false;
	ImGui::SeparatorText("選んだ Action");
	// --- 名前・見せ方 ---
	isChanged |= EditorWidgets::InputText("名前", desc.name);
	ImGui::SetItemTooltip("Script から読む名前（GameInput::IsTriggered(\"Jump\")）。変えると、Script と、プレイヤーが保存した割り当て（上書き）が当たらなくなる");
	isChanged |= EditorWidgets::InputText("表示名", desc.label);
	ImGui::SetItemTooltip("画面に出す名前（キーコンフィグの画面・この表）。空なら名前");
	isChanged |= EditorWidgets::InputText("グループ", desc.group);
	ImGui::SetItemTooltip("画面の見出し（続けて並んだ同じグループを、まとめて出す）");
	isChanged |= EditorWidgets::InputText("効く場所", desc.context);
	ImGui::SetItemTooltip("ぶつかりを調べる範囲（\"Gameplay\"・\"Menu\" など）。同じ文字どうしと、空（どこでも効く）との間だけ調べる");
	isChanged |= EditorWidgets::InputText("説明", desc.tooltip);
	ImGui::SetItemTooltip("表の名前にマウスを乗せると出る説明");

	// --- 種類 ---
	if (ImGui::BeginCombo("種類", magic_enum::enum_name(desc.type).data())) {
		for (const InputActionType type : magic_enum::enum_values<InputActionType>()) {
			if (ImGui::Selectable(magic_enum::enum_name(type).data(), desc.type == type)) {
				desc.type = type;
				isChanged = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Button：押す・離す（ジャンプ）　Value：1つの量（トリガー・ホイール）　Vector2：2つの量（移動）");
	if (desc.type == InputActionType::Vector2) {
		isChanged |= ImGui::Checkbox("長さを 1 までにする", &desc.clampLength);
		ImGui::SetItemTooltip("キーの斜め・キーとスティックを一緒に倒したときに、速くならないように（移動に。マウスの動きには付けない）");
	}
	isChanged |= ImGui::Checkbox("プレイヤーが変えられない", &desc.isFixed);
	ImGui::SetItemTooltip("キーコンフィグの画面で変えられない（ここでは直せる）。ほかの操作とのぶつかりを見せるために出す物に");

	// --- 割り当てられる入力（全部外すと、種類で決める）---
	ImGui::TextUnformatted("割り当てられる入力");
	ImGui::SetItemTooltip("1つも選ばなければ、種類で決める（Button はキー・ボタンだけ、ほかは全部）。マウスの動きとスティックは単位が違うので、1つの Action に混ぜない");
	for (const InputDevice device : magic_enum::enum_values<InputDevice>()) {
		if (device == InputDevice::None) {
			continue;
		}
		const InputDeviceMask bit = static_cast<InputDeviceMask>(1 << static_cast<uint8_t>(device));
		bool isOn = Contains(desc.accepts, device);
		ImGui::SameLine();
		if (ImGui::Checkbox(magic_enum::enum_name(device).data(), &isOn)) {
			desc.accepts = isOn ? (desc.accepts | bit) : static_cast<InputDeviceMask>(static_cast<uint8_t>(desc.accepts) & ~static_cast<uint8_t>(bit));
			isChanged = true;
		}
	}
	return isChanged;
}

// ===== 選んだ Action の並べ替え・消す =====
void DrawActionButtons(int index) {
	if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
		MoveAction(index, -1);
	}
	ImGui::SetItemTooltip("1つ前へ（画面に出す順）");
	ImGui::SameLine();
	if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
		MoveAction(index, 1);
	}
	ImGui::SetItemTooltip("1つ後ろへ");
	ImGui::SameLine();
	if (ImGui::Button("消す")) {
		RemoveAction(index);
	}
	ImGui::SetItemTooltip("この Action を消す（Save するまでファイルは変わらない。Revert で戻せる）");
}

// ===== プレイヤーの上書き（エディタで遊んで保存した割り当て）=====
void DrawPlayerOverrides() {
	ImGui::SeparatorText("プレイヤーの上書き");
	std::vector<std::filesystem::path> files; // ある物だけ
	for (int player = 0; player < GameInput::kMaxPlayers; ++player) {
		std::error_code ec;
		const std::filesystem::path path = GameInput::GetBindingsPath(player);
		if (std::filesystem::exists(path, ec)) {
			files.push_back(path);
		}
	}
	ImGui::TextDisabled("保存先: %s", ToUtf8(GameInput::GetBindingsPath(0).parent_path()).c_str());
	if (files.empty()) {
		ImGui::TextDisabled("ありません（Play 中にゲームが保存すると、ここの割り当てが既定より先に効く）");
		return;
	}
	ImGui::Text("%zu 人分あります。Play 中は、既定より先にこちらが効きます", files.size());
	if (ImGui::Button("プレイヤーの上書きを消す")) {
		for (const std::filesystem::path& path : files) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
		GameInput::ReloadActions(); // ゲームの表を既定に戻す
		LogManager::Log("[GameInputPage] プレイヤーの上書きを消しました");
	}
	ImGui::SetItemTooltip("エディタで遊んで保存した割り当てを消し、既定から始まるようにする（UserData/ は git に入れない、自分の PC の物だけ）");
}
} // namespace


//=============================================================================
// 初期化・毎フレーム
//=============================================================================
// ===== 初期化 =====
void GameInputPage::Initialize() {
	LoadFile();
	SettingsWindow::AddPage({
	    .name = kPageName,
	    .section = SettingsSection::Project,
	    .draw = &GameInputPage::Draw,
	    .isDirty = &IsDirty,
	    .revert = &Revert,
	    .save = &Save,
	    .saveLocation = GameInput::kActionsPath,
	    .revertTooltip = "保存した Action と割り当てに戻します",
	});
}

// ===== 毎フレーム =====
void GameInputPage::Update() {
	PageState& state = State();
	// 割り当てを拾う（入力を読めるのはここだけ。ページを閉じていたら、拾うのをやめる）
	state.table.UpdateListening(state.map);
}


//=============================================================================
// ページ
//=============================================================================
void GameInputPage::Draw() {
	PageState& state = State();
	SyncFromMap();

	// --- ファイルの様子・保存できない理由 ---
	if (!state.hasFile) {
		ImGui::TextDisabled("ファイルがまだありません。Action を足して Save すると作ります");
	}
	const std::string error = FindNameError();
	if (!error.empty()) {
		ImGui::TextColored(kErrorColor, "保存できません: %s", error.c_str());
	}
	ImGui::TextDisabled("Script から GameInput::IsTriggered(\"名前\") で読む。Save すると、Play 中でもすぐゲームに効く");

	// --- Action の一覧と、選んだ Action ---
	DrawActionList();
	if (state.selected >= 0 && state.selected < static_cast<int>(state.edited.size())) {
		ImGui::PushID(state.selected);
		const int index = state.selected;
		if (DrawActionDetail(state.edited[index])) {
			RebuildMap();
		}
		DrawActionButtons(index);
		ImGui::PopID();
	}

	// --- 割り当て ---
	ImGui::SeparatorText("割り当て");
	state.table.Draw(state.map);
	SyncFromMap(); // 表で外した・既定に戻した物を写す

	DrawPlayerOverrides();
}
#endif
