#pragma once
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"
#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// 調整項目のグループの置き場所（Settings ウィンドウの見出しと、保存先のフォルダが決まる）
enum class SettingsSection {
	Project, // ゲーム全体の設定。Parameters/<グループ名>/ に保存。製品版にも入る
	Editor,  // エディタだけの設定（Scene ビューのカメラなど）。Parameters/Editor/<グループ名>/ に保存。製品版では使わない
};

/// <summary>
/// 調整項目の管理
/// <para>表示の階層は Settings(ウィンドウ) → グループ(左の一覧の1ページ) → カテゴリ(入れ子可) → アイテム。</para>
/// <para>保存はカテゴリ単位。Parameters/&lt;グループ名&gt;/&lt;カテゴリ名&gt;.json に書き出す（エディタだけのグループは Parameters/Editor/ の下）。</para>
/// <para>アイテムを登録したときに、保存したファイルに値があればその値になる（シーンを切り替えて後から登録しても、保存した値から始まる）</para>
/// </summary>
class GlobalVariables {
public:
	// ImGui::Combo用
	struct ComboItem {
		std::vector<std::string> options; // 選択肢リスト
		int currentIndex = 0;             // 現在の選択インデックス

		bool operator==(const ComboItem&) const = default; // 変わったかを比べる（Undo用）
	};

	// ImGui::ColorEdit4用
	struct ColorItem {
		uint32_t rgba = 0xFFFFFFFF;

		bool operator==(const ColorItem&) const = default;
	};

	// std::string は名前の一覧（レイヤー名・攻撃の種類の名前）などの文字の項目
	using Item = std::variant<bool, int32_t, float, Vector2, Vector3, Vector4, ComboItem, ColorItem, std::string>;
	using json = nlohmann::json;
	// ツリーノード
	struct GVNode {
		struct Entry {
			std::string name;
			std::variant<Item, std::shared_ptr<GVNode>> value;
			Item saved; // アイテムのとき：保存した値（ファイルから読んだ・ファイルへ書いた値。ファイルに無ければ登録した値）。今の値と違えば * を出す
		};
		std::vector<Entry> entries_;
		// 子ノードを名前で検索
		GVNode* FindChild(const std::string& name) {
			for (auto& e : entries_) {
				if (e.name == name && std::holds_alternative<std::shared_ptr<GVNode>>(e.value)) {
					return std::get<std::shared_ptr<GVNode>>(e.value).get();
				}
			}
			return nullptr;
		}
		// 子ノードを名前で取得
		GVNode* GetOrCreateChild(const std::string& name) {
			if (GVNode* found = FindChild(name)) {
				return found;
			}
			entries_.push_back({name, std::make_shared<GVNode>()});
			return std::get<std::shared_ptr<GVNode>>(entries_.back().value).get();
		}
		// アイテムを名前で検索
		Item* FindItem(const std::string& name) {
			for (auto& e : entries_) {
				if (e.name == name && std::holds_alternative<Item>(e.value)) {
					return &std::get<Item>(e.value);
				}
			}
			return nullptr;
		}
	};

	// ===== カテゴリの追加 =====
	class CategoryBuilder {
	public:
		CategoryBuilder(GVNode* node, const std::string& groupName, const std::string& categoryPath) : node_(node), groupName_(groupName), categoryPath_(categoryPath) {}

		/// <summary>
		/// 子カテゴリに移動する。なければ作成する。
		/// <para>例: .Category("A").Category("B").Add(...)</para>
		/// </summary>
		CategoryBuilder Category(const std::string& categoryName) { return CategoryBuilder(node_->GetOrCreateChild(categoryName), groupName_, categoryPath_ + "/" + categoryName); }

		/// <summary>旧名。Category() と同じ。</summary>
		CategoryBuilder Group(const std::string& categoryName) { return Category(categoryName); }

		/// <summary>
		/// 項目を追加する。すでに登録済みの場合はスキップする（値を上書きしない）。
		/// <para>保存したファイルにこの項目の値があれば、value ではなくファイルの値になる</para>
		/// <para>例: .Add("X", x).Add("Y", y)</para>
		/// </summary>
		template<typename T> CategoryBuilder& Add(const std::string& itemName, const T& value) {
			if (!node_->FindItem(itemName)) {
				Item item(value);
				// 保存した値から始める（登録がエンジンの起動より後＝シーンを切り替えたときでも、ファイルの値になるように）
				GetInstance()->ReadSavedItem(groupName_, categoryPath_, itemName, item);
				node_->entries_.push_back({itemName, item, item}); // 今の値・保存した値（同じなので * は付かない）
				LogManager::Log(groupName_ + " / " + itemName);
			}
			return *this;
		}

	private:
		GlobalVariables::GVNode* node_;
		std::string groupName_;
		std::string categoryPath_; // グループの下からのカテゴリのパス（"Enemy/Transform"）。1つ目がファイルの名前になる
	};

	// ===== グループのルート =====
	class GroupRoot {
	public:
		GroupRoot(GVNode* root, const std::string& groupName) : root_(root), groupName_(groupName) {}

