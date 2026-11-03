参考：
- Unity の Prefab の上書き（Instance overrides・Apply・Revert）：https://docs.unity3d.com/Manual/PrefabInstanceOverrides.html
- nlohmann/json の `merge_patch`（RFC 7386 JSON Merge Patch）：https://json.nlohmann.me/api/basic_json/merge_patch/ ・ https://www.rfc-editor.org/rfc/rfc7386
- `std::remove_if`（erase と組で使う）：https://cpprefjp.github.io/reference/algorithm/remove_if.html
- `std::equal`（比べ方を渡す形）：https://cpprefjp.github.io/reference/algorithm/equal.html
- `std::max` に `{a, b, c}` を渡す形：https://cpprefjp.github.io/reference/algorithm/max.html
- 前の段階：`Docs/Tasks/PrefabInstance.md`（参照で置く）・`Docs/Tasks/PrefabNested.md`（入れ子の Prefab。**先に写す**）

# 置いた所ごとの上書き（Override）

> **2026-11-04：この md の中身は、写経せずに本体へ直接入れた。そのとき `PrefabManager` を `Scene/Prefab/` の4つに分けた**ので、パートA のコード（`Scene/PrefabManager.cpp` に足す形）と、本体の置き場所は違う。動きは同じ。解説（0章・1章・4章）はそのまま読める。コードは本体を見る。
>
> | この md の関数 | 本体の置き場所 |
> |---|---|
> | `IsSameValue`・`MakePatch`・`DiffComponents` | `Scene/Prefab/PrefabDiff.h`・`.cpp`（JSON だけを見る） |
> | `ApplyOverride`・`ApplyEntry`・`ShiftEntryIds`（`PrefabManager::ShiftEntry`） | `Scene/Prefab/PrefabRecords.h`・`.cpp`（`PrefabRecords::ApplyEntry`・`PrefabRecords::ShiftEntry`） |
> | `MakeInstanceEntry`・`HasOverrides`・`KeepUnreadEntry`（`unreadEntries`） | `Scene/Prefab/PrefabOverrides.h`・`.cpp`（`PrefabOverrides::MakeInstanceEntry` など。呼ぶ側の SceneSerializer・Inspector・EditorHistory もこの名前） |

Prefab から置いた物の中身（子も含む**全部の Component**・名前・有効・親）を、置いた所ごとに変えて、シーンに保存できるようにする（Unity の Override）。

今は、置いた物の中身をシーンで直しても保存されず、開き直すと Prefab のとおりに戻る。この md の後は：

- **シーンファイルには、Prefab と違う所だけを書く**：`Enemy.prefab` を5体置いて1体だけ体力を 200 にすると、その1体の項目に `"Health": 200` だけが書かれる
- **Prefab のほかの所を直すと、上書きしていない所は届く**：Enemy 担当が攻撃力を直すと、5体みんなに届く。体力 200 の1体も、攻撃力は新しくなる
- **入れ子にも効く**：Prefab の中の入れ子を直して Apply すると、外側の Prefab のファイルに入れ子の上書きが書かれる（`Sword.prefab` は変わらない）
- **Inspector**：上書きの数と、選んだ Entity の上書き（どの Component か）。**Apply**（Prefab に書く）・**Revert**（捨てて Prefab のとおりに。Ctrl+Z で戻せる）
- **ついでに直す前からの不具合2つ**：貼り付けた物の中の参照が元の物を指したまま・Undo で戻した物の Script の参照が外れる

パートは **A → B → C → D の順に写す**（B は A の `MakeInstanceEntry` を、C・D は A の関数を使う）。**ビルドは D まで写してから**（A で `IsModified` を消すので、B の `SceneSerializer.cpp` と D の `InspectorWindow.cpp` を直すまでは通らない）。

## 0章 全体の形

### 0.1 シーンファイルの形

```json
{
	"id": 1234, "name": "Enemy (3)", "parent": 0, "active": true,
	"prefab": "resources/prefabs/Enemy.prefab",
	"components": {
		"Transform": {"translation": [5.0, 0.0, 0.0], "rotation": [0.0, 90.0, 0.0], "scale": [1.0, 1.0, 1.0]},
		"EnemyStatus": {"Health": 200}
	},
	"removedComponents": ["Collider"],
	"overrides": [
		{"id": 5678, "name": "BigHead", "components": {"Model Renderer": {"texture": "resources/textures/red.png"}}},
		{"id": 9012, "active": false}
	],
	"removed": [3456]
}
```

| 項目 | 何か | いつ書くか |
|---|---|---|
| `"id"`・`"name"`・`"parent"`・`"active"`・`"prefab"` | 一番上（今までと同じ） | いつも |
| `"components"` | 一番上の Component。Transform は丸ごと、ほかは **違う項目だけ**（Prefab に無い型は丸ごと） | いつも（Transform があるので） |
| `"removedComponents"` | 一番上で外した Component の名前 | 外したとき |
| `"overrides"` | 中身1つずつの、違う名前・有効・親・Component（形は一番上と同じ。`"id"` で相手を決める） | 変えた中身があるとき |
| `"removed"` | 消した中身の番号（子も一緒に消える） | 消したとき |

あとから足した子は、今までどおりシーンにふつうに書く（`"overrides"` には入らない）。Prefab のファイルの中の入れ子（`PrefabNested.md`）も同じ形。

### 0.2 作る物・直す物

| ファイル | パート | 役目 |
|---|---|---|
| `Scene/PrefabManager.h`・`.cpp` | A | 違いを取る（`IsSameValue`・`MakePatch`・`DiffComponents`）・上書きを当てる（`ApplyOverride`・`ApplyEntry`）・`MakeInstanceEntry`・`HasOverrides`（`IsModified` は消す）・`ShiftEntry`・`KeepUnreadEntry` |
| `Scene/SceneSerializer.h`・`.cpp` | B | 版 4・置いた物を `MakeInstanceEntry` で書く・読めない Prefab の上書きを覚える |
| `Editor/History/EditorHistory.h`・`.cpp` | C | Undo の戻し方の順番・貼り付けの参照・中身の動かし方の決まり・`RequestRevertPrefab` |
| `Editor/Windows/InspectorWindow.cpp` | D | 上書きの数・選んだ Entity の上書き・Revert ボタン |
| `Editor/Windows/HierarchyWindow.cpp` | D | 右クリックに Revert Prefab |
| `Editor/Windows/ProjectWindowActions.cpp`・`Components/Core/PrefabInstanceComponent.h` | D | 説明の文だけ |

### 0.3 使い方

