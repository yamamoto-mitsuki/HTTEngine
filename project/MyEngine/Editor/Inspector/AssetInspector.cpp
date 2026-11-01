#include "AssetInspector.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/String/ConvertString.h"

#ifdef USE_IMGUI
#include <externals/imgui/imgui.h>
#endif

namespace {
std::vector<std::unique_ptr<AssetEditor>> editors; // 直せるファイルの種類（Register で足す）
std::filesystem::path selected; // Inspector に出しているファイル（空なら無し）
std::filesystem::path unsaved;  // 直して、まだ保存していないファイル（空なら無し）
bool needsLoadCheck = false;    // 選んだファイルを読めるか、まだ調べていない
bool isBroken = false;          // 選んでいるファイルが読めない

// そのファイルの係（いなければ nullptr）。拡張子を小文字にして比べる（.PHYSMAT でもよい）
AssetEditor* FindEditor(const std::filesystem::path& filePath) {
	if (filePath.empty()) {
		return nullptr;
	}
	std::string extension = ToUtf8(filePath.extension());
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const auto found = std::ranges::find_if(editors, [&extension](const std::unique_ptr<AssetEditor>& editor) { return editor->GetExtension() == extension; });
	return (found != editors.end()) ? found->get() : nullptr;
}

// 直して、まだ保存していないファイルがあれば保存する
void SaveUnsaved() {
	if (AssetEditor* editor = FindEditor(unsaved)) {
		editor->Save(unsaved);
	}
	unsaved.clear();
}
} // namespace

void AssetInspector::Register(std::unique_ptr<AssetEditor> editor) { editors.push_back(std::move(editor)); }

bool AssetInspector::CanEdit(const std::filesystem::path& filePath) { return FindEditor(filePath) != nullptr; }

void AssetInspector::Select(const std::filesystem::path& filePath) {
	if (filePath != unsaved) {
		SaveUnsaved(); // 別のファイルへ移る前に、直した物を保存する
	}
	selected = filePath;
	needsLoadCheck = true; // クリックのたびに調べ直す（直したファイルは、クリックし直せば読める）
}

bool AssetInspector::HasSelection() { return CanEdit(selected); }

void AssetInspector::Draw() {
#ifdef USE_IMGUI
	AssetEditor* editor = FindEditor(selected);
	if (editor == nullptr) {
		return;
	}
	// --- 見出し：種類とファイルの名前（置き場所はマウスを乗せると出る）---
	ImGui::TextUnformatted(std::string(editor->GetTypeName()).c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%s", ToUtf8(selected.filename()).c_str());
	ImGui::SetItemTooltip("%s", ToUtf8(selected).c_str());
	ImGui::Separator();

	// --- 読めるかは選んだときに1回だけ調べる（読めないファイルを毎フレーム読むと、ログがエラーで埋まる）---
	if (needsLoadCheck) {
		needsLoadCheck = false;
		isBroken = !editor->CanLoad(selected);
	}
	if (isBroken) {
		GetInspectorUI().Label("このファイルは読めません（無い・JSON が壊れている。直したらクリックし直す）");
		return;
	}

	// --- 中身。変えたら覚えておき、つまむ・打つのが終わったら（どの項目も操作していない）保存する ---
	// ドラッグしている間に毎フレームファイルを書かないように（Component の Undo の履歴と同じタイミング）
	if (editor->Draw(selected, GetInspectorUI())) {
		unsaved = selected;
	}
	if (!unsaved.empty() && !ImGui::IsAnyItemActive()) {
		SaveUnsaved();
	}
#endif
}