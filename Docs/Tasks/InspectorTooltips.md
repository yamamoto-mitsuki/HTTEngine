参考：
- Unreal の UMETA(ToolTip)（enum の値ごとに説明を付ける。説明は「項目」ではなく「enum の型」に付く）：https://dev.epicgames.com/documentation/en-us/unreal-engine/metadata-specifiers-in-unreal-engine
- Unity の Tooltip 属性（項目の説明。このエンジンの `Tip(...)`）：https://docs.unity3d.com/ScriptReference/TooltipAttribute.html
- ADL（引数の型から関数を探す決まり。`EnumTips` を登録なしで見つける仕組み）：https://en.cppreference.com/w/cpp/language/adl
- `using enum`（C++20。`ParticleShape::` を付けずに書く）：https://en.cppreference.com/w/cpp/language/enum#Using-enum-declaration
- ImGui の折り返す吹き出し（`imgui_demo.cpp` の `HelpMarker`：`BeginItemTooltip` → `PushTextWrapPos(GetFontSize() * 35.0f)`）
- この作業のきっかけ：enum の Tip が「A：〜 / B：〜 / C：〜」の長い1行で読みにくい。選択肢ごとに、マウスを乗せたら出るようにしたい。Add Component と Inspector の見出しにも、グループ・Component の説明を出したい

# Inspector の説明（enum の選択肢・Component・カテゴリ）

- **パートA enum の選択肢の説明**（`Core/EnumTips.h`・`Core/EnumTipTable.h`（新しい）・vcxproj・`ComponentUI.h/.cpp`・`ComponentSerializer.cpp`・`EditorWidgets.h`）：enum の型の横に `EnumTips` を書くと、Inspector で選択肢の一覧を開いて1つにマウスを乗せたとき、その選択肢の説明が出る。閉じた欄に乗せると「項目の説明」と「今の選択肢の説明」。長い説明は折り返す
- **パートB Component の説明**（`ComponentEditor.h`・`GameComponent.h`・`Script.h`・`ScriptSystem.h/.cpp`・`ScriptEditor.h`）：`ComponentEditor::GetDescription` を足す。エンジンは登録の所で、ゲームは `COMPONENT(型名, "サブカテゴリ", "説明")`・`SCRIPT(クラス名, "サブカテゴリ", "説明")` で書く
- **パートC カテゴリの説明**（`ComponentEditor.h/.cpp`・`GameComponent.h`）：Add Component の上の段（Camera など）の説明は並び順の表に一緒に書く。ゲームの Gameplay の下は `COMPONENT_CATEGORY("Movement", "説明")`
- **パートD Inspector に出す**（`InspectorWindow.cpp`）：見出し・Add Component の項目に Component の説明、カテゴリに説明。Add Component の右の列の上に、選んだカテゴリの説明を灰色で出す
- **パートE 確かめ方**

**説明文（データ）は本体に直接入れてある**（ユーザーの「仕組みは md・説明文は直接」）。入れた物は 0.7。説明文は A・B の仕組みを使うので、**パートA〜D を全部写すまで、本体はビルドが通らない**（`Core/EnumTips.h` が無い・`GetDescription` が無い・登録の引数が1つ多い、のエラーが出る）。A → B → C → D の順に写す（D は B の `GetDescription` と C の `FindCategoryDescription` を使う）。

ブランチは `feature/InspectorTooltips`（`develop` 87d6bb4 から。未コミット）。

**2026-11-12 本体へ直接入れた**（ユーザーがカテゴリの説明（C-2 の `kTopLevelCategories`）を短く直した md で「直接」）。パートA〜D を本体に入れ、カテゴリの説明は md の文のとおりにした。Debug・Development・Release のビルドでエラー・警告 0。吹き出しの見た目は、まだ目で見ていない（E-2）。

**本体で確かめた**：仕組みと説明文を全部入れた形で、Debug・Development・Release のビルドでエラー・警告 0。その後、仕組みの所だけを元に戻して、この md にした（答えの差分は E-3）。マウスを乗せた吹き出しの見た目は、まだ目で見ていない（E-2）。


## 0章 全体の形

### 0.1 今の困りごと

```cpp
ui.Field("Shape###shape", value.shape, Tip("Point：点から全部の向き／Sphere：球から外へ／Circle：XY の円から外へ／Cone：上（+Y）へ開く／Box：箱から外へ"));
```

- 吹き出しは折り返さないので、画面の端まで1行に伸びる
- 選んだ選択肢の説明を、長い1行の中から探すことになる
- 同じ enum を何か所でも使う（`BlendMode` は Sprite Renderer・Image・Text・Vector Shape・Polyline の5か所、`TimeMode` は4か所）。同じ説明を何か所にも書いていて、少しずつ違っている
- Add Component・Inspector の見出しには、何をする Component かの説明が無い（名前だけ。ゲームの物はファイルの場所だけ）

### 0.2 選択肢の説明は「enum の型」に付ける

Unreal は `UENUM` の値ごとに `UMETA(ToolTip = "...")` を書き、その enum を使うどの項目でも同じ説明が出る。このエンジンも同じにする。

| 説明 | どこに書くか | 出る所 |
|---|---|---|
| 項目そのもの（「重ね方」） | 今までどおり `ui.Field(..., Tip("重ね方"))` | 項目に乗せたとき |
| 選択肢（「Add：足して光る」） | **enum の宣言の横**に `EnumTips` 関数 | 一覧を開いて選択肢に乗せたとき・閉じた欄の「今の選択肢」 |

```cpp
enum class ParticleBlend {
	Add,    // 足す（光る粒。重なるほど明るい）
	Normal, // 普通（奥から順には並べないので、重なると縁が汚くなることがある）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ParticleBlend) {
	using enum ParticleBlend;
	return MakeEnumTips<ParticleBlend>({
	    {Add, "足す（光る粒。重なるほど明るい）"},
	    {Normal, "普通（奥から順には並べないので、重なると縁が汚くなることがある）"},
	});
}
```

