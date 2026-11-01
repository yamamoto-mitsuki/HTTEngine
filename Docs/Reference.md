# MyEngine リファレンス（2026-10-02 時点）

**今のエンジンで「ゲームを作るとき」に使う物だけをまとめた。** 設計の理由は [ARCHITECTURE.md](ARCHITECTURE.md)、作業手順と写経コードは [Tasks/](Tasks/) にある。

各項目は「**できること → 良い例 → だめな例**」の順。だめな例は実際にやりがちな物だけを載せた。

| 探している物 | 行き先 |
|---|---|
| ゲームを起動する形 | [1. 全体の形](#1-全体の形) |
| **ファイルをどこに置くか** | [1. 全体の形](#ファイルの置き場所) |
| Entityを作る・消す | [3. Entity](#3-entity) |
| Componentを付ける・取る | [4. Component](#4-component) |
| 自分のComponentを書く | [5. ゲーム固有Component](#5-ゲーム固有componentcomponent--system) |
| **敵が複数いるときのフラグの持ち方** | [6. 状態をどこに置くか](#6-状態をどこに置くか) |
| 位置・回転・親子（**見た目を変えずに親を付け替える**） | [7. Transform](#7-transform) |
| モデルを出す | [8. 描画](#8-描画) |
| ライト | [9. ライト](#9-ライト) |
| **カメラを置く・切り替える・追いかける** | [10. カメラ](#10-カメラ) |
| キー・マウス・パッド（**Game ビューのマウス位置・移動の入力**） | [11. 入力](#11-入力) |
| 音（**AudioSource**・**BGM / SE の音量**・**3Dの音**・SoundManager） | [12. 音](#12-音) |
| 経過時間・ポーズ（**スロー・ヒットストップ**） | [13. 時間](#13-時間) |
| 乱数・当たり判定・数学 | [14. 数学と当たり判定](#14-数学と当たり判定) |
| **当たったかどうか（Collider）** | [14. 数学と当たり判定](#collider当たり判定の-component) |
| **弾む床（トランポリン）・物の重さ** | [14. 数学と当たり判定](#弾む面elastic-surface) |
| **モデル無しで球・箱を出す**（テクスチャ・光沢も） | [8. 描画](#モデル無しで形を出すprimitive-renderer) |
| **同じ Entity を何個でも作る（Prefab）** | [3. Entity](#prefab同じ-entity-を何個でも作る) |
| **ポーズ画面・クリア画面（UI Screen）** | [画面（UI Screen）](#画面ui-screenポーズ画面クリア画面) |
| **モデルのマテリアルを Inspector の値で上書きする** | [8. 描画](#モデルを出す) |
| **壁にレイを飛ばす・速く動く物の当たり（Raycast・SweepSphere）** | [14. 数学と当たり判定](#その場で調べるraycastsweepsphere) |
| **パーティクル（Particle System）**・**軌跡（Trail）** | [15. パーティクル・軌跡](#15-パーティクル軌跡) |
| ログ・調整項目 | [16. ログ・アサート・調整項目](#16-ログアサート調整項目) |
| **シーンの保存・Play / Stop で何が戻るか** | [2. ゲームの入口](#2-ゲームの入口) / [17. エディタ](#17-エディタ) |
| **シーンを切り替える（`LoadScene`）・クラスを書かないシーン** | [2. ゲームの入口](#シーンを切り替えるloadscene) |
| **名前の決まり（Sound と Audio・Calc と Get）** | [1. 全体の形](#名前の決まり) |
| エディタの使い方 | [17. エディタ](#17-エディタ) |
| やりがちな失敗の一覧 | [18. だめな例まとめ](#18-だめな例まとめ) |
| まだ無い物 | [19. まだ無い物](#19-まだ無い物) |

---

## 1. 全体の形

```
main.cpp                  ゲームの入口。Engine::Initialize してループを回すだけ
  └ Engine               ウィンドウ・DirectX12・各Managerの初期化と、フレームの開始/終了
      └ WindowManager    ウィンドウごとの更新と描画。Editorのウィンドウもここ
          └ SceneManager 再生状態（Play / Pause / Stop）とシーンの作り直し
              └ IScene   ゲームが書くシーン（GameScene）
                  └ Entity + Component   置いてある物と、その中身のデータ
                       └ System          Componentを回して動かす処理
```

**エンジンは「データの置き場所」と「毎フレームの段取り」を持ち、ゲームは「データの中身」と「処理」を書く。**

### 1フレームの順番

| 順 | 何が起きるか | 書く場所 |
|---|---|---|
| 1 | `Engine::BeginFrame`（時間の更新、ImGuiの開始） | エンジン |
| 2 | `EntityManager::FlushComponentChanges`（前フレームに予約したComponentの追加・削除を反映） | エンジン |
| 3 | `EditorHistory::Flush`（Undo・削除・貼り付けの予約を実行） | エンジン |
| 4 | `SceneManager::Update`：頭で作り直し（Play / Stop / Restart）・シーン切り替え（前のフレームの `LoadScene`）・保存 → **`IScene::Update`**（停止中は呼ばれない） | **ゲーム** |
| 5 | **`GameComponentRegistry::UpdateAll`（`SYSTEM(...)` で書いた処理）** | **ゲーム** |
| 6 | `EntityManager::UpdateTransforms`（親→子の順に `worldMatrix` を計算） | エンジン |
| 6.2 | `WindZoneSystem::Update`（Wind Zone の範囲を集め直す。再生中は範囲の中の Rigidbody を押す・画面の風の線の値を LensPass へ書く）→ `PhysicsSystem::Update`（再生中だけ） | エンジン |
| 6.4 | `CameraSystem::UpdateFollow`（再生中だけ。Camera Follow のカメラを、物理で動いた後の追う物へ近づける。動かしたら `worldMatrix` を計算し直す） | エンジン |
| 6.5 | `CameraSystem::Update`（有効な Camera Component の中で Priority が一番大きい物を選び、注視点・揺れ・切り替えの補間を入れた値をゲームのカメラへ写す） | エンジン |
| 7 | `ParticleManager::Update` / `LightSystem::Update`（ライトを集めてGPUへ） | エンジン |
| 8 | `EntityManager::FlushDestroy`（`Destroy` の予約を反映。子も一緒に消える） | エンジン |
| 9 | `CollisionSystem::Update`（Collider の重なりを調べて「当たりの一覧」を作る。ゲームは次のフレームの 5 で `GetHits` を読む） | エンジン |
| 9.5 | `TriggerActivatorSystem::Update`（再生中だけ。9 の当たりで、範囲に入った・出たを見て Target を出す・消す・フェードを進める） | エンジン |
| 9.6 | `UIScreen::Update`（再生中だけ。画面の出す・消す動きを進め、ゲームを止める・動かす。止めない時間で動く）→ `SceneTransition::Update`（シーンを切り替える幕を進める。閉じきったら `LoadScene` を頼む） | エンジン |
| 10 | `AudioSystem::Update`（種類ごとの音量を伝え、消えた・外した・無効の AudioSource の音を止め、初めて見つけた AudioSource を Play On Start で鳴らし、3Dの音の大きさ・左右を更新する。停止中は鳴らさない） | エンジン |
| 10.5 | `TrailSystem::Update`（消えた・外した Trail の点を捨て、点を古くして寿命の物を消し、Emitting の Trail に今の位置を足す。停止中は点を持たない） | エンジン |
| 11 | `WindowManager::DrawAll` → **`IScene::Draw`** → `ModelRenderSystem::Draw` → パーティクル → `TrailSystem::Draw`（軌跡の帯）→ ポストエフェクト | **ゲーム** |
| 12 | `Engine::EndFrame`（Present。GPUを待った後、返してもらったSRVスロットを使い回せるようにする） | エンジン |

**覚えておくこと：**

- **追加・削除は必ず「予約 → 次のフレームの頭で反映」**。`ForEach` で回している最中に配列が動かないようにするため
- `worldMatrix` は 6 で計算されるので、**Updateの中で読むと1フレーム前の値**
- 当たり（`CollisionSystem::GetHits`）も 9 で作られるので、**SYSTEM で読むのは前のフレームの判定結果**（動かした後の位置で調べた物）。消すと決めた Entity は 8 で消えているので、もう当たらない
- 停止中（Stop）は 4 の `IScene::Update` と 5 が飛ぶ（4 の頭の作り直し・保存は動く）。Pause中は `Time::GetDeltaTime()` が 0 になり、音も止まる

### ファイルの置き場所

**名前の終わりで、置き場所が決まる**（理由は `Audio.md` の1章）。

| 名前 | 置き場所 | 例 |
|---|---|---|
| `〜Component` | `MyEngine/Components/` | `TransformComponent.h`・`ColliderComponent.h`・`AudioSourceComponent.h`・`CameraComponent.h`・`TrailComponent.h` |
| `〜System` | `MyEngine/Systems/` | `ModelRenderSystem`・`LightSystem`・`CollisionSystem`・`AudioSystem`・`CameraSystem`・`TrailSystem` |
| `〜Editor`（Inspector の区画） | `MyEngine/Editor/Inspector/` | `TransformEditor`・`LightEditor` |
| `〜Gizmo` | `MyEngine/Editor/Gizmos/` | `LightGizmo` |
| `〜Window`（エディタのウィンドウ） | `MyEngine/Editor/Windows/` | `HierarchyWindow`・`InspectorWindow`・`ProjectWindow`・`SettingsWindow` |
| エディタが出すアセットの見た目 | `MyEngine/Editor/Assets/` | `AssetIcons`（アイコン）・`AssetThumbnails`（画像の小さい絵） |
| エディタのアイコンの画像 | エンジンの `Resources/Editor/Icons/`（ゲームの `MyEngine/Resources/Editor/Icons/` へ写される） | `Folder.png`・`Scene.png`（`SceneFlow.md` の14章） |
| Entity と Component の仕組み | `MyEngine/ECS/` | `EntityManager`・`ComponentUI`・`GameComponent`（ゲームが読むのはこれ1つ） |
| `〜Manager` | その分野のフォルダ | `ModelManager`（`Graphics/Model/`）・`SoundManager`（`Sound/`） |
| ゲームの Component（データ＋見せ方＋処理の1ファイル） | `CG3_Project/Source/Components/`（約束。どこに置いても動く） | `Spin.cpp` |

- ゲームの Component は、**プロジェクトに入っていればどこに置いても登録される**（`COMPONENT(...)` が起動のときに登録する）。Inspector の見出しにマウスを乗せると、書いたファイルが出る。
- モデル・テクスチャ・音は、**`resources/` か `MyEngine/Resources/` の下**（深さは自由）に置くと Inspector の一覧に出る。一覧の一番上に探す場所が出る。
- エンジンのヘッダは、ビルドのたびにゲームの `MyEngine/include/` へ写される（エンジンから消えた物はゲーム側からも消える）。

### 名前の決まり

| 決まり | 例 | 理由 |
|---|---|---|
| **Sound＝音の素材**（ファイル・波形）、**Audio＝鳴らす仕組み** | `SoundManager`・`AssetType::Sound`（素材）／`AudioSourceComponent`・`AudioSystem`・`AudioBus`（鳴らす仕組み） | Unreal も同じ分け方（SoundWave と AudioComponent）。混ざっていても、決まりがあれば迷わない |
| **Calc＝呼ぶたびにその場で計算**、**Get・メンバ＝覚えてある値** | `EntityManager::CalcWorldMatrix`（親をたどって計算）／`TransformComponent::worldMatrix`（フレームに1回計算した値） | Calc だと「重いかもしれない」と分かるので、毎フレーム何百回も呼ばない |
| マウスの位置は **`InGame`＝ゲーム用、`InWindow`＝エディタ・エンジン用** | `GetMousePositionInGame`／`GetMousePositionInWindow` | エディタではウィンドウの中とゲーム画面の中がずれる |

---

## 2. ゲームの入口

`main.cpp` は基本これだけ。触るのはウィンドウの設定と最初のシーンだけ。

```cpp
#include <MyEngine/Engine.h>

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	WindowConfig wc;
	wc.title = L"CG3";
	wc.width = 1280;
	wc.height = 720;
#ifdef _DEBUG
	wc.isImGui = true; // Editorを出すか
#endif
	// 最初のシーンを名前で（resources/scenes/GameScene.scene.json）
	Engine::Initialize(wc, "GameScene");

	while (Engine::ProcessMessage()) {
		Engine::BeginFrame();
		Engine::GetWindowManager()->UpdateAll();
		Engine::GetWindowManager()->DrawAll();
		Engine::EndFrame();
	}
	Engine::Finalize();
	return 0;
}
```

**シーン＝シーンファイル**（`resources/scenes/<名前>.scene.json`）。**名前はファイル名**（`"GameScene"`）。**Entity はシーンファイルに保存され、エンジンが作って消す**。**カメラは Entity（Camera Component）**なので、シーンでは作らない（10章）。

シーンのクラス（`IScene` を継承）は、**「Entity ではない物」（IBL・パーティクルのグループなど）が要るシーンだけ**書く。書いたら `REGISTER_SCENE(クラス名)` で、同じ名前のシーンに登録する。**書かないシーンは `DefaultScene`**（エンジンのシーン。Entity だけで動き、IBL はエンジンの既定の空で作るので PBR のモデルも出る）で開く。

```cpp
// GameScene.h
class GameScene : public IScene {
public:
	void Initialize() override; // Entityがそろった後に呼ばれる。IBLなど、Entityではない物を作る
	void Update() override;     // 毎フレーム（停止中は呼ばれない）
	void Draw() override;       // 描画コマンドを積む（カメラは GetCamera()）
	void Finalize() override;   // Initializeで作った物を片付ける（Entityはエンジンが消す）
	// GetCamera() は上書きしない（Hierarchy に置いた Camera Component のカメラが返る）
	// GetSceneFile() も上書きしない（名前から resources/scenes/GameScene.scene.json に決まる）

	// ファイルがまだ無いときだけ呼ばれる、最初の配置
	void CreateDefaultEntities() override;
};

// GameScene.cpp（include の後）
REGISTER_SCENE(GameScene); // "GameScene" のシーンを開くときは、このクラスを使う
```

シーンが作られる順番（起動・Play・Stop・シーン切り替えのたびに同じ）：

1. 前のシーンの `Finalize`
2. **全部の Entity を消す**
3. Entity を作る：Play を押した瞬間の退避があればそれ → 無ければシーンファイル → それも無ければ `CreateDefaultEntities()`
4. 予約した Component を付ける（`CreateDefaultEntities` で `RequestAdd` した物も、ここで付く）
5. `Initialize`（Entity はもうそろっているので、中で探してよい）

| 置き場所 | 保存されるか | 例 |
|---|---|---|
| Entity と、その Component の `ui.Field` に書いた項目 | **される**（Save でファイルへ。Play → Stop でも戻る） | 位置、モデル、ライト、**カメラ**、敵のHP |
| `Initialize` で作った物（シーンのクラスのメンバ） | されない（毎回 `Initialize` で作り直す） | IBL、パーティクルのグループ |
| Component の中でも `ui.Field` に書いていない項目 | されない（読み込むと初期値） | 実行中だけのタイマー |

> 前の書き方（`Engine::Initialize(wc, [] { return std::make_unique<GameScene>(); })` で作り、`GetSceneFile()` を上書きしない）のシーンは、名前が無いのでシーンファイルを使わない（Entity を `Initialize` で作って `Finalize` で消す）。新しく書くシーンは名前で開くこと。

### シーンを切り替える（`LoadScene`）

```cpp
// SYSTEM の中（GameComponent.h だけで使える）。どこから呼んでもよい
void GoalCheck(Handle<Entity> entity, Goal&) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		if (hit.phase == HitPhase::Enter && EntityManager::Get<Player>(hit.other) != nullptr) {
			// 次のシーンへ渡す値は、ゲームに1つの入れ物に入れておく（6章の GameState。gGameState.killCount は Result でも読める）
			SceneManager::LoadScene("Result"); // 次のフレームの頭で切り替わる
		}
	}
}
SYSTEM(GoalCheck);
```

- **切り替わるのは次のフレームの頭**（呼んだフレームの残りの処理は、今のシーンのまま最後まで回る）。
- シーンが無い（ファイルも `REGISTER_SCENE` も無い）ときは、Console に警告が出て切り替わらない（戻り値 `false`）。
- **エディタでは Play 中だけ効く**。Stop すると、**Play を押したシーンの、Play を押した瞬間**に戻る（Result へ進んでいても）。Control ウィンドウに `Scene: GameScene → 今は Result` と出る。
- 次のシーンへ渡す値（スコアなど）は、ゲームに1つの入れ物（6章の `gGameState`）に入れる。シーンが替わっても消えない。
- `SoundManager::Play` で直接鳴らした音は、シーンを替えても止まらない（12章。止まるのは Play / Stop / Restart だけ）。
- 前の書き方（`IScene::NextScene()` を上書きして次のシーンを返す）も動くが、新しく書くときは `LoadScene`。
- 仕組み（次のフレームまで待つ理由・`REGISTER_SCENE` が何をしているか）は `SceneFlow.md` の12章。
- **今のシーンを最初からやり直す**のは `SceneManager::ReloadScene()`（やられた・R キー）。Play を押したシーンなら Play を押した瞬間の状態へ（エディタで保存していない編集も残る）、`LoadScene` で切り替えた先ならシーンファイルから読み直す（`G5Player.md` の 1.3）。

#### 幕を付けて切り替える（`SceneTransition`）
```cpp
SceneTransition::LoadScene("Level02"); // だんだん暗くする（0.4 秒）→ 切り替える → だんだん明るくする（0.4 秒）
SceneTransition::ReloadScene();        // 同じく、今のシーンを最初から
if (SceneTransition::IsBusy()) { ... } // 幕が動いている間（入力を受けない・2回頼まない）
```
- 呼び方・戻り値は `SceneManager::LoadScene`・`ReloadScene` と同じ（GameComponent.h だけで使える）。ゲームはふつう、こちらを使う。
- 幕は Entity ではなくエンジンが持つ（シーンを作り直しても消えない）。止めない時間で動く（ポーズ中でも動く）。幕が動いている間に頼まれた切り替えは断る（`false`）。Scene ビューには描かない。
- 仕組みは `G9cSceneTransition.md` の 1.1。

### エディタでシーンを作る・開く（Project ウィンドウ）

| したいこと | 操作 |
|---|---|
| 新しいシーン | Project の何も無い所を右クリック → Create → Scene（`resources/scenes/` にでき、すぐ名前を打てる）。File → New Scene（Ctrl+N）でも |
| 開く | Project でシーンをダブルクリック。File → Open Scene でも。**停止中だけ**。保存していなければ「保存しますか？」 |
| 名前を付けて保存 | File → Save As（Ctrl+Shift+S）。写したシーンが開く |
| 保存していないか | Control ウィンドウと Project のシーンの名前に `*`。Undo で保存した時点まで戻すと消える |

- シーンの名前（ファイルの名前）に使えるのは**英数字と `_` `-` だけ**（日本語はまだ全部のアセットで読めないので、打てない）。
- **エディタは前回最後に開いていたシーンから始まる**（`Parameters/Editor/LastScene.json`）。`main.cpp` の `Engine::Initialize(wc, "GameScene")` は**製品版で最初に出るシーン**。編集したいシーンのために `main.cpp` を書き換えなくてよい。
- 作ったばかりのシーン（まだ Save していない）は、ファイルの中身が空。開くとカメラと平行光源から始まる。

---

## 3. Entity

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

### 良い例：フレームをまたぐときは `Handle` で覚える

```cpp
class GameScene : public IScene {
private:
	Handle<Entity> player_; // Handleなら持っていてよい（ただの番号2つ）
};
```

### だめな例：ポインタを持ち続ける

```cpp
class GameScene : public IScene {
private:
	Entity* player_;              // ✗ 次のフレームには別の物を指しているかもしれない
	TransformComponent* transform_; // ✗ Componentの配列は詰め直されるので特に危険
};
```

**取ったポインタはその場で使い捨てる。** 配列は要素が消えると後ろから詰められるので、持ち続けたポインタは別のEntityの物になる。

### Prefab（同じ Entity を何個でも作る）

Entity（子・Component・Script も一緒に）をファイルに取っておき、ゲームから何個でも作れる（Unity の Prefab）。中身はシーンファイルと同じ形の `resources/prefabs/<名前>.prefab`。

1. **作る**：Hierarchy で Entity を組み立てる（名前は英数字と `_` `-` だけ）→ 右クリック → **Save As Prefab**。同じ名前があれば上書き（前の中身は `.bak`）。
2. **直す**：Project ウィンドウで `.prefab` を右クリック → **Place in Scene**（原点に出る。Undo には入らない）→ 直して Save As Prefab → Hierarchy から消す。
3. **使う**：Script に Prefab の欄を作り、Inspector で選ぶ。

```cpp
// Script の Fields で、Prefab を選ぶ欄（保存はパスで）
ui.AssetField("Prefab", prefab_, AssetType::Prefab);

// 作る（Unity の Instantiate）。一番上の Entity の位置が position になる（回転・大きさは Prefab のまま）
const GameObject shape = GameObject::Instantiate(prefab_, {x, 12.0f, 0.0f});
shape.transform.rotation = ...; // Transform はすぐ使える
// shape.GetComponent<RigidbodyComponent>() はまだ nullptr（ほかの Component・Script は次のフレームの頭に付く。AddComponent と同じ予約）
```

- 作るたびに新しい EntityId になり、Component・Script の中の「Prefab の中の Entity への参照」（`EntityRef`・`GameObject`）は、作った方を指すように付け替わる。Prefab の外の Entity を指していた項目は外れる（警告が出る）。
- 作った Entity は Prefab とつながっていない。Prefab を保存し直しても、もう作った物は変わらない（そのあと作る物から変わる）。
- コードで読むなら `PrefabManager::Load(ToPath("resources/prefabs/Box.prefab"))`（同じファイルは1回だけ読む）。

---

## 4. Component

Componentは「Entityに付けるデータ」。型ごとに `ComponentStorage<T>` という連続した配列に入る。

```cpp
// 付ける（予約。実際に付くのは次のフレームの頭）
EntityManager::RequestAdd<ModelRendererComponent>(ball, renderer);
EntityManager::RequestAdd<Spin>(ball); // 初期値でよければ値は省略できる

// 取る（持っていなければ nullptr）
if (ModelRendererComponent* renderer = EntityManager::Get<ModelRendererComponent>(ball)) {
	renderer->color = 0xFF0000FF;
}

// 外す（予約）
EntityManager::RequestRemove<Spin>(ball);

// 全部回す（その型を持っているEntityだけ）
EntityManager::ForEach<Spin>([](Handle<Entity> entity, Spin& spin) { … });
```

| 関数 | いつ効くか |
|---|---|
| `RequestAdd<T>` / `RequestRemove<T>` | **次のフレームの頭**（それまで `Get<T>` は `nullptr` のまま） |
| `IsAddPending<T>` | 追加を予約済みか |
| `Get<T>` | すぐ。反映済みの物だけ |
| `Destroy` | フレームの最後 |

### Componentに入れてよい物

```cpp
// ○ 良い例
struct Bullet {
	Vector3 velocity = {0.0f, 0.0f, 10.0f};
	float lifeTime = 3.0f;
	int damage = 1;
	bool piercing = false;
	EntityRef owner;        // 別のEntityへの参照は EntityRef（EntityIdで覚えるので、保存・Stop・Undoをまたいでも同じ相手）
};
```

```cpp
// ✗ だめな例
struct Bullet {
	std::string name;             // ✗ 可変長（ヒープを使う）
	std::vector<Vector3> path;    // ✗ 可変長
	Entity* owner;                // ✗ 生ポインタ
	ID3D12Resource* buffer;       // ✗ GPUリソース
	virtual void Update();        // ✗ 仮想関数（vtableポインタが入る）
};
```

理由：**Undoとコピーは中身を丸ごとバイト列で写している**ので、ヒープを指す物が入っていると壊れる。`static_assert(std::is_trivially_copyable_v<T>)` でコンパイル時に止まる。
文字やリストがどうしても要るときは相談すること（今は仕組みが無い）。

### だめな例：`ForEach` の最中にその型を増減させる

```cpp
EntityManager::ForEach<Enemy>([](Handle<Entity> entity, Enemy& enemy) {
	EntityManager::RequestAdd<Enemy>(other);  // ✗ assertで止まる（回している配列を動かせない）
});
```

**予約（`RequestAdd` / `RequestRemove` / `Destroy`）は安全**。止まるのは、その場で配列を動かす操作だけ。

---

## 5. ゲーム固有Component（COMPONENT / SYSTEM）

**1ファイル＝1Component。** `CG3_Project/Source/Components/` に `.cpp` を置いて、VSのプロジェクトに追加する。書くのは「①データ ②見せ方 ③処理」だけ。

```cpp
#include <MyEngine/ECS/GameComponent.h>

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
	transform.rotation += spin.speed * (kDegToRad * deltaTime);
}
SYSTEM(SpinUpdate);
```

これだけで `EntityManager::RegisterComponent` も Inspector への登録も毎フレームの呼び出しも済む。詳しい一覧は `Entity.md` の **F-14**。

### `ui.Field` で書けるもの（抜粋）

| 書き方 | 見た目 |
|---|---|
| `ui.Field("HP", value.hp)` | 数字（`float` / `int` / `bool` / `Vector2` / `Vector3` / `Vector4`） |
| `ui.Field("HP", value.hp, Range(0.0f, 100.0f))` | 範囲の中でつまんで動かす（200 ピクセルで端から端まで。数字を打っても範囲に収まる。スライダーは使わない） |
| `ui.Field("Radius", value.r, AtLeast(0.0f))` | 0より小さくできない |
| `ui.Field("HP", value.hp, Tip("体力"))` | マウスを乗せると説明 |
| `ui.ColorField("Color", value.color)` | 色見本 |
| `ui.Field("Mode", value.mode)` | **enum** はコンボ（名前は magic_enum が作る） |
| `ui.Field("Target", value.target)` | **`EntityRef`**（別の Entity への参照）は相手の名前の台。Hierarchy から Entity をドラッグして入れる。右クリックで外す。相手が消えていると `(missing)` |
| `ui.AssetField("Model", value.model, AssetType::Model)` | **モデル・テクスチャの番号**（`uint32_t`）。resources 以下のファイルから選ぶ |
| `ui.Separator("見出し")` / `ui.Label("説明")` / `ui.Space()` | 飾り（保存はされない） |

### `ui.Field` に書いた物は、そのまま保存される

- **保存の名前はラベル**（`"HP"`）。enum は名前、`EntityRef` は EntityId（相手が居なければ 0）、アセットはパスで書かれる。
- **ラベルを変えると、前に保存した値が読めなくなる**（Log に「ファイルの項目 "HP" を読む所がありません」と出る）。表示だけ変えたいときは `"体力###HP"` と書く（`###` の後ろが保存の名前。ImGui の決まりと同じ）。
- `ui.Field` に書かなかった項目は保存されない（読み込むと初期値）。実行中だけの値はわざと書かなければよい。
- Component に項目を足しても、前のファイルはそのまま読める（足した項目は初期値になる）。
- 見せ方の関数の中に処理を書かない。Inspector に描くときだけでなく、保存・読み込みでも呼ばれる。

### `SYSTEM` の引数（＝「この型を全部持つEntityだけ呼ぶ」という注文書）

| 書き方 | 意味 |
|---|---|
| `void F(Spin& spin)` | `Spin` を持つ全部 |
| `void F(Spin& spin, TransformComponent& transform)` | **両方**持つEntityだけ |
| `void F(Spin& spin, float deltaTime)` | 最後の `float` は経過秒 |
| `void F(Handle<Entity> entity, Enemy& enemy)` | 先頭の `Handle<Entity>` は**そのEntity自身**（消したいとき） |
| `void F(const Spin& spin, Health& health)` | 読むだけなら `const&` |

### だめな例

```cpp
// ✗ 1ファイルに2つのComponentを詰める（どこに何があるか分からなくなる）
// ✗ 引数を値渡しにする（コンパイルエラー）
void SpinUpdate(Spin spin, TransformComponent transform) { … }

// ✗ Componentのファイルを静的ライブラリ（エンジン側）に入れる
//    → 誰も参照していないのでリンカに捨てられ、Add Componentに出てこない

// ✗ System同士を直接呼ぶ
void PlayerMove(...) { EnemyUpdate(...); } // 呼ぶ順番に縛られて壊れやすくなる
```

---

## 6. 状態をどこに置くか

**これが一番よく迷う所。** 置き場所は3つあって、使い分けは「**誰の状態か**」で決まる。

| 置き場所 | 誰の状態か | 例 |
|---|---|---|
| ① **Componentの中** | **その物1つだけ**の状態 | 敵のHP、弾の速度、プレイヤーの残機 |
| ② **ゲームに1つだけの入れ物** | ゲーム全体の状態 | スコア、倒した数、残り時間、今のステージ |
| ③ **タグComponent（付いているかどうか）** | 状態の切り替え | 倒された、無敵中、狙われている |

### 敵が複数いるとき（よくある勘違い）

**「敵ごとに倒したかのフラグ」はComponentに入れる。** Componentは**Entityごとに1個ずつ**用意されるので、`Enemy` を10体に付ければ `Enemy` の実体も10個ある。グローバル変数にするのは**全体で1個しかない物だけ**。

```cpp
// ○ 良い例
// Source/Components/Enemy.cpp
struct Enemy {
	int hp = 3;          // ← 敵ごとに1個。10体いれば10個ある
	bool isDefeated = false;
	float respawnTimer = 0.0f;
};

COMPONENT(Enemy, "Enemy") {
	ui.Field("HP", value.hp, Range(0.0f, 20.0f));
	ui.Field("Defeated", value.isDefeated);
}

// 倒されたかの判定は「その敵の中」で完結する
void EnemyDeath(Handle<Entity> entity, Enemy& enemy) {
	if (enemy.hp > 0 || enemy.isDefeated) {
		return;
	}
	enemy.isDefeated = true;
	++gGameState.killCount;         // ← 「全体で何体倒したか」だけがグローバル
	gGameState.speedBonus += 0.5f;
	EntityManager::Destroy(entity); // 自分を消す
}
SYSTEM(EnemyDeath);
```

```cpp
// Source/GameState.h  ← ゲーム全体の状態（1個しかない物だけ）
#pragma once
struct GameState {
	int killCount = 0;
	float speedBonus = 0.0f;
	bool isGameOver = false;
};
inline GameState gGameState; // inline を付けると、どのcppから見ても「同じ1個」
```

```cpp
// ✗ だめな例：敵ごとの状態をグローバルに置く
struct GameState {
	bool enemyDefeated;          // ✗ どの敵の話？ 敵が2体になった瞬間に破綻する
	bool enemy1Defeated;         // ✗ 敵が増えるたびに変数と if が増える
	bool enemy2Defeated;
	std::vector<bool> defeated;  // ✗ 番号と敵の対応を自分で管理することになる（消えたらずれる）
};
```

**Componentは「Entityごとの変数」そのもの。** 配列の管理はエンジンがやるので、自分で `vector<Enemy>` を持つ必要はない。

### 全体から「敵がまだ何体いるか」を知りたい

```cpp
// ○ 良い例：数えるのはSystemか、シーンのUpdateで
int aliveCount = 0;
EntityManager::ForEach<Enemy>([&](Handle<Entity> entity, Enemy& enemy) {
	if (EntityManager::IsActiveInHierarchy(entity) && !enemy.isDefeated) {
		++aliveCount;
	}
});
if (aliveCount == 0) {
	gGameState.isGameOver = false; // クリア処理へ
}
```

### 特定の1体を覚えておきたい（ボスなど）

```cpp
// ○ 良い例：Handle か EntityId で覚える
class GameScene : public IScene {
private:
	Handle<Entity> boss_;  // 同じ実行中ならこれでよい
	EntityId bossId_ = 0;  // 消して作り直しても追いたいならこっち（FindById で引く）
};
```

> シーンファイルを使うシーンでは、**Play・Stop・シーン切り替えのたびに Entity が作り直される**（Handle は変わり、EntityId は同じ）。シーンのクラスも作り直されて `Initialize` がまた呼ばれるので、メンバの Handle は `Initialize` の中で探し直す。探す目印には、空のタグ Component（`struct Boss {};`）を付けて `ForEach<Boss>` で見つけるのが楽。

```cpp
// ✗ だめな例
Enemy* boss_;  // ✗ 配列が詰め直されると別の敵を指す
```

### 別のEntityの状態を見たい（追尾など）

```cpp
// ○ 良い例：相手を EntityRef でComponentに持って、その場で取る
struct Homing {
	EntityRef target; // EntityId で覚える（Handle と違い、Stop・削除の Undo・再起動の後も同じ相手）
	float speed = 5.0f;
};

// Inspector で相手を選べるようにする（Hierarchy からドラッグ＆ドロップ）
// コードで入れるときは homing.target = EntityManager::RefOf(handle);
COMPONENT(Homing, "Movement") {
	ui.Field("Target", value.target);
	ui.Field("Speed", value.speed, AtLeast(0.0f));
}

void HomingMove(Homing& homing, TransformComponent& transform, float deltaTime) {
	const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(homing.target); // EntityRef のまま取れる
	if (targetTransform == nullptr) {
		return; // 相手はもう居ない（入っていない）
	}
	const Vector3 toTarget = Normalize(targetTransform->translation - transform.translation);
	transform.translation += toTarget * (homing.speed * deltaTime);
}
SYSTEM(HomingMove);
```

### System同士のつなぎ方

**直接呼び合わない。「片方が書いて、片方が読む」にする。** そうすると呼ぶ順番が変わっても壊れない。

```cpp
// ○ 良い例：印（タグComponent）を付けて、別のSystemが拾う
struct Dead {}; // 中身は空でよい
COMPONENT(Dead, "Enemy") { ui.Label("倒された印"); }

void EnemyDeath(Handle<Entity> entity, Enemy& enemy) {
	if (enemy.hp <= 0) {
		EntityManager::RequestAdd<Dead>(entity); // 印を付けるだけ
	}
}
SYSTEM(EnemyDeath);

void DeadEffect(Handle<Entity> entity, Dead&, TransformComponent& transform) {
	transform.scale = transform.scale * 0.9f; // 縮んで消える演出
	if (transform.scale.x < 0.05f) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(DeadEffect);
```

---

## 7. Transform

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
| `GetWorldRight(transform)` | 右（ローカル +X）がワールドでどちらを向いているか（Unity の `transform.right`。G7 の緑の円の矢印） |
| `GetWorldUp(transform)` | 上（ローカル +Y）がワールドでどちらを向いているか（Unity の `transform.up`。G7 のばねの向き） |
| `GetWorldForward(transform)` | 前（ローカル +Z）がワールドでどちらを向いているか |

```cpp
// ✗ だめな例
transform->rotation.y += 90.0f;            // ✗ クォータニオンの数字を直接足している（角度ではない）→ Rotate(*transform, {0.0f, 90.0f, 0.0f})
transform->worldMatrix = MakeIdentity4x4(); // ✗ 自分で書いても次のフレームに上書きされる
```

> **`worldMatrix` はフレームの中盤（`UpdateTransforms`）で計算される。** シーンの `Update` で読むと1フレーム前の値。位置を正確に使いたいときは `translation` から計算するか、`Draw` で読む。今すぐ正しい値が欲しいときは `EntityManager::CalcWorldMatrix(entity)`（親をたどって計算し直す）。

- **ワールド座標で動かす**なら `EntityManager::TranslateWorld(entity, move)`（親の下なら、親から見た量に直して `translation` に足す）。物理の押し戻し・カメラの追いかけもこれを使う。
- **子を名前で探す**なら `gameObject.FindChild("Body")`（自分の子だけを見る。孫は見ない。Unity の `transform.Find`）。シーン全体から探す `GameObject::Find` と違い、Prefab を何個置いても、それぞれの子が見つかる。

### 親を付け替える（見た目を変えずに）

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
- 行列を大きさ・回転・位置に戻す関数は `DecomposeAffineMatrix`（`MakeAffineMatrix` の逆。`GameBasics.md` の1章）。

---

## 8. 描画

### モデルを出す

**一番早いのはエディタで置くこと**：Hierarchy の＋で Entity を作り、Add Component → Rendering3D → Model Renderer、Model の欄でファイルを選んで Save。

コードで置くとき（`CreateDefaultEntities` の中。シーンファイルがまだ無いときの最初の配置）：

```cpp
// ① モデルを読む（返るのは番号。同じパスは2回読まれない）
const uint32_t modelHandle = ModelManager::Load("resources/monsterBall/monsterBall.gltf");

// ② Entityに ModelRendererComponent を付ける
ModelRendererComponent renderer;
renderer.modelHandle = modelHandle;
renderer.shadingType = ShadingType::PBR;
const Handle<Entity> ball = EntityManager::Create("MonsterBall");
EntityManager::RequestAdd<ModelRendererComponent>(ball, renderer);
```

あとは `ModelRenderSystem` が毎フレーム描く。**ゲーム側で `Renderer::DrawModel` を呼ぶ必要はない。** 保存するとモデルはパスで書かれ、次の起動で読み直される。

| 項目 | 意味 |
|---|---|
| `modelHandle` | `ModelManager::Load` の戻り値。0は未選択 |
| `textureHandle` | 0ならモデルのマテリアルのテクスチャを使う（Inspector の Texture 欄で選べる） |
| `color` | 0xRRGGBBAA の掛け算 |
| `shadingType` | `Unlit` / `Lambert` / `HalfLambert` / `Phong` / `BlinnPhong` / **`PBR`** |
| `blendMode` | `Normal` / `Add` / `Subtract` / `Multiply` / `Screen` |
| `rasterizerType` | `SolidBack`（通常） / `SolidNone`（両面） |
| `depthMode` | `TestWrite`（不透明） / 半透明は書き込みを切る |
| `billboard` | `None` / `Full` / `AxisY`（カメラの方を向く） |
| `enabled` | falseで描かない |
| `material`・`overrideMaterial` | Ambient〜Roughness・Alpha Cutoff。**`overrideMaterial` が true のときだけ、モデルの mtl / glTF の値の代わりに使う**（false なら mtl に無いメッシュだけ）。Inspector の Material で値を触ると自動で入る。Copy From Model でモデルの値を写せる |

> **PBRはIBL（環境光）が無いと真っ黒になる。** シーンが `IBLEnvironment` を持って `MakeFromHDR` しておくこと（`GameScene` がやっている）。

マテリアルのどの値をどのシェーダーが使うか：

| Shading | 使う値 |
|---|---|
| Unlit | Color・テクスチャ |
| Lambert・HalfLambert | ＋ Emissive |
| Phong・BlinnPhong | ＋ Specular（ハイライトの色。黒なら無し）・Shininess・Emissive |
| PBR | ＋ Metallic・Roughness・Emissive（Ambient は IBL が無いときだけ） |

Diffuse は今どのシェーダーも使っていない（色は Color で付ける）。

### モデル無しで形を出す（Primitive Renderer）

**Add Component → Rendering3D → Primitive Renderer**（`PrimitiveRendererComponent`）。モデルのファイルを使わずに、球・箱を描く（Unity の 3D Object → Sphere / Cube の見た目の所）。

| 項目 | 中身 |
|---|---|
| Shape | `Sphere`（Scale 1 で直径1）／`Box`（Scale 1 で1辺1。Entity と一緒に回る） |
| Material → Color | 色（RGBA。0〜1）。テクスチャと掛け合わせる |
| Material → Intensity | 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る） |
| Material → Surface | `Opaque`（不透明）／`Transparent`（Color の A の分だけ透ける。氷・水）／`Additive`（色を足して光る）。Transparent・Additive は奥行きを書かずに、不透明な物の後でカメラから遠い順に描く |
| Material → Texture・UV Scale | 貼る画像と、1枚の大きさ（1面を1として。0.5 なら1面に2回くり返す） |
| Material → Shading | `Unlit`（色そのまま）／`HalfLambert`（既定）／`BlinnPhong`（ハイライト）／`PBR`（金属感・粗さ）など。値の使い方は上の表と同じ |
| Material → Specular〜Emissive | 光沢・金属感・自己発光（`MaterialParams`） |

- 大きさは Transform の Scale。Collider の初期値（球の半径 0.5・箱の 1×1×1）と同じ大きさなので、Scale だけ変えれば見た目と当たりがそろう。
- 見た目（`PrimitiveMaterial`）は Elastic Surface の板・支柱と同じ物。PBR は、シーンが環境光を持っていれば（`IScene::GetEnvironment`。DefaultScene は持っている）そのまま、無ければ Half Lambert で描かれる。
- コードで作るなら `PrimitiveRendererComponent renderer; renderer.shape = PrimitiveShape::Sphere; renderer.material.color = {1, 0, 0, 1}; gameObject.AddComponent(renderer);`（`GameComponent.h` に入っている）。何個も作るなら Prefab の方が楽（[3. Entity](#prefab同じ-entity-を何個でも作る)）。

### 横から見た形の足場（Polygon Shape・Polygon Renderer）

**Hierarchy の ＋ → 3D Object → Polygon** で、形・見た目・当たりの3つが付いた足場ができる（初めは横 4m・高さ 1m の四角）。横から見た形（多角形）を、奥（Z）へ押し出した形。横から見るゲームの、斜め・台形・とがった岩のような足場に使う。

| Component | 中身 |
|---|---|
| **Polygon Shape**（Rendering3D） | 形だけ。外周の点（Entity から見た X・Y。3〜32 個）と Depth（奥行き）。Inspector の **Edit Shape** を押すと、Scene ビューで点を動かせる |
| **Polygon Renderer**（Rendering3D） | 見た目。前の面と横の面を描く（後ろの面は描かない）。Material は Primitive Renderer と同じ。UV Scale は m（2 なら 2m に1枚）。**World Space UV**：テクスチャをワールドの位置で貼る（足場を動かしても伸ばしても絵が伸びず、隣とつながる）。Side Shade：横の面の明るさ |
| **Collider**（Shape：**Polygon**） | 当たり。同じ Entity の Polygon Shape の点を使う。**球・カプセルとだけ当たる**（箱・多角形どうしは当たらない）。へこんだ形もそのまま（三角形に分けないので、つなぎ目で引っかからない） |

**Edit Shape（Scene ビュー）**

| 操作 | 内容 |
|---|---|
| ■ をドラッグ | 点を動かす。0.25m に吸い付く（Ctrl を押している間は吸い付かない）。押しただけでは動かない |
| 辺の真ん中の ＋ を押す | そこに点を足す（そのままドラッグできる。32 個まで） |
| 点を押して Delete | その点を消す（3 個より少なくはできない） |
| Esc・Finish Editing | 編集を終わる（Polygon Shape の無い物を選んでも終わる） |
| Ctrl+Z | ドラッグ・足す・消すを1回ずつ戻す |

- 辺どうしが交差すると、線が赤くなって描かれない（当たりはそのまま）。
- Entity の回転は **Z だけ**使う（X・Y で回すと、見た目は回るが当たりは回らない）。
- 形・置き場所が変わった時だけメッシュを作り直すので、止まっている足場は何個あっても三角形に分け直さない。

### 向きを示す矢印の列（Arrow Strip）

「> > > >」と流れる矢印。線に沿って並ぶ（曲げられる）。Hierarchy の ＋ → 3D Object → **Arrow Strip**、または Add Component → Rendering3D → Arrow Strip。

| 項目 | 中身 |
|---|---|
| 線（Path の Point） | Entity から見た X・Y の点（2〜16 個）。点を順に、なめらかに通る曲線（Catmull-Rom）。2 点ならまっすぐ。初めは右へ 3.4m |
| Edit Path | Scene ビューで点を動かす（操作は Polygon Shape の Edit Shape と同じ。2 個より少なくはできない）。点を結ぶ薄い線と、実際の曲線を描く |
| Start Offset | 線の始まりから、矢印を並べ始めるまでの長さ（m。線に沿って測る。円の外から並べるときなど） |
| Spacing | 矢印の間（m。線に沿って測る）。並べる長さがちょうど割り切れる間に直す（数は長さで決まる。64 個まで） |
| Size・Thickness | 矢印1つの長さ・高さ（m）・線の太さ |
| Scroll Speed | 流れる速さ（m/秒。0 で止まる、マイナスで逆）。端から出た矢印は反対の端から出てくる |
| Fade | 両端で消していく長さ（並べた長さに対する割合。0〜0.5）。**Opaque は小さくして**、Transparent は A を、Additive は RGB を下げて消す |
| Material | Primitive Renderer と同じ（初めは加算・Unlit の緑）。Texture を選ぶと矢印1つに1枚貼る |

- **停止中も流れる**（何も掛けていない時間で動かす）。Inspector・Edit Path で変えると、Scene ビューですぐ見られる。
- 子に置けば親と一緒に回る（緑の円は、子の「Arrows」に付けている。キューブはこの線に沿って飛ぶ：G7c）。
- 線の計算は `PathCurve`（`Math/Geometry/PathCurve.h`）。`ArrowStripSystem::BuildPath(strip, worldMatrix, path)` でワールド座標の線を作り、`path.Evaluate(長さ)` で位置と向きを聞ける。

### 霧（Fog）

**Add Component → Rendering3D → Fog**（`FogComponent`）。シーンに1つ（どの Entity でもよい。2つ以上あれば先に見つかった物）。全部の 3D の物（モデル・Primitive・Polygon・Arrow Strip・Sprite Renderer の板・パーティクル）と空（Skybox）の色に、最後に混ぜる。停止中のエディタでも掛かる。

| 項目 | 中身 |
|---|---|
| Color・Start Distance・Density・Max Opacity | **距離の霧**。カメラから Start Distance（m）より遠い所が、`1 − exp(−Density × 距離)` の割合で Color に近づく（Max Opacity まで） |
| Height Color・Height Base・Height Falloff・Height Max Opacity | **高さの霧**。Height Base（ワールドの Y）より下で一番濃く、上へ `exp(−(Y − Base) × Falloff)` で薄くなる（Height Max Opacity まで）。穴の底を暗くする |

- 空は無限に遠いので、距離の霧はいつも Max Opacity。下を向いた空には高さの霧が一番濃く掛かる（背景の板の下からのぞいても暗い）。
- 色は sRGB で選ぶ（色見本どおりに出る）。混ぜる計算はリニア。

> **色の空間**：Inspector・コードで書く色（Primitive・Sprite・Image・Text・ライト・霧・パーティクル・マテリアルの Ambient〜Emissive）は全部 **sRGB**（見た目の値）。Renderer がシェーダーへ送るときに `Math/Color.h` の `SrgbToLinear` でリニアに直す。画像も sRGB として読むので、色と画像は同じ空間で掛け合わさる。A は直さない。モデルの mtl の値はファイルのまま。
> **色は全部 float の `Vector4`**（Renderer の Config・線の色も。1色8ビットの 0xRRGGBBAA はやめた）。1 より明るくする（光らせる）ときは、色は 0〜1 のまま **Intensity**（倍率）を上げる。Renderer へ渡すときは `ApplyIntensity(色, 倍率)`（リニアで倍にする。sRGB の値にそのまま掛けない）。→ `HDRRendering.md`
- Scene ビューだけ切れる（View → Scene View → Fog）。
- シェーダーでは `#include "Buffers/Camera.hlsli"` の後に `#include "Fog/Fog.hlsli"` を書いて、色が決まった後に `ApplyFog(色, ワールドの位置)`（空は `ApplySkyFog(色, 向き)`）。値はカメラの定数バッファ（b0）の後ろ（C++ の `FogData`）。
- 例：森の背景の作り方（板の Z・Pixels Per Unit・並べ方）は `G10Forest.md` の 6章。

### 被写界深度（Depth Of Field）

**Add Component → Rendering3D → Depth Of Field**（`DepthOfFieldComponent`）。シーンに1つ（Fog と同じく、どの Entity でもよい）。ピントの合った奥行きだけはっきり見え、手前と奥がぼける。ポストエフェクト（ブルームより先）。停止中のエディタでも掛かる。

| 項目 | 中身 |
|---|---|
| Focus | **FollowTarget**：メインカメラの Camera Follow の Target（遊ぶキャラ）の奥行きにピント（ビューごとに、そのカメラから測る）／**Distance**：カメラから Focus Distance（m） |
| Focus Range | ピントの前後この奥行き（m）までは、ぼけない |
| Far Transition・Near Transition | ピントの範囲の奥・手前から、一番ぼけるまでの奥行き（m）。Near を 0 にすると手前はぼかさない |
| Max Blur | 一番ぼけた所の、ぼけの半径（ピクセル。高さ 720 の画面で。上限 32） |

- ぼけ方は**深度**で決まる。奥行き（深度）を書かない物は、後ろの物と同じだけぼけてしまうので、**半透明（Primitive の Surface が Transparent・Additive）・線・パーティクルは、ぼかした後に描く**（ぼけない）。Particle System は **Use Depth Of Field** に印を付けると、ぼかす前に描いて一緒にぼける（ぼけ方は粒の後ろにある物の距離で決まるので、奥の層の葉・湯気向け）。背景の Sprite Renderer の板は **Alpha Clip を入れる**（入れないと深度を書かず、空と同じだけぼけてしまう）。
- Scene ビューだけ切れる（View → Scene View → Depth of Field。Post Effects を切っていると掛からない）。
- 作り：CS で半分の大きさに縮めてぼけの大きさを作る → CS で黄金角のうずまきに読んでぼかす → PS で元の絵と混ぜる。説明は `G11DepthOfField.md`。

### ブルーム（Settings → PostEffect → Bloom）

明るい所をにじませる。シーン全体に1つ（Component ではなく Settings。`Parameters/PostEffect/Bloom.json` に保存）。

| 項目 | 中身 |
|---|---|
| Enabled・Threshold・Knee・Intensity | 掛けるか／これより明るい所だけ光る／しきい値の柔らかさ／合成の強さ |
| Mode | **Dual**：縮めながらぼかし、戻しながら足す（芯の周りに強く）／**Kawase**：川瀬式（MGF）。1/4〜1/32 の段ごとにガウスでぼかして全部足す（外へ広く薄く届く） |
| Weight1〜4 | Kawase の段ごとの重み（1/4・1/8・1/16・1/32）。Weight1 を上げると芯が強く、Weight3・4 を上げると遠くまでにじむ |

- シーンの絵は HDR（float）なので、Threshold 1 なら「1 を超えた所だけ」光る＝ **Intensity を 1 より大きくした物だけ光る**（Additive の物・Add の粒の重なり・強いハイライトは Intensity 1 でも 1 を超えることがある）。作り方は `KawaseBloom.md`・`HDRRendering.md`。

### 露出・トーンマップ（Settings → PostEffect → ToneMap）

3D は HDR（`R16G16B16A16_FLOAT`）の絵に描き、最後のパス（レンズ）で画面の 0〜1 に収める。レンズや Post Effects を切っていても、いつも掛かる（`Parameters/PostEffect/ToneMap.json`）。2D（Sprite・Text・UI）はその後の画面（SDR）に重ねる。

| 項目 | 中身 |
|---|---|
| Mode | **None**：1 で切るだけ（前の見た目。初めの値）／**Reinhard**：x/(1+x)。飛ばないが暗く淡い／**ACES**：映画の標準の近似。明るい所は柔らかく白へ、暗い所は締まる／**GT**：グランツーリスモ（内村式）。真ん中はそのまま、明るい所だけ柔らかく（アニメ調に向く） |
| Exposure | トーンマップの前に掛ける倍率（2 で1段明るい）。ブルームの光り方は変わらない |

- 光らせる物：Primitive Renderer（Polygon Renderer・Elastic Surface・Arrow Strip も）・Particle System・Model Renderer・Sprite Renderer・Trail の **Intensity**（Color の下）。
- CS でテクスチャを読み書きするとき：`ComputeTexture`（UAV と SRV を持つ）、`ComputePipeline::SetTextureTable`（`gTextures[]`）・`SetUnorderedAccessTexture`（`RWTexture2D`）。

### 図形・スプライト

Entityにしないその場限りの描画は `Renderer` を直接呼ぶ（`Draw()` の中で）。

```cpp
Renderer::SphereConfig config;
config.transform.translation = {0.0f, 1.0f, 0.0f};
config.transform.scale = {0.5f, 0.5f, 0.5f};
config.color = 0xFF0000FF;
config.shadingType = ShadingType::Lambert;
Renderer::DrawSphere(config);
```

どの `***Config` も `transform`（拡縮・回転・移動）・`color`・`textureHandle`・`shadingType`・`blendMode` を同じ名前で持つ。

`DrawModel` / `DrawSprite` / `DrawTriangle` / `DrawSphere` / `DrawAABB` / `DrawOBB` / `DrawLines` / `DrawRect3d` / `DrawQuad2d` / `DrawQuad3d` / `DrawParticle` / **`DrawMesh`** がある。

`DrawMesh` は頂点と番号を自分で並べる形（毎フレーム形が変わる物。軌跡の帯は Trail がこれで描いている）。頂点はワールド座標、既定は両面描き。

```cpp
std::vector<Vertex3dData> vertices = {
	{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // 位置（w は 1）, UV, 法線
	{{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	{{0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
};
std::vector<uint32_t> indices = {0, 1, 2}; // 3つで三角形1つ
Renderer::MeshConfig config;
config.color = 0xFF00FFFF;
config.camera = GetCamera();
Renderer::DrawMesh(config, std::move(vertices), std::move(indices)); // move で渡すと配列を写さない
```

```cpp
// ✗ だめな例
void GameScene::Update() {
	Renderer::DrawSphere(config); // ✗ 描画は Draw() で積む（Updateで積むと停止中に消える）
}
```

---

## 9. ライト

**ライトもEntityに付けるComponent。** 位置と向きは付けたEntityのTransformから取る（向き＝ローカルの +Z）。

```cpp
const Handle<Entity> sun = EntityManager::Create("Directional Light"); // CreateDefaultEntities の中で（エディタで置くなら Create → Light）
TransformComponent* transform = EntityManager::Get<TransformComponent>(sun);
transform->rotation = {50.0f * kDegToRad, -30.0f * kDegToRad, 0.0f};
EntityManager::RequestAdd<DirectionalLightComponent>(sun);
```

| 種類 | 主な項目 |
|---|---|
| `DirectionalLightComponent` | `color` / `intensity`。**有効な物のうち最初の1つだけ**が使われる（2つ以上あると警告） |
| `PointLightComponent` | `color` / `intensity` / `radius` / `decay` |
| `SpotLightComponent` | `color` / `intensity` / `range` / `decay` / `outerAngle` / `innerAngle`（度、89まで） |

```cpp
// ✗ だめな例
struct DirectionalLightComponent { Vector3 direction; }; // ✗ 向きはTransformから取る（二重管理になる）
```

Hierarchyの **Create → Light** から作るのが早い（位置と向きの初期値もUnityと同じに入る）。

---

## 10. カメラ

**カメラは Entity に付ける Camera Component。** Hierarchy の **＋ → Camera** で「Main Camera」ができる（Add Component → Camera → Camera でも付く。3D・2D どちらでも使うので Rendering3D の中ではない）。

| 項目 | 中身 |
|---|---|
| Transform | カメラの位置・向き（大きさは使わない）。**親の下に置くと一緒に動く** |
| Field Of View | 縦の視野角（**度**）。小さいほど望遠 |
| Near / Far | 映る距離の範囲 |
| Priority | 有効な Camera が複数あるとき、大きい方を使う。切り替えは、この値か Entity の有効・無効で |
| Blend（Style・Time・Ease） | このカメラへ切り替わるときの動き方。`Cut`（すぐ切り替える）／`Ease`（Time の秒をかけて、位置・向き・視野角・Camera Volume の値を補間する） |
| Time Mode | このカメラの切り替え・Camera Follow・Camera Look At・Camera Shake が使う時間。`Game`（スローで遅くなり、ヒットストップ・ポーズで止まる）／`Real`（止まらない。エディタの一時停止では止まる） |

- Save される。Play 中に動いたカメラは Stop で戻る。停止中も Game ビューはそのカメラから撮る。
- **Transform はカメラの本来の位置・向き。** 注視点・揺れ・切り替えの補間は写す値にだけ入り、Transform は変わらない。画面に写っている位置・向きは `CameraSystem::GetMainCamera()` で読む。
- 補間の途中でさらに切り替えても、そのときに見えている値から始まるので絵は飛ばない。補間の途中かは `CameraSystem::IsBlending()` で分かる。`GetMainCameraEntity()` は補間の途中でも切り替わった先のカメラを返す。
- シーンを読み直したとき・Play を押したときは補間しない。
- Camera が1つも無いときは、既定のカメラ（(0, 0, -10) から +Z を向く）で撮る。
- シーンの `GetCamera()` は**上書きしない**（上書きしなければ、`CameraSystem` が選んだカメラが返る）。エンジンの描画・パーティクル・3Dの音は `GetCamera()` のカメラを使う。
- **カメラを動かす＝その Entity の Transform を動かす**（SYSTEM の中で）。

```cpp
void MoveCameraUp(float deltaTime) {
	const Handle<Entity> camera = CameraSystem::GetMainCameraEntity(); // Camera Component が無ければ無効な Handle
	if (TransformComponent* transform = EntityManager::Get<TransformComponent>(camera)) {
		transform->translation.y += 2.0f * deltaTime;
	}
}
```

### 追いかけるカメラ（Camera Follow）

**Add Component → Camera → Camera Follow**（`CameraFollowComponent`）。Camera と同じ Entity に付けて、Target に追う Entity を Hierarchy からドラッグする。動かすのは Plane の面の上で、カメラの向きは置いたまま。再生中だけ動く（停止中は置いた位置のまま）。

| 項目 | 中身 |
|---|---|
| Target | 追う Entity |
| Plane | カメラを動かす面。`XY`（ワールドの X・Y。横から見るゲーム）／`XZ`（地面。見下ろしのゲーム。傾けたカメラでも、置いた通りの見え方のまま追う）／`View`（カメラの右・上。どの向きのカメラでも。奥行きも追いかけ始めの長さに保つ） |
| Mode | 追い方。`Center`（追う物を真ん中に）／`LookAhead`（追う物の速さ × Look Ahead Time だけ先を真ん中に。止まると戻る）。選んだ追い方の値だけが出る |
| Look Ahead Time・Max・Smooth Time（LookAhead のとき） | 何秒先を見せるか（X 0.4・Y 0。XY で Y を入れると跳ぶたびに上下に揺れる）・先読みの上限（m）・先読みが変わるまでの時間（指数補間） |
| Offset | 追う物から、画面の真ん中をどれだけずらすか（m）。XY で Y を上げると足元が広く見える |
| Dead Zone | 遊び（m）。真ん中から横・縦にこの範囲で動いても、カメラは動かない |
| Ground Snap Y | Plane が XY のとき。追う物（Rigidbody）が地面にいる間は、縦の遊びを使わずに合わせる。跳んでも画面は揺れず、段に乗ると上がる |
| Smooth Time | 追いつくまでの時間の目安（秒）。走っている間は「速さ × Smooth Time」くらい遅れる |
| Max Lag | 遅れの上限（m）。速く動いても、追う物が画面から出ない |
| Bounds（Use Bounds・Min・Max） | 画面の端が Min〜Max の外を写さない（ステージの端で止まる）。XY はワールドの X・Y で追う物の奥行きで測る。XZ はワールドの X・Z で追う物の高さで測る（カメラが回っても範囲は回らない）。View には無い |

- 横・縦の向きは、XY はワールドの X・Y。XZ はカメラの右とカメラの前を地面に寝かせた向き、View はカメラの右・上で、カメラの Transform を回すと毎フレーム付いてくる（追う物のまわりを回る動きになる）。
- Camera Follow が読む向きは Transform の向きだけ。Camera Look At の向きは読まない。
- 動かすのは物理の後・`CameraSystem::Update` の前（1フレームの順番の 6.4）なので、追う物と1フレームずれない。
- 自分で動かしたいカメラ（演出など）は、Camera Follow を外す（Enabled）か、付けずに SYSTEM・Script で Transform を動かす（上の例）。
- 作りと数字の決め方は `G5Player.md` の 1.1。

### 注視点（Camera Look At）

**Add Component → Camera → Camera Look At**（`CameraLookAtComponent`）。カメラが Target を見続ける。変えるのは写す向きだけで、Transform の向きは変えない。停止中も Game ビューに効く。

| 項目 | 中身 |
|---|---|
| Target | 見る Entity。空なら Offset をワールドの座標として見る |
| Offset | 見る点のずれ（m。ワールドの向き） |
| Smooth Time | 見る点が追いつくまでの時間の目安（秒。再生中だけ効く） |

カメラの子の Entity は Transform に付いていくので、注視点の向きには回らない。

### カメラの揺れ（Camera Shake）

**Add Component → Camera → Camera Shake**（`CameraShakeComponent`）。揺れは写す値にだけ足すので、揺れが終わると必ず元の位置に戻る。再生中だけ揺れる。

| 項目 | 中身 |
|---|---|
| Noise：Position・Rotation | ずっと続く揺れの幅。位置は m（カメラの右・上・前）、向きは度（上下の首振り・左右の首振り・傾き） |
| Noise：Frequency・Gain | 1秒に揺れる回数の目安・揺れ幅に掛ける倍率 |
| Impulse：Gain | 受けた強さに掛ける倍率（0 なら受けない） |
| Impulse：Frequency・Damping | 1秒に揺れる回数・収まる速さ（減衰比。0 に近いほど長く揺れる） |
| Impulse：Rotation | 1m ずれたときに一緒に回す角度（度）。遠くの景色も揺れて見える |
| Test Impulse | 再生中に押すと 0.3m の衝撃を送る |

```cpp
CameraSystem::AddImpulse(0.4f);                     // 強さは最初に振れる幅（m）。向きは画面の面の中でランダム
CameraSystem::AddImpulse(0.4f, {1.0f, 0.0f, 0.0f}); // ワールドの +X へ。長さは1でなくてよい
```

有効な Camera Shake 全部が受ける（切り替えの途中でも、両方のカメラが揺れる）。

### カメラごとのポストエフェクト（Camera Volume）

**Add Component → Camera → Camera Volume**（`CameraVolumeComponent`）。チェックを入れた項目だけ、このカメラの値を使う。外した項目は全体の設定（Settings → PostEffect・Depth Of Field Component）のまま。カメラの切り替えと一緒に補間する。

| 項目 | 中身 |
|---|---|
| Weight | 全部の項目の効き具合（0：全体の設定のまま 〜 1：このカメラの値） |
| Focus | ピントの合わせ方。`Global`（全体の設定）／`Distance`（Focus Distance）／`LookAtTarget`（Camera Look At が見ている点）／`FollowTarget`（このカメラの Camera Follow の Target） |
| Depth Of Field | Focus Range・Far Transition・Near Transition・Max Blur |
| Bloom | Threshold・Knee・Intensity |
| Lens | Distortion・Aberration・Radial Blur・Vignette |
| ToneMap | Exposure |

- Bloom の Mode・ToneMap の Mode は途中の値が無いので持たない（全体の設定のまま）。
- 全体の設定で切ってある効果も、Bloom の Intensity・Lens の項目・Max Blur を上書きしたカメラでは掛かる。
- Game ビューと製品版に掛かる。Scene ビューには掛からない（別のカメラから見るので、ピントが合わない）。
- 空間に置く Volume は無い。

### マウスで地面を指す（`ScreenPointToRay`）

画面の位置から、カメラの奥へ向かうレイを作る（Unity の `Camera.ScreenPointToRay`）。地面（y=0）や壁との交点を取るのに使う。透視でも正射影でも動く。

```cpp
#include <MyEngine/Camera/Camera.h>
#include <MyEngine/Collision/Collision.h>

const Ray ray = CameraSystem::GetMainCamera()->ScreenPointToRay(InputManager::GetMousePositionInGame()); // 始点は近クリップ面、向きは長さ1
float t = 0.0f;
if (Collision::RayPlane(ray, {{0.0f, 1.0f, 0.0f}, 0.0f}, &t)) {
	const Vector3 ground = Collision::RayPoint(ray, t); // マウスが指している地面の位置
}
```

- 引数は `GetMousePositionInGame()` の値をそのまま渡す（真ん中0、右と上が+、端が±1）。
- 物（Collider）を指したいときは、このレイを `CollisionSystem::Raycast` に渡す（14章）。
- `GetViewProjectionMatrix()` は値で返る（前は参照で返していて、読むと壊れた値だった。`InkSlash.md` の 1.2）。

```cpp
// ✗ だめな例
class GameScene : public IScene {
	Camera* GetCamera() override { return camera_; } // ✗ 上書きすると Camera Component が無視される（前の書き方）
};
void GameScene::Update() {
	ParticleManager::SetCamera(GetCamera()); // ✗ 要らない（エンジンが毎フレーム GetCamera から取る）
}
```

Sceneビューのカメラ（デバッグカメラ）は別物で、ゲームのカメラには影響しない。

---

## 11. 入力

```cpp
if (InputManager::IsKeyTriggered(DIK_SPACE)) { … } // 押した瞬間
if (InputManager::IsKeyPressed(DIK_W))      { … } // 押している間
if (InputManager::IsKeyReleased(DIK_SPACE)) { … } // 離した瞬間

const float x = InputManager::GetLeftStickX();     // -1 〜 1（デッドゾーン処理済み）
if (InputManager::IsButtonTriggered(XINPUT_GAMEPAD_A)) { … }

const long dx = InputManager::GetMouseDeltaX();

const Vector2 move = InputManager::GetMoveAxis();                // WASD・矢印キー・左スティックをまとめた移動（-1〜1。斜めでも長さ1まで）
const Vector2 mouse = InputManager::GetMousePositionInGame();    // ゲーム画面の座標（スプライトの position と同じ：真ん中0、右と上が+、端が±1）
if (InputManager::IsMouseInGame()) { … }                          // マウスがゲーム画面（エディタでは Game ビューの絵）の上か
```

キーの名前は DirectInput の `DIK_A` `DIK_SPACE` `DIK_LSHIFT` など。

- キーボードとパッドは、**ウィンドウが手前に無いとき・ImGui に文字を打っているときは「押していない」**扱い。
- マウスの位置は、エディタでは Game ビューの中の位置が欲しいので、`GetMousePositionInWindow`（ウィンドウの中のピクセル。エディタ・エンジン用）ではなく `GetMousePositionInGame` を使う。

```cpp
// center・half はスプライトと同じ座標（真ん中が0、端が±1）。half はボタンの幅・高さの半分
bool IsButtonClicked(const Vector2& center, const Vector2& half) {
	if (!InputManager::IsMouseButtonTriggered(0)) {
		return false;
	}
	const Vector2 mouse = InputManager::GetMousePositionInGame();
	return std::abs(mouse.x - center.x) <= half.x && std::abs(mouse.y - center.y) <= half.y;
}
```

```cpp
// ✗ だめな例
if (GetAsyncKeyState(VK_SPACE)) { … } // ✗ Windowsを直接叩くとImGuiに文字を打っている間も反応する
```

---

## 12. 音

**ふつうは AudioSource（Component）で鳴らす。** Inspector の **Add Component → Audio → Audio Source** で付け、Sound（`resources/` の下の `.wav` / `.mp3`）・Volume・Pitch・Loop・Play On Start を決めて Save する。

- **BGM はコードが要らない**：Loop と Play On Start に印を付け、**Bus を BGM** にするだけ。Play を押すと鳴り、Pause で止まり、Stop で止まる。停止中は鳴らない。
- Entity を消す・無効にする・Audio Source を外すと、その音は止まる。
- **▶ Preview** で、停止中でも試し聞きできる（Inspector の Volume・Pitch・Bus で1回鳴らす）。

| 項目 | 中身 |
|---|---|
| Bus | 音の種類（BGM / SE）。種類ごとの音量が掛かる。新しく付けた物は SE |
| Spatial | 3Dの音。**ゲームのカメラ**から離れると小さくなり、左右に振れる（位置は Transform） |
| Min Distance / Max Distance | Min まで近いと元の大きさ、Max より遠いと聞こえない。その間はまっすぐ小さくなる |

種類ごとの音量は、Settings ウィンドウ（Edit → Settings）の **Audio** のページ → Volume（**Master・BGM・SE**。1が元の大きさ）。Save すると `Parameters/Audio/Volume.json` に残る。ゲームの設定画面からは次のように書く。

```cpp
	// 設定画面（0〜1）。Settings ウィンドウの Audio / Volume も一緒に変わる
	AudioSystem::SetBusVolume(AudioBus::BGM, 0.5f);
	AudioSystem::SetBusVolume(AudioBus::SE, 0.8f);
	AudioSystem::SetMasterVolume(1.0f);
	const float bgmVolume = AudioSystem::GetBusVolume(AudioBus::BGM);
```

効果音は、SYSTEM の中で `AudioSystem` を呼ぶ（`GameComponent.h` だけで使える）。

```cpp
void EnemyHit(Handle<Entity> entity, Enemy& enemy) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		if (hit.phase != HitPhase::Enter || EntityManager::Get<Bullet>(hit.other) == nullptr) {
			continue;
		}
		EntityManager::Destroy(hit.other); // 弾は消す
		--enemy.hp;
		// 敵に付けた Audio Source の音を重ねて鳴らす（Play On Start は外しておく）。この後で敵を消しても、音は最後まで鳴る
		AudioSystem::PlayOneShot(entity);
	}
	if (enemy.hp <= 0) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(EnemyHit);
```

| 関数 | すること |
|---|---|
| `AudioSystem::Play(entity)` | その Entity の Audio Source を頭から鳴らす（鳴っていたら止めてから）。Entity を消すと止まる |
| `AudioSystem::Stop(entity)` | 止める |
| `AudioSystem::IsPlaying(entity)` | 鳴っているか（鳴り終わったら `false`。一時停止中は `true`） |
| `AudioSystem::PlayOneShot(entity)` | 重ねて鳴らす。Entity を消しても最後まで鳴る（倒した音など）。3Dの音なら、鳴らした瞬間の位置で大きさ・左右が決まる |

```cpp
// ✗ だめな例
void EnemyDeath(Handle<Entity> entity, Enemy& enemy) {
	if (enemy.hp <= 0) {
		AudioSystem::Play(entity);      // ✗ 次の行で消すので、鳴った瞬間に止まる
		EntityManager::Destroy(entity); //   → PlayOneShot にする
	}
}
```

`SoundManager` を直接呼んでもよい（Entity に付かない音。シーンをまたいで鳴らし続けたい BGM など。`#include <MyEngine/Sound/SoundManager.h>`）。

```cpp
const uint32_t bgm = SoundManager::Load("resources/sounds/bgm.wav");                  // 読む（番号が返る）
const uint32_t play = SoundManager::Play(bgm, true, 0.5f, 1.0f, AudioBus::BGM);        // 鳴らす（ループ, 音量, ピッチ, 種類）
SoundManager::SetVolume(play, 0.2f);
SoundManager::Stop(play);
```

`Load` の戻り値が**音のデータ**、`Play` の戻り値が**鳴っている1つ**。止めたり音量を変えたりするのは後者。直接鳴らした音も、エディタの Pause で止まり、Play / Stop / Restart で全部止まる（次のシーンへの切り替えでは止まらない）。`Play` の最後の引数（種類）を書かなければ SE。

```cpp
// ✗ だめな例
void GameScene::Update() {
	const uint32_t se = SoundManager::Load("resources/se.wav"); // ✗ 毎フレーム読む（Loadは Initialize で1回）
	SoundManager::Play(se, false);
}
```

```cpp
	SoundManager::Play(bgm, true); // ✗ SE の音量つまみを通る（BGM の音量を下げても小さくならない）→ 最後に AudioBus::BGM
```

- **ファイル名は英数字にする**（日本語の名前は読めない。`Audio.md` の「8. 未解決」）。日本語の名前のファイルは Inspector の一覧に出ず、Console に「名前を英数字にしてください」と出る（`SceneFlow.md` の 19.1）。

---

## 13. 時間

```cpp
const float deltaTime = Time::GetDeltaTime();         // 前フレームからの秒（一時停止・スロー・ヒットストップが掛かる）
const float raw = Time::GetUnscaleDeltaTime();        // 何も掛かっていない方（UIの演出などに）
const float real = Time::GetRealDeltaTime();          // スロー・ヒットストップ・ポーズは掛からず、エディタの一時停止だけ 0（カメラの Time Mode が Real のとき）
Time::SetGameTimeScale(0.1f);                         // スロー（1で普通）
Time::HitStop(0.06f);                                 // 0.06秒だけ止める（重ねて呼んだら長い方）
```

`GetDeltaTime()` ＝ 前フレームからの秒 × 一時停止（0か1）× ゲームのスロー ×（ヒットストップ中なら0）。**`deltaTime` を掛けて動かしていれば、ポーズもスローも自分で書かなくても効く**（SYSTEM の最後の `float` 引数も同じ値）。

- 一時停止はエンジン（Pause ボタン）、スローとヒットストップはゲームが決める。別々に持っているので、ゲームのスローがエンジンに書き直されることはない。
- 1フレームの経過時間は **`Time::kMaxDeltaTime`（0.1 秒）で止める**（ブレークポイントで止めて再開しても、物が飛んでいかない）。上限を使う所（物理の「何歩に分けるか」）もこの値を読む。
- ヒットストップは**何も掛けていない時間**で数える（スロー中でも同じ長さだけ止まる）。
- **Play / Stop / Restart・シーンの切り替えで、スローとヒットストップは戻る**（スロー中に Stop しても、エディタはスローにならない）。
- **ポーズ**（`Time::SetGamePaused(true)`・`IsGamePaused()`）：時間が 0 になり、**Script の Update も呼ばれない**（ポーズ画面の操作でプレイヤーが動かないように）。ポーズ画面（Screen の Pause Game）が開いたときに書く。**ポーズ中も、開いている画面の中の Script は呼ばれる**。止めない時間（`GetUnscaleDeltaTime`）で動く物（幕・Text の演出）は止まらない。Play / Stop / Restart・シーンの切り替えで外れる。

```cpp
// ✗ だめな例
transform->translation.z += 0.1f;                    // ✗ フレームレートで速度が変わる／ポーズで止まらない
// ○
transform->translation.z += 5.0f * Time::GetDeltaTime();
```

---

## 14. 数学と当たり判定

```cpp
#include <MyEngine/Math/MathIncludes.h>

const float r = RandomEngine::GetFloat(0.0f, 1.0f);
const int n = RandomEngine::GetInt(0, 10);
const Vector3 v = RandomEngine::GetInsideUnitSphere();

const Vector3 a = Normalize(b - c);
const float len = Length(v);
const float t = MathUtility::Clamp(x, 0.0f, 1.0f);
const Vector3 p = MathUtility::Lerp(from, to, 0.5f);
velocity.x = MathUtility::MoveTowards(velocity.x, target, accel * deltaTime); // target へ最大 accel*deltaTime だけ近づける（行き過ぎない）
```

当たり判定は形どうしの関数。**`Collision::` を付けて呼ぶ**（`#include <MyEngine/Collision/CollisionIncludes.h>`）。

```cpp
const Sphere sphere{GetWorldPosition(*transform), 1.0f};   // 中心, 半径
const AABB box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}; // 最小点, 最大点
if (Collision::SphereAABB(sphere, box)) { … }

const Ray ray{origin, direction}; // 始点, 向き
float t = 0.0f;
if (Collision::RaySphere(ray, sphere, &t)) { … } // 当たった距離が t に入る
```

`SphereSphere` / `AABBAABB` / `SphereAABB` / `CapsuleSphere` / `CapsuleAABB` / `RaySphere` / `RayAABB` / `RayPlane` / `RayCapsule` / `IsPointInSphere` / `IsPointInAABB` / `ClosestPointOnAABB` / `ClosestPointOnSegment` / `ClosestPointsBetweenSegments` / `DeepestPointOnSegmentInAABB` / `RayPoint`。

`Capsule{start, end, radius}` は「線分 start-end を半径で太らせた形」（球が start から end まで動いた跡・カプセルの Collider）。`ClosestPointOnSegment(point, a, b)` は線分 a-b の上で point に一番近い点（線に触れているか＝その点との距離が半径以内か）。`ClosestPointsBetweenSegments` は2本の線分のお互いに一番近い点、`DeepestPointOnSegmentInAABB` は線分の上で箱に一番深く入っている点（外なら一番近い点）。

> **Rigidbody を付けた物だけ**、PhysicsSystem が重力で動かし、Collider から押し戻す（Add Component → Physics → Rigidbody。`velocity` を書いて動かす。`isGrounded` で乗っているかが分かる）。Collider だけなら押し戻さない。
> Rigidbody 同士は **Mass の逆の割合**で押し合う（軽い方がたくさん動き、ぶつかった後は同じ速さになる。跳ね返らない）。**Collide With Rigidbodies** を外した方は、ほかの Rigidbody とすり抜ける（壁・床にはぶつかる。当たりの知らせ `OnCollision〜` は来る。当たると吹き飛ぶプレイヤーなど。相手の図形の動きは変わらない）。

### 時間で値を変えるカーブ（Curve）

時間に対して値がどう変わるかを、キーでつないだカーブで表す（`#include <MyEngine/Curve/CurveManager.h>`）。補間はキーごとに Constant・Linear・Bezier。カーブの端より外は端のキーの値。

```cpp
const Curve* curve = CurveManager::Get(handle);   // .curve のファイルの番号から（選んでいなければ nullptr）
const float value = curve->Evaluate(time);        // 時間の値

Curve bounce = Curve::FromEase(Ease::Type::OutBounce); // Ease と同じ形（時間 0〜1・値 0〜1）
```

- ファイルは Project の **Create → Curve** で作り、選ぶと Inspector で直せる（17章）。Script の `Fields` に `ui.AssetField("Curve", handle, AssetType::Curve);` と書くと選べる。
- ほかのファイルの中に入れるときは `CurveManager::ToJson`・`FromJson` を使う。
- キーは時間の順に並び、値は有限で、ハンドルは隣のキーまでの長さに収まる（書き換えは全部 `Curve` の関数を通る）。同じ時間のキーは値の段差になる。
- 作りと確認の表は [Tasks/Curve.md](Tasks/Curve.md)。

### 時間に沿って動かす（Timeline）

`.timeline` のファイルに、トラック → クリップ → キーの形で動きを書き、Entity の **Timeline** Component で再生する（Add Component → Animation。`#include <MyEngine/Systems/Animation/TimelineSystem.h>`）。同じファイルを何体でも同時に再生できる。

```cpp
TimelineSystem::Play(entity);       // 止まっていれば最初から、一時停止中なら続きから
TimelineSystem::Pause(entity);
TimelineSystem::Stop(entity);       // 時間を 0 に戻して、0 の値を書く
TimelineSystem::Seek(entity, 1.5f); // イベントは呼ばない
const TimelineStatus status = TimelineSystem::GetStatus(entity); // state・time・duration・loopCount
```

- **値のトラック：** `component`（Inspector の見出し）と `property`（シーンファイルの項目の名前）で、Component の数の項目を `Curve` で動かす。Script の `Fields` も動かせる。
- **イベントのトラック：** 指定の時間に、対象の Entity の Script の `OnTimelineEvent(const TimelineEvent&)` を呼ぶ。シークでは呼ばない。
- **対象：** 再生する Entity から子の名前を `/` でつないだパス（`Panel/Title`。空なら自分）。見つからなければ、そのトラックだけ飛ばして警告を1回出す。
- **Time Mode：** Game はスロー・ヒットストップ・ポーズで止まる。Real はゲームの時間が止まっても動く。
- **新しい種類：** `TimelineTrackOf<クリップ>` を継承して `Evaluate` を書き、ゲーム側の `.cpp` に `TIMELINE_TRACK(型, "説明");`。手順と決まりは [Tasks/Timeline.md](Tasks/Timeline.md)。

### Collider（当たり判定の Component）

Inspector の **Add Component → Physics → Collider** で付ける（コードなら `EntityManager::RequestAdd<ColliderComponent>(entity, collider)`）。形は球（Radius）・箱（Size）・カプセル（Radius・Height）・多角形（Polygon：同じ Entity の Polygon Shape の点。[8. 描画](#横から見た形の足場polygon-shapepolygon-renderer)）で、Entity の Scale（親の分も）が掛かる。エディタの Scene / Game ビューに緑の線で出て（Release では出ない）、重なると赤になる。**押し戻しはしない**（重なったことを知らせるだけ）。

当たったかどうかは、SYSTEM の中で **`CollisionSystem::GetHits(entity)`** を読む（`GameComponent.h` だけで使える）。

```cpp
#include <MyEngine/ECS/GameComponent.h>

// ===== 弾：まっすぐ進む =====
struct Bullet {
	Vector3 velocity = {0.0f, 0.0f, 10.0f}; // 1秒に進む量
};
COMPONENT(Bullet, "Shooter") { ui.Field("Velocity", value.velocity); }

void BulletMove(Bullet& bullet, TransformComponent& transform, float deltaTime) { transform.translation += bullet.velocity * deltaTime; }
SYSTEM(BulletMove);

// ===== 敵：弾が当たるとHPが減り、0で消える =====
struct Enemy {
	int hp = 3;
};
COMPONENT(Enemy, "Shooter") { ui.Field("HP", value.hp); }

void EnemyHit(Handle<Entity> entity, Enemy& enemy) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		// 当たり始めだけ数える（Stay で数えると、重なっている間ずっと減る）
		if (hit.phase != HitPhase::Enter) {
			continue;
		}
		// 相手が弾のときだけ（相手が何者かは、持っているComponentで見分ける）
		if (EntityManager::Get<Bullet>(hit.other) == nullptr) {
			continue;
		}
		EntityManager::Destroy(hit.other); // 弾は消す
		--enemy.hp;
	}
	if (enemy.hp <= 0) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(EnemyHit);
```

| `hit.phase` | いつ来るか | 使いどころ |
|---|---|---|
| `HitPhase::Enter` | 重なり始めたフレームに1回 | 弾が当たった・アイテムを取った |
| `HitPhase::Stay` | 重なっている間、毎フレーム | 床のダメージ・範囲の中にいる間 |
| `HitPhase::Exit` | 離れたフレームに1回。相手が消えたときも来る（そのとき `EntityManager::Get(hit.other)` は `nullptr`） | 範囲から出た |

- `hit.self` は自分、`hit.other` が相手。1組の当たりは、両方の Entity に1件ずつ届く。
- `hit.normal` は**相手から自分へ押し出す向き**（長さ1。ぶつかった面の向き）。相手の上に乗っていれば、だいたい (0, 1, 0)。吹き飛ばす向き・踏んだか（`normal.y > 0.7` など）に使う。Exit は離れる前のフレームの向き。
- 相手が何者かは、**相手が持っている Component で見分ける**（タグ・レイヤーは無い）。
- 読めるのは**前のフレームの判定結果**（1章の順番の 9）。停止中も判定しているので、線の色で重なりが分かる。
- 1つの Entity に Collider は1つ。形を組み合わせたいときは、子の Entity に付ける。
- 箱は Entity を回しても回らない（軸にそろった箱＝AABB）。Center のずれは回る。
- カプセルもいつも立ったまま（Y の向き）。Height は下の端から上の端まで（半球も入れる）で、半径の2倍より小さいと球になる。プレイヤー向け（箱より段差の角で引っかからず、球より縦長にできる）。
- 多角形（Polygon）は、横から見た形を Z へ押し出した形。横の面・前の面・後ろの面の一番近い所から押し出す（Freeze Position Z の物は X・Y だけ）。球・カプセルとだけ当たり、箱・多角形どうしは当たらない。坂の上で止まりたいプレイヤーは、`hit.normal` から坂の向きを出して、坂に沿った速さにする（AL4_01 の `IceCube`）。

### だめな例：`Enter` を見ずに数える

```cpp
// だめ：重なっている間（Stay）も毎フレーム来るので、HP がみるみる減る
for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
	if (EntityManager::Get<Bullet>(hit.other)) {
		--enemy.hp;
	}
}
```

→ `if (hit.phase != HitPhase::Enter) { continue; }` を先に書く。

### Script で当たりを受け取る（OnCollision〜・OnTrigger〜）

Script なら、`GetHits` を読まずに、エンジンが関数を呼んでくれる（Unity と同じ名前）。どちらかが Is Trigger なら `OnTrigger〜(GameObject other)`、どちらも違えば `OnCollision〜(const CollisionInfo& collision)`。

```cpp
// 図形（動く Rigidbody）にぶつかったら、ぶつかった面の向きへ飛ばされる
void PlayerController::OnCollisionEnter(const CollisionInfo& collision) {
	if (collision.gameObject.GetComponent<RigidbodyComponent>() == nullptr) {
		return; // 床・壁（Rigidbody が無い）
	}
	if (RigidbodyComponent* rigidbody = GetComponent<RigidbodyComponent>()) {
		rigidbody->velocity = collision.normal * 8.0f; // normal は相手から自分へ（長さ1）
	}
}
```

- `collision.gameObject` が相手、`collision.normal` が `hit.normal` と同じ向き。
- Enter・Stay・Exit の分け方は `hit.phase` と同じ（Stay は触れている間ずっと。床に乗っている間も来る）。
- `collision.selfVelocity`・`collision.otherVelocity`：ぶつかる直前の自分・相手の速さ（物理が押し戻す前。跳ね返って一瞬で離れた組も Enter が来る）。「どちらがぶつかってきたか」は `相手の重さ * Dot(collision.otherVelocity, collision.normal)`（相手の勢い）、ぶつかった強さは `collision.GetImpactSpeed()`（G4Bounce.md）。
- 跳ね返り・摩擦は Collider の **Physic Material**（`.physmat`。Project の Create → Physic Material で作り、Project で選ぶと Inspector で直せる）。2つの値は混ぜ方（Average・Minimum・Multiply・Maximum）で1つにする。1m/s より遅いと跳ねない（G4Bounce.md）。

### 画像を使わない UI の図形（Vector Shape）

Hierarchy の ＋ → Vector Shape（Add Component → UI → Vector Shape）。四角形・角丸四角形・円・楕円・リング・平行四辺形を、シェーダーで描く（VectorShape.md）。置き方は Image と同じ（Translation の x・y がピクセル・Anchor・Pivot・Order・Blend。Image・Text と Order で重なり、Alpha Group も効く）。

- **Style**：`Fill`（塗り）・`FillAndStroke`（塗りと縁取り）・`Stroke`（線だけ）。縁取りの太さと角の丸みは Size を変えても変わらない。
- **Gradient**：`Linear`・`Radial`。Color から Gradient Color へ。Range で色が変わる範囲を決める。
- **Gauge**：`Linear`・`Radial`。Amount（0〜1）で見せる割合を決める。塗りと縁取りを一緒に切るので、背景の枠は別の Vector Shape を後ろに置く。
- **Transform の Scale**：Size は x・y それぞれ、丸み・縁取り・リングの太さは x・y の小さい方の倍率が掛かる。
- 値は Script から毎フレーム書いてよい（バッファを作り直さない）：

```cpp
if (VectorShapeComponent* shape = GetComponent<VectorShapeComponent>()) {
	shape->gaugeAmount = value / maxValue;
}
```

- ボタンは `VectorShapeSystem::IsMouseOver(entity)`（Size の四角で調べる）。

### ゲームの画面の比率と黒帯（GameScreen）

`main.cpp` の `WindowConfig` で決める。書かなければ 1280×720（GameScreen.md）。

```cpp
config.width = 540;       // ウィンドウの大きさ
config.height = 960;
config.gameWidth = 720;   // ゲームの画面（縦横比と UI のピクセルの基準）
config.gameHeight = 1280;
```

- UI の位置・大きさは `gameWidth`×`gameHeight` のピクセルで置く。カメラの縦横比もこの値。
- ウィンドウの比率が違うと、製品版は上下か左右に黒帯が出る（絵は伸びない）。エディタの Game・Scene ビューも比率を保つ。
- 帯の色は `RenderWindow::kBarColor`。クリアの色 `kClearColor` とは別の値。
- `InputManager::GetMousePositionInGame` は黒帯の内側を画面として数える。黒帯の上では `IsMouseInGame` が false。
- 起動の後は変えられない。コードから読むときは `GameScreen::GetWidth()`・`GetHeight()`・`GetAspectRatio()`。

### 画面に文字を出す（Text）

Hierarchy の ＋ → Text（Add Component → UI → Text）。置き方は Image と同じ（Translation の x・y がピクセル・Anchor・Pivot・Order）。Script から書き換える：

```cpp
if (TextComponent* text = scoreText_.GetComponent<TextComponent>()) {
	text->text = std::format("SCORE {:06}", score_); // text は FixedString<256>（"..."・std::string を入れられる。読むときは text->text.View()）
}
```

- フォントは Inspector で選ぶ（見本つき。`resources/` の下と Windows のフォント）。(none) はメイリオ。縁取り（Outline Width）・影（Shadow）。
- 大きさが要るときは `TextRenderSystem::CalcTextSize(entity)`。仕組みは Text.md。

### その場で調べる（Raycast・SweepSphere）

`GetHits` は「フレームの最後に1回作った重なりの一覧」。こちらは **SYSTEM の中で何回でも、その場で**聞ける（`GameComponent.h` だけで使える）。

```cpp
// 相手は「持っている Component」で絞る（レイヤーは無い）。空なら全部
bool IsWall(Handle<Entity> entity) { return EntityManager::Get<Wall>(entity) != nullptr; }

// レイを飛ばして、一番近い Collider（壁で線を止める）
RaycastHit hit;
if (CollisionSystem::Raycast({from, direction}, maxDistance, hit, IsWall)) {
	// hit.entity・hit.point（当たった位置）・hit.distance（始点からの距離）
}

// 球を start から end まで動かした跡に触れた Collider を全部（速く動く攻撃。1フレームで飛び越えてもすり抜けない）
std::vector<Handle<Entity>> touched;
CollisionSystem::SweepSphere({start, end, 0.5f}, touched, IsEnemy); // {始点, 終点, 半径}
for (const Handle<Entity> enemy : touched) {
	EntityManager::Destroy(enemy);
}
```

- 調べる相手の形は、**前のフレームに集めた物**（1章の順番の 9。ギズモの線と同じ）。動かない壁なら気にしなくてよい。
- **Play・シーン切り替えの直後の1フレームは、何にも当たらない**（作り直す前の形しか無い）。
- レイの向きは長さ1でなくてよい。始点が Collider の中なら距離0で当たる。多角形（Polygon）にも当たる（前の面・後ろの面・横の面）。
- 動いた跡（カプセル）は、Collider と同じく「線分の上で相手に一番近い点の球」にして調べる（箱の角でも正確）。
- 速く動く物の攻撃を Collider（`GetHits`）でやると、**1フレームで相手を飛び越えてすり抜ける**。動いた跡（`SweepSphere`）で調べる。

### 弾む面（Elastic Surface）

**Add Component → Physics → Elastic Surface**（`ElasticSurfaceComponent`）。トランポリンのように、乗った物を沈めて跳ね返す板。面の真ん中は付けた Entity の位置で、横（X）に Width・奥行き（Z）に Depth だけ広がる（回転・大きさは使わない）。厚み（Thickness）と両端の支柱（Post Height）は見た目だけで、当たるのは上の面。

- **乗れるのは Rigidbody と Collider（Is Trigger でない物）を付けた Entity**。球の Collider は面に一番近い点、箱の Collider は底に並べた点、カプセルの Collider は下の端の球で押し返される。坂の上の物は、低い方（くぼみ）へ押される。
- Rigidbody の **Mass**（kg）が重いほど深く沈め、周りの物を大きく跳ね上げる。Mass は Rigidbody 同士の押し合いにも使う（14章の上の囲み）。
- **Edge**（Edge Stiffness・Edge Damping・Edge Falloff）で、端ほど沈みにくく・跳ね返りを弱くできる（初期値は0＝無し）。値の目安は `EdgeStiffness.md` の3章。
- 面の計算は PhysicsSystem の小さな1歩（Steps Per Second。初期値 1/1200 秒）の中で、Rigidbody と一緒に進む。再生中だけ動き、停止中はいつも平ら。
- 調整の目安（Tension・Mass Per Meter・Damping など）は `G1Trampoline.md` の4章。
- 見た目は **Surface Material**（板）と **Posts**（支柱）に、Primitive Renderer と同じ Material の項目がある（テクスチャ・Shading・光沢・金属感）。テクスチャは **m 単位**で貼る（UV Scale 2 なら 2m 四方に1枚。上の面・壁・支柱のどこでも同じ大きさ）。

```cpp
// ゲームの Script・SYSTEM から（GameComponent.h だけで使える）
const Handle<Entity> floor = ElasticSurfaceSystem::FindSurface(x);       // x の上下にある面（無ければ無効な Handle）
const float floorY = ElasticSurfaceSystem::GetHeight(floor, x);          // その位置の面の高さ（ワールドの Y）
const float floorVelocity = ElasticSurfaceSystem::GetVelocity(floor, x); // 面の上下の速さ（戻る勢いに合わせて跳ぶ）
ElasticSurfaceSystem::AddImpulse(floor, x, -20.0f, 2.0f);                // 一瞬で叩く（N・s。下向きは負）
```

- `AddImpulse` で面の真ん中が変わる速さは、だいたい **力積 ÷（Mass Per Meter × 半径）**（Mass Per Meter 1・半径 2m で −20 なら約 −10m/s）。大きすぎると面が乗っている物より先に沈み、戻るときに追い越して、物が**面を突き抜ける**。

### 風（Wind Zone）

**Hierarchy の ＋ → 3D Object → Wind Zone**、または **Add Component → Physics → Wind Zone**（`WindZoneComponent`）。範囲の中の Rigidbody を、風の向きへ押す。

| 項目 | 中身 |
|---|---|
| 向き | Entity の右（ローカルの +X）へ吹く。Rotation の Z で回す（90 で上へ） |
| Size | 範囲の大きさ（m。Entity から見た箱。Scale も掛かる。Entity と一緒に回る） |
| Speed | 風の速さ（m/s）。物はこの速さまで流される。それより速い物は、それ以上押さない（止めもしない） |
| Strength | 風の速さまで速くする強さ（m/s²）。上向きの風で浮かせるなら、重力（Settings の Gravity × Gravity Scale）より大きくする |
| Gust・Gust Frequency | ゆれ（0.3 なら速さが ±30%）と、ゆれる速さ（1秒に何回くらい） |
| Line Amount | 画面の風の線の濃さ（0〜1）。**カメラが追っている物（Camera Follow の Target）が範囲の中にいる間だけ**、0.3 秒で出て、出たら 0.5 秒で消える。Game ビューだけ（Lens のパス） |

- 範囲と向きの線は、Collider のギズモと一緒に出る（Scene ビュー）。
- 押されるのは **Rigidbody の Wind Scale**（初期値 1）の倍率。0 なら押さない。Script で速さを毎フレーム目標へ近づけている物（プレイヤーなど）は、押した分がすぐ消されるので、Wind Scale を 0 にして、自分で風を読む：

```cpp
#include <MyEngine/Systems/Physics/WindZoneSystem.h>

// その位置の風（重なった範囲は足す。範囲の外なら全部 0）
const WindZoneSystem::WindSample wind = WindZoneSystem::Sample(GetWorldPosition(transform));
if (wind.IsBlowing()) {
	// wind.velocity：風の速さ（向き × 速さ。ゆれ込み）、wind.strength：近づける強さ（m/s²）
	targetSpeed += wind.velocity.x; // 例：走る目標の速さに足す（追い風で速く・向かい風で遅く）
}
// エンジンと同じ押し方（風の向きの速さが、風の速さより遅い間だけ速くする）
rigidbody->velocity = WindZoneSystem::Push(wind, rigidbody->velocity, Time::GetDeltaTime());
```
- Sample は前のフレームの終わりの範囲を使う（Script の Update は WindZoneSystem::Update より前）。

### 範囲に入ったら出す（Trigger Activator・Alpha Group）

**Hierarchy の ＋ → 3D Object → Trigger Activator**（Is Trigger の箱の Collider ＋ `TriggerActivatorComponent`）、または **Add Component → Gameplay → Trigger Activator**。同じ Entity の Collider の範囲に入ったら・出たら、**Target**（別の Entity）を出す（有効にする）・消す（無効にする）。チュートリアルの説明・看板に。Script は要らない（G9aTriggerActivator.md）。

| 項目 | 中身 |
|---|---|
| Target | 出す・消す Entity（文字と背景をまとめた親を入れると、子も一緒）。自分と自分の親は選ばない |
| Source | 誰が入ったら反応するか。`FollowTarget`（メインカメラの Camera Follow の Target＝プレイヤー）・`Entity`（Source Entity。子の Collider も数える）・`AnyRigidbody` |
| On Enter・On Exit | 入ったとき・出たとき。`Show`（出す）・`Hide`（消す）・`None` |
| Once | 入ったとき・出たときの動きを、それぞれ1回だけ |
| Hide On Start | 再生を始めたら、すぐ Target を消す（初期値オン。エディタでは見えたまま置ける） |
| Fade In Time・Fade Out Time | 見えきる・消えきるまでの秒数（0 ならすぐ）。消えきったら無効にする |

- 動くのは再生中だけ。Stop で、Play を押した瞬間の状態に戻る。入った・出たは、毎フレーム「Enter・Stay の当たりに相手がいるか」を前のフレームと比べて決める（相手が消えても「出た」になる）。
- フェードは Target の **Alpha Group**（`AlphaGroupComponent`。Add Component → UI → Alpha Group）の Alpha を動かす。無ければ再生中だけ付く。
- **Alpha Group** は、自分と子の Image・Text・Sprite Renderer の色の a に Alpha を掛ける（親の Alpha Group とも掛け合わせる。Unity の Canvas Group と同じ考え）。Script から子をまとめてフェードするのにも使える：

```cpp
if (AlphaGroupComponent* group = GetComponent<AlphaGroupComponent>()) {
	group->alpha = MathUtility::MoveTowards(group->alpha, 0.0f, Time::GetDeltaTime() / 0.5f); // 0.5 秒で消す
}
```
- 描くときの値は `SpriteRenderSystem::CalcGroupAlpha(entity)`（自分と親を全部たどって掛けた値）。モデル・Primitive Renderer には効かない。Sprite Renderer の Alpha Clip は、半分より透明になると急に消える。

### 画面（UI Screen）：ポーズ画面・クリア画面

`resources/ui/<名前>.uiscreen`（中身は Prefab と同じ形。全部のシーンで使える）。作り方は `G9dUIScreen.md` の 6章。

1. **作る**：Project ウィンドウの右クリック → **Create → UI Screen**。名前を打つ。
2. **直す**：ダブルクリック（Inspector の **Edit UI Screen**）で編集モード。Scene ビューに水色の枠（1280×720 の画面）、Hierarchy は画面の中身だけ。後ろのシーンは見えるが選べない。**Done** で保存して閉じる・**Cancel** で保存せずに閉じる。Save（Ctrl+S）はシーンと画面の両方。Play を押すと先に Done と同じことをする。
3. **出し方・消し方**：一番上の Entity の **UI Screen** Component（Pause Game・Show／Hide の Motion：None / Fade / Slide / Scale、Side・Distance・With Fade・Duration・Ease・Stagger）。動くのは一番上の**直下の子**1つずつ（Hierarchy の上から順に Stagger 秒ずつ遅れる）。子に **UI Screen Element** を付けると、その子だけ動き方を変えられる。
4. **使う**：
```cpp
UIScreen::Open("Pause");   // 出す（停止中・もう出ている・ファイルが無いときは false。見え始めるのは次のフレーム）
UIScreen::Close("Pause");  // 消す（消す動きが終わったら Entity を消す。消し始めたらすぐゲームが動く）
UIScreen::IsOpen("Pause"); // 出ているか（消している途中は false）
```
- Pause Game の画面を出している間は `Time::SetGamePaused(true)`。**ポーズ中も、開いている画面の中の Script は呼ばれる**（項目を選ぶ Script を画面の中に置く。AL4_01 の `PauseMenu`）。
- シーンが変わると一緒に消える（開いたまま次のシーンへ持っていくのは、まだ無い）。


## 15. パーティクル・軌跡

### パーティクル（Particle System）

小さな板をたくさん出して、動かして消す Component（Unity の Particle System の小さい版）。Hierarchy の **＋ → Effects → Particle System**、または Inspector の **Add Component → Effects → Particle System** で付ける。**Component は「どう出すか」の設定だけ**で、粒は `ParticleSystem` が持つ（Trail と同じ。粒は保存されず、Play / Stop で消える）。停止中も Inspector の **▶ Preview** で流して調整できる（選ぶ物を変えると止まる）。作り方と中身は `ParticleSystem.md`。

| 見出し | 項目 | 中身 |
|---|---|---|
| Playback | Play On Start・Looping・Duration | Play で流し始める（外すと `ParticleSystem::Play` まで出さない）／Duration 秒ごとにくり返す（Burst もまた）／1回の長さ |
| Emission | Rate Over Time・Burst Count・Max Particles | 1秒に出す数／1回の始めにまとめて出す数／同時に出ている数の上限（4096 まで。いっぱいの間は出さない） |
| Shape | Shape・Radius・Cone Angle・Box Size・From Surface | Point（全部の向き）・Sphere・Circle（**XY の面**。横から見る輪）・Cone（+Y へ開く）・Box。From Surface で表面（縁）から。向きは真ん中から外へ。Entity の向きで回る |
| Start (Min, Max) | Lifetime・Speed・Size・Color A・Color B・Intensity | 左と右の間でランダム（Size は m で板の1辺）。色は A と B の間。Intensity は全部の粒の明るさの倍率（1 より大きいとブルームで光る） |
| Over Lifetime | Gravity・Drag・End Size Rate・End Color・Ease | 加速（m/s²）／1秒あたりの減速の割合／最後の大きさ（初めに掛ける。0 で縮んで消える）／最後の色（初めに掛ける。A 0 でフェードアウト）／変わり方 |
| Rotation | Start Rotation・Angular Velocity・Random Spin Direction・Align To Velocity・Stretch | 画面の上で板を回す（度。**正で左回り**＝Transform の Rotation Z と同じ）。初めの角度・回る速さ（度/秒）は左と右の間でランダム／半分の粒を逆に／絵の右（+X）を進む向きへ／絵の横だけ伸ばす（Align と合わせて線）。作り方は `ParticleRotation.md` |
| Renderer | Texture・Blend・Space・Use Fog・Use Depth Of Field | 無ければ**丸いぼかし**（白。色は Color で）／Add（光る）・Normal（奥から順には並べない）／World（出た所に残る）・Local（Entity と一緒に動く）／霧を混ぜるか（初めは ✓）／被写界深度でぼかすか（初めは外す。ぼけ方は粒の後ろにある物の距離で決まるので、奥の層の葉・湯気に） |

```cpp
#include "MyEngine/Systems/Effects/ParticleSystem.h" // GameComponent.h を include していれば要らない

// 子の「Burst」（Play On Start・Looping を外した、使ったら飛び散る物）を最初から
GameObject burst = gameObject.FindChild("Burst");
if (burst) {
	ParticleSystem::Play(burst.handle);       // 最初から（Burst も出る）。出している途中で呼んでも最初から
}
GameObject sparkle = gameObject.FindChild("Sparkle");
ParticleSystem::Stop(sparkle.handle);         // 出すのをやめる（出ている粒は寿命まで残る）。無い（無効な Handle）なら何もしない
ParticleSystem::Stop(sparkle.handle, true);   // 出ている粒もすぐ消す
ParticleSystem::Emit(burst.handle, 10);       // 今すぐ 10 個（Play していなくても、Component の値で出る）
if (!ParticleSystem::IsAlive(burst.handle)) { … } // 出している途中でも、粒が残ってもいない（全部消えた）
```

- 1つの Entity に Particle System は1つ。キラキラと飛び散るの2つが要る物は、子を2つ作る（`FindChild` で探す）。
- Prefab から作った同じフレームに `Play` してもよい（付くのは次のフレームの頭だが、覚えている）。`Emit` は Component が付いてから。
- ゲームが動き出した最初のフレーム（製品版の起動・エディタの Play。Script の `Start`）に `Play`・`Emit` してもよい（`ParticleRotation.md` のパート A の後。前は消えていた）。
- 粒は `Time::GetDeltaTime` で動く（ポーズ・ヒットストップで止まる）。Use Depth Of Field を外した粒は被写界深度の後に描く（ぼけない）。

### 前からのパーティクル（ParticleManager・ParticleEmitter）

コードでグループを作って粒を1個ずつ登録する、前からの仕組み（CG3_Project が使っているので残してある）。**新しく作る物は Particle System を使う。**

```cpp
// グループを作る（Initializeで1回）
ParticleGroupSetting setting;
setting.textureHandle = TextureManager::Load("resources/particle.png");
groupHandle_ = ParticleManager::CreateGroup(setting);

// 出す
Particle p;
p.transform.translation = {0.0f, 1.0f, 0.0f};
p.velocity = RandomEngine::GetInsideUnitSphere();
p.lifeTime = 1.0f;
ParticleManager::Register(groupHandle_, p);
```

1グループ最大4096個。カメラはエンジンが毎フレーム渡す。**シーンを入れ替える（Play / Stop・`LoadScene`）と、グループは全部消える**（`ParticleManager::ClearGroups`。エミッターはシーンのメンバに置く）。

### 軌跡（Trail）

通った跡・描いた線を、帯にして描く Component（Unity の Trail Renderer）。Inspector の **Add Component → Effects → Trail** で付ける。**Component は「どう描くか」の設定だけ**で、点の列は `TrailSystem` が持つ（Component に `std::vector` は入れられないので。AudioSource と鳴っている音の関係と同じ）。点は保存されず、停止中は持たない。

| 使い方 | 設定 | 例 |
|---|---|---|
| **動いた跡** | Emitting に印（動くたびに自動で点が増え、Life Time 秒で消える） | 剣の残像・高速移動の跡 |
| **描いた線** | Emitting を外し、Life Time 0（消えない）。点は `TrailSystem::AddPoint` で足す | マウスで描く線・地面に残るインク |

| 項目 | 中身 |
|---|---|
| Min Vertex Distance | 動いた跡で、この距離動くたびに1点 |
| Life Time | 点が消えるまでの秒（0 で消えない） |
| Width / End Width Rate | 新しい方の端の太さ／古い方の端の太さ（Width に掛ける。1 でずっと同じ） |
| Alignment | View：カメラの方を向く／Ground：地面に寝かせる（帯の面が真上） |
| Color・Intensity / Texture / Blend | 色（光の当たり方は使わない）と明るさの倍率／U が古い方 0 → 新しい方 1、V が幅の向き／Add で光る線 |

```cpp
// 描いた線：線1本＝Entity 1つ。RequestAdd と同じフレームから AddPoint してよい
TrailComponent trail;
trail.emitting = false;
trail.lifeTime = 0.0f;
trail.alignment = TrailAlignment::Ground;
const Handle<Entity> line = EntityManager::Create("InkLine");
EntityManager::RequestAdd<TrailComponent>(line, trail);
TrailSystem::AddPoint(line, start);

// 点を読む（古い方 → 新しい方）・長さ・消す
for (const TrailPoint& point : TrailSystem::GetPoints(line)) { … } // point.position・point.age
const float length = TrailSystem::CalcLength(line);
EntityManager::Destroy(line); // 線ごと消す（点も消える）。点だけ消すなら TrailSystem::Clear(line)
```

- `GetPoints` の結果はその場で使う（`AddPoint` すると使えなくなる。最後の点が要るなら先に写す）。
- 線に触れているかは、点と点の間の線分ごとに `Collision::ClosestPointOnSegment` で見る（14章）。
- 使い方の全部（描く・壁で止める・線に沿って進んで斬る・拾う）は `InkSlash.md` の4章。

---

## 16. ログ・アサート・調整項目

```cpp
LogManager::Log("Push!");            // logs/日付.log に残る
LogManager::Warning("敵が多すぎる");
LogManager::Error("モデルが読めない");

MY_ASSERT_MSG(handle != 0, "モデルを読み込んでから呼んでください"); // Debugだけ。通知が出てログにも残る
```

数値の調整は `GlobalVariables`（Settings ウィンドウで触って、jsonに保存される）。**登録する関数と、読み戻す関数の2つを書く**のが決まり。

```cpp
// 登録（Initializeで1回）。Group → Category → Add をつなげて書く
void GameScene::RegisterGV() {
	auto gv = GlobalVariables::GetInstance()->Group("GameScene");
	gv.Category("Player").Add("Speed", playerSpeed_).Add("JumpPower", jumpPower_);
}

// 読み戻す（Updateで毎フレーム）
void GameScene::ApplyGV() {
	auto* gv = GlobalVariables::GetInstance();
	playerSpeed_ = gv->Get<float>("GameScene", "Player", "Speed");
	jumpPower_ = gv->Get<float>("GameScene", "Player", "JumpPower");
}
```

`Add` は**登録済みなら値を上書きしない**（jsonから読んだ値が生き残る）。`Category("Enemy").Category("Move")` と重ねると階層になり、`Get<float>("GameScene", "Enemy/Move", "Speed")` で引ける。

> **Inspectorで触れる物はComponentに入れる方がよい。** `GlobalVariables` は「Entityに紐づかない全体の設定」向け。

- **1グループ＝ Settings ウィンドウの1ページ**。左の一覧の見出しは Project（ゲーム全体。`Parameters/<グループ>/`）・Scene（開いているシーンの設定。SF4 から）・Editor（エディタだけ。`Parameters/Editor/<グループ>/`）。ゲームのグループは `Group("名前")` のまま（Project に出る）。エディタだけの物は `Group("名前", SettingsSection::Editor)`。**見出し（置き場所）は、そのグループを最初に作った `Group(...)` で決まる**（2回目からの `Group` は、もうあるグループを返すだけで、渡した見出しは使わない）。なので `SettingsSection` は、最初に呼ぶ所（ふつうは `Add` を並べる所）に書く。
- 保存していない変更があるページには `*`。ページの **Save** でファイルに書き、**Revert** で保存した値に戻す。
- Settings ウィンドウで変えた値も **Ctrl+Z / Ctrl+Y** で戻る（Inspector と同じく、つまんで離したら1件。Revert も1件）。ただしシーンには入っていないので、Play 中に変えた値は Stop しても戻らない。
- `Add` で項目を作ったときに、保存したファイルにその項目があればその値になる（シーンを切り替えて後から登録しても、保存した値から始まる）。
- **シーンの `Initialize` で `GlobalVariables::GetInstance()->LoadFiles()` を呼ばない**（全部のグループをファイルから読み直すので、Play を押すたびに、保存していない PostEffect・音量などが戻ってしまう）。

---

## 17. エディタ

| 操作 | 何ができるか |
|---|---|
| **Hierarchy** | Entityの一覧。右上の＋で作る（ライト・**カメラ**・**3D Object → Polygon・Arrow Strip**・**Folder**も）、右クリックでコピー/貼り付け/削除、ダブルクリック（F2）で名前変更、ドラッグで親子（**見た目の位置は変わらない**） |
| **Hierarchy の目** | 行の左の目を押すと、その Entity と子を **Scene ビューだけで隠す**（Game ビュー・ゲームの動きは変わらない。クリックでも選ばれない。保存しない）。斜線の目が隠している物、薄い斜線は親を隠したので隠れている物。描く物は1回だけ積んで、Scene ビューで `RenderQueue` が飛ばす（Config の `isSceneHidden`。G9bUIEditor.md） |
| **Hierarchy の並び** | 作った順（Entity を消しても、ほかの並びは変わらない。G9dUIScreen.md の 1.5）。並び替える機能は無い |
| **画面の編集（Edit UI Screen）** | `.uiscreen` をダブルクリック。Hierarchy は画面の中身だけ（一番上に Done・Cancel）、Scene ビューに水色の枠。後ろのシーンは見えるが選べない。中身は「シーンに保存しない」印（`Entity::isDontSave`）付き（G9dUIScreen.md） |
| **Hierarchy のフォルダ** | ＋ → **Folder**。整理するための入れ物（印の `FolderComponent` を付けた Entity）。中へはドラッグで入れる。Transform は原点のまま使う（Inspector に出さない・ギズモも出ない）。フォルダを入れられるのは一番上か、ほかのフォルダの中だけ。無効にすると中身ごと無効。フォルダの右クリック → Create Folder で中にフォルダ |
| **Inspector** | 選んだEntityのComponentを編集。下の **+ Add Component** で足す、見出しの右クリックで外す |
| **Transform の XYZ ボタン** | Position・Rotation・Scale の左。押すと X・Y・Z を一緒に変える（Position・Rotation は同じだけ足す、Scale は同じ倍＝形を保つ。0 からは同じだけ足す）。押している間は黄色。保存しない（エディタを閉じると外れる） |
| **Scene / Game** | Sceneは編集用（デバッグカメラ・ギズモ）、Gameは製品の絵。Polygon Shape の **Edit Shape**・Arrow Strip の **Edit Path** の間は、Scene ビューで点を動かす（移動のギズモは出ない） |
| **Scene ビューの UI** | Image・Vector Shape・Text をクリックで選び、**ドラッグで動かす**（1 ピクセルずつ。1回のドラッグで1件の履歴）。Vector Shape は枠の白い印をドラッグすると **Size が変わる**（反対の辺は動かない）。UI は 3D の物より先に選ぶ（重なっていたら手前に描いてある物）。UI には3D の移動のギズモは出ない。UI の後ろの物を選ぶときは **View → Scene View → Pick UI** を外す |
| **Play / Pause / Stop** | Playで `Update` が回り出す（**停止中に編集した状態のまま始まる**）。**Stopで Play を押した瞬間の状態へ戻る**（Play 中に動いた・作った・消した物は全部戻る。選んでいた Entity もそのまま） |
| **Restart** | 再生中だけ押せる。Play を押した瞬間からやり直す |
| **Save（Ctrl+S）** | Control ウィンドウ。停止中だけ。シーンファイル（`resources/scenes/〇〇.scene.json`）に書く。1つ前は `.bak` に残る |
| **カーブ（`.curve`）** | Project で選ぶと Inspector に出る。クリックで選ぶ・つまんで動かす、ダブルクリックでキーを足す（線の近くなら形を変えない）、Delete で消す、ホイールで拡大縮小（Ctrl：時間だけ、Shift：値だけ）、中ボタンか Alt+左ボタンで移動、F で全体。Ease Preset はキーを選んでいれば次のキーまでの区間に、選んでいなければ全体に入る。直すと自動で保存（1つ前は `.bak`） |
| **Timeline（`.timeline`）** | Project で Create → Timeline。選ぶと Inspector に長さとトラックの設定が出る（Add Track で種類を選ぶ）。クリップとキーはファイルを手で書く。直すと自動で保存（1つ前は `.bak`。Undo は無い） |
| **Undo / Redo** | Ctrl+Z / Ctrl+Y。作成・削除・名前・有効・親子・Componentの追加削除・Inspectorの値変更・**Settings（PostEffect など）の値変更**・**カーブの編集**が、やった順に戻る。Play・Stop・シーン切り替えで履歴は消える |
| **Project** | `resources/` のファイル。左にフォルダの木、右に中身（画像は小さい絵）。シーンはダブルクリックで開く。右クリックで Create（Folder / Scene）・Rename（F2）・Delete（ごみ箱へ）・Copy Path・Show in Explorer。右上の数字でアイコンの大きさ |
| **File** | New Scene（Ctrl+N）・Open Scene・Save（Ctrl+S）・Save As（Ctrl+Shift+S）。停止中だけ |
| **Settings（Edit → Settings）** | 調整項目（前の Parameters ウィンドウ）。Project：Audio（音量）・PostEffect・ゲームのグループ／Editor：Scene View（Scene ビューのカメラ）。ページごとに Save / Revert、保存していなければ `*` |
| **View → Gizmos** | ライトのアイコンと範囲、Collider の線（Colliders）の表示切り替え |
| **View → Scene View → Post Effects** | Scene ビューにもポストエフェクト（ブルームなど）を掛けるか（既定 ON）。Lens は Scene ビューでは掛けない（ギズモ・クリックの位置がずれるので） |
| **View → Scene View → Fog** | Scene ビューにも霧（Fog）を掛けるか（既定 ON）。Game ビューにはいつも掛ける |
| **View → Scene View → Depth of Field** | Scene ビューにも被写界深度を掛けるか（既定 ON）。Game ビューにはいつも掛ける |
| **View → Scene View → Grid・Game View → Grid** | グリッドを出すか。Scene は既定 ON、Game は既定 OFF（遊ぶときと同じ見た目にするため）。描く物は1回だけ積んで、ビューごとに `RenderQueue` が飛ばす（`DrawLayer::EditorGrid`） |
| **Control の Scene:** | 開いているシーンの名前と、保存するファイル。保存していなければ名前に `*`。再生中に `LoadScene` で進んだら「→ 今は Result」 |
| **Profiler → GPU → Stats** | Draw Calls・Frame Upload・**SRV Slots**（テクスチャの棚の使っている数。256個しかないので、Play / Stop を繰り返しても増え続けないことをここで見る） |

> **Save しないで終了すると、停止中の編集は消える**（Play → Stop では消えない）。Play 中の変更は Stop で戻るので、Play 中に見つけた良い値は、Stop してから入れ直して Save する（Unity と同じ）。

---

## 18. だめな例まとめ

| だめな書き方 | 何が起きるか | 正しくは |
|---|---|---|
| `Entity*` や `TransformComponent*` をメンバに持つ | 配列が詰め直されて別の物を指す | `Handle<Entity>` か `EntityId` で持つ |
| Componentに `std::string` / `std::vector` / ポインタ / 仮想関数 | Undo・コピーが壊れる（`static_assert` で止まる） | 固定長の値だけにする |
| `ForEach<T>` の中でその型を増減させる | assertで止まる | 予約（`RequestAdd` / `RequestRemove` / `Destroy`）を使う |
| `RequestAdd` の直後に `Get` | `nullptr` が返る | 次のフレームで取る。初期値は `RequestAdd` に渡す |
| 敵ごとの状態をグローバル変数に置く | 2体目で破綻する | Componentに入れる（Entityごとに1個ある） |
| `rotation` に度を入れる | とんでもない回転になる | ラジアン（`* kDegToRad`） |
| `worldMatrix` に自分で書き込む | 次のフレームに上書きされる | `translation` / `rotation` / `scale` を変える |
| `deltaTime` を掛けずに動かす | フレームレートで速度が変わる。ポーズで止まらない | `* Time::GetDeltaTime()` |
| `Update` で `Renderer::Draw***` を呼ぶ | 停止中に消える・順番が崩れる | `Draw()` の中で積む |
| 毎フレーム `ModelManager::Load` / `SoundManager::Load` | 無駄に遅くなる | `Initialize` で1回 |
| Componentのファイルを静的ライブラリに入れる | リンカに捨てられて登録が消える | ゲーム（exe）のプロジェクトに入れる |
| ヘッダで `std::numeric_limits<T>::max()` | `windows.h` の `max` マクロと衝突 | `<cfloat>` の `FLT_MAX` など |
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
| 古いパス（`MyEngine/Entity/`・`Component/`・`Light/`）をインクルードする | C1083（ファイルが無い） | 「1. ファイルの置き場所」の表のパス |
| アセットのファイル名に日本語を使う | 読めない（Inspector の一覧に出ない・Console に警告。前は選ぶと止まった） | 英数字にする |
| パスを文字にするのに `path.string()` / `generic_string()` を使う | 日本語の Windows では Shift-JIS になる（ImGui で文字化け・UTF-8 のつもりの所で読めない）。C++26 で非推奨 | `ToUtf8(path)`（`String/ConvertString.h`。UTF-8・区切りは `/`） |
| `GlobalVariables` の `SettingsSection` を、そのグループを最初に作る `Group(...)` 以外に渡す | 効かない（見出しは最初の `Group` で決まる）。Editor のつもりが Project の下に出て、`Parameters/<グループ>/` に保存される | 最初に呼ぶ `Group("名前", SettingsSection::Editor)` に渡す（`SceneFlow.md` の18章） |
| 物を掴む・乗せるのに `SetParent` を使う | 値がそのまま新しい親から見た位置になり、親の分だけ飛ぶ | `SetParentKeepWorld` |
| シーンの `GetCamera()` を上書きしたまま Camera Component を置く | 上書きしたカメラが使われ、置いた Camera が効かない | `GetCamera()` を上書きしない（カメラは Entity） |
| 直接鳴らす BGM を `SoundManager::Play(bgm, true)` で鳴らす | SE の音量つまみを通る（BGM の音量を下げても小さくならない） | 最後に `AudioBus::BGM` を渡す。Audio Source なら Bus を BGM に |
| Game ビューのクリック位置を `GetMousePositionInWindow` で取る | エディタではウィンドウ全体での位置（Game ビューの中ではない） | `GetMousePositionInGame()`（スプライトと同じ座標） |
| magic_enum で回す enum（Inspector のコンボ・保存・`enum_values` の for に使う物）の最後に、数える用の `Count` を足す | `Count` も「種類の1つ」として拾われる（コンボに出る・for で回る）。`SoundBus`（今の `AudioBus`）でこれをやり、配列の外を読んで起動できなくなった（`GameBasics.md` の11章） | `Count` は足さない。数は `magic_enum::enum_count<E>()` で取る |
| シーンの `Initialize` で `GlobalVariables::GetInstance()->LoadFiles()` を呼ぶ | Play・Stop のたびに、保存していない PostEffect・音量・Scene ビューのカメラがファイルの値に戻る | 呼ばない（起動のときにエンジンが読む） |
| 前の書き方（`std::make_unique<GameScene>()`）のまま、`GetSceneFile()` の上書きだけ消す | 名前が無いので、シーンファイルを使わないシーンになる（Entity が読まれない） | `main.cpp` を `Engine::Initialize(wc, "GameScene")` にして、`REGISTER_SCENE(GameScene)` を書く |
| シーンの名前（ファイル名）に日本語を使う | 読めない | 英数字にする（アセットと同じ） |
| スローに `Time::SetTimeScale` を使う | もう無い（前は毎フレームエンジンに 1 へ戻されて効かなかった） | `Time::SetGameTimeScale`。止めるのは `Time::HitStop` |
| 速く動く攻撃の当たりを `GetHits`（Collider の重なり）で見る | 1フレームで相手を飛び越えて、すり抜ける | `CollisionSystem::SweepSphere`（動いた跡で調べる） |
| 線の点の列を Component に `std::vector` で持つ | `static_assert` で止まる（Undo・保存が壊れる） | Trail Component を付けて、点は `TrailSystem::AddPoint` |
| `TrailSystem::GetPoints` の結果を持ったまま `AddPoint` する | 配列が動いて、持っていた方は壊れた所を指す | 要る点（最後の点など）を先に写してから `AddPoint` |

---

## 19. まだ無い物

**作っていない物を正直に書いておく（設計の穴ではなく、順番の問題）。**

| 無い物 | 今どうするか | 予定 |
|---|---|---|
| IBL・天球の保存 | Entity ではないので、`Initialize` のコードで作る（カメラは Camera Component になった）。クラスを書かないシーン（DefaultScene）はエンジンの既定の空 | シーンの設定（Settings ウィンドウの Scene）として SF4 で |
| 押し戻し・回る箱（OBB）・レイヤー | Collider は重なりを知らせるだけ。止める・戻すはゲームが書く（壁から押し出すのは `Collision::ClosestPointOnAABB` で30行くらい）。相手の種類は Component で見分ける（`Raycast` / `SweepSphere` の絞り込みも同じ） | 押し戻しは要るときに（Body として）。残りは [Tasks/Collision.md](Tasks/Collision.md) の「7. 未解決」 |
| 3Dの音の前後・ドップラー、キーの割り当て（名前で読む入力） | 3Dの音は大きさと左右だけ。入力はキー・ボタンを直接読む（移動は `GetMoveAxis`） | `GameBasics.md` の「10. 未解決」 |
| クオータニオン | オイラー角（ラジアン）。真上を向くと破綻する | 必要になってから（ファイルの版を上げて読み替える） |
| 物理エンジン | 当たり判定の関数だけ。押し戻しは自分で書く | 未定（PhysXは大きい） |
| プレハブ・コード無しの切り替え | 同じEntityを作る関数を自分で書く。切り替えは `SceneManager::LoadScene("名前")`（シーンを作る・開くのは Project ウィンドウでできる） | コード無しの切り替え（SF3）は `SceneFlow.md`。プレハブはその後 |
| アニメーション | 無い（モデルはノード階層まで） | [Tasks/Animation.md](Tasks/Animation.md)（設計済み。SceneFlow と S5 の後。ノードアニメ・Skeleton・Skinning は学校の範囲なので自分で書き、エンジンにつなぐ所を md で） |
| スプライト（絵を貼った板）・コマ送りアニメ | Model Renderer に板のモデルを入れる。コマ送りは無い | 要るときに（Sprite Renderer） |
| 2DのUIレイアウト | `Renderer::DrawSprite` に座標を直接渡す | 要るときに（UI Image） |
| 軌跡の頂点ごとの色・正射影のカメラ | 軌跡の薄くなり方はテクスチャで作る。正射影は透視の視野角を小さくして見せる | `InkSlash.md` の「7. 未解決」 |
| Componentの文字列データ | 入れられない（固定長のみ） | 必要になったら |
| Project ウィンドウから Inspector へドラッグ・名前を変えた／動かしたファイルの参照を直す | アセットは Inspector のコンボで選ぶ。Project で名前を変えた・消したファイルは、使っているシーンでは「見つかりません」になるので選び直す | その後（Audio.md の2章・SceneFlow.md の3章 SF2 の「後で」） |
| 閉じるときの「保存しますか？」 | ウィンドウの × で閉じると、保存していない編集は消える | 要るときに（別のシーンを開くときは聞く） |
| ビルドの高速化（PCH） | 1ファイル約2.5秒（ゲーム本編には関係しない）。**先に「複数プロセッサによるコンパイル（/MP）」をオンにする**（`SceneFlow.md` の13章） | 今はやらない。/MP の後でも待ちが気になったら（`GameBasics.md` の0章・10章） |
| 画面分割（カメラ2つを同時に描く） | 描くカメラは1つ（Priority の大きい物） | 要るときに |

---

## 関連

- [ARCHITECTURE.md](ARCHITECTURE.md) — なぜこの設計か（生ポインタ禁止、データと振る舞いの分離）
- `Entity.md` — Entity / Component の作り方と、書き味レイヤーの全コード
- `Serialize.md` — シーンの保存・読み込み、Play / Stop の仕組み、次の作業の順番
- [Tasks/Collision.md](Tasks/Collision.md) — Collider と当たりの一覧（S2）、Play / Stop を繰り返すと落ちた件（SRVスロット）の直し
- `Audio.md` — フォルダの整理（ECS / Components / Systems）、置き場所がずれるとどうなるか、AudioSource（S3）
- `GameBasics.md` — 親子の付け替え・Parameters の Undo の直し、音の種類ごとの音量・3Dの音・試し聞き、Camera Component、入力の穴、PCH は要るか、起動できなかったバグ（`Count`）の直しと質問6つの答え
- `SceneFlow.md` — シーンの切り替えをエディタで。SF0（小さな整理）・SF1（シーン＝ファイル・`LoadScene`・`REGISTER_SCENE`）・SF2（Project ウィンドウ）・SF2.5（Settings ウィンドウ）の写経コード、シーンの切り替えの仕組みの解説、ビルドが遅い理由、アイコンの画像の決まり、コード無しの切り替えの設計
- `InkSlash.md` — 課題のゲーム（AL4_01 の見下ろしインクスラッシュ）に要る物。時間のスローとヒットストップ・`ScreenPointToRay`・`Raycast` / `SweepSphere`・`DrawMesh`・Trail の写経コード、見つけたバグ3つ、ゲーム側の例（描く・進む・斬る・拾う）
- [Tasks/Animation.md](Tasks/Animation.md) — アニメーションで何ができるか・どう作るか・学校との分担。設計と順番
- [Tasks/Editor.md](Tasks/Editor.md) — Editorの責務、Undoの仕組み、決めたことの一覧
- [Tasks/Light.md](Tasks/Light.md) — ライトの整理の経緯
- [Tasks/Model.md](Tasks/Model.md) / [Tasks/Material.md](Tasks/Material.md) / [Tasks/PostEffect.md](Tasks/PostEffect.md) / [Tasks/FrameLoop.md](Tasks/FrameLoop.md) / [Tasks/Compute.md](Tasks/Compute.md)
