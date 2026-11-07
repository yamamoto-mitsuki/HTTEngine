参考：
- Unity の List を Inspector に出す（SerializeField・配列の要素の足し方・並べ替え）：https://docs.unity3d.com/Manual/class-ScriptableObject.html ・ https://docs.unity3d.com/ScriptReference/SerializedProperty.html
- Unity の Prefab の上書き（配列は要素の番号ごと）：https://docs.unity3d.com/Manual/PrefabInstanceOverrides.html
- Godot の @export の Array（Inspector の一覧・上書きは配列を丸ごと）：https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html
- JSON Merge Patch（RFC 7386。配列は丸ごと置き換える）：https://datatracker.ietf.org/doc/html/rfc7386
- `std::rotate`：https://en.cppreference.com/w/cpp/algorithm/rotate
- `std::move_backward`：https://en.cppreference.com/w/cpp/algorithm/move_backward
- Dear ImGui のドラッグ＆ドロップ（BeginDragDropSource・AcceptDragDropPayload）：https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp （"Drag and Drop" の所）
- 前の md：Docs/Tasks/Serialize.md（ui.Field の並びが保存の項目になる仕組み）・Docs/Tasks/PrefabOverride.md（置いた所ごとの上書き）

# Inspector で数を増やしたり減らしたりできる一覧（ListField）

Unity の List のように、**Inspector で足す・消す・並べ替えができて、シーン・Prefab に保存できる一覧**をエンジンに足す。

```cpp
// Script（std::vector）
ui.ListField("Stages###stages", stages_, &DescribeStage, &MakeStageTitle, Tip("距離の段階（近い順に並べる）"));
// データの Component（FixedList。上限の数が決まった一覧）
COMPONENT(LockOnSettings) { ui.ListField("Stages###stages", value.stages, &DescribeStage, &MakeStageTitle); }
```

要素の中身は、今までの `ui.Field` などで書く（要素1つの見せ方を1つの関数に書き、全部の要素に使い回す）。

- **パートA FixedList**（新しい `Core/FixedList.h`）：データの Component に持たせられる、上限の数が決まった一覧
- **パートB ComponentUI**（`ECS/ComponentUI.h`）：`ui.ListField(...)` と、要素の型を知らずに一覧を扱う窓口 `ListAccess`
- **パートC 保存・読み込み・参照の付け替え**（`ECS/ComponentSerializer.cpp`）：JSON の配列で書く・読む。要素の中の Entity への参照も、Prefab から作るときに付け替える
- **パートD Inspector**（`ECS/ComponentUI.cpp`）：折りたためる見出し・要素の見出し・つまみのドラッグで並べ替え・右クリック・＋ と －
- **パートE Sandbox**：見本の Script（入れ子の一覧・参照の一覧も）とデータの Component、テストシーン ListFieldTest

パートは **A → B → C → D → E の順に写す**。A の後・B の後・C の後・D の後の、どこでもビルドできる（B の後は、まだ Inspector に出ず保存もされないが、ビルドは通る）。

ブランチは `feature/ListField`（develop e3e8343 から）。UI World Follow（Docs/Tasks/UIWorldFollow.md）とは別の機能なので、どちらを先に写してもよい。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドでエラー・自分のファイルの警告 0。自動テスト 35 項目（FixedList 7・並べ替え 8・Script の保存と読み込み 9・データの Component 5・参照の付け替えと Prefab 6）が Debug・Development で合格。エディタで ＋・Undo・つまみのドラッグ・右クリックの Move Up・上限・ほかの一覧へ落とせないこと・入れ子の ＋ を操作して画面を撮って確かめた。Release でテストシーンを読み、一覧の値で動くこと（ログ）も確かめた。

## 0章 全体の形

### 0.1 何が困っていたか

- ComponentUI（`ui.Field` の仕組み）には float・int・bool・Vector・EntityRef・文字・enum・アセット・色・レイヤーしか無く、**std::vector を Inspector に出して保存する道が無い**
- 一覧が要る物は、それぞれ専用の作り方をしている：攻撃表は専用のエディタとファイル、Polyline の点は `std::array` と `pointCount` を手で書いて、点ごとに Field を並べている
- これから使う所：ロックオンの段階の一覧（要素＝{ 最大の距離, ダメージの倍率 }）、マークの段階ごとの見た目、ダメージの数字の段階

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Core/FixedList.h`（新しい） | 上限の数が決まった一覧。memcpy で写せる |
| B | `ECS/ComponentUI.h` | `ListAccess`（一覧の窓口）・`ComponentUIDetail::ListAccessOf`（std::vector と FixedList の違いを吸収）・`ui.ListField(...)`・係が上書きする `ListFieldErased` |
| C | `ECS/ComponentSerializer.cpp` | 書く係・読む係・参照を付け替える係の `ListFieldErased` |
| D | `ECS/ComponentUI.cpp` | Inspector に描く係の `ListFieldErased`（見出し・つまみ・右クリック・＋ －） |
| A | `MyEngine_v1.vcxproj`・`.filters` | `FixedList.h` を足す |
| E | `Sandbox/Source/Scripts/ListFieldSample.h`・`.cpp`（新しい） | 見本の Script と データの Component |
| E | `Sandbox/Resources/scenes/ListFieldTest.scene.json`（新しい） | テストシーン |
| E | `Sandbox/Sandbox.vcxproj`・`.filters` | 上の2つを足す |

Timeline の値を書く係・集める係（`ValueWriterUI`・`ValueListUI`）と Release の Inspector の係は、**直さない**（ComponentUI の `ListFieldErased` の既定が「何もしない」なので、一覧の中の値は扱わない）。Prefab の差分（`PrefabDiff`）も**直さない**（今の merge_patch のままで、一覧が丸ごとの上書きになる。1.5）。

### 0.3 使い方

**Script**（std::vector。上限なし）

```cpp
struct DistanceStage {
	float maxDistance = 10.0f; // この段階になる、相手までの距離の上限（m）
	float damageScale = 1.0f;  // この段階のダメージの倍率
};

// 要素1つの見せ方（COMPONENT の中と同じ書き方）
void DescribeStage(ComponentUI& ui, DistanceStage& stage) {
	ui.Field("Max Distance###maxDistance", stage.maxDistance, AtLeast(0.0f).Tip("この段階になる、相手までの距離の上限（m）"));
	ui.Field("Damage Scale###damageScale", stage.damageScale, AtLeast(0.0f).Drag(0.01f).Tip("この段階のダメージの倍率"));
}
// 見出しの文字（無くてもよい。折りたたんだままでも中身が分かるように）
std::string MakeStageTitle(const DistanceStage& stage) { return std::format("〜{:.1f}m  ×{:.2f}", stage.maxDistance, stage.damageScale); }