- **登録は要らない**。`ComponentUI` は `EnumTips(value)` と呼ぶだけで、C++ が「引数の型が宣言された名前空間」から関数を探す（ADL）。enum の横に書けば見つかる。書かなければ説明が出ないだけ
- `constexpr` なので、選択肢の番号の並び（`std::array<const char*, 選択肢の数>`）はコンパイルのときに作られる（毎フレーム作らない）
- 値は `ParticleBlend::Add` のように型で書く（文字で `"Add"` と書くと、名前を変えたときに気づけない。型ならコンパイルエラーになる）
- 項目ごとに選択肢の説明を変える書き方（`ui.Field` に選択肢の説明を渡す）は作らない。今ある説明は、どれも型の説明で足りた（使う所ごとの話は、項目の Tip の方に書く）。要るようになったら、型の説明の上書きとして後から足せる

ほかの案と、採らない理由：

| 案 | 採らない理由 |
|---|---|
| 今の Tip の文字を「／」で切って、選択肢の名前を探す | 書き方の約束に頼る（「：」「／」を1つ間違えると黙って出なくなる）。同じ enum の説明を何か所にも書くのも直らない |
| 項目ごとに選択肢の説明を渡す（`ui.Field(..., {{A, "…"}})`） | 同じ enum を使う所すべてに同じ説明を書くことになる |
| 値の順に説明の文字を並べる（`{"…", "…"}`） | enum の並びを変えると、説明が別の値にずれる |
| enum と説明を1つのマクロで作る | enum の宣言がマクロに隠れて、読みにくい・IntelliSense が効きにくい |

### 0.3 ファイルを2つに分ける

| ファイル | 中身 | include する所 |
|---|---|---|
| `Core/EnumTips.h` | `EnumTip`・`MakeEnumTips`（書く側の道具） | enum を書いたヘッダ（Component・RenderStates など） |
| `Core/EnumTipTable.h` | `HasEnumTips`・`MakeEnumTipTable`（読む側の道具。magic_enum を使う） | `ComponentUI.h`・`EditorWidgets.h` |

enum のヘッダは描画・物理のたくさんのファイルが読むので、書く側には magic_enum を読ませない。`Core` に置くのは、Graphics の enum（`BlendMode` など）も ECS を読まずに書けるように。

### 0.4 出し方

- **閉じた欄**に乗せる：項目の説明（Tip）と、線の下に「今の選択肢：その説明」
- **開いた一覧**の選択肢に乗せる：その選択肢の説明だけ（開いている間は、閉じた欄の吹き出しは出さない。2つが重ならないように）
- 吹き出しは文字の大きさの 32 倍で折り返す（`EditorWidgets::kTooltipWrapEm`）。ふつうの項目の Tip も同じ折り返しにする
- 乗せてすぐではなく、ImGui の決まった少しの間（`ImGuiHoveredFlags_ForTooltip`）を置いて出す。一覧の上でマウスを動かしている間に、吹き出しがちらつかないように
- 描く所は `EditorWidgets::EnumComboWithTips` の1つにする。Inspector の enum の項目（`ComponentUI`）と、Model Renderer の Shading・Blend などの欄（`EditorWidgets::EnumCombo`）の両方が使う

### 0.5 Component の説明

- `ComponentEditor` に `GetDescription()` を足す（`GetName`・`GetCategory` と並ぶ「種類の情報」。無ければ nullptr）
- 書くのは**登録する所**（名前・カテゴリを書く所の隣）。エンジンは `DescribedComponentEditor` の4つ目の引数、Transform・Model Renderer・ライトのような自前の Editor は `GetDescription` を上書きする
- ゲームは `COMPONENT(Spin, "Movement", "決まった速さで回り続ける")`・`SCRIPT(PlayerController, "Player", "プレイヤーの移動")`。3つ目を書かなければ今までどおり（説明なし）。今あるゲームのコードは直さなくてよい
- 出す所：Inspector の見出しと、Add Component の項目。ゲームの物は、今まで出していた書いてあるファイルの場所を、説明の下に灰色で出す

### 0.6 カテゴリの説明

- 上の段（Camera・Rendering3D…）の説明は、`ComponentEditor.cpp` の並び順の表 `kTopLevelOrder` を `kTopLevelCategories`（名前と説明）に変えて、一緒に書く。並びと説明を別々の表にすると、カテゴリを足したときに片方を忘れる
- 今まで表に無かった Animation・Combat・Scripts も表に足す（今と同じ並び＝Gameplay の後に、名前の順で）。並びは変わらない
- ゲームの Gameplay の下（`"Gameplay/Movement"`）は、`COMPONENT_CATEGORY("Movement", "動かす物");` で登録する（`ComponentEditorRegistry::RegisterCategoryDescription`）
- 出す所：左の列のカテゴリ・右の列の入れ子のカテゴリに乗せたとき。右の列の一番上に、選んでいるカテゴリの説明を灰色で（乗せなくても分かるように。検索中は出さない）

### 0.7 本体に直接入れた説明文

| 何 | 数 | どこ |
|---|---|---|
| Component の説明 | 47 | 各 System・`UI/UIScreen.cpp` の `DescribedComponentEditor<…>>("名前", "カテゴリ", &Describe, "説明")` |
| 自前の Editor の説明 | 5 | `TransformEditor.h`・`ModelRendererEditor.h`・`LightEditor.h`（Directional・Point・Spot）の `GetDescription` |
| enum の選択肢の説明 | 51 | enum の宣言の横の `EnumTips`（Components の各ヘッダ・`RenderStates.h`・`SceneBackground.h`・`EngineTime.h`・`Easing.h`・`HitShape.h`・`AudioBus.h`・`Curve.h`・`PhysicMaterialManager.h`・Timeline のトラック）。各ファイルに `#include "HTTEngine/Core/EnumTips.h"` も足した |
| 項目の Tip | 約 50 | 「A：〜 / B：〜」を消して、項目そのものの説明だけにした（例：`Tip("重ね方")`）。選択肢の説明は `EnumTips` へ移した |
| Sandbox | 1 | `CursorTrailSamples.cpp` の After Owner の Tip |
| Reference | 4 | `Docs/Reference.md`・`05-GameComponent.md`（説明を付ける表・`EnumTips` の書き方・だめな例）・`17-Editor.md`・`22-Script.md` |

