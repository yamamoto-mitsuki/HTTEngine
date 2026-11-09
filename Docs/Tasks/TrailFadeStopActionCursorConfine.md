参考：
- Unity の Particle System の Stop Action（出し終わったら Destroy・Disable・Callback）：https://docs.unity3d.com/ScriptReference/ParticleSystem.MainModule-stopAction.html
- Unity の Cursor.lockState（Confined：ウィンドウの外へ出さない／Locked：真ん中に固定）：https://docs.unity3d.com/ScriptReference/Cursor-lockState.html
- Unreal の PIE（Play In Editor）：遊んでいる間はマウスを Viewport に閉じ込め、Shift+F1 で放す（エディタの操作。この md の 0.4）
- Win32 の ClipCursor（カーソルを四角に閉じ込める。ほかのアプリと共有の物なので、手放す前に外す）：https://learn.microsoft.com/ja-jp/windows/win32/api/winuser/nf-winuser-clipcursor
- Win32 の WM_ACTIVATE（手前になった・手前でなくなった）：https://learn.microsoft.com/ja-jp/windows/win32/inputdev/wm-activate
- DirectInput のマウスの値（相対の値は mickey。コントロールパネルの速さ・加速は掛からない）：https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418272(v=vs.85)
- 高精度のマウスの動き（DirectInput・WM_INPUT・WM_MOUSEMOVE の違い）：https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/taking-advantage-of-high-dpi-mouse-movement
- Dear ImGui の ImGuiConfigFlags_NoMouse：`project/externals/imgui/imgui.h` の `ImGuiConfigFlags_` の所
- `std::string_view`（決まった文字を写さずに指す）：https://cpprefjp.github.io/reference/string_view/basic_string_view.html
- 前の md：Docs/Tasks/ScreenTrailCursor.md（Fade Out After Owner・SetCursorVisible・GameScreen::PixelToNdc）
- この作業のきっかけ：AL4_01 の射撃（feature/LockOnShoot）で、エンジンに足りなかった3つ

# 持ち主が消えた後の尾を薄くする・出し終わった Particle を消す・OS のカーソルをゲーム画面に閉じ込める

ゲーム（AL4_01）の射撃で足りなかった3つを、エンジンに足す。どれもゲーム側では埋めず、エンジンの中で完結させる。

- **パートA Trail：持ち主が消えた後、尾全体を薄くして消す**（`TrailComponent`・`TrailSystem`）：`bool fadeOutAfterOwner` を enum の **After Owner**（Discard・Shrink・Fade）と **Fade Seconds** に置き換える
- **パートB Particle：出し終わったら Entity を消す**（`ParticleSystemComponent`・`ParticleSystem`）：**Stop Action**（None・Destroy）を足す
- **パートC Input：OS のカーソルをゲーム画面に閉じ込める**（`MouseInput`・`InputManager`・`Win32Window`・`ImGuiManager`・`WindowManager`・`ViewportWindow`・`EditorViewport`・`SceneManager`）：`InputManager::SetCursorConfined(bool)`
- **パートD Sandbox で確かめる**：照準を「マウスの動いた量」で動かす見本・尾の消え方を見比べる弾・出し終わったら消える火花の見本と、テストシーン **TrailParticleCursorTest**・火花の Prefab **BurstEffect**
- **パートE `Docs/Reference.md`**：Trail・Particle System・入力の所

パートは **A → B → C → D → E の順に写す**。A・B・C はそれぞれ別の機能なので、どれの後でもビルドできる（D は A・B・C を全部使う）。

ブランチは `feature/TrailFadeStopActionCursorConfine`（develop 095a419 から）。**2026-11-09 に、パートA〜D は本体へ直接入れた**（この md は記録と読み方用。パートE の Reference.md は入れていない）。

**名前（2026-11-09 に直した）**：最初は `SetCursorLocked` だったが、「マウスが動かなくなる」と読めるので、Unity の Confined と同じ「閉じ込める」の **`SetCursorConfined`** にした。窓口・中の変数・コメントも全部「閉じ込める」（Confine）にそろえた（0.4）。InputManager の private の関数は、前からの `initInternal`・`finalizeInternal`・`updateInternal` も含めて、大文字で始める（`InitInternal`・`UpdateMouseSwallow` など）。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドでエラー・自分のファイルの警告 0。自動テスト（Debug・Development 51 項目、Release 37 項目。一時停止・Shift+F1・Game ビューのクリックの 14 項目はエディタだけ。最後の 6 項目は TrailParticleCursorTest を読んで遊ぶ）が合格（名前を直した版で、Debug・Development は全部が1回で合格。Release は、同じ写しを別の作業が使い始めたため、カーソルの項目とシーンの項目を別々の回で合格）。その中に「Fade の尾は Life Time を過ぎても点が減らない」「濃さが 1 - 秒/Fade Seconds」「Instantiate した同じフレームは Component が無いが、付いたフレームに出て、出し終わったら Entity が子ごと消える」「Looping でも Stop したら消える」「外へ動かしてもゲーム画面の角で止まる」「右の端で止まったまま、GetMouseDeltaX は送った量（8000）を全部返す」「Shift+F1 で放す・Game ビューのクリックで閉じ込め直し、そのクリックはゲームに届かない」「ほかのウィンドウが手前になったら放す」がある。止めて撮った絵で、Add と Normal の尾が形はそのままで薄くなることも見た。

## 0章 全体の形

### 0.1 何が困っていたか

1. **持ち主が消えた後の尾が、短い棒が縮むように見える**：Fade Out After Owner がオンだと、点が古い方から寿命で消える（尾が縮む）。色・明るさは最後まで変わらない。太さは点の並びの番号で決まる（古い方の端が End Width Rate、新しい方の端が Width）ので、縮む間も先が太いまま
2. **出し終わった Particle System の Entity を消す係が無い**：Looping を外しても、出し終わった後の Entity は残る。弾が当たった所に飛び散る物の Prefab を Instantiate すると、Entity がたまり続ける
3. **OS のカーソルがゲーム画面の外へ出る**：照準を「マウスの動いた量 × 感度」で動かすと、隠れたカーソルがゲーム画面の外へ出て、ほかの窓やエディタのパネルをクリックしてしまう

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Components/Effects/TrailComponent.h` | enum `TrailAfterOwner` を足し、`fadeOutAfterOwner` を `afterOwner`・`fadeSeconds` に置き換える |
| A | `Systems/Effects/TrailSystem.h`・`.cpp` | 離すとき・離れた尾を進めるとき・描くときに After Owner を見る |
| B | `Components/Effects/ParticleSystemComponent.h` | enum `ParticleStopAction` と `stopAction` を足す |
| B | `Systems/Effects/ParticleSystem.h`・`.cpp` | 出し終わったら Stop Action をする |
| C | `Input/MouseInput.h`・`.cpp` | `IsAnyButtonPressed`・`IsAnyButtonReleased` |
| C | `Input/InputManager.h`・`.cpp` | `SetCursorConfined`・`IsCursorConfined`・`IsCursorConfineActive`・`GetCursorConfineRect`・`ResetCursorRequests`・エディタで放す・閉じ込め直す |
| C | `Window/Win32Window.h`・`.cpp` | `SetCursorClip`（`ClipCursor`）と `WM_ACTIVATE`・`WM_DESTROY` で放す |
| C | `Editor/ImGuiManager.h`・`.cpp` | `SetMouseEnabled`（`ImGuiConfigFlags_NoMouse`） |
| C | `Window/WindowManager.cpp` | `UpdateCursor` で、閉じ込める → 隠すの順に渡す |
| C | `Editor/Viewport/ViewportWindow.h`・`.cpp` | `IsVisible`（ほかのタブの裏か） |
| C | `Editor/Viewport/EditorViewport.h`・`.cpp` | 見えていない Game ビューは大きさ 0・Game ビューの案内 |
| C | `Scene/SceneManager.cpp` | シーンを変えたら `ResetCursorRequests` |
| D | `Sandbox/Source/Scripts/CursorTrailSamples.h`・`.cpp` | 照準を動いた量で動かす・尾の After Owner・Blend |
| D | `Sandbox/Source/Scripts/BurstEffectSamples.h`・`.cpp`（新しい） | 出し終わったら消える火花を出す BurstSpawner |
| D | `Sandbox/Sandbox.vcxproj`・`.filters` | 上の2つを足す |
| D | `Sandbox/Resources/scenes/TrailParticleCursorTest.scene.json`・`Sandbox/Resources/prefabs/BurstEffect.prefab`（新しい） | テストシーンと、飛び散る物の Prefab |

### 0.3 使い方

**A Trail**：Inspector の Trail の **Fade Out After Owner** が、**After Owner** と **Fade Seconds** に変わる。

| After Owner | 持ち主（Entity かこの Component）が消えたとき |
|---|---|
| Discard | 消えたフレームに捨てる（前の Fade Out After Owner を外したのと同じ） |
| Shrink（既定） | 点が古い方から寿命で消える（尾が縮む。前の Fade Out After Owner と同じ） |
| Fade | 形はそのままで、**Fade Seconds** かけて尾全体の色と明るさを 0 にして消す。点は古くしない |

- どれも新しい点は足さない。Blend が Add でも Normal でも薄くなる（1.1）
- 残さない物は今までどおり：Enabled を外していた・持ち主が無効だった尾。それに加えて、**Shrink で Life Time 0**（点が消えないので）・**Fade で Fade Seconds 0**
- Fade は秒で消えるので、**Life Time 0 の尾（描いた線）も残せる**
- 一時停止・スローの間は、Fade も止まる・ゆっくりになる（`Time::GetDeltaTime`）
- 離れた尾はエンジン（TrailSystem）が設定を写して持っている。ゲームからは触れない（今までどおり）

**B Particle System**：Inspector の Particle System の Playback に **Stop Action** が増える。

| Stop Action | 出し終わったとき |
|---|---|
| None（既定） | 何もしない（前と同じ。Entity は残り、また Play できる） |
| Destroy | この Component を持つ Entity を消す（子も一緒） |

- **出し終わり**＝出し始めた物が、出すのをやめて（Looping でなく Duration が過ぎた・`ParticleSystem::Stop` を呼んだ）、粒が全部消えたとき。Unity と同じく、Looping の物も Stop すれば出し終わりになる
- 一度も出し始めていない物（Play On Start を外して Play を呼んでいない・`Emit` だけ）は出し終わりにならない
- Instantiate した同じフレームは、Prefab の Component がまだ付いていない。次のフレームの頭に付いて、`ParticleSystem::Update` が初めて見つけたときに Play On Start で出る。だからゲームは何も書かなくてよい
- 飛び散る物の Prefab は、**一番上の Entity** に Particle System を付けて「Looping を外す・Play On Start・Stop Action を Destroy」にする（子に付けると、消えるのはその子だけ）
- 停止中の Inspector の **▶ Preview** では消さない

```cpp
// 弾が当たった所に飛び散る物を出す。後は何もしなくてよい（出し終わったら消える）
GameObject::Instantiate(hitEffectPrefab_, hitPosition);
```

**C OS のカーソル**

```cpp
// 照準を出すシーンの Start などで
InputManager::SetCursorVisible(false); // ゲーム画面の上で隠す（前からある）
InputManager::SetCursorConfined(true);   // ゲーム画面の外へ出さない（今回）

