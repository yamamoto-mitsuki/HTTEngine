参考：
- Unity の CharacterController（Slope Limit・Step Offset・Skin Width・isGrounded・collisionFlags）：https://docs.unity3d.com/Manual/class-CharacterController.html
- Unity の CharacterController.Move（動かし方・Rigidbody を押さない）：https://docs.unity3d.com/ScriptReference/CharacterController.Move.html
- Unreal の UCharacterMovementComponent（Movement Mode・Walkable Floor Angle・Max Step Height・Physics Interaction）：https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent
- PhysX の Character Controller（collide and slide・contact offset・歩けない坂の扱い）：https://nvidia-omniverse.github.io/PhysX/physx/5.4.1/docs/CharacterControllers.html
- collide and slide の元の考え方（Kasper Fauerby, Improved Collision detection and Response）：https://www.peroxide.dk/papers/collision/collision.pdf
- 関数ポインタ（`using Getter = bool (*)(...)`）：https://en.cppreference.com/w/cpp/language/pointer#Pointers_to_functions
- `std::initializer_list`（`for (x : {a, b, c})`）：https://en.cppreference.com/w/cpp/utility/initializer_list

# Character Controller（キャラクターの歩き方）

キャラクターを歩かせる Component と System をエンジンに足す。Rigidbody（物理のまね）とは別の Component にする（Unity・Unreal と同じ）。

- **形**（Collider・CollisionSystem）：どこに何があるか。今回「形を動かして最初に当たる所を探す」（**Cast**）と「今重なっている物」（**Overlap**）を足す。新しい形（地形の Heightfield など）は `CalcPushOut` を書けば、Cast も使える＝Character Controller も Rigidbody もその上で動く
- **動き方**（Rigidbody・**Character Controller**）：ぶつかったらどうするか。形の種類は知らず、CollisionSystem に聞くだけ
- **ゲーム**（Script）：どこへ動きたいか。`velocity` の X・Z を書くだけ（跳ぶなら Y も）

Character Controller がすること：

- 立てる坂（**Slope Limit**）の上では止まる。**止まっていても滑らない**
- 小さな段（**Step Offset**）はそのまま上る
- 下り坂・段を下りるときは地面に**吸い付く**（浮かない）
- 空中では**重力**で落ちる（Use Gravity・Gravity Scale）
- 壁に当たったら**壁に沿って滑る**。急な坂は上れず、乗ると**滑り落ちる**
- 動かす前に当たる所を探して止めるので、**速く動いても薄い壁をすり抜けない**
- ぶつかった **Rigidbody を押す**（重いほどゆっくり。切れる）
- Script の **OnCollision〜** が Rigidbody と同じように来る
- **ほかのキャラの頭には乗らない**（滑り落ちる）

パートは **A → B → C → D の順に写す**（C は A と B を使う）。A まで・B までのどこでもビルドできる。D は Sandbox のテスト。

ブランチは `feature/CharacterController`（develop から）。

> **2026-11-04：パートA〜D は写経せず、本体のエンジンに直接入れた**（写しで確かめたコードそのまま。入れ子の Prefab（b34ecfd）の上に積んだ）。この md は、何をなぜ直したかの解説として残す。コードを読むときは、本体の該当ファイルを見る。D-4 のテストシーンも `project/Sandbox/Resources/scenes/` に置いてある。

## 0章 全体の形

### 0.1 流れ

```
1フレーム（再生中）
  Script の Update                    ← ゲームが velocity を書く
  …（攻撃表・アニメ・Timeline）
  EntityManager::UpdateTransforms
  WindZoneSystem::Update
  CollisionSystem::SyncWorld           ← 新：形の一覧を、今の位置で集め直す
  CharacterControllerSystem::Update    ← 新：キャラを1体ずつ動かす
    1 入り込まれていたら外へ出す        Overlap
    2 速さ（跳んだ・重力・地面なら上下0）
    3 動かす（当たる所まで進み、面に沿って向きを変える。段は上る）   Cast
    4 地面を探す（地面にいた間だけ。下へ吸い付ける）                 Cast
    5 Transform を動かす・形の一覧の自分の形もずらす（MoveCollider）
    → 動かしたら UpdateTransforms
  PhysicsSystem::Update                ← Rigidbody。キャラは「動いた後の位置にある壁」
  CameraSystem::UpdateFollow           ← 地面にいるかは MotionQuery::IsGrounded
  …
  CollisionSystem::Update              ← 当たりの一覧。キャラ・Rigidbody が ReportImpact で知らせた組も入る
```

```
聞く側                         窓口（新）          答える側（Initialize で登録）
CollisionSystem（速さ）  ┐                     ┌ PhysicsSystem：Rigidbody の velocity・isGrounded
CameraFollow（地面）     ├→  MotionQuery  ──→ ┤
Trigger（AnyMover）      ┘                     └ CharacterControllerSystem：velocity・IsGrounded()
```

### 0.2 作る物・直す物

| ファイル | パート | 役目 |
|---|---|---|
| `Collision/Collision.h`・`.cpp` | A | カプセル × 多角形の押し出し（足元を見落としていた穴を直す。1.7） |
| `Systems/Physics/CollisionSystem.h`・`.cpp` | A・B | `WorldCollider::Translate`・`Cast`・`Overlap`・`SyncWorld`・`MoveCollider`／速さは MotionQuery に聞く |
| `Systems/Physics/PhysicsSystem.h`・`.cpp` | A・B | 形をずらす関数を `WorldCollider::Translate` に／`GetUp`・MotionQuery に答え方を登録・壁の速さ |
| `Physics/MotionQuery.h`・`.cpp`（新しい） | B | 動き方の窓口（速さ・地面にいるか） |
| `Systems/EngineSystems.cpp` | B・C | 窓口の初期化・終了／Character Controller の初期化・呼ぶ順・ギズモ |
| `Systems/Camera/CameraFollow.cpp`・`Components/Camera/CameraFollowComponent.h` | B | Ground Snap Y を窓口に聞く |
| `Components/Gameplay/TriggerActivatorComponent.h`・`Systems/Gameplay/TriggerActivatorSystem.cpp`・`TimelineTriggerSystem.cpp` | B | 選択肢 `AnyMover` |
| `Components/Physics/CharacterControllerComponent.h`（新しい） | C | Character Controller の値 |
| `Systems/Physics/CharacterControllerSystem.h`・`.cpp`（新しい） | C | 動かす・Inspector と保存・ギズモ |
| `Components/Physics/ColliderComponent.h`・`ECS/Script.h`・`ECS/GameComponent.h` | C | コメントだけ |
| `MyEngine_v1.vcxproj`・`.filters` | B・C | 新しいファイルを足す |
| `Sandbox/Source/Scripts/CharacterKeyboardMover.h`・`.cpp`（新しい） | D | キーボードで動かすテスト用の Script |
| `Sandbox/Sandbox.vcxproj`・`.filters` | D | Script を足す |
| `Sandbox/Resources/scenes/CharacterControllerTest.scene.json`（新しい） | D | 坂・台地・段・壁・急な坂・押せる箱のテストシーン |

### 0.3 使い方

**付け方**：Entity に **Collider**（Shape を **Capsule** か **Sphere**）と **Character Controller**（Add Component → Physics）を付ける。Rigidbody は付けない。

- Collider の Center を「高さの半分」上げて、Entity の位置を足元にすると扱いやすい（カプセル 半径 0.4・高さ 1.8 なら Center (0, 0.9, 0)）
- Character Controller の形は Collider から取る。形を2か所に持たない

**プレイヤー（毎フレーム X・Z を書く）**

```cpp
CharacterControllerComponent* controller = GetComponent<CharacterControllerComponent>();
controller->velocity.x = input.x * moveSpeed_; // 横は毎フレーム書く（止まるなら 0）
controller->velocity.z = input.z * moveSpeed_;
if (controller->IsGrounded() && jumpPressed) {
	controller->velocity.y = jumpSpeed_; // 跳ぶときだけ Y を書く。あとは重力で落ちる
}
```

**ボス（飛びかかり：1回書いて放っておく）**

```cpp
// 飛ぶ瞬間に1回だけ書く（X・Z は向き × 速さ、Y は上向きの速さ）
controller->velocity = direction * leapSpeed_ + Vector3{0.0f, leapUpSpeed_, 0.0f};
// …着地したら止める（地面に着くと Y は 0 になるが、X・Z は書いたまま残る）
if (controller->IsGrounded()) {
	controller->velocity = {0.0f, 0.0f, 0.0f};
}
```

**読める値**（実行中だけ。保存しない）

| 値 | 意味 |
|---|---|
| `IsGrounded()` | 地面に乗っている（`movementMode == Walking`） |
| `movementMode` | `Walking`（地面）・`Falling`（空中） |
| `groundNormal`・`groundPoint`・`groundEntity` | 乗っている地面の向き・触れている所・Entity |
| `hitBelow`・`hitSides`・`hitAbove` | このフレームに下（床・立てる坂）・横（壁・急な坂）・上（天井）に当たった |

**当たりの知らせ**：Script の `OnCollisionEnter / Stay / Exit` が Rigidbody と同じように来る（`CollisionInfo` の `selfVelocity` はぶつかる直前の速さ）。床に乗っている間は `OnCollisionStay` が続く。

**カメラ**：Camera Follow の Ground Snap Y は、追う物が Character Controller でも効く（Plane が XY のとき）。

**Trigger**：Trigger Activator・Timeline Trigger の Source に `AnyMover`（Rigidbody か Character Controller を付けた物なら、どれでも）を足した。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 置き場所 | Rigidbody とは別の Component・System | Rigidbody は物理のまね（滑る・跳ねる・押し合う）が正しい動き。キャラの都合を入れると、物理の物が坂で止まるなどおかしくなる（Unity・Unreal と同じ分け方） |
| 形 | 同じ Entity の Collider（Sphere・Capsule）。ほかの形・無い・無効なら動かさない | 形を2か所に持たない（Rigidbody と同じ）。足元（一番下の点）が決まる形でないと、段・坂の上り方が決まらない |
| 動かし方 | **Cast（当たる所を探して、そこで止める）**。押し出しは入り込まれたときの保険だけ | 押し出しだけだと、`Time::kMaxDeltaTime`（0.1 秒）のときダッシュ 18m/秒で 1フレーム 1.8m 進み、薄い壁の反対側へ出る（近い方へ出すので） |
| Cast の作り方 | **重なり判定で探す**（少しずつ進めて CalcPushOut、当たった区間を二分探索）。形ごとの式は作らない | 新しい形は CalcPushOut を書くだけで Cast も使える（Heightfield を足すときに、形 × 形の Sweep の式を増やさなくてよい）。遅ければ、呼ぶ側を変えずに中を式へ差し替えられる |
| 形の一覧 | **キャラを動かす前に集め直す**（`SyncWorld`）。キャラが動いたら一覧の自分の形もずらす（`MoveCollider`） | 前のフレームの形だと、動く床・ほかのキャラ・Rigidbody が1フレーム前の位置に見える。Play の直後のフレームは何にも当たらず落ちる |
| 速さ・地面の窓口 | **`MotionQuery` に答え方を登録**。CollisionSystem・Camera Follow・Trigger は窓口だけ見る | 形の層・カメラが、動き方の種類（Rigidbody・Character Controller）を名指ししない。動き方を足しても、その System の中で1行登録するだけ |
| velocity | **1本で、System も直す**（Unreal と同じ）。空中は重力で Y を足す・地面にいる間は Y が 0・天井で上向きを消す・壁へ向かう分を消す | 飛びかかり・吹っ飛びのように「1回書いて放っておく」動きが壁で止まる。毎フレーム X・Z を書くプレイヤーには関係ない |
| 坂の上を歩く速さ | 横に進む量は平らな所と同じ（坂に沿って上下を足す） | Unreal の既定と同じ。入力どおりに横へ進む（上り坂で遅くならない） |
| 急な坂に空中で当たった | 横に入り込む速さだけ消し、落ちる速さは残す | 落ちる速さまで消すと、毎フレーム X・Z を書くゲームでは、坂を滑る速さが毎フレーム消えてゆっくりしか滑り落ちない（1.4） |
| Rigidbody を押すか | **押す・切れる**（Push Rigidbodies、既定 ON）。押す速さ ＝ キャラの速さ × Push Mass ÷ (Push Mass ＋ 相手の Mass) | Unreal の Physics Interaction と同じ考え。式は PhysicsSystem の Rigidbody 同士と同じ形（重いほどゆっくり）。切れば Unity と同じ（止まるだけ） |
| Rigidbody からキャラを見ると | 動く壁（何も足していない。Rigidbody の無い Collider は、もともと壁） | Rigidbody はキャラを押せない（Unity と同じ）。OnCollision〜 の相手の速さだけ、キャラの速さを入れる |
| ほかのキャラの頭 | **乗れない・滑り落ちる**（Slope Limit ちょうどの坂とみなす） | Unreal と同じ。大きい敵が飛びかかってプレイヤーの頭に立たないように。当たりの知らせは来るので、踏みつけは Script で書ける |
| ほかのキャラどうし | お互いに壁（押し合わない） | Unity と同じ。押し合いが要るなら Script で |
| 動く床 | **今回は場所だけ**（`groundEntity`・`groundPoint` を記録） | 今の2つのゲームに動く床が無い。記録があるので、後で「Move With Ground」を足すときに作り直さなくてよい（3章） |
| Trigger の選択肢 | **`AnyMover` を足す**。`AnyRigidbody` はそのまま | 保存したシーンの動きを変えない。中は MotionQuery だけなので、動き方を足しても直さなくてよい |
| 隙間（skin） | `CollisionSystem::kContactOffset` の半分（0.005m）。項目にしない | CollisionSystem の「触れている」距離（0.01m）より狭いので、床との当たり（OnCollisionStay）が切れない。ギズモも赤くなる |
| 立てる面からの隙間・押し出し | **真上へ**空ける | 面に垂直に空けると、坂では下り側へずれる＝止まっていても少しずつ滑る（Rigidbody が坂で滑るのと同じ理由。1.1） |
| 地面に吸い付く深さ | Step Offset ＋ 横に進んだ量 × tan(Slope Limit)。項目にしない | 一番急な坂を下りても浮かない深さ（Unreal と同じ考え）。項目を増やさない |
| 動きの状態 | `CharacterMovementMode`（Walking・Falling）を持つ。`IsGrounded()` はそれから作る | Unreal の Movement Mode。泳ぐ・飛ぶを足すときに、bool を増やさず enum に足せる |
| Freeze Position | 持つ（X・Y・Z） | 横から見るゲームで Z へ押し出されないように（Rigidbody と同じ） |
| Rigidbody と一緒に付けた | Rigidbody が動かす（Character Controller は何もしない） | 2つの System が同じ Transform を動かさないように。窓口も Rigidbody が先に答える |
| Is Trigger の Collider | 相手にしない（すり抜ける） | Rigidbody と同じ |
| 時間 | `Time::GetDeltaTime()`（スロー・ヒットストップが効く。0 なら何もしない） | Rigidbody と同じ |

