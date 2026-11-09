参考：
- Unity の入れ子の Prefab（Nested Prefabs）：https://docs.unity3d.com/Manual/NestedPrefabs.html
- nlohmann/json の `merge_patch`（RFC 7386 JSON Merge Patch）：https://json.nlohmann.me/api/basic_json/merge_patch/ ・ https://www.rfc-editor.org/rfc/rfc7386
- `std::make_move_iterator`：https://cpprefjp.github.io/reference/iterator/make_move_iterator.html
- `std::ranges::find`：https://cpprefjp.github.io/reference/algorithm/ranges_find.html
- `std::unordered_set`：https://cpprefjp.github.io/reference/unordered_set/unordered_set.html
- 前の段階：`Docs/Tasks/PrefabInstance.md`（Prefab を参照で置く・番号をでたらめにする・XOR のずれ）

# 入れ子の Prefab を参照で書く

> **2026-11-04：この md と `PrefabOverride.md` の中身は、写経せずに本体へ直接入れた。そのとき `PrefabManager` を `Scene/Prefab/` の4つに分けた**ので、この md のコード（`Scene/PrefabManager.cpp` 1つにまとめた形）と、本体の置き場所は違う。動きは同じ。解説（0章・1章）はそのまま読める。コードは本体を見る。
>
> | この md の関数 | 本体の置き場所 |
> |---|---|
> | `Expand`・`ApplyEntry`・`AreIdsFree`・`Relabel`・`MakeEmptyRecord` | `Scene/Prefab/PrefabRecords.h`・`.cpp`（`PrefabRecords::Expand` など） |
> | 読み込み・保存・作る・`FindOwner`・`ContentIdsOf` | `Scene/Prefab/PrefabManager.h`・`.cpp`（中身を渡す `GetEntities` を足した） |
> | `"prefab"`・`"Prefab Instance"`・入れ子の深さ 8 | `PrefabRecords.h` の `kPrefabEntryKey`・`kPrefabInstanceComponentName`・`kMaxPrefabNestDepth`（SceneSerializer もこれを使う） |

Prefab の中に、別の Prefab から置いた物（入れ子）を入れたとき、**外側の Prefab のファイルにも参照だけを書く**ようにする。

今は、`Player.prefab` の中に剣（`Sword.prefab` から置いた物）を入れて保存すると、剣は中身ごと `Player.prefab` に書かれる。あとで `Sword.prefab` を直しても、Player が持っている剣は古いまま。この md の後は、`Sword.prefab` を直すと、`Player.prefab` から作った剣も新しくなる。

- **Prefab のファイルの入れ子**：シーンファイルと同じ形（`"prefab"` と一番上の Transform）で書く。中身は書かない
- **作るとき**：入れ子は、その Prefab の中身を開いて作る（入れ子の入れ子も）。中の番号は、入れ子の一番上を基準に同じ XOR で決まるので、毎回同じ
- **中身の参照**：外側の Prefab の中から、入れ子の中身を指せる（Player の Watcher が剣の Blade を見る など）
- **自分の中に自分**：止まる（警告を出して、そこから先は空の Entity にする）

パートは **A → B → C の順に写す**（B は A の `RemapRefs` を、C は B の新しい `InstantiateNow` を使う）。A だけならビルドできる。B で `InstantiateNow` の引数が変わるので、**B の後は C まで写してからビルド**。続きは `Docs/Tasks/PrefabOverride.md`（置いた所ごとの上書き）。

## 0章 全体の形

### 0.1 流れ

```
Player.prefab（ファイル）               作った Player（シーン）
├─ Player                               Player        ← 一番上の番号 R
├─ Hand                                 ├─ Hand
├─ Watcher（Blade を見る）              ├─ Watcher（作った Blade を見る）
└─ Sword ← "prefab": Sword.prefab       └─ Hand/Sword ← 入れ子の一番上（印が付く）
   （中身は書かない）                          └─ Blade  ← Sword.prefab から作る
```

1. **パートA**（ComponentSerializer・ScriptSystem）：Component の JSON の中の「Entity への参照」だけを、Entity を作らずに番号の表で付け替える `RemapRefs`。
2. **パートB**（PrefabManager）：Prefab の中身を「置いた番号」で JSON のまま並べる `Expand`（入れ子も開く）。作るのは並べた物から。中身かを調べる番号の一覧にも、入れ子の中身を入れる。
3. **パートC**（SceneSerializer）：Prefab に書くとき、入れ子は参照だけ。シーンを読むときは `InstantiateNow` にシーンの項目ごと渡す。版を 3 に上げる。

### 0.2 作る物・直す物

| ファイル | パート | 役目 |
|---|---|---|
| `ECS/ComponentSerializer.h`・`.cpp` | A | `RemapRefs`・`RemapFields`・参照を付け替える係（`RefRemapUI`）。登録表に「付け替え方」を足す |
| `Systems/Gameplay/ScriptSystem.cpp` | A | Script の付け替え方（作りたての写しに見せ方の関数を呼ぶ） |
| `Scene/PrefabManager.h` | B | `InstantiateNow` の引数をシーンの項目に。説明 |
| `Scene/PrefabManager.cpp` | B | 全部書き直す（`Expand`・`ContentIdsOf`・並べた物から作る） |
| `Scene/SceneSerializer.h`・`.cpp` | C | 版 3・入れ子を参照で書く・`LoadInstance` |

### 0.3 使い方

- **入れ子を作る**：Hierarchy で、Prefab から置いた物（Player）の子に、別の Prefab（Sword）を Place in Scene して入れる（ドラッグで子にする）。Player を選んで **Apply**（か Save As Prefab）。`Player.prefab` には Sword の参照だけが書かれる。
- **内側を直す**：Player の中の Sword を選んで **Apply**。`Sword.prefab` だけが変わる（`Player.prefab` は変わらない）。シーンを開き直すと、Player の中の剣も新しくなる。
- **Hierarchy**：入れ子の一番上（Sword）も青く出る。Sword の中身（Blade）の持ち主は Sword、Sword の持ち主は Player。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 入れ子の書き方 | シーンファイルと同じ形（`"id"`・`"name"`・`"parent"`・`"active"`・`"prefab"`・`"components": {"Transform"}`） | 読み書きの関数を、シーンと Prefab で同じにできる |
| 入れ子の中身の番号 | 入れ子のファイルの番号 XOR（入れ子の一番上の番号 XOR 入れ子のファイルの一番上の番号） | シーンの置いた物と同じ決め方。外側の番号のずれも XOR なので、何段入れ子にしても「全員を同じ数で XOR」で行き来できる |
| 中身の作り方 | ファイルの JSON を「置いた番号」に直してから並べ、並べた物から作る | 入れ子は別の Prefab の番号なので、前の「ファイルの番号 → 作った番号の表を Load に渡す」では足りない。JSON のまま直せば、続きの md（上書き）でも同じ物を比べられる |
| Component の中の参照 | 見せ方の関数で「参照の項目」を探して、JSON の番号を付け替える（`RemapRefs`） | JSON の数だけを見ても、どれが参照か分からない（ただの数と同じ形）。Entity は作らないので、参照の相手が居るかは見ない |
| アセット | 付け替えるときは読まない | 画像やモデルを開くと重い。参照の項目の通り方には関係しない |
| 中身の番号の一覧 | Prefab ごとに覚えておき、どれかの Prefab の中身が変わったら作り直す（`generation`） | Hierarchy が毎フレーム「中身か」を調べるので、毎回入れ子を開くと重い |
| 自分の中に自分 | 開いている途中の Prefab をたどり、同じ物が出たら止める（深さは 8 段まで） | 止めないと、作るときに終わらない |
| 版 | 3 にする | 版 2 のエンジンで入れ子の参照を読むと、中身の無い Entity にして保存してしまう。版 2 のファイル（入れ子が中身ごと書いてある）はそのまま読める |
| 入れ子の中身をシーンで直したとき | この md ではまだ保存されない（今までと同じ。保存のときに警告） | 置いた所ごとの上書きは、続きの `PrefabOverride.md` |

