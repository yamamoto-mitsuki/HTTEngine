参考：
- XInput のパッドの値（ボタンの印 wButtons・トリガー 0〜255）：https://learn.microsoft.com/ja-jp/windows/win32/api/xinput/ns-xinput-xinput_gamepad
- Unity の Input System：トリガーも「ボタン」として読む（ButtonControl。Press Point を越えたら押した）：https://docs.unity3d.com/Packages/com.unity.inputsystem@1.7/api/UnityEngine.InputSystem.Controls.ButtonControl.html
- Unity の Input System の既定のしきい値（Default Button Press Point・Button Release Threshold）：https://docs.unity3d.com/Packages/com.unity.inputsystem@1.7/api/UnityEngine.InputSystem.InputSettings.html
- Unity URP の Color Adjustments（Saturation。トーンマップの前の色の調整）：https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/manual/Post-Processing-Color-Adjustments.html
- 明るさの重み（Rec.709 の 0.2126・0.7152・0.0722）：https://en.wikipedia.org/wiki/Relative_luminance
- Unity の Cinemachine の Rotation Composer（Screen Position：見る物を画面のどこに置くか）：https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineRotationComposer.html
- 前の md：Docs/Tasks/VirtualCamera.md（Camera Look At・Camera Volume）・Docs/Tasks/CameraTimeline.md・Docs/Tasks/TrailFadeStopActionCursorConfine.md（入力の直し）
- この作業のきっかけ：AL4_01 のジャスト反撃で、エンジンに足りなかった3つ

# トリガーの押した瞬間・彩度のポストエフェクト・Camera Look At の画面の位置

ゲーム（AL4_01）のジャスト反撃で足りなかった3つを、エンジンに足す。どれも特定のゲームを前提にせず、エンジンの中で完結させる（ゲームの Script で前のフレームの量を覚えて比べる、のような穴埋めはしない）。

- **パートA 入力：トリガー（LT・RT）をボタンとして読む**（`XInputGamepad`・`InputManager`）：`GamepadButton::LeftTrigger`・`RightTrigger` を足し、今の `IsButtonPressed`・`IsButtonTriggered`・`IsButtonReleased` でそのまま読む。しきい値は押す 0.5・離す 0.4（`SetTriggerPressThreshold`・`SetTriggerReleaseThreshold`）
- **パートB ポストエフェクト：彩度（Saturation）**（新しい `Post/ColorAdjust.hlsli`・`Lens.PS`・`LensPass`・`PostProcess`・`SceneRenderer`・`PostOverride`・`CameraVolumeComponent`・`CameraVolume`）：Settings → PostEffect → **ColorAdjust** の **Saturation**（1 で普通・0 で白黒）と、Camera Volume の **Color Adjust → Saturation**
- **パートC Camera Look At：Screen Position**（`CameraLookAtComponent`・新しい `Systems/Camera/CameraLookAt.h`・`.cpp`・`CameraSystem.cpp`）：見る点を画面の決めた位置（例：横 1/3）に写す。Look At の中身を CameraSystem.cpp から `CameraLookAt.h`・`.cpp` へ分ける（Follow・Shake・Volume・Path と同じ形）
- **パートD Sandbox で確かめる**：テストシーン **TriggerSaturationLookAtTest** と Script **TriggerCameraSwitcher**（RT か Enter でカメラを切り替える・LT/RT の押した・離したを Console へ）・**SideSwing**（見る物を左右に往復させる）
- **パートE ディザ**（後で足した直し。新しい `Post/Dither.hlsli`・`Lens.PS`・`LensPass`・`PostProcess`）：白黒の絵で見えた、ビネットの暗さの楕円の帯を消す。Settings → PostEffect → ToneMap → **Dither**

パート **A・B・C はそれぞれ別の機能なので、どの順に写してもよい**（どれの後でもビルドできる）。D は A・B・C を全部使うので最後。

ブランチは `feature/TriggerSaturationScreenPosition`（develop 1d45f28 から。Trail・Stop Action・カーソルを閉じ込める作業が入った後）。**2026-11-09 に、パートA〜D は本体へ直接入れた**（この md は記録と読み方用。本体は写しの答えと中身が同じ・3構成ビルド OK）。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドでエラー・自分のファイルの警告 0。自動テスト（Debug・Development 89 項目、Release 88 項目。Release は始めから再生中なので「停止中」の1項目が無い）が全部合格。その中に「LT 0.55 で押した瞬間・0.45 に戻しても押したまま・0.38 で離した瞬間」「抜いて差し直すと押したままのトリガーは押した瞬間（今までのボタンと同じ）」「しきい値 0 でもデッドゾーンの中は押していない」「Screen Position を 0 にすると今までと**ビットまで同じ**向き」「横 1/3・端（1, -1）・後ろの点・視野角 10 度と 120 度・Transform を回したカメラで、見る点が誤差 0.001 以内で Screen Position に写り、横に傾かない」「Smooth Time で遅れて追い、追いついたら Screen Position に写る（今見ている点はいつも写る）」「停止中は Smooth Time があっても、変えた値がすぐ Game ビューに出る」「カメラの切り替えで、彩度と露出が同じ速さで補間する（差 0）」がある。撮った絵で、Saturation 0 の Game ビューの画素の R・G・B の差が 1 以下（色が無い）・赤い UI は (255, 0, 0) のまま・Saturation 2 で色が濃くなることも見た。

## 0章 全体の形

### 0.1 何が困っていたか

1. **トリガーの押した瞬間が取れない**：`GetLeftTrigger`・`GetRightTrigger` の量（0〜1）しか無く、ボタンのような「押した瞬間・離した瞬間」が無い。ゲームの Script が前のフレームの量を覚えて比べることになる（エンジンの穴埋め）。どれだけ引いたら押したか（しきい値）も Script ごとにばらばらになる
2. **彩度を変えられない**：ジャストの瞬間に画面を白黒にする、のような演出ができない。全体の設定にも、カメラごとの上書き（Camera Volume）にも無い
3. **見る点は画面の真ん中だけ**：Camera Look At は見る点を必ず画面の真ん中に置く。キャラを横 1/3 に置いて、向いている先を広く見せる構図が作れない

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Input/XInputGamepad.h`・`.cpp` | ボタンの印を `uint32_t` に広げ、`GamepadButton::LeftTrigger`・`RightTrigger` を足す。トリガーをしきい値で判定して、ボタンの印に入れる |
| A | `Input/InputManager.h`・`.cpp` | ボタンの窓口を `uint32_t` に。`SetTriggerPressThreshold` など4つの窓口 |
| B | `Shader/Data/Post/ColorAdjust.hlsli.shader`（新しい） | `Luminance`・`ApplySaturation` |
| B | `Shader/Data/Post/Lens.PS.shader` | 露出の後・トーンマップの前に彩度を掛ける |
| B | `Graphics/PostEffect/LensPass.h`・`.cpp` | `kSaturation`。`Render` に「色の調整を掛けるか」を足す |
| B | `Graphics/PostEffect/PostProcess.h`・`.cpp` | Settings の ColorAdjust・カメラの上書きを混ぜる。`Prepare`・`Composite` に `PostEffectMode` を渡す |
| B | `Graphics/Renderer/SceneRenderer.cpp` | `Prepare`・`Composite` に mode を渡す |
| B | `Graphics/PostEffect/PostOverride.h` | `PostParam::Saturation` |
| B | `Components/Camera/CameraVolumeComponent.h`・`Systems/Camera/CameraVolume.cpp` | `saturation` と Inspector・上書きを作る所 |
| B | `HTTEngine.vcxproj`・`.filters` | ColorAdjust.hlsli.shader を足す |
| C | `Components/Camera/CameraLookAtComponent.h` | `Vector2 screenPosition` |
| C | `Systems/Camera/CameraLookAt.h`・`.cpp`（新しい） | Look At の Inspector・見る点・**画面の位置に写る向き**（`CalcRotation`） |
| C | `Systems/Camera/CameraSystem.cpp` | Look At の中身を CameraLookAt へ移し、`CalcRotation` を使う |
| C | `HTTEngine.vcxproj`・`.filters` | CameraLookAt.h・.cpp を足す |
| D | `Sandbox/Source/Scripts/TriggerCameraSamples.h`・`.cpp`（新しい） | `TriggerCameraSwitcher`・`SideSwing` |
| D | `Sandbox/Sandbox.vcxproj`・`.filters` | 上の2つを足す |
| D | `Sandbox/Resources/scenes/TriggerSaturationLookAtTest.scene.json`（新しい） | テストシーン |

`Docs/Reference.md`（入力・色の調整・Camera Look At・Camera Volume の所）は、直接直してある（写さなくてよい）。

### 0.3 使い方

**A トリガー**：今のボタンと同じ窓口で読む。

```cpp
if (InputManager::IsButtonTriggered(GamepadButton::RightTrigger)) { … } // RT を押した瞬間（0.5 まで引いたフレーム）
if (InputManager::IsButtonPressed(GamepadButton::LeftTrigger)) { … }    // LT を押している間
if (InputManager::IsButtonReleased(GamepadButton::RightTrigger)) { … }  // RT を離した瞬間（0.4 より戻したフレーム）
const float amount = InputManager::GetRightTrigger();                   // 量はいままで通り読める