### 0.5 場面ごとの動作

| 場面 | どうなるか |
|---|---|
| Play | 空中（Falling）から始まり、最初のフレームで地面を見つけて Walking になる |
| 平らな床で止まる | 床から 0.005m 浮いて止まる。OnCollisionStay が続く |
| 30 度の坂で止まる | 止まる（滑らない） |
| 坂を上る・下りる | 横は入力どおりの速さ。下りでもダッシュでも浮かない |
| 0.3 以下の段 | そのまま上る |
| それより高い段 | 止まる（壁に沿って横へは進める） |
| 壁に斜めに当たる | 壁に沿って進む |
| 60 度の坂（Slope Limit 45） | 上れない。上に乗ると滑り落ちる（落ちるほど速くなる） |
| 台地の端から出る | 空中になって落ち、着地する |
| ダッシュで薄い壁（0.1m）へ | 壁の手前で止まる（1フレームに 1.5m 進むスローでも） |
| 跳ぶ | Y に上向きの速さを書くと空中へ。着地で Y が 0 になる |
| 天井に当たる | 上向きの速さが消えて落ちる |
| ほかのキャラの頭の真上に落ちる | 滑り落ちて隣に立つ（下のキャラは動かない） |
| Rigidbody の箱へ歩く | 箱を押す（重い箱ほどゆっくり）。Push Rigidbodies を外すと止まるだけ |
| Rigidbody がキャラに落ちてくる | キャラの上に乗る（キャラは押されない） |
| ヒットストップ・一時停止 | 止まる（速さも変わらない） |
| Stop | 置いた位置・保存した値に戻る（実行中の値は戻る） |
| Collider が Box・Polygon・無い・無効 | 動かさない |

### 0.6 今回やらないこと

3章にまとめた（動く床・見下ろしのカメラの高さ・Z の向きに上る坂・風・弾む面 など）。

## 1章 解説

### 1.1 Rigidbody が坂で滑る理由と、ここで滑らない理由

Rigidbody の押し出し（`PhysicsSystem.cpp` の Resolve）は、重なりを「一番近い外」へ出す。坂の面に垂直に出すので、横から見ると**少し下り側へ**ずれる。重力で毎フレーム坂へ少し入り、毎フレーム下り側へ出されるので、止まっていても滑り落ちる。物理のまね（摩擦 0 の坂）としてはこれが正しい。

```
       ／           押し出し（面に垂直）        真上へ（ここ）
     ／  ●                ↖ ●                       ↑ ●
   ／                   ／                         ／
 ／  坂               ／                         ／
                 横にずれる＝滑る           横にずれない
```

Character Controller は、立てる面（Slope Limit より緩い面）から離すとき、**真上へ**動かす。面に垂直に測って d だけ離したいなら、真上へ d ÷ cosθ（θ は面の向きと上の間の角度）。

- 地面を探して吸い付けるとき（4）：当たった所まで下ろしてから、真上へ 0.005 ÷ cosθ 上げる
- 入り込まれて外へ出すとき（1）：立てる面からは、真上へ（深さ ＋ 0.005）÷ cosθ

横には動かないので、坂の上で止まっていても、位置は変わらない（写しで 140 フレーム止めて、ずれは 0.00003m。二分探索の細かさの分だけ上下に揺れる）。

### 1.2 Cast（形を動かして、最初に当たる所を探す）

```
動き motion（長さ L）       ●───●───●───●───▶
                           t=0  ↑   ↑   ↑
                               1回に進める長さ ＝ 半径 × 0.5
                                       重なった！
                           [前の t, 今の t] を半分ずつ狭める（12回）
                           → 重ならない一番遠い t（fraction）と、
                             重なった所の CalcPushOut の向き（normal）
```

1. 動いた跡を囲む箱に触れない物は調べない
2. 相手ごとに、少しずつ進めて `CalcPushOut` で重なるかを見る。1回に進める長さは**形の半径の半分**。厚さ 0 の壁でも、球が壁に重なっている間に進む長さは直径（半径 × 2）あるので、半径の半分ずつなら必ず1回は重なる＝**飛び越えない**（かすっただけの物は見落とすことがある）
3. 重なったら、最後に重ならなかった割合と重なった割合の間を、半分ずつ狭める（12回で、1回に進める長さの 4096 分の 1 まで）
4. 一番早く触れた相手を返す。2つ目からは「今までで一番早い割合」までしか調べない

**向き**は、重なった所で `CalcPushOut` が出す向き＝当たった面の向き。どの形でも同じ書き方なので、**新しい形は CalcPushOut を書けば、Cast も Overlap も使える**。

動かす前から重なっている相手は、近づく向きなら割合 0 で当たる（それ以上入らない）。離れる向きなら当たらない（中から出られる）。

### 1.3 1フレームの流れ（collide and slide）

```
1 入り込まれていたら外へ（Overlap。一番深い物から、4回まで。押される Rigidbody からは出ない）
2 速さ
    地面にいて Y が上向き → 空中へ（跳んだ）
    空中 → Y に重力
    地面 → Y は 0
3 動かす（4回まで）
    地面にいる：横の動きを、地面の坂に沿わせる（横に進む量は同じ。上下を足す）
    Cast → 当たる所まで進む → 当たりの知らせ・Rigidbody を押す
      立てる面：真上へ隙間 → 着地・別の坂へ乗り移り（以降はその坂に沿う）
      立てない面：面の向きへ隙間 → 地面にいれば段を上ってみる
                  → だめなら面に沿わせる（残りの動きと速さから、面へ向かう分を消す）
4 地面を探す（地面にいた間だけ。下へ Cast。立てる面なら吸い付け、無ければ空中へ）
5 Transform と、形の一覧の自分の形を動かす
```

**地面にいる間、壁の向きを水平にして沿わせる**：急な坂（立てない面）の向きはそのまま使うと、沿わせたときに上向きの分が残って、急な坂を少しずつ上ってしまう。水平にすると横にだけ沿う。

### 1.4 立てない面に当たったときの「速さ」

動き（motion）は面に沿わせるが、速さ（velocity）から消す向きは面で変える。

| 面 | 速さから消す向き | 何が起きるか |
|---|---|---|
| 壁（横向き）・天井（下向き） | 面の向きそのまま | 壁へ向かう速さ・上向きの速さが消える。飛びかかりは壁で止まる |
| 上を向いた面（急な坂・ほかのキャラの頭） | 面の向きを**水平にした物** | 横に入り込む速さだけ消え、**落ちる速さは残る** |

落ちる速さまで消すと、毎フレーム X・Z を書くゲーム（止まっていれば 0 を書く）では、坂を滑る速さが毎フレーム消えて、とてもゆっくりしか滑り落ちない（写しで 0.5m/秒くらいで止まりかけた）。落ちる速さを残すと、重力で毎フレーム増え、動きを坂に沿わせたときの下向きの加速が g × sin²θ になる（摩擦の無い坂と同じ）。

### 1.5 段を上る（Step Offset）

立てない面に当たったとき、地面にいて横に進もうとしていれば、Unreal の StepUp と同じ3回の Cast を試す（試すだけで、決まるまでキャラは動かさない）。

```
    ┌─────          1 上へ Step Offset（天井があれば、その手前まで）
 ↑→ │ 段            2 前へ（残りの横の動き。途中で当たったらそこまで）
 ●  │               3 下へ（上った分 ＋ 隙間）。立てる面に着けば段
────┘
```

上った高さ ＝ 上った分 − 下りた分 ＋ 隙間。これが

- **Step Offset より高い**：段の角に、球の丸みで乗り上げただけ → 上らない
- **隙間の分もない**：下りた所が元の床＝当たったのは段ではなく壁 → 上らない（これを外さないと、斜めに当たった壁で「高さ 0 の段」を上り続け、1回に隙間の分しか進めなくなる。写しで見つけた）

### 1.6 地面に吸い付く深さ

地面にいたキャラは、動いた後に下へ Cast して地面を探す。深さは

**Step Offset ＋ 横に進んだ量 × tan(Slope Limit) ＋ 隙間 × 2**

一番急な（Slope Limit ちょうどの）下り坂を、そのフレームに進んだ分だけ下りても届く深さ。Step Offset の分で、段を下りるときも浮かない。項目を増やさずに済む（Unreal も Max Step Height で下りる）。深さの中に立てる面が無ければ空中（Falling）になる＝崖から出たら落ちる。

### 1.7 見つけた穴：カプセル × 多角形で足元を見落とす

`CollisionSystem::CalcPushOut` はカプセルを「相手に一番近い所の球」に置き換えて調べる。多角形の相手は、囲む箱の中で**一番深く入っている点**を選んでいた（`DeepestPointOnSegmentInAABB`）。坂の多角形の囲む箱は、坂の上の空いている所まで含むので、坂の下の方に立つカプセルでは、足元より上の点を選んでしまう。

```
例：高さ 5 の坂（囲む箱は 0〜5）の x=1 の所（坂の表面 y=0.5）に、半径 0.4 のカプセル（線分 y=0.9〜1.9）
  囲む箱の面までの距離で一番深い点 → y=1 より上のどこか（x=1 の左の面まで 1、下の面まで y）
  そこに置いた球は坂から 0.89 離れている → 当たっていない扱い
  本当は足元の球（y=0.9）が坂から 0.36 で、半径 0.4 より近い → 当たっている
```

Rigidbody のカプセルでも同じ（坂に沈む）。直し方：線分の上の**候補**ごとに球を出してみて、一番大きく出す物を使う（`Collision::PushOutCapsulePolygon`）。候補は

- 線分の両端
- 多角形の点ごとに、線分の上で一番近い所（角に一番近づく所）
- 線分が辺をまたぐ所（X・Y で見て。表面にちょうど触れる所）

外にある間の「線分から多角形までの距離」は、このどれかで一番短くなる（辺までの距離は、線分の上で動かすと谷が1つの形なので、一番短いのは端・角・交わる所のどれか）。点は 32 個までなので、候補は 66 個まで。

### 1.8 動き方の窓口（MotionQuery）と関数ポインタ

```cpp
using Getter = bool (*)(Handle<Entity> entity, MotionState& out);
```

`Getter` は「`Handle<Entity>` と `MotionState&` を受け取って `bool` を返す**関数の場所**」の型（関数ポインタ）。`&GetRigidbodyMotion` のように関数名に `&` を付けると、その関数の場所になる。窓口は登録された場所を `std::vector` に並べておき、`TryGet` で先頭から呼ぶ。最初に `true` を返した物（その Component を持っていた物）の答えを使う。

`std::function` にしないのは、答え方が「名前の付いた関数」だけで、何も覚えておく物が無いから（ラムダで値を持たない）。関数ポインタなら軽く、`static` な関数をそのまま渡せる。

### 1.9 あまり使わない書き方：`for (x : {a, b, c})`

```cpp
for (const Vector3& fallback : {Flatten(velocity, up), Flatten({1, 0, 0}, up), Flatten({0, 0, 1}, up)}) {
```

`{a, b, c}` は `std::initializer_list<Vector3>`（その場で作る、変えられない並び）になり、range-based for で順に回せる。並びはこの for が終わるまで生きている。「候補を順に試して、使える物が見つかったら止める」を、配列を作らずに書ける（`CalcSlideNormal`）。

## パートA 形の層：Cast・Overlap・SyncWorld と、カプセル × 多角形

### A-1 `Collision/Collision.h`

`PushOutPolygonSphere` の宣言の下に足す。

```cpp
bool PushOutPolygonSphere(const ExtrudedPolygon& a, const Sphere& b, Vector3* push, AxisLock lock = {});
// カプセルと、多角形を押し出した形。線分の上で一番深く入っている所の球を出す（坂に立つカプセルの足元も見落とさない）
bool PushOutCapsulePolygon(const Capsule& a, const ExtrudedPolygon& b, Vector3* push, AxisLock lock = {});
bool PushOutPolygonCapsule(const ExtrudedPolygon& a, const Capsule& b, Vector3* push, AxisLock lock = {});
```

### A-2 `Collision/Collision.cpp`

include に `<array>` を足す（今の並びの先頭に）。

```cpp
#include <array>
#include <cmath>
```

`PushOutPolygonSphere` の後（`// ===== 箱を箱から =====` の前）に足す。

```cpp
// ===== カプセルを、多角形を押し出した形から =====
//   カプセルは線分を半径で太らせた形なので、線分の上の「一番深く入っている点」に置いた球を出せばよい
//   その点の候補（線分の上の位置 t。0 が start、1 が end）：
//     ・線分の両端
//     ・多角形の点ごとに、線分の上で一番近い所（外にあるとき、角に一番近づく所）
//     ・線分が辺をまたぐ所（X・Y で見て。表面にちょうど触れる所）
//   外にある間の距離は、どれかの候補で一番短くなる。候補ごとに球を出してみて、一番大きく出す物を使う
//   （囲む箱で1点だけ選ぶと、坂に立つカプセルでは足元より上の点を選んでしまい、足が坂に沈む）
bool PushOutCapsulePolygon(const Capsule& a, const ExtrudedPolygon& b, Vector3* push, AxisLock lock) {
	if (b.count < 3) {
		return false; // 形が無い
	}
	const std::span<const Vector2> points = b.GetPoints();
	const Vector3 direction = a.end - a.start;
	const float lengthSq = LengthSq(direction);

	// --- 候補の t を集める（両端・点の数・辺の数。kMaxPolygonPoints が上限なので配列で足りる）---
	std::array<float, kMaxPolygonPoints * 2 + 2> candidates{};
	size_t count = 0;
	candidates[count++] = 0.0f;
	candidates[count++] = 1.0f;
	if (lengthSq > kPushEpsilon * kPushEpsilon) {
		// 点に一番近い所：点を線分へ落とした位置（線分の外なら端）
		const float nearZ = std::clamp(a.start.z, b.origin.z - b.halfDepth, b.origin.z + b.halfDepth);
		for (const Vector2& point : points) {
			const Vector3 vertex = {b.origin.x + point.x, b.origin.y + point.y, nearZ};
			candidates[count++] = std::clamp(Dot(vertex - a.start, direction) / lengthSq, 0.0f, 1.0f);
		}
		// 辺をまたぐ所：線分と辺を X・Y で見た、2本の線分の交わる所（2次元の外積で解く）
		//   線分 P(t) = s + d·t、辺 Q(u) = e0 + e·u。P(t) = Q(u) を解くと t = (e0 − s)×e ÷ (d×e)、u = (e0 − s)×d ÷ (d×e)
		const Vector2 start = {a.start.x - b.origin.x, a.start.y - b.origin.y};
		const Vector2 segment = {direction.x, direction.y};
		const auto cross = [](const Vector2& p, const Vector2& q) { return p.x * q.y - p.y * q.x; };
		for (size_t i = 0; i < points.size(); ++i) {
			const Vector2& edgeStart = points[i];
			const Vector2 edge = points[(i + 1) % points.size()] - edgeStart;
			const float denominator = cross(segment, edge);
			if (std::abs(denominator) < kPushEpsilon) {
				continue; // 平行（またがない。近い所は点の候補で見ている）
			}
			const Vector2 toEdge = edgeStart - start;
			const float t = cross(toEdge, edge) / denominator;
			const float u = cross(toEdge, segment) / denominator;
			if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
				candidates[count++] = t;
			}
		}
	}

	// --- 候補ごとに球を出してみて、一番大きく出す物を使う ---
	bool isHit = false;
	float bestSq = 0.0f;
	for (size_t i = 0; i < count; ++i) {
		const Sphere sphere = {a.start + direction * candidates[i], a.radius};
		Vector3 candidatePush;
		if (!PushOutSpherePolygon(sphere, b, &candidatePush, lock)) {
			continue;
		}
		const float pushSq = LengthSq(candidatePush);
		if (!isHit || pushSq > bestSq) {
			*push = candidatePush;
			bestSq = pushSq;
			isHit = true;
		}
	}
	return isHit;
}

// ===== 多角形をカプセルから（カプセルを多角形から出す向きの、反対へ）=====
bool PushOutPolygonCapsule(const ExtrudedPolygon& a, const Capsule& b, Vector3* push, AxisLock lock) {
	if (!PushOutCapsulePolygon(b, a, push, lock)) {
		return false;
	}
	*push = -*push;
	return true;
}
```