		/// <summary>
		/// カテゴリを取得または作成して CategoryBuilder を返す。
		/// <para>ここで作られたカテゴリが &lt;カテゴリ名&gt;.json の単位になる。</para>
		/// </summary>
		CategoryBuilder Category(const std::string& categoryName) { return CategoryBuilder(root_->GetOrCreateChild(categoryName), groupName_, categoryName); }

		/// <summary>旧名。Category() と同じ。</summary>
		CategoryBuilder Group(const std::string& categoryName) { return Category(categoryName); }

	private:
		GVNode* root_;
		std::string groupName_;
	};

	// 旧名エイリアス（既存コードの互換用）
	using GroupBuilder = CategoryBuilder;
	using SceneBuilder = GroupRoot;

	// シングルトン
	static GlobalVariables* GetInstance();

	// コピー禁止
	GlobalVariables(const GlobalVariables&) = delete;
	GlobalVariables& operator=(const GlobalVariables&) = delete;

	/// <summary>
	/// グループを宣言する。（Settings ウィンドウの1ページ分）
	/// <para>既に存在する場合はそのグループのルートノードを返す（section は最初に宣言したときのまま）。</para>
	/// </summary>
	/// <param name="groupName">グループ名。保存先フォルダ名にもなる</param>
	/// <param name="section">置き場所。ゲームのグループは省略してよい（Project）</param>
	GroupRoot Group(const std::string& groupName, SettingsSection section = SettingsSection::Project);

	/// <summary>
	/// 値を取得する。カテゴリのパスは "/" 区切りで指定する。
	/// 例: Get&lt;Vector3&gt;("GameScene", "Enemy/Transform", "Position") → GameScene → Enemy → Transform → Position の値を返す
	/// </summary>
	/// <param name="groupName">グループ名</param>
	/// <param name="categoryPath">"/" 区切りのカテゴリパス（例: "Enemy/Transform"）</param>
	/// <param name="itemName">項目名</param>
	template<typename T> T Get(const std::string& groupName, const std::string& categoryPath, const std::string& itemName) const {
		const GVNode* node = FindNode(groupName, categoryPath);
		if (!node) {
			LogManager::Error("ノードが見つかりません: " + groupName + " / " + categoryPath);
			HTT_ASSERT_MSG(false, "ノードが見つかりません");
		}
		for (const auto& e : node->entries_) {
			if (e.name == itemName && std::holds_alternative<Item>(e.value)) {
				return std::get<T>(std::get<Item>(e.value));
			}
		}
		LogManager::Error("アイテムが見つかりません: " + itemName);
		HTT_ASSERT_MSG(false, "アイテムが見つかりません");
		return T{};
	}

	/// <summary>
	/// 登録済みアイテムの値を強制上書きする。
	/// 未登録の場合は何もしない。
	/// ウィンドウ追加後のComboリセット等、毎フレームでなく
	/// 特定タイミングで値を書き換えたいときに使う。
	/// </summary>
	template<typename T> void Set(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, const T& value) {
		GVNode* node = FindNode(groupName, categoryPath);
		if (!node) {
			return;
		}

		Item* item = node->FindItem(itemName);
		if (!item) {
			return;
		}

		*item = Item(value);
	}

	/// <summary>enum型 E から ComboItem を自動生成する（optionsを手入力せずに済む）</summary>
	template<typename E> static ComboItem MakeEnumCombo(E current) {
		static_assert(std::is_enum_v<E>, "MakeEnumCombo は enum 専用です");
		ComboItem combo;
		for (std::string_view name : magic_enum::enum_names<E>()) {
			combo.options.emplace_back(name); // 識別子名をそのまま選択肢に
		}
		combo.currentIndex = static_cast<int>(magic_enum::enum_index(current).value_or(0));
		return combo;
	}

	/// <summary>ComboItem の選択インデックスを enum E に戻す</summary>
	template<typename E> static E GetEnumCombo(const ComboItem& combo) {
		static_assert(std::is_enum_v<E>, "GetEnumCombo は enum 専用です");
		size_t idx = static_cast<size_t>(combo.currentIndex);
		if (idx >= magic_enum::enum_count<E>()) { // JSONに古いindexが残っていた場合の保険
			idx = 0;
		}
		return magic_enum::enum_value<E>(idx);
	}

	// ===== 保存 =====

	/// <summary>
	/// カテゴリ1つを Parameters/&lt;グループ名&gt;/&lt;カテゴリ名&gt;.json に書き出す。
	/// </summary>
	void SaveCategory(const std::string& groupName, const std::string& categoryName);

	/// <summary>
	/// グループ内の全カテゴリを一括保存する。（カテゴリごとに1ファイル）
	/// </summary>
	void SaveGroup(const std::string& groupName);

	/// <summary>
	/// 登録済み全グループを一括保存する。
	/// </summary>
	void SaveAll();

	/// <summary>旧名。SaveGroup() と同じ。</summary>
	void SaveFile(const std::string& groupName) { SaveGroup(groupName); }

