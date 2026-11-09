#include "GlobalVariables.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include <array>
#include <Windows.h>
#include <format>
#include <fstream>
#include <sstream>
#ifdef USE_IMGUI
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#endif

namespace {
// "Enemy/Transform" → {"Enemy", "Transform"}（空の段は飛ばす）
std::vector<std::string> SplitCategoryPath(const std::string& categoryPath) {
	std::vector<std::string> names;
	std::istringstream ss(categoryPath);
	std::string token;
	while (std::getline(ss, token, '/')) {
		if (!token.empty()) {
			names.push_back(token);
		}
	}
	return names;
}

// 数が count 個の配列か（Vector2〜4 の値）
bool IsNumberArray(const nlohmann::json& value, size_t count) {
	if (!value.is_array() || value.size() != count) {
		return false;
	}
	for (const nlohmann::json& element : value) {
		if (!element.is_number()) {
			return false;
		}
	}
	return true;
}
} // namespace

//=============================================================================
// シングルトン
//=============================================================================
GlobalVariables* GlobalVariables::GetInstance() {
	static GlobalVariables instance;
	return &instance;
}

//=============================================================================
// グループを宣言する
// 既に存在する場合はそのルートノードをそのまま返す（データをリセットしない）。
//=============================================================================
GlobalVariables::GroupRoot GlobalVariables::Group(const std::string& groupName, SettingsSection section) {
	// 既存グループを検索
	if (GroupData* group = FindGroup(groupName)) {
		return GroupRoot(&group->root, groupName);
	}
	// 新規作成（置き場所はここで決まる。保存先のフォルダも section で変わる）
	groups_.push_back({groupName, section, GVNode{}});
	LogManager::Log("RegisterGroup: " + groupName);
	return GroupRoot(&groups_.back().root, groupName);
}

//=============================================================================
// グループを名前で探す
//=============================================================================
const GlobalVariables::GroupData* GlobalVariables::FindGroup(const std::string& groupName) const {
	for (const GroupData& group : groups_) {
		if (group.name == groupName) {
			return &group;
		}
	}
	return nullptr;
}

GlobalVariables::GroupData* GlobalVariables::FindGroup(const std::string& groupName) {
	// const版に委譲してconst_castで返す
	return const_cast<GroupData*>(static_cast<const GlobalVariables*>(this)->FindGroup(groupName));
}

//=============================================================================
// groupName と "/" 区切りの categoryPath からノードを検索する
//=============================================================================
const GlobalVariables::GVNode* GlobalVariables::FindNode(const std::string& groupName, const std::string& categoryPath) const {
	const GroupData* group = FindGroup(groupName);
	if (!group) {
		return nullptr;
	}
	// "/" 区切りでパスを分割して辿る
	const GVNode* node = &group->root;
	for (const std::string& token : SplitCategoryPath(categoryPath)) {
		bool found = false;
		for (const auto& e : node->entries_) {
			if (e.name == token && std::holds_alternative<std::shared_ptr<GVNode>>(e.value)) {
				node = std::get<std::shared_ptr<GVNode>>(e.value).get();
				found = true;
				break;
			}
		}
		if (!found) {
			return nullptr;
		}
	}
	return node;
}

// ===== 非const版 =====
GlobalVariables::GVNode* GlobalVariables::FindNode(const std::string& groupName, const std::string& categoryPath) {
	// const版に委譲してconst_castで返す
	return const_cast<GVNode*>(static_cast<const GlobalVariables*>(this)->FindNode(groupName, categoryPath));
}

//=============================================================================
// グループのルートノードを検索する
//=============================================================================
GlobalVariables::GVNode* GlobalVariables::FindGroupRoot(const std::string& groupName) {
	GroupData* group = FindGroup(groupName);
	return group ? &group->root : nullptr;
}

//=============================================================================
// 保存先のフォルダ（エディタだけのグループは Parameters/Editor/ の下）
//=============================================================================
std::filesystem::path GlobalVariables::GroupDirectory(const std::string& groupName) const {
	const GroupData* group = FindGroup(groupName);
	const bool isEditor = group && group->section == SettingsSection::Editor;
	return std::filesystem::path(isEditor ? kEditorDirectoryPath : kDirectoryPath) / groupName;
}

