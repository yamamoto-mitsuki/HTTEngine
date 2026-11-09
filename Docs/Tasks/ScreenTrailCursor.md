参考：
- Unity の Cursor.visible（OS のカーソルを隠す。エディタでは Game ビューの上だけ）：https://docs.unity3d.com/ScriptReference/Cursor-visible.html
- Win32 の WM_SETCURSOR（マウスが動くたびに、ウィンドウにカーソルの形を聞いてくる）：https://learn.microsoft.com/ja-jp/windows/win32/menurc/wm-setcursor
- Win32 の SetCursor（null で隠す）：https://learn.microsoft.com/ja-jp/windows/win32/api/winuser/nf-winuser-setcursor
- Unity の TrailRenderer（Emitting・Autodestruct。持ち主を消すと尾も消えるので、Unity では尾を親から外してから消す書き方が多い）：https://docs.unity3d.com/ScriptReference/TrailRenderer.html
- Unreal の Niagara（エミッターの持ち主が消えた後の粒の扱いを選べる考え方）：https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-niagara-effects-for-unreal-engine
- `std::optional`：https://cpprefjp.github.io/reference/optional/optional.html
- 指示付き初期化子（`{.entity = entity}`。C++20）：https://cpprefjp.github.io/lang/cpp20/designated_initialization.html
- 前の md：Docs/Tasks/GameScreen.md（ゲームの画面の大きさ）・Docs/Tasks/UIWorldFollow.md（UI をワールドの点に重ねる）・Docs/Tasks/Polyline.md（UI の線）
- この作業のきっかけ：AL4_01 の Docs/AimShoot.md の「0. 決まったこと」の「エンジンに足りない物」1〜3

# 画面の変換・持ち主が消えた後の光の尾・OS のカーソルを隠す

ゲーム（AL4_01）の照準と弾で足りなかった3つを、エンジンに足す。

- **パートA 画面の位置と UI のピクセルの変換**（`Graphics/Renderer/GameScreen.h`）：`GameScreen::NdcToPixel`・`PixelToNdc`・`GetHalfSize` を公開し、エンジンの中の同じ式（Renderer・UI World Follow・SpriteRenderSystem・PolylineSystem）をこれにまとめる
- **パートB 持ち主が消えた後も、光の尾を消えきるまで描く**（`TrailComponent`・`TrailSystem`・`SceneManager`）：Trail に **Fade Out After Owner** を足す。持ち主（Entity か TrailComponent）が消えたら、TrailSystem が設定の写しで、点が寿命で消えきるまで描く
- **パートC OS のカーソルを隠す**（`InputManager`・`Win32Window`・`WindowManager`・`EditorViewport`・`SceneManager`）：`InputManager::SetCursorVisible(false)` で、ゲーム画面の上だけ Windows の矢印を隠す
- **パートD Sandbox で確かめる**：照準の見本（AimCursorSample）・3通りの尾の弾（TrailFadeShooter）と、テストシーン CursorTrailTest
- **パートE `Docs/Reference.md`**：1フレームの順番の表の TrailSystem の行

パートは **A → B → C → D → E の順に写す**。A・B・C はそれぞれ別の機能なので、どれの後でもビルドできる（D は A・B・C を全部使う）。

ブランチは `feature/ScreenTrailCursor`（develop f6038eb から）。**Docs/Tasks/FrameHeadTransforms.md を先に写す**（パートD の弾は、シーンを読んだ最初のフレームに撃つ。その直しが無いと、最初の3発が箱ではなく原点から出る）。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドでエラー・自分のファイルの警告 0。自動テスト（FrameHeadTransforms と合わせて Debug・Development 33 項目、Release 30 項目。一時停止の3項目はエディタだけ）が全部合格。その中に「変換が前の式とビットまで同じ」「離れた尾の最後の点は、消える前に描いた帯の先」「Fade On の尾は寿命 0.5 秒で消えきる」「シーンを変えたら離れた尾も捨てる」「ゲーム画面の上で OS のカーソルが本当に消える（`GetCursorInfo` が null）」「一時停止中・ゲーム画面の外では出す」がある。テストシーンを止めて撮り、3本の尾の残り方を目で確かめた。

## 0章 全体の形

### 0.1 何が困っていたか

1. **画面の変換が公開されていない**：画面の位置（真ん中 0・端が ±1。`InputManager::GetMousePositionInGame`・`Camera::ScreenPointToRay` の数え方）と、UI のピクセル（真ん中 0・上が＋。UI の Translation の数え方）の変換が、`Renderer` の private（`NdcToBasePixel`・`BasePixelToNdc`）と、UI World Follow の無名 namespace（`HalfScreen`）にしか無かった。ゲームは `GameScreen::GetWidth() * 0.5f` を自分で書いていた。エンジンの中にも同じ式が5か所あった
2. **弾が消えると、光の尾が一瞬で消える**：`TrailSystem::Update` の最初で、持ち主の Entity が消えた尾をそのフレームに全部捨てていた。弾（当たる・壁にぶつかる・着くと Destroy）では、長い尾が毎回パッと消える
3. **OS のカーソルを隠す仕組みが無い**：照準の UI を出しても、その上に Windows の矢印が重なって見える

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Graphics/Renderer/GameScreen.h` | `GetHalfSize`・`NdcToPixel`・`PixelToNdc` を足す |
| A | `Graphics/Renderer/Renderer.h`・`.cpp` | private の `NdcToBasePixel`・`BasePixelToNdc` を消して、GameScreen を使う |
| A | `Systems/UI/UIWorldFollowSystem.cpp` | `HalfScreen` を消して、GameScreen を使う |
| A | `Systems/Rendering2D/SpriteRenderSystem.cpp` | `AnchorPosition`・`CalcScreenPoint` で GameScreen を使う |
| A | `Systems/UI/PolylineSystem.cpp` | `MakeToScreen` で GameScreen を使う |
| B | `Components/Effects/TrailComponent.h` | `fadeOutAfterOwner` を足す |
| B | `Systems/Effects/TrailSystem.h`・`.cpp` | 持ち主から離す・離れた尾を古くして描く・`ClearAll` |
| B | `Scene/SceneManager.cpp` | シーンを変えたら `TrailSystem::ClearAll`（パートC の1行もここ） |
| C | `Input/InputManager.h`・`.cpp` | `SetCursorVisible`・`IsCursorVisible`・`ShouldHideCursor`・`SetGameViewHovered` |
| C | `Editor/Viewport/EditorViewport.cpp` | Game ビューの絵の上かを InputManager へ渡す |
| C | `Window/Win32Window.h`・`.cpp` | `SetCursorHidden` と `WM_SETCURSOR` |
| C | `Window/WindowManager.h`・`.cpp` | 毎フレーム、隠すかをメインのウィンドウへ渡す（`UpdateCursor`） |
| C | `Scene/SceneManager.cpp` | シーンを変えたら `SetCursorVisible(true)` |
| D | `Sandbox/Source/Scripts/CursorTrailSamples.h`・`.cpp`（新しい） | AimCursorSample・TrailFadeShooter・TrailFadeBullet |
| D | `Sandbox/Resources/scenes/CursorTrailTest.scene.json`（新しい） | テストシーン |
| D | `Sandbox/Sandbox.vcxproj`・`.filters` | 上の2つを足す |

### 0.3 使い方

**A 画面の変換**（GameScreen。どこからでも呼べる）

```cpp
// 照準の画面の位置（端が ±1）→ UI の Translation（Anchor は Center）
const Vector2 pixel = GameScreen::NdcToPixel(screenPosition);
transform.translation.x = pixel.x;
transform.translation.y = pixel.y;

// 右スティックで動かしたピクセルの「量」→ 画面の位置の「量」（真ん中が 0 なので、量にも同じ関数が使える）
screenPosition += GameScreen::PixelToNdc(stick * (padSpeed * Time::GetRealDeltaTime()));

