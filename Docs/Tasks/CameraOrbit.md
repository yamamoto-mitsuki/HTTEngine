参考：
- Cinemachine の Orbital Follow（追う物のまわりの球の上にカメラを置く。Position Control の1つ）：https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineOrbitalFollow.html
- Cinemachine の Rotation Composer（見る物を画面の決まった所に写す。このエンジンの Camera Look At）：https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineRotationComposer.html
- 前の md：`Docs/Tasks/CameraFollowZone.md`（**先に写す**。この md はそれを写した後の形に足す）・`Docs/Tasks/VirtualCamera.md`（Camera Follow・Camera Look At を作った時。「Camera Follow は Transform の向きだけ読む」の理由）・`Docs/Tasks/TriggerSaturationScreenPosition.md`（Camera Look At の Screen Position）
- この作業のきっかけ：ボスを見ながら、プレイヤーの後ろに付くカメラ（ロックオンのカメラ）を作りたい

# Camera Orbit：追う物のまわりを回るカメラ（ボスを見ながらプレイヤーの後ろに付く）

- **パートA Camera Orbit の本体**（`CameraOrbitComponent.h`・`CameraOrbit.h`・`CameraOrbit.cpp`（新しい）・`CameraLookAt.h/.cpp`・`EntityManager.h/.cpp`・vcxproj）：カメラを「回る中心（追う物 ＋ Pivot Offset）から、横の向きの後ろへ Distance、Pitch だけ上」に置き、回る中心を向かせる。横の向きは **Heading**（World・TargetForward・**LookAt**）で決める
- **パートB CameraSystem へつなぐ**（`CameraSystem.h/.cpp` と、「カメラが追っている物」を書いたコメント 11 か所）：Camera Orbit を登録して、`UpdateFollow` で動かす。同じ Entity の Camera Follow とは **Camera Orbit が勝つ**。「カメラが追っている物」も Camera Orbit の Target を返す
- **パートC Camera Orbit の線**（`CameraGizmo.h/.cpp`・`ImGuiManager.cpp`）：回る輪・回る中心・カメラを置く位置・見る点からの線を Scene ビューに出す。Component の **Show Gizmo** と、View メニューの **Camera Orbits**
- **パートD Sandbox と Reference**：テストシーン **CameraOrbitTest**（新しい。1〜4 キーで World・TargetForward・LookAt・肩越しの LookAt を切り替えて見比べる）・Sandbox の Script **OrbitCameraSwitcher**（カメラを番号で選ぶ・左上に Heading などを出す）・**OrbitYawInput**（Q・E・R・F・右スティックで Yaw・Pitch を回す）・`Docs/Reference/`
- **パートE 確かめ方**

**CameraFollowZone.md を先に写す**（パートC は CameraFollowZone.md の B-1・B-2・B-3 を写した後の `CameraGizmo`・`ImGuiManager` に足す。パートD の Reference も、CameraFollowZone.md の D-3 の後の文に足す）。この md の中は **A → B → C の順**（B は A の `CameraOrbit`、C は A の `CalcGuide` を使う）。D は A〜C の後。

ブランチは CameraFollowZone.md と同じ `feature/CameraFollowZone`（この md も未コミットで置いてある）。

**写しで確かめた**（`develop` ab269ad に CameraFollowZone.md の答え（answer-cfz）を当てた写しで、答えのブランチ `answer-orbit`。Debug・Development・Release のビルドでエラー・警告 0。測る Script を付けた 8 台のカメラを Release で走らせて、全部の項目が合った。くわしくは パートE の E-3）。線が Scene ビューに出るところ・ロックオンの切り替えの見た目は、まだ目で見ていない（E-2）。

**2026-11-12 に、直した後の CameraFollowZone の上でも確かめた**：CameraFollowZone.md は 11-11 に本体へ直接入り、そのあと 0.9 の直し（XY の画面の真ん中・Bounds の端・Screen の範囲は Game ビューだけ）も入った。この md の答えをその上に載せ直した写し（`orbit-try`）で、Debug・Release のビルドでエラー・警告 0、測る Script の 8 台も全部 0（E-3 と同じ。yawStep は1フレームの最大の回り方で、フレームの長さで変わるので比べない）。ぶつかったのは `CameraGizmo.h/.cpp` の「足す場所」だけで、この md は「〜の後に足す」書き方なのでそのまま写せる。**本体には CameraFollowZone がもう入っているので、この md はパートA から写す**。C-2 の `orbitLines` は `followLines` の後（今は次の行に `screenLines` がある。どちらの後でもよい）、Initialize の Orbit の4行は `screenLines` の設定の後に足す。

**2026-11-12 本体へ直接入れた**（ブランチ `feature/CameraOrbit`。未コミット）。写経してあった `CameraOrbitComponent.h`・`CameraOrbit.h`・`CameraOrbit.cpp` の前半（`CalcHeadingYaw` まで）は書いたままにして、`CameraOrbit.cpp` の続き（`CalcStartYaw` から最後まで）を足した。filters の3行には `<Filter>` を付けた（Visual Studio の Camera のフォルダに出るように）。ほかは `orbit-try` の差分をそのまま当てた。あわせて、**Heading の種類を見比べられるテストシーン**に作り直し、Sandbox に Script `OrbitCameraSwitcher`・`OrbitYawInput` を足した（D-1・D-3・E-2）。Debug・Development・Release のビルドでエラー・警告 0、エディタで CameraOrbitTest を開いて 12 個の Entity を読めた（警告なし）。Play しての見た目は、まだ目で見ていない。

## 0章 全体の形

### 0.1 今のカメラでは作れない理由

作りたい動き：**ボスを見ながら、プレイヤーの後ろに付く**（ロックオン）。上から見ると、ボス・プレイヤー・カメラが一直線に並び、プレイヤーが回り込むとカメラも回り込む。

```
   上から見た図
            ■ ボス（見る点）
            │
            │   ← カメラはこの線の上に付く（プレイヤーがどこへ動いても）
            ●  プレイヤー（回る中心）
            │
            ▲  カメラ（ボスの方を向く）
```

今のカメラの Component では作れない：

- **Camera Follow** は Plane の面の上でカメラを動かすだけで、**カメラの向きは変えない**（Transform の向きだけを読む）。カメラが回り込まないので、プレイヤーがボスの横へ回ると、カメラは横から写したままになる
- **Camera Look At** は**写す向きだけ**を変える（Transform は変えない）。なので Camera Follow ＋ Camera Look At にしても、カメラの位置は回り込まない
- Camera Follow が Look At の向きを読むようにすると、「見る物＝追う物」のときに自分の結果で自分が動く輪になる（VirtualCamera.md のときに避けた）

Script でカメラの Transform を毎フレーム計算して書く手もあるが、それはエンジンに足りない物をゲームで埋める回避策になる（次のゲーム・チームの別の人がまた書く）。**エンジンにカメラの置き方の Component を1つ足す。**

### 0.2 分け方：位置を決める物と、向きを決める物

Cinemachine はカメラを「**位置を決める物**（Position Control）」と「**向きを決める物**（Rotation Control）」に分けている。このエンジンも同じ分け方に当てはめる。

| 役目 | Cinemachine | このエンジン |
|---|---|---|
| 位置：画面の決まった所に写るように、面の上で追う | Position Composer | Camera Follow（今ある） |
| 位置：追う物のまわりの球の上に置く | Orbital Follow | **Camera Orbit（足す）** |
| 位置：決まった線の上 | Spline Dolly | Camera Path（今ある） |
| 向き：見る物を画面の決まった所に写す | Rotation Composer | Camera Look At（今ある） |

**ロックオン ＝ Camera Orbit（Target＝プレイヤー・Heading＝LookAt）＋ Camera Look At（Target＝ボス）**。どちらも1つの役目だけを持つので、ほかの使い方（ふつうの三人称の後ろのカメラ・ボスの登場の回り込み・決まった向きから見る）も、組み合わせを変えるだけで作れる。

- Camera Follow に Mode を足して回り込ませる案はとらない。Camera Follow は「画面の範囲（Dead Zone・Soft Zone）で面の上を追う」物で、球の上に置く Camera Orbit とは、持つ値も計算もほとんど重ならない（Zone・Bounds・Ground Snap Y は回り込みでは意味が無い）。1つの Component に2つの置き方を入れると、Inspector の半分がいつも使えない値になる

### 0.3 置き方

```
   横から見た図                          上から見た図（Yaw）
                 ▲ カメラ                       ＋Z（Yaw 0 で、カメラはこちらを向く）
                /                                ↑
     Distance  /  Pitch（＋で見下ろす）           │   ＋ は上から見て時計回り
              /)                                 ●──→ ＋X（Yaw 90）
   回る中心 ●───────                         
            │ Pivot Offset
   追う物   ●（足元）
```

1. **回る中心** ＝ 追う物の位置 ＋ Pivot Offset（ワールドの向き。足元が原点のキャラなら、胸・頭の高さへ上げる）
2. **横の向き**（Yaw。度）＝ Heading で決まる向き ＋ Yaw の値
3. カメラの向き ＝ X（Pitch）→ Y（横の向き）の順に回した向き（Inspector の Rotation と同じ Z → X → Y の順で、Z は 0＝傾けない）
4. カメラの位置 ＝ 回る中心 − 3 の前の向き × Distance（なので、カメラはいつも回る中心を真ん中に見る）

**Transform の位置と向きを両方書く**（Camera Follow は向きを変えないが、Camera Orbit は向きが置き方の一部なので）。Camera Look At を一緒に付けると、写す向きは Camera Look At が決める（Transform の向きは回る中心を見たまま。Look At を切ると、回る中心を見る）。

### 0.4 Heading：横の向きの決め方

| Heading | 横の向き | 使い道 |
|---|---|---|
| `World` | 0（Yaw がそのまま向き。0 で +Z を向く＝追う物の -Z の側に付く） | 決まった向きから見る。Script で `yaw` を書き換えて回す（スティックで回すカメラ） |
| `TargetForward` | 追う物の前の向き（ワールド行列の +Z を地面に寝かせた向き） | ふつうの三人称の、後ろに付くカメラ。追う物が振り向くと回り込む |
| `LookAt` | 回る中心から、**同じ Entity の Camera Look At が見る点**への向き | ロックオン。見る点と反対の側に付く |