//=============================================================================
// 項目1つの値を書き換える（Undo・Redo から）
//=============================================================================
bool GlobalVariables::SetItem(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, const Item& value) {
	GVNode* node = FindNode(groupName, categoryPath);
	Item* item = node ? node->FindItem(itemName) : nullptr;
	if (!item || item->index() != value.index()) {
		return false; // 見つからない・型が違う
	}
	*item = value;
	return true;
}

//=============================================================================
// 保存した値（*）
//=============================================================================
// ノードの中の全アイテムを「保存した」にする
void GlobalVariables::MarkSaved(GVNode& node) {
	for (GVNode::Entry& entry : node.entries_) {
		if (const Item* item = std::get_if<Item>(&entry.value)) {
			entry.saved = *item;
		} else {
			MarkSaved(*std::get<std::shared_ptr<GVNode>>(entry.value));
		}
	}
}

// ノードの中に、保存した値と違うアイテムがあるか
bool GlobalVariables::IsDirty(const GVNode& node) {
	for (const GVNode::Entry& entry : node.entries_) {
		if (const Item* item = std::get_if<Item>(&entry.value)) {
			if (!(*item == entry.saved)) {
				return true;
			}
		} else if (IsDirty(*std::get<std::shared_ptr<GVNode>>(entry.value))) {
			return true;
		}
	}
	return false;
}

#ifdef USE_IMGUI
//=============================================================================
// Settings ウィンドウから使う
//=============================================================================
std::vector<std::string> GlobalVariables::GetGroupNames(SettingsSection section) const {
	std::vector<std::string> names;
	for (const GroupData& group : groups_) {
		if (group.section == section) {
			names.push_back(group.name);
		}
	}
	return names;
}

bool GlobalVariables::IsGroupDirty(const std::string& groupName) const {
	const GroupData* group = FindGroup(groupName);
	return group && IsDirty(group->root);
}

std::string GlobalVariables::GetSaveDirectory(const std::string& groupName) const { return GroupDirectory(groupName).generic_string() + "/"; }

void GlobalVariables::RevertGroup(const std::string& groupName) {
	GroupData* group = FindGroup(groupName);
	if (!group) {
		return;
	}
	RevertNode(group->root, groupName, "");
	CommitEdits(); // 戻した分を1件の履歴にする（Ctrl+Z で、戻す前の値にもどせる）
}

void GlobalVariables::RevertNode(GVNode& node, const std::string& groupName, const std::string& categoryPath) {
	for (GVNode::Entry& entry : node.entries_) {
		if (Item* item = std::get_if<Item>(&entry.value)) {
			if (!(*item == entry.saved)) {
				RecordEdit(groupName, categoryPath, entry.name, *item, entry.saved);
				*item = entry.saved;
			}
		} else {
			const std::string childPath = categoryPath.empty() ? entry.name : categoryPath + "/" + entry.name;
			RevertNode(*std::get<std::shared_ptr<GVNode>>(entry.value), groupName, childPath);
		}
	}
}

//=============================================================================
// グループ1つ分（Settings ウィンドウの1ページ）を描画する
// 直下のカテゴリは <カテゴリ名>.json の単位。保存していない変更があれば見出しに * を付ける
//=============================================================================
void GlobalVariables::DrawGroup(const std::string& groupName) {
	GroupData* group = FindGroup(groupName);
	if (!group) {
		return;
	}
	for (GVNode::Entry& entry : group->root.entries_) {
		// カテゴリに属さないアイテムはその場で描画（通常は無い）
		if (Item* item = std::get_if<Item>(&entry.value)) {
			DrawItem(groupName, "", entry.name, *item);
			continue;
		}
		// 「###」の後ろを ImGui の ID にする（* が付いたり消えたりしても、開いた・閉じたが変わらないように）
		GVNode& category = *std::get<std::shared_ptr<GVNode>>(entry.value);
		const std::string label = std::format("{}{}###{}/{}", entry.name, IsDirty(category) ? " *" : "", groupName, entry.name);
		if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();
			DrawNode(category, groupName, entry.name);
			ImGui::Unindent();
		}
	}
}

void GlobalVariables::EndDraw() {
	// どの項目も触っていない（マウスを離した・入力を確定した）なら、ここまでの変更を1件の履歴にする
	if (!ImGui::IsAnyItemActive()) {
		CommitEdits();
	}
}