### A-3 `Systems/Physics/CollisionSystem.h`

**① `WorldCollider` の最後に、形をずらす関数を足す**（PhysicsSystem の `Translate` をここへ移す。CharacterControllerSystem・Cast も使う）

```cpp
	ExtrudedPolygon polygon{};                   // shape が Polygon のとき使う（box には、それを囲む箱が入る）
	bool isTrigger = false;                      // すり抜ける（押し戻さない）

	// 形を move だけずらす（向き・大きさは変えない。動かした物の形を、Transform から作り直さずに合わせる）
	void Translate(const Vector3& move) {
		sphere.center += move;
		box.min += move;
		box.max += move;
		capsule.start += move;
		capsule.end += move;
		polygon.origin += move;
	}
};
```

**② `using CollisionFilter = ...;` の下に足す**

```cpp
/// <summary>
/// 形を動かして調べる（Cast・Overlap）ときの決まり
/// <para>調べる形の持ち主・その親子とは、いつも当たらない（自分の Collider・持っている剣の Collider に止められないように）</para>
/// </summary>
struct ShapeQuery {
	CollisionFilter filter;       // 調べる相手を絞る（空なら全部）
	Collision::AxisLock lock;     // その軸へは押し出さない（当たった面の向き・押し出すずれから、その軸の分を抜く）
	bool includeTriggers = false; // Is Trigger の Collider も相手にするか（物を動かすときは、すり抜ける物を相手にしない）
};

/// <summary>
/// Cast で最初に当たった物
/// </summary>
struct ShapeCastHit {
	Handle<Entity> entity;               // 当たった Entity
	Vector3 normal = {0.0f, 0.0f, 0.0f}; // 相手から、動かした形へ押し出す向き（長さ1。当たった面の向き。床なら、だいたい (0, 1, 0)）
	Vector3 point = {0.0f, 0.0f, 0.0f};  // 当たった所（止まった所での、動かした形の表面の、相手に一番近い所。だいたいの位置）
	float fraction = 0.0f;               // 動きの何割まで進めるか（0〜1。ここまでは何にも重ならない）
	float distance = 0.0f;               // 進める距離（fraction × 動きの長さ）
};

/// <summary>
/// Overlap で重なっていた物1つ
/// </summary>
struct ShapeOverlap {
	Handle<Entity> entity;             // 重なっている Entity
	Vector3 push = {0.0f, 0.0f, 0.0f}; // 調べた形を、この相手から外へ出すずれ（CalcPushOut と同じ。足すと、ちょうど触れた所になる）
};
```

**③ クラスの説明のコメントを差し替える**

```cpp
/// <summary>
/// ColliderComponent を持つEntity同士の重なりを調べて、「当たりの一覧」を作る
/// <para>押し戻しはここではしない（Rigidbody を PhysicsSystem が、Character Controller を CharacterControllerSystem が動かす。どちらも Cast・CalcPushOut でここに聞く）。
/// ゲームは Script の OnTrigger〜 / OnCollision〜、または SYSTEM の中で GetHits を読んで、消す・音を鳴らすなどを決める</para>
/// <para>1フレームの順番：SYSTEM（前のフレームの当たりを読む）→ ワールド行列 → 形を集め直す（SyncWorld）→ キャラを動かす → Rigidbody の押し戻し
/// （どちらもぶつかった組を ReportImpact で知らせる）→ 破棄の反映 → ここで判定 → 描画</para>
/// <para>一覧などの中身は CollisionSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
```

**④ 「その場で調べる」の下の2行のコメントを差し替える**

```cpp
	// ===== その場で調べる（SYSTEMの中で、何回呼んでもよい）=====
	// 調べる相手の形は、最後に集めた物。再生中は、キャラを動かす前の SyncWorld（その後に動いた Rigidbody は動く前の位置）、Script の Update の中は前のフレームの Update
	// Play・シーン切り替えの直後の Script の Update の中は、作り直す前の Entity の形しか無いので、何にも当たらない
```

**⑤ `SweepSphere` の宣言の下に足す**

```cpp
	/// <summary>
	/// 形を motion だけ動かしたときに、最初に当たる Collider を探す（物を動かす係が、壁で止める・面に沿わせるのに使う）
	/// <para>どの形でも同じ書き方：少しずつ進めて CalcPushOut で重なりを見て、重なった区間を半分ずつ狭める。新しい形は CalcPushOut を書けば、ここでも使える</para>
	/// <para>動かす前から重なっている相手は、近づく向きなら 0 で当たる（それ以上入らない）。離れる向きなら当たらない（中から出られる）</para>
	/// </summary>
	/// <param name="shape">動かす形（ワールド座標。CalcWorldCollider で作る）</param>
	/// <param name="motion">動き（ワールド座標）</param>
	/// <param name="hit">最初に当たった物が入る（当たらなければ変えない）</param>
	/// <param name="query">調べる相手の決まり</param>
	/// <returns>当たったか</returns>
	static bool Cast(const WorldCollider& shape, const Vector3& motion, ShapeCastHit& hit, const ShapeQuery& query = {});

	/// <summary>
	/// 形に今重なっている Collider を全部集める（入り込まれたときに外へ出す量を知る）
	/// </summary>
	/// <param name="out">重なっている物と、そこから出すずれが入る（最初に空にする。並びは決まっていない）</param>
	static void Overlap(const WorldCollider& shape, std::vector<ShapeOverlap>& out, const ShapeQuery& query = {});

	/// <summary>
	/// 調べる相手の形を、今の Transform で集め直す（Update の 1 と同じ。当たりの一覧は作らない）
	/// <para>物を動かす係の前に呼ぶ（Script・アニメ・Timeline がこのフレームに動かした物を、動かした後の位置で調べる）</para>
	/// </summary>
	static void SyncWorld();

	/// <summary>
	/// 調べる相手の形のうち、entity とその子の物を move だけずらす（動かした係が呼ぶ。次に動かす物が、動いた後の位置で調べる）
	/// </summary>
	static void MoveCollider(Handle<Entity> entity, const Vector3& move);
```

**⑥ `CalcPushOut` の説明の2行を差し替える**

```cpp
	/// <para>形の組み合わせ（球・箱・カプセル・多角形）で、使う関数を選ぶ。カプセルは ReplaceCapsulesWithSpheres で球にしてから出す（多角形が相手のときだけ、線分の上で一番深い所を探す）</para>
	/// <para>多角形は球・カプセルとだけ当たる。箱 × 多角形・多角形 × 多角形は、いつも false（当たらない）</para>
```

### A-4 `Systems/Physics/CollisionSystem.cpp`

**① `NearestPointOnCapsule` の上のコメントの2行目を差し替える**

```cpp
// カプセルの線分の上で、相手（球・箱）に一番近い点。箱に入っていれば、一番深く入っている点
// 多角形は、囲む箱（box）で同じように選ぶ（押し出し・重なりの判定では使わない。CalcPushOut が多角形用の関数で、線分の上の候補から選ぶ）
```

**② `IsTouching` の前に、形を集める関数を足す**（Update の 1 を関数にして、SyncWorld でも使う）

```cpp
// 判定に使う形を集め直す（有効なEntityの、有効な Collider だけ）。Update と SyncWorld で共通
void CollectColliders() {
	state->colliders.clear();
	EntityManager::ForEach<ColliderComponent>([](Handle<Entity> entity, const ColliderComponent& collider) {
		if (!collider.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		state->colliders.push_back(CollisionSystem::CalcWorldCollider(entity, collider, *transform));
	});
}
```

**③ `Update` の 1 を差し替える**

```cpp
	// ===== 1. 判定に使う形を集める（有効なEntityの、有効な Collider だけ）=====
	CollectColliders();
```

**④ `CalcPushOut` の頭に、カプセル × 多角形を足す**

```cpp
bool CollisionSystem::CalcPushOut(WorldCollider a, WorldCollider b, Collision::AxisLock lock, Vector3* push) {
	// カプセル × 多角形：線分の上の候補から一番深い所を探す（囲む箱で1点だけ選ぶと、坂に立つカプセルの足元を見落とす）
	// 囲む箱にも触れていなければ、辺を1本ずつ調べなくても当たらない
	if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::Polygon) {
		return Collision::CapsuleAABB(a.capsule, b.box) && Collision::PushOutCapsulePolygon(a.capsule, b.polygon, push, lock);
	}
	if (a.shape == ColliderShape::Polygon && b.shape == ColliderShape::Capsule) {
		return Collision::CapsuleAABB(b.capsule, a.box) && Collision::PushOutPolygonCapsule(a.polygon, b.capsule, push, lock);
	}
	// カプセルは、相手に一番近い所の球に置き換える（その球を出す量だけ、カプセルも出す）。ここから下は球・箱・多角形だけ
	ReplaceCapsulesWithSpheres(a, b);
```

**⑤ 「その場で調べる」の名前のない namespace の最後（`RayCollider` の後、`} // namespace` の前）に足す**

```cpp
// ===== 形を動かして調べる（Cast・Overlap）の道具 =====
constexpr float kMinCastLength = 1e-6f; // これより短い動きは調べない（m）
constexpr float kCastStepRatio = 0.5f;  // 1回に進める長さ ＝ 動かす形の一番細い所の半分の太さ × これ（細かいほど、かすっただけの物も見つける）
constexpr float kMinCastStep = 0.01f;   // 1回に進める長さの下限（m。とても細い形でも、回数が増えすぎないように）
constexpr int kCastBisectCount = 12;    // 当たった区間を半分にする回数（12 回で、1回に進める長さの 4096 分の1まで狭まる）

// 形を囲む箱（調べる前に「近くにも無い物」を外すのに使う）
AABB CalcBounds(const WorldCollider& collider) {
	switch (collider.shape) {
	case ColliderShape::Sphere: {
		const Vector3 half = {collider.sphere.radius, collider.sphere.radius, collider.sphere.radius};
		return {collider.sphere.center - half, collider.sphere.center + half};
	}
	case ColliderShape::Box:
		return collider.box;
	case ColliderShape::Capsule: {
		const Capsule& capsule = collider.capsule;
		const Vector3 half = {capsule.radius, capsule.radius, capsule.radius};
		const Vector3 low = {(std::min)(capsule.start.x, capsule.end.x), (std::min)(capsule.start.y, capsule.end.y), (std::min)(capsule.start.z, capsule.end.z)};
		const Vector3 high = {(std::max)(capsule.start.x, capsule.end.x), (std::max)(capsule.start.y, capsule.end.y), (std::max)(capsule.start.z, capsule.end.z)};
		return {low - half, high + half};
	}
	case ColliderShape::Polygon: {
		// box は点だけを囲む箱なので、太らせた量（radius）を足す
		const Vector3 half = {collider.polygon.radius, collider.polygon.radius, collider.polygon.radius};
		return {collider.box.min - half, collider.box.max + half};
	}
	}
	return collider.box;
}

// 2つの箱を両方とも囲む箱
AABB MergeBounds(const AABB& a, const AABB& b) {
	return {
	    {(std::min)(a.min.x, b.min.x), (std::min)(a.min.y, b.min.y), (std::min)(a.min.z, b.min.z)},
	    {(std::max)(a.max.x, b.max.x), (std::max)(a.max.y, b.max.y), (std::max)(a.max.z, b.max.z)},
	};
}

// 形の一番細い所の、半分の太さ（Cast で1回に進める長さを決める。これより短く進めれば、壁を飛び越えない）
float CalcHalfThickness(const WorldCollider& collider) {
	switch (collider.shape) {
	case ColliderShape::Sphere:
		return collider.sphere.radius;
	case ColliderShape::Capsule:
		return collider.capsule.radius;
	case ColliderShape::Box:
	case ColliderShape::Polygon: {
		const AABB bounds = CalcBounds(collider);
		const Vector3 size = bounds.max - bounds.min;
		return (std::min)({size.x, size.y, size.z}) * 0.5f;
	}
	}
	return 0.0f;
}

// 形の表面で、direction（長さ1）の向きに一番出ている所（当たった所のだいたいの位置に使う）
Vector3 CalcFarthestPoint(const WorldCollider& collider, const Vector3& direction) {
	switch (collider.shape) {
	case ColliderShape::Sphere:
		return collider.sphere.center + direction * collider.sphere.radius;
	case ColliderShape::Capsule: {
		// 線分の端のうち、その向きに出ている方（同じくらいなら真ん中）から、半径だけ先
		const Capsule& capsule = collider.capsule;
		const float startSide = Dot(capsule.start, direction);
		const float endSide = Dot(capsule.end, direction);
		const Vector3 base = (std::abs(startSide - endSide) < 1e-4f) ? (capsule.start + capsule.end) * 0.5f : ((startSide > endSide) ? capsule.start : capsule.end);
		return base + direction * capsule.radius;
	}
	case ColliderShape::Box:
	case ColliderShape::Polygon: {
		// 箱の角のうち、その向きに一番出ている物（多角形は囲む箱で、だいたい）
		const AABB bounds = CalcBounds(collider);
		return {
		    (direction.x >= 0.0f) ? bounds.max.x : bounds.min.x,
		    (direction.y >= 0.0f) ? bounds.max.y : bounds.min.y,
		    (direction.z >= 0.0f) ? bounds.max.z : bounds.min.z,
		};
	}
	}
	return collider.sphere.center;
}

// 形を動かして調べる相手か（生きている・Is Trigger の扱い・自分と親子でない・絞り込みに合う）
bool IsShapeQueryTarget(const WorldCollider& collider, const WorldCollider& shape, const ShapeQuery& query) {
	if (!EntityManager::IsAlive(collider.entity) || (collider.isTrigger && !query.includeTriggers)) {
		return false;
	}
	if (shape.entity.IsValid() && EntityManager::IsRelated(shape.entity, collider.entity)) {
		return false; // 自分の形・自分の子に付けた形（持っている剣など）
	}
	return !query.filter || query.filter(collider.entity);
}

// shape を motion だけ動かしたとき、other に最初に触れる割合（0〜limit）を探す。limit までに触れなければ false
//   1. stepFraction ずつ進めて、重なったら止める（1回に進める長さは形の半分の太さより短いので、壁を飛び越えない）
//   2. 最後に重ならなかった割合と、重なった割合の間を、半分ずつ狭める（二分探索）
//   3. 向きは、重なった所で外へ出すずれの向き（CalcPushOut の向き＝当たった面の向き）
bool CastAgainst(const WorldCollider& shape, const Vector3& motion, const WorldCollider& other, float stepFraction, float limit, Collision::AxisLock lock, float* fraction,
                 Vector3* normal) {
	Vector3 push;
	// --- 動かす前から重なっている：近づく向きなら、そこで止める。離れる向きなら止めない（中から出られるように）---
	if (CollisionSystem::CalcPushOut(shape, other, lock, &push)) {
		if (Dot(push, motion) >= 0.0f) {
			return false;
		}
		*fraction = 0.0f;
		*normal = Normalize(push);
		return true;
	}
	// --- 1. 少しずつ進めて、重なる所を探す ---
	float freeFraction = 0.0f; // ここまでは重ならない
	while (freeFraction < limit) {
		const float next = (std::min)(freeFraction + stepFraction, limit);
		WorldCollider moved = shape;
		moved.Translate(motion * next);
		if (!CollisionSystem::CalcPushOut(moved, other, lock, &push)) {
			freeFraction = next;
			continue;
		}
		// --- 2. 重ならない所と重なる所の間を、半分ずつ狭める ---
		float hitFraction = next;
		for (int i = 0; i < kCastBisectCount; ++i) {
			const float middle = (freeFraction + hitFraction) * 0.5f;
			WorldCollider probe = shape;
			probe.Translate(motion * middle);
			Vector3 middlePush;
			if (CollisionSystem::CalcPushOut(probe, other, lock, &middlePush)) {
				hitFraction = middle;
				push = middlePush;
			} else {
				freeFraction = middle;
			}
		}
		// --- 3. 重ならない方の割合まで進めてよい。向きは重なった所の押し出す向き ---
		*fraction = freeFraction;
		*normal = Normalize(push);
		return true;
	}
	return false;
}
```