	// ===== 読み込み =====

	/// <summary>
	/// カテゴリ1つ分のjsonを読み込む。ファイルがなければスキップ。
	/// </summary>
	void LoadCategory(const std::string& groupName, const std::string& categoryName);

	/// <summary>
	/// グループ1つ分（フォルダ内の全カテゴリ）を読み込む。
	/// </summary>
	void LoadGroup(const std::string& groupName);

	/// <summary>
	/// 登録済みの全グループを、ファイルから読み直す。
	/// </summary>
	void LoadFiles();

#ifdef USE_IMGUI
	// ===== Settings ウィンドウから使う =====
	// section に入っているグループの名前（登録した順）
	std::vector<std::string> GetGroupNames(SettingsSection section) const;
	// 保存していない変更があるか（保存した値と違う項目が1つでもある）
	bool IsGroupDirty(const std::string& groupName) const;
	// 保存先のフォルダ（表示用。"Parameters/PostEffect/"）
	std::string GetSaveDirectory(const std::string& groupName) const;
	// 保存した値（ファイルの値。まだ保存していなければ登録した値）に戻す。Ctrl+Z で戻す前に戻せる
	void RevertGroup(const std::string& groupName);
	// グループ1つ分の中身（カテゴリと項目）を描く
	void DrawGroup(const std::string& groupName);
	// 描き終わったら毎フレーム呼ぶ。どの項目も触っていなければ、ここまでの変更を1件の履歴にする
	void EndDraw();
#endif

private:
	GlobalVariables() = default;
	~GlobalVariables() = default;

	// グループ1つ分
	struct GroupData {
		std::string name;
		SettingsSection section = SettingsSection::Project;
		GVNode root;
	};

	// ===== 内部ヘルパー =====
	const GroupData* FindGroup(const std::string& groupName) const;
	GroupData* FindGroup(const std::string& groupName);
	const GVNode* FindNode(const std::string& groupName, const std::string& categoryPath) const;
	GVNode* FindNode(const std::string& groupName, const std::string& categoryPath);
	GVNode* FindGroupRoot(const std::string& groupName);
	void NodeToJson(const GVNode& node, json& out) const;
	void JsonToNode(const json& json, GVNode& node);
	// アイテム1つ ↔ jsonの値（型が合わない値は入れずに false）
	static json ItemToJson(const Item& item);
	static bool JsonToItem(const json& value, Item& item);
	// 保存したファイルから、これから登録するアイテム1つの値を読む（ファイル・項目が無ければ item はそのまま）
	void ReadSavedItem(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, Item& item) const;
	// ノードの中の全アイテムを「保存した」にする（ファイルへ書けた後）
	static void MarkSaved(GVNode& node);
	// ノードの中に、保存した値と違うアイテムがあるか
	static bool IsDirty(const GVNode& node);
	// jsonファイルを out に読み込む。ファイルなし・パース失敗なら false
	bool ReadJsonFile(const std::filesystem::path& filePath, json& out) const;
	// ノードの中身をそのままjsonルートに書き出す
	bool WriteNodeToFile(const std::filesystem::path& filePath, const GVNode& node) const;
	// フォルダ内の <カテゴリ名>.json を全部読む
	void LoadGroupCategories(const std::string& groupName);
	// 保存先パス（エディタだけのグループは Parameters/Editor/ の下）
	std::filesystem::path GroupDirectory(const std::string& groupName) const;
	std::filesystem::path CategoryFilePath(const std::string& groupName, const std::string& categoryName) const { return GroupDirectory(groupName) / (categoryName + ".json"); }
	// 項目1つの値を書き換える。見つからなければ false（Undo・Redo から使う）
	bool SetItem(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, const Item& value);
#ifdef USE_IMGUI
	// categoryPath は "/" 区切りのカテゴリ（グループ直下なら空）
	void DrawNode(GVNode& node, const std::string& groupName, const std::string& categoryPath);
	void DrawItem(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, Item& item);
	// 保存した値と違うアイテムを、保存した値に戻す（戻した物は履歴に積む）
	void RevertNode(GVNode& node, const std::string& groupName, const std::string& categoryPath);

	// ===== Undo・Redo（Inspector と同じく、マウスを離すまでの変更を1件にまとめる）=====
	struct PendingEdit {
		std::string groupName;
		std::string categoryPath;
		std::string itemName;
		Item before; // 触り始める前の値
		Item after;  // 最後に変えた値
	};
	void RecordEdit(const std::string& groupName, const std::string& categoryPath, const std::string& itemName, const Item& before, const Item& after);
	void CommitEdits(); // ここまでの変更を EditorHistory に積む

	std::vector<PendingEdit> pendingEdits_; // まだ履歴に積んでいない変更
#endif

	std::vector<GroupData> groups_;
	const std::string kDirectoryPath = "Parameters/";              // ゲーム全体の設定の保存先
	const std::string kEditorDirectoryPath = "Parameters/Editor/"; // エディタだけの設定の保存先
};