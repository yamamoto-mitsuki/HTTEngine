#pragma once
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <externals/magic_enum/magic_enum.hpp>

#include "MyEngine/Core/FixedList.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Vector2.h"
#include "MyEngine/Math/Vector3.h"
#include "MyEngine/Math/Vector4.h"
#include "MyEngine/String/FixedString.h"

// 前方宣言
struct GameObject;
class NameList;
class ComponentUI;


/// <summary>
/// 項目の見せ方の指定。省略できる（Range(...) や Tip(...) で作る）
/// </summary>
struct FieldStyle {
	float min = 0.0f;              // 下限（min < max のときだけ効く）
	float max = 0.0f;              // 上限
	float dragSpeed = 0.0f;        // つまんで動かす速さ（0なら既定。範囲があれば「200ピクセルで端から端まで」）
	const char* tooltip = nullptr; // マウスを乗せたときに出す説明
	bool multiline = false;        // 文字の入力欄を何行もある箱にする（Enter で改行できる）

	// つなげて書ける（Range(0.0f, 10.0f).Tip("説明") のように）
	FieldStyle Tip(const char* text) const {
		FieldStyle copy = *this;
		copy.tooltip = text;
		return copy;
	}
	FieldStyle Drag(float speed) const {
		FieldStyle copy = *this;
		copy.dragSpeed = speed;
		return copy;
	}
};

// 範囲を決める（つまんで動かす。数字を打っても範囲の外にはならない）
// スライダーは使わない
inline FieldStyle Range(float min, float max) {
	FieldStyle style;
	style.min = min;
	style.max = max;
	return style;
}

// 下限だけ決める（つまんで動かす。負の値にしたくない項目に）
inline FieldStyle AtLeast(float min) {
	FieldStyle style;
	style.min = min;
	style.max = FLT_MAX; // windows.h の max マクロに邪魔されないよう、numeric_limits ではなく cfloat のFLT_MAXを使う
	return style;
}

// つまんで動かす速さを決める
inline FieldStyle Drag(float speed) {
	FieldStyle style;
	style.dragSpeed = speed;
	return style;
}

// 説明だけ付ける
inline FieldStyle Tip(const char* text) {
	FieldStyle style;
	style.tooltip = text;
	return style;
}

// 文字の入力欄を、何行もある箱にする（Multiline().Tip("説明") のようにつなげられる）
inline FieldStyle Multiline() {
	FieldStyle style;
	style.multiline = true;
	return style;
}

// ファイルから読み込む物（アセット）の種類。AssetField で使う
enum class AssetType {
	Model,     // ModelManager::Load の番号
	Texture,   // TextureManager::Load の番号
	Sound,     // SoundManager::Load の番号
	Animation, // AnimationManager::Load の番号
	Prefab,    // PrefabManager::Load の番号
	Font,      // FontManager::Load の番号
	PhysicMaterial, // PhysicMaterialManager::Load の番号（.physmat）
	AttackTable,    // AttackTableManager::Load の番号（.attacktable）
	Curve,          // CurveManager::Load の番号（.curve）
	Timeline,       // TimelineManager::Load の番号（.timeline）
	Shader,         // 描画プログラムの番号 + 1（ShaderPackageLoader::FindProgramHandle。#PROGRAM のある .shader）
	Material,       // MaterialManager::Load の番号（.material）
};

// アセットの番号からパスを引く（未選択・知らない番号なら空）。保存とInspectorの表示で使う
const std::string& GetAssetPath(AssetType type, uint32_t handle);
// パスのファイルを読み込んで番号をもらう（空・ファイルが無いなら0＝未選択）。path は UTF-8 の文字（シーンファイル・Inspector の文字）。中で ToPath を通して各 Manager に渡す
uint32_t LoadAsset(AssetType type, const std::string& path);
// その種類のアセットのファイルか（拡張子で見る。大文字でもよい）。Inspector のファイルの一覧・Project ウィンドウのアイコンで使う
bool IsAssetFile(std::string_view path, AssetType type);