// しきい値を変える（ゲームの設定画面など。0〜1。どちらもデッドゾーンを引いた後の量）
InputManager::SetTriggerPressThreshold(0.3f);   // 軽く引いただけで押した
InputManager::SetTriggerReleaseThreshold(0.2f); // 押している間は 0.2 より戻したら離した
```

**B 彩度**：

| 場所 | 項目 | 中身 |
|---|---|---|
| Settings → PostEffect → **ColorAdjust** | Saturation | 全体の彩度。1 でそのまま（初めの値）・0 で白黒・1 より大きいと色が濃くなる（`Parameters/PostEffect/ColorAdjust.json`） |
| Camera Volume の **Color Adjust** | Saturation | チェックを入れたカメラだけ、この彩度。カメラの切り替えで、ほかの項目と同じ速さで補間する |

- UI（Image・Text・Vector Shape など）には掛からない（今まで通り、ポストエフェクトの後に重ねる）。
- Game ビュー・製品版に掛かる。Scene ビューには全体の設定の値が掛かる（View → Scene View → Post Effects を切ると掛からない）。Camera Volume は今まで通り Scene ビューには掛からない。
- Script から一瞬だけ白黒にしたいときは、白黒の Camera Volume を付けたカメラへ切り替える（Timeline のカメラのトラックか、Priority を上げる）。値のトラックで Camera Volume の `saturation` を動かしてもよい。

**C Camera Look At の Screen Position**：

| 項目 | 中身 |
|---|---|
| Screen Position | 見る点を写す画面の位置。真ん中が 0、右と上が＋、端が ±1（スプライトの position・`GetMousePositionInGame` と同じ）。横 1/3 の所なら X を **-0.333**（左から 1/3）か **0.333**（右から 1/3） |

- Smooth Time は今まで通り「見る点」をなめらかにする。Screen Position はなめらかにした点を、その位置に写す。
- 停止中も Game ビューに出る（今まで通り）。
- カメラは横に傾かない（画面の上はいつもワールドの上の側）。見る点が真上・真下に近くて、決めた位置へ届かないときは、届く所で止まる（壊れない）。

### 0.4 決めたこと（2026-11-09 に相談して決めた）

- **トリガーはボタンの1つにする**（Unity の Input System の `Gamepad.leftTrigger`、Unreal の `Gamepad_LeftTrigger` と同じ）。専用の関数（`IsLeftTriggerTriggered` など）を足すと、後で Input Action（キー・ボタンを名前でまとめる仕組み）を作るときに、ボタンとトリガーで分かれ道が増える。ボタンの印の型は `uint16_t` → `uint32_t` に広げ、XInput が使っていない上のビット（16・17 ビット目）に置く。`GamepadButton::A` などを渡している今のコードはそのまま動く
- **しきい値は押す・離すの2つ**（既定は押す 0.5・離す 0.4）。1つだと、しきい値の近くで指が震えたときに、押した瞬間が何度も出る（1.1）
- **Screen Position はエンジンの画面座標**（真ん中 0・端 ±1）。Cinemachine の -0.5〜0.5 にすると、エンジンの中で画面の座標が2種類になる

相談せずに決めたこと：

- **彩度は露出の後・トーンマップの前**に掛ける（Unity の Color Adjustments と同じ順。1.2）
- **彩度は Scene ビューにも掛ける**（全体の設定の値）。色は絵の歪みと違い、ギズモ・クリックの位置とずれないので、Scene ビューで外す理由が無い（Unity の Scene ビューも、ポストエフェクトを出す設定なら掛かる）。View → Scene View → Post Effects を切ったときは外す（露出・トーンマップだけ）。そのため `PostProcess::Prepare`・`Composite` には、bool 2つの代わりに `PostEffectMode` を渡す（効果ごとに「どのモードで掛けるか」を PostProcess の中の1か所で決められる）
- 彩度の項目は、Settings に **ColorAdjust** という新しいカテゴリで置く（後で Contrast・Color Filter などを足す所）。ToneMap のカテゴリには入れない
- Look At の中身（Inspector・見る点・向き）は CameraSystem.cpp から **`CameraLookAt.h`・`.cpp`** へ分けた（Camera Follow・Shake・Volume・Path が既にこの形。CameraSystem.cpp を「どのカメラを・どう混ぜるか」だけにする）

### 0.5 今回やらないこと

- Rotation Composer の **Dead Zone・Soft Zone**（見る点がこの四角の中にいる間はカメラを回さない）。要るときは Screen Position の周りに足す
- 色の調整の **Contrast・Color Filter・Hue Shift**。ColorAdjust.hlsli と ColorAdjust のカテゴリに足せる形にしてある
- 抜いたフレームの「離した瞬間」：今までのボタンと同じく、抜けている間は `IsButtonReleased` も false（何も押していない扱い）

## 1章 解説

### 1.1 トリガーをボタンにする形と、しきい値を2つにする理由

XInput のボタンは 16 ビットの印（`wButtons`。1 ビットが1つのボタン）で、トリガーは別の 0〜255 の量（`bLeftTrigger`・`bRightTrigger`）。今の `IsButtonTriggered` は「前のフレームの印」と「今の印」を比べて押した瞬間を出している。

そこで、毎フレームの `Update` で **「XInput の印 ＋ トリガーをしきい値で判定した印」をまとめた自分の印（`buttons`）** を作り、前のフレームの印（`buttonsPrev`）と比べるようにする。トリガーの印はビット 16・17 に置くので、`uint16_t` では入らない → `uint32_t` に広げる。

```
ビット:  31 … 18 | 17           16          | 15 … 0
中身:    空き     | RightTrigger LeftTrigger | XInput の wButtons そのまま（A=0x1000 など）
```

`1u << 16` は「1 を左へ 16 ずらす」＝ 0x10000（ビット 16 だけが 1 の数）。`u` は符号なしの数にする印。

**しきい値を2つにする（ヒステリシス）**：しきい値が1つ（0.5）だと、0.5 のあたりで指を止めたとき、量が 0.49・0.51・0.49… と揺れるたびに「押した瞬間」「離した瞬間」が何度も出る。押すときは 0.5、**押している間は 0.4 より戻すまで押したまま**にすると、間（0.4〜0.5）で揺れても変わらない。

```
量:     0.0 → 0.45 → 0.55 → 0.45 → 0.41 → 0.38 → 0.45
押すか:  ×      ×      ○(瞬間)  ○      ○      ×(離した瞬間)  ×
                 ↑ 押す 0.5 の手前      ↑ 離す 0.4 より上なので押したまま
```

離す方を押す方より大きくされたとき（押す 0.3・離す 0.8 など）は、押してすぐ離した扱いにならないように、離す方も押す方の値として使う（`std::min`）。量はデッドゾーン（約 0.118）を引いた後の物（`GetLeftTrigger` と同じ）で比べるので、しきい値を 0 にしても、触っていないトリガーは押していない。

### 1.2 彩度の式と、掛ける所

**明るさ**：同じ明るさの灰色の値は、R・G・B をそのまま平均するのではなく、目の感じ方の重みで足す（緑を一番明るく、青を一番暗く感じる）。

```
明るさ Y = 0.2126 × R + 0.7152 × G + 0.0722 × B   （Rec.709。リニアの値に掛ける）
```

**彩度**：灰色（Y, Y, Y）と元の色の間を、saturation で混ぜる（`lerp(灰色, 色, saturation)`）。

- saturation = 1 → 元の色そのまま
- saturation = 0 → 灰色（白黒）
- saturation = 2 → 灰色から元の色へ、2倍遠くへ行く＝色が濃くなる。行き過ぎてマイナスになる所は 0 で止める

**掛ける所**（Lens.PS。最後のパス）：

```
歪み・色収差・放射ブラー（HDR）→ 露出（× exposure）→ ★彩度 → トーンマップ（0〜1 へ）→ 風の線 → ビネット → 画面
                                                                                      ↑ この後に UI を重ねる（彩度は掛からない）
```

トーンマップの前（HDR のまま）に掛けるのは Unity と同じ順。白黒（0）なら R・G・B が同じ値になり、トーンマップの曲線は R・G・B に同じ形で掛かるので、トーンマップの後も灰色のまま（撮った絵で R・G・B の差は 1 以下）。

**カメラの切り替えで同じ速さになる理由**：Camera Volume の値は、今までの項目と同じ `PostOverrides`（項目ごとの「値 × 効き具合」と「効き具合」）の1つとして持つ。切り替えの補間は `LerpPostOverrides` が全部の項目を同じ割合で混ぜるので、`PostParam` に `Saturation` を足すだけで露出と同じ速さになる（テストで、彩度の割合と露出の割合の差は 0）。

### 1.3 見る点を画面の位置に写す向きの計算

カメラの向きを「左右の角（ヨー）」と「上下の角（ピッチ）」に分け、**上下 → 左右の順に1つずつ合わせる**。横の傾き（ロール）は 0。

**1. 画面の位置 → カメラから見た向き u**：カメラの前へ 1 進んだ所では、画面の上の端までの高さが tan(縦の視野角 / 2)、右の端までの幅がその × 縦横の比。Screen Position (sx, sy) に写る物は、カメラから見て

```
u = 長さ1にした ( sx × tan(視野角/2) × 縦横の比,  sy × tan(視野角/2),  1 )   （カメラの右・上・前）
```

の向きにある。

**2. 上下（ピッチ）を合わせる**：u を「上・前」の面で見ると、長さ r・前から上へ alpha の角。カメラを上へ pitch だけ向けると、u のワールドでの高さは `r × sin(alpha + pitch)` になる（左右の成分 u.x は上下に回しても変わらない）。これが見る点の向き w（長さ 1）の高さ w.y と同じになるように

```
alpha + pitch = asin(w.y / r)   →   pitch = asin(w.y / r) - alpha
```

**3. 左右（ヨー）を合わせる**：ワールドの上の軸のまわりに回しても高さは変わらないので、2 で合わせた高さはくずれない。真上から見た u の角（前から右へ）は `atan2(u.x, r × cos(alpha + pitch))`、w の角は `atan2(w.x, w.z)`。この差だけ左右に回す。

```
yaw = atan2(w.x, w.z) - atan2(u.x, r × cos(alpha + pitch))
前の向き = ( sin(yaw) × cos(pitch),  sin(pitch),  cos(yaw) × cos(pitch) )  → MakeLookRotation（ワールドの上を上にする＝傾かない）
```

- Screen Position が (0, 0) のときは、u が (0, 0, 1)・alpha = 0 なので、今までの `MakeLookRotation(見る点 - カメラ)` と同じ向きになる。計算の誤差も出さないように、(0, 0) のときは今までの式をそのまま使う（テストでビットまで同じ）。
- 真上近くの点を、画面の下の方に写そうとすると、pitch が 90 度を超える（カメラが裏返る）。そうならないように pitch を ±(90 度 − 少し) で止める（その分、決めた位置からずれる）。
- 視野角は写すときと同じ範囲（`kMinCameraFovY`〜`kMaxCameraFovY`）に収めてから使う。縦横の比は `GameScreen::GetAspectRatio()`（写すときと同じ）。
- 切り替えの補間は、カメラごとに向きを作った後で混ぜる（今まで通り）。補間の途中は2つの向きの間になる。

## パートA 入力：トリガー（LT・RT）をボタンとして読む

### A-1 `Input/XInputGamepad.h`（全体を差し替え）

```cpp
#pragma once
#include <array>
#include <cstdint>

