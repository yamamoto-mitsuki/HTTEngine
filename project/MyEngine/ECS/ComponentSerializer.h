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
/// 値を読み書きできる項目1つ（GetValueFields）
/// </summary>
struct ComponentValueField {
	std::string key;         // 保存の名前（SetValue・GetValue に渡す）
	std::string label;       // 表示名
	size_t channelCount = 1; // 成分の数（float は 1、Vector3 は 3）
	bool isColor = false;    // 色（成分は r, g, b, a の順）
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
		    },
		    // --- 写す：本物の値を型のまま写して、戻す関数を返す（プレビュー。Transform の回転はクォータニオンなので、見せ方の関数を通すとずれる）---
		    [](Handle<Entity> entity) -> RestoreFunction {
			    const T* component = EntityManager::Get<T>(entity);
			    if (component == nullptr) {
				    return {}; // 持っていない
			    }
			    return [entity, copy = *component] {
				    if (T* now = EntityManager::Get<T>(entity)) {
					    *now = copy;
				    }
			    };
		    });
	}

	// Entityが持っているComponentを全部 out に書く（{"型の名前": {"項目": 値, ...}, ...}）。警告は warnings に集める（同じ文を何度も出さないため）
	// idMap を渡すと、Component の中の Entity への参照を表で付け替えて書く（Prefab に書くとき。表に無い番号はそのまま）
	static void Save(Handle<Entity> entity, SceneJson& out, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr);
	// idMap を渡すと、Component の中の Entity への参照（EntityRef・GameObject）を表で付け替える（Prefab から作るとき。表に無い番号は外す）
	static void Load(Handle<Entity> entity, const SceneJson& in, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr);

	/// <summary>
	/// Component の1つの項目へ値を書く（Timeline）。見せ方の関数を本物に呼ぶことで、Inspector に出せる項目なら Component の種類を問わず書ける
	/// <para>書けるのは float・int（四捨五入）・bool（0.5 以上で true）・Vector2〜4・色。Transform の rotation は度で書く</para>
	/// </summary>
	/// <param name="typeName">保存の名前（Inspector の見出しと同じ。"Image"・Script の名前）</param>
	/// <param name="key">項目の保存の名前（"表示名###名前" の名前）</param>
	static ComponentValueResult SetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, const ComponentValue& value);

	/// <summary>
	/// Component の1つの項目の今の値を読む（SetValue の逆。使う成分の isUsed が true になる）
	/// </summary>
	static ComponentValueResult GetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, ComponentValue& out);
	// Entity が持っている Component の名前（保存の順）
	static std::vector<std::string> GetComponentNames(Handle<Entity> entity);
	// Component の中の、値を読み書きできる項目の一覧（持っていなければ空）
	static std::vector<ComponentValueField> GetValueFields(Handle<Entity> entity, std::string_view typeName);

	// ===== 値を一時的に書く（Timeline のプレビュー）=====
	// これより後の SetValue は、書く前にその Component を丸ごと写す
	static void BeginValueCapture();
	// 写した Component を全部元へ戻して、写すのをやめる
	static void RestoreCapturedValues();

	// エンジンのComponent（Transform・ModelRenderer・ライト）を登録する。定義は EngineComponents.cpp
	static void RegisterEngineComponents();

	// ===== EntityManager の置き場所を使わない物（Script）の登録 =====
	using SaveFunction = std::function<bool(Handle<Entity>, ComponentUI&)>; // 持っていなければfalse
	using LoadFunction = std::function<void(Handle<Entity>, ComponentUI&)>;
	using RestoreFunction = std::function<void()>; // 写した値へ戻す関数
	using CaptureFunction = std::function<RestoreFunction(Handle<Entity>)>; // 持っていなければ空の関数を返す

	// visit は本物に見せ方の関数を呼ぶ（持っていなければ false。save と違い、写しに呼んではいけない）
	// capture は値を写す関数（空なら、JSON に書いて読み戻す）
	static void RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit, CaptureFunction capture = {});

	// ===== 見せ方の関数1つ分を JSON に書く・読む（Script の写し。Undo・コピーで使う）=====
	// typeName は警告に出す名前。読むときは、ファイルに無い項目は今の値のまま
	static void WriteFields(std::string_view typeName, const std::function<void(ComponentUI&)>& describe, SceneJson& out, std::set<std::string>& warnings);
	static void ReadFields(std::string_view typeName, const std::function<void(ComponentUI&)>& describe, const SceneJson& in, std::set<std::string>& warnings);
};