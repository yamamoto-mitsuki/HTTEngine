#ifdef USE_IMGUI
#include "SettingsWindow.h"

#include <algorithm>
#include <format>
#include <vector>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/UI/GlobalVariables.h"

// 静的メンバ変数
std::string SettingsWindow::selected_;
bool SettingsWindow::focusRequest_ = false;

namespace {
constexpr float kListWidth = 170.0f; // 左の一覧の最初の幅（境目をドラッグで変えられる）

// そのページ（グループ）が登録されているか
bool HasPage(const std::string& groupName) {
	GlobalVariables* gv = GlobalVariables::GetInstance();
	for (const SettingsSection section : {SettingsSection::Project, SettingsSection::Editor}) {
		const std::vector<std::string> names = gv->GetGroupNames(section);
		if (std::find(names.begin(), names.end(), groupName) != names.end()) {
			return true;
		}
	}
	return false;
}
} // namespace

//=============================================================================
// 描画
//=============================================================================
void SettingsWindow::Draw() {
	GlobalVariables* gv = GlobalVariables::GetInstance();
	// 選んでいたページが無い（最初・まだ登録されていない）なら、Project の最初のページにする
	if (!HasPage(selected_)) {
		const std::vector<std::string> names = gv->GetGroupNames(SettingsSection::Project);
		selected_ = names.empty() ? std::string() : names.front();
	}

	if (focusRequest_) {
		ImGui::SetNextWindowFocus();
		focusRequest_ = false;
	}
	ImGui::SetNextWindowSize(ImVec2(640.0f, 420.0f), ImGuiCond_FirstUseEver); // 初めて出すときの大きさ（後は imgui.ini に覚えられる）
	if (ImGui::Begin("Settings")) {
		// 左右に分ける（境目はドラッグで動かせる）
		if (ImGui::BeginTable("##settingsLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, kListWidth);
			ImGui::TableSetupColumn("page", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();
			// --- 左：一覧 ---
			ImGui::TableNextColumn();
			if (ImGui::BeginChild("##pageList")) {
				DrawPageList();
			}
			ImGui::EndChild();
			// --- 右：ページ ---
			ImGui::TableNextColumn();
			if (ImGui::BeginChild("##page")) {
				DrawPage();
			}
			ImGui::EndChild();
			ImGui::EndTable();
		}
	}
	ImGui::End();

	// 描き終わった。どの項目も触っていなければ、ここまでの変更を1件の履歴にする（Ctrl+Z で戻せる）
	gv->EndDraw();
}

//=============================================================================
// 左の一覧
//=============================================================================
void SettingsWindow::DrawPageList() {
	GlobalVariables* gv = GlobalVariables::GetInstance();

	// --- Project：ゲーム全体（Parameters/<名前>/。製品版にも入る）---
	ImGui::SeparatorText("Project");
	for (const std::string& name : gv->GetGroupNames(SettingsSection::Project)) {
		DrawPageItem(name);
	}

	// --- Scene：開いているシーンの設定（シーンファイルに入る）---
	const SceneManager* sceneManager = SceneManager::GetMain();
	const std::string sceneName = sceneManager ? sceneManager->GetOpenSceneName() : std::string();
	ImGui::SeparatorText(std::format("Scene: {}", sceneName).c_str());
	ImGui::TextDisabled("まだ項目はありません");
	ImGui::SetItemTooltip("空・IBL（環境光）を SceneFlow.md の SF4 でここに足します");

	// --- Editor：エディタだけ（Parameters/Editor/<名前>/。製品版では使わない）---
	ImGui::SeparatorText("Editor");
	for (const std::string& name : gv->GetGroupNames(SettingsSection::Editor)) {
		DrawPageItem(name);
	}
}

void SettingsWindow::DrawPageItem(const std::string& groupName) {
	// * ＝保存していない変更がある。「###」の後ろを ID にする（* が付いても別の項目にならないように）
	const bool isDirty = GlobalVariables::GetInstance()->IsGroupDirty(groupName);
	const std::string label = std::format("{}{}###{}", groupName, isDirty ? " *" : "", groupName);
	if (ImGui::Selectable(label.c_str(), selected_ == groupName)) {
		selected_ = groupName;
	}
}

//=============================================================================
// 右のページ
//=============================================================================
void SettingsWindow::DrawPage() {
	if (selected_.empty()) {
		ImGui::TextDisabled("設定がまだ登録されていません");
		return;
	}
	GlobalVariables* gv = GlobalVariables::GetInstance();
	const bool isDirty = gv->IsGroupDirty(selected_);

	// --- 見出し：名前（* 付き）と、右端に Revert・Save（変更が無ければ押せない）---
	const ImGuiStyle& style = ImGui::GetStyle();
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端
	const float buttonsWidth = ImGui::CalcTextSize("Revert").x + ImGui::CalcTextSize("Save").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s%s", selected_.c_str(), isDirty ? " *" : "");
	ImGui::SameLine(right - buttonsWidth);
	ImGui::BeginDisabled(!isDirty);
	if (ImGui::Button("Revert")) {
		gv->RevertGroup(selected_); // 保存した値に戻す
	}
	ImGui::SetItemTooltip("保存した値に戻します（Ctrl+Z で、戻す前にもどせます）");
	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		gv->SaveGroup(selected_);
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled("保存先: %s", gv->GetSaveDirectory(selected_).c_str());
	ImGui::Separator();

	// --- 中身（カテゴリと項目）---
	gv->DrawGroup(selected_);
}
#endif