**⑥ `SweepSphere` の定義の後（`// ギズモ` の前）に足す**

```cpp
bool CollisionSystem::Cast(const WorldCollider& shape, const Vector3& motion, ShapeCastHit& hit, const ShapeQuery& query) {
	MY_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	const float length = Length(motion);
	if (length < kMinCastLength) {
		return false; // 動かない
	}
	// 動いた跡を囲む箱（動く前と後の形を両方囲む）。これに触れない物は調べない
	WorldCollider end = shape;
	end.Translate(motion);
	const AABB sweptBounds = MergeBounds(CalcBounds(shape), CalcBounds(end));
	// 1回に進める割合（形の半分の太さ × kCastStepRatio を、動きの長さで割る）
	const float stepFraction = (std::max)(CalcHalfThickness(shape) * kCastStepRatio, kMinCastStep) / length;

	// 全部の相手を調べて、一番早く触れる物を残す（総当たり。Update と同じく数百個まではこれで足りる）
	// 2つ目からは「今までで一番早い割合」までしか調べない（それより後に触れる物は要らない）
	bool isHit = false;
	float nearest = 1.0f;
	for (const WorldCollider& collider : state->colliders) {
		if (!IsShapeQueryTarget(collider, shape, query) || !Collision::AABBAABB(sweptBounds, CalcBounds(collider))) {
			continue;
		}
		float fraction = 0.0f;
		Vector3 normal;
		if (!CastAgainst(shape, motion, collider, stepFraction, nearest, query.lock, &fraction, &normal)) {
			continue;
		}
		if (isHit && fraction >= nearest) {
			continue; // 同じ割合なら、先に見つけた方
		}
		isHit = true;
		nearest = fraction;
		hit.entity = collider.entity;
		hit.normal = normal;
	}
	if (isHit) {
		hit.fraction = nearest;
		hit.distance = nearest * length;
		// 止まった所での、相手に向いた表面の点（相手は normal の反対の向きにある）
		WorldCollider stopped = shape;
		stopped.Translate(motion * nearest);
		hit.point = CalcFarthestPoint(stopped, -hit.normal);
	}
	return isHit;
}

void CollisionSystem::Overlap(const WorldCollider& shape, std::vector<ShapeOverlap>& out, const ShapeQuery& query) {
	MY_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	out.clear();
	const AABB bounds = CalcBounds(shape);
	for (const WorldCollider& collider : state->colliders) {
		if (!IsShapeQueryTarget(collider, shape, query) || !Collision::AABBAABB(bounds, CalcBounds(collider))) {
			continue;
		}
		Vector3 push;
		if (CalcPushOut(shape, collider, query.lock, &push)) {
			out.push_back({collider.entity, push});
		}
	}
}

//=============================================================================
// 調べる相手の形を、今の位置に合わせる（物を動かす係から）
//=============================================================================
void CollisionSystem::SyncWorld() {
	MY_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	CollectColliders();
}

void CollisionSystem::MoveCollider(Handle<Entity> entity, const Vector3& move) {
	MY_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	// 子に付けた形も、Transform では一緒に動くので、ここでも一緒にずらす
	for (WorldCollider& collider : state->colliders) {
		if (collider.entity == entity || EntityManager::IsDescendantOf(collider.entity, entity)) {
			collider.Translate(move);
		}
	}
}
```

### A-5 `Systems/Physics/PhysicsSystem.cpp`

`Translate` を消して（`WorldCollider::Translate` に移した）、`MoveBody` から呼ぶ。

```cpp
//=============================================================================
// 小さな道具
//=============================================================================
// Rigidbody 同士がぶつかったとき、self が動く割合（0〜1。残りは相手が動く）。重さの逆の割合（軽い方がたくさん動く）
```

```cpp
// Rigidbody を move だけ動かす（形も一緒に。物理では回さないので、ずらすだけでよい）
void MoveBody(Body& body, const Vector3& move) {
	body.moved += move;
	if (body.solid >= 0) {
		state->solids[body.solid].shape.Translate(move);
	}
}
```

ここでビルドできる（まだ何も呼んでいないので、動きは変わらない。カプセル × 多角形だけ、Rigidbody のカプセルも足元で当たるようになる）。

## パートB 動き方の窓口（MotionQuery）

### B-1 `Physics/MotionQuery.h`（新しい）

```cpp
#pragma once
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Vector3.h"

/// <summary>
/// 動き方の Component（Rigidbody・Character Controller）に共通して聞ける値
/// </summary>
struct MotionState {
	Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 今の速さ（1秒に進む距離。ワールド座標）
	bool isGrounded = false;               // 前のフレームに、地面（足元の物）に乗っていた
};

/// <summary>
/// 「この Entity の速さ・地面にいるか」を、どの動き方の Component かを知らずに聞く窓口
/// <para>動き方の System（PhysicsSystem・CharacterControllerSystem）が Initialize で「自分の Component ならこう答える」を登録する</para>
/// <para>聞く側（当たり判定・カメラの追いかけ・Trigger）は、動き方の種類を知らない。動き方を足しても、聞く側は直さなくてよい</para>
/// </summary>
class MotionQuery {
public:
	// 答え方。entity に自分の Component が付いていれば out に入れて true、付いていなければ false
	using Getter = bool (*)(Handle<Entity> entity, MotionState& out);

	// 答え方の表を作る・消す。動き方の System の Initialize より前に作り、Release より後に消す
	static void Initialize();
	static void Release();

	// 答え方を足す（動き方の System の Initialize から）。先に足した物から聞く（1つの Entity に2つの動き方を付けたら、先の物が答える）
	static void Register(Getter getter);

	// 動き方の Component が付いていれば out に入れて true。付いていなければ false（out は変えない）
	static bool TryGet(Handle<Entity> entity, MotionState& out);
	// 動き方の Component が付いているか
	static bool Has(Handle<Entity> entity);
	// 今の速さ（動き方の Component が無ければ0。動かない壁・床）
	static Vector3 GetVelocity(Handle<Entity> entity);
	// 地面に乗っているか（動き方の Component が無ければ false）
	static bool IsGrounded(Handle<Entity> entity);
};
```

### B-2 `Physics/MotionQuery.cpp`（新しい）

```cpp
#include "MotionQuery.h"

#include <memory>
#include <vector>

#include "MyEngine/Diagnostics/MyAssert.h"

namespace {
/// <summary>
/// MotionQuery が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct MotionQueryState {
	std::vector<MotionQuery::Getter> getters; // 答え方（足した順）
};
std::unique_ptr<MotionQueryState> state;
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void MotionQuery::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<MotionQueryState>();
}

void MotionQuery::Release() { state.reset(); }

void MotionQuery::Register(Getter getter) {
	MY_ASSERT_MSG(state != nullptr, "MotionQuery::Initialize()を先に呼んでください");
	MY_ASSERT_MSG(getter != nullptr, "答え方が空です");
	state->getters.push_back(getter);
}


//=============================================================================
// 聞く
//=============================================================================
bool MotionQuery::TryGet(Handle<Entity> entity, MotionState& out) {
	MY_ASSERT_MSG(state != nullptr, "MotionQuery::Initialize()を先に呼んでください");
	for (const Getter getter : state->getters) {
		if (getter(entity, out)) {
			return true;
		}
	}
	return false;
}

bool MotionQuery::Has(Handle<Entity> entity) {
	MotionState unused;
	return TryGet(entity, unused);
}

Vector3 MotionQuery::GetVelocity(Handle<Entity> entity) {
	MotionState motion;
	return TryGet(entity, motion) ? motion.velocity : Vector3{0.0f, 0.0f, 0.0f};
}

bool MotionQuery::IsGrounded(Handle<Entity> entity) {
	MotionState motion;
	return TryGet(entity, motion) && motion.isGrounded;
}
```

### B-3 `MyEngine_v1.vcxproj`

ソリューション エクスプローラーの `MyEngine/Physics` に、B-1・B-2 を「既存の項目」で足す（`.vcxproj` には次の2行が入る）。

```xml
    <ClCompile Include="MyEngine\Physics\MotionQuery.cpp" />
    <ClInclude Include="MyEngine\Physics\MotionQuery.h" />
```

### B-4 `Systems/EngineSystems.cpp`（窓口の初期化・終了）

```cpp
#include "MyEngine/Particle/ParticleManager.h"
#include "MyEngine/Physics/MotionQuery.h"
#include "MyEngine/Physics/PhysicMaterialManager.h"
```

`EngineSystems::Initialize` の `EntityManager::Initialize();` の後：

```cpp
	EntityManager::Initialize();

	MotionQuery::Initialize(); // 動き方の窓口（速さ・地面にいるか）。動き方の System（Rigidbody・Character Controller）が答え方を登録するので先に

	// --- Component を EntityManager に登録する（保存・Inspector も。どれも EntityManager の後）---
```

`EngineSystems::Release` の `EntityManager::Release();` の前：

```cpp
	UIMaskSystem::Release();
	MotionQuery::Release(); // 動き方の System の Release より後
	EntityManager::Release();
```

### B-5 `Systems/Physics/PhysicsSystem.h`・`.cpp`

**① `.h`：`SetGravity` の下に足す**

```cpp
	// 「上」の向き（長さ1。重力の反対。重力が 0 なら +Y）。地面に乗っているか・立てる坂かを決めるのに使う（Character Controller も同じ向きを使う）
	static Vector3 GetUp();
```

**② `.cpp`：include**

```cpp
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Physics/MotionQuery.h"
#include "MyEngine/Physics/PhysicMaterialManager.h"
```

**③ `.cpp`：`MoveBody` の下に、窓口への答え方を足す**

```cpp
// 動き方の窓口（MotionQuery）への答え方：Rigidbody が付いていれば、その速さと「乗っているか」
bool GetRigidbodyMotion(Handle<Entity> entity, MotionState& out) {
	const RigidbodyComponent* rigidbody = EntityManager::Get<RigidbodyComponent>(entity);
	if (rigidbody == nullptr) {
		return false;
	}
	out.velocity = rigidbody->velocity;
	out.isGrounded = rigidbody->isGrounded;
	return true;
}
```

**④ `.cpp`：`Resolve` の壁・床の `ReportImpact` を差し替える**（相手がキャラなら、キャラの速さを知らせる）

```cpp
			if (approach < 0.0f) {
				// 相手の速さは窓口に聞く（ふつうの壁は0。Character Controller なら、その速さ。押し戻しには使わない＝動く壁も止まった壁と同じに押し戻す）
				CollisionSystem::ReportImpact(body.entity, other.shape.entity, normal, rigidbody.velocity, MotionQuery::GetVelocity(other.shape.entity));
```

**⑤ `.cpp`：`Initialize` の `ComponentEditorRegistry::Register(...)` の下に足す**

```cpp
	// 速さ・乗っているかを、動き方の窓口から聞けるようにする（当たりの知らせ・カメラの追いかけ・Trigger が使う）
	MotionQuery::Register(&GetRigidbodyMotion);
```

**⑥ `.cpp`：`Update` の「上」を `GetUp` で**

```cpp
	const Vector3 gravity = GetGravity();
	const Vector3 up = GetUp(); // 地面に乗っているかを決める「上」
```

**⑦ `.cpp`：一番下に足す**

```cpp
Vector3 PhysicsSystem::GetUp() {
	const Vector3 gravity = GetGravity();
	return (LengthSq(gravity) > 1e-8f) ? Normalize(-gravity) : Vector3{0.0f, 1.0f, 0.0f};
}
```