// 照準を動かす（Update）。動いた量は「マウスが数えた数」なので、感度を掛けてピクセルの量にする
if (InputManager::IsCursorConfineActive()) {
	const Vector2 pixelMove = {static_cast<float>(InputManager::GetMouseDeltaX()) * pixelsPerCount_, -static_cast<float>(InputManager::GetMouseDeltaY()) * pixelsPerCount_};
	screenPosition_ += GameScreen::PixelToNdc(pixelMove); // マウスは下が＋、画面の位置は上が＋なので Y を返している
}
```

- 閉じ込める四角はゲーム画面（エディタは Game ビューの絵、製品版は黒帯の内側）。外へは出られず、**端で止まる**（真ん中へ戻し続けるのではない。Unity の Confined）
- 閉じ込めている間も **`GetMouseDeltaX/Y` は動いた分を返す**（カーソルが端で止まっていても）。エディタの Game ビューの上で ImGui に取られて 0 にもならない
- **単位はマウスが数えた数（mickey）**。ピクセルではない。コントロールパネルのポインターの速さ・加速は掛からない。ふつうの設定なら 1 カウント ≒ Windows の矢印の 1 ピクセル（マウスの DPI で変わる）
- 隠すのとは別（`SetCursorVisible` はそのまま）。照準で使うときは両方頼む

| 場面 | エディタ | 製品版 |
|---|---|---|
| Play 中・ウィンドウが手前 | Game ビューの絵に閉じ込める。エディタのパネルはマウスで触れない | 黒帯の内側に閉じ込める |
| 一時停止 | 放す（再開したらまた閉じ込める） | ― |
| Alt+Tab などで手前でなくなった | 放す。戻ってもそのまま（Game ビューを左クリックしたら閉じ込める） | 放す。戻ったらまた閉じ込める |
| **Shift+F1** | 放す（Game ビューを左クリックしたら閉じ込める。そのクリックはゲームに渡さない） | ― |
| Game ビューがほかのタブの裏 | 閉じ込めない | ― |
| シーンを変えた・Stop | 放す（頼みも false に戻る。閉じ込めたいシーンが自分で頼む） | 同じ |

- `IsCursorConfined()` は頼んだ値、`IsCursorConfineActive()` は今本当に閉じ込めているか（前のフレームの終わりに決めた値）
- エディタでは、閉じ込めた直後の3秒「Shift+F1 : カーソルを画面の外へ出す」、放している間「クリックでカーソルを画面に閉じ込める」を Game ビューの左上に出す

### 0.4 決めたこと（2026-11-08 に相談して決めた）

| 項目 | 決まり | 理由 |
|---|---|---|
| Trail の設定の形 | `bool fadeOutAfterOwner` を消して、enum `afterOwner`（Discard・Shrink・Fade）1つにまとめる | 「残すか」と「どう消すか」を2つの項目に分けると、組み合わせ（残さないのに薄くする）が意味を持たない。1つの選択にすると、次の消え方（縮めながら薄くする・Curve で薄くする）も enum に足すだけで済む |
| Fade のときの点 | 古くしない（形はそのまま） | ほしいのは「形はそのままで薄くして消す」。縮みながら薄くなると、前の不自然さ（先が太いまま縮む）が残る |
| 薄くする所 | 尾全体の色の A に濃さを掛ける | Normal も Add も「色 × A」を重ねる（1.1）ので、A だけで両方薄くなる。RGB にも掛けると、Normal で2回掛かって速く消える |
| Stop Action の出し終わり | Unity と同じ：出すのをやめて（Duration が過ぎた・Stop を呼んだ）、粒が全部消えた | Looping の物も、ゲームが Stop したら消せる。Unity の Particle System と同じ考えなので、Unity の知識で読める |
| Stop Action の種類 | None・Destroy の2つ（Unity の Disable・Callback は今回作らない） | 今使うのは Destroy だけ。enum なので、使う所が来たら足すだけ |
| 閉じ込め方 | `ClipCursor` でゲーム画面に閉じ込める（端で止まる。Unity の Confined） | ゲームの頼みは「ゲーム画面から出ない」。カーソルの位置（`GetMousePositionInGame`）も意味を持ち続ける。真ん中に戻し続ける Locked は、位置が要らないゲーム（FPS）用で、別の頼み |
| 窓口 | `InputManager::SetCursorConfined(bool)`・`IsCursorConfined()`（`SetCursorVisible` と同じ形） | ゲームが頼む所は Input。本当に閉じ込めるのは Window（`ClipCursor`）。2つを毎フレームつなぐのは、窓とシーンを組で持つ WindowManager（`SetCursorVisible` と同じ層。1.4） |
| 名前（11-09） | Lock ではなく **Confine**（閉じ込める）。`SetCursorConfined`・`IsCursorConfined`・`IsCursorConfineActive`・`GetCursorConfineRect`・`SetCursorConfineActive`・`IsCursorConfineReleasedInEditor` | Lock は「マウスが動かない」と読めてしまう。Unity でも、外へ出さないのは Confined、真ん中に固定が Locked で、別の物 |
| private の関数の名前（11-09） | 大文字で始める（前からの `initInternal`・`finalizeInternal`・`updateInternal` も `InitInternal`・`FinalizeInternal`・`UpdateInternal` に） | エンジンのほかの関数（private も）は大文字で始まる。InputManager の3つだけが小文字だったので、足す2つに合わせず、3つを直した |
| エディタで放すキー | **Shift+F1**（Unreal と同じ） | Esc はゲームがポーズに使いやすい。Esc にすると、エディタだけポーズと同時にカーソルも放れて、製品版と動きがずれる |
| 閉じ込め直し | Game ビューの絵を左クリック。そのクリックはゲームに渡さない（離すまで） | 閉じ込め直したクリックで弾が出ないように |
| エディタで手前でなくなったとき | 放したままにする（クリックで閉じ込め直す） | Alt+Tab で戻ったとき、どこをクリックしたいか分からない（パネルを触りたいかもしれない）。製品版はゲームしか無いので、戻ったらすぐ閉じ込める |
| 閉じ込めている間のパネル | ImGui にマウスを見せない（`ImGuiConfigFlags_NoMouse`） | Game ビューの上に重ねた窓・パネルの縁は、閉じ込めた四角の中でも押せてしまう。閉じ込めている間はマウスはゲームの物 |

### 0.5 今回やらないこと・別に見つけたこと

- **AL4_01 の弾の Prefab**（`Resources/prefabs/PlayerBullet.prefab`）は `"fadeOutAfterOwner": true` を持っている。キーが無くなったので読み飛ばされ、After Owner は既定の Shrink（前と同じ動き）になる。Fade にするなら、Inspector で選び直して保存する（2章）
- カーソルを真ん中に戻し続ける Locked（FPS のカメラ）は作らない。要るときは `SetCursorLockMode` のように enum の窓口にして足す（3章）
- `ParticleSystem.h` の `Update` のコメントが「FlushDestroy の後に呼ぶ」になっていたが、実際は前（`EngineSystems::Update`）。Stop Action の Destroy は、すぐ後の FlushDestroy でそのフレームのうちに消えるので、コメントを直した

## 1章 解説

### 1.1 A だけで Normal も Add も薄くなる理由

重ね方（`Graphics/Pipeline/RenderStates.cpp` の `MakeBlendDesc`）は、どちらも描く色に自分の A を掛けてから重ねる。

| Blend | 画面の色 | A を 0 へ下げると |
|---|---|---|
| Normal | 描く色 × A ＋ 元の色 × (1 − A) | 描く色の分が 0、元の色が 1 倍 → 元の絵に戻る |
| Add | 描く色 × A ＋ 元の色 | 足す分が 0 → 元の絵に戻る |

Intensity（明るさの倍率）は描く色に掛かっているので、A を下げれば明るさも一緒に 0 になる。だから尾全体の色（`config.color`）の A に「濃さ」を掛けるだけでよい。

```
濃さ ＝ 1 − 離れてからの秒 ÷ Fade Seconds（0〜1 に収める）
```

Fade Seconds が過ぎたら点を空にして、次の `std::erase_if` で尾ごと捨てる（今までの「点が2つより少なくなった尾を捨てる」と同じ所）。

### 1.2 Stop Action の流れ

```
フレームの頭      Instantiate で予約した Component が付く
SYSTEM・Script   （ゲーム）
ParticleSystem::Update
  初めて見つけた → Play On Start なら Restart（hasPlayed = true）
  UpdateEmitter   粒を動かす・出す。Looping でなく Duration が過ぎたら isPlaying = false
  ApplyStopAction 出し終わった（hasPlayed・!isPlaying・粒が 0）なら1回だけ → Destroy なら EntityManager::Destroy
EntityManager::FlushDestroy   ← このフレームのうちに消える（子も一緒）
```

`hasPlayed` が無いと、Play On Start を外した物（まだ一度も出していない＝`isPlaying` が false・粒が 0）が、見つけたフレームに消えてしまう。`isStopActionDone` は、None の物を毎フレーム「出し終わった」と数えないため（もう一度 Play したら戻す）。

### 1.3 `ClipCursor` と `WM_ACTIVATE`

`ClipCursor(&四角)` は、OS のカーソルをスクリーン座標の四角の中に閉じ込める。**パソコン全体で1つ**の設定なので、ほかのアプリへ移る前に外す（`ClipCursor(nullptr)`）のが決まり。

- **毎フレーム頼み直す**：ウィンドウ・Game ビューが動いた、ほかのアプリや Windows が外した、に追いつく（同じ四角でも安い）
- **手前でなくなったら、すぐ外す**（`WM_ACTIVATE` の `WA_INACTIVE`）：Alt+Tab・Windows キーのほか、**assert のメッセージボックス**もこれ。メッセージボックスの間はフレームが回らないので、次のフレームの `UpdateCursor` を待つと、カーソルが OK ボタンへ届かない
- **ウィンドウを閉じるとき**（`WM_DESTROY`）も外す
- 閉じ込めていないのに外すと、ほかのアプリが閉じ込めていた物まで外してしまうので、自分が閉じ込めたときだけ外す（`isCursorClipped_`）

**DirectInput の動いた量は止まらない**：`ClipCursor` が止めるのは OS のカーソル（矢印の位置）だけ。DirectInput はマウスが数えた数そのもの（mickey）を読むので、矢印が端で止まっていても、動かした分だけ数える。だから「閉じ込める」と「動いた量で照準を動かす」は一緒に使える。

### 1.4 層と1フレームの流れ

| 層 | 持つ物 |
|---|---|
| ゲーム（Script） | 頼む：`SetCursorConfined(true)` |
| InputManager | 頼み・閉じ込める四角（ゲーム画面）・メインのウィンドウが手前か・エディタで放したか・閉じ込め直したクリックを渡さない |
| SceneManager | Play 中か・シーンを変えたら頼みを戻す（`ResetCursorRequests`） |
| WindowManager | 上の2つをつないで、メインのウィンドウへ渡す（`UpdateCursor`） |
| Win32Window | 本当に閉じ込める・放す（`ClipCursor`・`WM_ACTIVATE`） |
| ImGuiManager | 閉じ込めている間、ImGui にマウスを見せない |
| EditorViewport | Game ビューの絵の場所・見えているか・上か・案内の文字 |

```
Engine::BeginFrame
  InputManager::Update         入力を読む → 閉じ込め直したクリックを渡さない（UpdateMouseSwallow）
                               → エディタ：Shift+F1・手前でない で放す、Game ビューのクリックで閉じ込め直す（UpdateEditorCursorConfine）
