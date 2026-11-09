#pragma once
#include <format>
#include <string>
#include <type_traits>
#include <vector>

#include <externals/imgui/imgui.h>

// プロファイラのカテゴリ名（ウィンドウ名になる）
namespace ProfCategory {
inline const std::string CPU = "CPU";
inline const std::string GPU = "GPU";
inline const std::string Memory = "Memory";
inline const std::string Physics = "Physics";
inline const std::string Frame = "Frame";
}; // namespace ProfCategory

/// <summary>
/// 計測値の登録と表示を行うクラス
/// <para>Category（ウィンドウ）→ Group（折りたたみ）→ Entry（1項目）の3段構成。</para>
/// <para>ラベルに '/' を含めると、表示時に階層ツリーへ展開される（例: "Opaque/ModelPBR"）。</para>
/// </summary>
class Profiler {
public:
	// --- 名前登録時に間違えないようにするためのキー ---
	// CPUグループ
	struct CPUGroup {
		static inline std::string Times = "Times";
	};

	// --- 表示方法 ---
	enum class DisplayType {
		Value, // 数値表示
		Bar,   // バー表示
		Plot   // グラフ表示
	};

	// --- 表示項目 ---
	struct Entry {
		std::string label;                            // 名前（'/'区切りで階層になる）
		std::string text;                             // Value用の表示文字列
		std::string unit;                             // ms/MB などの単位
		float value = 0.0f;                           // Bar/Plotの現在値。Valueでも集計用に保持する
		float maxValue = 1.0f;                        // Bar/Plotの上限
		std::vector<float> history;                   // Plot用の履歴
		DisplayType displayType = DisplayType::Value; // 表示タイプ
	};
	// グループ情報
	struct GroupData {
		std::string name;
		std::vector<Entry> entries;
	};
	// カテゴリ情報
	struct CategoryData {
		std::string name;
		std::vector<GroupData> groups;
	};

	// ===== グループ =====
	class GroupProxy {
	public:
		explicit GroupProxy(GroupData* g) : groupData_(g) {};

		GroupProxy& Text(const std::string& label, const std::string& value);
		GroupProxy& Bar(const std::string& label, float value, float maxValue, const std::string& unit = "");
		GroupProxy& Plot(const std::string& label, float value, float maxValue = 0.0f);

		/// <summary>
		/// 数値を登録する。表示文字列に加えて数値も保持するので、親ノードの合計に使える。
		/// </summary>
		template<class T> GroupProxy& Value(const std::string& label, T v, const std::string& unit = "") {
			std::string s = std::is_floating_point_v<T> ? std::format("{:.2f}", double(v)) : std::to_string(v);
			if (!unit.empty()) {
				s += " " + unit;
			}
			Text(label, s);
			// 階層表示で親の合計を出すため、数値と単位も残しておく
			Entry& e = FindOrAdd(label);
			e.value = static_cast<float>(v);
			e.unit = unit;
			return *this;
		}

	private:
		Entry& FindOrAdd(const std::string& label);
		GroupData* groupData_;
	};

	// ===== カテゴリ =====
	class CategoryProxy {
	public:
		explicit CategoryProxy(CategoryData* c) : categoryData_(c) {}
		GroupProxy Group(const std::string& name);

		// --- グループ無しで直接書く（内部で名前なしの既定グループへ）---
		template<class T> CategoryProxy& Value(const std::string& label, T v, const std::string& unit = "") {
			Group("").Value(label, v, unit);
			return *this;
		}
		CategoryProxy& Text(const std::string& label, const std::string& value) {
			Group("").Text(label, value);
			return *this;
		}
		CategoryProxy& Bar(const std::string& label, float v, float maxV, const std::string& unit = "") {
			Group("").Bar(label, v, maxV, unit);
			return *this;
		}
		CategoryProxy& Plot(const std::string& label, float v, float maxV = 0.0f) {
			Group("").Plot(label, v, maxV);
			return *this;
		}

	private:
		CategoryData* categoryData_;
	};

	static CategoryProxy Category(const std::string& name);
	static void Draw(bool* open = nullptr);
	static void Clear();

private:
	/// <summary>
	/// ラベルを '/' で分割した木のノード
	/// <para>葉は Entry を指し、枝は子の合計値を持つ。</para>
	/// </summary>
	struct LabelNode {
		std::string name;             // この階層の名前（例: "Opaque" / "ModelPBR"）
		const Entry* entry = nullptr; // 葉のときだけ実体を指す
		std::vector<LabelNode> children;
		float sum = 0.0f; // 配下の合計値
		std::string unit; // 合計表示用の単位

		// 同名の子を探し、無ければ追加する
		LabelNode& FindOrAddChild(const std::string& childName);
	};

	// --- 表示ヘルパー ---
	// 使用率(0〜1)から色を決める（緑→黄→赤）
	static ImVec4 BarColor(float frac);
	// 1項目を表示形式に応じて描く
	static void DrawEntry(const Entry& entry);
	// グループ内の項目を描く（'/'があれば階層、無ければ従来どおり並べる）
	static void DrawEntries(const std::vector<Entry>& entries);
	// entries から '/' 区切りの木を構築する
	static LabelNode BuildLabelTree(const std::vector<Entry>& entries);
	// 木を再帰的に描く
	static void DrawLabelNode(const LabelNode& node);

	static inline std::vector<CategoryData> categories_;
};