#ifdef USE_IMGUI
#include "SceneDialogs.h"

#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/FileNameRule.h"
#include "HTTEngine/UI/GlobalVariables.h"

// 静的メンバ変数
SceneDialogs::Dialog SceneDialogs::openRequest_ = SceneDialogs::Dialog::None;
std::string SceneDialogs::pendingOpen_;
std::string SceneDialogs::nameBuffer_;
bool SceneDialogs::nameFocusRequest_ = false;
SceneDialogs::QuitState SceneDialogs::quitState_ = SceneDialogs::QuitState::None;
std::function<void()> SceneDialogs::closeWindow_;

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
// 保存（Control ウィンドウの Save・Ctrl+S・File → Save・「保存しますか？」の Save）
//=============================================================================
// ===== 開いているシーンを保存できるか =====
bool SceneDialogs::CanSaveScene() {
	// シーンファイルを使わない前の書き方のシーンは、保存できない
	return CanEditScene() && SceneManager::GetMain()->GetSceneFile() != nullptr;
}

// ===== 開いているシーンに保存していない変更があるか =====
bool SceneDialogs::IsSceneDirty() {
	const SceneManager* sceneManager = SceneManager::GetMain();
	return sceneManager != nullptr && sceneManager->IsDirty();
}

// ===== 保存していない物が何かあるか =====
bool SceneDialogs::HasUnsavedChanges() { return IsSceneDirty() || !SettingsWindow::GetDirtyPageNames(SettingsSection::Editor).empty(); }

// ===== 保存する物があるか =====
bool SceneDialogs::CanSave() {
	// シーンは変更が無くても保存できる（今までどおり）。エディタの設定は、変更があるときだけ（再生中でも）
	return CanSaveScene() || !SettingsWindow::GetDirtyPageNames(SettingsSection::Editor).empty();
}

// ===== 保存する =====
void SceneDialogs::RequestSave() {
	// --- エディタの設定（Scene ビューのカメラ・View の見え方・Keymap など、Settings → Editor の全ページ）---
	SettingsWindow::SaveSection(SettingsSection::Editor);
	// --- ウィンドウの配置（ImGui も数秒ごとに書くが、Save の時点の配置を確実に残す）---
	if (const char* iniFile = ImGui::GetIO().IniFilename) {
		ImGui::SaveIniSettingsToDisk(iniFile);
	}
	// --- シーン（書くのは次のフレームの頭。再生中・シーンファイルが無いときは書かない）---
	if (CanSaveScene()) {
		SceneManager::GetMain()->RequestSave();
	}
}


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
	UpdateQuit();
	// 預かっていた確認のウィンドウを開く（ここはどのポップアップの中でもないので、入れ子にならない）
	if (openRequest_ != Dialog::None) {
		ImGui::OpenPopup(TitleOf(openRequest_));
		openRequest_ = Dialog::None;
	}
	DrawUnsavedDialog();
	DrawQuitDialog();
	DrawNameDialog(Dialog::NewScene);
	DrawNameDialog(Dialog::SaveAs);
}

const char* SceneDialogs::TitleOf(Dialog dialog) {
	switch (dialog) {
	case Dialog::Unsaved:
		return "Unsaved Changes";
	case Dialog::Quit:
		return "Unsaved Changes###Quit"; // 見た目の題は同じ。「###」の後ろを ID にして、別のシーンを開く前の確認と分ける
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
	// シーンとエディタの設定（再生中は、エディタの設定に変更があるときだけ押せる）
	if (ImGui::MenuItem("Save", EditorKeymap::GetShortcutText(EditorAction::SaveScene).c_str(), false, CanSave())) {
		RequestSave();
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
	// エディタの設定も、ふつうの Save と同じく一緒に保存する
	if (ImGui::Button("Save")) {
		RequestSave();
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
// エディタを閉じる（ウィンドウの ×・Alt+F4）
//=============================================================================
// ===== もう閉じてよいか（ウィンドウが WM_CLOSE で聞く）=====
bool SceneDialogs::CanQuit() { return quitState_ == QuitState::Approved || !HasUnsavedChanges(); }

// ===== 閉じてよいか聞く（CanQuit が false だったとき、ウィンドウが呼ぶ）=====
void SceneDialogs::RequestQuit(std::function<void()> close) {
	if (quitState_ == QuitState::WaitingSave) {
		return; // もう Save を押して、保存が済むのを待っている（済めば閉じる）
	}
	closeWindow_ = std::move(close);
	openRequest_ = Dialog::Quit;
}

// ===== 保存しますか？（閉じる前）=====
void SceneDialogs::DrawQuitDialog() {
	CenterNextWindow();
	if (!ImGui::BeginPopupModal(TitleOf(Dialog::Quit), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// --- 保存していない物を並べる（何が消えるか分かるように）---
	ImGui::Text("保存していない変更があります。閉じる前に保存しますか？");
	ImGui::Spacing();
	const bool isSceneDirty = IsSceneDirty();
	if (isSceneDirty) {
		ImGui::BulletText("シーン: %s", SceneManager::GetMain()->GetOpenSceneName().c_str());
	}
	const std::vector<std::string> editorPages = SettingsWindow::GetDirtyPageNames(SettingsSection::Editor);
	if (!editorPages.empty()) {
		std::string pages; // "Scene View・View"
		for (const std::string& name : editorPages) {
			if (!pages.empty()) {
				pages += "・";
			}
			pages += name;
		}
		ImGui::BulletText("エディタの設定: %s", pages.c_str());
	}
	// 再生中はシーンを保存できない（Play 中の変更は Stop で戻る物なので、保存する物ではない）
	const bool canSaveAll = !isSceneDirty || CanSaveScene();
	if (!canSaveAll) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "再生中はシーンを保存できません（Stop してから閉じてください）");
	}
	ImGui::Spacing();

	// --- Save：保存して、保存できたら閉じる（シーンを書くのは次のフレームの頭なので、UpdateQuit が待つ）---
	ImGui::BeginDisabled(!canSaveAll);
	if (ImGui::Button("Save")) {
		RequestSave();
		quitState_ = QuitState::WaitingSave;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	// --- Don't Save：変更を捨てて閉じる ---
	if (ImGui::Button("Don't Save")) {
		ImGui::CloseCurrentPopup();
		ApproveQuit();
	}
	ImGui::SameLine();
	// --- Cancel：閉じない ---
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		closeWindow_ = nullptr;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ===== Save を押した後、保存が済んだら閉じる =====
void SceneDialogs::UpdateQuit() {
	if (quitState_ != QuitState::WaitingSave) {
		return;
	}
	const SceneManager* sceneManager = SceneManager::GetMain();
	if (sceneManager != nullptr && sceneManager->IsSaveRequested()) {
		return; // まだフレームの頭でシーンを書いていない
	}
	// 書けなかった物が残っていたら、閉じない（消えてしまうので。理由はログに出ている）
	if (HasUnsavedChanges()) {
		LogManager::Warning("保存できなかった物があるので、閉じるのをやめました");
		quitState_ = QuitState::None;
		closeWindow_ = nullptr;
		return;
	}
	ApproveQuit();
}

// ===== 閉じてよいことにして、閉じる =====
void SceneDialogs::ApproveQuit() {
	quitState_ = QuitState::Approved;
	// ウィンドウにもう一度「閉じて」と送る（今度は CanQuit が true なので閉じる。描いている途中で消さないように、次のフレームの頭で）
	if (closeWindow_) {
		closeWindow_();
		closeWindow_ = nullptr;
	}
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