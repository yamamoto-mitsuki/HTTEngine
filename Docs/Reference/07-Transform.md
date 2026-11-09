> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 7. Transform

全Entityが必ず1つ持つ。**作った直後から `Get` できる**（予約ではない）。

```cpp
TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
transform->translation = {0.0f, 3.0f, 0.0f};
SetEulerAngles(*transform, {50.0f, -30.0f, 0.0f}); // 角度（度）で回す（rotation はクォータニオン）
transform->scale = {2.0f, 2.0f, 2.0f};
```

| 項目 | 中身 |
|---|---|
| `translation` / `rotation` / `scale` | **親から見た**位置・回転・大きさ。`rotation` は**クォータニオン**。角度（度。Z → X → Y の順）で読み書きするなら `SetEulerAngles`・`GetEulerAngles`・`Rotate` |
| `worldMatrix` | 親の行列まで掛けた結果。**エンジンが毎フレーム計算する。自分で書き込まない** |
| `GetWorldPosition(transform)` | ワールド座標（行列の4行目） |
| `GetWorldRight(transform)` | 右（ローカル +X）がワールドでどちらを向いているか（Unity の `transform.right`） |
| `GetWorldUp(transform)` | 上（ローカル +Y）がワールドでどちらを向いているか（Unity の `transform.up`） |
| `GetWorldForward(transform)` | 前（ローカル +Z）がワールドでどちらを向いているか |

```cpp
// ✗ だめな例
transform->rotation.y += 90.0f;            // ✗ クォータニオンの数字を直接足している（角度ではない）→ Rotate(*transform, {0.0f, 90.0f, 0.0f})
transform->worldMatrix = MakeIdentity4x4(); // ✗ 自分で書いても次のフレームに上書きされる
```

> **`worldMatrix` はフレームの頭の最後（Script・SYSTEM の前）と、フレームの中盤（`UpdateTransforms`）で計算される。** シーンの `Update`・Script・SYSTEM で読むと、フレームの頭の時点の値（そのフレームの中で動かした分は入っていない）。位置を正確に使いたいときは `translation` から計算するか、`Draw` で読む。今すぐ正しい値が欲しいときは `EntityManager::CalcWorldMatrix(entity)`（親をたどって計算し直す）。

- **ワールド座標で動かす**なら `EntityManager::TranslateWorld(entity, move)`（親の下なら、親から見た量に直して `translation` に足す）。物理の押し戻し・カメラの追いかけもこれを使う。
- **子を名前で探す**なら `gameObject.FindChild("Body")`（自分の子だけを見る。孫は見ない。Unity の `transform.Find`）。シーン全体から探す `GameObject::Find` と違い、Prefab を何個置いても、それぞれの子が見つかる。

## 親を付け替える（見た目を変えずに）

Hierarchy のドラッグ＆ドロップは、**見えている位置・向き・大きさを変えずに**親を付け替える（値を新しい親から見た値に直す。Ctrl+Z で戻る）。コードで同じことをするなら `SetParentKeepWorld`。

```cpp
void Grab(Handle<Entity> hand, Handle<Entity> item) {
	EntityManager::SetParentKeepWorld(item, hand); // 手の子にする（今の場所のまま、手と一緒に動くようになる）
}
void Release(Handle<Entity> item) {
	EntityManager::SetParentKeepWorld(item, {}); // 一番上へ戻す（今の場所に置いていく）
}
```

```cpp
	EntityManager::SetParent(item, hand); // ✗ 値がそのまま「手から見た位置」になり、手の分だけ飛ぶ → SetParentKeepWorld
```

- 親の大きさが軸ごとに違い（Y だけ3倍など）、しかも子が回っていると、位置は合うが向き・大きさは近い値になる（ゆがみは Transform で表せない。Unity も同じ）。
- 行列を大きさ・回転・位置に戻す関数は `DecomposeAffineMatrix`（`MakeAffineMatrix` の逆）。