WindowManager::UpdateAll
  シーン・Script・SYSTEM       ゲームが SetCursorConfined・GetMouseDeltaX を使う
  UpdateCursor                 閉じ込める四角 → Win32Window::SetCursorClip → SetCursorConfineActive → ImGui のマウス → 隠す
PreRenderAll
  EditorViewport::Render       Game ビューの絵の場所・見えているか・上か（次のフレームで使う）・案内
```

放す・閉じ込め直すをフレームの頭（ゲームが入力を読む前）で決めるのは、**閉じ込め直したクリックを、そのフレームからゲームに渡さないため**。`UpdateCursor`（フレームの終わり）で決めると、ゲームはもうそのクリックを読んでいる。

**隠すかは「閉じ込めているか」も見る**：閉じ込めている間は ImGui がマウスを見ないので、Game ビューの「上か」（`IsItemHovered`）が false になる。でもカーソルは Game ビューの中にいるので、閉じ込めているなら隠す所とする。そのため `UpdateCursor` は「閉じ込める」を先に決めてから「隠す」を決める。

### 1.5 閉じ込め直したクリックを、離すまで渡さない

```cpp
// 押した：放している間に Game ビューを左クリック → 閉じ込め直して isMouseButtonSwallowed_ = true
// 押している間：IsMouseButtonPressed・Triggered・Released は false
// 全部のボタンを離して、離した瞬間（Released）も過ぎたフレームに false へ戻す
```

離したフレームにすぐ戻すと、そのフレームの `IsMouseButtonReleased(0)` が true になり、ゲームは「押していないのに離した」を読む。だから「押しているボタンが無く、離したボタンも無い」フレームで戻す。

## パートA Trail：持ち主が消えた後、尾全体を薄くして消す

### A-1 `Components/Effects/TrailComponent.h`

ファイルの頭のコメントの4行目

```cpp
// 持ち主が消えた後も残して描く（After Owner が Shrink・Fade）ときは、TrailSystem がこの設定を丸ごと写して持つ（なので trivially copyable のままにする）
```

`TrailAlignment` と `TrailBlend` の間に足す

```cpp
/// <summary>
/// 持ち主（Entity かこの Component）が消えた後の、尾の消え方
/// <para>離れた尾は TrailSystem が設定を写して持つ（ゲームからは触れない）。どれも新しい点は足さない</para>
/// </summary>
enum class TrailAfterOwner {
	Discard, // 消えたフレームに捨てる
	Shrink,  // 点が古い方から寿命で消える（尾が縮む。Life Time 0 の尾は消えないので残さない）
	Fade,    // 形はそのままで、Fade Seconds かけて尾全体を薄くして消す（点は古くしない。Life Time 0 の尾も消える）
};
```

`fadeOutAfterOwner` の1行を、この2行に置き換える

```cpp
	TrailAfterOwner afterOwner = TrailAfterOwner::Shrink; // 持ち主（Entity かこの Component）が消えた後の消え方
	float fadeSeconds = 0.3f;                        // After Owner が Fade のとき、薄くしきるまでの秒（Time::GetDeltaTime で数える。0 なら Discard と同じ）
```

### A-2 `Systems/Effects/TrailSystem.h`

`Update` のコメントの3行目

```cpp
	/// <para>離れた尾は、写しておいた設定の After Owner で消す（Shrink：点が寿命で消えきるまで／Fade：形はそのままで Fade Seconds かけて薄く／Discard：そのフレームに捨てる）。新しい点は足さない</para>
```

### A-3 `Systems/Effects/TrailSystem.cpp`

`TrailState` のコメントと、最後に1つ足す

```cpp
/// <summary>
/// Trail 1つ分の、実行中だけの状態（保存しない。Play / Stop で作り直すと消える）
/// <para>持ち主（Entity か TrailComponent）が消えたら「離れた尾」になる：entity を無効にして、写しておいた設定の After Owner で消えきるまで描く</para>
/// </summary>
struct TrailState {
	// …（前からの5つ）
	float detachedSeconds = 0.0f;           // 持ち主から離れてからの秒（After Owner が Fade のとき、薄くするのに使う）
};
```

`HasOwner` の次に足して、`Detach` を直す（頭のコメント2行と、頭の3行）

```cpp
// 持ち主から離れた後も残す設定か（いつかは消えきる物だけ残す）
// Shrink は Life Time 0 だと点が消えないので残さない。Fade は秒で消えるので Life Time 0 でも残せる（Fade Seconds 0 は残さない）
bool CanKeepAfterOwner(const TrailComponent& settings) {
	switch (settings.afterOwner) {
	case TrailAfterOwner::Shrink:
		return settings.lifeTime > 0.0f;
	case TrailAfterOwner::Fade:
		return settings.fadeSeconds > 0.0f;
	case TrailAfterOwner::Discard:
	default:
		return false;
	}
}