### 0.8 やらないこと

- Settings ウィンドウ（`GlobalVariables::MakeEnumCombo` の Bloom の Mode など）の選択肢の説明。Inspector の外なので今回は入れない（`EnumTips` は書けば同じ表から読めるので、後で足せる）
- Script の Add Component のカテゴリ：`ScriptEditor::GetCategory` は前から `"Scripts"` を返していて、`SCRIPT(…, "Player")` のサブカテゴリは使われていない（コメントには「Gameplay → Player に出る」とある）。今回は触らない。`COMPONENT_CATEGORY` は COMPONENT の Gameplay の下だけに効く


## パートA enum の選択肢の説明

### A-1 `HTTEngine/Core/EnumTips.h`（新しい）

```cpp
#pragma once
#include <array>
#include <cstddef>

// enum の選択肢ごとの説明（Inspector で選択肢の一覧を開き、1つにマウスを乗せると出る）
// Unreal の UMETA(ToolTip = "...") と同じく、説明は「項目」ではなく「enum の型」に付ける
// （同じ enum を何か所で使っても、説明は1か所に書くだけで済む。項目そのものの説明は今までどおり ui.Field の Tip に書く）
//
//   書き方：enum の宣言のすぐ下に、同じ名前空間で EnumTips という関数を1つ書く（constexpr で、引数はその enum の型だけ）
//
//   enum class ParticleShape { Point, Sphere };
//   constexpr auto EnumTips(ParticleShape) {
//       using enum ParticleShape; // 中では ParticleShape:: を付けずに書ける（C++20）
//       return MakeEnumTips<ParticleShape>({
//           {Point, "Entity の位置から、全部の向きへ"},
//           {Sphere, "球の中から、外へ"},
//       });
//   }
//
//   ComponentUI は、引数の型から関数を探す決まり（ADL：引数の型が宣言された名前空間も探す）でこの関数を見つける。
//   だから登録は要らない。書かなければ、選択肢の説明は出ないだけ
//   書かなかった選択肢は説明なし。並びは宣言の順でなくてよい


/// <summary>
/// enum の選択肢1つの説明
/// </summary>
template<class E> struct EnumTip {
	E value;          // どの選択肢か
	const char* text; // 説明（Inspector で選択肢にマウスを乗せると出る）
};

// EnumTip の並びを作る（std::to_array と同じ。型を EnumTip<E> と書かずに、enum の型だけで書けるようにする）
// tips の数 N は、{...} の数からコンパイラが決める
template<class E, size_t N> constexpr std::array<EnumTip<E>, N> MakeEnumTips(const EnumTip<E> (&tips)[N]) {
	std::array<EnumTip<E>, N> result{};
	for (size_t i = 0; i < N; ++i) {
		result[i] = tips[i];
	}
	return result;
}
```

- `MakeEnumTips<ParticleShape>({...})` の `{...}` は、引数の `const EnumTip<E> (&tips)[N]`（N 個の配列への参照）に入る。E を書いてあるので、`{Point, "…"}` が `EnumTip<ParticleShape>` になり、N は `{...}` の数になる
- 返すのは `std::array`（関数から配列をそのまま返せないので）

### A-2 `HTTEngine/Core/EnumTipTable.h`（新しい）

```cpp
#pragma once
#include <array>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/EnumTips.h"

// enum の選択肢の説明（EnumTips.h）を、選択肢を出す側が読むための道具
// 書く側（enum の宣言の横）は EnumTips.h だけを include する（magic_enum を読まずに済むように、ファイルを分けている）


// E の選択肢の説明（EnumTips 関数）を書いてあるか
// 関数は、引数の型から探す決まり（ADL）で見つける。だから E の宣言の横に書いてあれば、どこにも登録しなくてよい
template<class E>
concept HasEnumTips = requires(E value) { EnumTips(value); };

/// <summary>
/// E の選択肢の説明を、選択肢の番号（magic_enum::enum_names の並び＝宣言の順）の並びにする。説明の無い選択肢は nullptr
/// <para>Inspector の enum の項目（ComponentUI）と EditorWidgets::EnumCombo が使う</para>
/// <para>constexpr なので、static constexpr の変数で受ければ、作るのはコンパイルのときだけ（毎フレーム作らない）</para>
/// </summary>
template<class E> constexpr std::array<const char*, magic_enum::enum_count<E>()> MakeEnumTipTable() {
	std::array<const char*, magic_enum::enum_count<E>()> texts{}; // 初めは全部 nullptr
	if constexpr (HasEnumTips<E>) {
		for (const EnumTip<E>& tip : EnumTips(E{})) {
			// magic_enum が知らない値（範囲の外の数など）は飛ばす
			if (const auto index = magic_enum::enum_index(tip.value)) {
				texts[*index] = tip.text;
			}
		}
	}
	return texts;
}
```

- `concept HasEnumTips`：「`EnumTips(value)` と書いてコンパイルできるか」を調べる。書いていない enum では false になり、`if constexpr` の中は作られない（コンパイルエラーにならない）

### A-3 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

`HTTEngine.vcxproj` の `HTTEngine\Core\FixedList.h` の行の前に2行：

```xml
    <ClInclude Include="HTTEngine\Core\EnumTips.h" />
    <ClInclude Include="HTTEngine\Core\EnumTipTable.h" />
```

`HTTEngine.vcxproj.filters` の `HTTEngine\Core\FixedList.h` の `<ClInclude>` の前に：

```xml
    <ClInclude Include="HTTEngine\Core\EnumTips.h">
      <Filter>HTTEngine\Core</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Core\EnumTipTable.h">
      <Filter>HTTEngine\Core</Filter>
    </ClInclude>
```

### A-4 `HTTEngine/ECS/ComponentUI.h`

