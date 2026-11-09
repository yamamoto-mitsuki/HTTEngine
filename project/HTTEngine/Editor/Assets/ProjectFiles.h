#pragma once
#include <array>
#include <filesystem>
#include <string>

#include "HTTEngine/Editor/Assets/AssetIcons.h"

/// <summary>
/// Project ウィンドウが使う、ファイルの道具（状態を持たない。ImGui で描かない）
/// </summary>
namespace ProjectFiles {

// ===== 出すフォルダ =====

// 出すフォルダの一覧（resources/ と Source/。エンジンの HTTEngine/Resources/ は出さない）
const std::array<std::filesystem::path, 2>& GetRootFolders();
// ゲームのアセットのフォルダ（resources/）
const std::filesystem::path& GetResourceFolder();
// ゲームのコードのフォルダ（Source/）
const std::filesystem::path& GetSourceFolder();
// path が folder の中にあるか（folder そのものも含む）
bool IsInside(const std::filesystem::path& path, const std::filesystem::path& folder);
// path が、出しているフォルダ（resources/ か Source/）の中にあるか
bool IsInsideRoots(const std::filesystem::path& path);
// path が、ゲームのコードのフォルダ（Source/）の中にあるか
bool IsInsideSource(const std::filesystem::path& path);
// path が、出しているフォルダそのものか（左の木の一番上）
bool IsRootFolder(const std::filesystem::path& path);

// ===== 名前と種類 =====

// 大文字・小文字を区別しない名前の比べ方（並べる順）
bool NameLess(const std::string& a, const std::string& b);
// ファイルの名前から種類を決める
AssetIcon IconOf(const std::string& fileName);
// 出さないファイルか
bool IsHidden(const std::string& fileName);
// 名前を変えるときに打つ部分（シーンは .scene.json の前、ほかのファイルは拡張子の前、フォルダは全部）
std::string EditableName(const std::filesystem::path& path, AssetIcon icon, bool isFolder);
// 名前を newName に変えた後のパス（拡張子はそのまま）
std::filesystem::path RenamedPath(const std::filesystem::path& path, AssetIcon icon, bool isFolder, const std::string& newName);
// シーンの名前で開けるシーンファイルか（resources/scenes/ の直下にある物だけ。LoadScene も名前からこの場所を探す）
bool IsOpenableScene(const std::filesystem::path& path, AssetIcon icon, const std::string& name);
// folder の中で、まだ使われていない名前（NewFolder・NewFolder1・NewFolder2 …）
std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::string& baseName);
// まだ使われていないシーンの名前（NewScene・NewScene1 …）
std::string UniqueSceneName(const std::string& baseName);

// ===== Windows =====

// ごみ箱へ移す（Windows のごみ箱から元に戻せる）
bool MoveToRecycleBin(const std::filesystem::path& path);
// エクスプローラーで、その物を選んだ状態で開く
void RevealInExplorer(const std::filesystem::path& path);
// エクスプローラーで、フォルダの中を開く
void OpenInExplorer(const std::filesystem::path& folder);

} // namespace ProjectFiles