// 持ち主から離す。残す設定で、描いていた尾なら、帯の先を最後の点にして残す。残さないなら点を捨てる（点が無くなった尾は Update が捨てる）
// 残さないのは：After Owner が Discard・消えきらない設定（CanKeepAfterOwner）・描いていなかった（Enabled を外していた・持ち主が無効）・設定を一度も写していない
void Detach(TrailState& trail) {
	const bool canKeep = trail.settings.has_value() && CanKeepAfterOwner(*trail.settings) && trail.isShown;
	trail.detachedSeconds = 0.0f;
	if (!canKeep) {
		trail.points.clear();
	} else if (…（ここから下は前のまま）
```

`AgePoints` の次（`TrimPoints` の前）に足す

```cpp
// 離れた尾を1フレーム進める（写しておいた設定の After Owner で消していく。消えきったら点を空にする＝Update が捨てる）
void UpdateDetached(TrailState& trail, float deltaTime) {
	const TrailComponent& settings = *trail.settings;
	switch (settings.afterOwner) {
	// --- 縮む：点が古い方から寿命で消える ---
	case TrailAfterOwner::Shrink:
		AgePoints(trail.points, settings.lifeTime, deltaTime);
		break;
	// --- 薄くする：点は古くしない（形はそのまま）。秒を数えて、Fade Seconds が過ぎたら消す ---
	case TrailAfterOwner::Fade:
		trail.detachedSeconds += deltaTime;
		if (trail.detachedSeconds >= settings.fadeSeconds) {
			trail.points.clear();
		}
		break;
	// --- 残さない（Detach で点を捨てているので、ここには来ない）---
	case TrailAfterOwner::Discard:
	default:
		trail.points.clear();
		break;
	}
}

// 離れた尾の濃さ（1：そのまま → 0：消えた。尾全体の色の A に掛ける）。Fade 以外はいつも 1
float CalcDetachedAlpha(const TrailState& trail) {
	if (!trail.settings.has_value() || trail.settings->afterOwner != TrailAfterOwner::Fade || trail.settings->fadeSeconds <= 0.0f) {
		return 1.0f;
	}
	return std::clamp(1.0f - trail.detachedSeconds / trail.settings->fadeSeconds, 0.0f, 1.0f);
}
```

`DescribeTrail` の Fade Out After Owner の1行を、この2行に置き換える

```cpp
	ui.Field("After Owner###afterOwner", value.afterOwner, Tip("持ち主（Entity かこの Component）が消えた後の消え方（新しい点は足さない）。Discard：すぐ捨てる／Shrink：点が寿命で消えて縮む（Life Time 0 なら捨てる）／Fade：形はそのままで、Fade Seconds かけて薄くして消す"));
	ui.Field("Fade Seconds###fadeSeconds", value.fadeSeconds, AtLeast(0.0f).Tip("After Owner が Fade のとき、薄くしきるまでの秒。0 ならすぐ捨てる"));
```

`TrailSystem::Update` の 2.（見出しと、`AgePoints` の1行を `UpdateDetached` に）

```cpp
	// ===== 2. 離れた尾を After Owner で消していく（縮める・薄くする）。帯にならなくなった尾を捨てる =====
	for (TrailState& trail : trails) {
		if (IsDetached(trail) && trail.settings.has_value()) {
			UpdateDetached(trail, deltaTime);
		}
	}
```

`TrailSystem::Draw` の `config.color` の1行を、この3行に置き換える

```cpp
		// 明るさの倍率はリニアで掛ける（Color.h）。離れて薄くしている尾は、A に濃さを掛ける
		// Normal も Add も「色 × A」を重ねる（RenderStates の SrcBlend が SRC_ALPHA）ので、A を 0 へ下げれば、どちらも色と明るさごと 0 になる
		config.color = MultiplyAlpha(ApplyIntensity(component->color, component->intensity), isDetached ? CalcDetachedAlpha(trail) : 1.0f);
```

ここでビルドできる（Sandbox は D-2 まで `fadeOutAfterOwner` が無いと言われるので、エンジンだけ）。

## パートB Particle：出し終わったら Entity を消す

### B-1 `Components/Effects/ParticleSystemComponent.h`

`ParticleSpace` の次に足す

```cpp
/// <summary>
/// 出し終わったとき（出すのをやめて、粒が全部消えたとき）にすること。Unity の Stop Action
/// <para>出すのをやめる＝Looping でなく Duration が過ぎた・ゲームが ParticleSystem::Stop を呼んだ。一度も Play していない物（Play On Start を外して Play を呼んでいない・Emit だけ）は出し終わりにならない</para>
/// </summary>
enum class ParticleStopAction {
	None,    // 何もしない（Entity は残る。また Play できる）
	Destroy, // この Component を持つ Entity を消す（子も一緒に消える。飛び散る物の Prefab を Instantiate して、出し終わったら勝手に消す）
};
```

`duration` の次に足す

```cpp
	float duration = 1.0f;   // 1回の長さ（秒）
	ParticleStopAction stopAction = ParticleStopAction::None; // 出し終わったら（出すのをやめて、粒が全部消えたら）すること
```

### B-2 `Systems/Effects/ParticleSystem.h`

`Update` のコメント（1行目を直して、`<para>` を1行直して1行足す）

```cpp
	/// <summary>
	/// 毎フレーム1回、EntityManager::FlushDestroy の前に呼ぶ（ワールド行列も計算済み。Stop Action の Destroy は、すぐ後の FlushDestroy で消える）
	/// <para>消えた・外した Particle System の粒を捨て、出して、動かして、寿命の粒を消す。出し終わった物は Stop Action をする（Destroy なら Entity を消す予約）</para>
	/// <para>Instantiate した Prefab の Component は次のフレームの頭に付くので、Play On Start もそのフレームから（付いて初めて見つけたとき）</para>
	/// </summary>
```

`Stop` のコメントに1行足す・`IsAlive` のコメント

```cpp
	// 出すのをやめる。clear：出ている粒も消す（false なら、出ている粒は寿命まで残る）
	// 出し始めた後に呼ぶと「出し終わり」になる（粒が全部消えたら Stop Action。Looping の物も）
	static void Stop(Handle<Entity> entity, bool clear = false);
	// 今すぐ count 個出す（Play していなくても出る。Component の値で出す）。Component が付く前（RequestAdd した同じフレーム）は何もしない
	static void Emit(Handle<Entity> entity, int count);
	// 出している途中か、粒が1つでも残っているか（Burst が全部消えたら false。出し終わったら消すだけなら、Stop Action の Destroy で足りる）
	static bool IsAlive(Handle<Entity> entity);
```

### B-3 `Systems/Effects/ParticleSystem.cpp`

`EmitterState` の `isStartChecked` の次に2つ足す

```cpp
	bool isStartChecked = false;         // Play On Start を見たか（最初に見つけたときに1回だけ。先に Play・Stop を呼ばれたら見ない）
	bool hasPlayed = false;              // 一度でも出し始めたか（Play On Start・Play。Stop Action は、出し始めた物が出し終わったときだけ）
	bool isStopActionDone = false;       // Stop Action をもうしたか（1回の出し終わりに1回だけ。もう一度 Play したら戻す）
```

`Restart` に2行足す

```cpp
void Restart(EmitterState& emitter) {
	emitter.isStartChecked = true;
	emitter.hasPlayed = true;
	emitter.isStopActionDone = false; // 出し終わった後にもう一度 Play したら、また出し終わりを待つ（None で残していた物）
	emitter.isPlaying = true;
	// …（前のまま）
```

`IsSleeping` の次に足す

```cpp
// 出し終わったか（出し始めた物が、出すのをやめて＝Duration が過ぎた・Stop を呼んだ、粒が全部消えた）
bool IsFinished(const EmitterState& emitter) { return emitter.hasPlayed && !emitter.isPlaying && emitter.particles.empty(); }

// 出し終わったら、Stop Action を1回だけする（ゲームが動いている間だけ呼ぶ。停止中の Preview では Entity を消さない）
void ApplyStopAction(EmitterState& emitter, const ParticleSystemComponent& component) {
	if (emitter.isStopActionDone || !IsFinished(emitter)) {
		return;
	}
	emitter.isStopActionDone = true;
	switch (component.stopAction) {
	// --- Entity を消す（ParticleSystem::Update のすぐ後の EntityManager::FlushDestroy で、このフレームのうちに消える。子も一緒。粒はもう無いので、見た目は変わらない）---
	case ParticleStopAction::Destroy:
		EntityManager::Destroy(emitter.entity);
		break;
	// --- 何もしない ---
	case ParticleStopAction::None:
	default:
		break;
	}
}
```

`UpdatePreview` の最後の `if` を `IsFinished` に（同じ式をまとめただけ）

```cpp
	// 1回で終わる物は、出し終わって粒が全部消えたら Preview も終わる（ボタンが ▶ Preview に戻る）
	if (IsFinished(*emitter)) {
		ParticleSystem::StopPreview();
	}
```

`DescribeParticleSystem` の Duration の次に足す

```cpp
	ui.Field("Stop Action###stopAction", value.stopAction, Tip("出し終わったら（出すのをやめて＝Looping でなく Duration が過ぎた・Stop を呼んだ、粒が全部消えたら）すること。Destroy：この Entity を消す（子も一緒。飛び散る物の Prefab に）"));
```

`ParticleSystem::Update` の 2.（`ForEach` の中）の最後に足す

```cpp
		UpdateEmitter(emitter, component, WorldMatrixOf(entity), deltaTime);
		// 出し終わったら Stop Action（Destroy なら、この Entity を消す予約）
		ApplyStopAction(emitter, component);
	});
```

ここでビルドできる。

## パートC Input：OS のカーソルをゲーム画面に閉じ込める

### C-1 `Input/MouseInput.h`・`.cpp`

`.h`：`IsButtonReleased` の次に足す

```cpp
	/// <summary>どれかのボタン（8個のどれか）が押されているか。</summary>
	bool IsAnyButtonPressed() const;

	/// <summary>どれかのボタンを、このフレームに離したか。</summary>
	bool IsAnyButtonReleased() const;
```

`.cpp`：include に `<iterator>`（`std::size` のため）を足し、ファイルの最後に足す

```cpp
#include <format>
#include <cstring>
#include <iterator>
```

```cpp
bool MouseInput::IsAnyButtonPressed() const {
	// rgbButtons は DIMOUSESTATE2 の 8 個のボタン（上の 0x80 が押している印）
	for (const BYTE button : state_.rgbButtons) {
		if ((button & 0x80) != 0) {
			return true;
		}
	}
	return false;
}

bool MouseInput::IsAnyButtonReleased() const {
	for (int i = 0; i < static_cast<int>(std::size(state_.rgbButtons)); ++i) {
		if (IsButtonReleased(i)) {
			return true;
		}
	}
	return false;
}
```

### C-2 `Input/InputManager.h`

include に `<optional>` を足す

```cpp
#include <cstdint>
#include <optional>
```

`ShouldHideCursor` のコメントを直し、その次から `GetMouseDeltaY` までをこれに置き換える

```cpp
	/// <summary>
	/// 今 OS のカーソルを隠す所か（SetCursorVisible(false) を頼まれていて、マウスがゲーム画面の上か、カーソルを閉じ込めている。Play 中かは見ない）
	/// <para>WindowManager が毎フレーム（UpdateAll の最後）見て、メインのウィンドウに渡す</para>
	/// </summary>
	static bool ShouldHideCursor();

	/// <summary>
	/// OS のカーソルをゲーム画面の中に閉じ込めるか。true で頼む（外へ出られなくなり、画面の端で止まる。真ん中へ戻し続けるのではない＝Unity の CursorLockMode.Confined）
	/// <para>閉じ込めている間も GetMouseDeltaX/Y は動いた分を返す（カーソルが画面の端で止まっていても）。エディタの Game ビューの上で ImGui に取られて 0 にもならない</para>
	/// <para>本当に閉じ込めるのは、Play 中（一時停止中は放す）で、メインのウィンドウが手前の間だけ（Alt+Tab などで手前でなくなったら放す。製品版は手前に戻るとまた閉じ込める）</para>
	/// <para>エディタ：閉じ込めている間は、エディタのパネルをマウスで触れない。Shift+F1 で放す（手前でなくなったときも放す）。Game ビューの絵を左クリックすると、また閉じ込める（そのクリックはゲームに渡さない）</para>
	/// <para>シーンを変えると（Stop も）false に戻る。閉じ込めたいシーンが、自分で頼む。隠すのは SetCursorVisible（別に頼む）</para>
	/// </summary>
	static void SetCursorConfined(bool confined) { GetInstance()->isCursorConfineRequested_ = confined; }

	/// <summary>SetCursorConfined で頼んだ値（今本当に閉じ込めているかではない。それは IsCursorConfineActive）</summary>
	static bool IsCursorConfined() { return GetInstance()->isCursorConfineRequested_; }

	/// <summary>今本当に OS のカーソルをゲーム画面に閉じ込めているか（前のフレームの終わりに WindowManager が決めた値。一時停止中・手前でない・エディタで放した間は false）</summary>
	static bool IsCursorConfineActive() { return GetInstance()->isCursorConfineActive_; }

	/// <summary>
	/// 今カーソルを閉じ込める四角（メインのウィンドウのクライアント座標。ゲーム画面の内側に丸めた物）。閉じ込めない所なら空（Play 中かは見ない）
	/// <para>閉じ込めるのは：SetCursorConfined(true) を頼まれていて、メインのウィンドウが手前で、ゲーム画面がある（エディタは Game ビューの絵が見えていて、Shift+F1 で放していない）とき</para>
	/// <para>WindowManager が毎フレーム（UpdateAll の最後）見て、メインのウィンドウに渡し、SetCursorConfineActive で結果を返す</para>
	/// </summary>
	static std::optional<RECT> GetCursorConfineRect();

	/// <summary>今本当にカーソルを閉じ込めたか。WindowManager が毎フレーム（UpdateAll の最後）渡す</summary>
	static void SetCursorConfineActive(bool active) { GetInstance()->isCursorConfineActive_ = active; }

	/// <summary>
	/// 隠す・閉じ込める頼みを戻す（SetCursorVisible(true)・SetCursorConfined(false)。エディタで Shift+F1 で放したのも戻す）
	/// <para>シーンを変えたとき（Stop も）に SceneManager が呼ぶ。前のシーンの頼みを持ち越さない</para>
	/// </summary>
	static void ResetCursorRequests();

	/// <summary>
	/// 前フレームからのX移動量 (右が正)。
	/// <para>単位はマウスが数えた数（mickey。DirectInput の相対の値）。ピクセルではない。コントロールパネルのポインターの速さ・加速は掛からない</para>
	/// <para>どれだけ動かすと 1 になるかはマウス（DPI）で変わる。ふつうの設定（ポインターの速さが真ん中・加速なし）なら、Windows の矢印の 1 ピクセルとおよそ同じ</para>
	/// <para>ゲームで使うときは「1 カウントを何ピクセルにするか」の倍率（感度）を掛けてピクセルの量にし、GameScreen::PixelToNdc で画面の位置の量にする</para>
	/// <para>カーソルを閉じ込めている間（SetCursorConfined）は、画面の端でカーソルが止まっていても動いた分を返す</para>
	/// </summary>
	static long GetMouseDeltaX();

	/// <summary>前フレームからのY移動量 (下が正)。単位は GetMouseDeltaX と同じ（マウスが数えた数）</summary>
	static long GetMouseDeltaY();
```

`#ifdef USE_IMGUI` の中：`SetGameViewRect` のコメントを直し、`SetGameViewHovered` の次に足す

```cpp
	// Game ビューの絵がウィンドウのどこにあるか（左上・大きさ。クライアント座標）。EditorViewport が毎フレーム渡す（見えていない＝ほかのタブの裏なら、大きさ 0）
```

```cpp
	static void SetGameViewHovered(bool hovered) { GetInstance()->isGameViewHovered_ = hovered; }
	// カーソルを閉じ込めるのを、エディタで放しているか（Shift+F1 を押した・手前でなくなった。Game ビューを左クリックするか、シーンを変えると戻る）。Game ビューに出す案内に使う
	static bool IsCursorConfineReleasedInEditor() { return GetInstance()->isCursorConfineReleasedInEditor_; }
#endif
```

private：前からの3つ（`initInternal`・`finalizeInternal`・`updateInternal`）を大文字で始める名前に変える（ほかの関数と同じ書き方に。.cpp の定義と呼ぶ所は C-3）

```cpp
	// 内部ヘルパー
	void InitInternal(HINSTANCE hInstance, HWND hwnd);
	void FinalizeInternal();
	void UpdateInternal();
```

private：`GetGameViewRect` の次から `#ifdef USE_IMGUI` の中の最後までをこれに置き換える

```cpp
	// ゲーム画面がウィンドウのどこにあるか（左上・大きさ。クライアント座標）
	static void GetGameViewRect(Vector2& topLeft, Vector2& size);
	// メインのウィンドウが手前か（カーソルを閉じ込めるのは、手前の間だけ）
	bool IsMainWindowForeground() const { return mainHwnd_ != nullptr && GetForegroundWindow() == mainHwnd_; }
	// また閉じ込めたクリックを、ボタンを全部離すまでゲームに渡さない（フレームの頭。UpdateInternal から）
	void UpdateMouseSwallow();
#ifdef USE_IMGUI
	// エディタで閉じ込めるのを放す・また閉じ込める（フレームの頭、ゲームが入力を読む前。UpdateInternal から）
	void UpdateEditorCursorConfine();
#endif

	HWND activeHwnd_ = nullptr;
	HWND mainHwnd_ = nullptr; // Initialize で受け取ったウィンドウ（製品版のゲーム画面＝このウィンドウの中）
	bool isCursorVisible_ = true;           // ゲーム画面の上で OS のカーソルを出すか（SetCursorVisible で頼んだ値）
	bool isCursorConfineRequested_ = false; // OS のカーソルをゲーム画面に閉じ込めるか（SetCursorConfined で頼んだ値）
	bool isCursorConfineActive_ = false;    // 今本当に閉じ込めているか（WindowManager が SetCursorConfineActive で渡す）
	bool isMouseButtonSwallowed_ = false;   // マウスのボタンを「何も押していない」扱いにしているか（また閉じ込めたクリック。ボタンを全部離した次のフレームに戻る）

#ifdef USE_IMGUI
	bool mouseInViewport_ = false;           // マウスがViewportにあるか
	bool viewportFocused_ = false;           // Viewportがフォーカスされてるか
	Vector2 gameViewTopLeft_ = {0.0f, 0.0f}; // Game ビューの絵の左上（クライアント座標）
	Vector2 gameViewSize_ = {0.0f, 0.0f};    // Game ビューの絵の大きさ（0なら、まだ描いていない）
	bool isGameViewHovered_ = false;         // マウスが Game ビューの絵の上か（ほかの窓に隠れていない所だけ）
	bool isCursorConfineReleasedInEditor_ = false; // カーソルを閉じ込めるのを、エディタで放しているか（Shift+F1・手前でなくなった）
#endif
```

### C-3 `Input/InputManager.cpp`

前からの3つの名前を変える（呼ぶ所の3行と、定義の頭。中身はそのまま。`UpdateInternal` の中身は下で足す）

```cpp
void InputManager::Initialize(HINSTANCE hInstance, HWND hwnd) { GetInstance()->InitInternal(hInstance, hwnd); }
void InputManager::Release() { GetInstance()->FinalizeInternal(); }
void InputManager::Update() { GetInstance()->UpdateInternal(); }
```

```cpp
void InputManager::InitInternal(HINSTANCE hInstance, HWND hwnd) {
```

```cpp
void InputManager::FinalizeInternal() {
```

include に `<cmath>` を足す（`std::ceil`・`std::floor`）

```cpp
#include <cmath>
#include <format>
```

マウスのボタン3つ（`IsMouseButtonPressed`・`Triggered`・`Released`）の、ImGui の行の次に1行ずつ足す

```cpp
bool InputManager::IsMouseButtonPressed(int b) {
	if (!IsActive()) return false;
	if (IsMouseCapturedByImGui()) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonPressed(b);
}
```

`ShouldHideCursor` の頭（`isCursorVisible_` を見た次）に足し、関数の次に「OS のカーソルを閉じ込める」の大見出しを足す

```cpp
	// 閉じ込めている間は、カーソルはいつもゲーム画面の中（エディタは ImGui がマウスを見ないので、Game ビューの「上か」が取れない。WindowManager が先に閉じ込めるかを決めて渡している）
	if (GetInstance()->isCursorConfineActive_) {
		return true;
	}
```

```cpp
//=============================================================================
// OS のカーソルを閉じ込める
//=============================================================================
// ===== 閉じ込める四角 =====
std::optional<RECT> InputManager::GetCursorConfineRect() {
	InputManager* self = GetInstance();
	if (!self->isCursorConfineRequested_ || !self->IsMainWindowForeground()) {
		return std::nullopt;
	}
#ifdef USE_IMGUI
	if (self->isCursorConfineReleasedInEditor_) {
		return std::nullopt; // Shift+F1 で放している（Game ビューをクリックするまで）
	}
#endif
	// --- ゲーム画面（エディタは Game ビューの絵、製品版は黒帯の内側）を、内側へ丸めた整数の四角にする ---
	// 外側へ丸めると、端の 1 ピクセルが絵の外（エディタではパネルの縁）になる
	Vector2 topLeft;
	Vector2 size;
	GetGameViewRect(topLeft, size);
	const RECT rect = {
	    static_cast<LONG>(std::ceil(topLeft.x)),
	    static_cast<LONG>(std::ceil(topLeft.y)),
	    static_cast<LONG>(std::floor(topLeft.x + size.x)),
	    static_cast<LONG>(std::floor(topLeft.y + size.y)),
	};
	if (rect.right - rect.left < 1 || rect.bottom - rect.top < 1) {
		return std::nullopt; // ゲーム画面がまだ無い（最初のフレーム・エディタで Game ビューがほかのタブの裏）
	}
	return rect;
}

// ===== シーンを変えたとき =====
void InputManager::ResetCursorRequests() {
	InputManager* self = GetInstance();
	self->isCursorVisible_ = true;
	self->isCursorConfineRequested_ = false;
#ifdef USE_IMGUI
	self->isCursorConfineReleasedInEditor_ = false; // 次に閉じ込めるシーンは、Shift+F1 で放す前から始まる
#endif
}

// ===== また閉じ込めたクリックを、ゲームに渡さない =====
// ボタンを全部離して、離した瞬間（Released）も過ぎたフレームに戻す（押した・離したの片方だけがゲームに届かないように）
void InputManager::UpdateMouseSwallow() {
	if (isMouseButtonSwallowed_ && !mouse_.IsAnyButtonPressed() && !mouse_.IsAnyButtonReleased()) {
		isMouseButtonSwallowed_ = false;
	}
}

#ifdef USE_IMGUI
// ===== エディタで放す・また閉じ込める =====
// フレームの頭（ゲームが入力を読む前）に決めるので、また閉じ込めたクリックは、このフレームからゲームに渡さない
// 「閉じ込めているか」は前のフレームの終わりに WindowManager が決めた値（isCursorConfineActive_）
void InputManager::UpdateEditorCursorConfine() {
	const bool isForeground = IsMainWindowForeground();
	if (isCursorConfineActive_) {
		// --- 閉じ込めている：Shift+F1 か、手前でなくなったら放す（DirectInput のキーボードはエディタの ImGui に関係なく読める）---
		const bool isShift = keyboard_.IsPressed(DIK_LSHIFT) || keyboard_.IsPressed(DIK_RSHIFT);
		if (!isForeground || (isShift && keyboard_.IsTriggered(DIK_F1))) {
			isCursorConfineReleasedInEditor_ = true;
		}
		return;
	}
	// --- 放している：Game ビューの絵を左クリックしたら、また閉じ込める。そのクリックはゲームに渡さない（撃つなどが起きないように）---
	// 「絵の上か」は前のフレームの描画で決めた値（ほかの窓に隠れていない所だけ）
	if (isCursorConfineReleasedInEditor_ && isCursorConfineRequested_ && isForeground && isGameViewHovered_ && mouse_.IsButtonTriggered(0)) {
		isCursorConfineReleasedInEditor_ = false;
		isMouseButtonSwallowed_ = true;
	}
}
#endif
```

`UpdateInternal` の最後に足す・`IsMouseCapturedByImGui` の頭に足す

```cpp
void InputManager::UpdateInternal() {
	keyboard_.Update();
	mouse_.Update();
	gamepad_.Update();
	// --- OS のカーソルを閉じ込めるの（ゲームが入力を読む前に決める）---
	UpdateMouseSwallow();
#ifdef USE_IMGUI
	UpdateEditorCursorConfine();
#endif
}

bool InputManager::IsMouseCapturedByImGui() {
#ifdef USE_IMGUI
	// カーソルを閉じ込めている間は、マウスはゲームの物（カーソルは Game ビューの中にいて、ImGui はマウスを見ていない）
	if (GetInstance()->isCursorConfineActive_) return false;
	if (GetInstance()->mouseInViewport_) return false;
	// …（前のまま）
```

### C-4 `Window/Win32Window.h`・`.cpp`

`.h`：include に `<optional>` を足し、`SetCursorHidden` の次と private の最後に足す

```cpp
#include <string>
#include <optional>
#include <functional>
```

```cpp
	/// <summary>
	/// OS のカーソルを、このウィンドウの中の四角（クライアント座標）に閉じ込める。空なら放す。WindowManager が毎フレーム渡す（閉じ込める所かは InputManager::GetCursorConfineRect が決める）
	/// <para>閉じ込めている間は毎フレーム頼み直す（ウィンドウ・Game ビューが動いた、ほかのアプリや Windows が外した、に追いつく）</para>
	/// <para>このウィンドウが手前でなくなったら（WM_ACTIVATE。assert のメッセージボックスが出たときも）、次のフレームを待たずにすぐ放す</para>
	/// </summary>
	void SetCursorClip(const std::optional<RECT>& clientRect);
```

```cpp
	bool isCursorHidden_ = false;  // クライアント領域で OS のカーソルを隠すか（WM_SETCURSOR で見る）
	bool isCursorClipped_ = false; // OS のカーソルを閉じ込めているか（ClipCursor を頼んだか。放すときに1回だけ外す）
	WindowConfig config_;

	// 閉じ込めているカーソルを放す（閉じ込めていなければ何もしない。ClipCursor はほかのアプリと共有の物なので、閉じ込めていないのに外さない）
	void ReleaseCursorClip();
```

`.cpp`：`SetCursorHidden` の次に足す

```cpp
//=============================================================================
// OS のカーソルを閉じ込める
//=============================================================================
// ===== 閉じ込める・放す =====
void Win32Window::SetCursorClip(const std::optional<RECT>& clientRect) {
	if (!clientRect.has_value()) {
		ReleaseCursorClip();
		return;
	}
	// ClipCursor はスクリーン座標（モニターの左上が 0,0）なので、クライアント座標から直す
	POINT topLeft = {clientRect->left, clientRect->top};
	POINT bottomRight = {clientRect->right, clientRect->bottom};
	ClientToScreen(hwnd_, &topLeft);
	ClientToScreen(hwnd_, &bottomRight);
	const RECT screenRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
	// 毎フレーム頼み直す（同じ四角でも。ほかのアプリ・Windows が外していることがある）
	ClipCursor(&screenRect);
	isCursorClipped_ = true;
}

// ===== 放す =====
void Win32Window::ReleaseCursorClip() {
	if (!isCursorClipped_) {
		return;
	}
	ClipCursor(nullptr);
	isCursorClipped_ = false;
}
```

`WindowProc` の `WM_SIZE` の前に足し、`WM_DESTROY` を直す

```cpp
	// 手前になった・手前でなくなった。手前でなくなったら、閉じ込めているカーソルをすぐ放す
	// （Alt+Tab・Windows キー・assert のメッセージボックス。メッセージボックスの間はフレームが回らないので、次のフレームを待つとカーソルがボタンへ届かない）
	case WM_ACTIVATE: {
		if (self && LOWORD(wparam) == WA_INACTIVE) {
			self->ReleaseCursorClip();
		}
		break;
	}
```

```cpp
	// ウィンドウが破棄された（閉じ込めていたカーソルを放して終わる。ClipCursor はほかのアプリと共有なので、残さない）
	case WM_DESTROY: {
		if (self) {
			self->ReleaseCursorClip();
		}
		return 0;
	}
```

`ReleaseCursorClip` は private だが、`WindowProc` は同じクラスの static 関数なので呼べる。

### C-5 `Editor/ImGuiManager.h`・`.cpp`

`.h`：`ClearRequests` の次に足す。下の USE_IMGUI でない方（何もしないスタブ）にも1行足す

```cpp
	/// <summary>
	/// エディタ（ImGui）がマウスを見るか。false の間は、どのパネルもマウスで触れない（ホバーもしない）
	/// <para>ゲームが OS のカーソルを Game ビューに閉じ込めている間、WindowManager が false にする（ImGuiConfigFlags_NoMouse）</para>
	/// </summary>
	static void SetMouseEnabled(bool enabled);
```

```cpp
	static void AddDrawRequest(std::function<void()>) {}
	static void SetMouseEnabled(bool) {}
};
```

`.cpp`：`ClearRequests` の次に足す

```cpp
//=============================================================================
// マウスを見るか
//=============================================================================
void ImGuiManager::SetMouseEnabled(bool enabled) {
	MY_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	// NoMouse の間、ImGui はどの窓もホバーしない（マウスの位置は覚え続けるので、戻したフレームから今の位置で動く）
	ImGuiIO& io = ImGui::GetIO();
	if (enabled) {
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	} else {
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
	}
}
```

### C-6 `Window/WindowManager.cpp`

`UpdateCursor` を、コメントごと置き換える

```cpp
// 閉じ込める・隠すのは「メインのウィンドウのシーンが Play 中（一時停止・停止中は放す・出す）」のときだけ
// 閉じ込める：ゲームが頼んでいて、メインのウィンドウが手前で、ゲーム画面がある（エディタは Shift+F1 で放していない）
// 隠す　：ゲームが頼んでいて、マウスがゲーム画面の上か、閉じ込めている
// ゲーム画面（エディタは Game ビューの絵、製品版は黒帯の内側）は、メインのウィンドウの中にある
// 頼みと場所は InputManager、Play 中かは SceneManager、OS のカーソルは Win32Window が持つ。SceneManager と Win32Window を組で持っているのはここなので、ここでつなぐ
// エディタの「Game ビューの絵の上か・どこか」は描画（PreRenderAll の EditorViewport::Render）で決まるので、ここで読むのは前のフレームの値（1フレーム遅れは目に見えない）
void WindowManager::UpdateCursor() {
	if (windows_.empty()) {
		return;
	}
	const WindowSet& main = windows_[0];
	const bool isPlaying = main.sceneManager && main.sceneManager->GetPlayState() == PlayState::Playing;

	// --- 閉じ込める（隠すかは「閉じ込めているか」も見るので先に）---
	const std::optional<RECT> confineRect = isPlaying ? InputManager::GetCursorConfineRect() : std::nullopt;
	main.window->SetCursorClip(confineRect);
	InputManager::SetCursorConfineActive(confineRect.has_value());
#ifdef USE_IMGUI
	// 閉じ込めている間は、エディタのパネルをマウスで触らない（カーソルは Game ビューの中だが、上に重なった窓・パネルの縁を押せてしまうので）
	if (main.window->GetWindowConfig().isImGui) {
		ImGuiManager::SetMouseEnabled(!confineRect.has_value());
	}
#endif

	// --- 隠す ---
	main.window->SetCursorHidden(isPlaying && InputManager::ShouldHideCursor());
}
```

`ImGuiManager.h` は `WindowManager.h` が読んでいるので、include は足さなくてよい。

### C-7 `Editor/Viewport/ViewportWindow.h`・`.cpp`

`.h`：`IsHovered` の次と、メンバーの最後に足す

```cpp
	bool IsHovered() const { return hovered_; }
	// このフレームに絵を描いたか（ほかのタブの裏・たたんでいるときは false。そのときの GetImageMin などは当てにならない）
	bool IsVisible() const { return visible_; }
```

```cpp
	bool hovered_ = false;
	bool visible_ = false; // ImGui::Begin が true を返したか（中身を描く所か）
```

`.cpp`：`ImGui::Begin` の行

```cpp
	visible_ = ImGui::Begin(name_.c_str(), nullptr, flags); // ほかのタブの裏・たたんでいると false（中身は描かれない）
```

### C-8 `Editor/Viewport/EditorViewport.h`・`.cpp`

`.h`：private の `#ifdef USE_IMGUI` の頭に足す

```cpp
#ifdef USE_IMGUI
	// Game ビューの絵の左上に、OS のカーソルを閉じ込める・放すの案内を出す（gameView_.Draw の中から。閉じ込めた直後の数秒と、Shift+F1 で放している間）
	void DrawCursorConfineHint();
	double cursorConfineStartTime_ = -1.0; // 閉じ込め始めた時刻（ImGui::GetTime の秒。閉じ込めていない間は負）

	std::shared_ptr<DebugCamera> debugCamera_; // …（前のまま）
```

`.cpp`：include に `<string_view>` を足す

```cpp
#include "EditorViewport.h"

#include <string_view>

#include "MyEngine/Camera/Camera.h"
```

`.cpp`：`Render` の Game の所

```cpp
	// --- Game ---
	gameView_.Draw(gameRT, [this] { DrawCursorConfineHint(); });
	// ゲームのマウス座標（InputManager::GetMousePositionInGame）は、この絵の中を画面として数える
	// ほかのタブの裏で見えていないときは、大きさ 0（画面が無い。OS のカーソルを、見えない所へ閉じ込めないように）
	const ImVec2& gameMin = gameView_.GetImageMin();
	const ImVec2 gameSize = gameView_.IsVisible() ? gameView_.GetImageSize() : ImVec2{0.0f, 0.0f};
	InputManager::SetGameViewRect({gameMin.x, gameMin.y}, {gameSize.x, gameSize.y});
```

`Render` の次に足す

```cpp
//=============================================================================
// OS のカーソルを閉じ込める・放すの案内
//=============================================================================
// 閉じ込めている間はパネルをマウスで触れないので、放し方を見せる（Unreal の「Shift+F1 でマウスカーソルを表示」と同じ）
// 閉じ込めた直後の数秒だけ出す（ずっと出すと、ゲームの絵の上に残って邪魔なので）。放している間は、また閉じ込める方法を出し続ける
void EditorViewport::DrawCursorConfineHint() {
	constexpr double kConfineHintSeconds = 3.0;              // 閉じ込めた後、案内を出しておく秒
	constexpr float kHintPadding = 6.0f;                     // 絵の端・文字の周りの余白（ピクセル）
	constexpr ImU32 kHintBackColor = IM_COL32(0, 0, 0, 160); // 文字の後ろの帯（半分透けた黒）
	constexpr ImU32 kHintTextColor = IM_COL32(255, 255, 255, 255);

	// --- 出す文字を決める（空なら出さない）---
	std::string_view text;
	const double now = ImGui::GetTime();
	if (InputManager::IsCursorConfineActive()) {
		if (cursorConfineStartTime_ < 0.0) {
			cursorConfineStartTime_ = now;
		}
		if (now - cursorConfineStartTime_ < kConfineHintSeconds) {
			text = "Shift+F1 : カーソルを画面の外へ出す";
		}
	} else {
		cursorConfineStartTime_ = -1.0;
		if (InputManager::IsCursorConfined() && InputManager::IsCursorConfineReleasedInEditor()) {
			text = "クリックでカーソルを画面に閉じ込める";
		}
	}
	if (text.empty()) {
		return;
	}

	// --- 絵の左上に、帯と文字を描く（ImGui は文字の始まりと終わりのポインタで受け取る）---
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size());
	const ImVec2 min = {gameView_.GetImageMin().x + kHintPadding, gameView_.GetImageMin().y + kHintPadding};
	const ImVec2 max = {min.x + textSize.x + kHintPadding * 2.0f, min.y + textSize.y + kHintPadding * 2.0f};
	drawList->AddRectFilled(min, max, kHintBackColor);
	drawList->AddText({min.x + kHintPadding, min.y + kHintPadding}, kHintTextColor, text.data(), text.data() + text.size());
}
```

`std::string_view` は、決まった文字（ここでは文字列リテラル）を、写さずに「始まりと長さ」で指す型（C++17）。文字を持つ `std::string` と違って作るときにメモリを取らないので、毎フレーム出す決まった文字に向いている。ImGui は `const char*` の始まりと終わりで受け取るので、`data()` と `data() + size()` を渡す（終わりを渡すので、文字の最後の 0 に頼らない）。

### C-9 `Scene/SceneManager.cpp`

`ChangeScene` の `SetCursorVisible(true)` の所

```cpp
	// OS のカーソルを隠す・閉じ込める頼みも持ち越さない（したいシーンが自分で頼む。タイトルへ戻ったら矢印が出ない・外へ出られない、という戻し忘れが起きないように）
	// 実際に放すのは、このフレームの終わり（WindowManager::UpdateCursor）
	InputManager::ResetCursorRequests();
```

ここでビルドできる。

## パートD Sandbox で確かめる

見本の Script・Component と、それを並べたテストシーン TrailParticleCursorTest（SampleScene は変えない）。

### D-1 `Sandbox/Source/Scripts/CursorTrailSamples.h`（全体を差し替え）

```cpp
#pragma once
#include "MyEngine/Components/Effects/TrailComponent.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/ECS/Script.h"

//=============================================================================
// 照準の UI と OS のカーソル（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 画面に重ねる UI（Anchor は Center）を、マウスか右スティックで動かす照準にする（Sandbox のテスト用）
/// <para>Start で OS のカーソルを隠し（ゲーム画面の上だけ）、Confine On Start ならゲーム画面に閉じ込める。C キーで隠す、L キーで閉じ込めるを切り替える</para>
/// <para>閉じ込めている間：マウスの動いた量（数えた数）× Pixels Per Count ＝ピクセルの量 → 画面の位置の量（GameScreen::PixelToNdc）を足す（画面の端でも動く）</para>
/// <para>閉じ込めていない間：マウスは画面の位置（端が ±1）。右スティックはいつもピクセルの量 → 画面の位置の量</para>
/// </summary>
class AimCursorSample : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float padSpeed_ = 1200.0f;    // 右スティックで照準を動かす速さ（ピクセル／秒。倒しきったとき）
	bool confineOnStart_ = true;  // Start で OS のカーソルをゲーム画面に閉じ込める（照準をマウスの動いた量で動かす）
	float pixelsPerCount_ = 1.0f; // 閉じ込めている間、マウスが1つ数えたら照準を何ピクセル動かすか（感度）

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
/// <para>弾は Flight Time 秒飛んだら、End のやり方でいなくなる。After Owner を変えて、尾の残り方（捨てる・縮む・薄くする）を見比べる</para>
/// </summary>
struct TrailFadeShooter {
	float interval = 0.8f;                                // 撃つ間隔（秒）
	float speed = 12.0f;                                  // 弾の速さ（m/s）
	float flightTime = 0.6f;                              // 弾が飛ぶ秒（過ぎたら End のやり方でいなくなる）
	TrailFadeEnd end = TrailFadeEnd::DestroyEntity;       // 弾がいなくなる形
	TrailAfterOwner afterOwner = TrailAfterOwner::Fade;   // 弾の Trail の After Owner（弾がいなくなった後の尾の消え方）
	float fadeSeconds = 0.3f;                             // 弾の Trail の Fade Seconds（After Owner が Fade のとき、薄くしきるまでの秒）
	float trailLifeTime = 0.6f;                           // 弾の Trail の Life Time（点が消えるまでの秒）
	TrailBlend blend = TrailBlend::Add;                   // 弾の Trail の Blend（Add と Normal の両方で薄くなるのを見る）
	Vector4 trailColor = {0.4f, 0.8f, 1.0f, 1.0f};        // 弾の Trail の色（RGBA。0〜1）
	float timer = 0.0f;                                   // 次に撃つまでの秒（実行中だけ）
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

### D-2 `Sandbox/Source/Scripts/CursorTrailSamples.cpp`（全体を差し替え）

```cpp
#include "Source/Scripts/CursorTrailSamples.h"

#include <algorithm>

#include "MyEngine/Components/Effects/TrailComponent.h"
#include "MyEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Input/InputManager.h"

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
	InputManager::SetCursorVisible(false);        // ゲーム画面の上では、この照準がカーソルの代わり
	InputManager::SetCursorConfined(confineOnStart_);  // 閉じ込めると、照準はマウスの動いた量で動く（カーソルがゲーム画面の外へ出ない）
}

