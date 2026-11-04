#include "ComponentSerializer.h"

#include <cmath>
#include <format>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <string_view>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/String/ConvertString.h"

namespace {
//=============================================================================
// 登録表
//=============================================================================
struct Entry {
	std::string name; // ファイルに書く型の名前
	std::function<bool(Handle<Entity>, ComponentUI&)> save;  // 持っていれば書いてtrue
	std::function<void(Handle<Entity>, ComponentUI&)> load;  // 読んでその場で付ける
	std::function<bool(Handle<Entity>, ComponentUI&)> visit; // 持っていれば本物に見せ方の関数を呼んで true
	ComponentSerializer::CaptureFunction capture; // 本物の値を写して、戻す関数を返す（空なら JSON で写す）
	ComponentSerializer::RemapFunction remap;     // JSON の中の Entity への参照を付け替える（空なら付け替えない）
};

// 名前順に並べて持つ。関数の中のstaticなので、ゲームのCOMPONENT（mainより前に作られる）との初期化の順番に左右されない
std::vector<Entry>& Entries() {
	static std::vector<Entry> entries;
	return entries;
}

// ファイルに書く順番。Transform を先頭に（Inspectorと同じ）、あとは名前順
// 登録した順番（＝リンクの順番）に左右されないので、ビルドし直してもファイルの並びが変わらない
bool IsBefore(std::string_view a, std::string_view b) {
	const bool aIsTransform = (a == "Transform");
	const bool bIsTransform = (b == "Transform");
	if (aIsTransform != bIsTransform) {
		return aIsTransform;
	}
	return a < b;
}

const Entry* FindEntry(std::string_view name) {
	for (const Entry& entry : Entries()) {
		if (entry.name == name) {
			return &entry;
		}
	}
	return nullptr;
}

// "新しい表示名###前の名前" と書けば、前に保存したファイルもそのまま読める
std::string_view KeyViewOf(const char* label) {
	const std::string_view text(label);
	const size_t mark = text.find("###");
	return (mark == std::string_view::npos) ? text : text.substr(mark + 3);
}

std::string KeyOf(const char* label) { return std::string(KeyViewOf(label)); }

//=============================================================================
// 書く係（見せ方の関数に渡すと、ui.Field の1行ごとにJSONの項目を1つ書く）
//=============================================================================
class JsonWriterUI final : public ComponentUI {
public:
	JsonWriterUI(SceneJson& out, std::string_view typeName, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr)
	    : out_(out), typeName_(typeName), warnings_(warnings), idMap_(idMap) {}

	void Field(const char* label, float& value, FieldStyle) override { Put(label, value); }
	void Field(const char* label, int& value, FieldStyle) override { Put(label, value); }
	void Field(const char* label, bool& value, FieldStyle) override { Put(label, value); }
	void Field(const char* label, Vector2& value, FieldStyle) override { Put(label, SceneJson::array({value.x, value.y})); }
	void Field(const char* label, Vector3& value, FieldStyle) override { Put(label, SceneJson::array({value.x, value.y, value.z})); }
	void Field(const char* label, Vector4& value, FieldStyle) override { Put(label, SceneJson::array({value.x, value.y, value.z, value.w})); }

	// 相手の EntityId を書く。相手がもう居なければ0（次に起動したとき、同じ番号が別のEntityに配られることがあるので）
	void Field(const char* label, EntityRef& value, FieldStyle) override {
		const bool isAlive = EntityManager::Find(value).IsValid();
		EntityId id = isAlive ? value.id : EntityId{0};
		// Prefab に書くときは、表にある番号（Prefab の中の相手）を、ファイルの番号に直す。表に無い相手はそのまま（作るときに外れる）
		if (idMap_ != nullptr) {
			if (const auto found = idMap_->find(id); found != idMap_->end()) {
				id = found->second;
			}
		}
		Put(label, id);
	}

	void Field(const char* label, std::string& value, FieldStyle) override { Put(label, value); }

