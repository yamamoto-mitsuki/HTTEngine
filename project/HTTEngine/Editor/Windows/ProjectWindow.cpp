#ifdef USE_IMGUI
#include "ProjectWindow.h"

#include <algorithm>
#include <cfloat>

#include "HTTEngine/Editor/Assets/AssetThumbnails.h"
#include "HTTEngine/Editor/Assets/ProjectFiles.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/ConvertString.h"

// ProjectWindow の読む・描く側。操作の側（クリック・メニュー・開く・作る・名前の変更・ごみ箱）は ProjectWindowActions.cpp

// 静的メンバ変数
std::filesystem::path ProjectWindow::currentFolder_ = "resources";
std::filesystem::path ProjectWindow::selected_;
std::filesystem::path ProjectWindow::renaming_;
std::string ProjectWindow::renameBuffer_;
bool ProjectWindow::renameFocusRequest_ = false;
std::filesystem::path ProjectWindow::deleteTarget_;
bool ProjectWindow::deleteRequest_ = false;
float ProjectWindow::iconSize_ = 64.0f;
std::vector<ProjectWindow::Folder> ProjectWindow::rootFolders_;
std::vector<ProjectWindow::Item> ProjectWindow::items_;
double ProjectWindow::lastScanTime_ = -1000.0;
bool ProjectWindow::rescanRequest_ = true;
bool ProjectWindow::revealRequest_ = false;

namespace {
constexpr double kRescanSeconds = 1.0;                     // ファイルを読み直す間隔（エクスプローラーで足した物も出るように）
constexpr float kMinIconSize = 16.0f;                      // アイコンの大きさの下限
constexpr float kMaxIconSize = 128.0f;                     // 上限（小さい絵の大きさ AssetThumbnails::kThumbnailSize と同じ）
constexpr float kListModeSize = 24.0f;                     // これ以下なら、1行ずつの並びにする
constexpr float kCellPadding = 4.0f;                       // アイコンの並びの1つ分の、内側の余白
constexpr float kFolderListWidth = 180.0f;                 // 左の木の最初の幅（境目をドラッグで変えられる）

// 名前を w の幅に収めて描く（収まれば真ん中、収まらなければ左に寄せて、はみ出した所は切る）
void DrawClippedText(ImDrawList* drawList, const std::string& text, float x, float y, float width) {
	const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
	const float textX = (textWidth <= width) ? x + (width - textWidth) * 0.5f : x;
	const ImVec4 clip(x, y, x + width, y + ImGui::GetTextLineHeight());
	drawList->AddText(nullptr, 0.0f, ImVec2(textX, y), ImGui::GetColorU32(ImGuiCol_Text), text.c_str(), nullptr, 0.0f, &clip);
}
} // namespace


