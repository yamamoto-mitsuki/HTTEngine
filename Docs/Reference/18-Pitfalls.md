> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 18. だめな例まとめ

| だめな書き方 | 何が起きるか | 正しくは |
|---|---|---|
| `Entity*` や `TransformComponent*` をメンバに持つ | 配列が詰め直されて別の物を指す | `Handle<Entity>` で持ち、使うたびに `Get` する（Component の中なら `EntityRef`） |
| Componentに `std::string` / `std::vector` / ポインタ / 仮想関数 | Undo・コピー・保存が壊れる（`std::string`・`std::vector`・仮想関数は `static_assert` で止まる。ポインタは通ってしまうので気づきにくい） | 固定長の値だけにする（文字は `FixedString<N>`・一覧は `FixedList<T, N>`・相手は `EntityRef`） |
| `ForEach<TransformComponent>` の中（最初の引数が `TransformComponent&` の SYSTEM も同じ）で `EntityManager::Create`・`GameObject::Instantiate` する | assertで止まる（Transform は作った直後から付く。予約ではない） | Transform 以外の型を先頭にして回す。予約（`RequestAdd` / `RequestRemove` / `Destroy`）は `ForEach` の中でも安全（[4章](04-Component.md#foreach-の最中にその型を増減させてよいか)） |
| `RequestAdd` の直後に `Get` | `nullptr` が返る | 次のフレームで取る。初期値は `RequestAdd` に渡す |
| 敵ごとの状態をグローバル変数に置く | 2体目で破綻する | Componentに入れる（Entityごとに1個ある） |
| `rotation.y += 90.0f` のように、`rotation` の数字に角度を書く・足す | `rotation` はクォータニオンなので、とんでもない回転・大きさになる | 角度（度）なら `SetEulerAngles`・`Rotate`（[7章](07-Transform.md)） |
| `worldMatrix` に自分で書き込む | 次のフレームに上書きされる | `translation` / `rotation` / `scale` を変える |
| `deltaTime` を掛けずに動かす | フレームレートで速度が変わる。ポーズで止まらない | `* Time::GetDeltaTime()` |
| `Update` で `Renderer::Draw***` を呼ぶ | 停止中に消える・順番が崩れる | `Draw()` の中で積む |
| 毎フレーム `ModelManager::Load` / `SoundManager::Load` | 無駄に遅くなる | `Initialize` で1回 |
| Componentのファイルを静的ライブラリに入れる | リンカに捨てられて登録が消える | ゲーム（exe）のプロジェクトに入れる |
| シーンファイルを使うシーンの `Initialize` で Entity を作る | ファイルから読んだ物と二重になる（Play・Stop のたびに増える） | 最初の配置は `CreateDefaultEntities` に書く。後はエディタで置いて Save |
| `ui.Field` のラベルを変える | 前に保存した値が読めず、初期値に戻る | `"新しい表示###前のラベル"` と書く |
| 残したい値を `ui.Field` に書き忘れる | 保存されず、Stop・再起動で初期値に戻る | Stop して値が戻ったら書き忘れ。`ui.Field` に足す |
| 見せ方の関数（`COMPONENT(...) { }` の中）に処理を書く | 保存・読み込みのときにも実行される | 処理は `SYSTEM` に書く |
| Componentに `Handle<Entity>` で相手を覚える | Play / Stop・削除の Undo・再起動のたびに無効になる（`ui.Field` にも渡せない） | `EntityRef` で覚える |
| `CreateDefaultEntities` で作った Entity の Handle をメンバに覚える | Play の後・2回目の起動では無効（その関数が呼ばれない） | `Initialize` の中で `FindByName` などで探す |
| System同士を直接呼ぶ | 呼ぶ順番に縛られる | 片方が書いて片方が読む（タグComponentか全体の状態を経由） |
| 当たりを `hit.phase` を見ずに数える | 重なっている間（`Stay`）も毎フレーム数える | 1回だけなら `HitPhase::Enter` のときだけ数える |
| `GetHits` の結果（`std::span`）をメンバに覚えておく | 次のフレームで中身が入れ替わり、別の当たりを指す | その場で読む。相手を覚えるなら `EntityRef` で |
| `RenderTexture` を作り直すクラスで、SRVスロットを自分で `AllocateSRVSlot` したまま返さない | 作り直すたびに減り、256個で落ちる | もらったら、持ち主のデストラクタで `DirectXCommon::FreeSRVSlot` |
| 消す Entity の音を `AudioSystem::Play` で鳴らす | 消した瞬間に音が止まる | `AudioSystem::PlayOneShot` |
| コードで鳴らす Audio Source に Play On Start を付けたまま | Play を押した瞬間にも鳴る | Play On Start の印を外す |
| 古いパス（`HTTEngine/Entity/`・`Component/`・`Light/`）をインクルードする | C1083（ファイルが無い） | [1章の「ファイルの置き場所」](01-Overview.md#ファイルの置き場所)の表のパス |
| アセットのファイル名に日本語を使う | 読めない（Inspector の一覧に出ない・Console に警告。前は選ぶと止まった） | 英数字にする |
| パスを文字にするのに `path.string()` / `generic_string()` を使う | 日本語の Windows では Shift-JIS になる（ImGui で文字化け・UTF-8 のつもりの所で読めない）。C++26 で非推奨 | `ToUtf8(path)`（`String/ConvertString.h`。UTF-8・区切りは `/`） |
| `GlobalVariables` の `SettingsSection` を、そのグループを最初に作る `Group(...)` 以外に渡す | 効かない（見出しは最初の `Group` で決まる）。Editor のつもりが Project の下に出て、`Parameters/<グループ>/` に保存される | 最初に呼ぶ `Group("名前", SettingsSection::Editor)` に渡す |
| 物を掴む・乗せるのに `SetParent` を使う | 値がそのまま新しい親から見た位置になり、親の分だけ飛ぶ | `SetParentKeepWorld` |
| シーンの `GetCamera()` を上書きしたまま Camera Component を置く | 上書きしたカメラが使われ、置いた Camera が効かない | `GetCamera()` を上書きしない（カメラは Entity） |
| 直接鳴らす BGM を `SoundManager::Play(bgm, true)` で鳴らす | SE の音量つまみを通る（BGM の音量を下げても小さくならない） | 最後に `AudioBus::BGM` を渡す。Audio Source なら Bus を BGM に |
| Game ビューのクリック位置を `GetMousePositionInWindow` で取る | エディタではウィンドウ全体での位置（Game ビューの中ではない） | `GetMousePositionInGame()`（スプライトと同じ座標） |
| magic_enum で回す enum（Inspector のコンボ・保存・`enum_values` の for に使う物）の最後に、数える用の `Count` を足す | `Count` も「種類の1つ」として拾われる（コンボに出る・for で回る）。`SoundBus`（今の `AudioBus`）でこれをやり、配列の外を読んで起動できなくなった | `Count` は足さない。数は `magic_enum::enum_count<E>()` で取る |
| シーンの `Initialize` で `GlobalVariables::GetInstance()->LoadFiles()` を呼ぶ | Play・Stop のたびに、保存していない PostEffect・音量・Scene ビューのカメラがファイルの値に戻る | 呼ばない（起動のときにエンジンが読む） |
| 前の書き方（`std::make_unique<GameScene>()`）のまま、`GetSceneFile()` の上書きだけ消す | 名前が無いので、シーンファイルを使わないシーンになる（Entity が読まれない） | `main.cpp` を `Engine::Initialize(wc, "GameScene")` にして、`REGISTER_SCENE(GameScene)` を書く |
| シーンの名前（ファイル名）に日本語を使う | 読めない | 英数字にする（アセットと同じ） |
| スローに `Time::SetTimeScale` を使う | もう無い（前は毎フレームエンジンに 1 へ戻されて効かなかった） | `Time::SetGameTimeScale`。止めるのは `Time::HitStop` |
| 速く動く攻撃の当たりを `GetHits`（Collider の重なり）で見る | 1フレームで相手を飛び越えて、すり抜ける | `CollisionSystem::SweepSphere`（動いた跡で調べる） |
| 線の点の列を Component に `std::vector` で持つ | `static_assert` で止まる（Undo・保存が壊れる） | Trail Component を付けて、点は `TrailSystem::AddPoint` |
| `TrailSystem::GetPoints` の結果を持ったまま `AddPoint` する | 配列が動いて、持っていた方は壊れた所を指す | 要る点（最後の点など）を先に写してから `AddPoint` |
