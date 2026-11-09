> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 5. ゲーム固有Component（COMPONENT / SYSTEM）

**1ファイル＝1Component。** ゲームの `project/Source/Components/` に `.cpp` を置いて、VSのプロジェクトに追加する。書くのは「①データ ②見せ方 ③処理」だけ。

```cpp
#include <HTTEngine/ECS/GameComponent.h>

// ① データ
struct Spin {
	Vector3 speed = {0.0f, 90.0f, 0.0f}; // 1秒に回る角度（度）
};

// ② Inspectorでの見せ方（カテゴリは Gameplay 固定。"Movement" はその下）
COMPONENT(Spin, "Movement") {
	ui.Field("Speed (deg/s)", value.speed, Tip("1秒に回る角度"));
}

// ③ 毎フレームの処理（両方持っているEntityだけ呼ばれる）
void SpinUpdate(Spin& spin, TransformComponent& transform, float deltaTime) {
	Rotate(transform, spin.speed * deltaTime); // rotation はクォータニオンなので、角度（度）で回すときは Rotate（7章）
}
SYSTEM(SpinUpdate);
```

これだけで `EntityManager::RegisterComponent` も Inspector への登録も毎フレームの呼び出しも済む。

## `ui.Field` で書けるもの（抜粋）

| 書き方 | 見た目 |
|---|---|
| `ui.Field("HP", value.hp)` | 数字（`float` / `int` / `bool` / `Vector2` / `Vector3` / `Vector4`） |
| `ui.Field("HP", value.hp, Range(0.0f, 100.0f))` | 範囲の中でつまんで動かす（200 ピクセルで端から端まで。数字を打っても範囲に収まる。スライダーは使わない） |
| `ui.Field("Radius", value.r, AtLeast(0.0f))` | 0より小さくできない |
| `ui.Field("HP", value.hp, Tip("体力"))` | マウスを乗せると説明 |
| `ui.Field("Name", value.name)` | 文字の入力欄（Component は `FixedString<N>`、Script は `std::string` も書ける） |
| `ui.ColorField("Color", value.color)` | 色見本（`Vector3` か `Vector4`） |
| `ui.ListField("Stages", value.stages, [](ComponentUI& ui, Stage& s) { ui.Field("Time", s.time); })` | **一覧**（Component は `FixedList<T, N>`、Script は `std::vector`）。足す・消す・並べ替える。中の書き方は COMPONENT の中と同じ |
| `ui.Field("Mode", value.mode)` | **enum** はコンボ（名前は magic_enum が作る） |
| `ui.Field("Target", value.target)` | **`EntityRef`**（別の Entity への参照）は相手の名前の台。Hierarchy から Entity をドラッグして入れる。右クリックで外す。相手が消えていると `(missing)` |
| `ui.AssetField("Model", value.model, AssetType::Model)` | **アセットの番号**（`uint32_t`。`AssetType` はモデル・テクスチャ・音・アニメ・Prefab・フォント・マテリアル・カーブ・Timeline など）。resources 以下のファイルから選ぶ |
| `ui.Separator("見出し")` / `ui.Label("説明")` / `ui.Space()` | 飾り（保存はされない） |

## `ui.Field` に書いた物は、そのまま保存される

- **保存の名前はラベル**（`"HP"`）。enum は名前、`EntityRef` は EntityId（相手が居なければ 0）、アセットはパスで書かれる。
- **ラベルを変えると、前に保存した値が読めなくなる**（Log に「ファイルの項目 "HP" を読む所がありません」と出る）。表示だけ変えたいときは `"体力###HP"` と書く（`###` の後ろが保存の名前。ImGui の決まりと同じ）。
- `ui.Field` に書かなかった項目は保存されない（読み込むと初期値）。実行中だけの値はわざと書かなければよい。
- Component に項目を足しても、前のファイルはそのまま読める（足した項目は初期値になる）。
- 見せ方の関数の中に処理を書かない。Inspector に描くときだけでなく、保存・読み込みでも呼ばれる。

## `SYSTEM` の引数（＝「この型を全部持つEntityだけ呼ぶ」という注文書）

| 書き方 | 意味 |
|---|---|
| `void F(Spin& spin)` | `Spin` を持つ全部 |
| `void F(Spin& spin, TransformComponent& transform)` | **両方**持つEntityだけ |
| `void F(Spin& spin, float deltaTime)` | 最後の `float` は経過秒 |
| `void F(Handle<Entity> entity, Enemy& enemy)` | 先頭の `Handle<Entity>` は**そのEntity自身**（消したいとき） |
| `void F(const Spin& spin, Health& health)` | 読むだけなら `const&` |

## だめな例

```cpp
// ✗ 1ファイルに2つのComponentを詰める（どこに何があるか分からなくなる）
// ✗ 引数を値渡しにする（コンパイルエラー）
void SpinUpdate(Spin spin, TransformComponent transform) { … }

// ✗ Componentのファイルを静的ライブラリ（エンジン側）に入れる
//    → 誰も参照していないのでリンカに捨てられ、Add Componentに出てこない

// ✗ System同士を直接呼ぶ
void PlayerMove(...) { EnemyUpdate(...); } // 呼ぶ順番に縛られて壊れやすくなる
```