## 1章 解説

### 1.1 入れ子の中身の番号

PrefabInstance.md 1.2 のとおり、置いた物の中身の番号は「ファイルの番号 XOR ずれ」で、ずれは「一番上の番号 XOR ファイルの一番上の番号」。入れ子も同じに決める。

```
Sword.prefab：Sword = s0、Blade = s1
Player.prefab：Player = p0、Hand = p1、Watcher = p2、Sword（入れ子）= p3

Player を一番上の番号 R で置く → ずれ S = R ^ p0
  Player = p0 ^ S（= R）、Hand = p1 ^ S、Watcher = p2 ^ S
  入れ子の Sword = p3 ^ S
  入れ子のずれ T = (p3 ^ S) ^ s0
  Blade = s1 ^ T = s1 ^ p3 ^ s0 ^ S
```

Blade の番号も「Player.prefab の中での番号（`s1 ^ p3 ^ s0`）XOR S」になっている。だから、**Player.prefab の中の物は、入れ子の中身まで全部「同じ S で XOR」**。Player.prefab に「Watcher は Blade（`s1 ^ p3 ^ s0`）を見る」と書いておけば、置いたときは `^ S` するだけで、作った Blade を指す。

### 1.2 JSON の中の参照を付け替える（パートA）

並べるとき、Component の中の参照（Watcher の Look At の Target など）も、置いた番号に直す。ところが、JSON の中では参照もただの数で、`{"target": 1234, "smoothTime": 0.5}` のどれが参照かは JSON だけでは分からない。

分かるのは、その型の**見せ方の関数**（`ui.Field("target", value.target)` と書いてある所）。そこで、見せ方の関数に「参照の行だけ JSON の数を書き換える係」（`RefRemapUI`）を渡す。

| 行の種類 | `RefRemapUI` がすること |
|---|---|
| Entity への参照（`EntityRef`・`GameObject`） | JSON の数を表で付け替えて書き戻す（表に無い数はそのまま） |
| bool・int・float・文字・enum | JSON から値を読むだけ（「この bool が true なら次の行を出す」という見せ方の関数でも、ファイルと同じ行を通るように） |
| 数の並び・色・アセット | 何もしない（行の通り方に関係しない。アセットを読むと重い） |

Component（`COMPONENT(...)`）は型が決まっているので、初期値の写し（`T value{}`）に見せ方の関数を呼べばよい。Script は型が登録の表にしか無いので、作り方の関数で作りたての写しを作り、見せ方の関数を呼ぶ（Entity には付けないので、`Awake` なども呼ばれない）。

### 1.3 Expand（パートB）

`Expand(Prefab, 一番上の番号)` は、Prefab の中身を「置いた番号」の JSON で並べて返す。Entity はまだ作らない。

1. **1周目**：ファイルの項目を順に見る。ふつうの項目は、番号と親を `^ ずれ` にして並べる。入れ子（`"prefab"` の付いた項目）は、その Prefab を `Expand` して（入れ子の一番上の番号で）差し込み、一番上に印（`"Prefab Instance": {"path": ...}`）を入れる。
2. **2周目**：並べた全員の「ファイルの番号 → 置いた番号」の表を作り、ふつうの項目の Component の中の参照を付け替える（入れ子の中身は、入れ子の `Expand` の中で付け替え済み）。
3. **3周目**：入れ子の項目に書いてある名前・Transform などを、入れ子の一番上に当てる（`ApplyEntry`。書いてある項目だけ上書きする `merge_patch`）。

作る側（`Instantiate`・`InstantiateNow`）は、並べた物から Entity を作り、Component を付けるだけになる。番号はもう置いた番号なので、`ComponentSerializer::Load` に付け替えの表を渡さない。

### 1.4 中身かどうか（`FindOwner`）

PrefabInstance.md では「自分の番号 XOR 持ち主のずれ」がファイルの番号の一覧にあれば中身だった。入れ子の中身は外側のファイルに書いていないので、一覧に**入れ子の中身の番号も（外側のファイルの番号にして）入れる**（`CollectContentIds`）。

- 親の方へたどり、**一番近い**印のある物が中身として持っていれば、それが持ち主。Blade の持ち主は Sword、Sword の持ち主は Player。
- 一覧は Prefab ごとに覚える。どれかの Prefab の中身が変わる（保存・読み直し）と `generation` が増え、次に使うときに作り直す（入れ子の Sword.prefab が変わると、Player.prefab の一覧も変わるので）。

### 1.5 あまり使わない STL

| STL | 何をする物か | ここでの使い方 |
|---|---|---|
| `basic_json::merge_patch` | JSON の Merge Patch（RFC 7386）。`a.merge_patch(b)` で、b に書いてある項目だけ a を上書きする（`{...}` の中は項目ごと。`null` は消す） | 入れ子の項目に書いてある Transform などを、開いた中身の一番上に当てる |
| `std::make_move_iterator` | 指す物を「移す」（コピーしない）イテレータにする。`insert(end, make_move_iterator(b), make_move_iterator(e))` で、要素をコピーせずに移せる | 入れ子を開いた並び（大きい JSON）を、外側の並びへ写さずに移す |
| `std::ranges::find(範囲, 値)` | 範囲から値を探す（`begin`・`end` を書かなくてよい） | 開いている途中の Prefab に、同じ物があるか |

## パートA JSON の中の参照を付け替える

### A-1 `ECS/ComponentSerializer.h`

`Register` の、4つ目の関数（「写す」）の後ろに、5つ目の関数を足す（`});` の前の `}` に `,` を付けて続ける）。

```cpp
			    return [entity, copy = *component] {
				    if (T* now = EntityManager::Get<T>(entity)) {
					    *now = copy;
				    }
			    };
		    },
		    // --- 参照を付け替える：初期値の写しに見せ方の関数を呼び、Entity への参照の項目だけ JSON の番号を書き換える（Entity は作らない）---
		    [describe](SceneJson& fields, const EntityIdMap& map) {
			    T value{};
			    RemapFields([&value, describe](ComponentUI& ui) { describe(ui, value); }, fields, map);
		    });
	}
```

`Load` の次に足す。

```cpp
	static void Load(Handle<Entity> entity, const SceneJson& in, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr);
	// Component の JSON（{"型の名前": {...}, ...}。Save と同じ形）の中の、Entity への参照の番号だけを表で付け替える。表に無い番号はそのまま
	// Entity を作らずに JSON のまま番号をずらす（Prefab の入れ子を開くとき・置いた所ごとの上書きを Prefab に書くとき）。アセットは読まない
	static void RemapRefs(SceneJson& components, const EntityIdMap& map);
```

`RegisterErased` の所を書き換える（`RemapFunction` と、引数の `remap` と、`RemapFields`）。

```cpp
	using CaptureFunction = std::function<RestoreFunction(Handle<Entity>)>; // 持っていなければ空の関数を返す
	using RemapFunction = std::function<void(SceneJson&, const EntityIdMap&)>; // 1つの型の項目（{...}）の中の、Entity への参照を付け替える

	// visit は本物に見せ方の関数を呼ぶ（持っていなければ false。save と違い、写しに呼んではいけない）
	// capture は値を写す関数（空なら、JSON に書いて読み戻す）
	// remap は JSON の中の参照を付け替える関数（空なら付け替えない。RemapRefs で使う）
	static void RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit, CaptureFunction capture = {}, RemapFunction remap = {});
	// 見せ方の関数1つ分の JSON（fields）の中の、Entity への参照の番号を付け替える（Register・Script の remap から呼ぶ）
	static void RemapFields(const std::function<void(ComponentUI&)>& describe, SceneJson& fields, const EntityIdMap& map);
```