#include <Windows.h>
#include <Xinput.h>

// ============================================================
// ボタン定数
// 下の 16 ビットは XInput のボタンの印（wButtons）そのまま。トリガーはその上のビットに置く
// （XInput の 16 ビットの中の空き 0x0400・0x0800 は、公開されていない Guide ボタンなどが使うので避ける）
// ============================================================
namespace GamepadButton {
constexpr uint32_t DpadUp = XINPUT_GAMEPAD_DPAD_UP;
constexpr uint32_t DpadDown = XINPUT_GAMEPAD_DPAD_DOWN;
constexpr uint32_t DpadLeft = XINPUT_GAMEPAD_DPAD_LEFT;
constexpr uint32_t DpadRight = XINPUT_GAMEPAD_DPAD_RIGHT;
constexpr uint32_t Start = XINPUT_GAMEPAD_START;
constexpr uint32_t Back = XINPUT_GAMEPAD_BACK;
constexpr uint32_t LeftThumb = XINPUT_GAMEPAD_LEFT_THUMB;   // 左スティック押し込み
constexpr uint32_t RightThumb = XINPUT_GAMEPAD_RIGHT_THUMB; // 右スティック押し込み
constexpr uint32_t LeftShoulder = XINPUT_GAMEPAD_LEFT_SHOULDER;
constexpr uint32_t RightShoulder = XINPUT_GAMEPAD_RIGHT_SHOULDER;
constexpr uint32_t A = XINPUT_GAMEPAD_A;
constexpr uint32_t B = XINPUT_GAMEPAD_B;
constexpr uint32_t X = XINPUT_GAMEPAD_X;
constexpr uint32_t Y = XINPUT_GAMEPAD_Y;
// --- トリガー（LT・RT）をボタンとして読む。引いた量がしきい値を越えたら「押した」（SetTriggerPressThreshold / SetTriggerReleaseThreshold）---
constexpr uint32_t LeftTrigger = 1u << 16;
constexpr uint32_t RightTrigger = 1u << 17;
} // namespace GamepadButton

class XInputGamepad {
public:
	// コントローラーの最大台数
	static constexpr int kMaxControllers = 4;

	/// <summary>初期化。接続済みコントローラーをログに出す。</summary>
	void Init();

	/// <summary>終了処理。全コントローラーの振動を止める。</summary>
	void Finalize();

	/// <summary>毎フレーム呼ぶ。接続状態と入力状態を更新する。</summary>
	void Update();

	// ---- 接続確認 ----

	/// <summary>指定番号のコントローラーが接続されているか。</summary>
	bool IsConnected(int index = 0) const;

	// ---- ボタン (GamepadButton::*** を使う。LeftTrigger・RightTrigger も読める) ----

	/// <summary>指定ボタンが押されているか。</summary>
	bool IsButtonPressed(uint32_t button, int index = 0) const;

	/// <summary>指定ボタンを押した瞬間か。</summary>
	bool IsButtonTriggered(uint32_t button, int index = 0) const;

	/// <summary>指定ボタンを離した瞬間か。</summary>
	bool IsButtonReleased(uint32_t button, int index = 0) const;

	// ---- スティック (-1.0f〜1.0f) ----

	/// <summary>左スティックX軸 (-1.0f=左, 1.0f=右)。</summary>
	float GetLeftStickX(int index = 0) const;

	/// <summary>左スティックY軸 (-1.0f=下, 1.0f=上)。</summary>
	float GetLeftStickY(int index = 0) const;

	/// <summary>右スティックX軸 (-1.0f=左, 1.0f=右)。</summary>
	float GetRightStickX(int index = 0) const;

	/// <summary>右スティックY軸 (-1.0f=下, 1.0f=上)。</summary>
	float GetRightStickY(int index = 0) const;

	// ---- トリガー (0.0f〜1.0f) ----

	/// <summary>左トリガー (0.0f=未押し, 1.0f=最大)。</summary>
	float GetLeftTrigger(int index = 0) const;

	/// <summary>右トリガー (0.0f=未押し, 1.0f=最大)。</summary>
	float GetRightTrigger(int index = 0) const;

	// ---- デッドゾーン設定 (0.0f〜1.0f) ----
	/// <summary>左スティックのデッドゾーンを設定 (0.0f〜1.0f)。</summary>
	void SetLeftStickDeadzone(float deadzone);

	/// <summary>右スティックのデッドゾーンを設定 (0.0f〜1.0f)。</summary>
	void SetRightStickDeadzone(float deadzone);

	/// <summary>トリガーのデッドゾーンを設定 (0.0f〜1.0f)。</summary>
	void SetTriggerDeadzone(float deadzone);

	float GetLeftStickDeadzone() const { return leftStickDeadzone_; }
	float GetRightStickDeadzone() const { return rightStickDeadzone_; }
	float GetTriggerDeadzone() const { return triggerDeadzone_; }

	// ---- トリガーをボタンとして読むときのしきい値 (0.0f〜1.0f。デッドゾーンを引いた後の量で比べる) ----
	/// <summary>この量以上引いたら「押した」（GamepadButton::LeftTrigger・RightTrigger）。</summary>
	void SetTriggerPressThreshold(float threshold);

	/// <summary>押している間は、この量より戻したら「離した」。押すしきい値より大きくしても、押すしきい値として扱う。</summary>
	void SetTriggerReleaseThreshold(float threshold);

	float GetTriggerPressThreshold() const { return triggerPressThreshold_; }
	float GetTriggerReleaseThreshold() const { return triggerReleaseThreshold_; }

	// ---- 振動 ----

	/// <summary>
	/// 振動をセットする。
	/// leftMotor  : 低周波モーター(ドスン系) 0.0f〜1.0f
	/// rightMotor : 高周波モーター(ブルブル系) 0.0f〜1.0f
	/// </summary>
	void SetVibration(float leftMotor, float rightMotor, int index = 0);

	/// <summary>振動を止める。</summary>
	void StopVibration(int index = 0);

private:
	/// <summary>
	/// int16_t の生値をデッドゾーン処理して -1.0f〜1.0f に正規化する。
	/// デッドゾーン内はゼロを返す。
	/// </summary>
	float ApplyDeadzone(int16_t value, float deadzone) const;

	/// <summary>
	/// 今フレームのボタンの印（XInput のボタン ＋ トリガーをしきい値で判定した物）を作る。Update が前フレームの印を buttonsPrev へ移した後に呼ぶ
	/// </summary>
	uint32_t MakeButtons(int index) const;

	/// <summary>
	/// トリガー1本を押しているか。押す・離すのしきい値を分けて、間で止めても押し直しにならないようにする（ヒステリシス）
	/// </summary>
	/// <param name="amount">今の引いた量（0.0f〜1.0f。デッドゾーン処理済み）</param>
	/// <param name="wasPressed">前フレームに押していたか</param>
	bool IsTriggerHeld(float amount, bool wasPressed) const;

	// デッドゾーン (0.0f〜1.0f)。Microsoft 推奨値をデフォルトに使用。
	float leftStickDeadzone_ = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;   // ≒0.239f
	float rightStickDeadzone_ = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.0f; // ≒0.265f
	float triggerDeadzone_ = XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 255.0f;         // ≒0.118f

	// トリガーをボタンとして読むときのしきい値（Unity の Input System の Press Point と同じ考え）
	float triggerPressThreshold_ = 0.5f;   // この量以上引いたら押した
	float triggerReleaseThreshold_ = 0.4f; // 押している間は、この量より戻したら離した

	// コントローラー1台分の状態をまとめた構造体
	struct ControllerState {
		XINPUT_STATE state = {};  // 今フレームの入力状態（スティック・トリガーの量を読む）
		uint32_t buttons = 0;     // 今フレームのボタンの印（GamepadButton::***。トリガーも入る）
		uint32_t buttonsPrev = 0; // 前フレームのボタンの印（Triggered/Released判定に使う）
		bool connected = false;
	};
	std::array<ControllerState, kMaxControllers> controllers_{};
};
```

変わった所：`GamepadButton` の型（`uint32_t`）と `LeftTrigger`・`RightTrigger`、ボタンの3つの関数の引数、しきい値の窓口4つ、private の `MakeButtons`・`IsTriggerHeld`・`triggerPressThreshold_`・`triggerReleaseThreshold_`、`ControllerState` の `statePrev` を `buttons`・`buttonsPrev` に置き換え。

### A-2 `Input/XInputGamepad.cpp`

**`Init` の中**：`statePrev` を消した分を直す。

```cpp
	for (int i = 0; i < kMaxControllers; ++i) {
		memset(&controllers_[i].state, 0, sizeof(XINPUT_STATE));
		controllers_[i].buttons = 0;
		controllers_[i].buttonsPrev = 0;
		controllers_[i].connected = false;
	}
