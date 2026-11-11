参考：
- Unity の Prefab の上書き（シーンの物への参照は、置いた所ごとの上書きとしてシーンに残る）：https://docs.unity3d.com/Manual/PrefabInstanceOverrides.html
- 前の段階：`Docs/Tasks/PrefabInstance.md`（参照で置く・番号の XOR）・`Docs/Tasks/PrefabNested.md`（入れ子）・`Docs/Tasks/PrefabOverride.md`（上書き）
- 使う側の決まり：`Docs/Reference/03-Entity.md` の「Prefab」
- 見つけた所：エンジン解説サイトのガイド「はじめてのゲーム作り」段階3（カメラ）。サイトの `docs/guide.md` の「段階3 で分かったこと」

# Prefab から置いた物が、シーンのほかの Entity を指せるようにする

> **2026-11-10：この md の中身（パートA・B・C）は、写経せずに本体へ直接入れた。** コードは本体を見る。入れるときに、古くなったコメントを4か所だけ一緒に直した（`ComponentSerializer.h` の `Save` の説明・`PrefabOverrides.h` の「InstantiateNow から」→「CreateInstance から」・`SceneSerializer.cpp` の `SaveEntity` の説明・`PrefabManager.cpp` の `PrefabState` のコメントの位置）。

Prefab から置いた物の中の「Entity への参照」（`EntityRef`・`GameObject`）が、**Prefab の外の Entity（同じシーンに置いたほかの物）** を指しているとき、今は続かない。これを直す。

例：`GameCamera.prefab`（Camera Follow の Target ＝ シーンの Player）。カメラ担当が Prefab にして渡し、まとめ役がみんなのシーンに置いて Target に Player を入れる、という形が今は作れない。UI World Follow の Target、敵が狙う相手なども同じ。

直した後：

- **Prefab のファイルには、外の相手を書かない**（0 で書く）。Prefab はどのシーンにも置く物で、相手はそのシーンにしか居ないから。
- **外の相手は、置いた所ごとの上書きとしてシーンに残る**（今ある上書きの仕組みのまま。`"Camera Follow": {"target": 番号}` だけがシーンに書かれる）。
- **シーンを開いたとき、ファイルの並び順に関係なく参照がつながる。**

直すファイルは 6 つ（足すファイルは無い）。パートは A → B の順。**このコードは、写しではなく本体に仮に入れて Debug でビルドし、Sandbox で 4章の項目を確かめてから、本体を元に戻した物**（2026-11-10。Development・Release のビルドはしていない）。

## 0. 今、何が起きるか

Sandbox の `Player.scene.json`（GameCamera が先頭、Player が最後。Player は `Player.prefab` から置いた物）で確かめた。

| やること | 起きること |
|---|---|
| GameCamera を右クリック → Save As Prefab → Ctrl+S → エディタを起動し直す | **Target が (none) になる。** Console に `Camera Follow: "target" の相手（EntityId …）がシーンに無いので外しました` |
| 外れた Target に、Player を運び直す → Ctrl+S | Inspector は「上書き：なし（Prefab と同じ）」のまま。**シーンに何も書かれず**、次に開くとまた外れる |
| Unpack してふつうの Entity に戻す | 残る（Prefab でなければ問題ない） |

## 1. 原因（2つ。別々の不具合）

### 1.1 読むとき：Prefab から置いた物だけ、Component を先に付けている

`SceneSerializer.cpp` の `LoadAll` は3周する。

| 周 | ふつうの Entity | Prefab から置いた物 |
|---|---|---|
| 1周目 | Entity を作る | `PrefabManager::InstantiateNow`：Entity を作り、**Component まで全部付ける** |
| 2周目 | 親子をつなぐ | 親子をつなぐ |
| 3周目 | Component を付ける | 何もしない（もう付いている） |

`EntityRef` を読むとき（`JsonReaderUI`）は「その番号の Entity が居るか」を見て、居なければ外す。3周目なら全員居るが、1周目の途中では **ファイルで自分より後ろに並ぶ Entity がまだ居ない**。
なので、Prefab から置いた物が後ろの Entity を指していると外れる。前に並ぶ相手なら残る（＝ Hierarchy の並び順で結果が変わる）。