/// <summary>
/// 一覧1つを、要素の型を知らずに扱う窓口（ui.ListField の中身。Inspector・保存・読み込みの係が使う）
/// <para>ゲームは ui.ListField(...) で書くので、これを直接は使わない</para>
/// <para>ComponentUI の係は virtual 関数なので、テンプレート（要素の型ごとに別の関数）にできない。
/// そこで「数を変える・要素1つを描く」などの操作だけをこの形にそろえて渡す（ComponentSerializer の RegisterErased と同じ考え方）</para>
/// </summary>
class ListAccess {
public:
	virtual ~ListAccess() = default;

	// ===== 数 =====
	virtual size_t GetSize() const = 0;
	virtual size_t GetCapacity() const = 0; // 入る数の上限（std::vector は上限なし＝SIZE_MAX）
	virtual void Resize(size_t count) = 0;  // 数を変える（増えた分は初期値）

	// ===== 並びを変える（index は GetSize() より小さいこと。Insert だけは GetSize() と同じでもよい＝最後に足す）=====
	virtual void Insert(size_t index) = 0;         // index の所に初期値を入れる
	virtual void Duplicate(size_t index) = 0;      // index の写しを、すぐ後ろに入れる
	virtual void Erase(size_t index) = 0;          // index を消す
	virtual void Move(size_t from, size_t to) = 0; // from の要素を to の位置へ（間の要素は1つずつずれる）

	// ===== 要素1つ =====
	virtual void DescribeElement(ComponentUI& ui, size_t index) = 0; // 要素1つの見せ方を呼ぶ（ui.Field の並び）
	virtual std::string MakeTitle(size_t index) const = 0;            // Inspector の見出しの、番号の後ろの文字（空なら番号だけ）
};

namespace ComponentUIDetail {
// 一覧にできる入れ物か（std::vector と FixedList）
template<class Container> struct ListTraits : std::false_type {};
template<class T, class Allocator> struct ListTraits<std::vector<T, Allocator>> : std::true_type {
	using Element = T;
	static constexpr bool kIsFixed = false;
};
template<class T, size_t N> struct ListTraits<FixedList<T, N>> : std::true_type {
	using Element = T;
	static constexpr bool kIsFixed = true;
};

// 見出しを作らない（ui.ListField に見出しの関数を渡さなかったとき）
struct NoListTitle {};

/// <summary>
/// ListAccess の中身。std::vector と FixedList の違い（関数の名前・上限）を、ここで吸収する
/// </summary>
template<class Container, class Describe, class Title>
class ListAccessOf final : public ListAccess {
	using Traits = ListTraits<Container>;
	using Element = typename Traits::Element;

public:
	ListAccessOf(Container& list, Describe& describe, Title& title) : list_(list), describe_(describe), title_(title) {}

