参考：
- Unreal の Redirector と「Fix Up」（名前を変えたアセットを指しているファイルを、新しい名前に書き換える）：https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-redirectors-in-unreal-engine
- Unity の AssetDatabase.MoveAsset（エディタの中で動かすと、参照が外れない）：https://docs.unity3d.com/ScriptReference/AssetDatabase.MoveAsset.html
- Unity の AssetPostprocessor.OnPostprocessAllAssets（動いたアセットの前のパス・新しいパスを知らせる）：https://docs.unity3d.com/ScriptReference/AssetPostprocessor.OnPostprocessAllAssets.html
- std::unordered_map::extract（中身を作り直さずに表から外す。キーを書き換えて入れ直せる）：https://cpprefjp.github.io/reference/unordered_map/unordered_map/extract.html
- std::filesystem::recursive_directory_iterator::increment（例外ではなくエラーコードで受けて進める）：https://cpprefjp.github.io/reference/filesystem/recursive_directory_iterator/increment.html
- nlohmann::json の get_ref（中の文字そのものを指す参照）：https://json.nlohmann.me/api/basic_json/get_ref/

# AL4_01 で見つけた不具合3つの直し（名前を変えた後の保存先・Just Margin 0・ヒットストップのアニメ）

2026-11-07。ゲーム（AL4_01）で見つかったエンジンの不具合3つを直した記録。**本体に直接入れた**ので、写経はしなくてよい（読むための md）。
コードの差分は、この md と同じコミットにある。

| | 不具合 | 原因 | 直し |
|---|---|---|---|
| 1 | 攻撃表の名前を変えた後、編集が前の名前のファイルに保存される | 名前を変えても、Inspector・Manager が前のパスを覚えたまま | 名前を変えたことを、パスで覚えている所全部に知らせる。ほかのファイルの中のパスも書き換える |
| 2 | Just Margin が 0 だとジャスト判定が出ない | 「0 ならジャスト無し」を、Margin の値で兼ねていた | ジャストを使うかを `Just Enabled` に分ける。Margin は広げる量だけ |
| 3 | ヒットストップが入ると、ループしないアニメが最初の形で止まる | 0 秒進めたときにも「0 以下で止める」が成り立つ | 進む向きの終わりに着いたときだけ止める |


---

## 1章 名前を変えた・動かしたファイルの保存先

### 1.1 何が起きていたか

コードの流れを追うと、こうなっていた。

1. Project ウィンドウで `NewAttackTable.attacktable` を選ぶ → `AssetInspector::Select(パス)` が、Inspector に出すファイルとしてパスを覚える
2. 名前を `Boss.attacktable` に変える → `ProjectWindow::ApplyRename` は、ディスクの名前と、Project ウィンドウの `selected_` だけを変えていた
3. Inspector は毎フレーム `AttackTableManager::Load(前のパス)` を呼ぶ。Manager は前のパスをキーに覚えているので、同じ番号の表が返る（ファイルが無くても、読み込み済みなので気づかない）
4. 値を直す → `AttackTableManager::Save` → `WriteFile(asset->path)` が**前のパス**に書く → `NewAttackTable.attacktable` ができる
5. もう一度保存すると、そのファイルが `NewAttackTable.attacktable.bak` に写る（残っていた .bak の時刻・中身と合う）

シーンの Attack Table の Table の欄は、Component が**番号**で持っているので、名前を変えた直後は外れていないように見える。けれど、シーンを保存すると `GetAssetPath(番号)` ＝ **前のパス**が書かれる。次に開いたときは、前の名前のファイルを読みに行く（4 で作り直されていれば、そちらの古い表）。

同じ形でパスを覚えている所は、ほかにもあった：