	// アセットはパスで書く（番号は読み込んだ順で決まるので、次に起動したときは別の物を指す）
	void AssetField(const char* label, uint32_t& handle, AssetType type, FieldStyle) override {
		const std::string& path = GetAssetPath(type, handle);
		if (handle != 0 && path.empty()) {
			warnings_.insert(std::format("{}: \"{}\" はファイルから読んだ物ではないので保存できません（未選択として書きます）", typeName_, KeyOf(label)));
		}
		Put(label, path);
	}

	void ColorField(const char* label, Vector3& rgb) override { Field(label, rgb, {}); }
	void ColorField(const char* label, Vector4& rgba) override { Field(label, rgba, {}); }

	// 飾りは保存しない
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	// enumは名前で書く（途中に選択肢を足しても、並べ替えても読めるように）
	void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle) override {
		if (index < names.size()) {
			Put(label, std::string(names[index]));
		}
	}

private:
	template<class Value> void Put(const char* label, Value&& value) {
		const std::string key = KeyOf(label);
		if (out_.contains(key)) {
			warnings_.insert(std::format("{}: 項目の名前 \"{}\" が2つあります。後の方で上書きされます（\"表示名###別の名前\" で分けられます）", typeName_, key));
		}
		out_[key] = std::forward<Value>(value);
	}

	SceneJson& out_;
	std::string_view typeName_;       // 警告に出す型の名前
	std::set<std::string>& warnings_; // 警告（同じ文は1回だけ出すように、集めてから出す）
	const EntityIdMap* idMap_;        // EntityId の付け替え表（Prefab に書くとき。シーンに書くときは nullptr）
};

//=============================================================================
// 読む係（ui.Field の1行ごとに、同じ名前の項目を探して値を入れる。無ければ初期値のまま）
//=============================================================================
class JsonReaderUI final : public ComponentUI {
public:
	JsonReaderUI(const SceneJson& in, std::string_view typeName, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr)
	    : in_(in), typeName_(typeName), warnings_(warnings), idMap_(idMap) {}

	void Field(const char* label, float& value, FieldStyle) override {
		if (const SceneJson* item = Find(label, &SceneJson::is_number)) {
			value = item->get<float>();
		}
	}
	void Field(const char* label, int& value, FieldStyle) override {
		if (const SceneJson* item = Find(label, &SceneJson::is_number_integer)) {
			value = item->get<int>();
		}
	}
	void Field(const char* label, bool& value, FieldStyle) override {
		if (const SceneJson* item = Find(label, &SceneJson::is_boolean)) {
			value = item->get<bool>();
		}
	}
	void Field(const char* label, Vector2& value, FieldStyle) override { ReadFloats(label, &value.x, 2); }
	void Field(const char* label, Vector3& value, FieldStyle) override { ReadFloats(label, &value.x, 3); }
	void Field(const char* label, Vector4& value, FieldStyle) override { ReadFloats(label, &value.x, 4); }

	// 相手の EntityId を読む（読み込みは「全Entityを作ってからComponent」の順なので、相手が居るならもう作られている）
	void Field(const char* label, EntityRef& value, FieldStyle) override {
		const SceneJson* item = Find(label, &SceneJson::is_number_unsigned);
		if (item == nullptr) {
			return;
		}
		EntityId id = item->get<EntityId>();
		// Prefab から作るときは、ファイルの番号を、作った Entity の番号に付け替える（表に無い＝Prefab の外の Entity は指せない）
		if (idMap_ != nullptr && id != 0) {
			const auto found = idMap_->find(id);
			if (found == idMap_->end()) {
				warnings_.insert(std::format("{}: \"{}\" の相手（EntityId {}）は Prefab の外の Entity なので外しました", typeName_, KeyOf(label), id));
				value = {};
				return;
			}
			id = found->second;
		}
		if (id != 0 && !EntityManager::FindById(id).IsValid()) {
			warnings_.insert(std::format("{}: \"{}\" の相手（EntityId {}）がシーンに無いので外しました", typeName_, KeyOf(label), id));
			value = {};
			return;
		}
		value.id = id;
	}

