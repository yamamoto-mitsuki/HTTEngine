#ifdef USE_IMGUI
#include "SceneDialogs.h"

#include <vector>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/FileNameRule.h"

// 静的メンバ変数
SceneDialogs::Dialog SceneDialogs::openRequest_ = SceneDialogs::Dialog::None;
std::string SceneDialogs::pendingOpen_;
std::string SceneDialogs::nameBuffer_;
bool SceneDialogs::nameFocusRequest_ = false;

namespace {
// シーンのファイルを触れるか（エディタのシーンマネージャがあって、停止中）
bool CanEditScene() {
	const SceneManager* sceneManager = SceneManager::GetMain();
	return sceneManager != nullptr && sceneManager->GetPlayState() == PlayState::Editing;
}

// 次に出すウィンドウを、画面の真ん中に置く
void CenterNextWindow() { ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); }
} // namespace

//=============================================================================
// 頼む（Project ウィンドウ・メニュー・ショートカットから）
//=============================================================================
void SceneDialogs::RequestOpen(const std::string& name) {
	SceneManager* sceneManager = SceneManager::GetMain();
	if (!CanEditScene() || name == sceneManager->GetOpenSceneName()) {
		return; // 再生中・もう開いている
	}
	// 保存していない変更があれば、先に聞く（答えを聞いてから開く）
	if (sceneManager->IsDirty()) {
		pendingOpen_ = name;
		openRequest_ = Dialog::Unsaved;
		return;
	}
	sceneManager->RequestOpenScene(name);
}

void SceneDialogs::RequestNew() {
	if (!CanEditScene()) {
		return;
	}
	nameBuffer_.clear();
	nameFocusRequest_ = true;
	openRequest_ = Dialog::NewScene;
}

void SceneDialogs::RequestSaveAs() {
	const SceneManager* sceneManager = SceneManager::GetMain();
	if (!CanEditScene() || sceneManager->GetSceneFile() == nullptr) {
		return; // シーンファイルを使わない前の書き方のシーンは、保存できない
	}
	nameBuffer_ = sceneManager->GetOpenSceneName(); // 今の名前から直して付けられるように
	nameFocusRequest_ = true;
	openRequest_ = Dialog::SaveAs;
}

//=============================================================================
// 毎フレーム
//=============================================================================
void SceneDialogs::Draw() {
	HandleShortcuts();
	// 預かっていた確認のウィンドウを開く（ここはどのポップアップの中でもないので、入れ子にならない）
	if (openRequest_ != Dialog::None) {
		ImGui::OpenPopup(TitleOf(openRequest_));
		openRequest_ = Dialog::None;
	}
	DrawUnsavedDialog();
	DrawNameDialog(Dialog::NewScene);
	DrawNameDialog(Dialog::SaveAs);
}

const char* SceneDialogs::TitleOf(Dialog dialog) {
	switch (dialog) {
	case Dialog::Unsaved:
		return "Unsaved Changes";
	case Dialog::NewScene:
		return "New Scene";
	case Dialog::SaveAs:
		return "Save Scene As";
	case Dialog::None:
		break;
	}
	return "";
}