- どれも **Yaw を足す**。LookAt で Yaw を 10〜20 にすると、少し斜め後ろから、プレイヤーの肩越しにボスを見る
- **LookAt は Camera Look At の見る点を読む**（Camera Orbit に「見る物」を別に持たせない）。ボスを入れる所が1つになるので、ロックオンの相手を変えるときは Camera Look At の Target を書き換えるだけで、カメラの向きと回り込みがずれることが無い。Cinemachine もカメラ1台に Tracking Target（追う物）と Look At Target（見る物）を1つずつ持ち、位置を決める物・向きを決める物がそれを読む
- 読むのは **見たい点**（Target ＋ Offset。Camera Look At の Smooth Time をかける前）。`CameraSystem::UpdateFollow` は `CameraLookAt::Update`（見る点をなめらかに動かす）より前に動くので、なめらかにした点は1フレーム古い。回り込みの遅れは Camera Orbit の Heading Smooth Time だけで決める。そのために `CameraLookAt` の名前の無い namespace の `FindPoint` を、`CameraLookAt::FindWantedPoint` として外から呼べるようにする
- **横の向きが決まらないとき**は、そのときの向きを保つ：LookAt で見る点が回る中心の真上・真下・同じ所（ボスがプレイヤーを飛び越える瞬間など）・Camera Look At が無い・切ってある・Target が居ない。TargetForward で追う物が真上・真下を向いている
- 回り始めに決まらなければ、**置いたカメラから回る中心への向き**から始める（置いた側から回り始める）。それも決まらなければ Yaw

### 0.5 なめらかに動かす

| 値 | なめらかにする物 | 計算 |
|---|---|---|
| Smooth Time | 回る中心（追う物に遅れて付いていく） | `CalcSmoothRate` の指数補間（Camera Follow・Camera Look At と同じ） |
| Heading Smooth Time | 横の向き | `DeltaAngle` で一番近い回り方の差を出し、指数補間（350° から 10° へは +20°） |

- 横の向きは回る中心を動かした**後**に測る（LookAt では、見る点と、今の回る中心を結んだ線の上にカメラが来る）
- 回り始め（Play・シーンを読んだ直後・Enabled を入れ直した・Entity を有効にし直した・Target を入れた）は、なめらかにせずにすぐ合わせる（置いた位置から流れてこないように）。止めている間に追う物は動くので、入れ直したときに古い値から流れてくるのを避ける
- カメラの時間（Camera の Time Mode）が止まっている間（ヒットストップ・ポーズ）は動かさない（Camera Follow と同じ）

### 0.6 Camera Follow と一緒に付いていたら

どちらも Transform の位置を決めるので、両方が動かすと毎フレーム引っ張り合う。**Camera Orbit が有効なら Camera Orbit だけが動かす**（Camera Follow は止まり、範囲の線も出ない）。Cinemachine も Position Control は1台に1つ。

- 「両方付けない」決まりにして何もしない案はとらない（付けたときに、なぜおかしく動くのか分からない）
- Camera Follow の Enabled を自分で切る案もとらない（Camera Orbit を外したときに、Camera Follow が切れたまま残る）
- Camera Orbit の Enabled の説明（Tooltip）に書いておく

### 0.7 「カメラが追っている物」

`CameraSystem::GetFollowTargetEntity`（Trigger Activator の FollowTarget）・`GetFollowTargetTransform`（Wind Zone の線・Depth Of Field の FollowTarget）・Camera Volume の Focus の FollowTarget は、今は Camera Follow の Target だけを見ている。ロックオンのカメラに切り替えると、これらが「追っている物が無い」になって、ピントが外れる・風の線が出なくなる。

**有効な Camera Orbit があればその Target、無ければ Camera Follow の Target** を返す `FindFollowTarget` を CameraSystem の中に作り、3 か所をそれに替える（`HasEnabledOrbit` は 0.6 の決まりと同じ関数）。コメント・Tooltip の「Camera Follow の Target」も「Camera Follow・Camera Orbit の Target」に直す。

### 0.8 親の下に置いたカメラ：`EntityManager::SetWorldRotation`

位置は今ある `EntityManager::TranslateWorld`（ワールドで動かす量を、親から見た量に直して足す）を使う。向きはワールドの向きを入れる関数が無いので足す。

- ワールドの向き ＝ 親のワールドの向き × 親から見た向き（クォータニオンは右に掛けた方が先に回る）。なので、親から見た向き ＝ 親のワールドの向きの共役 × ワールドの向き
- 親の向きは `GetWorldRotation(親)`（ワールド行列から大きさを外して取り出す。親が大きさを持っていてもよい）。親の大きさが 0 で取り出せない（数でなくなる）ときは、ワールドの向きをそのまま入れる（TranslateWorld と同じ逃げ方）

### 0.9 線

`CameraOrbit::CalcGuide(entity, orbit)` が、今の回る中心・カメラを置く位置などを**ワールドの点**で返す（計算は回り込みと同じ `CalcPose`・`CalcStartYaw`。線と動きがずれない）。停止中は「再生したら置かれる位置」を返す（Camera Orbit は停止中は動かないので、どこに置かれるかを線で見せる）。

| 線 | 色 | 中身 |
|---|---|---|
| 回る輪 | 紫 | Yaw だけを回したときにカメラが通る円（回る中心の上の、カメラの高さ。半径は Distance × cos(Pitch)） |
| 回る中心からカメラへ | 紫 | 今どこから見ているか（停止中は、再生したら置かれる位置） |
| 回る中心の十字・追う物から回る中心へ | 黄 | Pivot Offset・Smooth Time の遅れでどれだけずれているか |
| 見る点から回る中心へ | 赤 | Heading が LookAt のときだけ。カメラはこの線を伸ばした先に付く |

- **Scene ビューだけ**（`DrawLayer::EditorGizmo`）。輪はカメラ自身を通るので、Game ビューに出しても見えない・邪魔になる（Camera Path の線と同じ）
- 物の後ろは隠す（ふつうのギズモの線）。遠くでも薄くしない（`kNoFadeStart`）
- 出す・消すは CameraFollowZone.md と同じ **2 段**（Component の Show Gizmo ＋ View → Gizmos の Camera Orbits）。既定は入り
- Camera Follow の線は、同じ Entity の Camera Orbit が有効なら出さない（0.6）

### 0.10 ロックオンの組み方（パートD のテストシーン）

```
1 World（Q・E で回す）    ：Camera Orbit（World・Distance 7・Pitch 20）＋ OrbitYawInput（Q・E・R・F・右スティックで Yaw・Pitch を書き換える）
2 TargetForward（後ろに付く）：Camera Orbit（TargetForward・Heading Smooth Time 0.6）
3 LookAt（ロックオン）     ：Camera Orbit（LookAt・Yaw 10）＋ Camera Look At（Target＝Boss・Screen Position (0, 0.15)）
4 LookAt（肩越し・近い）   ：Camera Orbit（LookAt・Yaw 25・Distance 3.5・Pitch 8）＋ Camera Look At（Target＝Boss・Screen Position (-0.2, 0.1)）
CameraSwitcher           ：OrbitCameraSwitcher（Cameras＝上の4台・Label＝左上の Text）。1〜4 キー・Tab・LB・RB で選ぶ
```

- どのカメラも Camera の Blend は Ease 0.5 秒。選んだカメラの Priority を 10、ほかを 0 にして切り替える
- ロックオンを入れる・切るは、カメラの切り替え（Priority か Entity の有効・無効）でする。Camera の Blend で、ふつうのカメラからロックオンのカメラへなめらかに移る（どのカメラも毎フレーム自分の位置へ動いているので、補間の始まりと終わりがどちらも今の絵になる）
- 1台のカメラの Heading を書き換えて切り替える案はとらない。横の向きは Heading Smooth Time で回り込むが、写す向き（Camera Look At の有り無し）は一瞬で変わって絵が飛ぶ。2台にすれば Blend が位置と向きを一緒に補間する
- 相手を変える（次の敵へロックオンを移す）ときは、ロックオンのカメラの Camera Look At の Target を書き換える。カメラは Heading Smooth Time で新しい相手の反対側へ回り込み、写す向きは Camera Look At の Smooth Time で移る
- どの敵を選ぶか・ロックオンを外す距離などは、ゲームの決まりなのでゲーム側（Script）で持つ
- 一覧に出すのは Sandbox の Script（ゲーム側）。エンジンには入れない

### 0.11 やらないこと

- **壁に当たったら寄る**（Cinemachine の Deoccluder。カメラと回る中心の間に壁があれば、壁の手前へ寄せる）：Collision の Cast を使えば作れるが、置き方とは別の役目なので、要るようになったら別の Component（Camera Collider など）で足す
- **スティック・マウスで回す専用の項目**：Input Action（入力の割り当て）がまだ無い。今は Heading を World にして、Script で `yaw`（と `pitch`）を書き換える。Input Action ができたら、そこから読む項目を足す
- **2人（プレイヤーとボス）が画面に入るように引く**（Cinemachine の Group Framing）：今は Distance を決めて置く。ボスが大きすぎて入らないときは、Distance・Pitch・Camera Look At の Offset で合わせる
- **見下ろす角度を追う物の高さで変える**（Cinemachine の Orbital Follow の Three Ring）：今は Pitch 1つ
- `CameraSystem.cpp` は 622 行から 658 行になる（500 行を超えたまま）。見直した：増えたのは追う物を探す 2 関数・登録 3 行・UpdateFollow の Camera Orbit の回しで、コピペは無い

## パートA Camera Orbit の本体