- **上書きする**：置いた物の中身を、Inspector でふつうに直す。シーンを保存すると、違う所だけが書かれる。Inspector の Prefab の所に「上書き：2 個の Entity」と、選んだ Entity の上書き（「この Entity：Collider・名前」など）が出る。
- **Prefab に書く（Apply）**：上書きを全部、元の Prefab に書く。上書きは無くなる（Prefab と同じになる）。
- **捨てる（Revert）**：上書きを全部捨てて、Prefab のとおりに作り直す。**あとから足した子も消える**。一番上の名前・位置はそのまま。Ctrl+Z で戻せる。入れ子の中では押せない（一番外側の置いた物で押す）。
- **中身の親を変える**：同じ置いた物の中身の間でなら動かせる（親を変えたことも上書きとして保存される）。外へ出す・あとから足した子の下に入れるのは、今までどおり断る。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 上書きの細かさ | 項目ごと（Component の中の、違う項目だけ） | Component ごと丸ごとだと、体力だけ変えた敵に Prefab の攻撃力の直しが届かない |
| 違いの取り方 | 保存するときに、Prefab のとおりに並べた物（`Expand`）と今の物を JSON で比べる | 「どこを触ったか」を覚えておかなくてよい。Undo・貼り付け・Play の退避の後でも、同じ答えになる |
| 小数 | とても小さな違い（値の大きさの 10万分の1）は同じと見る | Transform の回転は「度 → クォータニオン → 度」と変換するので、読み直すと最後の桁が変わる。違いと見ると、開くたびに Transform の上書きが増える |
| 整数 | そのまま比べる | Entity の番号（64 ビット）を float にすると、違う番号が同じに見える |
| 一番上の Transform | いつも丸ごと書く（今までと同じ） | 置いた所ごとに必ず違う |
| 上書きの相手が Prefab から消えた | 読むときに警告を出して、その上書きを外す | Prefab 担当が子を消したら、上書きも意味が無い |
| 読めない Prefab | 読んだときの項目を覚えて、保存するときにそのまま書き戻す | まだ pull していない人が保存しても、ほかの人の上書きが消えない |
| Revert | 一度消して、同じ番号で Prefab から作り直す。前の物を写しておき、Undo で戻す | 上書きを1つずつ戻すより確か（足した子・消した中身も戻る）。番号が同じなので、ほかの物からの参照も切れない |
| 入れ子の中の Revert | 押せない | 入れ子の中身の「元」は、外側の Prefab に書いた値か、内側の Prefab の値か、が分かりにくい |
| 中身を動かす | 同じ一番外側の置いた物の中身の間だけ | あとから足した子は、読むときに中身より後に作られるので、その下には入れられない |
| 版 | 4 にする | 版 3 のエンジンで読むと、上書きを知らずに捨てて保存してしまう |

## 1章 解説

### 1.1 保存するとき（`MakeInstanceEntry`）

1. 置いた物の一番上の番号で、Prefab を `Expand` する（`PrefabNested.md` 1.3。Prefab のとおりの中身を、今と同じ番号で並べた JSON）。
2. 一番上：今の Component を書いた JSON（`ComponentSerializer::Save`）と、並べた物の一番上を比べる（`DiffComponents`）。
3. 中身：並べた物を1つずつ見て、同じ番号の Entity を探す。
   - **居ない** → `"removed"`（親も消していれば、親だけ書く）。
   - **居る** → 名前・有効・親・Component を比べて、違う所だけを `"overrides"` に足す。
4. あとから足した子は、並べた物に番号が無いので見ない（シーンにふつうに書かれる）。

### 1.2 違いの取り方（`MakePatch`）

`MakePatch(前, 後)` は、`前.merge_patch(差分)` で `後` になる「差分」を作る。

| 前 | 後 | 差分 |
|---|---|---|
| `{"Health": 100, "Attack": 10}` | `{"Health": 200, "Attack": 10}` | `{"Health": 200}` |
| `{"translation": [0, 1, 0]}` | `{"translation": [0, 2, 0]}` | `{"translation": [0, 2, 0]}`（配列は丸ごと） |
| `{"Health": 100}` | `{"Health": 100}` | `{}`（同じ） |

- `{...}` の中は項目ごとに見る（違う項目だけ書く）。数・配列・文字は、違えば丸ごと書く。
- 前にしか無い項目は `null`（merge_patch の「消す」）。Component の項目はふつう全部書かれるので、まず出ない。

読むときは、Prefab のとおりに並べた物に、書いてある差分を `merge_patch` で当てる（`ApplyOverride`）。だから、**差分に書いていない項目は Prefab の値**になる（Prefab を直すと届く）。

### 1.3 番号の行き来（`ShiftEntry`）

上書きの中の番号（`"id"`・`"parent"`・`"removed"`・Component の中の参照）は、シーンでは今の番号で書く。Prefab のファイルの中の入れ子では、ファイルの番号（ずれを戻した番号）で書く。

どちらも「全員を同じ数で XOR」なので、同じ関数（`ShiftEntry`）で行き来できる。番号は `^ shift`、Component の中の参照は `RemapRefs`（`PrefabNested.md` 1.2）で付け替える。

### 1.4 Revert と Undo

Revert は `EditorHistory` の中で、次の順にする（フレームの頭＝境目なので、その場で消す・作るができる）。

1. 今の物を写す（`Capture`。削除の Undo と同じ写し）。
2. 消す（`Destroy`・`FlushDestroy`）。
3. 同じ番号で、上書きを外した項目から作り直す（`InstantiateNow`）。
4. 作り直した物を写し、「Undo：作り直した物を消して前の写しを戻す／Redo：その逆」を履歴に積む。

### 1.5 前からの不具合2つ

| 不具合 | 原因 | 直し方 |
|---|---|---|
| Undo で戻した物の Script の参照が外れる（剣の Script が、剣の中の刃を指していた など） | 写しから戻すとき、Entity を1つ作るたびに、その Script も戻していた。Script を読むときに相手が居るかを調べるので、後ろに並ぶ相手（子）はまだ居ない | 先に全員を作ってから、Component・Script を戻す（C-1） |
| 貼り付けた物の中の参照が、元の物を指す | 写しの Component はバイトのままで、番号を直していなかった | 一度作ってから、Component を JSON にして `RemapRefs` で番号を直し、写し直す（C-2） |

上書きが保存されるようになったので、どちらも放っておくと「元の物を指す」が上書きとしてシーンに残ってしまう。

### 1.6 あまり使わない STL

| STL | 何をする物か | ここでの使い方 |
|---|---|---|
| `std::remove_if(first, last, 条件)` と `erase` | `remove_if` は、条件に合わない物を前へ詰めて「新しい終わり」を返す（消しはしない）。`v.erase(新しい終わり, last)` で本当に消す | 消した中身を、並べた物から取り除く |
| `std::equal(a.begin(), a.end(), b.begin(), 比べ方)` | 2つの範囲を、渡した関数で1つずつ比べる | 配列（`[x, y, z]`）を、小数の丸めを許して比べる |
| `(std::max)({a, b, c})` | 3つ以上の中の一番大きい値（`{...}` で渡す）。`( )` で囲むのは、Windows.h の `max` マクロとぶつからないように | 小数の「同じと見る幅」を、値の大きさに合わせる |

## パートA 違いを取る・上書きを当てる（PrefabManager）

### A-1 `Scene/PrefabManager.h`

クラスの説明の、`番号のずれ` の `<para>` の次に足す。

```cpp
/// <para>上書き：置いた物の中身（子も含む全部の Component・名前・有効・親）を、置いた所ごとに変えてよい。シーンファイルには違う所だけが書かれ、
/// Prefab のほかの所を直すと、上書きしていない所は Prefab のとおりに変わる（Unity の Override）</para>
```

`IsModified` を消して、次の4つにする。