3周目のコメントは「全Entityがそろった後なので、Componentの中のEntityへの参照も引ける」。Prefab から置いた物だけ、この決まりから外れていた。

### 1.2 書くとき：Prefab のファイルに、外の相手の番号がそのまま入る

`ComponentSerializer.cpp` の `JsonWriterUI`（Prefab に書くとき）：

```cpp
// Prefab に書くときは、表にある番号（Prefab の中の相手）を、ファイルの番号に直す。表に無い相手はそのまま（作るときに外れる）
```

「作るときに外れる」はずだったが、今のシーンの読み込み（`LoadComponents`）は番号の表を渡さずに読むので外れない。その結果：

1. `GameCamera.prefab` に `"target": 15981731572122588503`（作業シーンの Player の番号）が書かれる。
2. 上書きを作るときの基準（`PrefabRecords::Expand`）にも、その番号が入る。
3. 今の値も同じ番号なので、`PrefabOverrides::MakeInstanceEntry` の比べで **「違いなし」** になり、シーンに書かれない。
4. ほかのシーンに置くと、その番号の Entity は居ないので外れる。たまたま同じ番号の別の Entity が居れば、それを指してしまう。

## 2. 直し方

| 原因 | 直し | 責務の置き場所 |
|---|---|---|
| 1.1 | Prefab から置いた物も「1周目は Entity と Transform まで、3周目で Component」にする。`InstantiateNow` を `CreateInstance`（1段目）と `FinishInstance`（2段目）に分ける | シーンを読む順番は `SceneSerializer`、Prefab から作る中身は `PrefabManager`。作りかけの物（前の `PendingLoad`）を `PrefabManager.h` に出し、`SceneSerializer` が3周目まで持つ |
| 1.2 | Prefab に書くとき、表に無い相手（外の Entity）は 0 で書く。入れ子の上書きを作るときも、0 にしてから比べる | 「Prefab に何を書くか」は `ComponentSerializer`（書く係）。上書きの比べは `PrefabOverrides` |

- Transform だけは1周目で付けてよい（ほかの Entity を指さない。Entity を作ったときから持っているので配列も増えない。`Instantiate` も同じ分け方をしている）。
- `InstantiateNow` は残す（`CreateInstance` → `FinishInstance` を続けて呼ぶだけ）。使うのは `EditorHistory::RequestRevertPrefab`（シーンがもうできているので、順番の問題が無い）。
- `Instantiate`（ゲーム中に作る）は変えない。Prefab のファイルに外の相手が無くなるので、作った物の参照は空で始まる（Script の `Start` などで入れる。Unity と同じ）。

### やらなかった案

| 案 | やらない理由 |
|---|---|
| Hierarchy の並びを変える（相手を前に置く） | 並び順で結果が変わるのが不具合そのもの。チームでは守れない |
| `EntityRef` を読むときに「居るか」を見ない（後で引く） | 居ない番号を持ったままになり、あとで同じ番号が別の Entity に配られると、別の物を指す（`JsonWriterUI` のコメントにある心配と同じ） |
| 付け替えの関数（`RemapRefs`・`RefRemapUI`・Script の remap）に「外の相手を 0 にする」の切り替えを足す | 最初に試した形。動くが、型ごとの remap の関数の引数まで変わる（8 ファイル）。しかも入れ子の項目に `"target": 0` という要らない上書きが残り、あとで内側の Prefab がその項目を変えても届かなくなる。**比べる前に 0 にする**（下のパートB）方が、直す所が少なく、要らない上書きも出ない |
| Prefab のファイルに外の相手の番号を残し、置く側で外す | Prefab を作ったシーンの番号が、ほかの人のシーンに持ち込まれる。意味のない番号が git の差分に出る |

## パートA：読むとき（シーンの全部の Entity を作ってから、Component を付ける）

### A-1. `Scene/Prefab/PrefabManager.h`

include に2行足す（`<vector>` と `PrefabRecords.h`。`EntityRecords` を使うため）。

```cpp
#include <string>
#include <vector>
#include <filesystem>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Scene/Prefab/PrefabRecords.h"
```