### B-6 `Systems/Physics/CollisionSystem.cpp`（速さは窓口に聞く）

include の `RigidbodyComponent.h` を消して、`MotionQuery.h` を足す。

```cpp
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
```

```cpp
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Physics/MotionQuery.h"
#include "MyEngine/Systems/Rendering3D/PolygonShapeSystem.h"
```

`VelocityOf` を差し替える。

```cpp
// 今の速さ（Rigidbody・Character Controller など、動き方の Component が無ければ0。動かない壁・床）
// どの動き方かは知らずに、窓口（MotionQuery）に聞く
Vector3 VelocityOf(Handle<Entity> entity) { return MotionQuery::GetVelocity(entity); }
```

### B-7 `Systems/Camera/CameraFollow.cpp`・`Components/Camera/CameraFollowComponent.h`

**① `CameraFollow.cpp` の include**：`RigidbodyComponent.h` を消して、`MotionQuery.h` を足す。

```cpp
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Physics/MotionQuery.h"
```

**② Ground Snap Y の Tip**

```cpp
	    Tip("追う物が地面にいる間は、縦の遊びを使わずに合わせる（跳んでも画面は揺れず、段に乗ると上がる・降りると下がる）。追う物に Rigidbody か Character Controller が要る。Plane が XY のときだけ"));
```

**③ `Update` の `targetBody` の2行を差し替える**

```cpp
		// 地面にいるかは動き方の窓口に聞く（Rigidbody でも Character Controller でも同じ。どちらも無ければ地面にいない扱い）
		const bool isSnapY = (follow.plane == CameraFollowPlane::XY) && follow.groundSnapY && MotionQuery::IsGrounded(targetEntity);
```

**④ `CameraFollowComponent.h` の `groundSnapY` のコメント**

```cpp
	bool groundSnapY = true;              // true：追う物が地面にいる間は、縦の遊びを使わずに合わせる（XY のとき。跳んでも画面は揺れず、段に乗ると上がる）。追う物の Rigidbody・Character Controller が地面にいるかを見る（MotionQuery::IsGrounded）
```

### B-8 Trigger の選択肢 `AnyMover`

**① `Components/Gameplay/TriggerActivatorComponent.h`**（末尾に足すので、保存したシーンはそのまま読める。保存は名前）

```cpp
	AnyRigidbody, // Rigidbody を付けた物なら、どれでも
	AnyMover,     // 動き方の Component（Rigidbody・Character Controller）を付けた物なら、どれでも
};
```

**② `Systems/Gameplay/TriggerActivatorSystem.cpp`**

```cpp
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Physics/MotionQuery.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"
```

```cpp
	case TriggerSource::AnyRigidbody:
		return EntityManager::Get<RigidbodyComponent>(other) != nullptr;
	case TriggerSource::AnyMover:
		return MotionQuery::Has(other); // どの動き方かは知らずに、窓口に聞く
	}
```

`DescribeTriggerActivator` の Source の Tip と switch：

```cpp
	    Tip("誰が入ったら反応するか。FollowTarget：カメラが追っている物（プレイヤー） / Entity：Source Entity に入れた物 / AnyRigidbody：Rigidbody を付けた物なら、どれでも / AnyMover：Rigidbody か Character Controller を付けた物なら、どれでも"));
```

```cpp
	case TriggerSource::AnyRigidbody:
	case TriggerSource::AnyMover:
		break; // 選ぶ物は無い
```

**③ `Systems/Gameplay/TimelineTriggerSystem.cpp`**：②の Tip と switch と同じ2か所（判定は `TriggerActivatorSystem::IsSourceInside` を使っているので、ほかは要らない）。

ここでビルドできる（Rigidbody だけで、今までと同じ動き）。

## パートC Character Controller

### C-1 `Components/Physics/CharacterControllerComponent.h`（新しい）

```cpp
#pragma once
#include <type_traits>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Vector3.h"

// キャラクターの歩き方。Entityに付けるComponent（カテゴリ：Physics）
// 形は同じ Entity の Collider（Sphere か Capsule。無い・無効・Box・Polygon なら動かさない）。形を2か所に持たない（Rigidbody と同じ決まり）
// 動かすのは CharacterControllerSystem（再生中だけ。Script の後・物理の前）。ゲームは velocity の X・Z を書く（跳ぶなら Y に上向きの速さも）
// Rigidbody と違い、跳ね返らず・押されず・坂で滑らない。立てる坂の上では止まり、小さな段は上り、下り坂・段を下りるときは地面に吸い付く
// ほかの Rigidbody からは動く壁に見える。Rigidbody と一緒に付けない（付けたら Rigidbody が動かし、こちらは何もしない）


/// <summary>
/// 動きの状態（Unreal の Movement Mode と同じ考え。泳ぐ・飛ぶなどは、ここに足す）
/// </summary>
enum class CharacterMovementMode {
	Walking, // 地面に乗っている（重力をかけず、地面に沿って横に動く）
	Falling, // 空中（重力で落ちる。跳んだ・段から落ちた・立てない急な坂の上）
};

/// <summary>
/// キャラクターの歩き方（Unity の CharacterController・Unreal の CharacterMovementComponent の小さい版。回転は無い）
/// </summary>
struct CharacterControllerComponent {
	bool enabled = true;                   // false の間は動かさない（その場に止まる）
	float slopeLimit = 45.0f;              // 立てる坂の角度（度）。これより緩い面は地面（止まっても滑らない）。急な面は壁と同じく、沿って滑る
	float stepOffset = 0.3f;               // そのまま上れる段の高さ（m）。下り坂・段を下りるときに地面へ吸い付く距離にも使う
	bool useGravity = true;                // 空中で重力で落ちる（重力の向き・強さは Settings の Physics → World → Gravity。Rigidbody と同じ値）
	float gravityScale = 1.0f;             // 重力の倍率（2 で2倍速く落ちる。ジャンプの手触りの調整に）
	bool pushRigidbodies = true;           // true：ぶつかった Rigidbody（Is Kinematic でない物）を押す。false：止まるだけ
	float pushMass = 80.0f;                // 押すときのキャラの重さ（kg）。押す速さ ＝ キャラの速さ × pushMass ÷ (pushMass ＋ 相手の重さ)
	bool freezePositionX = false;          // その軸には動かさない（押し出しも、その軸ではしない）
	bool freezePositionY = false;
	bool freezePositionZ = false;          // 横から見るゲームなら Z を止める
	Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 速さ（1秒に進む距離。ワールド座標）。ゲームは X・Z を書く。Y は空中で重力が足され、地面にいる間は 0。壁に当たると壁へ向かう分が消える

	// --- 実行中だけの値（CharacterControllerSystem が書く。保存しない）---
	CharacterMovementMode movementMode = CharacterMovementMode::Falling; // 今の動きの状態（Play の直後は空中から始めて、最初のフレームで地面を見つける）
	Vector3 groundNormal = {0.0f, 1.0f, 0.0f}; // 乗っている地面の向き（長さ1。Walking の間だけ意味がある）
	Vector3 groundPoint = {0.0f, 0.0f, 0.0f};  // 地面に触れている所（ワールド座標。Walking の間だけ意味がある）
	Handle<Entity> groundEntity;               // 乗っている地面の Entity（Walking の間だけ意味がある。後で「動く床と一緒に動く」に使う）
	bool hitSides = false;                     // このフレームに横（壁・立てない急な坂）に当たった
	bool hitAbove = false;                     // このフレームに上（天井）に当たった
	bool hitBelow = false;                     // このフレームに下（床・立てる坂・ほかのキャラの頭）に当たった

	// 地面に乗っているか（前のフレームの終わりに）
	bool IsGrounded() const { return movementMode == CharacterMovementMode::Walking; }
};

static_assert(std::is_trivially_copyable_v<CharacterControllerComponent>);
```

### C-2 `Systems/Physics/CharacterControllerSystem.h`（新しい）

```cpp
#pragma once
#include "MyEngine/Components/Physics/CharacterControllerComponent.h"

// 前方宣言
class Camera;


/// <summary>
/// Character Controller を、速さと重力で動かす（立てる坂で止まる・段を上る・壁に沿って滑る・地面に吸い付く）
/// <para>形は同じ Entity の Collider（Sphere・Capsule）。何に当たるかは CollisionSystem::Cast・Overlap に聞く（形の種類は知らない。新しい形も、そのまま歩ける）</para>
/// <para>押し出しだけでなく、動かす前に当たる所を探して（Cast）そこで止めるので、速く動いても薄い壁をすり抜けない</para>
/// <para>ぶつかった組は CollisionSystem::ReportImpact で知らせる（Script の OnCollision〜 が Rigidbody と同じように来る）</para>
/// <para>動かすのは再生中だけ。一時停止・ヒットストップ中は時間が0なので止まる。スロー（SetGameTimeScale）ではゆっくり動く</para>
/// </summary>
class CharacterControllerSystem {
public:
	// CharacterControllerComponent を登録する（置き場所・保存・Inspector・動き方の窓口）。EntityManager・MotionQuery の Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// Character Controller を動かす。ワールド行列の計算と CollisionSystem::SyncWorld の後、物理（PhysicsSystem）の前に呼ぶ
	/// </summary>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。0 なら何もしない）</param>
	/// <returns>動かした物があったか（あれば、呼んだ側がワールド行列を計算し直す）</returns>
	static bool Update(float deltaTime);

	// 上れる段の高さの輪・地面の向きの線を描く（エディタ用。Collider のギズモと一緒に出す・消す）
	static void DrawGizmos(Camera* camera);
};
```

### C-3 `Systems/Physics/CharacterControllerSystem.cpp`（新しい）

関数の並び：小さな道具 → 当たった物への働きかけ（OnHit・PushRigidbody）→ 1 Depenetrate → 2 UpdateVelocity → 3 TryStepUp・CalcSlideNormal・CalcVelocityNormal・SlideMove → 4 FindGround → MoveController（1体分）→ 窓口・Inspector → Initialize・Update・DrawGizmos。