	void Field(const char* label, std::string& value, FieldStyle) override {
		if (const SceneJson* item = Find(label, &SceneJson::is_string)) {
			value = item->get<std::string>();
		}
	}

	void AssetField(const char* label, uint32_t& handle, AssetType type, FieldStyle) override {
		const SceneJson* item = Find(label, &SceneJson::is_string);
		if (item == nullptr) {
			return;
		}
		// LoadAsset も見つからなければ知らせてくれるが、同じアセットを何個ものEntityが指していると同じ文が何度も出る。
		// ここで先に確かめて warnings_（同じ文は1回だけ）に入れることで、1回にまとめる
		const std::string& path = item->get_ref<const std::string&>();
		std::error_code error;
		if (!path.empty() && !std::filesystem::exists(ToPath(path), error)) {
			warnings_.insert(std::format("{}: \"{}\" のアセットが見つかりません（未選択にします）: {}", typeName_, KeyOf(label), path));
			handle = 0;
			return;
		}
		handle = LoadAsset(type, path); // 空なら0（未選択）
	}

	void ColorField(const char* label, Vector3& rgb) override { ReadFloats(label, &rgb.x, 3); }
	void ColorField(const char* label, Vector4& rgba) override { ReadFloats(label, &rgba.x, 4); }

	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

	// ファイルにあったのに誰も読まなかった項目を知らせる（ラベルを変えた・項目を消したときに気づけるように）
	void WarnUnused() const {
		for (auto it = in_.begin(); it != in_.end(); ++it) {
			if (std::find(used_.begin(), used_.end(), it.key()) == used_.end()) {
				warnings_.insert(
				    std::format(
				        "{}: ファイルの項目 \"{}\" を読む所がありません（ラベルを変えたなら \"新しい表示名###{}\" と書くと読めます。このまま保存すると消えます）", typeName_, it.key(), it.key()));
			}
		}
	}

protected:
	void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle) override {
		const SceneJson* item = Find(label, &SceneJson::is_string);
		if (item == nullptr) {
			return;
		}
		const std::string& name = item->get_ref<const std::string&>();
		const auto found = std::find(names.begin(), names.end(), name);
		if (found == names.end()) {
			warnings_.insert(std::format("{}: \"{}\" = \"{}\" は知らない選択肢です（初期値のままにします）", typeName_, KeyOf(label), name));
			return;
		}
		index = static_cast<size_t>(found - names.begin());
	}

private:
	using TypeCheck = bool (SceneJson::*)() const noexcept; // is_number などの「形を調べる関数」

	// 項目を探す。無ければnullptr（後から足した項目は、古いファイルには無い＝初期値のまま）。形が違えば警告してnullptr
	const SceneJson* Find(const char* label, TypeCheck isExpected) {
		const std::string key = KeyOf(label);
		const auto it = in_.find(key); // const の json に [] で無いキーを引くと止まるので、必ず find を使う
		if (it == in_.end()) {
			return nullptr;
		}
		used_.push_back(key);
		if (!((*it).*isExpected)()) {
			warnings_.insert(std::format("{}: \"{}\" の値の形が違うので、初期値のままにします", typeName_, key));
			return nullptr;
		}
		return &*it;
	}

	// [x, y, z] のような数の並びを読む。数が合わなければ警告して読まない
	void ReadFloats(const char* label, float* out, size_t count) {
		const SceneJson* item = Find(label, &SceneJson::is_array);
		if (item == nullptr) {
			return;
		}
		const bool allNumbers = std::all_of(item->begin(), item->end(), [](const SceneJson& element) { return element.is_number(); });
		if (item->size() != count || !allNumbers) {
			warnings_.insert(std::format("{}: \"{}\" は数が{}個の配列にしてください（初期値のままにします）", typeName_, KeyOf(label), count));
			return;
		}
		for (size_t i = 0; i < count; ++i) {
			out[i] = (*item)[i].get<float>();
		}
	}

	const EntityIdMap* idMap_; // EntityId の付け替え表（Prefab から作るとき。シーンを読むときは nullptr）
	const SceneJson& in_;
	std::string_view typeName_;       // 警告に出す型の名前
	std::set<std::string>& warnings_; // 警告
	std::vector<std::string> used_;   // 読んだ項目の名前
};