> `RemapFields` は `Register`（テンプレートなので `.h` の中）から呼ぶので public。`RefRemapUI` は `.cpp` の中だけの物なので、`.h` からは直接使えない。

### A-2 `ECS/ComponentSerializer.cpp`

`Entry` に1行足す。

```cpp
	ComponentSerializer::CaptureFunction capture; // 本物の値を写して、戻す関数を返す（空なら JSON で写す）
	ComponentSerializer::RemapFunction remap;     // JSON の中の Entity への参照を付け替える（空なら付け替えない）
};
```

読む係（`JsonReaderUI`）の終わりの `};` と、「値を書く係」の大見出しの間に足す。

```cpp
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
```

`RegisterErased` の1行目と、最後の `entries.insert` を書き換える。

```cpp
void ComponentSerializer::RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit, CaptureFunction capture, RemapFunction remap) {
```

```cpp
	entries.insert(position, Entry{name, std::move(save), std::move(load), std::move(visit), std::move(capture), std::move(remap)});
```

`Load` の関数の次に足す。

```cpp
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
```

### A-3 `Systems/Gameplay/ScriptSystem.cpp`（`RegisterType`）

`RegisterErased` の最後の引数 `visit);` を書き換える。

```cpp
	    [type](Handle<Entity> entity, ComponentUI& reader) {
		    if (Script* script = Add(entity, type)) {
			    Describe(*script, reader);
		    }
	    },
	    visit, {},
	    // 参照を付け替える（Prefab の入れ子・置いた所ごとの上書き）：作りたての写しに見せ方の関数を呼ぶ（Entity には付けないので、Awake なども呼ばれない）
	    [create](SceneJson& fields, const EntityIdMap& map) {
		    const std::unique_ptr<Script> temporary = create();
		    ComponentSerializer::RemapFields([&temporary](ComponentUI& ui) { Describe(*temporary, ui); }, fields, map);
	    });
```

> `{}` は5つ目の `capture`（Script は今までどおり空。JSON に書いて写す）。

## パートB Prefab の中身を JSON のまま並べる

### B-1 `Scene/PrefabManager.h`

include に `ComponentSerializer.h` を足す（`SceneJson` を使うので）。

```cpp
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"
```

クラスの説明（`/// <summary>` の中）の最後の `<para>` の次に足す。

```cpp
/// <para>入れ子：Prefab の中に、別の Prefab から置いた物を入れてよい。Prefab のファイルにも参照だけが書かれ、作るときにその Prefab から作る
/// （内側の Prefab を直すと、外側の Prefab から作った物も変わる）。中の番号は、入れ子の一番上を基準に同じ XOR で決まる</para>
```

`InstantiateNow` を書き換える。

```cpp
	/// <summary>
	/// シーンファイルを読むとき用。entry（シーンファイルの "prefab" の付いた1項目）のとおりに作り、Component・印まで今すぐ付ける（フレームの境目でだけ呼ぶ）
	/// <para>一番上の番号・名前・有効・Component（Transform など）は entry のとおり。親は付けない（呼ぶ側が付ける）</para>
	/// <para>中の番号がほかの Entity とぶつかったら、新しい番号で作って warnings に書く。読めていない番号なら無効な Handle</para>
	/// </summary>
	static Handle<Entity> InstantiateNow(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings);
```

`FindOwner` のコメントの2行目を書き換える。

```cpp
	// あとから足した子は中身ではない（シーンファイルにそのまま書かれる）。入れ子の中身なら、入れ子の一番上が持ち主（その一番上の持ち主は外側）
```

### B-2 `Scene/PrefabManager.cpp`（全部）

前の中身を全部消して、これにする。前からの関数（`Load`・`Save`・`IsOtherPrefabFile`・`Flush`・`FindInstanceRoot`・`Apply`・`Unpack` など）は、ほとんどそのまま。変わったのは：

| 所 | 変わったこと |
|---|---|
| `PrefabAsset` | `ids` を消して、`contentIds`・`contentGeneration`（入れ子の中身も入る一覧と、作ったときの `generation`） |
| `PendingLoad` | `entities`・`idMap` を消して、`records`（置いた番号で並べた中身） |
| `PrefabState` | `generation` |
| `SetEntities` | 一覧を作らずに `generation` を増やす |
| `CanUseShift`・`PickShift` | 消した（`AreIdsFree`・`Relabel` に） |
| 新しい関数 | `ContentIdsOf`・`CollectContentIds`・`MakeEmptyRecord`・`ApplyEntry`・`Expand` |
| `CreateEntities`・`LoadComponents` | 並べた物（`records`）から作る。`Load` に表を渡さない |
| `Instantiate`・`InstantiateNow` | `Expand` してから作る。`InstantiateNow` はシーンの項目を当てる |
| `FindOwner` | `ContentIdsOf` で調べる |
| `IsModified` | 先に中身を持っておく（`SaveTree` の中の `Load` で `asset` の場所が変わるので） |