```

（前は `memset(&controllers_[i].statePrev, 0, sizeof(XINPUT_STATE));` があった所。）

**`Update`**：前フレームの「印」を残し、最後に今フレームの印を作る。

```cpp
void XInputGamepad::Update() {
	for (int i = 0; i < kMaxControllers; ++i) {
		// 前フレームのボタンの印を保存してから今フレームを取得する
		controllers_[i].buttonsPrev = controllers_[i].buttons;

		DWORD result = XInputGetState(i, &controllers_[i].state);
		bool nowConnected = (result == ERROR_SUCCESS);

		if (nowConnected != controllers_[i].connected) {
			if (nowConnected) {
				LogManager::Log(std::format("[XInputGamepad] コントローラー{}番 接続", i));
			} else {
				LogManager::Log(std::format("[XInputGamepad] コントローラー{}番 切断", i));
				memset(&controllers_[i].state, 0, sizeof(XINPUT_STATE));
			}
		}
		controllers_[i].connected = nowConnected;
		// トリガーの判定は前フレームの印（buttonsPrev）を見るので、上で移した後に作る
		controllers_[i].buttons = MakeButtons(i);
	}
}
```

**ボタンの3つ**：`state.Gamepad.wButtons`・`statePrev.Gamepad.wButtons` を `buttons`・`buttonsPrev` に、引数を `uint32_t` に。

```cpp
bool XInputGamepad::IsButtonPressed(uint32_t button, int index) const {
	if (!IsConnected(index)) {
		return false;
	}
	return (controllers_[index].buttons & button) != 0;
}

bool XInputGamepad::IsButtonTriggered(uint32_t button, int index) const {
	if (!IsConnected(index)) {
		return false;
	}
	bool now = (controllers_[index].buttons & button) != 0;
	bool prev = (controllers_[index].buttonsPrev & button) != 0;
	return now && !prev;
}

bool XInputGamepad::IsButtonReleased(uint32_t button, int index) const {
	if (!IsConnected(index)) {
		return false;
	}
	bool now = (controllers_[index].buttons & button) != 0;
	bool prev = (controllers_[index].buttonsPrev & button) != 0;
	return !now && prev;
}
```

**`SetTriggerDeadzone` の下に足す**：

```cpp
void XInputGamepad::SetTriggerPressThreshold(float threshold) {
	triggerPressThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
}

void XInputGamepad::SetTriggerReleaseThreshold(float threshold) {
	triggerReleaseThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
}
```

**ファイルの一番下（`ApplyDeadzone` の下）に足す**：

```cpp
uint32_t XInputGamepad::MakeButtons(int index) const {
	if (!IsConnected(index)) {
		return 0; // 抜けている間は何も押していない（差し直したとき、押したままのボタンは「押した瞬間」になる）
	}
	const ControllerState& controller = controllers_[index];
	uint32_t buttons = controller.state.Gamepad.wButtons;
	// --- トリガー：引いた量をしきい値で判定して、ボタンの印に足す ---
	if (IsTriggerHeld(GetLeftTrigger(index), (controller.buttonsPrev & GamepadButton::LeftTrigger) != 0)) {
		buttons |= GamepadButton::LeftTrigger;
	}
	if (IsTriggerHeld(GetRightTrigger(index), (controller.buttonsPrev & GamepadButton::RightTrigger) != 0)) {
		buttons |= GamepadButton::RightTrigger;
	}
	return buttons;
}

bool XInputGamepad::IsTriggerHeld(float amount, bool wasPressed) const {
	// 押していない間は押すしきい値、押している間は離すしきい値で比べる（離す方が大きくされていても、押す方を超えない）
	const float threshold = wasPressed ? (std::min)(triggerReleaseThreshold_, triggerPressThreshold_) : triggerPressThreshold_;
	// 0 は「引いていない」（デッドゾーンの中も 0）。しきい値を 0 にしても、触っていないのに押した扱いにはしない
	return amount > 0.0f && amount >= threshold;
}
```

- `(std::min)` と括弧で囲むのは、Windows.h の `min` マクロとぶつからないように（CLAUDE.md の std::max と同じ）。
- `MakeButtons` は `GetLeftTrigger`（デッドゾーンを引いた量）を使うので、量と判定がずれない。

### A-3 `Input/InputManager.h`

**ボタンの3つ**（`IsGamepadConnected` の下）を差し替え：

```cpp
	/// <summary>
	/// 指定ボタンが押されているか。button は GamepadButton::***
	/// <para>GamepadButton::LeftTrigger・RightTrigger で、トリガーをしきい値（SetTriggerPressThreshold / SetTriggerReleaseThreshold）で判定したボタンとして読める</para>
	/// </summary>
	static bool IsButtonPressed(uint32_t button, int index = 0);

	/// <summary>指定ボタンを押した瞬間か。トリガーは、押すしきい値まで引いたフレーム</summary>
	static bool IsButtonTriggered(uint32_t button, int index = 0);

	/// <summary>指定ボタンを離した瞬間か。トリガーは、離すしきい値より戻したフレーム</summary>
	static bool IsButtonReleased(uint32_t button, int index = 0);
```

**トリガーの量の2つ**のコメントに、ボタンとして読む方を書き足す：

```cpp
	/// <summary>左トリガー (0.0f=未押し, 1.0f=最大)。押した瞬間を読むなら IsButtonTriggered(GamepadButton::LeftTrigger)</summary>
	static float GetLeftTrigger(int index = 0);

	/// <summary>右トリガー (0.0f=未押し, 1.0f=最大)。押した瞬間を読むなら IsButtonTriggered(GamepadButton::RightTrigger)</summary>
	static float GetRightTrigger(int index = 0);
```

**`static float GetTriggerDeadzone();` の下に足す**：

```cpp

	// ---- トリガーをボタンとして読むときのしきい値 (0.0f〜1.0f。デッドゾーン処理後の量。既定は押す 0.5・離す 0.4) ----
	// 2つに分けるのは、しきい値の近くで指を止めたときに、押した・離したが何度も出ないように（離す方を小さくする）
	static void SetTriggerPressThreshold(float threshold);   // この量以上引いたら押した
	static void SetTriggerReleaseThreshold(float threshold); // 押している間は、この量より戻したら離した
	static float GetTriggerPressThreshold();
	static float GetTriggerReleaseThreshold();
```

### A-4 `Input/InputManager.cpp`

**ボタンの3つ**の引数を `uint32_t` に：

```cpp
bool InputManager::IsButtonPressed(uint32_t btn, int i) { return !IsGamepadBlocked() && GetInstance()->gamepad_.IsButtonPressed(btn, i); }
bool InputManager::IsButtonTriggered(uint32_t btn, int i) { return !IsGamepadBlocked() && GetInstance()->gamepad_.IsButtonTriggered(btn, i); }
bool InputManager::IsButtonReleased(uint32_t btn, int i) { return !IsGamepadBlocked() && GetInstance()->gamepad_.IsButtonReleased(btn, i); }
```

**`GetTriggerDeadzone` の下（`// ---- まとめて読む ----` の上）に足す**：

```cpp

// ---- トリガーのしきい値 ----
void InputManager::SetTriggerPressThreshold(float t) { GetInstance()->gamepad_.SetTriggerPressThreshold(t); }
void InputManager::SetTriggerReleaseThreshold(float t) { GetInstance()->gamepad_.SetTriggerReleaseThreshold(t); }
float InputManager::GetTriggerPressThreshold() { return GetInstance()->gamepad_.GetTriggerPressThreshold(); }
float InputManager::GetTriggerReleaseThreshold() { return GetInstance()->gamepad_.GetTriggerReleaseThreshold(); }
```

ウィンドウが手前に無いとき・ImGui に文字を打っているときに「何も押していない」になるのは、トリガーも今のボタンと同じ（`IsGamepadBlocked` を通るので）。

## パートB ポストエフェクト：彩度（Saturation）

### B-1 `Shader/Data/Post/ColorAdjust.hlsli.shader`（新しいファイル）

```hlsl
#META
stage: Include
#META_END

#HLSL
// 色の調整（Unity の Color Adjustments の小さい版）
// どれもリニアの値を受け取って、リニアの値を返す。トーンマップの前（HDR のまま）に掛ける（Unity と同じ順）

// 明るさの重み（Rec.709。人の目は緑を一番明るく、青を一番暗く感じる。リニアの値に掛ける）
static const float32_t3 kLuminanceWeight = float32_t3(0.2126f, 0.7152f, 0.0722f);

// ===== 明るさ（色を抜いた灰色の明るさ）=====
float Luminance(float32_t3 color)
{
    return dot(color, kLuminanceWeight);
}

// ===== 彩度：1 でそのまま・0 で白黒（同じ明るさの灰色）・1 より大きいと色が濃くなる =====
// 灰色と元の色の間を saturation で混ぜる。1 を超えると灰色から離れる向きへ行き過ぎるので、マイナスにならないように 0 で止める
float32_t3 ApplySaturation(float32_t3 color, float saturation)
{
    float32_t3 gray = Luminance(color).xxx;
    return max(lerp(gray, color, saturation), 0.0f);
}
#HLSL_END
```

`Luminance(color).xxx` は、1つの値を3つ並べた `float32_t3`（スウィズル。`(Y, Y, Y)`）。

### B-2 `Shader/Data/Post/Lens.PS.shader`

**include に足す**：

```hlsl
#include "Resources/Texture.hlsli"
#include "Post/ToneMap.hlsli"
#include "Post/ColorAdjust.hlsli"
```

**`LensParam` の最後に足す**（`toneMap` の下）：

```hlsl
    uint32_t toneMap; // トーンマップの種類（ToneMap.hlsli の kToneMap〇〇）
    float saturation; // 彩度（1 でそのまま・0 で白黒）
};
```

**露出・トーンマップの所を差し替え**：

```hlsl
     // ===== 露出・彩度・トーンマップ：HDR の明るさを、画面の 0〜1 に収める =====
    // ここまでは HDR のまま（明るい所ほど色収差・放射ブラーの筋も明るい）。風の線・ビネットは収めた後に掛ける（今までの見た目と同じ）
    // 彩度は露出の後・トーンマップの前（Unity の Color Adjustments と同じ順。白黒でもトーンマップの明るさの付き方は同じ）
    color = ApplySaturation(color * gLens.exposure, gLens.saturation);
    color = ApplyToneMap(color, gLens.toneMap);
```

（前は `color = ApplyToneMap(color * gLens.exposure, gLens.toneMap);` の1行。）

### B-3 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