// ===== 更新 =====
void AimCursorSample::Update() {
	// --- C キー：OS のカーソルを出す・隠すを切り替える ---
	if (InputManager::IsKeyTriggered(DIK_C)) {
		InputManager::SetCursorVisible(!InputManager::IsCursorVisible());
	}
	// --- L キー：OS のカーソルを閉じ込める・放すを切り替える ---
	if (InputManager::IsKeyTriggered(DIK_L)) {
		InputManager::SetCursorConfined(!InputManager::IsCursorConfined());
	}

	const long deltaX = InputManager::GetMouseDeltaX();
	const long deltaY = InputManager::GetMouseDeltaY();
	if (InputManager::IsCursorConfineActive()) {
		// --- 閉じ込めている：マウスの動いた量（数えた数）→ ピクセルの量（感度を掛ける）→ 画面の位置の量 ---
		// マウスは下が＋、画面の位置は上が＋なので、Y を返す
		const Vector2 pixelMove = {static_cast<float>(deltaX) * pixelsPerCount_, -static_cast<float>(deltaY) * pixelsPerCount_};
		const Vector2 move = GameScreen::PixelToNdc(pixelMove);
		screenPosition_ = {screenPosition_.x + move.x, screenPosition_.y + move.y};
	} else if ((deltaX != 0 || deltaY != 0) && InputManager::IsMouseInGame()) {
		// --- 閉じ込めていない：ゲーム画面の中で動いたら、その位置（画面の位置。端が ±1）---
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
	ui.Field("Confine On Start###confineOnStart", confineOnStart_, Tip("Start で OS のカーソルをゲーム画面に閉じ込める（照準はマウスの動いた量で動く。L キーで切り替え・エディタは Shift+F1 で放す）"));
	ui.Field("Pixels Per Count###pixelsPerCount", pixelsPerCount_, AtLeast(0.0f).Tip("閉じ込めている間、マウスが1つ数えたら照準を何ピクセル動かすか（感度。1 で Windows の矢印とおよそ同じ）"));
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
	ui.Field("After Owner###afterOwner", value.afterOwner, Tip("弾の Trail の After Owner（弾がいなくなった後の尾。Discard：すぐ消える／Shrink：縮む／Fade：形はそのままで薄くなる）"));
	ui.Field("Fade Seconds###fadeSeconds", value.fadeSeconds, AtLeast(0.0f).Tip("弾の Trail の Fade Seconds（Fade のとき、薄くしきるまでの秒）"));
	ui.Field("Trail Life Time###trailLifeTime", value.trailLifeTime, AtLeast(0.0f).Tip("弾の Trail の Life Time（点が消えるまでの秒）"));
	ui.Field("Blend###blend", value.blend, Tip("弾の Trail の Blend（Add・Normal のどちらでも薄くなる）"));
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
	trail.afterOwner = shooter.afterOwner;
	trail.fadeSeconds = shooter.fadeSeconds;
	trail.minVertexDistance = kTrailVertexDistance;
	trail.width = kBulletScale * kTrailWidthRate;
	trail.color = shooter.trailColor;
	trail.intensity = kTrailIntensity;
	trail.blend = shooter.blend;
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

### D-3 `Sandbox/Source/Scripts/BurstEffectSamples.h`・`.cpp`（新しい）

```cpp
#pragma once
#include <cstdint>

#include "MyEngine/ECS/GameComponent.h"

//=============================================================================
// 出し終わったら消える、飛び散る物（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 決まった間隔で、飛び散る物を出す（Sandbox のテスト用。弾が当たった所に出すのと同じ形）
/// <para>Prefab を選んでいれば Instantiate、選んでいなければコードで Entity を作って Particle System を付ける</para>
/// <para>どちらも、作った同じフレームには Component がまだ付いていない（次のフレームの頭に付く）。付いたフレームに Play On Start で出て、出し終わったら Stop Action の Destroy で消える</para>
/// <para>Prefab を使うときは、一番上の Entity の Particle System を「Looping を外す・Play On Start・Stop Action を Destroy」にしておく</para>
/// </summary>
struct BurstSpawner {
	float interval = 0.5f;  // 出す間隔（秒）
	uint32_t prefab = 0;    // 飛び散る物の Prefab（0 ならコードで作る）
	float spread = 3.0f;    // 出す所を、この Entity の位置から左右（X）にずらす幅（m。この中でランダム）
	float timer = 0.0f;     // 次に出すまでの秒（実行中だけ）
};
```

```cpp
#include "Source/Scripts/BurstEffectSamples.h"

#include "MyEngine/Components/Effects/ParticleSystemComponent.h"
#include "MyEngine/ECS/GameObject.h"
#include "MyEngine/Math/RandomEngine.h"

namespace {
// ===== コードで作るときの、飛び散る物の見た目 =====
constexpr int kBurstCount = 24;                           // 1回に出す粒の数
constexpr float kBurstDuration = 0.1f;                    // 1回の長さ（秒。Burst だけなので短くてよい）
constexpr Vector2 kBurstSpeed = {3.0f, 6.0f};             // 飛び散る速さ（m/s）
constexpr Vector2 kBurstLifetime = {0.3f, 0.6f};          // 粒が消えるまでの秒
constexpr Vector2 kBurstSize = {0.15f, 0.3f};             // 粒の大きさ（m）
constexpr float kBurstDrag = 3.0f;                        // すぐ止まる火花にする
constexpr float kBurstIntensity = 3.0f;                   // ブルームで光る明るさ
constexpr Vector4 kBurstColor = {1.0f, 0.6f, 0.2f, 1.0f}; // 橙の火花
} // namespace


//=============================================================================
// BurstSpawner
//=============================================================================
// ===== 見せ方 =====
COMPONENT(BurstSpawner, "Test") {
	ui.Field("Interval###interval", value.interval, AtLeast(0.01f).Tip("出す間隔（秒）"));
	ui.AssetField("Prefab###prefab", value.prefab, AssetType::Prefab, Tip("飛び散る物の Prefab（一番上の Particle System を Looping なし・Stop Action を Destroy に）。無ければコードで作る"));
	ui.Field("Spread###spread", value.spread, AtLeast(0.0f).Tip("出す所を左右（X）にずらす幅（m）"));
}

// ===== 出す =====
void BurstSpawnerUpdate(Handle<Entity>, BurstSpawner& spawner, TransformComponent& transform, float deltaTime) {
	spawner.timer -= deltaTime;
	if (spawner.timer > 0.0f) {
		return;
	}
	spawner.timer += spawner.interval;

	// --- 出す所（この Entity の位置から、左右にずらす）---
	Vector3 position = GetWorldPosition(transform);
	position.x += RandomEngine::GetFloat(-spawner.spread, spawner.spread);

	// --- Prefab：Instantiate（Component は次のフレームの頭に付く。そのフレームに Play On Start で出る）---
	if (spawner.prefab != 0) {
		GameObject::Instantiate(spawner.prefab, position);
		return;
	}

	// --- コード：Entity を作って Particle System を付ける（これも付くのは次のフレームの頭）---
	const GameObject burst = GameObject::Create("Burst");
	burst.transform.translation = position;

	ParticleSystemComponent particle;
	particle.playOnStart = true;                          // 付いたフレームから出す
	particle.looping = false;                             // 1回だけ
	particle.duration = kBurstDuration;
	particle.stopAction = ParticleStopAction::Destroy;    // 出し終わったら（粒が全部消えたら）Entity ごと消える
	particle.rateOverTime = 0.0f;                         // Burst だけ
	particle.burstCount = kBurstCount;
	particle.shape = ParticleShape::Point;
	particle.lifetime = kBurstLifetime;
	particle.speed = kBurstSpeed;
	particle.size = kBurstSize;
	particle.drag = kBurstDrag;
	particle.endSizeRate = 0.0f;                          // 縮んで消える
	particle.colorA = kBurstColor;
	particle.colorB = kBurstColor;
	particle.intensity = kBurstIntensity;
	burst.AddComponent(particle);
}
SYSTEM(BurstSpawnerUpdate);
```

### D-4 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

`.vcxproj`：CursorTrailSamples の次に1行ずつ

```xml
    <ClCompile Include="Source\Scripts\CursorTrailSamples.cpp" />
    <ClCompile Include="Source\Scripts\BurstEffectSamples.cpp" />
```

```xml
    <ClInclude Include="Source\Scripts\CursorTrailSamples.h" />
    <ClInclude Include="Source\Scripts\BurstEffectSamples.h" />
```

`.filters`：CursorTrailSamples の次に3行ずつ

```xml
    <ClCompile Include="Source\Scripts\BurstEffectSamples.cpp">
      <Filter>Source\Scripts</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="Source\Scripts\BurstEffectSamples.h">
      <Filter>Source\Scripts</Filter>
    </ClInclude>
```

### D-5 `Sandbox/Resources/scenes/TrailParticleCursorTest.scene.json`・`Sandbox/Resources/prefabs/BurstEffect.prefab`（新しい）

テストシーン（SampleScene はそのまま）。3つの弾（左から Fade・Add の水色、Fade・Normal の赤、Shrink の緑）・火花を出す2つ（左がコードで作る橙、右が BurstEffect の Prefab を Instantiate する水色）・照準（Aim Cursor）。

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
			"name": "Shooter Fade Add",
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
					"afterOwner": "Fade",
					"fadeSeconds": 0.3,
					"trailLifeTime": 0.6,
					"blend": "Add",
					"trailColor": [0.4, 0.8, 1.0, 1.0]
				}
			}
		},
		{
			"id": 11,
			"name": "Shooter Fade Normal",
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
					"color": [1.0, 0.3, 0.3, 1.0]
				},
				"TrailFadeShooter": {
					"interval": 0.8,
					"speed": 12.0,
					"flightTime": 0.6,
					"end": "DestroyEntity",
					"afterOwner": "Fade",
					"fadeSeconds": 0.3,
					"trailLifeTime": 0.6,
					"blend": "Normal",
					"trailColor": [1.0, 0.3, 0.3, 1.0]
				}
			}
		},
		{
			"id": 12,
			"name": "Shooter Shrink",
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
					"end": "DestroyEntity",
					"afterOwner": "Shrink",
					"fadeSeconds": 0.3,
					"trailLifeTime": 0.6,
					"blend": "Add",
					"trailColor": [0.6, 1.0, 0.4, 1.0]
				}
			}
		},
		{
			"id": 13,
			"name": "Burst Spawner Code",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-3.0, 1.0, 5.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"BurstSpawner": {
					"interval": 0.5,
					"prefab": "",
					"spread": 2.0
				}
			}
		},
		{
			"id": 14,
			"name": "Burst Spawner Prefab",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [3.0, 1.0, 5.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"BurstSpawner": {
					"interval": 0.5,
					"prefab": "resources/prefabs/BurstEffect.prefab",
					"spread": 2.0
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
					"padSpeed": 1200.0,
					"confineOnStart": true,
					"pixelsPerCount": 1.0
				}
			}
		}
	]
}
```

飛び散る物の Prefab（一番上の Entity に Particle System：Looping なし・Play On Start・Stop Action を Destroy）

```json
{
	"version": 5,
	"entities": [
		{
			"id": 7310455182039612417,
			"name": "Burst Effect",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Particle System": {
					"enabled": true,
					"playOnStart": true,
					"looping": false,
					"duration": 0.1,
					"stopAction": "Destroy",
					"rateOverTime": 0.0,
					"burstCount": 24,
					"maxParticles": 256,
					"shape": "Point",
					"radius": 0.5,
					"coneAngle": 25.0,
					"boxSize": [1.0, 1.0, 1.0],
					"fromSurface": false,
					"lifetime": [0.3, 0.6],
					"speed": [3.0, 6.0],
					"size": [0.15, 0.3],
					"colorA": [0.5, 0.9, 1.0, 1.0],
					"colorB": [0.5, 0.9, 1.0, 1.0],
					"intensity": 3.0,
					"gravity": [0.0, 0.0, 0.0],
					"drag": 3.0,
					"endSizeRate": 0.0,
					"endColor": [1.0, 1.0, 1.0, 0.0],
					"ease": "Linear",
					"converge": false,
					"convergeEase": "InQuad",
					"startRotation": [0.0, 0.0],
					"angularVelocity": [0.0, 0.0],
					"randomSpinDirection": false,
					"alignToVelocity": false,
					"stretch": 1.0,
					"texture": "",
					"blend": "Add",
					"space": "World",
					"useFog": true,
					"useDepthOfField": false
				}
			}
		}
	]
}
```

### D-6 確かめること

Project ウィンドウで `scenes/TrailParticleCursorTest` を開いて Play。

| # | すること | こうなれば合格 |
|---|---|---|
| 1 | Play | Game ビューの左上に「Shift+F1 : カーソルを画面の外へ出す」が3秒出る。マウスを大きく動かしても、矢印は Game ビューの外へ出ない（隠れているので、黄色い輪の照準で見る） |
| 2 | マウスを右へ動かし続ける | 照準が右の端（その先）まで動く。Hierarchy などのパネルはマウスで触れない |
| 3 | Shift+F1 | 「クリックでカーソルを画面に閉じ込める」が出て、マウスがパネルへ出られる。照準はマウスの位置に付いて来る |
| 4 | Game ビューを左クリック | また閉じ込められる（案内が3秒出る） |
| 5 | L キー | 閉じ込めるのをやめる（矢印が外へ出られる）。もう一度 L で閉じ込める |
| 6 | 一時停止（Ⅱ）・再開 | 一時停止の間は外へ出られる。再開するとまた閉じ込められる |
| 7 | Alt+Tab でほかのアプリへ・戻る | ほかのアプリでは自由に動く。戻っても放したまま（Game ビューのクリックで閉じ込められる） |
| 8 | 左の水色（Fade・Add）・真ん中の赤（Fade・Normal）の弾を見る | 弾が消えると、尾は形はそのままで薄くなって消える（短い棒が縮むようには見えない） |
| 9 | 右の緑（Shrink）の弾を見る | 前と同じく、尾が縮んで消える。Inspector で After Owner を Discard にすると、弾と一緒に消える |
| 10 | 奥の火花を見る | 左（コード）は橙、右（Prefab）は水色の火花が 0.5 秒ごとに飛び散る。Hierarchy の「Burst」「Burst Effect」が出てはすぐ消え、たまらない |
| 11 | Stop | 尾・火花・Burst が全部消える。マウスが外へ出られる |
| 12 | Release（製品版）でこのシーンを最初のシーンにして 1・2・5・7（任意） | 黒帯の内側に閉じ込められる。Alt+Tab で戻ると、すぐまた閉じ込められる |

## パートE `Docs/Reference.md`

「1フレームの順番」の表の 10.5 の行

```
| 10.5 | `TrailSystem::Update`（持ち主の Entity・Trail が消えた尾を持ち主から離し〈After Owner で消す：Shrink は点が寿命で消えきるまで・Fade は形はそのままで Fade Seconds かけて薄く・Discard はすぐ捨てる。新しい点は足さない〉、点を古くして寿命の物を消し、Emitting の Trail に今の位置を足す。停止中は点を持たない。シーンを変えたら `TrailSystem::ClearAll` で全部捨てる） | エンジン |
```

「11. 入力」の `const long dx = InputManager::GetMouseDeltaX();` の行を、この2行に

```cpp
const long dx = InputManager::GetMouseDeltaX(); // マウスが数えた数（ピクセルではない。感度を掛けてピクセルの量にする）