```cpp
#include "PrefabManager.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Components/Core/PrefabInstanceComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Scene/SceneSerializer.h"
#include "HTTEngine/String/ConvertString.h"


namespace {
constexpr const char* kTransformName = "Transform";            // Transform の保存の名前（作るときに、これだけ先に付ける）
constexpr const char* kPrefabInstanceName = "Prefab Instance"; // 置いた物の印の保存の名前（EngineComponents.cpp）
constexpr const char* kPrefabKey = "prefab";                   // ファイルの、Prefab から置いた物の印（値は Prefab のパス。シーンファイルと同じ）
constexpr size_t kMaxNestDepth = 8;                            // 入れ子の深さの上限（Prefab の中に自分が入っていても、ここで止まる）

// Entity 1つ分の JSON（{"id", "name", "parent", "active", "components"}。シーンファイルの1項目と同じ形）の並び。親が子より前
// Prefab の中身を「置いたときの番号」で並べた物。Entity を作る前の形
using EntityRecords = std::vector<SceneJson>;

/// <summary>
/// 読み込んだ Prefab 1つ分
/// </summary>
struct PrefabAsset {
	std::string path;                          // パス（UTF-8。ToPathKey の形。読み込み済みかを調べるキーにもなる）
	std::shared_ptr<const SceneJson> entities; // Entity の並び（ファイルの "entities"）。保存し直すと、新しい物に差し替わる
	std::filesystem::file_time_type writeTime; // 読んだ・書いたときのファイルの時刻（ReloadChangedFiles で比べる）
	// 中身の番号（この Prefab のファイルの番号。入れ子の Prefab の中身も、この Prefab の番号にして入れる）。置いた物の中身かを調べる
	// 入れ子の Prefab が書き換わると変わるので、使うときに作る（contentGeneration が PrefabState::generation と違えば作り直す）
	std::unordered_set<EntityId> contentIds;
	uint64_t contentGeneration = 0;
};

/// <summary>
/// 作ったけれど、まだ Transform 以外の Component を付けていない物（次のフレームの頭の Flush で付ける）
/// </summary>
struct PendingLoad {
	uint32_t prefab = 0;                          // どの Prefab から作ったか（印に書く）
	std::shared_ptr<const EntityRecords> records; // 作ったときの中身（置いた番号。途中で Prefab を保存し直しても、作ったときの中身で付ける）
	std::vector<Handle<Entity>> created;          // records の i 番目から作った Entity
};

/// <summary>
/// PrefabManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PrefabState {
	std::vector<PrefabAsset> assets;       // 読み込んだ Prefab（番号 - 1 が添字。番号は1から、0は未選択）
	std::vector<PendingLoad> pendingLoads; // Component を付ける予約
	std::vector<uint32_t> placeRequests;   // エディタから置く予約
	uint64_t generation = 1;               // どれかの Prefab の中身が変わるたびに増える（PrefabAsset::contentIds を作り直す合図）
};
std::unique_ptr<PrefabState> state;

//=============================================================================
// 小さな道具
//=============================================================================
// 番号から Prefab（知らない番号なら nullptr）
// 返したポインタは、次に Load するまでに使い終える（Load が assets を増やすと、指している場所が変わる）
PrefabAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// 集めた警告を1回ずつ出す（同じ Prefab を何個作っても、同じ文は1回だけ）
void FlushWarnings(const std::set<std::string>& warnings) {
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

// Entity 1つ分の "components"（無ければ nullptr）
const SceneJson* FindComponents(const SceneJson& item) {
	const auto it = item.find("components");
	return (it != item.end() && it->is_object()) ? &*it : nullptr;
}

// Entity 1つ分の形が正しいか（読むときに1回だけ確かめる。作るたびに value() で形の違う値を読むと例外が出るので）
bool IsValidItem(const SceneJson& item) {
	if (!item.is_object()) {
		return false;
	}
	// --- 番号は必ず要る（0は「無し」なので使えない）---
	const auto id = item.find("id");
	if (id == item.end() || !id->is_number_unsigned() || id->get<EntityId>() == 0) {
		return false;
	}
	// --- ほかは、あれば形を確かめる（無ければ作るときに初期値）---
	const auto isOptional = [&item](const char* key, bool (SceneJson::*isExpected)() const noexcept) {
		const auto it = item.find(key);
		return it == item.end() || ((*it).*isExpected)();
	};
	return isOptional("name", &SceneJson::is_string) && isOptional("parent", &SceneJson::is_number_unsigned) && isOptional("active", &SceneJson::is_boolean) &&
	       isOptional("components", &SceneJson::is_object) && isOptional(kPrefabKey, &SceneJson::is_string);
}

// ファイルを読んで、Entity の並びを取り出す（読めなければ nullptr）
std::shared_ptr<const SceneJson> ReadEntities(const std::filesystem::path& filePath) {
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Prefab のファイルを開けませんでした: {}", ToUtf8(filePath)));
		return nullptr;
	}
	const SceneJson root = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error(std::format("Prefab のファイルが壊れています: {}", ToUtf8(filePath)));
		return nullptr;
	}
	const SceneJson* entities = SceneSerializer::FindEntities(root);
	if (entities == nullptr || entities->empty()) {
		LogManager::Error(std::format("Prefab に Entity がありません: {}", ToUtf8(filePath)));
		return nullptr;
	}
	if (!std::all_of(entities->begin(), entities->end(), &IsValidItem)) {
		LogManager::Error(std::format("Prefab の Entity の形が違います（\"id\" が無い・数でない など）: {}", ToUtf8(filePath)));
		return nullptr;
	}
	return std::make_shared<const SceneJson>(*entities);
}

// ファイルの時刻（読めなければ、いちばん古い時刻）
std::filesystem::file_time_type WriteTimeOf(const std::string& path) {
	std::error_code error; // 例外ではなくエラーコードで受ける
	const std::filesystem::file_time_type time = std::filesystem::last_write_time(ToPath(path), error);
	return error ? std::filesystem::file_time_type{} : time;
}

// 中身を差し替える（入れ子にしている Prefab の中身の番号も変わるので、全部の Prefab の contentIds を作り直させる）
void SetEntities(PrefabAsset& asset, std::shared_ptr<const SceneJson> entities) {
	asset.entities = std::move(entities);
	++state->generation;
}

//=============================================================================
// 番号のずれ（置いた物の番号 = ファイルの番号 XOR ずれ）
//=============================================================================
// ファイルの一番上の Entity の番号
EntityId RootFileId(const SceneJson& entities) { return entities.front().at("id").get<EntityId>(); }

// 一番上を rootId の番号で置いたときの、ずれ（一番上は ファイルの番号 XOR ずれ = rootId になる）
EntityId ShiftOf(const PrefabAsset& asset, EntityId rootId) { return rootId ^ RootFileId(*asset.entities); }

// 並べた中身の番号が、どれも0でなく、まだ誰も使っていないか
bool AreIdsFree(const EntityRecords& records) {
	return std::ranges::all_of(records, [](const SceneJson& record) {
		const EntityId id = record.at("id").get<EntityId>();
		return id != 0 && !EntityManager::FindById(id).IsValid();
	});
}

// ===== 並べた中身の番号を、全部 delta で XOR する（Component の中の、中身どうしの参照も同じだけずらす）=====
// 一番上の親（中身の外）は変えない
void Relabel(EntityRecords& records, EntityId delta) {
	EntityIdMap map;
	for (const SceneJson& record : records) {
		const EntityId id = record.at("id").get<EntityId>();
		map[id] = id ^ delta;
	}
	for (SceneJson& record : records) {
		record["id"] = map.at(record.at("id").get<EntityId>());
		if (const auto parent = map.find(record.value("parent", EntityId{0})); parent != map.end()) {
			record["parent"] = parent->second;
		}
		if (const auto components = record.find("components"); components != record.end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
	}
}

//=============================================================================
// 中身の番号（置いた物の中身かを調べる）
//=============================================================================
const std::unordered_set<EntityId>& ContentIdsOf(uint32_t handle, std::vector<uint32_t>& stack);

// ===== Prefab の中身の番号（ファイルの番号）を集める。入れ子の Prefab は、その中身をこの Prefab の番号にして入れる =====
// stack は集めている途中の Prefab（自分の中に自分が入っていたら、そこで止める）
std::unordered_set<EntityId> CollectContentIds(uint32_t handle, std::vector<uint32_t>& stack) {
	std::unordered_set<EntityId> ids;
	const PrefabAsset* asset = Find(handle);
	if (asset == nullptr || asset->entities == nullptr || std::ranges::find(stack, handle) != stack.end() || stack.size() >= kMaxNestDepth) {
		return ids;
	}
	const std::shared_ptr<const SceneJson> entities = asset->entities; // 入れ子の Load で asset の場所が変わるので、中身を持っておく
	stack.push_back(handle);
	for (const SceneJson& item : *entities) {
		const EntityId id = item.at("id").get<EntityId>();
		ids.insert(id);
		const auto prefab = item.find(kPrefabKey);
		if (prefab == item.end()) {
			continue;
		}
		// --- 入れ子：中身の番号 = 入れ子のファイルの番号 XOR（この項目の番号 XOR 入れ子のファイルの一番上の番号）---
		const uint32_t child = PrefabManager::Load(ToPath(prefab->get<std::string>()));
		const PrefabAsset* childAsset = Find(child);
		if (childAsset == nullptr || childAsset->entities == nullptr) {
			continue; // 読めない（ファイルがまだ無い）。一番上だけが中身
		}
		const EntityId shift = id ^ RootFileId(*childAsset->entities);
		for (const EntityId childId : ContentIdsOf(child, stack)) {
			ids.insert(childId ^ shift);
		}
	}
	stack.pop_back();
	return ids;
}

// ===== 中身の番号（前に集めた物があれば、それを使う）=====
// 返した参照は、次に Load するまでに使い終える
const std::unordered_set<EntityId>& ContentIdsOf(uint32_t handle, std::vector<uint32_t>& stack) {
	static const std::unordered_set<EntityId> kEmpty; // 知らない番号のときに返す空の集まり（参照で返すので static にする）
	PrefabAsset* asset = Find(handle);
	if (asset == nullptr) {
		return kEmpty;
	}
	if (asset->contentGeneration != state->generation) {
		std::unordered_set<EntityId> ids = CollectContentIds(handle, stack);
		asset = Find(handle); // 入れ子の Load で場所が変わったかもしれないので、引き直す
		asset->contentIds = std::move(ids);
		asset->contentGeneration = state->generation;
	}
	return asset->contentIds;
}

//=============================================================================
// 中身を並べる（入れ子の Prefab も開く。Entity はまだ作らない）
//=============================================================================
// ===== 読めない入れ子の代わりの、空の Entity 1つ分 =====
SceneJson MakeEmptyRecord(EntityId id) {
	SceneJson record = SceneJson::object();
	record["id"] = id;
	record["name"] = "Entity";
	record["parent"] = EntityId{0};
	record["active"] = true;
	record["components"] = SceneJson::object();
	return record;
}

// ===== 置いた物の項目（"prefab" の付いた1項目。番号は置いた番号）を、並べた中身の一番上（records[begin]）に当てる =====
// 当てるのは、一番上の名前・有効・親と、書いてある Component（足りない項目は Prefab の値のまま。merge_patch）
void ApplyEntry(EntityRecords& records, size_t begin, const SceneJson& entry) {
	SceneJson& root = records[begin];
	root["name"] = entry.value("name", root.value("name", std::string("Entity")));
	root["active"] = entry.value("active", true);
	root["parent"] = entry.value("parent", EntityId{0});
	if (!root.contains("components") || !root["components"].is_object()) {
		root["components"] = SceneJson::object();
	}
	const SceneJson* components = FindComponents(entry);
	if (components == nullptr) {
		return;
	}
	SceneJson& target = root["components"];
	for (auto it = components->begin(); it != components->end(); ++it) {
		// merge_patch：書いてある項目だけを上書きする（{"translation": [...]} なら、translation だけ変わる）
		if (target.contains(it.key()) && target[it.key()].is_object()) {
			target[it.key()].merge_patch(*it);
		} else {
			target[it.key()] = *it;
		}
	}
}

// ===== Prefab の中身を、一番上を rootId の番号にして並べる =====
// 番号・親・Component の中の Entity への参照は、置いたときの番号（ファイルの番号 XOR ずれ）にして返す。一番上（[0]）の親は 0
// 入れ子（"prefab" の付いた項目）は、その Prefab の中身を開いて差し込み、一番上に「置いた物の印」（Prefab Instance）を入れる
// stack は開いている途中の Prefab（自分の中に自分が入っていたら、そこで止める）。読めない Prefab なら空
EntityRecords Expand(uint32_t handle, EntityId rootId, std::vector<uint32_t>& stack, std::set<std::string>& warnings) {
	const PrefabAsset* asset = Find(handle);
	if (asset == nullptr || asset->entities == nullptr) {
		return {};
	}
	// asset は入れ子の Load で場所が変わるので、ここで中身を持っておき、この先は使わない
	const std::shared_ptr<const SceneJson> entities = asset->entities;
	const std::string path = asset->path;
	if (std::ranges::find(stack, handle) != stack.end() || stack.size() >= kMaxNestDepth) {
		warnings.insert(std::format("Prefab の中に自分が入っている（か、入れ子が {} 段より深い）ので、そこから先は作りません: {}", kMaxNestDepth, path));
		return {};
	}
	const EntityId shift = rootId ^ RootFileId(*entities);
	stack.push_back(handle);

	// --- 1周目：並べる（番号と親を、置いた番号に）。入れ子は、その Prefab の中身を開いて差し込む ---
	EntityRecords records;
	std::vector<size_t> plainIndices;                        // ファイルにそのまま書いてあった項目の、records での位置
	std::vector<std::pair<size_t, const SceneJson*>> nested; // 入れ子を差し込んだ先頭の位置と、ファイルのその項目
	for (const SceneJson& item : *entities) {
		const EntityId id = item.at("id").get<EntityId>() ^ shift;
		if (const auto prefab = item.find(kPrefabKey); prefab != item.end()) {
			const std::string& nestedPath = prefab->get_ref<const std::string&>();
			nested.emplace_back(records.size(), &item);
			EntityRecords sub = Expand(PrefabManager::Load(ToPath(nestedPath)), id, stack, warnings);
			if (sub.empty()) {
				warnings.insert(std::format("入れ子の Prefab が読めないので、空の Entity にしました（ファイルが届いてから開き直すと、中身ができます）: {}", nestedPath));
				sub.push_back(MakeEmptyRecord(id));
			}
			// 入れ子の一番上に、置いた物の印を付ける（パスはファイルに書いてあるとおり）
			sub.front()["components"][kPrefabInstanceName] = SceneJson{{"path", nestedPath}};
			records.insert(records.end(), std::make_move_iterator(sub.begin()), std::make_move_iterator(sub.end()));
			continue;
		}
		SceneJson record = item;
		record["id"] = id;
		if (const EntityId parent = item.value("parent", EntityId{0}); parent != 0) {
			record["parent"] = parent ^ shift;
		}
		plainIndices.push_back(records.size());
		records.push_back(std::move(record));
	}

	// --- 2周目：ファイルの番号 → 置いた番号 の表（入れ子の中身も入る）で、Component の中の参照を付け替える ---
	EntityIdMap map;
	for (const SceneJson& record : records) {
		const EntityId id = record.at("id").get<EntityId>();
		map[id ^ shift] = id;
	}
	for (const size_t index : plainIndices) {
		if (const auto components = records[index].find("components"); components != records[index].end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
	}

	// --- 3周目：入れ子の項目（ファイルに書いてある名前・Transform など）を、置いた番号にしてから当てる ---
	for (const auto& [begin, item] : nested) {
		SceneJson entry = *item;
		if (const EntityId parent = entry.value("parent", EntityId{0}); parent != 0) {
			entry["parent"] = parent ^ shift;
		}
		if (const auto components = entry.find("components"); components != entry.end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
		ApplyEntry(records, begin, entry);
	}
	records.front()["parent"] = EntityId{0}; // 一番上の親は、置く側が決める
	stack.pop_back();
	return records;
}

//=============================================================================
// 作る
//=============================================================================
// ===== 並べた中身から Entity を作り、親子をつなぐ（Component はまだ。Transform は Entity と一緒にできている）=====
PendingLoad CreateEntities(uint32_t prefab, std::shared_ptr<const EntityRecords> records, Handle<Entity> parent) {
	PendingLoad pending;
	pending.prefab = prefab;
	pending.records = std::move(records);
	const EntityRecords& items = *pending.records;
	pending.created.reserve(items.size());

	// --- 1周目：Entity を作る（番号はもう置いた番号になっている）---
	for (const SceneJson& item : items) {
		const Handle<Entity> created = EntityManager::CreateWithId(item.at("id").get<EntityId>(), item.value("name", std::string("Entity")), {});
		EntityManager::Get(created)->isActive = item.value("active", true);
		pending.created.push_back(created);
	}

	// --- 2周目：親子をつなぐ ---
	// 先頭（一番上）は parent の子に。それ以外は、書いてある親（見つからなければ一番上）の子にする
	const Handle<Entity> root = pending.created.front();
	for (size_t i = 0; i < items.size(); ++i) {
		Handle<Entity> newParent = parent;
		if (i > 0) {
			const Handle<Entity> found = EntityManager::FindById(items[i].value("parent", EntityId{0}));
			newParent = found.IsValid() ? found : root;
		}
		if (newParent.IsValid()) {
			EntityManager::SetParent(pending.created[i], newParent);
		}
	}
	return pending;
}

// ===== Component を付ける。isTransform なら Transform だけ、そうでなければ Transform 以外を全部 =====
// Transform は Entity を作ったときから持っているので、上書きするだけ（Component の配列は増えない＝フレームの途中でも安全）
// ほかの Component は配列が増えるので、フレームの境目でだけ付ける
void LoadComponents(const PendingLoad& pending, bool isTransform, std::set<std::string>& warnings) {
	for (size_t i = 0; i < pending.created.size(); ++i) {
		if (!EntityManager::IsAlive(pending.created[i])) {
			continue; // 作ってから今までに消された（Destroy・Stop）
		}
		const SceneJson* components = FindComponents((*pending.records)[i]);
		if (components == nullptr) {
			continue;
		}
		SceneJson part = SceneJson::object();
		if (isTransform) {
			if (const auto transform = components->find(kTransformName); transform != components->end()) {
				part[kTransformName] = *transform;
			}
		} else {
			part = *components;
			part.erase(kTransformName);
		}
		// 番号はもう置いた番号なので、付け替えずに読む（中身どうしの参照も、相手はもう作ってある）
		ComponentSerializer::Load(pending.created[i], part, warnings);
	}
}

// ===== 一番上に「置いた物の印」を付ける（Component が増えるので、フレームの境目でだけ呼ぶ）=====
void Link(const PendingLoad& pending) {
	const Handle<Entity> root = pending.created.front();
	if (!EntityManager::IsAlive(root)) {
		return;
	}
	PrefabInstanceComponent link;
	link.path = PrefabManager::GetPath(pending.prefab);
	link.prefab = pending.prefab;
	EntityManager::RestoreComponents(root, {ComponentSnapshot::Make(link)});
}

// ===== 置いた物の一番上から、元の Prefab（印が無い・読めていない Prefab なら nullptr）=====
const PrefabAsset* FindAssetOf(Handle<Entity> root) {
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	const PrefabAsset* asset = (link != nullptr) ? Find(link->prefab) : nullptr;
	return (asset != nullptr && asset->entities != nullptr) ? asset : nullptr;
}

// ===== Entity 1つ分の Component から、Transform を除いた物（一番上は、置いた所ごとに Transform を変えてよいので比べない）=====
SceneJson ComponentsWithoutTransform(const SceneJson& item) {
	const SceneJson* components = FindComponents(item);
	SceneJson result = (components != nullptr) ? *components : SceneJson::object();
	result.erase(kTransformName);
	return result;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void PrefabManager::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<PrefabState>();
}

void PrefabManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t PrefabManager::Load(const std::filesystem::path& filePath) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &PrefabAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む ---
	std::shared_ptr<const SceneJson> entities = ReadEntities(filePath);
	if (entities == nullptr) {
		return 0; // 止めずに未選択の番号を返す
	}
	const size_t count = entities->size();
	PrefabAsset asset;
	asset.path = key;
	asset.writeTime = WriteTimeOf(key);
	SetEntities(asset, std::move(entities));
	state->assets.push_back(std::move(asset));
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("読み込み完了 \"{}\" -> handle={} Entity数={}", key, handle, count));
	return handle;
}

const std::string& PrefabManager::GetPath(uint32_t handle) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	const PrefabAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmpty;
}

void PrefabManager::ReloadChangedFiles() {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	for (PrefabAsset& asset : state->assets) {
		const std::filesystem::file_time_type time = WriteTimeOf(asset.path);
		if (time == asset.writeTime) {
			continue;
		}
		// 読めなかったら前の中身のまま（書いている途中のファイルを読んだときなど）。時刻は進めて、毎回読みに行かないようにする
		asset.writeTime = time;
		if (std::shared_ptr<const SceneJson> entities = ReadEntities(ToPath(asset.path))) {
			SetEntities(asset, std::move(entities));
			LogManager::Log(std::format("Prefab が書き換わっていたので読み直しました: {}", asset.path));
		}
	}
}

//=============================================================================
// 保存
//=============================================================================
bool PrefabManager::Save(Handle<Entity> root, const std::string& path) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	const Entity* rootEntity = EntityManager::Get(root);
	if (rootEntity == nullptr) {
		return false;
	}
	const std::string key = ToPathKey(ToPath(path));
	const auto cached = std::ranges::find(state->assets, key, &PrefabAsset::path);
	const uint32_t cachedHandle = (cached != state->assets.end()) ? static_cast<uint32_t>(cached - state->assets.begin()) + 1 : 0;

	// --- この Prefab から置いた物なら、ずれを戻して書く（ファイルの番号が前と同じになる）。ほかは今の番号のまま書く ---
	const PrefabAsset* source = FindAssetOf(root);
	const EntityId shift = (source != nullptr && source == Find(cachedHandle)) ? ShiftOf(*source, rootEntity->id) : EntityId{0};
	const SceneJson json = SceneSerializer::SaveTree(root, shift);
	if (!SceneSerializer::WriteFile(path, json)) {
		return false;
	}

	// --- もう読み込んでいる Prefab なら、中身を差し替える。まだなら読み込む ---
	// このあと作る物から新しい中身になる。作りかけの物（PendingLoad）は前の中身を shared_ptr で持っているので、そのまま付け終われる
	uint32_t handle = cachedHandle;
	if (PrefabAsset* asset = Find(handle)) {
		SetEntities(*asset, std::make_shared<const SceneJson>(json.at("entities")));
		asset->writeTime = WriteTimeOf(key); // 自分で書いた分は、ReloadChangedFiles で読み直さない
	} else {
		handle = Load(ToPath(path));
	}
	LogManager::Log(std::format("Prefab を保存しました: {}（Entity {}個）", path, json.at("entities").size()));

	// --- 保存した物を、この Prefab から置いた物にする（印が無ければ次のフレームの頭に付く。ImGui を描いている途中で呼ばれるので）---
	PrefabInstanceComponent link;
	link.path = GetPath(handle);
	link.prefab = handle;
	if (PrefabInstanceComponent* now = EntityManager::Get<PrefabInstanceComponent>(root)) {
		*now = link;
	} else {
		EntityManager::RequestAdd(root, link);
	}
	return true;
}

bool PrefabManager::IsOtherPrefabFile(Handle<Entity> root, const std::string& path) {
	std::error_code error; // 例外ではなくエラーコードで受ける
	if (!std::filesystem::exists(ToPath(path), error)) {
		return false; // まだ無いファイル（新しく作る）
	}
	// 同じファイルから置いた物なら、書いても Apply と同じ（自分の Prefab を書き直すだけ）
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link == nullptr) || (ToPathKey(ToPath(link->path.ToString())) != ToPathKey(ToPath(path)));
}

//=============================================================================
// 作る
//=============================================================================
Handle<Entity> PrefabManager::Instantiate(uint32_t prefab, const Vector3& position, Handle<Entity> parent) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	if (Find(prefab) == nullptr) {
		LogManager::Warning(std::format("読み込んでいない Prefab の番号 {} から作ろうとしました（Inspector で Prefab を選んでいるか確かめてください）", prefab));
		return {};
	}
	// ===== 中身を並べる（一番上の番号は新しくもらう。中の誰かがほかの Entity とぶつかったら、もらい直す）=====
	std::set<std::string> warnings;
	std::vector<uint32_t> stack;
	EntityRecords records;
	do {
		records = Expand(prefab, EntityManager::NewId(), stack, warnings);
	} while (!records.empty() && !AreIdsFree(records));
	if (records.empty()) {
		FlushWarnings(warnings);
		return {};
	}

	// ===== Entity を作り、Transform だけ今すぐ付ける =====
	PendingLoad pending = CreateEntities(prefab, std::make_shared<const EntityRecords>(std::move(records)), parent);
	LoadComponents(pending, true, warnings);
	FlushWarnings(warnings);
	// 一番上の位置を、渡された位置にする（回転・大きさは Prefab のまま）
	const Handle<Entity> root = pending.created.front();
	if (TransformComponent* transform = EntityManager::Get<TransformComponent>(root)) {
		transform->translation = position;
	}

	// ===== ほかの Component・Script・印は、次のフレームの頭に付ける =====
	state->pendingLoads.push_back(std::move(pending));
	return root;
}

Handle<Entity> PrefabManager::InstantiateNow(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// --- 中身を並べ、ファイルの項目（一番上の名前・有効・Transform など）を当てる ---
	std::vector<uint32_t> stack;
	EntityRecords records = Expand(prefab, entry.value("id", EntityId{0}), stack, warnings);
	if (records.empty()) {
		return {};
	}
	ApplyEntry(records, 0, entry);
	// --- 中の誰かがほかの Entity とぶつかるなら、全員を新しい番号にずらす（シーンのほかの物からの参照は外れる）---
	if (!AreIdsFree(records)) {
		warnings.insert(std::format("Prefab の中の番号がほかの Entity とぶつかったので、新しい番号で作りました（この物への参照は外れます）: {}", GetPath(prefab)));
		const EntityId rootId = records.front().at("id").get<EntityId>();
		EntityRecords moved;
		do {
			moved = records;
			Relabel(moved, EntityManager::NewId() ^ rootId);
		} while (!AreIdsFree(moved));
		records = std::move(moved);
	}
	// --- 作って、Component・印までその場で付ける（フレームの境目なので安全）---
	const PendingLoad pending = CreateEntities(prefab, std::make_shared<const EntityRecords>(std::move(records)), {});
	LoadComponents(pending, true, warnings);
	LoadComponents(pending, false, warnings);
	Link(pending);
	return pending.created.front();
}

void PrefabManager::RequestPlace(uint32_t prefab) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	state->placeRequests.push_back(prefab);
}

//=============================================================================
// フレームの頭
//=============================================================================
void PrefabManager::Flush() {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// ===== エディタから置く予約（作るだけ。Component は下でまとめて付く）=====
	// 取り出してから回す（回している間に予約が足されても壊れないように）
	const std::vector<uint32_t> places = std::exchange(state->placeRequests, {});
	for (const uint32_t prefab : places) {
		Instantiate(prefab, {0.0f, 0.0f, 0.0f});
	}

	// ===== Component・Script・印を付ける（Transform は作ったときに付けたので飛ばす）=====
	// フレームの境目なので、予約せずその場で付けてよい（シーンを読むときと同じ）
	const std::vector<PendingLoad> loads = std::exchange(state->pendingLoads, {});
	std::set<std::string> warnings;
	for (const PendingLoad& pending : loads) {
		LoadComponents(pending, false, warnings);
		Link(pending);
	}
	FlushWarnings(warnings);
}

//=============================================================================
// 置いた物
//=============================================================================
Handle<Entity> PrefabManager::FindOwner(Handle<Entity> entity) {
	const Entity* target = EntityManager::Get(entity);
	if (target == nullptr) {
		return {};
	}
	// 親の方へたどり、印のある物の中身か（自分の番号 XOR ずれが、中身の番号にあるか）を調べる。入れ子なら、一番近い置いた物が持ち主
	std::vector<uint32_t> stack;
	Handle<Entity> current = target->parent;
	while (const Entity* ancestor = EntityManager::Get(current)) {
		const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(current);
		if (const PrefabAsset* asset = (link != nullptr) ? Find(link->prefab) : nullptr; asset != nullptr && asset->entities != nullptr) {
			const EntityId fileId = target->id ^ ShiftOf(*asset, ancestor->id); // asset は ContentIdsOf の中の Load で場所が変わるので、先に使う
			if (ContentIdsOf(link->prefab, stack).contains(fileId)) {
				return current;
			}
		}
		current = ancestor->parent;
	}
	return {};
}

Handle<Entity> PrefabManager::FindInstanceRoot(Handle<Entity> entity) {
	const Handle<Entity> root = (EntityManager::Get<PrefabInstanceComponent>(entity) != nullptr) ? entity : FindOwner(entity);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link != nullptr && IsAssetFile(link->path.View(), AssetType::Prefab)) ? root : Handle<Entity>{};
}

bool PrefabManager::IsModified(Handle<Entity> root) {
	const Entity* rootEntity = EntityManager::Get(root);
	const PrefabAsset* asset = (rootEntity != nullptr) ? FindAssetOf(root) : nullptr;
	if (asset == nullptr) {
		return false;
	}
	// --- 今の物を、ファイルと同じ番号で書いてみる（入れ子は、ファイルと同じく参照だけになる）---
	const std::shared_ptr<const SceneJson> entities = asset->entities; // SaveTree の中の Load で asset の場所が変わるので、中身を持っておく
	const SceneJson saved = SceneSerializer::SaveTree(root, ShiftOf(*asset, rootEntity->id));
	const SceneJson& now = saved.at("entities");
	// --- ファイルの番号 → ファイルの1項目 ---
	std::unordered_map<EntityId, const SceneJson*> original;
	for (const SceneJson& item : *entities) {
		original[item.at("id").get<EntityId>()] = &item;
	}
	// --- 同じ番号どうしを比べる（あとから足した子は、ファイルに無い番号なので飛ばす）---
	size_t matched = 0;
	for (size_t i = 0; i < now.size(); ++i) {
		const auto found = original.find(now[i].at("id").get<EntityId>());
		if (found == original.end()) {
			continue;
		}
		++matched;
		// 一番上は、置いた所ごとに変えてよい物（名前・有効・Transform）を除いて比べる
		const bool isSame = (i == 0) ? (ComponentsWithoutTransform(now[i]) == ComponentsWithoutTransform(*found->second)) : (now[i] == *found->second);
		if (!isSame) {
			return true;
		}
	}
	return matched != original.size(); // 足りない＝Prefab の中の物を消した
}

bool PrefabManager::Apply(Handle<Entity> root) {
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link != nullptr) && Save(root, link->path.ToString());
}

void PrefabManager::Unpack(Handle<Entity> root) {
	if (EntityManager::RequestRemove<PrefabInstanceComponent>(root)) {
		LogManager::Log("Prefab とのつながりを切りました（これからは中身もシーンファイルに書かれます）");
	}
}
```