include に1行（`FixedList.h` の前）：

```cpp
#include "HTTEngine/Core/EnumTipTable.h"
#include "HTTEngine/Core/FixedList.h"
```

enum の `Field` を、こう置き換える（`tips` を作って `EnumField` に渡す）：

```cpp
	// ===== enum（選択肢の名前は magic_enum が型から作る。保存も名前で行うので、並びを変えても読める）=====
	// 選択肢ごとの説明は、enum の型に EnumTips を書く（EnumTips.h）。style の Tip は項目そのものの説明
	template<class E>
	    requires std::is_enum_v<E>
	void Field(const char* label, E& value, FieldStyle style = {}) {
		constexpr auto names = magic_enum::enum_names<E>();                   // 選択肢の名前（宣言の順）
		static constexpr auto tips = MakeEnumTipTable<E>();  // 選択肢の説明（names と同じ並び）
		const auto current = magic_enum::enum_index(value); // 今の値が何番目か（magic_enumが知らない値なら無し）
		size_t index = current.value_or(0);
		const size_t before = index;
		EnumField(label, index, names, tips, style);
		// 変わったときだけ書き戻す（知らない値を、触っていないのに先頭の選択肢へ変えてしまわないように）
		if (index != before && index < names.size()) {
			value = magic_enum::enum_value<E>(index);
		}
	}
```

`protected:` の `EnumField` に `tips` を足す：

```cpp
	// enumの中身。何番目の選択肢かで受け渡す（names は選択肢の名前、tips は選択肢の説明で names と同じ並び。説明の無い物は nullptr）
	virtual void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, std::span<const char* const> tips, FieldStyle style) = 0;
```

- `static constexpr auto tips`：関数の中の static は、最初の1回だけ作られる。constexpr なので、作るのもコンパイルのとき
- `std::span<const char* const>`：「書き換えない `const char*` の並び」を、持ち主を決めずに渡す（`std::array` でも `std::vector` でも渡せる）

### A-5 `HTTEngine/Editor/Widgets/EditorWidgets.h`

include を足す（`<span>`・`<string_view>` と `EnumTipTable.h`）：

```cpp
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/EnumTipTable.h"
#include "HTTEngine/String/FileNameRule.h"
```

今の `EnumCombo` を、まるごとこう置き換える（吹き出しの道具・`EnumComboWithTips` を足して、`EnumCombo` はそれを呼ぶ）：

```cpp
// ===== 説明の吹き出し =====
inline constexpr float kTooltipWrapEm = 32.0f; // 吹き出しの文字を折り返す幅（文字の大きさの何倍か。長い説明が画面の端まで1行に伸びないように）

// 折り返す吹き出しを始める。true のときだけ中身を描いて、EndWrappedTooltip を呼ぶ
inline bool BeginWrappedTooltip() {
	if (!ImGui::BeginTooltip()) {
		return false;
	}
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipWrapEm);
	return true;
}
inline void EndWrappedTooltip() {
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

/// <summary>
/// 直前の項目にマウスを乗せていたら、text を折り返した吹き出しで出す（text が nullptr なら何もしない）
/// <para>flags は足す条件（押せない項目の上でも出すなら ImGuiHoveredFlags_AllowWhenDisabled）</para>
/// </summary>
inline void ItemTooltip(const char* text, ImGuiHoveredFlags flags = 0) {
	if (text != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | flags) && BeginWrappedTooltip()) {
		ImGui::TextUnformatted(text);
		EndWrappedTooltip();
	}
}

/// <summary>
/// enum の選択肢の一覧（説明付き）。Inspector の enum の項目と EnumCombo の中身
/// <para>names は選択肢の名前、tips は選択肢の説明（names と同じ並び。無い物は nullptr）、tooltip は項目そのものの説明（無ければ nullptr）</para>
/// <para>閉じているときは「項目の説明」と「今の選択肢の説明」、開いているときは「マウスを乗せた選択肢の説明」を出す</para>
/// <para>選び直したら index を変えて true</para>
/// </summary>
inline bool EnumComboWithTips(const char* label, size_t& index, std::span<const std::string_view> names, std::span<const char* const> tips, const char* tooltip = nullptr) {
	// string_view は末尾に0があるとは限らないので、ImGuiに渡す前に std::string にする
	const std::string current = index < names.size() ? std::string(names[index]) : std::string("?");
	if (!ImGui::BeginCombo(label, current.c_str())) {
		// --- 閉じた欄 ---（開いている間は出さない。選択肢の説明とぶつからないように）
		const char* currentTip = (index < tips.size()) ? tips[index] : nullptr;
		if ((tooltip != nullptr || currentTip != nullptr) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && BeginWrappedTooltip()) {
			if (tooltip != nullptr) {
				ImGui::TextUnformatted(tooltip);
			}
			if (currentTip != nullptr) {
				if (tooltip != nullptr) {
					ImGui::Separator();
				}
				ImGui::Text("%s：%s", current.c_str(), currentTip);
			}
			EndWrappedTooltip();
		}
		return false;
	}

	// --- 開いた一覧 ---
	bool changed = false;
	for (size_t i = 0; i < names.size(); ++i) {
		const std::string name(names[i]);
		if (ImGui::Selectable(name.c_str(), i == index)) {
			index = i;
			changed = true;
		}
		ItemTooltip((i < tips.size()) ? tips[i] : nullptr);
		if (i == index) {
			ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
		}
	}
	ImGui::EndCombo();
	return changed;
}

/// <summary>
/// enumをコンボボックスで選ばせる。選択肢の名前はmagic_enumが型から作り、説明は enum の型の EnumTips（EnumTips.h）を出す
/// <para>tooltip は項目そのものの説明（無ければ nullptr）</para>
/// </summary>
template<typename E> bool EnumCombo(const char* label, E& value, const char* tooltip = nullptr) {
	static_assert(std::is_enum_v<E>, "EnumCombo は enum 専用です");
	constexpr auto names = magic_enum::enum_names<E>();
	static constexpr auto tips = MakeEnumTipTable<E>(); // 選択肢の説明（names と同じ並び）
	size_t index = magic_enum::enum_index(value).value_or(0);
	if (!EnumComboWithTips(label, index, names, tips, tooltip)) {
		return false;
	}
	value = magic_enum::enum_value<E>(index);
	return true;
}
```