void LockOn::Fields(ComponentUI& ui) {
	ui.ListField("Stages###stages", stages_, &DescribeStage, &MakeStageTitle, Tip("距離の段階（近い順に並べる）"));
}
```

- 見出しの関数を省いた `ui.ListField("Stages###stages", stages_, &DescribeStage, Tip(...))` も書ける（見出しは番号だけ）
- 見せ方はラムダでもよい。**要素の中に一覧**があるときは、見せ方の中でもう一度 `ui.ListField` を呼ぶ
- `GameObject`・`EntityRef` の一覧も書ける（`ui.Field("Target###target", target)` を要素の見せ方に書く）。Prefab から作ると、Prefab の中の相手に付け替わる

**データの Component**（FixedList。上限の数が決まっている）

```cpp
struct LockOnSettings {
	FixedList<DistanceStage, 4> stages = {{10.0f, 1.0f}, {20.0f, 1.5f}}; // 距離の段階（4つまで）
};
COMPONENT(LockOnSettings) { ui.ListField("Stages###stages", value.stages, &DescribeStage, &MakeStageTitle, Tip("距離の段階（4つまで）")); }
```

- 要素は memcpy で写せる型だけ（`std::string` は `FixedString` に、一覧の中の一覧は FixedList に）
- ゲームのコードでは `stages.Size()`・`stages[i]`・`for (DistanceStage& stage : stages)`・`PushBack`・`Erase` で使う

**保存の形**（Script もデータの Component も同じ）

```json
"stages": [
	{"maxDistance": 10.0, "damageScale": 1.0},
	{"maxDistance": 20.0, "damageScale": 1.5}
]
```

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| md の分け方 | **ListField.md と UIWorldFollow.md の2つ**（ユーザーが決めた） | 層が違い（ECS と保存／UI とカメラ）、互いに使わない。コミットも確認も別にできる |
| データの Component の一覧 | **FixedList&lt;T, N&gt; も足す**（ユーザーが決めた）。Script は std::vector、Component は FixedList、どちらも同じ `ui.ListField` | 保存の形が同じなので、Component を Script に移しても（逆でも）ファイルがそのまま読める。今の Polyline の「std::array と数」を手で書く形を、これからは書かなくて済む |
| Prefab の置いた所ごとの上書き | **一覧を丸ごと**（ユーザーが決めた）。1つでも変えたら一覧の全部が上書き | 今の merge_patch のまま（配列は丸ごと置き換える）。要素の番号ごとにすると、Prefab の頭に1つ足したときに、上書きが1つ後ろの要素に当たる（Unity でよくある事故）。足す・消す・並べ替えで上書きがずれることが絶対に無い |
| 要素の見出し | **番号＋書いた人が決める文字**（ユーザーが決めた）。関数を渡さなければ番号だけ | Unity は「先頭の文字の項目」を勝手に使うが、数だけの要素では効かない。折りたたんだままでも中身が分かる |
| ＋ ボタン | **最後の要素を写して足す**（空なら初期値）。Unity と同じ | 段階の一覧のように、前の要素と似た値を続けて入れることが多い。初期値で足したいときは右クリックの Insert Above |
| 並べ替え | 要素の左の**つまみをドラッグ**して、行き先の要素の見出しに落とす。右クリックの Move Up・Move Down でもできる | 同じ一覧の中だけ（ほかの一覧・ほかの Component には落とせない。落とせる色も出さない） |
| 読み込み | **一覧を丸ごと置き換える**。ファイルの数に合わせて、要素は初期値から読む | 要素に無い項目は、前の値ではなくその要素の初期値（Undo で数が変わっても、前の要素の値が残らない） |
| 上限を超えるファイル | 入る分だけ読み、**警告**（後ろを捨てる） | FixedList の N を減らしたときに気づけるように |
| Timeline の値のトラック | 一覧の中の値は**書けない**（Timeline の項目の一覧に出ない） | 保存の形を変えずに、後から「Stages[1]/Max Distance」のような名前で足せる（3章） |
| Undo | 何もしなくてよい | Inspector は「描く前の写し」と「描いた後の写し」を比べて Undo に積む。足す・消す・並べ替えも、そのまま1回の Undo になる |

### 0.5 ComponentUI の係ごとの扱い

`ui.ListField` は、どの係にも同じ `ListAccess`（一覧の窓口）を渡す。係が `ListFieldErased` を上書きして、自分の仕事をする。

| 係 | どこ | 一覧のときにすること |
|---|---|---|
| Inspector に描く係 | ComponentUI.cpp | 見出し・要素の見出し・中身・つまみ・右クリック・＋ －。操作は描き終わってからする（1.4） |
| 書く係（保存・Undo の写し・Prefab） | ComponentSerializer.cpp | 要素ごとに新しい書く係を作って `{...}` を書き、`[...]` にまとめる |
| 読む係（読み込み・Undo で戻す） | ComponentSerializer.cpp | 一度空にしてからファイルの数にして、要素ごとに新しい読む係で読む |
| 参照を付け替える係（Prefab を開く） | ComponentSerializer.cpp | 要素ごとに、`{...}` の中の Entity の番号を付け替える |
| Timeline の値を書く係・集める係 | ComponentSerializer.cpp | 何もしない（既定のまま） |
| Release の Inspector の係 | ComponentUI.cpp | 何もしない（既定のまま） |

要素ごとに**新しい係を作る**のは、JSON の `{...}` が要素ごとに別だから。警告に出す名前は `LockOn.stages[1]` のように、どの要素かが分かる形にする。

### 0.6 今回やらないこと

3章にまとめた（Timeline から要素の値を動かす・Polyline の点を FixedList にする・要素ごとの Prefab の上書き・数を打ち込む欄 など）。

## 1章 解説

### 1.1 ListAccess（要素の型を消す）にした理由

`ComponentUI` の係は virtual 関数で差し替える仕組み。**virtual 関数はテンプレートにできない**（要素の型ごとに別の関数を作ることができない）ので、`virtual void ListField(std::vector<T>&)` とは書けない。

そこで、一覧に対してする操作（数を見る・数を変える・足す・消す・動かす・要素1つの見せ方を呼ぶ・見出しを作る）だけを `ListAccess` という形にそろえる。テンプレートの `ui.ListField` が、要素の型を知っている `ListAccessOf<Container, Describe, Title>` を作って、係の `ListFieldErased(label, ListAccess&)` に渡す。係は要素の型を知らないまま、`DescribeElement(ui, i)` で「要素 i の見せ方」を自分に向けて呼べる。

これは今の enum（`Field<E>` が名前の一覧にして `EnumField` に渡す）や、`ComponentSerializer::RegisterErased`（Script の型を消して関数で持つ）と同じ考え方。

### 1.2 `std::rotate`（あまり使わない STL）

`std::rotate(first, middle, last)` は、`[first, last)` を「`middle` が先頭に来るように」回す。

```
[A B C D E] を rotate(A, B, D の次) → [B C D A E]   （A を D の位置へ。B〜D が1つ前へ）
[A B C D E] を rotate(B, D, D の次) → [A D B C E]   （D を B の位置へ。B〜C が1つ後ろへ）
```

`Move(from, to)` は、from を後ろへ動かすときは `rotate(from, from+1, to+1)`、前へ動かすときは `rotate(to, from, from+1)`。消して入れ直すのと違い、std::vector でもメモリを取り直さず、FixedList（中は std::array）でも同じ書き方で動く。

`FixedList::Insert` の `std::move_backward(first, last, dest)` は、`[first, last)` を**後ろの端から**1つずつ `dest` の手前へ写す。前から写すと、まだ写していない要素を上書きしてしまう（1つ後ろへずらすときは後ろから）。

### 1.3 FixedList の使っていない所を、いつも初期値にする理由

FixedList は中に `std::array<T, N>` と数を持つ。Undo は Component を**バイトのまま**比べる（データの Component の写しは memcpy）。消した要素の値が配列の後ろに残っていると、中身が同じ一覧でもバイトが違い、「変わった」と見えて余計な Undo が積まれる。

そこで `PopBack`・`Erase`・`Resize` で減った所は、必ず `T{}` に戻す。こうすると、**同じ中身ならバイトも同じ**になる（FixedString の「残りを 0 で埋める」と同じ）。

### 1.4 Inspector で、描き終わってから操作する理由

ImGui は描きながらボタンを押したかが分かる。要素 1 の右クリックで「Remove」を押したその場で消すと、残りの要素の番号が1つずつずれて、同じフレームの後ろの描き方がずれる（要素 2 のつもりで要素 3 を描く・数より先を読む）。

そこで、押された操作を `ListEdit` に覚えておき、**全部の要素を描き終わってから**1回だけする。操作は1フレームに1つ（2つ同時には押せない）。

足した要素は、次のフレームから見出しを開いておく（`OpenListElement`）。ImGui は見出しの開け閉めを **ID ごと**に覚えているので、その要素の見出しの ID（番号の `PushID` の中の `"##element"`）に「開いている」を書いておく。

### 1.5 Prefab の上書きが「一覧を丸ごと」になる仕組み

置いた所ごとの上書きは、Prefab の JSON とシーンの JSON の違いを **merge_patch**（RFC 7386）の形で持つ（`PrefabDiff::MakePatch`）。merge_patch は `{...}` は項目ごとに比べるが、**配列は1つでも違えば丸ごと置き換える**。一覧は JSON の配列なので、何も直さなくても「一覧を丸ごと上書き」になる。

| 場面 | 結果 |
|---|---|
| 置いた所で段階 1 の倍率だけ変えた | 上書きは `"stages": [段階 0, 段階 1, 段階 2]`（丸ごと） |
| その後、Prefab の段階 2 の距離を変えた | 置いた所には**届かない**（一覧ごと上書きしているので） |
| その後、Prefab の別の項目（Target Distance など）を変えた | 置いた所に届く（一覧の外の項目は、今までどおり項目ごと） |
| 置いた所で一覧を Prefab と同じに戻した | 上書きが消える（同じなら差分が無い） |

Prefab の一覧を置いた所に届けたいときは、置いた所の上書きを Revert する（Prefab の値に戻る）。

## パートA FixedList

### A-1 `Core/FixedList.h`（新しい）

```cpp
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <type_traits>
#include <initializer_list>