> **`Find` が返したポインタは、`Load` の後で使わない**：`Load` が `assets`（`std::vector`）に足すと、中身が別の場所へ引っ越して、前に取ったポインタは壊れる。入れ子を開く・番号の一覧を作るときは、中で `Load` が呼ばれるので、先に `shared_ptr`（中身）や番号を取り出しておく（`Expand`・`CollectContentIds`・`ContentIdsOf`・`FindOwner` のコメント）。

## パートC 書くとき・読むとき

### C-1 `Scene/SceneSerializer.h`

版を 3 にする。

```cpp
	// 2：Prefab から置いた物を参照で書く（"prefab"）。1 のファイルはそのまま読める（"prefab" が無いだけ）
	// 3：Prefab のファイルの中の入れ子の Prefab も参照で書く。2 のファイルはそのまま読める（入れ子が中身ごと書いてあるだけ）
	static constexpr int kVersion = 3;
```

`SaveTree` のコメントに1行足す。

```cpp
	// 番号は shift で XOR して書く（中の Entity への参照も。Prefab の番号のずれを戻すため。PrefabManager.h）。root の「置いた物の印」は書かない
	// 中に、別の Prefab から置いた物（入れ子）があれば、それは参照だけを書く（シーンファイルと同じ形。中身は書かない）
	static SceneJson SaveTree(Handle<Entity> root, EntityId shift = 0);
```