- 閉じた欄の吹き出しは、`BeginCombo` が false のとき（＝開いていない）だけ。`IsItemHovered` は直前の項目＝閉じた欄を見る
- `PushTextWrapPos` の数は「吹き出しの左からの幅」。`TextUnformatted`・`Text` は、これがあると折り返す

### A-6 `HTTEngine/ECS/ComponentUI.cpp`

`DrawTooltip` を、折り返す吹き出しにする：

```cpp
// 直前の項目にマウスを乗せたら説明を出す（長い説明は折り返す）
void DrawTooltip(const FieldStyle& style) { EditorWidgets::ItemTooltip(style.tooltip); }
```

Inspector の係（`InspectorComponentUI`）の `EnumField` を、まるごとこう置き換える：

```cpp
	// 閉じているときは項目の説明（Tip）と今の選択肢の説明、開いたら選択肢ごとの説明（EnumTips）を出す
	void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, std::span<const char* const> tips, FieldStyle style) override {
		EditorWidgets::EnumComboWithTips(label, index, names, tips, style.tooltip);
	}
```

同じファイルの下の方の、何もしない係の `EnumField`（1行の物）にも `std::span<const char* const>,` を足す：

```cpp
	void EnumField(const char*, size_t&, std::span<const std::string_view>, std::span<const char* const>, FieldStyle) override {}
```

### A-7 `HTTEngine/ECS/ComponentSerializer.cpp`

`EnumField` が5か所ある（書く係・読む係など）。どれも `names` の後ろに、名前の無い `std::span<const char* const>,` を足すだけ（保存・読み込みは説明を使わない）：

```cpp
	void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, std::span<const char* const>, FieldStyle) override {
```

```cpp
	void EnumField(const char*, size_t&, std::span<const std::string_view>, std::span<const char* const>, FieldStyle) override {}
```


## パートB Component の説明

### B-1 `HTTEngine/Editor/Inspector/ComponentEditor.h`（`GetDescription`）

「種類の情報」の `GetSourceFile` の次に1行：

```cpp
	virtual const char* GetSourceFile() const { return nullptr; } // 書いてあるファイル（COMPONENT(...)・SCRIPT(...) の物だけ。分からなければnullptr）
	virtual const char* GetDescription() const { return nullptr; } // 何をする Component かの説明（Inspector の見出し・Add Component の項目にマウスを乗せると出る。無ければnullptr）
```

### B-2 `HTTEngine/ECS/GameComponent.h`

`DescribedComponentEditor` の constructor・関数・変数を、こう置き換える（`description` を足す。`sourceFile` は後ろへ）：

```cpp
	// description は何をする Component かの説明、sourceFile は書いてあるファイル（どちらも Inspector の見出しにマウスを乗せると出る）。無ければ nullptr
	DescribedComponentEditor(const char* name, std::string category, DescribeFunction describe, const char* description = nullptr, const char* sourceFile = nullptr)
	    : name_(name), category_(std::move(category)), describe_(describe), description_(description), sourceFile_(sourceFile) {}

	const char* GetName() const override { return name_; }
	const char* GetCategory() const override { return category_.c_str(); }
	const char* GetSourceFile() const override { return sourceFile_; }
	const char* GetDescription() const override { return description_; }
```

```cpp
	DescribeFunction describe_; // ②に書いた見せ方
	const char* description_;   // 何をする Component かの説明（文字列リテラルなので、プログラムの終わりまで消えない）
	const char* sourceFile_;    // COMPONENT(...) を書いたファイル（コンパイラが決めた文字列なので、プログラムの終わりまで消えない）
```

`ComponentRegistrar` の constructor を、こう置き換える（**見せ方の関数を先頭へ**。後ろの「サブカテゴリ・説明」を書かなくてよいように）：

```cpp
	// 引数の並びは COMPONENT(型名, "サブカテゴリ", "説明") の並び（後ろの2つは書かなくてよいので、見せ方を先頭に置く）
	// description は Inspector の見出し・Add Component の項目にマウスを乗せると出る説明（書かなければ nullptr＝説明なし）
	// where は書かなくてよい。既定の引数は「呼んだ側」で作られるので、COMPONENT(...) を書いたファイルと行が入る
	ComponentRegistrar(void (*describe)(ComponentUI&, T&), const char* name, const char* subCategory, const char* description = nullptr,
	                   std::source_location where = std::source_location::current()) {
		const char* sourceFile = where.file_name();
		ComponentRegistryDetail::AddPending([describe, name, subCategory, description, sourceFile]() {
			EntityManager::RegisterComponent<T>();            // 置き場所
			ComponentSerializer::Register<T>(name, describe); // 保存・読み込み（Releaseでも要る）
			ComponentEditorRegistry::Register(                // Inspector
			    std::make_unique<DescribedComponentEditor<T>>(name, MakeGameplayCategory(subCategory), describe, description, sourceFile));
		});
	}
```

`COMPONENT` のマクロを、こう置き換える（`registrarOf_##Type(...)` の引数の並びが変わる）：

