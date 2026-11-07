参考：
- Unity の Transform（`transform.position` は読むたびに親をたどった正しい値。このエンジンは「決まった所でまとめて作る写し」なので、作る所の決まりが要る）：https://docs.unity3d.com/ScriptReference/Transform.html
- Unity の 1フレームの順番（Awake・Start・Update の前に、読み込んだ物の Transform はそろっている）：https://docs.unity3d.com/Manual/ExecutionOrder.html
- 前の md：Docs/Tasks/FrameLoop.md（1フレームの順番）・Docs/Tasks/PrefabOverride.md（Prefab をフレームの頭で作る）
- 見つけたきっかけ：Docs/Tasks/ScreenTrailCursor.md の Sandbox の弾（シーンを読んだ最初のフレームに撃った弾が、箱ではなく原点から出た）

# フレームの頭でワールド行列を作り直す

**シーンを読んだ最初のフレームに、Script・SYSTEM が読むワールドの位置が原点になる**不具合を直す。

- **パートA** `Scene/SceneManager.cpp`：フレームの頭の処理の最後で `EntityManager::UpdateTransforms` を呼ぶ
- **パートB** `ECS/EntityManager.h`：`UpdateTransforms` のコメント（呼ぶ所と、読める値の決まり）
- **パートC** `Docs/Reference.md`：1フレームの順番の表と「覚えておくこと」

ブランチは `feature/FrameHeadTransforms`（develop f6038eb から）。Docs/Tasks/ScreenTrailCursor.md より**先に**写す（ScreenTrailCursor の Sandbox の弾は、最初のフレームに撃つので、この直しが無いと最初の3発が原点から出る）。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドでエラー・警告 0。この直しが**無い**と「シーンを読んだ最初のフレームの SYSTEM が撃った弾」が3発とも原点（x=0）から出て、テストが落ちる。直しを入れると3発とも箱（x=-4・0・4）から出て、ScreenTrailCursor のテストと合わせて Debug・Development 33 項目、Release 30 項目が全部合格。重さは下の 0.4。

## 0章 全体の形

### 0.1 何が困っていたか

- ワールド行列（`TransformComponent::worldMatrix`）は、`EntityManager::UpdateTransforms` が決まった所でまとめて作る**写し**。1フレームの中では `EngineSystems::Update`（Script・SYSTEM の**後**）でしか作っていなかった
- ところがフレームの頭（`EngineSystems::FlushPending` と `SceneManager::Update` の頭）では、Entity を**作る・戻す**：シーンを読む・作り直す（Play・Stop・Restart）・開く・Undo（`EditorHistory::Flush`）・エディタから Prefab を置く（`PrefabManager::Flush`）
- そのすぐ後に Script（Awake・Start・Update）・SYSTEM が回るので、その Entity の `worldMatrix` は**まだ単位行列（原点）か、古いまま**。`GetWorldPosition(transform)` が原点を返す
- 例：Sandbox の `TestBulletSpawner`（HitDamageTest の Turret）も、タイマー 0 で最初のフレームに撃つので、最初の1発が原点から出ていた

### 0.2 決めたこと（2026-11-07 に相談して決めた）

| 項目 | 決まり | 理由 |
|---|---|---|
| 保証すること | **Script・SYSTEM が回る前には、全部の Entity のワールド行列が、そのときの Transform から作られている** | シーンを読んだときだけ直すと、Undo・エディタで置いた Prefab・この先フレームの頭で作る物が、また同じ穴に落ちる。「いつ読んでも、フレームの頭の時点では正しい」を決まりにする |
| 置き場所 | `SceneManager::Update` のフレームの頭の処理（作り直し・切り替え・保存・開く）の**最後** | フレームの頭で Entity を作る・戻すのは、ここと、その前の `EngineSystems::FlushPending` だけ。その全部が済んだ後で、Script・SYSTEM の前 |
| 読むたびに正しい形（Unity 形） | やらない | Transform は値を直接書く形で「変わった」が分からない。`GetWorldPosition(transform)` は Entity を知らない。UI World Follow などの行列の手直し（`AddTransformAdjuster`）とも合わなくなる。読む所を全部 Handle で読む形に変える大きな作業になる |

### 0.3 変わること・変わらないこと

- **変わる**：シーンを読んだ・作り直した・開いた・Undo した・エディタで Prefab を置いたフレームの Script（Awake・Start・Update）・SYSTEM・シーンの Update で、`worldMatrix`・`GetWorldPosition` が正しい
- **変わらない**：**そのフレームの中で動かした分**は、次に `UpdateTransforms` を呼ぶ（Script・SYSTEM の後）まで入らない。例：Script の Update で親を動かして、すぐ子の `GetWorldPosition` を読むと、動かす前の値。同じフレームで `Instantiate` した物の子も、そのフレームの中では行列が無い。今すぐ正しい値が欲しいときは、今までどおり `EntityManager::CalcWorldMatrix(entity)`（親をたどって計算し直す）
- 複数のウィンドウ：ウィンドウごとの `SceneManager::Update` で呼ぶので、2つ目のウィンドウの頭でも作り直す（1つ目のウィンドウの Script が動かした分も入る。害は無い）

