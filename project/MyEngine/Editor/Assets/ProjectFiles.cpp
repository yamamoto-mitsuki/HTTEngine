#ifdef USE_IMGUI
#include "MyEngine/Editor/Assets/ProjectFiles.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string_view>
#include <system_error>

#include <Windows.h>
#include <shellapi.h>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Windows/CodeWindow.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/String/ConvertString.h"

namespace {
const std::filesystem::path kResourceFolder = "resources"; // 出すのはゲームのフォルダだけ（エンジンの MyEngine/Resources/ は出さない）
const std::filesystem::path kSourceFolder = "Source";      // ゲームのコード（中の物は Visual Studio のプロジェクトに名前で書いてある）
const std::array<std::filesystem::path, 2> kRootFolders = {kResourceFolder, kSourceFolder};
constexpr auto kHiddenExtensions = std::to_array<std::string_view>({".bak"}); // 出さないファイル（シーンの保存で残る1つ前のファイル）
} // namespace

namespace ProjectFiles {


//=============================================================================
// 出すフォルダ
//=============================================================================
// ===== 出すフォルダの一覧 =====
const std::array<std::filesystem::path, 2>& GetRootFolders() { return kRootFolders; }

// ===== アセットのフォルダ・コードのフォルダ =====
const std::filesystem::path& GetResourceFolder() { return kResourceFolder; }
const std::filesystem::path& GetSourceFolder() { return kSourceFolder; }

// ===== path が folder の中にあるか（folder そのものも含む） =====
bool IsInside(const std::filesystem::path& path, const std::filesystem::path& folder) {
	auto it = path.begin();
	for (const std::filesystem::path& part : folder) {
		if (it == path.end() || *it != part) {
			return false;
		}
		++it;
	}
	return true;
}

// ===== path が、出しているフォルダ（resources/ か Source/）の中にあるか =====
bool IsInsideRoots(const std::filesystem::path& path) {
	return std::any_of(kRootFolders.begin(), kRootFolders.end(), [&path](const std::filesystem::path& root) { return IsInside(path, root); });
}

// ===== path が、出しているフォルダそのものか（左の木の一番上） =====
bool IsRootFolder(const std::filesystem::path& path) { return std::find(kRootFolders.begin(), kRootFolders.end(), path) != kRootFolders.end(); }

// ===== path が、ゲームのコードのフォルダ（Source/）の中にあるか =====
bool IsInsideSource(const std::filesystem::path& path) { return IsInside(path, kSourceFolder); }


//=============================================================================
// 名前と種類
//=============================================================================
// ===== 大文字・小文字を区別しない名前の比べ方（並べる順） =====
bool NameLess(const std::string& a, const std::string& b) {
	return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
}

// ===== ファイルの名前から種類を決める =====
AssetIcon IconOf(const std::string& fileName) {
	if (fileName.ends_with(kSceneFileExtension)) {
		return AssetIcon::Scene;
	}
	if (IsAssetFile(fileName, AssetType::Model)) {
		return AssetIcon::Model;
	}
	if (IsAssetFile(fileName, AssetType::Texture)) {
		return AssetIcon::Texture;
	}
	if (IsAssetFile(fileName, AssetType::Sound)) {
		return AssetIcon::Sound;
	}
	if (CodeWindow::IsCodeFile(fileName)) {
		return AssetIcon::Code;
	}
	return AssetIcon::File;
}

// ===== 出さないファイルか =====
bool IsHidden(const std::string& fileName) {
	return std::any_of(std::begin(kHiddenExtensions), std::end(kHiddenExtensions), [&](std::string_view extension) { return fileName.ends_with(extension); });
}

// ===== 名前を変えるときに打つ部分（シーンは .scene.json の前、ほかのファイルは拡張子の前、フォルダは全部） =====
std::string EditableName(const std::filesystem::path& path, AssetIcon icon, bool isFolder) {
	const std::string fileName = ToUtf8(path.filename());
	if (isFolder) {
		return fileName;
	}
	if (icon == AssetIcon::Scene) {
		return fileName.substr(0, fileName.size() - std::string_view(kSceneFileExtension).size());
	}
	return ToUtf8(path.stem());
}

// ===== 名前を newName に変えた後のパス（拡張子はそのまま） =====
std::filesystem::path RenamedPath(const std::filesystem::path& path, AssetIcon icon, bool isFolder, const std::string& newName) {
	if (isFolder) {
		return path.parent_path() / newName;
	}
	if (icon == AssetIcon::Scene) {
		return path.parent_path() / (newName + kSceneFileExtension);
	}
	return path.parent_path() / (newName + path.extension().string());
}

// ===== シーンの名前で開けるシーンファイルか（resources/scenes/ の直下にある物だけ。LoadScene も名前からこの場所を探す） =====
bool IsOpenableScene(const std::filesystem::path& path, AssetIcon icon, const std::string& name) { return icon == AssetIcon::Scene && path == std::filesystem::path(MakeSceneFilePath(name)); }

// ===== folder の中で、まだ使われていない名前（NewFolder・NewFolder1・NewFolder2 …） =====
std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::string& baseName) {
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::filesystem::path path = folder / (number == 0 ? baseName : baseName + std::to_string(number));
		if (!std::filesystem::exists(path, error)) {
			return path;
		}
	}
}

// ===== まだ使われていないシーンの名前（NewScene・NewScene1 …） =====
std::string UniqueSceneName(const std::string& baseName) {
	for (int number = 0;; ++number) {
		const std::string name = (number == 0) ? baseName : baseName + std::to_string(number);
		if (!SceneManager::HasScene(name)) {
			return name;
		}
	}
}


//=============================================================================
// Windows
//=============================================================================
// ===== ごみ箱へ移す（Windows のごみ箱から元に戻せる） =====
bool MoveToRecycleBin(const std::filesystem::path& path) {
	std::error_code error;
	// SHFileOperation は「\0\0」で終わる絶対パスを受け取る（何個もまとめて渡せる形のため。std::wstring の終わりの \0 と足して2つ）
	std::wstring from = std::filesystem::absolute(path, error).make_preferred().wstring();
	from.push_back(L'\0');
	SHFILEOPSTRUCTW operation{};
	operation.wFunc = FO_DELETE;
	operation.pFrom = from.c_str();
	operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI; // ごみ箱へ・確かめるのはこちらで済んだ・進み具合を出さない
	if (SHFileOperationW(&operation) != 0 || operation.fAnyOperationsAborted) {
		LogManager::Warning(std::format("ごみ箱へ移せませんでした: {}", ToUtf8(path)));
		return false;
	}
	LogManager::Log(std::format("ごみ箱へ移しました: {}", ToUtf8(path)));
	return true;
}

// ===== エクスプローラーで、その物を選んだ状態で開く =====
void RevealInExplorer(const std::filesystem::path& path) {
	std::error_code error;
	const std::wstring arguments = L"/select,\"" + std::filesystem::absolute(path, error).make_preferred().wstring() + L"\"";
	ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

// ===== エクスプローラーで、フォルダの中を開く =====
void OpenInExplorer(const std::filesystem::path& folder) {
	std::error_code error;
	const std::wstring target = std::filesystem::absolute(folder, error).make_preferred().wstring();
	ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace ProjectFiles
#endif