### A-1 `Components/Camera/CameraOrbitComponent.h`（新しい）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// カメラを、追う物のまわりの球の上に置く。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// 位置 ＝ 回る中心（追う物 ＋ Pivot Offset）から、横の向き（Heading ＋ Yaw）の後ろへ Distance 離れ、Pitch の角度だけ上。向きは回る中心を見る
// Camera Look At を一緒に付けると、写す向きは Camera Look At が決める（Transform の向きは回る中心を見たまま）
//   Heading を LookAt にすると、見る点・回る中心・カメラが上から見て一直線になる（相手を見ながら、追う物の後ろに付くロックオン）
// 動かすのは CameraSystem::UpdateFollow（再生中だけ。物理で動いた後・カメラの絵を決める前）。停止中は置いた位置のまま
// 同じ Entity に Camera Follow も付いていて、どちらも有効なら、Camera Orbit だけが動かす（どちらもカメラの位置を決めるので）

/// <summary>
/// 横の向き（カメラが追う物のどちら側に付くか）の決め方。どれも Yaw の角度を足す
/// </summary>
enum class CameraOrbitHeading {
	World,         // ワールドの決まった向き（Yaw 0 で +Z を向く＝追う物の -Z の側に付く）。Script で Yaw を書き換えると回せる
	TargetForward, // 追う物の前の向き（追う物の後ろに付く。追う物が振り向くと回り込む）
	LookAt,        // 同じ Entity の Camera Look At が見る点の向き（見る点と反対の側に付く）。Camera Look At が無い・見る点が無いときは、向きをそのまま保つ
};

/// <summary>
/// 追う物のまわりを回るカメラ（Unity の Cinemachine の Orbital Follow の小さい版）
/// </summary>
struct CameraOrbitComponent {
	bool enabled = true;                                            // false の間は動かさない（置いた位置・向きのまま）
	EntityRef target;                                               // 追う Entity（Hierarchy からドラッグ。保存は EntityId で）
	Vector3 pivotOffset = {0.0f, 1.5f, 0.0f};                       // 追う物の位置から、回る中心へのずれ（m。ワールドの向き。足元の原点から胸・頭の高さへ上げる）
	float distance = 6.0f;                                          // 回る中心からカメラまでの長さ（m）
	float pitch = 15.0f;                                            // 見下ろす角度（度。＋ で上から見る。-80〜80）
	CameraOrbitHeading heading = CameraOrbitHeading::TargetForward; // 横の向きの決め方
	float yaw = 0.0f;                                               // 横の角度（度）。World：向きそのもの / ほか：決まった向きに足す角度（ずらすと斜め後ろ・肩越しになる）
	float smoothTime = 0.1f;                                        // 回る中心が、追う物に追いつくまでの時間の目安（秒。0 ならすぐ合わせる）
	float headingSmoothTime = 0.3f;                                 // 横の向きが、決まった向きに追いつくまでの時間の目安（秒。0 ならすぐ。相手が急に回り込んでも、画面がガクッと回らない）
	bool showGizmo = true;                                          // Scene ビューに、回る中心・回る輪・カメラを置く位置の線を出す（エディタだけ。View メニューの Camera Orbits で全部まとめて消せる）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isOrbiting = false;                   // 回り始めたか（false なら、次のフレームで決まった位置・向きへすぐ合わせる）
	Vector3 currentPivot = {0.0f, 0.0f, 0.0f}; // 今の回る中心（ワールド）
	float currentYaw = 0.0f;                   // 今の横の向き（度。ワールドで、0 が +Z・90 が +X）
};

static_assert(std::is_trivially_copyable_v<CameraOrbitComponent>);
```

- 実行中だけの値（`isOrbiting`・`currentPivot`・`currentYaw`）は Describe に並べないので保存されない（Camera Follow と同じ）
- `currentYaw` を Component に持つのは、Heading が決まらないフレームに向きを保つため・Heading Smooth Time でなめらかにするため。測る Script・ゲームからも、今どちらを向いているかを読める

### A-2 `Systems/Camera/CameraOrbit.h`（新しい）

```cpp
#pragma once
#include <optional>

#include "HTTEngine/Components/Camera/CameraOrbitComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Orbit の中身（CameraSystem から使う）
/// </summary>
namespace CameraOrbit {
/// <summary>
/// 今の回る中心・カメラを置く位置などを、ワールドの点で表した物（エディタの線に使う。CalcGuide で作る）
/// </summary>
struct Guide {
	Vector3 target;                   // 追う物の位置
	Vector3 pivot;                    // 回る中心
	Vector3 camera;                   // カメラを置く位置（停止中は、再生したら置かれる位置）
	float ringRadius = 0.0f;          // 回る輪の半径（m。カメラの高さでの、回る中心からの横の長さ）
	float ringHeight = 0.0f;          // 回る輪の高さ（ワールドの Y）
	std::optional<Vector3> lookPoint; // Heading が LookAt のとき、Camera Look At が見る点（見る点・回る中心・カメラが上から見て一直線）
};

// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraOrbitComponent& value);

// カメラ1つ分の回り込み。Transform を動かしたら true
bool Update(Handle<Entity> entity, CameraOrbitComponent& orbit, float deltaTime);

// 今の位置（停止中は、再生したら置かれる位置）。追う物が居なければ無し。回り込みと同じ計算で作る
std::optional<Guide> CalcGuide(Handle<Entity> entity, const CameraOrbitComponent& orbit);
} // namespace CameraOrbit
```

### A-3 `Systems/Camera/CameraOrbit.cpp`（新しい）

```cpp
#include "CameraOrbit.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Camera/CameraLookAt.h"

namespace {
constexpr float kMaxPitch = 80.0f;        // 見下ろす・見上げる角度の上限（度）。真上・真下から見ると、横の向きが画面の回転になってしまう
constexpr float kMinDistance = 0.01f;     // 回る中心からカメラまでの長さの下限（m。0 だとカメラが回る中心に重なる）
constexpr float kMinFlatLengthSq = 1e-4f; // 地面に寝かせた向きの長さの2乗がこれより小さければ、横の向きが決まらない（真上・真下を向いている・2つの点が重なっている）

/// <summary>
/// カメラを置く位置と向き
/// </summary>
struct OrbitPose {
	Vector3 position;    // 位置（ワールド）
	Quaternion rotation; // 向き（ワールド。回る中心を見る）
};

//=============================================================================
// 向きの計算
//=============================================================================
// ===== 地面に寝かせた向きの、横の角度（度。0 が +Z・90 が +X。真上・真下を向いていて決まらなければ無し）=====
std::optional<float> CalcFlatYaw(const Vector3& direction) {
	if (direction.x * direction.x + direction.z * direction.z < kMinFlatLengthSq) {
		return std::nullopt;
	}
	return MathUtility::DirectionToYaw(direction);
}

// ===== Heading で決まる横の向き（度。Yaw を足す前。決まらなければ無し）=====
// pivot は今の回る中心（LookAt は、回る中心から見る点への向き。カメラはその反対の側に付く）
std::optional<float> CalcHeadingYaw(Handle<Entity> entity, const CameraOrbitComponent& orbit, const TransformComponent& target, const Vector3& pivot) {
	switch (orbit.heading) {
	case CameraOrbitHeading::World:
		return 0.0f; // Yaw がそのまま向き
	case CameraOrbitHeading::TargetForward:
		return CalcFlatYaw(GetWorldForward(target));
	case CameraOrbitHeading::LookAt: {
		// 見る点はなめらかにする前の点（Camera Look At の Smooth Time は写す向きにだけ効く。回り込みの遅れは Heading Smooth Time で決める）
		const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity);
		if (lookAt == nullptr || !lookAt->enabled) {
			return std::nullopt;
		}
		const std::optional<Vector3> point = CameraLookAt::FindWantedPoint(*lookAt);
		return point.has_value() ? CalcFlatYaw(*point - pivot) : std::nullopt;
	}
	}
	HTT_ASSERT_MSG(false, "知らない CameraOrbitHeading です");
	return std::nullopt;
}

// ===== 回り始めの横の向き（度）=====
// Heading で決まればそれ。決まらなければ、置いたカメラから回る中心への向き（置いた側から回り始める）。それも決まらなければ Yaw
float CalcStartYaw(Handle<Entity> entity, const CameraOrbitComponent& orbit, const TransformComponent& camera, const TransformComponent& target, const Vector3& pivot) {
	if (const std::optional<float> heading = CalcHeadingYaw(entity, orbit, target, pivot)) {
		return *heading + orbit.yaw;
	}
	return CalcFlatYaw(pivot - GetWorldPosition(camera)).value_or(orbit.yaw);
}

// ===== 回る中心と横の向きから、カメラを置く位置と向き =====
// 向きは X（見下ろす）→ Y（横）の順に回す（Inspector の Rotation と同じ Z → X → Y の順で、Z は 0＝傾けない）
// 位置は、回る中心からその向きの後ろへ Distance。なのでカメラはいつも回る中心を真ん中に見る
OrbitPose CalcPose(const CameraOrbitComponent& orbit, const Vector3& pivot, float yaw) {
	const float pitch = std::clamp(orbit.pitch, -kMaxPitch, kMaxPitch);
	const Quaternion rotation = MakeQuaternionFromEuler({pitch, yaw, 0.0f});
	const Vector3 forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	return {pivot - forward * (std::max)(orbit.distance, kMinDistance), rotation};
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraOrbit::Describe(ComponentUI& ui, CameraOrbitComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと動かさない（置いた位置・向きのまま）。同じ Entity の Camera Follow と両方入っているときは、こちらだけが動かす"));
	ui.Field("Target###target", value.target, Tip("追う Entity（Hierarchy からドラッグ）。再生中だけ、このまわりを回る位置へカメラを動かし、回る中心を向かせる"));
	ui.Field("Pivot Offset###pivotOffset", value.pivotOffset, Tip("追う物の位置から、回る中心へのずれ（m。ワールドの向き）。足元が原点のキャラなら、Y を胸・頭の高さにする"));
	ui.Field("Distance###distance", value.distance, AtLeast(kMinDistance).Tip("回る中心からカメラまでの長さ（m）"));
	ui.Field("Pitch###pitch", value.pitch, Range(-kMaxPitch, kMaxPitch).Tip("見下ろす角度（度）。＋ で上から見下ろす、− で下から見上げる"));
	ui.Separator("Heading");
	ui.Field(
	    "Heading###heading", value.heading,
	    Tip("カメラが追う物のどちら側に付くか。World：ワールドの決まった向き（Yaw 0 で +Z を向く）/ TargetForward：追う物の前の向き（追う物の後ろに付く）/ LookAt：Camera Look At が見る点の向き（見る点と反対の側に付く。相手を見ながら、追う物の後ろに付くロックオン）"));
	ui.Field(
	    "Yaw###yaw", value.yaw,
	    Tip("横の角度（度。＋ で上から見て時計回り）。World：向きそのもの / TargetForward・LookAt：決まった向きに足す角度（20 くらいずらすと、斜め後ろから肩越しに見る）"));
	ui.Field(
	    "Heading Smooth Time###headingSmoothTime", value.headingSmoothTime,
	    AtLeast(0.0f).Tip("横の向きが追いつくまでの時間の目安（秒）。大きいほどゆっくり回り込む（相手が追う物の上を飛び越えても、画面がガクッと回らない）。0 ならすぐ合わせる"));
	ui.Separator("Follow");
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("回る中心が、追う物に追いつくまでの時間の目安（秒）。走っている間は「速さ × Smooth Time」くらい遅れる。0 ならすぐ合わせる"));
	ui.Field(
	    "Show Gizmo###showGizmo", value.showGizmo,
	    Tip("Scene ビューに線を出す（エディタだけ）。黄：回る中心の十字と、追う物からの線 / 紫：回る輪と、回る中心からカメラを置く位置への線 / 赤：LookAt の見る点から回る中心への線。View メニューの Camera Orbits で全部まとめて消せる"));
}

