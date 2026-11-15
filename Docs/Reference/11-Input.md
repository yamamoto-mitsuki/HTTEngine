> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 11. 入力

```cpp
if (InputManager::IsKeyTriggered(DIK_SPACE)) { … } // 押した瞬間
if (InputManager::IsKeyPressed(DIK_W))      { … } // 押している間
if (InputManager::IsKeyReleased(DIK_SPACE)) { … } // 離した瞬間

const float x = InputManager::GetLeftStickX();     // -1 〜 1（デッドゾーン処理済み）
if (InputManager::IsButtonTriggered(GamepadButton::A)) { … }

// トリガー（LT・RT）もボタンとして読める（押す 0.5 まで引いたら押した・押している間は 0.4 より戻したら離した）
if (InputManager::IsButtonTriggered(GamepadButton::RightTrigger)) { … } // RT を押した瞬間
if (InputManager::IsButtonReleased(GamepadButton::LeftTrigger)) { … }   // LT を離した瞬間
const float amount = InputManager::GetRightTrigger();                   // 量（0〜1）はいままで通り
InputManager::SetTriggerPressThreshold(0.3f);                           // しきい値を変える（離す方は SetTriggerReleaseThreshold）

const long dx = InputManager::GetMouseDeltaX();

const Vector2 mouse = InputManager::GetMousePositionInGame();    // ゲーム画面の座標（スプライトの position と同じ：真ん中0、右と上が+、端が±1）
if (InputManager::IsMouseInGame()) { … }                          // マウスがゲーム画面（エディタでは Game ビューの絵）の上か
```

キーの名前は DirectInput の `DIK_A` `DIK_SPACE` `DIK_LSHIFT` など。**ゲームの操作（移動・ジャンプ）は、キーを直接書かず `GameInput` で Action の名前から読む**（下の「Action の名前で読む」。プレイヤーがキーコンフィグで変えられる）。InputManager を直接読むのは、デバッグのキーなど割り当てを変えない物だけ。

- 移動（`GameInput::GetVector2("Move")`）は入力の値（x が右・y が上）で、ワールドの向きではない。キャラをカメラから見た向き（W＝画面の奥）で動かすなら `CameraRelativeInput` に通す（[10 カメラ](10-Camera.md) の「カメラから見た向きで動かす」）。
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

## OS のカーソルを隠す・閉じ込める

```cpp
InputManager::SetCursorVisible(false); // ゲーム画面の上では矢印を隠す（ほかの窓・ゲームの外では出る）
InputManager::SetCursorConfined(true); // ゲーム画面の外へ出られなくする（画面の端で止まる。Unity の CursorLockMode.Confined）
```

- 頼みはシーンを変えると（Stop も）戻る（隠さない・閉じ込めない）。したいシーンが自分で頼む（シーンの `Initialize` や Script の `Start` で）。
- 本当に隠す・閉じ込めるのは Play 中だけ（一時停止中は放す）。閉じ込めはメインのウィンドウが手前の間だけ。
- 閉じ込めている間も `GetMouseDeltaX/Y` は動いた分を返す（照準・カメラを回すのに使える）。
- エディタでは閉じ込めている間パネルを触れないので、**Shift+F1 で放す**。Game ビューの絵を左クリックすると、また閉じ込める（そのクリックはゲームに渡さない）。
- 今本当に閉じ込めているかは `IsCursorConfineActive()`（頼んだ値は `IsCursorConfined()`）。

## パッドの振動

```cpp
InputManager::SetVibration(0.5f, 0.2f); // 左（低い・ドスン）・右（高い・ブルブル）のモーター。0〜1
InputManager::StopVibration();
```

```cpp
// ✗ だめな例
if (GetAsyncKeyState(VK_SPACE)) { … } // ✗ Windowsを直接叩くとImGuiに文字を打っている間も反応する
```

## エディタの操作のキー

エディタのショートカット・Scene ビューのカメラのボタンは、決め打ちではなく **Settings → Editor → Keymap** で人ごとに変えられる（[17章](17-Editor.md)）。中身は Input Action の表（`Input/Action/InputActionMap`）で、ゲームの `GameInput`（次の節）も同じ表で作っている。

- `InputManager` の `IsKeyPressed` などには、読む決まり `InputGate` を渡せる。ゲームは書かない（`InputGate::Game`：ImGui のパネルを触っている間は「押していない」）。`InputGate::Editor` はエディタの操作用（パネルを触っていても読む。文字を打っている間だけ読まない）

## Action の名前で読む（GameInput・キーコンフィグ）

キー・ボタンを直接書かず、**「Jump」「Move」のような Action の名前で読む**。どのキー・ボタンかは、ゲームのファイル `Parameters/Input/InputActions.json`（git に入れて、チームで1つ）で決める。プレイヤーがキーコンフィグで変えた割り当ては、プレイヤーごとに保存される。

```cpp
// Script.h に入っているので、include は要らない
if (GameInput::IsTriggered("Jump")) { … }            // 押した瞬間（IsPressed：押している間、IsReleased：離した瞬間）
const Vector2 move = GameInput::GetVector2("Move");  // 2つの量（x が右・y が上。-1〜1）
const float zoom = GameInput::GetValue("Zoom");      // 1つの量（トリガー・ホイール）
const std::string key = GameInput::GetDisplayText("Jump"); // "Space" など（「Space でジャンプ」の案内に）
```