```cpp
	/// <summary>
	/// 置いた物を、シーンファイルの1項目の形にする（{"id", "name", "parent", "active", "prefab", "components", "removedComponents", "overrides", "removed"}）
	/// <para>Prefab のとおりに作った物と比べて、置いた所ごとに変えた所（上書き）だけを書く。番号は今の番号。一番上の Transform はいつも書く</para>
	/// <para>"components"：一番上の Component の違う項目だけ（Prefab に無い型は丸ごと）。"removedComponents"：外した型</para>
	/// <para>"overrides"：中身1つずつの、違う名前・有効・親・Component。"removed"：消した中身。あとから足した子は書かない（シーンにふつうに書く）</para>
	/// </summary>
	static SceneJson MakeInstanceEntry(Handle<Entity> root, std::set<std::string>& warnings);
	// 置いた所ごとの上書きがあるか（一番上の名前・有効・親・Transform と、あとから足した子は数えない）
	static bool HasOverrides(Handle<Entity> root);
	// 置いた物の項目（MakeInstanceEntry の形）の番号を全部 shift で XOR し、Component の中の参照を map で付け替える（Prefab のファイルに書くとき）
	static SceneJson ShiftEntry(SceneJson entry, EntityId shift, const EntityIdMap& map);
	// Prefab が読めなかった置いた物の、ファイルの項目を覚える（MakeInstanceEntry が、上書きをそのまま書き戻す）
	static void KeepUnreadEntry(const SceneJson& entry);
```

### A-2 `Scene/PrefabManager.cpp`（include・持つ物）

include に2つ足す。

```cpp
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
```

`PrefabState` の最後に足す。

```cpp
	uint64_t generation = 1;               // どれかの Prefab の中身が変わるたびに増える（PrefabAsset::contentIds を作り直す合図）
	// Prefab が読めなかった置いた物の、ファイルの項目（一番上の番号 → 項目）。保存するときに上書きをそのまま書き戻す
	std::unordered_map<EntityId, SceneJson> unreadEntries;
};
```

### A-3 `Scene/PrefabManager.cpp`（違いを取る）

`Relabel` の次（「中身の番号」の大見出しの前）に足す。

```cpp
//=============================================================================
// 違いを調べる（置いた所ごとの上書き）
//=============================================================================
// ===== 2つの値が同じか。小数は、とても小さな違い（丸め）なら同じと見る =====
// Transform の回転は「度 → クォータニオン → 度」と変換するので、読み直すと最後の桁が変わることがある
// 整数（Entity の番号など）は、そのまま比べる（float にすると大きな番号の違いが消える）
bool IsSameValue(const SceneJson& a, const SceneJson& b) {
	if (a.is_number_float() || b.is_number_float()) {
		if (!a.is_number() || !b.is_number()) {
			return false;
		}
		constexpr float kTolerance = 1.0e-5f; // 値の大きさに対して、これくらいの違いまでは同じ
		const float x = a.get<float>();
		const float y = b.get<float>();
		return std::abs(x - y) <= kTolerance * (std::max)({1.0f, std::abs(x), std::abs(y)});
	}
	if (a.is_array() && b.is_array()) {
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), &IsSameValue);
	}
	if (a.is_object() && b.is_object()) {
		if (a.size() != b.size()) {
			return false;
		}
		for (auto it = a.begin(); it != a.end(); ++it) {
			const auto found = b.find(it.key());
			if (found == b.end() || !IsSameValue(*it, *found)) {
				return false;
			}
		}
		return true;
	}
	return a == b;
}

// ===== {...} の before を after にするための差分（merge_patch に渡す形）。同じなら空の {} =====
// {...} どうしは項目ごとに見て、違う項目だけを書く。それ以外（数・配列・文字）は、違えば after をそのまま書く
SceneJson MakePatch(const SceneJson& before, const SceneJson& after) {
	SceneJson patch = SceneJson::object();
	for (auto it = after.begin(); it != after.end(); ++it) {
		const auto found = before.find(it.key());
		if (found == before.end()) {
			patch[it.key()] = *it;
		} else if (!IsSameValue(*found, *it)) {
			patch[it.key()] = (found->is_object() && it->is_object()) ? MakePatch(*found, *it) : *it;
		}
	}
	// before にしか無い項目は null（merge_patch で「消す」の印）
	for (auto it = before.begin(); it != before.end(); ++it) {
		if (!after.contains(it.key())) {
			patch[it.key()] = nullptr;
		}
	}
	return patch;
}

// ===== Component の違い。patches に上書き（Prefab に無い型は丸ごと）、removed に外した型の名前を足す =====
void DiffComponents(const SceneJson& before, const SceneJson& after, SceneJson& patches, SceneJson& removed) {
	for (auto it = after.begin(); it != after.end(); ++it) {
		const auto found = before.find(it.key());
		if (found == before.end()) {
			patches[it.key()] = *it; // 足した Component
			continue;
		}
		SceneJson patch = MakePatch(*found, *it);
		if (!patch.empty()) {
			patches[it.key()] = std::move(patch);
		}
	}
	for (auto it = before.begin(); it != before.end(); ++it) {
		if (!after.contains(it.key())) {
			removed.push_back(it.key()); // 外した Component
		}
	}
}
```

### A-4 `Scene/PrefabManager.cpp`（上書きを当てる）

`ApplyEntry` を消して、次の3つにする（`MakeEmptyRecord` の次）。

```cpp
// ===== Entity 1つ分に、上書き（書いてある名前・有効・親・Component と、外した Component）を当てる =====
// Component は書いてある項目だけを上書きする（merge_patch。{"translation": [...]} なら translation だけ変わる）。Prefab に無い型は足す
void ApplyOverride(SceneJson& record, const SceneJson& override) {
	for (const char* key : {"name", "active", "parent"}) {
		if (const auto it = override.find(key); it != override.end()) {
			record[key] = *it;
		}
	}
	if (!record.contains("components") || !record["components"].is_object()) {
		record["components"] = SceneJson::object();
	}
	SceneJson& target = record["components"];
	if (const SceneJson* components = FindComponents(override)) {
		for (auto it = components->begin(); it != components->end(); ++it) {
			if (target.contains(it.key()) && target[it.key()].is_object() && it->is_object()) {
				target[it.key()].merge_patch(*it);
			} else {
				target[it.key()] = *it;
			}
		}
	}
	if (const auto removed = override.find("removedComponents"); removed != override.end() && removed->is_array()) {
		for (const SceneJson& name : *removed) {
			if (name.is_string()) {
				target.erase(name.get<std::string>());
			}
		}
	}
}

// ===== 置いた物の項目の番号（一番上・親・上書きの相手・消した中身）を全部 shift で XOR し、Component の中の参照を map で付け替える =====
// Prefab のファイルの番号 ⇔ 置いた番号 の行き来に使う（どちらも全員が同じ数で XOR なので、同じ関数で行き来できる）
SceneJson ShiftEntryIds(SceneJson entry, EntityId shift, const EntityIdMap& map) {
	// Entity 1つ分（一番上・上書き1つ）の番号と参照
	const auto shiftOne = [shift, &map](SceneJson& item) {
		if (const auto id = item.find("id"); id != item.end() && id->is_number_unsigned()) {
			*id = id->get<EntityId>() ^ shift;
		}
		if (const auto parent = item.find("parent"); parent != item.end() && parent->is_number_unsigned() && parent->get<EntityId>() != 0) {
			*parent = parent->get<EntityId>() ^ shift;
		}
		if (const auto components = item.find("components"); components != item.end()) {
			ComponentSerializer::RemapRefs(*components, map);
		}
	};
	shiftOne(entry);
	if (const auto overrides = entry.find("overrides"); overrides != entry.end() && overrides->is_array()) {
		for (SceneJson& override : *overrides) {
			shiftOne(override);
		}
	}
	if (const auto removed = entry.find("removed"); removed != entry.end() && removed->is_array()) {
		for (SceneJson& id : *removed) {
			if (id.is_number_unsigned()) {
				id = id.get<EntityId>() ^ shift;
			}
		}
	}
	return entry;
}

// ===== 置いた物の項目（"prefab" の付いた1項目。番号は置いた番号）を、並べた中身 records[begin, begin + count) に当てる =====
// 一番上（records[begin]）：名前・有効・親・Component の上書き（"components"）・外した Component（"removedComponents"）
// "overrides"：中身1つずつの上書き（形は一番上と同じ。"id" で相手を決める）
// "removed"：消した中身の番号（その子も一緒に消える）
void ApplyEntry(EntityRecords& records, size_t begin, size_t count, const SceneJson& entry, std::set<std::string>& warnings) {
	// --- 一番上（名前・有効・親は、書いていなくても置いた所ごとの値にする）---
	SceneJson& root = records[begin];
	ApplyOverride(root, entry);
	root["active"] = entry.value("active", true);
	root["parent"] = entry.value("parent", EntityId{0});
	const EntityId rootId = root.at("id").get<EntityId>();

	// --- 中身の上書き（番号 → records の位置。相手が Prefab から消えていたら、その上書きは外す）---
	std::unordered_map<EntityId, size_t> positions;
	for (size_t i = begin; i < begin + count; ++i) {
		positions[records[i].at("id").get<EntityId>()] = i;
	}
	if (const auto overrides = entry.find("overrides"); overrides != entry.end() && overrides->is_array()) {
		for (const SceneJson& override : *overrides) {
			const EntityId id = override.is_object() ? override.value("id", EntityId{0}) : EntityId{0};
			const auto found = positions.find(id);
			if (found == positions.end()) {
				warnings.insert(std::format("上書きの相手（EntityId {}）が Prefab にありません（Prefab から消された？）。この上書きは外れます: {}", id, entry.value("name", std::string())));
				continue;
			}
			ApplyOverride(records[found->second], override);
		}
	}

	// --- 消した中身（子も一緒に。上書きで親を変えた後の親子で見る）---
	std::unordered_set<EntityId> removed;
	if (const auto list = entry.find("removed"); list != entry.end() && list->is_array()) {
		for (const SceneJson& id : *list) {
			if (id.is_number_unsigned() && id.get<EntityId>() != rootId) {
				removed.insert(id.get<EntityId>());
			}
		}
	}
	if (removed.empty()) {
		return;
	}
	// 子の子まで広げる（親を変えた物は、親が後ろに並ぶこともあるので、増えなくなるまで繰り返す）
	for (bool isGrown = true; isGrown;) {
		isGrown = false;
		for (size_t i = begin; i < begin + count; ++i) {
			const EntityId id = records[i].at("id").get<EntityId>();
			if (!removed.contains(id) && removed.contains(records[i].value("parent", EntityId{0}))) {
				removed.insert(id);
				isGrown = true;
			}
		}
	}
	const auto first = records.begin() + static_cast<std::ptrdiff_t>(begin);
	const auto last = first + static_cast<std::ptrdiff_t>(count);
	records.erase(std::remove_if(first, last, [&removed](const SceneJson& record) { return removed.contains(record.at("id").get<EntityId>()); }), last);
}
```