//=============================================================================
// 回り込み
//=============================================================================
bool CameraOrbit::Update(Handle<Entity> entity, CameraOrbitComponent& orbit, float deltaTime) {
	// --- 早期リターン ---
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* target = EntityManager::Get<TransformComponent>(orbit.target);
	if (transform == nullptr || target == nullptr) {
		orbit.isOrbiting = false; // 追う物が入っていない・もう居ない（入ったら、そこから回り始める）
		return false;
	}
	const Vector3 wantedPivot = GetWorldPosition(*target) + orbit.pivotOffset;

	// --- 1. 回る中心を、追う物へなめらかに近づける ---
	if (!orbit.isOrbiting) {
		// 回り始め（Play・シーンを読んだ直後・有効にした直後）：置いた位置から流れてこないように、すぐ合わせる
		orbit.currentPivot = wantedPivot;
		orbit.currentYaw = CalcStartYaw(entity, orbit, *transform, *target, orbit.currentPivot);
		orbit.isOrbiting = true;
	} else {
		orbit.currentPivot += (wantedPivot - orbit.currentPivot) * MathUtility::CalcSmoothRate(deltaTime, orbit.smoothTime);
		// --- 2. 横の向きを、Heading で決まる向きへ一番近い回り方で近づける ---
		// 回る中心を動かした後に測る（LookAt は、見る点と今の回る中心を結んだ線の上にカメラが来る）。決まらないフレームは、向きをそのまま保つ
		if (const std::optional<float> heading = CalcHeadingYaw(entity, orbit, *target, orbit.currentPivot)) {
			orbit.currentYaw += MathUtility::DeltaAngle(orbit.currentYaw, *heading + orbit.yaw) * MathUtility::CalcSmoothRate(deltaTime, orbit.headingSmoothTime);
			orbit.currentYaw = std::remainder(orbit.currentYaw, 360.0f); // -180〜180 に収める（足し続けても大きくならないように）
		}
	}

	// --- 3. 位置と向きを Transform へ（親の下に置いたカメラでも、ワールドでその位置・向きになる）---
	const OrbitPose pose = CalcPose(orbit, orbit.currentPivot, orbit.currentYaw);
	EntityManager::TranslateWorld(entity, pose.position - GetWorldPosition(*transform));
	EntityManager::SetWorldRotation(entity, pose.rotation);
	return true;
}

//=============================================================================
// エディタの線
//=============================================================================
std::optional<CameraOrbit::Guide> CameraOrbit::CalcGuide(Handle<Entity> entity, const CameraOrbitComponent& orbit) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* target = EntityManager::Get<TransformComponent>(orbit.target);
	if (transform == nullptr || target == nullptr) {
		return std::nullopt;
	}
	Guide guide;
	guide.target = GetWorldPosition(*target);
	// 回っている間は今の値。停止中（回り始める前）は、回り始めに置かれる値（Update の回り始めと同じ）
	float yaw = orbit.currentYaw;
	guide.pivot = orbit.currentPivot;
	if (!orbit.isOrbiting) {
		guide.pivot = guide.target + orbit.pivotOffset;
		yaw = CalcStartYaw(entity, orbit, *transform, *target, guide.pivot);
	}
	guide.camera = CalcPose(orbit, guide.pivot, yaw).position;
	// 回る輪：Yaw だけを回したときにカメラが通る円（回る中心の真上・真下の、カメラの高さ）
	guide.ringRadius = (std::max)(orbit.distance, kMinDistance) * std::cos(std::clamp(orbit.pitch, -kMaxPitch, kMaxPitch) * MathUtility::kDegToRad);
	guide.ringHeight = guide.camera.y;
	if (orbit.heading == CameraOrbitHeading::LookAt) {
		const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity);
		if (lookAt != nullptr && lookAt->enabled) {
			guide.lookPoint = CameraLookAt::FindWantedPoint(*lookAt);
		}
	}
	return guide;
}
```

- `CalcStartYaw` を Update の回り始めと CalcGuide の両方で使う（停止中の線が、再生したときに置かれる位置とずれない）
- `std::remainder(yaw, 360)` は「360 の一番近い整数倍を引いた余り」で、-180〜180 に収まる（`MathUtility::DeltaAngle` と同じ使い方）。足し続けても値が大きくならないように、毎フレーム戻す
- Pitch と Distance の下限・上限は、ここで縛る（Inspector の Range は打ち込んだ値を縛るが、Script・シーンファイルから入った値は縛らないので）

### A-4 `Systems/Camera/CameraLookAt.h`・`CameraLookAt.cpp`（見たい点を外から呼べるように）

`CameraLookAt.h`：`Describe` の宣言の後に足す。

```cpp
// 見たい点（ワールド。Target ＋ Offset で、なめらかにする前の点）。Target が居なくなったら nullopt。Enabled は見ない
// Camera Orbit の Heading（LookAt）も使う（見る点と反対の側にカメラを置く）
std::optional<Vector3> FindWantedPoint(const CameraLookAtComponent& lookAt);
```

`CameraLookAt.cpp`：名前の無い namespace の `FindPoint` を消して、

```cpp
namespace {
constexpr float kMinLookDistanceSq = 1e-6f; // 見る点がカメラとほとんど同じ位置なら、向きが決まらないので Transform の向きのままにする
// 上下に向ける角の上限（ラジアン）。真上・真下まで向けると、左右の向きが決まらなくなる
constexpr float kMaxPitch = std::numbers::pi_v<float> * 0.5f - 0.01f;
} // namespace
```

「見る点」の大見出しの下（`CameraLookAt::Update` の前）に、同じ中身で外から呼べる関数を置く。

```cpp
//=============================================================================
// 見る点
//=============================================================================
// ===== 見たい点 =====
std::optional<Vector3> CameraLookAt::FindWantedPoint(const CameraLookAtComponent& lookAt) {
	if (!lookAt.target.IsSet()) {
		return lookAt.offset;
	}
	const TransformComponent* target = EntityManager::Get<TransformComponent>(lookAt.target);
	if (target == nullptr) {
		return std::nullopt;
	}
	return GetWorldPosition(*target) + lookAt.offset;
}
```

`CameraLookAt::Update` の頭と `CameraLookAt::GetPoint` の最後の `FindPoint(lookAt)` を `FindWantedPoint(lookAt)` に直す（2 か所）。

### A-5 `ECS/EntityManager.h`・`EntityManager.cpp`（ワールドの向きを入れる）

`EntityManager.h`：`TranslateWorld` の宣言の後に足す。

```cpp
	// ワールドの向きを worldRotation にする（rotation は親から見た向きなので、親の下なら、親の向きを外して入れる）
	// 親の向きは UpdateTransforms の結果を使う。カメラの回り込み（Camera Orbit）が使う
	static void SetWorldRotation(Handle<Entity> handle, const Quaternion& worldRotation);
```

`EntityManager.cpp`：`EntityManager::TranslateWorld` の後に足す。

```cpp
void EntityManager::SetWorldRotation(Handle<Entity> handle, const Quaternion& worldRotation) {
	TransformComponent* transform = Get<TransformComponent>(handle);
	const Entity* entity = Get(handle);
	if (!transform || !entity) {
		return;
	}
	Quaternion local = worldRotation;
	if (const TransformComponent* parent = Get<TransformComponent>(entity->parent)) {
		// ワールドの向き ＝ 親のワールドの向き × 親から見た向き（右に掛けた方が先に回る）なので、左から親の逆（長さ1なら共役）を掛ける
		const Quaternion fromParent = Conjugate(GetWorldRotation(*parent)) * worldRotation;
		// 親の大きさが0だと親の向きが取り出せず、数でなくなる。そのときは、ワールドの向きをそのまま入れる
		if (std::isfinite(fromParent.x) && std::isfinite(fromParent.y) && std::isfinite(fromParent.z) && std::isfinite(fromParent.w)) {
			local = fromParent;
		}
	}
	transform->rotation = Normalize(local);
}
```

- `eulerHint`（Inspector に出す角度の元）は書かない。Inspector は `GetEulerAngles` で、今の向きに一番近い角度を出す（コードで rotation を書いたときと同じ）

### A-6 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

`HTTEngine.vcxproj`：Look At の3行の後に1行ずつ足す。

```xml
    <ClCompile Include="HTTEngine\Systems\Camera\CameraLookAt.cpp" />
    <ClCompile Include="HTTEngine\Systems\Camera\CameraOrbit.cpp" />
```

```xml
    <ClInclude Include="HTTEngine\Components\Camera\CameraLookAtComponent.h" />
    <ClInclude Include="HTTEngine\Components\Camera\CameraOrbitComponent.h" />