- **Action を足す・直すのは Settings → Project → Input**（[17章](17-Editor.md)）。名前・種類（Button・Value・Vector2）・グループ・効く場所（ぶつかりを調べる範囲）を決め、割り当てを押して拾う。Save で `Parameters/Input/InputActions.json` に書き、Play 中でもすぐ効く。
- 種類が Vector2・Value の割り当ては、**向きの行**に分けて並ぶ（Vector2 は 上・下・左・右（キー・ボタン）と 横の軸・縦の軸（スティック・マウスの動き））。行の ＋ で、その向きの割り当てを足す。軸は ＋ の向き（右・上）へ動かして拾う（反対へ動かすと「（反対）」になる）。
- 知らない名前は false・0 を返し、ログに1回だけ警告が出る（書き間違いはログで気づく）。
- 修飾キーは「書いた物を押していればよい」（Shift を押しながらでもジャンプできる）。エディタのパネルを触っている間は押していない（`InputGate::Game`）。

### キーコンフィグの画面（プレイヤーが変える）

画面はゲームの UI（Text・UI State など）で作り、`GameInput` に頼む。

```cpp
// 決めたとき：次に押したキー・ボタンを「Jump」の1つ目の割り当てにする（index を省くと足す）
GameInput::StartRebind("Jump", InputBindingRow::All, 0);

// 毎フレーム（Update）：結果を見る（GameInput がこのフレームの Script より前に拾っている）
switch (GameInput::GetRebindResult()) {
case InputRebinder::Result::Captured: /* 入った。ぶつかりは GameInput::GetMap().FindConflicts で見せる */ break;
case InputRebinder::Result::Canceled: /* Esc でやめた */ break;
default: break;
}

GameInput::SaveBindings();          // 保存（既定から変えた Action だけ）
GameInput::RevertBindings();        // 保存した割り当てに戻す
GameInput::GetMap().ResetAllToDefault(); // 全部を既定に戻す（残すなら SaveBindings）
```

- 割り当ての一覧・行は `GameInput::GetMap()`（`GetBindings`・`GetDesc`）と `InputBindingRows::Get(種類)`・`InputBindingRows::Of(種類, 割り当て)` で作る。行の名前（「上」など）はゲームの画面の文字なので、ゲームが決める。
- 拾っている間は、その人の全部の Action が効かない（決定のキーを拾っても、メニューが動かない）。拾い始めたときに押していた決定のキーは、離して押し直すまで拾わない。**Esc でやめる**（Esc は割り当てられない）。
- 「プレイヤーが変えられない」にした Action（メニューの決定など）は、`StartRebind` しても何もしない。画面にも出さない。
- 保存先は、ゲームのフォルダ（`Parameters/` と同じ所）の `UserData/Input/Player1.json`（エディタも製品版も同じ）。**ゲームのリポジトリの `.gitignore` に `UserData/` を足す**（遊んだ人ごとの物なので）。ゲームを書けない所（Program Files など）に置くと保存できない（ログに出る）。
- エディタで遊んで保存した割り当ては、Settings → Project → Input の一番下の「プレイヤーの上書きを消す」で消せる（消さないと、既定を直しても、その Action はプレイヤーの割り当てのまま）。
- 上書きは **Action ごと**（変えた Action は割り当ての全部を持つ）。既定に足した割り当ては、上書きのある Action には届かない。

### 2人で遊ぶ

```cpp
GameInput::SetPlayerCount(2);              // 1〜4
if (GameInput::IsTriggered("Jump", 1)) { … } // 2P（最後の引数。0 が 1P）
```

- 1P はキーボード・マウス・パッド0、2P 以降はパッドだけ（n 人目＝ n 番のパッド）。全員が同じファイルの既定から始まり、保存は人ごと（`Player2.json`）。
- ボタンを押した人がパッドを取る「参加」、キーボードを2人で分けるのは無い（[19章](19-NotYet.md)）。

```cpp
// ✗ だめな例
if (InputManager::IsKeyTriggered(DIK_SPACE)) { Jump(); } // ✗ キーの決め打ち。プレイヤーがキーコンフィグで変えられない
const Vector2 look = GameInput::GetVector2("Look");      // ✗ マウスの動きとスティックを1つの Action に混ぜている（下）
```

- **マウスの動きとスティックは別の Action にする**（`LookMouse`：割り当てられる入力を MouseMove だけ、`LookStick`：PadAxis だけ）。マウスはそのフレームに動いた数、スティックは倒した量（時間を掛けて使う）で、単位が違う。
- 前にあった `InputManager::GetMoveAxis()`（WASD・矢印・左スティックの決め打ち）は消した。同じ動きは、Action の Move（Vector2・長さを 1 までにする）に `Key.W:+Y`・`Key.S:-Y`・`Key.A:-X`・`Key.D`・矢印・`Pad.LeftStickX`・`Pad.LeftStickY:+Y` を割り当てて `GameInput::GetVector2("Move")` で読む。