> `ApplyEntry` に `count`（並べた物のうち、どこまでがこの置いた物か）と `warnings` が増えた。入れ子の上書きは、その入れ子の中身にだけ当てる（ほかの入れ子に同じ番号は無いが、探す範囲を狭くする）。

### A-5 `Scene/PrefabManager.cpp`（`Expand`）

`Expand` を全部これにする。変わったのは、入れ子の覚え方（`NestedRange` に数を足した）と、3周目（後ろから・`ShiftEntryIds` で番号を直してから `ApplyEntry`）。

```cpp
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
	std::vector<size_t> plainIndices; // ファイルにそのまま書いてあった項目の、records での位置
	// 入れ子を差し込んだ所（先頭の位置・数・ファイルのその項目）
	struct NestedRange {
		size_t begin = 0;
		size_t count = 0;
		const SceneJson* item = nullptr;
	};
	std::vector<NestedRange> nested;
	for (const SceneJson& item : *entities) {
		const EntityId id = item.at("id").get<EntityId>() ^ shift;
		if (const auto prefab = item.find(kPrefabKey); prefab != item.end()) {
			const std::string& nestedPath = prefab->get_ref<const std::string&>();
			EntityRecords sub = Expand(PrefabManager::Load(ToPath(nestedPath)), id, stack, warnings);
			if (sub.empty()) {
				warnings.insert(std::format("入れ子の Prefab が読めないので、空の Entity にしました（ファイルが届いてから開き直すと、中身ができます）: {}", nestedPath));
				sub.push_back(MakeEmptyRecord(id));
			}
			// 入れ子の一番上に、置いた物の印を付ける（パスはファイルに書いてあるとおり）
			sub.front()["components"][kPrefabInstanceName] = SceneJson{{"path", nestedPath}};
			nested.push_back({records.size(), sub.size(), &item});
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

	// --- 3周目：入れ子の項目（ファイルに書いてある名前・Transform・上書きなど）を、置いた番号にしてから当てる ---
	// 後ろから当てる（消した中身があると records が詰まるので、前から当てると、後の入れ子の位置がずれる）
	for (auto range = nested.rbegin(); range != nested.rend(); ++range) {
		ApplyEntry(records, range->begin, range->count, ShiftEntryIds(*range->item, shift, map), warnings);
	}
	records.front()["parent"] = EntityId{0}; // 一番上の親は、置く側が決める
	stack.pop_back();
	return records;
}

```

> **後ろから当てる理由**：入れ子の上書きに `"removed"` があると、`records` からその中身が消えて、後ろの物が前へ詰まる。前から当てると、まだ当てていない後ろの入れ子の `begin`（位置）がずれる。後ろから当てれば、前の入れ子の位置は変わらない。

### A-6 `Scene/PrefabManager.cpp`（作る・道具）

`InstantiateNow` の `ApplyEntry` の呼び方を直す。

```cpp
	ApplyEntry(records, 0, records.size(), entry, warnings);
```

無名の名前空間の最後の `ComponentsWithoutTransform` を消して、これにする。

```cpp
// ===== Entity 1つ分の今の Component（Save と同じ形）=====
SceneJson SaveComponents(Handle<Entity> handle, std::set<std::string>& warnings) {
	SceneJson components = SceneJson::object();
	ComponentSerializer::Save(handle, components, warnings);
	return components;
}
```

### A-7 `Scene/PrefabManager.cpp`（`MakeInstanceEntry` など）

`IsModified` を消して、次の4つにする（`FindInstanceRoot` の次）。