```

```xml
    <ClInclude Include="HTTEngine\Systems\Camera\CameraLookAt.h" />
    <ClInclude Include="HTTEngine\Systems\Camera\CameraOrbit.h" />
```

`HTTEngine.vcxproj.filters`：同じく Look At の後に。

```xml
    <ClCompile Include="HTTEngine\Systems\Camera\CameraOrbit.cpp">
      <Filter>HTTEngine\Systems\Camera</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Components\Camera\CameraOrbitComponent.h">
      <Filter>HTTEngine\Components\Camera</Filter>
    </ClInclude>
```

```xml
    <ClInclude Include="HTTEngine\Systems\Camera\CameraOrbit.h">
      <Filter>HTTEngine\Systems\Camera</Filter>
    </ClInclude>
```

## パートB CameraSystem へつなぐ

### B-1 `Systems/Camera/CameraSystem.h`

include に1行足す（Look At の後）。

```cpp
#include "HTTEngine/Components/Camera/CameraOrbitComponent.h"
```

クラスの説明の「Transform を動かすのは」の文：

```cpp
/// <para>1台分の値 ＝ Transform ＋ Camera Path（位置）＋ Camera Look At（向き）＋ Camera Shake（揺れ）＋ Camera Volume（ポストエフェクト）。Transform を動かすのは Camera Follow・Camera Orbit と Timelineのキーだけで、ほかは写す値にだけ入る</para>
```

`UpdateFollow` の説明の1行目を2行に：

```cpp
	/// Camera Follow・Camera Orbit を付けた Entity（カメラ）の Transform を、追う物へ近づける（再生中だけ。EngineSystems から1フレームに1回）
	/// <para>同じ Entity に両方あって、どちらも有効なら Camera Orbit だけが動かす（どちらもカメラの位置を決めるので）</para>
```

`GetFollowTargetTransform` の説明の1行目：

```cpp
	// メインカメラが追っている物の Transform（有効な Camera Orbit があればその Target、無ければ Camera Follow の Target。どれも無ければ nullptr。停止中も返す）
```

### B-2 `Systems/Camera/CameraSystem.cpp`

include に1行足す（`CameraLookAt.h` の後）。

```cpp
#include "HTTEngine/Systems/Camera/CameraOrbit.h"
```

名前の無い namespace の「時間」の大見出しのまとまり（`GetCameraDeltaTime`）の後に、大見出しごと足す。

```cpp
//=============================================================================
// 追う物
//=============================================================================
// ===== その Entity に、有効な Camera Orbit が付いているか（付いていれば、Camera Follow の代わりにこちらが位置を決める）=====
bool HasEnabledOrbit(Handle<Entity> entity) {
	const CameraOrbitComponent* orbit = EntityManager::Get<CameraOrbitComponent>(entity);
	return orbit != nullptr && orbit->enabled;
}

// ===== その Entity のカメラが追う物（有効な Camera Orbit の Target、無ければ Camera Follow の Target。どれも無ければ無効な Handle）=====
Handle<Entity> FindFollowTarget(Handle<Entity> entity) {
	if (HasEnabledOrbit(entity)) {
		return EntityManager::Find(EntityManager::Get<CameraOrbitComponent>(entity)->target);
	}
	const CameraFollowComponent* follow = EntityManager::Get<CameraFollowComponent>(entity);
	return (follow != nullptr) ? EntityManager::Find(follow->target) : Handle<Entity>{};
}
```

`DescribeCamera` の Time Mode の Tip：

```cpp
	    Tip("このカメラの切り替え・Camera Follow・Camera Orbit・Camera Look At・Camera Shake が使う時間。Game：スローで遅くなり、ヒットストップ・ポーズで止まる / Real：止まらない"));
```

`CalcVolumeFocusDistance` の `FollowTarget` の case（2行を1行に）：

```cpp
	case CameraVolumeFocus::FollowTarget: {
		const TransformComponent* target = EntityManager::Get<TransformComponent>(FindFollowTarget(entity));
		return target ? std::optional<float>(depthOf(GetWorldPosition(*target))) : std::nullopt;
	}
```

`CameraSystem::Initialize`：Camera Look At の登録の後に足す。

```cpp
	EntityManager::RegisterComponent<CameraOrbitComponent>();
	ComponentSerializer::Register<CameraOrbitComponent>("Camera Orbit", &CameraOrbit::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraOrbitComponent>>("Camera Orbit", "Camera", &CameraOrbit::Describe));
```

`CameraSystem::UpdateFollow`（全体を差し替え）：

```cpp
bool CameraSystem::UpdateFollow() {
	bool isMoved = false;
	// --- Camera Orbit（追う物のまわりを回る）---
	EntityManager::ForEach<CameraOrbitComponent>([&isMoved](Handle<Entity> entity, CameraOrbitComponent& orbit) {
		if (!orbit.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			orbit.isOrbiting = false; // 止めている間に追う物は動くので、また動かすときは、決まった位置からすぐ始める（離れた所から流れてこないように）
			return;
		}
		const float deltaTime = GetCameraDeltaTime(entity);
		if (deltaTime <= 0.0f) {
			return; // このカメラの時間が止まっている（今の値のまま。止まっている間に回り直さない）
		}
		if (CameraOrbit::Update(entity, orbit, deltaTime)) {
			isMoved = true;
		}
	});
	// --- Camera Follow（面の上で追う。同じ Entity の Camera Orbit が有効なら、そちらが位置を決めるので動かさない）---
	EntityManager::ForEach<CameraFollowComponent>([&isMoved](Handle<Entity> entity, CameraFollowComponent& follow) {
		if (!follow.enabled || !EntityManager::IsActiveInHierarchy(entity) || HasEnabledOrbit(entity)) {
			return;
		}
		const float deltaTime = GetCameraDeltaTime(entity);
		if (deltaTime <= 0.0f) {
			return; // このカメラの時間が止まっている（一時停止・ヒットストップ中のゲームの時間など）
		}
		if (CameraFollow::Update(entity, follow, deltaTime)) {
			isMoved = true;
		}
	});
	return isMoved;
}
```

`CameraSystem::GetFollowTargetEntity`（全体を差し替え）：

```cpp
Handle<Entity> CameraSystem::GetFollowTargetEntity() {
	// メインカメラの Camera Orbit・Camera Follow → その Target の Entity（どこかで無ければ無効な Handle）
	return FindFollowTarget(GetMainCameraEntity());
}
```

### B-3 「カメラが追っている物」を書いたコメント

動きは B-2 の `FindFollowTarget` で変わるので、ここはコメント・Tooltip の文だけ。どれも「Camera Follow の Target」を「**Camera Follow・Camera Orbit の Target**」に直す。

| ファイル | 場所 |
|---|---|
| `Components/Camera/CameraVolumeComponent.h` | `FollowTarget` の右のコメント |
| `Components/Gameplay/TriggerActivatorComponent.h` | `FollowTarget` の右のコメント |
| `Components/Physics/WindZoneComponent.h` | 頭のコメントの「画面の風の線は」の行 |
| `Components/Rendering3D/DepthOfFieldComponent.h` | `FollowTarget` の右のコメント |
| `Graphics/PostEffect/DepthOfFieldPass.h` | `focusPoint` の右のコメント |
| `Systems/Camera/CameraVolume.cpp` | Focus の Tip の最後 |
| `Systems/Gameplay/TimelineTriggerSystem.cpp` | `break;` の右のコメント |
| `Systems/Gameplay/TriggerActivatorSystem.cpp` | `FollowTarget` の case の `break;` の右のコメント |
| `Systems/Physics/WindZoneSystem.cpp` | 線の関数の上のコメントと、Line Amount の Tip（2 か所） |
| `Systems/Rendering3D/DepthOfFieldSystem.cpp` | Focus の Tip と、`FollowTarget` の case のコメント（「Target は Camera Follow の物を使う」→「Target は Camera Follow・Camera Orbit の物を使う」） |

ほかに 2 か所：

`Systems/EngineSystems.cpp`（`UpdateFollow` の上のコメント）：

```cpp
	// カメラの追いかけ（Camera Follow・Camera Orbit）。物理で動いた後の位置を追う。カメラを動かしたら、カメラ（と子）のワールド行列を計算し直す
```

`ECS/GameComponent.h`（カメラの説明の行の最後に足す）：

```cpp
//   カメラは Entity に付ける Camera Component。追いかけるなら、同じ Entity に Camera Follow（Add Component → Camera）を付けて Target を入れる（まわりを回って後ろに付くなら Camera Orbit）
```

## パートC Camera Orbit の線

CameraFollowZone.md の B-1・B-2・B-3 を写した後の `CameraGizmo.h`・`CameraGizmo.cpp`・`ImGuiManager.cpp` に足す。

### C-1 `Editor/Gizmos/CameraGizmo.h`

クラスの説明に1行足す（Camera Follow の行の後）。

```cpp
/// <para>Camera Orbit の回る輪・回る中心・カメラを置く位置の線（Scene ビューだけ）</para>
```

`GetShowFollowZones` の後に足す。

```cpp
	// Camera Orbit の線を出すか（View メニューから全部まとめて切り替える。1つずつは Inspector の Show Gizmo）
	static bool& GetShowOrbits();