InputManager::SetCursorVisible(false); // ゲーム画面の上で OS のカーソルを隠す
InputManager::SetCursorConfined(true);   // OS のカーソルをゲーム画面の外へ出さない（端で止まる。動いた量は返す）。エディタは Shift+F1 で放す
```

同じ所の箇条書きの最後に足す

```
- 隠す・閉じ込めるのは Play 中だけ（一時停止中は出す・放す）。**シーンを変えると（Stop も）頼みが戻る**ので、したいシーンが自分で頼む。作り方は `Tasks/ScreenTrailCursor.md`・`Tasks/TrailFadeStopActionCursorConfine.md`。
```

「パーティクル（Particle System）」の表の Playback の行

```
| Playback | Play On Start・Looping・Duration・Stop Action | Play で流し始める（外すと `ParticleSystem::Play` まで出さない）／Duration 秒ごとにくり返す（Burst もまた）／1回の長さ／出し終わったら（出すのをやめて粒が全部消えたら）Destroy で Entity を消す |
```

同じ所の箇条書きの1つ目の次に足す

```
- 飛び散る物の Prefab は、一番上の Particle System を Looping なし・Play On Start・**Stop Action を Destroy** にしておくと、Instantiate するだけで出て、出し終わったら消える（Looping の物も `ParticleSystem::Stop` すれば消える）。
```

「軌跡（Trail）」の項目の表の Life Time の次に足す

```
| After Owner / Fade Seconds | 持ち主（Entity かこの Component）が消えた後の尾。Discard：すぐ捨てる／Shrink：点が寿命で消えて縮む（既定）／Fade：形はそのままで Fade Seconds かけて薄くする |
```

## 2章 AL4_01 で差し替える所（公開した物の一覧）

### 2.1 光の尾（`#include <MyEngine/Components/Effects/TrailComponent.h>`）