```cpp
SceneJson PrefabManager::MakeInstanceEntry(Handle<Entity> root, std::set<std::string>& warnings) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	const Entity* entity = EntityManager::Get(root);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	if (entity == nullptr || link == nullptr) {
		return {};
	}
	// ===== 置いた所ごとに必ず書く物（一番上の番号・名前・親・有効・Prefab のパス・Transform）=====
	const Entity* parent = EntityManager::Get(entity->parent);
	SceneJson entry = SceneJson::object();
	entry["id"] = entity->id;
	entry["name"] = entity->name;
	entry["parent"] = (parent != nullptr) ? parent->id : EntityId{0};
	entry["active"] = entity->isActive;
	entry[kPrefabKey] = link->path.ToString();
	SceneJson nowRoot = SaveComponents(root, warnings);
	nowRoot.erase(kPrefabInstanceName); // 印は書かない（"prefab" で分かる）
	SceneJson components = SceneJson::object();
	if (const auto transform = nowRoot.find(kTransformName); transform != nowRoot.end()) {
		components[kTransformName] = *transform;
	}
	nowRoot.erase(kTransformName);

	// ===== Prefab が読めない：読んだときの上書きをそのまま残す（Prefab をまだ pull していない人が保存しても、上書きが消えないように）=====
	const PrefabAsset* asset = FindAssetOf(root);
	if (asset == nullptr) {
		const auto kept = state->unreadEntries.find(entity->id);
		if (kept == state->unreadEntries.end()) {
			entry["components"] = std::move(components);
			return entry;
		}
		SceneJson keptEntry = kept->second;
		for (const char* key : {"id", "name", "parent", "active"}) {
			keptEntry[key] = entry[key];
		}
		if (components.contains(kTransformName)) {
			keptEntry["components"][kTransformName] = components[kTransformName];
		}
		return keptEntry;
	}

	// ===== Prefab のとおりに並べた物（上書きの基準）と比べる =====
	std::vector<uint32_t> stack;
	const EntityRecords records = Expand(link->prefab, entity->id, stack, warnings);
	// --- 一番上の Component（Transform は上で丸ごと書いたので比べない）---
	SceneJson baseRoot = (records.empty() || FindComponents(records.front()) == nullptr) ? SceneJson::object() : *FindComponents(records.front());
	baseRoot.erase(kPrefabInstanceName);
	baseRoot.erase(kTransformName);
	SceneJson removedComponents = SceneJson::array();
	DiffComponents(baseRoot, nowRoot, components, removedComponents);
	entry["components"] = std::move(components);
	if (!removedComponents.empty()) {
		entry["removedComponents"] = std::move(removedComponents);
	}

	// --- 中身1つずつ（名前・有効・親・Component）。居なくなった物は「消した」 ---
	SceneJson overrides = SceneJson::array();
	SceneJson removed = SceneJson::array();
	std::unordered_set<EntityId> removedIds;
	for (size_t i = 1; i < records.size(); ++i) {
		const SceneJson& record = records[i];
		const EntityId id = record.at("id").get<EntityId>();
		const Handle<Entity> handle = EntityManager::FindById(id);
		const Entity* content = EntityManager::Get(handle);
		if (content == nullptr) {
			// 親も消していれば、親だけ書く（子は読むときに一緒に消える）
			if (!removedIds.contains(record.value("parent", EntityId{0}))) {
				removed.push_back(id);
			}
			removedIds.insert(id);
			continue;
		}
		SceneJson override = SceneJson::object();
		override["id"] = id;
		if (content->name != record.value("name", std::string("Entity"))) {
			override["name"] = content->name;
		}
		if (content->isActive != record.value("active", true)) {
			override["active"] = content->isActive;
		}
		const Entity* contentParent = EntityManager::Get(content->parent);
		const EntityId parentId = (contentParent != nullptr) ? contentParent->id : EntityId{0};
		if (parentId != record.value("parent", EntityId{0})) {
			override["parent"] = parentId;
		}
		SceneJson patches = SceneJson::object();
		SceneJson removedOne = SceneJson::array();
		DiffComponents(FindComponents(record) != nullptr ? *FindComponents(record) : SceneJson::object(), SaveComponents(handle, warnings), patches, removedOne);
		if (!patches.empty()) {
			override["components"] = std::move(patches);
		}
		if (!removedOne.empty()) {
			override["removedComponents"] = std::move(removedOne);
		}
		if (override.size() > 1) {
			overrides.push_back(std::move(override)); // "id" だけなら、何も変えていない
		}
	}
	if (!overrides.empty()) {
		entry["overrides"] = std::move(overrides);
	}
	if (!removed.empty()) {
		entry["removed"] = std::move(removed);
	}
	return entry;
}

bool PrefabManager::HasOverrides(Handle<Entity> root) {
	std::set<std::string> warnings; // 調べるだけなので、警告は出さない（保存するときに出る）
	const SceneJson entry = MakeInstanceEntry(root, warnings);
	if (!entry.is_object()) {
		return false;
	}
	const SceneJson* components = FindComponents(entry);
	const bool hasRootOverride = (components != nullptr) && (components->size() > (components->contains(kTransformName) ? 1u : 0u));
	return hasRootOverride || entry.contains("removedComponents") || entry.contains("overrides") || entry.contains("removed");
}

SceneJson PrefabManager::ShiftEntry(SceneJson entry, EntityId shift, const EntityIdMap& map) { return ShiftEntryIds(std::move(entry), shift, map); }

void PrefabManager::KeepUnreadEntry(const SceneJson& entry) {
	MY_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	state->unreadEntries[entry.value("id", EntityId{0})] = entry;
}
```

> **Prefab が読めないとき**（`asset == nullptr`）：並べる元が無いので、違いは取れない。読んだときの項目（B-2 で覚える）をそのまま返し、置いた所ごとに変えてよい物（番号・名前・親・有効・Transform）だけ今の値にする。

## パートB シーンに上書きを書く（SceneSerializer）

### B-1 `Scene/SceneSerializer.h`

クラスの説明の4行目と、版を直す。

```cpp
/// <para>シーンファイルには、Prefab から置いた物を参照（"prefab" と一番上の Transform）と、置いた所ごとの上書きだけで書く。Play の退避の文字列は中身も全部書く</para>
```

```cpp
	// 3：Prefab のファイルの中の入れ子の Prefab も参照で書く。2 のファイルはそのまま読める（入れ子が中身ごと書いてあるだけ）
	// 4：置いた所ごとの上書き（"overrides" など）を書く。3 のファイルはそのまま読める（上書きが無いだけ）
	static constexpr int kVersion = 4;
```

### B-2 `Scene/SceneSerializer.cpp`

`SaveInstance` を全部これにする（中身は `MakeInstanceEntry` に任せる。「違う所は保存されない」の警告は要らなくなった）。

```cpp
// ===== Prefab から置いた物 1つ分（{"id", "name", "parent", "active", "prefab", "components", "overrides", "removed"}）=====
// 中身は書かない（読むときに Prefab のファイルから作り直す）。置いた所ごとに変えた所（上書き）だけを書く（PrefabManager::MakeInstanceEntry）
// shift・idMap を渡すと、番号を Prefab のファイルの番号にして書く（Prefab の中の入れ子。シーンなら 0 と nullptr）
SceneJson SaveInstance(const Entity& entity, EntityId shift, const EntityIdMap* idMap, std::set<std::string>& warnings) {
	SceneJson item = PrefabManager::MakeInstanceEntry(entity.self, warnings);
	return (idMap != nullptr) ? PrefabManager::ShiftEntry(std::move(item), shift, *idMap) : item;
}
```

`SaveAll` の呼び方を直す。

```cpp
			if (EntityManager::Get<PrefabInstanceComponent>(entity.self) != nullptr) {
				entities.push_back(SaveInstance(entity, 0, nullptr, warnings));
				continue;
			}
```

`SaveTree` の呼び方を直す（Prefab の中の入れ子は、ファイルの番号にして書く）。

```cpp
			if (EntityManager::Get<PrefabInstanceComponent>(queue[i]) != nullptr) {
				entities.push_back(SaveInstance(*entity, shift, &idMap, warnings));
				continue;
			}
```

`LoadInstance` の、ファイルが無いときに、項目を覚える（コメントも直す）。

```cpp
	// --- ファイルが無い（まだ pull していない など）。印と上書きは残すので、このまま保存しても参照・上書きは消えない ---
	warnings.insert(std::format("Prefab が読めないので、空の Entity にしました（ファイルが届いてからシーンを開き直すと、中身ができます）: {}", path));
	PrefabManager::KeepUnreadEntry(item);
```

> `SaveAll` の `parentId` は、置いた物では使わなくなった（`MakeInstanceEntry` が親を見る）。ふつうの Entity では今までどおり使う。

