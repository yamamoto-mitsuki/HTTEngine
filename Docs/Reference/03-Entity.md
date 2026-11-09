> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 3. Entity

Entityは「場面に置いてある物1つ」。名前と有効フラグと親子関係だけを持ち、中身は全部Componentで足す。

```cpp
// 作る（親を省略すると一番上）
const Handle<Entity> root = EntityManager::Create("GameScene");
const Handle<Entity> ball = EntityManager::Create("MonsterBall", root);

// 消す（予約。子も一緒に消える）
EntityManager::Destroy(ball);

// 生きているか
if (EntityManager::IsAlive(ball)) { … }

// 名前・有効フラグを触る（Entity自体の情報）
if (Entity* entity = EntityManager::Get(ball)) {
	entity->name = "Boss";
	entity->isActive = false;
}

// 親を付け替える／子を集める
EntityManager::SetParent(ball, root);          // 値はそのまま（＝親から見た位置として読まれるので、見た目は動く）
EntityManager::SetParentKeepWorld(ball, root); // 見た目の位置・向き・大きさを変えない（掴む・乗る。7章）
std::vector<Handle<Entity>> children;
EntityManager::GetChildren(root, children);
```

| やりたいこと | 書き方 |
|---|---|
| 自分と親を全部たどって有効か | `EntityManager::IsActiveInHierarchy(handle)` |
| 消えても変わらない番号がほしい | `EntityManager::Get(h)->id`（`EntityId`）。`FindById(id)` で引き直す |
| Componentに別のEntityを覚えさせたい | `EntityRef`（`EntityManager::RefOf(h)` で作り、`EntityManager::Get<T>(ref)` / `Find(ref)` で使う） |
| 名前で探したい（シーンの `Initialize` で） | `EntityManager::FindByName("Player")`（同じ名前が複数なら最初の1つ） |
| 数を知る | `EntityManager::GetCount()` |

## 良い例：フレームをまたぐときは `Handle` で覚える

```cpp
class GameScene : public IScene {
private:
	Handle<Entity> player_; // Handleなら持っていてよい（ただの番号2つ）
};
```

## だめな例：ポインタを持ち続ける

```cpp
class GameScene : public IScene {
private:
	Entity* player_;              // ✗ 次のフレームには別の物を指しているかもしれない
	TransformComponent* transform_; // ✗ Componentの配列は詰め直されるので特に危険
};
```

**取ったポインタはその場で使い捨てる。** 配列は要素が消えると後ろから詰められるので、持ち続けたポインタは別のEntityの物になる。

## Prefab（同じ Entity を何個でも作る）

Entity（子・Component・Script も一緒に）をファイルに取っておき、ゲームから何個でも作れる（Unity の Prefab）。中身はシーンファイルと同じ形の `resources/prefabs/<名前>.prefab`。

1. **作る**：Hierarchy で Entity を組み立てる（名前は英数字と `_` `-` だけ）→ 右クリック → **Save As Prefab**。同じ名前の Prefab がもうあるときは、その Prefab から置いた物でなければ押せない（ほかの人の Prefab を上書きしないように。置き換えるなら古い方を消してから）。前の中身は `.bak` に残る。保存した物は、そのまま「Prefab から置いた物」になる（名前が青くなる）。
2. **置く**：Project ウィンドウで `.prefab` を右クリック → **Place in Scene**（原点に出る。Undo には入らない）。
3. **直す**：置いた物の中身をシーンで直すと、置いた所ごとの上書きとしてシーンに残る（Inspector に「上書き：N 個の Entity」と、選んだ Entity の上書きが出る）。Prefab に書くなら Inspector の **Apply**（か Hierarchy の右クリック → **Apply to Prefab**）、捨てるなら **Revert**（Ctrl+Z で戻せる）。
4. **使う**：Script に Prefab の欄を作り、Inspector で選ぶ。

```cpp
// Script の Fields で、Prefab を選ぶ欄（保存はパスで）
ui.AssetField("Prefab", prefab_, AssetType::Prefab);

// 作る（Unity の Instantiate）。一番上の Entity の位置が position になる（回転・大きさは Prefab のまま）
const GameObject shape = GameObject::Instantiate(prefab_, {x, 12.0f, 0.0f});
shape.transform.rotation = ...; // Transform はすぐ使える
// shape.GetComponent<RigidbodyComponent>() はまだ nullptr（ほかの Component・Script は次のフレームの頭に付く。AddComponent と同じ予約）
```

- 作るたびに新しい EntityId になり、Component・Script の中の「Prefab の中の Entity への参照」（`EntityRef`・`GameObject`）は、作った方を指すように付け替わる。Prefab の外の Entity を指していた項目は外れる（警告が出る）。
- 置いた物は Prefab とつながっている（一番上に `Prefab Instance` の印）。**シーンファイルには「どの Prefab を、どこに置いたか」だけを書く**（一番上の名前・有効・親・Transform と、あとから足した子）。中身は開くたびに Prefab から作るので、Prefab を直すと、次に開いたシーンから新しい中身になる（シーンファイルは変わらない。チームでぶつかりにくい）。
- 中の子の番号は「Prefab のファイルの番号 XOR ずれ」（ずれは置いた物ごとに1つ）なので、開き直しても同じ。シーンのほかの物から、置いた物の中の子を指してよい。
- シーンで中身（子も含む全部の Component・名前・有効・親）を直すと、シーンファイルには **Prefab と違う項目だけ** が書かれる（`"overrides"` など。体力だけ変えた敵なら `"Health": 200` だけ）。上書きしていない項目は、Prefab を直すと届く。Revert は一番外側の置いた物ごと（あとから足した子も消える）。
- 中身は、同じ置いた物の中身の間でだけ動かせる（外・あとから足した子の下には出せない）。つながりを切るなら **Unpack**。
- Prefab の中に、別の Prefab から置いた物（入れ子）を入れてよい。外側の Prefab のファイルにも参照（と、入れ子の上書き）だけが書かれ、内側の Prefab を直すと、外側から作った物も変わる。
- Play / Stop の退避は中身も全部写すので、Play 中・前に中身を試しに直しても Stop で戻る。
- 書き換わった Prefab（git pull など）は、シーンを開くときに読み直す。
- コードで読むなら `PrefabManager::Load(ToPath("resources/prefabs/Box.prefab"))`（同じファイルは1回だけ読む。`#include <HTTEngine/Scene/Prefab/PrefabManager.h>`）。
- 仕組みの解説は `../Tasks/PrefabInstance.md`（参照で置く・番号）・`../Tasks/PrefabNested.md`（入れ子）・`../Tasks/PrefabOverride.md`（上書き）。コードは `Scene/Prefab/` の4つ：`PrefabManager`（読み込み・保存・作る・持ち主）・`PrefabRecords`（中身を JSON で並べる・上書きを当てる）・`PrefabDiff`（JSON の違い）・`PrefabOverrides`（上書きを作る）。
