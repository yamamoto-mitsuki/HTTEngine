#include "ComponentEditor.h"

#include <array>
#include <algorithm>
#include <iterator>
#include <string_view>

namespace {
std::vector<std::unique_ptr<ComponentEditor>> editors; // カテゴリ順（同じカテゴリの中は名前順）

// Add Componentの、上の段の並び順。ここに無い名前は後ろに回る
constexpr auto kTopLevelOrder = std::to_array<std::string_view>({"Core", "Camera", "Rendering3D", "Rendering2D", "UI", "Lighting", "Effects", "Physics", "Audio", "Gameplay"});

// "Gameplay/Movement" の上の段は "Gameplay"
std::string_view TopLevelOf(std::string_view path) {
	const size_t slash = path.find('/');
	return slash == std::string_view::npos ? path : path.substr(0, slash);
}

// 上の段が何番目か（知らない名前は一番後ろ）
size_t TopLevelOrderOf(std::string_view path) {
	const std::string_view topLevel = TopLevelOf(path);
	for (size_t i = 0; i < std::size(kTopLevelOrder); ++i) {
		if (kTopLevelOrder[i] == topLevel) {
			return i;
		}
	}
	return std::size(kTopLevelOrder);
}

// aがbより前に来るか。上の段の順 → カテゴリのパスの文字順 → 表示名の文字順
// （"Gameplay" は "Gameplay/Movement" より前。だから各段で「そこで終わる物」が先に並ぶ）
// 登録した順に頼らないので、ファイルが増えても並びが変わらない
bool IsBefore(const ComponentEditor& a, const ComponentEditor& b) {
	const size_t orderA = TopLevelOrderOf(a.GetCategory());
	const size_t orderB = TopLevelOrderOf(b.GetCategory());
	if (orderA != orderB) {
		return orderA < orderB;
	}
	const std::string_view categoryA(a.GetCategory());
	const std::string_view categoryB(b.GetCategory());
	if (categoryA != categoryB) {
		return categoryA < categoryB;
	}
	return std::string_view(a.GetName()) < std::string_view(b.GetName());
}
} // namespace

void ComponentEditorRegistry::Register(std::unique_ptr<ComponentEditor> editor) {
	for (const std::unique_ptr<ComponentEditor>& registered : editors) {
		if (std::string_view(registered->GetName()) == editor->GetName()) {
			return;
		}
	}
	// 並びの決まった場所に入れる（Coreが必ず先頭、Gameplayが最後になる）
	const auto position = std::find_if(editors.begin(), editors.end(), [&editor](const std::unique_ptr<ComponentEditor>& registered) { return IsBefore(*editor, *registered); });
	editors.insert(position, std::move(editor));
}

const std::vector<std::unique_ptr<ComponentEditor>>& ComponentEditorRegistry::GetAll() { return editors; }