参考：
- Unity の Prefab（置いた物とファイルのつながり・Apply・Unpack の考え方）：https://docs.unity3d.com/Manual/Prefabs.html
- Unity のシーンを複数人で直すときの考え方（シーンを分ける・Prefab に分ける）：https://unity.com/how-to/version-control-systems
- `std::mt19937_64`：https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
- `std::random_device`：https://en.cppreference.com/w/cpp/numeric/random/random_device
- `std::seed_seq`：https://en.cppreference.com/w/cpp/numeric/random/seed_seq
- `std::unordered_set`：https://en.cppreference.com/w/cpp/container/unordered_set
- `std::filesystem::last_write_time`：https://en.cppreference.com/w/cpp/filesystem/last_write_time
- XOR（`^`）：https://en.cppreference.com/w/cpp/language/operator_arithmetic#Bitwise_logic_operators
- `.gitignore` と `git rm --cached`：https://git-scm.com/docs/gitignore ・ https://git-scm.com/docs/git-rm

# チーム制作の下準備：Prefab を参照で置く・番号をでたらめにする

Player 担当と Enemy 担当に分かれても git でぶつかりにくくするための、エンジンの直し。考え方は「**1つのファイルを2人が同時に書き換えない**」。

- **Entity の番号をでたらめな 64 ビットにする**：連番だと、2人が別々に足した Entity が同じ番号になり、マージしたシーンで参照が別の物を指す（コンフリクトにならずに静かに壊れる）
- **Prefab から置いた物を、シーンファイルに参照で書く**：シーンには「どの Prefab を、どこに置いたか」だけを書く。Player 担当が `Player.prefab` を直しても、シーンファイルは変わらない
- **エディタ**：Prefab から置いた物は Hierarchy で青く出る。Inspector に「Prefab と違うか」と Apply・Unpack のボタン
- **Save As Prefab で、ほかの Prefab を上書きしない**：同じ名前の別の Prefab がもうあるときは押せなくする（パートF）
- **チームのリポジトリの .gitignore**：人ごとに違うファイル（`.bak`・`Parameters/Editor/`）を git に入れない（2章）

パートは **A → B → C → D の順に写す**。A・B・C・D のどこまででもビルドできる（B だけだと、まだシーンは今までどおり中身ごと書かれる）。E はおまけ（見つけた不具合の1文字）。F は D の後（B の印を使う）。2章はチームのリポジトリを作るときの作業（AL4_01 ではやらない）。

ブランチは `feature/PrefabInstance`（develop から）。

> **2026-11-04：パートA〜F は写経せず、本体のエンジンに直接入れた**（写しで確かめたコードそのまま）。この md は、何をなぜ直したかの解説として残す。コードを読むときは、本体の該当ファイルを見る。

## 0章 全体の形

### 0.1 流れ

```
保存（Ctrl+S）
   SceneSerializer::SaveFile
     ├ ふつうの Entity            → 今までどおり中身を全部書く
     ├ Prefab から置いた物の一番上 → {"id", "name", "parent", "active", "prefab": パス, "components": {"Transform"}}
     ├ Prefab の中身              → 書かない（PrefabManager::FindOwner で分かる）
     └ あとから足した子           → ふつうの Entity と同じ（親の番号は Prefab の中の子の番号でもよい）

読み込み（シーンを開く）
   SceneSerializer::LoadFile
     ├ PrefabManager::ReloadChangedFiles（git pull で書き換わった Prefab を読み直す）
     └ "prefab" がある項目 → PrefabManager::InstantiateNow（一番上の番号＝ファイルの id。中の子の番号も毎回同じ）

Play / Stop の退避（SaveToText / LoadFromText）
   中身も全部書く（シーンで Prefab の中身を直していても、Stop で戻る）
```

### 0.2 作る物・直す物

| ファイル | パート | 役目 |
|---|---|---|
| `ECS/EntityManager.h`・`.cpp`・`ECS/Entity.h` | A | 番号をでたらめな 64 ビットで配る（`std::mt19937_64`） |
| `Editor/Windows/HierarchyWindow.cpp` | A・D | ImGui の ID を 64 ビットのまま使う／青い名前・Apply・Unpack のメニュー |
| `Editor/History/EditorHistory.cpp` | A・D | 貼り付けの番号を全員同じ数で XOR／Prefab の中身を外へ出さない |
| `Components/Core/PrefabInstanceComponent.h`（新しい） | B | 置いた物の印（Prefab のパス） |
| `Components/EngineComponents.cpp`・`ECS/EntityManager.cpp` | B | 印の登録（保存名 `Prefab Instance`） |
| `ECS/ComponentSerializer.h`・`.cpp` | B | 書くときにも、Entity への参照を表で付け替えられるように |
| `Scene/SceneSerializer.h`・`.cpp` | B・C | SaveTree で番号をずらして書く／シーンに参照で書く・読む（版 2） |
| `Scene/PrefabManager.h`・`.cpp` | B | 番号のずれ・印を付ける・中身か調べる・違うか調べる・Apply・Unpack・読み直し |
| `Editor/Windows/InspectorWindow.cpp` | D | Prefab のパス・違うか・Select Root・Apply・Unpack |
| `Editor/Windows/ProjectWindowActions.cpp` | D | Place in Scene の説明 |
| `String/FixedString.h` | E | 日本語の途中で切れる不具合（`0xC00` → `0xC0`） |
| `Scene/PrefabManager.h`・`.cpp`・`Editor/Windows/HierarchyWindow.cpp` | F | Save As Prefab で、同じ名前の別の Prefab を上書きしない（`IsOtherPrefabFile`） |

### 0.3 使い方

- **Prefab を作る**：今までどおり Hierarchy の右クリック → **Save As Prefab**。保存した物は、そのまま「Prefab から置いた物」になる（名前が青くなる）。
- **置く**：Project ウィンドウで `.prefab` を右クリック → **Place in Scene**。Script の `GameObject::Instantiate` で作った物も同じ。
- **Prefab を直す**：
  - Player 担当は、自分のテストシーン（`PlayerTest` など）に Player を置いて直し、Inspector の **Apply**（か Hierarchy の右クリック → **Apply to Prefab**）。`Player.prefab` だけが変わる。
  - 本番のシーンは開き直すと新しい中身になる（シーンファイルは変わらない）。
  - **もう一度 Save As Prefab と、Apply の違い**：書き先の決め方だけが違う。中身の書き方（ずれを戻してファイルの番号を変えない）は同じ。

    | | 書き先 | 違いが出るとき |
    |---|---|---|
    | Apply | つながっている Prefab（Inspector の `Prefab:` に出ているファイル） | — |
    | Save As Prefab | `resources/prefabs/<今の名前>.prefab` | 名前を変えていると、別の新しいファイルができて、つながりもそちらへ移る。`resources/prefabs/Enemy/` などフォルダの中の Prefab は、外の別のファイルに書いてしまう。Prefab と関係ない物で、同じ名前の Prefab がもうあるときは押せない（パートF） |

    名前を変えていない・フォルダに入れていないなら、結果は同じ。直すときは、書き先を間違えない Apply を使う。
- **置いた所ごとに変えてよい物**：一番上の名前・有効・Transform。中身（子・Component）を変えるなら Prefab を直す。
- **中身をシーンで直してしまったら**：Inspector に「Prefab と違う所があります」と出る。そのままシーンを保存すると、次に開いたときに Prefab のとおりに戻る（保存のときにも警告が出る）。残したいなら Apply。
- **あとから子を足す**：置いた物の下に、ふつうの Entity を子として入れてよい（シーンファイルに書かれる）。
- **つながりを切る**：Inspector の **Unpack**（か右クリック → **Unpack Prefab**）。それからは中身もシーンファイルに書かれ、Prefab を直しても変わらない。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 番号の配り方 | でたらめな 64 ビット（`std::mt19937_64`、種は `random_device` 4回分） | 2人が別々に足してもぶつからない（2^64 回に1回くらい）。前からある小さい番号のファイルもそのまま読める |
| 置いた物の中の子の番号 | ファイルの番号 XOR ずれ（ずれ＝置いた物の一番上の番号 XOR ファイルの一番上の番号） | 一番上の番号だけシーンに書けば、中の子の番号も毎回同じになる。シーンのほかの物から、中の子を指せる（カメラの Look At で Player の頭を見る など） |
| 中身かどうかの見分け | 親の方へたどり、印のある物 A について「自分の番号 XOR A のずれ」が A の Prefab のファイルにあるか | 印を子に付けなくてよい。足した子は番号がでたらめなので、ファイルに無い＝中身でない |
| 貼り付けの番号 | 全員を同じでたらめな数で XOR | 1つずつ別の番号にすると、貼った物の中の Prefab の「ずれが全員同じ」が崩れる |
| シーンに書く物 | 一番上の id・名前・親・有効・`prefab`・Transform と、足した子 | 置いた所ごとに違う物だけ。ほかを書くと、Prefab を直すたびにシーンも変わってしまう |
| 中身をシーンで直したとき | 保存しない（Prefab のとおりに戻る）。Inspector と保存の警告で知らせる | 1つずつの上書き（Unity の Override）を持つと、シーンファイルにまた中身が書かれ、ぶつかる所が戻ってくる。必要になったら足す（5章） |
| 中身を外へ出す | させない（警告を出して何もしない）。中身の中で動かすのはよい | 外へ出した物はシーンにふつうに書かれ、開き直すと Prefab からも同じ番号で作られてぶつかる |
| 中身を消す | してよい（「違う」になり、開き直すと戻る） | Unity は消せないが、ここでは分かれば十分 |
| Play の退避 | 中身も全部書く（印も Component として書く） | 参照だけにすると、シーンで中身を試しに直したのが Stop で消える |
| Prefab のファイルが無い | 印だけ付けた空の Entity にする。保存しても参照は消えない | Prefab をまだ pull していない人が保存しても、つながりが消えないように |
| Prefab のファイルが書き換わった | シーンを読む前に、時刻が変わった Prefab を読み直す | git pull で届いた Prefab を、エディタを開き直さずに使える |
| Apply したときのファイルの番号 | ずれを戻して書く（前と同じ番号） | Apply のたびに番号が全部変わると、diff が大きくなり、中の子への参照も切れる |
| 新しく Save As Prefab | 今の番号のまま書く（ずれ 0） | 保存した物がそのまま「置いた物」になり、中の子の番号も変わらない |
| Prefab の中の印（一番上） | Prefab のファイルには書かない | 自分を指す印が入ってしまう |
| 入れ子の Prefab | Prefab の中に、別の Prefab から置いた物を入れてもよい（中身ごと書かれる） | ただし、内側の Prefab を直しても、外側の Prefab の中の物は変わらない（5章） |
| UI Screen（`.uiscreen`） | 同じ仕組みで作るが、Hierarchy の青・Inspector のボタンは出さない | 画面の編集は Done で保存する、今までの流れのまま。保存するときにずれを戻すので、`.uiscreen` の番号も毎回同じになる |
| 版 | シーンファイルの版を 2 にする | 古いエンジンで開くと `prefab` を知らずに空の Entity にして保存してしまうので、読めないようにする。版 1 のファイルは今のエンジンでそのまま読める |
| Inspector の「違うか」 | 履歴が動いたとき・選ぶ物が変わったときだけ調べる | 調べるたびに中身を全部 JSON に書くので、毎フレームはしない |