//=============================================================================
// File メニューの中身
//=============================================================================
void SceneDialogs::DrawFileMenuItems() {
	SceneManager* sceneManager = SceneManager::GetMain();
	const bool canEdit = CanEditScene();
	const bool hasFile = canEdit && sceneManager->GetSceneFile() != nullptr;

	if (ImGui::MenuItem("New Scene...", EditorKeymap::GetShortcutText(EditorAction::NewScene).c_str(), false, canEdit)) {
		RequestNew();
	}
	// resources/scenes/ のシーンの一覧（開いているシーンにはチェック）
	if (ImGui::BeginMenu("Open Scene", canEdit)) {
		const std::vector<std::string> names = SceneManager::GetSceneNames();
		if (names.empty()) {
			ImGui::TextDisabled("resources/scenes/ にシーンがありません");
		}
		for (const std::string& name : names) {
			const bool isOpen = (name == sceneManager->GetOpenSceneName());
			if (ImGui::MenuItem(name.c_str(), nullptr, isOpen)) {
				RequestOpen(name);
			}
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Save", EditorKeymap::GetShortcutText(EditorAction::SaveScene).c_str(), false, hasFile)) {
		sceneManager->RequestSave();
	}
	if (ImGui::MenuItem("Save As...", EditorKeymap::GetShortcutText(EditorAction::SaveSceneAs).c_str(), false, hasFile)) {
		RequestSaveAs();
	}
}

//=============================================================================
// 保存しますか？
//=============================================================================
void SceneDialogs::DrawUnsavedDialog() {
	CenterNextWindow();
	if (!ImGui::BeginPopupModal(TitleOf(Dialog::Unsaved), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	SceneManager* sceneManager = SceneManager::GetMain();
	if (sceneManager == nullptr) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}
	ImGui::Text("%s に保存していない変更があります。", sceneManager->GetOpenSceneName().c_str());
	ImGui::Text("%s を開く前に保存しますか？", pendingOpen_.c_str());
	ImGui::Spacing();

	// Save：保存してから開く（SceneManager は同じフレームに頼まれたら「保存 → 開く」の順にする。保存できなければ開かない）
	if (ImGui::Button("Save")) {
		sceneManager->RequestSave();
		sceneManager->RequestOpenScene(pendingOpen_);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	// Don't Save：変更を捨てて開く
	if (ImGui::Button("Don't Save")) {
		sceneManager->RequestOpenScene(pendingOpen_);
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	// Cancel：開かない（今のシーンのまま）
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

//=============================================================================
// 名前を聞く（New Scene・Save As）
//=============================================================================
void SceneDialogs::DrawNameDialog(Dialog dialog) {
	CenterNextWindow();
	if (!ImGui::BeginPopupModal(TitleOf(dialog), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// --- 名前の入力欄（使えない文字は打っても入らない）---
	if (nameFocusRequest_) {
		ImGui::SetKeyboardFocusHere();
		nameFocusRequest_ = false;
	}
	ImGui::SetNextItemWidth(280.0f);
	const bool entered = EditorWidgets::InputFileName("Name", nameBuffer_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

	// --- 使える名前か。だめなら理由を出して、押せなくする ---
	const char* problem = nullptr;
	if (nameBuffer_.empty()) {
		problem = "名前を入れてください（英数字と _ -）";
	} else if (!IsValidFileName(nameBuffer_)) {
		problem = "使えない文字が入っています（英数字と _ - だけ）"; // Save As の最初の名前（今のシーンの名前）が日本語のときなど
	} else if (SceneManager::HasScene(nameBuffer_)) {
		problem = "その名前のシーンはもうあります";
	}
	if (problem == nullptr) {
		ImGui::TextDisabled("%s", MakeSceneFilePath(nameBuffer_).c_str()); // できるファイル
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", problem);
	}
	ImGui::Spacing();

	// --- Create / Save（Enter でも）---
	ImGui::BeginDisabled(problem != nullptr);
	const bool pressed = ImGui::Button(dialog == Dialog::NewScene ? "Create" : "Save");
	ImGui::EndDisabled();
	if (problem == nullptr && (pressed || entered)) {
		if (dialog == Dialog::NewScene) {
			// 作って開く（保存していない変更があれば、次のフレームに「保存しますか？」が出る）
			if (SceneManager::CreateSceneFile(nameBuffer_)) {
				RequestOpen(nameBuffer_);
			}
		} else if (SceneManager* sceneManager = SceneManager::GetMain()) {
			sceneManager->RequestSaveAs(nameBuffer_);
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

//=============================================================================
// ショートカット（Ctrl+S は Control ウィンドウが受ける）
//=============================================================================
void SceneDialogs::HandleShortcuts() {
	// 文字の入力中・ドラッグ中・ほかのポップアップが出ている間は何もしない
	if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
		return;
	}
	if (EditorKeymap::IsTriggered(EditorAction::NewScene)) {
		RequestNew();
	}
	if (EditorKeymap::IsTriggered(EditorAction::SaveSceneAs)) {
		RequestSaveAs();
	}
}
#endif