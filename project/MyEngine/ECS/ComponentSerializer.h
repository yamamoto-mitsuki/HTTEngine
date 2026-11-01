#pragma once
#include <set>
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

#include <externals/nlohmann/json_fwd.hpp>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"

// シーンファイルに使うJSONの型
// ・キーは書いた順に並ぶ（ordered_map。ui.Field の順番のままファイルに出る）
// ・小数は float のまま書く（普通の nlohmann::json は double なので、0.1f が 0.10000000149011612 と書かれてしまう）
using SceneJson = nlohmann::basic_json<nlohmann::ordered_map, std::vector, std::string, bool, std::int64_t, std::uint64_t, float>;
// EntityId の付け替え表（ファイルに書いてある番号 → 作った Entity の番号）。Prefab から作るとき、Component の中の Entity への参照を付け替えるのに使う
using EntityIdMap = std::unordered_map<EntityId, EntityId>;

/// <summary>
/// Component の1つの項目へ書く値（SetValue）。1つの float なら x、色なら r, g, b, a の順
/// </summary>
struct ComponentValue {
	static constexpr size_t kChannelCount = 4; // 成分の数の上限
	std::array<float, kChannelCount> channels{}; // [x, y, z, w] 書く値
	std::array<bool, kChannelCount> isUsed{};    // [x, y, z, w] false の成分は今の値のまま
};

/// <summary>
/// SetValue の結果
/// </summary>
enum class ComponentValueResult {
	Written,     // 書いた
	NoComponent, // その Entity に Component が無い（名前が違う）
	NoField,     // 項目が無い。または書けない形（文字・Entity・アセット・enum）
};


/// <summary>
/// Componentの型ごとの「保存する項目」の表。型の名前（ファイルに書く名前）と、見せ方の関数（COMPONENTの中身と同じ形）を持つ
/// <para>見せ方の関数に「書く係」「読む係」の ComponentUI を渡すと、ui.Field の並びがそのままJSONの項目になる</para>
/// <para>Editorの登録表（ComponentEditorRegistry）とは別物。Releaseでもシーンを読むのに要る</para>
/// </summary>
class ComponentSerializer {
public:
	/// <summary>
	/// 型を登録する。name はファイルに書く名前（変えると、前に保存したファイルのその型が読めなくなる）
	/// </summary>
	template<class T> static void Register(const char* name, void (*describe)(ComponentUI&, T&)) {
		RegisterErased(
		    name,
		    // --- 書く：持っていれば、写しに対して見せ方の関数を呼ぶ（関数は T& を受け取るので、本物を書き換えないように写す）---
		    [describe](Handle<Entity> entity, ComponentUI& writer) {
			    const T* component = EntityManager::Get<T>(entity);
			    if (component == nullptr) {
				    return false; // 持っていない
			    }
			    T copy = *component;
			    describe(writer, copy);
			    return true;
		    },
		    // --- 読む：初期値から始めて、ファイルにある項目だけ上書きし、予約せずその場で付ける（Transformのように持っていれば上書き）---
		    [describe](Handle<Entity> entity, ComponentUI& reader) {
			    T value{};
			    describe(reader, value);
			    EntityManager::RestoreComponents(entity, {ComponentSnapshot::Make(value)});
		    },
		    // --- 本物に呼ぶ：持っていれば、本物に対して見せ方の関数を呼ぶ（SetValue。渡された係が書き換える）---
		    [describe](Handle<Entity> entity, ComponentUI& ui) {
			    T* component = EntityManager::Get<T>(entity);
			    if (component == nullptr) {
				    return false; // 持っていない
			    }
			    describe(ui, *component);
			    return true;
		    });
	}

	// Entityが持っているComponentを全部 out に書く（{"型の名前": {"項目": 値, ...}, ...}）。警告は warnings に集める（同じ文を何度も出さないため）
	static void Save(Handle<Entity> entity, SceneJson& out, std::set<std::string>& warnings);
	// idMap を渡すと、Component の中の Entity への参照（EntityRef・GameObject）を表で付け替える（Prefab から作るとき。表に無い番号は外す）
	static void Load(Handle<Entity> entity, const SceneJson& in, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr);

	/// <summary>
	/// Component の1つの項目へ値を書く（Timeline）。見せ方の関数を本物に呼ぶので、Inspector に出せる項目なら Component の種類を問わず書ける
	/// <para>書けるのは float・int（四捨五入）・bool（0.5 以上で true）・Vector2〜4・色。Transform の rotation は度で書く</para>
	/// </summary>
	/// <param name="typeName">保存の名前（Inspector の見出しと同じ。"Image"・Script の名前）</param>
	/// <param name="key">項目の保存の名前（"表示名###名前" の名前）</param>
	static ComponentValueResult SetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, const ComponentValue& value);

	// エンジンのComponent（Transform・ModelRenderer・ライト）を登録する。定義は EngineComponents.cpp
	static void RegisterEngineComponents();

	// ===== EntityManager の置き場所を使わない物（Script）の登録 =====
	using SaveFunction = std::function<bool(Handle<Entity>, ComponentUI&)>; // 持っていなければfalse
	using LoadFunction = std::function<void(Handle<Entity>, ComponentUI&)>;

	// 型を消した形で表に入れる（名前順に並べる）。save・load の中で、渡された ComponentUI に見せ方の関数を呼ぶ
	// visit は本物に見せ方の関数を呼ぶ（持っていなければ false。save と違い、写しに呼んではいけない）
	static void RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit);

	// ===== 見せ方の関数1つ分を JSON に書く・読む（Script の写し。Undo・コピーで使う）=====
	// typeName は警告に出す名前。読むときは、ファイルに無い項目は今の値のまま
	static void WriteFields(std::string_view typeName, const std::function<void(ComponentUI&)>& describe, SceneJson& out, std::set<std::string>& warnings);
	static void ReadFields(std::string_view typeName, const std::function<void(ComponentUI&)>& describe, const SceneJson& in, std::set<std::string>& warnings);
};