//=============================================================================
// 描画
//=============================================================================
// ===== ウィンドウ全体 =====
void ProjectWindow::Draw() {
	ImGui::SetNextWindowSize(ImVec2(800.0f, 300.0f), ImGuiCond_FirstUseEver); // 初めて出すときの大きさ（後は imgui.ini に覚えられる）
	if (!ImGui::Begin("Project")) {
		ImGui::End();
		return; // 隠れている間はファイルを読まない
	}

	// --- ファイルを読み直す（頼まれたとき・1秒ごと）---
	if (rescanRequest_ || ImGui::GetTime() - lastScanTime_ > kRescanSeconds) {
		Rescan();
	}
	if (rootFolders_.empty()) {
		ImGui::TextDisabled("resources/ フォルダも Source/ フォルダもありません（ゲームのプロジェクトのフォルダで起動してください）");
		ImGui::End();
		return;
	}

	// --- 左右に分ける（境目はドラッグで動かせる）---
	if (ImGui::BeginTable("##projectLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, kFolderListWidth);
		ImGui::TableSetupColumn("items", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		// ===== 左：フォルダの木 =====
		ImGui::TableNextColumn();
		if (ImGui::BeginChild("##folderTree")) {
			for (const Folder& root : rootFolders_) {
				DrawFolderTree(root);
			}
			revealRequest_ = false;
		}
		ImGui::EndChild();

		// ===== 右：今のフォルダの中身 =====
		ImGui::TableNextColumn();
		DrawToolbar();
		ImGui::Separator();
		// 一番下の1行（選んでいる物のパス）の分を残して、残りを中身にする
		if (ImGui::BeginChild("##items", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()))) {
			DrawItems();
			HandleEmptySpace();
		}
		ImGui::EndChild();
		ImGui::TextDisabled("%s", ToUtf8(selected_.empty() ? currentFolder_ : selected_).c_str());
		ImGui::EndTable();
	}
	HandleShortcuts();

	// --- ごみ箱へ移すか聞く（メニューの中から開くと入れ子になるので、ここで開く）---
	if (deleteRequest_) {
		ImGui::OpenPopup("Delete Asset");
		deleteRequest_ = false;
	}
	DrawDeleteDialog();
	ImGui::End();
}


//=============================================================================
// 読む
//=============================================================================
// ===== フォルダの木と中身を読み直す =====
void ProjectWindow::Rescan() {
	lastScanTime_ = ImGui::GetTime();
	rescanRequest_ = false;
	// 左の木（あるフォルダだけ）
	std::error_code error;
	rootFolders_.clear();
	for (const std::filesystem::path& root : ProjectFiles::GetRootFolders()) {
		if (std::filesystem::is_directory(root, error)) {
			rootFolders_.push_back(ScanFolderTree(root));
		}
	}
	if (rootFolders_.empty()) {
		items_.clear();
		return;
	}
	// 見ていたフォルダが消えていたら（名前を変えた・エクスプローラーで消した）、一番上のフォルダに戻る
	if (!std::filesystem::is_directory(currentFolder_, error) || !ProjectFiles::IsInsideRoots(currentFolder_)) {
		currentFolder_ = rootFolders_.front().path;
	}
	items_ = ScanItems(currentFolder_);
	// 名前を変えている物が無くなっていたら、やめる
	if (!renaming_.empty() && std::none_of(items_.begin(), items_.end(), [](const Item& item) { return item.path == renaming_; })) {
		renaming_.clear();
	}
}

// ===== フォルダの木を読む =====
ProjectWindow::Folder ProjectWindow::ScanFolderTree(const std::filesystem::path& path) {
	Folder folder;
	folder.path = path;
	folder.name = ToUtf8(path.filename());
	std::error_code error;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path, std::filesystem::directory_options::skip_permission_denied, error)) {
		if (entry.is_directory(error)) {
			folder.children.push_back(ScanFolderTree(entry.path()));
		}
	}
	std::sort(folder.children.begin(), folder.children.end(), [](const Folder& a, const Folder& b) { return ProjectFiles::NameLess(a.name, b.name); });
	return folder;
}

// ===== 今のフォルダの中身を読む =====
std::vector<ProjectWindow::Item> ProjectWindow::ScanItems(const std::filesystem::path& folder) {
	std::vector<Item> folders;
	std::vector<Item> files;
	std::error_code error;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, error)) {
		const std::string fileName = ToUtf8(entry.path().filename());
		Item item;
		item.path = entry.path();
		item.isFolder = entry.is_directory(error);
		if (item.isFolder) {
			item.name = fileName;
			item.icon = AssetIcon::Folder;
			folders.push_back(std::move(item));
			continue;
		}
		if (ProjectFiles::IsHidden(fileName)) {
			continue;
		}
		item.icon = ProjectFiles::IconOf(fileName);
		// 種類が分かる物は拡張子を出さない（アイコンで分かる。Unity と同じ）。分からない物・コードは拡張子まで出す（plane.mtl と plane.blend、Coin.h と Coin.cpp を見分けられるように）
		item.name = (item.icon == AssetIcon::File || item.icon == AssetIcon::Code) ? fileName : ProjectFiles::EditableName(item.path, item.icon, false);
		item.writeTime = entry.last_write_time(error);
		files.push_back(std::move(item));
	}
	// フォルダが先、その後にファイル。それぞれ名前の順
	auto byName = [](const Item& a, const Item& b) { return ProjectFiles::NameLess(a.name, b.name); };
	std::sort(folders.begin(), folders.end(), byName);
	std::sort(files.begin(), files.end(), byName);
	folders.insert(folders.end(), files.begin(), files.end());
	return folders;
}