Visual Studio のソリューションエクスプローラーで `HTTEngine/Shader/Data/Post` に ColorAdjust.hlsli.shader を足す（既存の項目の追加）。手で書くなら、`BloomUp.PS.shader` の下に：

`.vcxproj`：
```xml
    <None Include="HTTEngine\Shader\Data\Post\ColorAdjust.hlsli.shader" />
```

`.vcxproj.filters`：
```xml
    <None Include="HTTEngine\Shader\Data\Post\ColorAdjust.hlsli.shader">
      <Filter>HTTEngine\Shader\Data\Post</Filter>
    </None>
```

### B-4 `Graphics/PostEffect/LensPass.h`

**クラスの説明の1行目**（順番に彩度を足す）：

```cpp
/// 最後のパス（HDR の絵 → 画面）。レンズの効果（歪み・色収差・放射ブラー）→ 露出・彩度・トーンマップ → ビネットの順に掛ける
```

**`kExposure` の下に足す**：

```cpp

	// --- 色の調整（GlobalVariables から入る。レンズを切っていても掛ける。ポストエフェクトを全部切ったビューだけ外す）---
	float kSaturation = 1.0f; // 彩度：1 でそのまま・0 で白黒（同じ明るさの灰色）・1 より大きいと色が濃くなる
```

**`Render` を差し替え**（bool を2つに）：

```cpp
	/// <summary>
	/// source を読んで、今バインドされている描画先へ描く
	/// </summary>
	/// <param name="useLens">false ならレンズの効果（歪み・色収差・放射ブラー・ビネット・風の線）を掛けない</param>
	/// <param name="useColorAdjust">false なら色の調整（彩度）を掛けない。露出・トーンマップはどちらでも掛ける</param>
	void Render(RenderTexture& source, const ViewRect& rect, bool useLens, bool useColorAdjust);
```

**`Param` の最後に足して、大きさを直す**：

```cpp
		uint32_t toneMap = 0;                     // トーンマップの種類（ToneMapMode の番号）
		float saturation = 1.0f;                  // 彩度（1 でそのまま・0 で白黒）
	};
	static_assert(sizeof(Param) == 52, "シェーダーの LensParam（16 バイト × 3 ＋ 4 バイト）と大きさをそろえる");
```

定数バッファは 16 バイトずつ並ぶので、シェーダーの `saturation` は4つ目の 16 バイトの頭に入る（C++ の 48 バイト目と同じ所）。1つのビューの置き場は 256 バイト（`kSlotSize`）なので、52 バイトでも足りる。

### B-5 `Graphics/PostEffect/LensPass.cpp`

**`Render` の頭を差し替え**（`if (enabled) {` まで）：

```cpp
void LensPass::Render(RenderTexture& source, const ViewRect& rect, bool useLens, bool useColorAdjust) {
	HTT_ASSERT_MSG(slot_ < kMaxPassPerFrame, "1フレームのレンズパス数が上限を超えました");
	auto* cmdList = DirectXCommon::GetCommandList();

	// --- パラメータを作る。切ってあるときはレンズの値が全部0・彩度が1＝露出・トーンマップだけ掛けたコピーになる ---
	Param param{};
	param.srcIndex = source.GetSRVSlot();
	param.exposure = kExposure;
	param.toneMap = static_cast<uint32_t>(toneMap);
	param.saturation = useColorAdjust ? kSaturation : 1.0f;
	if (useLens) {
```

（その下の `param.distortion = …` からは今のまま。）

### B-6 `Graphics/PostEffect/PostProcess.h`

**`PostEffectMode` の None のコメントと、クラスの説明の1行目**：

```cpp
	None,   // 掛けない（被写界深度・ブルーム・レンズ・色の調整を外す。露出・トーンマップだけは掛ける）
```

```cpp
/// ポストエフェクトをまとめて順に掛ける（被写界深度 → ブルーム → レンズ・露出・彩度・トーンマップ）。ブルーム・レンズ・色の調整・トーンマップの有無と数値は GlobalVariables で持つ
```

**`Prepare`・`Composite` を差し替え**（bool の代わりに mode を渡す）：

```cpp
	// cameraOverrides はカメラごとの上書き。mode が None ならブルームを掛けない（ポストエフェクトを切ったビュー）
	void Prepare(RenderTexture& scene, const PostOverrides& cameraOverrides, PostEffectMode mode = PostEffectMode::All);
	// mode が All のときだけレンズ、None 以外のとき色の調整を掛ける。どれでも露出・トーンマップは掛けて通す（Prepare と同じ mode を渡す）
	void Composite(const ViewRect& rect, PostEffectMode mode = PostEffectMode::All);
```

### B-7 `Graphics/PostEffect/PostProcess.cpp`

**`Initialize` のログ**（Saturation も出す）：

```cpp
	LogManager::Log(std::format("PostEffect Bloom={} Lens={} Threshold={:.2f} Intensity={:.2f} Vignette={:.2f} ToneMap={} Exposure={:.2f} Saturation={:.2f}", kBloomEnabled, kLensEnabled, bloom_.kThreshold,
	        bloom_.kIntensity, lens_.kVignette, static_cast<int>(lens_.toneMap), lens_.kExposure, lens_.kSaturation));
```

**`Prepare` の頭と、ブルームの if**：

```cpp
void PostProcess::Prepare(RenderTexture& scene, const PostOverrides& cameraOverrides, PostEffectMode mode) {
	ApplyGV(); // ImGuiでいじった値がその場で効くように毎フレーム反映する
	ApplyCameraOverrides(cameraOverrides);
	source_ = &scene;

	// --- ブルーム：明るい所を滲ませて work_ へ合成する ---
	if (mode != PostEffectMode::None && isBloomActive_) {
```

**`Composite` を差し替え**：

```cpp
void PostProcess::Composite(const ViewRect& rect, PostEffectMode mode) {
	// 切っていても最終出力として通す（HDR の絵を、露出・トーンマップで画面の 0〜1 に収めるのもここ）
	// 色の調整は Scene ビュー（NoLens）にも掛ける（色は絵の歪みと違い、ギズモ・クリックの位置とずれないので。Unity の Scene ビューと同じ）
	const bool useLens = (mode == PostEffectMode::All) && isLensActive_;
	const bool useColorAdjust = (mode != PostEffectMode::None);
	lens_.Render(*source_, rect, useLens, useColorAdjust);
}
```

**`RegisterGV` の最後（ToneMap の下）に足す**：

```cpp
	// 色の調整。Enabled は無い（Saturation 1 で何も変わらない＝切ったのと同じ）
	post.Category("ColorAdjust")
		.Add("Saturation", lens_.kSaturation);
```

**`ApplyGV` の最後（Exposure の下）に足す**：

```cpp

	lens_.kSaturation = gv->Get<float>(s, "ColorAdjust", "Saturation");
```

**`ApplyCameraOverrides` の最後（露出の下）に足す**：

```cpp
	// --- 色の調整（これも切る設定が無いので、いつも混ぜる）---
	lens_.kSaturation = camera.Apply(PostParam::Saturation, lens_.kSaturation);
```

### B-8 `Graphics/Renderer/SceneRenderer.cpp`

`RenderToTexture` の中の2行（mode をそのまま渡す）：

```cpp
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides(), mode); // 張り替える処理はここで全部済ませる
	target->PreDraw();                                                      // その後に描画先を張る
	instance_->postProcess_->Composite(rect, mode);
```

`RenderToWindow` の `Prepare(*scene, GetViewPostOverrides())`・`Composite(rect)` は、既定の All のまま変えない。

### B-9 `Graphics/PostEffect/PostOverride.h`

`PostParam` の最後に足す：

```cpp
	Exposure,       // 露出
	Saturation,     // 色の調整：彩度（1 でそのまま・0 で白黒）
};
```

`PostOverrides` の配列の大きさは `magic_enum::enum_count<PostParam>()` なので、足すだけで1つ増える。補間（`LerpPostOverrides`）も全部の項目を回すので、直す所は無い。

### B-10 `Components/Camera/CameraVolumeComponent.h`

`exposure` の下に足す：

```cpp
	CameraVolumeValue exposure = {false, 1.0f}; // 露出（2 で1段明るい・0.5 で1段暗い）
	// --- Color Adjust ---
	CameraVolumeValue saturation = {false, 1.0f}; // 彩度（1 でそのまま・0 で白黒・1 より大きいと色が濃くなる）
};
```

### B-11 `Systems/Camera/CameraVolume.cpp`

**`Describe` の最後（Exposure の下）に足す**：

```cpp
	// ===== 色の調整 =====
	ui.Separator("Color Adjust");
	DescribeItem(ui, "Saturation", "saturation", value.saturation, AtLeast(0.0f).Tip("彩度。1 でそのまま・0 で白黒（同じ明るさの灰色）・1 より大きいと色が濃くなる。UI には掛からない"));
```

**`MakeOverrides` の最後（Exposure の下）に足す**：

```cpp
	setItem(PostParam::Saturation, volume.saturation);
```

保存の名前は `saturationOverride`（チェック）と `saturation`（値）。前に保存したシーンには無いので、読むと既定（外した 1）になる。

## パートC Camera Look At：Screen Position

### C-1 `Components/Camera/CameraLookAtComponent.h`（全体を差し替え）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// カメラに注視点を見させる。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// 変えるのは写す向きだけで、Transform の向きは変えない（Camera Look At を外すと、置いた向きに戻る）
// 見る点は Screen Position の所に写る（既定は画面の真ん中）。カメラは横に傾けない（画面の上がいつもワールドの上の側）
// 停止中も見る向きが Game ビューに出る。なめらかに動くのは再生中だけ

/// <summary>
/// 注視点（Unity の Cinemachine の Rotation Composer の小さい版）
/// </summary>
struct CameraLookAtComponent {
	bool enabled = true;                   // false の間は Transform の向きのまま
	EntityRef target;                      // 見る Entity（Hierarchy からドラッグ）。空なら Offset をワールドの座標として見る
	Vector3 offset = {0.0f, 0.0f, 0.0f};   // 見る点のずれ（m。ワールドの向き）
	Vector2 screenPosition = {0.0f, 0.0f}; // 見る点を写す画面の位置（スプライトの position と同じ：真ん中が 0、右と上が＋、端が ±1）
	float smoothTime = 0.0f;               // 見る点が追いつくまでの時間の目安（秒。0 ならすぐ合わせる）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isAiming = false;                     // 見始めたか（false なら、次のフレームで見る点へすぐ合わせる）
	Vector3 currentPoint = {0.0f, 0.0f, 0.0f}; // 今見ている点（ワールド）
};