/// <summary>
/// 上限の数が決まった一覧。データの Component に一覧を持たせるときに使う（Script は std::vector でよい）
/// <para>Component は memcpy で写せる型しか持てない。std::vector は中で別の場所のメモリを指しているので持てない。
/// これは要素を自分の中の配列に入れるので、写すだけで中身ごと写る（Undo・Prefab・Duplicate がそのまま使える）。FixedString と同じ考え方</para>
/// <para>Inspector・保存は ui.ListField(...) で std::vector と同じに書ける（ファイルの形も同じ JSON の配列）</para>
/// <para>使っていない所（Size() から後ろ）は、いつも初期値（T{}）にしておく。同じ一覧なら配列の中身も全部同じになるので、Undo が「変わったか」をバイトで比べても正しく見える</para>
/// </summary>
template<class T, size_t N>
class FixedList {
	static_assert(std::is_trivially_copyable_v<T>, "FixedList の要素は、memcpy で写せる型にしてください（std::string・std::vector を持たない struct）");
	static_assert(std::is_default_constructible_v<T>, "FixedList の要素は、引数の無いコンストラクタで作れるようにしてください（足したときの初期値）");
	static_assert(N >= 1, "FixedList は1つ以上入る大きさにしてください");

public:
	FixedList() = default;
	// { {20.0f, 1.0f}, {40.0f, 1.5f} } のように、最初の中身を書ける（入りきらない分は捨てる）
	FixedList(std::initializer_list<T> items) {
		for (const T& item : items) {
			if (!PushBack(item)) {
				break;
			}
		}
	}

	// ===== 数 =====
	size_t Size() const { return size_; }
	bool Empty() const { return size_ == 0; }
	bool IsFull() const { return size_ == N; }
	// 入る数の上限
	static constexpr size_t Capacity() { return N; }

	// ===== 要素（index は Size() より小さいこと。範囲の外は読まない・書かない）=====
	T& operator[](size_t index) { return items_[index]; }
	const T& operator[](size_t index) const { return items_[index]; }
	T& Front() { return items_[0]; }
	const T& Front() const { return items_[0]; }
	T& Back() { return items_[size_ - 1]; }
	const T& Back() const { return items_[size_ - 1]; }

