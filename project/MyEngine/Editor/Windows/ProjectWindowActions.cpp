#ifdef USE_IMGUI
#include "ProjectWindow.h"

#include <format>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Assets/AssetMover.h"
#include "MyEngine/Editor/Assets/ProjectFiles.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Editor/Timeline/TimelineWindow.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/Editor/Windows/CodeWindow.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Editor/Windows/SceneDialogs.h"
#include "MyEngine/Editor/Windows/UIScreenEditor.h"
#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"
#include "MyEngine/Physics/PhysicMaterialManager.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/String/FileNameRule.h"
#include "MyEngine/UI/UIScreen.h"

// ProjectWindow の操作の側（クリック・メニュー・開く・作る・名前の変更・ごみ箱）。読む・描く側は ProjectWindow.cpp


//=============================================================================
// クリック・メニュー
//=============================================================================
// ===== 項目の上でのクリック =====
void ProjectWindow::HandleItemInput(const Item& item) {
	// --- 左・右クリックで選ぶ ---
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		selected_ = item.path;
		// Inspector で直せるファイル（Physic Material など）なら、Inspector に出す（Entity の選択は外す。Unity と同じ）
		if (!item.isFolder && AssetInspector::CanEdit(item.path)) {
			AssetInspector::Select(item.path);
			HierarchyWindow::SetSelected({});
		}
	}
	// --- ダブルクリックで開く（フォルダは中へ、シーンはエディタで開く）---
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		Open(item);
	}
	// --- マウスを乗せたらファイルの名前（拡張子まで。名前が長くて切れていても分かる）---
	ImGui::SetItemTooltip("%s", ToUtf8(item.path.filename()).c_str());
	// --- 右クリックのメニュー ---
	if (ImGui::BeginPopupContextItem("itemMenu")) {
		DrawItemMenu(item);
		ImGui::EndPopup();
	}
}

// ===== 項目の右クリックのメニュー =====
void ProjectWindow::DrawItemMenu(const Item& item) {
	// --- 開く ---
	if (item.isFolder) {
		if (ImGui::MenuItem("Open")) {
			Open(item);
		}
	} else if (item.icon == AssetIcon::Scene) {
		// シーン
		const SceneManager* sceneManager = SceneManager::GetMain();
		const bool canOpen = ProjectFiles::IsOpenableScene(item.path, item.icon, item.name) && sceneManager && sceneManager->GetPlayState() == PlayState::Editing;
		if (ImGui::MenuItem("Open", nullptr, false, canOpen)) {
			Open(item);
		}
		ImGui::SetItemTooltip("停止中に、resources/scenes/ のシーンだけ開けます");
	} else if (item.icon == AssetIcon::Code) {
		// コードはエンジンの中で読む（Code ウィンドウ）か、Visual Studio で直す
		if (ImGui::MenuItem("Open in Code Window")) {
			Open(item);
		}
		if (ImGui::MenuItem("Open in Visual Studio")) {
			CodeWindow::OpenExternal(item.path);
		}
	} else if (IsAssetFile(ToUtf8(item.path), AssetType::Prefab)) {
		// Prefab はシーンに置ける（原点に。次のフレームの頭にできる。Undo には入らないので、要らなければ Hierarchy で消す）
		// 置いた物は Prefab とつながっていて、シーンファイルには参照だけが書かれる
		if (ImGui::MenuItem("Place in Scene")) {
			if (const uint32_t prefab = LoadAsset(AssetType::Prefab, ToUtf8(item.path)); prefab != 0) {
				PrefabManager::RequestPlace(prefab);
			}
		}
		ImGui::SetItemTooltip("原点に置く。直した所は、置いた所ごとの上書きとしてシーンに残る（Inspector の Apply で Prefab に書ける）");
	} else if (ToUtf8(item.path).ends_with(kUIScreenFileExtension)) {
		// 画面は編集モードで開く（ダブルクリックと同じ。停止中だけ）
		const SceneManager* sceneManager = SceneManager::GetMain();
		const bool canEdit = sceneManager && sceneManager->GetPlayState() == PlayState::Editing;
		if (ImGui::MenuItem("Edit UI Screen", nullptr, false, canEdit)) {
			UIScreenEditor::RequestOpen(item.path);
		}
		ImGui::SetItemTooltip("停止中に、Scene ビューで画面を編集する（Hierarchy の Done で保存して閉じる）");
	} else if (ToUtf8(item.path).ends_with(TimelineManager::kFileExtension)) {
		// Timeline
		if (ImGui::MenuItem("Open Timeline")) {
			TimelineWindow::Open(item.path);
		}
		ImGui::SetItemTooltip("Timeline ウィンドウで直す（ダブルクリックと同じ）");
	}
	ImGui::Separator();

	// --- 名前を変える・消す（開いているシーンと、それが入っているフォルダ・Source/ の中の物はできない）---
	const bool isLocked = IsLocked(item);
	if (ImGui::MenuItem("Rename", "F2", false, !isLocked)) {
		StartRename(item);
	}
	if (ImGui::MenuItem("Delete", "Del", false, !isLocked)) {
		deleteTarget_ = item.path;
		deleteRequest_ = true;
	}
	if (isLocked && ProjectFiles::IsInsideSource(item.path)) {
		ImGui::TextDisabled("コードは Visual Studio で名前を変える・消す（プロジェクトにファイルの名前が書いてあるので）");
	} else if (isLocked) {
		ImGui::TextDisabled("開いているシーンは、名前を変える・消すができません");
	} else if (item.icon != AssetIcon::Scene) {
		ImGui::TextDisabled("名前を変える・消すと、使っているシーンからは見つからなくなります");
	}
	ImGui::Separator();

	// --- そのほか ---
	if (ImGui::MenuItem("Copy Path")) {
		ImGui::SetClipboardText(ToUtf8(item.path).c_str()); // Inspector やコードに貼れるように、resources/ から始まるパスで
	}
	if (ImGui::MenuItem("Show in Explorer")) {
		ProjectFiles::RevealInExplorer(item.path);
	}
	ImGui::Separator();
	DrawCreateMenu();
}