```

### C-2 `Editor/Gizmos/CameraGizmo.cpp`

include に2行足す（`EntityManager.h` の後と、`CameraFollow.h` の後）。

```cpp
#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
```

```cpp
#include "HTTEngine/Systems/Camera/CameraOrbit.h"
```

名前の無い namespace の、`kBoundsColor` の後に足す。

```cpp
// Camera Orbit の線の色（Inspector の Show Gizmo の説明と合わせる）
constexpr Vector4 kOrbitColor = {0.75f, 0.45f, 1.0f, 1.0f};  // 回る輪と、回る中心からカメラを置く位置への線（紫）
constexpr Vector4 kLookAtColor = {1.0f, 0.35f, 0.3f, 1.0f};  // Heading が LookAt のとき、見る点から回る中心への線（赤）
constexpr uint32_t kOrbitRingDivision = 48;                  // 回る輪を何本の線で描くか（大きな輪なので、ふつうの円より細かく）
constexpr float kOrbitMarkSize = 0.2f;                       // 回る中心の十字の、半分の長さ（m）
```

変数に2つ足す（`isShowFollowZones` の後と、`followLines` の後）。

```cpp
bool isShowOrbits = true;             // Camera Orbit の線を出すか
```

```cpp
Renderer::LineListConfig orbitLines;  // Camera Orbit の線の置き場（Scene ビューだけ。View メニューで lines と別に消すので分ける）
```

`PushFollowGuide` の後に足す。

```cpp
// ===== Camera Orbit 1つ分の線を積む =====
// 輪は Yaw だけを回したときにカメラが通る円。回る中心からカメラを置く位置へ線を引いて、今どこから見ているかを見せる
void PushOrbitGuide(const CameraOrbit::Guide& guide) {
	std::vector<Renderer::LineSegment>& out = orbitLines.lines;
	const Vector3 ringCenter = {guide.pivot.x, guide.ringHeight, guide.pivot.z};
	GizmoLines::PushCircle(out, ringCenter, guide.ringRadius, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, kOrbitColor, kOrbitRingDivision);
	out.push_back({guide.pivot, guide.camera, kOrbitColor});
	// 回る中心の十字と、追う物から回る中心へ（Pivot Offset・Smooth Time の遅れでどれだけずれているか）
	for (const Vector3& axis : {Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}}) {
		out.push_back({guide.pivot - axis * kOrbitMarkSize, guide.pivot + axis * kOrbitMarkSize, kFocusColor});
	}
	out.push_back({guide.target, guide.pivot, kFocusColor});
	if (guide.lookPoint.has_value()) {
		out.push_back({*guide.lookPoint, guide.pivot, kLookAtColor});
	}
}
```

- `{a, b, c}` の範囲 for は `std::initializer_list` を回す（3 つの軸を並べて書くため）

`CameraGizmo::Initialize` の最後に足す。

```cpp
	// Camera Orbit の線は Scene ビューだけ（輪はカメラ自身を通るので、Game ビューに出しても見えない・邪魔になる）
	orbitLines.fadeStartDistance = kNoFadeStart;
	orbitLines.fadeEndDistance = kNoFadeEnd;
	orbitLines.layer = DrawLayer::EditorGizmo;
	orbitLines.isGizmo = true;
```

`CameraGizmo::Draw` の Camera Follow の ForEach の中、Show Gizmo を見る `if` の後に足す。

```cpp
			// 同じ Entity の Camera Orbit が有効なら、Camera Follow は動かさないので線も出さない（CameraSystem::UpdateFollow と同じ決まり）
			const CameraOrbitComponent* orbit = EntityManager::Get<CameraOrbitComponent>(entity);
			if (orbit != nullptr && orbit->enabled) {
				return;
			}
```

`CameraGizmo::Draw` の最後（`if (isShowFollowZones) { … }` の後）に足す。

```cpp
	if (isShowOrbits) {
		orbitLines.camera = camera;
		orbitLines.lines.clear();
		EntityManager::ForEach<CameraOrbitComponent>([](Handle<Entity> entity, const CameraOrbitComponent& orbit) {
			if (!orbit.enabled || !orbit.showGizmo || !EntityManager::IsActiveInHierarchy(entity)) {
				return;
			}
			if (const std::optional<CameraOrbit::Guide> guide = CameraOrbit::CalcGuide(entity, orbit)) {
				PushOrbitGuide(*guide);
			}
		});
		Renderer::DrawLines(orbitLines);
		orbitLines.camera = nullptr;
	}
```

ファイルの最後（`GetShowFollowZones` の後）に足す。

```cpp
bool& CameraGizmo::GetShowOrbits() { return isShowOrbits; }
```

### C-3 `Editor/ImGuiManager.cpp`（View メニューに1行）

`Camera Follow Zones` の行の後に足す。

```cpp
		ImGui::MenuItem("Camera Orbits", nullptr, &CameraGizmo::GetShowOrbits());           // 回る輪・回る中心（Scene ビューだけ。1つずつは Inspector の Show Gizmo）