`MakePrefabFilePath` のすぐ下に、作りかけの物の型を足す（`PrefabManager.cpp` の `PendingLoad` を、名前を変えてここへ移す）。

```cpp
/// <summary>
/// Prefab から作りかけの物（Entity と Transform まではできていて、ほかの Component と「置いた物の印」がまだ付いていない）
/// <para>Instantiate は、これを次のフレームの頭（Flush）まで PrefabManager の中に取っておく</para>
/// <para>シーンを読むときは、CreateInstance が返した物を呼ぶ側が持ち、全部の Entity がそろってから FinishInstance に渡す</para>
/// </summary>
struct PrefabPendingLoad {
	uint32_t prefab = 0;                          // どの Prefab から作ったか（印に書く）
	std::shared_ptr<const EntityRecords> records; // 作ったときの中身（置いた番号。途中で Prefab を保存し直しても、作ったときの中身で付ける）
	std::vector<Handle<Entity>> created;          // records の i 番目から作った Entity（[0] が一番上）

	// 作れたか（読めない Prefab から作ろうとした物は false）
	bool IsValid() const { return !created.empty(); }
	// 一番上の Entity（作れていなければ無効な Handle）
	Handle<Entity> GetRoot() const { return created.empty() ? Handle<Entity>{} : created.front(); }
};
```

`InstantiateNow` の宣言（`/// <summary>` から）を、次の3つに置き換える。

```cpp
	/// <summary>
	/// シーンファイルを読むとき用の1段目。entry（シーンファイルの "prefab" の付いた1項目）のとおりに Entity を作り、Transform だけ付ける（フレームの境目でだけ呼ぶ）
	/// <para>一番上の番号・名前・有効・Transform は entry のとおり。親は付けない（呼ぶ側が付ける）</para>
	/// <para>ほかの Component はまだ付けない。シーンの全部の Entity を作り終えてから FinishInstance に渡す
	/// （Component の中の、シーンのほかの Entity を指す項目が、相手がまだ居なくて外れないように）</para>
	/// <para>中の番号がほかの Entity とぶつかったら、新しい番号で作って warnings に書く。読めていない番号なら IsValid が false の物を返す</para>
	/// </summary>
	static PrefabPendingLoad CreateInstance(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings);
	// シーンファイルを読むとき用の2段目。CreateInstance で作った物に、Transform 以外の Component・Script・置いた物の印を付ける（フレームの境目でだけ呼ぶ）
	static void FinishInstance(const PrefabPendingLoad& pending, std::set<std::string>& warnings);
	// CreateInstance と FinishInstance を続けて呼ぶ（シーンがもうできているとき。Inspector の Revert）。一番上の Entity を返す（作れなければ無効な Handle）
	static Handle<Entity> InstantiateNow(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings);
```

### A-2. `Scene/Prefab/PrefabManager.cpp`

1. 無名 namespace の `struct PendingLoad { … };`（上の `/// <summary>` 3行も）を消す。
2. ファイルの中の `PendingLoad` を全部 `PrefabPendingLoad` に置き換える（型の名前 8 か所と、`Save` の中のコメント1か所）。
3. `InstantiateNow` を、次の3つに置き換える。**中身の前半（`Expand` から `Relabel` の do-while まで）は今のまま**で、変わるのは関数の名前・戻り値と、最後の4行。