	size_t GetSize() const override {
		if constexpr (Traits::kIsFixed) {
			return list_.Size();
		} else {
			return list_.size();
		}
	}
	size_t GetCapacity() const override {
		if constexpr (Traits::kIsFixed) {
			return Container::Capacity();
		} else {
			return SIZE_MAX;
		}
	}
	void Resize(size_t count) override {
		if constexpr (Traits::kIsFixed) {
			list_.Resize(count);
		} else {
			list_.resize(count);
		}
	}
	void Insert(size_t index) override { InsertValue(index, Element{}); }
	void Duplicate(size_t index) override {
		const Element copy = list_[index]; // 先に写す（入れると並びが動いて、元の要素の場所が変わることがある）
		InsertValue(index + 1, copy);
	}
	void Erase(size_t index) override {
		if constexpr (Traits::kIsFixed) {
			list_.Erase(index);
		} else {
			list_.erase(list_.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}
	void Move(size_t from, size_t to) override {
		// std::rotate(first, middle, last)：[first, last) を、middle が先頭に来るように回す（間の要素が1つずつずれる）
		const auto at = [this](size_t i) { return list_.begin() + static_cast<std::ptrdiff_t>(i); };
		if (from < to) {
			std::rotate(at(from), at(from + 1), at(to + 1)); // from を後ろへ：from+1〜to が1つ前へ
		} else if (to < from) {
			std::rotate(at(to), at(from), at(from + 1)); // from を前へ：to〜from-1 が1つ後ろへ
		}
	}
	void DescribeElement(ComponentUI& ui, size_t index) override { describe_(ui, list_[index]); }
	std::string MakeTitle(size_t index) const override {
		if constexpr (std::is_same_v<Title, NoListTitle>) {
			(void)index;
			return {};
		} else {
			return std::string(title_(std::as_const(list_[index])));
		}
	}

private:
	// index の所に value を入れる（一杯なら入れない）
	void InsertValue(size_t index, const Element& value) {
		if constexpr (Traits::kIsFixed) {
			list_.Insert(index, value);
		} else {
			list_.insert(list_.begin() + static_cast<std::ptrdiff_t>(index), value);
		}
	}

	Container& list_;    // 一覧そのもの
	Describe& describe_; // 要素1つの見せ方
	Title& title_;       // 見出しの文字を作る関数（NoListTitle なら作らない）
};
} // namespace ComponentUIDetail


/// <summary>
/// Componentの中身の見せ方を書くための道具。ゲーム側にImGuiが出てこないようにするための薄い包み
/// <para>実体は差し替えられる。Inspectorに描く係・ファイルへ書く係・ファイルから読む係の3つがあり、
/// COMPONENT(...) に書いた並びが、そのまま「Inspectorの見た目」と「セーブデータの項目」になる（ComponentSerializer.cpp）</para>
/// <para>保存するときの項目の名前は label。label を変えると、前に保存した値は読めなくなる（読み込み時に警告が出る）</para>
/// </summary>
class ComponentUI {
public:
	virtual ~ComponentUI() = default;

	// ===== 値を1つ見せる（型で見た目が決まる）=====
	virtual void Field(const char* label, float& value, FieldStyle style = {}) = 0;
	virtual void Field(const char* label, int& value, FieldStyle style = {}) = 0;
	virtual void Field(const char* label, bool& value, FieldStyle style = {}) = 0;
	virtual void Field(const char* label, Vector2& value, FieldStyle style = {}) = 0;
	virtual void Field(const char* label, Vector3& value, FieldStyle style = {}) = 0;
	virtual void Field(const char* label, Vector4& value, FieldStyle style = {}) = 0;
	// 別のEntityへの参照（InspectorではHierarchyからドラッグ＆ドロップで入れる。保存はEntityIdで）
	virtual void Field(const char* label, EntityRef& value, FieldStyle style = {}) = 0;
	// GameObject で持つ版（Script 用。見た目と保存は EntityRef と同じ。中身は ComponentUI.cpp）
	void Field(const char* label, GameObject& value, FieldStyle style = {});
	// 文字（Script 用。データの Component は std::string を持てないので使わない）
	virtual void Field(const char* label, std::string& value, FieldStyle style = {}) = 0;

	// ===== 決まった長さまでの文字（データの Component 用。見た目と保存は std::string の項目と同じ）=====
	template<size_t N> 
	void Field(const char* label, FixedString<N>& value, FieldStyle style = {}) { 
		std::string text = value.ToString();
		Field(label, text, style); // 入力欄・保存・読み込みは、std::stringの項目に任せる
		// 変わったときだけ書き戻す（入りきらない分は切れる）
		if (value != text) {
			value = text;
		}
	}

	// ===== enum（選択肢の名前は magic_enum が型から作る。保存も名前で行うので、並びを変えても読める）=====
	template<class E>
	    requires std::is_enum_v<E>
	void Field(const char* label, E& value, FieldStyle style = {}) {
		constexpr auto names = magic_enum::enum_names<E>(); // 選択肢の名前（宣言の順）
		const auto current = magic_enum::enum_index(value); // 今の値が何番目か（magic_enumが知らない値なら無し）
		size_t index = current.value_or(0);
		const size_t before = index;
		EnumField(label, index, names, style);
		// 変わったときだけ書き戻す（知らない値を、触っていないのに先頭の選択肢へ変えてしまわないように）
		if (index != before && index < names.size()) {
			value = magic_enum::enum_value<E>(index);
		}
	}

