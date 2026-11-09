#include "DamageTypes.h"

#include "HTTEngine/Core/NameList.h"

//=============================================================================
// 名前の一覧
//=============================================================================
// ===== 名前の一覧（関数の中の static。初めて呼んだときに作る）=====
const NameList& DamageTypes::GetNameList() {
	static const NameList names("DamageTypes", "Type", {"Default"}); // 保存先のフォルダの名前になるので、空白を入れない
	return names;
}

// ===== Settings に名前の行を登録する =====
void DamageTypes::Initialize() { GetNameList().Register(); }


//=============================================================================
// 番号 ↔ 名前
//=============================================================================
std::string DamageTypes::TypeToName(int type) { return GetNameList().GetName(type); }

int DamageTypes::NameToType(std::string_view name) { return GetNameList().Find(name); }

uint32_t DamageTypes::GetMask(std::initializer_list<std::string_view> names) { return GetNameList().MakeMask(names); }
