#include "NameList.h"

#include <format>
#include <utility>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/UI/GlobalVariables.h"

//=============================================================================
// 作る・登録
//=============================================================================
// ===== 作る =====
NameList::NameList(std::string groupName, std::string itemPrefix, std::initializer_list<std::string_view> fixedNames)
    : groupName_(std::move(groupName)), itemPrefix_(std::move(itemPrefix)) {
	for (const std::string_view name : fixedNames) {
		fixedNames_.emplace_back(name);
	}
}

// ===== Settings に名前の行を登録する =====
void NameList::Register() const {
	// ページ（グループ）とファイル（カテゴリ）は同じ名前。決まった名前の番号は出さない
	GlobalVariables::CategoryBuilder category = GlobalVariables::GetInstance()->Group(groupName_).Category(groupName_);
	for (int i = static_cast<int>(fixedNames_.size()); i < kCount; ++i) {
		category.Add(ItemName(i), std::string());
	}
}


//=============================================================================
// 名前を引く
//=============================================================================
// ===== 名前 =====
std::string NameList::GetName(int index) const {
	if (index < 0 || index >= kCount) {
		return {};
	}
	if (index < static_cast<int>(fixedNames_.size())) {
		return fixedNames_[index];
	}
	return GlobalVariables::GetInstance()->Get<std::string>(groupName_, groupName_, ItemName(index));
}

// ===== 名前から番号 =====
int NameList::Find(std::string_view name) const {
	if (name.empty()) {
		return -1;
	}
	for (int i = 0; i < kCount; ++i) {
		if (GetName(i) == name) {
			return i;
		}
	}
	return -1;
}

// ===== Inspector に出す文字 =====
std::string NameList::GetLabel(int index) const {
	const std::string name = GetName(index);
	return name.empty() ? std::format("{}: ({})", index, ItemName(index)) : std::format("{}: {}", index, name);
}

// ===== 名前のビットを立てた並び =====
uint32_t NameList::MakeMask(std::initializer_list<std::string_view> names) const {
	uint32_t bits = 0;
	for (const std::string_view name : names) {
		const int index = Find(name);
		if (index < 0) {
			LogManager::Warning(std::format("{} に \"{}\" という名前はありません（Settings の {} で付けてください）", groupName_, name, groupName_));
			continue;
		}
		bits |= (1u << index);
	}
	return bits;
}

// ===== 名前の付いている番号の一覧 =====
std::vector<int> NameList::GetNamedIndices() const {
	std::vector<int> indices;
	for (int i = 0; i < kCount; ++i) {
		if (!GetName(i).empty()) {
			indices.push_back(i);
		}
	}
	return indices;
}

// ===== Settings の行の名前 =====
std::string NameList::ItemName(int index) const { return std::format("{} {}", itemPrefix_, index); }