```cpp
/// <summary>
/// Componentの見せ方を書く。COMPONENT(型名)・COMPONENT(型名, "サブカテゴリ")・COMPONENT(型名, "サブカテゴリ", "説明")
/// <para>中では ui（見せ方の道具）と value（Componentの中身）が使える。カテゴリは Gameplay で固定</para>
/// <para>説明は Inspector の見出し・Add Component の項目にマウスを乗せると出る（サブカテゴリ無しで説明だけなら COMPONENT(型名, "", "説明")）</para>
/// </summary>
// [[maybe_unused]] は「使わなくても警告しないで」の印。中身の無いComponent（印だけのタグ）でも警告が出ないようにしている
// "" __VA_ARGS__ は、何も書かなければ ""、書けば "" "Movement", "説明" ＝ "Movement", "説明"（並んだ文字列はつながる）
#define COMPONENT(Type, ...)                                                                                                                                                                           \
	inline void Describe_##Type([[maybe_unused]] ComponentUI& ui, [[maybe_unused]] Type& value);                                                                                                       \
	inline const ComponentRegistrar<Type> registrarOf_##Type(&Describe_##Type, #Type, "" __VA_ARGS__);                                                                                                 \
	inline void Describe_##Type([[maybe_unused]] ComponentUI& ui, [[maybe_unused]] Type& value)
```

- 前は `(#Type, "" __VA_ARGS__, &Describe_##Type)` で、見せ方が最後だった。説明を足すと `"Movement", "説明", &Describe` になり、書かなかったときと引数の並びがずれる。見せ方を先頭にすれば、後ろの2つは既定の引数で埋まる

### B-3 `HTTEngine/ECS/Script.h`

`ScriptRegistrar` の constructor を、こう置き換える（`description` を `where` の前に）：

```cpp
	// subCategory は Add Component の Gameplay の下の分け方（空なら Gameplay の直下）
	// description は Inspector の見出し・Add Component の項目にマウスを乗せると出る説明（書かなければ nullptr＝説明なし）
	// where は書かなくてよい。既定の引数は「呼んだ側」で作られるので、SCRIPT(...) を書いたファイルと行が入る
	ScriptRegistrar(const char* name, const char* subCategory, const char* description = nullptr, std::source_location where = std::source_location::current()) {
		const char* sourceFile = where.file_name();
		ComponentRegistryDetail::AddPending([name, subCategory, description, sourceFile] {
			ScriptSystem::RegisterType(name, typeid(T), &Create, sourceFile, MakeGameplayCategory(subCategory), description);
		});
	}
```

`SCRIPT` のマクロは変えない（`"" __VA_ARGS__` の3つ目が、そのまま `description` に入る）。上のコメントだけ直す：

```cpp
/// Script を登録する。.cpp に SCRIPT(クラス名); か SCRIPT(クラス名, "サブカテゴリ"); か SCRIPT(クラス名, "サブカテゴリ", "説明"); と1行書く
/// <para>説明は Inspector の見出し・Add Component の項目にマウスを乗せると出る（サブカテゴリ無しで説明だけなら SCRIPT(クラス名, "", "説明")）</para>
```

```cpp
// "" __VA_ARGS__ は、サブカテゴリを書かなければ ""、書けば "" "Player" ＝ "Player"（並んだ文字列はつながる。COMPONENT と同じ）。説明はその後ろの引数になる
```

### B-4 `HTTEngine/Systems/Gameplay/ScriptSystem.h`・`.cpp`

`.h`：

```cpp
	// category は Add Component のカテゴリ（"Gameplay" か "Gameplay/サブカテゴリ"）、description は何をする Script かの説明（無ければ nullptr）
	static void RegisterType(const char* name, std::type_index type, CreateFunction create, const char* sourceFile, std::string category, const char* description);
```

`.cpp`：関数の頭と、最後の `ScriptEditor` を作る行：

```cpp
void ScriptSystem::RegisterType(const char* name, std::type_index type, CreateFunction create, const char* sourceFile, std::string category, const char* description) {
```

```cpp
	ComponentEditorRegistry::Register(std::make_unique<ScriptEditor>(name, type, sourceFile, std::move(category), description));
```

### B-5 `HTTEngine/Editor/Inspector/ScriptEditor.h`

constructor と「種類の情報」を、こう置き換える：

```cpp
	// category は "Gameplay" か "Gameplay/サブカテゴリ"（COMPONENT(...) と同じ所に並ぶ）
	// description は SCRIPT(...) に書いた説明（文字列リテラル。書かなければ nullptr）
	ScriptEditor(const char* name, std::type_index type, const char* sourceFile, std::string category, const char* description)
	    : name_(name), type_(type), sourceFile_(sourceFile), category_(std::move(category)), description_(description) {}

	// ===== 種類の情報 =====
	const char* GetName() const override { return name_; }
	const char* GetCategory() const override { return "Scripts"; }
	const char* GetSourceFile() const override { return sourceFile_; }
	const char* GetDescription() const override { return description_; }
```

変数に1行（`category_` の次）：

```cpp
	const char* description_; // 何をする Script かの説明（見出し・Add Component の項目にマウスを乗せると出る。無ければ nullptr）
```


## パートC カテゴリの説明

### C-1 `HTTEngine/Editor/Inspector/ComponentEditor.h`

include に `<string_view>` を足す：

```cpp
#include <span>
#include <string_view>
#include <type_traits>
```

上のコメントの最後の行を直す：

```cpp
// 上の段の並び順と説明は ComponentEditor.cpp の kTopLevelCategories が決める。そこに無い名前は後ろに回る
```

`ComponentEditorRegistry` の `GetAll` の次に：

```cpp
	// ===== カテゴリの説明（Add Component で、カテゴリにマウスを乗せると出る・右の列の上に出る）=====
	// 上の段（"Camera" など）の説明は ComponentEditor.cpp の kTopLevelCategories に書く。ここで足すのは、その下（"Gameplay/Movement" など）
	// path は "/" で区切ったカテゴリのパス。description はプログラムの終わりまで消えない文字（文字列リテラル）。同じ path に2回足したら後の物
	static void RegisterCategoryDescription(std::string_view path, const char* description);
	// path の説明（無ければ nullptr）
	static const char* FindCategoryDescription(std::string_view path);
```

### C-2 `HTTEngine/Editor/Inspector/ComponentEditor.cpp`

include：

```cpp
#include "ComponentEditor.h"

#include <array>
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
```

`kTopLevelOrder` を、名前と説明の表に置き換える（並びは今と同じ。表に無かった Animation・Combat・Scripts は、今と同じ所＝Gameplay の後に名前の順で足す）：