### 0.5 場面ごとの動作

| 場面 | どうなるか |
|---|---|
| Prefab から置いた物のあるシーンを保存 | 一番上だけが `"prefab"` 付きで書かれる |
| 開き直す | Prefab から作り直す。中の子の番号は前と同じ |
| Prefab を Apply → 別のシーンを開く | 別のシーンの物も新しい中身になる（そのシーンのファイルは変わらない） |
| git pull で Prefab が変わった → シーンを開き直す | 新しい中身で作られる |
| 置いた物の子を、置いた物の外へドラッグ | 警告が出て、動かない |
| 置いた物を Ctrl+D・Ctrl+C → Ctrl+V | 貼った物も Prefab から置いた物（印ごと写る） |
| 置いた物を消して Ctrl+Z | 同じ番号で戻る |
| Unpack | 印が外れ、名前が白に戻る。次の保存から中身も書かれる |
| Prefab のファイルを消した | 開くと、印だけの空の Entity（警告）。ファイルを戻して開き直せば中身が戻る |
| 前に作ったシーン（版 1・中身ごと書いてある） | 今までどおり読める。印は付いていない（Prefab とはつながっていない） |

## 1章 解説

### 1.1 連番だと、マージしたシーンが静かに壊れる

今は `EntityManager` が 1, 2, 3, … と番号を配っている。シーンファイルの最後の番号が 20 のとき：

```
Aさん：Enemy を足す   → 番号 21。カメラの Look At に 21 を入れる
Bさん：アイテムを足す → 番号 21
```

2人の変更を git でマージすると、`"id": 21` が2つあるシーンになる（2人とも配列の最後に足すので、コンフリクトとして出ることも、出ないこともある）。読み込みは「2つ目の同じ番号」を飛ばすので、片方の Entity が消え、カメラの Look At は別の物を見る。

でたらめな 64 ビットの数なら、2人が別々に足してもまず同じにならない。数は大きくなる（`"id": 8845337304621853523`）が、ファイルが読みにくくなるだけで困らない。

### 1.2 Prefab の中の子の番号（XOR のずれ）

シーンファイルに一番上の番号だけを書くとき、中の子の番号はどう決めるか。毎回でたらめにもらうと、開くたびに変わり、シーンのほかの物から中の子を指せない（Watcher が Player の頭を見る、が保存できない）。

そこで「**置いた物の番号 = ファイルの番号 XOR ずれ**」にする。ずれは置いた物ごとに1つで、中の全員が同じ。

```
TestRig.prefab のファイル：  Rig=100  A=101  B=102
1つ目を置く：一番上の番号を 5000 にしたい → ずれ = 5000 XOR 100
              Rig = 100 XOR ずれ = 5000
              A   = 101 XOR ずれ（毎回同じ数）
              B   = 102 XOR ずれ
2つ目を置く：一番上を 7000 に → ずれが違うので、A・B も1つ目と別の番号
```

XOR（`^`）を使う理由：

- `x ^ s ^ s == x`（同じ数で2回 XOR すると戻る）。置いた物の番号からファイルの番号に戻すのも同じ計算（Apply のとき）
- 違う x どうしは、同じ s で XOR しても違う数のまま（ぶつからない）
- 全員を同じ数 d で XOR しても、全員のずれが同じ d だけ変わるだけ（貼り付けで使う。`(a ^ d) ^ (r ^ d) == a ^ r`）

「中身か」は、親の方へたどって印のある物 A を見つけ、`自分の番号 ^ (A の番号 ^ ファイルの一番上の番号)` がファイルの番号の一覧にあるかで分かる（`PrefabManager::FindOwner`）。足した子は番号がでたらめなので、一覧にはまず無い。

### 1.3 シーンファイルの形

**前**（置いた物の中身が全部シーンにある。Prefab を直すとシーンも直す必要がある）：

```json
{"id": 5, "name": "Player", "parent": 0, "active": true, "components": {"Transform": {...}, "Rigidbody": {...}, ...}},
{"id": 6, "name": "Body",   "parent": 5, "active": true, "components": {...}},
{"id": 7, "name": "Head",   "parent": 6, "active": true, "components": {...}},
```

**後**（一番上だけ。中身は `Player.prefab` から作る）：

```json
{"id": 8845337304621853523, "name": "Player", "parent": 0, "active": true, "prefab": "resources/prefabs/Player.prefab", "components": {
	"Transform": {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]}
}},
```

Player 担当が `Player.prefab` を直しても、この行は変わらない。Enemy 担当が敵を置いても、増えるのは敵の行だけ。2人が同時に本番シーンを触っても、同じ行を書き換えることが減る。

### 1.5 Apply の流れ（Player 担当・Enemy 担当の例）

**Prefab のファイルが設計図、シーンに置いた物は設計図から作った物**。シーンを開くたびに、設計図から作り直す。シーンで中身をいじっても、それはメモリの上だけの下書きで、設計図は変わらない。**Apply は、下書きを設計図に書き写すボタン**。

| 操作 | 書き換わるファイル |
|---|---|
| シーンを保存（Ctrl+S） | そのシーンのファイル。置いた物については「どの Prefab を、どこに」だけ |
| Apply | 置いた物がつながっている Prefab のファイル（シーンのファイルは変わらない） |
| Save As Prefab（まだ Prefab でない物） | 新しい Prefab のファイル |

Player 担当を A さん、Enemy 担当を B さんとする。

1. **A さんが Player を作る**：`PlayerTest` で Player（子に体・帽子）を組み立て、Save As Prefab → `Player.prefab` ができる。`PlayerTest` の Player は名前が青くなり、`PlayerTest` のファイルには「`Player.prefab` を (0, 0, 0) に」とだけ書かれる。push する。
2. **B さんが Player を置く**：pull して `EnemyTest` を開き、Project の `Player.prefab` → Place in Scene。`EnemyTest` のファイルには「`Player.prefab` を (5, 0, 0) に」とだけ書かれる。
3. **A さんがジャンプ力を直す**：`PlayerTest` の Player の Script の値を変える。この時点では下書きなので、Inspector に「Prefab と違う所があります」と出る。
   - Apply を押す → `Player.prefab` が新しいジャンプ力になる。`git status` で変わったのは `Player.prefab` だけ（`PlayerTest` は変わらない）。push する。
   - Apply を押さずにシーンだけ保存すると、次に開いたとき元のジャンプ力に戻る（保存のときに警告が出る）。
   - Apply の代わりに Save As Prefab を押した場合：名前が `Player` のままで、`Player.prefab` が `resources/prefabs/` のすぐ下にあるなら、Apply と同じ（番号も変わらない）。ただし名前を `Player2` に変えていると、`Player2.prefab` が新しくでき、`PlayerTest` の Player もそちらにつながる。`Player.prefab` は古いままなので、B さんには届かない（エラーも出ない）。`Characters/Player.prefab` のようにフォルダの中にあるときも、外に `Player.prefab` が新しくできて同じことになる。だから直すときは Apply にする。
4. **B さんに届く**：pull して `EnemyTest` を開き直すと、Player は新しいジャンプ力になる。B さんは何もしていないので、`EnemyTest` のファイルも変わらず、ぶつからない。
5. **B さんが Player の体力を 9999 にして、敵の攻撃を試す**：下書きなので「違う」と出るが、そのまま。`EnemyTest` を保存しても、残るのは Player の位置だけで、開き直すと体力は元に戻る。
   - ここで B さんが Apply を押すと、`Player.prefab` が体力 9999 になる。push すると、A さんの直しとぶつかるか、A さんの Player が体力 9999 になってしまう。

> 間違えて Apply したら：`git status` に自分の持ち物でないファイル（`Player.prefab`）が出る。コミットする前に、`git status` に出たパスのまま `git restore <パス>` で戻す（エディタはシーンを開き直すと読み直す）。コミットの前に `git status` を見るのを、チームの決まりにしておく。

### 1.4 あまり使わない STL

| STL | 何をする物か | ここでの使い方 |
|---|---|---|
| `std::mt19937_64` | メルセンヌ・ツイスタ（質のよい乱数）の 64 ビット版。`()` で次の 64 ビットの数を返す | 番号を配る（`EntityManager::idRandom_`） |
| `std::random_device` | OS が持っている本物のでたらめ（毎回違う数）。遅いので、種にだけ使う | 乱数の種 |
| `std::seed_seq` | いくつかの数を混ぜて、乱数の種にする道具。`std::mt19937_64(seed)` のように渡す | `random_device` の4回分を混ぜる。1回分（32 ビット）だけだと 40 億通りしかなく、別の PC で同じ並びになることがありうる |
| `std::unordered_set` | 重ならない値の集まり。`contains(x)` で、入っているかをすぐ調べられる（並びは決まっていない） | Prefab のファイルの番号の一覧（`PrefabAsset::ids`） |
| `std::filesystem::last_write_time` | ファイルを最後に書いた時刻。`error_code` を渡すと、無いファイルでも例外を投げない | Prefab が書き換わったかを見る（`ReloadChangedFiles`） |

## パートA 番号をでたらめにする

### A-1 `ECS/Entity.h`（コメントだけ）

```cpp
using EntityId = uint64_t; // 保存・Undoで使う番号。0は無し。実行中Handleとは別物。でたらめな数で配る（EntityManager::NewId）
```