| 種類 | 覚えている所 | 起きること |
|---|---|---|
| 書いて保存する物 | AttackTable・Curve・Material・PhysicMaterial・Timeline の Manager、Inspector | 前の名前のファイルを作り直す |
| 読むだけの物 | Model・Texture・Sound・Font・Animation の Manager、Shader | シーンを保存すると前のパスが書かれて、次に開くと外れる |
| Prefab | PrefabManager（パスと、中身の JSON）、置いた物の印（`PrefabInstanceComponent::path`） | 置いた物が前のパスの Prefab を指す。中身に書いたアセットのパスも古いまま |
| エディタの窓 | UI 画面の編集（`edit.filePath`）、Code ウィンドウのタブ | Done で前の名前の画面を作り直す・タブが読み直せない |
| ほかのファイル | 開いていないシーン・Prefab・.material など | 前のパスのまま（開くと外れる） |

### 1.2 どう直したか（責務の分け方）

Unreal の「Fix Up」と同じ考えで、**名前を変える係**と、**パスを覚えている係**を分けた。

```
Project ウィンドウ（ApplyRename）
   │ AssetMover::Move(前, 新)               ← Editor/Assets。エディタの中で名前を変えるときの入口
   ├─ 1. ディスクの名前を変える（.bak も一緒に）
   ├─ 2. AssetMoveRegistry::Notify(MovedPath) ← Core。パスで覚えている所へ知らせる
   │      ├─ 各 Manager（Initialize で AddListener した物）：覚えているパスを付け替える。番号はそのまま
   │      └─ エディタの窓（Inspector・UI 画面の編集・Code ウィンドウ）：覚えているパスを付け替える
   ├─ 3. resources/ の中の、パスを書く種類のファイル（AddReferenceFileType した物）を書き換える
   └─ 4. Source/ のコードに前のパスがあれば、警告だけ出す
```

- **番号はそのまま**：Component は番号で持っているので、開いているシーンの欄は外れない。保存すると、Manager が付け替えた新しいパスが書かれる。
- **知らせを受ける所は、自分で登録する**：Manager は自分の Initialize で `AssetMoveRegistry::AddListener` を呼ぶ。新しい Manager を足したときも、その Manager の中だけで済む（AssetMover に一覧を書き足さない）。
- **パスを書くファイルの種類も、持ち主が登録する**：`AddReferenceFileType(".prefab")` のように。シーン（`.scene.json`）は SceneManager、UI 画面（`.uiscreen`）は UIScreen、ほかは各 Manager。
- **コードは書き換えない**：文字を組み立てているかもしれないので、行を警告に出すだけ（`コードに前のパスが書いてあります（コードは書き換えていません）: Source/〜.cpp:12`）。

### 1.3 足した物・変えた物

| ファイル | 何をしたか |
|---|---|
| `Core/AssetMoveRegistry.h/.cpp`（新） | `MovedPath`（前のパス・新しいパス。`Apply`・`ApplyToKeys`）と、知らせの表 `AssetMoveRegistry` |
| `Editor/Assets/AssetMover.h/.cpp`（新） | 名前を変える入口。ディスク → 知らせ → ファイルの中 → コードの警告 |
| `Scene/SceneSerializer` | `RemapPaths(json, moved)`：JSON の中の文字の値で、動いた物のパスを書き換える（項目の名前は見ない） |
| `String/ConvertString` | `ToLowerAscii`（大文字・小文字を区別せずに比べる。2か所で使うので1つにした） |
| `Editor/Assets/ProjectFiles` | `GetResourceFolder`・`GetSourceFolder` |
| `Editor/Windows/ProjectWindowActions.cpp` | `ApplyRename` を `AssetMover::Move` に置き換え。右に出しているフォルダの中の名前を変えたときも、`currentFolder_` を付け替える |
| `Editor/Inspector/AssetInspector` | `Initialize` を足して、選んでいる・まだ保存していないファイルのパスを付け替える（今回の不具合の直接の原因） |
| AttackTable・Curve・Material・PhysicMaterial・Timeline・Prefab の Manager | パスの付け替えと、ファイルの種類の登録。攻撃表は部品の値（JSON の文字）の中、Timeline は知らないトラック（JSON の文字）の中、Prefab は中身の JSON と、置いた物の印も |
| Model・Texture・Sound・Font・Animation・ShaderPackageLoader | パスで引く表のキーの付け替え（`ApplyToKeys`）。Model はマテリアルのテクスチャのパスも |
| `SceneManager.cpp`・`UIScreen.cpp` | `.scene.json`・`.uiscreen` を、パスを書くファイルの種類として登録 |
| `UIScreenEditor.cpp`・`CodeWindow.cpp` | 編集中の画面・開いているタブのパスを付け替える |
| `EngineSystems.cpp` | `AssetInspector::Initialize()` を呼ぶ（エディタだけ） |