```cpp
PrefabPendingLoad PrefabManager::CreateInstance(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	// --- 中身を並べ、ファイルの項目（一番上の名前・有効・Transform・上書きなど）を当てる ---
	EntityRecords records = PrefabRecords::Expand(prefab, entry.value("id", EntityId{0}), warnings);
	if (records.empty()) {
		return {};
	}
	PrefabRecords::ApplyEntry(records, 0, records.size(), entry, warnings);
	// --- 中の誰かがほかの Entity とぶつかるなら、全員を新しい番号にずらす（シーンのほかの物からの参照は外れる）---
	if (!PrefabRecords::AreIdsFree(records)) {
		warnings.insert(std::format("Prefab の中の番号がほかの Entity とぶつかったので、新しい番号で作りました（この物への参照は外れます）: {}", GetPath(prefab)));
		const EntityId rootId = records.front().at("id").get<EntityId>();
		EntityRecords moved;
		do {
			moved = records;
			PrefabRecords::Relabel(moved, EntityManager::NewId() ^ rootId);
		} while (!PrefabRecords::AreIdsFree(moved));
		records = std::move(moved);
	}
	// --- Entity を作り、Transform だけ付ける（Transform はほかの Entity を指さないので、今付けてよい）---
	// ほかの Component は FinishInstance で付ける（シーンのほかの Entity を指す項目が、相手がまだ居なくて外れないように）
	PrefabPendingLoad pending = CreateEntities(prefab, std::make_shared<const EntityRecords>(std::move(records)), {});
	LoadComponents(pending, true, warnings);
	return pending;
}

void PrefabManager::FinishInstance(const PrefabPendingLoad& pending, std::set<std::string>& warnings) {
	if (!pending.IsValid()) {
		return; // 作れなかった物（読めない Prefab）
	}
	LoadComponents(pending, false, warnings);
	Link(pending);
}

Handle<Entity> PrefabManager::InstantiateNow(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings) {
	const PrefabPendingLoad pending = CreateInstance(prefab, entry, warnings);
	FinishInstance(pending, warnings);
	return pending.GetRoot();
}
```

### A-3. `Scene/SceneSerializer.cpp`（読む方）

`LoadInstance`：引数に `pending` を足し、頭の4行を変える（「ファイルが無い」から下は今のまま）。

```cpp
// ===== Prefab から置いた物を作る（中身は Prefab のファイルから。Prefab が読めなければ、印だけ付けた空の Entity）=====
// 一番上の名前・有効・Transform は、item（シーンファイルの1項目）のとおりになる
// ここで作るのは Entity と Transform まで。ほかの Component は、作りかけの物を pending に入れて返し、LoadAll の3周目で付ける
Handle<Entity> LoadInstance(const SceneJson& item, const std::string& path, PrefabPendingLoad& pending, std::set<std::string>& warnings) {
	const uint32_t prefab = PrefabManager::Load(ToPath(path));
	pending = PrefabManager::CreateInstance(prefab, item, warnings);
	if (pending.IsValid()) {
		return pending.GetRoot();
	}
	// --- ファイルが無い（まだ pull していない など）。…（ここから下は今のまま）
```

`LoadAll` の1周目：`isInstance` のコメントを直し、作りかけの物を持つ配列を足して、`LoadInstance` に渡す。

```cpp
	std::vector<Handle<Entity>> created(entities.size()); // 作れなかった所は無効なHandleのまま
	std::vector<bool> isInstance(entities.size());        // Prefab から置いた物か（3周目で、ファイルの "components" ではなく作りかけの物から付ける）
	// Prefab から置いた物の作りかけ（Entity と Transform まで）。ふつうの Entity・読めなかった Prefab の所は空のまま
	std::vector<PrefabPendingLoad> pendingInstances(entities.size());
```

```cpp
		created[i] = isInstance[i] ? LoadInstance(item, prefab->get<std::string>(), pendingInstances[i], warnings) : EntityManager::CreateWithId(id, name, {});
```

`LoadAll` の3周目を、次に置き換える。

```cpp
	// ===== 3周目：Componentを付ける（全Entityがそろった後なので、Componentの中のEntityへの参照も引ける）=====
	// ファイルの並びの順に付ける（Prefab から置いた物も、ふつうの Entity も同じ扱い）
	for (size_t i = 0; i < entities.size(); ++i) {
		if (!created[i].IsValid()) {
			continue;
		}
		// --- Prefab から置いた物：中身（Prefab のファイル＋置いた所ごとの上書き）の Component と、置いた物の印 ---
		// 読めなかった Prefab の所は pendingInstances[i] が空で、何もしない（LoadInstance が Transform まで付けてある）
		if (isInstance[i]) {
			PrefabManager::FinishInstance(pendingInstances[i], warnings);
			continue;
		}
		// --- ふつうの Entity：ファイルの "components" ---
		const auto componentsIt = entities[i].find("components");
		if (componentsIt == entities[i].end() || !componentsIt->is_object()) {
			continue;
		}
		ComponentSerializer::Load(created[i], *componentsIt, warnings);
	}
```

