参考：
- Cinemachine の Position Composer（Dead Zone・Soft Zone・Screen Position。範囲は画面の割合で、Game ビューに線で出る）：https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachinePositionComposer.html
- Unity の Particle System の Shape モジュール（Scene ビューに形の線が出る）：https://docs.unity3d.com/Manual/PartSysShapeModule.html
- 前の md：`Docs/Tasks/VirtualCamera.md`（Camera Follow を作った時）・`Docs/Tasks/TriggerSaturationScreenPosition.md`（Camera Look At の Screen Position。画面の座標の決まり）・`Docs/Tasks/GizmoLineBackground.md`（ギズモの線をトーンマップの後に描く `isGizmo`）
- この作業のきっかけ：Camera Follow の遊びを画面の割合でも決めたい・Soft Zone が欲しい・範囲を線で見たい。Particle の出る形も線で見たい

# Camera Follow の Zone Space・Soft Zone と、範囲の線（Particle の出る形も）

- **パートA Camera Follow の計算**（`CameraFollowComponent.h`・`CameraFollow.h`・`CameraFollow.cpp`）：Dead Zone・Soft Zone を **World（m）か Screen（画面の割合）** で測れるようにする（**Zone Space**）。**Max Lag の名前を Soft Zone に変える**（意味は Cinemachine の Soft Zone と同じ）。線に使う「今の範囲をワールドの点で表した物」を返す `CameraFollow::CalcGuide` を足す
- **パートB Camera Follow の線**（`CameraGizmo.h`・`CameraGizmo.cpp`・`ImGuiManager.cpp`・`RenderStates.h`・`SceneRenderer.h/.cpp`・`EditorViewport.cpp`）：Dead Zone（緑）・Soft Zone（青）・focus（黄）・Bounds（赤）を線で出す。Screen の範囲は Game ビューだけ（新しい層 `DrawLayer::GameViewGuide`）。Component の **Show Gizmo** と、View メニューの **Camera Follow Zones**
- **パートC Particle の出る形の線**（`ParticleSpawn.h`・`ParticleSpawn.cpp`（新しい）・`ParticleSystem.h/.cpp`・`ParticleSystemComponent.h`・`EngineSystems.cpp`・`ImGuiManager.cpp`・vcxproj）：形の出し方を `ParticleSpawn` に移し、同じ所に形の線を足す。Component の **Show Gizmo** と、View メニューの **Particle Shapes**
- **パートD Sandbox と Reference**：テストシーン **CameraFollowZoneTest**（新しい）・Player.scene の `maxLag` → `softZone`・`Docs/Reference/` の 10-Camera・15-ParticleTrail・17-Editor
- **パートE 確かめ方**

パートは **A → B の順**（B は A の `CalcGuide` を使う）。**C は A・B と関係が無い**ので、どこで写してもよい。D は A〜C の後。

ブランチは `develop`（ab269ad）から `feature/CameraFollowZone` を切ってある（この md だけが入っている）。

続きの md：`Docs/Tasks/CameraOrbit.md`（追う物のまわりを回るカメラ。ボスを見ながらプレイヤーの後ろに付くロックオン）。この md を写した後に写す（`CameraGizmo`・`ImGuiManager`・Reference に、この md の後の形から足す）。

**写しで確かめた**（`develop` ab269ad を clone した写しで、答えのブランチ `answer-cfz`。Debug・Development・Release のビルドでエラー・警告 0。測る Script を付けた 12 台のカメラを Release で走らせて、全部の項目が合った。くわしくは パートE の E-3）。線が Scene・Game ビューに出るところは、まだ目で見ていない（E-2）。

**2026-11-11 に本体へ直接入れた**（ユーザーの「直接入れて」。`ab269ad..answer-cfz` の差分をそのまま当て、全部のファイルが写しと同じ。ブランチは `feature/CameraFollowZone`・未コミット）。本体の Debug・Development・Release のビルドでエラー・警告 0。Sandbox の起動シーン（`Parameters/Editor/LastScene.json`。git には入らない）を CameraFollowZoneTest にした。`CameraOrbit.md` はまだ入れていない。

**同じ日に、ユーザーの指摘で3つ直した**（0.9。本体にも当てた）：XY の面の「画面の真ん中」を、カメラの位置から「前の向きが追う物の奥行きの面と交わる点」に変えた・Bounds の画面の端を四隅を面に当てて測る（XY・XZ を1つの計算に）・Screen の範囲は Game ビューだけ（メインカメラの物だけ）に出す。**AL4_01 の GameScene（XY・55° 傾けたカメラ・Offset Y 8.5）は、画面の見え方が変わるので Offset Y を直す必要がある**。

## 0章 全体の形

### 0.1 今の Camera Follow と、足りない物

今の Camera Follow は、画面の真ん中に置きたい点 **focus**（追う物 ＋ Offset ＋ 先読み）が

1. **Dead Zone**（遊び）の外へ出た分だけ、Smooth Time でなめらかに追い
2. **Max Lag**（遅れの上限）より離れたら、すぐ縛る

という2段で動いている。どちらも **Plane の面の上の長さ（m）** で決まっている。

- 遊びを **画面の割合** で決めたいときがある（「画面の真ん中 2 割の中なら動かない」）。m で決めると、カメラを引いたとき（遠くから写すとき）に遊びが画面の上で小さくなり、寄ったときに大きくなる
- 2 の Max Lag は、Cinemachine の **Soft Zone** と同じ働き（Dead Zone の外ではなめらかに追い、Soft Zone の外へは出さない）。名前が違うので、「Soft Zone が無い」ように見える
- 範囲が目に見えないので、値を変えても「どこで追い始めるか」が分からない

### 0.2 Zone Space：World と Screen

`CameraFollowZoneSpace` を足して、Dead Zone・Soft Zone を何で測るか選ぶ。

| Zone Space | 測り方 | 使う値 |
|---|---|---|
| `World` | Plane の面の上の長さ（m）。画面の真ん中から横・縦に何 m か（今までと同じ） | `deadZone`・`softZone` |
| `Screen` | 画面の割合。**真ん中 0・端 ±1**（Camera Look At の Screen Position・Sprite の position と同じ決まり）。画面の真ん中から横・縦への半分の幅 | `screenDeadZone`・`screenSoftZone` |

- **値は World と Screen で別に持つ**。同じ `deadZone` を m と割合の両方に使うと、切り替えた瞬間に 0.5 m が「画面の半分」になって、カメラがおかしな動きをする。Mode（Center・LookAhead）と同じく、**Inspector には選んだ方だけが出て、保存も使っている方だけ**（Collider の Shape と同じ書き方）
- Screen の既定は Dead Zone 0.1・Soft Zone 0.8（真ん中から左右へ画面の幅の 5% は遊び、40% より外へは出さない）
- Offset・Look Ahead・Bounds は今までどおり m（「追う物からどれだけずらすか」はワールドの話なので）
- Soft Zone が Dead Zone より小さい軸は、Dead Zone と同じとみなす（遊びの中なのに縛られる、が起きないように）

### 0.3 Soft Zone は Max Lag の名前を変えた物

| 前 | 今 |
|---|---|
| `Vector2 maxLag = {6, 4}`（m）「遅れの上限」 | `Vector2 softZone = {6, 4}`（m）「focus を出さない範囲」 |

計算は同じ（focus が「真ん中 ± softZone」の外なら、真ん中をすぐ寄せる）。**保存の名前も `maxLag` → `softZone` に変わる**。前の名前を読む仕組みは足さない（AGENTS.md の「既存の名前を変えてよい。ゲーム側が直す必要が出てもよい」）。

- 既定値が同じ `[6, 4]` なので、`maxLag: [6, 4]` のまま残っているシーンは、読むと既定の `[6, 4]` になり、**動きは変わらない**。Sandbox の Player.scene と AL4_01 の GameScene はどちらも `[6, 4]`
- Sandbox の Player.scene はパートD で `softZone` に書き直す（ガイドのお手本なので、古い名前を残さない）。AL4_01 は、次にシーンを保存したときに `softZone` で書かれる

### 0.4 Screen の計算

```
   画面                                   ワールド（focus を通る、Plane の面）
 ┌───────────────┐
 │   ┌───────┐   │  focus を画面に写す      ●focus
 │   │ ┌───┐ │ ● │  → Dead Zone の外        ↑ この差だけカメラを動かす
 │   │ └───┘ │   │                          ○ 「Dead Zone の端に今写っている、面の上の点」
 │   └───────┘   │
 └───────────────┘
```

1. 今のカメラ（Transform の位置・向き、Camera の Fov Y、画面の縦横の比）で focus を画面に写す（真ん中 0・端 ±1）
2. 範囲（Dead Zone か Soft Zone）の中なら動かない。外なら、範囲の端へ寄せた画面の位置 `kept` を作る
3. `kept` に今写っている **面の上の点**（カメラから `kept` へ伸ばした線と、focus を通る Plane の面が交わる点）を探す
4. その点から focus までが、カメラを動かす量

3・4 が正しいのは、**カメラを面に沿って m 動かすと、面の上のどの点も、画面の上では「m 動いた点」がいた所に写る**から（カメラは面と平行に動き、向きは変えない）。なので「`kept` に写っている点」を focus まで運べば、focus が `kept` に写る。

- XY・View の面はカメラの正面に近いので、ほぼ「割合 × 奥行き × tan(視野角の半分)」の m になる
- **XZ（見下ろしの地面）でもそのまま正しい**。傾いたカメラの写す地面は台形なので、画面の割合と m は比例しないが、線を面と交わらせているので台形のまま測れる
- Dead Zone の計算（なめらかに近づける目標）と Soft Zone の計算（すぐ縛る）は同じ関数 `KeepInZone` で、範囲の大きさだけが違う。World のときも同じ関数（中は今までの `ApplyDeadZone`。名前を `ApplyZone` に変えた）
- 向きは **Transform の向き**だけを使う（今までと同じ。Camera Look At・揺れは入れない）。Look At で回しているカメラは、Transform の向きで測った画面になる

### 0.5 線：何を、どこに描くか

`CameraFollow::CalcGuide(entity, follow)` が、今のカメラの位置での範囲を **ワールドの点** で返す（計算は追いかけと同じ `FollowFrame`・`ZoneSizes` を使う。線と動きがずれない）。

| 線 | 色 | 中身 |
|---|---|---|
| Dead Zone | 緑 | focus がこの中なら、カメラは動かない |
| Soft Zone | 青 | focus はこの外へ出ない |
| focus | 黄 | 十字と、追う物から focus へ引いた線（Offset・先読みでどれだけずれているか） |
| Bounds | 赤 | Use Bounds の範囲（XY・XZ のとき） |

- 四角は **focus を通る Plane の面の上** に置く。どの面に置いても、**このカメラから見ると画面の上の範囲にぴったり重なる**（Game ビューでは、Screen の範囲が画面の四角に見える。XZ の World なら地面の台形）
- Screen で、面が画面の端まで届かない（XZ で四隅が地平線より上を向く）ときは、カメラの正面の面（focus の奥行きで、カメラの前の向きに垂直）に置く
- **World の範囲・focus・Bounds は Scene・Game ビューの両方**（`DrawLayer::Default`）。ワールドの物なので、どのカメラから見ても意味がある。どこで追い始めるかは、遊ぶときの絵の上でも見たい（Cinemachine も Game ビューに出す）。Collider・Hit Shapes と同じく、Release には出ない（エディタのギズモの中で積むので）
- **Screen の範囲は Game ビューだけ、Game ビューを写しているカメラ（`CameraSystem::GetMainCameraEntity`）の物だけ**（新しい層 `DrawLayer::GameViewGuide`。`SceneViewOptions::gameViewGuide` を Scene ビューだけ false にする）。Screen の範囲は「そのカメラから見た画面の四角」なので、Scene ビューのカメラやほかのカメラから見ると、宙に浮いた傾いた板にしか見えない
- **深度で隠さない**（`DepthMode::Disable`）。範囲は追う物のまわりの面なので、地面や壁に半分埋まって見えなくなるのを避ける
- 遠くでも薄くしない（Camera Path と同じ `kNoFadeStart`）

### 0.6 線を出す・消す

ライトのギズモと同じ **2 段**：

1. **Component の Show Gizmo**（1つずつ。シーンに保存される）
2. **View → Gizmos の Camera Follow Zones・Particle Shapes**（全部まとめて。エディタの間だけ）

両方が入っているときだけ出る。Show Gizmo の既定は入り（付けたらすぐ見える）。

### 0.7 Particle の出る形を1ファイルに（ParticleSpawn）

粒の出る場所を選ぶ `SampleShape`（と `RandomDirection`・`SpawnPoint`）は、`ParticleSystem.cpp` の名前の無い namespace に入っている。形の線も「Circle は XY の面」「Cone は XZ の面から +Y へ、縁で Cone Angle」という**同じ決まり**を描くので、**出し方と線を同じファイルに並べる**（形を足す・変える人が、片方だけ直すのを防ぐ）。

- `Systems/Effects/ParticleSpawn.h/.cpp`（namespace `ParticleSpawn`）：`Sample`（今の `SampleShape` そのまま）・`PushShapeGizmo`（線）
- 名前を `ParticleShape` にしないのは、Component の enum `ParticleShape` とぶつかるから
- 線は **Entity から見た座標で作ってから、ワールド行列で写す**（粒を出すときと同じく、位置・回転・大きさが効く）
- **Scene ビューだけ**（`DrawLayer::EditorGizmo`）。置く場所を決める助けで、遊ぶときの絵には要らない（Light の範囲・Decal の箱と同じ）
- 物の後ろは隠す（Collider と同じ、ふつうのギズモの線）
- 無効の Particle System・無効の Entity・Hierarchy の目で隠した物は描かない
- `ParticleSystem.cpp` は 620 行から 569 行になる（線の分を足しても減る）

| Shape | 線 |
|---|---|
| Point | 3 つの軸の小さな十字（半分の長さ 0.1 m） |
| Sphere | XY・YZ・ZX の 3 つの円 |
| Circle | XY の面の円 |
| Cone | 底の輪（XZ）と、縁から出た粒の向きへ 1 m 進んだ所の輪、その間の 4 本（ラッパの形。1 m は向きを見せるためで、粒の飛ぶ距離ではない） |
| Box | 12 本の辺 |

From Surface は線を変えない（中から出るか縁から出るかの違いで、形は同じ）。

### 0.8 やらないこと

- **Screen Position**（Dead Zone の真ん中を画面の真ん中からずらす。Cinemachine にはある）：今は Offset（m）でずらす。要るようになったら、Camera Look At の Screen Position と同じ決まりで足す（`KeepInZone` の範囲を `screenPosition ± zone` にするだけ）
- Game ビューだけ別に消す項目（Unity の Game ビューの Gizmos ボタン）：今は View → Gizmos の 1 つで両方
- **Bounds に World / Screen を選ぶ**：Bounds は「ステージの端」というワールドの物。画面の割合で「focus をここから出さない」は Soft Zone の役目なので、Screen を足すと同じ働きが2つになる
- **XZ で跳んだ高さを無視する（最後に地面にいた高さで測る）**：XZ は追う物の今の高さの面で測るので、跳ぶと画面の真ん中が手前へ寄り（跳んだ高さ ÷ tan(下を向く角度)）、奥行きの Dead Zone を超えるとカメラが前後に動く。四角も追う物の高さに描くので一緒に浮く。Unity の Position Composer も、跳ぶと追う物が画面の上へ出て、縦の Dead Zone を超えればカメラが付いていく（範囲は画面の上で変わらない）ので、同じ考え方として今のままにした（2026-11-12 にユーザーと決めた）。跳びに付いていかせたくないときは、Dead Zone の縦（XZ では奥行き）を大きくする。Camera Orbit の回る中心も同じく跳ぶと上がる（Unity の Orbital Follow と同じ）。要るようになったら、Follow と Orbit が共通で使う「最後に地面にいた高さ」の仕組みとして足す
- Particle の線を「選んでいる物だけ」出す（Unity の Particle System）：今は Show Gizmo の入った物を全部
- `CameraFollow.cpp` は 553 行になる（500 行を超えた）。見直した：コピペは無い。分けられる所は `ViewFrustum`（Transform の向きと視野角で、点を画面へ・画面の位置を面へ）で、Camera Look At の `CalcRotation` も同じ「画面の位置 → 向き」を持っている。3 つ目の使い道（UI を画面の位置に置くなど）が出たら、`Systems/Camera/` の共通のヘッダへ出す