### 0.4 重さ

`UpdateTransforms` は全部の Entity の深さを数えて並べ、親から順に作る。1フレームに1回増える。写しで、3段の親子を 1000 組（Entity 3004 個）作って、1回の時間を測った：

| 構成 | 1回 |
|---|---|
| Release | 0.60 ms |
| Development | 1.60 ms |
| Debug | 4.2 ms |

今のゲームの数（数百個）なら Release で 0.1 ms 前後。数千個を超えて気になったら、3章の「親子が変わったときだけ並べ直す」を入れる。

## パートA `Scene/SceneManager.cpp`

`Update` の、フレームの頭の処理の最後（`UpdateDirty` の `#endif` の後、「一時停止は時間を0にする」の前）に足す

```cpp
#ifdef USE_IMGUI
	UpdateDirty();
#endif

	// ===== フレームの頭の最後：ワールド行列を、今の Transform から作り直す =====
	// 決まり：Script・SYSTEM が回る前には、全部の Entity のワールド行列が、そのときの Transform から作られている
	// フレームの頭（ここまでと EngineSystems::FlushPending）で Entity を作った・戻した（シーンを読む・作り直す・開く・Undo・エディタから Prefab を置く）ので、
	// 作り直さないと、その Entity の行列はまだ無い（単位行列＝原点）か、古いまま
	EntityManager::UpdateTransforms();

	// 一時停止は時間を0にする。Updateは走るので ApplyGV が効く
```

（`EntityManager.h` はもう include してある）

- 停止中（Editing）も呼ぶ：エディタで Undo したフレームのギズモ・UI World Follow も、戻した後の位置で描ける
- `UpdateDirty`（保存した中身と比べて * を付ける）より後でよい：保存する中身に `worldMatrix` は入らない

## パートB `ECS/EntityManager.h`

`UpdateTransforms` のコメントを書き換える

```cpp
	// 更新順序2：ワールド行列を親→子の順で計算する
	// フレームの頭の最後（SceneManager::Update。Script・SYSTEM の前）と、Script・SYSTEM の後（EngineSystems::Update）に呼ぶ
	// なので Script・SYSTEM が読むワールド行列は、フレームの頭の時点の Transform から作った物（そのフレームの中で動かした分は、次に呼ぶまで入らない）
	static void UpdateTransforms();
```

ここでビルドできる。

## パートC `Docs/Reference.md`

「1フレームの順番」の表の 4 の行

```
| 4 | `SceneManager::Update`：頭で作り直し（Play / Stop / Restart）・シーン切り替え（前のフレームの `LoadScene`）・保存 → `EntityManager::UpdateTransforms`（頭で作った・戻した Entity の `worldMatrix` も作る）→ **`IScene::Update`**（停止中は呼ばれない） | **ゲーム** |
```

「覚えておくこと」の `worldMatrix` の行

```
- `worldMatrix` は 4 の頭の最後と 6 で計算されるので、**Updateの中で読むと、フレームの頭の時点の値**（シーンを読んだ最初のフレームでも正しい。そのフレームの中で動かした分は 6 まで入らない）
```

「`worldMatrix` は…」の注意（`> **` で始まる段落）

```
> **`worldMatrix` はフレームの頭の最後（Script・SYSTEM の前）と、フレームの中盤（`UpdateTransforms`）で計算される。** シーンの `Update`・Script・SYSTEM で読むと、フレームの頭の時点の値（そのフレームの中で動かした分は入っていない）。位置を正確に使いたいときは `translation` から計算するか、`Draw` で読む。今すぐ正しい値が欲しいときは `EntityManager::CalcWorldMatrix(entity)`（親をたどって計算し直す）。
```

## 確かめること

| # | すること | こうなれば合格 |
|---|---|---|
| 1 | `scenes/HitDamageTest` を開いて Play | Turret の最初の1発が Turret から出る（前は画面の原点から出ていた） |
| 2 | ScreenTrailCursor.md を写した後、`scenes/CursorTrailTest` を開いて Play | 最初の3発が、3つの箱からまっすぐ出る |
| 3 | 何かを動かして Undo（停止中） | ギズモ・UI World Follow の印が、戻した位置にすぐ出る |
| 4 | 今までのシーンを Play・Stop | 動きが前と変わらない |

（2 は写しの自動テストで確かめた。1・3・4 は目では見ていない）

## 3章 次の候補

- `UpdateTransforms` を軽くする：今は呼ぶたびに全部の深さを数えて並べ直している。親子が変わったとき（作る・消す・SetParent）だけ並べ直し、並べた順を覚えておけば、並べる分（n log n）が無くなる
- 同じフレームの中で今すぐ正しい値が要る所が増えたら、`CalcWorldMatrix` を GameObject からも呼べるようにする（`GameObject::GetWorldPosition()` など）