### 1.4 比べ方の決まり（MovedPath::Apply）

- パスは `ToPathKey` の形（区切りは `/`）にそろえてから比べる。`\` 区切り・`./` が付いた物も同じ物として扱う。
- **大文字・小文字は区別しない**。Windows では同じファイルなので（エンジンの中にも `"resources/"` と書く所と、ディスクの `Resources/` がある）。
- そのもの、または**フォルダの中の物**だけを動かす。`resources/Enemy` を動かしても `resources/EnemyBoss/〜` は動かない（後ろが `/` か終わりかを見る）。
- 書き換えた後のパスは、新しいパスの書き方になる（`to` ＋ 残り）。

### 1.5 あまり使わない STL・ライブラリの解説

**`std::unordered_map::extract`**（`MovedPath::ApplyToKeys`）

キーで引く表のキーは `const` なので、そのままでは書き換えられない。消して入れ直すと、中身（テクスチャの GPU の資源など）を作り直すか、動かす必要がある。`extract` は、中身を壊さずに表から1つ外して「ノード」として返す。ノードの `key()` は書き換えてよいので、書き換えて `insert` で戻す。

```cpp
auto node = map.extract(key); // 表から外す（中身はそのまま）
Apply(node.key());            // キーを新しいパスにする
auto result = map.insert(std::move(node));
if (!result.inserted) {       // 新しいパスの物がもう表にあった
	result.node.key() = key;  // 前のキーのまま戻す（外した物を捨てると、番号が指す中身が無くなる）
	map.insert(std::move(result.node));
}
```

回している途中で表に足し引きすると回し方が壊れるので、付け替えるキーを先に集めてから1つずつ行う。

**`recursive_directory_iterator::increment(error)`**（`AssetMover.cpp`）

`for (const auto& entry : recursive_directory_iterator(folder))` で回すと、読めないフォルダがあったときに例外が飛ぶ。`increment(error)` で進めると、例外ではなくエラーコードで受けられる。

**`json.get_ref<std::string&>()`**（`SceneSerializer::RemapPaths`）

`get<std::string>()` は写しを返す。`get_ref` は JSON の中の文字そのものを指す参照を返すので、写さずにその場で書き換えられる。

### 1.6 決めたこと

- **.bak も一緒に動かす**：保存したときに残る1つ前のファイル。前の名前のまま残ると、どのファイルの物か分からなくなる（今回、`NewAttackTable.attacktable.bak` が残っていた）。新しい名前の .bak が前からあれば上書きする（無いファイルの1つ前なので要らない）。
- **書き換えたファイルも .bak を残す**：`SceneSerializer::WriteFile` で書くので、1つ前に戻せる。書き方も同じなので、git の差分には変えた行だけが出る。
- **書き換えるのは resources/ の中だけ**：Parameters/ の JSON は今はアセットのパスを書いていない（シーンは名前で持つ）。書くようになったら、そこも足す。
- **外のツールの形のファイルは書き換えない**：.gltf の中の .bin・テクスチャの名前、.mtl の中のテクスチャの名前は、Blender などが書く物なので触らない（読み直すと、前の名前を探す）。

### 1.7 まだ外れる所（今回は入れていない）

- **エクスプローラー・git で名前を変えたとき**：エディタに知らせが来ないので、外れる。名前はエディタの Project ウィンドウで変える。これも外れないようにするには、Unity のように .meta に番号（GUID）を置いて、番号で参照する形に作り替える（別の大きな作業）。
- **Undo の履歴の中**：名前を変える前に消した「Prefab から置いた物」を Undo で戻すと、印のパスが前のパスのまま戻る（履歴は Component を型のまま写しているので、中のパスを見られない）。
- **Prefab の印のパスの長さ**：`PrefabInstanceComponent::path` は 255 バイトまで。新しいパスがそれより長いと切れる（前からある決まり）。


---

## 2章 Just Margin 0 でもジャスト判定を出す

### 2.1 決めたこと

| 項目 | 決めたこと |
|---|---|
| 切り替えの名前 | `justEnabled`（Inspector は `Just Enabled`）。Just の区画の一番上 |
| 初めの値 | `true`（今までの初めの値 Just Margin 0.5 も、ジャストを使う攻撃だった） |
| Just Margin の意味 | ジャストの範囲を、攻撃の形からどれだけ広げるか（m）だけ。**0 なら形そのものの中**で、タイミングが合えばジャスト |
| 前に 0 で保存した Hitbox | 項目 `justEnabled` が無いので、初めの値の「使う」で読む。読むたびに古い形か調べるコードをずっと残したくないので、読み替えはしない。0 で保存していた物は、エンジンとゲームを全部見て直した（2.3） |
| ギズモ | `Just Enabled` がオンで、Margin が 0 より大きいときだけ、ジャストの範囲の線（水色）を描く。0 のときは形の線と重なるので描かない（形の線がそのままジャストの範囲） |
| オフのときの Margin・Lead Time | 出したまま・保存したまま（オンに戻したとき、前の値が残っているように） |

### 2.2 変えた所

- `Components/Combat/HitboxComponent.h`：`bool justEnabled = true;` を足し、`justMargin` のコメントを「0 なら形そのものの中だけ」に
- `Systems/Combat/HitSystem.cpp`：
  - `FindJustCandidate` の外す条件を `hitbox.justMargin <= 0.0f` → `!hitbox.justEnabled` に（`Overlaps(capsule, 0)` は形そのものとの重なりなので、0 でもそのまま測れる）
  - `DescribeHitbox` に `Just Enabled` の欄、`Just Margin` の Tip を直す
  - `DrawGizmos` の条件を `hitbox.justEnabled && hitbox.justMargin > 0.0f` に

### 2.3 見直した物

| 物 | 前 | 今 |
|---|---|---|
| Sandbox の `TestBullet.cpp`（弾） | `hitbox.justMargin = 0.0f; // 弾はジャスト判定に使わない` | `hitbox.justEnabled = false;` |
| Sandbox の `HitDamageTest`・`MaterialTelegraphTest` の `Multi Hit Zone`（多段ヒットの床） | `"justMargin": 0.0` | `"justEnabled": false` を足す（ずっと Active の床なので、ジャストにしない） |
| `Docs/Tasks/HitDamage.md` | 弾は「Just Margin は 0」・コードの写し | 「Just Enabled はオフ」・今のコードに合わせた。ジャスト範囲の式の下に追記 |
| `Docs/Tasks/MaterialTelegraph.md` | シーンの写しの `Multi Hit Zone` | `"justEnabled": false` を足した |
| AL4_01 | `PounceImpact.prefab`・GameScene の Tail Sweep は 0.5（前に 0 から直してあった） | そのまま（0 の物は無い） |