//=============================================================================
// 描く
//=============================================================================
// ===== 見出し（今のフォルダの場所・アイコンの大きさ） =====
void ProjectWindow::DrawToolbar() {
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端

	// --- 今のフォルダの場所（resources / monsterBall）。押すとそのフォルダへ ---
	const std::filesystem::path folder = currentFolder_; // 押すと currentFolder_ が変わるので、写してから回す
	std::filesystem::path partial;
	for (const std::filesystem::path& part : folder) {
		const bool isFirst = partial.empty();
		partial /= part;
		if (!isFirst) {
			ImGui::SameLine(0.0f, 2.0f);
			ImGui::TextDisabled("/");
			ImGui::SameLine(0.0f, 2.0f);
		}
		ImGui::PushID(ToUtf8(partial).c_str());
		if (ImGui::SmallButton(ToUtf8(part).c_str())) {
			OpenFolder(partial);
		}
		ImGui::PopID();
	}

	// --- 右端：アイコンの大きさ（小さくすると1行ずつの並び）---
	constexpr float kDragWidth = 90.0f;
	ImGui::SameLine(right - kDragWidth);
	ImGui::SetNextItemWidth(kDragWidth);
	ImGui::DragFloat("##iconSize", &iconSize_, 0.5f, kMinIconSize, kMaxIconSize, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("アイコンの大きさ（%.0f 以下で1行ずつの並び）", kListModeSize);
}

// ===== 左の木 =====
void ProjectWindow::DrawFolderTree(const Folder& folder) {
	const bool isCurrent = (folder.path == currentFolder_);
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (folder.children.empty()) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (isCurrent) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (ProjectFiles::IsRootFolder(folder.path)) {
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	// 右でフォルダの中へ入ったら、そこまでの親を開いて見えるようにする
	if (revealRequest_ && !isCurrent && ProjectFiles::IsInside(currentFolder_, folder.path)) {
		ImGui::SetNextItemOpen(true);
	}

	ImGui::PushID(ToUtf8(folder.path).c_str());
	const bool isOpen = ImGui::TreeNodeEx("##folder", flags);
	// 押したらそのフォルダの中身を右に出す（矢印を押したときは開く・閉じるだけ）
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		OpenFolder(folder.path);
	}
	// 矢印の右に、アイコンと名前
	ImGui::SameLine();
	const float iconSide = ImGui::GetTextLineHeight();
	const ImVec2 iconMin = ImGui::GetCursorScreenPos();
	AssetIcons::Draw(ImGui::GetWindowDrawList(), isCurrent ? AssetIcon::FolderOpen : AssetIcon::Folder, iconMin, ImVec2(iconMin.x + iconSide, iconMin.y + iconSide));
	ImGui::Dummy(ImVec2(iconSide, iconSide));
	ImGui::SameLine();
	ImGui::TextUnformatted(folder.name.c_str());

	if (isOpen) {
		for (const Folder& child : folder.children) {
			DrawFolderTree(child);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

// ===== 右の中身 =====
void ProjectWindow::DrawItems() {
	if (items_.empty()) {
		ImGui::TextDisabled("空のフォルダです（右クリック → Create）");
		return;
	}
	// --- 1行ずつの並び ---
	if (iconSize_ <= kListModeSize) {
		for (const Item& item : items_) {
			DrawListItem(item);
		}
		return;
	}
	// --- アイコンの並び（入るだけ横に並べて、はみ出したら次の行）---
	const float cellWidth = iconSize_ + kCellPadding * 2.0f;
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cellWidth + spacing)));
	for (size_t i = 0; i < items_.size(); ++i) {
		if (i % static_cast<size_t>(columns) != 0) {
			ImGui::SameLine();
		}
		DrawGridItem(items_[i], cellWidth);
	}
}

// ===== アイコンの並びの1つ =====
void ProjectWindow::DrawGridItem(const Item& item, float cellWidth) {
	const bool isRenaming = (item.path == renaming_);
	const float textHeight = ImGui::GetTextLineHeight();
	const ImVec2 cellSize(cellWidth, kCellPadding + iconSize_ + kCellPadding + textHeight + kCellPadding);

	ImGui::PushID(ToUtf8(item.path).c_str());
	ImGui::BeginGroup();
	const ImVec2 cellMin = ImGui::GetCursorScreenPos();
	// 押せる四角（選ぶ・ダブルクリック・右クリック。マウスが乗る・選ぶと色が付く）。名前の変更中は、下に入力欄を重ねる
	ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
	if (isRenaming) {
		flags |= ImGuiSelectableFlags_AllowOverlap;
	}
	ImGui::Selectable("##cell", item.path == selected_, flags, cellSize);
	HandleItemInput(item);

	// アイコン（上の真ん中）
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 iconMin(cellMin.x + (cellWidth - iconSize_) * 0.5f, cellMin.y + kCellPadding);
	DrawItemIcon(drawList, item, iconMin, ImVec2(iconMin.x + iconSize_, iconMin.y + iconSize_));

	// 名前（下）。変更中は入力欄
	const float textY = iconMin.y + iconSize_ + kCellPadding;
	if (isRenaming) {
		ImGui::SetCursorScreenPos(ImVec2(cellMin.x, textY - ImGui::GetStyle().FramePadding.y));
		ImGui::SetNextItemWidth(cellWidth);
		DrawRenameField(item);
	} else {
		DrawClippedText(drawList, LabelOf(item), cellMin.x, textY, cellWidth);
	}
	ImGui::EndGroup();
	ImGui::PopID();
}

// ===== 1行ずつの並びの1つ =====
void ProjectWindow::DrawListItem(const Item& item) {
	const bool isRenaming = (item.path == renaming_);
	const float rowHeight = ImGui::GetFrameHeight();

	ImGui::PushID(ToUtf8(item.path).c_str());
	const ImVec2 rowMin = ImGui::GetCursorScreenPos();
	ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
	if (isRenaming) {
		flags |= ImGuiSelectableFlags_AllowOverlap;
	}
	ImGui::Selectable("##row", item.path == selected_, flags, ImVec2(0.0f, rowHeight)); // 幅0＝横いっぱい
	HandleItemInput(item);

	// 左にアイコン、右に名前
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float iconSide = rowHeight - 2.0f;
	DrawItemIcon(drawList, item, ImVec2(rowMin.x, rowMin.y + 1.0f), ImVec2(rowMin.x + iconSide, rowMin.y + 1.0f + iconSide));
	const float textX = rowMin.x + iconSide + ImGui::GetStyle().ItemInnerSpacing.x;
	if (isRenaming) {
		ImGui::SetCursorScreenPos(ImVec2(textX, rowMin.y));
		ImGui::SetNextItemWidth(-FLT_MIN);
		DrawRenameField(item);
	} else {
		const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
		drawList->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_Text), LabelOf(item).c_str());
	}
	ImGui::PopID();
}