```cpp
/// <summary>
/// Add Component の上の段のカテゴリ1つ（名前と説明）
/// </summary>
struct TopLevelCategory {
	std::string_view name;   // カテゴリの名前（GetCategory の最初の "/" まで）
	const char* description; // Add Component でカテゴリにマウスを乗せると出る説明
};

// Add Component の上の段（この並びで出す）。ここに無い名前は後ろに回る
constexpr auto kTopLevelCategories = std::to_array<TopLevelCategory>({
    {"Core", "どの Entity も必ず持つ物（Transform）。Add Component には出ない"},
    {"Camera", "ゲームの絵を撮るカメラと、追いかけるなどのカメラの動き"},
    {"Rendering3D", "3D の見た目（モデル・霧・被写界深度など）"},
    {"Rendering2D", "3D の空間に置く画像の板など"},
    {"UI", "文字・図形など"},
    {"Lighting", "ライト"},
    {"Effects", "パーティクルや動いた跡の軌跡など"},
    {"Physics", "当たり判定や重力で落ちるなどの動き方など"},
    {"Audio", "音を鳴らす"},
    {"Gameplay", "ゲームの仕組みやCOMPONENT(...) で書いた物"},
    {"Animation", "モデルのアニメーション、時間に沿って値を動かす Timeline"},
    {"Combat", "攻撃の当たり・HP とダメージ・床の予告など"},
    {"Scripts", "SCRIPT(...) で書いた Script（Awake・Start・Update などを持つクラス）"},
});

// カテゴリの説明（上の段より下。"Gameplay/Movement" など。RegisterCategoryDescription で足す）
// std::less<> を付けると、std::string_view のままで探せる（探すたびに std::string を作らなくて済む）
std::map<std::string, const char*, std::less<>> categoryDescriptions;
```

`TopLevelOrderOf` の中の `kTopLevelOrder` を `kTopLevelCategories` に、比べる所を `.name` にする：

```cpp
	const std::string_view topLevel = TopLevelOf(path);
	for (size_t i = 0; i < std::size(kTopLevelCategories); ++i) {
		if (kTopLevelCategories[i].name == topLevel) {
			return i;
		}
	}
	return std::size(kTopLevelCategories);
```

ファイルの最後（`GetAll` の後）に：

```cpp
void ComponentEditorRegistry::RegisterCategoryDescription(std::string_view path, const char* description) {
	// insert_or_assign：無ければ足し、あれば書き換える
	categoryDescriptions.insert_or_assign(std::string(path), description);
}

const char* ComponentEditorRegistry::FindCategoryDescription(std::string_view path) {
	// --- 上の段は表から ---
	const size_t order = TopLevelOrderOf(path);
	if (order < std::size(kTopLevelCategories) && kTopLevelCategories[order].name == path) {
		return kTopLevelCategories[order].description;
	}
	// --- その下は足された物から ---
	const auto found = categoryDescriptions.find(path);
	return (found != categoryDescriptions.end()) ? found->second : nullptr;
}
```

- `std::map<…, std::less<>>`：`std::less<>`（型を書かない less）にすると、`find` に `std::string_view` をそのまま渡せる（「透過的な比較」）

### C-3 `HTTEngine/ECS/GameComponent.h`（`COMPONENT_CATEGORY`）

`COMPONENT` のマクロの後ろに：

```cpp
/// <summary>
/// COMPONENT_CATEGORY(...) が1つ作る物。作られた時点では「登録待ちの表」に並ぶだけ
/// </summary>
class ComponentCategoryRegistrar {
public:
	// subCategory は COMPONENT・SCRIPT に書くサブカテゴリと同じ書き方（"Movement" なら Gameplay → Movement）
	ComponentCategoryRegistrar(const char* subCategory, const char* description) {
		ComponentRegistryDetail::AddPending([subCategory, description] { ComponentEditorRegistry::RegisterCategoryDescription(MakeGameplayCategory(subCategory), description); });
	}
};

// 2つの字句をつなぐ（__LINE__ を先に数字にしてからつなぐため、2段にしている）
#define HTT_CONCAT_INNER(a, b) a##b
#define HTT_CONCAT(a, b) HTT_CONCAT_INNER(a, b)

/// <summary>
/// Add Component の Gameplay の下のカテゴリに説明を付ける。.cpp に COMPONENT_CATEGORY("Movement", "動かす物"); と1行書く
/// <para>Add Component でカテゴリにマウスを乗せると出る。同じカテゴリの COMPONENT のファイルのどれか1つに書けばよい</para>
/// </summary>
// static：このファイルの中だけの変数にする（別のファイルの同じ行に書いても、名前がぶつからない）
#define COMPONENT_CATEGORY(subCategory, description) static const ComponentCategoryRegistrar HTT_CONCAT(componentCategoryRegistrar_, __LINE__)(subCategory, description)
```

見出しのコメントも直す：

```cpp
// ゲーム側が書くのはこの2つだけ（カテゴリに説明を付けたいときは COMPONENT_CATEGORY も）
```

- 変数の名前を `__LINE__` で作るのは、1つのファイルに2つ書いても名前がぶつからないように。`a##b` に直接 `__LINE__` を渡すと、数字にする前につながって `componentCategoryRegistrar___LINE__` になるので、1段はさむ
- `inline` ではなく `static`：`inline` の変数は「全部のファイルで同じ1つ」になるので、別のファイルの同じ行に書くと、違う中身の同じ変数になってしまう


## パートD Inspector に出す

### D-1 `HTTEngine/Editor/Windows/InspectorWindow.cpp`

`CategorySegment` の後ろに、道具を3つ足す：

