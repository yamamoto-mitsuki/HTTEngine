#pragma once
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/ECS/EntityManager.h"

// Add Componentのメニューの分け方。"/" で区切ると、その下にさらにカテゴリを作れる（"Gameplay/Movement" など）
//
//   Core        Transformなど、全Entityが必ず持つもの（Add Componentには出ない）
//   Camera      カメラ（3D・2D のどちらでも使うので、Rendering3D には入れない）
//   Rendering3D 3Dモデル（ModelRenderer、将来はSkinnedModelRenderer）
//   Rendering2D 2D（将来のSpriteRenderer）
//   Lighting    ライト
//   Effects     パーティクルなど
//   Physics     当たり判定
//   Audio       音
//   Gameplay    ゲーム固有の物（COMPONENT(...) と SCRIPT(...) で書いた物は全部ここ。"Gameplay/サブカテゴリ" に分けられる）
//
// 上の段の並び順と説明は ComponentEditor.cpp の kTopLevelCategories が決める。そこに無い名前は後ろに回る


/// <summary>
/// Componentの種類ごとに1つ作る「エディタでの扱い方」（UnityのCustomEditorと同じ考え方）
/// <para>新しいComponentを作ったら、EntityManager::RegisterComponent&lt;T&gt;() で置き場所を作り、
/// TypedComponentEditorを継承したクラスを ComponentEditorRegistry::Register する</para>
/// </summary>
class ComponentEditor {
public:
	virtual ~ComponentEditor() = default;

	// ===== 種類の情報 =====
	virtual const char* GetName() const = 0;                      // Inspectorの見出し・Add Componentの表示名
	virtual const char* GetCategory() const = 0;                  // Add Componentでどのカテゴリに出すか（"Gameplay/Movement" のように "/" で入れ子）
	virtual bool IsOptional() const { return true; }              // Add・Removeの対象か（Transformのように必ず持つものはfalse）
	virtual const char* GetSourceFile() const { return nullptr; } // 書いてあるファイル（COMPONENT(...)・SCRIPT(...) の物だけ。分からなければnullptr）
	virtual const char* GetDescription() const { return nullptr; } // 何をする Component かの説明（Inspector の見出し・Add Component の項目にマウスを乗せると出る。無ければnullptr）
	virtual bool IsScript() const { return false; }               // Script か（Inspector の見出しに「(Script)」を付ける。Unity と同じ）

	// ===== 実体の出し入れ（実体を持っているManagerへの窓口）=====
	virtual void* Get(Handle<Entity> handle) const = 0;               // 持っていなければnullptr。ポインタは使い捨て
	virtual bool IsAddPending(Handle<Entity>) const { return false; } // 追加を予約済みか
	// 追加を予約（initial が空なら初期値。中身は Save で作った写し）。EditorHistory が Flush の中で呼ぶ
	virtual void RequestAdd(Handle<Entity>, std::span<const std::byte> /*initial*/) const {}
	virtual void RequestRemove(Handle<Entity>) const {} // 取り外しを予約

	// ===== 中身の写し（Undo・外したComponentを戻すときに使う）=====
	// 写し方は種類ごとに違う：データの Component はバイト列をそのまま、Script は保存と同じ JSON の文字
	virtual std::vector<std::byte> Save(const void* component) const = 0;          // 今の中身を写す
	virtual void Load(void* component, std::span<const std::byte> data) const = 0; // 写しを書き戻す


	// ===== Inspectorの中身 =====
	virtual void Draw(void* component) const = 0;
};

/// <summary>
/// ComponentEditorの登録表。カテゴリ順（同じカテゴリの中は名前順）に並べて持つ
/// <para>ゲームのシーンはエンジンのEditorより先に登録することがある（ImGuiManagerの初期化より前にシーンが作られる）。
/// それでもTransformが一番上に来るように、登録した順ではなくカテゴリで並べる</para>
/// </summary>
template<class T> class TypedComponentEditor : public ComponentEditor {
	// これはバイトコピーできることの検査。ポインタを含まないことまでは検査できない。
	static_assert(std::is_trivially_copyable_v<T>);

public:
	bool IsOptional() const final { return EntityManager::IsRemovable<T>(); }
	void* Get(Handle<Entity> handle) const final { return EntityManager::Get<T>(handle); }
	bool IsAddPending(Handle<Entity> handle) const final { return EntityManager::IsAddPending<T>(handle); }
	void RequestAdd(Handle<Entity> handle, std::span<const std::byte> initial) const final {
		T value{};
		if (initial.size() == sizeof(T)) {
			std::memcpy(&value, initial.data(), sizeof(T));
		}
		EntityManager::RequestAdd<T>(handle, value);
	}
	void RequestRemove(Handle<Entity> handle) const final { EntityManager::RequestRemove<T>(handle); }

	// データの Component は、中身をバイト列のまま写す
	std::vector<std::byte> Save(const void* component) const final {
		const auto* bytes = static_cast<const std::byte*>(component);
		return std::vector<std::byte>(bytes, bytes + sizeof(T));
	}
	void Load(void* component, std::span<const std::byte> data) const final {
		if (data.size() == sizeof(T)) {
			std::memcpy(component, data.data(), sizeof(T));
		}
	}

	void Draw(void* component) const final { DrawComponent(*static_cast<T*>(component)); }

protected:
	virtual void DrawComponent(T& component) const = 0;
};

/// <summary>
/// ComponentEditorの登録表。Inspectorの区画も Add Component のメニューも、この並びをそのまま使う
/// </summary>
class ComponentEditorRegistry {
public:
	// 登録する。同じ名前のものが登録済みなら何もしない（シーンを作り直すたびに増えないように）
	static void Register(std::unique_ptr<ComponentEditor> editor);
	static const std::vector<std::unique_ptr<ComponentEditor>>& GetAll();

	// ===== カテゴリの説明（Add Component で、カテゴリにマウスを乗せると出る・右の列の上に出る）=====
	// 上の段（"Camera" など）の説明は ComponentEditor.cpp の kTopLevelCategories に書く。ここで足すのは、その下（"Gameplay/Movement" など）
	// path は "/" で区切ったカテゴリのパス。description はプログラムの終わりまで消えない文字（文字列リテラル）。同じ path に2回足したら後の物
	static void RegisterCategoryDescription(std::string_view path, const char* description);
	// path の説明（無ければ nullptr）
	static const char* FindCategoryDescription(std::string_view path);

	/// <summary>
	/// T の ComponentEditor（登録されていなければ nullptr）
	/// <para>Inspector の外（Scene ビューのギズモなど）で Component を変えて、Undo の履歴に積むときに、どの Component の変更かを渡すのに使う</para>
	/// </summary>
	template<class T> static const ComponentEditor* Find() {
		for (const std::unique_ptr<ComponentEditor>& editor : GetAll()) {
			// dynamic_cast：実体が TypedComponentEditor<T> を継承した物なら、そのポインタ。違えば nullptr
			if (dynamic_cast<const TypedComponentEditor<T>*>(editor.get()) != nullptr) {
				return editor.get();
			}
		}
		return nullptr;
	}
};