// ===== アイコン =====
void ProjectWindow::DrawItemIcon(ImDrawList* drawList, const Item& item, const ImVec2& min, const ImVec2& max) {
	// 見えていない所は描かない（小さい絵も作らない）
	if (!ImGui::IsRectVisible(min, max)) {
		return;
	}
	// 画像ファイルは、その画像の小さい絵（縦横比を保って、四角の真ん中に）
	if (item.icon == AssetIcon::Texture) {
		ImTextureID texture = ImTextureID_Invalid;
		ImVec2 size;
		if (AssetThumbnails::Get(ToUtf8(item.path), item.writeTime, texture, size)) {
			const ImVec2 box(max.x - min.x, max.y - min.y);
			const float scale = (std::min)(box.x / size.x, box.y / size.y);
			const ImVec2 drawSize(size.x * scale, size.y * scale);
			const ImVec2 drawMin(min.x + (box.x - drawSize.x) * 0.5f, min.y + (box.y - drawSize.y) * 0.5f);
			drawList->AddImage(texture, drawMin, ImVec2(drawMin.x + drawSize.x, drawMin.y + drawSize.y));
			return;
		}
		// まだ作っていない（次のフレーム以降に作る）・作れなかった → アイコンで代わりに描く
	}
	AssetIcons::Draw(drawList, item.icon, min, max);
}

// ===== 出す名前 =====
std::string ProjectWindow::LabelOf(const Item& item) {
	// 開いているシーンに保存していない変更があれば、名前に * を付ける（Control ウィンドウと同じ）
	const SceneManager* sceneManager = SceneManager::GetMain();
	if (sceneManager && sceneManager->IsDirty() && item.name == sceneManager->GetOpenSceneName() && ProjectFiles::IsOpenableScene(item.path, item.icon, item.name)) {
		return item.name + " *";
	}
	return item.name;
}
#endif