static_assert(std::is_trivially_copyable_v<CameraLookAtComponent>);
```

### C-2 `Systems/Camera/CameraLookAt.h`（新しいファイル）

```cpp
#pragma once
#include <optional>

#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Math/Quaternion.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Look At の中身（CameraSystem から使う）
/// </summary>
namespace CameraLookAt {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraLookAtComponent& value);

// 今見ている点を、見たい点へ deltaTime 秒ぶん近づける（再生中だけ呼ぶ）
void Update(CameraLookAtComponent& lookAt, float deltaTime);

// 今見る点（ワールド）。再生中はなめらかにした点、停止中は見たい点そのもの（Inspector で変えた値がすぐ見えるように）。見ないなら nullopt
std::optional<Vector3> GetPoint(const CameraLookAtComponent& lookAt, bool isGameRunning);

/// <summary>
/// 見る点が、画面の screenPosition に写るカメラの向きを作る。カメラは横に傾けない（画面の上がワールドの上の側）
/// </summary>
/// <param name="position">カメラの位置（ワールド）</param>
/// <param name="point">見る点（ワールド）</param>
/// <param name="screenPosition">写す画面の位置（真ん中が 0、右と上が＋、端が ±1）</param>
/// <param name="fovYDegrees">縦の視野角（度）</param>
/// <param name="aspect">画面の横 ÷ 縦</param>
/// <returns>見る点がカメラとほとんど同じ位置で、向きが決まらないなら nullopt</returns>
std::optional<Quaternion> CalcRotation(const Vector3& position, const Vector3& point, const Vector2& screenPosition, float fovYDegrees, float aspect);
} // namespace CameraLookAt
```

### C-3 `Systems/Camera/CameraLookAt.cpp`（新しいファイル）

`Describe`・`Update`・`GetPoint`・`FindPoint` は、CameraSystem.cpp の `DescribeCameraLookAt`・`UpdateLookAt`・`GetLookAtPoint`・`FindLookAtPoint` を移した物（Describe に Screen Position の1行を足した）。新しいのは `CalcRotation`（1.3）。

```cpp
#include "CameraLookAt.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kMinLookDistanceSq = 1e-6f; // 見る点がカメラとほとんど同じ位置なら、向きが決まらないので Transform の向きのままにする
// 上下に向ける角の上限（ラジアン）。真上・真下まで向けると、左右の向きが決まらなくなる
constexpr float kMaxPitch = std::numbers::pi_v<float> * 0.5f - 0.01f;

// ===== 見たい点（Target が居なくなったら nullopt）=====
std::optional<Vector3> FindPoint(const CameraLookAtComponent& lookAt) {
	if (!lookAt.target.IsSet()) {
		return lookAt.offset;
	}
	const TransformComponent* target = EntityManager::Get<TransformComponent>(lookAt.target);
	if (target == nullptr) {
		return std::nullopt;
	}
	return GetWorldPosition(*target) + lookAt.offset;
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraLookAt::Describe(ComponentUI& ui, CameraLookAtComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと Transform の向きのまま"));
	ui.Field("Target###target", value.target, Tip("見る Entity（Hierarchy からドラッグ）。空なら Offset をワールドの座標として見る"));
	ui.Field("Offset###offset", value.offset, Tip("見る点のずれ（m。ワールドの向き）"));
	ui.Field(
	    "Screen Position###screenPosition", value.screenPosition,
	    Range(-1.0f, 1.0f).Tip("見る点を写す画面の位置。真ん中が 0、右と上が＋、端が ±1（スプライトの position と同じ）。横 1/3 の所なら X を -0.333（左から 1/3）か 0.333（右から 1/3）"));
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("見る点が追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる（再生中だけ効く）"));
}

//=============================================================================
// 見る点
//=============================================================================
// ===== 今見ている点を、見たい点へ近づける =====
void CameraLookAt::Update(CameraLookAtComponent& lookAt, float deltaTime) {
	const std::optional<Vector3> point = FindPoint(lookAt);
	if (!lookAt.enabled || !point.has_value()) {
		lookAt.isAiming = false;
		return;
	}
	if (!lookAt.isAiming) {
		lookAt.currentPoint = *point; // 見始め：カメラの向きが置いた向きから流れないように、すぐ合わせる
		lookAt.isAiming = true;
		return;
	}
	lookAt.currentPoint += (*point - lookAt.currentPoint) * MathUtility::CalcSmoothRate(deltaTime, lookAt.smoothTime);
}

// ===== 今見る点 =====
std::optional<Vector3> CameraLookAt::GetPoint(const CameraLookAtComponent& lookAt, bool isGameRunning) {
	if (!lookAt.enabled) {
		return std::nullopt;
	}
	if (isGameRunning && lookAt.isAiming) {
		return lookAt.currentPoint;
	}
	return FindPoint(lookAt);
}

//=============================================================================
// 向き
//=============================================================================
// 考え方：カメラの向きを「左右（ヨー）」と「上下（ピッチ）」の2つの角に分けて、上下 → 左右の順に1つずつ合わせる（横の傾き＝ロールは 0）
// 1. カメラから見て、見る点がどの向きにあれば画面の screenPosition に写るか（カメラの右・上・前で表した向き u）を、視野角から出す
// 2. 上下：カメラを上下に向けても、u の左右の成分は変わらない。上下だけ回して、u のワールドの高さを、見る点の向き w の高さにそろえる
// 3. 左右：ワールドの上の軸のまわりに回しても、高さは変わらない。真上から見た角を、w の角にそろえる
// どちらの回転も、もう一方で合わせた物をくずさないので、1回ずつで合う
std::optional<Quaternion> CameraLookAt::CalcRotation(const Vector3& position, const Vector3& point, const Vector2& screenPosition, float fovYDegrees, float aspect) {
	const Vector3 toPoint = point - position;
	const float distanceSq = LengthSq(toPoint);
	if (distanceSq <= kMinLookDistanceSq) {
		return std::nullopt;
	}
	// 真ん中なら、見る向きをそのまま前にする（今までと同じ計算。誤差も出さない）
	if (screenPosition.x == 0.0f && screenPosition.y == 0.0f) {
		return MakeLookRotation(toPoint);
	}
	const Vector3 w = toPoint / std::sqrt(distanceSq); // 見る点の向き（ワールド。長さ1）

	// --- 1. 画面の位置 → カメラから見た向き u（右・上・前。前が 1 の所の、画面の端までの長さが tan(視野角の半分)）---
	// 写すときの視野角と同じ範囲に収める（CameraSystem の ApplyProjection と同じ）
	const float fovY = std::clamp(fovYDegrees, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
	const float tanHalfY = std::tan(fovY * 0.5f);
	const float tanHalfX = tanHalfY * aspect;
	const Vector3 u = Normalize(Vector3{screenPosition.x * tanHalfX, screenPosition.y * tanHalfY, 1.0f});

	// --- 2. 上下：u を上・前の面で見ると、長さ r・カメラの前から上へ alpha の角。ピッチ pitch だけ上へ向けると、ワールドの高さは r × sin(alpha + pitch) ---
	// これが w の高さ w.y になる pitch を求める（左右へ大きくずらした位置に、真上近くの点は写せないので、届かない分は止める）
	const float r = std::sqrt(u.y * u.y + u.z * u.z);
	const float alpha = std::atan2(u.y, u.z);
	const float elevation = std::asin(std::clamp(w.y / r, -1.0f, 1.0f)); // alpha + pitch（u のワールドでの仰角）
	const float pitch = std::clamp(elevation - alpha, -kMaxPitch, kMaxPitch);

	// --- 3. 左右：上下を合わせた u を真上から見た角（前から右へ）を、w を真上から見た角にそろえる ---
	// 真上から見た u は（右 u.x、前 r × cos(alpha + pitch)）。角は atan2(右, 前)
	const float pointYaw = std::atan2(w.x, w.z);
	const float offsetYaw = std::atan2(u.x, r * std::cos(alpha + pitch));
	const float yaw = pointYaw - offsetYaw;

	// --- カメラの前の向きにして、向きを作る（MakeLookRotation はワールドの上を上にする＝横に傾けない）---
	const Vector3 forward = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
	return MakeLookRotation(forward);
}
```

- `std::atan2(y, x)`：点 (x, y) の角（ラジアン）を、4つの向き全部で正しく返す（`atan(y / x)` は x が 0 で割れず、左右の区別が無い）。
- `std::asin` に渡す値は、誤差で 1 をわずかに超えると NaN になるので、`std::clamp` で -1〜1 に収める。

### C-4 `Systems/Camera/CameraSystem.cpp`

**include に足す**（CameraFollow の下）：

```cpp
#include "HTTEngine/Systems/Camera/CameraFollow.h"
#include "HTTEngine/Systems/Camera/CameraLookAt.h"
#include "HTTEngine/Systems/Camera/CameraShake.h"
```

**無名 namespace の頭の `kMinLookDistanceSq` の行を消す**（CameraLookAt.cpp へ移した）：

```cpp
constexpr float kMinLookDistanceSq = 1e-6f;            // 見る点がカメラとほとんど同じ位置なら、向きが決まらないので Transform の向きのままにする
```

**次の4つを丸ごと消す**（CameraLookAt.cpp へ移した）：`// ===== Camera Look At =====` の `DescribeCameraLookAt` と、その下の大見出し「注視点」の `FindLookAtPoint`・`UpdateLookAt`・`GetLookAtPoint`。`DescribeCamera` の閉じ括弧の次が、すぐ「揺れのプレビュー」の大見出しになる。

**`EvaluateCamera` の「注視点」の所を差し替え**：

```cpp
	// --- 注視点：向きだけ置き換える（見る点が Screen Position の所に写る向き）---
	std::optional<Vector3> lookAtPoint;
	if (const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity)) {
		lookAtPoint = CameraLookAt::GetPoint(*lookAt, isGameRunning);
		if (lookAtPoint.has_value()) {
			// 縦横の比は写すときと同じ（ApplyProjection）。視野角はこのカメラの物（切り替えの補間はこの後なので、補間の途中でも、それぞれのカメラの絵で合う）
			const std::optional<Quaternion> rotation = CameraLookAt::CalcRotation(output.position, *lookAtPoint, lookAt->screenPosition, output.fovY, GameScreen::GetAspectRatio());
			if (rotation.has_value()) {
				output.rotation = *rotation;
			}
		}
	}
```

（前は `lookAtPoint = GetLookAtPoint(…)` の後に `if (lookAtPoint.has_value() && LengthSq(…) > kMinLookDistanceSq) { output.rotation = MakeLookRotation(…); }` だった所。ポストエフェクトのピント（`CalcVolumeFocusDistance`）は今まで通り `lookAtPoint` を使う。）

**`UpdateCameraMotion` の中**：

```cpp
			CameraLookAt::Update(lookAt, GetCameraDeltaTime(entity));
```

（前は `UpdateLookAt(lookAt, …)`。）

**`CameraSystem::Initialize` の登録**：

```cpp
	EntityManager::RegisterComponent<CameraLookAtComponent>();
	ComponentSerializer::Register<CameraLookAtComponent>("Camera Look At", &CameraLookAt::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraLookAtComponent>>("Camera Look At", "Camera", &CameraLookAt::Describe));
```

### C-5 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

ソリューションエクスプローラーで `HTTEngine/Systems/Camera` に CameraLookAt.h・.cpp を足す。手で書くなら CameraFollow の下に：

`.vcxproj`：
```xml
    <ClCompile Include="HTTEngine\Systems\Camera\CameraLookAt.cpp" />
```
```xml
    <ClInclude Include="HTTEngine\Systems\Camera\CameraLookAt.h" />
```

`.vcxproj.filters`：
```xml
    <ClCompile Include="HTTEngine\Systems\Camera\CameraLookAt.cpp">
      <Filter>HTTEngine\Systems\Camera</Filter>
    </ClCompile>
```
```xml
    <ClInclude Include="HTTEngine\Systems\Camera\CameraLookAt.h">
      <Filter>HTTEngine\Systems\Camera</Filter>
    </ClInclude>
```

`Systems/Camera` のファイルは 12 個（15 より少ない）。

## パートD Sandbox で確かめる

### D-1 `Sandbox/Source/Scripts/TriggerCameraSamples.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/ECS/GameObject.h"
#include "HTTEngine/ECS/Script.h"
#include "HTTEngine/Math/Vector3.h"

//=============================================================================
// トリガーでカメラを切り替える（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// RT（キーボードなら Enter）を押した瞬間に、Other Camera の Priority を上げ下げして、カメラを切り替える（Sandbox のテスト用）
/// <para>切り替えは Other Camera の Camera の Blend で補間する。Camera Look At の Screen Position の向き・Camera Volume の Saturation も同じ速さで変わるのを見る</para>
/// <para>LT・RT を押した瞬間・離した瞬間を、そのときの量と一緒に Console に出す（しきい値は既定で押す 0.5・離す 0.4）</para>
/// </summary>
class TriggerCameraSwitcher : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	GameObject otherCamera_;    // 切り替える先のカメラ（Camera Component を付けた Entity）
	int switchedPriority_ = 10; // 切り替えている間の Other Camera の Priority（今のカメラより大きくする）

	// ===== 実行中だけの値 =====
	int basePriority_ = 0;    // Start のときの Other Camera の Priority（戻すときの値）
	bool isSwitched_ = false; // Other Camera へ切り替えているか
};