	// ===== 一覧（Unity の List。Inspector で足す・消す・並べ替える。保存は JSON の配列で、要素1つが {...} 1つ）=====
	// list は std::vector（Script）か FixedList（データの Component）。describe は要素1つの見せ方（COMPONENT の中と同じ書き方）
	//   ui.ListField("Stages", stages_, [](ComponentUI& ui, Stage& stage) {
	//       ui.Field("Max Distance", stage.maxDistance, AtLeast(0.0f).Tip("この段階になる、相手までの距離の上限（m）"));
	//   });
	// title を渡すと、Inspector の要素の見出しの番号の後ろに出す（折りたたんだままでも中身が分かるように）
	//   [](const Stage& stage) { return std::format("〜{:.0f}m", stage.maxDistance); }
	// Prefab の置いた所ごとの上書きは、一覧を丸ごと（1つでも変えたら、一覧の全部が上書き。足す・消すで上書きが別の要素にずれない）
	// Timeline の SetValue（値のトラック）からは、一覧の中の値は書けない
	template<class Container, class Describe>
	    requires ComponentUIDetail::ListTraits<Container>::value
	void ListField(const char* label, Container& list, Describe describe, FieldStyle style = {}) {
		ComponentUIDetail::NoListTitle noTitle;
		ComponentUIDetail::ListAccessOf<Container, Describe, ComponentUIDetail::NoListTitle> access(list, describe, noTitle);
		ListFieldErased(label, access, style);
	}
	// 見出しの文字を作る関数も渡す版（title は 要素 → 文字。std::string か const char* を返す）
	template<class Container, class Describe, class Title>
	    requires ComponentUIDetail::ListTraits<Container>::value && std::is_invocable_v<Title&, const typename ComponentUIDetail::ListTraits<Container>::Element&>
	void ListField(const char* label, Container& list, Describe describe, Title title, FieldStyle style = {}) {
		ComponentUIDetail::ListAccessOf<Container, Describe, Title> access(list, describe, title);
		ListFieldErased(label, access, style);
	}

	// ===== アセット（モデル・テクスチャの番号。Inspectorではファイルを選ぶ。保存はパスで）=====
	virtual void AssetField(const char* label, uint32_t& handle, AssetType type, FieldStyle style = {}) = 0;

	// ===== 色（0〜1。色見本を押すと色を選べる。style は説明（Tip）だけ効く）=====
	virtual void ColorField(const char* label, Vector3& rgb, FieldStyle style = {}) = 0;
	virtual void ColorField(const char* label, Vector4& rgba, FieldStyle style = {}) = 0;

	// ===== 飾り（保存には関係しない）=====
	virtual void Label(const char* text) = 0;               // 灰色の説明文
	virtual void Separator(const char* text = nullptr) = 0; // 区切り線（文字を入れると見出しになる）
	virtual void Space() = 0;                               // 1行あける

	// ===== 今の設定では効かない項目を灰色にする（Begin と End の間の項目。保存には関係しない＝値は書く・読む）=====
	// 隠す（if で ui.Field を呼ばない）と保存からも落ちるので、値を残したい項目はこちらを使う
	virtual void BeginDisabled(bool isDisabled) { (void)isDisabled; }
	virtual void EndDisabled() {}
	// 直前のチェックの右に、次の項目を並べる（保存には関係しない）
	virtual void SameLineAfterCheck() {}

	// ===== ボタン（保存には関係しない。押された瞬間だけ true。保存・読み込みのときはいつも false）=====
	// 値を変えずに何かしたいとき（音の試し聞きなど）に使う
	virtual bool Button(const char* label) = 0;