	// range-based for で回せるように（for (Stage& stage : stages)）。名前は C++ の決まりで小文字
	T* begin() { return items_.data(); }
	T* end() { return items_.data() + size_; }
	const T* begin() const { return items_.data(); }
	const T* end() const { return items_.data() + size_; }

	// ===== 足す・消す =====
	// 最後に足す（一杯なら足さずに false）
	bool PushBack(const T& item) {
		if (IsFull()) {
			return false;
		}
		items_[size_] = item;
		++size_;
		return true;
	}

	// 最後を消す（空なら何もしない）
	void PopBack() {
		if (size_ > 0) {
			--size_;
			items_[size_] = T{};
		}
	}

	// index の所に入れて、後ろを1つずつずらす（一杯・index が Size() より大きいなら、入れずに false）
	bool Insert(size_t index, const T& item) {
		if (IsFull() || index > size_) {
			return false;
		}
		// std::move_backward：[index, size_) を、後ろの端から1つ後ろへずらす（前から写すと、写す前の要素を上書きしてしまう）
		std::move_backward(items_.begin() + index, items_.begin() + size_, items_.begin() + size_ + 1);
		items_[index] = item;
		++size_;
		return true;
	}

	// index を消して、後ろを1つずつ詰める（範囲の外なら何もしない）
	void Erase(size_t index) {
		if (index >= size_) {
			return;
		}
		std::move(items_.begin() + index + 1, items_.begin() + size_, items_.begin() + index);
		PopBack(); // 詰めた後の最後（前の最後の写し）を初期値に戻す
	}

	// 数を変える（増えた分は初期値。上限より多くは増やさない）
	void Resize(size_t count) {
		count = (std::min)(count, N);
		// 減った分は初期値に戻す（使っていない所はいつも初期値なので、増えた分はもう初期値になっている）
		if (count < size_) {
			std::fill(items_.begin() + count, items_.begin() + size_, T{});
		}
		size_ = static_cast<uint32_t>(count);
	}

	void Clear() { Resize(0); }

private:
	std::array<T, N> items_{}; // 要素（使うのは先頭から size_ 個）
	uint32_t size_ = 0;         // 入っている数
};
```

### A-2 `MyEngine_v1.vcxproj`・`MyEngine_v1.vcxproj.filters`

Visual Studio のソリューションエクスプローラーで、MyEngine の Core フィルターに `FixedList.h` を足してもよい。手で書くなら：

`MyEngine_v1.vcxproj`（`MyEngine\Core\NameList.h` の前）

```xml
    <ClInclude Include="MyEngine\Core\FixedList.h" />
```

`MyEngine_v1.vcxproj.filters`（`MyEngine\Core\NameList.h` の前）

```xml
    <ClInclude Include="MyEngine\Core\FixedList.h">
      <Filter>MyEngine\Core</Filter>
    </ClInclude>
```

## パートB ComponentUI

### B-1 `ECS/ComponentUI.h`

**include** に `<algorithm>`・`<utility>`・`<vector>` と FixedList を足す（標準ライブラリの中は名前順）。

```cpp
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
```

**前方宣言**に `ComponentUI` を足す（`ListAccess` が `ComponentUI&` を受け取るので、`ComponentUI` より前に名前だけ知らせる）。

```cpp
// 前方宣言
struct GameObject;
class NameList;
class ComponentUI;
```

`IsAssetFile` の宣言の後・`ComponentUI` の説明（`/// <summary>`）の前に、`ListAccess` と `ComponentUIDetail` を足す。

```cpp
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
```

- `if constexpr`：条件が false の側は、その型ではコンパイルしない。std::vector に `Size()` は無いが、std::vector のときは `list_.size()` の側だけが使われるのでエラーにならない
- `std::as_const`：`const` を付けた参照にする。見出しの関数に、要素を書き換えられない形で渡す

`ComponentUI` の中、enum の `Field` の後・`// ===== アセット` の前に `ListField` を足す。

```cpp
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
```

- `requires`：この条件に合う型のときだけ、この関数を候補にする。`ui.ListField("A", list, describe, Tip("…"))` は4つ目が `FieldStyle` で「要素を渡して呼べる関数」ではないので、見出しの無い方が選ばれる

`protected:` の `EnumField` の後に、係が上書きする `ListFieldErased` を足す。

```cpp
protected:
	// enumの中身。何番目の選択肢かで受け渡す（names は選択肢の名前）
	virtual void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle style) = 0;
	// 一覧の中身（list で数・並び・要素を扱う）。何もしない係（Timeline の値を書く・集める係）は、そのまま（一覧の中の値は扱わない）
	virtual void ListFieldErased(const char* label, ListAccess& list, FieldStyle style) {
		(void)label;
		(void)list;
		(void)style;
	}
```

## パートC 保存・読み込み・参照の付け替え

### C-1 `ECS/ComponentSerializer.cpp`

`KeyOf` の後に、警告に出す要素の名前を作る関数を足す。

```cpp
std::string KeyOf(const char* label) { return std::string(KeyViewOf(label)); }

// 一覧の要素1つの、警告に出す名前（"LockOn.Stages[1]"。どの要素の項目がおかしいか分かるように）
std::string MakeElementName(std::string_view typeName, const char* label, size_t index) { return std::format("{}.{}[{}]", typeName, KeyViewOf(label), index); }
```

**書く係**（`JsonWriterUI`）の `EnumField` の後（`private:` の前）に足す。

```cpp
	// 一覧は [{...}, {...}] で書く（要素1つの見せ方を、要素ごとに別の書く係へ通す）
	void ListFieldErased(const char* label, ListAccess& list, FieldStyle) override {
		SceneJson items = SceneJson::array();
		for (size_t i = 0; i < list.GetSize(); ++i) {
			SceneJson fields = SceneJson::object();
			const std::string elementName = MakeElementName(typeName_, label, i); // 警告に出す名前（"LockOn.Stages[1]"）。書く係より長く持つ
			JsonWriterUI writer(fields, elementName, warnings_, idMap_);
			list.DescribeElement(writer, i);
			items.push_back(std::move(fields));
		}
		Put(label, std::move(items));
	}
```

- `elementName` を書く係より先に作るのは、書く係が名前を `std::string_view`（文字を指すだけ）で持つから。書く係より先に消えると、指す先が無くなる