- 親子（2周目）は、Prefab から置いた物の中身どうしは `CreateEntities` がもうつないでいる。一番上の親だけが2周目で付く（今と同じ）。
- 「Prefab から置いた物の中身」を、シーンのほかの Entity の子に付け替えた上書き（`"overrides"` の `"parent"`）は、相手がまだ居ないと一番上の子になる（`CreateEntities` の「見つからなければ一番上」）。これは今も同じで、この md では直していない（5章）。

## パートB：書くとき（Prefab のファイルに、外の相手を書かない）

### B-1. `ECS/ComponentSerializer.cpp`

`JsonWriterUI` の `Field(const char* label, EntityRef& value, FieldStyle)` の中。

```cpp
		// Prefab に書くときは、表にある番号（Prefab の中の相手）を、ファイルの番号に直す
		// 表に無い相手は Prefab の外の Entity（そのシーンにしか居ない）なので、0 で書く。番号をそのまま書くと、
		// 置いた所ごとの上書きを作るときの基準（Prefab の中身）にその番号が入り、同じ相手を指しても「違い無し」になってシーンに残らない
		if (idMap_ != nullptr && id != 0) {
			if (const auto found = idMap_->find(id); found != idMap_->end()) {
				id = found->second;
			} else {
				warnings_.insert(std::format("{}: \"{}\" の相手は Prefab の外の Entity なので、Prefab のファイルには書きません（シーンに置いた物なら、置いた所ごとの上書きとしてシーンに残ります）", typeName_, KeyOf(label)));
				id = 0;
			}
		}
		Put(label, id);
```

これだけで、Save As Prefab・Apply のたびに次のようになる。

1. Prefab のファイル：`"target": 0`。
2. 基準（Expand）：target は 0。今の値は Player の番号 → **違う** → シーンの項目に `"Camera Follow": {"target": 番号}` が書かれる。
3. Inspector に「上書き：1 個の Entity…／この Entity：Camera Follow」と出る（外の相手を指している、という印になる）。

### B-2. `Scene/Prefab/PrefabOverrides.h`

`MakeInstanceEntry` に引数を1つ足す（`/// </summary>` の下に param を足す）。

```cpp
	/// <param name="prefabIds">Prefab のファイルに書くとき（入れ子）だけ渡す。その Prefab に入る Entity の番号の表（SceneSerializer::SaveTree の idMap）。
	/// 渡すと、表に無い相手（Prefab の外の Entity）への参照を 0 として比べる（Prefab には、外の相手を書かない）</param>
	static SceneJson MakeInstanceEntry(Handle<Entity> root, std::set<std::string>& warnings, const EntityIdMap* prefabIds = nullptr);
```

### B-3. `Scene/Prefab/PrefabOverrides.cpp`

無名 namespace の `SaveComponents`：

```cpp
// ===== Entity 1つ分の今の Component（Save と同じ形）=====
// insideIds を渡すと、表に無い相手（Prefab の外の Entity）への参照を 0 で書く（表にある相手は、同じ番号のまま）
SceneJson SaveComponents(Handle<Entity> handle, std::set<std::string>& warnings, const EntityIdMap* insideIds) {
	SceneJson components = SceneJson::object();
	ComponentSerializer::Save(handle, components, warnings, insideIds);
	return components;
}
```

`MakeInstanceEntry` の頭（早期リターンのすぐ下に足す）：

```cpp
SceneJson PrefabOverrides::MakeInstanceEntry(Handle<Entity> root, std::set<std::string>& warnings, const EntityIdMap* prefabIds) {
	const Entity* entity = EntityManager::Get(root);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	if (entity == nullptr || link == nullptr) {
		return {};
	}
	// ===== Prefab に書くとき：その Prefab に入る Entity の表（番号 → 同じ番号）=====
	// 比べる基準（Expand）は今の番号で並ぶので、今の Component も今の番号のまま書く。表に無い相手（Prefab の外）だけが 0 になる
	// 0 にしてから比べるので、外の相手を指しているだけの項目は「Prefab と同じ」になり、入れ子の項目に要らない上書きが残らない
	EntityIdMap insideMap;
	if (prefabIds != nullptr) {
		for (const auto& pair : *prefabIds) {
			insideMap[pair.first] = pair.first; // first が今の番号（second はファイルに書く番号。ここでは使わない）
		}
	}
	const EntityIdMap* insideIds = (prefabIds != nullptr) ? &insideMap : nullptr;
```