### 0.9 傾けたカメラの「画面の真ん中」と Bounds の端（ユーザーの指摘で直した）

**困っていたこと**：Plane が XY のとき、「画面の真ん中＝カメラの X・Y」、「画面の端＝真ん中から上下左右に、奥行き × tan(視野角の半分) ずつ」としていた。カメラが +Z をまっすぐ向いていれば合うが、傾けると合わない。

CameraFollowZoneTest のカメラ（下へ 8°・追う物まで奥行き 12 m・Fov Y 50）で：

| | 前の計算 | 本当（画面に写る所） |
|---|---|---|
| 画面の真ん中 | カメラの高さ | カメラの高さより 1.7 m 下（12 × tan 8°） |
| 画面の上の端 | +5.6 m | +3.7 m（12 × tan(25° − 8°)） |
| 画面の下の端 | −5.6 m | −7.8 m（12 × tan(25° + 8°)） |

- World の緑・青の四角が、Game ビューで画面の真ん中より上に出る（四角は「真ん中 ± Dead Zone」に描くので）
- 追い始め・Ground Snap Y で、focus が画面の真ん中に来ない（カメラの高さを focus にそろえるだけなので）
- Bounds で止まったとき、下はステージの外が写り、上は端まで見えずに止まる。赤い線と画面の端が合わない
- AL4_01 の GameScene（XY・55° 傾けた・Offset Y 8.5）は、このずれを Offset Y で手で埋めていた

**直し**：XZ（見下ろし）ですでにやっていた測り方を、XY（と View）にも使う。

- 画面の真ん中 ＝ カメラの前の向きへ伸ばした線が、追う物を通る面（XY：追う物の奥行き、XZ：追う物の高さ、View：カメラの正面）と交わる点（`MakeFollowFrame` の `viewCenter`。`ViewFrustum::CastToPlane({0, 0}, ...)`。前の `CalcGroundCenter` は消した）
- 画面の端 ＝ 画面の四隅から線を伸ばし、同じ面と交わる点（`CalcViewExtent`。前の XZ だけの `CalcGroundExtent` と、XY の「± 奥行き × tan」を1つにした）。上下（左右）で長さが違ってよいので、四隅を1本ずつ測る
- Bounds の Min・Max がワールドのどの向きか（XY：X・Y、XZ：X・Z、View：無し）を `GetBoundsAxes` 1か所で決め、追いかけと線（`CalcGuide`）の両方が使う
- カメラが傾いていなければ、交わる点はカメラの真正面なので、前と同じ動き。View は前の向きが面に垂直なので、前と同じ
- 面に沿って見ている（交わらない）ときは、前と同じくカメラの位置を使う

**影響**：XY の面で傾けたカメラを使っているシーンは、focus が本当の画面の真ん中に来るようになる。傾きの分を Offset Y で埋めていたシーン（AL4_01 の GameScene）は、Offset Y を直す（0 に近くなる）。

## パートA Camera Follow の計算

### A-1 `Components/Camera/CameraFollowComponent.h`（全体を差し替え）

- `CameraFollowZoneSpace`（World・Screen）を足す
- `maxLag` → `softZone`。`zoneSpace`・`screenDeadZone`・`screenSoftZone`・`showGizmo` を足す
- 並びを「遊び → ステージの端 → 先読み」の見出しに分けた（中身の並びが変わるだけで、保存の並びは Describe が決める）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// カメラが Entity を追いかける。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// どの面の上で動かすかは Plane で決める。カメラの向きは変えない
// 動かすのは CameraSystem::UpdateFollow（再生中だけ。物理で動いた後・カメラの絵を決める前）。停止中は置いた位置のまま
// 画面の真ん中に置きたい点（focus）＝ 追う物の位置 ＋ Offset ＋ 追い方（Mode）で決まるずれ
// カメラは、focus が Dead Zone（遊び）の外へ出た分だけなめらかに追い、Soft Zone の外へは出さない（Cinemachine の Dead Zone・Soft Zone と同じ）
// 2つの範囲は、ワールドの長さ（m）か画面の割合で測る（Zone Space）

/// <summary>
/// 追い方（focus をどこに置くか）。Inspector では、選んだ追い方の値だけが出る
/// </summary>
enum class CameraFollowMode {
	Center,    // 追う物を真ん中に（Offset の分だけずらす）
	LookAhead, // 進む向きの先を見せる（追う物の速さ × Look Ahead Time だけ先を真ん中に。止まると真ん中に戻る）
};

/// <summary>
/// カメラを動かす面。Offset・Look Ahead と、World の Dead Zone・Soft Zone の X・Y は、下の「横」と「縦」の向きになる
/// </summary>
enum class CameraFollowPlane {
	XY,   // ワールドの X・Y（横から見るゲーム）。横 = X・縦 = Y
	XZ,   // 地面の X・Z（見下ろしのゲーム）。横 = カメラの右、縦 = カメラの前を地面に寝かせた向き（カメラが回ると一緒に回る）
	View, // カメラから見た位置を保つ（どの向きのカメラでも）。横 = カメラの右、縦 = カメラの上。奥行きも、追いかけ始めの長さに保つ
};

/// <summary>
/// Dead Zone・Soft Zone を何で測るか。Inspector では、選んだ方の値だけが出る
/// </summary>
enum class CameraFollowZoneSpace {
	World,  // ワールドの長さ（m）。Plane の面の上で、画面の真ん中から横・縦に何 m か（カメラが寄っても引いても、追い始める距離は同じ）
	Screen, // 画面の割合。真ん中が 0、端が ±1（Camera Look At の Screen Position と同じ）。カメラが引くと、ワールドでは広がる
};

/// <summary>
/// カメラの追いかけ（Unity の Cinemachine の Position Composer の小さい版）
/// </summary>
struct CameraFollowComponent {
	// --- どの追い方でも使う ---
	bool enabled = true;                                 // false の間は追わない（置いた位置のまま）
	EntityRef target;                                    // 追う Entity（Hierarchy からドラッグ。保存は EntityId で）
	CameraFollowPlane plane = CameraFollowPlane::XY;     // カメラを動かす面
	CameraFollowMode mode = CameraFollowMode::LookAhead; // 追い方
	Vector2 offset = {0.0f, 1.5f};                       // 追う物から、画面の真ん中をどれだけずらすか（m。XY で Y を上げると、足元が広く見える）
	// --- 遊び（Dead Zone）と、focus を出さない範囲（Soft Zone）。どちらも画面の真ん中から横・縦への半分の幅 ---
	CameraFollowZoneSpace zoneSpace = CameraFollowZoneSpace::World; // 2つの範囲を何で測るか
	Vector2 deadZone = {0.5f, 1.5f};                                // World の遊び（m）。focus がこの中で動いても、カメラは動かない
	Vector2 softZone = {6.0f, 4.0f};                                // World の Soft Zone（m）。focus はこの外へ出ない（速く動いても画面から出ないように）。Dead Zone より小さい軸は Dead Zone と同じ
	Vector2 screenDeadZone = {0.1f, 0.1f};                          // Screen の遊び（画面の割合。端が 1）
	Vector2 screenSoftZone = {0.8f, 0.8f};                          // Screen の Soft Zone（画面の割合。1 より小さいと、focus はいつも画面の中）
	bool groundSnapY = true;  // true：追う物が地面にいる間は、縦の遊びを使わずに合わせる（XY のとき。跳んでも画面は揺れず、段に乗ると上がる）。追う物の Rigidbody・Character Controller が地面にいるかを見る（MotionQuery::IsGrounded）
	float smoothTime = 0.15f; // 遊びの外へ出たとき、追いつくまでの時間の目安（秒。0 ならすぐ合わせる）
	bool showGizmo = true;    // Dead Zone・Soft Zone・focus・Bounds の線を出す（エディタだけ。Screen の範囲は Game ビューだけ。View メニューの Camera Follow Zones で全部まとめて消せる）
	// --- ステージの端 ---
	bool useBounds = false;               // true：画面の端が boundsMin〜boundsMax の外を写さない（ステージの端でカメラが止まる。XY・XZ のとき）
	Vector2 boundsMin = {-10.0f, -10.0f}; // 写してよい範囲の左下（XY はワールドの X・Y、XZ はワールドの X・Z。追う物の奥行き・高さで測る）
	Vector2 boundsMax = {100.0f, 30.0f};  // 写してよい範囲の右上
	// --- Look Ahead のときだけ使う ---
	Vector2 lookAheadTime = {0.4f, 0.0f}; // 何秒先を見せるか（先読み ＝ 追う物の速さ × これ。Y を 0 にすると、縦には先読みしない）
	Vector2 lookAheadMax = {3.0f, 2.0f};  // 先読みの上限（m）。吹っ飛びのような速い動きで、先へ行きすぎないように
	float lookAheadSmoothTime = 0.3f;     // 先読みが変わるまでの時間の目安（秒。速さが急に変わっても、画面がガクッと動かないように）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isFollowing = false;                        // 追いかけ始めたか（false なら、次のフレームで focus へすぐ合わせる）
	float viewDepth = 0.0f;                          // View のとき、保つ奥行き（m。追いかけ始めの、カメラの前の向きに測った長さ）
	Vector2 currentLookAhead = {0.0f, 0.0f};         // 今の先読み（m。横・縦が＋。Center の間は 0 へ戻っていく）
	Vector3 lastTargetPosition = {0.0f, 0.0f, 0.0f}; // 前のフレームの、追う物の位置（速さを出すのに使う。カメラが回ると横・縦の向きが変わるので、ワールドの位置で持つ）
};

static_assert(std::is_trivially_copyable_v<CameraFollowComponent>);
```

### A-2 `Systems/Camera/CameraFollow.h`（全体を差し替え）

`Guide`（線に使う、今の範囲をワールドの点で表した物）と `CalcGuide` を足す。

```cpp
#pragma once
#include <array>
#include <optional>

#include "HTTEngine/Components/Camera/CameraFollowComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Follow の中身（CameraSystem から使う）
/// </summary>
namespace CameraFollow {
/// <summary>
/// 今の Dead Zone・Soft Zone などを、ワールドの点で表した物（エディタの線に使う。CalcGuide で作る）
/// <para>四角は左下・右下・右上・左上の順。focus を通る、Plane の面の上に置く（Screen で面が画面の外へ伸びるときは、カメラの正面の面）</para>
/// <para>どの面に置いても、このカメラから見ると画面の上の範囲にぴったり重なる（Game ビューでは、範囲が画面の四角に見える）</para>
/// </summary>
struct Guide {
	std::array<Vector3, 4> deadZone; // focus がこの中なら、カメラは動かない
	std::array<Vector3, 4> softZone; // focus はこの外へ出ない
	Vector3 target;                  // 追う物の位置
	Vector3 focus;                   // 画面の真ん中に置きたい点（追う物 ＋ Offset ＋ 先読み）
	Vector3 horizontal;              // focus の印を描く向き（面の横。長さ1）
	Vector3 vertical;                // focus の印を描く向き（面の縦。長さ1）
	float markSize = 0.0f;           // focus の印の大きさ（m。カメラからの距離に合わせて、画面の上でいつも同じくらいに見える）
	std::optional<std::array<Vector3, 4>> bounds; // Use Bounds の範囲（XY・XZ で Use Bounds のときだけ）
};

// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraFollowComponent& value);

// カメラ1つ分の追いかけ。Transform を動かしたら true
bool Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime);

// 今のカメラの位置での範囲（追う物が居ない・focus がカメラの後ろなら無し）。追いかけと同じ計算で作る
std::optional<Guide> CalcGuide(Handle<Entity> entity, const CameraFollowComponent& follow);
} // namespace CameraFollow
```

### A-3 `Systems/Camera/CameraFollow.cpp`（全体を差し替え）

変わった所：

- `FollowAxes::ToWorld`（面の上の動き → ワールドの動き）を足した。前は `axes.horizontal * x + axes.vertical * y` を何か所にも書いていた
- `ViewFrustum`（0.4 の 1〜3）・`FollowFrame`（1フレーム分の値。Update と CalcGuide が同じ物を作る）・`ZoneSizes` を足した
- `MakeFollowFrame`：Update の頭にあった「Transform を読んで、面・奥行き・真ん中を出す」所を外へ出した。真ん中は、前の向きが追う物を通る面と交わる点（0.9。`CalcGroundCenter` は消した）
- `CalcViewExtent`（四隅を面に当てて画面の端を測る。前の `CalcGroundExtent` と XY の「± 奥行き × tan」を1つに）・`GetBoundsAxes`（Bounds の向き）を足し、Update の 4 を XY・XZ で1つの計算にした（0.9）
- `ApplyDeadZone` → `ApplyZone`（Soft Zone にも使うので）。`KeepInZone`（World・Screen を分ける）を足した
- Update の 0.：View の奥行きを保つ前後の動き（前は 5 で足していた）を、範囲を測る前にカメラへ入れる（後で足すと Screen の Soft Zone から少し出る。E-3）
- Update の 3：`KeepInZone(dead)` → なめらかに → `KeepInZone(soft)`。前の Max Lag の `clamp` は、`KeepInZone(soft)` の World と同じ計算
- Describe：Zone Space の switch・Soft Zone・Show Gizmo
- `CalcGuide`・`MakeWorldRect`・`MakeScreenRect` を足した

```cpp
#include "CameraFollow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Physics/MotionQuery.h"

namespace {
constexpr float kMinFlatLength = 0.0001f;     // 地面に寝かせた向きの長さの2乗がこれより小さければ、向きが決まらない
constexpr float kMinDepth = 0.001f;           // カメラの前の向きに測った長さがこれより短い点は、画面に写らない（カメラの後ろ・真横）
constexpr float kMinCastAlignment = 0.0001f;  // 線の向きと面に垂直な向きの内積の大きさがこれより小さければ、線は面に沿っていて交わらない
constexpr float kGuideMarkScale = 0.03f;      // focus の印の大きさ（カメラから focus までの長さに掛ける。画面の上でいつも同じくらいに見える）

/// <summary>
/// カメラを動かす面の、横と縦の向き（どちらも長さ1）
/// </summary>
struct FollowAxes {
	Vector3 horizontal; // 横（画面の右に当たる向き）
	Vector3 vertical;   // 縦（画面の上に当たる向き）