### C-2 `Scene/SceneSerializer.cpp`（書く側）

`SaveInstance` に、書く番号 `id` を足す（Prefab の中の入れ子は、ずれを戻した番号で書くので）。警告の文も、Prefab に書くときに合うように直す。

```cpp
// ===== Prefab から置いた物 1つ分（{"id", "name", "parent", "active", "prefab", "components": {"Transform"}}）=====
// 中身は書かない（読むときに Prefab のファイルから作り直す）。Prefab を直しても、シーンファイルは変わらない
// id・parentId は書く番号（シーンなら今の番号。Prefab の中の入れ子なら、ずれを戻した番号）
SceneJson SaveInstance(const Entity& entity, EntityId id, EntityId parentId, const PrefabInstanceComponent& link, std::set<std::string>& warnings) {
	SceneJson item = SaveEntity(entity, id, parentId, warnings);
```

```cpp
		warnings.insert(std::format("「{}」は Prefab（{}）と違う所がありますが、Prefab の参照だけを書きました。次に開くと Prefab のとおりに戻ります（残すなら、それを選んで Inspector の Apply）",
```

`SaveAll` の呼び方を直す。

```cpp
				entities.push_back(SaveInstance(entity, entity.id, parentId, *link, warnings));
```

`SaveTree` の「書く」の繰り返しの中、`parentId` を決めた次に足す。