同じ関数の中の `SaveComponents` の2か所に `insideIds` を渡す。

```cpp
	SceneJson nowRoot = SaveComponents(root, warnings, insideIds);
```

```cpp
		PrefabDiff::DiffComponents((base != nullptr) ? *base : SceneJson::object(), SaveComponents(handle, warnings, insideIds), patches, removedOne);
```

なぜ「番号 → 同じ番号」の表か：`JsonWriterUI` は「表にあれば表の番号、無ければ 0」で書く。基準（Expand）は今の番号で並んでいるので、比べる相手も今の番号のままでなければならない。ファイルの番号へずらすのは、比べた後の `ShiftEntry`（今のまま）。

### B-4. `Scene/SceneSerializer.cpp`（書く方）

`SaveInstance`：`idMap` を `MakeInstanceEntry` にも渡す。

```cpp
// shift・idMap を渡すと、番号を Prefab のファイルの番号にして書く（Prefab の中の入れ子。シーンなら 0 と nullptr）
// idMap は MakeInstanceEntry にも渡す（Prefab の外の Entity への参照は、Prefab には書かない。ふつうの Entity の Component と同じ決まり）
SceneJson SaveInstance(const Entity& entity, EntityId shift, const EntityIdMap* idMap, std::set<std::string>& warnings) {
	SceneJson item = PrefabOverrides::MakeInstanceEntry(entity.self, warnings, idMap);
	return (idMap != nullptr) ? PrefabRecords::ShiftEntry(std::move(item), shift, *idMap) : item;
}
```

入れ子のとき（`CameraRig.prefab` の中に `GameCamera.prefab` から置いた物。Target は外の Player）に書かれる物：

| ファイル | Target について書かれる物 |
|---|---|
| `GameCamera.prefab` | `"target": 0` |
| `CameraRig.prefab` の入れ子の項目 | **何も書かれない**（外の相手を 0 にしてから比べるので、内側の Prefab と同じ） |
| シーン | CameraRig の項目の `"overrides": [{"id": 1, "components": {"Camera Follow": {"target": 番号}}}]` |

## パートC：ドキュメント

`Docs/Reference/03-Entity.md` の「Prefab」の箇条書きの1行を直す。

```md
- 作るたびに新しい EntityId になり、Component・Script の中の「Prefab の中の Entity への参照」（`EntityRef`・`GameObject`）は、作った方を指すように付け替わる。**Prefab の外の Entity（シーンに置いたほかの物）への参照は、Prefab のファイルには入らない**（書くときに警告が出る）。シーンに置いた物なら、その参照は置いた所ごとの上書きとしてシーンに残る（Inspector の Target などへ、シーンごとに入れる）。`Instantiate` で作った物は空で始まるので、Script で入れる。
```

## 3. 前に保存したファイル

版（`"version"`）は上げない。ファイルの形は変わらず、Prefab に外の番号が残っている古いファイルもそのまま読める。

| 古いファイルの状態 | 直した後の動き |
|---|---|
| Prefab に外の番号・シーンに上書き無し・相手が同じシーンに居る（Prefab を作った作業シーン） | 並び順に関係なくつながる（パートA）。Apply するか、Prefab を保存し直すと、Prefab は 0・シーンに上書き、の新しい形になる（確かめた） |
| 同じ Prefab を、相手の居ないシーンに置いてある | 今と同じく「シーンに無いので外しました」。Target を入れると、基準（古い番号）と違うので上書きとして残る |

今のリポジトリで、Prefab から置いた物が外の Entity を指しているファイルは無い（Sandbox の `Player.prefab`・`BurstEffect.prefab` は外を指さない）。ゲームのリポジトリは、写した後に Console の警告で分かる。

## 4. 確かめる項目