//=============================================================================
// ノードを再帰的に描画する
//=============================================================================
void GlobalVariables::DrawNode(GVNode& node, const std::string& groupName, const std::string& categoryPath) {
	for (auto& entry : node.entries_) {
		if (std::holds_alternative<Item>(entry.value)) {
			// ① itemはその場で直接描画
			DrawItem(groupName, categoryPath, entry.name, std::get<Item>(entry.value));

		} else {
			// ② 子カテゴリはCollapsingHeaderを開いて中身を再帰描画
			auto& childNode = std::get<std::shared_ptr<GVNode>>(entry.value);
			std::string childPath = categoryPath + "/" + entry.name;
			std::string headerLabel = entry.name + "##" + groupName + "/" + childPath;

			if (!ImGui::CollapsingHeader(headerLabel.c_str())) {
				continue;
			}
			ImGui::Indent();
			DrawNode(*childNode, groupName, childPath);
			ImGui::Unindent();
		}
	}
}

//=============================================================================
// 1つのアイテムをImGuiで描画する
// ラベル（左）＋ ウィジェット（右）の2列レイアウト
//=============================================================================
void GlobalVariables::DrawItem(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, Item& item) {
	const std::string& label = itemName;
	const std::string uid = "##" + groupName + "/" + categoryPath + "/" + itemName; // ImGuiのID（表示はしない）
	const Item before = item;                                                       // 描く前の値。描いた後と比べて、変わっていたらUndoのために覚える

	std::visit(
	    [&](auto& v) {
		    using T = std::decay_t<decltype(v)>;

		    if constexpr (std::is_same_v<T, bool>) {
			    ImGui::Text("%s", label.c_str());
			    ImGui::SameLine(150.0f);
			    ImGui::Checkbox(uid.c_str(), &v);

		    } else if constexpr (std::is_same_v<T, int32_t>) {
			    ImGui::Text("%s", label.c_str());
			    ImGui::SameLine(150.0f);
			    ImGui::SetNextItemWidth(-1);
			    ImGui::DragInt(uid.c_str(), &v);

		    } else if constexpr (std::is_same_v<T, float>) {
			    ImGui::Text("%s", label.c_str());
			    ImGui::SameLine(150.0f);
			    ImGui::SetNextItemWidth(-1);
			    ImGui::DragFloat(uid.c_str(), &v, 0.01f);

		    } else if constexpr (std::is_same_v<T, Vector2>) {
			    ImGui::Text("%s", label.c_str());
			    ImGui::SetNextItemWidth(-1);
			    ImGui::DragFloat2(uid.c_str(), &v.x, 0.01f);

		    } else if constexpr (std::is_same_v<T, Vector3>) {
			    ImGui::Text("%s", label.c_str());
			    ImGui::SetNextItemWidth(-1);
			    ImGui::DragFloat3(uid.c_str(), &v.x, 0.01f);

		    } else if constexpr (std::is_same_v<T, Vector4>) {
			    ImGui::Text("%s", label.c_str());
			    ImGui::SetNextItemWidth(-1);
			    ImGui::DragFloat4(uid.c_str(), &v.x, 0.01f);

		    } else if constexpr (std::is_same_v<T, ComboItem>) {
			    std::vector<const char*> cstrs;
			    for (const auto& s : v.options) {
				    cstrs.push_back(s.c_str());
			    }
			    ImGui::Text("%s", label.c_str());
			    ImGui::SetNextItemWidth(-1);
			    ImGui::Combo(uid.c_str(), &v.currentIndex, cstrs.data(), static_cast<int>(cstrs.size()));

		    } else if constexpr (std::is_same_v<T, ColorItem>) {
			    std::array<float, 4> col{};
			    col[0] = ((v.rgba >> 24) & 0xFF) / 255.0f;
			    col[1] = ((v.rgba >> 16) & 0xFF) / 255.0f;
			    col[2] = ((v.rgba >> 8) & 0xFF) / 255.0f;
			    col[3] = (v.rgba & 0xFF) / 255.0f;
			    ImGui::Text("%s", label.c_str());
			    ImGui::SetNextItemWidth(-1);
			    if (ImGui::ColorEdit4(uid.c_str(), col.data())) {
				    uint32_t r = static_cast<uint32_t>(col[0] * 255.0f);
				    uint32_t g = static_cast<uint32_t>(col[1] * 255.0f);
				    uint32_t b = static_cast<uint32_t>(col[2] * 255.0f);
				    uint32_t a = static_cast<uint32_t>(col[3] * 255.0f);
				    v.rgba = (r << 24) | (g << 16) | (b << 8) | a;
			    }

		    } else if constexpr (std::is_same_v<T, std::string>) {
			    // 文字（長さの上限なし。日本語も打てる）。ラベルは左、入力欄は右（bool・int と同じ並び）
			    ImGui::Text("%s", label.c_str());
			    ImGui::SameLine(150.0f);
			    ImGui::SetNextItemWidth(-1);
			    EditorWidgets::InputText(uid.c_str(), v);
		    }
	    },
	    item);

	if (!(item == before)) {
		RecordEdit(groupName, categoryPath, itemName, before, item);
	}
}