```cpp
#include "CharacterControllerSystem.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "MyEngine/Collision/Collision.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/Physics/ColliderComponent.h"
#include "MyEngine/Components/Physics/RigidbodyComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Gizmos/GizmoLines.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Physics/MotionQuery.h"
#include "MyEngine/Systems/Physics/CollisionSystem.h"
#include "MyEngine/Systems/Physics/PhysicsSystem.h"

namespace {
constexpr float kSkinWidth = CollisionSystem::kContactOffset * 0.5f; // 周りの物と空けておく隙間（m）。CollisionSystem の「触れている」距離より狭くして、乗っている床との当たり（OnCollisionStay）が切れないように
constexpr int kMaxSlideCount = 4;         // 1フレームに「当たって向きを変えて、残りを進む」回数の上限（角で何回も当たっても、ここで止める）
constexpr int kMaxDepenetrationCount = 4; // 入り込まれたときに外へ出す回数の上限（深い物から1つずつ出す）
constexpr float kMinMoveSq = 1e-10f;      // これより短い動きは動かない（長さの2乗。m²）
constexpr float kMinJumpSpeed = 0.001f;   // 地面にいるとき、上向きの速さがこれより大きければ「跳んだ」（m/s。計算の誤差で跳ばないように）
constexpr float kMinNormalUp = 1e-4f;     // 面の向きの「上」の成分の下限（0 で割らないように）
constexpr float kMinMass = 0.01f;         // 重さの下限（0で割らないように。Rigidbody と同じ）
constexpr float kMaxSlopeLimit = 89.0f;   // Slope Limit の上限（度。90 だと壁にも立ててしまう）
constexpr Vector4 kStepColor = {1.0f, 0.8f, 0.2f, 1.0f};   // 上れる段の高さの輪の色（黄）
constexpr Vector4 kGroundColor = {0.3f, 0.6f, 1.0f, 1.0f}; // 地面の向きの線の色（青）
constexpr float kGroundLineLength = 0.5f; // 地面の向きの線の長さ（m）
constexpr float kNoFadeStart = 10000.0f;  // 距離でフェードさせないための値（Collider のギズモと同じ）
constexpr float kNoFadeEnd = 20000.0f;

/// <summary>
/// CharacterControllerSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct ControllerSystemState {
	std::vector<ShapeOverlap> overlaps;  // Overlap の結果の置き場（配列のメモリを使い回す）
	Renderer::LineListConfig gizmoLines; // ギズモの線の置き場（同上）
};
std::unique_ptr<ControllerSystemState> state;

/// <summary>
/// 1体を動かす間だけ使う物
/// </summary>
struct Mover {
	Handle<Entity> entity;                              // 持ち主
	CharacterControllerComponent* controller = nullptr; // 動かしている間だけ持つ（その間、Component の配列は増えも減りもしない）
	WorldCollider shape;                                // 今の形（動かすたびに一緒にずらす）
	Vector3 moved = {0.0f, 0.0f, 0.0f};                 // このフレームに動いた量（ワールド座標）
	Vector3 up = {0.0f, 1.0f, 0.0f};                    // 上（重力の反対。長さ1）
	float walkableCos = 0.0f;                           // 面の向きと上の間の cos がこれ以上なら立てる（cos(Slope Limit)）
	ShapeQuery query;                                   // 調べる相手の決まり（Is Trigger は相手にしない。止めた軸へは押さない）
};

//=============================================================================
// 小さな道具
//=============================================================================
// 上の向きの分を抜く（地面に平らにする）
Vector3 Flatten(const Vector3& vector, const Vector3& up) { return vector - up * Dot(vector, up); }

// 面へ近づく分を抜く（離れる向きなら、そのまま）。面に沿った分だけが残る
Vector3 RemoveApproach(const Vector3& vector, const Vector3& normal) {
	const float approach = Dot(vector, normal);
	return (approach < 0.0f) ? vector - normal * approach : vector;
}

// 地面の上で横に horizontal だけ進む動き。坂なら、坂に沿うように上下を足す（横に進む量は坂でも同じ。Unreal と同じ）
//   地面を「groundNormal を向いた平面」とみて、horizontal ＋ 上 × k が平面に乗る k を解く：Dot(horizontal ＋ 上 × k, n) = 0 → k = −Dot(horizontal, n) ÷ Dot(上, n)
Vector3 AlongGround(const Vector3& horizontal, const Vector3& groundNormal, const Vector3& up) {
	const float normalUp = Dot(groundNormal, up);
	if (normalUp < kMinNormalUp) {
		return horizontal;
	}
	return horizontal - up * (Dot(horizontal, groundNormal) / normalUp);
}

// 立てる面から隙間（kSkinWidth）を空けるずれ。真上へ空ける（面に垂直に空けると、坂では下り側へずれる＝止まっていても少しずつ滑る）
// 面に垂直に測って kSkinWidth になるよう、上へ kSkinWidth ÷ cos（面の向きと上の間）だけ
Vector3 CalcLift(const Mover& mover, const Vector3& normal) { return mover.up * (kSkinWidth / (std::max)(Dot(normal, mover.up), kMinNormalUp)); }

// その Entity（か親）が Character Controller を持つか（体の Collider を子に付けたキャラも、キャラとして数える）
bool IsCharacter(Handle<Entity> entity) {
	for (Handle<Entity> current = entity; EntityManager::IsAlive(current); current = EntityManager::Get(current)->parent) {
		if (EntityManager::Get<CharacterControllerComponent>(current) != nullptr) {
			return true;
		}
	}
	return false;
}

// 押される Rigidbody（Is Kinematic でない物）。無ければ nullptr
RigidbodyComponent* FindPushable(Handle<Entity> entity) {
	RigidbodyComponent* rigidbody = EntityManager::Get<RigidbodyComponent>(entity);
	return (rigidbody != nullptr && !rigidbody->isKinematic) ? rigidbody : nullptr;
}

// 立てる面か（緩い坂・床。ほかのキャラの頭には立たない＝滑り落ちる。Unreal と同じ）
bool IsWalkable(const Mover& mover, const Vector3& normal, Handle<Entity> entity) { return Dot(normal, mover.up) >= mover.walkableCos && !IsCharacter(entity); }

// キャラを move だけ動かす（形も一緒に。止めた軸へは動かさない）
void Translate(Mover& mover, const Vector3& move) {
	const Vector3 free = Collision::RemoveLockedAxes(move, mover.query.lock);
	mover.shape.Translate(free);
	mover.moved += free;
}

// 立った地面を覚える
void SetGround(Mover& mover, const ShapeCastHit& hit) {
	CharacterControllerComponent& controller = *mover.controller;
	controller.movementMode = CharacterMovementMode::Walking;
	controller.groundNormal = hit.normal;
	controller.groundPoint = hit.point;
	controller.groundEntity = hit.entity;
}

// 同じ Entity の Collider から、動かす形を作る（球・カプセルだけ。無い・無効・ほかの形なら false）
// 球・カプセルでないと、足元（一番下の点）が決まらず、段・坂の上り方が決まらない
bool MakeShape(Handle<Entity> entity, WorldCollider& out) {
	const ColliderComponent* collider = EntityManager::Get<ColliderComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (collider == nullptr || transform == nullptr || !collider->enabled) {
		return false;
	}
	if (collider->shape != ColliderShape::Sphere && collider->shape != ColliderShape::Capsule) {
		return false;
	}
	out = CollisionSystem::CalcWorldCollider(entity, *collider, *transform);
	return true;
}

//=============================================================================
// 当たった物への働きかけ（当たった向きの印・当たりの知らせ・Rigidbody を押す）
//=============================================================================
// ===== Rigidbody を押す =====
// ぶつかった向き（水平）の相手の速さを「キャラの速さ × 重さの割合」まで上げる。キャラはこのフレームはそこで止まり、相手はこの後の物理が動かす
// 次のフレームは、相手が動いた分だけキャラも進める（押している間は、キャラも相手と同じ速さになる）
void PushRigidbody(const Mover& mover, const ShapeCastHit& hit) {
	const CharacterControllerComponent& controller = *mover.controller;
	RigidbodyComponent* rigidbody = controller.pushRigidbodies ? FindPushable(hit.entity) : nullptr;
	if (rigidbody == nullptr) {
		return;
	}
	// 押す向き：当たった面の向きの反対（キャラから相手へ）を水平にした物。上に乗っても、床へ押し付けない
	const Vector3 flat = Flatten(-hit.normal, mover.up);
	if (LengthSq(flat) <= kMinMoveSq) {
		return;
	}
	const Vector3 direction = Normalize(flat);
	// 押す速さ ＝ キャラのその向きの速さ × 重さの割合（PhysicsSystem の Rigidbody 同士と同じ形。重い物ほどゆっくり）
	const float pushMass = (std::max)(controller.pushMass, kMinMass);
	const float share = pushMass / (pushMass + (std::max)(rigidbody->mass, kMinMass));
	const float wanted = Dot(controller.velocity, direction) * share;
	const float current = Dot(rigidbody->velocity, direction);
	if (wanted <= current) {
		return; // もう同じか、それより速く離れている
	}
	const Collision::AxisLock lock = {rigidbody->freezePositionX, rigidbody->freezePositionY, rigidbody->freezePositionZ};
	rigidbody->velocity += Collision::RemoveLockedAxes(direction * (wanted - current), lock);
}

// ===== 当たった1件 =====
void OnHit(const Mover& mover, const ShapeCastHit& hit) {
	CharacterControllerComponent& controller = *mover.controller;
	// --- 当たった向きの印（面の向きで分ける。立てない急な坂は横）---
	const float normalUp = Dot(hit.normal, mover.up);
	if (normalUp >= mover.walkableCos) {
		controller.hitBelow = true;
	} else if (normalUp <= -mover.walkableCos) {
		controller.hitAbove = true;
	} else {
		controller.hitSides = true;
	}
	// --- 当たりの知らせ（Script の OnCollision〜）。速さを変える前の速さで知らせる。同じ相手は、そのフレームの最初の1回だけ覚えられる ---
	CollisionSystem::ReportImpact(mover.entity, hit.entity, hit.normal, controller.velocity, MotionQuery::GetVelocity(hit.entity));
	PushRigidbody(mover, hit);
}

//=============================================================================
// 1. 入り込まれていたら外へ出す（動く物・ほかのキャラに入り込まれた・置いた位置が重なっていた）
//=============================================================================
void Depenetrate(Mover& mover) {
	for (int i = 0; i < kMaxDepenetrationCount; ++i) {
		CollisionSystem::Overlap(mover.shape, state->overlaps, mover.query);
		// 一番深く入っている物から出す。押される Rigidbody からは出ない（相手の方が、物理でキャラ（壁）から押し出される）
		const ShapeOverlap* deepest = nullptr;
		for (const ShapeOverlap& overlap : state->overlaps) {
			if (FindPushable(overlap.entity) != nullptr) {
				continue;
			}
			if (deepest == nullptr || LengthSq(overlap.push) > LengthSq(deepest->push)) {
				deepest = &overlap;
			}
		}
		if (deepest == nullptr) {
			return; // 重なっていない
		}
		const Vector3 normal = Normalize(deepest->push);
		const float depth = Length(deepest->push) + kSkinWidth; // 出した後に隙間も空ける
		if (IsWalkable(mover, normal, deepest->entity)) {
			// 立てる面からは真上へ（面に垂直に出すと、坂では下り側へずれる）。面に垂直に測って depth 出るだけ上げる
			Translate(mover, mover.up * (depth / Dot(normal, mover.up)));
		} else {
			Translate(mover, normal * depth);
		}
	}
}

//=============================================================================
// 2. 速さ（跳んだら空中へ・空中なら重力・地面なら上下の速さは0）
//=============================================================================
void UpdateVelocity(Mover& mover, const Vector3& gravity, float deltaTime) {
	CharacterControllerComponent& controller = *mover.controller;
	if (controller.IsGrounded() && Dot(controller.velocity, mover.up) > kMinJumpSpeed) {
		controller.movementMode = CharacterMovementMode::Falling; // ゲームが上向きの速さを書いた＝跳んだ
	}
	switch (controller.movementMode) {
	case CharacterMovementMode::Walking:
		// 地面に沿って動くので、上下の速さは使わない（下向きを書いても地面へは潜らない）
		controller.velocity = Flatten(controller.velocity, mover.up);
		break;
	case CharacterMovementMode::Falling:
		if (controller.useGravity) {
			controller.velocity += gravity * (controller.gravityScale * deltaTime);
		}
		break;
	}
	controller.velocity = Collision::RemoveLockedAxes(controller.velocity, mover.query.lock);
}

//=============================================================================
// 3. 動かす
//=============================================================================
// ===== 段を上る =====
// 上へ Step Offset → 前へ → 下へ。下りた所が立てる面で、上った高さが Step Offset 以下なら、そこへ動かす（Unreal の StepUp と同じ手順）
// 上れたら motion を残りの横の動きにして true
bool TryStepUp(Mover& mover, Vector3& motion) {
	const CharacterControllerComponent& controller = *mover.controller;
	const Vector3 forward = Flatten(motion, mover.up);
	if (controller.stepOffset <= 0.0f || LengthSq(forward) <= kMinMoveSq) {
		return false; // 上る段が無い・横に進もうとしていない
	}
	WorldCollider probe = mover.shape; // 試しに動かす形（上れると決まるまで、キャラは動かさない）
	ShapeCastHit hit;
	// --- 1. 上へ（天井があれば、その手前まで）---
	float rise = controller.stepOffset;
	if (CollisionSystem::Cast(probe, mover.up * rise, hit, mover.query)) {
		rise = hit.distance - kSkinWidth;
	}
	if (rise <= kSkinWidth) {
		return false; // 上に余裕が無い
	}
	probe.Translate(mover.up * rise);
	// --- 2. 前へ（上った高さで、残りの横の動きだけ。途中で当たったら、そこまで）---
	float advance = 1.0f;
	if (CollisionSystem::Cast(probe, forward, hit, mover.query)) {
		advance = hit.fraction;
	}
	if (LengthSq(forward * advance) <= kMinMoveSq) {
		return false; // 上っても前へ進めない（段ではなく、高い壁）
	}
	probe.Translate(forward * advance);
	// --- 3. 下へ（上った分と隙間の分。その間に立てる面が無ければ、段ではない）---
	const float drop = rise + kSkinWidth * 2.0f;
	if (!CollisionSystem::Cast(probe, -mover.up * drop, hit, mover.query) || !IsWalkable(mover, hit.normal, hit.entity)) {
		return false;
	}
	// --- 4. 上った高さが段になっているか ---
	//   Step Offset を超えた：段の角に、形の丸みで乗り上げただけ
	//   隙間の分も上っていない：下りた所が元の床（当たったのは段ではなく壁。斜めに当たった壁で「高さ 0 の段」を上り続けて、沿って進めなくなる）
	const float climbed = rise - hit.distance + Dot(CalcLift(mover, hit.normal), mover.up);
	if (climbed > controller.stepOffset + kSkinWidth || climbed <= kSkinWidth) {
		return false;
	}
	// --- 5. 上る ---
	Translate(mover, mover.up * climbed + forward * advance);
	motion = forward * (1.0f - advance); // 残りの横の動き
	SetGround(mover, hit);
	return true;
}

// ===== 立てない面に沿うときに使う向き =====
//   地面にいる間：面の向きを水平にする（急な坂を上らない・壁に沿って上下へずれない）
//   空中でほかのキャラの頭に当たった：Slope Limit ちょうどの坂とみなす（真上に乗ったままにならず、滑り落ちる）
//   それ以外（空中で壁・急な坂・天井）：面の向きのまま（急な坂なら、沿って滑り落ちる）
Vector3 CalcSlideNormal(const Mover& mover, const ShapeCastHit& hit) {
	const Vector3 flat = Flatten(hit.normal, mover.up);
	if (mover.controller->IsGrounded()) {
		return (LengthSq(flat) > kMinMoveSq) ? Normalize(flat) : hit.normal;
	}
	if (Dot(hit.normal, mover.up) < mover.walkableCos) {
		return hit.normal;
	}
	// ここに来るのは「立てる角度だが、ほかのキャラ」だけ（立てる面は SlideMove で先に扱う）
	// 滑り落ちる向き：面の向きの水平の分。真上から乗った（水平の分が無い）なら、動いていた向き。それも無ければ X、Z の向き
	Vector3 away = flat;
	for (const Vector3& fallback : {Flatten(mover.controller->velocity, mover.up), Flatten({1.0f, 0.0f, 0.0f}, mover.up), Flatten({0.0f, 0.0f, 1.0f}, mover.up)}) {
		if (LengthSq(away) > kMinMoveSq) {
			break;
		}
		away = fallback;
	}
	const float sinLimit = std::sqrt((std::max)(1.0f - mover.walkableCos * mover.walkableCos, 0.0f));
	return Normalize(Normalize(away) * sinLimit + mover.up * mover.walkableCos);
}

// ===== 立てない面に当たったとき、速さから消す向き =====
//   壁・天井：面の向きのまま（壁へ向かう速さ・天井へ向かう上向きの速さを消す。飛びかかり・吹っ飛びのように、1回書いて放っておく動きも、壁で止まる）
//   上を向いた面（急な坂・ほかのキャラの頭）：面の向きを水平にした物（横に入り込む速さだけ消し、落ちる速さは残す）
//     落ちる速さまで消すと、毎フレーム X・Z を書くゲームでは、坂を滑る速さが毎フレーム消えて、ゆっくりしか滑り落ちない
//     落ちる速さを残して、動き（motion）の方を面に沿わせると、滑る速さが重力で増えていく（摩擦の無い坂と同じ、下向きに g × sin² の加速）
Vector3 CalcVelocityNormal(const Mover& mover, const Vector3& normal) {
	if (Dot(normal, mover.up) <= 0.0f) {
		return normal;
	}
	const Vector3 flat = Flatten(normal, mover.up);
	return (LengthSq(flat) > kMinMoveSq) ? Normalize(flat) : Vector3{0.0f, 0.0f, 0.0f}; // 真上を向いた面は、横の速さを消さない（RemoveApproach が何もしない）
}

// ===== 当たる所まで進んで、当たった面に沿って向きを変え、残りを進む（collide and slide）=====
void SlideMove(Mover& mover, Vector3 motion) {
	CharacterControllerComponent& controller = *mover.controller;
	for (int i = 0; i < kMaxSlideCount && LengthSq(motion) > kMinMoveSq; ++i) {
		ShapeCastHit hit;
		if (!CollisionSystem::Cast(mover.shape, motion, hit, mover.query)) {
			Translate(mover, motion);
			return; // 何にも当たらない
		}
		// --- 当たる所まで進む ---
		Translate(mover, motion * hit.fraction);
		motion *= 1.0f - hit.fraction; // 残り
		OnHit(mover, hit);

		// --- 立てる面（床・緩い坂）：真上へ隙間を空けて、残りを地面に沿わせる ---
		if (IsWalkable(mover, hit.normal, hit.entity)) {
			Translate(mover, CalcLift(mover, hit.normal));
			if (controller.IsGrounded() || Dot(controller.velocity, mover.up) <= 0.0f) {
				// 歩いて別の坂へ乗り移った・落ちてきて着地した：その面に立つ（下へ向かう速さは消す）
				controller.velocity = Flatten(controller.velocity, mover.up);
				SetGround(mover, hit);
				motion = AlongGround(Flatten(motion, mover.up), hit.normal, mover.up);
			} else {
				// 跳んで上っている途中に、坂の下側から当たった：沿って進む（まだ着地しない）
				motion = RemoveApproach(motion, hit.normal);
				controller.velocity = RemoveApproach(controller.velocity, CalcVelocityNormal(mover, hit.normal));
			}
			continue;
		}

		// --- 立てない面（壁・急な坂・天井・ほかのキャラ）：面から隙間を空ける ---
		Translate(mover, hit.normal * kSkinWidth);
		// 段を上る（地面にいて、横に進もうとしていたときだけ）
		if (controller.IsGrounded() && TryStepUp(mover, motion)) {
			continue;
		}
		// 面に沿う
		const Vector3 normal = CalcSlideNormal(mover, hit);
		motion = RemoveApproach(motion, normal);
		controller.velocity = RemoveApproach(controller.velocity, CalcVelocityNormal(mover, normal));
	}
}

//=============================================================================
// 4. 地面を探す（地面にいた間だけ。下り坂・段を下りるときに浮かないよう、下へ吸い付ける）
//=============================================================================
void FindGround(Mover& mover, float horizontalDistance) {
	CharacterControllerComponent& controller = *mover.controller;
	if (!controller.IsGrounded()) {
		return; // 空中（跳んだ・落ちている）なら、着地は SlideMove で見る
	}
	// 探す深さ：段の高さ ＋ 横に進んだ分だけ一番急な坂を下りた高さ ＋ 隙間（Unreal と同じ考え。項目は増やさない）
	const float tanLimit = std::sqrt((std::max)(1.0f - mover.walkableCos * mover.walkableCos, 0.0f)) / (std::max)(mover.walkableCos, kMinNormalUp);
	const float depth = controller.stepOffset + horizontalDistance * tanLimit + kSkinWidth * 2.0f;
	ShapeCastHit hit;
	if (CollisionSystem::Cast(mover.shape, -mover.up * depth, hit, mover.query) && IsWalkable(mover, hit.normal, hit.entity)) {
		// 隙間を残して下ろす（真上の向きに測る。面に垂直に下ろすと、坂では下り側へずれる）
		Translate(mover, -mover.up * hit.distance + CalcLift(mover, hit.normal));
		SetGround(mover, hit);
		OnHit(mover, hit); // 乗っている間も、毎フレーム当たりを知らせる（OnCollisionStay が切れないように）
		return;
	}
	controller.movementMode = CharacterMovementMode::Falling; // 足元に立てる面が無い（崖から出た・急な坂へ出た・ほかのキャラの頭）
}

//=============================================================================
// 1体を動かす
//=============================================================================
bool MoveController(Handle<Entity> entity, CharacterControllerComponent& controller, const Vector3& gravity, const Vector3& up, float deltaTime) {
	// --- 早期リターン ---
	if (!controller.enabled || !EntityManager::IsActiveInHierarchy(entity) || EntityManager::Get<RigidbodyComponent>(entity) != nullptr) {
		return false; // 止めてある・無効・Rigidbody が動かす
	}
	Mover mover;
	if (!MakeShape(entity, mover.shape)) {
		return false; // 形が無い
	}

	// --- 準備 ---
	mover.entity = entity;
	mover.controller = &controller;
	mover.up = up;
	mover.walkableCos = std::cos(std::clamp(controller.slopeLimit, 0.0f, kMaxSlopeLimit) * MathUtility::kDegToRad);
	mover.query.lock = {controller.freezePositionX, controller.freezePositionY, controller.freezePositionZ};
	controller.hitSides = false; // 当たった向きは毎フレーム作り直す
	controller.hitAbove = false;
	controller.hitBelow = false;

	// --- 1. 入り込まれていたら外へ → 2. 速さ ---
	Depenetrate(mover);
	UpdateVelocity(mover, gravity, deltaTime);

	// --- 3. 動かす。地面にいれば地面に沿って、空中なら速さのまま ---
	Vector3 motion = controller.velocity * deltaTime;
	float horizontalDistance = 0.0f;
	if (controller.IsGrounded()) {
		horizontalDistance = Length(motion); // 地面にいる間の velocity は水平
		motion = AlongGround(motion, controller.groundNormal, up);
	}
	SlideMove(mover, Collision::RemoveLockedAxes(motion, mover.query.lock));

	// --- 4. 地面を探す ---
	FindGround(mover, horizontalDistance);

	// --- 5. 動いた量を Transform へ。調べる相手の形も動かす（次のキャラが、動いた後の位置で調べる）---
	if (LengthSq(mover.moved) == 0.0f) {
		return false;
	}
	EntityManager::TranslateWorld(entity, mover.moved);
	CollisionSystem::MoveCollider(entity, mover.moved);
	return true;
}

//=============================================================================
// 動き方の窓口・Inspector と 保存
//=============================================================================
// 動き方の窓口（MotionQuery）への答え方：Character Controller が付いていれば、その速さと「地面にいるか」
bool GetControllerMotion(Handle<Entity> entity, MotionState& out) {
	const CharacterControllerComponent* controller = EntityManager::Get<CharacterControllerComponent>(entity);
	if (controller == nullptr) {
		return false;
	}
	out.velocity = controller->velocity;
	out.isGrounded = controller->IsGrounded();
	return true;
}

// Character Controller の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Rigidbody と同じ書き方）
void DescribeCharacterController(ComponentUI& ui, CharacterControllerComponent& value) {
	ui.Label("形は同じ Entity の Collider（Sphere か Capsule）。Rigidbody とは一緒に付けない");
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと動かさない（その場に止まる）"));
	ui.Field("Slope Limit###slopeLimit", value.slopeLimit, Range(0.0f, kMaxSlopeLimit).Tip("立てる坂の角度（度）。これより緩い面は地面（止まっても滑らない）。急な面は壁と同じく、沿って滑る"));
	ui.Field("Step Offset###stepOffset", value.stepOffset, AtLeast(0.0f).Tip("そのまま上れる段の高さ（m）。下り坂・段を下りるときに、地面へ吸い付く距離にも使う"));
	ui.Field("Use Gravity###useGravity", value.useGravity, Tip("空中で重力で落ちる（向きと強さは Settings → Physics。Rigidbody と同じ値）"));
	ui.Field("Gravity Scale###gravityScale", value.gravityScale, Tip("重力の倍率（2 で2倍速く落ちる。ジャンプの手触りの調整に）"));
	ui.Separator("Push");
	ui.Field("Push Rigidbodies###pushRigidbodies", value.pushRigidbodies, Tip("ぶつかった Rigidbody（Is Kinematic でない物）を押す。外すと止まるだけ（押したければ Script の OnCollision〜 で）"));
	ui.BeginDisabled(!value.pushRigidbodies);
	ui.Field("Push Mass###pushMass", value.pushMass, AtLeast(kMinMass).Tip("押すときのキャラの重さ（kg）。押す速さ ＝ キャラの速さ × Push Mass ÷ (Push Mass ＋ 相手の Mass)。重い物ほどゆっくり動く"));
	ui.EndDisabled();
	ui.Separator("Constraints");
	ui.Field("Freeze Position X###freezePositionX", value.freezePositionX);
	ui.Field("Freeze Position Y###freezePositionY", value.freezePositionY);
	ui.Field("Freeze Position Z###freezePositionZ", value.freezePositionZ, Tip("横から見るゲームなら止める"));
	ui.Separator("Info");
	ui.Field("Velocity###velocity", value.velocity,
	         Tip("今の速さ。ゲームは X・Z を書く（跳ぶなら Y に上向きの速さも）。Y は空中で重力が足され、地面にいる間は 0。壁に当たると壁へ向かう分が消える。停止中に入れた値は、Play を押したときの速さになる"));
	ui.Label(value.IsGrounded() ? "Is Grounded: 地面に乗っている" : "Is Grounded: 空中");
	const bool hasHit = value.hitBelow || value.hitSides || value.hitAbove;
	const std::string hits = std::format("Hit: {}{}{}{}", value.hitBelow ? "下 " : "", value.hitSides ? "横 " : "", value.hitAbove ? "上 " : "", hasHit ? "" : "なし");
	ui.Label(hits.c_str());
}

// 足元（形の一番下の点）と、形の太さ（ギズモの輪に使う）
void CalcFoot(const WorldCollider& shape, const Vector3& up, Vector3* foot, float* radius) {
	if (shape.shape == ColliderShape::Sphere) {
		*foot = shape.sphere.center - up * shape.sphere.radius;
		*radius = shape.sphere.radius;
		return;
	}
	// カプセル：線分の端のうち、下にある方の球の一番下
	const Capsule& capsule = shape.capsule;
	const Vector3& lower = (Dot(capsule.start, up) < Dot(capsule.end, up)) ? capsule.start : capsule.end;
	*foot = lower - up * capsule.radius;
	*radius = capsule.radius;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void CharacterControllerSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<ControllerSystemState>();
	// 線は距離でフェードさせない
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;

	// Character Controller の置き場所・保存・Inspector を登録する。見せ方は DescribeCharacterController 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<CharacterControllerComponent>();
	ComponentSerializer::Register<CharacterControllerComponent>("Character Controller", &DescribeCharacterController);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CharacterControllerComponent>>("Character Controller", "Physics", &DescribeCharacterController));
	// 速さ・地面にいるかを、動き方の窓口から聞けるようにする（当たりの知らせ・カメラの追いかけ・Trigger が使う）
	MotionQuery::Register(&GetControllerMotion);
}

void CharacterControllerSystem::Release() { state.reset(); }


//=============================================================================
// 毎フレーム（再生中だけ）
//=============================================================================
bool CharacterControllerSystem::Update(float deltaTime) {
	MY_ASSERT_MSG(state != nullptr, "CharacterControllerSystem::Initialize()を先に呼んでください");
	if (deltaTime <= 0.0f) {
		return false; // 一時停止・ヒットストップ中（速さも重力も変えない）
	}
	const Vector3 gravity = PhysicsSystem::GetGravity();
	const Vector3 up = PhysicsSystem::GetUp();
	bool isMoved = false;
	EntityManager::ForEach<CharacterControllerComponent>([&](Handle<Entity> entity, CharacterControllerComponent& controller) {
		if (MoveController(entity, controller, gravity, up, deltaTime)) {
			isMoved = true;
		}
	});
	return isMoved;
}


//=============================================================================
// ギズモ
//=============================================================================
void CharacterControllerSystem::DrawGizmos(Camera* camera) {
	MY_ASSERT_MSG(state != nullptr, "CharacterControllerSystem::Initialize()を先に呼んでください");
	if (!CollisionSystem::GetShowGizmos()) {
		return; // Collider のギズモと一緒に出す・消す
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	// 輪を描く面：上と直交する2本の向き（上が X に近ければ Z を使って、外積が 0 にならないように）
	const Vector3 up = PhysicsSystem::GetUp();
	const Vector3 side = Normalize(Cross(up, (std::abs(up.x) < 0.9f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 0.0f, 1.0f}));
	const Vector3 front = Cross(side, up);
	EntityManager::ForEach<CharacterControllerComponent>([&](Handle<Entity> entity, const CharacterControllerComponent& controller) {
		WorldCollider shape;
		if (!controller.enabled || !EntityManager::IsActiveInHierarchy(entity) || !MakeShape(entity, shape)) {
			return;
		}
		// --- 上れる段の高さ：足元から Step Offset の高さに、形と同じ太さの輪 ---
		Vector3 foot;
		float radius = 0.0f;
		CalcFoot(shape, up, &foot, &radius);
		GizmoLines::PushCircle(gizmo.lines, foot + up * controller.stepOffset, radius, side, front, kStepColor);
		// --- 地面の向き：地面に触れている所から、面の向きへ（地面にいる間だけ）---
		if (controller.IsGrounded()) {
			gizmo.lines.push_back({controller.groundPoint, controller.groundPoint + controller.groundNormal * kGroundLineLength, kGroundColor});
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}
```