### A-2 `ECS/EntityManager.h`

include に `<random>` を足す（`<optional>` の次）。

```cpp
#include <optional>
#include <random>
```

`CreateWithId`・`NewId` のコメントを直す。

```cpp
	// 番号（EntityId）を指定して作る。Undoで消したEntityを戻すとき・シーンや Prefab を読むとき
	static Handle<Entity> CreateWithId(EntityId id, const std::string& name, Handle<Entity> parent);
	// まだ誰も使っていない番号を1つもらう（でたらめな 64 ビットの数。チームの2人が別々に作っても、まずぶつからない）
	static EntityId NewId();
```

private の `EnsureInitialized` の次に足す。

```cpp
	static void EnsureInitialized();
	// 番号を配る乱数を作る（種は random_device から4回分。32 ビット1回分だけだと、別の PC と同じ並びになることがある）
	// ゲームの乱数（RandomEngine）とは分ける。ゲームの乱数の種を固定したときに番号まで毎回同じにならないように・Entity を作るたびにゲームの乱数の並びがずれないように
	static std::mt19937_64 MakeIdRandom();
```

メンバの `nextId_` を、乱数に変える。

```cpp
	std::mt19937_64 idRandom_ = MakeIdRandom(); // 番号を配る乱数（起動するたびに違う並びになる）
```

> メンバの初期値（`= MakeIdRandom()`）は、クラスの中を全部読んだ後に決まるので、後で宣言した関数を呼んでよい。

### A-3 `ECS/EntityManager.cpp`

`CreateWithId` の中の、`nextId_` を進める2行を消す。

```cpp
	instance_->idToHandle_[id] = handle;
	// 全Entityの必須データ。作った直後から取れるように、予約せずその場で作る
```

（消す行）

```cpp
	// 番号を指定して作ったときも、この先配る番号とぶつからないようにする
	instance_->nextId_ = (std::max)(instance_->nextId_, id + 1);
```

`NewId` を書き換え、`MakeIdRandom` を足す。

```cpp
EntityId EntityManager::NewId() {
	EnsureInitialized();
	// 連番だと、2人が別々に足した Entity が同じ番号になり、シーンファイルをマージしたときに参照が別の物を指してしまう
	// でたらめな数なら、ぶつかるのは 2^64 回に1回くらい。もう使っている番号・0 が出たら引き直す
	for (;;) {
		const EntityId id = instance_->idRandom_();
		if (id != 0 && !instance_->idToHandle_.contains(id)) {
			return id;
		}
	}
}

std::mt19937_64 EntityManager::MakeIdRandom() {
	std::random_device device;
	// seed_seq：いくつかの数を混ぜて、乱数の種にする（mt19937_64 は中に持つ値が大きいので、種も多めに渡す）
	std::seed_seq seed{device(), device(), device(), device()};
	return std::mt19937_64(seed);
}
```

> **乱数を作る物が増えてよいか**：「乱数生成器を何個も作らない」は、呼ぶたびに `std::mt19937 engine(std::random_device{}());` と作り直すのをやめよう、という意味。作り直すと重いうえに、同じ種から同じ数が続くことがある。ここで持ち続けるのは `idRandom_` の1つだけで、エンジンが動いている間に1回だけ作る。`random_device` と `seed_seq` は、その1回で種を作るのに使って捨てる。
>
> **`RandomEngine` を使わない理由**：
>
> | | `RandomEngine`（ゲームの乱数） | `idRandom_`（番号を配る） |
> |---|---|---|
> | 使う所 | 粒の飛び方・カメラの揺れなど | Entity の番号だけ |
> | 種を固定したいか | したいことがある（同じ動きを再現してデバッグする・リプレイ） | してはいけない。固定すると、2人が同じ番号を配ってぶつかる |
> | 大きさ | `mt19937`（32 ビット）・種は `random_device` 1回分（約 40 億通り） | `mt19937_64`（64 ビット）・種は4回分 |
>
> 同じ物から取ると、Entity を1つ作るたびにゲームの乱数の並びが1つずれて、同じ動きを再現できなくなる。また、種が 40 億通りしかないと、2人の起動の種がたまたま同じになったときに、配る番号が並びごと全部同じになる（めったに無いが、起きるとシーン全体がぶつかる）。用途ごとに1つずつ持つのが正解。

### A-4 `Editor/Windows/HierarchyWindow.cpp`（ImGui の ID）

include に `<string>` を足す（`<cfloat>` の次）。

```cpp
#include <cfloat>
#include <string>
```

`DrawEntityNode` の `PushID` を直す。`static_cast<int>` は 64 ビットの上の半分を捨てるので、番号がでたらめになると別の Entity と同じ ID になることがある。

```cpp
	// EntityIdをIDにする（名前が同じEntityがあってもぶつからない）
	// Handleの番号だと、Play / Stop で作り直したときに振り直されて、開いた・閉じた状態が別のEntityに移ってしまう
	// 番号は 64 ビットなので、int にすると上の半分が消えてぶつかることがある。文字にして全部使う
	ImGui::PushID(std::to_string(entity->id).c_str());
```

### A-5 `Editor/History/EditorHistory.cpp`（貼り付けの番号）

`Paste` の前に、XOR する数を選ぶ関数を足す。

```cpp
// 貼る物の番号を全部 XOR で変える数（変えた後の番号が、どれも0でなく、まだ誰も使っていない数を選ぶ）
EntityId MakeIdShift(const EntityTree& tree) {
	for (;;) {
		const EntityId shift = EntityManager::NewId(); // でたらめな数がほしいだけ（番号としては使わない）
		const bool canUse = std::ranges::all_of(tree, [shift](const EntityData& data) {
			const EntityId newId = data.id ^ shift;
			return newId != 0 && !EntityManager::FindById(newId).IsValid();
		});
		if (canUse) {
			return shift;
		}
	}
}
```

`Paste` の中の、番号を振り直す所を置き換える。

```cpp
	// 番号を振り直す。全員を同じ数（shift）で XOR する（子が覚えている親の番号も同じ数で XOR すれば、そのまま新しい親を指す）
	// 1つずつ別の番号にしないのは、貼った物に Prefab から置いた物が入っていても、Prefab の中の番号の並び（PrefabManager.h）が崩れないように
	const EntityId shift = MakeIdShift(tree);
	for (EntityData& data : tree) {
		data.id ^= shift;
	}
	for (size_t i = 1; i < tree.size(); ++i) {
		tree[i].parent ^= shift;
	}
```

（消す所：`std::unordered_map<EntityId, EntityId> newIds;` から `tree[i].parent = newIds.at(tree[i].parent);` の `for` の閉じかっこまで）

ここでビルドできる。新しく作った Entity の Inspector の `ID:` が大きな数になっていれば OK。

## パートB 置いた物の印と、Prefab の番号のずれ

### B-1 `Components/Core/PrefabInstanceComponent.h`（新しい）

vcxproj に足す（`FolderComponent.h` の次。filters も `HTTEngine\Components\Core`）。

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/String/FixedString.h"

// Prefab から置いた物の一番上に付く印（Unity の Prefab インスタンス）。Add Component には出さない
// PrefabManager::Instantiate（Project の Place in Scene・Script の Instantiate）と、Hierarchy の Save As Prefab で付く
// この印がある Entity は、シーンファイルに「どの Prefab を、どこに置いたか」だけを書く（中身は Prefab のファイルから作り直す）
// → Player 担当が Player.prefab を直しても、シーンファイルは変わらない（チームで同じシーンを触ってもぶつかりにくい）
// シーンに書くのは、一番上の名前・有効・親・Transform と、あとから足した子だけ。Prefab の中身をシーンで直しても、保存すると消える
// （残すときは Inspector の Apply で Prefab に書く。Prefab と違うかどうかは Inspector に出る）

/// <summary>
/// Prefab から置いた物の印
/// </summary>
struct PrefabInstanceComponent {
	FixedString<256> path; // 元の Prefab のファイル（UTF-8。"resources/prefabs/Player.prefab"）。ファイルに書くのはこれだけ
	uint32_t prefab = 0;   // PrefabManager の番号（書かない。読んだときに path から引く。ファイルが無ければ 0）
};

static_assert(std::is_trivially_copyable_v<PrefabInstanceComponent>);
```

> パスを番号（`prefab`）だけで持たないのは、Prefab のファイルがまだ届いていない人が開くと番号が 0 になり、そのまま保存するとどの Prefab だったかが消えてしまうため。

### B-2 `ECS/EntityManager.cpp`（型の登録）

```cpp
#include "HTTEngine/Components/Core/FolderComponent.h"
#include "HTTEngine/Components/Core/PrefabInstanceComponent.h"
```

`Initialize` の中：

```cpp
	RegisterComponent<FolderComponent>();
	RegisterComponent<PrefabInstanceComponent>();
```

### B-3 `Components/EngineComponents.cpp`（保存の項目）

include に3つ足す。

```cpp
#include "HTTEngine/Components/Core/FolderComponent.h"
#include "HTTEngine/Components/Core/PrefabInstanceComponent.h"
#include "HTTEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/Lighting/LightComponent.h"
#include "HTTEngine/Scene/PrefabManager.h"
#include "HTTEngine/String/ConvertString.h"
```

`DescribeFolder` の次に足す。

```cpp
//=============================================================================
// Prefab から置いた物の印
//=============================================================================
void DescribePrefabInstance(ComponentUI& ui, PrefabInstanceComponent& value) {
	const FixedString<256> before = value.path;
	ui.Field("path", value.path);
	// 読んだときだけ（path が変わったとき）番号を引く。書くときは何も変えない（Transform の回転と同じやり方）
	// Play の退避から戻すときもここを通るので、番号はいつも path に合う
	if (value.path != before) {
		value.prefab = value.path.Empty() ? 0 : PrefabManager::Load(ToPath(value.path.ToString()));
	}
}
```

`RegisterEngineComponents` に1行。Inspector の登録（`ComponentEditorRegistry`）はしない＝ Inspector の区画にも Add Component にも出ない（フォルダの印と同じ）。

```cpp
	Register<FolderComponent>("Folder", &DescribeFolder);
	Register<PrefabInstanceComponent>("Prefab Instance", &DescribePrefabInstance);