## パートC Undo・貼り付け・Revert（EditorHistory）

### C-1 `Editor/History/EditorHistory.cpp`（include・戻し方の順番）

include を足す。

```cpp
#include <algorithm>
#include <cstddef>
#include <format>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Scene/PrefabManager.h"
#include "MyEngine/Systems/Gameplay/ScriptSystem.h"
```

`Restore` の、作って戻す繰り返しを2つに分ける。

```cpp
	// 先に全員を作ってから、Component・Script を戻す（Script の中の参照は、相手が居ないと外れるので。親が子より前に並ぶので、親は先にできている）
	for (const EntityData& data : tree) {
		Handle<Entity> handle = EntityManager::CreateWithId(data.id, data.name, EntityManager::FindById(data.parent));
		EntityManager::Get(handle)->isActive = data.isActive;
	}
	for (const EntityData& data : tree) {
		const Handle<Entity> handle = EntityManager::FindById(data.id);
		// 予約せずその場で付ける（Flushの中＝フレームの境目なので安全）。Transformのように作った時点で持っている物は上書き
		EntityManager::RestoreComponents(handle, data.components);
		ScriptSystem::Restore(handle, data.scripts);
	}
```

### C-2 `Editor/History/EditorHistory.cpp`（貼り付け・一番外側の持ち主）

`Paste` を全部これにして、次に `TopOwnerOf` を足す。

```cpp
// 写しに新しい番号を振り直して、parentの子として作る
void Paste(EntityTree tree, EntityId parent) {
	if (tree.empty() || (parent != 0 && !EntityManager::FindById(parent).IsValid())) {
		return;
	}
	// 番号を振り直す。全員を同じ数（shift）で XOR する（子が覚えている親の番号も同じ数で XOR すれば、そのまま新しい親を指す）
	// 1つずつ別の番号にしないのは、貼った物に Prefab から置いた物が入っていても、Prefab の中の番号の並び（PrefabManager.h）が崩れないように
	const EntityId shift = MakeIdShift(tree);
	EntityIdMap idMap; // 元の番号 → 貼った番号（貼った物の中どうしの参照を、貼った物に向け直すため）
	for (EntityData& data : tree) {
		idMap[data.id] = data.id ^ shift;
		data.id ^= shift;
	}
	for (size_t i = 1; i < tree.size(); ++i) {
		tree[i].parent ^= shift;
	}
	tree.front().parent = parent;
	tree.front().name = MakeCopyName(tree.front().name, parent);

	// --- 中の参照を、貼った物に向け直す（写しの Component はバイトのままなので番号を直せない。一度作って JSON で直し、写し直す）---
	if (!Restore(tree)) {
		return;
	}
	std::set<std::string> warnings;
	for (const EntityData& data : tree) {
		const Handle<Entity> handle = EntityManager::FindById(data.id);
		SceneJson components = SceneJson::object();
		ComponentSerializer::Save(handle, components, warnings);
		SceneJson remapped = components;
		ComponentSerializer::RemapRefs(remapped, idMap);
		// 番号が変わった Component だけ読み直す（読み直すと Transform の回転がわずかにずれるので、変わらない物は触らない）
		SceneJson changed = SceneJson::object();
		for (auto it = remapped.begin(); it != remapped.end(); ++it) {
			if (*it != components[it.key()]) {
				changed[it.key()] = *it;
			}
		}
		if (!changed.empty()) {
			ComponentSerializer::Load(handle, changed, warnings);
		}
	}
	EntityTree fixed = Capture(EntityManager::FindById(tree.front().id));
	Remove(fixed);
	AddTree(std::move(fixed));
}

// ===== entity を中身として持つ、一番外側の置いた物（どの置いた物の中身でもなければ entity 自身）=====
Handle<Entity> TopOwnerOf(Handle<Entity> entity) {
	Handle<Entity> current = entity;
	for (Handle<Entity> owner = PrefabManager::FindOwner(current); owner.IsValid(); owner = PrefabManager::FindOwner(current)) {
		current = owner;
	}
	return current;
}
```

> 一度作ってから写し直すのは、`EntityTree` の Component が「型のバイトのまま」で、どこが参照か分からないから。作った Entity なら `ComponentSerializer::Save` で JSON にでき、`RemapRefs` で番号を直せる。履歴に積むのは直した後の写し（`fixed`）なので、Undo・Redo しても直した参照のまま。

### C-3 `Editor/History/EditorHistory.cpp`（中身の動かし方）

`RequestReparent` の中の、Prefab の中身を止める所を書き換える。

```cpp
		// Prefab の中身は、同じ置いた物の中身の間でだけ動かせる（親を変えたことは、置いた所ごとの上書きとして保存される）
		// 外へ出すと、開き直したときに Prefab から同じ番号でもう1つ作られてしまう。
		// あとから足した子の下にも入れない（開き直すと、中身は足した子より先に作られるので、親が見つからない）
		const Handle<Entity> owner = PrefabManager::FindOwner(childHandle);
		const Handle<Entity> newParent = EntityManager::FindById(parentId);
		if (owner.IsValid() && newParent != owner && TopOwnerOf(newParent) != TopOwnerOf(childHandle)) {
			LogManager::Warning(std::format("「{}」は Prefab の中身なので、同じ置いた物の中身の間でだけ動かせます（外へ出すときは、先に Inspector の Unpack）", entity->name));
			return;
		}
```

| 動かす先 | `TopOwnerOf` | 結果 |
|---|---|---|
| 持ち主（一番近い置いた物） | — | 動かせる（前と同じ） |
| 同じ置いた物の別の中身・入れ子の中身・一番外側の一番上 | 動かす物と同じ | 動かせる（親の上書きになる） |
| あとから足した子・置いた物の外 | 違う | 断る |

### C-4 `Editor/History/EditorHistory.cpp`・`.h`（Revert）

`.cpp`：`RequestDuplicate` の次（「Component」の大見出しの前）に足す。

```cpp
//=============================================================================
// Prefab
//=============================================================================
void EditorHistory::RequestRevertPrefab(Handle<Entity> root) {
	requests.push_back([id = IdOf(root)] {
		const Handle<Entity> handle = EntityManager::FindById(id);
		const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(handle);
		if (link == nullptr || link->prefab == 0) {
			return; // 置いた物でない・Prefab が読めていない
		}
		const uint32_t prefab = link->prefab;
		// --- 戻した後の項目：一番上の番号・名前・親・有効・Transform だけ（上書きは書かない）---
		std::set<std::string> warnings;
		SceneJson entry = PrefabManager::MakeInstanceEntry(handle, warnings);
		SceneJson components = SceneJson::object();
		if (entry["components"].contains("Transform")) {
			components["Transform"] = entry["components"]["Transform"];
		}
		entry["components"] = std::move(components);
		for (const char* key : {"removedComponents", "overrides", "removed"}) {
			entry.erase(key);
		}
		// --- 今の物を写してから消し、Prefab から作り直す（同じ番号でできるので、ほかの物からの参照もそのまま）---
		const EntityTree before = Capture(handle);
		EntityManager::Destroy(handle);
		EntityManager::FlushDestroy(); // Flushの中＝フレームの境目なので、すぐ消してよい
		const Handle<Entity> created = PrefabManager::InstantiateNow(prefab, entry, warnings);
		if (!created.IsValid()) {
			Restore(before);
			return;
		}
		if (const Handle<Entity> parent = EntityManager::FindById(entry.value("parent", EntityId{0})); parent.IsValid()) {
			EntityManager::SetParent(created, parent);
		}
		const EntityTree after = Capture(created);
		// Undo は作り直した物を消して前の写しを戻す。Redo はその逆
		Append({[before, after] { return Remove(after) && Restore(before); }, [before, after] { return Remove(before) && Restore(after); }});
		HierarchyWindow::SetSelected(created);
		LogManager::Log(std::format("「{}」を Prefab のとおりに戻しました: {}", entry.value("name", std::string()), PrefabManager::GetPath(prefab)));
	});
}
```