### C-4 `MyEngine_v1.vcxproj`

C-1 を `MyEngine/Components/Physics`、C-2・C-3 を `MyEngine/Systems/Physics` に「既存の項目」で足す。

```xml
    <ClCompile Include="MyEngine\Systems\Physics\CharacterControllerSystem.cpp" />
    <ClInclude Include="MyEngine\Components\Physics\CharacterControllerComponent.h" />
    <ClInclude Include="MyEngine\Systems\Physics\CharacterControllerSystem.h" />
```

### C-5 `Systems/EngineSystems.cpp`

**① include**

```cpp
// Physics
#include "MyEngine/Systems/Physics/CharacterControllerSystem.h"
#include "MyEngine/Systems/Physics/CollisionSystem.h"
```

**② `Initialize`（`PhysicsSystem` の下）**

```cpp
	PhysicsSystem::Initialize();          // RigidBody
	CharacterControllerSystem::Initialize(); // Character Controller
```

**③ `Release`（`PhysicsSystem` の下）**

```cpp
	PhysicsSystem::Release();
	CharacterControllerSystem::Release();
```

**④ `Update`：`WindZoneSystem::Update` の下、Rigidbody の前**

```cpp
	WindZoneSystem::Update(isGameRunning, Time::GetDeltaTime()); // 風（Wind Zone）の範囲を集め直して、再生中なら Rigidbody を押す（押した速さで、すぐ後の物理が動かす）。画面の風の線の値も書く

	// キャラ（Character Controller）を動かす。動かす前に、当たりの形を今の位置で集め直す（Script・アニメ・Timeline がこのフレームに動かした物を、動いた後の位置で見る）
	// 物理より前（Unity・Unreal と同じ。Rigidbody から見ると、キャラは動いた後の位置にある壁）。動かしたら、ワールド行列を計算し直す
	if (isGameRunning) {
		CollisionSystem::SyncWorld();
		if (CharacterControllerSystem::Update(Time::GetDeltaTime())) {
			EntityManager::UpdateTransforms();
		}
	}

	// Rigid body を動かして、ぶつかった物から押し戻す
```

**⑤ `Draw`：Collider のギズモの下**

```cpp
	CollisionSystem::DrawGizmos(gameCamera);
	CharacterControllerSystem::DrawGizmos(gameCamera); // 上れる段の高さ・地面の向き（Collider のギズモと一緒に出す・消す）
```

### C-6 コメント

**① `Components/Physics/ColliderComponent.h`**

```cpp
// 押し戻す（ぶつかったら止まる）のは、Rigidbody（PhysicsSystem）と Character Controller（CharacterControllerSystem）を付けた物だけ。Is Trigger の Collider は押し戻さない（すり抜ける）
```

```cpp
	bool isTrigger = false;                   // true：すり抜ける（Rigidbody・Character Controller を押し戻さない。Script には OnTrigger〜）。false：ぶつかる（Script には OnCollision〜）
```