	// ===== アニメーションのクリップの名前（持つのも保存も名前）=====
	// Inspector では、ClipSourceScope で決めたアニメーションのファイルの中の名前から選ぶ（決めていなければ、名前を打つ欄）
	// 候補に無い名前も消さずに持つ（Inspector では赤で出す。Blender で名前を戻せば、そのまま直る）
	// 保存・読み込みの係は、文字の項目と同じに扱う（候補は使わない）
	virtual void ClipField(const char* label, std::string& clip, FieldStyle style = {}) { Field(label, clip, style); }
	// 決まった長さまでの文字で持つ版（データの Component・攻撃部品の値）
	template<size_t N>
	void ClipField(const char* label, FixedString<N>& clip, FieldStyle style = {}) {
		std::string text = clip.ToString();
		ClipField(label, text, style);
		// 変わったときだけ書き戻す
		if (clip != text) {
			clip = text;
		}
	}
	// 今、クリップの候補を出しているアニメーションのファイル（AnimationManager の番号。0 なら決めていない）
	uint32_t GetClipSource() const { return clipSource_; }

	// ===== Settings の名前の一覧から選ぶ（レイヤー・攻撃の種類。NameList）=====
	// 保存は番号（名前を付け直しても、選んだ物は変わらない）。名前の選択肢を出すのは Inspector だけで、保存・読み込みの係は int の項目と同じに扱う
	// 1つ選ぶ（index は 0〜31）
	virtual void NameField(const char* label, int& index, const NameList& names, FieldStyle style = {}) {
		(void)names;
		Field(label, index, style);
	}
	// いくつでも選ぶ（i 番を選んだら、bits の i 番目のビットが 1）。保存は数（Unity の LayerMask と同じ）
	virtual void NameMaskField(const char* label, uint32_t& bits, const NameList& names, FieldStyle style = {}) {
		(void)names;
		int value = std::bit_cast<int>(bits); // ビットの並びを、そのまま int として読み書きする（全部 1 なら -1 になる）
		Field(label, value, style);
		bits = std::bit_cast<uint32_t>(value);
	}

protected:
	// enumの中身。何番目の選択肢かで受け渡す（names は選択肢の名前）
	virtual void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle style) = 0;
	// 一覧の中身（list で数・並び・要素を扱う）。何もしない係（Timeline の値を書く・集める係）は、そのまま（一覧の中の値は扱わない）
	virtual void ListFieldErased(const char* label, ListAccess& list, FieldStyle style) {
		(void)label;
		(void)list;
		(void)style;
	}

private:
	friend class ClipSourceScope;
	uint32_t clipSource_ = 0; // ClipField の候補を出すアニメーションのファイル（ClipSourceScope だけが変える）
};

/// <summary>
/// この中で描く ClipField の候補を、そのアニメーションのファイルの中の名前にする（抜けると前の値に戻る）
/// <para>Animator は自分のファイル、攻撃表は見本のファイル（Sample Animation）を渡す。Script の値などを描く側が決める</para>
/// <para>何を描いているかを知っているのは描く側なので、候補は ClipField の呼び手ではなく、ここで決める（攻撃部品はファイルを知らない）</para>
/// </summary>
class ClipSourceScope {
public:
	ClipSourceScope(ComponentUI& ui, uint32_t animationHandle) : ui_(ui), previous_(ui.clipSource_) { ui.clipSource_ = animationHandle; }
	~ClipSourceScope() { ui_.clipSource_ = previous_; }
	// 写すと、2回戻してしまうので禁止
	ClipSourceScope(const ClipSourceScope&) = delete;
	ClipSourceScope& operator=(const ClipSourceScope&) = delete;

private:
	ComponentUI& ui_;   // 候補を変えた UI
	uint32_t previous_; // 入る前の候補（入れ子にしても戻せるように）
};

// Inspectorに描く係（エンジンが1つだけ持っている。ゲームからは触らない）
ComponentUI& GetInspectorUI();