`.h`：`RequestDuplicate` の次に足す。

```cpp
	static void RequestDuplicate(Handle<Entity> handle); // 同じ親の下に複製する

	// ===== Prefab =====
	// Prefab から置いた物を、Prefab のとおりに作り直す（置いた所ごとの上書きと、あとから足した子が消える。一番上の名前・有効・親・Transform は残す）
	static void RequestRevertPrefab(Handle<Entity> root);
```

## パートD エディタの見せ方

### D-1 `Editor/Windows/InspectorWindow.cpp`

include を2つ足す。

```cpp
#include <memory>
#include <set>
#include <string>
```

```cpp
#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
```

無名の名前空間の頭（色と覚え）を書き換える。

```cpp
const ImVec4 kPrefabWarningColor = ImVec4(1.0f, 0.55f, 0.4f, 1.0f);  // Prefab が読めないときの文字の色
const ImVec4 kPrefabOverrideColor = ImVec4(1.0f, 0.80f, 0.35f, 1.0f); // 置いた所ごとの上書きがあるときの文字の色

/// <summary>
/// 置いた物の上書き（PrefabManager::MakeInstanceEntry）の覚え
/// <para>調べるたびに中身を全部 JSON に書くので、毎フレームはしない。履歴が動いたか、選ぶ物が変わったときだけ調べ直す</para>
/// </summary>
struct PrefabEntryCache {
	EntityId rootId = 0;      // 調べた置いた物
	uint64_t changeCount = 0; // 調べたときの EditorHistory::GetChangeCount
	bool isValid = false;     // false なら、次は必ず調べ直す（Apply した直後など）
	SceneJson entry;          // 調べた結果（シーンファイルに書く項目と同じ形）
};
PrefabEntryCache prefabEntryCache;
```

`IsPrefabModified` と `DrawPrefabInfo` を消して、これにする。

```cpp
// 置いた物の上書き（覚えた結果が使えれば、それを返す）
const SceneJson& GetPrefabEntry(Handle<Entity> root) {
	const EntityId rootId = EntityManager::Get(root)->id;
	const uint64_t changeCount = EditorHistory::GetChangeCount();
	PrefabEntryCache& cache = prefabEntryCache;
	if (!cache.isValid || cache.rootId != rootId || cache.changeCount != changeCount) {
		std::set<std::string> warnings; // 調べるだけなので出さない（保存するときに出る）
		cache = {rootId, changeCount, true, PrefabManager::MakeInstanceEntry(root, warnings)};
	}
	return cache.entry;
}

// ===== 1つの Entity の上書きを、短い文にする（"名前・Collider・外した: Script"。無ければ空）=====
// isRoot なら Transform は数えない（一番上の Transform は、置いた所ごとにいつも書くので）
std::string DescribeOverride(const SceneJson& item, bool isRoot) {
	std::vector<std::string> parts;
	if (item.contains("name") && !isRoot) {
		parts.emplace_back("名前");
	}
	if (item.contains("active") && !isRoot) {
		parts.emplace_back("有効");
	}
	if (item.contains("parent") && !isRoot) {
		parts.emplace_back("親");
	}
	if (const auto components = item.find("components"); components != item.end() && components->is_object()) {
		for (auto it = components->begin(); it != components->end(); ++it) {
			if (!isRoot || it.key() != "Transform") {
				parts.push_back(it.key());
			}
		}
	}
	if (const auto removed = item.find("removedComponents"); removed != item.end() && removed->is_array()) {
		for (const SceneJson& name : *removed) {
			parts.push_back(std::format("外した: {}", name.is_string() ? name.get<std::string>() : std::string()));
		}
	}
	std::string text;
	for (const std::string& part : parts) {
		text += text.empty() ? part : "・" + part;
	}
	return text;
}

// Prefab から置いた物なら、元の Prefab・置いた所ごとの上書き・ボタン（Apply・Revert・Unpack）を出す
void DrawPrefabInfo(Handle<Entity> handle) {
	const Handle<Entity> root = PrefabManager::FindInstanceRoot(handle);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	if (link == nullptr) {
		return;
	}
	const std::string path = link->path.ToString();
	ImGui::TextDisabled("Prefab: %s%s", path.c_str(), (root == handle) ? "" : "（の中身）");
	if (link->prefab == 0) {
		ImGui::TextColored(kPrefabWarningColor, "Prefab のファイルが読めません（届いたら、シーンを開き直す）");
		return;
	}
	// --- 置いた所ごとの上書き（シーンに保存される。Prefab のほかの所を直すと、上書きしていない所は変わる）---
	const SceneJson& entry = GetPrefabEntry(root);
	const std::string rootText = DescribeOverride(entry, true);
	const size_t overrideCount = entry.contains("overrides") ? entry["overrides"].size() : 0;
	const size_t removedCount = entry.contains("removed") ? entry["removed"].size() : 0;
	const size_t entityCount = overrideCount + (rootText.empty() ? 0 : 1);
	if (entityCount == 0 && removedCount == 0) {
		ImGui::TextDisabled("上書き：なし（Prefab と同じ）");
	} else {
		ImGui::TextColored(kPrefabOverrideColor, "上書き：%zu 個の Entity・消した中身 %zu 個（シーンに保存される）", entityCount, removedCount);
	}
	// この Entity の上書き
	std::string selectedText;
	if (handle == root) {
		selectedText = rootText;
	} else if (const Entity* entity = EntityManager::Get(handle); entity != nullptr && entry.contains("overrides")) {
		for (const SceneJson& item : entry["overrides"]) {
			if (item.value("id", EntityId{0}) == entity->id) {
				selectedText = DescribeOverride(item, false);
			}
		}
	}
	if (!selectedText.empty()) {
		ImGui::TextColored(kPrefabOverrideColor, "この Entity：%s", selectedText.c_str());
	}

	// --- ボタン ---
	if (root != handle) {
		if (ImGui::SmallButton("Select Root")) {
			HierarchyWindow::SetSelected(root);
		}
		ImGui::SameLine();
	}
	if (ImGui::SmallButton("Apply")) {
		PrefabManager::Apply(root);
		prefabEntryCache.isValid = false; // ファイルが変わったので調べ直す
	}
	ImGui::SetItemTooltip("上書きを全部、元の Prefab のファイルに書く（ほかのシーンの物も、次に開いたときから同じになる）");
	ImGui::SameLine();
	// Revert は一番外側の置いた物だけ（入れ子の中は、外側の Prefab の値に戻すのか、内側の値に戻すのかが分かりにくいので）
	const bool isOutermost = !PrefabManager::FindOwner(root).IsValid();
	ImGui::BeginDisabled(!isOutermost);
	if (ImGui::SmallButton("Revert")) {
		EditorHistory::RequestRevertPrefab(root);
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip(isOutermost ? "上書きを全部捨てて、Prefab のとおりに戻す（あとから足した子も消える。Ctrl+Z で戻せる）" : "入れ子の中は、一番外側の置いた物の Revert で戻す");
	ImGui::SameLine();
	if (ImGui::SmallButton("Unpack")) {
		PrefabManager::Unpack(root);
	}
	ImGui::SetItemTooltip("Prefab とのつながりを切る（中身もシーンファイルに書かれるようになる。Prefab を直しても変わらなくなる）");
}
```