```cpp
		const EntityId parentId = (i == 0 || parent == nullptr) ? EntityId{0} : (parent->id ^ shift);
		// --- 中の入れ子（別の Prefab から置いた物）は、シーンファイルと同じく参照だけを書く ---
		if (i > 0) {
			// 入れ子の中身は書かない（作るときに、その Prefab から作られる）。持ち主が root なら root の中身なので書く。
			// 持ち主が root より上（root 自身が入れ子のときの外側）なら、この Prefab の中身として書く
			const Entity* owner = EntityManager::Get(PrefabManager::FindOwner(queue[i]));
			if (owner != nullptr && owner->self != root && idMap.contains(owner->id)) {
				continue;
			}
			if (const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(queue[i])) {
				entities.push_back(SaveInstance(*entity, entity->id ^ shift, parentId, *link, warnings));
				continue;
			}
		}
```

> **「持ち主が root より上」**：Player の中の Sword を選んで Apply すると、`SaveTree` の root は Sword。Sword の下の物で、持ち主が Player（root より上）の物は、Player.prefab で Sword の下に足した物。それは Sword.prefab の中身として書く。

### C-3 `Scene/SceneSerializer.cpp`（読む側）

`LoadInstance` を書き換える（シーンの項目ごと `InstantiateNow` に渡す。名前・有効・Transform も中で当たる）。