**読む係**（`JsonReaderUI`）の `EnumField` の後（`private:` の前）に足す。

```cpp
	// 一覧は、ファイルの数に合わせ直してから、要素ごとに別の読む係で読む
	// 前の中身は残さない（一覧を丸ごと置き換える。要素に無い項目は、その要素の初期値）
	void ListFieldErased(const char* label, ListAccess& list, FieldStyle) override {
		const SceneJson* items = Find(label, &SceneJson::is_array);
		if (items == nullptr) {
			return; // 無い（後から足した一覧）なら、初期値の一覧のまま
		}
		size_t count = items->size();
		if (count > list.GetCapacity()) {
			warnings_.insert(std::format("{}: \"{}\" はファイルに {} 個ありますが、{} 個までしか入りません（後ろを捨てます）", typeName_, KeyOf(label), count, list.GetCapacity()));
			count = list.GetCapacity();
		}
		list.Resize(0); // 一度空にしてから増やす（残っていた要素の値を、次の要素の初期値にしないように）
		list.Resize(count);
		for (size_t i = 0; i < count; ++i) {
			const SceneJson& fields = (*items)[i];
			const std::string elementName = MakeElementName(typeName_, label, i);
			if (!fields.is_object()) {
				warnings_.insert(std::format("{}: {{ }} の形ではないので、初期値にします", elementName));
				continue;
			}
			JsonReaderUI reader(fields, elementName, warnings_, idMap_);
			list.DescribeElement(reader, i);
			reader.WarnUnused();
		}
	}
```

- `idMap_` を要素の読む係にも渡すので、Prefab から作るとき、要素の中の参照（`GameObject`・`EntityRef`）も Prefab の中の相手に付け替わる
- `reader.WarnUnused()`：要素の中の、読む所が無い項目を知らせる（要素の項目の名前を変えたとき）

**参照を付け替える係**（`RefRemapUI`）の `EnumField` の後（`private:` の前）に足す。

```cpp
	// 一覧は、要素ごとに別の付け替える係で、要素の {...} の中の参照を付け替える
	// list は写し（初期値か、使い捨ての Script）なので、ファイルと同じ数に合わせてよい（要素の中で行の分かれ方に使う値も、ファイルから読む）
	void ListFieldErased(const char* label, ListAccess& list, FieldStyle) override {
		const auto it = fields_.find(KeyOf(label));
		if (it == fields_.end() || !it->is_array()) {
			return;
		}
		SceneJson& items = it.value();
		const size_t count = (std::min)(items.size(), list.GetCapacity());
		list.Resize(0);
		list.Resize(count);
		for (size_t i = 0; i < count; ++i) {
			if (items[i].is_object()) {
				RefRemapUI remapper(items[i], map_);
				list.DescribeElement(remapper, i);
			}
		}
	}
```

- `(std::min)` とかっこで囲むのは、Windows.h の min マクロに邪魔されないため

## パートD Inspector

### D-1 `ECS/ComponentUI.cpp`

`// ===== クリップの名前の欄 =====` の `kMissingClipColor` の後（`InspectorComponentUI` の説明の前）に足す。

```cpp
// ===== 一覧 =====
constexpr const char* kListDragType = "MYENGINE_LIST_ITEM"; // 一覧の要素を運ぶドラッグの種類（ImGui の決まりで 32 文字まで）
constexpr float kListGripWidthRate = 0.75f;                  // 並べ替えのつまみの幅（行の高さに対する割合）
constexpr float kListGripLineRate = 0.18f;                   // つまみの横線の間隔（行の高さに対する割合）

/// <summary>
/// ドラッグで運ぶ中身（どの一覧の何番か。ほかの一覧へは落とせないように、一覧の ID も運ぶ）
/// </summary>
struct ListDragPayload {
	ImGuiID listId = 0; // 運び始めた一覧（ImGui の ID。Component・項目の名前で決まる）
	size_t index = 0;   // 運んでいる要素の番号
};

/// <summary>
/// 描き終わってからする操作（描いている途中で並びを変えると、残りの要素の番号がずれて、別の要素を描いてしまう）
/// </summary>
struct ListEdit {
	enum class Kind {
		None,        // 何もしない
		Add,         // 最後に足す（最後の要素の写し。空なら初期値）
		RemoveLast,  // 最後を消す
		InsertAbove, // index の上に初期値を足す
		Duplicate,   // index の写しを、すぐ下に足す
		Remove,      // index を消す
		Move,        // index を to の位置へ
	};
	Kind kind = Kind::None;
	size_t index = 0; // 対象の要素
	size_t to = 0;    // Move の行き先
};

// "表示名##キー" の表示名の所（ImGui と同じく、## から後ろは出さない）
std::string DisplayLabel(const char* label) {
	const char* mark = std::strstr(label, "##");
	return (mark != nullptr) ? std::string(label, mark) : std::string(label);
}

// 並べ替えのつまみ（横線3本）。押したまま動かすと、要素を運ぶドラッグになる
void DrawListGrip() {
	const float height = ImGui::GetFrameHeight();
	ImGui::InvisibleButton("##grip", ImVec2(height * kListGripWidthRate, height));
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const bool isHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
	const ImU32 color = ImGui::GetColorU32(isHot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
	const float centerY = (min.y + max.y) * 0.5f;
	const float gap = height * kListGripLineRate;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	for (int line = -1; line <= 1; ++line) {
		const float y = centerY + gap * static_cast<float>(line);
		drawList->AddLine(ImVec2(min.x + 2.0f, y), ImVec2(max.x - 2.0f, y), color, 1.5f);
	}
	if (isHot) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); // 上下に動かせることが分かるように
	}
}

// 次のフレームから、index 番の要素の見出しを開いておく（足した要素の中身がすぐ見えるように）
// 見出しの開け閉めは、ImGui が ID ごとに覚えている（要素の見出しの ID は、番号の PushID の中の "##element"）
void OpenListElement(size_t index) {
	ImGui::PushID(static_cast<int>(index));
	const ImGuiID id = ImGui::GetID("##element");
	ImGui::PopID();
	ImGui::GetStateStorage()->SetInt(id, 1);
}

// 描き終わってから、頼まれた操作をする
void ApplyListEdit(ListAccess& list, const ListEdit& edit) {
	const size_t size = list.GetSize();
	const bool isFull = size >= list.GetCapacity();
	switch (edit.kind) {
	case ListEdit::Kind::None:
		break;
	case ListEdit::Kind::Add:
		if (isFull) {
			break;
		}
		// Unity と同じく、最後の要素を写して足す（段階の一覧のように、前の要素と似た値を続けて入れることが多いので）
		if (size > 0) {
			list.Duplicate(size - 1);
		} else {
			list.Insert(0);
		}
		OpenListElement(size);
		break;
	case ListEdit::Kind::RemoveLast:
		if (size > 0) {
			list.Erase(size - 1);
		}
		break;
	case ListEdit::Kind::InsertAbove:
		if (!isFull && edit.index <= size) {
			list.Insert(edit.index);
			OpenListElement(edit.index);
		}
		break;
	case ListEdit::Kind::Duplicate:
		if (!isFull && edit.index < size) {
			list.Duplicate(edit.index);
			OpenListElement(edit.index + 1);
		}
		break;
	case ListEdit::Kind::Remove:
		if (edit.index < size) {
			list.Erase(edit.index);
		}
		break;
	case ListEdit::Kind::Move:
		if (edit.index < size && edit.to < size) {
			list.Move(edit.index, edit.to);
		}
		break;
	}
}
```