// 画面の真ん中から端までのピクセル（端から内側へ空ける計算など）
const Vector2 half = GameScreen::GetHalfSize();
```

**B 光の尾**：Inspector の Trail に **Fade Out After Owner**（既定 true）が増える。

| Fade Out After Owner | 持ち主（Entity かこの Component）が消えたとき |
|---|---|
| true（既定） | 尾はその場に残り、点が Life Time で古くなって消えきるまで描く。新しい点は足さない |
| false | 消えたフレームに捨てる（前と同じ） |

- 弾は何も書かなくてよい：Trail を付けて Destroy するだけで、尾が自然に消える
- 次の尾は true でも残さない：**Life Time 0**（消えないので、いつまでも残ってしまう）・**Enabled を外していた**・**持ち主が無効（SetActive(false)）だった**（描いていなかった尾が急に出てこないように）
- Entity は残して **Trail だけ外した**（RemoveComponent）ときも、Entity を消したのと同じ。同じ Entity に Trail を付け直すと、新しい尾は空から始まる（古い尾は離れたまま消えていく）
- 一時停止・スローの間は、離れた尾も持ち主のいる尾と同じく止まる・ゆっくり古くなる（`Time::GetDeltaTime`）
- **シーンを変えたら**（LoadScene・NextScene・作り直し）、離れた尾も全部捨てる。**Stop** したら、今までどおり全部捨てる
- 離れた尾は誰の物でもないので、`TrailSystem::GetPoints`・`CalcLength`・`Clear` からは見えない

**C OS のカーソル**

```cpp
// 照準を出すシーンの Start などで（ゲーム画面の上では、照準がカーソルの代わり）
InputManager::SetCursorVisible(false);
```

- 隠れるのは**マウスがゲーム画面の上にいる間だけ**。エディタは **Play 中**に **Game ビューの絵の上**（ほかの窓が重なっている所は出す）。**一時停止中・停止中は出す**。製品版は黒帯の内側
- **シーンを変えると（Stop も）true に戻る**。隠したいシーンが、自分で頼む（タイトルへ戻ったら矢印が出ない、という戻し忘れが起きない）
- `IsCursorVisible()` は頼んだ値（ゲーム画面の外では false でも出ている）
- ゲームのポーズ画面などで矢印を出したいときは、ゲームが `SetCursorVisible(true)` を呼ぶ（エンジンはゲームのポーズを見ない）

### 0.4 決めたこと（2026-11-07 に相談して決めた）

| 項目 | 決まり | 理由 |
|---|---|---|
| 変換の置き場所 | `GameScreen` | GameScreen は「UI の位置・大きさのピクセルの基準を決める」所（ヘッダの頭のコメント）。大きさを決めている所が、その大きさを使う変換も持つ。Input・Camera・UI・Renderer のどれからも読める |
| 変換の形 | `NdcToPixel`・`PixelToNdc` の1組で、位置にも量にも使う | どちらも真ん中が 0 なので、位置と量は同じ式になる。名前を分けると、中身が同じ関数が2組になる（1.1） |
| 尾を残すか | Trail ごとに選ぶ（`fadeOutAfterOwner`）・既定は残す | 一瞬で消したい尾（瞬間移動で前の体の尾を残したくない等）だけ外す。今あるシーンの Trail も、消えるとき自然に消えるようになる |
| Trail だけ外したとき | Entity が消えたのと同じ | 「TrailComponent が無くなったら、持ち主から離れて消えきるまで描く」の1つの決まりになる。点を足すのを止めるだけなら Emitting、すぐ見えなくするなら Enabled がある |
| シーンを変えたとき | 離れた尾も捨てる | 点は前のシーンのワールドの位置なので、新しいシーンへ持ち越さない（`ParticleManager::ClearGroups` と同じ所・同じ考え） |
| 描く設定の残し方 | TrailSystem が TrailComponent を丸ごと写して持つ | TrailComponent は trivially copyable（`static_assert` がある）なので、写すのは安い。持ち主のいる間は毎フレーム写し直す |
| カーソルの窓口 | `InputManager::SetCursorVisible` | ゲームが頼む所は Input。OS のカーソルを実際に隠すのは Window（`WM_SETCURSOR`）。2つを毎フレームつなぐのは、窓とシーンを組で持つ WindowManager（1.5） |
| カーソルの頼みを戻すか | シーンを変えたら出すに戻す | `Time::ResetGameTime` の「スローを次のシーンへ持ち越さない」と同じ考え |

### 0.5 今回やらないこと・別に見つけたこと

- **Particle System にも同じ問題がある**：`ParticleSystem::Update` も「1. 消えた・外した Particle System の粒を捨てる」で、持ち主が消えたら粒をそのフレームに捨てている。弾に火花の粒を付けると、同じように一瞬で消える。名前を `fadeOutAfterOwner` にしたのは、後で Particle System に同じ項目を足せるようにするため（今回は足さない）
- **シーンを読んだ最初のフレームでは、ワールド行列がまだ作られていなかった**：Sandbox の弾で見つけた（最初の3発が箱ではなく原点から出た）。別の md（Docs/Tasks/FrameHeadTransforms.md）で直す。こちらより先に写す
- 複数のウィンドウ：TrailSystem はゲーム全体で1つなので、どれかのウィンドウのシーンを変えると全部の尾を捨てる（今の ParticleManager と同じ）。カーソルはメインのウィンドウだけ隠す

## 1章 解説

### 1.1 位置と量が同じ式になる理由

画面の位置 `n`（端が ±1）と UI のピクセル `p`（真ん中 0）の関係は、横なら `p = n × (幅 ÷ 2)`。足し算の部分（ずらす量）が無い**比例**なので、2点の差にも同じ式が使える：

```
p2 - p1 = n2 × (幅÷2) - n1 × (幅÷2) = (n2 - n1) × (幅÷2)
```

もし UI のピクセルが「左上が 0」なら `p = (n + 1) × (幅 ÷ 2)` になり、位置と量で式が変わる。このエンジンの UI のピクセルは真ん中が 0 と決まっている（Anchor が Center の Translation）ので、1組で足りる。

PolylineSystem の `MakeToScreen` は「1 ピクセルの量が、画面の位置でいくつか」を `GameScreen::PixelToNdc({1.0f, 1.0f})` で出している（量として使う例）。

### 1.2 持ち主から離れた尾の流れ

TrailSystem の1つの尾（`TrailState`）は、毎フレームこう動く。

```
[持ち主のいる尾]  毎フレーム：設定を写す（settings）・描いているかを覚える（isShown）・Emitting なら帯の先を覚える（head）
      │
      │ 持ち主の Entity が消えた／TrailComponent が無くなった（Update の 1.）
      ▼
  Detach：残す設定か？（Fade Out After Owner・描いていた・Life Time > 0・設定を写したことがある）
      ├─ いいえ → 点を捨てる（→ 2. で尾ごと捨てる）
      └─ はい   → 帯の先（head）を最後の点にして、entity を無効にする
      ▼
[離れた尾]  毎フレーム：写しの Life Time で古くする（2.）。点が2つ未満になったら捨てる
            描くときは写しの設定（色・太さ・テクスチャ…）で描く。先を持ち主にくっつけない
```

- **帯の先（head）を最後の点にする理由**：Emitting の尾は、描くときに最後に「持ち主の今の位置」を足して、帯の先を持ち主にくっつけている（点は Min Vertex Distance ごとにしか足さないので、最後の点と持ち主の間が空いている）。持ち主が消えた後にその1点が無くなると、帯の先が少し縮んで見える。head は前のフレームに描いた帯の先と同じ所なので、見た目がつながる
- **Update の順番**：持ち主が消えるのは `EntityManager::FlushDestroy` の中で、TrailSystem::Update はその後に回る（EngineSystems.cpp）。なので「消えたフレーム」に離して、そのフレームの描画から離れた尾として描く
- **なぜ entity を無効な Handle にするか**：離れた尾が前の持ち主の Handle を持ったままだと、Trail だけ外して付け直したとき、新しい Trail が古い尾を見つけてしまう。`FindTrail` は無効な Handle では何も探さない

### 1.3 `std::optional`（あまり使っていない STL）

`std::optional<T>` は「T の値が1つあるか、無いか」を表す型。ポインタと違ってメモリを別に取らず、T をそのまま中に持つ。

```cpp
std::optional<TrailComponent> settings; // 最初は空（has_value() が false）
settings = component;                   // 写す（値が入る）
if (settings.has_value()) {
	settings->lifeTime;                 // -> で中の T を使う
}
const TrailComponent* p = &*settings;   // *settings が中の T。& でそのアドレス
settings.reset();                       // 空に戻す
```

`TrailState::settings` を optional にしたのは、「AddPoint で尾を作ったが、まだ TrailComponent が付いていない（RequestAdd の次のフレーム）」間は、写す設定が無いから。そのまま持ち主が消えたら、残さずに捨てる（既定の白い太い帯で描いてしまわないように）。`head` も「Emitting でない・無効なら帯の先が無い」を optional で表している。

### 1.4 指示付き初期化子（`{.entity = entity}`）

C++20 から、構造体の初期化で**メンバの名前を書いて**値を入れられる。書かなかったメンバは、宣言に書いた初期値になる。

```cpp
state->trails.push_back({.entity = entity}); // points・settings・head は空、isShown は false
```

前は `{entity, {}}` だった。メンバが5つに増えたので、順番に全部書くより、入れる物だけ名前で書く方が読み違えない。

### 1.5 カーソルを隠す仕組みと層

Windows は、マウスがウィンドウの上で動くたびに `WM_SETCURSOR` を送って「どの形にする？」と聞いてくる。`DefWindowProc` に任せると、ウィンドウを作ったときの矢印（`wc_.hCursor`）に戻される。なので「隠す」は、このメッセージで `SetCursor(nullptr)` して `TRUE` を返し続けること。

- ImGui は `ImGuiConfigFlags_NoMouseCursorChange`（ImGuiManager.cpp）でカーソルを触らない設定なので、エディタでも `WM_SETCURSOR` が Win32Window まで届く
- `WM_SETCURSOR` はマウスが動くまで来ない。隠す・出すが変わったフレームは、`SetCursorHidden` が自分で `SetCursor` を呼ぶ（マウスがこのウィンドウの上にいるときだけ。ほかのアプリの窓の上で呼ぶと、その窓のカーソルを消してしまう）
- 「ゲーム画面の上か」は、エディタでは Game ビューの絵を描くとき（`EditorViewport::Render`）に分かる（毎フレーム InputManager へ教える）。`UpdateCursor` が読むのは前のフレームの値だが、1フレーム遅れは目に見えない
- つなぐのは `WindowManager::UpdateAll` の最後（`EngineSystems::Update` の後）。このフレームの Play 中か（頭で Play・Stop した後）と、Script・SYSTEM の頼みがそろっている

```
ゲーム（Script）── SetCursorVisible(false) ──▶ InputManager（頼みと、マウスがゲーム画面の上か）
                                                   │ ShouldHideCursor()
