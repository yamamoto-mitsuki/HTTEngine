#include "ComponentEditor.h"

#include <array>
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <string_view>

namespace {
std::vector<std::unique_ptr<ComponentEditor>> editors; // カテゴリ順（同じカテゴリの中は名前順）

/// <summary>
/// Add Component の上の段のカテゴリ1つ（名前と説明）
/// </summary>
struct TopLevelCategory {
	std::string_view name;   // カテゴリの名前（GetCategory の最初の "/" まで）
	const char* description; // Add Component でカテゴリにマウスを乗せると出る説明
};

// Add Component の上の段（この並びで出す）。ここに無い名前は後ろに回る
constexpr auto kTopLevelCategories = std::to_array<TopLevelCategory>({
    {"Core", "どの Entity も必ず持つ物（Transform）。Add Component には出ない"},
    {"Camera", "ゲームの絵を撮るカメラと、追いかけるなどのカメラの動き"},
    {"Rendering3D", "3D の見た目（モデル・霧・被写界深度など）"},
    {"Rendering2D", "3D の空間に置く画像の板など"},
    {"UI", "文字・図形など"},
    {"Lighting", "ライト"},
    {"Effects", "パーティクルや動いた跡の軌跡など"},
    {"Physics", "当たり判定や重力で落ちるなどの動き方など"},
    {"Audio", "音を鳴らす"},
    {"Gameplay", "ゲームの仕組みやCOMPONENT(...) で書いた物"},
    {"Animation", "モデルのアニメーション、時間に沿って値を動かす Timeline"},
    {"Combat", "攻撃の当たり・HP とダメージ・床の予告など"},
    {"Scripts", "SCRIPT(...) で書いた Script（Awake・Start・Update などを持つクラス）"},
});

// カテゴリの説明（上の段より下。"Gameplay/Movement" など。RegisterCategoryDescription で足す）
// std::less<> を付けると、std::string_view のままで探せる（探すたびに std::string を作らなくて済む）
std::map<std::string, const char*, std::less<>> categoryDescriptions;

// "Gameplay/Movement" の上の段は "Gameplay"
std::string_view TopLevelOf(std::string_view path) {
	const size_t slash = path.find('/');
	return slash == std::string_view::npos ? path : path.substr(0, slash);
}

// 上の段が何番目か（知らない名前は一番後ろ）
size_t TopLevelOrderOf(std::string_view path) {
	const std::string_view topLevel = TopLevelOf(path);
	for (size_t i = 0; i < std::size(kTopLevelCategories); ++i) {
		if (kTopLevelCategories[i].name == topLevel) {
			return i;
		}
	}
	return std::size(kTopLevelCategories);
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

void ComponentEditorRegistry::RegisterCategoryDescription(std::string_view path, const char* description) {
	// insert_or_assign：無ければ足し、あれば書き換える
	categoryDescriptions.insert_or_assign(std::string(path), description);
}

const char* ComponentEditorRegistry::FindCategoryDescription(std::string_view path) {
	// --- 上の段は表から ---
	const size_t order = TopLevelOrderOf(path);
	if (order < std::size(kTopLevelCategories) && kTopLevelCategories[order].name == path) {
		return kTopLevelCategories[order].description;
	}
	// --- その下は足された物から ---
	const auto found = categoryDescriptions.find(path);
	return (found != categoryDescriptions.end()) ? found->second : nullptr;
}