`InspectorComponentUI` の `protected:` の `EnumField` の後（`FontField` の前）に足す。

```cpp
	// 一覧：見出し（名前と数）を開くと、要素の見出しが並ぶ。要素の見出しを開くと、その要素の項目
	// 左のつまみをドラッグして並べ替え、要素の見出しの右クリックで上に足す・写す・消す。一番下の ＋ で最後を写して足す、－ で最後を消す
	void ListFieldErased(const char* label, ListAccess& list, FieldStyle style) override {
		ImGui::PushID(label); // 同じ区画に一覧が2つあっても、要素の ID がぶつからないように
		const size_t size = list.GetSize();
		const size_t capacity = list.GetCapacity();
		const bool isOpen = ImGui::TreeNodeEx("##list", ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu)", DisplayLabel(label).c_str(), size);
		DrawTooltip(style);
		if (!isOpen) {
			ImGui::PopID();
			return;
		}
		const ImGuiID listId = ImGui::GetID("##items"); // 運んだ要素を、同じ一覧にだけ落とせるようにする印
		ListEdit edit;

		// --- 要素 ---
		for (size_t i = 0; i < size; ++i) {
			ImGui::PushID(static_cast<int>(i));
			const std::string title = list.MakeTitle(i);
			const std::string heading = title.empty() ? std::format("{}", i) : std::format("{}  {}", i, title);

			// つまみ（ここから運ぶ）
			DrawListGrip();
			if (ImGui::BeginDragDropSource()) {
				const ListDragPayload payload{listId, i};
				ImGui::SetDragDropPayload(kListDragType, &payload, sizeof(payload));
				ImGui::TextUnformatted(heading.c_str()); // 運んでいる間、マウスの横に出す
				ImGui::EndDragDropSource();
			}
			ImGui::SameLine();

			// 見出し（ID は番号だけで決める。見出しの文字が変わっても、開け閉めが変わらないように）
			const bool isElementOpen = ImGui::TreeNodeEx("##element", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", heading.c_str());
			// 見出しに落とすと、その位置へ動かす（同じ一覧から運んだ物だけ。ほかの一覧の物は、落とせる色も出さない）
			if (ImGui::BeginDragDropTarget()) {
				const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
				const bool isSameList = dragging != nullptr && dragging->IsDataType(kListDragType) && static_cast<const ListDragPayload*>(dragging->Data)->listId == listId;
				if (isSameList) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kListDragType)) {
						edit = {ListEdit::Kind::Move, static_cast<const ListDragPayload*>(payload->Data)->index, i};
					}
				}
				ImGui::EndDragDropTarget();
			}
			// 見出しの右クリック
			if (ImGui::BeginPopupContextItem("##elementMenu")) {
				const bool isFull = size >= capacity;
				if (ImGui::MenuItem("Insert Above", nullptr, false, !isFull)) {
					edit = {ListEdit::Kind::InsertAbove, i};
				}
				if (ImGui::MenuItem("Duplicate", nullptr, false, !isFull)) {
					edit = {ListEdit::Kind::Duplicate, i};
				}
				if (ImGui::MenuItem("Remove")) {
					edit = {ListEdit::Kind::Remove, i};
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Move Up", nullptr, false, i > 0)) {
					edit = {ListEdit::Kind::Move, i, i - 1};
				}
				if (ImGui::MenuItem("Move Down", nullptr, false, i + 1 < size)) {
					edit = {ListEdit::Kind::Move, i, i + 1};
				}
				ImGui::EndPopup();
			}
			// 中身（要素1つの見せ方。ui.Field の並び）
			if (isElementOpen) {
				list.DescribeElement(*this, i);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		// --- 足す・消すボタン（上限があれば、今の数と上限も出す）---
		ImGui::BeginDisabled(size >= capacity);
		if (EditorWidgets::PlusButton("##add")) {
			edit = {ListEdit::Kind::Add};
		}
		ImGui::SetItemTooltip("最後の要素を写して足す（空なら初期値）");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(size == 0);
		if (ImGui::Button(" - ")) {
			edit = {ListEdit::Kind::RemoveLast};
		}
		ImGui::SetItemTooltip("最後の要素を消す（途中の要素は、見出しの右クリックの Remove）");
		ImGui::EndDisabled();
		if (capacity != SIZE_MAX) {
			ImGui::SameLine();
			ImGui::TextDisabled("%zu / %zu", size, capacity);
		}

		ApplyListEdit(list, edit);
		ImGui::TreePop();
		ImGui::PopID();
	}
```

- `ImGui::GetDragDropPayload()`：今ドラッグで運んでいる物（落とす前でも見られる）。同じ一覧の物のときだけ `AcceptDragDropPayload` を呼ぶので、ほかの一覧の物を運んでいる間は、落とせる色が付かない
- `ApplyListEdit` は `TreePop` の前に呼ぶ（`OpenListElement` が要素の見出しと同じ ID の並びの中で、見出しの ID を作るため）
- Release の `InspectorComponentUI`（`#else` の側）は直さない（既定の「何もしない」のまま）

## パートE Sandbox で確かめる

### E-1 `Sandbox/Source/Scripts/ListFieldSample.h`（新しい）