### D-2 `Editor/Windows/HierarchyWindow.cpp`（`DrawEntityMenu`）

Prefab から置いた物の一番上のメニューを書き換える（Revert Prefab を足す）。

```cpp
	// --- Prefab から置いた物の一番上：元の Prefab に書く・Prefab のとおりに戻す・つながりを切る（Inspector のボタンと同じ）---
	if (PrefabManager::FindInstanceRoot(handle) == handle) {
		if (ImGui::MenuItem("Apply to Prefab")) {
			PrefabManager::Apply(handle);
		}
		ImGui::SetItemTooltip("上書きを全部、元の Prefab のファイルに書く（ほかのシーンの物も、次に開いたときから同じになる）");
		// 戻せるのは一番外側の置いた物だけ（入れ子の中は、外側の Revert で戻す）
		if (ImGui::MenuItem("Revert Prefab", nullptr, false, !PrefabManager::FindOwner(handle).IsValid())) {
			EditorHistory::RequestRevertPrefab(handle);
		}
		ImGui::SetItemTooltip("上書きを全部捨てて、Prefab のとおりに戻す（あとから足した子も消える。Ctrl+Z で戻せる）");
		if (ImGui::MenuItem("Unpack Prefab")) {
			PrefabManager::Unpack(handle);
		}
		ImGui::SetItemTooltip("Prefab とのつながりを切る（中身もシーンファイルに書かれるようになる。Prefab を直しても変わらなくなる）");
	}
```

### D-3 説明の文だけ

`Components/Core/PrefabInstanceComponent.h` の上のコメントの最後の2行。

```cpp
// シーンに書くのは、一番上の名前・有効・親・Transform と、置いた所ごとに変えた所（上書き）と、あとから足した子だけ
// （上書きは Inspector に出る。Apply で Prefab に書く・Revert で捨てる）
```

`Editor/Windows/ProjectWindowActions.cpp` の Place in Scene の Tooltip。

```cpp
		ImGui::SetItemTooltip("原点に置く。直した所は、置いた所ごとの上書きとしてシーンに残る（Inspector の Apply で Prefab に書ける）");
```

## 2章 確認（エディタで）

1. `PrefabNested.md` 2章の `Player`（中に `Sword`・`Blade`・`Watcher`）を置く。Inspector の Prefab の所が「上書き：なし」。
2. `Watcher` の Camera Look At の Smooth Time を 0.5 に → Inspector に「上書き：1 個の Entity」「この Entity：Camera Look At」。
3. `Blade` の名前を `Blade2` に、`Hand` を無効に、`Hand` に Camera Look At を足す、`Blade` の Camera Look At を外す。
4. シーンを保存して開く：`Player` の項目に `"overrides"`。`Watcher` の所は `"smoothTime": 0.5` だけ。開き直すと全部そのまま。
5. 別のシーンで `Sword` を置いて `Blade` を動かし、Apply → 元のシーンを開き直す：`Blade2` は動いた位置（上書きしていない位置は届く）、名前は `Blade2` のまま。
6. `Watcher` を消して保存 → `"removed"` に `Watcher` の番号。開き直すと無い。
7. `Blade2` を `Hand` の下へドラッグ → 動く（保存すると `"parent"` の上書き）。`Player` の下に新しい Entity `Extra` を作り、`Blade2` を `Extra` の下へドラッグ → 動かず、Console に「同じ置いた物の中身の間でだけ」。
8. `Player` を選んで Revert → 「上書き：なし」、`Watcher` が戻り、`Extra` が消える。Ctrl+Z → 上書きが戻る。Ctrl+Y → また戻る。
9. 入れ子の `Sword` を選ぶ → Revert が灰色。
10. `Player` を Ctrl+D → 複製の `Watcher` は複製の `Blade` を見る。複製は「上書き：なし」。

## 3章 写しで確かめた結果

本体と同じコード（`PrefabNested.md` の後から）を一時フォルダの写しに入れ、Sandbox で動かした（確かめるコードは写しの `main.cpp` にだけ置いた）。

- **ビルド**：Debug・Development・Release で警告 0（DirectXTex の前からある C4865 を除く）（エラー 0・DirectXTex の前からある C4865 のほかは警告 0）。パートA だけ・B までの途中は確かめていない。
- **動き**（`PrefabNested.md` の 51 項目と合わせて 94 項目 OK）：
  - 書く：版 4。変えた1項目だけ（Smooth Time）・名前・有効・足した Component（丸ごと）・外した Component・入れ子の一番上の Script の1項目（Power だけ。Target は書かない）・足した子はふつうに書く。
  - 読み直し：全部そのまま。上書きしていない項目（Watcher の Target・Sword の Target）は Prefab の値。
  - 消す・親を変える：`"removed"`・`"parent"` に書かれ、読み直しても同じ。足した子の下には動かせない。
  - Prefab を直す：上書きしていない位置は届き、上書きした名前は残る。回転のある Prefab を読み直しても Transform の上書きにならない（小数の丸め）。新しく置いた物は上書き無し。
  - Revert：上書き・足した子が消え、消した中身・名前・Script の値が Prefab のとおりに戻る。Undo で全部戻り、Redo でまた戻る。
  - 入れ子の上書きを Apply：外側の Prefab の入れ子の項目に上書きが書かれ、内側の Prefab は変わらない。新しく置いた物にも届く。
  - 貼り付け：中の参照は貼った物の中を指す・上書き無し。
  - 読めない Prefab：上書き（Component・overrides・removed）が、保存しても残る。
- **ログ**：エラー 0。警告はわざと起こした物（動かせない・読めない Prefab・自分の中に自分）だけ。前は出ていた「Script の相手がシーンに無いので外しました」（Undo・貼り付け）も出なくなった。
- **確かめていない**：本物のマウスでの操作（Inspector の表示・ボタン・ドラッグ）、UI Screen（`.uiscreen`）の編集、2人で実際にマージすること。

## 4章 気をつけること

- **アセットが手元に無い人が保存すると、上書きになる**：Prefab が使っている画像を、まだ pull していない人がシーンを保存すると、その Component の項目は「未選択」になっているので、`"texture": ""` の上書きが書かれる。前からある「アセットが見つからないと未選択として書く」動きと同じだが、上書きは Prefab の直しより強いので、気づきにくい。pull してからシーンを開く。
- **上書きは Prefab の直しより強い**：上書きした項目は、Prefab を直しても変わらない。思ったとおりに変わらないときは、Inspector の「この Entity：…」を見る（要らない上書きは Revert か、その項目を Prefab と同じ値に戻す）。
- **Revert は一番外側の置いた物ごと**：1つの Component・1つの項目だけ戻すことはできない（5章）。

## 5章 まだやっていないこと（必要になったら）

| 機能 | 今 | 作るなら |
|---|---|---|
| 1つの Component だけ Apply・Revert | 置いた物ごと | Inspector の Component の見出しの右クリックに。`ApplyOverride` を1つの型だけに当てる |
| 上書きした項目を太字に | 「この Entity：…」に型の名前だけ出す | Inspector の項目を描く所（ComponentUI の ImGui の係）に「上書きした項目の名前の一覧」を渡す |
| 入れ子の中の Revert | 押せない | 「外側の Prefab の値に戻す」か「内側の値に戻す」かを選ばせる |