//=============================================================================
// Undo・Redo
//=============================================================================
// 変わった項目を覚える。同じ項目がもうあれば「変えた後」だけ新しくする（「前」は触り始めたときのまま）
void GlobalVariables::RecordEdit(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, const Item& before, const Item& after) {
	for (PendingEdit& edit : pendingEdits_) {
		if (edit.groupName == groupName && edit.categoryPath == categoryPath && edit.itemName == itemName) {
			edit.after = after;
			return;
		}
	}
	pendingEdits_.push_back({groupName, categoryPath, itemName, before, after});
}

// まとめて1件の履歴にする。戻すときは「前」、やり直すときは「後」を書く
void GlobalVariables::CommitEdits() {
	std::erase_if(pendingEdits_, [](const PendingEdit& edit) { return edit.before == edit.after; }); // 動かして元に戻しただけの物は捨てる
	if (pendingEdits_.empty()) {
		return;
	}
	auto apply = [](const std::vector<PendingEdit>& edits, bool undo) {
		for (const PendingEdit& edit : edits) {
			if (!GetInstance()->SetItem(edit.groupName, edit.categoryPath, edit.itemName, undo ? edit.before : edit.after)) {
				return false;
			}
		}
		return true;
	};
	EditorHistory::Record([apply, edits = pendingEdits_] { return apply(edits, true); }, [apply, edits = pendingEdits_] { return apply(edits, false); });
	pendingEdits_.clear();
}
#endif

//=============================================================================
// カテゴリ1つを Parameters/<グループ名>/<カテゴリ名>.json に書き出す
//=============================================================================
void GlobalVariables::SaveCategory(const std::string& groupName, const std::string& categoryName) {
	GVNode* root = FindGroupRoot(groupName);
	if (!root) {
		LogManager::Error("[GlobalVariables::SaveCategory] グループが見つかりません: " + groupName);
		return;
	}
	GVNode* category = root->FindChild(categoryName);
	if (!category) {
		LogManager::Error("[GlobalVariables::SaveCategory] カテゴリが見つかりません: " + groupName + " / " + categoryName);
		return;
	}
	if (WriteNodeToFile(CategoryFilePath(groupName, categoryName), *category)) {
		MarkSaved(*category); // ファイルと同じになった（* が消える）
	}
}

//=============================================================================
// グループ内の全カテゴリを一括保存する
//=============================================================================
void GlobalVariables::SaveGroup(const std::string& groupName) {
	GVNode* root = FindGroupRoot(groupName);
	if (!root) {
		LogManager::Error("[GlobalVariables::SaveGroup] グループが見つかりません: " + groupName);
		return;
	}

	int savedCount = 0;
	for (const auto& entry : root->entries_) {
		// カテゴリ（子ノード）だけがファイルの単位
		if (!std::holds_alternative<std::shared_ptr<GVNode>>(entry.value)) {
			continue;
		}
		GVNode& category = *std::get<std::shared_ptr<GVNode>>(entry.value);
		if (WriteNodeToFile(CategoryFilePath(groupName, entry.name), category)) {
			MarkSaved(category); // ファイルと同じになった（* が消える）
			++savedCount;
		}
	}
	LogManager::Log(std::format("[GlobalVariables::SaveGroup] {} のカテゴリを{}件保存しました", groupName, savedCount));
}

//=============================================================================
// 全グループを一括保存
//=============================================================================
void GlobalVariables::SaveAll() {
	for (const GroupData& group : groups_) {
		SaveGroup(group.name);
	}
	LogManager::Log("[GlobalVariables::SaveAll] 全グループ保存完了");
}