メインの窓の SceneManager（Play 中か）──────▶ WindowManager::UpdateAll の最後（UpdateCursor）
                                                   │ SetCursorHidden(隠すか)
                                                   ▼
                                          メインの窓の Win32Window（WM_SETCURSOR で SetCursor(nullptr)）
```

置き場所の考え方（誰が誰を知るか）：
- **Win32Window**：一番下の層（Input も Scene も読まない）。なので Win32Window に InputManager・SceneManager を読ませない
- **SceneManager**：窓を知らない（窓が SceneManager を持つ側）。なので SceneManager から Win32Window を触らない
- **WindowManager**：1つの窓の Win32Window・SceneManager・EditorViewport を組（`WindowSet`）で持ち、`UpdateAll` で Play 中かも入力の窓も、もう見ている。なのでつなぐのはここ。新しく知る相手は増えない
- **Engine**：WindowManager だけを知る。Engine が WindowManager の頭越しに `SceneManager::GetMain()`・`GetMainWindow()` を取りに行くと、知る順がねじれるので、Engine には置かない
- 見る SceneManager は `SceneManager::GetMain()` ではなく、メインの窓の組の物（`windows_[0].sceneManager`）。窓とシーンの組が、持っている所の通りになる

## パートA 画面の位置と UI のピクセルの変換

### A-1 `Graphics/Renderer/GameScreen.h`

include（`<cstdint>` の後）

```cpp
#pragma once
#include <cstdint>

#include "HTTEngine/Math/Vector2.h"
```

`GetAspectRatio` の次に足す

```cpp
	static float GetWidth() { return width_; }                 // 幅（UI のピクセル）
	static float GetHeight() { return height_; }               // 高さ（UI のピクセル）
	static float GetAspectRatio() { return width_ / height_; } // 縦横比（幅 ÷ 高さ）
	static Vector2 GetHalfSize() { return {width_ * 0.5f, height_ * 0.5f}; } // 幅・高さの半分（UI のピクセル。画面の真ん中から端まで）

	// ===== 画面の位置 ↔ UI のピクセル =====
	// 画面の位置：真ん中が 0、右と上が＋、端が ±1（InputManager::GetMousePositionInGame・Camera::ScreenPointToRay と同じ数え方）
	// UI のピクセル：真ん中が 0、右と上が＋（Anchor が Center の UI の Translation と同じ数え方）
	// どちらも真ん中が 0 なので、位置にも量（動いた分・大きさ）にもそのまま使える

	/// <summary>
	/// 画面の位置（端が ±1）→ UI のピクセル（真ん中が 0、上が＋）。量にも使える
	/// </summary>
	static Vector2 NdcToPixel(const Vector2& ndc) { return {ndc.x * width_ * 0.5f, ndc.y * height_ * 0.5f}; }

	/// <summary>
	/// UI のピクセル（真ん中が 0、上が＋）→ 画面の位置（端が ±1）。量にも使える
	/// </summary>
	static Vector2 PixelToNdc(const Vector2& pixel) { return {pixel.x / (width_ * 0.5f), pixel.y / (height_ * 0.5f)}; }
```

### A-2 `Graphics/Renderer/Renderer.h`

`RotateAround3d` の後の2つ（コメントごと）を**消す**

```cpp
	/// <summary>正規化座標(-1〜1) → 基準解像度でのピクセル座標（画面中央が原点、Y+が上）</summary>
	static Vector2 NdcToBasePixel(const Vector2& ndc) { return {ndc.x * GameScreen::GetWidth() * 0.5f, ndc.y * GameScreen::GetHeight() * 0.5f}; }

	/// <summary>基準解像度でのピクセル座標 → 正規化座標(-1〜1)</summary>
	static Vector2 BasePixelToNdc(const Vector2& px) { return {px.x / (GameScreen::GetWidth() * 0.5f), px.y / (GameScreen::GetHeight() * 0.5f)}; }