//=============================================================================
// 参照を付け替える係（ui.Field の Entity への参照の行だけ、JSON の番号を表で書き換える）
//=============================================================================
// ほかの項目は JSON から値を読むだけ（見せ方の関数が「この bool が true なら次の項目を出す」のように分けていても、ファイルと同じ行を通るように）
// アセットは読まない（ファイルを開くと重い。参照の行の通り方は変わらない）。Entity が居るかも見ない（まだ作っていない番号を扱うので）
class RefRemapUI final : public ComponentUI {
public:
	RefRemapUI(SceneJson& fields, const EntityIdMap& map) : fields_(fields), map_(map) {}

	void Field(const char* label, float& value, FieldStyle) override {
		if (const SceneJson* item = Find(label); item != nullptr && item->is_number()) {
			value = item->get<float>();
		}
	}
	void Field(const char* label, int& value, FieldStyle) override {
		if (const SceneJson* item = Find(label); item != nullptr && item->is_number_integer()) {
			value = item->get<int>();
		}
	}
	void Field(const char* label, bool& value, FieldStyle) override {
		if (const SceneJson* item = Find(label); item != nullptr && item->is_boolean()) {
			value = item->get<bool>();
		}
	}
	// 数の並びは、行の分かれ方に使われないので読まない
	void Field(const char*, Vector2&, FieldStyle) override {}
	void Field(const char*, Vector3&, FieldStyle) override {}
	void Field(const char*, Vector4&, FieldStyle) override {}

	// 相手の番号を表で付け替えて、JSON に書き戻す（表に無い番号はそのまま）
	void Field(const char* label, EntityRef& value, FieldStyle) override {
		const auto it = fields_.find(KeyOf(label));
		if (it == fields_.end() || !it->is_number_unsigned()) {
			return;
		}
		const EntityId id = it->get<EntityId>();
		const auto found = map_.find(id);
		value.id = (found != map_.end()) ? found->second : id;
		it.value() = value.id;
	}

	void Field(const char* label, std::string& value, FieldStyle) override {
		if (const SceneJson* item = Find(label); item != nullptr && item->is_string()) {
			value = item->get<std::string>();
		}
	}

	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	void ColorField(const char*, Vector3&) override {}
	void ColorField(const char*, Vector4&) override {}
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle) override {
		const SceneJson* item = Find(label);
		if (item == nullptr || !item->is_string()) {
			return;
		}
		const auto found = std::find(names.begin(), names.end(), item->get_ref<const std::string&>());
		if (found != names.end()) {
			index = static_cast<size_t>(found - names.begin());
		}
	}

private:
	// 項目を探す（無ければ nullptr）
	const SceneJson* Find(const char* label) const {
		const auto it = fields_.find(KeyOf(label));
		return (it != fields_.end()) ? &*it : nullptr;
	}

	SceneJson& fields_;       // 書き換える JSON（1つの型の項目）
	const EntityIdMap& map_;  // 番号の付け替え表
};

//=============================================================================
// 値を書く係（見せ方の関数に渡すと、名前の合う項目にだけ値を書く）
//=============================================================================
class ValueWriterUI final : public ComponentUI {
public:
	ValueWriterUI(std::string_view key, const ComponentValue& value) : key_(key), value_(value) {}

	bool IsWritten() const { return isWritten_; }

	void Field(const char* label, float& value, FieldStyle) override { WriteFloats(label, &value, 1); }
	void Field(const char* label, int& value, FieldStyle) override {
		if (Match(label) && value_.isUsed[0]) {
			value = static_cast<int>(std::lround(value_.channels[0]));
		}
	}
	void Field(const char* label, bool& value, FieldStyle) override {
		if (Match(label) && value_.isUsed[0]) {
			value = value_.channels[0] >= 0.5f;
		}
	}
	void Field(const char* label, Vector2& value, FieldStyle) override { WriteFloats(label, &value.x, 2); }
	void Field(const char* label, Vector3& value, FieldStyle) override { WriteFloats(label, &value.x, 3); }
	void Field(const char* label, Vector4& value, FieldStyle) override { WriteFloats(label, &value.x, 4); }
	void ColorField(const char* label, Vector3& rgb) override { WriteFloats(label, &rgb.x, 3); }
	void ColorField(const char* label, Vector4& rgba) override { WriteFloats(label, &rgba.x, 4); }

