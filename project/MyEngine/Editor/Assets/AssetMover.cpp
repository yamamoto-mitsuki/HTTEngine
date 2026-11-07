#ifdef USE_IMGUI
#include "MyEngine/Editor/Assets/AssetMover.h"

#include <array>
#include <format>
#include <string>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <system_error>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Editor/Assets/ProjectFiles.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr auto kCodeExtensions = std::to_array<std::string_view>({".cpp", ".h", ".hpp", ".inl"}); // 前のパスが書いてないか見るコードのファイル
constexpr std::string_view kPathEndChars = "\"'/\\";                                          // パスの後ろに来てよい文字（"Boss" が "Boss2" の頭に当たらないように）

//=============================================================================
// 1つ前のファイル
//=============================================================================
// ===== .bak も一緒に動かす（ファイルを保存したときに残る1つ前の物。前の名前のまま残ると、どのファイルの物か分からなくなる）=====
void MoveBackup(const std::filesystem::path& from, const std::filesystem::path& to) {
	std::filesystem::path fromBackup = from;
	fromBackup += ".bak"; // += は、パスの最後にそのまま文字を足す（/ で区切らない）
	std::error_code error;
	if (!std::filesystem::is_regular_file(fromBackup, error)) {
		return; // 無い（フォルダの中の .bak は、フォルダと一緒に動いている）
	}
	std::filesystem::path toBackup = to;
	toBackup += ".bak";
	std::filesystem::rename(fromBackup, toBackup, error); // 新しい名前の .bak が前からあれば、上書きする（無いファイルの1つ前なので要らない）
	if (error) {
		LogManager::Warning(std::format("1つ前のファイルを動かせませんでした: {}（{}）", ToUtf8(fromBackup), error.message()));
	}
}

//=============================================================================
// ファイルの中のパス
//=============================================================================
// ===== resources/ の中の、パスを書く種類のファイルを書き換える =====
/// <returns>書き換えたファイルの数</returns>
int FixReferenceFiles(const MovedPath& moved) {
	int fixedCount = 0;
	std::error_code error;
	// increment(error)：for ( : ) で回すと、読めないフォルダがあったときに例外で止まるので、エラーコードで受けながら進める
	for (auto it = std::filesystem::recursive_directory_iterator(ProjectFiles::GetResourceFolder(), error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
		const std::filesystem::path& path = it->path();
		if (!it->is_regular_file(error) || !AssetMoveRegistry::IsReferenceFile(path)) {
			continue;
		}
		// --- 読む（壊れているファイルは触らない）---
		std::ifstream stream(path);
		SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
		stream.close(); // 同じファイルに書くので、先に閉じる
		if (json.is_discarded()) {
			LogManager::Warning(std::format("読めないので、中のパスを書き換えませんでした: {}", ToUtf8(path)));
			continue;
		}
		// --- 前のパスがあれば、書き換えて保存する（ファイルを書く物と同じ書き方なので、変えた行だけが git の差分に出る）---
		if (!SceneSerializer::RemapPaths(json, moved)) {
			continue;
		}
		if (SceneSerializer::WriteFile(ToUtf8(path), json)) {
			LogManager::Log(std::format("中のパスを書き換えました: {}", ToUtf8(path)));
			++fixedCount;
		}
	}
	if (error) {
		LogManager::Warning(std::format("resources/ の中を全部は見られませんでした（{}）", error.message()));
	}
	return fixedCount;
}

//=============================================================================
// コードの中のパス
//=============================================================================
// ===== line の中に、前のパスが書いてあるか（大文字・小文字は区別しない）=====
bool ContainsPath(const std::string& line, const std::string& lowerPath) {
	const std::string lowerLine = ToLowerAscii(line);
	for (size_t found = lowerLine.find(lowerPath); found != std::string::npos; found = lowerLine.find(lowerPath, found + 1)) {
		const size_t end = found + lowerPath.size();
		if (end == lowerLine.size() || kPathEndChars.find(lowerLine[end]) != std::string_view::npos) {
			return true;
		}
	}
	return false;
}

// ===== Source/ のコードに前のパスが書いてあれば、警告を出す =====
void WarnCodeReferences(const MovedPath& moved) {
	const std::string lowerPath = ToLowerAscii(moved.from);
	std::error_code error;
	for (auto it = std::filesystem::recursive_directory_iterator(ProjectFiles::GetSourceFolder(), error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
		const std::filesystem::path& path = it->path();
		const std::string extension = ToLowerAscii(ToUtf8(path.extension()));
		if (!it->is_regular_file(error) || std::ranges::find(kCodeExtensions, extension) == kCodeExtensions.end()) {
			continue;
		}
		std::ifstream stream(path);
		std::string line;
		for (int lineNumber = 1; std::getline(stream, line); ++lineNumber) {
			if (ContainsPath(line, lowerPath)) {
				LogManager::Warning(std::format("コードに前のパスが書いてあります（コードは書き換えていません）: {}:{}", ToUtf8(path), lineNumber));
			}
		}
	}
}
} // namespace


//=============================================================================
// 名前を変える
//=============================================================================
bool AssetMover::Move(const std::filesystem::path& from, const std::filesystem::path& to) {
	std::error_code error;
	// --- 同じ名前がもうあるならやめる（大文字・小文字だけ変えるときは、Windows では同じファイルなので通す）---
	if (std::filesystem::exists(to, error) && !std::filesystem::equivalent(from, to, error)) {
		LogManager::Warning(std::format("同じ名前がもうあります: {}", ToUtf8(to)));
		return false;
	}
	// --- 1. ディスク ---
	const bool isFile = std::filesystem::is_regular_file(from, error);
	std::filesystem::rename(from, to, error);
	if (error) {
		LogManager::Warning(std::format("名前を変えられませんでした: {}（{}）", ToUtf8(from), error.message()));
		return false;
	}
	if (isFile) {
		MoveBackup(from, to);
	}
	// --- 2. 覚えているパス（Manager・エディタの窓。中身を直している途中の物も、この後は新しいパスへ保存する）---
	const MovedPath moved{ToPathKey(from), ToPathKey(to)};
	AssetMoveRegistry::Notify(moved);
	// --- 3. ファイルの中のパス ---
	const int fixedCount = FixReferenceFiles(moved);
	// --- 4. コードの中のパス（警告だけ）---
	WarnCodeReferences(moved);
	LogManager::Log(std::format("名前を変えました: {} → {}（中のパスを書き換えたファイル: {} 個）", moved.from, moved.to, fixedCount));
	return true;
}
#endif