	// ワールドの点（向き）を、面の上の座標にする
	Vector2 ToPlane(const Vector3& point) const { return {Dot(point, horizontal), Dot(point, vertical)}; }
	// 面の上の動き → ワールドの動き
	Vector3 ToWorld(const Vector2& move) const { return horizontal * move.x + vertical * move.y; }
};

/// <summary>
/// 画面の四隅が、真ん中の点からどれだけ離れた所を写すか（ワールドの2つの軸で測る。ApplyBounds に渡す）
/// </summary>
struct ViewExtent {
	Vector2 min; // 真ん中より小さい方へのずれ（マイナス）
	Vector2 max; // 真ん中より大きい方へのずれ（プラス）
};

/// <summary>
/// Bounds の Min・Max の X・Y が、ワールドのどの向きか（長さ1）
/// </summary>
struct BoundsAxes {
	Vector3 a; // Min・Max の X
	Vector3 b; // Min・Max の Y
};

/// <summary>
/// カメラから見た画面の測り方（Transform の向きと Camera Component の視野角。Camera Look At・揺れを入れる前の向き）
/// <para>画面の位置は、真ん中が 0・右と上が＋・端が ±1（Camera Look At の Screen Position と同じ）</para>
/// </summary>
struct ViewFrustum {
	Vector3 position;      // カメラの位置
	Vector3 right;         // カメラの右（長さ1）
	Vector3 up;            // カメラの上（長さ1）
	Vector3 forward;       // カメラの前（長さ1）
	float tanHalfX = 1.0f; // 画面の右の端へ向かう線の、前に対する傾き（tan(横の視野角の半分)）
	float tanHalfY = 1.0f; // 画面の上の端へ向かう線の、前に対する傾き（tan(縦の視野角の半分)）

	// 点が写る画面の位置（カメラの後ろ・真横なら無し）
	// カメラから見た点を「前に1進んだ所での横・縦のずれ」にして、端までのずれ（tanHalf）で割る
	std::optional<Vector2> ToScreen(const Vector3& point) const {
		const Vector3 local = point - position;
		const float depth = Dot(local, forward);
		if (depth < kMinDepth) {
			return std::nullopt;
		}
		return Vector2{Dot(local, right) / (depth * tanHalfX), Dot(local, up) / (depth * tanHalfY)};
	}

	// 画面の位置 screen に写る点を、planePoint を通り normal に垂直な面の上で探す（線が面に沿っている・面がカメラの後ろなら無し）
	std::optional<Vector3> CastToPlane(const Vector2& screen, const Vector3& planePoint, const Vector3& normal) const {
		const Vector3 direction = forward + right * (screen.x * tanHalfX) + up * (screen.y * tanHalfY); // ToScreen の逆（前に1進んだ所）
		const float alignment = Dot(direction, normal);
		if (std::abs(alignment) < kMinCastAlignment) {
			return std::nullopt;
		}
		const float distance = Dot(planePoint - position, normal) / alignment; // 線の上の点 position + direction × distance が面に乗る distance
		if (distance <= 0.0f) {
			return std::nullopt;
		}
		return position + direction * distance;
	}