//=============================================================================
// 左右に往復する（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// Entity をワールドの X の向きに往復させる（Sandbox のテスト用。Camera Look At の Smooth Time で遅れて追うのを見る）
/// </summary>
class SideSwing : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float width_ = 3.0f;  // 真ん中から左右へ動く幅（m）
	float period_ = 4.0f; // 1往復の秒

	// ===== 実行中だけの値 =====
	Vector3 center_ = {0.0f, 0.0f, 0.0f}; // 往復の真ん中（Start のときの位置）
	float time_ = 0.0f;                   // 動き始めてからの秒
};
```

### D-2 `Sandbox/Source/Scripts/TriggerCameraSamples.cpp`（新しいファイル）

```cpp
#include "Source/Scripts/TriggerCameraSamples.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Time/EngineTime.h"

SCRIPT(TriggerCameraSwitcher, "Test");
SCRIPT(SideSwing, "Test");

namespace {
// ===== トリガーを押した瞬間・離した瞬間を Console に出す =====
void LogTrigger(const char* name, uint32_t button, float amount) {
	if (InputManager::IsButtonTriggered(button)) {
		LogManager::Log(std::format("{} を押した（量 {:.2f}）", name, amount));
	}
	if (InputManager::IsButtonReleased(button)) {
		LogManager::Log(std::format("{} を離した（量 {:.2f}）", name, amount));
	}
}
} // namespace


//=============================================================================
// TriggerCameraSwitcher
//=============================================================================
// ===== 初期化 =====
void TriggerCameraSwitcher::Start() {
	if (const CameraComponent* camera = otherCamera_.GetComponent<CameraComponent>()) {
		basePriority_ = camera->priority;
	}
}

// ===== 更新 =====
void TriggerCameraSwitcher::Update() {
	// --- LT・RT：押した瞬間・離した瞬間（量も出して、しきい値で判定しているのを見る）---
	LogTrigger("LT", GamepadButton::LeftTrigger, InputManager::GetLeftTrigger());
	LogTrigger("RT", GamepadButton::RightTrigger, InputManager::GetRightTrigger());

	// --- RT か Enter を押した瞬間：Other Camera の Priority を上げ下げして切り替える ---
	if (!InputManager::IsButtonTriggered(GamepadButton::RightTrigger) && !InputManager::IsKeyTriggered(DIK_RETURN)) {
		return;
	}
	CameraComponent* camera = otherCamera_.GetComponent<CameraComponent>();
	if (camera == nullptr) {
		LogManager::Log("TriggerCameraSwitcher：Other Camera に Camera Component が付いた Entity を入れてください");
		return;
	}
	isSwitched_ = !isSwitched_;
	camera->priority = isSwitched_ ? switchedPriority_ : basePriority_;
}

// ===== Inspector に出す・保存する値 =====
void TriggerCameraSwitcher::Fields(ComponentUI& ui) {
	ui.Field("Other Camera###otherCamera", otherCamera_, Tip("切り替える先のカメラ（Camera Component を付けた Entity。Hierarchy からドラッグ）"));
	ui.Field("Switched Priority###switchedPriority", switchedPriority_, Tip("切り替えている間の Other Camera の Priority（今のカメラより大きくする）"));
}


//=============================================================================
// SideSwing
//=============================================================================
// ===== 初期化 =====
void SideSwing::Start() {
	center_ = transform.translation;
}

// ===== 更新 =====
void SideSwing::Update() {
	time_ += Time::GetDeltaTime();
	// 1往復で sin が1回まわる（2π）。period_ が 0 に近いと割れないので、少しだけ下を止める
	const float phase = 2.0f * std::numbers::pi_v<float> * time_ / (std::max)(period_, 0.01f);
	transform.translation.x = center_.x + width_ * std::sin(phase);
}

// ===== Inspector に出す・保存する値 =====
void SideSwing::Fields(ComponentUI& ui) {
	ui.Field("Width###width", width_, AtLeast(0.0f).Tip("真ん中から左右へ動く幅（m）"));
	ui.Field("Period###period", period_, AtLeast(0.01f).Tip("1往復の秒"));
}
```

### D-3 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

ソリューションエクスプローラーで Sandbox の `Source/Scripts` に TriggerCameraSamples.h・.cpp を足す。手で書くなら BurstEffectSamples の下に：

`.vcxproj`：
```xml
    <ClCompile Include="Source\Scripts\TriggerCameraSamples.cpp" />
```
```xml
    <ClInclude Include="Source\Scripts\TriggerCameraSamples.h" />
```

`.vcxproj.filters`：
```xml
    <ClCompile Include="Source\Scripts\TriggerCameraSamples.cpp">
      <Filter>Source\Scripts</Filter>
    </ClCompile>
```
```xml
    <ClInclude Include="Source\Scripts\TriggerCameraSamples.h">
      <Filter>Source\Scripts</Filter>
    </ClInclude>
```

### D-4 `Sandbox/Resources/scenes/TriggerSaturationLookAtTest.scene.json`（新しいファイル）

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
				"Transform": {"translation": [-2.0, 3.0, -9.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 50.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 0, "blendStyle": "Ease", "blendTime": 0.8, "blendEase": "InOutCubic"},
				"Camera Look At": {"enabled": true, "target": 4, "offset": [0.0, 0.5, 0.0], "screenPosition": [-0.333, 0.0], "smoothTime": 0.3}
			}
		},
		{
			"id": 2,
			"name": "Gray Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [5.0, 2.0, -6.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 40.0, "nearZ": 0.1, "farZ": 1000.0, "priority": -1, "blendStyle": "Ease", "blendTime": 0.8, "blendEase": "InOutCubic"},
				"Camera Look At": {"enabled": true, "target": 4, "offset": [0.0, 0.5, 0.0], "screenPosition": [0.333, 0.2], "smoothTime": 0.3},
				"Camera Volume": {"enabled": true, "weight": 1.0, "saturationOverride": true, "saturation": 0.0}
			}
		},
		{
			"id": 3,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, 0.0], "rotation": [50.0, -30.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Directional Light": {"color": [1.0, 1.0, 1.0]}
			}
		},
		{
			"id": 4,
			"name": "Target",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 0.5, 2.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [1.0, 0.45, 0.1, 1.0]},
				"SideSwing": {"width": 3.0, "period": 4.0}
			}
		},
		{
			"id": 5,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, -0.5, 5.0], "rotation": [0.0, 0.0, 0.0], "scale": [16.0, 1.0, 24.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [0.2, 0.55, 0.3, 1.0]}
			}
		},
		{
			"id": 6,
			"name": "Pillar",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [-4.0, 1.5, 6.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 4.0, 1.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [0.2, 0.4, 1.0, 1.0]}
			}
		},
		{
			"id": 7,
			"name": "Switcher",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"TriggerCameraSwitcher": {"otherCamera": 2, "switchedPriority": 10}
			}
		},
		{
			"id": 8,
			"name": "Red UI",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [40.0, -40.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Image": {"color": [1.0, 0.0, 0.0, 1.0], "size": [120.0, 120.0], "anchor": "TopLeft", "pivot": [0.0, 1.0]}
			}
		}
	]
}
```