```cpp
#pragma once
#include <vector>

#include "MyEngine/Core/FixedList.h"
#include "MyEngine/ECS/Script.h"

/// <summary>
/// 距離の段階1つ（一覧の要素の見本）。memcpy で写せる型なので、データの Component の FixedList にも入れられる
/// </summary>
struct DistanceStage {
	float maxDistance = 10.0f; // この段階になる、相手までの距離の上限（m）
	float damageScale = 1.0f;  // この段階のダメージの倍率
};

/// <summary>
/// 出す場所の組1つ（一覧の中に一覧がある見本）
/// </summary>
struct SpawnWave {
	float delay = 1.0f;          // 前の組から何秒後に出すか
	std::vector<Vector3> points; // 出す場所（ワールドの位置）
};

/// <summary>
/// Inspector の一覧（ui.ListField）を確かめる Script（Sandbox のテスト用）
/// <para>Stages：要素が struct の一覧。見出しに距離と倍率を出す</para>
/// <para>Waves：要素の中に、さらに一覧がある</para>
/// <para>Targets：Entity への参照の一覧（Hierarchy からドラッグで入れる。Prefab に入れても、作った物の中の相手を指す）</para>
/// <para>再生中は、Target Distance の距離がどの段階か、と倍率をログに出す（Stages を上から見て、最初に距離が入る段階）</para>
/// </summary>
class ListFieldSample : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

	// 距離がどの段階か（Stages を上から見て、最初に maxDistance 以下になる段階。どれにも入らなければ -1）
	int FindStage(float distance) const;

private:
	std::vector<DistanceStage> stages_ = {{10.0f, 1.0f}, {20.0f, 1.5f}, {40.0f, 2.0f}}; // 距離の段階（近い順に並べる）
	std::vector<SpawnWave> waves_;                                                     // 出す場所の組
	std::vector<GameObject> targets_;                                                  // 相手の一覧
	float targetDistance_ = 15.0f;                                                     // 段階を調べる距離（m。再生中に変えると、ログの段階が変わる）
	int lastStage_ = -2;                                                               // 前にログに出した段階（実行中だけ。変わったときだけ出す）
};


/// <summary>
/// データの Component の一覧（FixedList）を確かめる見本（Sandbox のテスト用）
/// </summary>
struct ListFieldSampleData {
	FixedList<DistanceStage, 4> stages = {{10.0f, 1.0f}, {20.0f, 1.5f}}; // 距離の段階（4つまで）
};
```

### E-2 `Sandbox/Source/Scripts/ListFieldSample.cpp`（新しい）

```cpp
#include "Source/Scripts/ListFieldSample.h"

#include <format>

#include "MyEngine/Diagnostics/LogManager.h"

SCRIPT(ListFieldSample, "Test");

namespace {
// ===== 要素1つの見せ方（Script とデータの Component で同じ物を使う）=====
void DescribeStage(ComponentUI& ui, DistanceStage& stage) {
	ui.Field("Max Distance###maxDistance", stage.maxDistance, AtLeast(0.0f).Tip("この段階になる、相手までの距離の上限（m）"));
	ui.Field("Damage Scale###damageScale", stage.damageScale, AtLeast(0.0f).Drag(0.01f).Tip("この段階のダメージの倍率"));
}

// ===== 見出しの文字（折りたたんだままでも中身が分かるように）=====
std::string MakeStageTitle(const DistanceStage& stage) { return std::format("〜{:.1f}m  ×{:.2f}", stage.maxDistance, stage.damageScale); }
} // namespace

// データの Component（FixedList）。見せ方は Script と同じ ui.ListField
COMPONENT(ListFieldSampleData, "Test") { ui.ListField("Stages###stages", value.stages, &DescribeStage, &MakeStageTitle, Tip("距離の段階（4つまで）")); }


//=============================================================================
// 毎フレーム
//=============================================================================
void ListFieldSample::Update() {
	const int stage = FindStage(targetDistance_);
	if (stage == lastStage_) {
		return;
	}
	lastStage_ = stage;
	const float scale = (stage >= 0) ? stages_[static_cast<size_t>(stage)].damageScale : 0.0f;
	LogManager::Log(std::format("ListFieldSample: {:.1f}m は段階 {}（倍率 {:.2f}）", targetDistance_, stage, scale));
}

int ListFieldSample::FindStage(float distance) const {
	for (size_t i = 0; i < stages_.size(); ++i) {
		if (distance <= stages_[i].maxDistance) {
			return static_cast<int>(i);
		}
	}
	return -1;
}


//=============================================================================
// Inspector と保存
//=============================================================================
void ListFieldSample::Fields(ComponentUI& ui) {
	ui.ListField("Stages###stages", stages_, &DescribeStage, &MakeStageTitle, Tip("距離の段階（近い順に並べる）"));
	// 要素の中に一覧がある（要素の見せ方の中で、もう一度 ui.ListField を呼ぶ）
	ui.ListField(
	    "Waves###waves", waves_,
	    [](ComponentUI& ui, SpawnWave& wave) {
		    ui.Field("Delay###delay", wave.delay, AtLeast(0.0f).Tip("前の組から何秒後に出すか"));
		    ui.ListField("Points###points", wave.points, [](ComponentUI& ui, Vector3& point) { ui.Field("Position###position", point, Tip("出す場所（ワールドの位置）")); }, Tip("出す場所"));
	    },
	    [](const SpawnWave& wave) { return std::format("{:.1f} 秒後・{} か所", wave.delay, wave.points.size()); }, Tip("出す場所の組"));
	ui.ListField("Targets###targets", targets_, [](ComponentUI& ui, GameObject& target) { ui.Field("Target###target", target, Tip("相手（Hierarchy からドラッグで入れる）")); }, Tip("相手の一覧"));
	ui.Field("Target Distance###targetDistance", targetDistance_, AtLeast(0.0f).Tip("段階を調べる距離（m。再生中に変えると、ログの段階が変わる）"));
}
```

- 見せ方 `DescribeStage` と見出し `MakeStageTitle` を、Script（std::vector）と データの Component（FixedList）の両方で使い回している
- Waves の見せ方の中で、もう一度 `ui.ListField`（一覧の中の一覧）

### E-3 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

ソリューションエクスプローラーで Sandbox の Source\Scripts に2つを足してもよい。手で書くなら：

`Sandbox.vcxproj`（`Source\Scripts\TestBullet.cpp`・`TestBullet.h` の前）

```xml
    <ClCompile Include="Source\Scripts\ListFieldSample.cpp" />
```