	// 数で表せない項目は書かない（isWritten_ が false のままなので、NoField になる）
	void Field(const char*, EntityRef&, FieldStyle) override {}
	void Field(const char*, std::string&, FieldStyle) override {}
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	// 名前の一覧の番号・ビットの並びは、数でも途中の値に意味が無い（Timeline でなめらかに動かす物ではない）ので書かない
	void NameField(const char*, int&, const NameList&, FieldStyle) override {}
	void NameMaskField(const char*, uint32_t&, const NameList&, FieldStyle) override {}
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	void EnumField(const char*, size_t&, std::span<const std::string_view>, FieldStyle) override {}

private:
	// 名前が合えば、書いたことにする
	bool Match(const char* label) {
		if (KeyViewOf(label) != key_) {
			return false;
		}
		isWritten_ = true;
		return true;
	}

	// 使う成分だけ書く（x, y, z, w は並んでいるので、先頭のポインタから順に書ける）
	void WriteFloats(const char* label, float* out, size_t count) {
		if (!Match(label)) {
			return;
		}
		for (size_t i = 0; i < count; ++i) {
			if (value_.isUsed[i]) {
				out[i] = value_.channels[i];
			}
		}
	}

	std::string_view key_;        // 書く項目の名前
	const ComponentValue& value_; // 書く値
	bool isWritten_ = false;      // 書いた
};

//=============================================================================
// 値を読む係（見せ方の関数に渡すと、数で表せる項目を全部集める）
//=============================================================================
class ValueListUI final : public ComponentUI {
public:
	/// <summary>
	/// 集めた項目1つ
	/// </summary>
	struct Item {
		ComponentValueField field; // 名前と形
		ComponentValue value;      // 今の値
	};

	const std::vector<Item>& GetItems() const { return items_; }

	void Field(const char* label, float& value, FieldStyle) override { Add(label, &value, 1, false); }
	void Field(const char* label, int& value, FieldStyle) override {
		const float number = static_cast<float>(value);
		Add(label, &number, 1, false);
	}
	void Field(const char* label, bool& value, FieldStyle) override {
		const float number = value ? 1.0f : 0.0f;
		Add(label, &number, 1, false);
	}
	void Field(const char* label, Vector2& value, FieldStyle) override { Add(label, &value.x, 2, false); }
	void Field(const char* label, Vector3& value, FieldStyle) override { Add(label, &value.x, 3, false); }
	void Field(const char* label, Vector4& value, FieldStyle) override { Add(label, &value.x, 4, false); }
	void ColorField(const char* label, Vector3& rgb) override { Add(label, &rgb.x, 3, true); }
	void ColorField(const char* label, Vector4& rgba) override { Add(label, &rgba.x, 4, true); }

	// 数で表せない項目は集めない（ValueWriterUI が書けない物と同じ）
	void Field(const char*, EntityRef&, FieldStyle) override {}
	void Field(const char*, std::string&, FieldStyle) override {}
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	void NameField(const char*, int&, const NameList&, FieldStyle) override {}
	void NameMaskField(const char*, uint32_t&, const NameList&, FieldStyle) override {}
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	void EnumField(const char*, size_t&, std::span<const std::string_view>, FieldStyle) override {}

private:
	void Add(const char* label, const float* values, size_t count, bool isColor) {
		const std::string_view text(label);
		Item item;
		item.field.key = KeyOf(label);
		item.field.label = std::string(text.substr(0, text.find("###")));
		item.field.channelCount = count;
		item.field.isColor = isColor;
		for (size_t i = 0; i < count; ++i) {
			item.value.channels[i] = values[i];
			item.value.isUsed[i] = true;
		}
		items_.push_back(std::move(item));
	}

