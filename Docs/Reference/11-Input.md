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

const Vector2 move = InputManager::GetMoveAxis();                // WASD・矢印キー・左スティックをまとめた移動（-1〜1。斜めでも長さ1まで）
const Vector2 mouse = InputManager::GetMousePositionInGame();    // ゲーム画面の座標（スプライトの position と同じ：真ん中0、右と上が+、端が±1）
if (InputManager::IsMouseInGame()) { … }                          // マウスがゲーム画面（エディタでは Game ビューの絵）の上か
```

キーの名前は DirectInput の `DIK_A` `DIK_SPACE` `DIK_LSHIFT` など。

- `GetMoveAxis()` は入力の値（x が右・y が上）で、ワールドの向きではない。キャラをカメラから見た向き（W＝画面の奥）で動かすなら `CameraRelativeInput` に通す（[10 カメラ](10-Camera.md) の「カメラから見た向きで動かす」）。
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