```cpp
enum class TrailAfterOwner { Discard, Shrink, Fade };

struct TrailComponent {
	// …（前からの項目。fadeOutAfterOwner は無くなった）
	TrailAfterOwner afterOwner = TrailAfterOwner::Shrink; // 持ち主が消えた後の消え方
	float fadeSeconds = 0.3f;                             // Fade のとき、薄くしきるまでの秒
};
```

- 弾の Prefab（`PlayerBullet.prefab`）の Trail で **After Owner を Fade** にして、Fade Seconds を決めて保存する。JSON のキーは `afterOwner`（`"Fade"`）・`fadeSeconds`。前の `"fadeOutAfterOwner": true` は読み飛ばされるので、消してよい（保存し直すと消える）
- 弾のコードは何も変えない（当たって消える・着いて消えるのは、今までどおり Destroy）

### 2.2 飛び散る物（`#include <MyEngine/Components/Effects/ParticleSystemComponent.h>`）

```cpp
enum class ParticleStopAction { None, Destroy };

struct ParticleSystemComponent {
	// …
	ParticleStopAction stopAction = ParticleStopAction::None; // 出し終わったら（出すのをやめて、粒が全部消えたら）すること
};
```

- 飛び散る物の Prefab を作る：一番上の Entity に Particle System（Looping なし・Play On Start・Stop Action を Destroy）
- 弾の当たった所で `GameObject::Instantiate(hitEffectPrefab_, position)`。後は何も書かない