	std::vector<Item> items_; // 集めた項目（見せ方の関数の順）
};

//=============================================================================
// 値を一時的に書く（Timeline のプレビュー）
//=============================================================================
/// <summary>
/// 写した Component 1つ
/// </summary>
struct CapturedComponent {
	Handle<Entity> entity;
	const Entry* entry = nullptr;
	ComponentSerializer::RestoreFunction restore; // 写した値へ戻す
};

std::vector<CapturedComponent> captured; // 写した Component（同じ物は1回だけ）
bool isCapturing = false;                // SetValue の前に写すか

// ===== JSON で写す（型が消えている Script の分）=====
ComponentSerializer::RestoreFunction CaptureAsJson(const Entry& entry, Handle<Entity> entity) {
	std::set<std::string> warnings;
	SceneJson fields = SceneJson::object();
	JsonWriterUI writer(fields, entry.name, warnings);
	if (!entry.save(entity, writer)) {
		return {}; // 持っていない
	}
	return [&entry, entity, fields = std::move(fields)] {
		std::set<std::string> ignored;
		JsonReaderUI reader(fields, entry.name, ignored);
		entry.visit(entity, reader);
	};
}

// ===== まだ写していなければ写す =====
void CaptureOnce(const Entry& entry, Handle<Entity> entity) {
	const bool isCaptured = std::ranges::any_of(captured, [&](const CapturedComponent& item) { return item.entity == entity && item.entry == &entry; });
	if (isCaptured) {
		return;
	}
	ComponentSerializer::RestoreFunction restore = entry.capture ? entry.capture(entity) : CaptureAsJson(entry, entity);
	if (restore) {
		captured.push_back({entity, &entry, std::move(restore)});
	}
}

// ===== Component の項目を全部集める（持っていなければ空）=====
std::vector<ValueListUI::Item> ListValues(Handle<Entity> entity, std::string_view typeName) {
	ValueListUI list;
	const Entry* entry = FindEntry(typeName);
	if (entry != nullptr) {
		entry->save(entity, list); // save は写しに呼ぶので、本物を変えない
	}
	return list.GetItems();
}
} // namespace


//=============================================================================
// 登録
//=============================================================================
void ComponentSerializer::RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit, CaptureFunction capture, RemapFunction remap) {
	std::vector<Entry>& entries = Entries();
	// 決まった順番の場所に入れる（IsBefore）
	const auto position = std::lower_bound(entries.begin(), entries.end(), std::string_view(name), [](const Entry& entry, std::string_view key) { return IsBefore(entry.name, key); });
	if (position != entries.end() && position->name == name) {
		LogManager::Error(std::format("Componentの名前 \"{}\" が2つ登録されました。後の方は保存されません", name));
		return;
	}
	entries.insert(position, Entry{name, std::move(save), std::move(load), std::move(visit), std::move(capture), std::move(remap)});
}

//=============================================================================
// 書く・読む
//=============================================================================
void ComponentSerializer::Save(Handle<Entity> entity, SceneJson& out, std::set<std::string>& warnings, const EntityIdMap* idMap) {
	for (const Entry& entry : Entries()) {
		// 中身は手元で作ってから入れる（ordered_map は中身が vector なので、out[...] の参照を持ったまま別の項目を足すと参照が壊れる）
		SceneJson fields = SceneJson::object();
		JsonWriterUI writer(fields, entry.name, warnings, idMap);
		if (entry.save(entity, writer)) {
			out[entry.name] = std::move(fields);
		}
	}
}

void ComponentSerializer::Load(Handle<Entity> entity, const SceneJson& in, std::set<std::string>& warnings, const EntityIdMap* idMap) {
	for (auto it = in.begin(); it != in.end(); ++it) {
		const Entry* entry = FindEntry(it.key());
		if (entry == nullptr) {
			warnings.insert(std::format("知らないComponent \"{}\" を飛ばしました（名前を変えた・消した型は、このまま保存すると消えます）", it.key()));
			continue;
		}
		if (!it->is_object()) {
			warnings.insert(std::format("\"{}\" の中身が {{ }} の形ではないので飛ばしました", it.key()));
			continue;
		}
		JsonReaderUI reader(*it, entry->name, warnings, idMap);
		entry->load(entity, reader);
		reader.WarnUnused();
	}
}

