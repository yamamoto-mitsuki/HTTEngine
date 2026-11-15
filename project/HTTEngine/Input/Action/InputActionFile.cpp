#include "InputActionFile.h"

#include <fstream>
#include <iomanip>
#include <string>
#include <unordered_set>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json; // 書いた順に項目を並べる json（name を先頭にして、読みやすく）

constexpr int kFileVersion = 1; // ファイルの形の版（形を変えたら上げる）

//=============================================================================
// 読む
//=============================================================================
// ===== 文字の項目（無い・文字でなければ fallback）=====
std::string ReadString(const json& object, const char* key, const std::string& fallback = {}) {
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

// ===== bool の項目（無ければ false）=====
bool ReadBool(const json& object, const char* key) {
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() && it->get<bool>();
}

// ===== 割り当てられる入力（"accepts": ["Key", "PadButton"]。無ければ None＝種類で決める）=====
InputDeviceMask ReadAccepts(const json& object, const std::string& actionName) {
	InputDeviceMask mask = InputDeviceMask::None;
	const auto it = object.find("accepts");
	if (it == object.end() || !it->is_array()) {
		return mask;
	}
	for (const json& item : *it) {
		const std::optional<InputDevice> device = item.is_string() ? magic_enum::enum_cast<InputDevice>(item.get<std::string>()) : std::nullopt;
		if (!device || *device == InputDevice::None) {
			LogManager::Warning("[InputActionFile] 知らない入力の種類を飛ばしました: " + actionName + " " + item.dump());
			continue;
		}
		mask = mask | static_cast<InputDeviceMask>(1 << static_cast<uint8_t>(*device));
	}
	return mask;
}

// ===== 割り当ての一覧（読めない物だけ飛ばす）=====
std::vector<InputBinding> ReadBindings(const json& object, const std::string& actionName) {
	std::vector<InputBinding> bindings;
	const auto it = object.find("bindings");
	if (it == object.end() || !it->is_array()) {
		return bindings;
	}
	for (const json& text : *it) {
		const std::optional<InputBinding> binding = text.is_string() ? InputBindingText::Parse(text.get<std::string>()) : std::nullopt;
		if (!binding) {
			LogManager::Warning("[InputActionFile] 読めない割り当てを飛ばしました: " + actionName + " " + text.dump());
			continue;
		}
		bindings.push_back(*binding);
	}
	return bindings;
}

// ===== Action 1つ（名前が無ければ nullopt）=====
std::optional<InputActionDesc> ReadAction(const json& object) {
	if (!object.is_object()) {
		return std::nullopt;
	}
	InputActionDesc desc;
	desc.name = ReadString(object, "name");
	if (desc.name.empty()) {
		return std::nullopt;
	}
	desc.label = ReadString(object, "label", desc.name);
	desc.group = ReadString(object, "group");
	desc.context = ReadString(object, "context");
	desc.tooltip = ReadString(object, "tooltip");
	const std::string typeName = ReadString(object, "type", std::string(magic_enum::enum_name(InputActionType::Button)));
	const std::optional<InputActionType> type = magic_enum::enum_cast<InputActionType>(typeName);
	if (!type) {
		LogManager::Warning("[InputActionFile] 知らない種類なので Button にしました: " + desc.name + " " + typeName);
	}
	desc.type = type.value_or(InputActionType::Button);
	desc.accepts = ReadAccepts(object, desc.name);
	desc.clampLength = ReadBool(object, "clampLength");
	desc.isFixed = ReadBool(object, "fixed");
	desc.defaults = ReadBindings(object, desc.name);
	return desc;
}


//=============================================================================
// 書く
//=============================================================================
// ===== Action 1つ（既定の値の項目は書かない）=====
ordered_json WriteAction(const InputActionDesc& desc) {
	ordered_json object;
	object["name"] = desc.name;
	if (desc.label != desc.name) {
		object["label"] = desc.label;
	}
	if (!desc.group.empty()) {
		object["group"] = desc.group;
	}
	if (!desc.context.empty()) {
		object["context"] = desc.context;
	}
	if (!desc.tooltip.empty()) {
		object["tooltip"] = desc.tooltip;
	}
	if (desc.type != InputActionType::Button) {
		object["type"] = std::string(magic_enum::enum_name(desc.type));
	}
	if (desc.accepts != InputDeviceMask::None) {
		ordered_json accepts = ordered_json::array();
		for (const InputDevice device : magic_enum::enum_values<InputDevice>()) {
			if (device != InputDevice::None && Contains(desc.accepts, device)) {
				accepts.push_back(std::string(magic_enum::enum_name(device)));
			}
		}
		object["accepts"] = std::move(accepts);
	}
	if (desc.clampLength) {
		object["clampLength"] = true;
	}
	if (desc.isFixed) {
		object["fixed"] = true;
	}
	ordered_json bindings = ordered_json::array();
	for (const InputBinding& binding : desc.defaults) {
		bindings.push_back(InputBindingText::ToString(binding));
	}
	object["bindings"] = std::move(bindings);
	return object;
}
} // namespace


//=============================================================================
// 読む・書く
//=============================================================================
// ===== 読む =====
std::optional<std::vector<InputActionDesc>> InputActionFile::Load(const std::filesystem::path& path) {
	std::ifstream ifs(path);
	if (!ifs) {
		return std::nullopt; // まだ作っていない（ログは出さない。Action 0 個で動く）
	}
	// 例外を投げない版のパース
	const json root = json::parse(ifs, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error("[InputActionFile] Action のファイルを読めません: " + ToUtf8(path));
		return std::nullopt;
	}
	if (root.value("version", 0) != kFileVersion) {
		LogManager::Warning("[InputActionFile] Action のファイルの版が違います: " + ToUtf8(path));
	}

	std::vector<InputActionDesc> actions;
	std::unordered_set<std::string> names; // もう読んだ名前（重なりを飛ばす）
	const auto list = root.find("actions");
	if (list == root.end() || !list->is_array()) {
		return actions;
	}
	for (const json& object : *list) {
		std::optional<InputActionDesc> desc = ReadAction(object);
		if (!desc) {
			LogManager::Warning("[InputActionFile] 名前の無い Action を飛ばしました: " + object.dump());
			continue;
		}
		if (!names.insert(desc->name).second) {
			LogManager::Warning("[InputActionFile] 同じ名前の Action を飛ばしました: " + desc->name);
			continue;
		}
		actions.push_back(std::move(*desc));
	}
	return actions;
}

// ===== 書く =====
bool InputActionFile::Save(const std::filesystem::path& path, std::span<const InputActionDesc> actions) {
	ordered_json list = ordered_json::array();
	for (const InputActionDesc& desc : actions) {
		list.push_back(WriteAction(desc));
	}
	ordered_json root;
	root["version"] = kFileVersion;
	root["actions"] = std::move(list);

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream ofs(path);
	if (!ofs) {
		LogManager::Error("[InputActionFile] Action のファイルを書けません: " + ToUtf8(path));
		return false;
	}
	ofs << std::setw(4) << root << std::endl;
	LogManager::Log("[InputActionFile] 保存しました: " + ToUtf8(path));
	return true;
}