```

> `!=` は FixedString に書いていないが、C++20 は `==` から `!=` を作ってくれる。

### B-4 `ECS/ComponentSerializer.h`・`.cpp`（書くときの付け替え表）

Prefab に書くとき、中の Entity への参照（B の Look At が A を見る、など）も、ずれを戻した番号で書く必要がある。読む係には付け替え表（`idMap`）がもうあるので、書く係にも同じ物を渡せるようにする。

`.h` の `Save`：

```cpp
	// Entityが持っているComponentを全部 out に書く（{"型の名前": {"項目": 値, ...}, ...}）。警告は warnings に集める（同じ文を何度も出さないため）
	// idMap を渡すと、Component の中の Entity への参照を表で付け替えて書く（Prefab に書くとき。表に無い番号はそのまま）
	static void Save(Handle<Entity> entity, SceneJson& out, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr);
```

`.cpp` の `JsonWriterUI`：コンストラクタ・`EntityRef` の項目・メンバ。

```cpp
	JsonWriterUI(SceneJson& out, std::string_view typeName, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr)
	    : out_(out), typeName_(typeName), warnings_(warnings), idMap_(idMap) {}
```

```cpp
	// 相手の EntityId を書く。相手がもう居なければ0（次に起動したとき、同じ番号が別のEntityに配られることがあるので）
	void Field(const char* label, EntityRef& value, FieldStyle) override {
		const bool isAlive = EntityManager::Find(value).IsValid();
		EntityId id = isAlive ? value.id : EntityId{0};
		// Prefab に書くときは、表にある番号（Prefab の中の相手）を、ファイルの番号に直す。表に無い相手はそのまま（作るときに外れる）
		if (idMap_ != nullptr) {
			if (const auto found = idMap_->find(id); found != idMap_->end()) {
				id = found->second;
			}
		}
		Put(label, id);
	}
```

```cpp
	SceneJson& out_;
	std::string_view typeName_;       // 警告に出す型の名前
	std::set<std::string>& warnings_; // 警告（同じ文は1回だけ出すように、集めてから出す）
	const EntityIdMap* idMap_;        // EntityId の付け替え表（Prefab に書くとき。シーンに書くときは nullptr）
};
```

`ComponentSerializer::Save`：引数を足して、書く係に渡す。

```cpp
void ComponentSerializer::Save(Handle<Entity> entity, SceneJson& out, std::set<std::string>& warnings, const EntityIdMap* idMap) {
	for (const Entry& entry : Entries()) {
		// 中身は手元で作ってから入れる（ordered_map は中身が vector なので、out[...] の参照を持ったまま別の項目を足すと参照が壊れる）
		SceneJson fields = SceneJson::object();
		JsonWriterUI writer(fields, entry.name, warnings, idMap);
		if (entry.save(entity, writer)) {
			out[entry.name] = std::move(fields);
		}
	}
}
```

### B-5 `Scene/SceneSerializer.h`・`.cpp`（SaveTree で番号をずらす）

`.h` の `SaveTree`：

```cpp
	// root と、その子孫を {"version", "entities"} にする（root が先頭・親が子より前。root の親は0として書く）
	// 番号は shift で XOR して書く（中の Entity への参照も。Prefab の番号のずれを戻すため。PrefabManager.h）。root の「置いた物の印」は書かない
	static SceneJson SaveTree(Handle<Entity> root, EntityId shift = 0);
