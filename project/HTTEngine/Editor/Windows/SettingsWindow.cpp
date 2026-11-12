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
std::vector<SettingsPage> SettingsWindow::pages_;

namespace {
constexpr float kListWidth = 170.0f; // 左の一覧の最初の幅（境目をドラッグで変えられる）
} // namespace

//=============================================================================
// ページ
//=============================================================================
// ===== 足す =====
void SettingsWindow::AddPage(SettingsPage page) {
	pages_.push_back(std::move(page));
}

// ===== 足したページを名前で =====
const SettingsPage* SettingsWindow::FindPage(const std::string& name) {
	for (const SettingsPage& page : pages_) {
		if (page.name == name) {
			return &page;
		}
	}
	return nullptr;
}

// ===== そのページ（グループか、足したページ）があるか =====
bool SettingsWindow::HasPage(const std::string& name) {
	if (FindPage(name)) {
		return true;
	}
	GlobalVariables* gv = GlobalVariables::GetInstance();
	for (const SettingsSection section : {SettingsSection::Project, SettingsSection::Editor}) {
		const std::vector<std::string> names = gv->GetGroupNames(section);
		if (std::find(names.begin(), names.end(), name) != names.end()) {
			return true;
		}
	}
	return false;
}

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
		DrawPageItem(name, gv->IsGroupDirty(name));
	}
	for (const SettingsPage& page : pages_) {
		if (page.section == SettingsSection::Project) {
			DrawPageItem(page.name, page.isDirty());
		}
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
		DrawPageItem(name, gv->IsGroupDirty(name));
	}
	for (const SettingsPage& page : pages_) {
		if (page.section == SettingsSection::Editor) {
			DrawPageItem(page.name, page.isDirty());
		}
	}
}

void SettingsWindow::DrawPageItem(const std::string& name, bool isDirty) {
	// * ＝保存していない変更がある。「###」の後ろを ID にする（* が付いても別の項目にならないように）
	const std::string label = std::format("{}{}###{}", name, isDirty ? " *" : "", name);
	if (ImGui::Selectable(label.c_str(), selected_ == name)) {
		selected_ = name;
	}
}

//=============================================================================
// 右のページ
//=============================================================================
// ===== ページ =====
void SettingsWindow::DrawPage() {
	if (selected_.empty()) {
		ImGui::TextDisabled("設定がまだ登録されていません");
		return;
	}

	// --- AddPage で足したページ：見出しだけここで描き、中身と保存は持ち主に任せる ---
	if (const SettingsPage* page = FindPage(selected_)) {
		switch (DrawPageHeader(page->name, page->isDirty(), page->saveLocation, page->revertTooltip.c_str())) {
		case HeaderAction::Revert:
			page->revert();
			break;
		case HeaderAction::Save:
			page->save();
			break;
		case HeaderAction::None:
			break;
		}
		page->draw();
		return;
	}

	// --- GlobalVariables のグループ ---
	GlobalVariables* gv = GlobalVariables::GetInstance();
	switch (DrawPageHeader(selected_, gv->IsGroupDirty(selected_), gv->GetSaveDirectory(selected_), "保存した値に戻します（Ctrl+Z で、戻す前にもどせます）")) {
	case HeaderAction::Revert:
		gv->RevertGroup(selected_); // 保存した値に戻す
		break;
	case HeaderAction::Save:
		gv->SaveGroup(selected_);
		break;
	case HeaderAction::None:
		break;
	}
	// 中身（カテゴリと項目）
	gv->DrawGroup(selected_);
}

// ===== 見出し：名前（* 付き）と、右端に Revert・Save（変更が無ければ押せない）=====
SettingsWindow::HeaderAction SettingsWindow::DrawPageHeader(const std::string& name, bool isDirty, const std::string& saveLocation, const char* revertTooltip) {
	HeaderAction action = HeaderAction::None;
	const ImGuiStyle& style = ImGui::GetStyle();
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端
	const float buttonsWidth = ImGui::CalcTextSize("Revert").x + ImGui::CalcTextSize("Save").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s%s", name.c_str(), isDirty ? " *" : "");
	ImGui::SameLine(right - buttonsWidth);
	ImGui::BeginDisabled(!isDirty);
	if (ImGui::Button("Revert")) {
		action = HeaderAction::Revert;
	}
	ImGui::SetItemTooltip("%s", revertTooltip);
	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		action = HeaderAction::Save;
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled("保存先: %s", saveLocation.c_str());
	ImGui::Separator();
	return action;
}
#endif