//=============================================================================
// ノードの中身をjsonのルートに書き出してファイルへ保存する
//=============================================================================
bool GlobalVariables::WriteNodeToFile(const std::filesystem::path& filePath, const GVNode& node) const {
	json j = json::object();
	NodeToJson(node, j);

	// ディレクトリ作成
	std::error_code ec;
	std::filesystem::create_directories(filePath.parent_path(), ec);

	std::ofstream ofs(filePath);
	if (!ofs) {
		LogManager::Error("[GlobalVariables] ファイルを開けません: " + filePath.string());
		assert(false && "GlobalVariables: ファイルオープン失敗");
		return false;
	}
	ofs << std::setw(4) << j << std::endl;
	LogManager::Log("[GlobalVariables] 保存完了: " + filePath.string());
	return true;
}

//=============================================================================
// アイテム1つ ↔ jsonの値
//=============================================================================
GlobalVariables::json GlobalVariables::ItemToJson(const Item& item) {
	return std::visit(
	    [](const auto& v) -> json {
		    using T = std::decay_t<decltype(v)>;
		    if constexpr (std::is_same_v<T, Vector2>) {
			    return json::array({v.x, v.y});
		    } else if constexpr (std::is_same_v<T, Vector3>) {
			    return json::array({v.x, v.y, v.z});
		    } else if constexpr (std::is_same_v<T, Vector4>) {
			    return json::array({v.x, v.y, v.z, v.w});
		    } else if constexpr (std::is_same_v<T, ColorItem>) {
			    return static_cast<int32_t>(v.rgba);
		    } else if constexpr (std::is_same_v<T, ComboItem>) {
			    return v.currentIndex;
		    } else {
			    return v; // bool・int32_t・float・std::string はそのまま
		    }
	    },
	    item);
}