Sandbox の `Player.scene.json`（GameCamera はふつうの Entity。Target ＝ Player。GameCamera が先頭）から。**2026-11-10 に Debug で全部確かめた**（1 の Console と Inspector の表示・3・6 の Ctrl+Z の後の Target は、パートB を今の形に作り直す前のビルドで見た。その道のコードは変わっていない）。

| # | やること | 合っているとき | 結果 |
|---|---|---|---|
| 1 | GameCamera を右クリック → Save As Prefab → Ctrl+S | `GameCamera.prefab` は `"target": 0`。シーンの項目は `"prefab"` と Transform と `"Camera Follow": {"target": 番号}` だけ。Console に「Prefab の外の Entity なので、Prefab のファイルには書きません」。Inspector に「上書き：1 個の Entity…／この Entity：Camera Follow」 | OK |
| 2 | エディタを起動し直す | Target が Player のまま。Console に「外しました」が出ない | OK |
| 3 | Play → 歩く → Stop | カメラが追う。Stop の後も Target が残る（再生の退避から戻す道） | OK |
| 4 | 空の Entity `CameraRig` を作り、GameCamera をその子にする → CameraRig を Save As Prefab → Ctrl+S | `CameraRig.prefab` の入れ子の項目に Camera Follow が**書かれない**。シーンの CameraRig の `"overrides"` に target | OK |
| 5 | 起動し直す → Play | 入れ子の GameCamera の Target が Player。カメラが追う | OK |
| 6 | （1 の状態で）Inspector の Revert → Ctrl+Z | Revert で Target が (none)（Prefab のとおり）。Ctrl+Z で Player に戻る | OK |
| 7 | 古い形を手で作る（`GameCamera.prefab` の target に Player の番号・シーンの項目は Transform だけ）→ 起動 | Target が Player（並びは GameCamera が先） | OK |
| 8 | 7 の続きで Apply → Ctrl+S | `GameCamera.prefab` が `"target": 0` になり、シーンに上書きが書かれる | OK |

確かめていないこと：

- Development・Release のビルド（直した所に `USE_IMGUI` の分かれ目は無い）。
- Project ウィンドウの Place in Scene で別のシーンに置き、Target を入れて保存する流れ（`Instantiate` → `Flush` の道は変えていない。上書きの保存は 1 と同じ道）。
- Script の `GameObject` の欄（`EntityRef` と同じ書く係を通る）・一覧（`ui.ListField`）の中の参照。
- UI Screen（`.uiscreen`。`PrefabManager::Save` を使う）の中から、画面の外の Entity を指している物。あれば、保存したときに同じ警告が出て 0 になる（画面は `Instantiate` で作るので、今も作るときに相手は居ない）。

## 5. 見つけたこと（この md では直さない）

- **Revert を Ctrl+Z で戻した直後、Inspector の「上書き：」が「なし」のまま**（値は戻っている。ほかの編集をすると正しくなる）。Inspector の上書きの覚え（`InspectorWindow.cpp` の cache。`EditorHistory::GetChangeCount` で作り直す）が、戻した物を見る前に作られている。前からの動き。
- **中身を、シーンのほかの Entity の子に付け替えた上書き**は、開き直すと一番上の子になることがある（`CreateEntities` が親を探すとき、シーンの相手がまだ居ない）。Reference には「中身は、同じ置いた物の中身の間でだけ動かせる」とあるので、エディタが断っていれば起きない（確かめていない）。
- **Save As Prefab・Unpack の後、シーンの名前に `*` が付かない**（ガイド段階2 で見つけた物。未修正）。Save As Prefab の直後は Ctrl+S が要る。

## 終わったら

- この md を写して本体に入れたら、エンジン解説サイトのガイドを直す（サイトの `docs/guide.md` の「段階3 で分かったこと」に、直す所が書いてある）：`gd-camtune` の ⑨ を「Prefab にして渡す」の手順に・`gd-overview` の表の2か所・Sandbox のお手本の GameCamera を `GameCamera.prefab` に。
- ガイドに書くこと：Prefab にしたカメラをほかのシーンに置いたら、そのシーンの Player を Target へ運ぶ（上書きとしてそのシーンに残る）。