```

`.cpp` の include に2つ足し、namespace の頭に名前を3つ置く（`kTransformName`・`kPrefabKey` はパートC で使う）。

```cpp
#include "HTTEngine/Components/Core/PrefabInstanceComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Scene/PrefabManager.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr const char* kTransformName = "Transform";            // Transform の保存の名前（Prefab から置いた物は、これだけシーンに書く）
constexpr const char* kPrefabInstanceName = "Prefab Instance"; // 置いた物の印の保存の名前（EngineComponents.cpp）
constexpr const char* kPrefabKey = "prefab";                   // シーンファイルの、Prefab から置いた物の印（値は Prefab のパス）
```

`SaveEntity`：書く番号と付け替え表を受け取るようにする。

```cpp
// ===== Entity 1つ分（{"id", "name", "parent", "active", "components"}）。id・parentId は書く番号（0は親なし）=====
// idMap を渡すと、Component の中の Entity への参照を表で付け替えて書く（Prefab に書くとき）
SceneJson SaveEntity(const Entity& entity, EntityId id, EntityId parentId, std::set<std::string>& warnings, const EntityIdMap* idMap = nullptr) {
	SceneJson components = SceneJson::object();
	ComponentSerializer::Save(entity.self, components, warnings, idMap);

	// 1つのEntityの中身は手元で組み立ててから入れる（ordered_map は途中で参照を持つと壊れるため）
	SceneJson item = SceneJson::object();
	item["id"] = id;
	item["name"] = entity.name;
```

（`"parent"` から下は今のまま）

`SaveAll` の `SaveEntity` の呼び方を合わせる（パートC で `SaveAll` を書き換えるので、ここではこの1行だけ）。

```cpp
		entities.push_back(SaveEntity(entity, entity.id, parent ? parent->id : EntityId{0}, warnings));
```

`SaveTree` を書き換える。

```cpp
SceneJson SceneSerializer::SaveTree(Handle<Entity> root, EntityId shift) {
	// ===== root から幅優先でたどる（先に見つけた物から並べるので、親が必ず子より前になる。EditorHistory の写しと同じ）=====
	std::vector<Handle<Entity>> queue = {root};
	std::vector<Handle<Entity>> children;
	for (size_t i = 0; i < queue.size(); ++i) {
		EntityManager::GetChildren(queue[i], children);
		queue.insert(queue.end(), children.begin(), children.end());
	}
	// ===== 番号の付け替え表（中の Entity の番号 → 書く番号）。中の相手への参照も、同じ番号に直して書く =====
	EntityIdMap idMap;
	for (const Handle<Entity> handle : queue) {
		if (const Entity* entity = EntityManager::Get(handle)) {
			idMap[entity->id] = entity->id ^ shift;
		}
	}
	// ===== 書く =====
	std::set<std::string> warnings;
	SceneJson entities = SceneJson::array();
	for (size_t i = 0; i < queue.size(); ++i) {
		const Entity* entity = EntityManager::Get(queue[i]);
		if (entity == nullptr) {
			continue;
		}
		// root の親は書かない（Prefab の外なので。作るときに Instantiate の parent を付ける）
		const Entity* parent = EntityManager::Get(entity->parent);
		const EntityId parentId = (i == 0 || parent == nullptr) ? EntityId{0} : (parent->id ^ shift);
		SceneJson item = SaveEntity(*entity, entity->id ^ shift, parentId, warnings, &idMap);
		if (i == 0) {
			item["components"].erase(kPrefabInstanceName); // root の「置いた物の印」は書かない（Prefab の中に、自分を指す印が入ってしまう）
		}
		entities.push_back(std::move(item));
	}
	FlushWarnings(warnings);
	return MakeRoot(std::move(entities));
}
```

### B-6 `Scene/PrefabManager.h`（全部）

```cpp
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <filesystem>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// Prefab の置き場所と、名前の後ろに付く部分（"resources/prefabs/Box.prefab" の Prefab の名前は "Box"）
inline constexpr const char* kPrefabFolder = "resources/prefabs/";
inline constexpr const char* kPrefabFileExtension = ".prefab";
// 名前から Prefab のファイルの場所を作る（"Box" → "resources/prefabs/Box.prefab"）
inline std::string MakePrefabFilePath(const std::string& name) { return std::string(kPrefabFolder) + name + kPrefabFileExtension; }


/// <summary>
/// Prefab（Entity・その子・Component・Script をまとめてファイルに取っておき、同じ物を何個でも作れる物。Unity の Prefab）
/// <para>ファイルの中身はシーンファイルと同じ形（{"version", "entities"}）。並びは一番上の Entity が先頭で、親が子より前</para>
/// <para>作る：Instantiate。Entity と Transform はその場でできるので、すぐ位置を変えてよい。
/// ほかの Component・Script は次のフレームの頭（Flush）に付く（GameObject::AddComponent と同じ「予約」）</para>
/// <para>作った物の一番上には「置いた物の印」（PrefabInstanceComponent）が付き、シーンファイルには参照だけが書かれる。
/// Prefab を保存し直すと、次にシーンを開いたときから新しい中身になる（開いている今のシーンの物は変わらない）</para>
/// <para>番号のずれ：置いた物の番号は「ファイルの番号 XOR ずれ」。ずれは「置いた物の一番上の番号 XOR ファイルの一番上の番号」で、
/// 中の全員で同じ。一番上の番号さえシーンに書けば、中の子の番号も毎回同じになる（シーンのほかの Entity から、中の子を指せる）</para>
/// </summary>
class PrefabManager {
public:
	static void Initialize();
	static void Release();

	// ===== 読み込み（アセット。InspectorのAssetField（AssetType::Prefab）からも呼ばれる）=====
	// 同じファイルは1回だけ読む（2回目からは同じ番号を返す）。読めなければ0
	static uint32_t Load(const std::filesystem::path& filePath);
	// 番号からパス（UTF-8。保存・Inspector の表示に使う）。知らない番号なら空
	static const std::string& GetPath(uint32_t handle);
	// 読み込んだ後にファイルが書き換わった Prefab を読み直す（git pull で届いた Prefab を使うため）。シーンファイルを読む前に呼ぶ
	static void ReloadChangedFiles();

	// ===== 保存（エディタの Hierarchy の右クリック → Save As Prefab・Apply から）=====
	/// <summary>
	/// rootと、その子孫をPrefabのファイルに書く。もう読み込んでいる Prefab なら、中身も差し替える（このあと作る物から新しい中身になる）
	/// <para>root には「この Prefab から置いた物」の印を付ける（Unity と同じ。次からシーンには参照だけが書かれる）</para>
	/// <para>root がこの Prefab から置いた物なら、ずれを戻して書く（ファイルの番号が変わらないので、diff が小さい）</para>
	/// </summary>
	/// <param name="path">書く場所（UTF-8。ふつうは MakePrefabFilePath(名前)）</param>
	static bool Save(Handle<Entity> root, const std::string& path);

	// ===== 作る =====
	/// <summary>
	/// Prefab から Entity を作り、一番上の Entity を返す（読めていない番号なら無効な Handle）
	/// <para>一番上の Entity の位置（translation）は position になる。回転・大きさは Prefab のまま</para>
	/// </summary>
	/// <param name="parent">親（無効な Handle なら一番上に置く）</param>
	static Handle<Entity> Instantiate(uint32_t prefab, const Vector3& position, Handle<Entity> parent = {});
	/// <summary>
	/// シーンファイルを読むとき用。一番上を rootId の番号で作り、Component・印まで今すぐ付ける（フレームの境目でだけ呼ぶ）。親は付けない
	/// <para>中の番号がほかの Entity とぶつかったら、新しい番号で作って warnings に書く。読めていない番号なら無効な Handle</para>
	/// </summary>
	static Handle<Entity> InstantiateNow(uint32_t prefab, EntityId rootId, std::set<std::string>& warnings);
	// エディタから置く（ImGui を描いている途中で Entity を増やさないように、次のフレームの頭に作る。原点に置く）
	static void RequestPlace(uint32_t prefab);
	// 予約した物を作る・Component を付ける。フレームの頭（WindowManager::UpdateAll の EntityManager::FlushComponentChanges の後）に呼ぶ
	static void Flush();

	// ===== 置いた物（Prefab のインスタンス）=====
	// entity を「Prefab の中身」として持つ、一番近い置いた物（親の方へたどる。entity 自身は見ない）。無ければ無効な Handle
	// あとから足した子は中身ではない（シーンファイルにそのまま書かれる）
	static Handle<Entity> FindOwner(Handle<Entity> entity);
	// エディタで「Prefab から置いた物」として見せる一番上（entity が置いた物なら entity、中身なら持ち主）。無ければ無効な Handle
	// .prefab から置いた物だけ（UI Screen の .uiscreen から作った画面は、同じ仕組みで作るが Prefab としては見せない）
	static Handle<Entity> FindInstanceRoot(Handle<Entity> entity);
	// 置いた物の中身が、Prefab のファイルと違うか（一番上の名前・有効・Transform と、あとから足した子は見ない）
	static bool IsModified(Handle<Entity> root);
	// 置いた物を、元の Prefab のファイルに書く（Unity の Apply）
	static bool Apply(Handle<Entity> root);
	// Prefab とのつながりを切る（印を外す。次のフレームの頭に外れ、それからは中身もシーンファイルに書かれる）
	static void Unpack(Handle<Entity> root);
};
```

### B-7 `Scene/PrefabManager.cpp`（全部）

作る処理を「Entity を作る（`CreateEntities`）」「Component を付ける（`LoadComponents`）」「印を付ける（`Link`）」に分けて、`Instantiate`（Transform 以外は次のフレーム）と `InstantiateNow`（全部その場で）で使い回す。

```cpp
#include "PrefabManager.h"

#include <algorithm>
#include <format>
#include <fstream>
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
constexpr const char* kTransformName = "Transform"; // Transform の保存の名前（作るときに、これだけ先に付ける）

/// <summary>
/// 読み込んだ Prefab 1つ分
/// </summary>
struct PrefabAsset {
	std::string path;                          // パス（UTF-8。ToPathKey の形。読み込み済みかを調べるキーにもなる）
	std::shared_ptr<const SceneJson> entities; // Entity の並び（ファイルの "entities"）。保存し直すと、新しい物に差し替わる
	std::unordered_set<EntityId> ids;          // ファイルに書いてある番号全部（置いた物の中身かを調べる。entities と一緒に差し替える）
	std::filesystem::file_time_type writeTime; // 読んだ・書いたときのファイルの時刻（ReloadChangedFiles で比べる）
};

/// <summary>
/// 作ったけれど、まだ Transform 以外の Component を付けていない物（次のフレームの頭の Flush で付ける）
/// </summary>
struct PendingLoad {
	uint32_t prefab = 0;                       // どの Prefab から作ったか（印に書く）
	std::shared_ptr<const SceneJson> entities; // 作ったときの中身（途中で Prefab を保存し直しても、作ったときの中身で付ける）
	std::vector<Handle<Entity>> created;       // entities の i 番目から作った Entity
	EntityIdMap idMap;                         // ファイルの番号 → 作った Entity の番号
};

/// <summary>
/// PrefabManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PrefabState {
	std::vector<PrefabAsset> assets;       // 読み込んだ Prefab（番号 - 1 が添字。番号は1から、0は未選択）
	std::vector<PendingLoad> pendingLoads; // Component を付ける予約
	std::vector<uint32_t> placeRequests;   // エディタから置く予約
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
	       isOptional("components", &SceneJson::is_object);
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

// 中身を差し替える（番号の一覧も一緒に作り直す）
void SetEntities(PrefabAsset& asset, std::shared_ptr<const SceneJson> entities) {
	asset.ids.clear();
	for (const SceneJson& item : *entities) {
		asset.ids.insert(item.at("id").get<EntityId>());
	}
	asset.entities = std::move(entities);
}

//=============================================================================
// 番号のずれ（置いた物の番号 = ファイルの番号 XOR ずれ）
//=============================================================================
// ファイルの一番上の Entity の番号
EntityId RootFileId(const PrefabAsset& asset) { return asset.entities->front().at("id").get<EntityId>(); }

// 一番上を rootId の番号で置いたときの、ずれ（一番上は ファイルの番号 XOR ずれ = rootId になる）
EntityId ShiftOf(const PrefabAsset& asset, EntityId rootId) { return rootId ^ RootFileId(asset); }

// そのずれで作ると、全員の番号が 0 でなく、まだ誰も使っていないか
bool CanUseShift(const PrefabAsset& asset, EntityId shift) {
	return std::ranges::all_of(asset.ids, [shift](EntityId fileId) {
		const EntityId id = fileId ^ shift;
		return id != 0 && !EntityManager::FindById(id).IsValid();
	});
}

// 新しく置くときの、ずれ（一番上の番号を新しくもらい、中の全員がぶつからなければ使う）
EntityId PickShift(const PrefabAsset& asset) {
	for (;;) {
		const EntityId shift = ShiftOf(asset, EntityManager::NewId());
		if (CanUseShift(asset, shift)) {
			return shift;
		}
	}
}

//=============================================================================
// 作る
//=============================================================================
// ===== Entity を作り、親子をつなぐ（Component はまだ。Transform は Entity と一緒にできている）=====
PendingLoad CreateEntities(uint32_t prefab, const PrefabAsset& asset, EntityId shift, Handle<Entity> parent) {
	PendingLoad pending;
	pending.prefab = prefab;
	pending.entities = asset.entities; // 中身を共有する（写さない）
	const SceneJson& entities = *pending.entities;
	pending.created.reserve(entities.size());

	// --- 1周目：Entity を作る（番号はファイルの番号 XOR ずれ。ファイルの番号 → 作った番号を表に入れる）---
	for (const SceneJson& item : entities) {
		const EntityId fileId = item.at("id").get<EntityId>();
		const Handle<Entity> created = EntityManager::CreateWithId(fileId ^ shift, item.value("name", std::string("Entity")), {});
		EntityManager::Get(created)->isActive = item.value("active", true);
		pending.idMap[fileId] = fileId ^ shift;
		pending.created.push_back(created);
	}

	// --- 2周目：親子をつなぐ ---
	// 先頭（一番上）は parent の子に。それ以外は、ファイルに書いてある親（見つからなければ一番上）の子にする
	const Handle<Entity> root = pending.created.front();
	for (size_t i = 0; i < entities.size(); ++i) {
		Handle<Entity> newParent = parent;
		if (i > 0) {
			const auto found = pending.idMap.find(entities[i].value("parent", EntityId{0}));
			newParent = (found != pending.idMap.end()) ? EntityManager::FindById(found->second) : root;
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
		const SceneJson* components = FindComponents((*pending.entities)[i]);
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
		ComponentSerializer::Load(pending.created[i], part, warnings, &pending.idMap);
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

//=============================================================================
// 作る
//=============================================================================
Handle<Entity> PrefabManager::Instantiate(uint32_t prefab, const Vector3& position, Handle<Entity> parent) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	const PrefabAsset* asset = Find(prefab);
	if (asset == nullptr) {
		LogManager::Warning(std::format("読み込んでいない Prefab の番号 {} から作ろうとしました（Inspector で Prefab を選んでいるか確かめてください）", prefab));
		return {};
	}
	// ===== Entity を作る（一番上の番号は新しくもらう）=====
	PendingLoad pending = CreateEntities(prefab, *asset, PickShift(*asset), parent);

	// ===== Transform だけ今すぐ付ける =====
	std::set<std::string> warnings;
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

Handle<Entity> PrefabManager::InstantiateNow(uint32_t prefab, EntityId rootId, std::set<std::string>& warnings) {
	HTT_ASSERT_MSG(state != nullptr, "PrefabManager::Initialize()を先に呼んでください");
	const PrefabAsset* asset = Find(prefab);
	if (asset == nullptr) {
		return {};
	}
	// --- 一番上を rootId にするずれ。中の誰かがぶつかるなら、新しい番号で作る（シーンのほかの物からの参照は外れる）---
	EntityId shift = ShiftOf(*asset, rootId);
	if (!CanUseShift(*asset, shift)) {
		warnings.insert(std::format("Prefab の中の番号がほかの Entity とぶつかったので、新しい番号で作りました（この物への参照は外れます）: {}", asset->path));
		shift = PickShift(*asset);
	}
	// --- 作って、Component・印までその場で付ける（フレームの境目なので安全）---
	// asset は、この先の Load（中に入れ子の Prefab があるとき）で場所が変わることがあるので、ここから先は使わない
	const PendingLoad pending = CreateEntities(prefab, *asset, shift, {});
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
	// 親の方へたどり、印のある物の中身か（自分の番号 XOR ずれが、ファイルの番号にあるか）を調べる
	Handle<Entity> current = target->parent;
	while (const Entity* ancestor = EntityManager::Get(current)) {
		const PrefabAsset* asset = FindAssetOf(current);
		if (asset != nullptr && asset->ids.contains(target->id ^ ShiftOf(*asset, ancestor->id))) {
			return current;
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
	// --- 今の物を、ファイルと同じ番号で書いてみる ---
	const SceneJson saved = SceneSerializer::SaveTree(root, ShiftOf(*asset, rootEntity->id));
	const SceneJson& now = saved.at("entities");
	// --- ファイルの番号 → ファイルの1項目 ---
	std::unordered_map<EntityId, const SceneJson*> original;
	for (const SceneJson& item : *asset->entities) {
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

> **`Find` が返すポインタに気をつける**：`state->assets` は `std::vector` なので、`Load` で新しい Prefab を足すと、中身の場所が変わることがある（前に取ったポインタが壊れる）。`InstantiateNow` で Component を付けると、入れ子の Prefab の印（`DescribePrefabInstance`）が `Load` を呼ぶので、`CreateEntities` の後は `asset` を使わない。

ここでビルドできる。Save As Prefab した物が、次のフレームから印を持つ（まだシーンファイルは中身ごと書かれる）。

## パートC シーンファイルに参照で書く

すべて `Scene/SceneSerializer.h`・`.cpp`。

### C-1 `.h`（版とコメント）

```cpp
/// <para>読み込みはフレームの境目（SceneManager::Update の頭）でだけ行う。今あるEntityは消さないので、先に DestroyAllNow を呼ぶ</para>
/// <para>シーンファイルには、Prefab から置いた物を参照（"prefab" と一番上の Transform）だけで書く。Play の退避の文字列は中身も全部書く</para>
/// </summary>
class SceneSerializer {
public:
	// ファイルの形の版。値の意味を変えたら（回転をクオータニオンにする等）上げて、古い版を読み替える処理を足す
	// 2：Prefab から置いた物を参照で書く（"prefab"）。1 のファイルはそのまま読める（"prefab" が無いだけ）
	static constexpr int kVersion = 2;

	// ===== ファイル =====
	// フォルダが無ければ作る。前のファイルは「〇〇.bak」として1つだけ残す
	static bool SaveFile(const std::string& path);
	// 読む前に、書き換わった Prefab を読み直す（PrefabManager::ReloadChangedFiles）
	static SceneLoadResult LoadFile(const std::string& path);

	// ===== 文字列（Playを押した瞬間の退避に使う。Prefab の中身も全部書く＝シーンで直した所も Stop で戻る）=====
```

### C-2 `.cpp` 書く側（`SaveInstance` と `SaveAll`）

今の `SaveAll` を置き換える（`SaveInstance` はその前に足す）。

```cpp
// ===== Prefab から置いた物 1つ分（{"id", "name", "parent", "active", "prefab", "components": {"Transform"}}）=====
// 中身は書かない（読むときに Prefab のファイルから作り直す）。Prefab を直しても、シーンファイルは変わらない
SceneJson SaveInstance(const Entity& entity, EntityId parentId, const PrefabInstanceComponent& link, std::set<std::string>& warnings) {
	SceneJson item = SaveEntity(entity, entity.id, parentId, warnings);
	// --- Component は Transform だけ残す（置いた所ごとに違う物。ほかは Prefab と同じ）---
	SceneJson components = SceneJson::object();
	if (const auto transform = item["components"].find(kTransformName); transform != item["components"].end()) {
		components[kTransformName] = *transform;
	}
	item.erase("components");
	item[kPrefabKey] = link.path.ToString();
	item["components"] = std::move(components); // 並びを id, name, parent, active, prefab, components にする
	// --- シーンで Prefab の中身を直していたら、保存されないことを知らせる ---
	if (PrefabManager::IsModified(entity.self)) {
		warnings.insert(std::format("「{}」は Prefab（{}）と違う所がありますが、シーンには Prefab の参照だけを書きました。次に開くと Prefab のとおりに戻ります（残すなら Inspector の Apply）",
		                            entity.name, link.path.View()));
	}
	return item;
}

// ===== 全Entity。isFile なら、Prefab から置いた物は参照だけを書く（Play の退避は、シーンで直した所も戻せるよう中身も全部書く）=====
SceneJson SaveAll(bool isFile) {
	std::set<std::string> warnings;
	SceneJson entities = SceneJson::array();
	// 並んでいる順（Hierarchyに出る順）のまま書く。読むときも同じ順に作るので、並びが変わらない
	for (const Entity& entity : EntityManager::GetAll()) {
		if (IsDontSave(entity)) {
			continue; // エディタで編集中の画面の中身（画面のファイルに書く）
		}
		const Entity* parent = EntityManager::Get(entity.parent);
		const EntityId parentId = parent ? parent->id : EntityId{0};
		if (isFile) {
			if (PrefabManager::FindOwner(entity.self).IsValid()) {
				continue; // Prefab の中身（読むときに Prefab から作られる）
			}
			if (const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(entity.self)) {
				entities.push_back(SaveInstance(entity, parentId, *link, warnings));
				continue;
			}
		}
		entities.push_back(SaveEntity(entity, entity.id, parentId, warnings));
	}
	FlushWarnings(warnings);
	return MakeRoot(std::move(entities));
}
```

> `item.erase("components")` してから `"prefab"` と `"components"` を入れ直すのは、`SceneJson` が書いた順に並ぶ（ordered_map）ため。`"prefab"` が `"components"` の後ろの行に来ると、長い Transform の後ろに隠れて読みにくい。

### C-3 `.cpp` 読む側（`LoadInstance` と `LoadAll` の1周目）

`LoadAll` の前に足す。

```cpp
// ===== Prefab から置いた物を作る（中身は Prefab のファイルから。Prefab が読めなければ、印だけ付けた空の Entity）=====
Handle<Entity> LoadInstance(EntityId id, const std::string& name, const std::string& path, std::set<std::string>& warnings) {
	const uint32_t prefab = PrefabManager::Load(ToPath(path));
	Handle<Entity> root = PrefabManager::InstantiateNow(prefab, id, warnings);
	if (!root.IsValid()) {
		// ファイルが無い（まだ pull していない など）。印は残すので、このまま保存しても参照は消えない
		warnings.insert(std::format("Prefab が読めないので、空の Entity にしました（ファイルが届いてからシーンを開き直すと、中身ができます）: {}", path));
		root = EntityManager::CreateWithId(id, name, {});
		PrefabInstanceComponent link;
		link.path = path;
		EntityManager::RestoreComponents(root, {ComponentSnapshot::Make(link)});
	}
	EntityManager::Get(root)->name = name; // 名前は置いた所ごとに変えてよい
	return root;
}
```

`LoadAll` の1周目の、`CreateWithId` の所を置き換える。

```cpp
		if (id == 0 || EntityManager::FindById(id).IsValid()) {
			warnings.insert(std::format("EntityIdが0か、2つ目の同じ番号なので飛ばしました: {}", name));
			continue;
		}
		// "prefab" があれば Prefab から置いた物（中身も一緒にできる。一番上の Transform は3周目でファイルの値になる）
		const auto prefab = item.find(kPrefabKey);
		const bool isInstance = (prefab != item.end() && prefab->is_string());
		created[i] = isInstance ? LoadInstance(id, name, prefab->get<std::string>(), warnings) : EntityManager::CreateWithId(id, name, {});
		EntityManager::Get(created[i])->isActive = item.value("active", true);
```

2周目（親子）・3周目（Component）は今のまま。置いた物の項目の `"components"` には Transform しか無いので、3周目は一番上の Transform をファイルの値で上書きするだけになる。足した子の `"parent"` は Prefab の中の子の番号のこともあるが、1周目で中身が全部できているので見つかる。

### C-4 `.cpp` の残り（`SaveFile`・`LoadFile`・`SaveToText`）

```cpp
bool SceneSerializer::SaveFile(const std::string& path) {
	if (!WriteFile(path, SaveAll(true))) {
```

`LoadFile` の、ファイルを開く前：

```cpp
	// git pull で Prefab が届いていたら、新しい中身から作る
	PrefabManager::ReloadChangedFiles();
	std::ifstream stream(path);
```

```cpp
std::string SceneSerializer::SaveToText() { return ToLine(SaveAll(false)); } // 1行に詰める（人が読まないので）
```

ここでビルドできる。Prefab から置いた物のあるシーンを保存すると、シーンファイルに `"prefab"` の行が出る。

## パートD エディタ

### D-1 `Editor/History/EditorHistory.cpp`（中身を外へ出さない）

include を2つ足す。

```cpp
#include <cstddef>
#include <format>
#include <span>
```

```cpp
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Scene/PrefabManager.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
```

`RequestReparent` の中の、`oldParentId == parentId` の `return` の次に足す。

```cpp
		// Prefab の中身は、置いた物の外へは出せない（シーンを開き直すと、Prefab から同じ番号でもう1つ作られてしまうので）
		const Handle<Entity> owner = PrefabManager::FindOwner(childHandle);
		const Handle<Entity> newParent = EntityManager::FindById(parentId);
		if (owner.IsValid() && newParent != owner && !EntityManager::IsDescendantOf(newParent, owner)) {
			LogManager::Warning(std::format("「{}」は Prefab の中身なので、外へは出せません（出すときは、先に Inspector の Unpack）", entity->name));
			return;
		}
```

### D-2 `Editor/Windows/HierarchyWindow.cpp`（青い名前・メニュー）

namespace の頭に色を2つ。

```cpp
namespace {
// Prefab から置いた物の名前の色（Unity と同じく青。一番上は濃く、中身は薄く）
const ImVec4 kPrefabRootColor = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
const ImVec4 kPrefabContentColor = ImVec4(0.65f, 0.80f, 0.95f, 1.0f);
```

`DrawEntityNode` の `TreeNodeEx` を、色で挟む。

```cpp
	// Prefab から置いた物は、名前を青で描く（どこまでが Prefab の中身か＝シーンで直しても保存されない所か、分かるように）
	const Handle<Entity> prefabRoot = PrefabManager::FindInstanceRoot(handle);
	if (prefabRoot.IsValid()) {
		ImGui::PushStyleColor(ImGuiCol_Text, (prefabRoot == handle) ? kPrefabRootColor : kPrefabContentColor);
	}
	// フォルダの名前は、フォルダの絵の後ろに自分で描く（DrawFolderLabel）ので、ツリーには名前を出さない
	const bool isOpen = ImGui::TreeNodeEx("##node", flags, "%s", (isRenaming || isFolder) ? "" : entity->name.c_str());
	if (prefabRoot.IsValid()) {
		ImGui::PopStyleColor();
	}
```

`DrawEntityMenu` の Save As Prefab の説明を直し、その次に Apply・Unpack を足す。

```cpp
	ImGui::SetItemTooltip(canSavePrefab ? "子も一緒に resources/prefabs/ に保存する（同じ名前があれば上書き）。これからは Prefab から置いた物になる" : "名前を英数字と _ - だけにすると保存できる（ファイルの名前になるので）");
	// --- Prefab から置いた物の一番上：元の Prefab に書く・つながりを切る（Inspector のボタンと同じ）---
	if (PrefabManager::FindInstanceRoot(handle) == handle) {
		if (ImGui::MenuItem("Apply to Prefab")) {
			PrefabManager::Apply(handle);
		}
		ImGui::SetItemTooltip("シーンで直した中身を、元の Prefab のファイルに書く（ほかのシーンの物も、次に開いたときから同じになる）");
		if (ImGui::MenuItem("Unpack Prefab")) {
			PrefabManager::Unpack(handle);
		}
		ImGui::SetItemTooltip("Prefab とのつながりを切る（中身もシーンファイルに書かれるようになる。Prefab を直しても変わらなくなる）");
	}
	ImGui::Separator();
```

### D-3 `Editor/Windows/InspectorWindow.cpp`（Prefab の情報とボタン）

include に2つ足す。

```cpp
#include "HTTEngine/Components/Core/FolderComponent.h"
#include "HTTEngine/Components/Core/PrefabInstanceComponent.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Scene/PrefabManager.h"
#include "HTTEngine/String/ConvertString.h"
```

namespace の頭に足す。

```cpp
namespace {
const ImVec4 kPrefabWarningColor = ImVec4(1.0f, 0.55f, 0.4f, 1.0f); // Prefab と違う・読めないときの文字の色

/// <summary>
/// 置いた物が Prefab と違うか、の覚え（調べるたびに中身を全部 JSON に書くので、毎フレームはしない。履歴が動いたか、選ぶ物が変わったときだけ調べ直す）
/// </summary>
struct PrefabModifiedCache {
	EntityId rootId = 0;      // 調べた置いた物
	uint64_t changeCount = 0; // 調べたときの EditorHistory::GetChangeCount
	bool isValid = false;     // false なら、次は必ず調べ直す（Apply した直後など）
	bool isModified = false;  // 調べた結果
};
PrefabModifiedCache prefabModifiedCache;
```

`DrawComponentSeparator` の次に足す。

```cpp
// 置いた物が Prefab と違うか（覚えた結果が使えれば、それを返す）
bool IsPrefabModified(Handle<Entity> root) {
	const EntityId rootId = EntityManager::Get(root)->id;
	const uint64_t changeCount = EditorHistory::GetChangeCount();
	PrefabModifiedCache& cache = prefabModifiedCache;
	if (!cache.isValid || cache.rootId != rootId || cache.changeCount != changeCount) {
		cache = {rootId, changeCount, true, PrefabManager::IsModified(root)};
	}
	return cache.isModified;
}

// Prefab から置いた物なら、元の Prefab・違うか・ボタン（Apply・Unpack）を出す
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
	// --- Prefab と違うか（シーンに書くのは参照だけなので、違う所は保存されない）---
	if (IsPrefabModified(root)) {
		ImGui::TextColored(kPrefabWarningColor, "Prefab と違う所があります。このままシーンを保存すると、Prefab のとおりに戻ります");
	} else {
		ImGui::TextDisabled("Prefab と同じ（直したいときは Prefab を直すか、ここで直して Apply）");
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
		prefabModifiedCache.isValid = false; // ファイルが変わったので調べ直す
	}
	ImGui::SetItemTooltip("シーンで直した中身を、元の Prefab のファイルに書く（ほかのシーンの物も、次に開いたときから同じになる）");
	ImGui::SameLine();
	if (ImGui::SmallButton("Unpack")) {
		PrefabManager::Unpack(root);
	}
	ImGui::SetItemTooltip("Prefab とのつながりを切る（中身もシーンファイルに書かれるようになる。Prefab を直しても変わらなくなる）");
}
```

`DrawHeader` の `Parent:` の後で呼ぶ。

```cpp
	} else {
		ImGui::TextDisabled("Parent: (root)");
	}
	DrawPrefabInfo(handle);
	DrawComponentSeparator();
```

> Inspector の値をドラッグしている間は、まだ履歴に積まれていない（マウスを離すと積まれる）ので、「違う」の表示はマウスを離してから変わる。

### D-4 `Editor/Windows/ProjectWindowActions.cpp`（説明だけ）

Place in Scene の説明（コメントと Tooltip）を今の動きに合わせる。

```cpp
		// Prefab はシーンに置ける（原点に。次のフレームの頭にできる。Undo には入らないので、要らなければ Hierarchy で消す）
		// 置いた物は Prefab とつながっていて、シーンファイルには参照だけが書かれる
```

```cpp
		ImGui::SetItemTooltip("原点に置く。直したら Inspector の Apply で、Prefab に書ける");
```

## パートE おまけ：FixedString が日本語の途中で切れる

`String/FixedString.h` の `Assign`。UTF-8 の続きのバイト（`10xxxxxx`）かを調べるのに、上の2ビット（`0xC0` = `11000000`）を取り出すはずが、`0xC00` になっている。`uint8_t` の値（0〜255）と `0xC00`（3072）は重なるビットが無いので、いつも 0 になり、入りきらないときに日本語の1文字の途中で切れて化ける。

```cpp
		while (length > 0 && length < text.size() && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80) {
```

## パートF Save As Prefab で、ほかの Prefab を上書きしない

Save As Prefab は名前で書き先（`resources/prefabs/<名前>.prefab`）を決めるので、Prefab と関係ない物でも、名前が同じだと黙って上書きしていた。チームだと、Enemy 担当が仮に作った `Player` で、Player 担当の `Player.prefab` を消してしまう。

そこで、**ファイルがもうあって、その Prefab から置いた物でもない**ときは、メニューを押せなくする。

| 押した物 | `resources/prefabs/<名前>.prefab` | どうなるか |
|---|---|---|
| どこともつながっていない物 | 無い | 新しく作る（今までどおり） |
| どこともつながっていない物 | ある | **押せない**（Tooltip で理由を出す） |
| その Prefab から置いた物 | ある | 書ける（Apply と同じ） |
| 別の Prefab から置いた物（名前を変えた など） | ある | **押せない** |
| 別の Prefab から置いた物 | 無い | 新しく作り、つながりも新しい方へ移る（今までどおり） |

> わざと置き換えたいとき（作り直した物で、古い Prefab と替えたい）は、Project ウィンドウで古い Prefab を消してから押す。間違えて押す1回で消えないように、手を1つ増やしている。

### F-1 `Scene/PrefabManager.h`

`Save` の次に足す。

```cpp
	static bool Save(Handle<Entity> root, const std::string& path);
	// path に Prefab のファイルがもうあって、root がその Prefab から置いた物ではないか（Save As Prefab で、ほかの Prefab を上書きしないように調べる）
	static bool IsOtherPrefabFile(Handle<Entity> root, const std::string& path);
```

### F-2 `Scene/PrefabManager.cpp`

`Save` の関数の終わり（`return true;` の後の `}`）の次に足す。

```cpp
bool PrefabManager::IsOtherPrefabFile(Handle<Entity> root, const std::string& path) {
	std::error_code error; // 例外ではなくエラーコードで受ける
	if (!std::filesystem::exists(ToPath(path), error)) {
		return false; // まだ無いファイル（新しく作る）
	}
	// 同じファイルから置いた物なら、書いても Apply と同じ（自分の Prefab を書き直すだけ）
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	return (link == nullptr) || (ToPathKey(ToPath(link->path.ToString())) != ToPathKey(ToPath(path)));
}
```

> パスは `ToPathKey` でそろえてから比べる（`./resources/...` と `resources/...` を同じファイルと見る）。印のパスは読み込んだときの形、`MakePrefabFilePath` は作った形なので、文字のまま比べると違うことがある。

### F-3 `Editor/Windows/HierarchyWindow.cpp`（`DrawEntityMenu`）

Save As Prefab の所を書き換える。

```cpp
	// --- Prefab にする（resources/prefabs/<名前>.prefab。前の中身は .bak に残る）---
	// ファイルの名前になるので、名前が英数字と _ - だけのときに押せる
	// 同じ名前の別の Prefab がもうあるときは押せない（上書きすると、それを置いたシーンがみな変わる。チームでほかの人の Prefab を書き換えないように）
	const Entity* entity = EntityManager::Get(handle);
	const bool isValidName = (entity != nullptr) && IsValidFileName(entity->name);
	const bool isOtherPrefab = isValidName && PrefabManager::IsOtherPrefabFile(handle, MakePrefabFilePath(entity->name));
	if (ImGui::MenuItem("Save As Prefab", nullptr, false, isValidName && !isOtherPrefab)) {
		PrefabManager::Save(handle, MakePrefabFilePath(entity->name));
	}
	if (!isValidName) {
		ImGui::SetItemTooltip("名前を英数字と _ - だけにすると保存できる（ファイルの名前になるので）");
	} else if (isOtherPrefab) {
		ImGui::SetItemTooltip("同じ名前の Prefab がもうある（上書きすると、それを置いたシーンがみな変わる）。その Prefab を直すなら、置いて Apply。別の Prefab にするなら名前を変える");
	} else {
		ImGui::SetItemTooltip("子も一緒に resources/prefabs/ に保存する。これからは Prefab から置いた物になる");
	}
```

> `IsOtherPrefabFile` はファイルがあるかを見に行くが、右クリックのメニューを開いている間だけなので重くない。
>
> UI Screen の保存（`UIScreenEditor` の Done）と Apply は `PrefabManager::Save` を直接呼ぶので、この止め方は効かない（どちらも、つながっているファイルにしか書かない）。

## 2章 チームでの使い方（ゲーム側の作業）

### 2.1 チームのリポジトリを作るとき（.gitignore）

個人のゲーム（AL4_01）ではやらなくてよい。1人なら、下のファイルが git に入っていてもぶつからない。

エディタは、保存するたびに次のファイルを書く。どれも人ごとに中身が違うので、git に入れると2人が保存するたびにぶつかる。

| ファイル | 何か |
|---|---|
| `*.bak` | 保存のたびに、1つ前の中身を残す物（`SceneSerializer::WriteFile`） |
| `*.broken` | 読めなかったシーンの写し |
| `project/Parameters/Editor/` | エディタだけの設定（最後に開いたシーン・Scene ビューのカメラ） |

git が見ないファイルは、リポジトリの `.gitignore` で決まる。プロパティシート（`HTTEngine.props`）はビルドの設定なので、git には効かない。チームのリポジトリを作るときに、いつもの Visual Studio 用の `.gitignore`（ビルドの出力・`.vs/` を外す物）の最後に足す。

```gitignore
# エディタが書く、人ごとに違うファイル（git に入れると、保存するたびにぶつかる）
*.bak
*.broken
/project/Parameters/Editor/
```

> 最初のコミットより前に足すこと。もう git に入ってしまった物は、`.gitignore` に書いても見られ続ける（そのときは `git rm -r --cached <ファイル>` で外す。ファイルは消えない）。

### 2.2 分担の決まり（おすすめ）

**持ち主の表**。ファイルごとに「書いてよい人」を1人決める。ほかの人は読むだけ（置く・使うのはよい）。

| 担当 | 持ち主になるファイル | 作業するシーン |
|---|---|---|
| Player | `Player*.prefab`・Player の Script・Player の設定 | `PlayerTest` |
| Enemy | `Enemy*.prefab`・敵の Script・攻撃表（`.attacktable`） | `EnemyTest`（`Player.prefab` を置いてよい） |
| ステージ | 本番シーン（`GameScene`）・`Stage*.prefab`（地形・ギミック）・ステージのモデル | `GameScene`・`StageTest` |
| UI | `.uiscreen`・UI の Script・UI の画像とフォント・UI 用の `.timeline`／`.curve` | `UITest` |

- **本番シーンはステージ担当が持つ**：本番シーンは「何をどこに置くか」だけの短いファイルになる。Player の出る場所・敵を置く場所はステージの作りで決まるので、ステージ担当がまとめて置く。Player 担当・Enemy 担当は自分の Prefab を直すだけで、本番シーンは開き直すと新しくなる。
- **ステージが大きいとき**：エリアごとに Prefab（`Stage_Area1.prefab` など）に分けて、本番シーンに置く。2人でステージを作るなら、エリアごとに持ち主を分ける。
- **UI と Player の受け渡し**：UI が Player の体力などを出すなら、最初に「どの Component のどの値を見るか」を2人で決める（名前と型だけ）。決まれば、UI 担当はその Component を付けた仮の Entity で `UITest` を作れて、Player の完成を待たなくてよい。

**ほかの人の Prefab を、自分のテストシーンに置く**（例：Enemy 担当が `EnemyTest` に `Player.prefab` を置く）。置いてよい。シーンファイルには「`Player.prefab` をどこに置いたか」しか書かれないので、ぶつからない。

| | どうなるか |
|---|---|
| Player 担当が `Player.prefab` を直して push | Enemy 担当が pull して `EnemyTest` を開き直すと、新しい Player になる |
| Enemy 担当が `EnemyTest` で Player を動かす・置き直す | 一番上の Transform だけ `EnemyTest` に保存される（Player 担当には関係ない） |
| Enemy 担当が試しに Player の速さなどを変える | 保存されない（開き直すと戻る）。Inspector に「Prefab と違う所があります」と出るが、気にしなくてよい |
| Enemy 担当が Player で **Apply** か **Save As Prefab** を押す | `Player.prefab` が書き換わり、Player 担当とぶつかる。**押さない** |

> 自分で作った仮の Entity に `Player` と名前を付けて Save As Prefab を押そうとしても、`Player.prefab` がもうあるので押せない（パートF）。止められるのは「つながっていない物で上書きする」ときだけで、置いた Player からの Apply・Save As Prefab は止まらない（Player 担当がふだん直すのと同じ操作なので）。そこは決まりで守る。
>
> Player の Script がほかの物（カメラ・地面など）を探して使うなら、`EnemyTest` にもそれを置く（`Stage*.prefab` を置くのが早い）。

**そのほかの決まり**

| 決まり | 理由 |
|---|---|
| 同じ Prefab を2人で同時に直さない | Prefab のファイルは1つなので、同時に直すとぶつかる。持ち主以外が直したいときは、持ち主に頼む |
| pull したら、開いているシーンを開き直す | 書き換わった Prefab は、シーンを読むときに読み直す |
| 共有の設定（`Parameters/` の Physics・Audio など）は持ち主を1人に | 同じファイルに2人が1項目ずつ足すと、ぶつかる |
| Script は担当ごとのフォルダ（`Source/Scripts/Player/`・`Source/Scripts/Enemy/`） | 同じ .cpp を触らない。vcxproj の行はぶつかっても、両方残せば直る |
| ビルドが通る状態でだけ push する | C++ のコードは全員で1つの exe になる。誰か1人のビルドが通らないと、全員のテストシーンが動かない |

## 3章 確認（エディタで）

1. 空のシーンに Entity を3つ（`Rig` と、その子 `A`・`B`）作る。`B` に Camera Look At を付け、Target に `A` をドラッグ。
2. `Rig` を右クリック → Save As Prefab。`Rig`・`A`・`B` の名前が青くなる。Inspector に `Prefab: resources/prefabs/Rig.prefab` と「Prefab と同じ」。
3. ほかに Entity `Watcher` を作り、Camera Look At の Target に `B` を入れる。
4. シーンを保存し、シーンファイルを開く：`Rig` の行に `"prefab"` があり、`A`・`B` の行が無い。
5. シーンを開き直す：`A`・`B` ができ、Inspector の `ID:` が前と同じ。`Watcher` の Target は `B` のまま。
6. `B` を動かす → Inspector に「Prefab と違う所があります」（マウスを離した後）。保存すると Console に警告。開き直すと元の位置。
7. もう一度 `B` を動かし、Apply → 「Prefab と同じ」。`Rig.prefab` の `id` は前と同じ数のまま（git の diff で、変わったのは `translation` だけ）。
8. `A` を一番上へドラッグ → 動かず、Console に「外へは出せません」。
9. Project の `Rig.prefab` → Place in Scene をもう1回 → 2つ目の `Rig`。2つ目の `B` の Target は2つ目の `A`。
10. `Rig` を Ctrl+D → 複製も青い。保存して開き直しても数が増えない。
11. Play → `A` を動かす → Stop：`A` は元の位置、Hierarchy の青も残る。
12. `Rig` を Unpack → 白に戻る。保存すると `A`・`B` の行もシーンファイルに出る。
13. 前からあるシーン（`CameraTimelineTest` など）を開いて保存 → 今までどおり（`"version": 2` になる以外は、番号も中身も同じ）。
14. （パートF）新しい Entity を作って名前を `Rig` にし、右クリック → Save As Prefab が灰色で押せない。マウスを乗せると「同じ名前の Prefab がもうある」。置いた `Rig` の右クリックでは押せる。

## 4章 写しで確かめた結果

本体と同じコード（develop `44a36b3` から）を一時フォルダの写しに入れ、AL4_01 のゲームとして動かした（確かめるコードは写しの `main.cpp` にだけ置いた）。

- **ビルド**：全部入れた後は Debug・Development・Release で警告 0（DirectXTex の前からある C4865 を除く）。パートA だけの所も Debug でビルドできた。パートB だけ・C までの途中は確かめていない。
- **動き**（62 項目全部 OK）：
  - 番号がでたらめな大きい数になる。
  - Save As Prefab：今の番号のまま書く・一番上の印は書かない・保存した物に印が付く・中身が分かる。
  - シーンの保存：置いた物は `"prefab"` と Transform だけ、中身の行は無い。版 2。
  - 読み直し：同じ番号で中身ができる。Prefab の中の参照（B→A）、シーンから中身への参照（Watcher→B）が切れない。
  - 中身を動かすと「違う」、一番上の位置・名前だけなら「同じ」。Apply してもファイルの番号は同じ。
  - 2つ目を置く：別の番号、2つ目の B は 2つ目の A を見る、ずれは中の全員で同じ。
  - 足した子は中身でない・保存して読み直すと戻る。
  - 中身を外へ出せない・中身の中なら動かせる・Undo で戻る。
  - 貼り付け：貼った物も中身が分かる。保存・読み直しで数が増えない（11 個のまま）。
  - Play の退避：中身の直しも戻る・印の番号が引ける・中身が分かる。
  - 外でファイルを書き換える（git pull のつもり）→ 読み直すと新しい中身、番号は同じ。
  - Unpack → 保存・読み直しで数が同じ。
  - 無い Prefab：印だけの Entity になり、保存しても参照が消えない。
  - 版 1 のシーン（`CameraTimelineTest`）が読めて、保存・読み直しで数が同じ。
- **パートF**（後から足した。Debug で 4 項目 OK、全部で 66 項目）：つながっている Prefab なら書ける・`./` 付きのパスでも同じファイルと分かる・つながっていない物は止める・まだ無いファイルは書ける。メニューが灰色になる所は、本物のマウスでは見ていない。
- **ログ**：エラー 0。警告は確かめるためにわざと起こした物だけ（外へ出す・違うまま保存・無い Prefab）。
- **確かめていない**：本物のマウスでの操作（Hierarchy の青・Inspector のボタン・ドラッグ）、UI Screen の編集（`.uiscreen` の番号が保存のたびに同じになるか）、入れ子の Prefab、2人で実際にマージすること。

## 5章 まだやっていないこと（必要になったら）

| 物 | 今どうなるか | やるなら |
|---|---|---|
| 置いた所ごとの上書き（Unity の Override） | → `PrefabOverride.md` で入れる（違う項目だけシーンに書く） | — |
| Revert（Prefab のとおりに戻すボタン） | → `PrefabOverride.md` で入れる（Ctrl+Z で戻せる） | — |
| 入れ子の Prefab の更新 | → `PrefabNested.md` で入れる（外側の Prefab にも参照で書く） | — |
| 貼り付けた物の中の参照 | → `PrefabOverride.md` で直す（作ってから JSON で番号を直す） | — |
| シーンを分けて重ねて読む（サブシーン） | 無い | ステージ担当が別にいて、配置の作業がぶつかるようになったら |