```xml
    <ClInclude Include="Source\Scripts\ListFieldSample.h" />
```

`Sandbox.vcxproj.filters`（`TestBullet.cpp`・`TestBullet.h` の前）

```xml
    <ClCompile Include="Source\Scripts\ListFieldSample.cpp">
      <Filter>Source\Scripts</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="Source\Scripts\ListFieldSample.h">
      <Filter>Source\Scripts</Filter>
    </ClInclude>
```

### E-4 `Sandbox/Resources/scenes/ListFieldTest.scene.json`（新しい）

```json
{
	"version": 1,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 5.0, -14.0],
					"rotation": [15.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
				}
			}
		},
		{
			"id": 2,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 3.0, 0.0],
					"rotation": [50.0, -30.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Directional Light": {
					"color": [1.0, 1.0, 1.0]
				}
			}
		},
		{
			"id": 3,
			"name": "Target A",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-3.0, 1.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.4, 0.3, 1.0]
				}
			}
		},
		{
			"id": 4,
			"name": "Target B",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [3.0, 1.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.3, 0.5, 0.9, 1.0]
				}
			}
		},
		{
			"id": 5,
			"name": "List Sample",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"ListFieldSampleData": {
					"stages": [
						{"maxDistance": 10.0, "damageScale": 1.0},
						{"maxDistance": 20.0, "damageScale": 1.5}
					]
				},
				"ListFieldSample": {
					"stages": [
						{"maxDistance": 10.0, "damageScale": 1.0},
						{"maxDistance": 20.0, "damageScale": 1.5},
						{"maxDistance": 40.0, "damageScale": 2.0}
					],
					"waves": [
						{"delay": 0.5, "points": [{"position": [0.0, 0.0, 5.0]}, {"position": [2.0, 0.0, 5.0]}]},
						{"delay": 2.0, "points": []}
					],
					"targets": [
						{"target": 3},
						{"target": 4}
					],
					"targetDistance": 15.0
				}
			}
		}
	]
}
```

### E-5 確かめること

Sandbox を起動して、Project ウィンドウから ListFieldTest を開き、Hierarchy で **List Sample** を選ぶ。

| # | すること | 見えること |
|---|---|---|
| 1 | Inspector を見る | ListFieldSampleData に `Stages (2)`、要素の見出しが `0  〜10.0m  ×1.00`・`1  〜20.0m  ×1.50`、下に ＋ － と `2 / 4`。ListFieldSample (Script) に Stages (3)・Waves (2)（見出しは `0  0.5 秒後・2 か所`）・Targets (2)・Target Distance |
| 2 | ListFieldSampleData の ＋ | 3 つ目に `2  〜20.0m  ×1.50`（最後の写し）が足され、開いて中身が見える。シーンの名前に `*` |
| 3 | Ctrl+Z | 2 つに戻る（足したのが1回の Undo） |
| 4 | 要素 0 の左のつまみ（横線3本）を、要素 1 の見出しまでドラッグして離す | 並びが `0  〜20.0m`・`1  〜10.0m` に入れ替わる |
| 5 | 要素 1 の見出しを右クリック → Move Up | 元の並びに戻る。メニューに Insert Above・Duplicate・Remove・Move Up・Move Down |
| 6 | ＋ を押して 4 つにする | `4 / 4` で ＋ が押せなくなる（押しても増えない）。右クリックの Insert Above・Duplicate も灰色 |
| 7 | ListFieldSampleData の要素を、Script の Stages の見出しまでドラッグ | 落とせる色が付かず、離しても何も変わらない（ほかの一覧には落とせない） |
| 8 | Waves の `1  2.0 秒後・0 か所` を開き、中の Points の ＋ | Points (1) になり、見出しが `1  2.0 秒後・1 か所` に変わる（一覧の中の一覧） |
| 9 | Targets の要素を開き、Hierarchy の Target A をドラッグ | 相手が入る（今までの GameObject の欄と同じ） |
| 10 | Save → Sandbox を閉じて開き直す | 変えた一覧がそのまま残る。シーンファイルに `"stages": [{...}, ...]` の形で書かれている |
| 11 | Play | Console に `ListFieldSample: 15.0m は段階 1（倍率 1.50）`。再生中に Target Distance や Stages を変えると、段階が変わったときにまたログが出る |
| 12 | List Sample を Prefab にして置き、置いた方の Stages の倍率を1つ変える | Inspector の上に上書きの印。Prefab の方で別の要素を変えても、置いた方の一覧は変わらない（一覧を丸ごと上書き）。Revert で Prefab の一覧に戻る |

## 2章 AL4_01 での使い方（ロックオンの段階）

エンジンのサブモジュールをこのコミットへ進めたら：

- LockOn（Script）に `std::vector<LockOnStage> stages_`（要素＝{ 最大の距離, ダメージの倍率 }）を持ち、`Fields` で `ui.ListField("Stages###stages", stages_, &DescribeStage, &MakeStageTitle, Tip(...))`
- 段階を選ぶのは、Sandbox の `ListFieldSample::FindStage` と同じ形（近い順に並べ、最初に距離が入る段階）
- マークの段階ごとの見た目（色・大きさ・二重にするか）も、要素の struct に足すだけで同じ一覧に入る。ダメージの数字の段階も同じ
- データの Component に持たせたいとき（たくさんの Entity で同じ処理を回す）は `FixedList<LockOnStage, N>`。保存の形が同じなので、後から Script と Component を入れ替えてもシーンはそのまま読める

## 3章 次の候補

- **Timeline から要素の値を動かす**：値のトラックの項目の名前を `Stages[1]/Max Distance` のようにして、`ValueWriterUI`・`ValueListUI` で一覧の中へ入る。保存の形は変わらない
- **Polyline の点を FixedList にする**：今は `std::array` と `pointCount` を手で書いている。FixedList にすると Inspector の点の並べ替えが付く（シーンのファイルの形が変わるので、読み替えが要る）
- **要素ごとの Prefab の上書き**：要るようになったら、要素に消えない番号（id）を持たせて、番号ではなく id で上書きを当てる形にする（Unity の番号ごとより、並べ替えに強い）
- **数を打ち込む欄**（Unity の Size）：今は ＋ － と右クリックだけ。たくさん並べる一覧が出てきたら足す
- **要素を写す・貼る**（別の一覧・別の Entity へ）：今は Duplicate（同じ一覧の中）だけ