```

## パートD Sandbox と Reference

### D-1 `project/Sandbox/Resources/scenes/CameraOrbitTest.scene.json`（新しい）

0.10 の組み方。Player は Player.prefab（WASD で歩く・Space で跳ぶ。進む向きを向く）、Boss は左右に往復する赤い箱（SideSwing）、Pillar は横に置いた柱（回り込んだのが分かる目印）。**1〜4 キー（Tab・LB・RB）でカメラを選ぶ**。左上の Label に、カメラの一覧と、選んだカメラの Heading・Yaw・Pitch・Distance・Camera Look At の有り無しが出る。

`OrbitCameraSwitcher` の `cameras` は、一覧（`ui.ListField`）の要素ごとに `{"camera": 番号}` で書く（要素の見せ方の `ui.Field("Camera###camera", …)` の名前）。

```json
{
	"version": 5,
	"entities": [
		{
			"id": 1,
			"name": "World（Q・E で回す）",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, -6.0], "rotation": [15.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 60.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 10, "blendStyle": "Ease", "blendTime": 0.5, "blendEase": "InOutCubic", "timeMode": "Game"},
				"Camera Orbit": {
					"enabled": true,
					"target": 15981731572122588503,
					"pivotOffset": [0.0, 1.5, 0.0],
					"distance": 7.0,
					"pitch": 20.0,
					"heading": "World",
					"yaw": 0.0,
					"smoothTime": 0.1,
					"headingSmoothTime": 0.15,
					"showGizmo": true
				},
				"OrbitYawInput": {"yawSpeed": 120.0, "pitchSpeed": 60.0, "minPitch": -30.0, "maxPitch": 70.0}
			}
		},
		{
			"id": 2,
			"name": "TargetForward（後ろに付く）",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, -6.0], "rotation": [15.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 60.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 0, "blendStyle": "Ease", "blendTime": 0.5, "blendEase": "InOutCubic", "timeMode": "Game"},
				"Camera Orbit": {
					"enabled": true,
					"target": 15981731572122588503,
					"pivotOffset": [0.0, 1.5, 0.0],
					"distance": 6.0,
					"pitch": 15.0,
					"heading": "TargetForward",
					"yaw": 0.0,
					"smoothTime": 0.1,
					"headingSmoothTime": 0.6,
					"showGizmo": true
				}
			}
		},
		{
			"id": 3,
			"name": "LookAt（ロックオン）",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, -6.0], "rotation": [15.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 60.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 0, "blendStyle": "Ease", "blendTime": 0.5, "blendEase": "InOutCubic", "timeMode": "Game"},
				"Camera Orbit": {
					"enabled": true,
					"target": 15981731572122588503,
					"pivotOffset": [0.0, 1.5, 0.0],
					"distance": 6.0,
					"pitch": 15.0,
					"heading": "LookAt",
					"yaw": 10.0,
					"smoothTime": 0.1,
					"headingSmoothTime": 0.3,
					"showGizmo": true
				},
				"Camera Look At": {"enabled": true, "target": 5, "offset": [0.0, 1.0, 0.0], "screenPosition": [0.0, 0.15], "smoothTime": 0.1}
			}
		},
		{
			"id": 4,
			"name": "LookAt（肩越し・近い）",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, -6.0], "rotation": [15.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Camera": {"fovY": 50.0, "nearZ": 0.1, "farZ": 1000.0, "priority": 0, "blendStyle": "Ease", "blendTime": 0.5, "blendEase": "InOutCubic", "timeMode": "Game"},
				"Camera Orbit": {
					"enabled": true,
					"target": 15981731572122588503,
					"pivotOffset": [0.0, 1.7, 0.0],
					"distance": 3.5,
					"pitch": 8.0,
					"heading": "LookAt",
					"yaw": 25.0,
					"smoothTime": 0.05,
					"headingSmoothTime": 0.2,
					"showGizmo": true
				},
				"Camera Look At": {"enabled": true, "target": 5, "offset": [0.0, 1.0, 0.0], "screenPosition": [-0.2, 0.1], "smoothTime": 0.1}
			}
		},
		{
			"id": 5,
			"name": "Boss",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 1.5, 8.0], "rotation": [0.0, 0.0, 0.0], "scale": [2.0, 3.0, 2.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [0.85, 0.2, 0.25, 1.0]},
				"SideSwing": {"width": 6.0, "period": 8.0}
			}
		},
		{
			"id": 6,
			"name": "CameraSwitcher",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"OrbitCameraSwitcher": {"cameras": [{"camera": 1}, {"camera": 2}, {"camera": 3}, {"camera": 4}], "label": 7, "activePriority": 10}
			}
		},
		{
			"id": 7,
			"name": "Label",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [24.0, -24.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Text": {"enabled": true, "text": "1〜4：カメラ / Tab・LB・RB：次・前", "fontSize": 24.0, "align": "Left", "anchor": "TopLeft", "pivot": [0.0, 1.0], "outlineWidth": 3.0}
			}
		},
		{
			"id": 8,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, 3.0, 0.0], "rotation": [50.0, -30.0, 0.0], "scale": [1.0, 1.0, 1.0]},
				"Directional Light": {"color": [1.0, 1.0, 1.0], "intensity": 1.0}
			}
		},
		{
			"id": 9,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [0.0, -0.05, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [40.0, 0.1, 40.0]},
				"Collider": {"enabled": true, "isTrigger": false, "shape": "Box", "center": [0.0, 0.0, 0.0], "size": [1.0, 1.0, 1.0]},
				"Primitive Renderer": {"enabled": true, "shape": "Box", "color": [0.45, 0.47, 0.5, 1.0]}
			}
		},
		{
			"id": 10,
			"name": "Pillar",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {"translation": [-5.0, 1.5, 2.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 3.0, 1.0]},
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
		}
	]
}
```

### D-2 `Docs/Reference/`

CameraFollowZone.md の D-3 を写した後に直す。

**`10-Camera.md`**

Camera の表の Time Mode の行：

```md
| Time Mode | このカメラの切り替え・Camera Follow・Camera Orbit・Camera Look At・Camera Shake が使う時間。`Game`（スローで遅くなり、ヒットストップ・ポーズで止まる）／`Real`（止まらない。エディタの一時停止では止まる） |
```

「追いかけるカメラ（Camera Follow）」の節の最後の箇条書きの後に1行足す。

```md
- 追う物のまわりを回るカメラ・相手を見ながら後ろに付くカメラは、Camera Follow ではなく Camera Orbit（次の節）。Camera Follow はカメラの向きを変えない。
```

その後（「注視点（Camera Look At）」の節の前）に、節を1つ足す。

````md
## 追う物のまわりを回るカメラ（Camera Orbit）

**Add Component → Camera → Camera Orbit**（`CameraOrbitComponent`）。Camera と同じ Entity に付けて、Target に追う Entity を Hierarchy からドラッグする。カメラを「回る中心（追う物 ＋ Pivot Offset）から、横の向きの後ろへ Distance、Pitch だけ上」に置き、回る中心を向かせる（Transform の位置と向きを書く）。再生中だけ動く（停止中は置いた位置のまま。Scene ビューの線が、再生したら置かれる位置を見せる）。

| 項目 | 中身 |
|---|---|
| Target | 追う Entity |
| Pivot Offset | 追う物の位置から回る中心へのずれ（m。ワールドの向き）。足元が原点のキャラなら Y を胸・頭の高さに |
| Distance | 回る中心からカメラまでの長さ（m） |
| Pitch | 見下ろす角度（度。-80〜80）。＋ で上から見下ろす |
| Heading | カメラが追う物のどちら側に付くか。`World`（ワールドの決まった向き。Yaw 0 で +Z を向く）／`TargetForward`（追う物の前の向き＝追う物の後ろに付く）／`LookAt`（同じ Entity の Camera Look At が見る点の向き＝見る点と反対の側に付く。**ロックオン**） |
| Yaw | 横の角度（度。＋ で上から見て時計回り）。World は向きそのもの、ほかは決まった向きに足す角度（20 くらいずらすと肩越し） |
| Heading Smooth Time | 横の向きが追いつくまでの時間の目安（秒）。一番近い回り方で回る |
| Smooth Time | 回る中心が追う物に追いつくまでの時間の目安（秒） |
| Show Gizmo | Scene ビューに線を出す（エディタだけ）。黄：回る中心の十字と追う物からの線／紫：回る輪と、回る中心からカメラを置く位置への線／赤：LookAt の見る点から回る中心への線。View → Gizmos → Camera Orbits で全部まとめて消せる |

- **相手を見ながら追う物の後ろに付く（ロックオン）**：Camera Orbit（Target＝プレイヤー・Heading＝LookAt）＋ Camera Look At（Target＝ボス）。上から見て、ボス・プレイヤー・カメラが一直線に並び、写す向きは Camera Look At がボスへ向ける（Screen Position を (0, 0.15) くらいにすると、ボスが少し上・プレイヤーが下に写る）。
- ロックオンを入れる・切るは、ロックオンのカメラを別に置いて Priority か Entity の有効・無効で切り替える（Camera の Blend で補間する）。相手を変えるのは Camera Look At の Target を書き換えるだけ（Camera Orbit は同じ Entity の Camera Look At を読むので、ボスを2か所に入れない）。
- 横の向きが決まらないとき（LookAt で見る点が回る中心の真上・真下・同じ所、Camera Look At が無い・切ってある、TargetForward で追う物が真上・真下を向いている）は、そのときの向きを保つ。回り始めに決まらなければ、置いたカメラから回る中心への向きから始める。
- LookAt の向きは Camera Look At の見たい点（Smooth Time をかける前の点）で決める。回り込みの遅れは Heading Smooth Time だけで決まる。
- 同じ Entity に Camera Follow も付いていて、どちらも有効なら、**Camera Orbit だけが動かす**（Camera Follow の範囲の線も出ない）。Enabled を切った・Entity を無効にした Camera Orbit は、また動かすときに決まった位置からすぐ始める。
- 「カメラが追っている物」（`CameraSystem::GetFollowTargetEntity`・Camera Volume と Depth Of Field の FollowTarget・Trigger Activator の FollowTarget・Wind Zone の線）は、有効な Camera Orbit があればその Target。
- 親の下に置いたカメラでも、ワールドでその位置・向きになる（`EntityManager::SetWorldRotation`）。
- 壁に当たって寄る（Cinemachine の Deoccluder）・スティックで回す専用の項目は無い。回すなら Heading を World にして、Script で `yaw` を書き換える。

```cpp
// ロックオンの相手を変える（lockOnCamera は Camera Orbit の Heading が LookAt のカメラ）
if (CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(lockOnCamera)) {
	lookAt->target = EntityManager::RefOf(nextEnemy); // カメラは Heading Smooth Time で新しい相手の反対側へ回り込む
}
```
````

Camera Volume の表の Focus の行：

```md
| Focus | ピントの合わせ方。`Global`（全体の設定）／`Distance`（Focus Distance）／`LookAtTarget`（Camera Look At が見ている点）／`FollowTarget`（このカメラの Camera Follow・Camera Orbit の Target。両方あれば有効な Camera Orbit の方） |
```

**`17-Editor.md`**（View → Gizmos の行）：「Camera Icons・Camera Paths（カメラのアイコンと線）・」の後に「**Camera Orbits（Camera Orbit の回る輪・回る中心）・**」を足す（Scene ビューだけに出る物の並び）。最後の「Camera Follow と Particle System は、1つずつ」を「**Camera Follow・Camera Orbit・Particle System は、1つずつ**」に。

**`01-Overview.md`**（1フレームの順番の 5）：「`CameraSystem::UpdateFollow`（Camera Follow のカメラを、」を「`CameraSystem::UpdateFollow`（**Camera Follow・Camera Orbit** のカメラを、」に。

**`08-Rendering.md`**（Depth Of Field の Focus の行）・**`14-MathCollision.md`**（Wind Zone の Line Amount の行・Trigger Activator の Source の行）：「Camera Follow の Target」を「**Camera Follow・Camera Orbit の Target**」に。

**`Docs/Reference.md`**（索引）：カメラの行の「カメラを置く・切り替える・追いかける・揺らす」を「**カメラを置く・切り替える・追いかける・回り込む（ロックオン）・揺らす**」に。


### D-3 `project/Sandbox/Source/Scripts/OrbitCameraSamples.h`・`.cpp`（新しい）

テストシーンで使う Script を2つ。Sandbox のテスト用なので、エンジンには入れない。`Sandbox.vcxproj` と `.filters` に、TriggerCameraSamples の後ろへ足す（filters は `Source\Scripts`）。

- **OrbitCameraSwitcher**：Cameras の一覧から写すカメラを選ぶ（選んだカメラの Priority を Active Priority、ほかを 0）。Label の Text に、一覧と選んだカメラの Camera Orbit の値を毎フレーム書く（Inspector・OrbitYawInput で値を変えると、すぐ出る）
- **OrbitYawInput**：同じ Entity の Camera Orbit の `yaw`・`pitch` を書き換える。Heading が World のカメラに付けると、スティックで回す三人称のカメラになる（0.11 の「回すなら World にして Script で yaw を書き換える」の見本）

```cpp
#pragma once
#include <string>
#include <vector>

#include "HTTEngine/ECS/GameObject.h"
#include "HTTEngine/ECS/Script.h"

//=============================================================================
// カメラを番号で選ぶ（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// Cameras に入れたカメラから、写すカメラを選ぶ（Sandbox のテスト用。Camera Orbit の Heading の違いを見比べる）
/// <para>1〜9 キー：その番号のカメラ / Tab・RB：次のカメラ / LB：前のカメラ</para>
/// <para>選んだカメラの Priority を Active Priority に、ほかを 0 にする（切り替えは選んだカメラの Camera の Blend で補間する）</para>
/// <para>Label に Text の付いた Entity を入れると、カメラの一覧と、選んだカメラの Camera Orbit の Heading・Yaw などを出す</para>
/// </summary>
class OrbitCameraSwitcher : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// 選んだカメラの Priority を上げて、Label の文字を書き直す
	void Select(int index);
	// Label に出す文字（カメラの一覧と、選んだカメラの Camera Orbit の値）
	std::string MakeLabel() const;

	// ===== Inspector で変える値 =====
	std::vector<GameObject> cameras_; // 選べるカメラ（Camera Component を付けた Entity。上から 1・2・3…）
	GameObject label_;                // 一覧を出す Text の Entity（空なら出さない）
	int activePriority_ = 10;         // 選んだカメラの Priority（ほかのカメラは 0）

	// ===== 実行中だけの値 =====
	int selected_ = -1; // 今選んでいるカメラの番号（0 から。-1 はまだ選んでいない）
};


//=============================================================================
// Camera Orbit の向きを入力で回す（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 同じ Entity の Camera Orbit の Yaw・Pitch を、キーとスティックで書き換える（Sandbox のテスト用）
/// <para>Q・E か右スティックの左右：Yaw / R・F か右スティックの上下：Pitch</para>
/// <para>Heading が World のカメラに付けると、スティックで回す三人称のカメラになる（ほかの Heading では、決まった向きからのずれを変える）</para>
/// </summary>
class OrbitYawInput : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float yawSpeed_ = 120.0f;  // 横に回す速さ（度/秒）
	float pitchSpeed_ = 60.0f; // 縦に回す速さ（度/秒）
	float minPitch_ = -30.0f;  // Pitch の下限（度。見上げる）
	float maxPitch_ = 70.0f;   // Pitch の上限（度。見下ろす）
};
```

```cpp
#include "Source/Scripts/OrbitCameraSamples.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Components/Camera/CameraOrbitComponent.h"
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Time/EngineTime.h"

SCRIPT(OrbitCameraSwitcher, "Test");
SCRIPT(OrbitYawInput, "Test");

namespace {
// 1〜9 キー（DirectInput のキーの番号。数字の順に並んでいる）
constexpr std::array<uint8_t, 9> kNumberKeys = {DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8, DIK_9};
} // namespace


//=============================================================================
// OrbitCameraSwitcher
//=============================================================================
// ===== 初期化 =====
void OrbitCameraSwitcher::Start() { Select(0); }