```cpp
// カテゴリのパスの、depth 段目までの部分（"Gameplay/Movement/Enemy" の1段目までは "Gameplay/Movement"）。カテゴリの説明を探すのに使う
std::string_view CategoryPrefix(std::string_view path, size_t depth) {
	size_t end = 0;
	for (size_t i = 0; i <= depth; ++i) {
		end = path.find('/', end);
		if (end == std::string_view::npos) {
			return path; // これより下の段は無い（全部）
		}
		if (i < depth) {
			++end; // "/" の次から、次の "/" を探す
		}
	}
	return path.substr(0, end);
}

// 直前の項目（Component の見出し・Add Component の項目）にマウスを乗せていたら、Component の説明と書いてあるファイルを出す
// flags は足す条件（押せない項目の上でも出すなら ImGuiHoveredFlags_AllowWhenDisabled）
void DrawComponentTooltip(const ComponentEditor& editor, ImGuiHoveredFlags flags = 0) {
	const char* description = editor.GetDescription();
	const char* file = editor.GetSourceFile();
	if ((description == nullptr && file == nullptr) || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | flags)) {
		return;
	}
	if (!EditorWidgets::BeginWrappedTooltip()) {
		return;
	}
	if (description != nullptr) {
		ImGui::TextUnformatted(description);
	}
	// ゲームの物は、書いてあるファイル（ゲームの Component はどこに置いてもよいので、探さなくて済むように）
	if (file != nullptr) {
		ImGui::TextDisabled("%s", ShortenSourcePath(file).c_str());
	}
	EditorWidgets::EndWrappedTooltip();
}

// 直前の項目（Add Component のカテゴリ）にマウスを乗せていたら、カテゴリの説明を出す
void DrawCategoryTooltip(std::string_view path) { EditorWidgets::ItemTooltip(ComponentEditorRegistry::FindCategoryDescription(path)); }
```

`DrawAddComponentItem` の最後の、ファイルを出す `if` を、こう置き換える：

```cpp
	// 説明と、ゲームの物は書いてあるファイルを出す（付いていて押せない物の上でも出す）
	DrawComponentTooltip(editor, ImGuiHoveredFlags_AllowWhenDisabled);
```

`DrawAddComponentLevel` の入れ子の `TreeNodeEx` を、こう置き換える（開いたかどうかを先に受けて、間でカテゴリの説明を出す）：

```cpp
		const bool isOpen = ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth);
		DrawCategoryTooltip(CategoryPrefix(items[index]->GetCategory(), depth)); // "Gameplay/Movement" の説明
		if (isOpen) {
			DrawAddComponentLevel(handle, items, index, groupEnd, depth + 1, filter);
			ImGui::TreePop();
		}
```

`DrawCategoryColumn` の `Selectable` の `if` の後ろに1行：

```cpp
			DrawCategoryTooltip(category.name);
```

`DrawComponentColumn` の、検索していないときの `else` を、こう置き換える：

```cpp
		} else {
			// 選んでいるカテゴリの説明を、一覧の上に灰色で（何のカテゴリかが、マウスを乗せなくても分かるように）
			if (const char* description = ComponentEditorRegistry::FindCategoryDescription(selected->name); description != nullptr) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", description);
				ImGui::PopStyleColor();
				ImGui::Separator();
			}
			DrawAddComponentLevel(handle, items, selected->begin, selected->end, 1, filter);
		}
```

`DrawComponent`（Component1つ分の区画）の、見出しにファイルを出す `if` を、こう置き換える：

```cpp
	// --- 見出しにマウスを乗せると、何をする Component かの説明と、書いてあるファイル（ゲームの物だけ）を出す ---
	DrawComponentTooltip(editor);
```

- 灰色の折り返す文字は `TextDisabled` に折り返しが無いので、文字の色を灰色にしてから `TextWrapped` で描く


## パートE 確かめ方

### E-1 ビルド

パートA〜D を全部写してから、Debug・Development・Release でビルドする。エラー・警告 0 になるはず（本体の説明文と合わせて、写す前の形で確かめた）。

よくある写し違い：

| エラー | 見る所 |
|---|---|
| `EnumTips.h` が無い | A-1・A-3（ファイルを足して、vcxproj にも） |
| `GetDescription`：基底クラスのメンバーを上書きしません | B-1 |
| `DescribedComponentEditor` の constructor の引数が多い | B-2 |
| `COMPONENT` で「関数のポインタを const char* に変換できません」 | B-2 のマクロの並び（`&Describe_##Type` が先頭） |
| `EnumField`：抽象クラスのまま | A-6・A-7（5＋2か所すべてに `std::span<const char* const>,`） |

### E-2 Sandbox で見る（まだ目で見ていない）

1. Development で Sandbox を起動する
2. Particle System の付いた Entity を選び、**Shape** の欄にマウスを乗せる → 「粒が出る場所と、飛ぶ向き」と、線の下に「Point：Entity の位置から、全部の向きへ」（今の選択肢）
3. Shape の欄を開いて、**Cone** に乗せる → 「XZ の面の円から、上（+Y）を中心に Cone Angle まで開いて」
4. 長い説明（Camera Orbit の Heading の LookAt など）が、吹き出しの中で折り返されている
5. Component の見出し（「Camera」など）に乗せる → 何をする Component かの説明。Sandbox の Script の見出しは、説明は無く、ファイルの場所だけ（前と同じ）
6. **Add Component** を開く → 右の列の一番上に、選んでいるカテゴリの説明が灰色で出る。左のカテゴリ・右の Component に乗せると説明（付いていて押せない物の上でも出る）
7. Model Renderer の **Shading**・**Blend** の欄も、開いて乗せると選択肢の説明が出る（`EditorWidgets::EnumCombo`）

### E-3 答えの差分

作業のときの scratchpad（セッションの物なので消えうる）に残した：

- `C:\Users\K025G\AppData\Local\Temp\claude\0d92a1827ecc6639\7f6a3a39-7a6d-4e2c-b34d-b7fa4bd52a86\scratchpad\answer-mechanism.patch`：この md のパートA〜D の答え（今の本体に `git apply --check` で当たることを確かめた）
- 同じ所の `answer-all.patch`：仕組みと説明文を全部入れた形（ビルドを確かめた形）

この md の「足す行」は、`answer-mechanism.patch` の足す行と1行ずつ突き合わせて、抜けが無いことを確かめた。