```

### A-3 `Graphics/Renderer/Renderer.cpp`

`NdcToBasePixel(` を `GameScreen::NdcToPixel(` に、`BasePixelToNdc(` を `GameScreen::PixelToNdc(` に**全部置き換える**（Ctrl+H。DrawSprite の5か所と DrawQuad2d の8か所）。直した後：

```cpp
	// 中心へ移動してから正規化座標へ
	Vector2 center = GameScreen::NdcToPixel(config.position);
	lb = GameScreen::PixelToNdc(lb + center);
	lt = GameScreen::PixelToNdc(lt + center);
	rb = GameScreen::PixelToNdc(rb + center);
	rt = GameScreen::PixelToNdc(rt + center);
```

```cpp
		// 正規化座標のまま回すと歪むので、ピクセル空間に直してから回す
		Vector2 lbPx = GameScreen::NdcToPixel(lb), ltPx = GameScreen::NdcToPixel(lt);
		Vector2 rbPx = GameScreen::NdcToPixel(rb), rtPx = GameScreen::NdcToPixel(rt);
		Vector2 center = {(lbPx.x + ltPx.x + rbPx.x + rtPx.x) * 0.25f, (lbPx.y + ltPx.y + rbPx.y + rtPx.y) * 0.25f};
		lb = GameScreen::PixelToNdc(RotateAround2d(lbPx, center, config.rotate));
		lt = GameScreen::PixelToNdc(RotateAround2d(ltPx, center, config.rotate));
		rb = GameScreen::PixelToNdc(RotateAround2d(rbPx, center, config.rotate));
		rt = GameScreen::PixelToNdc(RotateAround2d(rtPx, center, config.rotate));
```

### A-4 `Systems/UI/UIWorldFollowSystem.cpp`

`HalfScreen` を消して、`Project` を書き換える（「画面の半分の大きさ」の中見出しごと消す）

```cpp
// ===== ワールドの点を画面に写す =====
ScreenProjection Project(const Camera& camera, const Vector3& world) {
	ScreenProjection result;
	Vector2 ndc;
	if (camera.WorldToScreenPoint(world, &ndc)) {
		// 画面の位置（端が ±1）→ UI のピクセル（画面の真ん中から。Image の Transform と同じ数え方）
		result.pixel = GameScreen::NdcToPixel(ndc);
		result.direction = result.pixel;
		return result;
	}
	// --- カメラの後ろ：w で割ると裏返るので、カメラから見た向き（ビュー空間の x・y）を使う ---
	// 投影行列の [0][0]・[1][1] は、横・縦の広がりの倍率（画角と縦横の比）。掛けておくと、画面の位置の量になる。それをピクセルの量に直すと、画面の上での向きと同じ比になる
	result.isBehind = true;
	const Vector3 view = TransformPoint(world, camera.GetViewMatrix());
	const Matrix4x4& projection = camera.GetProjectionMatrix();
	result.direction = GameScreen::NdcToPixel({view.x * projection.m[0][0], view.y * projection.m[1][1]});
	return result;
}
```

`IsInside`・`ClampToEdge` の1行目

```cpp
	const Vector2 half = GameScreen::GetHalfSize();
```

### A-5 `Systems/Rendering2D/SpriteRenderSystem.cpp`

`AnchorPosition` の最後の行

```cpp
	const float row = static_cast<float>(1 - index / 3);    //  1=上 0=真ん中 -1=下
	return GameScreen::NdcToPixel({column, row});           // 画面の端（±1）をピクセルに
```

`CalcScreenPoint`（2つ目の小見出しと、最後の行）

```cpp
	// --- 2. アンカーの場所を足して、ピクセル → 画面の位置（端が ±1）---
	const Matrix4x4& m = worldMatrix;
	const Vector2 anchorPosition = AnchorPosition(anchor);
	const float pixelX = local.x * m.m[0][0] + local.y * m.m[1][0] + m.m[3][0] + anchorPosition.x;
	const float pixelY = local.x * m.m[0][1] + local.y * m.m[1][1] + m.m[3][1] + anchorPosition.y;
	return GameScreen::PixelToNdc({pixelX, pixelY});
```

### A-6 `Systems/UI/PolylineSystem.cpp`

`MakeToScreen` の頭（`toNdcX`・`toNdcY` の2行を1行にして、下の4行を `.x`・`.y` に）

```cpp
Matrix4x4 MakeToScreen(const Matrix4x4& world, ScreenAnchor anchor, const Vector2& scale) {
	const Vector2 toNdc = GameScreen::PixelToNdc({1.0f, 1.0f}); // 1 ピクセルの量が、画面の位置でいくつか（横・縦）
	const Vector2 origin = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 0.0f});
	Matrix4x4 m = MakeIdentity4x4();
	m.m[0][0] = world.m[0][0] / scale.x * toNdc.x;
	m.m[0][1] = world.m[0][1] / scale.x * toNdc.y;
	m.m[1][0] = world.m[1][0] / scale.y * toNdc.x;
	m.m[1][1] = world.m[1][1] / scale.y * toNdc.y;
```

ここでビルドできる。`GameScreen::GetWidth() * 0.5f` のような式は、もうエンジンに無い（`Editor/Gizmos/UIGizmo.cpp` の `GetWidth()` は「Game ビューの絵のピクセル → UI のピクセル」で、別の変換なのでそのまま）。

## パートB 持ち主が消えた後も、光の尾を消えきるまで描く

### B-1 `Components/Effects/TrailComponent.h`

ファイルの頭のコメントに1行足す

```cpp
// 軌跡（通った跡・描いた線を、帯にして描く）。Entityに付けるComponent（カテゴリ：Rendering3D）
// 点の列は TrailSystem が持つ。Component に可変長の配列は入れられない
// ここは「どう描くか」の設定だけ。AudioSource と鳴っている音の関係と同じ
// 持ち主が消えた後も消えきるまで描く（Fade Out After Owner）ときは、TrailSystem がこの設定を丸ごと写して持つ（なので trivially copyable のままにする）
```

`lifeTime` の次に足す

```cpp
	float lifeTime = 0.5f;                           // 点が消えるまでの秒（0 で消えない。Clear か Entityを消すまで残る）
	bool fadeOutAfterOwner = true;                   // true：持ち主（Entity かこの Component）が消えても、点が寿命で消えきるまで描く（新しい点は足さない）／false：消えたフレームに捨てる
```

### B-2 `Systems/Effects/TrailSystem.h`

`Update` のコメント（`<para>` を1行直して1行足す）

```cpp
	/// <summary>
	/// 毎フレーム1回、EntityManager::FlushDestroy の後に呼ぶ（ワールド行列も計算済み）
	/// <para>持ち主（Entity か TrailComponent）が消えた尾を持ち主から離し、点を古くして寿命の物を消し、Emitting の Trail に今の位置を足す</para>
	/// <para>離れた尾は、Fade Out After Owner なら写しておいた設定で消えきるまで描く（新しい点は足さない）。そうでなければ、そのフレームに捨てる</para>
	/// </summary>
```

`Draw` の次に足す

```cpp
	// 帯の描画リクエストを積む（WindowManager::DrawAll から）。camera は帯をカメラへ向けるのに使う
	static void Draw(Camera* camera);

	// 全部の尾（持ち主から離れて消えている途中の物も）を捨てる。シーンを変えたときに SceneManager から呼ぶ
	// 点は前のシーンのワールドの位置なので、新しいシーンへ持ち越さない（ParticleManager::ClearGroups と同じ所で呼ぶ）
	static void ClearAll();
```

`GetPoints` のコメント

```cpp
	// 点の列（古い方 → 新しい方）。無ければ空（持ち主から離れた尾は、もう誰の物でもないので出ない）。次に点を足す・Update するまで有効（覚えておかない）
	static std::span<const TrailPoint> GetPoints(Handle<Entity> entity);
```

### B-3 `Systems/Effects/TrailSystem.cpp`

include（`<vector>` の次）

```cpp
#include <vector>
#include <optional>
#include <algorithm>
```

`TrailState`・`TrailSystemState`・`FindTrail`・`FindOrAddTrail` を書き換え、その後ろ（`TrimPoints` の前）に4つ足す。`TrimPoints` の前までを、まるごとこれにする：

```cpp
/// <summary>
/// Trail 1つ分の、実行中だけの状態（保存しない。Play / Stop で作り直すと消える）
/// <para>持ち主（Entity か TrailComponent）が消えたら「離れた尾」になる：entity を無効にして、写しておいた設定で、点が寿命で消えきるまで描く</para>
/// </summary>
struct TrailState {
	Handle<Entity> entity;                  // 持ち主（離れた尾は無効な Handle。同じ Entity に Trail を付け直しても、新しい尾は別に始まる）
	std::vector<TrailPoint> points;         // 点の列（古い方 → 新しい方）
	std::optional<TrailComponent> settings; // 描く設定の写し（毎フレーム持ち主の Component から写す。離れた後はこれで描く。まだ一度も写していなければ空）
	std::optional<Vector3> head;            // 帯の先（Emitting の持ち主の今の位置。離れるときに最後の点にする。前のフレームに描いた帯の先と同じ所）
	bool isShown = false;                   // 今描いているか（Enabled で、持ち主が有効）。描いていなかった尾は、離れるときに捨てる（急に出てこないように）
};

/// <summary>
/// TrailSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct TrailSystemState {
	std::vector<TrailState> trails; // 点を持っている Trail の一覧（離れた尾も入る。Trail の数しか無いので、順に見れば足りる）
	std::vector<Vector3> path;      // 描くときの点の置き場（Draw の中だけで使う。配列のメモリを使い回す）
};
std::unique_ptr<TrailSystemState> state;

// 持ち主から離れた尾か
bool IsDetached(const TrailState& trail) { return !trail.entity.IsValid(); }

// Entityの状態を探す（無ければ nullptr。離れた尾は持ち主がいないので、探しても見つからない）
TrailState* FindTrail(Handle<Entity> entity) {
	if (!entity.IsValid()) {
		return nullptr;
	}
	const auto it = std::ranges::find(state->trails, entity, &TrailState::entity);
	return (it != state->trails.end()) ? &*it : nullptr;
}

// Entityの状態を探し、無ければ足す
TrailState& FindOrAddTrail(Handle<Entity> entity) {
	if (TrailState* trail = FindTrail(entity)) {
		return *trail;
	}
	state->trails.push_back({.entity = entity});
	return state->trails.back();
}

// 持ち主がまだいるか（Entity が生きていて、TrailComponent が付いているか、付ける予約がある）
// 予約を見るのは、RequestAdd したばかり（次のフレームに付く）の Trail に、同じフレームに AddPoint した点を捨てないため
bool HasOwner(const TrailState& trail) {
	if (!EntityManager::IsAlive(trail.entity)) {
		return false;
	}
	return EntityManager::Get<TrailComponent>(trail.entity) != nullptr || EntityManager::IsAddPending<TrailComponent>(trail.entity);
}

// 持ち主から離す。残す設定で、描いていた尾なら、帯の先を最後の点にして残す。残さないなら点を捨てる（点が無くなった尾は Update が捨てる）
// 残さないのは：Fade Out After Owner を外した・描いていなかった（Enabled を外していた・持ち主が無効）・Life Time 0（消えないので、いつまでも残ってしまう）・設定を一度も写していない
void Detach(TrailState& trail) {
	const bool canFadeOut = trail.settings.has_value() && trail.settings->fadeOutAfterOwner && trail.isShown && trail.settings->lifeTime > 0.0f;
	if (!canFadeOut) {
		trail.points.clear();
	} else if (trail.head.has_value() && (trail.points.empty() || Distance(trail.points.back().position, *trail.head) > kSamePointDistance)) {
		trail.points.push_back({*trail.head, 0.0f}); // 前のフレームに描いた帯の先で終わらせる（Emitting は先を持ち主にくっつけて描いていたので）
	}
	trail.entity = {};
	trail.head.reset();
}

// 点を古くして、寿命が来た物を消す（Life Time 0 は消えない）
void AgePoints(std::vector<TrailPoint>& points, float lifeTime, float deltaTime) {
	for (TrailPoint& point : points) {
		point.age += deltaTime;
	}
	if (lifeTime > 0.0f) {
		std::erase_if(points, [lifeTime](const TrailPoint& point) { return point.age >= lifeTime; });
	}
}
```

`DescribeTrail`（`Life Time` の次に1行）

```cpp
	ui.Field("Life Time###lifeTime", value.lifeTime, AtLeast(0.0f).Tip("点が消えるまでの秒。0 で消えない"));
	ui.Field("Fade Out After Owner###fadeOutAfterOwner", value.fadeOutAfterOwner, Tip("true：持ち主（Entity かこの Component）が消えても、点が寿命で消えきるまで描く（新しい点は足さない）／false：消えたフレームに捨てる"));
```

`Update`：「停止中は点を持たない」の後（`// ===== 1.` から関数の終わりまで）を、まるごとこれにする。その後ろに `ClearAll` を足す

```cpp
	// ===== 1. 持ち主（Entity か TrailComponent）が消えた尾を、持ち主から離す =====
	// 離れた尾は、写しておいた設定で点が寿命で消えきるまで描く（残さない設定なら、ここで点を捨てる）。新しい点は足さない
	for (TrailState& trail : trails) {
		if (!IsDetached(trail) && !HasOwner(trail)) {
			Detach(trail);
		}
	}

	// ===== 2. 離れた尾を古くして、寿命が来た点を消す。帯にならなくなった尾を捨てる =====
	for (TrailState& trail : trails) {
		if (IsDetached(trail) && trail.settings.has_value()) {
			AgePoints(trail.points, trail.settings->lifeTime, deltaTime);
		}
	}
	std::erase_if(trails, [](const TrailState& trail) { return IsDetached(trail) && trail.points.size() < static_cast<size_t>(kMinPoints); });

	// ===== 3. 持ち主のいる尾：設定を写し、点を古くして寿命が来た物を消す。Emitting なら今の位置を足す =====
	EntityManager::ForEach<TrailComponent>([deltaTime](Handle<Entity> entity, const TrailComponent& component) {
		TrailState* trail = FindTrail(entity);
		const bool isActive = EntityManager::IsActiveInHierarchy(entity);
		// --- 古くする・消す ---
		if (trail != nullptr) {
			AgePoints(trail->points, component.lifeTime, deltaTime);
		}
		// --- 動いた分だけ足す（無効な Entity は足さない）。今の位置は帯の先として覚えておく ---
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (component.emitting && isActive && transform != nullptr) {
			const Vector3 position = GetWorldPosition(*transform);
			TrailState& emitter = (trail != nullptr) ? *trail : FindOrAddTrail(entity);
			if (emitter.points.empty() || Distance(emitter.points.back().position, position) >= component.minVertexDistance) {
				emitter.points.push_back({position, 0.0f});
			}
			emitter.head = position;
			trail = &emitter;
		} else if (trail != nullptr) {
			trail->head.reset(); // 帯の先を持ち主にくっつけていない（Emitting でない・無効）
		}
		// --- 上限・設定の写し（持ち主が消えたら、この写しで描く）---
		if (trail != nullptr) {
			TrimPoints(trail->points, component.maxPoints);
			trail->settings = component;
			trail->isShown = component.enabled && isActive;
		}
	});
}

void TrailSystem::ClearAll() {
	HTT_ASSERT_MSG(state != nullptr, "TrailSystem::Initialize()を先に呼んでください");
	state->trails.clear();
}
```

`Draw`：`for` の頭（描く設定を決める所）と、Emitting の `if` を書き換える

```cpp
	for (const TrailState& trail : state->trails) {
		// --- 描く設定（離れた尾は写しておいた設定。持ち主のいる尾は、Inspector で変えた値がすぐ出るように Component を読む） ---
		const bool isDetached = IsDetached(trail);
		const TrailComponent* component = isDetached ? (trail.settings.has_value() ? &*trail.settings : nullptr) : EntityManager::Get<TrailComponent>(trail.entity);
		if (component == nullptr) {
			continue;
		}
		if (!isDetached && (!component->enabled || !EntityManager::IsActiveInHierarchy(trail.entity))) {
			continue; // 離れた尾は、描いていた物だけ残しているので、ここは見ない
		}

		// --- 描く点を並べる（持ち主のいる Emitting の尾は、最後に今の位置を足して、帯の先を Entity にくっつける） ---
		path.clear();
		for (const TrailPoint& point : trail.points) {
			path.push_back(point.position);
		}
		if (!isDetached && component->emitting) {
```

（その下の `if (const TransformComponent* transform = ...` から後は、そのまま）

### B-4 `Scene/SceneManager.cpp`

include（`CameraSystem.h` の次）

```cpp
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Effects/TrailSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
```

`ChangeScene`（`ParticleManager::ClearGroups();` の次）

```cpp
	ParticleManager::ClearGroups();
	// 光の尾（Trail）も全部捨てる。点は前のシーンのワールドの位置なので、持ち主から離れて消えている途中の尾も持ち越さない
	// この後 Entity を消しても、尾がもう無いので、次の Update で離れた尾にはならない
	TrailSystem::ClearAll();
```

ここでビルドできる。Inspector の Trail に Fade Out After Owner が出る。

## パートC OS のカーソルを隠す

### C-1 `Input/InputManager.h`

`IsMouseInGame` の次に足す

```cpp
	/// <summary>マウスがゲーム画面の上にあるか（エディタでは Game ビューの絵の上）</summary>
	static bool IsMouseInGame();

	/// <summary>
	/// ゲーム画面の上で OS のカーソル（Windows の矢印）を出すか。false で隠す（照準などの UI をカーソルの代わりに出すとき）
	/// <para>隠れるのは、マウスがゲーム画面の上にいる間だけ（エディタは Play 中に Game ビューの絵の上。一時停止中は出す。製品版は黒帯の内側）。ほかの窓・ゲームの外では出す</para>
	/// <para>シーンを変えると（Stop も）true に戻る。隠したいシーンが、自分で頼む</para>
	/// </summary>
	static void SetCursorVisible(bool visible) { GetInstance()->isCursorVisible_ = visible; }

	/// <summary>SetCursorVisible で頼んだ値（今本当に隠れているかではない。ゲーム画面の外では false でも出ている）</summary>
	static bool IsCursorVisible() { return GetInstance()->isCursorVisible_; }

	/// <summary>
	/// 今 OS のカーソルを隠す所か（SetCursorVisible(false) を頼まれていて、マウスがゲーム画面の上。Play 中かは見ない）
	/// <para>WindowManager が毎フレーム（UpdateAll の最後）見て、メインのウィンドウに渡す</para>
	/// </summary>
	static bool ShouldHideCursor();
```

`#ifdef USE_IMGUI` の中、`SetGameViewRect` の次に足す

```cpp
	static void SetGameViewRect(const Vector2& topLeft, const Vector2& size) {
		GetInstance()->gameViewTopLeft_ = topLeft;
		GetInstance()->gameViewSize_ = size;
	}
	// マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）。EditorViewport が毎フレーム渡す（OS のカーソルを隠すかに使う）
	static void SetGameViewHovered(bool hovered) { GetInstance()->isGameViewHovered_ = hovered; }
#endif
```

メンバ（`mainHwnd_` の次と、`#ifdef USE_IMGUI` の中の最後）

```cpp
	HWND mainHwnd_ = nullptr; // Initialize で受け取ったウィンドウ（製品版のゲーム画面＝このウィンドウの中）
	bool isCursorVisible_ = true; // ゲーム画面の上で OS のカーソルを出すか（SetCursorVisible で頼んだ値）

#ifdef USE_IMGUI
	bool mouseInViewport_ = false;           // マウスがViewportにあるか
	bool viewportFocused_ = false;           // Viewportがフォーカスされてるか
	Vector2 gameViewTopLeft_ = {0.0f, 0.0f}; // Game ビューの絵の左上（クライアント座標）
	Vector2 gameViewSize_ = {0.0f, 0.0f};    // Game ビューの絵の大きさ（0なら、まだ描いていない）
	bool isGameViewHovered_ = false;         // マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）
#endif
```

### C-2 `Input/InputManager.cpp`

`IsMouseInGame` の次に足す

```cpp
bool InputManager::ShouldHideCursor() {
	if (GetInstance()->isCursorVisible_) {
		return false;
	}
#ifdef USE_IMGUI
	// エディタ：Game ビューの絵の上（ほかの窓が重なっている所は出す）
	return GetInstance()->isGameViewHovered_;
#else
	// 製品版：黒帯の内側
	return IsMouseInGame();
#endif
}
```

### C-3 `Editor/Viewport/EditorViewport.cpp`

`Render` の `SetGameViewRect` の次に1行

```cpp
	InputManager::SetGameViewRect({gameMin.x, gameMin.y}, {gameSize.x, gameSize.y});
	InputManager::SetGameViewHovered(gameView_.IsHovered()); // OS のカーソルを隠すのは、絵の上（ほかの窓に隠れていない所）だけ
```

（`gameView_.IsHovered()` は `ImGui::IsItemHovered()`。絵の上で、ほかの窓に隠れていないときだけ true）

### C-4 `Window/Win32Window.h`

`SetPositionLock` の次に足す

```cpp
	void SetPositionLock(bool isLocked) { isPositionLocked_ = isLocked; }

	/// <summary>
	/// このウィンドウの中（クライアント領域）で、OS のカーソルを隠すか。WindowManager が毎フレーム渡す（隠す所かは InputManager::ShouldHideCursor が決める）
	/// <para>変わったとき、マウスがこのウィンドウの上なら、マウスが動くのを待たずにすぐ形を変える</para>
	/// </summary>
	void SetCursorHidden(bool hidden);
```

メンバ（`isFullscreen_` の次）

```cpp
	bool isFullscreen_ = false;
	bool isCursorHidden_ = false; // クライアント領域で OS のカーソルを隠すか（WM_SETCURSOR で見る）
	WindowConfig config_;
```

### C-5 `Window/Win32Window.cpp`

`SetFullscreen` と `WindowProc` の間に足す

```cpp
void Win32Window::SetCursorHidden(bool hidden) {
	if (hidden == isCursorHidden_) {
		return;
	}
	isCursorHidden_ = hidden;
	// WM_SETCURSOR はマウスが動くまで来ないので、マウスがこのウィンドウの上なら、すぐ形を変える
	// （ほかのアプリの窓の上にいるときに変えると、その窓のカーソルを消してしまうので、上にいるか確かめる）
	POINT point{};
	if (GetCursorPos(&point) && WindowFromPoint(point) == hwnd_) {
		SetCursor(hidden ? nullptr : wc_.hCursor);
	}
}
```

`WindowProc` の `switch`（`WM_SYSKEYDOWN` の次、`WM_SIZE` の前）

```cpp
	// マウスが動いた（カーソルの形を決める）。クライアント領域の上で、隠すと決まっていれば隠す
	// ImGui は NoMouseCursorChange でカーソルを触らないので、ここまで届く
	case WM_SETCURSOR: {
		if (self && self->isCursorHidden_ && LOWORD(lparam) == HTCLIENT) {
			SetCursor(nullptr);
			return TRUE; // DefWindowProc に渡すと、ウィンドウクラスの矢印に戻される
		}
		break;
	}
```

### C-6 `Window/WindowManager.h`・`.cpp`

`WindowManager.h` の private（`FindGameScene` の次）

```cpp
	// カメラを持っている最初のシーン（無ければ nullptr）
	IScene* FindGameScene() const;

	// OS のカーソルを隠すかを、メインのウィンドウへ渡す（UpdateAll の最後に呼ぶ）
	void UpdateCursor();
```

`WindowManager.cpp` の `UpdateAll` の最後（`EngineSystems::Update` の後）と、その下に `UpdateCursor` の中身

```cpp
	EngineSystems::Update(isGameRunning, gameCamera);

	// OS のカーソル（このフレームの Play 中かと、Script・SYSTEM の頼みがそろった後）
	UpdateCursor();

	InputManager::SetActiveWindow(nullptr); // 入力をリセット
}


//=============================================================================
// OS のカーソル
//=============================================================================
// 隠すのは「メインのウィンドウのシーンが Play 中（一時停止・停止中は出す）」で「ゲームが隠すと頼んでいて、マウスがゲーム画面の上」のときだけ
// ゲーム画面（エディタは Game ビューの絵、製品版は黒帯の内側）は、メインのウィンドウの中にある
// 頼みと場所は InputManager、Play 中かは SceneManager、OS のカーソルは Win32Window が持つ。SceneManager と Win32Window を組で持っているのはここなので、ここでつなぐ
// エディタの「Game ビューの絵の上か」は描画（PreRenderAll の EditorViewport::Render）で決まるので、ここで読むのは前のフレームの値（1フレーム遅れは目に見えない）
void WindowManager::UpdateCursor() {
	if (windows_.empty()) {
		return;
	}
	const WindowSet& main = windows_[0];
	const bool isPlaying = main.sceneManager && main.sceneManager->GetPlayState() == PlayState::Playing;
	main.window->SetCursorHidden(isPlaying && InputManager::ShouldHideCursor());
}
```

（InputManager・SceneManager・Win32Window は WindowManager.h・.cpp がもう読んでいるので、include は足さなくてよい）

### C-7 `Scene/SceneManager.cpp`

include（`GameComponent.h` の次）

```cpp
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Particle/ParticleManager.h"
```

`ChangeScene`（`Time::ResetGameTime();` の次）

```cpp
	// スロー・ヒットストップの途中で Stop・シーン切り替えをしても、次のシーンへ持ち越さない
	Time::ResetGameTime();
	// OS のカーソルを隠す頼みも持ち越さない（隠したいシーンが自分で頼む。タイトルへ戻ったら矢印が出ない、という戻し忘れが起きないように）
	InputManager::SetCursorVisible(true);
```

ここでビルドできる。

## パートD Sandbox で確かめる

`Sandbox/Source/Scripts` は、このパートで 18 ファイルになる（15 を超えた）。テスト用の見本が増え続けるなら、機能ごとのフォルダ分けを考え始める（今回は分けない）。

### D-1 `Sandbox/Source/Scripts/CursorTrailSamples.h`（新しい）

```cpp
#pragma once
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/Script.h"

//=============================================================================
// 照準の UI と OS のカーソル（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 画面に重ねる UI（Anchor は Center）を、マウスか右スティックで動かす照準にする（Sandbox のテスト用）
/// <para>Start で OS のカーソルを隠す（ゲーム画面の上だけ）。C キーで出す・隠すを切り替える</para>
/// <para>マウスは画面の位置（端が ±1）→ UI のピクセル（GameScreen::NdcToPixel）。右スティックはピクセルの量 → 画面の位置の量（GameScreen::PixelToNdc）</para>
/// </summary>
class AimCursorSample : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float padSpeed_ = 1200.0f; // 右スティックで照準を動かす速さ（ピクセル／秒。倒しきったとき）

	// ===== 実行中だけの値 =====
	Vector2 screenPosition_ = {0.0f, 0.0f}; // 照準の画面の位置（真ん中 0、右と上が＋、端が ±1）
};


//=============================================================================
// 持ち主が消えた後の光の尾（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 弾がいなくなる形（光の尾の残り方を見比べる）
/// </summary>
enum class TrailFadeEnd {
	DestroyEntity, // Entity ごと消す
	RemoveTrail,   // Trail だけ外す（球は残って飛び続け、同じだけ飛んだら消える）
};

/// <summary>
/// 決まった間隔で、光の尾（Trail）を付けた弾を撃つ（Sandbox のテスト用。Entity の前 +Z へ）
/// <para>弾は Flight Time 秒飛んだら、End のやり方でいなくなる。Fade Out After Owner を変えて、尾の残り方を見比べる</para>
/// </summary>
struct TrailFadeShooter {
	float interval = 0.8f;                        // 撃つ間隔（秒）
	float speed = 12.0f;                          // 弾の速さ（m/s）
	float flightTime = 0.6f;                      // 弾が飛ぶ秒（過ぎたら End のやり方でいなくなる）
	TrailFadeEnd end = TrailFadeEnd::DestroyEntity; // 弾がいなくなる形
	bool fadeOutAfterOwner = true;                // 弾の Trail の Fade Out After Owner（true：消えても尾が寿命で消えきるまで残る）
	float trailLifeTime = 0.6f;                   // 弾の Trail の Life Time（点が消えるまでの秒）
	Vector4 trailColor = {0.4f, 0.8f, 1.0f, 1.0f}; // 弾の Trail の色（RGBA。0〜1）
	float timer = 0.0f;                           // 次に撃つまでの秒（実行中だけ）
};

/// <summary>
/// TrailFadeShooter が撃った弾（まっすぐ飛び、Flight Time が過ぎたら End のやり方でいなくなる）
/// </summary>
struct TrailFadeBullet {
	Vector3 velocity = {0.0f, 0.0f, 12.0f};         // 速さ（m/s。ワールド座標）
	float flightTime = 0.6f;                        // 1回に飛ぶ秒（RemoveTrail は、Trail を外した後にもう1回飛ぶ）
	float timeLeft = 0.6f;                          // いなくなるまでの秒
	TrailFadeEnd end = TrailFadeEnd::DestroyEntity; // いなくなる形
	bool isTrailRemoved = false;                    // Trail を外した後か（RemoveTrail のとき。外した後は、同じだけ飛んだら消える）
};
```

### D-2 `Sandbox/Source/Scripts/CursorTrailSamples.cpp`（新しい）

```cpp
#include "Source/Scripts/CursorTrailSamples.h"

#include <algorithm>

#include "HTTEngine/Components/Effects/TrailComponent.h"
#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Input/InputManager.h"

SCRIPT(AimCursorSample, "Test");

namespace {
constexpr float kBulletScale = 0.3f;       // 弾の見た目の球の大きさ
constexpr float kTrailWidthRate = 0.6f;    // 弾の Trail の太さ（球の大きさに掛ける）
constexpr float kTrailIntensity = 3.0f;    // 弾の Trail の明るさの倍率（1 より大きいとブルームで光る）
constexpr float kTrailVertexDistance = 0.05f; // 弾の Trail の点の間（m）
} // namespace


//=============================================================================
// AimCursorSample
//=============================================================================
// ===== 初期化 =====
void AimCursorSample::Start() {
	InputManager::SetCursorVisible(false); // ゲーム画面の上では、この照準がカーソルの代わり
}

// ===== 更新 =====
void AimCursorSample::Update() {
	// --- C キー：OS のカーソルを出す・隠すを切り替える ---
	if (InputManager::IsKeyTriggered(DIK_C)) {
		InputManager::SetCursorVisible(!InputManager::IsCursorVisible());
	}

	// --- マウス：ゲーム画面の中で動いたら、その位置（画面の位置。端が ±1）---
	const bool isMouseMoved = InputManager::GetMouseDeltaX() != 0 || InputManager::GetMouseDeltaY() != 0;
	if (isMouseMoved && InputManager::IsMouseInGame()) {
		screenPosition_ = InputManager::GetMousePositionInGame();
	}

	// --- 右スティック：ピクセルで動かした量を、画面の位置の量に直して足す（真ん中が 0 なので、量にも同じ関数が使える）---
	const Vector2 stick = {InputManager::GetRightStickX(), InputManager::GetRightStickY()};
	const Vector2 move = GameScreen::PixelToNdc(stick * (padSpeed_ * Time::GetRealDeltaTime()));
	screenPosition_ = {std::clamp(screenPosition_.x + move.x, -1.0f, 1.0f), std::clamp(screenPosition_.y + move.y, -1.0f, 1.0f)};

	// --- 画面の位置 → UI のピクセル（Anchor が Center の Translation）---
	const Vector2 pixel = GameScreen::NdcToPixel(screenPosition_);
	transform.translation.x = pixel.x;
	transform.translation.y = pixel.y;
}

// ===== Inspector に出す・保存する値 =====
void AimCursorSample::Fields(ComponentUI& ui) {
	ui.Field("Pad Speed###padSpeed", padSpeed_, AtLeast(0.0f).Tip("右スティックで照準を動かす速さ（ピクセル／秒。倒しきったとき）"));
}


//=============================================================================
// TrailFadeShooter
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TrailFadeShooter, "Test") {
	ui.Field("Interval###interval", value.interval, AtLeast(0.01f).Tip("撃つ間隔（秒）"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("弾の速さ（m/s）"));
	ui.Field("Flight Time###flightTime", value.flightTime, AtLeast(0.0f).Tip("弾が飛ぶ秒（過ぎたら End のやり方でいなくなる）"));
	ui.Field("End###end", value.end, Tip("弾がいなくなる形"));
	ui.Field("Fade Out After Owner###fadeOutAfterOwner", value.fadeOutAfterOwner, Tip("弾の Trail の Fade Out After Owner（true：消えても尾が寿命で消えきるまで残る）"));
	ui.Field("Trail Life Time###trailLifeTime", value.trailLifeTime, AtLeast(0.0f).Tip("弾の Trail の Life Time（点が消えるまでの秒）"));
	ui.ColorField("Trail Color###trailColor", value.trailColor);
}

// ===== 撃つ =====
void TrailFadeShooterUpdate(Handle<Entity>, TrailFadeShooter& shooter, TransformComponent& transform, float deltaTime) {
	shooter.timer -= deltaTime;
	if (shooter.timer > 0.0f) {
		return;
	}
	shooter.timer += shooter.interval;

	// --- 弾：見た目の球・動き・光の尾（付くのは次のフレームの頭）---
	const GameObject bullet = GameObject::Create("Trail Bullet");
	bullet.transform.translation = GetWorldPosition(transform);
	bullet.transform.scale = {kBulletScale, kBulletScale, kBulletScale};

	PrimitiveRendererComponent look;
	look.shape = PrimitiveShape::Sphere;
	look.material.color = shooter.trailColor;
	bullet.AddComponent(look);

	TrailFadeBullet move;
	move.velocity = GetWorldForward(transform) * shooter.speed;
	move.flightTime = shooter.flightTime;
	move.timeLeft = shooter.flightTime;
	move.end = shooter.end;
	bullet.AddComponent(move);

	TrailComponent trail;
	trail.lifeTime = shooter.trailLifeTime;
	trail.fadeOutAfterOwner = shooter.fadeOutAfterOwner;
	trail.minVertexDistance = kTrailVertexDistance;
	trail.width = kBulletScale * kTrailWidthRate;
	trail.color = shooter.trailColor;
	trail.intensity = kTrailIntensity;
	trail.blend = TrailBlend::Add;
	bullet.AddComponent(trail);
}
SYSTEM(TrailFadeShooterUpdate);


//=============================================================================
// TrailFadeBullet
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TrailFadeBullet, "Test") {
	ui.Field("Velocity###velocity", value.velocity, Tip("速さ（m/s。ワールド座標）"));
	ui.Field("Flight Time###flightTime", value.flightTime, AtLeast(0.0f).Tip("1回に飛ぶ秒（RemoveTrail は、Trail を外した後にもう1回飛ぶ）"));
	ui.Field("Time Left###timeLeft", value.timeLeft, AtLeast(0.0f).Tip("いなくなるまでの秒"));
	ui.Field("End###end", value.end, Tip("いなくなる形"));
}

// ===== 動かす・いなくなる =====
void TrailFadeBulletUpdate(Handle<Entity> entity, TrailFadeBullet& bullet, TransformComponent& transform, float deltaTime) {
	transform.translation += bullet.velocity * deltaTime;
	bullet.timeLeft -= deltaTime;
	if (bullet.timeLeft > 0.0f) {
		return;
	}
	// --- Trail だけ外す：球は残して、同じだけ飛んだら消す ---
	if (bullet.end == TrailFadeEnd::RemoveTrail && !bullet.isTrailRemoved) {
		EntityManager::RequestRemove<TrailComponent>(entity);
		bullet.isTrailRemoved = true;
		bullet.timeLeft = bullet.flightTime;
		return;
	}
	EntityManager::Destroy(entity);
}
SYSTEM(TrailFadeBulletUpdate);
```

- `TrailFadeBulletUpdate` の RemoveTrail は、Trail だけ外して（`RequestRemove`）球は飛び続ける。尾は外れたその場に置いていかれて消える

### D-3 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

ソリューションエクスプローラーで Sandbox の `Source\Scripts` を右クリック → **追加 → 既存の項目** で `CursorTrailSamples.h`・`CursorTrailSamples.cpp` を足す。手で書くなら：

`Sandbox.vcxproj`（`TestBullet` の次）

```xml
    <ClCompile Include="Source\Scripts\TestBullet.cpp" />
    <ClCompile Include="Source\Scripts\CursorTrailSamples.cpp" />
```

```xml
    <ClInclude Include="Source\Scripts\TestBullet.h" />
    <ClInclude Include="Source\Scripts\CursorTrailSamples.h" />
```

`Sandbox.vcxproj.filters`（`TestBullet` の次）

```xml
    <ClCompile Include="Source\Scripts\CursorTrailSamples.cpp">
      <Filter>Source\Scripts</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="Source\Scripts\CursorTrailSamples.h">
      <Filter>Source\Scripts</Filter>
    </ClInclude>
```

### D-4 `Sandbox/Resources/scenes/CursorTrailTest.scene.json`（新しい）

```json
{
	"version": 5,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 7.0, -9.0],
					"rotation": [30.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
				}
			}
		},
		{
			"id": 2,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 3.0, 0.0],
					"rotation": [50.0, -30.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Directional Light": {
					"color": [1.0, 1.0, 1.0]
				}
			}
		},
		{
			"id": 3,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, -0.5, 5.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [16.0, 1.0, 24.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.3, 0.3, 0.35, 1.0]
				}
			}
		},
		{
			"id": 10,
			"name": "Shooter Fade On",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-4.0, 1.0, -2.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.5, 0.5, 0.5]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.4, 0.8, 1.0, 1.0]
				},
				"TrailFadeShooter": {
					"interval": 0.8,
					"speed": 12.0,
					"flightTime": 0.6,
					"end": "DestroyEntity",
					"fadeOutAfterOwner": true,
					"trailLifeTime": 0.6,
					"trailColor": [0.4, 0.8, 1.0, 1.0]
				}
			}
		},
		{
			"id": 11,
			"name": "Shooter Fade Off",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 1.0, -2.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.5, 0.5, 0.5]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [1.0, 0.5, 0.3, 1.0]
				},
				"TrailFadeShooter": {
					"interval": 0.8,
					"speed": 12.0,
					"flightTime": 0.6,
					"end": "DestroyEntity",
					"fadeOutAfterOwner": false,
					"trailLifeTime": 0.6,
					"trailColor": [1.0, 0.5, 0.3, 1.0]
				}
			}
		},
		{
			"id": 12,
			"name": "Shooter Remove Trail",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [4.0, 1.0, -2.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.5, 0.5, 0.5]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.6, 1.0, 0.4, 1.0]
				},
				"TrailFadeShooter": {
					"interval": 0.8,
					"speed": 12.0,
					"flightTime": 0.6,
					"end": "RemoveTrail",
					"fadeOutAfterOwner": true,
					"trailLifeTime": 0.6,
					"trailColor": [0.6, 1.0, 0.4, 1.0]
				}
			}
		},
		{
			"id": 20,
			"name": "Aim Cursor",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Vector Shape": {
					"enabled": true,
					"shape": "Ring",
					"size": [40.0, 40.0],
					"ringThickness": 4.0,
					"color": [1.0, 0.9, 0.3, 1.0]
				},
				"AimCursorSample": {
					"padSpeed": 1200.0
				}
			}
		}
	]
}
```

### D-5 確かめること

Project ウィンドウで `scenes/CursorTrailTest` を開いて Play。

| # | すること | こうなれば合格 |
|---|---|---|
| 1 | Play して、マウスを Game ビューの上へ | Windows の矢印が消え、黄色い輪（照準）がマウスに付いて来る |
| 2 | マウスを Hierarchy・Inspector の上へ | 矢印が出る。Game ビューに戻すと消える |
| 3 | Game ビューの上で C キー | 矢印が出る。もう一度 C で消える |
| 4 | 一時停止（Ⅱ） | Game ビューの上でも矢印が出る。再開すると消える |
| 5 | 左の青い列（Fade On） | 弾が消えても尾がその場に残り、細く短くなって消える |
| 6 | 真ん中の橙の列（Fade Off） | 弾が消えたフレームに、尾も一緒に消える（前と同じ） |
| 7 | 右の緑の列（Remove Trail） | 弾の球は飛び続け、尾だけ置いていかれて消える。球はもう尾を引かない |
| 8 | 一時停止したまま見る | 消えている途中の尾も止まっている |
| 9 | Stop | 尾が全部消える。Game ビューの上で矢印が出る |
| 10 | Shooter Fade On の Fade Out After Owner を Inspector で外して Play | 青い列も、橙と同じく一緒に消える |
| 11 | 前からあるシーン（UIWorldFollowTest の照準・数字・端の矢印、ほかの UI のあるシーン） | 見た目が前と変わらない（パートA は式をまとめただけ） |

## パートE `Docs/Reference.md`

「1フレームの順番」の表の 10.5 の行

```
| 10.5 | `TrailSystem::Update`（持ち主の Entity・Trail が消えた尾を持ち主から離し〈Fade Out After Owner なら写した設定で消えきるまで描く。新しい点は足さない〉、点を古くして寿命の物を消し、Emitting の Trail に今の位置を足す。停止中は点を持たない。シーンを変えたら `TrailSystem::ClearAll` で全部捨てる） | エンジン |
```

## 2章 AL4_01 で差し替える所（公開した物の一覧）

AL4_01 の Docs/AimShoot.md を直すときに使う。

### 2.1 画面の変換（`#include <HTTEngine/Graphics/Renderer/GameScreen.h>`）

```cpp
class GameScreen {
public:
	static Vector2 GetHalfSize();                    // 幅・高さの半分（UI のピクセル。画面の真ん中から端まで）
	static Vector2 NdcToPixel(const Vector2& ndc);   // 画面の位置（端が ±1）→ UI のピクセル（真ん中が 0、上が＋）。量にも使える
	static Vector2 PixelToNdc(const Vector2& pixel); // UI のピクセル（真ん中が 0、上が＋）→ 画面の位置（端が ±1）。量にも使える
};
```

AimReticle（`LateUpdate` の最後の2行）

```cpp
	const Vector2 pixel = GameScreen::NdcToPixel(aim->GetScreenPosition());
	transform.translation.x = pixel.x;
	transform.translation.y = pixel.y;
```

PlayerAim（右スティックの所）

```cpp
		const Vector2 move = GameScreen::PixelToNdc(stick * (padCursorSpeed_ * Time::GetRealDeltaTime()));
		screenPosition_.x = std::clamp(screenPosition_.x + move.x, -1.0f, 1.0f);
		screenPosition_.y = std::clamp(screenPosition_.y + move.y, -1.0f, 1.0f);
```

### 2.2 光の尾（`#include <HTTEngine/Components/Effects/TrailComponent.h>`）

```cpp
struct TrailComponent {
	// …（前からの項目）
	bool fadeOutAfterOwner = true; // true：持ち主（Entity かこの Component）が消えても、点が寿命で消えきるまで描く（新しい点は足さない）／false：消えたフレームに捨てる
};
```

- Inspector の表示名は **Fade Out After Owner**（シーン・Prefab の JSON のキーは `fadeOutAfterOwner`）。既定が true なので、弾の Prefab に Trail を足すだけでよい（弾のコードは何も変えない）
- 残らないのは Life Time 0・Enabled を外していた・持ち主が無効だった尾
- `TrailSystem::ClearAll()` はエンジン（SceneManager）が呼ぶ物。ゲームから呼ぶことはない

### 2.3 OS のカーソル（`#include <HTTEngine/Input/InputManager.h>`）

```cpp
class InputManager {
public:
	static void SetCursorVisible(bool visible); // ゲーム画面の上で OS のカーソルを出すか。false で隠す（シーンを変えると true に戻る）
	static bool IsCursorVisible();              // SetCursorVisible で頼んだ値（今本当に隠れているかではない）
	static bool ShouldHideCursor();             // 今 OS のカーソルを隠す所か（WindowManager が使う。ゲームは使わなくてよい）
};
```

AimReticle（`Start` を足して、そこで頼む）

```cpp
void AimReticle::Start() {
	InputManager::SetCursorVisible(false); // ゲーム画面の上では、照準がカーソルの代わり（シーンを変えると戻る）
}
```

## 3章 次の候補

- Particle System にも Fade Out After Owner（0.5。弾に火花の粒を付けるとき）
- カーソルを閉じ込める（`ClipCursor`）・照準の絵に変える（`SetCursor` に .cur を渡す）。今の AL4_01 の決まりは「閉じ込めない」なので要らない