void ComponentSerializer::RemapRefs(SceneJson& components, const EntityIdMap& map) {
	if (!components.is_object() || map.empty()) {
		return;
	}
	for (auto it = components.begin(); it != components.end(); ++it) {
		const Entry* entry = FindEntry(it.key());
		// 知らない型・付け替え方の無い型はそのまま（知らない型は、読むときに警告が出る）
		if (entry != nullptr && entry->remap && it->is_object()) {
			entry->remap(it.value(), map);
		}
	}
}

void ComponentSerializer::RemapFields(const std::function<void(ComponentUI&)>& describe, SceneJson& fields, const EntityIdMap& map) {
	if (!fields.is_object() || map.empty()) {
		return;
	}
	RefRemapUI remapper(fields, map);
	describe(remapper);
}


//=============================================================================
// 値を書く（Timeline）
//=============================================================================
ComponentValueResult ComponentSerializer::SetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, const ComponentValue& value) {
	const Entry* entry = FindEntry(typeName);
	if (entry != nullptr && isCapturing) {
		CaptureOnce(*entry, entity);
	}
	ValueWriterUI writer(key, value);
	if (entry == nullptr || !entry->visit(entity, writer)) {
		return ComponentValueResult::NoComponent;
	}
	return writer.IsWritten() ? ComponentValueResult::Written : ComponentValueResult::NoField;
}

ComponentValueResult ComponentSerializer::GetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, ComponentValue& out) {
	const Entry* entry = FindEntry(typeName);
	ValueListUI list;
	if (entry == nullptr || !entry->save(entity, list)) {
		return ComponentValueResult::NoComponent;
	}
	const std::vector<ValueListUI::Item>& items = list.GetItems();
	const auto found = std::ranges::find_if(items, [key](const ValueListUI::Item& item) { return item.field.key == key; });
	if (found == items.end()) {
		return ComponentValueResult::NoField;
	}
	out = found->value;
	return ComponentValueResult::Written;
}

std::vector<std::string> ComponentSerializer::GetComponentNames(Handle<Entity> entity) {
	std::vector<std::string> names;
	for (const Entry& entry : Entries()) {
		ValueListUI list; // 持っているかを調べるだけ（読んだ値は使わない）
		if (entry.save(entity, list)) {
			names.push_back(entry.name);
		}
	}
	return names;
}

std::vector<ComponentValueField> ComponentSerializer::GetValueFields(Handle<Entity> entity, std::string_view typeName) {
	std::vector<ComponentValueField> fields;
	for (const ValueListUI::Item& item : ListValues(entity, typeName)) {
		fields.push_back(item.field);
	}
	return fields;
}

void ComponentSerializer::BeginValueCapture() { isCapturing = true; }

void ComponentSerializer::RestoreCapturedValues() {
	isCapturing = false;
	for (const CapturedComponent& item : captured) {
		item.restore();
	}
	captured.clear();
}


//=============================================================================
// 見せ方の関数1つ分を書く・読む（Script の写し）
//=============================================================================
void ComponentSerializer::WriteFields(std::string_view typeName, const std::function<void(ComponentUI&)>& describe, SceneJson& out, std::set<std::string>& warnings) {
	out = SceneJson::object();
	JsonWriterUI writer(out, typeName, warnings);
	describe(writer);
}

void ComponentSerializer::ReadFields(std::string_view typeName, const std::function<void(ComponentUI&)>& describe, const SceneJson& in, std::set<std::string>& warnings) {
	if (!in.is_object()) {
		warnings.insert(std::format("\"{}\" の中身が {{ }} の形ではないので飛ばしました", typeName));
		return;
	}
	JsonReaderUI reader(in, typeName, warnings);
	describe(reader);
	reader.WarnUnused();
}