	// 向きはそのまま、位置だけ move 動かしたカメラ
	ViewFrustum Moved(const Vector3& move) const {
		ViewFrustum moved = *this;
		moved.position = position + move;
		return moved;
	}
};

/// <summary>
/// 1フレーム分の、追いかけの計算に使う値（Update と CalcGuide が同じ物を使う）
/// </summary>
struct FollowFrame {
	FollowAxes axes;        // 面の横・縦
	Vector3 normal;         // 面に垂直な向き（Screen で、画面の位置を面の上の点に直すのに使う）
	ViewFrustum frustum;    // 今のカメラ
	Vector3 targetPosition; // 追う物の位置
	Vector3 viewCenter;     // 画面の真ん中が写している点（カメラの前の向きが、追う物を通る面と交わる点）
	Vector2 center;         // viewCenter の面の上の座標（このフレームで追う前の真ん中）
	float depth = 0.0f;     // 追う物の奥行き（カメラの前の向きに測った長さ）
};

/// <summary>
/// Dead Zone と Soft Zone の大きさ（Zone Space で選んだ方。Soft Zone は Dead Zone より小さくしない）
/// </summary>
struct ZoneSizes {
	Vector2 dead; // focus がこの中なら、カメラは動かない
	Vector2 soft; // focus はこの外へ出ない
};

//=============================================================================
// 面の計算
//=============================================================================
// ===== 面の、横と縦の向き =====
FollowAxes MakeFollowAxes(CameraFollowPlane plane, const Quaternion& rotation) {
	const Vector3 right = RotateVector({1.0f, 0.0f, 0.0f}, rotation);
	switch (plane) {
	case CameraFollowPlane::XY:
		return {
		    {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}
        };
	case CameraFollowPlane::XZ: {
		// カメラの右を地面に寝かせる。右は真下を見ても水平のままなので、見下ろしのカメラでも向きが決まる
		const Vector3 flatRight = {right.x, 0.0f, right.z};
		const Vector3 horizontal = (LengthSq(flatRight) > kMinFlatLength) ? Normalize(flatRight) : Vector3{1.0f, 0.0f, 0.0f};
		// 右 × 上 ＝ 前（Quaternion.cpp の MakeLookRotation と同じ左手座標系の決まり）
		return {horizontal, Cross(horizontal, {0.0f, 1.0f, 0.0f})};
	}
	case CameraFollowPlane::View:
		return {right, RotateVector({0.0f, 1.0f, 0.0f}, rotation)};
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowPlane です");
	return {
	    {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    };
}

// ===== 面の上で、画面の四隅が写す範囲 =====
// 画面の四隅から線を伸ばして、planePoint を通り normal に垂直な面と交わる点を、真ん中 center からのずれ（axisA・axisB の向きに測る）にする
// 傾いたカメラの写す範囲は、真ん中から上下（左右）に同じ長さではない（XY を下へ傾けると、下の方が遠くまで写る。XZ は台形）ので、四隅を1本ずつ測る
// 面と交わらない隅（地平線より上を向いている）は数えない。1つも交わらなければ無し
std::optional<ViewExtent> CalcViewExtent(const ViewFrustum& frustum, const Vector3& planePoint, const Vector3& normal, const Vector3& center, const Vector3& axisA, const Vector3& axisB) {
	const std::array<Vector2, 4> corners = {
	    {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}}
    };
	std::optional<ViewExtent> extent;
	for (const Vector2& corner : corners) {
		const std::optional<Vector3> hit = frustum.CastToPlane(corner, planePoint, normal);
		if (!hit.has_value()) {
			continue;
		}
		const Vector2 offset = {Dot(*hit - center, axisA), Dot(*hit - center, axisB)};
		if (!extent.has_value()) {
			extent = ViewExtent{offset, offset};
		}
		extent->min = {(std::min)(extent->min.x, offset.x), (std::min)(extent->min.y, offset.y)};
		extent->max = {(std::max)(extent->max.x, offset.x), (std::max)(extent->max.y, offset.y)};
	}
	return extent;
}

// ===== Bounds の Min・Max の向き（View は面がカメラと一緒に回るので、決まった範囲を持てない＝無し）=====
std::optional<BoundsAxes> GetBoundsAxes(CameraFollowPlane plane) {
	switch (plane) {
	case CameraFollowPlane::XY:
		return BoundsAxes{
		    {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}
        };
	case CameraFollowPlane::XZ:
		return BoundsAxes{
		    {1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}
        };
	case CameraFollowPlane::View:
		return std::nullopt;
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowPlane です");
	return std::nullopt;
}

// ===== カメラの視野角（ラジアン）=====
float GetFovY(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return std::clamp(camera ? camera->fovY : CameraComponent{}.fovY, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
}

// ===== 1フレーム分の値を集める（カメラか追う物の Transform が無ければ無し）=====
std::optional<FollowFrame> MakeFollowFrame(Handle<Entity> entity, Handle<Entity> targetEntity, CameraFollowPlane plane) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(targetEntity);
	if (transform == nullptr || targetTransform == nullptr) {
		return std::nullopt; // 追う物が入っていない・もう居ない
	}
	FollowFrame frame;
	// 向きは毎フレーム Transform から読む（カメラを回しても、横・縦が画面の向きに付いてくる）
	const Quaternion rotation = GetWorldRotation(*transform);
	frame.axes = MakeFollowAxes(plane, rotation);
	frame.normal = Cross(frame.axes.horizontal, frame.axes.vertical); // 向き（表か裏か）は使わない。面と交わる所を探すだけ
	frame.frustum.position = GetWorldPosition(*transform);
	frame.frustum.right = RotateVector({1.0f, 0.0f, 0.0f}, rotation);
	frame.frustum.up = RotateVector({0.0f, 1.0f, 0.0f}, rotation);
	frame.frustum.forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	frame.frustum.tanHalfY = std::tan(GetFovY(entity) * 0.5f);
	frame.frustum.tanHalfX = frame.frustum.tanHalfY * GameScreen::GetAspectRatio();
	frame.targetPosition = GetWorldPosition(*targetTransform);
	frame.depth = Dot(frame.targetPosition - frame.frustum.position, frame.frustum.forward);
	// 画面の真ん中は、カメラの前の向きへ伸ばした線が、追う物を通る面（XY：追う物の奥行き、XZ：追う物の高さ、View：カメラの正面）と交わる点
	// 傾けたカメラでは、カメラの真横・真下は画面の真ん中ではない（XY を下へ傾けると、真ん中に写るのはカメラより低い所）
	// 面と交わらない（面に沿って見ている・面が後ろにある）ときは、カメラの位置を使う
	frame.viewCenter = frame.frustum.CastToPlane({0.0f, 0.0f}, frame.targetPosition, frame.normal).value_or(frame.frustum.position);
	frame.center = frame.axes.ToPlane(frame.viewCenter);
	return frame;
}

// ===== Zone Space で選んだ方の大きさ =====
ZoneSizes GetZoneSizes(const CameraFollowComponent& follow) {
	const auto make = [](const Vector2& dead, const Vector2& soft) {
		const Vector2 safeDead = {(std::max)(dead.x, 0.0f), (std::max)(dead.y, 0.0f)};
		return ZoneSizes{safeDead, {(std::max)(soft.x, safeDead.x), (std::max)(soft.y, safeDead.y)}};
	};
	switch (follow.zoneSpace) {
	case CameraFollowZoneSpace::World:
		return make(follow.deadZone, follow.softZone);
	case CameraFollowZoneSpace::Screen:
		return make(follow.screenDeadZone, follow.screenSoftZone);
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowZoneSpace です");
	return make(follow.deadZone, follow.softZone);
}

//=============================================================================
// 追いかけの計算
//=============================================================================
// ===== 追い方で決まる、focus のずれ（先読み）の目標 =====
Vector2 CalcWantedLookAhead(const CameraFollowComponent& follow, const Vector2& velocity) {
	switch (follow.mode) {
	case CameraFollowMode::Center:
		return {0.0f, 0.0f}; // ずらさない（LookAhead から切り替えたときは、0 へなめらかに戻る）
	case CameraFollowMode::LookAhead:
		// 速さ × 何秒先か（t = p + α・v の α・v）。速いほど先を見せ、止まると 0（真ん中に戻る）。上限で縛る
		return {
		    std::clamp(velocity.x * follow.lookAheadTime.x, -follow.lookAheadMax.x, follow.lookAheadMax.x),
		    std::clamp(velocity.y * follow.lookAheadTime.y, -follow.lookAheadMax.y, follow.lookAheadMax.y),
		};
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowMode です");
	return {0.0f, 0.0f};
}

// ===== 1つの軸で、focus が「真ん中 ± zone」の外へ出た分だけ真ん中を動かす（中なら、真ん中のまま）=====
float ApplyZone(float center, float focus, float zone) {
	if (focus > center + zone) {
		return focus - zone; // 右（上）へはみ出した：はみ出した分だけ追う
	}
	if (focus < center - zone) {
		return focus + zone; // 左（下）へはみ出した
	}
	return center; // 範囲の中：動かない
}

// ===== focus が「真ん中 ± zone」の中に来るように、真ん中を一番少なく動かす =====
// center・focus は面の上の座標、focusPoint は focus のワールドの点。Dead Zone（なめらかに追う目標）と Soft Zone（すぐ縛る）の両方に使う
// World：面の上の長さで比べる
// Screen：真ん中が center のときのカメラで focus を画面に写して比べ、はみ出した分を面の上の長さに直す
//   カメラを面に沿って m 動かすと、面の上のどの点も、画面の上では「m 動いた点」の写っていた所へ移る（面とカメラの動く向きが平行なので）
//   → 「focus を写したい画面の位置（範囲の端）に、今写っている面の点」から focus までが、動かす量（面が傾いた XZ の台形でもそのまま正しい）
Vector2 KeepInZone(const FollowFrame& frame, CameraFollowZoneSpace space, const Vector2& center, const Vector2& focus, const Vector3& focusPoint, const Vector2& zone) {
	switch (space) {
	case CameraFollowZoneSpace::World:
		return {ApplyZone(center.x, focus.x, zone.x), ApplyZone(center.y, focus.y, zone.y)};
	case CameraFollowZoneSpace::Screen: {
		const ViewFrustum camera = frame.frustum.Moved(frame.axes.ToWorld(center - frame.center));
		const std::optional<Vector2> screen = camera.ToScreen(focusPoint);
		if (!screen.has_value()) {
			return focus; // focus がカメラの後ろ：真ん中へ合わせる
		}
		const Vector2 kept = {std::clamp(screen->x, -zone.x, zone.x), std::clamp(screen->y, -zone.y, zone.y)};
		if (kept == *screen) {
			return center; // 範囲の中：動かない
		}
		const std::optional<Vector3> hit = camera.CastToPlane(kept, focusPoint, frame.normal);
		if (!hit.has_value()) {
			return focus; // 範囲の端が面に写らない（地平線より上など）：真ん中へ合わせる
		}
		return center + frame.axes.ToPlane(focusPoint - *hit);
	}
	}
	HTT_ASSERT_MSG(false, "知らない CameraFollowZoneSpace です");
	return center;
}

// ===== 1つの軸で、画面の端が min〜max の外を写さないように、真ん中を寄せる =====
// viewMin・viewMax は、真ん中から画面の端までのずれ。範囲が画面より狭いときは、どこに置いてもはみ出すので、画面と範囲の真ん中をそろえる
float ApplyBounds(float center, float viewMin, float viewMax, float min, float max) {
	const float low = min - viewMin;  // 真ん中がこれより小さいと、画面の左（下）の端が min の外を写す
	const float high = max - viewMax; // 真ん中がこれより大きいと、画面の右（上）の端が max の外を写す
	if (low > high) {
		return ((min + max) - (viewMin + viewMax)) * 0.5f;
	}
	return std::clamp(center, low, high);
}

//=============================================================================
// エディタの線の形
//=============================================================================
// ===== World の範囲の四隅（focus を通る面の上で、真ん中 ± zone）=====
std::array<Vector3, 4> MakeWorldRect(const FollowFrame& frame, const Vector2& focus, const Vector3& focusPoint, const Vector2& zone) {
	const auto corner = [&](float signX, float signY) {
		const Vector2 point = {frame.center.x + zone.x * signX, frame.center.y + zone.y * signY};
		return focusPoint + frame.axes.ToWorld(point - focus);
	};
	return {corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(1.0f, 1.0f), corner(-1.0f, 1.0f)};
}

// ===== Screen の範囲の四隅（画面の ± zone に写る、focus を通る面の上の点。面に写らない隅があれば無し）=====
std::optional<std::array<Vector3, 4>> MakeScreenRect(const ViewFrustum& frustum, const Vector3& focusPoint, const Vector3& normal, const Vector2& zone) {
	const std::array<Vector2, 4> screens = {
	    {{-zone.x, -zone.y}, {zone.x, -zone.y}, {zone.x, zone.y}, {-zone.x, zone.y}}
    };
	std::array<Vector3, 4> corners;
	for (size_t i = 0; i < screens.size(); ++i) {
		const std::optional<Vector3> hit = frustum.CastToPlane(screens[i], focusPoint, normal);
		if (!hit.has_value()) {
			return std::nullopt;
		}
		corners[i] = *hit;
	}
	return corners;
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraFollow::Describe(ComponentUI& ui, CameraFollowComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと追わない（置いた位置のまま）"));
	ui.Field("Target###target", value.target, Tip("追う Entity（Hierarchy からドラッグ）。再生中だけ追う。動かすのは Plane の面の上だけ（カメラの向きは置いたまま）"));
	ui.Field(
	    "Plane###plane", value.plane,
	    Tip("カメラを動かす面。XY：ワールドの X・Y（横から見る）/ XZ：地面（見下ろし。カメラの向きが変わると横・縦も回る）/ View：カメラの右・上（どの向きのカメラでも。奥行きも保つ）"));
	ui.Field("Mode###mode", value.mode, Tip("追い方。Center：追う物を真ん中に / LookAhead：進む向きの先を見せる（速さ × Time。止まると真ん中に戻る）"));
	// --- 選んだ追い方の値だけを見せる（保存も、使っている方だけが書かれる。Collider の Shape と同じ）---
	switch (value.mode) {
	case CameraFollowMode::Center:
		break; // Center だけの値は無い
	case CameraFollowMode::LookAhead:
		ui.Field(
		    "Look Ahead Time###lookAheadTime", value.lookAheadTime,
		    AtLeast(0.0f).Tip("何秒先を見せるか（先読み ＝ 追う物の速さ × これ）。X 0.4 なら 7m/s で 2.8m 先。Y を 0 にすると、縦には先読みしない"));
		ui.Field("Look Ahead Max###lookAheadMax", value.lookAheadMax, AtLeast(0.0f).Tip("先読みの上限（m）。吹っ飛びのような速い動きで、先へ行きすぎないように"));
		ui.Field(
		    "Look Ahead Smooth Time###lookAheadSmoothTime", value.lookAheadSmoothTime,
		    AtLeast(0.0f).Tip("先読みが変わるまでの時間の目安（秒）。速さが急に変わっても（振り返る・止まる）、画面がガクッと動かない。0 ならすぐ変わる"));
		break;
	}
	ui.Separator("Follow");
	ui.Field("Offset###offset", value.offset, Tip("追う物から、画面の真ん中をどれだけずらすか（m）。X が横、Y が縦。XY で Y を上げると、足元が広く見える"));
	ui.Field(
	    "Zone Space###zoneSpace", value.zoneSpace,
	    Tip("Dead Zone・Soft Zone を何で測るか。World：Plane の面の上の長さ（m。カメラが引いても、追い始める距離は同じ）/ Screen：画面の割合（真ん中 0・端 1。カメラが引くと、ワールドでは広がる）"));
	// --- 選んだ測り方の値だけを見せる（保存も、使っている方だけ）---
	switch (value.zoneSpace) {
	case CameraFollowZoneSpace::World:
		ui.Field("Dead Zone###deadZone", value.deadZone, AtLeast(0.0f).Tip("遊び（m）。focus が画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない（跳ぶたびに画面が上下しないように）"));
		ui.Field(
		    "Soft Zone###softZone", value.softZone,
		    AtLeast(0.0f).Tip("focus を出さない範囲（m。画面の真ん中から横・縦）。Dead Zone の外ではなめらかに追い、ここに着いたらすぐ付いていく（速く動いても画面から出ない）。Dead Zone より小さい軸は Dead Zone と同じ"));
		break;
	case CameraFollowZoneSpace::Screen:
		ui.Field("Dead Zone###screenDeadZone", value.screenDeadZone, AtLeast(0.0f).Tip("遊び（画面の割合。真ん中から端までが 1）。focus がこの中で動いても、カメラは動かない。0.1 なら、真ん中から左右へ画面の幅の 5% ずつ"));
		ui.Field(
		    "Soft Zone###screenSoftZone", value.screenSoftZone,
		    AtLeast(0.0f).Tip("focus を出さない範囲（画面の割合。真ん中から端までが 1）。Dead Zone の外ではなめらかに追い、ここに着いたらすぐ付いていく。1 より小さいと、focus はいつも画面の中。Dead Zone より小さい軸は Dead Zone と同じ"));
		break;
	}
	ui.BeginDisabled(value.plane != CameraFollowPlane::XY);
	ui.Field(
	    "Ground Snap Y###groundSnapY", value.groundSnapY,
	    Tip("追う物が地面にいる間は、縦の遊びを使わずに合わせる（跳んでも画面は揺れず、段に乗ると上がる・降りると下がる）。追う物に Rigidbody か Character Controller が要る。Plane が XY のときだけ"));
	ui.EndDisabled();
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("focus が Dead Zone の外へ出たとき、追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる"));
	ui.Field(
	    "Show Gizmo###showGizmo", value.showGizmo,
	    Tip("線を出す（エディタだけ）。緑：Dead Zone / 青：Soft Zone / 黄：focus（追う物から引いた線の先）/ 赤：Bounds。Screen の範囲は Game ビューだけ（今写しているカメラの物だけ）、ほかは Scene・Game ビュー。View メニューの Camera Follow Zones で全部まとめて消せる"));
	ui.Separator("Bounds");
	ui.BeginDisabled(value.plane == CameraFollowPlane::View); // View の面はカメラと一緒に回るので、決まった範囲を持てない
	ui.Field("Use Bounds###useBounds", value.useBounds, Tip("画面の端が、Min〜Max の外を写さない（ステージの端でカメラが止まる）。Plane が XY・XZ のとき"));
	ui.Field("Min###boundsMin", value.boundsMin, Tip("写してよい範囲の小さい方の端。XY はワールドの X・Y（追う物の奥行きで測る）、XZ はワールドの X・Z（追う物の高さで測る）"));
	ui.Field("Max###boundsMax", value.boundsMax, Tip("写してよい範囲の大きい方の端（XY はワールドの X・Y、XZ はワールドの X・Z）"));
	ui.EndDisabled();
}

//=============================================================================
// 追いかけ
//=============================================================================
bool CameraFollow::Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime) {
	// --- 早期リターン ---
	const Handle<Entity> targetEntity = EntityManager::Find(follow.target);
	const std::optional<FollowFrame> frameValue = MakeFollowFrame(entity, targetEntity, follow.plane);
	if (!frameValue.has_value()) {
		return false; // 追う物が入っていない・もう居ない
	}
	FollowFrame frame = *frameValue;
	const FollowAxes& axes = frame.axes;
	const Vector2 target = axes.ToPlane(frame.targetPosition);

	// --- 0. View の奥行きを保つ前後の動き ---
	// View の面はカメラと一緒に回るので、横・縦だけでは奥行きがずれていく（90 度回ると、奥にいた物が真横になる）。保つ奥行きへ戻す
	// 範囲を測る前にカメラを前後へ動かしておく（後で足すと、Screen の範囲で縛った後に画面の位置が伸び縮みして、Soft Zone から少し出る）
	// 前の向きは面の横・縦と直角なので、面の上の座標（frame.center）は変わらない
	Vector3 depthMove = {0.0f, 0.0f, 0.0f};
	if (follow.plane == CameraFollowPlane::View && follow.isFollowing) {
		depthMove = frame.frustum.forward * ((frame.depth - follow.viewDepth) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime));
		frame.frustum.position += depthMove;
		frame.viewCenter += depthMove;
	}

	// --- 1. 先読み（追い方で決まる focus のずれ）---
	// 速さは位置の変化から出す（Rigidbody が無い物・スクリプトで動かす物でも使える。壁に止められている間は 0）
	// 目標の先読みへ、指数でなめらかに近づける（速さはフレームごとにガタつくので、そのままだと画面が震える）
	if (follow.isFollowing) {
		const Vector2 velocity = (target - axes.ToPlane(follow.lastTargetPosition)) / deltaTime;
		const Vector2 wanted = CalcWantedLookAhead(follow, velocity);
		follow.currentLookAhead += (wanted - follow.currentLookAhead) * MathUtility::CalcSmoothRate(deltaTime, follow.lookAheadSmoothTime);
	}
	follow.lastTargetPosition = frame.targetPosition;

	// --- 2. 画面の真ん中に置きたい点（focus）---
	const Vector2 shift = follow.offset + follow.currentLookAhead;
	const Vector2 focus = target + shift;
	const Vector3 focusPoint = frame.targetPosition + axes.ToWorld(shift); // ワールドの点（Screen で画面に写すのに使う）

	// --- 3. 新しい真ん中 ---
	Vector2 center = frame.center;
	if (!follow.isFollowing) {
		// 追いかけ始め（Play・シーンを読んだ直後）：置いた位置から流れてこないように、すぐ focus へ
		center = focus;
		follow.viewDepth = frame.depth;
		follow.isFollowing = true;
	} else {
		const ZoneSizes zones = GetZoneSizes(follow);
		// 3-1. Dead Zone の外へ出た分だけ追う。Y は、追う物が地面にいる間は遊びを使わずに合わせる（Ground Snap Y）
		// 遊びだけだと、跳んで遊びの上へはみ出した分カメラが上がり、着地しても遊びの中なので戻らない（跳ぶたびに少しずつずれていく）
		// 地面にいるかは動き方の窓口に聞く（Rigidbody でも Character Controller でも同じ。どちらも無ければ地面にいない扱い）
		const bool isSnapY = (follow.plane == CameraFollowPlane::XY) && follow.groundSnapY && MotionQuery::IsGrounded(targetEntity);
		Vector2 wanted = KeepInZone(frame, follow.zoneSpace, center, focus, focusPoint, zones.dead);
		if (isSnapY) {
			wanted.y = focus.y;
		}
		// なめらかに近づける（指数補間。1フレームに「残りの差 × t」だけ進む）
		center += (wanted - center) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime);
		// 3-2. Soft Zone の外へは出さない（吹っ飛びのような速い動きでも、追う物が画面から出ない）
		center = KeepInZone(frame, follow.zoneSpace, center, focus, focusPoint, zones.soft);
	}

	// --- 4. ステージの端で止める（画面の端が、範囲の外を写さない）---
	// 範囲はワールドの軸（XY：X・Y、XZ：X・Z）で決まっているので、いったんワールドへ戻して縛り、面の座標へ戻す（カメラが回っていても、範囲は回らない）
	// 真ん中から画面の端までのずれは、四隅を面に当てて測る。カメラは面と平行に動き、向きは変えないので、真ん中を動かしてもこのずれは変わらない
	if (follow.useBounds) {
		const std::optional<BoundsAxes> boundsAxes = GetBoundsAxes(follow.plane);
		const std::optional<ViewExtent> extent =
		    boundsAxes.has_value() ? CalcViewExtent(frame.frustum, frame.targetPosition, frame.normal, frame.viewCenter, boundsAxes->a, boundsAxes->b) : std::nullopt;
		if (extent.has_value()) {
			const Vector3 moved = frame.viewCenter + axes.ToWorld(center - frame.center);
			const float a = Dot(moved, boundsAxes->a);
			const float b = Dot(moved, boundsAxes->b);
			const float boundedA = ApplyBounds(a, extent->min.x, extent->max.x, follow.boundsMin.x, follow.boundsMax.x);
			const float boundedB = ApplyBounds(b, extent->min.y, extent->max.y, follow.boundsMin.y, follow.boundsMax.y);
			center = axes.ToPlane(moved + boundsAxes->a * (boundedA - a) + boundsAxes->b * (boundedB - b));
		}
	}

	// --- 5. 動いた量を Transform へ（親の下に置いたカメラでも、ワールドで動いた量だけ動く）---
	const Vector3 move = axes.ToWorld(center - frame.center) + depthMove; // 0 の前後の動きも足す
	if (LengthSq(move) == 0.0f) {
		return false;
	}
	EntityManager::TranslateWorld(entity, move);
	return true;
}

//=============================================================================
// エディタの線
//=============================================================================
std::optional<CameraFollow::Guide> CameraFollow::CalcGuide(Handle<Entity> entity, const CameraFollowComponent& follow) {
	const std::optional<FollowFrame> frameValue = MakeFollowFrame(entity, EntityManager::Find(follow.target), follow.plane);
	if (!frameValue.has_value()) {
		return std::nullopt;
	}
	const FollowFrame& frame = *frameValue;
	// --- focus（Update と同じ。停止中は先読みが 0 なので、追う物 ＋ Offset）---
	const Vector2 shift = follow.offset + follow.currentLookAhead;
	const Vector2 focus = frame.axes.ToPlane(frame.targetPosition) + shift;
	const Vector3 focusPoint = frame.targetPosition + frame.axes.ToWorld(shift);
	const Vector3 toFocus = focusPoint - frame.frustum.position;
	if (Dot(toFocus, frame.frustum.forward) < kMinDepth) {
		return std::nullopt; // focus がカメラの後ろ（範囲を画面に重ねられない）
	}

	Guide guide;
	guide.target = frame.targetPosition;
	guide.focus = focusPoint;
	guide.horizontal = frame.axes.horizontal;
	guide.vertical = frame.axes.vertical;
	guide.markSize = Length(toFocus) * kGuideMarkScale;

	// --- Dead Zone・Soft Zone ---
	const ZoneSizes zones = GetZoneSizes(follow);
	switch (follow.zoneSpace) {
	case CameraFollowZoneSpace::World:
		guide.deadZone = MakeWorldRect(frame, focus, focusPoint, zones.dead);
		guide.softZone = MakeWorldRect(frame, focus, focusPoint, zones.soft);
		break;
	case CameraFollowZoneSpace::Screen: {
		// 追いかけと同じ面に置く（XZ なら地面の台形）。隅が地平線より上で面に写らなければ、2つともカメラの正面の面に置く
		std::optional<std::array<Vector3, 4>> dead = MakeScreenRect(frame.frustum, focusPoint, frame.normal, zones.dead);
		std::optional<std::array<Vector3, 4>> soft = MakeScreenRect(frame.frustum, focusPoint, frame.normal, zones.soft);
		if (!dead.has_value() || !soft.has_value()) {
			dead = MakeScreenRect(frame.frustum, focusPoint, frame.frustum.forward, zones.dead);
			soft = MakeScreenRect(frame.frustum, focusPoint, frame.frustum.forward, zones.soft);
		}
		if (!dead.has_value() || !soft.has_value()) {
			return std::nullopt; // focus がカメラの前にあれば、正面の面にはいつも写る（ここへは来ない）
		}
		guide.deadZone = *dead;
		guide.softZone = *soft;
		break;
	}
	}

	// --- Bounds（追いかけと同じく、XY は追う物の奥行き、XZ は追う物の高さの面に置く）---
	// Min・Max の向き（a・b）以外の向きは、追う物の値のまま（XY なら Z、XZ なら Y）
	const std::optional<BoundsAxes> boundsAxes = GetBoundsAxes(follow.plane);
	if (follow.useBounds && boundsAxes.has_value()) {
		const Vector3& position = frame.targetPosition;
		const auto corner = [&](float a, float b) { return position + boundsAxes->a * (a - Dot(position, boundsAxes->a)) + boundsAxes->b * (b - Dot(position, boundsAxes->b)); };
		const Vector2& min = follow.boundsMin;
		const Vector2& max = follow.boundsMax;
		guide.bounds = std::array<Vector3, 4>{corner(min.x, min.y), corner(max.x, min.y), corner(max.x, max.y), corner(min.x, max.y)};
	}
	return guide;
}
```

A まで写したらビルドが通る（`maxLag` を使っている所はエンジンの中には無い）。

## パートB Camera Follow の線

### B-1 `Editor/Gizmos/CameraGizmo.h`（全体を差し替え）

```cpp
#pragma once

// 前方宣言
class Camera;

/// <summary>
/// カメラをエディタで見せるためのギズモ
/// <para>Camera Component の置いてある所のアイコンと、Camera Path の線（Scene ビューだけ）</para>
/// <para>Camera Follow の Dead Zone・Soft Zone などの線（Game ビューでは、画面の上の範囲にぴったり重なる）</para>
/// <para>World の範囲・focus・Bounds は Scene・Game ビューの両方、Screen の範囲は Game ビューだけ（メインカメラの物だけ）</para>
/// </summary>
class CameraGizmo {
public:
	static void Initialize();

	// 描く物を積む（1フレームに1回）
	static void Draw(Camera* camera);

	// アイコンを出すか（View メニューから切り替える。クリックで選べるかも、これで決まる）
	static bool& GetShowIcons();
	// Camera Path の線を出すか（View メニューから切り替える）
	static bool& GetShowPaths();
	// Camera Follow の範囲の線を出すか（View メニューから全部まとめて切り替える。1つずつは Inspector の Show Gizmo）
	static bool& GetShowFollowZones();
};
```

### B-2 `Editor/Gizmos/CameraGizmo.cpp`（全体を差し替え）

Camera Path の線（`lines`）は Scene ビューだけの層なので、Camera Follow の線は別の置き場にする。World の範囲・focus・Bounds は `followLines`（両方のビュー）、Screen の範囲は `screenLines`（Game ビューだけ・メインカメラの物だけ）。

```cpp
#ifdef USE_IMGUI
#include "CameraGizmo.h"

#include <array>
#include <cstdint>
#include <span>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Math/Geometry/PathCurve.h"
#include "HTTEngine/Systems/Camera/CameraFollow.h"
#include "HTTEngine/Systems/Camera/CameraPath.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr float kNoFadeStart = 10000.0f; // 距離でフェードさせないための値
constexpr float kNoFadeEnd = 20000.0f;
constexpr Vector4 kPathColor = {0.3f, 0.8f, 1.0f, 1.0f};     // 線の色（水色）
constexpr Vector4 kPositionColor = {1.0f, 0.8f, 0.2f, 1.0f}; // 線の上の今の位置に立てる線と、アイコンからそこへ引く線の色
constexpr float kPositionMarkHeight = 0.5f;                  // 今の位置に立てる線の高さ（m）
// Camera Follow の範囲の色（Inspector の Show Gizmo の説明と合わせる）
constexpr Vector4 kDeadZoneColor = {0.35f, 1.0f, 0.45f, 1.0f}; // Dead Zone（緑）
constexpr Vector4 kSoftZoneColor = {0.35f, 0.6f, 1.0f, 1.0f};  // Soft Zone（青）
constexpr Vector4 kFocusColor = {1.0f, 0.85f, 0.2f, 1.0f};     // focus の印と、追う物から focus へ引く線（黄）
constexpr Vector4 kBoundsColor = {1.0f, 0.35f, 0.3f, 1.0f};    // Use Bounds の範囲（赤）

uint32_t iconTextureHandle = 0;       // カメラのアイコン
bool isShowIcons = true;              // アイコンを出すか
bool isShowPaths = true;              // Camera Path の線を出すか
bool isShowFollowZones = true;        // Camera Follow の範囲の線を出すか
Renderer::LineListConfig lines;       // 線の置き場（配列のメモリを使い回す）
Renderer::LineListConfig followLines; // Camera Follow のワールドの線の置き場（Scene・Game ビュー。Scene ビューだけの lines と分ける）
Renderer::LineListConfig screenLines; // Camera Follow の Screen の範囲の置き場（Game ビューだけ）
PathCurve pathLine;                   // 描く線（使い回す）

// ===== 四角（4つの角を順に結ぶ）=====
void PushRect(std::vector<Renderer::LineSegment>& out, const std::array<Vector3, 4>& corners, const Vector4& color) {
	for (size_t i = 0; i < corners.size(); ++i) {
		out.push_back({corners[i], corners[(i + 1) % corners.size()], color});
	}
}

// ===== Camera Follow 1つ分の線を積む =====
// 範囲は focus を通る面の上にあるので、このカメラから見ると画面の上の範囲にぴったり重なる（Game ビューでは画面の四角に見える）
// World の範囲・focus・Bounds はワールドの物なので、Scene・Game ビューの両方に出す
// Screen の範囲は「このカメラから見た画面の四角」なので、ほかのカメラから見ても意味がない
// → Game ビューだけ（GameViewGuide の層）に、Game ビューを写しているカメラ（メインカメラ）の物だけを出す
void PushFollowGuide(const CameraFollow::Guide& guide, CameraFollowZoneSpace zoneSpace, bool isMainCamera) {
	std::vector<Renderer::LineSegment>& out = followLines.lines;
	switch (zoneSpace) {
	case CameraFollowZoneSpace::World:
		PushRect(out, guide.softZone, kSoftZoneColor);
		PushRect(out, guide.deadZone, kDeadZoneColor);
		break;
	case CameraFollowZoneSpace::Screen:
		if (isMainCamera) {
			PushRect(screenLines.lines, guide.softZone, kSoftZoneColor);
			PushRect(screenLines.lines, guide.deadZone, kDeadZoneColor);
		}
		break;
	}
	// focus の十字と、追う物から focus へ（Offset・先読みでどれだけずれているか）
	const Vector3 h = guide.horizontal * guide.markSize;
	const Vector3 v = guide.vertical * guide.markSize;
	out.push_back({guide.focus - h, guide.focus + h, kFocusColor});
	out.push_back({guide.focus - v, guide.focus + v, kFocusColor});
	out.push_back({guide.target, guide.focus, kFocusColor});
	if (guide.bounds.has_value()) {
		PushRect(out, *guide.bounds, kBoundsColor);
	}
}

// ===== アイコンを1つ描く =====
void DrawIcon(const Vector3& position, Camera* camera) {
	Renderer::Rect3dConfig icon;
	icon.textureHandle = iconTextureHandle;
	icon.shadingType = ShadingType::Unlit;
	icon.blendMode = BlendMode::Normal;
	icon.rasterizerType = RasterizerType::SolidNone;
	icon.depthMode = DepthMode::TestNoWrite; // 半透明として描く（深度を書かない）
	icon.isBillboard = true;
	icon.camera = camera;
	icon.layer = DrawLayer::EditorGizmo;
	icon.transform.translation = position;
	Renderer::DrawRect3d(icon);
}

// ===== Camera Path の線と、今の位置の印を積む =====
// アイコンは Transform の位置（ギズモ・クリックと同じ所）なので、写す位置へ線を引いて、どのカメラの位置か分かるようにする
void PushPath(Handle<Entity> entity, const CameraPathComponent& path, const TransformComponent& transform) {
	CameraPath::BuildLine(path, pathLine);
	const std::span<const Vector3> polyline = pathLine.GetPolyline();
	for (size_t i = 1; i < polyline.size(); ++i) {
		lines.lines.push_back({polyline[i - 1], polyline[i], kPathColor});
	}
	if (!pathLine.IsEmpty()) {
		const Vector3 position = CameraSystem::CalcCameraPosition(entity);
		lines.lines.push_back({
		    position, position + Vector3{0.0f, kPositionMarkHeight, 0.0f},
               kPositionColor
        });
		lines.lines.push_back({GetWorldPosition(transform), position, kPositionColor});
	}
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void CameraGizmo::Initialize() {
	iconTextureHandle = TextureManager::Load("HTTEngine/Resources/Textures/Camera.png");
	lines.fadeStartDistance = kNoFadeStart;
	lines.fadeEndDistance = kNoFadeEnd;
	lines.layer = DrawLayer::EditorGizmo;
	lines.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く
	// Camera Follow の範囲は Game ビューにも出す（画面のどこで追い始めるかを、遊ぶときの絵の上で見る）。物の後ろでも見えるように深度で隠さない
	followLines.fadeStartDistance = kNoFadeStart;
	followLines.fadeEndDistance = kNoFadeEnd;
	followLines.layer = DrawLayer::Default;
	followLines.depthMode = DepthMode::Disable;
	followLines.isGizmo = true;
	screenLines.fadeStartDistance = kNoFadeStart;
	screenLines.fadeEndDistance = kNoFadeEnd;
	screenLines.layer = DrawLayer::GameViewGuide;
	screenLines.depthMode = DepthMode::Disable;
	screenLines.isGizmo = true;
}

//=============================================================================
// 描く
//=============================================================================
void CameraGizmo::Draw(Camera* camera) {
	if (isShowIcons) {
		// Transform の位置に出す（Camera Path の写す位置に出すと、ギズモが何も無い所に出て、線の上のアイコンは別の Entity に見える）
		EntityManager::ForEach<CameraComponent>([camera](Handle<Entity> entity, const CameraComponent&) {
			const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
			if (transform != nullptr && EntityManager::IsActiveInHierarchy(entity)) {
				DrawIcon(GetWorldPosition(*transform), camera);
			}
		});
	}
	if (isShowPaths) {
		lines.camera = camera;
		lines.lines.clear();
		EntityManager::ForEach<CameraPathComponent>([](Handle<Entity> entity, const CameraPathComponent& path) {
			const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
			if (path.enabled && transform != nullptr && EntityManager::IsActiveInHierarchy(entity)) {
				PushPath(entity, path, *transform);
			}
		});
		Renderer::DrawLines(lines); // 線が0本なら中で何もしない
		lines.camera = nullptr;     // フレームをまたいで持たない
	}
	if (isShowFollowZones) {
		followLines.camera = camera;
		followLines.lines.clear();
		screenLines.camera = camera;
		screenLines.lines.clear();
		const Handle<Entity> mainCamera = CameraSystem::GetMainCameraEntity();
		EntityManager::ForEach<CameraFollowComponent>([mainCamera](Handle<Entity> entity, const CameraFollowComponent& follow) {
			if (!follow.enabled || !follow.showGizmo || !EntityManager::IsActiveInHierarchy(entity)) {
				return;
			}
			if (const std::optional<CameraFollow::Guide> guide = CameraFollow::CalcGuide(entity, follow)) {
				PushFollowGuide(*guide, follow.zoneSpace, entity == mainCamera);
			}
		});
		Renderer::DrawLines(followLines);
		Renderer::DrawLines(screenLines);
		followLines.camera = nullptr;
		screenLines.camera = nullptr;
	}
}

//=============================================================================
// 表示の設定
//=============================================================================
bool& CameraGizmo::GetShowIcons() { return isShowIcons; }

bool& CameraGizmo::GetShowPaths() { return isShowPaths; }

bool& CameraGizmo::GetShowFollowZones() { return isShowFollowZones; }
#endif```

### B-2b Game ビューだけの層（`RenderStates.h`・`SceneRenderer.h`・`SceneRenderer.cpp`・`EditorViewport.cpp`）

`DrawLayer::GameViewGuide` を足し、`SceneViewOptions::gameViewGuide`（初めは true）で描くかを決める。Scene ビューだけ false にする。

```diff
@@ -105,6 +105,7 @@ enum class DrawLayer {
 	Default,     // ふつうの物（いつも描く）
 	EditorGrid,  // エディタのグリッド（Scene・Game ビューで別々に切れる。View メニュー）
 	EditorGizmo, // エディタのギズモ（Scene ビューだけ描く。カメラのアイコンなど、ゲームの絵に入れない物）
+	GameViewGuide, // エディタの線のうち、Game ビューだけに描く物（Camera Follow の Screen の範囲など。ゲームのカメラから見た画面の四角なので、Scene ビューのカメラから見ても意味がない）
 };
 
 /// <summary>
```

```diff
@@ -23,6 +23,7 @@ struct SceneViewOptions {
 	bool depthOfField = true; // 被写界深度（Depth Of Field Component）を掛ける（ポストエフェクトを掛けるときだけ）
 	bool editorGrid = true;   // エディタのグリッド（DrawLayer::EditorGrid）を描く
 	bool editorGizmo = false; // エディタのギズモ（DrawLayer::EditorGizmo）を描く（Scene ビューだけ true）
+	bool gameViewGuide = true; // Game ビューだけの線（DrawLayer::GameViewGuide）を描く（Scene ビューだけ false）
 	bool sceneHidden = true;  // Hierarchy の目で隠した物（DrawTarget::isSceneHidden）を描く（Scene ビューだけ false）
 	bool cameraVolume = true; // カメラごとのポストエフェクト（Camera Volume）を掛ける（Scene ビューだけ false。別のカメラから見るので、ピントが合わない）
 	std::optional<SceneBackground> background; // このビューの背景（無ければメインカメラの背景。Scene ビューは View メニューの Skybox で決める）
```

```diff
@@ -54,6 +54,7 @@ void SceneRenderer::ApplyViewOptions(const SceneViewOptions& options) {
 	instance_->viewOptions_ = options;
 	RenderQueue::SetLayerVisible(DrawLayer::EditorGrid, options.editorGrid); // グリッドは描く物を1回だけ積み、ビューごとに飛ばす
 	RenderQueue::SetLayerVisible(DrawLayer::EditorGizmo, options.editorGizmo); // ギズモは描く物を1回だけ積み、ビューごとに飛ばす
+	RenderQueue::SetLayerVisible(DrawLayer::GameViewGuide, options.gameViewGuide); // Game ビューだけの線も同じ（Scene ビューで飛ばす）
 	RenderQueue::SetSceneHiddenVisible(options.sceneHidden); // 目で隠した物も同じ（積むのは1回、Scene ビューだけ飛ばす）
 }
 
```

```diff
@@ -103,7 +103,7 @@ void EditorViewport::Render(const Camera* gameCamera, const std::wstring& window
 	// 霧・被写界深度も Scene だけ切れる（遠くの物を霧やぼけに隠さずに置きたいとき）。グリッドは Scene・Game 別々に切れる
 	// 背景は Skybox を切ったらエディタのグラデーション（Scene ビューはカメラの Background を見ない。Unity の Scene ビューと同じ）
 	const SceneBackground sceneBackground = flags.sceneSkybox ? SceneBackground{} : MakeSceneViewBackground();
-	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .editorGizmo = true, .sceneHidden = false, .cameraVolume = false, .background = sceneBackground};
+	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .editorGizmo = true, .gameViewGuide = false, .sceneHidden = false, .cameraVolume = false, .background = sceneBackground};
 	const SceneViewOptions gameOptions = {.editorGrid = flags.gameGrid}; // 書かない物は初期値の true（霧・被写界深度はいつも掛ける）
 	SceneRenderer::RenderToTexture(debugCamera_.get(), sceneRT, windowTitle, sceneMode, sceneOptions); // Scene：レンズ以外
 	SceneRenderer::RenderToTexture(gameCamera, gameRT, windowTitle, PostEffectMode::All, gameOptions); // Game：全部
```

### B-3 `Editor/ImGuiManager.cpp`（View メニューに1行）

`DrawViewMenu` の `Camera Paths` の次に足す（`Particle Shapes` の行はパートC-6）。

```cpp
		ImGui::MenuItem("Camera Paths", nullptr, &CameraGizmo::GetShowPaths());
		ImGui::MenuItem("Camera Follow Zones", nullptr, &CameraGizmo::GetShowFollowZones()); // Dead Zone・Soft Zone（Game ビューにも出る。1つずつは Inspector の Show Gizmo）
```

## パートC Particle の出る形の線

### C-1 `Systems/Effects/ParticleSpawn.h`（新しい）

```cpp
#pragma once
#include <vector>

#include "HTTEngine/Components/Effects/ParticleSystemComponent.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"

/// <summary>
/// Particle System の Shape（粒が出る場所と向き）。ParticleSystem から使う
/// <para>粒を出す所と、エディタの線を同じファイルに置く（形を足す・変えるときに、出し方と線がずれないように）</para>
/// </summary>
namespace ParticleSpawn {
/// <summary>
/// 粒が出る場所と、飛ぶ向き（どちらも Entity から見た座標）
/// </summary>
struct SpawnPoint {
	Vector3 position;  // 出る場所
	Vector3 direction; // 飛ぶ向き（長さ1）
};

// 形の中（From Surface なら表面）から、出る場所と向きを1つ選ぶ
SpawnPoint Sample(const ParticleSystemComponent& component);

// 形の線を out に足す（worldMatrix：Entity のワールド行列。粒を出すときと同じく、位置・回転・大きさが効く）
void PushShapeGizmo(std::vector<Renderer::LineSegment>& out, const ParticleSystemComponent& component, const Matrix4x4& worldMatrix, const Vector4& color);
} // namespace ParticleSpawn
```

### C-2 `Systems/Effects/ParticleSpawn.cpp`（新しい）

`Sample` は今の `ParticleSystem.cpp` の `SampleShape` そのまま（Box の半分の大きさだけ `HalfBoxSize` にして、線と分け合う）。`RandomDirection`・`kMinDirectionLengthSq` も一緒に移す。

```cpp
#include "ParticleSpawn.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
#include "HTTEngine/Math/Geometry/AABB.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Math/RandomEngine.h"

namespace {
constexpr float kMinDirectionLengthSq = 1e-4f; // これより短い向きは、向きが決まらないとみなす
constexpr float kPointMarkSize = 0.1f;         // Point の印（3つの軸の十字）の半分の長さ（m）
constexpr float kConeGizmoLength = 1.0f;       // Cone の線で、開く向きを見せる長さ（m。粒が飛ぶ距離ではない）
constexpr int kConeSideLines = 4;              // Cone の、底の輪から開く向きへ引く線の数

// 長さ1のランダムな向き（球の中の点を長さ1に伸ばす。真ん中に近すぎて向きが決まらない点は選び直す）
Vector3 RandomDirection() {
	while (true) {
		const Vector3 point = RandomEngine::GetInsideUnitSphere();
		const float lengthSq = LengthSq(point);
		if (lengthSq > kMinDirectionLengthSq) {
			return point * (1.0f / std::sqrt(lengthSq));
		}
	}
}

// Box の半分の大きさ（負の値は正にする）
Vector3 HalfBoxSize(const ParticleSystemComponent& component) {
	return {std::abs(component.boxSize.x) * 0.5f, std::abs(component.boxSize.y) * 0.5f, std::abs(component.boxSize.z) * 0.5f};
}

// Cone の、縁（真ん中から outward へ radius 離れた所）から出た粒の飛ぶ向き。縁で Cone Angle だけ外へ傾く
Vector3 ConeRimDirection(const ParticleSystemComponent& component, const Vector3& outward) {
	const float tilt = component.coneAngle * MathUtility::kDegToRad;
	return outward * std::sin(tilt) + Vector3{0.0f, std::cos(tilt), 0.0f};
}
} // namespace

//=============================================================================
// 粒の出る場所と向き
//=============================================================================
// 中に均等に散らすため、真ん中からの距離は「0〜1 の乱数」をそのまま使わず、球なら3乗根・円なら平方根にする
// （球の中の体積は半径の3乗、円の面積は半径の2乗に比例するので、そのままだと真ん中に集まって見える）
ParticleSpawn::SpawnPoint ParticleSpawn::Sample(const ParticleSystemComponent& component) {
	switch (component.shape) {
	case ParticleShape::Point:
		return {
		    {0.0f, 0.0f, 0.0f},
            RandomDirection()
        };

	case ParticleShape::Sphere: {
		const Vector3 direction = RandomDirection();
		const float distance = component.fromSurface ? 1.0f : std::cbrt(RandomEngine::GetFloat(0.0f, 1.0f));
		return {direction * (component.radius * distance), direction};
	}

	case ParticleShape::Circle: {
		// XY の面（横から見るゲームで、画面と同じ向きの円）
		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
		const Vector3 direction = {std::cos(angle), std::sin(angle), 0.0f};
		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
		return {direction * (component.radius * distance), direction};
	}

	case ParticleShape::Cone: {
		// XZ の面の円から出て、上（+Y）へ。縁から出た粒ほど外へ傾ける（縁で Cone Angle。真ん中はまっすぐ上）
		// → 粒どうしが交差せず、ラッパの形に広がる。Radius 0 でも、傾きは乱数で決まるので広がる
		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
		const Vector3 outward = {std::cos(angle), 0.0f, std::sin(angle)};
		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
		const float tilt = component.coneAngle * MathUtility::kDegToRad * distance;
		const Vector3 direction = outward * std::sin(tilt) + Vector3{0.0f, std::cos(tilt), 0.0f};
		return {outward * (component.radius * distance), direction};
	}

	case ParticleShape::Box: {
		const Vector3 half = HalfBoxSize(component);
		Vector3 position = RandomEngine::GetVector3({-half.x, -half.y, -half.z}, half);
		if (component.fromSurface) {
			// 面を1つ選んで（広い面ほど選ばれやすく）、その軸だけ端へ寄せる
			const float areaX = half.y * half.z; // 左右の面の広さ（の 1/4。比べるだけなので、そろっていればよい）
			const float areaY = half.x * half.z; // 上下の面
			const float areaZ = half.x * half.y; // 前後の面
			const float pick = RandomEngine::GetFloat(0.0f, areaX + areaY + areaZ);
			const float side = (RandomEngine::GetInt(0, 1) == 0) ? -1.0f : 1.0f;
			if (pick < areaX) {
				position.x = half.x * side;
			} else if (pick < areaX + areaY) {
				position.y = half.y * side;
			} else {
				position.z = half.z * side;
			}
		}
		// 向きは箱の真ん中から外へ（ちょうど真ん中に出た粒は、どの向きでも）
		const Vector3 direction = (LengthSq(position) > kMinDirectionLengthSq) ? Normalize(position) : RandomDirection();
		return {position, direction};
	}
	}
	return {
	    {0.0f, 0.0f, 0.0f},
        RandomDirection()
    };
}

//=============================================================================
// エディタの線
//=============================================================================
// Entity から見た座標で線を足してから、足した分だけワールドへ写す（Transform の大きさで形が伸びるのも、粒を出すときと同じ）
void ParticleSpawn::PushShapeGizmo(std::vector<Renderer::LineSegment>& out, const ParticleSystemComponent& component, const Matrix4x4& worldMatrix, const Vector4& color) {
	const size_t first = out.size();
	const Vector3 origin = {0.0f, 0.0f, 0.0f};
	const Vector3 axisX = {1.0f, 0.0f, 0.0f};
	const Vector3 axisY = {0.0f, 1.0f, 0.0f};
	const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
	switch (component.shape) {
	case ParticleShape::Point:
		// 点は大きさが無いので、3つの軸の小さな十字
		out.push_back({axisX * -kPointMarkSize, axisX * kPointMarkSize, color});
		out.push_back({axisY * -kPointMarkSize, axisY * kPointMarkSize, color});
		out.push_back({axisZ * -kPointMarkSize, axisZ * kPointMarkSize, color});
		break;

	case ParticleShape::Sphere:
		GizmoLines::PushWireSphere(out, origin, component.radius, color);
		break;

	case ParticleShape::Circle:
		GizmoLines::PushCircle(out, origin, component.radius, axisX, axisY, color); // XY の面
		break;

	case ParticleShape::Cone: {
		// 底の輪（XZ の面）と、縁から出た粒の向きへ kConeGizmoLength 進んだ所の輪。間を何本かの線で結ぶ（ラッパの形）
		const float tilt = component.coneAngle * MathUtility::kDegToRad;
		const Vector3 topCenter = axisY * (kConeGizmoLength * std::cos(tilt));
		const float topRadius = component.radius + kConeGizmoLength * std::sin(tilt);
		GizmoLines::PushCircle(out, origin, component.radius, axisX, axisZ, color);
		GizmoLines::PushCircle(out, topCenter, topRadius, axisX, axisZ, color);
		for (int i = 0; i < kConeSideLines; ++i) {
			const float angle = MathUtility::kTwoPi * static_cast<float>(i) / static_cast<float>(kConeSideLines);
			const Vector3 outward = {std::cos(angle), 0.0f, std::sin(angle)};
			const Vector3 rim = outward * component.radius;
			out.push_back({rim, rim + ConeRimDirection(component, outward) * kConeGizmoLength, color});
		}
		break;
	}

	case ParticleShape::Box: {
		const Vector3 half = HalfBoxSize(component);
		const std::array<Vector3, 8> corners = AABB{-half, half}.GetCorners();
		for (const auto& [from, to] : kBoxEdges) {
			out.push_back({corners[from], corners[to], color});
		}
		break;
	}
	}
	for (size_t i = first; i < out.size(); ++i) {
		out[i].start = TransformPoint(out[i].start, worldMatrix);
		out[i].end = TransformPoint(out[i].end, worldMatrix);
	}
}
```

### C-3 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

`ParticleSystem.cpp`・`ParticleSystem.h` の前の行に足す。

```xml
    <ClCompile Include="HTTEngine\Systems\Effects\ParticleSpawn.cpp" />
```
```xml
    <ClInclude Include="HTTEngine\Systems\Effects\ParticleSpawn.h" />
```

filters：

```xml
    <ClCompile Include="HTTEngine\Systems\Effects\ParticleSpawn.cpp">
      <Filter>HTTEngine\Systems\Effects</Filter>
    </ClCompile>
```
```xml
    <ClInclude Include="HTTEngine\Systems\Effects\ParticleSpawn.h">
      <Filter>HTTEngine\Systems\Effects</Filter>
    </ClInclude>
```

### C-4 `Components/Effects/ParticleSystemComponent.h`（1行）

```diff
@@ -71,6 +71,7 @@ struct ParticleSystemComponent {
 	float coneAngle = 25.0f;              // Cone の開き（度。真上から何度まで）
 	Vector3 boxSize = {1.0f, 1.0f, 1.0f}; // Box の大きさ（m）
 	bool fromSurface = false;             // 形の表面（縁）から出す（円の輪から飛び散る）
+	bool showGizmo = true;                // Scene ビューに、形を線で出す（エディタだけ。View メニューの Particle Shapes で全部まとめて消せる）
 
 	// ===== 粒の初めの値（Min〜Max の乱数）=====
 	Vector2 lifetime = {0.5f, 1.0f};           // 消えるまでの秒
```

### C-5 `Systems/Effects/ParticleSystem.h`・`ParticleSystem.cpp`

- `ParticleSystem.cpp` の `// 長さ1のランダムな向き` から `SampleShape` の終わりまで（「出る場所と向き（Shape）」の大見出しごと）と、`kMinDirectionLengthSq` を消す（C-2 へ移した）
- `SampleShape(component)` → `ParticleSpawn::Sample(component)`
- 線の置き場・`showGizmos`・Show Gizmo の項目・`DrawGizmos`・`GetShowGizmos` を足す

```diff
@@ -3,6 +3,9 @@
 #include "HTTEngine/Core/Handle.h"
 #include "HTTEngine/ECS/Entity.h"
 
+// 前方宣言
+class Camera;
+
 /// <summary>
 /// ParticleSystemComponent を持つEntityの粒を出し・動かし・描く
 /// <para>粒は実行中だけの状態（保存しない。Play / Stop で消える。停止中は持たない。ただし Inspector の Preview 中の1つだけは動かす）</para>
@@ -28,6 +31,12 @@ public:
 	// 粒の描画リクエストを積む（WindowManager::DrawAll から。粒はどのビューでもカメラの方を向く）
 	static void Draw();
 
+	// ===== エディタの線 =====
+	// 粒の出る形を線で描く（Scene ビューだけ。Show Gizmo を入れた物）
+	static void DrawGizmos(Camera* camera);
+	// 線を出すか（メニューバーの View から、全部まとめて切り替える）
+	static bool& GetShowGizmos();
+
 	// ===== ゲームから（Script・SYSTEM の中で）使う =====
 
 	// 最初から出し始める（Burst も出る）。出している途中で呼んでも最初から。Play On Start を外した物は、これを呼ぶまで出さない
```

```diff
@@ -16,6 +16,7 @@
 #include "HTTEngine/Graphics/Renderer/Renderer.h"
 #include "HTTEngine/Math/Color.h"
 #include "HTTEngine/Math/RandomEngine.h"
+#include "HTTEngine/Systems/Effects/ParticleSpawn.h"
 #include "HTTEngine/Time/EngineTime.h"
 #ifdef USE_IMGUI
 #include "HTTEngine/Editor/Windows/HierarchyWindow.h"
@@ -25,7 +26,7 @@
 namespace {
 constexpr float kMinDuration = 0.01f;          // Duration の下限（秒。0 だとくり返しが止まらないので）
 constexpr float kMinLifetime = 0.01f;          // 寿命の下限（秒。0 で割らないように）
-constexpr float kMinDirectionLengthSq = 1e-4f; // これより短い向きは、向きが決まらないとみなす
+constexpr Vector4 kGizmoColor = {1.0f, 0.6f, 0.9f, 1.0f}; // 粒の出る形の線の色（桃色。Collider・カメラの線と分ける）
 
 /// <summary>
 /// 生きている粒1つ（実行中だけ。保存しない）
@@ -66,6 +67,8 @@ struct ParticleSystemState {
 	std::vector<ParticleData> instances; // 描くときの粒の置き場（Draw の中だけで使う。配列のメモリを使い回す）
 	Handle<Entity> previewEntity;        // 停止中にも流す1つ（Inspector の Preview。無効なら無し）
 	bool wasGameRunning = false;         // ゲームが動いているとして粒を持っているか（Play / Stop の切り替わりで、粒を全部捨てる。SwitchGameRunning）
+	Renderer::LineListConfig gizmoLines; // 出る形の線の置き場（配列のメモリを使い回す）
+	bool showGizmos = true;              // 出る形の線を出すか（View メニュー。1つずつは Component の Show Gizmo）
 };
 std::unique_ptr<ParticleSystemState> state;
 
@@ -123,93 +126,6 @@ float RandomRange(const Vector2& range) {
 	return (low < high) ? RandomEngine::GetFloat(low, high) : low;
 }
 
-// 長さ1のランダムな向き（球の中の点を長さ1に伸ばす。真ん中に近すぎて向きが決まらない点は選び直す）
-Vector3 RandomDirection() {
-	while (true) {
-		const Vector3 point = RandomEngine::GetInsideUnitSphere();
-		const float lengthSq = LengthSq(point);
-		if (lengthSq > kMinDirectionLengthSq) {
-			return point * (1.0f / std::sqrt(lengthSq));
-		}
-	}
-}
-
-//=============================================================================
-// 出る場所と向き（Shape）
-//=============================================================================
-/// <summary>
-/// 粒が出る場所と、飛ぶ向き（どちらも Entity から見た座標）
-/// </summary>
-struct SpawnPoint {
-	Vector3 position;  // 出る場所
-	Vector3 direction; // 飛ぶ向き（長さ1）
-};
-
-// 形の中（From Surface なら表面）から、出る場所と向きを1つ選ぶ
-// 中に均等に散らすため、真ん中からの距離は「0〜1 の乱数」をそのまま使わず、球なら3乗根・円なら平方根にする
-// （球の中の体積は半径の3乗、円の面積は半径の2乗に比例するので、そのままだと真ん中に集まって見える）
-SpawnPoint SampleShape(const ParticleSystemComponent& component) {
-	switch (component.shape) {
-	case ParticleShape::Point:
-		return {
-		    {0.0f, 0.0f, 0.0f},
-            RandomDirection()
-        };
-
-	case ParticleShape::Sphere: {
-		const Vector3 direction = RandomDirection();
-		const float distance = component.fromSurface ? 1.0f : std::cbrt(RandomEngine::GetFloat(0.0f, 1.0f));
-		return {direction * (component.radius * distance), direction};
-	}
-
-	case ParticleShape::Circle: {
-		// XY の面（横から見るゲームで、画面と同じ向きの円）
-		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
-		const Vector3 direction = {std::cos(angle), std::sin(angle), 0.0f};
-		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
-		return {direction * (component.radius * distance), direction};
-	}
-
-	case ParticleShape::Cone: {
-		// XZ の面の円から出て、上（+Y）へ。縁から出た粒ほど外へ傾ける（縁で Cone Angle。真ん中はまっすぐ上）
-		// → 粒どうしが交差せず、ラッパの形に広がる。Radius 0 でも、傾きは乱数で決まるので広がる
-		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
-		const Vector3 outward = {std::cos(angle), 0.0f, std::sin(angle)};
-		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
-		const float tilt = component.coneAngle * MathUtility::kDegToRad * distance;
-		const Vector3 direction = outward * std::sin(tilt) + Vector3{0.0f, std::cos(tilt), 0.0f};
-		return {outward * (component.radius * distance), direction};
-	}
-
-	case ParticleShape::Box: {
-		const Vector3 half = {std::abs(component.boxSize.x) * 0.5f, std::abs(component.boxSize.y) * 0.5f, std::abs(component.boxSize.z) * 0.5f};
-		Vector3 position = RandomEngine::GetVector3({-half.x, -half.y, -half.z}, half);
-		if (component.fromSurface) {
-			// 面を1つ選んで（広い面ほど選ばれやすく）、その軸だけ端へ寄せる
-			const float areaX = half.y * half.z; // 左右の面の広さ（の 1/4。比べるだけなので、そろっていればよい）
-			const float areaY = half.x * half.z; // 上下の面
-			const float areaZ = half.x * half.y; // 前後の面
-			const float pick = RandomEngine::GetFloat(0.0f, areaX + areaY + areaZ);
-			const float side = (RandomEngine::GetInt(0, 1) == 0) ? -1.0f : 1.0f;
-			if (pick < areaX) {
-				position.x = half.x * side;
-			} else if (pick < areaX + areaY) {
-				position.y = half.y * side;
-			} else {
-				position.z = half.z * side;
-			}
-		}
-		// 向きは箱の真ん中から外へ（ちょうど真ん中に出た粒は、どの向きでも）
-		const Vector3 direction = (LengthSq(position) > kMinDirectionLengthSq) ? Normalize(position) : RandomDirection();
-		return {position, direction};
-	}
-	}
-	return {
-	    {0.0f, 0.0f, 0.0f},
-        RandomDirection()
-    };
-}
-
 //=============================================================================
 // 出す・動かす
 //=============================================================================
@@ -219,7 +135,7 @@ void EmitParticles(EmitterState& emitter, const ParticleSystemComponent& compone
 	const int room = limit - static_cast<int>(emitter.particles.size());
 	const int spawnCount = (std::min)(count, room);
 	for (int i = 0; i < spawnCount; ++i) {
-		SpawnPoint point = SampleShape(component);
+		ParticleSpawn::SpawnPoint point = ParticleSpawn::Sample(component);
 		// World：出た瞬間のワールド座標にする（この後 Entity が動いても付いていかない）
 		if (component.space == ParticleSpace::World) {
 			point.position = TransformPoint(point.position, worldMatrix);
@@ -387,6 +303,7 @@ void DescribeParticleSystem(ComponentUI& ui, ParticleSystemComponent& value) {
 	ui.Field("Cone Angle###coneAngle", value.coneAngle, Range(0.0f, 90.0f).Tip("Cone の開き（度）。縁から出た粒ほど外へ傾く"));
 	ui.Field("Box Size###boxSize", value.boxSize, AtLeast(0.0f).Tip("Box の大きさ（m）"));
 	ui.Field("From Surface###fromSurface", value.fromSurface, Tip("形の表面（縁）から出す。Circle なら輪から飛び散る"));
+	ui.Field("Show Gizmo###showGizmo", value.showGizmo, Tip("Scene ビューに、粒の出る形を線で出す（エディタだけ。桃色）。Cone は縁から出た粒の開く向きも。View メニューの Particle Shapes で全部まとめて消せる"));
 
 	ui.Separator("Start (Min, Max)");
 	ui.Field("Lifetime###lifetime", value.lifetime, AtLeast(kMinLifetime).Tip("消えるまでの秒。左と右の間でランダム"));
@@ -435,6 +352,10 @@ void ParticleSystem::Initialize() {
 	EntityManager::RegisterComponent<ParticleSystemComponent>();
 	ComponentSerializer::Register<ParticleSystemComponent>("Particle System", &DescribeParticleSystem);
 	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ParticleSystemComponent>>("Particle System", "Effects", &DescribeParticleSystem));
+
+	// 出る形の線は Scene ビューだけ（置く場所を決める助け。ゲームの絵には入れない）
+	state->gizmoLines.layer = DrawLayer::EditorGizmo;
+	state->gizmoLines.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く
 }
 
 void ParticleSystem::Release() { state.reset(); }
@@ -541,6 +462,34 @@ void ParticleSystem::Draw() {
 	}
 }
 
+//=============================================================================
+// ギズモ
+//=============================================================================
+// 粒が出ているかに関係なく、有効な Particle System の形を全部描く（停止中に置く場所を決めるため）
+void ParticleSystem::DrawGizmos(Camera* camera) {
+	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
+	if (!state->showGizmos) {
+		return;
+	}
+	Renderer::LineListConfig& gizmo = state->gizmoLines;
+	gizmo.lines.clear(); // 中身だけ消す。確保済みのメモリは残るので、次から再確保が起きにくい
+	gizmo.camera = camera;
+	EntityManager::ForEach<ParticleSystemComponent>([&gizmo](Handle<Entity> entity, const ParticleSystemComponent& component) {
+		// 出さない物（無効・隠した Entity）と、Hierarchy の目で隠した物は描かない
+		if (!component.showGizmo || IsSleeping(entity, component) || SceneVisibility::IsHidden(entity)) {
+			return;
+		}
+		ParticleSpawn::PushShapeGizmo(gizmo.lines, component, WorldMatrixOf(entity), kGizmoColor);
+	});
+	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
+	gizmo.camera = nullptr;     // フレームをまたいで持たない
+}
+
+bool& ParticleSystem::GetShowGizmos() {
+	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
+	return state->showGizmos;
+}
+
 //=============================================================================
 // ゲームから使う
 //=============================================================================
```

### C-6 `Systems/EngineSystems.cpp`・`Editor/ImGuiManager.cpp`

```diff
@@ -283,6 +283,7 @@ void EngineSystems::Draw(Camera* gameCamera, IBLEnvironment* gameEnvironment) {
 	CharacterControllerSystem::DrawGizmos(gameCamera); // 上れる段の高さ・地面の向き（Collider のギズモと一緒に出す・消す）
 	WindZoneSystem::DrawGizmos(gameCamera); // 風の範囲と向き（Collider のギズモと一緒に出す・消す）
 	DecalRenderSystem::DrawGizmos(gameCamera); // デカールの箱（Scene ビューだけ）
+	ParticleSystem::DrawGizmos(gameCamera);    // 粒の出る形（Scene ビューだけ）
 	HitSystem::DrawGizmos(gameCamera);      // 攻撃の形（段階で色が変わる）・ジャスト判定の範囲・Hurtbox
 	AnimationSystem::DrawGizmos(gameCamera);
 #endif
```

`ImGuiManager.cpp`：include と、View メニュー（B-3 の行の次）。

```cpp
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
```
```cpp
		ImGui::MenuItem("Particle Shapes", nullptr, &ParticleSystem::GetShowGizmos());      // 粒の出る形（Scene ビューだけ。1つずつは Inspector の Show Gizmo）
```

## パートD Sandbox と Reference

### D-1 `project/Sandbox/Resources/scenes/CameraFollowZoneTest.scene.json`（新しい）

Player（Prefab。WASD で歩く）を、XY・Screen の Camera Follow で追う。Bounds も入れてある。奥に 5 つの Shape の Particle System（Box は回してある）。

```json
{
	"version": 5,
	"entities": [
		{
			"id": 1,
			"name": "GameCamera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, -12.0], "rotation": [8.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 50.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 0, "blendStyle": "Cut", "blendTime": 1.0, "blendEase": "InOutCubic", "timeMode": "Game"},
				"Camera Follow": {
					"enabled": true,
					"target": 15981731572122588503,
					"plane": "XY",
					"mode": "Center",
					"offset": [0.0, 1.0],
					"zoneSpace": "Screen",
					"screenDeadZone": [0.2, 0.25],
					"screenSoftZone": [0.6, 0.7],
					"groundSnapY": false,
					"smoothTime": 0.4,
					"showGizmo": true,
					"useBounds": true,
					"boundsMin": [-14.0, -1.0],
					"boundsMax": [14.0, 12.0]
				}
			}
		},
		{
			"id": 2,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, 0.0], "rotation": [50.0, -30.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Directional Light": {"color": [1.0, 1.0, 1.0], "intensity": 1.0}
			}
		},
		{
			"id": 3,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, -0.05, 2.0], "rotation": [0.0, 0.0, 0.0], "scale": [28.0, 0.1, 12.0]},
				"Collider": {"enabled": true, "isTrigger": false, "shape": "Box", "center": [0.0, 0.0, 0.0], "size": [1.0, 1.0, 1.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [0.45, 0.47, 0.5, 1.0]}
			}
		},
		{
			"id": 4,
			"name": "Step",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [6.0, 1.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [4.0, 2.0, 3.0]},
				"Collider": {"enabled": true, "isTrigger": false, "shape": "Box", "center": [0.0, 0.0, 0.0], "size": [1.0, 1.0, 1.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [0.9, 0.5, 0.2, 1.0]}
			}
		},
		{
			"id": 15981731572122588503,
			"name": "Player",
			"parent": 0,
			"active": true,
			"prefab": "resources/prefabs/Player.prefab",
			"components": {
				"Transform": {"translation": [0.0, 1.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]}
			}
		},
		{
			"id": 10,
			"name": "Particle Point",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [-8.0, 1.5, 4.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Particle System": {"enabled": true, "rateOverTime": 20.0, "shape": "Point", "lifetime": [0.6, 1.0], "speed": [0.5, 1.0], "size": [0.08, 0.12], "colorA": [1.0, 0.8, 0.3, 1.0], "colorB": [1.0, 0.5, 0.2, 1.0]}
			}
		},
		{
			"id": 11,
			"name": "Particle Sphere",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [-4.0, 1.5, 4.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Particle System": {"enabled": true, "rateOverTime": 20.0, "shape": "Sphere", "radius": 0.8, "lifetime": [0.6, 1.0], "speed": [0.3, 0.6], "size": [0.08, 0.12], "colorA": [0.4, 0.8, 1.0, 1.0], "colorB": [0.6, 0.9, 1.0, 1.0]}
			}
		},
		{
			"id": 12,
			"name": "Particle Circle",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 1.5, 4.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Particle System": {"enabled": true, "rateOverTime": 30.0, "shape": "Circle", "radius": 1.0, "fromSurface": true, "lifetime": [0.6, 1.0], "speed": [0.5, 1.0], "size": [0.08, 0.12], "colorA": [0.5, 1.0, 0.5, 1.0], "colorB": [0.8, 1.0, 0.6, 1.0]}
			}
		},
		{
			"id": 13,
			"name": "Particle Cone",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [4.0, 0.2, 4.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Particle System": {"enabled": true, "rateOverTime": 30.0, "shape": "Cone", "radius": 0.4, "coneAngle": 30.0, "lifetime": [0.8, 1.2], "speed": [1.5, 2.0], "size": [0.08, 0.12], "colorA": [1.0, 0.4, 0.3, 1.0], "colorB": [1.0, 0.7, 0.3, 1.0]}
			}
		},
		{
			"id": 14,
			"name": "Particle Box (Rotated)",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [8.0, 1.5, 4.0], "rotation": [0.0, 30.0, 15.0], "scale": [1.0, 1.0, 1.0]},
				"Particle System": {"enabled": true, "rateOverTime": 30.0, "shape": "Box", "boxSize": [2.0, 1.0, 1.0], "fromSurface": true, "lifetime": [0.6, 1.0], "speed": [0.2, 0.4], "size": [0.08, 0.12], "colorA": [0.9, 0.5, 1.0, 1.0], "colorB": [1.0, 0.7, 1.0, 1.0]}
			}
		}
	]
}
```

### D-2 `project/Sandbox/Resources/scenes/Player.scene.json`

```diff
@@ -28,10 +28,12 @@
 					"plane": "View",
 					"mode": "Center",
 					"offset": [0.0, 1.5],
+					"zoneSpace": "World",
 					"deadZone": [0.5, 0.0],
+					"softZone": [6.0, 4.0],
 					"groundSnapY": true,
 					"smoothTime": 0.15,
-					"maxLag": [6.0, 4.0],
+					"showGizmo": true,
 					"useBounds": false,
 					"boundsMin": [-10.0, -10.0],
 					"boundsMax": [100.0, 30.0]
```

### D-3 `Docs/Reference/`

```diff
@@ -42,14 +42,18 @@ void MoveCameraUp(float deltaTime) {
 | Mode | 追い方。`Center`（追う物を真ん中に）／`LookAhead`（追う物の速さ × Look Ahead Time だけ先を真ん中に。止まると戻る）。選んだ追い方の値だけが出る |
 | Look Ahead Time・Max・Smooth Time（LookAhead のとき） | 何秒先を見せるか（X 0.4・Y 0。XY で Y を入れると跳ぶたびに上下に揺れる）・先読みの上限（m）・先読みが変わるまでの時間（指数補間） |
 | Offset | 追う物から、画面の真ん中をどれだけずらすか（m）。XY で Y を上げると足元が広く見える |
-| Dead Zone | 遊び（m）。真ん中から横・縦にこの範囲で動いても、カメラは動かない |
+| Zone Space | Dead Zone・Soft Zone を何で測るか。`World`（Plane の面の上の長さ m。カメラが寄っても引いても、追い始める距離は同じ）／`Screen`（画面の割合。真ん中 0・端 1。Camera Look At の Screen Position と同じ測り方。カメラが引くとワールドでは広がる）。選んだ方の値だけが出る |
+| Dead Zone | 遊び。focus（追う物 ＋ Offset ＋ 先読み）が、画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない |
+| Soft Zone | focus を出さない範囲。Dead Zone の外ではなめらかに（Smooth Time）追い、ここに着いたらすぐ付いていく（速く動いても画面から出ない。前の Max Lag）。Dead Zone より小さい軸は Dead Zone と同じ |
 | Ground Snap Y | Plane が XY のとき。追う物（Rigidbody か Character Controller）が地面にいる間は、縦の遊びを使わずに合わせる。跳んでも画面は揺れず、段に乗ると上がる |
-| Smooth Time | 追いつくまでの時間の目安（秒）。走っている間は「速さ × Smooth Time」くらい遅れる |
-| Max Lag | 遅れの上限（m）。速く動いても、追う物が画面から出ない |
+| Smooth Time | focus が Dead Zone の外へ出たとき、追いつくまでの時間の目安（秒）。走っている間は「速さ × Smooth Time」くらい遅れる |
+| Show Gizmo | 線を出す（エディタだけ）。緑：Dead Zone／青：Soft Zone／黄：focus の十字と、追う物からの線／赤：Bounds。World の範囲・focus・Bounds は Scene・Game ビュー、Screen の範囲は Game ビューだけ（今写しているカメラの物だけ）。View → Gizmos → Camera Follow Zones で全部まとめて消せる |
 | Bounds（Use Bounds・Min・Max） | 画面の端が Min〜Max の外を写さない（ステージの端で止まる）。XY はワールドの X・Y で追う物の奥行きで測る。XZ はワールドの X・Z で追う物の高さで測る（カメラが回っても範囲は回らない）。View には無い |
 
 - 横・縦の向きは、XY はワールドの X・Y。XZ はカメラの右とカメラの前を地面に寝かせた向き、View はカメラの右・上で、カメラの Transform を回すと毎フレーム付いてくる（追う物のまわりを回る動きになる）。
-- Camera Follow が読む向きは Transform の向きだけ。Camera Look At の向きは読まない。
+- Camera Follow が読む向きは Transform の向きだけ。Camera Look At の向きは読まない（Screen の範囲も、Transform の向きと Camera の Fov Y で測る）。
+- 範囲の線は focus を通る面の上に描くので、Game ビューでは画面の上の範囲にぴったり重なる（Screen なら画面の四角、XZ の World なら地面の台形）。
+- 「画面の真ん中」は、カメラの前の向きが追う物を通る面（XY：追う物の奥行き、XZ：追う物の高さ）と交わる点。傾けたカメラでも、追い始め・Ground Snap Y で focus は画面の真ん中に来る。Bounds の画面の端も、画面の四隅をその面に当てて測る（下へ傾けると、下の方が遠くまで写る）。
 - 動かすのは物理の後・`CameraSystem::Update` の前（[1フレームの順番](01-Overview.md#1フレームの順番) の 5）なので、追う物と1フレームずれない。
 - 自分で動かしたいカメラ（演出など）は、Camera Follow を外す（Enabled）か、付けずに SYSTEM・Script で Transform を動かす（上の例）。
 
```

```diff
@@ -10,7 +10,7 @@
 |---|---|---|
 | Playback | Play On Start・Looping・Duration・Stop Action | Play で流し始める（外すと `ParticleSystem::Play` まで出さない）／Duration 秒ごとにくり返す（Burst もまた）／1回の長さ／出し終わって（Looping でなく Duration が過ぎた・`Stop` を呼んだ）粒が全部消えたらすること（`None`：何もしない・`Destroy`：この Entity を子ごと消す） |
 | Emission | Rate Over Time・Burst Count・Max Particles | 1秒に出す数／1回の始めにまとめて出す数／同時に出ている数の上限（4096 まで。いっぱいの間は出さない） |
-| Shape | Shape・Radius・Cone Angle・Box Size・From Surface | Point（全部の向き）・Sphere・Circle（**XY の面**。横から見る輪）・Cone（+Y へ開く）・Box。From Surface で表面（縁）から。向きは真ん中から外へ。Entity の向きで回る |
+| Shape | Shape・Radius・Cone Angle・Box Size・From Surface・Show Gizmo | Point（全部の向き）・Sphere・Circle（**XY の面**。横から見る輪）・Cone（+Y へ開く）・Box。From Surface で表面（縁）から。向きは真ん中から外へ。Entity の向きで回る。Show Gizmo で形を Scene ビューに桃色の線で出す（Cone は縁から出た粒の開く向きも。View → Gizmos → Particle Shapes で全部まとめて消せる） |
 | Start (Min, Max) | Lifetime・Speed・Size・Color A・Color B・Intensity | 左と右の間でランダム（Size は m で板の1辺）。色は A と B の間。Intensity は全部の粒の明るさの倍率（1 より大きいとブルームで光る） |
 | Over Lifetime | Gravity・Drag・End Size Rate・End Color・Ease・Converge・Converge Ease | 加速（m/s²）／1秒あたりの減速の割合／最後の大きさ（初めに掛ける。0 で縮んで消える）／最後の色（初めに掛ける。A 0 でフェードアウト）／大きさと色の変わり方／中心（Entity の位置）へ集まり、寿命の終わりにちょうど着く（Speed・Gravity・Drag は使わない。From Surface と合わせると輪・球の表面から吸い込まれる）／集まり方（InQuad：だんだん速く） |
 | Rotation | Start Rotation・Angular Velocity・Random Spin Direction・Align To Velocity・Stretch | 画面の上で板を回す（度。**正で左回り**＝Transform の Rotation Z と同じ）。初めの角度・回る速さ（度/秒）は左と右の間でランダム／半分の粒を逆に／絵の右（+X）を進む向きへ／絵の横だけ伸ばす（Align と合わせて線） |
```

```diff
@@ -22,7 +22,7 @@
 | **Project** | `resources/` のファイル。左にフォルダの木、右に中身（画像は小さい絵）。ダブルクリックで開く（シーン・`.uiscreen`・`.timeline`・コード）。右クリックで Create（Folder・Scene・Physic Material・Attack Table・Curve・Material・Timeline・UI Screen）・Rename（F2。ほかのシーン・Prefab などに書いてあるパスも書き換える。コードは書き換えず警告だけ）・Delete（ごみ箱へ）・Copy Path・Show in Explorer、Prefab は Place in Scene（原点に置く）。`.physmat`・`.material`・`.curve`・`.attacktable` などは選ぶと Inspector で直せる。右上の数字でアイコンの大きさ |
 | **File** | New Scene（Ctrl+N）・Open Scene・Save（Ctrl+S）・Save As（Ctrl+Shift+S）。停止中だけ |
 | **Settings（Edit → Settings）** | 調整項目（前の Parameters ウィンドウ）。Project：Audio（音量）・PostEffect・Physics（World → Gravity）・Layers・DamageTypes・ゲームのグループ／Scene：開いているシーンの名前だけ（まだ項目は無い）／Editor：Scene View（Scene ビューのカメラ）。ページごとに Save / Revert、保存していなければ `*`（[16章](16-LogAssertParams.md)） |
-| **View → Gizmos** | Light Icons・Light Ranges（ライトのアイコンと範囲）・Camera Icons・Camera Paths（カメラのアイコンと線）は Scene ビューだけに出る。Colliders（Collider の線。Character Controller・Wind Zone の線も一緒）・Hit Shapes（攻撃の形・ジャスト判定の範囲・Hurtbox）・Skeletons（骨）は Game ビューにも出る（Release では出ない） |
+| **View → Gizmos** | Light Icons・Light Ranges（ライトのアイコンと範囲）・Camera Icons・Camera Paths（カメラのアイコンと線）・Particle Shapes（粒の出る形）は Scene ビューだけに出る。Camera Follow Zones（Camera Follow の Dead Zone・Soft Zone・Bounds。Zone Space が Screen の範囲だけは Game ビューだけ）・Colliders（Collider の線。Character Controller・Wind Zone の線も一緒）・Hit Shapes（攻撃の形・ジャスト判定の範囲・Hurtbox）・Skeletons（骨）は Game ビューにも出る（Release では出ない）。Camera Follow と Particle System は、1つずつ Inspector の Show Gizmo でも消せる |
 | **View → Scene View → Post Effects** | Scene ビューにもポストエフェクト（ブルームなど）を掛けるか（既定 ON）。Lens は Scene ビューでは掛けない（ギズモ・クリックの位置がずれるので） |
 | **View → Scene View → Fog** | Scene ビューにも霧（Fog）を掛けるか（既定 ON）。Game ビューにはいつも掛ける |
 | **View → Scene View → Depth of Field** | Scene ビューにも被写界深度を掛けるか（既定 ON。Post Effects を切っていると押せない）。Game ビューにはいつも掛ける |
```

## パートE 確かめ方

### E-1 ビルド

Debug・Development・Release の 3 つでビルドする（64 ビットの MSBuild）。警告 0 になる。

### E-2 Sandbox で見る

`project/Sandbox/Parameters/Editor/LastScene.json` を `{"scene": "CameraFollowZoneTest"}` にして（または File → Open Scene）、Debug か Development の Sandbox を起動する。テストシーンのカメラは下へ 8° 傾いている（0.9 の直しを見るため）。

| # | すること | 見えればよい物 |
|---|---|---|
| 1 | 停止中に Game ビューを見る | 画面の真ん中に緑の四角（Dead Zone・幅の 20%・高さの 25%）、その外に青の四角（Soft Zone）。Player の足元から上へ黄の線と十字（Offset Y 1）。赤の四角（Bounds）の一部 |
| 2 | 停止中に Scene ビューを見る | 緑・青の四角は出ない（Screen の範囲は Game ビューだけ）。黄の線と十字・赤の Bounds は出る。奥の 5 つの Particle System に桃色の線（十字・球・XY の輪・ラッパ・回った箱） |
| 3 | Play して、WASD で左右へ歩く | 黄の十字が緑の四角の中にある間は、カメラが止まっている。緑の外へ出ると追い始める。走っても黄の十字は青の四角の外へ出ない |
| 4 | 右の段（オレンジ）に乗る・左右の端まで歩く | 段に乗ると縦にも追う。端では、画面の端と赤の線がぴったり合って止まる（外を写さない） |
| 5 | GameCamera の Inspector で Zone Space を World にする | Dead Zone・Soft Zone の値が m の物に入れ替わる（既定 0.5 m・1.5 m と 6 m・4 m）。緑・青の四角が Scene ビューにも出て、Game ビューでは画面の真ん中にある（カメラが傾いていても上にずれない） |
| 6 | Camera の Fov Y を 30 にする（Screen に戻して） | Game ビューの四角の大きさは変わらない（画面の割合なので） |
| 7 | Show Gizmo を外す・View → Gizmos → Camera Follow Zones を外す | どちらでも線が消える。Particle System の Show Gizmo・Particle Shapes も同じ |
| 8 | Particle System の Shape・Radius・Cone Angle・Box Size を変える | 線がすぐ付いてくる。Entity を回す・大きさを変えると、線も回る・伸びる |
| 9 | Release で起動する | 線は出ない（エディタのギズモなので） |

### E-3 写しで確かめたこと（測る Script）

写しだけのブランチ `test-cfz` に、Camera Follow の付いたカメラに付ける Script `FollowZoneProbe`（`project/Sandbox/Source/TestOnly/`。エンジンにも本体にも入れない）と、カメラ 12 台のシーン `FollowZoneProbeTest` を置き、Release で 8 秒走らせた。追う物は SideSwing で左右に往復させる（Fast：幅 8 m・1.5 秒＝最大 33 m/s、Ground：地面の上で幅 6 m・2 秒、Slow：幅 1 m・4 秒）。

毎フレーム（Script の LateUpdate。前のフレームの追いかけの後のカメラと、そのとき追いかけが見た追う物の位置で）測った。画面への写し方は CameraFollow.cpp を写さずに Script の中で別に書いた：

- **Soft**：focus が Soft Zone からはみ出した最大（0 以下なら外へ出ていない。World の真ん中は、前の向きが追う物の奥行きの面と交わる点）
- **Dead**：Smooth Time 0 のカメラで、focus が Dead Zone からはみ出した最大（0 なら、追うときはちょうど Dead Zone の端に置いている）
- **Hold**：前のカメラで focus が Dead Zone の中だったフレームに、カメラが動いた回数（View は奥行きを保つ前後の動きを除く）
- **Guide**：`CalcGuide` の四隅を、このカメラで画面に写したときの、±Zone からのずれ（World は画面の真ん中からの m）と、focus のずれ
- **Bounds**：画面の四隅を追う物の面に当てた点が、Min〜Max からはみ出した最大と、画面の端が Bounds の端にぴったり着いたフレームの数

| カメラ | Plane | Zone Space | Smooth | Soft | Dead | Hold（中だった／動いた） | Guide |
|---|---|---|---|---|---|---|---|
| XYScreen | XY | Screen 0.2 / 0.4・0.6 | 0.3 | 0.00000 | — | 82 / 0 | 0.000000 |
| XYScreenSnap | XY | 〃 | 0 | -0.2 | 0.00000 | 133 / 0 | 0.000000 |
| XZScreen | XZ（下へ 50°・横へ 35°） | Screen 0.1 / 0.3 | 0.2 | 0.00000 | — | 78 / 0 | 0.000000 |
| XZScreenSnap | 〃 | 〃 | 0 | -0.2 | 0.00000 | 120 / 0 | 0.000000 |
| ViewScreen | View（下へ 20°・横へ -30°） | Screen 0.15 / 0.35 | 0.25 | 0.00000 | — | 56 / 0 | 0.000000 |
| ViewScreenSnap | 〃 | 〃 | 0 | -0.2 | 0.00000 | 109 / 0 | 0.000000 |
| XYWorld | XY | World 1 m / 3 m | 0.5 | 0.00000 | — | 44 / 0 | 0.000000 |
| XYScreenHold（Slow） | XY | Screen 0.2 / 0.6 | 0.3 | -0.496 | — | 416 / 0（一度も動かない） | 0.000000 |
| XYWorldTilt | XY（下へ 15°） | World 1 m / 3 m | 0.5 | 0.00000 | — | 44 / 0 | 0.000000 |
| XYScreenTilt | XY（下へ 15°・横へ 10°） | Screen 0.2 / 0.4・0.6 | 0.3 | 0.00000 | — | 86 / 0 | 0.000000 |

| カメラ | Plane | Bounds | はみ出しの最大 | 端に着いたフレーム |
|---|---|---|---|---|
| XYBoundsTilt | XY（下へ 20°） | X -14〜14・Y -1〜16 | 0.00000 m | 416 |
| XZBounds | XZ（下へ 50°・横へ 35°） | X -18〜18・Z -15〜40 | 0.00000 m | 272 |

- 最初は View の Soft が **0.005 はみ出した**。奥行きを保つ前後の動きを、範囲で縛った**後**に足していたので、前後に動いた分だけ画面の位置が伸び縮みしていた。前後の動きを先に入れたカメラで範囲を測るように直した（A-3 の Update の「0.」）。前の Max Lag は面の上の長さで縛っていたので、この順番の影響を受けなかった
- Soft が 0.00000 ちょうどなのは、速い追う物が Soft Zone の端まで行って、そこで縛られているから（出ていない）
- Bounds の最初の確かめでは XYBoundsTilt が 0.025 m はみ出した。Y の範囲（-1〜12 の 13 m）が、傾けたカメラの写す高さ（約 13.05 m）より狭く、「範囲が画面より狭いときは真ん中をそろえる」決まりどおりだったので、テストの Max を 16 に広げた
- Bounds で止まっている間は Soft Zone から出る（Bounds の方が強い）ので、XYBoundsTilt・XZBounds の Soft は数えていない
- 線を描くところ（Scene・Game ビューに出るか）は、窓を前に出さずには撮れなかったので、目では見ていない。E-2 の 1・2・5・7 で見てほしい
