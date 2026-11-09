> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 2. ゲームの入口

`main.cpp` は基本これだけ。触るのはウィンドウの設定と最初のシーンだけ。

```cpp
#include <HTTEngine/Engine.h>

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	WindowConfig wc;
	wc.title = L"CG3";
	wc.width = 1280;
	wc.height = 720;
#ifdef USE_IMGUI
	wc.isImGui = true; // Editorを出すか（Debug・Development。Release は出さない）
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
	IBLEnvironment* GetEnvironment() override; // IBL を持つシーンは返す（エンジンが描く球・箱・足場の PBR に使う）
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

## シーンを切り替える（`LoadScene`）

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
- **今のシーンを最初からやり直す**のは `SceneManager::ReloadScene()`（やられた・R キー）。Play を押したシーンなら Play を押した瞬間の状態へ（エディタで保存していない編集も残る）、`LoadScene` で切り替えた先ならシーンファイルから読み直す。

### 幕を付けて切り替える（`SceneTransition`）
```cpp
SceneTransition::LoadScene("Level02"); // だんだん暗くする（0.4 秒）→ 切り替える → だんだん明るくする（0.4 秒）
SceneTransition::ReloadScene();        // 同じく、今のシーンを最初から
if (SceneTransition::IsBusy()) { ... } // 幕が動いている間（入力を受けない・2回頼まない）
```
- 呼び方・戻り値は `SceneManager::LoadScene`・`ReloadScene` と同じ（GameComponent.h だけで使える）。ゲームはふつう、こちらを使う。
- 幕は Entity ではなくエンジンが持つ（シーンを作り直しても消えない）。止めない時間で動く（ポーズ中でも動く）。幕が動いている間に頼まれた切り替えは断る（`false`）。Scene ビューには描かない。

## エディタでシーンを作る・開く（Project ウィンドウ）

| したいこと | 操作 |
|---|---|
| 新しいシーン | Project の何も無い所を右クリック → Create → Scene（`resources/scenes/` にでき、すぐ名前を打てる）。File → New Scene（Ctrl+N）でも |
| 開く | Project でシーンをダブルクリック。File → Open Scene でも。**停止中だけ**。保存していなければ「保存しますか？」 |
| 名前を付けて保存 | File → Save As（Ctrl+Shift+S）。写したシーンが開く |
| 保存していないか | Control ウィンドウと Project のシーンの名前に `*`。Undo で保存した時点まで戻すと消える |

- シーンの名前（ファイルの名前）に使えるのは**英数字と `_` `-` だけ**（日本語はまだ全部のアセットで読めないので、打てない）。
- **エディタは前回最後に開いていたシーンから始まる**（`Parameters/Editor/LastScene.json`）。`main.cpp` の `Engine::Initialize(wc, "GameScene")` は**製品版で最初に出るシーン**。編集したいシーンのために `main.cpp` を書き換えなくてよい。
- 作ったばかりのシーン（まだ Save していない）は、ファイルの中身が空。開くとカメラと平行光源から始まる。
