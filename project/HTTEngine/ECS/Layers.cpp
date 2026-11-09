#include "Layers.h"

#include "HTTEngine/Core/NameList.h"

//=============================================================================
// 名前の一覧
//=============================================================================
// ===== 名前の一覧（関数の中の static。初めて呼んだときに作る）=====
const NameList& Layers::GetNameList() {
	static const NameList names("Layers", "Layer", {"Default"});
	return names;
}

// ===== Settings に名前の行を登録する =====
void Layers::Initialize() { GetNameList().Register(); }


//=============================================================================
// 番号 ↔ 名前
//=============================================================================
std::string Layers::LayerToName(int layer) { return GetNameList().GetName(layer); }

int Layers::NameToLayer(std::string_view name) { return GetNameList().Find(name); }

LayerMask Layers::GetMask(std::initializer_list<std::string_view> names) { return {GetNameList().MakeMask(names)}; }