---

## 3章 ヒットストップでループしないアニメが止まる

### 3.1 何が起きていたか

`AnimationPlayer::Update` のループしないときの止め方：

```cpp
time_ += deltaTime * speed_;
if (time_ >= duration || time_ <= 0.0f) { // ← 0 秒のときに 0 秒進めても「0 以下」になる
	time_ = std::clamp(time_, 0.0f, duration);
	isPlaying_ = false;
}
```

1. 攻撃の段階に入ったフレームに `Play` → `Restart`（最初から）。流し始めたフレームは進めない（`isJustStarted`）
2. 同じフレームに当たって `Time::HitStop(0.08)` → 次のフレームから `Time::GetDeltaTime()` が 0
3. 次のフレームの `player.Update(0)` で、時刻 0 のまま `time_ <= 0.0f` が成り立って止まる
4. ヒットストップが明けても `isPlaying_` が false なので、回らない

一時停止（ポーズ）でも deltaTime が 0 なので、同じことが起きていた。

### 3.2 直し

`Graphics/Animation/AnimationPlayer.cpp`：進む向きを見て、終わりに着いたときだけ止める。

```cpp
const float step = deltaTime * speed_; // このフレームに進める秒（負なら戻る。一時停止・ヒットストップで deltaTime が 0 なら 0）
time_ += step;
...
// 進む向きの終わりに着いたときだけ止める（前に流すなら duration、逆に流すなら 0）
if (step > 0.0f && time_ >= duration) {
	time_ = duration;
	isPlaying_ = false;
} else if (step < 0.0f && time_ <= 0.0f) {
	time_ = 0.0f;
	isPlaying_ = false;
}
```