**② `ECS/Script.h` の `CollisionInfo::selfVelocity`**

```cpp
	Vector3 selfVelocity = {0.0f, 0.0f, 0.0f};  // 自分の、ぶつかる直前の速さ（物理でぶつかったとき。押し合わない当たりは今の速さ。動き方の Component（Rigidbody・Character Controller）が無ければ0）
```

**③ `ECS/GameComponent.h` の使い方の説明（Rigidbody の行の下に1行）**

```cpp
//   ぶつかったら止まる物は Rigidbody（Add Component → Physics）。③の中で EntityManager::Get<RigidbodyComponent>(entity)->velocity を書いて動かす
//   歩くキャラは Character Controller（坂で滑らない・段を上る）。同じく velocity を書いて動かす
```

ここでビルドすると、Add Component → Physics に **Character Controller** が出る。

## パートD Sandbox のテスト

### D-1 `Sandbox/Source/Scripts/CharacterKeyboardMover.h`（新しい）

```cpp
#pragma once
#include "MyEngine/ECS/Script.h"

/// <summary>
/// Character Controller をキーボードで動かす（Sandbox のテスト用）
/// <para>W・A・S・D：ワールドの +Z・−X・−Z・+X へ歩く / Space：跳ぶ / Left Shift：ダッシュ（Dash Speed で Dash Time 秒、歩いていた向きへ）</para>
/// <para>同じ Entity に Collider（Capsule）と Character Controller を付ける。当たりの知らせ（OnCollisionEnter・Exit）を Console に出す</para>
/// </summary>
class CharacterKeyboardMover : public Script {
public:
	void Update() override;
	void OnCollisionEnter(const CollisionInfo& collision) override;
	void OnCollisionExit(const CollisionInfo& collision) override;
	void Fields(ComponentUI& ui) override;

private:
	float moveSpeed_ = 6.0f;                     // 歩く速さ（m/s）
	float jumpSpeed_ = 6.0f;                     // 跳んだ瞬間の上向きの速さ（m/s）
	float dashSpeed_ = 18.0f;                    // ダッシュの速さ（m/s）
	float dashTime_ = 0.18f;                     // ダッシュの長さ（秒）
	float dashRemaining_ = 0.0f;                 // ダッシュの残り時間（秒。0 ならダッシュしていない）
	Vector3 facing_ = {0.0f, 0.0f, 1.0f};        // 最後に歩いた向き（ダッシュの向き。長さ1）
};
```

### D-2 `Sandbox/Source/Scripts/CharacterKeyboardMover.cpp`（新しい）

```cpp
#include "Source/Scripts/CharacterKeyboardMover.h"

#include <format>

#include "MyEngine/Components/Physics/CharacterControllerComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Input/InputManager.h"

SCRIPT(CharacterKeyboardMover, "Test");


//=============================================================================
// 毎フレーム
//=============================================================================
void CharacterKeyboardMover::Update() {
	CharacterControllerComponent* controller = GetComponent<CharacterControllerComponent>();
	if (controller == nullptr) {
		return; // Character Controller を付けていない
	}

	// --- 歩く向き（キーを押している向きを足して、長さ1にする）---
	Vector3 input = {0.0f, 0.0f, 0.0f};
	if (InputManager::IsKeyPressed(DIK_W)) {
		input.z += 1.0f;
	}
	if (InputManager::IsKeyPressed(DIK_S)) {
		input.z -= 1.0f;
	}
	if (InputManager::IsKeyPressed(DIK_D)) {
		input.x += 1.0f;
	}
	if (InputManager::IsKeyPressed(DIK_A)) {
		input.x -= 1.0f;
	}
	if (LengthSq(input) > 0.0f) {
		input = Normalize(input);
		facing_ = input;
	}

	// --- ダッシュ（押した瞬間から Dash Time 秒、向きは押した瞬間の向き）---
	if (InputManager::IsKeyTriggered(DIK_LSHIFT)) {
		dashRemaining_ = dashTime_;
	}
	Vector3 horizontal = input * moveSpeed_;
	if (dashRemaining_ > 0.0f) {
		horizontal = facing_ * dashSpeed_;
		dashRemaining_ -= Time::GetDeltaTime();
	}

	// --- 速さを書く。X・Z は毎フレーム書き、Y は Character Controller に任せる（跳ぶときだけ書く）---
	controller->velocity.x = horizontal.x;
	controller->velocity.z = horizontal.z;
	if (controller->IsGrounded() && InputManager::IsKeyTriggered(DIK_SPACE)) {
		controller->velocity.y = jumpSpeed_;
	}
}

//=============================================================================
// 当たりの知らせ（Rigidbody と同じように来るかを見る）
//=============================================================================
void CharacterKeyboardMover::OnCollisionEnter(const CollisionInfo& collision) {
	LogManager::Log(std::format("Enter {} 向き({:.2f}, {:.2f}, {:.2f}) 強さ {:.2f}", collision.gameObject.GetName(), collision.normal.x, collision.normal.y, collision.normal.z, collision.GetImpactSpeed()));
}

void CharacterKeyboardMover::OnCollisionExit(const CollisionInfo& collision) {
	LogManager::Log(std::format("Exit {}", collision.gameObject ? collision.gameObject.GetName() : std::string("(消えた)")));
}

//=============================================================================
// Inspector と保存
//=============================================================================
void CharacterKeyboardMover::Fields(ComponentUI& ui) {
	ui.Field("Move Speed###moveSpeed", moveSpeed_, AtLeast(0.0f).Tip("歩く速さ（m/s）"));
	ui.Field("Jump Speed###jumpSpeed", jumpSpeed_, AtLeast(0.0f).Tip("跳んだ瞬間の上向きの速さ（m/s）"));
	ui.Field("Dash Speed###dashSpeed", dashSpeed_, AtLeast(0.0f).Tip("ダッシュの速さ（m/s）。速く動いても壁をすり抜けないかを見る"));
	ui.Field("Dash Time###dashTime", dashTime_, AtLeast(0.0f).Tip("ダッシュの長さ（秒）"));
}
```

### D-3 `Sandbox/Sandbox.vcxproj`・`.filters`

Sandbox プロジェクトに `Source/Scripts` のフィルターを作り、D-1・D-2 を「既存の項目」で足す（`.vcxproj` には次が入る）。

```xml
  <ItemGroup>
    <ClCompile Include="main.cpp" />
    <ClCompile Include="Source\Scripts\CharacterKeyboardMover.cpp" />
  </ItemGroup>
  <ItemGroup>
    <ClInclude Include="Source\Scripts\CharacterKeyboardMover.h" />
  </ItemGroup>
```

### D-4 テストシーン

`project/Sandbox/Resources/scenes/CharacterControllerTest.scene.json`（手で書く物ではないので、ファイルごと置いた）。Sandbox の File → Open（か Project ウィンドウ）で開く。

| 置いてある物 | 場所（X・Z） | 確かめること |
|---|---|---|
| Player（黄色・Capsule 半径 0.4 高さ 1.8・Keyboard Mover） | (0, 0) | WASD・Space・Left Shift で動かす |
| Slope 30 → Plateau（台地 高さ 3.46）→ 崖 | X 2〜8 が坂・8〜18 が台地、Z −8 | 坂で止まる・上る・下りる・台地の端から落ちる |
| Slope 60 | X 4〜7、Z −16 | 上れない・乗ると滑り落ちる |
| Step 1〜3（0.2 ずつ）→ High Step（0.5 上がる） | X 3〜12、Z 0 | 3段上って、High Step で止まる |
| Thin Wall（厚さ 0.1） | X 8、Z 6〜10 | ダッシュですり抜けない |
| Long Wall | Z 13（X −1〜9） | 斜めに当たって沿って進む |
| Push Box（Rigidbody 20kg） | (4, 18) | 押せる |
| Other Character（水色） | (−4, 0) | 頭に乗れない（台から跳び乗る代わりに、ぶつかる・隣に立つ） |
| Main Camera（Camera Follow XZ） | | Player を追う |

## 2章 確かめる

### 2.1 Sandbox で確かめること

Sandbox を Development か Debug で起動し、`CharacterControllerTest` を開いて Play。

1. **床**：Player が少し落ちて止まる。Inspector の Character Controller の Info が「Is Grounded: 地面に乗っている」「Hit: 下」。Console に `Enter Ground` が1回だけ出る（止まっている間、Exit が出ない）
2. **30 度の坂**：坂の途中で手を離す → 止まったまま（滑らない）。上る・下りるで浮かない（Info が空中にならない）。ダッシュで下りても浮かない
3. **台地の端**：台地から +X へ出る → 落ちて着地。Console に `Enter Ground`
4. **段**：Z 0 の列を +X へ → 3段は上る、High Step で止まる。W・S を足すと、止まった所から横へ動ける
5. **薄い壁**：Thin Wall へ向かって Left Shift（ダッシュ 18m/秒）→ 手前で止まる（1フレームにもっと進む場合は、2.2 で確かめた）
6. **長い壁**：W と D を同時に押して Long Wall へ → 壁に沿って +X へ進む
7. **60 度の坂**：歩いて上れない。Space で坂の上へ跳ぶ代わりに、Inspector で Player の Transform の位置を坂の上（X 6・Y 5・Z −16）にして Play → 滑り落ちる
8. **ジャンプ**：Space で跳び、着地する。跳んでいる間は「空中」・Hit が空
9. **ほかのキャラ**：Other Character へ歩くと止まる（押せない）。Player の位置を Other Character の真上（X −4・Y 3・Z 0）にして Play → 頭から滑り落ちて隣に立つ
10. **押せる箱**：Push Box へ歩く → 押せる。Character Controller の Push Rigidbodies を外す → 止まるだけ
11. **ギズモ**：Scene ビューで、Player の足元から Step Offset の高さに黄色の輪、地面にいる間は足元から青い線（地面の向き。坂では傾く）
12. **Stop**：位置が置いた所に戻る。Inspector で Slope Limit・Step Offset を変えて保存 → 開き直しても残る
13. **Undo**：Character Controller の値を変えて Ctrl+Z

### 2.2 写しで確かめたこと

答えのコードを写しのエンジン（`W:\eng`、タグ `answer-cc`）に入れて確かめた。

- **ビルド**：Debug・Development・Release の3構成、警告 0（DirectXTex の前からある警告を除く）。途中で止めた形（パートA だけ・A＋B）も、エンジンを Debug でビルドできた
- **動き**：テスト用の Script（写しだけ。md には入れない）で Player を決まった手順で動かし、Release と Debug（エディタの Play を通る道）の両方で 31 項目が合格

| 確かめた物 | 結果 |
|---|---|
| 床に置く | 0.005 浮いて止まる。50 フレームで床との当たりの Exit 0 回。窓口と Component の「地面にいるか」が全フレーム一致。Hit は下だけ |
| 30 度の坂で止まる | 140 フレームでずれ 0.00003m（上下の揺れだけ）。高さは計算どおり（足元の球が坂から半径＋隙間） |
| 坂を上る（6m/秒）・下りる（18m/秒） | 台地の高さ 3.469 に乗る。上り・下りとも空中のフレーム 0 |
| 台地の端から出る | 50 フレーム空中で、床に着地 |
| 段 | 0.6 まで上り、0.5 上がる所で x = 8.595（壁の手前 半径＋隙間）で止まる |
| 薄い壁（時間 5 倍・18m/秒＝1フレーム約 1.5m） | 壁の手前 7.545 で止まる（壁は 7.95） |
| 斜めに壁へ | 壁の手前で止まり、沿って x が 1→7.9 |
| 60 度の坂 | 歩いて上れない（x 3.77 で止まる）。上に置くと床まで滑り落ちる（毎フレーム X・Z に 0 を書いても） |
| ジャンプ（6m/秒） | 高さ 1.79（式 v²÷2g は 1.84。1フレームずつ足すので少し低い）。着地する |
| ほかのキャラの頭の真上 | 滑り落ちて隣に立つ（中心の距離 0.80）。毎フレーム X・Z に 0 を書いても同じ。下のキャラは動かない |
| 箱を押す（3m/秒・20kg） | 箱が 4→7.8 へ動く |
| ヒットストップ | 止まっている間、動き 0 |

- **確かめていない**：本物のキーボードでの手触り・Scene ビューのギズモの見た目・Inspector の操作と Undo・Stop で戻ること（どれも 2.1 で見てほしい）。AL4_01 での動き

## 3章 今回やらないこと・この先

| 項目 | 今の動き | 足すなら |
|---|---|---|
| 動く床（Move With Ground） | 乗っても一緒に動かない | `groundEntity` の前のフレームのワールド行列を覚え、今の行列との差（動いた量・回った角度）だけ、Cast でキャラを動かす。床から跳んだときに床の速さを足すかも決める |
| 見下ろし（Camera Follow の XZ）で台地に上る | カメラの高さは変わらない（Ground Snap Y は XY のときだけ）。台地の上ではキャラがカメラに近づく | Camera Follow に「高さも追う」を足す（地面にいるかは MotionQuery で聞ける） |
| Z の向きに上る坂 | 作れない（Polygon は X・Y の形を Z へ押し出した形で、Z の回転だけ効く。Box は回らない） | 地形の形（Heightfield）か、回る箱（OBB）を形の層に足す。CalcPushOut を書けば Cast も使える＝Character Controller はそのまま歩ける |
| 風（Wind Zone）・弾む面（Elastic Surface） | キャラには効かない | 効かせるなら、Character Controller の速さに足す（Wind Scale のような項目） |
| Rigidbody が動くキャラに当たったとき | 押し戻しは止まった壁と同じ（キャラの速さで跳ね返らない） | PhysicsSystem の壁の押し戻しで、壁の速さ（MotionQuery）を引いた「近づく速さ」を使う（Is Kinematic の床も同じになる） |
| 調べる数 | Cast は全部の Collider を、動いた跡を囲む箱で外してから調べる（数百個まで） | 多くなったら、形の層に空間の分け方（グリッド・BVH）を足す。呼ぶ側は変わらない |
| キャラどうしの押し合い | お互いに壁 | 要るなら Script で |
| AL4_01 の PlayerMove.md | 重力と地面を Script で計算する形のまま（保留中） | Character Controller を使う形に書き直す（velocity の X・Z を書く・跳ぶ・ダッシュ） |

見つけた物（今回は直さない）：Collider の Capsule の高さの保存の名前が `"height "`（最後に空白）になっている（`CollisionSystem.cpp` の `"Height###height "`）。直すと前に保存したシーンの高さが読めなくなるので、直すなら `"Height###height"` にして、読むときに `"height "` も見る形にする。テストシーンは今の名前（空白あり）で書いてある。
