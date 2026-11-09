#include "AssetMoveRegistry.h"

#include <algorithm>

#include "HTTEngine/String/ConvertString.h"

namespace {
// 知らせを受ける関数と、パスを書くファイルの種類（Manager の Initialize から足すので、関数の中の static にする。先に作られる保証がある）
std::vector<AssetMoveRegistry::Listener>& Listeners() {
	static std::vector<AssetMoveRegistry::Listener> listeners;
	return listeners;
}
std::vector<std::string>& ReferenceFileTypes() {
	static std::vector<std::string> suffixes; // 小文字にした物
	return suffixes;
}

// text の頭が prefix か（大文字・小文字は区別しない）
bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) {
	return text.size() >= prefix.size() && ToLowerAscii(text.substr(0, prefix.size())) == ToLowerAscii(prefix);
}
} // namespace


//=============================================================================
// MovedPath
//=============================================================================
// ===== 動いた物なら付け替える =====
bool MovedPath::Apply(std::string& path) const {
	if (from.empty() || path.size() < from.size()) {
		return false; // 短すぎる（前のパスより短い文字は、動いた物ではない）
	}
	const std::string key = ToPathKey(ToPath(path));
	if (!StartsWithIgnoreCase(key, from)) {
		return false;
	}
	// --- そのもの、またはフォルダの中の物（"Resources/Enemy" は "Resources/EnemyBoss" を含まない）---
	const std::string_view rest = std::string_view(key).substr(from.size());
	if (!rest.empty() && rest.front() != '/') {
		return false;
	}
	path = to + std::string(rest);
	return true;
}

// ===== パスの型 =====
bool MovedPath::Apply(std::filesystem::path& path) const {
	std::string text = ToUtf8(path);
	if (!Apply(text)) {
		return false;
	}
	path = ToPath(text);
	return true;
}


//=============================================================================
// 登録と知らせ
//=============================================================================
// ===== 知らせを受ける関数を足す =====
void AssetMoveRegistry::AddListener(Listener listener) {
	std::vector<Listener>& listeners = Listeners();
	if (listener != nullptr && std::ranges::find(listeners, listener) == listeners.end()) {
		listeners.push_back(listener);
	}
}

// ===== パスを書くファイルの種類を足す =====
void AssetMoveRegistry::AddReferenceFileType(std::string_view suffix) {
	std::vector<std::string>& suffixes = ReferenceFileTypes();
	const std::string lower = ToLowerAscii(suffix);
	if (!lower.empty() && std::ranges::find(suffixes, lower) == suffixes.end()) {
		suffixes.push_back(lower);
	}
}

// ===== 知らせる =====
void AssetMoveRegistry::Notify(const MovedPath& moved) {
	for (const Listener listener : Listeners()) {
		listener(moved);
	}
}

// ===== パスを書く種類のファイルか =====
bool AssetMoveRegistry::IsReferenceFile(const std::filesystem::path& path) {
	// 拡張子ではなく名前の後ろで見る（シーンの ".scene.json" は、拡張子だけだと ".json" になる）
	const std::string name = ToLowerAscii(ToUtf8(path.filename()));
	return std::ranges::any_of(ReferenceFileTypes(), [&name](const std::string& suffix) { return name.ends_with(suffix); });
}