```cpp
// ===== Prefab から置いた物を作る（中身は Prefab のファイルから。Prefab が読めなければ、印だけ付けた空の Entity）=====
// 一番上の名前・有効・Transform は、item（シーンファイルの1項目）のとおりになる
Handle<Entity> LoadInstance(const SceneJson& item, const std::string& path, std::set<std::string>& warnings) {
	const uint32_t prefab = PrefabManager::Load(ToPath(path));
	const Handle<Entity> root = PrefabManager::InstantiateNow(prefab, item, warnings);
	if (root.IsValid()) {
		return root;
	}
	// --- ファイルが無い（まだ pull していない など）。印は残すので、このまま保存しても参照は消えない ---
	warnings.insert(std::format("Prefab が読めないので、空の Entity にしました（ファイルが届いてからシーンを開き直すと、中身ができます）: {}", path));
	const Handle<Entity> empty = EntityManager::CreateWithId(item.value("id", EntityId{0}), item.value("name", std::string("Entity")), {});
	PrefabInstanceComponent link;
	link.path = path;
	EntityManager::RestoreComponents(empty, {ComponentSnapshot::Make(link)});
	// 置いた位置は残す（Transform だけ読む。ほかの Component は Prefab の中身なので、ここには無い）
	if (const auto components = item.find("components"); components != item.end() && components->is_object() && components->contains(kTransformName)) {
		SceneJson transform = SceneJson::object();
		transform[kTransformName] = components->at(kTransformName);
		ComponentSerializer::Load(empty, transform, warnings);
	}
	return empty;
}
```

`LoadAll` の1周目。`isInstance` を覚える配列を足す。

```cpp
	std::vector<Handle<Entity>> created(entities.size()); // 作れなかった所は無効なHandleのまま
	std::vector<bool> isInstance(entities.size());        // Prefab から置いた物か（Component はもう付いているので、3周目で読まない）
```

```cpp
		// "prefab" があれば Prefab から置いた物（中身も一緒にできる。一番上の Transform などもファイルの値になる）
		const auto prefab = item.find(kPrefabKey);
		isInstance[i] = (prefab != item.end() && prefab->is_string());
		created[i] = isInstance[i] ? LoadInstance(item, prefab->get<std::string>(), warnings) : EntityManager::CreateWithId(id, name, {});
```

3周目で、置いた物を飛ばす。

```cpp
		if (!created[i].IsValid() || isInstance[i] || componentsIt == entities[i].end() || !componentsIt->is_object()) {
```

> 3周目で読まないのは、続きの md で `"components"` に「上書きの項目だけ」を書くようになるから（足りない項目を初期値にされると困る）。この md の時点では Transform しか無いので、読んでも同じ。

## 2章 確認（エディタで）

1. 空のシーンに `Sword`（子に `Blade`）を作り、`Blade` に Camera Look At を付けて Target に `Sword`。`Sword` を Save As Prefab。
2. 空のシーンに `Player`（子に `Hand`・`Watcher`）を作る。Project の `Sword.prefab` → Place in Scene → できた `Sword` を `Hand` の子にドラッグ。`Watcher` に Camera Look At を付けて Target に `Blade`。
3. `Player` を Save As Prefab。`Player.prefab` を開く：`Sword` の項目に `"prefab"` があり、`Blade` の項目が無い。
4. Hierarchy：`Sword` も青い。`Blade` を選ぶと Inspector に `Prefab: resources/prefabs/Sword.prefab（の中身）`。
5. シーンを保存して開き直す：`Blade` ができ、`Watcher` の Target は `Blade`、`Blade` の Target は `Sword`。
6. `Blade` を動かす → `Sword` を選んで Apply。`Player.prefab` は変わらない（git の diff に出ない）。
7. Project の `Player.prefab` → Place in Scene をもう1回。2つ目の `Blade` は動かした位置。2つ目の `Watcher` は2つ目の `Blade` を見る。
8. （止まるか）新しい Entity `Loop` を Save As Prefab → `Loop.prefab` を `Loop` の子に Place in Scene → `Loop` を Apply → `Loop.prefab` をもう1回 Place in Scene：止まらず、Console に「Prefab の中に自分が入っている」。

## 3章 写しで確かめた結果

本体と同じコード（`feature/PrefabInstance` の 26a5129 から）を一時フォルダの写しに入れ、Sandbox で動かした（確かめるコードは写しの `main.cpp` にだけ置いた）。

- **ビルド**：この md の後（パートA〜C 全部）を Debug でビルド・確かめた。続きの `PrefabOverride.md` まで入れた状態で、Debug・Development・Release のビルドが通った（エラー 0・DirectXTex の前からある C4865 のほかは警告 0）。パートA だけ・B までの途中は確かめていない。
- **動き**（51 項目 OK）：
  - Prefab のファイル：入れ子は `"prefab"` と Transform だけ・中身を書かない・外側の中から入れ子の中身への参照が書ける・版 3。
  - 持ち主：Blade → Sword、Sword → Player、Hand → Player。
  - 読み直し：同じ番号で中身ができる。外側から中身への参照（Watcher → Blade）、入れ子の中の参照（Blade → Sword）、Script の中の参照（Sword の Script → Blade）が切れない。入れ子の位置は Player.prefab に書いた位置。
  - 入れ子を Apply：Sword.prefab だけが変わり、開き直すと Player の中の物も新しくなる。
  - 2つ目の Player：別の番号で、参照は2つ目の中を指す。
  - Play の退避・貼り付け・保存して読み直しで、数と持ち主が変わらない。
  - 自分の中に自分：止まる（空の Entity 1つ）。
  - 前からある Prefab（版 1）が作れる。
- **ログ**：わざと起こした物（自分の中に自分）のほかは、貼り付けた物の中の参照が元の物を指したまま（前からの動き。`PrefabOverride.md` で直す）の警告だけ。
- **確かめていない**：本物のマウスでの操作、版 2 で入れ子を中身ごと書いた Prefab（読めるはずだが、写しに無かった）、UI Screen の中の入れ子。

## この先

- 入れ子の中身をシーンで直した所は、この md ではまだ保存されない → `Docs/Tasks/PrefabOverride.md`（置いた所ごとの上書き）。