bool GlobalVariables::JsonToItem(const json& value, Item& item) {
	return std::visit(
	    [&](auto& v) -> bool {
		    using T = std::decay_t<decltype(v)>;
		    if constexpr (std::is_same_v<T, bool>) {
			    if (!value.is_boolean()) {
				    return false;
			    }
			    v = value.get<bool>();
		    } else if constexpr (std::is_same_v<T, int32_t>) {
			    if (!value.is_number_integer()) {
				    return false;
			    }
			    v = value.get<int32_t>();
		    } else if constexpr (std::is_same_v<T, float>) {
			    if (!value.is_number()) {
				    return false;
			    }
			    v = value.get<float>();
		    } else if constexpr (std::is_same_v<T, Vector2>) {
			    if (!IsNumberArray(value, 2)) {
				    return false;
			    }
			    v = Vector2{value[0].get<float>(), value[1].get<float>()};
		    } else if constexpr (std::is_same_v<T, Vector3>) {
			    if (!IsNumberArray(value, 3)) {
				    return false;
			    }
			    v = Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
		    } else if constexpr (std::is_same_v<T, Vector4>) {
			    if (!IsNumberArray(value, 4)) {
				    return false;
			    }
			    v = Vector4{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
		    } else if constexpr (std::is_same_v<T, ColorItem>) {
			    if (!value.is_number_integer()) {
				    return false;
			    }
			    v.rgba = static_cast<uint32_t>(value.get<int32_t>());
		    } else if constexpr (std::is_same_v<T, ComboItem>) {
			    if (!value.is_number_integer()) {
				    return false;
			    }
			    v.currentIndex = value.get<int>();
		    } else if constexpr (std::is_same_v<T, std::string>) {
			    if (!value.is_string()) {
				    return false;
			    }
			    v = value.get<std::string>();
		    }
		    return true;
	    },
	    item);
}

//=============================================================================
// ノードをjsonに再帰的に書き出す
// アイテムはフラットに、子カテゴリは子オブジェクトとして書き出す
//=============================================================================
void GlobalVariables::NodeToJson(const GVNode& node, json& out) const {
	for (const auto& entry : node.entries_) {
		if (const Item* item = std::get_if<Item>(&entry.value)) {
			out[entry.name] = ItemToJson(*item);
		} else {
			json childJson = json::object();
			NodeToJson(*std::get<std::shared_ptr<GVNode>>(entry.value), childJson);
			out[entry.name] = childJson;
		}
	}
}

//=============================================================================
// jsonファイルの読み込み
// ファイルなし・壊れたjsonは false を返すだけで落とさない
//=============================================================================
bool GlobalVariables::ReadJsonFile(const std::filesystem::path& filePath, json& out) const {
	std::ifstream ifs(filePath);
	if (!ifs) {
		LogManager::Warning("[GlobalVariables] ファイルなし（スキップ）: " + filePath.string());
		return false;
	}
	// 例外を投げない版のパース
	out = json::parse(ifs, nullptr, false);
	if (out.is_discarded()) {
		LogManager::Error("[GlobalVariables] jsonの解析に失敗しました: " + filePath.string());
		return false;
	}
	return true;
}

//=============================================================================
// これから登録するアイテム1つの値を、保存したファイルから読む
//=============================================================================
void GlobalVariables::ReadSavedItem(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, Item& item) const {
	// categoryPath の1つ目がファイル（<カテゴリ名>.json）、2つ目からはファイルの中の入れ子
	const std::vector<std::string> names = SplitCategoryPath(categoryPath);
	if (names.empty()) {
		return; // カテゴリに入っていないアイテムは保存されない
	}
	const std::filesystem::path filePath = CategoryFilePath(groupName, names[0]);
	std::error_code error;
	json file;
	if (!std::filesystem::exists(filePath, error) || !ReadJsonFile(filePath, file)) {
		return; // まだ保存していない（登録した値のまま）
	}
	const json* node = &file;
	for (size_t i = 1; i < names.size(); ++i) {
		const auto it = node->find(names[i]);
		if (it == node->end() || !it->is_object()) {
			return;
		}
		node = &*it;
	}
	const auto it = node->find(itemName);
	if (it != node->end()) {
		JsonToItem(*it, item); // 型が違う値は入れない（登録した値のまま）
	}
}

//=============================================================================
// カテゴリ1つ分のjsonを読み込む
// 既に登録済みのアイテムにのみ値を適用する（新規アイテムは作らない）
//=============================================================================
void GlobalVariables::LoadCategory(const std::string& groupName, const std::string& categoryName) {
	std::filesystem::path filePath = CategoryFilePath(groupName, categoryName);
	json j;
	if (!ReadJsonFile(filePath, j)) {
		return;
	}

	GVNode* root = FindGroupRoot(groupName);
	if (!root) {
		// まだグループが登録されていない場合はスキップ（登録したときに、アイテムごとにファイルから読む）
		return;
	}
	GVNode* category = root->FindChild(categoryName);
	if (!category) {
		// まだカテゴリが登録されていない場合もスキップ
		return;
	}
	JsonToNode(j, *category);
	LogManager::Log("[GlobalVariables::LoadCategory] 読み込み完了: " + filePath.string());
}

//=============================================================================
// グループ1つ分を読み込む
//=============================================================================
void GlobalVariables::LoadGroup(const std::string& groupName) { LoadGroupCategories(groupName); }

//=============================================================================
// Parameters/<グループ名>/ 以下の全カテゴリを読み込む
//=============================================================================
void GlobalVariables::LoadGroupCategories(const std::string& groupName) {
	std::filesystem::path dir = GroupDirectory(groupName);
	std::error_code ec;
	if (!std::filesystem::exists(dir, ec)) {
		return;
	}
	for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (!entry.is_regular_file() || entry.path().extension().string() != ".json") {
			continue;
		}
		LoadCategory(groupName, entry.path().stem().string());
	}
}

//=============================================================================
// jsonをノードに再帰的に読み込む
// 登録済みアイテムの型に合わせて値を復元する。読んだ値は「保存した値」にもなる
//=============================================================================
void GlobalVariables::JsonToNode(const json& j, GVNode& node) {
	for (GVNode::Entry& entry : node.entries_) {
		const auto it = j.find(entry.name);
		if (it == j.end()) {
			continue; // ファイルに無い項目は、今の値のまま
		}
		if (Item* item = std::get_if<Item>(&entry.value)) {
			// 型に合わせて復元
			if (JsonToItem(*it, *item)) {
				entry.saved = *item;
			}
		} else if (it->is_object()) {
			// 子カテゴリ
			JsonToNode(*it, *std::get<std::shared_ptr<GVNode>>(entry.value));
		}
	}
}

//=============================================================================
// 全ファイル読み込み
// 登録済みの全グループを Parameters/ から読み直す（まだ登録していないグループは、登録したときに読まれる）
//=============================================================================
void GlobalVariables::LoadFiles() {
	for (const GroupData& group : groups_) {
		LoadGroupCategories(group.name);
	}
}