### 3.3 一緒に直した物：逆に流すアニメの流し始め

確かめる途中で見つかった。`Play` はいつも 0 秒から始めていたので、逆に流す（speed が負）ループしないアニメは、最初の Update で「0 以下」になって、流れずに止まっていた（「0 で止まる」は、0 から一歩も動かないので成り立っていただけ）。

- `AnimationPlayer::Play`：速さが負なら、終わり（duration）から始める
- `AnimationSystem.cpp` の `ResetPlayer`：`Play` の前に `SetSpeed(component.speed)` を呼ぶ（`Play` が速さの向きで始まりを決めるので）


---

## 4章 確かめたこと

写し（`J:\eng`、86df43b から）で直して、Debug・Development・Release の3つでビルド（自分の警告 0）。Sandbox に自動テスト（TEST ONLY。本体には入れていない）を置いて、エディタを起動したまま毎フレーム進めて 65 項目を確かめた（Debug・Development の両方で全部合格。Release は起動を確かめた）。

| 区画 | 確かめたこと |
|---|---|
| MovedPath・RemapPaths（12） | そのもの・`\` 区切り・大文字小文字・後ろが続く別の名前・フォルダの中・頭が同じ別のフォルダ・表のキーの付け替え（ぶつかったら前のまま）・JSON の値だけ書き換える |
| アニメ（16） | 0 秒で 0 秒進めても止まらない・終わりで止まる・逆は終わりから始まって 0 で止まる。Animator で、流し始めたフレームに `Time::HitStop(0.08)` → 止まっている4フレームも再生中 → 明けた後に進む。`Time::SetGamePaused` でも同じ |
| ジャスト（7） | Margin 0 で形の中ならジャスト・Just Enabled オフならならない・Margin 0 で形の外ならならない・Margin 0.6 なら同じ所でジャスト・項目の無い前のファイルは「使う」で読む・Sandbox の Multi Hit Zone はオフ |
| 名前の変更（30） | NewAttackTable.attacktable を作って直す → 表を使う敵・その Prefab・置いた物・開いていないシーンを用意 → Inspector で選ぶ → Boss.attacktable に変える：ディスク・.bak・Manager のパス・開いているシーンの Table の欄・開いていないシーンと Prefab のファイルの中・読み込み済みの Prefab の中身・コードの警告（後ろが続く別の名前には出ない）。何フレームか Inspector を描いても前の名前のファイルを作らない・前のパスで読みに行かない・保存は新しいファイルへ。続けてフォルダの名前を変える：中の表・Prefab・置いた物の印・開いていないシーンの中・開いているシーンを書いたときのパス |

テストが本当に不具合を見つけられるかも確かめた：Inspector の付け替えと、アニメの止め方を前の形に戻して同じテストを流すと、その2つの項目（11 個）が落ちた。

### 4.1 エディタで手で確かめる（AL4_01・Sandbox のどちらでも）

1. Project ウィンドウで Create → Attack Table、Inspector で値を少し直す
2. 名前を変えて、続けて Inspector で値を直す → Project ウィンドウに前の名前のファイルが出てこない。Console に「名前を変えました: 〜 → 〜（中のパスを書き換えたファイル: N 個）」
3. その表を使っている敵の Attack Table の Table の欄が、新しい名前になっている。シーンを開き直しても外れない
4. ボスのしっぽの薙ぎ払いにわざと当たる → ヒットストップが明けた後に、しっぽが回る

AL4_01 の `project/Resources/attackTable/NewAttackTable.attacktable.bak` は、前の不具合で残った物なので消してよい（GameScene はもう `Boss.attacktable` を指している）。