// ===== 更新 =====
void OrbitCameraSwitcher::Update() {
	const int count = static_cast<int>(cameras_.size());
	if (count == 0) {
		return;
	}
	// --- 番号のキー ---
	for (int i = 0; i < (std::min)(count, static_cast<int>(kNumberKeys.size())); ++i) {
		if (InputManager::IsKeyTriggered(kNumberKeys[static_cast<size_t>(i)])) {
			Select(i);
			return;
		}
	}
	// --- 次・前（端まで行ったら反対の端へ）---
	if (InputManager::IsKeyTriggered(DIK_TAB) || InputManager::IsButtonTriggered(GamepadButton::RightShoulder)) {
		Select((selected_ + 1) % count);
	} else if (InputManager::IsButtonTriggered(GamepadButton::LeftShoulder)) {
		Select((selected_ + count - 1) % count);
	}
	// --- 選んだカメラの値は Inspector・OrbitYawInput で変わるので、毎フレーム書き直す ---
	if (TextComponent* text = label_.GetComponent<TextComponent>()) {
		text->text = MakeLabel();
	}
}

// ===== 選ぶ =====
void OrbitCameraSwitcher::Select(int index) {
	if (index < 0 || index >= static_cast<int>(cameras_.size())) {
		return;
	}
	selected_ = index;
	for (size_t i = 0; i < cameras_.size(); ++i) {
		if (CameraComponent* camera = cameras_[i].GetComponent<CameraComponent>()) {
			camera->priority = (static_cast<int>(i) == selected_) ? activePriority_ : 0;
		}
	}
	LogManager::Log(std::format("OrbitCameraSwitcher：{} を写す", cameras_[static_cast<size_t>(index)].name));
}

// ===== Label の文字 =====
std::string OrbitCameraSwitcher::MakeLabel() const {
	std::string label;
	for (size_t i = 0; i < cameras_.size(); ++i) {
		label += std::format("{} [{}] {}\n", (static_cast<int>(i) == selected_) ? ">" : " ", i + 1, cameras_[i].name);
	}
	if (selected_ < 0) {
		return label;
	}
	const GameObject& camera = cameras_[static_cast<size_t>(selected_)];
	if (const CameraOrbitComponent* orbit = camera.GetComponent<CameraOrbitComponent>()) {
		label += std::format("Heading {}  Yaw {:.0f}  Pitch {:.0f}  Distance {:.1f}\n", magic_enum::enum_name(orbit->heading), orbit->yaw, orbit->pitch, orbit->distance);
		const CameraLookAtComponent* lookAt = camera.GetComponent<CameraLookAtComponent>();
		label += (lookAt != nullptr && lookAt->enabled) ? "Camera Look At あり（写す向きは見る点へ）" : "Camera Look At なし（回る中心を見る）";
	}
	return label;
}

// ===== Inspector に出す・保存する値 =====
void OrbitCameraSwitcher::Fields(ComponentUI& ui) {
	ui.ListField("Cameras###cameras", cameras_, [](ComponentUI& ui, GameObject& camera) { ui.Field("Camera###camera", camera, Tip("選べるカメラ（Camera Component を付けた Entity）")); }, Tip("選べるカメラ。上から 1・2・3…のキーで選ぶ"));
	ui.Field("Label###label", label_, Tip("カメラの一覧を出す Text の Entity（空なら出さない）"));
	ui.Field("Active Priority###activePriority", activePriority_, Tip("選んだカメラの Priority（ほかのカメラは 0 にする）"));
}


//=============================================================================
// OrbitYawInput
//=============================================================================
// ===== 更新 =====
void OrbitYawInput::Update() {
	CameraOrbitComponent* orbit = GetComponent<CameraOrbitComponent>();
	const float deltaTime = Time::GetDeltaTime();
	if (orbit == nullptr || deltaTime <= 0.0f) {
		return;
	}
	// --- 入力（キーは -1・0・1、スティックは -1〜1。両方を足して -1〜1 に収める）---
	const float keyYaw = (InputManager::IsKeyPressed(DIK_E) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_Q) ? 1.0f : 0.0f);
	const float keyPitch = (InputManager::IsKeyPressed(DIK_R) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_F) ? 1.0f : 0.0f);
	const float yawInput = std::clamp(keyYaw + InputManager::GetRightStickX(), -1.0f, 1.0f);
	const float pitchInput = std::clamp(keyPitch - InputManager::GetRightStickY(), -1.0f, 1.0f); // スティックを上へ倒すと見上げる
	// --- Camera Orbit の値を書き換える（回り込みは Heading Smooth Time でなめらかになる）---
	orbit->yaw += yawInput * yawSpeed_ * deltaTime;
	orbit->pitch = std::clamp(orbit->pitch + pitchInput * pitchSpeed_ * deltaTime, minPitch_, maxPitch_);
}

// ===== Inspector に出す・保存する値 =====
void OrbitYawInput::Fields(ComponentUI& ui) {
	ui.Field("Yaw Speed###yawSpeed", yawSpeed_, AtLeast(0.0f).Tip("横に回す速さ（度/秒）"));
	ui.Field("Pitch Speed###pitchSpeed", pitchSpeed_, AtLeast(0.0f).Tip("縦に回す速さ（度/秒）"));
	ui.Field("Min Pitch###minPitch", minPitch_, Range(-80.0f, 80.0f).Tip("Pitch の下限（度。− で見上げる）"));
	ui.Field("Max Pitch###maxPitch", maxPitch_, Range(-80.0f, 80.0f).Tip("Pitch の上限（度。＋ で見下ろす）"));
}
```

## パートE 確かめ方

### E-1 ビルド

Debug・Development・Release の 3 つ（`MSBuild.exe project/HTTEngine.slnx -m -p:Configuration=Debug -p:Platform=x64` の Configuration を変えて）。警告 0 になる。

### E-2 Sandbox で見る

Sandbox を起動して、Project から `CameraOrbitTest` を開く（エディタは前に開いていたシーンから始まる。今は CameraOrbitTest にしてある）。

1. **停止中**：Scene ビューに、4 台のカメラの紫の輪（Player の上、カメラの高さ）と、回る中心の黄の十字・紫の線が出る。3・4 のカメラには、Boss から回る中心への赤い線も出る（カメラは赤い線を伸ばした先）。View → Gizmos → Camera Orbits を外すと消える。1台の Camera Orbit の Show Gizmo を外すと、その分だけ消える
2. Inspector でカメラの Camera Orbit の Distance・Pitch・Yaw を変えると、紫の線の先（再生したら置かれる位置）が動く
3. **Play**：1 の World から始まる。左上に一覧が出る。WASD で歩いても、カメラの向きは変わらず（いつも +Z を向く）、Player に付いていく。**Q・E**（右スティックの左右）で横に回り、**R・F**（右スティックの上下）で見下ろす角度が変わる。左上の Yaw・Pitch の数字も変わる
4. **2 キー**：0.5 秒かけて TargetForward へ移る。歩いて Player が向きを変えるたびに、カメラが 0.6 秒くらいかけて後ろへ回り込む
5. **3 キー**：ロックオン。Boss が画面の少し上の真ん中、Player がその手前の下に写る（Yaw 10 なので少し右の肩越し）。A・D で横へ歩くと、カメラが Boss の反対側へ回り込み続け、Boss はいつも画面の同じ所に写る。Boss が左右に往復しても同じ
6. Player が Boss の真下へ近づいても、カメラがガクッと半回転しない（見る点が回る中心の真上に来たフレームは向きを保つ・Heading Smooth Time でなめらかに回る）
7. **4 キー**：近い肩越しのロックオン。Player が画面の右手前に大きく、Boss が少し左上に写る
8. Inspector で 3 のカメラの Heading を World・TargetForward に変えると、その場で回り方が変わる（Camera Look At は付いたままなので、写す向きは Boss のまま）
9. Tab・LB・RB でも順に切り替わる。Stop：カメラは置いた位置に戻る

### E-3 写しで確かめたこと（測る Script）

写しの `test-orbit` ブランチ（答えの `answer-orbit` ＋ 測る Script）。円の上を回って進む向きを向く箱（Mover）と、左右に往復する箱（Boss）を、8 台のカメラが追う。Release で 7 秒走らせ、毎フレーム「前のフレームの回り込みの結果」を測った。

| カメラ | 中身 | 結果 |
|---|---|---|
| LockSnap | LookAt・Yaw 0・Smooth Time 両方 0・メインカメラ（Screen Position (0, 0.15)） | 位置・前の向きのずれ 0.000002、回る中心 0、横の向き 0.00000°、写した Boss が Screen Position からずれた最大 0.000000 |
| LockYawSnap | LookAt・Yaw 25・Pitch 25・Distance 5 | 位置 0.000001、横の向き 0.00000° |
| ForwardSnap | TargetForward・Yaw -30・Pitch 30 | 位置 0.000001、横の向き 0.00000° |
| WorldSnap | World・Yaw 135・Pitch -10（見上げる）・Pivot Offset (0.5, 1, -0.5) | 位置 0、横の向き 0.00000° |
| ParentedSnap | LookAt・回した・大きさ 2 の親の下 | 位置 0.000003、横の向き 0.00000°（SetWorldRotation・TranslateWorld でワールドの位置・向きが合う） |
| FollowToo | LookAt ＋ 同じ Entity に Camera Follow（Boss を追う設定） | 位置 0.000002（Camera Follow は動かしていない） |
| LockSmoothToggle | LookAt・Smooth Time 0.2・Heading Smooth Time 0.4。3 秒で Enabled を切り、4 秒で入れ直す | 位置 0.000002、1 フレームに回った最大 1.2°、入れ直した直後の回る中心のずれ 0（すぐ合わせた） |
| Degenerate | Camera Look At の見る点が回る中心と同じ所（向きが決まらない） | 横の向きが置いた側の向きのまま、1 フレームに回った最大 0° |

- 「位置」は、Transform のワールドの位置と前の向きが、`currentPivot`・`currentYaw` から別に計算した物とずれた最大（m）
- 「横の向き」は、Smooth Time 0 のカメラで、追う物・見る物の位置から別に計算した向きとずれた最大
- 測る Script はエンジンに入れない（写しだけ）
