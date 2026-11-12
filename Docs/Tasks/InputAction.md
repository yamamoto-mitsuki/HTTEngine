参考：
- Unity Input System の Action（名前の付いた操作に、キー・ボタンを割り当てる。この md の InputActionMap と同じ考え）：https://docs.unity3d.com/Packages/com.unity.inputsystem@1.11/manual/Actions.html
- Unity Input System の割り当て（Binding・Composite・押して割り当てを拾う Interactive Rebinding・上書きの保存）：https://docs.unity3d.com/Packages/com.unity.inputsystem@1.11/manual/ActionBindings.html
- Unity の Shortcuts Manager（エディタのショートカットを人ごとに変える。ぶつかりの表示・Global とウィンドウごとの区別）：https://docs.unity3d.com/Manual/ShortcutsManager.html
- Blender の Keymap（プリセット・修飾キーとマウスのボタンの組で視点を動かす）：https://docs.blender.org/manual/en/latest/editors/preferences/keymap.html
- DirectInput のキーの番号（DIK_〜。キーの場所の番号）：https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418641(v=vs.85)
- この作業のきっかけ：Scene ビューのカメラが「ホイールボタンを押しながらドラッグで回転・右ドラッグで平行移動」に決め打ちで、メンバーごとに慣れたツール（Unity・Blender）の操作にできない

# Input Action とエディタの Keymap（段階1・段階2）

- **段階1 土台（Input Action。Release にも入る）**
  - **パートA InputManager に「読む決まり」（InputGate）を足す**（`Input/InputManager.h/.cpp`）
  - **パートB InputBinding（新しい）**（`Input/Action/InputBinding.h/.cpp`）：入力1つ（キー・マウス・パッドのボタンと軸）と、修飾キー・向きの組。保存の文字（`"Ctrl+Key.Z"`）との行き来と名前の表
  - **パートC InputActionMap（新しい）**（`Input/Action/InputActionMap.h/.cpp`）：名前の付いた操作（Action）と割り当ての表。毎フレーム読み、既定と人ごとの上書きを分けて保存し、ぶつかりを調べる
  - **パートD InputRebinder（新しい）**（`Input/Action/InputRebinder.h/.cpp`）：「次に押したキー・ボタン」を拾う係
  - **パートE vcxproj（段階1）**
- **段階2 エディタの Keymap**
  - **パートF Settings ウィンドウに、GlobalVariables 以外のページを足せるようにする**（`Editor/Windows/SettingsWindow.h/.cpp`）
  - **パートG InputActionTable（新しい）**（`Editor/Widgets/InputActionTable.h/.cpp`）：割り当てを直す ImGui の表（段階3の Input ページでも使う）
  - **パートH EditorKeymap（新しい）**（`Editor/Keymap/EditorKeymap.h/.cpp`・vcxproj）：エディタの操作の一覧・プリセット・Settings の Keymap ページ
  - **パートI Scene ビューのカメラ**（`Camera/DebugCamera.h/.cpp`・`Editor/Viewport/EditorViewport.h/.cpp`・`Window/WindowManager.cpp`・`Editor/ImGuiManager.cpp`）
  - **パートJ ショートカットを Keymap で聞く**（SceneGizmo・ControlWindow・SceneDialogs・Hierarchy・Project・Timeline・Curve・点の編集・Code ウィンドウ）
  - **パートK Reference**
  - **パートL 確かめ方**

**A → B → C → D → E の順**に写すと、段階1だけでビルドが通る（まだ誰も使わない）。段階2は **F → G → H → I → J** の順（G は F の `SettingsPage`、H は F・G、I・J は H の `EditorKeymap` を使う）。

段階3（ゲームから Input Action を使う・プレイヤーのキーコンフィグ）は別のチャットでやる。そのチャットへの申し送りは 0.12。

ブランチは `feature/InputAction`（`develop` aa6717a から。この md も未コミットで置いてある）。

**2026-11-12 本体へ直接入れた**（ブランチ `feature/InputAction`。未コミット）。写しの `aa6717a..answer-input` の差分（パートA〜J）を当て、パートK の Reference も直した。`InputManager.h` の `InputGate` の enum は、写してあった物をそのまま残した。確かめのコード（L-3）は写しだけで、本体には入れていない。

**2026-11-12 直した**（Sandbox で見てもらって）：①拾い始めた後に右クリックしても拾わなかった → 「始めたときに押していた物を全部離すまで待つ」を「1つずつ、離して押し直すまで拾わない」に直した（0.8・パートD）。②Keymap の表の × と ＋ を小さくし、割り当てのボタンと × の間をあけた（パートG）。③①を直しても、右クリックも Esc も拾わなかった。本当の原因は、拾う係を ImGui を描く所で動かしていたこと（その間 InputManager は何も読まない）。拾う係をフレームの頭（`EditorKeymap::Update`）で動かすように直した（0.8・パートG・H）

## 0章 全体の形

### 0.1 今の作りと困ること

| 場所 | 今の書き方 |
|---|---|
| Scene ビューのカメラ（`DebugCamera.cpp`） | `InputManager::IsMouseButtonPressed(2)` で回転、`(1)` で平行移動、ホイールで前後。カメラが自分で入力を読んでいる |
| ギズモの W・E・R・F（`SceneGizmo.cpp`） | `ImGui::IsKeyPressed(ImGuiKey_W)` |
| Undo・Redo・保存・Hierarchy のコピーなど | `ImGui::IsKeyChordPressed(ImGuiMod_Ctrl \| ImGuiKey_Z)` が、ウィンドウごとにばらばら |
| メニュー・吹き出しのキーの文字 | `"Ctrl+Z"`・`"移動（W）"` を文字で書いている |

キーが 10 か所以上に散らばって決め打ちなので、人ごとに変えられない。ゲームの側も `InputManager::IsKeyTriggered(DIK_SPACE)` と直接書いているので、プレイヤーのキーコンフィグを作れない（Reference の 19 章「まだ無い物」にある「キーの割り当て（Input Action）」）。

### 0.2 層の分け方

```
 ┌ ゲーム（段階3・別のチャット）───┐   ┌ エディタ（段階2）─────────────────────┐
 │ Script：IsTriggered("Jump")      │   │ EditorKeymap：IsTriggered(EditorAction::Undo)│
 │ キーコンフィグの画面（ゲームの UI）│   │ Settings → Editor → Keymap（ImGui の表）     │
 └────────────┬─────────────┘   └──────────────┬──────────────┘
                 │       同じ部品を使う                      │
 ┌ Input/Action/（段階1。Release にも入る）──────────────────────────────┐
 │ InputBinding（入力1つ＋修飾キー＋向き・文字との行き来）                         │
 │ InputActionMap（Action の表・毎フレーム読む・既定と上書き・保存・ぶつかり）    │
 │ InputRebinder（次に押した物を拾う）                                             │
 └───────────────────────────┬────────────────────────────┘
 ┌ Input/InputManager（今まで通り。読む決まり InputGate を足す）──────────────────┐
 │ DirectInput のキーボード・マウス、XInput のパッド                                │
 └────────────────────────────────────────────────────────┘
```