// ===== 作成メニュー =====
void ProjectWindow::DrawCreateMenu() {
	if (ImGui::BeginMenu("Create")) {
		// フォルダ
		if (ImGui::MenuItem("Folder")) {
			CreateFolder();
		}
		// シーン
		if (ImGui::MenuItem("Scene")) {
			CreateScene();
		}
		ImGui::SetItemTooltip("シーンは resources/scenes/ に作ります（シーンは名前で探すので、置き場所は1つ）");
		// Physic Material
		if (ImGui::MenuItem("Physic Material")) {
			CreateAssetFile("NewPhysicMaterial", PhysicMaterialManager::kFileExtension, &PhysicMaterialManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（跳ね返り・摩擦。Collider の Physic Material で選ぶ）");
		// Attack Table
		if (ImGui::MenuItem("Attack Table")) {
			CreateAssetFile("NewAttackTable", AttackTableManager::kFileExtension, &AttackTableManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（敵のフェーズと攻撃。Attack Table の Table で選ぶ）");
		// Curve
		if (ImGui::MenuItem("Curve")) {
			CreateAssetFile("NewCurve", CurveManager::kFileExtension, &CurveManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に対して値がどう変わるかのカーブ）");
		// Material
		if (ImGui::MenuItem("Material")) {
			CreateAssetFile("NewMaterial", MaterialManager::kFileExtension, &MaterialManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（Shader と値。Decal Renderer・Hitbox Telegraph の Material で選ぶ）");
		// Timeline
		if (ImGui::MenuItem("Timeline")) {
			CreateAssetFile("NewTimeline", TimelineManager::kFileExtension, &TimelineManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に沿って Component の値を動かす。Timeline Component で再生する）");
		// UIScreen
		if (ImGui::MenuItem("UI Screen")) {
			CreateUIScreen();
		}
		ImGui::SetItemTooltip("画面（ポーズ画面など）は resources/ui/ に作ります（UIScreen::Open(名前) で出すので、置き場所は1つ）");
		ImGui::EndMenu();
	}
}

// ===== 何も無い所 =====
void ProjectWindow::HandleEmptySpace() {
	// --- 何も無い所をクリックしたら、選ぶのをやめる ---
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		selected_.clear();
	}
	// --- 何も無い所の右クリック（項目の上では項目のメニューが出る）---
	if (ImGui::BeginPopupContextWindow("emptySpaceMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		DrawCreateMenu();
		ImGui::Separator();
		if (ImGui::MenuItem("Show in Explorer")) {
			ProjectFiles::OpenInExplorer(currentFolder_);
		}
		if (ImGui::MenuItem("Refresh")) {
			rescanRequest_ = true;
		}
		ImGui::EndPopup();
	}
}

// ===== ショートカット（Project ウィンドウを触っているときだけ） =====
void ProjectWindow::HandleShortcuts() {
	// 名前の入力中・ほかの入力中は、入力欄のほうを優先する
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || !renaming_.empty() || ImGui::GetIO().WantTextInput) {
		return;
	}
	const Item* item = FindSelected();
	if (item == nullptr || IsLocked(*item)) {
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
		StartRename(*item);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		deleteTarget_ = item->path;
		deleteRequest_ = true;
	}
}


//=============================================================================
// 開く・作る
//=============================================================================
// ===== 開く =====
void ProjectWindow::Open(const Item& item) {
	if (item.isFolder) {
		OpenFolder(item.path);
		return;
	}
	// シーンはエディタで開く（保存していない変更があれば、先に聞く）
	if (ProjectFiles::IsOpenableScene(item.path, item.icon, item.name)) {
		SceneDialogs::RequestOpen(item.name);
	}
	// コードは Code ウィンドウで読む
	if (item.icon == AssetIcon::Code) {
		CodeWindow::Open(item.path);
	}
	// 画面は編集モードで開く（Scene ビューで置く。Hierarchy の Done で保存して閉じる）
	if (ToUtf8(item.path).ends_with(kUIScreenFileExtension)) {
		UIScreenEditor::RequestOpen(item.path);
	}
	// Timeline は Timeline ウィンドウで直す
	if (ToUtf8(item.path).ends_with(TimelineManager::kFileExtension)) {
		TimelineWindow::Open(item.path);
	}
	// そのほかのファイルは、今は何もしない（後で：Inspector へドラッグ）
}

// ===== フォルダを開く =====
void ProjectWindow::OpenFolder(const std::filesystem::path& folder) {
	currentFolder_ = folder;
	selected_.clear();
	renaming_.clear();
	rescanRequest_ = true; // 中身は次のフレームの頭で読む（今は中身の一覧を回している途中かもしれないので）
	revealRequest_ = true;
}

// ===== フォルダを作る =====
void ProjectWindow::CreateFolder() {
	const std::filesystem::path path = ProjectFiles::UniquePath(currentFolder_, "NewFolder");
	std::error_code error;
	if (!std::filesystem::create_directory(path, error)) {
		LogManager::Warning(std::format("フォルダを作れませんでした: {}", ToUtf8(path)));
		return;
	}
	// 作ったフォルダを選んで、すぐに名前を打てるようにする（Unity と同じ）
	selected_ = path;
	renaming_ = path;
	renameBuffer_ = ToUtf8(path.filename());
	renameFocusRequest_ = true;
	rescanRequest_ = true;
}

// ===== シーンを作る =====
void ProjectWindow::CreateScene() {
	const std::string name = ProjectFiles::UniqueSceneName("NewScene");
	if (!SceneManager::CreateSceneFile(name)) {
		return;
	}
	// resources/scenes/ を開いて、作ったシーンの名前をすぐに打てるようにする
	const std::filesystem::path path = MakeSceneFilePath(name);
	OpenFolder(path.parent_path());
	selected_ = path;
	renaming_ = path;
	renameBuffer_ = name;
	renameFocusRequest_ = true;
}

// ===== UI Screen を作る =====
void ProjectWindow::CreateUIScreen() {
	// まだ使われていない名前（NewUIScreen.uiscreen・NewUIScreen1.uiscreen …）
	std::filesystem::path path;
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::string name = "NewUIScreen" + (number == 0 ? std::string() : std::to_string(number));
		path = ToPath(MakeUIScreenFilePath(name));
		if (!std::filesystem::exists(path, error)) {
			break;
		}
	}
	if (!UIScreen::CreateNewFile(path)) {
		return;
	}
	// resources/ui/ を開いて、作った画面の名前をすぐに打てるようにする
	OpenFolder(path.parent_path());
	selected_ = path;
	AssetInspector::Select(path);
	HierarchyWindow::SetSelected({});
	renaming_ = path;
	renameBuffer_ = ProjectFiles::EditableName(path, ProjectFiles::IconOf(ToUtf8(path.filename())), false); // 拡張子の前だけ打つ（StartRename と同じ）
	renameFocusRequest_ = true;
}

// ===== 既定の中身のファイルを作る =====
void ProjectWindow::CreateAssetFile(std::string_view baseName, std::string_view extension, bool (*create)(const std::filesystem::path&)) {
	// まだ使われていない名前（NewAttackTable.attacktable・NewAttackTable1.attacktable …）。拡張子の前に番号を付ける
	std::filesystem::path path;
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::string name = std::string(baseName) + (number == 0 ? std::string() : std::to_string(number));
		path = currentFolder_ / (name + std::string(extension));
		if (!std::filesystem::exists(path, error)) {
			break;
		}
	}
	if (!create(path)) {
		return;
	}
	// 作ったファイルを選んで Inspector に出し、すぐに名前を打てるようにする
	selected_ = path;
	AssetInspector::Select(path);
	HierarchyWindow::SetSelected({});
	renaming_ = path;
	renameBuffer_ = ToUtf8(path.filename());
	renameFocusRequest_ = true;
	rescanRequest_ = true;
}


//=============================================================================
// 名前の変更
//=============================================================================
// ===== 名前の変更を始める =====
void ProjectWindow::StartRename(const Item& item) {
	if (IsLocked(item)) {
		return;
	}
	selected_ = item.path;
	renaming_ = item.path;
	renameBuffer_ = ProjectFiles::EditableName(item.path, item.icon, item.isFolder); // 拡張子は変えない
	renameFocusRequest_ = true;
}

// ===== 入力欄 =====
void ProjectWindow::DrawRenameField(const Item& item) {
	// 出した最初のフレームだけ、入力欄にフォーカスを移す（すぐに文字を打てるように）
	const bool justStarted = renameFocusRequest_;
	if (renameFocusRequest_) {
		ImGui::SetKeyboardFocusHere();
		renameFocusRequest_ = false;
	}
	// EnterReturnsTrue：Enterで確定　AutoSelectAll：最初は全部選んだ状態（そのまま打つと置き換わる）
	const bool entered = EditorWidgets::InputFileName("##rename", renameBuffer_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
	// 確定：Enter、または欄の外をクリックして抜けた（書き換えていた場合）。Esc で抜けたら ImGui が元に戻すので、変わらない
	if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
		ApplyRename(item);
	}
	// 終了：Enter・Esc・欄の外をクリック、のどれでも入力欄を閉じる（Hierarchy の名前の変更と同じ）
	bool finished = entered || ImGui::IsItemDeactivated();
	if (!justStarted && !ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		finished = true;
	}
	if (finished) {
		renaming_.clear();
	}
}

// ===== 確定 =====
void ProjectWindow::ApplyRename(const Item& item) {
	if (renameBuffer_ == ProjectFiles::EditableName(item.path, item.icon, item.isFolder)) {
		return; // 変わっていない
	}
	if (!IsValidFileName(renameBuffer_)) {
		LogManager::Warning(std::format("名前に使えるのは、英数字と _ - だけです: \"{}\"", renameBuffer_));
		return;
	}
	const std::filesystem::path newPath = ProjectFiles::RenamedPath(item.path, item.icon, item.isFolder, renameBuffer_);
	// ディスク・読み込み済みの物・ほかのファイルの中のパスまで、まとめて新しい名前にする（同じ名前がもうあるときは、警告を出して何もしない）
	if (!AssetMover::Move(item.path, newPath)) {
		return;
	}
	// --- この窓が覚えているパス（選んでいる物・右に出しているフォルダ。フォルダの名前を変えると、中の物も変わる）---
	const MovedPath moved{ToPathKey(item.path), ToPathKey(newPath)};
	moved.Apply(selected_);
	moved.Apply(currentFolder_);
	rescanRequest_ = true;
}


//=============================================================================
// ごみ箱
//=============================================================================
// ===== ごみ箱へ移すか聞く =====
void ProjectWindow::DrawDeleteDialog() {
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}
	ImGui::Text("%s をごみ箱へ移しますか？", ToUtf8(deleteTarget_).c_str());
	ImGui::TextDisabled("（Windows のごみ箱から元に戻せます）");
	ImGui::Spacing();
	if (ImGui::Button("Delete")) {
		ProjectFiles::MoveToRecycleBin(deleteTarget_);
		if (ProjectFiles::IsInside(selected_, deleteTarget_)) {
			selected_.clear();
		}
		rescanRequest_ = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}


//=============================================================================
// 小さな道具
//=============================================================================
// ===== 名前を変える・消すことができない物か =====
bool ProjectWindow::IsLocked(const Item& item) {
	// Source/ の中の物（コードとフォルダ）：Visual Studio のプロジェクトにファイルの名前が書いてあるので、ここで変えるとビルドできなくなる
	if (ProjectFiles::IsInsideSource(item.path)) {
		return true;
	}
	const SceneManager* sceneManager = SceneManager::GetMain();
	if (sceneManager == nullptr || sceneManager->GetOpenSceneName().empty()) {
		return false;
	}
	// 開いているシーンそのもの、またはそれが入っているフォルダ（名前を変えると、保存する先が無くなってしまう）
	const std::filesystem::path openFile = MakeSceneFilePath(sceneManager->GetOpenSceneName());
	return item.path == openFile || (item.isFolder && ProjectFiles::IsInside(openFile, item.path));
}

// ===== 選んでいる項目 =====
const ProjectWindow::Item* ProjectWindow::FindSelected() {
	for (const Item& item : items_) {
		if (item.path == selected_) {
			return &item;
		}
	}
	return nullptr;
}
#endif