### D-5 確かめ方

Sandbox を起動して、Project の `resources/scenes` から **TriggerSaturationLookAtTest** を開き、Play。

1. **Screen Position**：Main Camera の絵で、橙の箱（Target）が画面の**横 1/3（左から）**の高さ真ん中に写る。箱は左右に往復し、Smooth Time 0.3 で少し遅れて追う。停止中に Main Camera の Camera Look At の Screen Position を動かすと、Game ビューがすぐ変わる
2. **トリガー**：パッドの RT を少しずつ引く。半分（0.5）を越えたフレームに Console へ「RT を押した（量 0.5〜）」が1回出る。0.4〜0.5 の間で止めても何も出ない。0.4 より戻すと「RT を離した」。LT も同じ
3. **彩度と補間**：RT を押した瞬間（キーボードなら Enter）に Gray Camera へ 0.8 秒で切り替わる。向き（箱が右 1/3・少し上へ）と一緒に、色が少しずつ抜けて白黒になる。左上の赤い四角（UI）は赤いまま。もう一度押すと戻る
4. **全体の設定**：Settings → PostEffect → ColorAdjust → Saturation を 0 にすると、Main Camera でも白黒（Scene ビューも）。2 にすると色が濃くなる。View → Scene View → Post Effects を切ると、Scene ビューだけ色が戻る。確かめたら 1 に戻す（Save しない）
5. **ディザ**：Gray Camera の白黒の絵で、下の地面に大きな楕円の線（帯の境目）が見えない。Settings → PostEffect → ToneMap → Dither を 0 にすると楕円が見え、1 に戻すと消える（確かめたら 1 のまま。Save しない）

## パートE ディザ：ビネットの暗さが楕円の帯に見えないようにする（2026-11-09 に足した）

パートA〜D を入れた後に、白黒の絵の下の地面に「うっすら大きな楕円」が見えた。画素を読むと、明るさが 82 → 81 → 80 → 79 と広い平らな帯ごとに1段ずつ変わり、画面の真ん中を中心に左右対称だった＝**ビネットのなめらかな暗さが、画面の 256 段（8 ビット）に丸められた帯の境目**。色があると色の違いに紛れるが、白黒だと平らな所で境目だけが見える（彩度の作業で入った物ではなく、前からあった物）。

直し方：最後のパス（Lens.PS）の一番最後、画面に書く直前に、**1 段より小さい粒（ディザ）**を足す。境目の近くで上の段・下の段が細かく混ざり、目には間の明るさに見える（Unreal のトーンマップの Grain Quantization と同じ考え。Unity はカメラの Dithering）。

- 粒は sRGB の値（画面に書く値。丸めはここで起きる）で ±1 段の三角形の分布。R・G・B に同じ粒を足すので、白黒の絵は灰色のまま
- 粒の並びは Interleaved Gradient Noise（画素の位置だけから作る。テクスチャが要らず、隣どうしが似ない）
- Settings → PostEffect → ToneMap → **Dither**（0/1、初めは 1）。画面へ書くときの事なので、レンズ・色の調整を切ったビューでも掛ける
- 確かめたこと：写しで3構成のテスト（89/89/88 項目）が合格。同じ所の明るさが 81〜83 で細かく混ざり（R・G・B は同じ）、明るさを 13 倍に強調した絵で、楕円の段々がなめらかな変化になった
- エディタの Game ビューは絵を縮めて出すので粒が少し平均され、わずかに帯が残ることがある（製品版・等倍では消える）

### E-1 `Shader/Data/Post/Dither.hlsli.shader`（新しいファイル）

```hlsl
#META
stage: Include
#META_END

#HLSL
// ディザ：画面（8 ビット。0〜255 の 256 段）へ書く前に、1 段より小さい粒を足して、なめらかな明るさの変化が帯（等高線）に見えないようにする
// ビネット・空・霧のような、広い所でゆっくり変わる明るさは、256 段に丸めると「同じ値の広い帯」と「1 段の境目」になり、境目が線に見える（白黒だと特に目立つ）
// 粒を足すと、境目の近くで上の段・下の段が細かく混ざるので、目には間の明るさに見える（Unreal のトーンマップの Grain Quantization と同じ考え）

// ===== リニア ⇔ sRGB（画面の _SRGB の形と同じ式）=====
// 丸めは sRGB の値（画面に書く値）で起きるので、粒は sRGB の値で 1 段分にそろえて足す
float32_t3 LinearToSrgb(float32_t3 color)
{
    float32_t3 low = color * 12.92f;
    float32_t3 high = 1.055f * pow(max(color, 0.0f), 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(color, 0.0031308f)); // step(a, b) は b >= a なら 1（しきい値以下の所は low）
}

float32_t3 SrgbToLinear(float32_t3 color)
{
    float32_t3 low = color / 12.92f;
    float32_t3 high = pow(max((color + 0.055f) / 1.055f, 0.0f), 2.4f);
    return lerp(high, low, step(color, 0.04045f));
}

// ===== 画素ごとの 0〜1 の数（Interleaved Gradient Noise。テクスチャを使わず、隣どうしが似ない並びになる）=====
float InterleavedGradientNoise(float32_t2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float32_t2(0.06711056f, 0.00583715f))));
}

// ===== 粒を足す。color はリニアの 0〜1、pixel は画面の画素の位置（SV_Position の xy）=====
// 粒は 2 つの数を足して 1 を引いた -1〜1（三角形の分布。真ん中が多いので、1 つの数より粒が目立たない）× 1 段（1/255）
// R・G・B に同じ粒を足す（灰色は灰色のまま。色ごとに違う粒だと、白黒の絵に色の点が出る）
float32_t3 ApplyDither(float32_t3 color, float32_t2 pixel)
{
    float noise = InterleavedGradientNoise(pixel) + InterleavedGradientNoise(pixel + float32_t2(113.0f, 71.0f)) - 1.0f;
    float32_t3 srgb = LinearToSrgb(saturate(color)) + noise / 255.0f;
    return SrgbToLinear(saturate(srgb));
}
#HLSL_END
```

`lerp(high, low, step(color, しきい値))` は、R・G・B それぞれで「しきい値以下なら low、より大きければ high」を選ぶ（HLSL の版によって、ベクトルの `? :` が使えない・`select` が無いので、どの版でも通る形にした）。

### E-2 `Shader/Data/Post/Lens.PS.shader`

include に `#include "Post/Dither.hlsli"`（ColorAdjust の下）、`LensParam` の最後（`saturation` の下）に：

```hlsl
    uint32_t dither;  // 1 なら画面へ書く前に粒を足して、明るさの帯（等高線）を消す
```

ビネットの後・`return` の前に：

```hlsl
    // ===== ディザ：画面の 256 段に丸める前に、1 段より小さい粒を足す（ビネットのようななめらかな暗さが、楕円の帯に見えないように）=====
    // 一番最後（この後に明るさを変える物が無い所）で足す。粒の並びは画面の画素の位置で決まる（毎フレーム同じ並び）
    if (gLens.dither != 0)
    {
        color = ApplyDither(color, input.position.xy);
    }
```

### E-3 `Graphics/PostEffect/LensPass.h`・`.cpp`

`kSaturation` の下に：

```cpp

	// --- 画面へ書くとき（GlobalVariables から入る。どのビューでも掛ける）---
	bool dither = true; // 画面の 256 段に丸める前に、1 段より小さい粒を足して、なめらかな明るさの変化が帯（等高線）に見えないようにする
```

`Param` の最後と大きさ：

```cpp
		uint32_t dither = 1;                      // 1 なら画面へ書く前に粒を足す
	};
	static_assert(sizeof(Param) == 56, "シェーダーの LensParam（16 バイト × 3 ＋ 8 バイト）と大きさをそろえる");
```

`Render` の `param.saturation = …` の下に：

```cpp
	param.dither = dither ? 1u : 0u; // 画面へ書くときの事なので、レンズ・色の調整を切ったビューでも掛ける
```

### E-4 `Graphics/PostEffect/PostProcess.h`・`.cpp`

`kLensEnabled` の下に：

```cpp
	int kDitherEnabled = 1; // 画面へ書く前に粒を足して、明るさの帯を消すか（LensPass::dither へ渡す）
```

`RegisterGV` の ToneMap：

```cpp
	// Dither は画面の 256 段に丸める前に粒を足して、ビネット・空のようななめらかな明るさが帯（等高線）に見えないようにする（0/1。初めは 1）
	post.Category("ToneMap")
		.Add("Mode", GlobalVariables::MakeEnumCombo(lens_.toneMap))
		.Add("Exposure", lens_.kExposure)
		.Add("Dither", kDitherEnabled);
```

`ApplyGV` の Exposure の下に：

```cpp
	kDitherEnabled = gv->Get<int32_t>(s, "ToneMap", "Dither");
	lens_.dither = (kDitherEnabled != 0);
```

### E-5 `HTTEngine.vcxproj`・`.filters`

ColorAdjust.hlsli.shader の下に `HTTEngine\Shader\Data\Post\Dither.hlsli.shader` を同じ形で足す。