- **表（InputActionMap）・拾う係（InputRebinder）は Input/ に置く**。ゲーム（Release）でも使うので、Editor/ や ImGui に頼らない
- **ImGui の表（InputActionTable）は Editor/Widgets/**。Keymap のページと、段階3の Project の Input ページ（ゲームの既定の割り当てを直す）で使い回す。プレイヤーが遊びながら割り当てを変える画面は、ゲームの UI で作る（ImGui は Release に無い）
- **Settings のページは、GlobalVariables のグループではない物も足せるようにする**（パートF）。Keymap は「割り当ての一覧を押して拾う」形で、GlobalVariables の項目（数・色など）では書けない。段階3の Input ページも同じ形で足す

### 0.3 エディタも DirectInput で読む（InputGate）

エディタのショートカットは今 ImGui のキー（`ImGuiKey_W`）で読んでいる。表は1つの読み方にそろえたいので、**エディタもゲームと同じ InputManager（DirectInput）から読む**。

ただし、今の `InputManager::IsKeyPressed` は「ImGui がキーボードを使っている間は false」。ゲームにはそれが正しい（Hierarchy で名前を打っている間にキャラが歩かない）が、エディタのショートカット（Hierarchy を触っているときの Delete）はちょうどその間に効いてほしい。なので **読む決まり `InputGate` を足し、表ごとに選ぶ**。

| | Game（今まで通り・既定） | Editor（足す） |
|---|---|---|
| ウィンドウが手前に無い | 押していない | 押していない |
| ImGui がキーボードを使っている（パネルを触っている） | 押していない | **読む** |
| ImGui の入力欄に文字を打っている | 押していない | 押していない |
| ImGui がマウスを使っている | 押していない | **読む**（どのパネルの上で効かせるかは、読む側が決める。Scene ビューの上か など） |

- 引数の既定を `InputGate::Game` にしたので、今あるゲームのコードは何も変えなくてよい
- DirectInput で読むので、**日本語入力が ON のままでもショートカットが効く**（ImGui のキーは IME を通るので、ON だと W が届かないことがある）
- DirectInput のキーの番号はキーの「場所」なので、名前は英語の配列で付ける（日本語の配列の「@」は英語の配列の「[」の場所なので `Key.LeftBracket`）。画面にもその名前が出る。日本語の配列の印に合わせて出すのはやらない（0.11）

### 0.4 Action・割り当て・種類

**Action** は名前の付いた操作（`"SceneView.Rotate"`・段階3なら `"Jump"`）。**割り当て（InputBinding）** は「入力1つ＋一緒に押す修飾キー＋足す向き」で、1つの Action にいくつでも付けられる（Redo ＝ Ctrl+Y と Ctrl+Shift+Z）。

| 種類 | 使い道 | 値の決め方 |
|---|---|---|
| Button | ジャンプ・Undo・ドラッグ中か | どれかの割り当てを押していれば押している。軸（ホイール・スティック）は、割り当ての向きに 0.5 以上で押した |
| Value | ホイールで前後・トリガーの量 | 割り当ての値を足す（向きが - なら引く） |
| Vector2 | 移動・視点（段階3） | 割り当ての値を ±X・±Y の側に足す（W は +Y、A は -X、左スティックの X は +X）。`clampLength` で長さを 1 までにできる（斜めで速くならない） |

入力の種類（`InputDevice`）は Key・MouseButton・MouseMove・MouseWheel・PadButton・PadAxis。**マウスの動きとホイールを別の種類にした**のは、「ホイールだけ受け付ける操作」（エディタの前後（ホイール））を作るため。同じ種類にすると、割り当てを拾っている間にマウスを動かしただけで動きを拾ってしまう。

### 0.5 修飾キーの合わせ方（表ごとに決める）

| 合わせ方 | 意味 | 使う所 |
|---|---|---|
| Exact | ちょうど同じときだけ。`Ctrl+Z` は Ctrl+Shift+Z では効かない。`Mouse.Right` は Shift を押していると効かない | エディタ（今の `IsKeyChordPressed` と同じ。Blender 風の「ホイールボタンで回転・Shift+ホイールボタンで平行移動」を分けられる） |
| AtLeast | 書いた修飾キーを押していればよい。`Space` は Shift を押しながらでも効く | ゲーム（ダッシュに Shift を押しながらジャンプ） |

割り当ての入力そのものが修飾キー（`Key.LeftShift` でダッシュ）のときは、それを押していることは数えない（数えると、Exact で Shift を押した瞬間に「修飾キーが違う」になる）。

### 0.6 ぶつかり（同時に効いてしまう割り当て）

同じ入力で、修飾キーが合い（Exact は同じ・AtLeast は片方がもう片方に入っている）、**効く場所（context）が重なる**ときにぶつかる。

- context は Action ごとの文字。**空は「どこでも効く」**で、全部の Action とぶつかる。同じ文字どうしもぶつかる。違う文字どうしはぶつからない
- エディタでは、Scene ビューの操作（回転・ギズモの W など）を `"SceneView"`、Undo・保存などを空にした
- Delete・F2・F は Hierarchy・Project・Timeline でそれぞれ使っているが、**1つの Action（消す・名前を変える・画面に入れる）を全部のウィンドウで共有する**（Unity の Shortcuts Manager の Delete と同じ）。何に効くかは触っているウィンドウが決めるので、ぶつかりではない。ウィンドウごとに別の Action にすると、Delete を変えたいとき5か所変えることになる
- ホイールの上と下を、別々のボタンの Action に割り当てるのはぶつからない（ホイールで「次へ」「前へ」）
- ぶつかっていても割り当ては入る（拾った後に赤くして知らせる）。両方効いてよい場合もあるので、入れるのを断らない

### 0.7 変えられない Action（isFixed）

Scene ビューの左クリックは「選ぶ」だけでなく、ImGuizmo のドラッグ・UI のドラッグ・点の編集でも使っていて、それらは ImGui・ImGuizmo の中で左ボタンに決まっている。「選ぶ」だけを変えられるようにすると、ほかと食い違う。

なので **「選ぶ（クリック）」は表に出すが変えられない**（`isFixed`）。出す理由は、ほかの操作（回転など）を左クリックにしたときに「選ぶとぶつかる」と知らせるため。読む・保存するときも変えない。

### 0.8 割り当てを変えた瞬間・拾っている間

- **割り当てを変えた Action は、今押している物を一度離すまで効かない**（`waitRelease`）。Redo を Ctrl+Z に変えた瞬間、まだ Ctrl+Z を押していると、次のフレームに Redo が「押した瞬間」になってしまうため
- **拾っている間は、表の全部の Action を止める**（`SetEnabled(false)`）。Ctrl+Z を拾うときに Undo が動かないように。戻したときに押していた物も、一度離すまで効かない
- 拾い始めたときに押していた物（拾い始めるボタンを押したクリック・Enter）は、離して押し直すまで拾わない（最初のフレームに「前のフレームに押していた」として覚える）。「全部を離すまで待つ」にはしない。日本語のキーボードの半角/全角・カタカナひらがな・英数は、DirectInput に離した知らせが来ず押しっぱなしに見えることがあると言われていて、1つでもあると全部を待つ形は永久に拾えなくなる（確かめてはいない）
- **Esc でやめる**（なので Esc は割り当てられない）
- 修飾キーを押しながらほかを押すと `Ctrl+Z` のように拾う。修飾キーだけを押して離すと、そのキー1つ（`Key.LeftShift`）を拾う
- 拾っている間は ImGui のモーダルを出す（ほかのボタンを触れないので、拾うクリックが別の所に効かない）
- **拾う係（`InputRebinder::Update`）は、フレームの頭（`EditorKeymap::Update`。表の `Update` の前）で動かす**。`WindowManager::UpdateAll` の最後で `InputManager::SetActiveWindow(nullptr)`（入力のリセット）をするので、その後に描く ImGui の中では、InputManager は何を読んでも「押していない」になる。最初は表を描く所で動かしていて、何も拾えなかった（2026-11-12 に Sandbox で起きた）。表を描く所では、案内のモーダルを出すだけ
- 拾えたフレームも表を止めておく（`EditorKeymap::Update` は、拾う係を動かす前に「拾っていたか」を覚え、それで `SetEnabled` を決める）。拾った Ctrl+Z が、同じフレームに Undo にならないように

### 0.9 保存：既定と人ごとの上書き

- 表は Action ごとに「既定の割り当て」（コードか、段階3ならゲームのファイル）と「今の割り当て」を持つ
- `SaveOverrides` は **既定から変えた Action だけ**を書く。エンジンの既定を後で変えても、変えていない人にはそのまま届く
- エディタは `Parameters/Editor/Keymap.json`。`Parameters/Editor/` はもう .gitignore に入っているので、**人ごとの設定になり、git でぶつからない**
- 読むときは、全部を既定に戻してから上書きを当てる。知らない Action（名前を変えた・消した）と、読めない割り当ての文字は飛ばしてログに出す（落とさない）

```json
{
    "bindings": {
        "SceneView.DollyDrag": ["Alt+Mouse.Right"],
        "SceneView.Pan": ["Mouse.Middle"],
        "SceneView.Rotate": ["Mouse.Right"]
    },
    "version": 1
}
```

割り当ての文字は `[修飾キー+]入力の名前[:向き]`。修飾キーは `Ctrl+`・`Shift+`・`Alt+` の順。向きは `+X`（書かない）・`-X`・`+Y`・`-Y`。

### 0.10 Scene ビューのカメラ

- `DebugCamera` は**入力を読まない**。`ApplyInput(DebugCamera::Input)` で「回す量・平行に動く量・前後の量」を受け取って動くだけにする。どのボタンで動かすかは EditorViewport が EditorKeymap で決める（カメラが EditorKeymap を知ると、Camera/ が Editor/ に頼ることになる）
- 操作は4つ：回転（ドラッグ）・平行移動（ドラッグ）・**前後に動く（ドラッグ）**・前後に動く（ホイール）。ドラッグの前後は今は無いが、Unity（Alt+右ドラッグ）・Blender（Ctrl+ホイールボタン）のプリセットに要るので足した（既定は割り当てなし）
- 回転は今まで通り**その場で向きを変える**（Unity の右ドラッグと同じ。Blender のホイールボタンは注視点のまわりを回るので、Blender 風のプリセットでも回り方は違う）
- ホイールの量は「目盛り」（1 目盛りで 1）にした。今までの `zoomSpeed`（120 で 1 目盛り）は `dollyWheelSpeed = 1.2`（1 目盛りで 1.2）に直したので、動く量は今と同じ
- 速さの名前を直した：`orbitSpeed` → `rotateSpeed`（回り方はその場で回るので）、`zoomSpeed` → `dollyWheelSpeed`。`distanceMin`（使っていなかった）は消した

**プリセット**（Keymap ページの上のボタン）は、カメラの3つ（回転・平行移動・前後（ドラッグ））だけを入れ替える。

| プリセット | 回転 | 平行移動 | 前後（ドラッグ） |
|---|---|---|---|
| HTTEngine（既定） | ホイールボタン | 右 | なし |
| Unity 風 | 右 | ホイールボタン | Alt+右 |
| Blender 風 | ホイールボタン | Shift+ホイールボタン | Ctrl+ホイールボタン |

### 0.11 やらないこと・知っておくこと

| やらない物 | 理由・今どうするか |
|---|---|
| Esc でダイアログを閉じる・点の選択をやめる | 決め打ちのまま。Esc は拾うのをやめるキーでもあるので、割り当てに使わない |
| Shift+F1（閉じ込めたカーソルを放す） | 決め打ちのまま。InputManager（Input/）の中で読んでいて、Editor/ の Keymap を知らない層。ゲームがマウスを持っている間に効く必要もある |
| Timeline・Curve のホイールボタンでの移動・ホイールの拡大 | ImGui の「その部品を押しているか」で読んでいて、Scene ビューのカメラとは別の仕組み。変えたくなったら同じ形で Action を足す |
| ImGuizmo・UI のドラッグ（左ボタン） | ImGui・ImGuizmo の中で決まっている（0.7） |
| Undo（Keymap の割り当ての変更を Ctrl+Z で戻す） | Revert（保存した割り当てに戻す）と「既定」で足りる |
| 画面に出すキーの名前を、日本語の配列の印に合わせる | 英語の配列の名前のまま（0.3）。困ったら `GetKeyNameText`（Windows の今の配列の名前）で出すのを足す |
| エディタのウィンドウが2つ以上 | ImGui のウィンドウは1つだけ（`ImGuiManager` が1つ）なので、`EditorKeymap::Update` は ImGui のウィンドウの番に1回 |

### 0.12 段階3（ゲーム）への申し送り

段階3は別のチャットでやる。段階1の土台は、ゲームから使う前提で作ってある。

- ゲームの表は `InputActionMap(InputGate::Game, InputModifierMatch::AtLeast)`（パッドも読む）。2人で遊ぶなら表を2つ作り、`SetGamepadIndex` で番号を変える
- Action の名前で聞ける（`IsTriggered("Jump")`・`GetVector2("Move")`）。毎フレームたくさん聞くなら `Find` で番号にしておく
- 既定の割り当ては、ゲームのファイル（`Parameters/Input/〜.json` など、git に入る）から作る。その形・Project の Input ページ（`SettingsWindow::AddPage` と `InputActionTable` で作れる）・Script からの窓口（どこが表を持つか・`Update` をフレームのどこで呼ぶか）を決める
- **プレイヤーの上書きの保存先がエンジンに無い**。Release で exe の隣に書くと、Program Files に入れたときに書けない。`%LOCALAPPDATA%\<ゲームの名前>\` などの「ユーザーのデータの場所」をエンジンに足す
- プレイヤーのキーコンフィグの画面は、ゲームの UI で `InputRebinder`（`Start`・毎フレーム `Update`・`GetCaptured`）と `FindConflicts` を呼んで作る。**`InputRebinder::Update` と表の `Update` は、入力を読める所（Script の Update など、`WindowManager::UpdateAll` の中）で呼ぶ**（描く所では何も読めない。0.8）。Vector2 の割り当てを足すときの向き（上・下・左・右）を選ぶ所は、`InputActionTable` にまだ無い（今は元の割り当ての向きを引き継ぐだけ）
- 今の `InputManager::GetMoveAxis` は残す（使っているゲームがある）

# 段階1 土台

## パートA InputManager に「読む決まり」（InputGate）を足す

### A-1 `Input/InputManager.h`

**1. `class InputManager` の前に enum を足す**（include の後）：

```cpp
#include "HTTEngine/Input/XInputGamepad.h"


/// <summary>
/// キーボード・マウスを、どの決まりで読むか（ふだんは Game。Input Action の表が、表ごとに決めて渡す）
/// <para>どちらも、ウィンドウが手前に無い間は「押していない」。パッドは、どちらでも Game と同じ（ImGui に文字を打っている間も「押していない」）</para>
/// </summary>
enum class InputGate : uint8_t {
	Game,   // ゲーム：ImGui がキーボード・マウスを使っている間も「押していない」（エディタのパネルを触っている間に、ゲームが動かないように）
	Editor, // エディタの操作：ImGui の入力欄に文字を打っている間だけ「押していない」（パネルを触っている間も読める。マウスはどこにあっても読める）
};


/// <summary>
/// すべての入力処理を管理するクラス
/// </summary>
```

**2. キーボード・マウスのボタンの6つに `gate` を足す**（既定が Game なので、今のゲームのコードは変えなくてよい）：

```cpp
	//============================================================
	// キーボード（gate は読む決まり。ゲームは書かない＝Game）
	//============================================================

	/// <summary>指定したキーが押されているか。</summary>
	static bool IsKeyPressed(uint8_t key, InputGate gate = InputGate::Game);

	/// <summary>指定したキーを押した瞬間か。</summary>
	static bool IsKeyTriggered(uint8_t key, InputGate gate = InputGate::Game);

	/// <summary>指定したキーを離した瞬間か。</summary>
	static bool IsKeyReleased(uint8_t key, InputGate gate = InputGate::Game);

	//============================================================
	// マウス ボタン番号: 0=左, 1=右, 2=中, 3・4=横（戻る・進む）
	//============================================================

	/// <summary>指定ボタンが押されているか。</summary>
	static bool IsMouseButtonPressed(int button, InputGate gate = InputGate::Game);

	/// <summary>指定ボタンを押した瞬間か。</summary>
	static bool IsMouseButtonTriggered(int button, InputGate gate = InputGate::Game);

	/// <summary>指定ボタンを離した瞬間か。</summary>
	static bool IsMouseButtonReleased(int button, InputGate gate = InputGate::Game);
```

**3. マウスの動きの3つにも `gate` を足す**（`GetMouseDeltaX` の長いコメントはそのまま）：

```cpp
	static long GetMouseDeltaX(InputGate gate = InputGate::Game);

	/// <summary>前フレームからのY移動量 (下が正)。単位は GetMouseDeltaX と同じ（マウスが数えた数）</summary>
	static long GetMouseDeltaY(InputGate gate = InputGate::Game);

	/// <summary>ホイール移動量 (上スクロールが正)。1 目盛りで WHEEL_DELTA（120）</summary>
	static long GetMouseDeltaWheel(InputGate gate = InputGate::Game);
```

**4. private の `IsTypingInImGui` の次に2行**：

```cpp
	static bool IsTypingInImGui(); // ImGui の入力欄に文字を打っている最中か
	static bool IsKeyboardBlocked(InputGate gate); // キーボードを「何も押していない」扱いにするか（gate の決まりで）
	static bool IsMouseBlocked(InputGate gate);    // マウス（ボタン・動き）を「何も押していない・動いていない」扱いにするか（gate の決まりで。また閉じ込めたクリックは別）
```

### A-2 `Input/InputManager.cpp`

**1. キーボード・マウスのボタンの6つを差し替える**（「止めるか」を `IsKeyboardBlocked`・`IsMouseBlocked` にまとめる）：

```cpp
// ---- キーボード ----
bool InputManager::IsKeyPressed(uint8_t key, InputGate gate) {
	if (IsKeyboardBlocked(gate)) return false;
	return GetInstance()->keyboard_.IsPressed(key);
}
bool InputManager::IsKeyTriggered(uint8_t key, InputGate gate) {
	if (IsKeyboardBlocked(gate)) return false;
	return GetInstance()->keyboard_.IsTriggered(key);
}
bool InputManager::IsKeyReleased(uint8_t key, InputGate gate) {
	if (IsKeyboardBlocked(gate)) return false;
	return GetInstance()->keyboard_.IsReleased(key);
}

// ---- マウス ----
bool InputManager::IsMouseButtonPressed(int b, InputGate gate) {
	if (IsMouseBlocked(gate)) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonPressed(b);
}
bool InputManager::IsMouseButtonTriggered(int b, InputGate gate) {
	if (IsMouseBlocked(gate)) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonTriggered(b);
}
bool InputManager::IsMouseButtonReleased(int b, InputGate gate) {
	if (IsMouseBlocked(gate)) return false;
	if (GetInstance()->isMouseButtonSwallowed_) return false; // また閉じ込めたクリック（ボタンを全部離すまで）
	return GetInstance()->mouse_.IsButtonReleased(b);
}
```

**2. マウスの動きの3つを差し替える**：

```cpp
long InputManager::GetMouseDeltaX(InputGate gate) {
	if (IsMouseBlocked(gate)) return 0;
	return GetInstance()->mouse_.GetDeltaX();
}
long InputManager::GetMouseDeltaY(InputGate gate) {
	if (IsMouseBlocked(gate)) return 0;
	return GetInstance()->mouse_.GetDeltaY();
}
long InputManager::GetMouseDeltaWheel(InputGate gate) {
	if (IsMouseBlocked(gate)) return 0;
	return GetInstance()->mouse_.GetDeltaWheel();
}
```

**3. `IsTypingInImGui` の後に2つ足す**：

```cpp
// ===== gate の決まりで「押していない」扱いにするか =====
// Game：ImGui がキーボード・マウスを使っている間も止める。Editor：文字を打っている間だけ止める（マウスは止めない）
bool InputManager::IsKeyboardBlocked(InputGate gate) {
	if (!IsActive()) {
		return true;
	}
	if (gate == InputGate::Editor) {
		return IsTypingInImGui();
	}
	return IsKeyboardCapturedByImGui();
}
bool InputManager::IsMouseBlocked(InputGate gate) {
	if (!IsActive()) {
		return true;
	}
	if (gate == InputGate::Editor) {
		return false; // どのパネルの上で効かせるかは、読む側（Scene ビューの上か など）が決める
	}
	return IsMouseCapturedByImGui();
}
```

- Game の決まりは今と同じ（`!IsActive()` → `IsKeyboardCapturedByImGui()` / `IsMouseCapturedByImGui()`）。値が2つの enum なので if で分けた（rules の「1つの値だけ聞くなら if」）
- Release では `IsTypingInImGui`・`IsKeyboardCapturedByImGui` が false なので、Game と Editor は同じになる

## パートB InputBinding（新しい）

### B-1 `Input/Action/InputBinding.h`（新しい）

入力1つ（`InputControl`）と、割り当て1つ（`InputBinding`）。`InputBinding::Key(DIK_Z, InputModifiers::Ctrl)` のように、コードで既定の割り当てを書ける。

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>


//=============================================================================
// 入力の割り当て1つ（Input Action の部品）
//=============================================================================
// 「どのキー・ボタン・軸か（InputControl）」と「一緒に押す修飾キー」と「2軸のどちらの向きに足すか」の組。
// 保存は文字で書く（"Ctrl+Key.Z"・"Mouse.Right"・"Key.A:-X"）。名前の一覧は InputBinding.cpp の kControlNames


/// <summary>
/// 入力の出どころ（キーボード・マウス・パッドの、ボタンか軸か）
/// </summary>
enum class InputDevice : uint8_t {
	None,        // 割り当てなし
	Key,         // キーボードのキー（code は DIK_〜。キーの場所で決まる。日本語の配列でも英語の配列の名前で呼ぶ）
	MouseButton, // マウスのボタン（code は 0=左 1=右 2=中 3・4=横）
	MouseMove,   // マウスの動き（code は InputMouseMove）
	MouseWheel,  // マウスのホイール（code は 0。上が+。1 目盛りで 1）
	PadButton,   // パッドのボタン（code は GamepadButton::〜。LT・RT をボタンとして読むのもここ）
	PadAxis,     // パッドのスティック・トリガーの量（code は InputPadAxis）
};

/// <summary>
/// マウスの動きの向き（InputDevice::MouseMove の code）
/// </summary>
enum class InputMouseMove : uint8_t {
	X, // 横の動き（右が+。マウスが数えた数。InputManager::GetMouseDeltaX と同じ）
	Y, // 縦の動き（下が+。InputManager::GetMouseDeltaY と同じ）
};

/// <summary>
/// パッドの軸（InputDevice::PadAxis の code）
/// </summary>
enum class InputPadAxis : uint8_t {
	LeftStickX,   // 左スティックの横（右が+。-1〜1）
	LeftStickY,   // 左スティックの縦（上が+）
	RightStickX,  // 右スティックの横
	RightStickY,  // 右スティックの縦
	LeftTrigger,  // LT を引いた量（0〜1）
	RightTrigger, // RT を引いた量（0〜1）
};

/// <summary>
/// 一緒に押す修飾キー（左右どちらでもよい）。ビットの組
/// </summary>
enum class InputModifiers : uint8_t {
	None = 0,
	Ctrl = 1 << 0,
	Shift = 1 << 1,
	Alt = 1 << 2,
};
constexpr InputModifiers operator|(InputModifiers a, InputModifiers b) { return static_cast<InputModifiers>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
constexpr InputModifiers operator&(InputModifiers a, InputModifiers b) { return static_cast<InputModifiers>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); }
constexpr InputModifiers operator~(InputModifiers a) { return static_cast<InputModifiers>(~static_cast<uint8_t>(a) & 0x07); }
constexpr InputModifiers& operator|=(InputModifiers& a, InputModifiers b) { return a = a | b; }

/// <summary>
/// 2軸（Vector2）の Action で、この割り当ての値をどちらの向きに足すか。1軸（Value）の Action では + か - かだけを見る
/// </summary>
enum class InputDirection : uint8_t {
	PositiveX, // 右（1軸なら +）。書かないときはこれ
	NegativeX, // 左（1軸なら -）
	PositiveY, // 上（1軸なら +）
	NegativeY, // 下（1軸なら -）
};

/// <summary>
/// 入力1つ（どのキー・ボタン・軸か）
/// </summary>
struct InputControl {
	InputDevice device = InputDevice::None;
	uint32_t code = 0; // device ごとの番号（InputDevice のコメント）

	bool operator==(const InputControl&) const = default;
	bool IsEmpty() const { return device == InputDevice::None; }
	// 軸（押す・離すではなく量）か
	bool IsAxis() const { return device == InputDevice::MouseMove || device == InputDevice::MouseWheel || device == InputDevice::PadAxis; }
};

/// <summary>
/// 割り当て1つ（入力1つ＋修飾キー＋足す向き）
/// </summary>
struct InputBinding {
	InputControl control;
	InputModifiers modifiers = InputModifiers::None;            // 一緒に押していないと効かない修飾キー
	InputDirection direction = InputDirection::PositiveX; // 値を足す向き

	bool operator==(const InputBinding&) const = default;
	bool IsEmpty() const { return control.IsEmpty(); }

	// --- コードで既定の割り当てを書くときの短い書き方 ---
	static constexpr InputBinding Key(uint32_t dik, InputModifiers modifiers = InputModifiers::None) { return {{InputDevice::Key, dik}, modifiers}; }
	static constexpr InputBinding Mouse(uint32_t button, InputModifiers modifiers = InputModifiers::None) { return {{InputDevice::MouseButton, button}, modifiers}; }
	static constexpr InputBinding Wheel(InputModifiers modifiers = InputModifiers::None) { return {{InputDevice::MouseWheel, 0}, modifiers}; }
	static constexpr InputBinding Pad(uint32_t button) { return {{InputDevice::PadButton, button}}; }
};


/// <summary>
/// 割り当てと文字の行き来・名前の一覧
/// </summary>
namespace InputBindingText {
// --- 入力1つ ---
// 保存に使う名前（"Key.Z"・"Mouse.Right"・"Pad.A"・"Pad.LeftStickX"）。一覧に無ければ空
std::string ControlName(const InputControl& control);
// 名前から入力1つ（大文字・小文字は区別する）。知らない名前なら空
std::optional<InputControl> ParseControl(std::string_view name);
// 名前を持つ入力の全部（キー・マウス・パッドの順）。割り当てを拾うとき（InputRebinder）に、新しく押した物を探すのに使う
std::span<const InputControl> AllControls();

// --- 割り当て1つ ---
// 保存に使う文字（"Ctrl+Shift+Key.Z"・"Key.A:-X"。向きが +X なら書かない）
std::string ToString(const InputBinding& binding);
// 文字から割り当て。読めなければ空
std::optional<InputBinding> Parse(std::string_view text);
// 画面に出す文字（"Ctrl+Z"・"Mouse Right"・"Pad A"。キーは "Key." を付けない。向きは書かない）
std::string ToDisplayString(const InputBinding& binding);

// --- 修飾キー ---
// そのキーが修飾キー（左右の Ctrl・Shift・Alt）なら、どれか。違えば None
InputModifiers ModifierOfKey(uint32_t dik);
} // namespace InputBindingText
```

- `InputModifiers` はビットの組なので、`|`・`&`・`~` を自分で書いた（enum class はそのままでは `|` できない）。`~` は下の3ビットだけ残す（Ctrl・Shift・Alt 以外のビットを立てない）
- `InputDirection` は Button の Action では、軸の割り当て（ホイール）の「どちらへ回したら押した」にだけ使う

### B-2 `Input/Action/InputBinding.cpp`（新しい）

名前の表（`kControlNames`）が保存の文字の正本。キーは英語の配列の名前（0.3）。

```cpp
#include "InputBinding.h"

#include <array>
#include <vector>

#include <Windows.h>
#include <dinput.h>

#include "HTTEngine/Input/XInputGamepad.h"

namespace {
//=============================================================================
// 名前の一覧
//=============================================================================
// 保存に使う名前。変えると、保存した割り当てが読めなくなる（足すのはよい）
// キーは DirectInput の番号（キーの場所）で決まる。日本語の配列の「@」は英語の配列の「[」の場所なので LeftBracket になる
struct NamedControl {
	InputControl control;
	std::string_view name;
};

// --- 作るときの短い書き方 ---
constexpr InputControl Key(uint32_t dik) { return {InputDevice::Key, dik}; }
constexpr InputControl MouseButton(uint32_t button) { return {InputDevice::MouseButton, button}; }
constexpr InputControl MouseMove(InputMouseMove axis) { return {InputDevice::MouseMove, static_cast<uint32_t>(axis)}; }
constexpr InputControl MouseWheel() { return {InputDevice::MouseWheel, 0}; }
constexpr InputControl PadButton(uint32_t button) { return {InputDevice::PadButton, button}; }
constexpr InputControl PadAxis(InputPadAxis axis) { return {InputDevice::PadAxis, static_cast<uint32_t>(axis)}; }

constexpr auto kControlNames = std::to_array<NamedControl>({
    // ===== キーボード =====
    // --- 文字 ---
    {Key(DIK_A), "Key.A"}, {Key(DIK_B), "Key.B"}, {Key(DIK_C), "Key.C"}, {Key(DIK_D), "Key.D"}, {Key(DIK_E), "Key.E"},
    {Key(DIK_F), "Key.F"}, {Key(DIK_G), "Key.G"}, {Key(DIK_H), "Key.H"}, {Key(DIK_I), "Key.I"}, {Key(DIK_J), "Key.J"},
    {Key(DIK_K), "Key.K"}, {Key(DIK_L), "Key.L"}, {Key(DIK_M), "Key.M"}, {Key(DIK_N), "Key.N"}, {Key(DIK_O), "Key.O"},
    {Key(DIK_P), "Key.P"}, {Key(DIK_Q), "Key.Q"}, {Key(DIK_R), "Key.R"}, {Key(DIK_S), "Key.S"}, {Key(DIK_T), "Key.T"},
    {Key(DIK_U), "Key.U"}, {Key(DIK_V), "Key.V"}, {Key(DIK_W), "Key.W"}, {Key(DIK_X), "Key.X"}, {Key(DIK_Y), "Key.Y"},
    {Key(DIK_Z), "Key.Z"},
    // --- 数字（上の段）---
    {Key(DIK_0), "Key.0"}, {Key(DIK_1), "Key.1"}, {Key(DIK_2), "Key.2"}, {Key(DIK_3), "Key.3"}, {Key(DIK_4), "Key.4"},
    {Key(DIK_5), "Key.5"}, {Key(DIK_6), "Key.6"}, {Key(DIK_7), "Key.7"}, {Key(DIK_8), "Key.8"}, {Key(DIK_9), "Key.9"},
    // --- ファンクションキー ---
    {Key(DIK_F1), "Key.F1"}, {Key(DIK_F2), "Key.F2"}, {Key(DIK_F3), "Key.F3"}, {Key(DIK_F4), "Key.F4"},
    {Key(DIK_F5), "Key.F5"}, {Key(DIK_F6), "Key.F6"}, {Key(DIK_F7), "Key.F7"}, {Key(DIK_F8), "Key.F8"},
    {Key(DIK_F9), "Key.F9"}, {Key(DIK_F10), "Key.F10"}, {Key(DIK_F11), "Key.F11"}, {Key(DIK_F12), "Key.F12"},
    // --- 修飾キー・空白など ---
    {Key(DIK_LSHIFT), "Key.LeftShift"}, {Key(DIK_RSHIFT), "Key.RightShift"},
    {Key(DIK_LCONTROL), "Key.LeftCtrl"}, {Key(DIK_RCONTROL), "Key.RightCtrl"},
    {Key(DIK_LMENU), "Key.LeftAlt"}, {Key(DIK_RMENU), "Key.RightAlt"},
    {Key(DIK_ESCAPE), "Key.Escape"}, {Key(DIK_TAB), "Key.Tab"}, {Key(DIK_CAPITAL), "Key.CapsLock"},
    {Key(DIK_SPACE), "Key.Space"}, {Key(DIK_RETURN), "Key.Enter"}, {Key(DIK_BACK), "Key.Backspace"},
    // --- 矢印・編集 ---
    {Key(DIK_UP), "Key.Up"}, {Key(DIK_DOWN), "Key.Down"}, {Key(DIK_LEFT), "Key.Left"}, {Key(DIK_RIGHT), "Key.Right"},
    {Key(DIK_INSERT), "Key.Insert"}, {Key(DIK_DELETE), "Key.Delete"}, {Key(DIK_HOME), "Key.Home"}, {Key(DIK_END), "Key.End"},
    {Key(DIK_PRIOR), "Key.PageUp"}, {Key(DIK_NEXT), "Key.PageDown"},
    // --- 記号（英語の配列の名前）---
    {Key(DIK_MINUS), "Key.Minus"}, {Key(DIK_EQUALS), "Key.Equals"}, {Key(DIK_LBRACKET), "Key.LeftBracket"}, {Key(DIK_RBRACKET), "Key.RightBracket"},
    {Key(DIK_BACKSLASH), "Key.Backslash"}, {Key(DIK_SEMICOLON), "Key.Semicolon"}, {Key(DIK_APOSTROPHE), "Key.Apostrophe"}, {Key(DIK_GRAVE), "Key.Grave"},
    {Key(DIK_COMMA), "Key.Comma"}, {Key(DIK_PERIOD), "Key.Period"}, {Key(DIK_SLASH), "Key.Slash"},
    // --- 日本語の配列だけにあるキー ---
    {Key(DIK_YEN), "Key.Yen"}, {Key(DIK_ABNT_C1), "Key.Ro"}, {Key(DIK_KANA), "Key.Kana"}, {Key(DIK_CONVERT), "Key.Convert"}, {Key(DIK_NOCONVERT), "Key.NoConvert"},
    // --- テンキー ---
    {Key(DIK_NUMPAD0), "Key.Numpad0"}, {Key(DIK_NUMPAD1), "Key.Numpad1"}, {Key(DIK_NUMPAD2), "Key.Numpad2"}, {Key(DIK_NUMPAD3), "Key.Numpad3"}, {Key(DIK_NUMPAD4), "Key.Numpad4"},
    {Key(DIK_NUMPAD5), "Key.Numpad5"}, {Key(DIK_NUMPAD6), "Key.Numpad6"}, {Key(DIK_NUMPAD7), "Key.Numpad7"}, {Key(DIK_NUMPAD8), "Key.Numpad8"}, {Key(DIK_NUMPAD9), "Key.Numpad9"},
    {Key(DIK_ADD), "Key.NumpadAdd"}, {Key(DIK_SUBTRACT), "Key.NumpadSubtract"}, {Key(DIK_MULTIPLY), "Key.NumpadMultiply"}, {Key(DIK_DIVIDE), "Key.NumpadDivide"},
    {Key(DIK_DECIMAL), "Key.NumpadDecimal"}, {Key(DIK_NUMPADENTER), "Key.NumpadEnter"},
    // --- そのほか ---
    {Key(DIK_SYSRQ), "Key.PrintScreen"}, {Key(DIK_SCROLL), "Key.ScrollLock"}, {Key(DIK_PAUSE), "Key.Pause"}, {Key(DIK_NUMLOCK), "Key.NumLock"},

    // ===== マウス =====
    {MouseButton(0), "Mouse.Left"}, {MouseButton(1), "Mouse.Right"}, {MouseButton(2), "Mouse.Middle"}, {MouseButton(3), "Mouse.X1"}, {MouseButton(4), "Mouse.X2"},
    {MouseMove(InputMouseMove::X), "Mouse.MoveX"}, {MouseMove(InputMouseMove::Y), "Mouse.MoveY"}, {MouseWheel(), "Mouse.Wheel"},

    // ===== パッド（Xbox の並び）=====
    {PadButton(GamepadButton::A), "Pad.A"}, {PadButton(GamepadButton::B), "Pad.B"}, {PadButton(GamepadButton::X), "Pad.X"}, {PadButton(GamepadButton::Y), "Pad.Y"},
    {PadButton(GamepadButton::LeftShoulder), "Pad.LeftShoulder"}, {PadButton(GamepadButton::RightShoulder), "Pad.RightShoulder"},
    {PadButton(GamepadButton::LeftTrigger), "Pad.LeftTrigger"}, {PadButton(GamepadButton::RightTrigger), "Pad.RightTrigger"},
    {PadButton(GamepadButton::LeftThumb), "Pad.LeftThumb"}, {PadButton(GamepadButton::RightThumb), "Pad.RightThumb"},
    {PadButton(GamepadButton::Start), "Pad.Start"}, {PadButton(GamepadButton::Back), "Pad.Back"},
    {PadButton(GamepadButton::DpadUp), "Pad.DpadUp"}, {PadButton(GamepadButton::DpadDown), "Pad.DpadDown"},
    {PadButton(GamepadButton::DpadLeft), "Pad.DpadLeft"}, {PadButton(GamepadButton::DpadRight), "Pad.DpadRight"},
    {PadAxis(InputPadAxis::LeftStickX), "Pad.LeftStickX"}, {PadAxis(InputPadAxis::LeftStickY), "Pad.LeftStickY"},
    {PadAxis(InputPadAxis::RightStickX), "Pad.RightStickX"}, {PadAxis(InputPadAxis::RightStickY), "Pad.RightStickY"},
    {PadAxis(InputPadAxis::LeftTrigger), "Pad.LeftTriggerAmount"}, {PadAxis(InputPadAxis::RightTrigger), "Pad.RightTriggerAmount"},
});

// --- 修飾キーの文字（書く順番もこの順）---
struct ModifierName {
	InputModifiers modifier;
	std::string_view name;
};
constexpr auto kModifierNames = std::to_array<ModifierName>({
    {InputModifiers::Ctrl, "Ctrl"},
    {InputModifiers::Shift, "Shift"},
    {InputModifiers::Alt, "Alt"},
});

// --- 向きの文字（+X は書かない）---
struct DirectionName {
	InputDirection direction;
	std::string_view name;
};
constexpr auto kDirectionNames = std::to_array<DirectionName>({
    {InputDirection::PositiveX, "+X"},
    {InputDirection::NegativeX, "-X"},
    {InputDirection::PositiveY, "+Y"},
    {InputDirection::NegativeY, "-Y"},
});
} // namespace


//=============================================================================
// 入力1つ
//=============================================================================
// ===== 名前 =====
std::string InputBindingText::ControlName(const InputControl& control) {
	for (const NamedControl& entry : kControlNames) {
		if (entry.control == control) {
			return std::string(entry.name);
		}
	}
	return {};
}

// ===== 名前から =====
std::optional<InputControl> InputBindingText::ParseControl(std::string_view name) {
	for (const NamedControl& entry : kControlNames) {
		if (entry.name == name) {
			return entry.control;
		}
	}
	return std::nullopt;
}

// ===== 全部 =====
std::span<const InputControl> InputBindingText::AllControls() {
	// kControlNames から入力だけを抜き出した物（初めて呼ばれたときに1回だけ作る）
	static const std::vector<InputControl> controls = [] {
		std::vector<InputControl> result;
		result.reserve(kControlNames.size());
		for (const NamedControl& entry : kControlNames) {
			result.push_back(entry.control);
		}
		return result;
	}();
	return controls;
}


//=============================================================================
// 割り当て1つ
//=============================================================================
// ===== 文字にする =====
std::string InputBindingText::ToString(const InputBinding& binding) {
	const std::string name = ControlName(binding.control);
	if (name.empty()) {
		return {};
	}
	std::string text;
	// --- 修飾キー（"Ctrl+Shift+"）---
	for (const ModifierName& entry : kModifierNames) {
		if ((binding.modifiers & entry.modifier) != InputModifiers::None) {
			text += entry.name;
			text += '+';
		}
	}
	text += name;
	// --- 向き（+X のときは書かない）---
	if (binding.direction != InputDirection::PositiveX) {
		for (const DirectionName& entry : kDirectionNames) {
			if (entry.direction == binding.direction) {
				text += ':';
				text += entry.name;
			}
		}
	}
	return text;
}

// ===== 文字から =====
std::optional<InputBinding> InputBindingText::Parse(std::string_view text) {
	InputBinding binding;
	// --- 前から「修飾キー+」を外していく（入力の名前に + は入らない）---
	for (size_t plus = text.find('+'); plus != std::string_view::npos; plus = text.find('+')) {
		const std::string_view token = text.substr(0, plus);
		bool isModifier = false;
		for (const ModifierName& entry : kModifierNames) {
			if (entry.name == token) {
				binding.modifiers |= entry.modifier;
				isModifier = true;
			}
		}
		if (!isModifier) {
			break; // ":+X" の + まで来た
		}
		text.remove_prefix(plus + 1);
	}
	// --- 後ろの ":向き" ---
	const size_t colon = text.find(':');
	if (colon != std::string_view::npos) {
		const std::string_view directionName = text.substr(colon + 1);
		bool isFound = false;
		for (const DirectionName& entry : kDirectionNames) {
			if (entry.name == directionName) {
				binding.direction = entry.direction;
				isFound = true;
			}
		}
		if (!isFound) {
			return std::nullopt;
		}
		text = text.substr(0, colon);
	}
	// --- 残りが入力の名前 ---
	const std::optional<InputControl> control = ParseControl(text);
	if (!control) {
		return std::nullopt;
	}
	binding.control = *control;
	return binding;
}

// ===== 画面に出す文字 =====
std::string InputBindingText::ToDisplayString(const InputBinding& binding) {
	std::string name = ControlName(binding.control);
	if (name.empty()) {
		return {};
	}
	constexpr std::string_view kKeyPrefix = "Key.";
	if (name.starts_with(kKeyPrefix)) {
		name.erase(0, kKeyPrefix.size()); // キーは "Z" だけ
	} else {
		name[name.find('.')] = ' '; // "Mouse Right"・"Pad A"
	}
	std::string text;
	for (const ModifierName& entry : kModifierNames) {
		if ((binding.modifiers & entry.modifier) != InputModifiers::None) {
			text += entry.name;
			text += '+';
		}
	}
	return text + name;
}


//=============================================================================
// 修飾キー
//=============================================================================
InputModifiers InputBindingText::ModifierOfKey(uint32_t dik) {
	switch (dik) {
	case DIK_LCONTROL:
	case DIK_RCONTROL:
		return InputModifiers::Ctrl;
	case DIK_LSHIFT:
	case DIK_RSHIFT:
		return InputModifiers::Shift;
	case DIK_LMENU:
	case DIK_RMENU:
		return InputModifiers::Alt;
	default:
		return InputModifiers::None;
	}
}
```

- 表の名前を変えると、みんなの `Keymap.json` が読めなくなる（その割り当てだけ飛ばして既定に戻る）。足すのはよい
- `AllControls` の `static const std::vector<InputControl> controls = [] { … }();` は、その場で作って呼ぶラムダ（初めて通ったときに1回だけ作る。C++ は関数の中の static を1回だけ初期化する）
- `Parse` は、前から `+` の手前が修飾キーのうちは外していき、修飾キーでない所で止める（`Key.A:+X` の `+` は向きの方）
- 構造体の名前を `ControlName` にすると、関数 `InputBindingText::ControlName` の中で関数の名前に隠れて型として使えない（C4430 になる）。なので `NamedControl`

## パートC InputActionMap（新しい）

### C-1 `Input/Action/InputActionMap.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Input/Action/InputBinding.h"
#include "HTTEngine/Math/Vector2.h"


/// <summary>
/// Action の値の種類
/// </summary>
enum class InputActionType : uint8_t {
	Button,  // 押す・離す（ジャンプ・Undo・ドラッグ中か）。軸を割り当てたら、その向きに 0.5 以上で押した
	Value,   // 1つの量（ホイール・トリガー）。割り当ての値を足す（向きが - なら引く）
	Vector2, // 2つの量（移動・視点）。割り当ての値を、向き（±X・±Y）の側に足す
};

/// <summary>
/// 修飾キー（Ctrl・Shift・Alt）の合わせ方。表ごとに決める
/// </summary>
enum class InputModifierMatch : uint8_t {
	Exact,   // ちょうど同じときだけ（"Ctrl+Z" は Ctrl+Shift+Z では効かない。"Z" は Ctrl を押していると効かない）。エディタのショートカット向き
	AtLeast, // 書いた修飾キーを押していればよい（ほかを一緒に押していても効く。"Space" は Shift を押しながらでも効く）。ゲーム向き
};

/// <summary>
/// 割り当てられる入力の種類（InputDevice のビットの組）
/// </summary>
enum class InputDeviceMask : uint8_t {
	None = 0,
	Key = 1 << static_cast<uint8_t>(InputDevice::Key),
	MouseButton = 1 << static_cast<uint8_t>(InputDevice::MouseButton),
	MouseMove = 1 << static_cast<uint8_t>(InputDevice::MouseMove),
	MouseWheel = 1 << static_cast<uint8_t>(InputDevice::MouseWheel),
	PadButton = 1 << static_cast<uint8_t>(InputDevice::PadButton),
	PadAxis = 1 << static_cast<uint8_t>(InputDevice::PadAxis),
	Buttons = Key | MouseButton | PadButton,                  // 押す・離すの入力だけ
	All = Buttons | MouseMove | MouseWheel | PadAxis,          // 全部
};
constexpr InputDeviceMask operator|(InputDeviceMask a, InputDeviceMask b) { return static_cast<InputDeviceMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
constexpr InputDeviceMask operator&(InputDeviceMask a, InputDeviceMask b) { return static_cast<InputDeviceMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); }
// その入力の種類が入っているか
constexpr bool Contains(InputDeviceMask mask, InputDevice device) { return (static_cast<uint8_t>(mask) & (1 << static_cast<uint8_t>(device))) != 0; }

/// <summary>
/// Action 1つの決まり（表に足すときに渡す）
/// </summary>
struct InputActionDesc {
	std::string name;    // 保存に使う名前（"SceneView.Rotate"・"Jump"）。表の中で1つだけ。変えると、保存した割り当てが外れる
	std::string label;   // 画面に出す名前
	std::string group;   // 画面の見出し（続けて足した同じ group を、まとめて出す）
	std::string context; // 効く場所。ぶつかりを調べるのは、同じ context どうしと、空（どこでも効く）との間だけ
	std::string tooltip; // 説明（画面の吹き出し）
	InputActionType type = InputActionType::Button;
	InputDeviceMask accepts = InputDeviceMask::None; // 割り当てられる入力（None なら type で決める：Button は押す入力だけ、ほかは全部）
	bool clampLength = false;                        // Vector2 の長さを 1 までにする（キーの斜め・スティックの移動。マウスの動きには付けない）
	bool isFixed = false;                            // 割り当てを変えられない（表に出してぶつかりを知らせるだけ。ほかの所でもその入力を決め打ちで使っている物）
	std::vector<InputBinding> defaults;              // 既定の割り当て（空なら、割り当てなし）
};

/// <summary>
/// 表の中の Action の番号（足した順。Find で名前から引く）
/// </summary>
struct InputActionId {
	static constexpr uint32_t kInvalid = UINT32_MAX;
	uint32_t index = kInvalid;
	bool IsValid() const { return index != kInvalid; }
	bool operator==(const InputActionId&) const = default;
};

/// <summary>
/// ぶつかっている割り当て1つ（どの Action の何番目か）
/// </summary>
struct InputBindingRef {
	InputActionId action;
	int index = -1;
};


/// <summary>
/// Input Action の表（名前の付いた操作と、それに割り当てた入力の一覧）
/// <para>毎フレーム1回 Update で入力を読み、IsPressed などで Action の名前（番号）から聞く。キー・ボタンを直接書かないので、割り当てを人ごとに変えられる</para>
/// <para>割り当ては「既定」と「人ごとの上書き」に分けて持つ。SaveOverrides は既定から変えた Action だけをファイルに書く</para>
/// <para>エディタの操作（EditorKeymap）とゲームの操作の両方で使う。読む決まり（InputGate）と修飾キーの合わせ方は、表ごとに決める</para>
/// </summary>
class InputActionMap {
public:
	InputActionMap(InputGate gate, InputModifierMatch modifierMatch, InputDeviceMask allowedDevices = InputDeviceMask::All);

	//=========================================================================
	// 登録
	//=========================================================================
	// Action を足す（名前がもうあれば足さずに、その番号を返す）
	InputActionId AddAction(InputActionDesc desc);
	// 名前から番号（無ければ無効な番号）
	InputActionId Find(std::string_view name) const;
	size_t GetActionCount() const { return actions_.size(); }
	const InputActionDesc& GetDesc(InputActionId id) const;
	// その Action に割り当てられる入力（表の allowedDevices と、Action の accepts の両方に入っている物）
	InputDeviceMask GetAcceptedDevices(InputActionId id) const;

	//=========================================================================
	// 毎フレーム
	//=========================================================================
	// 入力を読んで、全部の Action の今の状態を決める（フレームの頭に1回。2回呼ぶと、押した瞬間が分からなくなる）
	void Update();
	// false の間は、全部の Action を「押していない」にする（割り当てを拾っている間など）。true に戻したとき押していた物は、一度離すまで効かない
	void SetEnabled(bool enabled);
	bool IsEnabled() const { return isEnabled_; }
	// 読むパッドの番号（0〜3。初めは 0）。2人で遊ぶなら、表を2つ作って番号を変える
	void SetGamepadIndex(int index) { gamepadIndex_ = index; }
	// 読む決まり（拾うとき InputRebinder も同じ決まりで読む）
	InputGate GetGate() const { return gate_; }
	int GetGamepadIndex() const { return gamepadIndex_; }

	//=========================================================================
	// 聞く
	//=========================================================================
	bool IsPressed(InputActionId id) const;   // 押している間
	bool IsTriggered(InputActionId id) const; // 押した瞬間
	bool IsReleased(InputActionId id) const;  // 離した瞬間
	float GetValue(InputActionId id) const;   // Value の量（Button は 0 か 1、Vector2 は長さ）
	Vector2 GetVector2(InputActionId id) const; // Vector2 の量（Button・Value は x だけ）
	// 名前でも聞ける（毎回名前を探すので、毎フレームたくさん聞くなら Find で番号にしておく）
	bool IsPressed(std::string_view name) const { return IsPressed(Find(name)); }
	bool IsTriggered(std::string_view name) const { return IsTriggered(Find(name)); }
	bool IsReleased(std::string_view name) const { return IsReleased(Find(name)); }
	float GetValue(std::string_view name) const { return GetValue(Find(name)); }
	Vector2 GetVector2(std::string_view name) const { return GetVector2(Find(name)); }

	//=========================================================================
	// 割り当て
	//=========================================================================
	const std::vector<InputBinding>& GetBindings(InputActionId id) const;
	// 割り当てを全部入れ替える（入れ替えた Action は、今押している物を一度離すまで効かない。変えられない Action（isFixed）は何もしない。下の3つも同じ）
	void SetBindings(InputActionId id, std::vector<InputBinding> bindings);
	// index 番目を入れ替える（index が数と同じなら後ろに足す）
	void SetBinding(InputActionId id, int index, const InputBinding& binding);
	// index 番目を消す
	void RemoveBinding(InputActionId id, int index);
	// 既定に戻す
	void ResetToDefault(InputActionId id);
	void ResetAllToDefault();
	bool IsDefault(InputActionId id) const;
	// 画面に出す文字（1つ目の割り当て。割り当てが無ければ空）。メニューのショートカットの欄などに
	std::string GetDisplayText(InputActionId id) const;

	//=========================================================================
	// ぶつかり
	//=========================================================================
	// id の index 番目の割り当てと、同時に効いてしまう割り当て（同じ入力・合う修飾キー・効く場所が重なる）の一覧
	std::vector<InputBindingRef> FindConflicts(InputActionId id, int index) const;

	//=========================================================================
	// 入力を読む（表と、拾う係 InputRebinder の両方が使う）
	//=========================================================================
	// 入力1つの今の値（ボタンは 0 か 1、軸はその量。gate の決まりで読む。パッドは gamepadIndex 番）
	static float ReadControl(const InputControl& control, InputGate gate, int gamepadIndex);
	// 今押している修飾キー
	static InputModifiers ReadModifiers(InputGate gate);

	//=========================================================================
	// 保存（人ごとの上書き）
	//=========================================================================
	// 上書きを読む（ファイルが無ければ既定のまま。知らない Action・読めない割り当ては飛ばして、ログに出す）。読んだ状態を「保存した状態」にする
	bool LoadOverrides(const std::filesystem::path& path);
	// 既定から変えた Action だけを書く。書いた状態を「保存した状態」にする
	bool SaveOverrides(const std::filesystem::path& path);
	// 保存した状態から変えたか・保存した状態に戻す
	bool IsDirty() const;
	void RevertToSaved();

private:
	// Action 1つ
	struct Action {
		InputActionDesc desc;
		std::vector<InputBinding> bindings; // 今の割り当て
		std::vector<InputBinding> saved;    // 保存した（読んだ）ときの割り当て
		// --- 今のフレームの状態 ---
		bool isDown = false;       // 押している
		bool wasDown = false;      // 前のフレームに押していた
		bool waitRelease = false;  // 一度離すまで効かせない（割り当てを変えた・止めていたのを戻した）
		Vector2 value = {0.0f, 0.0f};
	};

	const Action* Get(InputActionId id) const;
	// 書き換えてよい Action（無効な番号・変えられない Action なら nullptr）
	Action* GetEditable(InputActionId id);
	// 修飾キーが合っているか（割り当ての入力そのものが修飾キーなら、それは数えない）
	bool MatchesModifiers(const InputBinding& binding, InputModifiers held) const;
	// 2つの割り当てが、同時に効いてしまうか
	bool Overlaps(const Action& a, const InputBinding& ba, const Action& b, const InputBinding& bb) const;
	// 聞いてよい状態か（止めている・離すのを待っている間は false）
	bool IsActive(const Action& action) const { return isEnabled_ && !action.waitRelease; }

	std::vector<Action> actions_;
	InputGate gate_;
	InputModifierMatch modifierMatch_;
	InputDeviceMask allowedDevices_;
	bool isEnabled_ = true;
	int gamepadIndex_ = 0; // 読むパッドの番号（0〜3）
};
```

- `InputActionDesc` は集まりなので、`{.name = "Jump", .type = InputActionType::Button, .defaults = {InputBinding::Key(DIK_SPACE)}}` のように名前を付けて書ける（C++20 の指示付き初期化。書く順番はメンバの順。飛ばしてよい）
- `InputActionId` は番号を包んだだけの型。`uint32_t` のままだと、ほかの番号（Entity の番号など）を渡しても通ってしまう
- `ReadControl`・`ReadModifiers` を static にして外へ出したのは、拾う係（パートD）も同じ読み方をするため（同じコードを2か所に書かない）

### C-2 `Input/Action/InputActionMap.cpp`（新しい）

```cpp
#include "InputActionMap.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <utility>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"

namespace {
using json = nlohmann::json;

constexpr float kPressThreshold = 0.5f; // 軸をボタンとして読むとき・Value と Vector2 を「押している」とするとき、この量から
constexpr int kFileVersion = 1;         // 上書きのファイルの形の版（形を変えたら上げる）

// 向きが - か（-X・-Y）
bool IsNegative(InputDirection direction) { return direction == InputDirection::NegativeX || direction == InputDirection::NegativeY; }
// 向きが Y か（+Y・-Y）
bool IsVertical(InputDirection direction) { return direction == InputDirection::PositiveY || direction == InputDirection::NegativeY; }
} // namespace


//=============================================================================
// 作る・登録
//=============================================================================
InputActionMap::InputActionMap(InputGate gate, InputModifierMatch modifierMatch, InputDeviceMask allowedDevices)
    : gate_(gate), modifierMatch_(modifierMatch), allowedDevices_(allowedDevices) {}

// ===== Action を足す =====
InputActionId InputActionMap::AddAction(InputActionDesc desc) {
	HTT_ASSERT_MSG(!desc.name.empty(), "Input Action の名前が空です");
	const InputActionId existing = Find(desc.name);
	if (existing.IsValid()) {
		LogManager::Warning("[InputActionMap] 同じ名前の Action がもうあります: " + desc.name);
		return existing;
	}
	Action action;
	action.bindings = desc.defaults;
	action.saved = desc.defaults;
	action.desc = std::move(desc);
	actions_.push_back(std::move(action));
	return {static_cast<uint32_t>(actions_.size() - 1)};
}

// ===== 名前から番号 =====
InputActionId InputActionMap::Find(std::string_view name) const {
	for (size_t i = 0; i < actions_.size(); ++i) {
		if (actions_[i].desc.name == name) {
			return {static_cast<uint32_t>(i)};
		}
	}
	return {};
}

const InputActionDesc& InputActionMap::GetDesc(InputActionId id) const {
	const Action* action = Get(id);
	HTT_ASSERT_MSG(action != nullptr, "無効な Input Action の番号です");
	return action->desc;
}

// ===== 割り当てられる入力 =====
InputDeviceMask InputActionMap::GetAcceptedDevices(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr) {
		return InputDeviceMask::None;
	}
	InputDeviceMask accepts = action->desc.accepts;
	if (accepts == InputDeviceMask::None) {
		accepts = (action->desc.type == InputActionType::Button) ? InputDeviceMask::Buttons : InputDeviceMask::All;
	}
	return accepts & allowedDevices_;
}

const InputActionMap::Action* InputActionMap::Get(InputActionId id) const {
	if (!id.IsValid() || id.index >= actions_.size()) {
		return nullptr;
	}
	return &actions_[id.index];
}


//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 入力を読んで、全部の Action の状態を決める =====
void InputActionMap::Update() {
	const InputModifiers held = ReadModifiers(gate_);
	for (Action& action : actions_) {
		// --- 割り当てを全部見て、足す ---
		bool isAnyDown = false;
		Vector2 sum = {0.0f, 0.0f};
		for (const InputBinding& binding : action.bindings) {
			if (binding.IsEmpty() || !Contains(allowedDevices_, binding.control.device) || !MatchesModifiers(binding, held)) {
				continue;
			}
			const float raw = ReadControl(binding.control, gate_, gamepadIndex_);
			const float signedValue = IsNegative(binding.direction) ? -raw : raw;
			switch (action.desc.type) {
			case InputActionType::Button:
				// ボタンは押していれば 1。軸は、割り当ての向きに 0.5 以上で押した（ホイールの上だけ、など）
				if ((binding.control.IsAxis() ? signedValue : raw) >= kPressThreshold) {
					isAnyDown = true;
				}
				break;
			case InputActionType::Value:
				sum.x += signedValue;
				break;
			case InputActionType::Vector2:
				(IsVertical(binding.direction) ? sum.y : sum.x) += signedValue;
				break;
			}
		}

		// --- 種類ごとに、量と「押しているか」を決める ---
		bool isDown = false;
		switch (action.desc.type) {
		case InputActionType::Button:
			action.value = {isAnyDown ? 1.0f : 0.0f, 0.0f};
			isDown = isAnyDown;
			break;
		case InputActionType::Value:
			action.value = {sum.x, 0.0f};
			isDown = std::abs(sum.x) >= kPressThreshold;
			break;
		case InputActionType::Vector2:
			// キーの斜め（長さ √2）・キーとスティックを一緒に倒したとき、長さを 1 までにする
			if (action.desc.clampLength && LengthSq(sum) > 1.0f) {
				sum = Normalize(sum);
			}
			action.value = sum;
			isDown = Length(sum) >= kPressThreshold;
			break;
		}
		action.wasDown = action.isDown;
		action.isDown = isDown;

		// --- 一度離した（前のフレームも今も押していない）ら、また効かせる ---
		// 前のフレームも見るのは、離したフレームに「離した瞬間」を出さないため
		if (action.waitRelease && !action.isDown && !action.wasDown) {
			action.waitRelease = false;
		}
	}
}

// ===== 止める・戻す =====
void InputActionMap::SetEnabled(bool enabled) {
	if (enabled && !isEnabled_) {
		// 止めている間に押した物（割り当てを拾ったキーなど）が、戻した瞬間に効かないように
		for (Action& action : actions_) {
			action.waitRelease = true;
		}
	}
	isEnabled_ = enabled;
}


//=============================================================================
// 聞く
//=============================================================================
bool InputActionMap::IsPressed(InputActionId id) const {
	const Action* action = Get(id);
	return action && IsActive(*action) && action->isDown;
}
bool InputActionMap::IsTriggered(InputActionId id) const {
	const Action* action = Get(id);
	return action && IsActive(*action) && action->isDown && !action->wasDown;
}
bool InputActionMap::IsReleased(InputActionId id) const {
	const Action* action = Get(id);
	return action && IsActive(*action) && !action->isDown && action->wasDown;
}
float InputActionMap::GetValue(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr || !IsActive(*action)) {
		return 0.0f;
	}
	if (action->desc.type == InputActionType::Vector2) {
		return Length(action->value);
	}
	return action->value.x;
}
Vector2 InputActionMap::GetVector2(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr || !IsActive(*action)) {
		return {0.0f, 0.0f};
	}
	return action->value;
}


//=============================================================================
// 入力を読む
//=============================================================================
// ===== 入力1つの今の値 =====
float InputActionMap::ReadControl(const InputControl& control, InputGate gate, int gamepadIndex) {
	switch (control.device) {
	case InputDevice::None:
		return 0.0f;
	case InputDevice::Key:
		return InputManager::IsKeyPressed(static_cast<uint8_t>(control.code), gate) ? 1.0f : 0.0f;
	case InputDevice::MouseButton:
		return InputManager::IsMouseButtonPressed(static_cast<int>(control.code), gate) ? 1.0f : 0.0f;
	case InputDevice::MouseMove:
		if (static_cast<InputMouseMove>(control.code) == InputMouseMove::Y) {
			return static_cast<float>(InputManager::GetMouseDeltaY(gate));
		}
		return static_cast<float>(InputManager::GetMouseDeltaX(gate));
	case InputDevice::MouseWheel:
		return static_cast<float>(InputManager::GetMouseDeltaWheel(gate)) / static_cast<float>(WHEEL_DELTA); // 1 目盛りで 1
	case InputDevice::PadButton:
		return InputManager::IsButtonPressed(control.code, gamepadIndex) ? 1.0f : 0.0f;
	case InputDevice::PadAxis:
		switch (static_cast<InputPadAxis>(control.code)) {
		case InputPadAxis::LeftStickX:
			return InputManager::GetLeftStickX(gamepadIndex);
		case InputPadAxis::LeftStickY:
			return InputManager::GetLeftStickY(gamepadIndex);
		case InputPadAxis::RightStickX:
			return InputManager::GetRightStickX(gamepadIndex);
		case InputPadAxis::RightStickY:
			return InputManager::GetRightStickY(gamepadIndex);
		case InputPadAxis::LeftTrigger:
			return InputManager::GetLeftTrigger(gamepadIndex);
		case InputPadAxis::RightTrigger:
			return InputManager::GetRightTrigger(gamepadIndex);
		}
		return 0.0f;
	}
	return 0.0f;
}

// ===== 今押している修飾キー =====
InputModifiers InputActionMap::ReadModifiers(InputGate gate) {
	InputModifiers held = InputModifiers::None;
	if (InputManager::IsKeyPressed(DIK_LCONTROL, gate) || InputManager::IsKeyPressed(DIK_RCONTROL, gate)) {
		held |= InputModifiers::Ctrl;
	}
	if (InputManager::IsKeyPressed(DIK_LSHIFT, gate) || InputManager::IsKeyPressed(DIK_RSHIFT, gate)) {
		held |= InputModifiers::Shift;
	}
	if (InputManager::IsKeyPressed(DIK_LMENU, gate) || InputManager::IsKeyPressed(DIK_RMENU, gate)) {
		held |= InputModifiers::Alt;
	}
	return held;
}

// ===== 修飾キーが合っているか =====
bool InputActionMap::MatchesModifiers(const InputBinding& binding, InputModifiers held) const {
	// 割り当ての入力そのものが修飾キー（"Key.LeftShift" でダッシュ など）なら、それを押していることは数えない
	const InputModifiers own = (binding.control.device == InputDevice::Key) ? InputBindingText::ModifierOfKey(binding.control.code) : InputModifiers::None;
	const InputModifiers others = held & ~own;
	if (modifierMatch_ == InputModifierMatch::Exact) {
		return others == binding.modifiers;
	}
	return (others & binding.modifiers) == binding.modifiers;
}


//=============================================================================
// 割り当て
//=============================================================================
const std::vector<InputBinding>& InputActionMap::GetBindings(InputActionId id) const {
	const Action* action = Get(id);
	HTT_ASSERT_MSG(action != nullptr, "無効な Input Action の番号です");
	return action->bindings;
}

// ===== 書き換えてよい Action（無効な番号・変えられない Action なら nullptr）=====
InputActionMap::Action* InputActionMap::GetEditable(InputActionId id) {
	const Action* action = Get(id);
	if (action == nullptr || action->desc.isFixed) {
		return nullptr;
	}
	return &actions_[id.index];
}

// ===== 全部入れ替える =====
void InputActionMap::SetBindings(InputActionId id, std::vector<InputBinding> bindings) {
	Action* action = GetEditable(id);
	if (action == nullptr) {
		return;
	}
	action->bindings = std::move(bindings);
	action->waitRelease = true; // 新しい割り当てのキーを今押していても、一度離すまで効かせない
}

// ===== 1つ入れ替える・足す =====
void InputActionMap::SetBinding(InputActionId id, int index, const InputBinding& binding) {
	Action* action = GetEditable(id);
	if (action == nullptr || index < 0) {
		return;
	}
	if (index < static_cast<int>(action->bindings.size())) {
		action->bindings[index] = binding;
	} else {
		action->bindings.push_back(binding);
	}
	action->waitRelease = true;
}

// ===== 1つ消す =====
void InputActionMap::RemoveBinding(InputActionId id, int index) {
	Action* action = GetEditable(id);
	if (action == nullptr || index < 0 || index >= static_cast<int>(action->bindings.size())) {
		return;
	}
	action->bindings.erase(action->bindings.begin() + index);
}

// ===== 既定に戻す =====
void InputActionMap::ResetToDefault(InputActionId id) {
	const Action* action = Get(id);
	if (action) {
		SetBindings(id, action->desc.defaults);
	}
}
void InputActionMap::ResetAllToDefault() {
	for (Action& action : actions_) {
		action.bindings = action.desc.defaults;
		action.waitRelease = true;
	}
}
bool InputActionMap::IsDefault(InputActionId id) const {
	const Action* action = Get(id);
	return action && action->bindings == action->desc.defaults;
}

// ===== 画面に出す文字 =====
std::string InputActionMap::GetDisplayText(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr) {
		return {};
	}
	for (const InputBinding& binding : action->bindings) {
		if (!binding.IsEmpty()) {
			return InputBindingText::ToDisplayString(binding);
		}
	}
	return {};
}


//=============================================================================
// ぶつかり
//=============================================================================
// ===== 一覧 =====
std::vector<InputBindingRef> InputActionMap::FindConflicts(InputActionId id, int index) const {
	std::vector<InputBindingRef> conflicts;
	const Action* self = Get(id);
	if (self == nullptr || index < 0 || index >= static_cast<int>(self->bindings.size())) {
		return conflicts;
	}
	const InputBinding& binding = self->bindings[index];
	if (binding.IsEmpty()) {
		return conflicts;
	}
	for (size_t a = 0; a < actions_.size(); ++a) {
		const Action& other = actions_[a];
		for (size_t b = 0; b < other.bindings.size(); ++b) {
			if (a == id.index && static_cast<int>(b) == index) {
				continue; // 自分
			}
			if (Overlaps(*self, binding, other, other.bindings[b])) {
				conflicts.push_back({{static_cast<uint32_t>(a)}, static_cast<int>(b)});
			}
		}
	}
	return conflicts;
}

// ===== 2つの割り当てが、同時に効いてしまうか =====
bool InputActionMap::Overlaps(const Action& a, const InputBinding& ba, const Action& b, const InputBinding& bb) const {
	// --- 入力が違えば、ぶつからない ---
	if (ba.control != bb.control || ba.IsEmpty()) {
		return false;
	}
	// --- 効く場所が重ならなければ、ぶつからない（空はどこでも効く）---
	if (!a.desc.context.empty() && !b.desc.context.empty() && a.desc.context != b.desc.context) {
		return false;
	}
	// --- 軸を、ボタンとして逆の向きに使い分けている（ホイールの上と下）なら、ぶつからない ---
	if (ba.control.IsAxis() && a.desc.type == InputActionType::Button && b.desc.type == InputActionType::Button && IsNegative(ba.direction) != IsNegative(bb.direction)) {
		return false;
	}
	// --- 修飾キー ---
	if (modifierMatch_ == InputModifierMatch::Exact) {
		return ba.modifiers == bb.modifiers;
	}
	// AtLeast：片方の修飾キーが、もう片方に全部入っていれば、両方効く時がある（"Space" と "Shift+Space"）
	return (ba.modifiers & bb.modifiers) == ba.modifiers || (ba.modifiers & bb.modifiers) == bb.modifiers;
}


//=============================================================================
// 保存（人ごとの上書き）
//=============================================================================
// ===== 読む =====
bool InputActionMap::LoadOverrides(const std::filesystem::path& path) {
	// 前に読んだ上書きが残らないように、全部既定から始める
	for (Action& action : actions_) {
		action.bindings = action.desc.defaults;
		action.waitRelease = true;
	}
	auto markSaved = [this] {
		for (Action& action : actions_) {
			action.saved = action.bindings;
		}
	};

	std::ifstream ifs(path);
	if (!ifs) {
		markSaved(); // まだ保存していない：既定のまま
		return false;
	}
	// 例外を投げない版のパース
	const json root = json::parse(ifs, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error("[InputActionMap] 割り当てのファイルを読めません（既定のままにします）: " + path.string());
		markSaved();
		return false;
	}
	if (root.value("version", 0) != kFileVersion) {
		LogManager::Warning("[InputActionMap] 割り当てのファイルの版が違います: " + path.string());
	}

	const auto bindings = root.find("bindings");
	if (bindings != root.end() && bindings->is_object()) {
		for (const auto& [name, list] : bindings->items()) {
			const InputActionId id = Find(name);
			if (!id.IsValid() || !list.is_array() || actions_[id.index].desc.isFixed) {
				LogManager::Warning("[InputActionMap] 知らない（変えられない）Action を飛ばしました: " + name);
				continue;
			}
			// --- 割り当ての文字を1つずつ読む（読めない物だけ飛ばす）---
			std::vector<InputBinding> parsed;
			for (const json& text : list) {
				const std::optional<InputBinding> binding = text.is_string() ? InputBindingText::Parse(text.get<std::string>()) : std::nullopt;
				if (!binding) {
					LogManager::Warning("[InputActionMap] 読めない割り当てを飛ばしました: " + name + " " + text.dump());
					continue;
				}
				parsed.push_back(*binding);
			}
			actions_[id.index].bindings = std::move(parsed);
		}
	}
	markSaved();
	return true;
}

// ===== 書く =====
bool InputActionMap::SaveOverrides(const std::filesystem::path& path) {
	// --- 既定から変えた Action だけ（割り当てを全部外したのも、空の配列で書く）---
	json bindings = json::object();
	for (const Action& action : actions_) {
		if (action.bindings == action.desc.defaults) {
			continue;
		}
		json list = json::array();
		for (const InputBinding& binding : action.bindings) {
			list.push_back(InputBindingText::ToString(binding));
		}
		bindings[action.desc.name] = std::move(list);
	}
	const json root = {{"version", kFileVersion}, {"bindings", std::move(bindings)}};

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream ofs(path);
	if (!ofs) {
		LogManager::Error("[InputActionMap] 割り当てのファイルを書けません: " + path.string());
		return false;
	}
	ofs << std::setw(4) << root << std::endl;
	for (Action& action : actions_) {
		action.saved = action.bindings;
	}
	LogManager::Log("[InputActionMap] 保存しました: " + path.string());
	return true;
}

// ===== 保存した状態から変えたか =====
bool InputActionMap::IsDirty() const {
	for (const Action& action : actions_) {
		if (action.bindings != action.saved) {
			return true;
		}
	}
	return false;
}

// ===== 保存した状態に戻す =====
void InputActionMap::RevertToSaved() {
	for (Action& action : actions_) {
		if (action.bindings != action.saved) {
			action.bindings = action.saved;
			action.waitRelease = true;
		}
	}
}
```

- `Update` の `waitRelease` を外す条件は「今も前のフレームも押していない」。今だけを見ると、離したフレームに「離した瞬間」（`IsReleased`）が出てしまう
- `json::parse(ifs, nullptr, false)` は例外を投げない版。壊れたファイルは `is_discarded()` で分かる（GlobalVariables と同じ）
- `root.value("version", 0)` は「`version` があればその値、無ければ 0」
- 書いた json は nlohmann の決まりで名前の順に並ぶ（`bindings` が `version` より上）

## パートD InputRebinder（新しい）

### D-1 `Input/Action/InputRebinder.h`（新しい）

```cpp
#pragma once
#include <optional>
#include <vector>

#include "HTTEngine/Input/Action/InputActionMap.h"


/// <summary>
/// 割り当てを拾う係（「次に押したキー・ボタン」を、Action の割り当てにする）
/// <para>Start の後、毎フレーム Update を呼ぶ。拾えたら Captured を返し、GetCaptured で割り当てを渡す（表に入れるのは呼ぶ側。ぶつかりを見せてから入れたいので）</para>
/// <para>Update は、入力を読める所（表の InputActionMap::Update と同じ、フレームの頭）で呼ぶ。WindowManager::UpdateAll の後（描く間・ImGui の中）は InputManager がウィンドウを手前でない扱いにするので、何も拾えない</para>
/// <para>始めたときに押していた物（Start を押したクリックなど）は、離して押し直すまで拾わない。Esc でやめる（なので Esc は割り当てられない）</para>
/// <para>修飾キー（Ctrl・Shift・Alt）を押しながらほかを押すと "Ctrl+Z" のように拾う。修飾キーだけを押して離すと、そのキー1つを拾う</para>
/// <para>拾っている間は、その表の SetEnabled(false) にしておく（Ctrl+Z を拾うときに Undo が動かないように）。エディタの Keymap とゲームのキーコンフィグの画面の両方で使う</para>
/// </summary>
class InputRebinder {
public:
	// Update の結果
	enum class Result {
		Idle,      // 拾っていない
		Listening, // 拾っている（まだ何も押していない）
		Captured,  // 拾えた（GetCaptured で受け取る）
		Canceled,  // Esc でやめた
	};

	// 拾い始める（map の id の、index 番目の割り当てを作る。index が割り当ての数と同じなら、新しく足す分）
	void Start(const InputActionMap& map, InputActionId id, int index);
	// やめる
	void Cancel();
	// 毎フレーム1回
	Result Update();

	bool IsListening() const { return map_ != nullptr; }
	InputActionId GetAction() const { return action_; }
	int GetIndex() const { return index_; }
	// 拾った割り当て（Captured を返した後だけ。向きは、元の割り当ての向きを引き継ぐ）
	const std::optional<InputBinding>& GetCaptured() const { return captured_; }

private:
	// 入力1つを、拾うときの決まりで「押している」とみなすか（軸は大きく動かしたときだけ）
	bool IsDown(const InputControl& control) const;
	// 拾い終える
	void Finish(const InputBinding& binding);

	const InputActionMap* map_ = nullptr; // 拾っている表（拾っていなければ nullptr。持ち主ではない）
	InputActionId action_;
	int index_ = -1;
	InputDeviceMask accepts_ = InputDeviceMask::None; // 拾ってよい入力
	InputDirection direction_ = InputDirection::PositiveX; // 作る割り当ての向き（元の割り当てから）
	bool isFirstUpdate_ = true;                       // 始めてから、まだ Update していない（最初の Update は、押している物を覚えるだけ）
	std::optional<InputControl> pendingModifierKey_; // 修飾キーだけを押している（離したら、それ1つを拾う）
	InputModifiers pendingModifiers_ = InputModifiers::None; // 修飾キーを押したときに、一緒に押していたほかの修飾キー
	std::vector<bool> wasDown_;                       // 前のフレームに押していたか（InputBindingText::AllControls の順）
	std::optional<InputBinding> captured_;
};
```

### D-2 `Input/Action/InputRebinder.cpp`（新しい）

```cpp
#include "InputRebinder.h"

#include <cmath>

#include <Windows.h>
#include <dinput.h>

namespace {
constexpr float kAxisThreshold = 0.5f;       // スティック・トリガーを、この量より倒したら拾う
constexpr float kMouseMoveThreshold = 40.0f; // マウスを1フレームにこの数より動かしたら拾う（ボタンへ動かす途中で拾わないように大きめ）
constexpr InputControl kCancelKey = {InputDevice::Key, DIK_ESCAPE}; // やめるキー
} // namespace


//=============================================================================
// 始める・やめる
//=============================================================================
// ===== 始める =====
void InputRebinder::Start(const InputActionMap& map, InputActionId id, int index) {
	map_ = &map;
	action_ = id;
	index_ = index;
	accepts_ = map.GetAcceptedDevices(id);
	// 向きは元の割り当てから引き継ぐ（Vector2 の「上」の割り当てを入れ替えたら、新しいキーも「上」）
	const std::vector<InputBinding>& bindings = map.GetBindings(id);
	direction_ = (index >= 0 && index < static_cast<int>(bindings.size())) ? bindings[index].direction : InputDirection::PositiveX;
	isFirstUpdate_ = true;
	pendingModifierKey_.reset();
	pendingModifiers_ = InputModifiers::None;
	wasDown_.assign(InputBindingText::AllControls().size(), false);
	captured_.reset();
}

// ===== やめる =====
void InputRebinder::Cancel() {
	map_ = nullptr;
	pendingModifierKey_.reset();
}

// ===== 拾い終える =====
void InputRebinder::Finish(const InputBinding& binding) {
	captured_ = binding;
	map_ = nullptr;
	pendingModifierKey_.reset();
}


//=============================================================================
// 毎フレーム
//=============================================================================
InputRebinder::Result InputRebinder::Update() {
	if (map_ == nullptr) {
		return Result::Idle;
	}
	const std::span<const InputControl> controls = InputBindingText::AllControls();

	// --- 今押している物 ---
	std::vector<bool> isDown(controls.size(), false);
	for (size_t i = 0; i < controls.size(); ++i) {
		isDown[i] = IsDown(controls[i]);
	}

	// --- 始めた最初のフレームは、今押している物を覚えるだけ ---
	// 覚えた物は「前のフレームに押していた」になるので、離して押し直すまで拾わない（Start を押したクリック・Enter など）
	// 全部を離すまで待つ形にしないのは、押しっぱなしに見えるキーがあるため（日本語のキーボードの半角/全角などは、DirectInput に離した知らせが来ないことがある）
	if (isFirstUpdate_) {
		isFirstUpdate_ = false;
		wasDown_ = isDown;
		return Result::Listening;
	}

	// --- 新しく押した物を探す ---
	// ボタンの Action はボタンを先に、量の Action は軸を先に見る（トリガーを引いたら、量の Action は "LeftTriggerAmount" を拾う）
	const bool isButtonAction = map_->GetDesc(action_).type == InputActionType::Button;
	const InputModifiers held = InputActionMap::ReadModifiers(map_->GetGate());
	for (const bool isAxisPass : {!isButtonAction, isButtonAction}) {
		for (size_t i = 0; i < controls.size(); ++i) {
			const InputControl& control = controls[i];
			if (control.IsAxis() != isAxisPass || !isDown[i] || wasDown_[i]) {
				continue;
			}
			if (control == kCancelKey) {
				Cancel();
				return Result::Canceled;
			}
			if (!Contains(accepts_, control.device)) {
				continue;
			}
			// 修飾キーは、ほかを押すかもしれないので、離すまで待つ
			const InputModifiers own = (control.device == InputDevice::Key) ? InputBindingText::ModifierOfKey(control.code) : InputModifiers::None;
			if (own != InputModifiers::None) {
				pendingModifierKey_ = control;
				pendingModifiers_ = held & ~own;
				continue;
			}
			InputBinding binding = {control, held, direction_};
			// ボタンの Action に軸（ホイールなど）を割り当てるときは、動かした向きを使う（ホイールの下で「次へ」など）
			if (isButtonAction && control.IsAxis()) {
				binding.direction = (InputActionMap::ReadControl(control, map_->GetGate(), map_->GetGamepadIndex()) < 0.0f) ? InputDirection::NegativeX : InputDirection::PositiveX;
			}
			Finish(binding);
			return Result::Captured;
		}
	}

	// --- 修飾キーだけを押して、ほかを押さずに離した：そのキー1つを拾う ---
	if (pendingModifierKey_ && !IsDown(*pendingModifierKey_)) {
		Finish({*pendingModifierKey_, pendingModifiers_, direction_});
		return Result::Captured;
	}

	wasDown_ = isDown;
	return Result::Listening;
}


//=============================================================================
// 読む
//=============================================================================
// ===== 拾うときの決まりで「押している」か =====
bool InputRebinder::IsDown(const InputControl& control) const {
	const float value = InputActionMap::ReadControl(control, map_->GetGate(), map_->GetGamepadIndex());
	switch (control.device) {
	case InputDevice::MouseMove:
		return std::abs(value) >= kMouseMoveThreshold;
	case InputDevice::MouseWheel:
		return value != 0.0f;
	case InputDevice::PadAxis:
		return std::abs(value) >= kAxisThreshold;
	default:
		return value >= kAxisThreshold; // ボタン（0 か 1）
	}
}
```

- 名前の表（`AllControls`）を全部読んで、前のフレームに押していなかった物を探す（拾っている間だけなので、毎フレーム 140 個ほど読んでもよい）
- `for (const bool isAxisPass : {!isButtonAction, isButtonAction})` は、2回まわすループ（1回目が軸か、ボタンか）。量の Action（Value・Vector2）でトリガーを引くと、ボタンの `Pad.LeftTrigger` ではなく量の `Pad.LeftTriggerAmount` を拾う
- マウスの動きは、1フレームに 40 より動かしたときだけ拾う（割り当てたいボタンへカーソルを動かす途中で拾わないように）
- 拾い始めの最初のフレームは、今押している物を `wasDown_` に覚えるだけ（`isFirstUpdate_`）。覚えた物は「新しく押した」にならないので、離して押し直すまで拾わない。押しっぱなしに見えるキー（0.8）があっても、ほかのキーは拾える

## パートE vcxproj（段階1）

`HTTEngine.vcxproj` の `ClCompile` の並びに3行、`ClInclude` の並びに3行足す（`Input\XInputGamepad` の後）。

```xml
    <ClCompile Include="HTTEngine\Input\Action\InputActionMap.cpp" />
    <ClCompile Include="HTTEngine\Input\Action\InputBinding.cpp" />
    <ClCompile Include="HTTEngine\Input\Action\InputRebinder.cpp" />
```

```xml
    <ClInclude Include="HTTEngine\Input\Action\InputActionMap.h" />
    <ClInclude Include="HTTEngine\Input\Action\InputBinding.h" />
    <ClInclude Include="HTTEngine\Input\Action\InputRebinder.h" />
```

`HTTEngine.vcxproj.filters` は、フィルタを1つ（`HTTEngine\Input` の後）と、6つのファイルを足す。

```xml
    <Filter Include="HTTEngine\Input\Action">
      <UniqueIdentifier>{7c3e9a51-2f4b-4d8e-9b6a-1e5d3c8f2a47}</UniqueIdentifier>
    </Filter>
```

```xml
    <ClCompile Include="HTTEngine\Input\Action\InputActionMap.cpp">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClCompile>
    <ClCompile Include="HTTEngine\Input\Action\InputBinding.cpp">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClCompile>
    <ClCompile Include="HTTEngine\Input\Action\InputRebinder.cpp">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Input\Action\InputActionMap.h">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Input\Action\InputBinding.h">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Input\Action\InputRebinder.h">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClInclude>
```

Input/ のファイルは 9 個あり、6 個足すと 15 を超えるので、`Input/Action/` に分けた（rules の「1つのフォルダのファイルが 15 を超えたら、フォルダ分けを考える」）。include のフォルダは `HTTEngine.props` が下のフォルダを全部足すので、何もしなくてよい。

ここで一度 Debug・Development・Release をビルドする（まだ誰も使わないので、動きは変わらない）。

# 段階2 エディタの Keymap

## パートF Settings ウィンドウに、GlobalVariables 以外のページを足せるようにする

### F-1 `Editor/Windows/SettingsWindow.h`（全体を差し替え）

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

// 前方宣言（GlobalVariables.h。json まで読まないように）
enum class SettingsSection;

/// <summary>
/// GlobalVariables のグループではない、Settings のページ1つ（Keymap など。中身と保存は持ち主が受け持つ）
/// </summary>
struct SettingsPage {
	std::string name;              // 一覧に出す名前（GlobalVariables のグループの名前とかぶらないように）
	SettingsSection section;       // 一覧のどの見出しに出すか
	std::function<void()> draw;    // 中身を描く（名前・Revert・Save の見出しは Settings ウィンドウが描く）
	std::function<bool()> isDirty; // 保存していない変更があるか（* を付ける・Revert と Save を押せるようにする）
	std::function<void()> revert;  // 保存した状態に戻す
	std::function<void()> save;    // 保存する
	std::string saveLocation;      // 保存先（見出しの下に出す）
	std::string revertTooltip;     // Revert の説明
};

/// <summary>
/// Settings ウィンドウ（ゲーム全体・シーン・エディタの設定を1か所に。Unity の Project Settings と同じ形）
/// <para>左に一覧（Project／Scene／Editor の見出し）、右に選んだページ。1ページ＝ GlobalVariables のグループ1つか、AddPage で足したページ1つ</para>
/// <para>保存していない変更があるページには * が付く。Save でファイルに書き、Revert で保存した値に戻す</para>
/// </summary>
class SettingsWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();
	// ウィンドウを前に出す（Edit → Settings）
	static void Focus() { focusRequest_ = true; }
	// GlobalVariables のグループではないページを足す（起動のときに1回。同じ見出しの、GlobalVariables のグループの後に出る）
	static void AddPage(SettingsPage page);

private:
	// 見出しのボタンで押された物
	enum class HeaderAction {
		None,
		Revert,
		Save,
	};

	// 左の一覧
	static void DrawPageList();
	// 一覧の1行（押すとそのページを選ぶ）
	static void DrawPageItem(const std::string& name, bool isDirty);
	// 右のページ
	static void DrawPage();
	// ページの見出し（名前（* 付き）と、右端に Revert・Save。その下に保存先）
	static HeaderAction DrawPageHeader(const std::string& name, bool isDirty, const std::string& saveLocation, const char* revertTooltip);
	// AddPage で足したページ（無ければ nullptr）
	static const SettingsPage* FindPage(const std::string& name);
	// そのページ（グループか、足したページ）があるか
	static bool HasPage(const std::string& name);

	static std::string selected_;            // 選んでいるページ（グループの名前）
	static bool focusRequest_;               // 次のフレームでウィンドウを前に出す
	static std::vector<SettingsPage> pages_; // AddPage で足したページ
};
```

- `enum class SettingsSection;` は前方宣言（GlobalVariables.h を読むと json まで読むので）。enum class は型を書かないと int なので、中身を書かずに宣言だけできる
- 見出し（名前・Revert・Save・保存先）は GlobalVariables のグループと同じ物を出したいので、`DrawPageHeader` にまとめ、ページの持ち主は中身だけを描く

### F-2 `Editor/Windows/SettingsWindow.cpp`

**1. 静的メンバ変数と、名前の無い namespace を差し替える**（`HasPage` はページも見るので、クラスの関数にした）：

```cpp
// 静的メンバ変数
std::string SettingsWindow::selected_;
bool SettingsWindow::focusRequest_ = false;
std::vector<SettingsPage> SettingsWindow::pages_;

namespace {
constexpr float kListWidth = 170.0f; // 左の一覧の最初の幅（境目をドラッグで変えられる）
} // namespace

//=============================================================================
// ページ
//=============================================================================
// ===== 足す =====
void SettingsWindow::AddPage(SettingsPage page) {
	pages_.push_back(std::move(page));
}

// ===== 足したページを名前で =====
const SettingsPage* SettingsWindow::FindPage(const std::string& name) {
	for (const SettingsPage& page : pages_) {
		if (page.name == name) {
			return &page;
		}
	}
	return nullptr;
}

// ===== そのページ（グループか、足したページ）があるか =====
bool SettingsWindow::HasPage(const std::string& name) {
	if (FindPage(name)) {
		return true;
	}
	GlobalVariables* gv = GlobalVariables::GetInstance();
	for (const SettingsSection section : {SettingsSection::Project, SettingsSection::Editor}) {
		const std::vector<std::string> names = gv->GetGroupNames(section);
		if (std::find(names.begin(), names.end(), name) != names.end()) {
			return true;
		}
	}
	return false;
}
```

**2. `DrawPageList` の Project と Editor の並びに、足したページを足す**（`DrawPageItem` に「保存していない変更があるか」を渡す形にした）：

```cpp
	ImGui::SeparatorText("Project");
	for (const std::string& name : gv->GetGroupNames(SettingsSection::Project)) {
		DrawPageItem(name, gv->IsGroupDirty(name));
	}
	for (const SettingsPage& page : pages_) {
		if (page.section == SettingsSection::Project) {
			DrawPageItem(page.name, page.isDirty());
		}
	}
```

```cpp
	ImGui::SeparatorText("Editor");
	for (const std::string& name : gv->GetGroupNames(SettingsSection::Editor)) {
		DrawPageItem(name, gv->IsGroupDirty(name));
	}
	for (const SettingsPage& page : pages_) {
		if (page.section == SettingsSection::Editor) {
			DrawPageItem(page.name, page.isDirty());
		}
	}
}
```

**3. `DrawPageItem` から最後までを差し替える**：

```cpp
void SettingsWindow::DrawPageItem(const std::string& name, bool isDirty) {
	// * ＝保存していない変更がある。「###」の後ろを ID にする（* が付いても別の項目にならないように）
	const std::string label = std::format("{}{}###{}", name, isDirty ? " *" : "", name);
	if (ImGui::Selectable(label.c_str(), selected_ == name)) {
		selected_ = name;
	}
}

//=============================================================================
// 右のページ
//=============================================================================
// ===== ページ =====
void SettingsWindow::DrawPage() {
	if (selected_.empty()) {
		ImGui::TextDisabled("設定がまだ登録されていません");
		return;
	}

	// --- AddPage で足したページ：見出しだけここで描き、中身と保存は持ち主に任せる ---
	if (const SettingsPage* page = FindPage(selected_)) {
		switch (DrawPageHeader(page->name, page->isDirty(), page->saveLocation, page->revertTooltip.c_str())) {
		case HeaderAction::Revert:
			page->revert();
			break;
		case HeaderAction::Save:
			page->save();
			break;
		case HeaderAction::None:
			break;
		}
		page->draw();
		return;
	}

	// --- GlobalVariables のグループ ---
	GlobalVariables* gv = GlobalVariables::GetInstance();
	switch (DrawPageHeader(selected_, gv->IsGroupDirty(selected_), gv->GetSaveDirectory(selected_), "保存した値に戻します（Ctrl+Z で、戻す前にもどせます）")) {
	case HeaderAction::Revert:
		gv->RevertGroup(selected_); // 保存した値に戻す
		break;
	case HeaderAction::Save:
		gv->SaveGroup(selected_);
		break;
	case HeaderAction::None:
		break;
	}
	// 中身（カテゴリと項目）
	gv->DrawGroup(selected_);
}

// ===== 見出し：名前（* 付き）と、右端に Revert・Save（変更が無ければ押せない）=====
SettingsWindow::HeaderAction SettingsWindow::DrawPageHeader(const std::string& name, bool isDirty, const std::string& saveLocation, const char* revertTooltip) {
	HeaderAction action = HeaderAction::None;
	const ImGuiStyle& style = ImGui::GetStyle();
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端
	const float buttonsWidth = ImGui::CalcTextSize("Revert").x + ImGui::CalcTextSize("Save").x + style.FramePadding.x * 4.0f + style.ItemSpacing.x;
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s%s", name.c_str(), isDirty ? " *" : "");
	ImGui::SameLine(right - buttonsWidth);
	ImGui::BeginDisabled(!isDirty);
	if (ImGui::Button("Revert")) {
		action = HeaderAction::Revert;
	}
	ImGui::SetItemTooltip("%s", revertTooltip);
	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		action = HeaderAction::Save;
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled("保存先: %s", saveLocation.c_str());
	ImGui::Separator();
	return action;
}
#endif
```

## パートG InputActionTable（新しい）

### G-1 `Editor/Widgets/InputActionTable.h`（新しい）

```cpp
#pragma once
#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/Input/Action/InputRebinder.h"

/// <summary>
/// Input Action の表の割り当てを直す ImGui の表（Settings の Keymap ページなど。表1つに1つ持つ）
/// <para>Action を group ごとに見出しを付けて並べ、割り当てをボタンで出す。ボタンを押すと、次に押したキー・ボタンを拾って入れ替える</para>
/// <para>ぶつかっている割り当ては赤くし、相手の名前と「相手の方を外す」ボタンを出す</para>
/// </summary>
class InputActionTable {
public:
	// 表を描く（ページの中から毎フレーム。拾っている間は、案内のモーダルも出す）
	void Draw(InputActionMap& map);
	// 拾う係を動かし、拾えたら map に入れる（毎フレーム1回、フレームの頭に。表の Update の前）
	// <para>入力を読めるのはフレームの頭（WindowManager::UpdateAll の中）だけなので、Draw の中では動かさない</para>
	// <para>前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、拾うのをやめる</para>
	void UpdateListening(InputActionMap& map);
	// 割り当てを拾っているか（その間、表の SetEnabled(false) にしておく）
	bool IsListening() const { return rebinder_.IsListening(); }

private:
	// Action 1行（名前・割り当て・既定に戻す）
	void DrawAction(InputActionMap& map, InputActionId id);
	// 割り当て1つのボタン（押すと拾い始める。右の × で外す）。外したら true（後ろが詰まるので、その行の残りを描かない）
	bool DrawBinding(InputActionMap& map, InputActionId id, int index);
	// ぶつかっている相手（行の下に出す）
	void DrawConflicts(InputActionMap& map, InputActionId id, int index);
	// 拾っている間の案内（モーダル。ほかを触れないようにして、押したクリックが別の所に効かないように）
	void DrawListening(const InputActionMap& map);

	InputRebinder rebinder_;
	int lastDrawFrame_ = -1; // 最後に表を描いたフレーム（ImGui::GetFrameCount）
};
```

### G-2 `Editor/Widgets/InputActionTable.cpp`（新しい）

```cpp
#ifdef USE_IMGUI
#include "InputActionTable.h"

#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>

namespace {
constexpr float kLabelWidth = 200.0f;                               // Action の名前の列の幅
constexpr ImVec4 kConflictButtonColor = {0.62f, 0.20f, 0.20f, 1.0f}; // ぶつかっている割り当てのボタン
constexpr ImVec4 kConflictTextColor = {1.0f, 0.50f, 0.45f, 1.0f};    // ぶつかりの説明の文字
constexpr const char* kListeningPopupId = "割り当てを拾う";          // 拾っている間のモーダルの名前（ID にもなる）
constexpr float kSmallButtonScale = 0.75f;                          // × と ＋ の文字の大きさ（ふつうの文字の何倍か）
constexpr float kRemoveButtonSpacing = 4.0f;                        // 割り当てのボタンと × の間（ピクセル）

// 小さいボタン（× と ＋）。文字を小さくし、同じ行の割り当てのボタンと上下の真ん中をそろえる
bool SmallRowButton(const char* label) {
	const float rowHeight = ImGui::GetFrameHeight(); // 割り当てのボタン（ふつうの文字）の高さ
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kSmallButtonScale);
	// 小さいボタンの高さ（小さい文字の高さ）との差の半分だけ下げる
	// SmallButton は、同じ行の文字の高さに合わせて自分でも FramePadding.y だけ下がるので、その分は引く
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f - ImGui::GetStyle().FramePadding.y);
	const bool isPressed = ImGui::SmallButton(label);
	ImGui::PopFont();
	return isPressed;
}

// 割り当ての一覧を "A / B" の文字にする（既定に戻すボタンの説明）
std::string JoinBindings(const std::vector<InputBinding>& bindings) {
	std::string text;
	for (const InputBinding& binding : bindings) {
		text += text.empty() ? "" : " / ";
		text += InputBindingText::ToDisplayString(binding);
	}
	return text.empty() ? "（なし）" : text;
}
} // namespace


//=============================================================================
// 表
//=============================================================================
// ===== 全部 =====
void InputActionTable::Draw(InputActionMap& map) {
	lastDrawFrame_ = ImGui::GetFrameCount();

	// --- group ごとに見出しを付け、表を分ける（同じ group が続く間は、同じ表）---
	std::string currentGroup;
	bool isTableOpen = false;
	for (uint32_t i = 0; i < map.GetActionCount(); ++i) {
		const InputActionId id = {i};
		const InputActionDesc& desc = map.GetDesc(id);
		if (i == 0 || desc.group != currentGroup) {
			if (isTableOpen) {
				ImGui::EndTable();
			}
			currentGroup = desc.group;
			ImGui::SeparatorText(currentGroup.c_str());
			isTableOpen = ImGui::BeginTable(std::format("##group{}", i).c_str(), 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
			if (isTableOpen) {
				ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
				ImGui::TableSetupColumn("bindings", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed);
			}
		}
		if (isTableOpen) {
			ImGui::PushID(static_cast<int>(i));
			DrawAction(map, id);
			ImGui::PopID();
		}
	}
	if (isTableOpen) {
		ImGui::EndTable();
	}

	DrawListening(map);
}

// ===== 拾う係を動かす（フレームの頭）=====
void InputActionTable::UpdateListening(InputActionMap& map) {
	if (!rebinder_.IsListening()) {
		return;
	}
	// --- 前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、やめる ---
	// フレームの頭（ImGui::NewFrame の前）では、GetFrameCount は前のフレームの番号。前のフレームに描いていれば同じ
	if (lastDrawFrame_ < ImGui::GetFrameCount()) {
		rebinder_.Cancel();
		return;
	}
	// --- 拾えたら、表に入れる ---
	const InputActionId id = rebinder_.GetAction();
	const int index = rebinder_.GetIndex();
	if (rebinder_.Update() == InputRebinder::Result::Captured) {
		map.SetBinding(id, index, *rebinder_.GetCaptured());
	}
}


//=============================================================================
// Action 1行
//=============================================================================
void InputActionTable::DrawAction(InputActionMap& map, InputActionId id) {
	const InputActionDesc& desc = map.GetDesc(id);
	ImGui::TableNextRow();

	// --- 名前 ---
	ImGui::TableNextColumn();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(desc.label.c_str());
	if (!desc.tooltip.empty()) {
		ImGui::SetItemTooltip("%s", desc.tooltip.c_str());
	}

	// --- 割り当て（ボタンを横に並べる）と、足すボタン ---
	ImGui::TableNextColumn();
	const int count = static_cast<int>(map.GetBindings(id).size());
	for (int index = 0; index < count; ++index) {
		if (DrawBinding(map, id, index)) {
			break; // 外したので、後ろが詰まった（次のフレームで描き直す）
		}
		ImGui::SameLine();
	}
	if (map.GetBindings(id).empty()) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("（なし）");
		ImGui::SameLine();
	}
	if (desc.isFixed) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("変えられません");
	} else {
		if (SmallRowButton("+")) {
			rebinder_.Start(map, id, static_cast<int>(map.GetBindings(id).size()));
		}
		ImGui::SetItemTooltip("割り当てを足す（押してから、足したいキー・ボタンを押す）");
	}
	// ぶつかりは、割り当ての並びの下に1行ずつ
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		DrawConflicts(map, id, index);
	}

	// --- 既定に戻す ---
	ImGui::TableNextColumn();
	if (!desc.isFixed) {
		ImGui::BeginDisabled(map.IsDefault(id));
		if (ImGui::Button("既定")) {
			map.ResetToDefault(id);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("既定の割り当てに戻す（%s）", JoinBindings(desc.defaults).c_str());
		}
	}
}

// ===== 割り当て1つのボタン =====
bool InputActionTable::DrawBinding(InputActionMap& map, InputActionId id, int index) {
	const InputActionDesc& desc = map.GetDesc(id);
	const InputBinding binding = map.GetBindings(id)[index]; // 写しておく（外すと、元の物は消える）
	const bool isListeningHere = rebinder_.IsListening() && rebinder_.GetAction() == id && rebinder_.GetIndex() == index;
	const bool hasConflict = !map.FindConflicts(id, index).empty();

	ImGui::PushID(index);
	// --- ボタン（押すと拾い始める。ぶつかっていれば赤）---
	std::string text = isListeningHere ? std::string("押してください…") : InputBindingText::ToDisplayString(binding);
	if (text.empty()) {
		text = "?"; // 名前の無い入力（ふつうは無い）
	}
	if (hasConflict) {
		ImGui::PushStyleColor(ImGuiCol_Button, kConflictButtonColor);
	}
	ImGui::BeginDisabled(desc.isFixed);
	if (ImGui::Button(text.c_str())) {
		rebinder_.Start(map, id, index);
	}
	ImGui::EndDisabled();
	if (hasConflict) {
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", desc.isFixed ? "ほかの所でも、この入力を決め打ちで使っているので変えられません" : "押してから、新しいキー・ボタンを押すと入れ替わります");
	}

	// --- 外す ---
	bool isRemoved = false;
	if (!desc.isFixed) {
		ImGui::SameLine(0.0f, kRemoveButtonSpacing);
		if (SmallRowButton("×")) {
			map.RemoveBinding(id, index);
			isRemoved = true;
		}
		ImGui::SetItemTooltip("この割り当てを外す");
	}
	ImGui::PopID();
	return isRemoved;
}

// ===== ぶつかっている相手 =====
void InputActionTable::DrawConflicts(InputActionMap& map, InputActionId id, int index) {
	const std::vector<InputBindingRef> conflicts = map.FindConflicts(id, index);
	const std::string bindingText = InputBindingText::ToDisplayString(map.GetBindings(id)[index]);
	for (const InputBindingRef& other : conflicts) {
		const InputActionDesc& otherDesc = map.GetDesc(other.action);
		ImGui::PushID(index);
		ImGui::PushID(static_cast<int>(other.action.index));
		ImGui::PushID(other.index);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(kConflictTextColor, "! %s は「%s」とぶつかっています", bindingText.c_str(), otherDesc.label.c_str());
		bool isRemoved = false;
		if (!otherDesc.isFixed) {
			ImGui::SameLine();
			if (ImGui::SmallButton(std::format("「{}」の方を外す", otherDesc.label).c_str())) {
				map.RemoveBinding(other.action, other.index);
				isRemoved = true;
			}
		}
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();
		if (isRemoved) {
			break; // 相手の並びが詰まったので、残りの一覧は古い（次のフレームで描き直す）
		}
	}
}


//=============================================================================
// 拾っている間
//=============================================================================
void InputActionTable::DrawListening(const InputActionMap& map) {
	// 拾う係はここでは動かさない（UpdateListening）。ImGui を描く間は、InputManager がウィンドウを手前でない扱いにしていて、何も読めない
	// --- 案内のモーダル（拾い終えたら閉じる）---
	if (rebinder_.IsListening() && !ImGui::IsPopupOpen(kListeningPopupId)) {
		ImGui::OpenPopup(kListeningPopupId);
	}
	if (ImGui::BeginPopupModal(kListeningPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!rebinder_.IsListening()) {
			ImGui::CloseCurrentPopup();
		} else {
			ImGui::Text("「%s」に割り当てるキー・ボタンを押してください", map.GetDesc(rebinder_.GetAction()).label.c_str());
			ImGui::TextDisabled("Ctrl・Shift・Alt を押しながら押すと、組にして割り当てます（Ctrl+Z など）");
			ImGui::TextDisabled("Esc でやめる");
		}
		ImGui::EndPopup();
	}
}
#endif
```

- 割り当てのボタンの ID は `PushID(Action の番号)` → `PushID(割り当ての番号)` で分けている（同じ `Mouse Right` の文字のボタンが何個あってもぶつからない）
- 割り当てを外した（×・相手の方を外す）ら、その行の残りは描かない。並びが詰まって、手元の番号が古くなるため（次のフレームで描き直す）
- 吹き出しを押せないボタン（変えられない・既定のまま）にも出すため、`SetItemTooltip` ではなく `IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)` を使う所がある
- `UpdateListening` はフレームの頭（ImGui::NewFrame の前）に呼ぶので、`GetFrameCount` はまだ前のフレームの番号。前のフレームに描いていれば同じ数になる（描いていなければ、ページを閉じたのでやめる）
- 拾う係を `DrawListening`（描く所）で動かさないのは 0.8。描く所は、モーダルを出す・閉じるだけ
- × と ＋ は `SmallRowButton`（文字を 0.75 倍にした SmallButton）。SmallButton は同じ行の文字に合わせて自分でも `FramePadding.y` だけ下がるので、上下の真ん中にそろえるときはその分を引く

## パートH EditorKeymap（新しい）

### H-1 `Editor/Keymap/EditorKeymap.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

/// <summary>
/// エディタの操作（Settings → Editor → Keymap で、割り当てを人ごとに変えられる物）
/// <para>足すときは、EditorKeymap.cpp の MakeActionDescs にも同じ順番で足す</para>
/// </summary>
enum class EditorAction : uint8_t {
	// --- Scene ビューのカメラ（Scene ビューの上でだけ効く）---
	SceneRotate,     // 押している間、マウスの動きで向きを変える（その場で回る）
	ScenePan,        // 押している間、マウスの動きで平行に動く
	SceneDollyDrag,  // 押している間、マウスの上下で前後に動く
	SceneDollyWheel, // ホイールで前後に動く
	// --- Scene ビュー ---
	SceneSelect, // クリックで選ぶ（ギズモ・UI のドラッグも左クリックなので、変えられない）
	ToolMove,    // 移動のギズモにする
	ToolRotate,  // 回転のギズモにする
	ToolScale,   // 拡縮のギズモにする
	// --- 編集（どのウィンドウでも。何に効くかは、触っているウィンドウが決める）---
	Undo,
	Redo,
	Delete,    // 選んだ物を消す（Hierarchy・Project・Timeline・Curve・点の編集）
	Rename,    // 名前を変える（Hierarchy・Project）
	Copy,      // Hierarchy
	Paste,     // Hierarchy
	Duplicate, // Hierarchy
	Frame,     // 選んだ物・全体を画面に入れる（Scene ビュー・Timeline・Curve）
	Find,      // 検索欄へ（Code ウィンドウ）
	// --- シーンのファイル ---
	NewScene,
	SaveScene,
	SaveSceneAs,
};

/// <summary>
/// エディタの操作の割り当て（Unity の Shortcuts・Blender の Keymap と同じ役目）
/// <para>キー・ボタンを直接読まず、EditorAction で聞く。割り当ては Settings → Editor → Keymap で変え、Parameters/Editor/Keymap.json に保存する（git に入れないので、人ごとに違ってよい）</para>
/// <para>読むのは InputGate::Editor（ImGui のパネルを触っていても読める。文字を打っている間は読まない）。修飾キーはちょうど同じときだけ（Ctrl+Z は Ctrl+Shift+Z では効かない）</para>
/// <para>中身は Input Action の表（InputActionMap）。ゲームのキーコンフィグも同じ表で作る</para>
/// </summary>
class EditorKeymap {
public:
	// 操作を登録し、保存した割り当てを読み、Settings にページを足す（ImGuiManager::Initialize から1回）
	static void Initialize();
	// 入力を読む（毎フレーム1回。WindowManager::UpdateAll で、Scene ビューのカメラを動かす前）
	static void Update();

	// --- 聞く ---
	static bool IsPressed(EditorAction action);   // 押している間（ドラッグの操作）
	static bool IsTriggered(EditorAction action); // 押した瞬間（ショートカット）
	static float GetValue(EditorAction action);   // 量（ホイールの目盛り）

	// --- 画面に出す文字 ---
	// 割り当て（"Ctrl+Z"。割り当てが無ければ空）。メニューのショートカットの欄に
	static std::string GetShortcutText(EditorAction action);
	// "移動（W）" のように、説明の後ろに割り当てを付ける（割り当てが無ければ説明だけ）。ボタンの吹き出しに
	static std::string WithShortcut(std::string_view text, EditorAction action);

private:
	// Settings の Keymap ページの中身（プリセットと表）
	static void DrawPage();
};
```

### H-2 `Editor/Keymap/EditorKeymap.cpp`（新しい）

```cpp
#ifdef USE_IMGUI
#include "EditorKeymap.h"

#include <array>
#include <format>
#include <utility>
#include <vector>

#include <Windows.h>
#include <dinput.h>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Editor/Widgets/InputActionTable.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
constexpr const char* kSavePath = "Parameters/Editor/Keymap.json"; // 人ごとの割り当て（Parameters/Editor/ は git に入れない）
constexpr const char* kPageName = "Keymap";                        // Settings の一覧の名前

// --- 効く場所（ぶつかりを調べる範囲）---
constexpr const char* kContextSceneView = "SceneView"; // Scene ビューの上でだけ
constexpr const char* kContextAnywhere = "";            // どこでも（どの場所の割り当てともぶつかる）

// --- 割り当てられる入力 ---
// ドラッグの操作：押している間マウスを動かす物なので、マウスのボタンか、キー（押しながらマウスを動かす）
constexpr InputDeviceMask kDragDevices = InputDeviceMask::Key | InputDeviceMask::MouseButton;
// エディタで読む入力（パッド・マウスの動きは割り当てない）
constexpr InputDeviceMask kEditorDevices = InputDeviceMask::Key | InputDeviceMask::MouseButton | InputDeviceMask::MouseWheel;

//=============================================================================
// 操作の一覧（EditorAction と同じ順番）
//=============================================================================
struct ActionDef {
	EditorAction action; // 順番を確かめるため
	InputActionDesc desc;
};

std::vector<ActionDef> MakeActionDescs() {
	using Mod = InputModifiers;
	using Type = InputActionType;
	return {
	    // ===== Scene ビューのカメラ =====
	    {EditorAction::SceneRotate, {.name = "SceneView.Rotate", .label = "回転（ドラッグ）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                                 .tooltip = "押している間、マウスの動きでカメラの向きを変える（その場で回る）", .type = Type::Button, .accepts = kDragDevices,
	                                 .defaults = {InputBinding::Mouse(2)}}},
	    {EditorAction::ScenePan, {.name = "SceneView.Pan", .label = "平行移動（ドラッグ）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                              .tooltip = "押している間、マウスの動きでカメラを上下左右に動かす", .type = Type::Button, .accepts = kDragDevices,
	                              .defaults = {InputBinding::Mouse(1)}}},
	    {EditorAction::SceneDollyDrag, {.name = "SceneView.DollyDrag", .label = "前後に動く（ドラッグ）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                                    .tooltip = "押している間、マウスを上へ動かすと前へ、下へ動かすと後ろへ動く", .type = Type::Button, .accepts = kDragDevices,
	                                    .defaults = {}}},
	    {EditorAction::SceneDollyWheel, {.name = "SceneView.DollyWheel", .label = "前後に動く（ホイール）", .group = "Scene View のカメラ", .context = kContextSceneView,
	                                     .tooltip = "ホイールを奥へ回すと前へ、手前へ回すと後ろへ動く", .type = Type::Value, .accepts = InputDeviceMask::MouseWheel,
	                                     .defaults = {InputBinding::Wheel()}}},
	    // ===== Scene ビュー =====
	    {EditorAction::SceneSelect, {.name = "SceneView.Select", .label = "選ぶ（クリック）", .group = "Scene View", .context = kContextSceneView,
	                                 .tooltip = "クリックで選ぶ。ギズモ・UI のドラッグも左クリックなので変えられない（ほかの操作とぶつかっていないかを見るために出している）",
	                                 .type = Type::Button, .accepts = InputDeviceMask::MouseButton, .isFixed = true, .defaults = {InputBinding::Mouse(0)}}},
	    {EditorAction::ToolMove, {.name = "SceneView.ToolMove", .label = "移動のギズモ", .group = "Scene View", .context = kContextSceneView,
	                              .tooltip = "ギズモを移動にする", .defaults = {InputBinding::Key(DIK_W)}}},
	    {EditorAction::ToolRotate, {.name = "SceneView.ToolRotate", .label = "回転のギズモ", .group = "Scene View", .context = kContextSceneView,
	                                .tooltip = "ギズモを回転にする", .defaults = {InputBinding::Key(DIK_E)}}},
	    {EditorAction::ToolScale, {.name = "SceneView.ToolScale", .label = "拡縮のギズモ", .group = "Scene View", .context = kContextSceneView,
	                               .tooltip = "ギズモを拡縮にする", .defaults = {InputBinding::Key(DIK_R)}}},
	    // ===== 編集 =====
	    {EditorAction::Undo, {.name = "Edit.Undo", .label = "元に戻す", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "Undo（文字の入力中は、入力欄の文字を戻す）", .defaults = {InputBinding::Key(DIK_Z, Mod::Ctrl)}}},
	    {EditorAction::Redo, {.name = "Edit.Redo", .label = "やり直す", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "Redo", .defaults = {InputBinding::Key(DIK_Y, Mod::Ctrl), InputBinding::Key(DIK_Z, Mod::Ctrl | Mod::Shift)}}},
	    {EditorAction::Delete, {.name = "Edit.Delete", .label = "消す", .group = "編集", .context = kContextAnywhere,
	                            .tooltip = "選んだ物を消す（Hierarchy・Project・Timeline・Curve・点の編集）", .defaults = {InputBinding::Key(DIK_DELETE)}}},
	    {EditorAction::Rename, {.name = "Edit.Rename", .label = "名前を変える", .group = "編集", .context = kContextAnywhere,
	                            .tooltip = "選んだ物の名前を変える（Hierarchy・Project）", .defaults = {InputBinding::Key(DIK_F2)}}},
	    {EditorAction::Copy, {.name = "Edit.Copy", .label = "コピー", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "選んだ Entity をコピーする（Hierarchy）", .defaults = {InputBinding::Key(DIK_C, Mod::Ctrl)}}},
	    {EditorAction::Paste, {.name = "Edit.Paste", .label = "貼り付け", .group = "編集", .context = kContextAnywhere,
	                           .tooltip = "コピーした Entity を貼る（Hierarchy）", .defaults = {InputBinding::Key(DIK_V, Mod::Ctrl)}}},
	    {EditorAction::Duplicate, {.name = "Edit.Duplicate", .label = "複製", .group = "編集", .context = kContextAnywhere,
	                               .tooltip = "選んだ Entity を複製する（Hierarchy）", .defaults = {InputBinding::Key(DIK_D, Mod::Ctrl)}}},
	    {EditorAction::Frame, {.name = "Edit.Frame", .label = "画面に入れる", .group = "編集", .context = kContextAnywhere,
	                           .tooltip = "選んだ物へ寄る（Scene ビュー）・全体が見える範囲にする（Timeline・Curve）", .defaults = {InputBinding::Key(DIK_F)}}},
	    {EditorAction::Find, {.name = "Edit.Find", .label = "検索", .group = "編集", .context = kContextAnywhere,
	                          .tooltip = "検索欄へ（Code ウィンドウ）", .defaults = {InputBinding::Key(DIK_F, Mod::Ctrl)}}},
	    // ===== シーンのファイル =====
	    {EditorAction::NewScene, {.name = "File.NewScene", .label = "新しいシーン", .group = "シーンのファイル", .context = kContextAnywhere,
	                              .tooltip = "新しいシーンを作る", .defaults = {InputBinding::Key(DIK_N, Mod::Ctrl)}}},
	    {EditorAction::SaveScene, {.name = "File.SaveScene", .label = "保存", .group = "シーンのファイル", .context = kContextAnywhere,
	                               .tooltip = "開いているシーンを保存する（停止中だけ）", .defaults = {InputBinding::Key(DIK_S, Mod::Ctrl)}}},
	    {EditorAction::SaveSceneAs, {.name = "File.SaveSceneAs", .label = "名前を付けて保存", .group = "シーンのファイル", .context = kContextAnywhere,
	                                 .tooltip = "開いているシーンを、別の名前で保存する", .defaults = {InputBinding::Key(DIK_S, Mod::Ctrl | Mod::Shift)}}},
	};
}

//=============================================================================
// カメラの操作のプリセット（ほかのツールに慣れている人向け。Scene ビューのカメラの3つだけを入れ替える）
//=============================================================================
struct CameraPreset {
	const char* name;
	const char* tooltip;
	InputBinding rotate;    // 回転（ドラッグ）
	InputBinding pan;       // 平行移動（ドラッグ）
	InputBinding dollyDrag; // 前後に動く（ドラッグ）。空なら割り当てなし
};
constexpr auto kCameraPresets = std::to_array<CameraPreset>({
    {"HTTEngine", "ホイールボタンで回転、右ドラッグで平行移動（既定）", InputBinding::Mouse(2), InputBinding::Mouse(1), {}},
    {"Unity 風", "右ドラッグで回転、ホイールボタンで平行移動、Alt+右ドラッグで前後", InputBinding::Mouse(1), InputBinding::Mouse(2), InputBinding::Mouse(1, InputModifiers::Alt)},
    {"Blender 風", "ホイールボタンで回転、Shift+ホイールボタンで平行移動、Ctrl+ホイールボタンで前後（Blender と違い、注視点のまわりではなく、その場で回る）",
     InputBinding::Mouse(2), InputBinding::Mouse(2, InputModifiers::Shift), InputBinding::Mouse(2, InputModifiers::Ctrl)},
});

//=============================================================================
// 持ち物
//=============================================================================
// 割り当ての表（エディタの決まりで読む・修飾キーはちょうど同じときだけ）
InputActionMap& Map() {
	static InputActionMap map(InputGate::Editor, InputModifierMatch::Exact, kEditorDevices);
	return map;
}
// Keymap ページの表
InputActionTable& Table() {
	static InputActionTable table;
	return table;
}
// EditorAction → 表の番号（足した順番が EditorAction の順番）
InputActionId ToId(EditorAction action) { return {static_cast<uint32_t>(action)}; }
// 割り当て1つ → 割り当ての一覧（空なら割り当てなし）
std::vector<InputBinding> ToList(const InputBinding& binding) { return binding.IsEmpty() ? std::vector<InputBinding>{} : std::vector<InputBinding>{binding}; }
} // namespace


//=============================================================================
// 初期化・毎フレーム
//=============================================================================
// ===== 初期化 =====
void EditorKeymap::Initialize() {
	InputActionMap& map = Map();
	for (ActionDef& def : MakeActionDescs()) {
		const InputActionId id = map.AddAction(std::move(def.desc));
		HTT_ASSERT_MSG(id == ToId(def.action), "MakeActionDescs の順番が EditorAction と違います");
	}
	HTT_ASSERT_MSG(map.GetActionCount() == magic_enum::enum_count<EditorAction>(), "MakeActionDescs に足りない EditorAction があります");
	map.LoadOverrides(kSavePath);

	// --- Settings → Editor → Keymap ---
	SettingsWindow::AddPage({
	    .name = kPageName,
	    .section = SettingsSection::Editor,
	    .draw = &EditorKeymap::DrawPage,
	    .isDirty = [] { return Map().IsDirty(); },
	    .revert = [] { Map().RevertToSaved(); },
	    .save = [] { Map().SaveOverrides(kSavePath); },
	    .saveLocation = kSavePath,
	    .revertTooltip = "保存した割り当てに戻します",
	});
}

// ===== 毎フレーム =====
void EditorKeymap::Update() {
	// 割り当てを拾う（入力を読めるのはここだけ。Keymap ページを閉じていたら、拾うのをやめる）
	const bool wasListening = Table().IsListening();
	Table().UpdateListening(Map());
	// 拾っている間（拾えたフレームも）は、どの操作も効かせない（Ctrl+Z を拾うときに Undo が動かないように）
	Map().SetEnabled(!wasListening);
	Map().Update();
}


//=============================================================================
// 聞く
//=============================================================================
bool EditorKeymap::IsPressed(EditorAction action) { return Map().IsPressed(ToId(action)); }
bool EditorKeymap::IsTriggered(EditorAction action) { return Map().IsTriggered(ToId(action)); }
float EditorKeymap::GetValue(EditorAction action) { return Map().GetValue(ToId(action)); }

// ===== 画面に出す文字 =====
std::string EditorKeymap::GetShortcutText(EditorAction action) { return Map().GetDisplayText(ToId(action)); }

std::string EditorKeymap::WithShortcut(std::string_view text, EditorAction action) {
	const std::string shortcut = GetShortcutText(action);
	if (shortcut.empty()) {
		return std::string(text);
	}
	return std::format("{}（{}）", text, shortcut);
}


//=============================================================================
// Settings の Keymap ページ
//=============================================================================
void EditorKeymap::DrawPage() {
	InputActionMap& map = Map();

	// --- カメラの操作の型 ---
	ImGui::SeparatorText("カメラの操作の型");
	for (size_t i = 0; i < kCameraPresets.size(); ++i) {
		const CameraPreset& preset = kCameraPresets[i];
		if (i > 0) {
			ImGui::SameLine();
		}
		if (ImGui::Button(preset.name)) {
			map.SetBindings(ToId(EditorAction::SceneRotate), ToList(preset.rotate));
			map.SetBindings(ToId(EditorAction::ScenePan), ToList(preset.pan));
			map.SetBindings(ToId(EditorAction::SceneDollyDrag), ToList(preset.dollyDrag));
		}
		ImGui::SetItemTooltip("%s", preset.tooltip);
	}
	ImGui::TextDisabled("Scene View のカメラの回転・平行移動・前後（ドラッグ）だけを入れ替えます。残すなら Save");
	if (ImGui::Button("全部を既定に戻す")) {
		map.ResetAllToDefault();
	}
	ImGui::SetItemTooltip("全部の操作を、エンジンの既定の割り当てに戻す（残すなら Save）");

	// --- 全部の操作 ---
	Table().Draw(map);
}
#endif
```

- `EditorAction` の順番と `MakeActionDescs` の順番が同じなので、表の番号は `static_cast<uint32_t>(action)`（`ToId`）。ずれたら Initialize の assert で分かる。数が足りないのも `magic_enum::enum_count` で分かる
- 表は `Map()` の中の static（初めて呼ばれたときに作る）。`static InputActionMap map_;` をクラスに持つと、ヘッダーに InputActionMap.h（と Windows.h・dinput.h）を読ませることになり、EditorKeymap.h を読む全部のファイルが重くなる
- 割り当てを拾っている間は `SetEnabled(false)`（0.8）。拾う係は `Table().UpdateListening(Map())` でここ（フレームの頭）で動かす。Keymap のページを閉じていたら、そこで拾うのをやめる（やめないと、ずっとショートカットが効かない）

### H-3 vcxproj

`HTTEngine.vcxproj` の `ClCompile` の並び（`Editor\Widgets\ImeInput.cpp` の後）：

```xml
    <ClCompile Include="HTTEngine\Editor\Widgets\InputActionTable.cpp" />
    <ClCompile Include="HTTEngine\Editor\Keymap\EditorKeymap.cpp" />
```

`ClInclude` の並び（`Editor\Widgets\ImeInput.h` の後）：

```xml
    <ClInclude Include="HTTEngine\Editor\Widgets\InputActionTable.h" />
    <ClInclude Include="HTTEngine\Editor\Keymap\EditorKeymap.h" />
```

`HTTEngine.vcxproj.filters` は、フィルタを1つ（`HTTEngine\Editor\History` の後）と、4つのファイルを足す。

```xml
    <Filter Include="HTTEngine\Editor\Keymap">
      <UniqueIdentifier>{4e8b2d17-9a3c-4f6e-b1d5-7c2a9e4f8b36}</UniqueIdentifier>
    </Filter>
```

```xml
    <ClCompile Include="HTTEngine\Editor\Widgets\InputActionTable.cpp">
      <Filter>HTTEngine\Editor\Widgets</Filter>
    </ClCompile>
    <ClCompile Include="HTTEngine\Editor\Keymap\EditorKeymap.cpp">
      <Filter>HTTEngine\Editor\Keymap</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Editor\Widgets\InputActionTable.h">
      <Filter>HTTEngine\Editor\Widgets</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Editor\Keymap\EditorKeymap.h">
      <Filter>HTTEngine\Editor\Keymap</Filter>
    </ClInclude>
```

InputActionTable は「〜Editor」で終わる名前にしない（`Docs/ARCHITECTURE.md` のフォルダの決まりで、〜Editor は Inspector の部品）。

## パートI Scene ビューのカメラ

### I-1 `Camera/DebugCamera.h`

**1. include に Vector2 を足す**：

```cpp
#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"
```

**2. 速度係数から `Matrix4x4 CalcWVP` までを差し替える**（`Rotate()`・`Zoom()`・`Translate()` は private の、量を受け取る物に変わる。`distanceMin` は使っていなかったので消した）：

```cpp
	// 速度係数
	float rotateSpeed = 0.001f;    // 向きを変える速さ（マウスが数えた数 1 あたりのラジアン）
	float panSpeed = 0.025f;       // 平行移動の速さ（マウスが数えた数 1 あたりの距離）
	float dollyDragSpeed = 0.05f;  // ドラッグで前後に動く速さ（マウスが数えた数 1 あたりの距離）
	float dollyWheelSpeed = 1.2f;  // ホイールで前後に動く速さ（1 目盛りあたりの距離）
	// 手振れの補正
	float mouseDeltaThreshold = 2.0f; // 向きを変えるとき、この値より小さい動きは無視

	/// <summary>
	/// 1フレームの操作の量（EditorViewport が、EditorKeymap の割り当てとマウスの動きから作る）
	/// </summary>
	struct Input {
		Vector2 rotate = {0.0f, 0.0f}; // 向きを変える量（マウスが数えた数。右・下が+）
		Vector2 pan = {0.0f, 0.0f};    // 平行移動の量（マウスが数えた数。右・下が+）
		float dollyDrag = 0.0f;        // ドラッグで前後に動く量（マウスが数えた数。前が+）
		float dollyWheel = 0.0f;       // ホイールで前後に動く量（目盛り。前が+）
	};

public:

	void Initialize(float fovY, float aspectRatio, float nearZ, float farZ) override;
	void Update() override;
	// 操作の量で動かして、ビュー行列を作り直す（どのキー・ボタンで動かすかは、呼ぶ側が決める）
	void ApplyInput(const Input& input);
	Matrix4x4 CalcWVP(const Matrix4x4& worldMatrix) const override;
```

**3. クラスの最後（`GetTranslation` の後）に private を足す**：

```cpp
	const Vector3& GetTranslation() const { return transform_.translation; }

private:
	void Rotate(Vector2 amount);       // 向きを変える
	void Pan(const Vector2& amount);   // 上下左右に動く
	void Dolly(float distance);        // 向いている方へ前後に動く
};
```

### I-2 `Camera/DebugCamera.cpp`

`#include "HTTEngine/Input/InputManager.h"` を消し（もう入力を読まない）、`Update` から `Translate` の終わりまでを差し替える：

```cpp
void DebugCamera::Update() { ReCalcViewMatrix(); }

//=============================================================
// 操作の量で動かす
//=============================================================
void DebugCamera::ApplyInput(const Input& input) {
	Rotate(input.rotate);
	Dolly(input.dollyDrag * dollyDragSpeed + input.dollyWheel * dollyWheelSpeed);
	Pan(input.pan);
	ReCalcViewMatrix();
}

// ===== 向きを変える（その場で回る）=====
void DebugCamera::Rotate(Vector2 amount) {
	// 手振れ：小さい動きは無視する
	if (std::abs(amount.x) < mouseDeltaThreshold) {
		amount.x = 0.0f;
	}
	if (std::abs(amount.y) < mouseDeltaThreshold) {
		amount.y = 0.0f;
	}
	transform_.rotation.x -= amount.y * rotateSpeed;
	transform_.rotation.y -= amount.x * rotateSpeed;
	transform_.rotation.x = std::clamp(transform_.rotation.x, pitchMin, pitchMax);
}

// ===== 向いている方へ前後に動く =====
void DebugCamera::Dolly(float distance) {
	if (distance == 0.0f) {
		return;
	}
	// カメラが向いている前方向を求める
	const float cosPitch = std::cos(transform_.rotation.x);
	const float sinPitch = std::sin(transform_.rotation.x);
	const float cosYaw = std::cos(transform_.rotation.y);
	const float sinYaw = std::sin(transform_.rotation.y);
	// 前方向ベクトル（左手座標系）
	const Vector3 forward = {cosPitch * sinYaw, -sinPitch, cosPitch * cosYaw};
	transform_.translation.x += forward.x * distance;
	transform_.translation.y += forward.y * distance;
	transform_.translation.z += forward.z * distance;
}

// ===== 上下左右に動く =====
void DebugCamera::Pan(const Vector2& amount) {
	if (amount.x == 0.0f && amount.y == 0.0f) {
		return;
	}
	const float cosYaw = std::cos(transform_.rotation.y);
	const float sinYaw = std::sin(transform_.rotation.y);
	// カメラの右方向
	const Vector3 right = {cosYaw, 0.0f, -sinYaw};
	transform_.translation.x -= right.x * amount.x * panSpeed;
	transform_.translation.z -= right.z * amount.x * panSpeed;
	transform_.translation.y += amount.y * panSpeed;
}
```

- 前の `Zoom` は、ホイールの生の値（1 目盛りで 120）に `zoomSpeed`（0.01）を掛けていた＝1 目盛りで 1.2。今は表が目盛りに直して渡すので `dollyWheelSpeed = 1.2`。動く量は同じ
- 前の `Zoom` の「2 より小さいホイールは無視」は、生の値が 120 ずつなので効いていなかった。消した
- 回す・平行に動く・前後の順番は前と同じ（回してから、新しい向きで前後に動く）

### I-3 `Editor/Viewport/EditorViewport.h`

private の頭に足す：

```cpp
private:
#ifdef USE_IMGUI
	// Scene ビューのカメラの操作の量（EditorKeymap の割り当てとマウスの動きから）
	static DebugCamera::Input ReadCameraInput();
#endif
```

### I-4 `Editor/Viewport/EditorViewport.cpp`

**1. include**（`Camera.h` の次）：

```cpp
#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/UI/GlobalVariables.h"
```

**2. `Update` を差し替え、その後に `ReadCameraInput` を足す**：

```cpp
void EditorViewport::Update() {
	// Sceneビューにあるときだけ操作（どのキー・ボタンで動かすかは EditorKeymap。Settings → Editor → Keymap で変えられる）
	if (sceneView_.IsHovered()) {
		debugCamera_->ApplyInput(ReadCameraInput());
	}
	// ImGuiの調整項目と同期
	SyncGV();
}

//=============================================================================
// Scene ビューのカメラの操作の量
//=============================================================================
DebugCamera::Input EditorViewport::ReadCameraInput() {
	// マウスの動き（エディタの決まりで読む。Scene ビューの上かは、呼ぶ側が見ている）
	const Vector2 mouse = {static_cast<float>(InputManager::GetMouseDeltaX(InputGate::Editor)), static_cast<float>(InputManager::GetMouseDeltaY(InputGate::Editor))};
	const Vector2 zero = {0.0f, 0.0f};
	DebugCamera::Input input;
	input.rotate = EditorKeymap::IsPressed(EditorAction::SceneRotate) ? mouse : zero;
	input.pan = EditorKeymap::IsPressed(EditorAction::ScenePan) ? mouse : zero;
	input.dollyDrag = EditorKeymap::IsPressed(EditorAction::SceneDollyDrag) ? -mouse.y : 0.0f; // マウスを上へ動かす（Y は下が+）と前へ
	input.dollyWheel = EditorKeymap::GetValue(EditorAction::SceneDollyWheel);
	return input;
}
```

### I-5 `Window/WindowManager.cpp`

**1. include**：

```cpp
#ifdef USE_IMGUI
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Windows/ControlWindow.h"
#endif
```

**2. `UpdateAll` の `w.editor->Update()` の前に足す**：

```cpp
		#ifdef USE_IMGUI
		// エディタの操作の割り当てを読む（ImGui のウィンドウは1つだけなので、1フレームに1回。このウィンドウが手前かを見るので、SetActiveWindow の後）
		if (w.window->GetWindowConfig().isImGui) {
			EditorKeymap::Update();
		}
		if (w.editor) {
			w.editor->Update(); // デバッグカメラ更新
		}
#endif
```

`w.editor` は全部のウィンドウにある（ゲームのウィンドウを2つ出すと2つ）。`EditorKeymap::Update` を2回呼ぶと「押した瞬間」が分からなくなるので、ImGui のウィンドウの番だけにした。`InputManager::Update`（`Engine::BeginFrame`）の後・`EditorViewport::Update`（カメラ）と ImGui の各ウィンドウ（ショートカット）の前になる。

### I-6 `Editor/ImGuiManager.cpp`

**1. include**（`EditorHistory.h` の次）：

```cpp
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
```

**2. `HandleGlobalShortcuts` を差し替える**：

```cpp
// エディタ全体で効くショートカット（どのウィンドウを触っていても効く。キーは EditorKeymap）
// 文字の入力中・ドラッグ中は何もしない（入力欄の中のCtrl+Zは、ImGuiが文字のUndoに使う）
void HandleGlobalShortcuts() {
	if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
		return;
	}
	if (EditorKeymap::IsTriggered(EditorAction::Undo)) {
		EditorHistory::RequestUndo();
	}
	if (EditorKeymap::IsTriggered(EditorAction::Redo)) {
		EditorHistory::RequestRedo();
	}
}
```

**3. Edit メニューのキーの文字**：

```cpp
		if (ImGui::MenuItem("Undo", EditorKeymap::GetShortcutText(EditorAction::Undo).c_str(), false, EditorHistory::CanUndo())) {
			EditorHistory::RequestUndo();
		}
		if (ImGui::MenuItem("Redo", EditorKeymap::GetShortcutText(EditorAction::Redo).c_str(), false, EditorHistory::CanRedo())) {
			EditorHistory::RequestRedo();
		}
```

**4. `Initialize` の `InspectorWindow::Initialize();` の次**：

```cpp
	// InspectorにエンジンのComponent（Transform・ModelRenderer）を登録する
	InspectorWindow::Initialize();
	// エディタの操作の割り当て（保存した人ごとの割り当てを読み、Settings に Keymap のページを足す）
	EditorKeymap::Initialize();
```

## パートJ ショートカットを Keymap で聞く

どれも `#include "HTTEngine/Editor/Keymap/EditorKeymap.h"` を、`HTTEngine/Editor/` の include の並び（名前の順）に足す。

### J-1 `Editor/Gizmos/SceneGizmo.cpp`（`HandleShortcuts` の W・E・R・F）

```cpp
	// キーは EditorKeymap（Settings → Editor → Keymap で変えられる）
	if (EditorKeymap::IsTriggered(EditorAction::ToolMove)) {
		settings.operation = GizmoOperation::Move;
	}
	if (EditorKeymap::IsTriggered(EditorAction::ToolRotate)) {
		settings.operation = GizmoOperation::Rotate;
	}
	if (EditorKeymap::IsTriggered(EditorAction::ToolScale)) {
		settings.operation = GizmoOperation::Scale;
	}
	if (EditorKeymap::IsTriggered(EditorAction::Frame)) {
		FocusSelected(camera);
	}
}
```

`HandleClick` の `ImGui::IsMouseClicked(ImGuiMouseButton_Left)` はそのまま（「選ぶ」は変えられない。0.7）。

### J-2 `Editor/Windows/ControlWindow.cpp`

**1. `ToolButton` と `kToolButtons` を差し替える**（吹き出しの「（W）」を、今の割り当てから作る。ボタンの ID は吹き出しと分けた。吹き出しの文字が変わっても、別のボタンにならないように）：

```cpp
struct ToolButton {
	GizmoOperation operation; // 押すと、この操作になる
	ToolIcon icon;            // 絵
	const char* id;           // ボタンの ID
	const char* name;         // 説明（後ろに、今の割り当てのキーを付ける）
	EditorAction shortcut;    // Scene ビューで切り替えるキー
};
constexpr auto kToolButtons = std::to_array<ToolButton>({
    {GizmoOperation::Move,   ToolIcon::Move,   "move",   "移動", EditorAction::ToolMove},
    {GizmoOperation::Rotate, ToolIcon::Rotate, "rotate", "回転", EditorAction::ToolRotate},
    {GizmoOperation::Scale,  ToolIcon::Scale,  "scale",  "拡縮", EditorAction::ToolScale},
});
```

**2. `DrawGizmoTools` の道具のループ**：

```cpp
	// --- 移動・回転・拡縮（Scene ビューで W・E・R（EditorKeymap の割り当て）を押しても切り替わる）---
	for (const ToolButton& tool : kToolButtons) {
		const std::string tooltip = EditorKeymap::WithShortcut(tool.name, tool.shortcut); // "移動（W）"
		if (ToolIcons::Button(tool.id, tool.icon, settings.operation == tool.operation, tooltip.c_str())) {
			settings.operation = tool.operation;
		}
		ImGui::SameLine(0.0f, kToolSpacing);
	}
```

**3. `DrawSaveButton` の最後**：

```cpp
	ImGui::SetItemTooltip("%s。停止中だけ", EditorKeymap::WithShortcut("シーンを保存", EditorAction::SaveScene).c_str());

	// --- Ctrl+S（文字の入力中は、入力欄のほうを優先する。名前の変更中に Ctrl+S を押しても保存しない）---
	const bool isTyping = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
	if (canSave && !isTyping && EditorKeymap::IsTriggered(EditorAction::SaveScene)) {
		sceneManager.RequestSave();
	}
```

### J-3 `Editor/Windows/SceneDialogs.cpp`

**1. `DrawFileMenuItems` のキーの文字**（3か所）：

```cpp
	if (ImGui::MenuItem("New Scene...", EditorKeymap::GetShortcutText(EditorAction::NewScene).c_str(), false, canEdit)) {
```

```cpp
	if (ImGui::MenuItem("Save", EditorKeymap::GetShortcutText(EditorAction::SaveScene).c_str(), false, hasFile)) {
		sceneManager->RequestSave();
	}
	if (ImGui::MenuItem("Save As...", EditorKeymap::GetShortcutText(EditorAction::SaveSceneAs).c_str(), false, hasFile)) {
```

**2. `HandleShortcuts`**：

```cpp
	if (EditorKeymap::IsTriggered(EditorAction::NewScene)) {
		RequestNew();
	}
	if (EditorKeymap::IsTriggered(EditorAction::SaveSceneAs)) {
		RequestSaveAs();
	}
```

### J-4 `Editor/Windows/HierarchyWindow.cpp`

**1. `DrawEntityMenu` のキーの文字**：

```cpp
	if (ImGui::MenuItem("Rename", EditorKeymap::GetShortcutText(EditorAction::Rename).c_str())) {
		StartRename(handle);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Copy", EditorKeymap::GetShortcutText(EditorAction::Copy).c_str())) {
		EditorHistory::Copy(handle);
	}
	// Paste：このEntityと同じ親の下（兄弟）に貼る　Paste As Child：このEntityの子として貼る
	if (ImGui::MenuItem("Paste", EditorKeymap::GetShortcutText(EditorAction::Paste).c_str(), false, EditorHistory::CanPaste())) {
		EditorHistory::RequestPaste(ParentOf(handle));
	}
	if (ImGui::MenuItem("Paste As Child", nullptr, false, EditorHistory::CanPaste())) {
		EditorHistory::RequestPaste(handle);
	}
	if (ImGui::MenuItem("Duplicate", EditorKeymap::GetShortcutText(EditorAction::Duplicate).c_str(), false, !IsUIScreenRoot(handle))) {
```

**2. `HandleShortcuts` の頭の return の後を差し替える**：

```cpp
	// キーは EditorKeymap（修飾キーまでちょうど同じときだけ。Ctrl+Shift+C では Ctrl+C は効かない）
	if (EditorKeymap::IsTriggered(EditorAction::Rename)) {
		StartRename(selected_); // 選んでいなければ何もしない
	}
	if (EditorKeymap::IsTriggered(EditorAction::Delete) && selected_.IsValid() && !IsUIScreenRoot(selected_)) {
		EditorHistory::RequestDestroy(selected_);
	}
	if (EditorKeymap::IsTriggered(EditorAction::Copy) && selected_.IsValid()) {
		EditorHistory::Copy(selected_);
	}
	if (EditorKeymap::IsTriggered(EditorAction::Paste)) {
		EditorHistory::RequestPaste(ParentOf(selected_)); // 選んでいるEntityの兄弟に貼る（選んでいなければroot）
	}
	if (EditorKeymap::IsTriggered(EditorAction::Duplicate) && selected_.IsValid() && !IsUIScreenRoot(selected_)) {
		EditorHistory::RequestDuplicate(selected_);
	}
```

### J-5 `Editor/Windows/ProjectWindowActions.cpp`

**1. 右クリックのメニュー**：

```cpp
	if (ImGui::MenuItem("Rename", EditorKeymap::GetShortcutText(EditorAction::Rename).c_str(), false, !isLocked)) {
		StartRename(item);
	}
	if (ImGui::MenuItem("Delete", EditorKeymap::GetShortcutText(EditorAction::Delete).c_str(), false, !isLocked)) {
```

**2. `ProjectWindow::HandleShortcuts`**：

```cpp
	if (EditorKeymap::IsTriggered(EditorAction::Rename)) {
		StartRename(*item);
	}
	if (EditorKeymap::IsTriggered(EditorAction::Delete)) {
		deleteTarget_ = item->path;
		deleteRequest_ = true;
	}
```

### J-6 `Editor/Timeline/TimelineRows.cpp`（`HandleKeyboard`）

```cpp
	if (EditorKeymap::IsTriggered(EditorAction::Delete)) {
		DeleteSelected(state);
	}
	if (EditorKeymap::IsTriggered(EditorAction::Frame)) {
		state.needsFit = true;
	}
```

### J-7 `Editor/Timeline/TimelineWindow.cpp`（Fit の吹き出し）

```cpp
	ImGui::SetItemTooltip("%s", EditorKeymap::WithShortcut("全体が見える範囲にする", EditorAction::Frame).c_str());
```

### J-8 `Editor/Widgets/CurveEditor.cpp`

**1. Frame の吹き出し**：

```cpp
	ImGui::SetItemTooltip("%s", EditorKeymap::WithShortcut("全体が見える範囲にする", EditorAction::Frame).c_str());
```

**2. `HandleKeyboard`**：

```cpp
	if (EditorKeymap::IsTriggered(EditorAction::Frame)) {
		needsFrame_ = true;
	}
	if (selectedKey_ >= 0 && EditorKeymap::IsTriggered(EditorAction::Delete)) {
		RemoveSelectedKey(curve);
	}
```

### J-9 `Editor/Gizmos/PointListGizmo.cpp`（`HandleDelete`）

```cpp
// ===== キー：Delete（EditorKeymap の「消す」）で選んだ点を消す（minCount より少なくはしない）=====
void HandleDelete(const PointList& list) {
	if (edit.dragPoint >= 0 || edit.selectedPoint < 0 || edit.selectedPoint >= *list.count || !EditorKeymap::IsTriggered(EditorAction::Delete)) {
```

### J-10 `Editor/Windows/CodeWindow.cpp`

**1. 検索欄の吹き出し**：

```cpp
	ImGui::SetItemTooltip("%s。Enter で次を探す。大文字・小文字は区別しない", EditorKeymap::WithShortcut("検索", EditorAction::Find).c_str());
```

**2. Ctrl+F**：

```cpp
	// --- Ctrl+F（EditorKeymap の「検索」）で検索欄へ（このウィンドウを触っているときだけ。検索欄に文字を打っている間は効かない）---
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && EditorKeymap::IsTriggered(EditorAction::Find)) {
		findFocusRequest = true;
	}
```

前は検索欄に文字を打っている間も Ctrl+F が効いた（もう検索欄にいるので、何も変わらない）。

## パートK Reference

### K-1 `Docs/Reference/17-Editor.md`

表の **Settings** の行の後ろに、2行足す。

```markdown
| **Keymap（Settings → Editor → Keymap）** | エディタの操作のキー・ボタンを人ごとに変える。割り当てのボタンを押してから、新しいキー・ボタンを押すと入れ替わる（Ctrl・Shift・Alt を押しながら押すと組になる。Esc でやめる）。＋で足す・×で外す・「既定」で戻す。ほかとぶつかると赤くなり、相手の名前と「相手の方を外す」が出る。上のボタンでカメラの操作を **Unity 風・Blender 風** にできる。Save で `Parameters/Editor/Keymap.json`（git に入らないので人ごと）。この章に書いたキーは、全部既定の割り当て |
| **Scene ビューのカメラ** | 既定は、ホイールボタンを押しながらドラッグで向きを変える（その場で回る）・右ドラッグで平行移動・ホイールで前後。Keymap で変えられる（前後に動くドラッグも足せる） |
```

**Undo / Redo** の行の頭の「Ctrl+Z / Ctrl+Y」の後ろに「（Ctrl+Shift+Z でも Redo）」を足す。

### K-2 `Docs/Reference/19-NotYet.md`

「キーの割り当て」の行を差し替える（エディタの Keymap はできたが、ゲームから使う所はまだ）。

```markdown
| ゲームのキーの割り当て（Script から名前で読む入力・プレイヤーのキーコンフィグ） | Input Action の表（`InputActionMap`）はあるが、ゲームから使う窓口・プレイヤーの割り当ての保存先はまだ。キー・ボタンを直接読む（移動は `InputManager::GetMoveAxis`）。エディタの操作は Keymap で変えられる（[17章](17-Editor.md)） |
```

### K-3 `Docs/Reference/11-Input.md`

一番下に足す。

```markdown
## エディタの操作のキー

エディタのショートカット・Scene ビューのカメラのボタンは、決め打ちではなく **Settings → Editor → Keymap** で人ごとに変えられる（[17章](17-Editor.md)）。中身は Input Action の表（`Input/Action/InputActionMap`）で、ゲームのキーコンフィグも同じ表で作る予定。

- `InputManager` の `IsKeyPressed` などには、読む決まり `InputGate` を渡せる。ゲームは書かない（`InputGate::Game`：ImGui のパネルを触っている間は「押していない」）。`InputGate::Editor` はエディタの操作用（パネルを触っていても読む。文字を打っている間だけ読まない）
```

## パートL 確かめ方

### L-1 ビルド

段階1（A〜E）だけを写した所で一度、全部写した後にもう一度、Debug・Development・Release をビルドする（Release は ImGui が無いので、Editor/ の物が入っていないかを見る）。

```bash
MSBuild.exe project/HTTEngine.slnx -m -p:Configuration=Debug -p:Platform=x64
```

### L-2 Sandbox で見る（Development）

1. Edit → Settings → 左の一覧の Editor の **Keymap**。カメラ・Scene View・編集・シーンのファイルの4つの見出しに、操作が並ぶ。「選ぶ（クリック）」だけは押せない（変えられません）
2. 「回転（ドラッグ）」の `Mouse Middle` を押す → 「割り当てるキー・ボタンを押してください」のモーダル → 右クリック → `Mouse Right` になる。平行移動の `Mouse Right` と一緒に赤くなり、下に「! Mouse Right は「平行移動（ドラッグ）」とぶつかっています」と「「平行移動（ドラッグ）」の方を外す」が出る
3. 「平行移動（ドラッグ）」の方を外す → 平行移動の ＋ → ホイールボタン → 赤が消える。一覧の Keymap に `*` が付く
4. Scene ビューで右ドラッグ → 向きが変わる、ホイールボタンでドラッグ → 平行に動く
5. Save → `project/Sandbox/Parameters/Editor/Keymap.json` に、変えた2つだけが書かれる。エディタを閉じて開き直しても右ドラッグで回る
6. 上の **Unity 風** → Alt+右ドラッグで前後に動く。**Blender 風** → Shift+ホイールボタンで平行移動、Ctrl+ホイールボタンで前後。**HTTEngine** で元に戻る（Save しなければ Revert でも戻る）
7. 「移動のギズモ」に Q を割り当てる → Scene ビューの上で Q で移動のギズモ、W では変わらない。Control ウィンドウの移動のボタンの吹き出しが「移動（Q）」になる
8. 「元に戻す」の ＋ → Ctrl を押しながら Z ではなく **Ctrl+Shift+Z** を押すと、「やり直す」とぶつかる（同じ組）。× で外す
9. 拾っている間に Ctrl+Z を押して割り当てても、Undo は動かない。Esc でやめると、元の割り当てのまま
10. Hierarchy で名前を変えている間（入力欄に文字を打っている間）に Delete・Ctrl+C を押しても、Entity は消えない・コピーされない（今まで通り）
11. 日本語入力を ON にしたまま、Scene ビューの上で W・E・R → ギズモが切り替わる（今までは ImGui のキーだったので、ON だと効かないことがあった）
12. File・Edit・Hierarchy の右クリックのメニューの右のキーの文字が、割り当てを変えると変わる

### L-3 写しで確かめたこと

`develop`（aa6717a）から作った写しに、この md のコードを全部入れて確かめた（写しのブランチ `answer-input`）。

- **ビルド**：Debug・Development・Release で、エラー・警告 0（前からある DirectXTex の C4865 は別）
- **写しだけに入れた確かめのコード**（本体には入れない）で、起動したときに 60 項目を見て全部合った
  - 名前の表の 136 個の入力 × 修飾キー 8 通り × 向き 4 つを、文字にして読み直すと元に戻る。名前・入力のかぶり無し
  - `Ctrl+Shift+Key.Z`・`Key.A:-X`・`Alt+Pad.LeftStickY:+Y` を読める。`Bogus`・`Ctrl+`・`Key.A:+Q`・`Hyper+Key.A`・空は読まない。画面の文字は `Ctrl+Z`・`Mouse Right`・`Pad A`・`Alt+Mouse Wheel`
  - エディタの既定の 20 個の操作と、3つのプリセットに、ぶつかりが無い
  - 回転を右クリックにすると平行移動とぶつかり、左クリックにすると変えられない「選ぶ」とぶつかる。「選ぶ」は SetBindings・RemoveBinding・ファイルから読むのでも変わらない
  - context：空はほかの場所の両方とぶつかり、別の場所どうしはぶつからない。Exact で Ctrl+Z と Ctrl+Shift+Z はぶつからない。AtLeast で Space と Shift+Space はぶつかり、Shift+Space と Ctrl+Space はぶつからない。ホイールの上と下のボタンはぶつからず、ホイールの量の Action とはぶつかる
  - 保存は変えた Action だけ（全部外したのは空の配列）。読み直すと同じ。知らない Action・読めない割り当て・変えられない Action は飛ばす。壊れたファイル・無いファイルは既定のまま。Revert・SetBinding（足す・入れ替え）・RemoveBinding・ResetToDefault
  - メニューの文字 `Ctrl+Z`・`Ctrl+Y`（Redo は1つ目）・`Ctrl+Shift+S`、吹き出し「移動（W）」、割り当てなしは説明だけ
- **画面**（写しを起動して撮った）：Keymap のページに4つの見出しと 20 個の操作が並ぶ。回転を右クリックにすると、回転と平行移動の割り当てが赤くなり、相手の名前と「〜の方を外す」が出る。一覧と見出しに `*`。「選ぶ（クリック）」は押せず「変えられません」。拾い始めると、ほかを暗くしたモーダルに「「前後に動く（ドラッグ）」に割り当てるキー・ボタンを押してください」が出る
- **まだ見ていない**：本物のキー・マウスを押して拾う所（L-2 の 2・3・7〜9）、Scene ビューのカメラを右ドラッグなどで動かす所（L-2 の 4・6）、保存して開き直す所（L-2 の 5）、日本語入力 ON（L-2 の 11）。写しを裏から起動すると Windows がウィンドウを手前に出させず、DirectInput が読まない（手前でない間は「押していない」）ので、手で見てほしい
- **2026-11-12 の直しの後**：本体で3構成のビルド（エラー・警告 0）、写しで確かめの 60 項目をやり直して合った。× と ＋ が小さくなり、上下の真ん中にそろうのを撮って見た。右クリックで拾えるようになったかは、まだ手で見ていない（原因は半角/全角などの押しっぱなしに見えるキーと考えたが、写しでは本物のキーを押せないので確かめられていない）
- **2026-11-12 の2回目の直し**（拾う係をフレームの頭で動かす）：本体の Development でビルドが通った。右クリック・Esc で拾えるかは、まだ手で見ていない（写しでは本物のキーを押せない）
- **2026-11-12 手で確かめた**：2回目の直しの後、Sandbox の Keymap で、モーダルを出して右クリックすると拾えた（ユーザーが見た）