### 2.3 OS のカーソル（`#include <MyEngine/Input/InputManager.h>`）

```cpp
class InputManager {
public:
	static void SetCursorConfined(bool confined); // OS のカーソルをゲーム画面に閉じ込めるか（シーンを変えると false に戻る）
	static bool IsCursorConfined();             // 頼んだ値
	static bool IsCursorConfineActive();         // 今本当に閉じ込めているか（一時停止中・手前でない・エディタで放した間は false）
	static long GetMouseDeltaX();             // 単位はマウスが数えた数（mickey）。閉じ込めている間は端でも動いた分を返す
	static long GetMouseDeltaY();             // 下が＋
};
```

AimReticle（`OnEnable`・`OnDisable` で、隠すのと一緒に）

```cpp
void AimReticle::OnEnable() {
	InputManager::SetCursorVisible(false);
	InputManager::SetCursorConfined(true);
}
void AimReticle::OnDisable() {
	InputManager::SetCursorVisible(true);
	InputManager::SetCursorConfined(false);
}
```

PlayerAim（マウスの所）：閉じ込めている間は動いた量で動かす。D-2 の `AimCursorSample::Update` と同じ形（感度は「1 カウントで何ピクセル」）。

## 3章 次の候補

- Trail の Fade の変わり方を Curve・Ease で選ぶ（今は直線）
- Particle System の Stop Action に Disable（使い回す物）・Callback（Script に知らせる）
- カーソルを真ん中に戻し続ける Locked（FPS のカメラ。位置が要らないゲーム用）。そのときは `SetCursorLockMode(CursorLockMode)` のような enum の窓口にする
- 閉じ込め直したクリックの代わりに、キー（Unreal の「Game ビューのクリックで閉じ込める」も設定で選べる）を選べるようにする
