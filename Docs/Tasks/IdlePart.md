参考：
- Unity の Mathf.DeltaAngle（一番近い回り方の角度の差）：https://docs.unity3d.com/ScriptReference/Mathf.DeltaAngle.html
- `std::remainder`（一番近い整数倍を引いた余り）：https://en.cppreference.com/w/cpp/numeric/math/remainder
- `std::atan2`：https://en.cppreference.com/w/cpp/numeric/math/atan2
- フレームの速さに左右されない Lerp（1 − e^(−λ dt)）（Rory Driscoll, Frame Rate Independent Damping using Lerp）：https://www.rorydriscoll.com/2016/03/07/frame-rate-independent-damping-using-lerp/
- Unity の Animator.Play（番号・名前でアニメを流す）：https://docs.unity3d.com/ScriptReference/Animator.Play.html
- 前の攻撃表の md（状態マシン・攻撃部品・段階）：Docs/Tasks/AttackTable.md

# 待ちの部品・アニメを番号で流す・角度の計算（IdlePart）

3つをエンジンに足す。どれも見下ろしのボス戦で要るが、特定のゲームを前提にしない形にする。

- **パートA 数学**（MathUtility）：角度の差 `DeltaAngle`、なめらかに近づける割合 `CalcSmoothRate`（CameraFollow から移す）、Y のまわりの角度と向きを行き来する `DirectionToYaw`・`YawToDirection`
- **パートB アニメを番号で流す**（AnimationSystem）：`Play(entity, clipIndex, loop, speed)`・`Restart(entity)`（今の `Play(entity)` の言い換え）・`CalcSpeedToFit(entity, clipIndex, seconds)`
- **パートC 待ちの部品**（攻撃表）：フェーズごとに **Idle Part**（待ちの部品）と **Min Idle Seconds**（攻撃の後の最短の待ち）。Selecting の間は待ちの部品を動かし、攻撃を選んだら、攻撃の OnBegin より先に待ちの部品の OnEnd
- **パートD Sandbox**：相手を追う待ちの部品（ChaseTarget）と、予備動作でアニメを流しきる攻撃（SpinAttack）、テストシーン IdlePartTest

パートは **A → B → C → D の順に写す**。A だけ・B までのどこでもビルドできる。**C は名前を変える所があるので、C を全部写してから1回ビルドする**（途中ではビルドが通らない）。D は C を使う。

ブランチは `feature/IdlePart`（develop aa8eb33 から）。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドで自分のファイルの警告 0、自動テスト 67 項目（数学 16・アニメ 20・攻撃表 31）と、テストシーンを再生して見る 7 項目が合格。Inspector の Idle Part の欄と Attack Table ウィンドウの表示も撮って確かめた。

## 0章 全体の形

### 0.1 何が困っていたか

- **攻撃と攻撃の間が 0 秒**：Selecting で使える攻撃があると、そのフレームのうちに次を選ぶ。使える攻撃が無いと、何もせずに待つ
- **待つ間の動きが Script に分かれる**：「Script で GetStatus を見て近づける」形だと、AI が攻撃表と Script の2か所に分かれる。しかも Script は攻撃表より先に回るので、**攻撃が始まったフレームに、Script が書いた歩きの速さが残る**
- **アニメの切り替えがゲームの private の関数**：Animator の clipIndex・loop・speed を直接書き、「番号が違うときだけ切り替える」「長さ ÷ 流したい秒」をプレイヤー（とボス）がそれぞれ持っている
- **角度の計算がばらばら**：`std::remainder(…, 360)`・`atan2(x, z)`・`1 − e^(−…)` を、プレイヤーと攻撃部品がそれぞれ書くことになる。なめらかに近づける割合は CameraFollow にもう同じ式がある

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Math/MathUtility.h` | `CalcSmoothRate`・`DeltaAngle`・`DirectionToYaw`・`YawToDirection` を足す |
| A | `Systems/Camera/CameraFollow.h`・`.cpp`・`CameraSystem.cpp` | `CameraFollow::CalcSmoothRate` を消して、`MathUtility::CalcSmoothRate` を呼ぶ |
| B | `Systems/Animation/AnimationSystem.h`・`.cpp` | `Play(entity, clipIndex, loop, speed)`・`Restart`・`CalcSpeedToFit`。流し始めたフレームは時刻を進めない |
| C | `AI/AttackTableManager.h`・`.cpp` | `AttackPartSlot`（部品の名前と値）。フェーズに `minIdleSeconds`・`idlePart`。ファイルの読み書き |
| C | `AI/AttackPart.h` | `AttackPartContext::state`（攻撃か待ちか）・説明 |
| C | `Components/Gameplay/AttackTableComponent.h` | Selecting の説明だけ |
| C | `Systems/Gameplay/AttackTableSystem.h`・`.cpp` | 待ちの部品を作る・呼ぶ・止める。Min Idle Seconds |
| C | `Editor/Inspector/AttackTableEditor.cpp` | 部品の欄を `AttackPartSlot` で書き直し、フェーズに Idle Part の欄 |
| C | `Editor/Windows/AttackTableWindow.cpp` | 待ちの部品の名前と、待っている理由を出す |
| D | `Sandbox/Source/AttackParts/ChaseTarget.cpp`・`SpinAttack.cpp`（新しい） | テスト用の待ちの部品と攻撃部品 |
| D | `Sandbox/Sandbox.vcxproj`・`.filters` | 上の2つを足す |
| D | `Sandbox/Resources/AttackTables/IdlePartTest.attacktable`・`scenes/IdlePartTest.scene.json`（新しい） | テストの表とシーン |

### 0.3 使い方

**待ちの部品**は、攻撃部品と同じクラス（`AttackPart`）・同じ `ATTACK_PART` で書いて、攻撃表のフェーズの **Idle Part** に選ぶ。

```cpp
// Chase.cpp（ゲーム側）
class Chase : public AttackPart {
public:
	void OnUpdate(const AttackPartContext& context) override; // 相手の方を向く・遠ければ近づく・近ければ止まる
	void OnEnd(const AttackPartContext& context, AttackPartEnd reason) override; // 書いた速さを 0 に戻す
	void Fields(ComponentUI& ui) override;
};
ATTACK_PART(Chase, "相手の方を向き、遠ければ近づく");
```

- `OnEnd` は、攻撃を選んだときは**その攻撃の OnBegin より先に**呼ばれる。ここで velocity の X・Z を 0 にすれば、攻撃が始まったフレームに歩きの速さが残らない
- 同じ部品を攻撃の Part にも選べる。どちらで呼ばれたかは `context.state`（攻撃は `Attacking`、待ちは `Selecting`）
- `context.stageTime` は Selecting に入ってからの秒、`context.stageDuration` はそのフェーズの Min Idle Seconds

**アニメ**は番号で流す。毎フレーム呼んでよい。

```cpp
AnimationSystem::Play(self, kClipRun, true, 1.0f);                // 同じ番号が流れていれば、速さと loop だけ変わる
const float speed = AnimationSystem::CalcSpeedToFit(self, kClipSwing, context.stageDuration);
AnimationSystem::Play(self, kClipSwing, false, speed);            // 予備動作の長さで、ちょうど1回流しきる
AnimationSystem::Restart(self);                                    // 流れている途中でも、最初から流し直す
```

**向き**は角度の差となめらかに近づける割合で回す。

```cpp
const float targetYaw = MathUtility::DirectionToYaw(toTarget); // +Z で 0、+X で 90
yaw += MathUtility::DeltaAngle(yaw, targetYaw) * MathUtility::CalcSmoothRate(deltaTime, turnSmoothTime);
const Vector3 forward = MathUtility::YawToDirection(yaw);       // 地面に平らな長さ 1 の向き
```

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 待ちの部品のクラス | **攻撃部品と同じ `AttackPart`・同じ一覧**から選ぶ。`AttackPartContext` に `state` を足して見分ける | Chase を「数秒近づく攻撃」にも使える。登録の仕組みを2つ持たない。今の `BeginAttack` は `stage = None` で OnBegin を呼ぶので、`stage` だけでは待ちと攻撃を見分けられない |
| Min Idle Seconds が効く時 | **Selecting に入るたび**（攻撃の後・怯みの後・フェーズの移行の後・再生の始め） | 「攻撃の後か」を覚える値が要らない。`stageTime`・GetStatus の `duration` が、いつも「Selecting に入ってからの秒・最短の待ち」という同じ意味になる |
| RequestAttack | Min Idle Seconds を**待たない** | 演出の後に、すぐ大技を出すため |
| 部品の名前と値 | **`AttackPartSlot { name, fields }`** にまとめ、攻撃の行とフェーズの両方で使う。Inspector の欄・ファイルの読み書き・値の読み直しもこれ1つを通す | 攻撃の行だけの形のままだと、同じ処理を写した物が2つになる。ファイルの攻撃の行のキー（`part`・`partFields`）は変えない（今の .attacktable がそのまま読める） |
| 待ちの部品の OnBegin を呼ぶ時 | Selecting に入った所ではなく、**Selecting の最初の（時間が進む）フレーム**の頭 | 再生の始めは、AI が止められている・時間が 0 のフレームでも表を作る。そこで呼ぶと「時間が進んでいるときだけ呼ぶ」決まりが崩れる（1.4） |
| 待ちの部品の実体 | 敵1体の**フェーズごとに1つ**作る | 攻撃の行と同じ（メンバ変数に途中の値を持てる）。フェーズで値を変えられる |
| 同じ番号のアニメが止まっている時の Play | **最初から流す** | ループしないアニメを2回続けて出しても、2回目が流れる。流れている途中で最初からにしたいときだけ `Restart` |
| アニメが効くフレーム | **呼んだフレームから**。流し始めたフレームは**時刻を進めず、0 秒の形**を見せる | Script も攻撃部品も AnimationSystem::Update より先に回る。進めると最初の形が1フレームも見えず、攻撃部品の `stageTime` と1フレームずれる（1.6） |
| なめらかに近づける割合 | **MathUtility に1つ**（`CalcSmoothRate(deltaTime, smoothTime)`）。引数は**秒**。CameraFollow の物は消す | CameraFollow にもう同じ式があり、カメラの値（Smooth Time）が秒で保存されている。0 なら「すぐ合わせる」がそのまま書ける |
| 角度の単位 | **度**（エンジンの角度は全部度） | SetEulerAngles・GetEulerAngles と同じ |

### 0.5 待ちの部品の呼ばれ方

`D` は Min Idle Seconds。どれも時間が進んでいるときだけ呼ぶ（ポーズ・時間停止・ヒットストップ・AI 停止の間は呼ばない）。

| 場面 | 待ちの部品 | 攻撃の部品 |
|---|---|---|
| 再生の始め・攻撃の後・怯みの後・フェーズの移行の後 | 次に時間が進むフレームの頭に **OnBegin**（stageTime 0）、同じフレームから **OnUpdate** | － |
| Selecting の間 | 毎フレーム **OnUpdate**（stageTime は Selecting に入ってからの秒） | － |
| stageTime が D 以上で、使える攻撃があった | そのフレームの OnUpdate の後に **OnEnd(Finished)** | その後に **OnBegin** → **OnStage(Windup)** |
| RequestAttack があった | 次の Selecting のフレームで OnUpdate の後に **OnEnd(Finished)**（D を待たない） | その後に OnBegin |
| 使える攻撃が無い | D を過ぎても OnUpdate を続ける（stageTime は D を超えて増える） | － |
| Selecting の途中で怯み・フェーズの移行・撃破 | **OnEnd(Interrupted)**（フェーズの移行は、移る前のフェーズの部品） | － |
| Selecting の途中で表を作り直す（表を替えた・行やフェーズの数が変わった） | **OnEnd(Interrupted)**。そのフレームのうちに新しい表で OnBegin・OnUpdate | － |
| Selecting の途中で Stop・Entity が消える | **OnEnd(Interrupted)** | － |
| 怯み・フェーズの移行・撃破の間 | 呼ばない | 呼ばない |
| AI を止めた（Enabled を外した）・Entity が無効 | 呼ばない。戻したら続きから（OnBegin はもう一度呼ばない） | 同じ |
| OnBegin を呼ぶ前に止まった（始めから AI 停止など） | OnEnd も呼ばない | － |
| Idle Part が空・D が 0 | 何も呼ばない・Selecting の最初のフレームに選ぶ（**今までと同じ**） | － |

### 0.6 1フレームの順番と、アニメが効くフレーム

```
シーンの Update → Script の Update → SYSTEM
→ AttackTableSystem::Update（攻撃部品・待ちの部品）
→ AnimationSystem::Update（アニメの時刻を進めて、形を作る）
→ UIState・Timeline → ワールド行列 → CharacterController → 物理 → 描画
```

- Script の Update と攻撃部品（待ちの部品も）から `AnimationSystem::Play`・`Restart` を呼ぶと、**そのフレームの AnimationSystem::Update から効く**。流し始めたフレームは時刻を進めないので、そのフレームに描くのはアニメの **0 秒の形**、次のフレームから進む
- Play で同じ番号が流れているとき（速さ・loop だけ変える）は、そのフレームの Update から新しい速さで進む
- Timeline の Event など、AnimationSystem::Update より後から呼ぶと、**次のフレーム**から効く（流し始めの扱いは同じで、次のフレームが 0 秒の形）
- 待ちの部品が書いた Character Controller の velocity は、同じフレームの CharacterController で使われる。OnEnd で 0 にすれば、攻撃が始まったフレームに動かない

### 0.7 今回やらないこと

3章にまとめた（アニメの名前で流す・切り替えの混ぜ合わせ・待ちの部品を Selecting 以外で使う など）。

## 1章 解説

### 1.1 なめらかに近づける割合（1 − e^(−dt / smoothTime)）

毎フレーム「残りの差 × t」だけ近づける書き方（`value = Lerp(value, target, t)`）は、t を決まった値（0.2 など）にすると、**フレームが多いほど速く近づく**。60fps なら1秒に60回、144fps なら144回 0.2 ずつ詰めるので。

1秒たった後の残りの差は、t を固定すると (1 − t)^(フレームの数) になる。これをフレームの速さに関係なく同じにしたい。残りの差が「時間に対して指数で減る」と決めると、dt 秒で残りが e^(−dt / smoothTime) 倍になるので

- t ＝ 1 − e^(−dt / smoothTime)

にすればよい。60fps の 1/60 秒 × 60 回でも、30fps の 1/30 秒 × 30 回でも、残りは e^(−1 / smoothTime) で同じになる（写しの自動テストで確かめた）。

- smoothTime 秒たつと、残りの差は約 37%（e^(−1)）
- smoothTime が 0 なら t ＝ 1（すぐ合わせる）。0 で割らないように、関数の中で分ける
- 「1秒に何倍の速さで詰めるか」（sharpness）で書く人もいる。sharpness ＝ 1 / smoothTime なので、`CalcSmoothRate(dt, 1.0f / sharpness)` で同じ値になる

### 1.2 DeltaAngle と std::remainder（あまり使わない STL）

`std::remainder(a, b)`（`<cmath>`）は、**a から「b の一番近い整数倍」を引いた余り**を返す。

- `std::remainder(350, 360)` → 350 に一番近い 360 の整数倍は 360 → 350 − 360 ＝ **−10**
- `std::remainder(170, 360)` → 一番近いのは 0 → **170**
- `std::fmod(350, 360)` は **350**（fmod は 0 の方へ切り捨てた割り算の余りなので、a と同じ符号の側に収まる）

なので `std::remainder(to − from, 360)` は −180〜180 に収まる＝**一番近い回り方の差**になる（Unity の Mathf.DeltaAngle と同じ）。

- 350 度から 10 度へは **+20 度**（−340 度ではない）
- ちょうど半周（180 度・540 度）のときは、割り算の答えが x.5 になり、偶数の方へ丸める（180 → +180、540 → −180）。どちら回りでも同じ長さなので困らない

### 1.3 atan2(x, z) と (sin, 0, cos)

エンジンの Y のまわりの角度（SetEulerAngles の Y）は、**+Z を向いて 0 度、+X を向いて 90 度**。

- 向き (x, z) から角度：`std::atan2(x, z)`。ふつうの `atan2(y, x)` と引数の並びが違うのは、「0 度の向き」が +Z だから（+Z を数学の x 軸、+X を数学の y 軸とみなしている）
- 角度から向き：`(sin θ, 0, cos θ)`。θ ＝ 0 で (0, 0, 1)＝+Z、θ ＝ 90 度で (1, 0, 0)＝+X
- `DirectionToYaw` は向きの Y を見ない（地面に平らにしてから測るのと同じ）。X と Z が両方 0 のときは atan2(0, 0) ＝ 0

### 1.4 待ちの部品の OnBegin を「入った所」で呼ばない理由

Selecting に入る所は5つある：再生の始め（Build）・攻撃の後・怯みの後・フェーズの移行の後・表の作り直し。

このうち **Build は、AI が止められている（Enabled を外した）・時間が 0・Entity が無効のフレームでも走る**（表を作るのは早い方がよいので、止まっているかを見る前に作る）。入った所で OnBegin を呼ぶと、止まった時間の中で部品が動く。

そこで、**Selecting に入ったら「まだ始めていない」印（`isIdleBegun = false`）だけ付け、OnBegin は UpdateSelecting の頭で呼ぶ**。UpdateSelecting は時間が進んでいるときしか来ないので、決まりが崩れない。OnEnd も、この印が立っているときだけ呼ぶ（OnBegin を呼んでいない部品に OnEnd を送らない）。

印は **OnEnd を呼ぶ前に下ろす**。OnEnd の中で Stagger を呼ばれても、OnEnd を2回送らないため。

### 1.5 AttackPartSlot にまとめた理由

今のコードは、部品の名前（`part`）と値（`partFields`）を攻撃の行（`AttackData`）が直接持ち、Inspector の欄（`DrawPartCombo`・`DrawPartFields`）・ファイルの読み書き・値の読み直しが、どれも `AttackData&` を受け取る。フェーズにも部品を持たせると、これを全部写した物がもう1つ要る。

`AttackPartSlot { name, fields }` を作って、受け取る物をこれにすれば、攻撃の行もフェーズも同じ関数を通る。ファイルのキーだけは場所ごとに違う（攻撃の行は `part`・`partFields`、フェーズは `idlePart`・`idlePartFields`）ので、`PartKeys` で渡す。

### 1.6 流し始めたフレームを進めない理由

攻撃部品の OnStage(Windup) で `Play(…, CalcSpeedToFit(…, windupSeconds))` を呼ぶと：

| フレーム | 攻撃表の stageTime | 進める（今まで） | 進めない（今回） |
|---|---|---|---|
| N（OnStage） | 0 | dt × speed | **0** |
| N＋1 | dt | 2dt × speed | dt × speed |
| N＋k | k × dt | (k＋1) dt × speed | k × dt × speed |

進めると、アニメが攻撃表より1フレーム先に行き、0 秒の形が1度も描かれない。予備動作の長さで流しきるはずのアニメが、1フレーム早く終わる。進めなければ、アニメの時刻と stageTime が同じになる。

Play On Start・Inspector で Clip を選び直したときも同じ扱い（流し始めはどれも同じ関数 `ResetPlayer` を通る）。

## パートA 数学（MathUtility）

### A-1 `Math/MathUtility.h`

`Lerp`（Vector4）の後・`MoveTowards` の前に `CalcSmoothRate` を足す。

```cpp
/// なめらかに近づけるときの、1フレームに進む割合（残りの差 × これ だけ進む）。smoothTime が 0 以下なら 1（すぐ合わせる）
/// 1 - e^(-deltaTime / smoothTime)。毎フレーム決まった割合（0.2 など）にすると、フレームが多いほど速く近づいてしまう。これならフレームの速さに左右されない
/// smoothTime 秒たつと、残りの差が約 37%（e の -1 乗）になる。使い方：value = Lerp(value, target, CalcSmoothRate(deltaTime, smoothTime))
inline float CalcSmoothRate(float deltaTime, float smoothTime) { return (smoothTime > 0.0f) ? 1.0f - std::exp(-deltaTime / smoothTime) : 1.0f; }
```

`MoveTowards` の後（namespace の閉じ括弧の前）に、角度の3つを足す。

```cpp
// ===== 角度（度） =====
/// from から to へ、一番近い回り方で回るときの差（度。-180〜180）。350 度から 10 度へは +20 度（-340 度ではない）
/// std::remainder(a, b) は「a から、b の一番近い整数倍を引いた余り」なので -b/2〜b/2 に収まる（std::fmod は a と同じ符号の側に収まる）
/// 向きをなめらかに回すとき：yaw += DeltaAngle(yaw, targetYaw) * CalcSmoothRate(deltaTime, smoothTime)
inline float DeltaAngle(float from, float to) { return std::remainder(to - from, 360.0f); }

/// 地面に平らな向きから、Y のまわりの角度（度）。+Z で 0、+X で 90（SetEulerAngles の Y と同じ測り方）
/// 向きの Y は使わない。X と Z が両方 0 なら 0
inline float DirectionToYaw(const Vector3& direction) { return std::atan2(direction.x, direction.z) * kRadToDeg; }

/// Y のまわりの角度（度）から、地面に平らな長さ 1 の向き（DirectionToYaw の逆）
inline Vector3 YawToDirection(float yaw) {
	const float radian = yaw * kDegToRad;
	return {std::sin(radian), 0.0f, std::cos(radian)};
}
```

### A-2 `Systems/Camera/CameraFollow.h`

`CalcSmoothRate` の宣言（とその上のコメント）を消す。

```cpp
// 消す
// なめらかに近づけるときの、1フレームに進む割合（残りの差 × これ だけ進む）。smoothTime が 0 なら 1（すぐ合わせる）
float CalcSmoothRate(float deltaTime, float smoothTime);
```

### A-3 `Systems/Camera/CameraFollow.cpp`

「追いかけ」の大見出しのすぐ下の `CameraFollow::CalcSmoothRate` の定義（1行）を消す。

```cpp
// 消す
float CameraFollow::CalcSmoothRate(float deltaTime, float smoothTime) { return (smoothTime > 0.0f) ? 1.0f - std::exp(-deltaTime / smoothTime) : 1.0f; }
```

`CameraFollow::Update` の中の3か所の `CalcSmoothRate(` を `MathUtility::CalcSmoothRate(` にする（MathUtility.h はもうインクルードしてある）。

```cpp
		follow.currentLookAhead += (wanted - follow.currentLookAhead) * MathUtility::CalcSmoothRate(deltaTime, follow.lookAheadSmoothTime);
```

```cpp
		center += (wanted - center) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime);
```

```cpp
		move += forward * ((depth - follow.viewDepth) * MathUtility::CalcSmoothRate(deltaTime, follow.smoothTime));
```

### A-4 `Systems/Camera/CameraSystem.cpp`

Look At の `CameraFollow::CalcSmoothRate(` を `MathUtility::CalcSmoothRate(` にする。

```cpp
	lookAt.currentPoint += (*point - lookAt.currentPoint) * MathUtility::CalcSmoothRate(deltaTime, lookAt.smoothTime);
```

ここでビルドできる。

## パートB アニメを番号で流す（AnimationSystem）

### B-1 `Systems/Animation/AnimationSystem.h`

「ゲームから使う」の中見出しから `Play` の宣言までを、次で置き換える（`Stop` から下はそのまま）。

```cpp
	// ===== ゲームから使う =====
	// Script・攻撃部品から呼ぶ。どれも、呼んだフレームの Update（Script・攻撃表の AI より後に回る）から効く
	// 流し始めたフレームは時刻を進めないので、そのフレームはアニメの 0 秒の形が見える

	/// <summary>
	/// アニメーションを番号で流す（毎フレーム呼んでよい）。Animator の Clip・Loop・Speed をこの値に書き換える
	/// <para>番号が違う・止まっている（ループしないアニメが最後まで行った・Stop した）→ そのアニメを最初から</para>
	/// <para>同じ番号が流れている → 速さと loop だけ変える（最初には戻らない。途中で最初からにしたいときは Restart）</para>
	/// </summary>
	/// <param name="clipIndex">ファイルの中の何番目のアニメーションか（Animator の Clip と同じ番号）</param>
	/// <param name="loop">ループするか</param>
	/// <param name="speed">1 で等速（CalcSpeedToFit で「N 秒で流しきる」速さを作れる）</param>
	/// <returns>流れているか（Animator が無い・その番号のアニメが無い・Entity が無効なら false）</returns>
	static bool Play(Handle<Entity> entity, int clipIndex, bool loop, float speed = 1.0f);
	// 今のアニメーション（Animator の Clip）を最初から流し直す。Animator が無い・アニメーションが未選択なら false
	static bool Restart(Handle<Entity> entity);
	// そのアニメーションを、ちょうど seconds 秒で流しきる速さ（長さ ÷ seconds）。Animator・アニメが無い・seconds が 0 以下なら 1
	static float CalcSpeedToFit(Handle<Entity> entity, int clipIndex, float seconds);
```

### B-2 `Systems/Animation/AnimationSystem.cpp`

**AnimatorState** に1行足す。

```cpp
struct AnimatorState {
	Handle<Entity> entity;        // 持ち主
	uint32_t animationHandle = 0; // 再生しているアニメーションの番号
	int clipIndex = 0;            // 再生しているのはファイルの中の何番目か
	AnimationPlayer player;       // 今の時刻・ノードの行列
	bool isJustStarted = false;   // 流し始めたばかり（次の Update は時刻を進めず、0 秒の形を見せる）
};
```

**無名 namespace の `Restart`** を `ResetPlayer` に改名して、流し始めの印を付ける（関数を丸ごと置き換え）。

```cpp
// ===== Componentで選んでいるアニメーションを最初の時刻から持ち直す =====
// AnimationSystem::Restart（ゲームから呼ぶ方）と名前がぶつかるので、こちらは ResetPlayer
void ResetPlayer(AnimatorState& animator, const AnimatorComponent& component, bool play) {
	animator.animationHandle = component.animationHandle;
	animator.clipIndex = component.clipIndex;
	animator.isJustStarted = false;
	// 未選択・無い番号: 空の AnimationPlayerにする（ノードの行列が空 → モデルに入っている形で描く）
	if (!HasClip(component.animationHandle, component.clipIndex)) {
		animator.player = AnimationPlayer{};
		return;
	}
	animator.player.Play(component.animationHandle, static_cast<uint32_t>(component.clipIndex), component.loop);
	if (!play) {
		animator.player.Stop(); // アニメーションは覚えたまま、最初の時刻で止まる
		return;
	}
	// 流し始めたフレームは 0 秒の形を見せる（Update で進めると、最初の形が1フレームも見えず、呼んだ側の「段階に入ってからの秒」とも1フレームずれる）
	animator.isJustStarted = true;
}
```

**DescribeAnimator** の Play On Start の Tooltip の最後を `AnimationSystem::Play・Restart` にする。

```cpp
	ui.Field("Play On Start###playOnStart", value.playOnStart, Tip("Play を押したとき（シーンが始まったとき）に再生する。オフならゲームから AnimationSystem::Play・Restart"));
```

**Update** の中の `Restart(` 2か所を `ResetPlayer(` にし、時刻の所を置き換える（「一覧から探す」の小見出しから、停止中の SetTime を閉じる `}` まで）。

```cpp
		// --- 一覧から探す。無ければ足す（再生中なら Play on Start）---
		AnimatorState* animator = FindAnimator(entity);
		if (animator == nullptr) {
			animators.push_back({entity});
			animator = &animators.back();
			ResetPlayer(*animator, component, isGameRunning && component.playOnStart);
		} else if (component.animationHandle != animator->animationHandle || component.clipIndex != animator->clipIndex) {
			// Inspector・ゲームからアニメーションから選び直した → 新しい方を最初から
			// 再生中のゲームなら、再生していた・Play On Start のときは再生する（未選択から選んでときも動き出すように）
			ResetPlayer(*animator, component, isGameRunning && (animator->player.IsPlaying() || component.playOnStart));
		}

		// --- 時刻 ---
		AnimationPlayer& player = animator->player;
		player.SetSpeed(component.speed);
		player.SetLoop(component.loop);
		if (isGameRunning) {
			// 流し始めたフレーム（Play・Restart・Play On Start・Clip の選び直し）は進めない。0 秒の形を見せて、次のフレームから進める
			if (animator->isJustStarted) {
				animator->isJustStarted = false;
			} else {
				player.Update(deltaTime); // 一時停止中は deltaTime が0なので進まない
			}
		} else {
			player.SetTime(component.previewTime); // 停止中はInspectorの時刻のポーズ
		}
```

**ゲームから** の大見出しの下の `Play`（最初から再生）を消して、`Play`・`Restart`・`CalcSpeedToFit` の3つにする（`Stop` から下はそのまま）。

```cpp
// ===== 番号で流す =====
bool AnimationSystem::Play(Handle<Entity> entity, int clipIndex, bool loop, float speed) {
	MY_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr) {
		return false; // Animator が無い（RequestAdd した直後は、次のフレームまで付いていない）
	}
	// --- Component に書く ---
	// Update は毎フレーム、Component の Speed・Loop を AnimationPlayer へ写し、Clip が変わっていたら最初から流す。ここに書かないと Update に戻されてしまう
	component->clipIndex = clipIndex;
	component->loop = loop;
	component->speed = speed;
	if (!EntityManager::IsActiveInHierarchy(entity)) {
		return false; // 無効な Entity（値だけ書いておく）
	}
	// --- 同じアニメが流れているなら、ここまで（速さと loop は Update が写す）---
	// 一覧を見る前に、ゲームが動いている側へ切り替えておく（理由は Restart と同じ）
	SwitchGameRunning(true);
	const AnimatorState* animator = FindAnimator(entity);
	if (animator != nullptr && animator->animationHandle == component->animationHandle && animator->clipIndex == clipIndex && animator->player.IsPlaying()) {
		return true;
	}
	// --- 番号が違う・止まっている：最初から ---
	return Restart(entity);
}

// ===== 最初から流し直す =====
bool AnimationSystem::Restart(Handle<Entity> entity) {
	MY_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	// Restart を呼ぶのはゲーム（Script・SYSTEM。ゲームが動いている間だけ回る）なので、ここで切り替えておく
	// ゲームが動き出した最初のフレームは、Script が AnimationSystem::Update より先に回るので、切り替えないと後の Update がこの Restart を捨てる
	SwitchGameRunning(true);
	const AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	// Animatorが無い（RequestAdd した直後は、次のフレームまで付いていない）・無効なEntity
	if (component == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		return false;
	}
	// 一覧に入れるので、この後のUpdateがPlay On Startでもう一度からにすることはない
	AnimatorState& animator = FindOrAddAnimator(entity);
	ResetPlayer(animator, *component, true);
	return animator.player.IsPlaying();
}

// ===== N 秒で流しきる速さ =====
float AnimationSystem::CalcSpeedToFit(Handle<Entity> entity, int clipIndex, float seconds) {
	const AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr || seconds <= 0.0f || !HasClip(component->animationHandle, clipIndex)) {
		return 1.0f;
	}
	const Animation* clip = AnimationManager::GetAnimation(component->animationHandle, static_cast<uint32_t>(clipIndex));
	return (clip->duration > 0.0f) ? clip->duration / seconds : 1.0f; // 長さ 0 のアニメ（1枚の形だけ）は、どの速さでも同じ
}
```

- `Play` は Component に書いてから、同じアニメが流れているかを見る。Update は毎フレーム Component の Speed・Loop を AnimationPlayer へ写し、Clip が変わっていたら最初から流すので、Component に書かないと Update に戻される
- 今の `Play(entity)` を呼んでいる所は、エンジン・Sandbox には無い（ゲームで使っていたら `Restart(entity)` にする）

ここでビルドできる。

## パートC 待ちの部品（攻撃表）

C-1〜C-8 を全部写してからビルドする（`AttackData::part` の形が変わるので、途中では通らない）。

### C-1 `AI/AttackTableManager.h`

**AttackPhaseData と AttackData の頭**を置き換える（`AttackPartSlot` を足し、フェーズに2つ、攻撃の行の `part`・`partFields` を `AttackPartSlot part` 1つに）。

```cpp
/// <summary>
/// 選んだ攻撃部品1つ（部品の名前と、その部品の Fields に書いた値）。攻撃の行と、フェーズの待ちの部品が使う
/// </summary>
struct AttackPartSlot {
	std::string name;          // ATTACK_PART(...) の名前（空なら部品を使わない）
	std::string fields = "{}"; // 部品の Fields に書いた値（JSON の文字）

	bool operator==(const AttackPartSlot&) const = default;
};

/// <summary>
/// フェーズ1つ
/// </summary>
struct AttackPhaseData {
	std::string name = "Phase";     // 名前（確認の画面に出す）
	float healthThreshold = 0.5f;   // HP の割合がこの値以下になったら、このフェーズへ移る（一番上のフェーズでは使わない）
	float transitionSeconds = 2.0f; // このフェーズへ移るときの長さ（0 なら移行の状態を飛ばす）
	float minIdleSeconds = 0.0f;    // 次の攻撃を選ぶまでの最短の秒（Selecting に入るたびに 0 から数える。0 ならすぐ選ぶ）
	AttackPartSlot idlePart;        // 待ちの部品（Selecting の間だけ動かす。空なら何もしない）

	bool operator==(const AttackPhaseData&) const = default;
};

/// <summary>
/// 攻撃1行
/// </summary>
struct AttackData {
	std::string name = "Attack";   // 名前（確認の画面・RequestAttack に使う）
	AttackPartSlot part;           // 攻撃部品（空なら時間が進むだけ）
```

`AttackData` の `weight` から下はそのまま。

見せ方の中見出しの下のコメントを直す。

```cpp
	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	// 表全体の値・フェーズ1つ・攻撃1行。部品（攻撃の Part・フェーズの Idle Part）と部品の値は、形が違うので別に扱う（選択肢が実行中に決まる）
```

### C-2 `AI/AttackTableManager.cpp`

**JSON にする**の「攻撃1行」を、`PartKeys`・`WritePartSlot` と一緒に置き換える。

```cpp
/// <summary>
/// 部品（名前と値）を書くときのキーの名前
/// </summary>
struct PartKeys {
	const char* name;   // 部品の名前
	const char* fields; // 部品の値
};
constexpr PartKeys kAttackPartKeys = {"part", "partFields"};         // 攻撃の行の部品
constexpr PartKeys kIdlePartKeys = {"idlePart", "idlePartFields"}; // フェーズの待ちの部品

// ===== 部品（名前と値）=====
// 部品は Describe の並びに入れない（選択肢が実行中に決まり、値の形も部品ごとに違う）ので、決めたキーで別に書く
void WritePartSlot(SceneJson& json, const AttackPartSlot& slot, const PartKeys& keys) {
	json[keys.name] = slot.name;
	json[keys.fields] = SceneJson::parse(slot.fields, nullptr, false);
}

// ===== 攻撃1行 =====
// Describe の項目のほかに、Part と部品の値を書く
SceneJson AttackToJson(AttackData attack, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) {
	SceneJson json;
	ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, json, warnings);
	WritePartSlot(json, attack.part, kAttackPartKeys);
	return json;
}
```

**ToJson** のフェーズの所に1行足す。

```cpp
		ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, i == 0); }, item, warnings);
		WritePartSlot(item, phase.idlePart, kIdlePartKeys);
		phases.push_back(std::move(item));
```

**JSON から読む**の「攻撃1行」を、`TakePartSlot` と一緒に置き換える。

```cpp
// ===== 部品（名前と値）=====
// 読んだキーは json から消す（部品を除いた残りを Describe の並びで読むため。残すと「読む所がない」と警告が出る）
AttackPartSlot TakePartSlot(SceneJson& json, const PartKeys& keys) {
	AttackPartSlot slot;
	if (const auto name = json.find(keys.name); name != json.end()) {
		if (name->is_string()) {
			slot.name = name->get<std::string>();
		}
		json.erase(name);
	}
	if (const auto fields = json.find(keys.fields); fields != json.end()) {
		if (fields->is_object()) {
			slot.fields = fields->dump();
		}
		json.erase(fields);
	}
	return slot;
}

// ===== 攻撃1行 =====
AttackData AttackFromJson(const SceneJson& json, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) {
	AttackData attack;
	SceneJson fields = json; // 部品を除いた残りを、Describe の並びで読む
	attack.part = TakePartSlot(fields, kAttackPartKeys);
	ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, fields, warnings);
	return attack;
}
```

**FromJson** のフェーズの読み方を直す（`item` をそのまま読まず、待ちの部品を抜いた写しを読む）。

```cpp
			AttackPhaseData phase = isFirst ? AttackTable{}.phases.front() : AttackPhaseData{}; // 一番上のフェーズはしきい値を書かないので、表の既定の値から始める
			SceneJson fields = item; // 待ちの部品を除いた残りを、Describe の並びで読む
			phase.idlePart = TakePartSlot(fields, kIdlePartKeys);
			ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, isFirst); }, fields, warnings);
			table.phases.push_back(std::move(phase));
```

**DescribePhase** を置き換える（一番上のフェーズでも Min Idle Seconds は出す・保存する）。

```cpp
// ===== フェーズ1つ =====
void AttackTableManager::DescribePhase(ComponentUI& ui, AttackPhaseData& value, bool isFirst) {
	ui.Field("Name###name", value.name);
	// 一番上のフェーズは最初から入っているので、しきい値も移行の長さも使わない（保存もしない）
	if (!isFirst) {
		ui.Field("HP Threshold###healthThreshold", value.healthThreshold, Range(0.0f, 1.0f).Tip("HP の割合がこの値以下になったら、このフェーズへ移る（0.5 で 50%）"));
		ui.Field("Transition Seconds###transitionSeconds", value.transitionSeconds, AtLeast(0.0f).Tip("このフェーズへ移るときの長さ（秒）。0 なら移行の状態を飛ばす"));
	}
	ui.Field("Min Idle Seconds###minIdleSeconds", value.minIdleSeconds,
	         AtLeast(0.0f).Tip("次の攻撃を選ぶまでの最短の秒。攻撃の後・怯みの後・フェーズの移行の後・始めに、Selecting に入るたびに 0 から数える（0 ならすぐ選ぶ。RequestAttack は待たない）"));
}
```

### C-3 `AI/AttackPart.h`

使い方のコメントの最後の段落から、`AttackPart` クラスの `<summary>` までを置き換える（`AttackPartContext` に `state` を足す・終わり方と順番の説明）。

```cpp
// 実体は敵1体の攻撃1行（待ちの部品はフェーズ1つ）につき1つできるので、メンバ変数に攻撃の途中の値を持ってよい
//
// 同じ部品を、攻撃の行の Part にも、フェーズの Idle Part（攻撃を選ぶまでの待ち）にも選べる
// どちらで呼ばれたかは context.state で分かる（攻撃は Attacking、待ちは Selecting）


/// <summary>
/// 攻撃部品に渡す今の様子
/// </summary>
struct AttackPartContext {
	GameObject self;                                      // 自分（この敵）
	GameObject target;                                    // 狙う相手（居なければ if (context.target) が false）
	AttackTableState state = AttackTableState::Selecting; // 今の状態（攻撃の部品は Attacking、待ちの部品は Selecting）
	AttackStage stage = AttackStage::None;                // 今の段階（待ちの部品は None）
	float stageTime = 0.0f;                               // 今の段階に入ってからの秒（待ちの部品は Selecting に入ってからの秒）
	float stageDuration = 0.0f;                           // 今の段階の長さ（秒。待ちの部品はフェーズの Min Idle Seconds）
	float deltaTime = 0.0f;                               // このフレームの秒（スローが掛かった物）
	int phase = 0;                                        // 今のフェーズ（0 から）

	// 今の段階がどこまで進んだか（0〜1）
	float GetStageProgress() const { return (stageDuration > 0.0f) ? (std::min)(stageTime / stageDuration, 1.0f) : 1.0f; }
};

/// <summary>
/// 攻撃の終わり方
/// </summary>
enum class AttackPartEnd {
	Finished,    // 硬直まで終わった（待ちの部品は、攻撃を選んだ）
	Interrupted, // 途中で止められた（怯み・フェーズの移行・撃破・表の作り直し・敵が消えた）
};


/// <summary>
/// 攻撃部品。継承して、使う関数だけ override する
/// <para>呼ばれるのは時間が進んでいるときだけ（ポーズ・時間停止・ヒットストップ・AI 停止の間は呼ばれない）</para>
/// <para>攻撃の Part のときの順番は ①OnBegin ②OnStage（予備動作）③OnUpdate を毎フレーム ④段階が変わるたびに OnStage ⑤OnEnd</para>
/// <para>Idle Part のときの順番は ①Selecting の最初のフレームに OnBegin ②同じフレームから OnUpdate を毎フレーム
/// ③攻撃を選んだら、その攻撃の OnBegin より先に OnEnd（Finished）。OnStage は呼ばれない</para>
/// </summary>
class AttackPart {
```

### C-4 `Components/Gameplay/AttackTableComponent.h`

`Selecting` の説明だけ直す。

```cpp
	Selecting,       // 次の攻撃を選んでいる（Min Idle Seconds が過ぎるまでと、使える攻撃が無い間は、待ちの部品を動かして待つ）
```

### C-5 `Systems/Gameplay/AttackTableSystem.h`

`AttackTableStatus::duration` の説明。

```cpp
	float duration = 0.0f;                                // 今の段階・状態の長さ（秒。Selecting はフェーズの Min Idle Seconds、Defeated は 0）
```

クラスの説明の「流れ」から2行（3行にする）。

```cpp
/// <para>流れ：Selecting → Attacking（Windup → Active → Recovery）→ Selecting。怯み・フェーズの移行・撃破が、待ちと攻撃を途中で止める</para>
/// <para>Selecting の間は、フェーズの待ちの部品（Idle Part）を動かす。Min Idle Seconds が過ぎて、使える攻撃があれば選ぶ</para>
/// <para>タイマー・クールタイム・部品の実体は、ここが敵ごとに持つ（再生が始まって最初の Update で作り、Stop で Entity と一緒に消える）</para>
```

`RequestAttack` の説明に1行足す。

```cpp
	/// 次の攻撃を名前で決める（条件を見ずに、次に選ぶときに必ずこの攻撃になる）
	/// <para>フェーズの Min Idle Seconds は待たない（演出の後に、すぐ大技を出すため）。攻撃の途中なら、終わってすぐ次がこの攻撃</para>
	/// </summary>
```

### C-6 `Systems/Gameplay/AttackTableSystem.cpp`

**AttackTableRuntime**：`idleParts` と `isIdleBegun` を足し、`duration` の説明を直す（並びが揃うように、`parts`・`cooldowns` の行のコメントの位置もずらした）。

```cpp
struct AttackTableRuntime {
	Handle<Entity> entity;                          // どの敵か
	uint32_t table = 0;                             // 作ったときの攻撃表（Component の表が替わったら作り直す）
	uint32_t revision = 0;                          // 読んだ攻撃表の版（変わったら部品の値を読み直す）
	std::vector<std::unique_ptr<AttackPart>> parts;     // [攻撃の行] 攻撃部品（Part が空・知らない名前なら nullptr）
	std::vector<std::unique_ptr<AttackPart>> idleParts; // [フェーズ] 待ちの部品（Idle Part が空・知らない名前なら nullptr）
	std::vector<float> cooldowns;                       // [攻撃の行] クールタイムの残りの秒

	AttackTableState state = AttackTableState::Selecting; // 状態
	AttackStage stage = AttackStage::None;                // 攻撃の段階
	int phase = 0;                                        // 今のフェーズ
	int attack = kNoAttack;                               // 今の攻撃の行
	int lastAttack = kNoAttack;                           // 最後に選んだ攻撃の行（同じ攻撃が続いた回数を数える）
	int repeatCount = 0;                                  // lastAttack を続けて選んだ回数
	int requestedAttack = kNoAttack;                      // RequestAttack で決められた次の攻撃の行
	bool isIdleBegun = false;                             // 待ちの部品の OnBegin を呼んで、まだ OnEnd を呼んでいない（Selecting の間だけ true になる）
	float time = 0.0f;                                    // 今の段階・状態に入ってからの秒
	float duration = 0.0f;                                // 今の段階・状態の長さ（Selecting はフェーズの Min Idle Seconds）
};
```

**攻撃部品の値を読む**の大見出しから `ReadAllPartFields` までを置き換える（作る所も、攻撃の行とフェーズで1つにする）。

```cpp
//=============================================================================
// 攻撃部品を作る・値を読む
//=============================================================================
// ===== 部品を作る（Part が空・知らない名前なら nullptr。知らない名前は警告を出す）=====
// owner は警告に出す持ち主（「攻撃 "Dash"」「フェーズ "Phase 1" の待ち」）
std::unique_ptr<AttackPart> CreatePart(const AttackPartSlot& slot, std::string_view owner) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
	if (part == nullptr && !slot.name.empty()) {
		LogManager::Warning(std::format("{}の部品 \"{}\" がありません（ATTACK_PART(...) を確かめてください）。部品なしで動かします", owner, slot.name));
	}
	return part;
}

// ===== 部品の値を読む（label は警告に出す名前）=====
void ReadPartFields(AttackPart& part, const AttackPartSlot& slot, std::string_view label, std::string_view tablePath) {
	const SceneJson json = SceneJson::parse(slot.fields, nullptr, false);
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(label, [&part](ComponentUI& ui) { part.Fields(ui); }, json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", tablePath, warning));
	}
}

// ===== 攻撃の行と、フェーズの待ちの部品の値を、全部読む =====
void ReadAllPartFields(AttackTableRuntime& runtime, const AttackTable& table) {
	const std::string& tablePath = AttackTableManager::GetPath(runtime.table);
	for (size_t i = 0; i < runtime.parts.size(); ++i) {
		if (runtime.parts[i] != nullptr) {
			ReadPartFields(*runtime.parts[i], table.attacks[i].part, table.attacks[i].name, tablePath);
		}
	}
	for (size_t i = 0; i < runtime.idleParts.size(); ++i) {
		if (runtime.idleParts[i] != nullptr) {
			ReadPartFields(*runtime.idleParts[i], table.phases[i].idlePart, table.phases[i].name, tablePath);
		}
	}
}
```

**MakeContext** に1行足す。

```cpp
	context.target = GameObject{EntityManager::Find(component.target)};
	context.state = runtime.state;
	context.stage = runtime.stage;
```

**EnterState** の後に `EnterSelecting` を足す。

```cpp
// ===== 選ぶ状態へ（長さはフェーズの Min Idle Seconds）=====
// 待ちの部品の OnBegin はここでは呼ばず、次に時間が進むフレームの UpdateSelecting で呼ぶ（AI が止まっている・時間が 0 のフレームに呼ばないため）
void EnterSelecting(AttackTableRuntime& runtime, const AttackTable& table) { EnterState(runtime, AttackTableState::Selecting, table.phases[runtime.phase].minIdleSeconds); }
```

**EndAttack** の後（「攻撃の抽選」の大見出しの前）に、大見出しごと足す。

```cpp
//=============================================================================
// 待ちの部品・止める
//=============================================================================
// ===== 今のフェーズの待ちの部品（Idle Part が空・知らない名前なら nullptr）=====
AttackPart* GetIdlePart(const AttackTableRuntime& runtime) {
	const size_t phase = static_cast<size_t>(runtime.phase);
	return (phase < runtime.idleParts.size()) ? runtime.idleParts[phase].get() : nullptr;
}

// ===== 待ちの部品へ、終わったことを知らせる（OnBegin を呼んでいなければ何もしない）=====
void EndIdle(AttackTableRuntime& runtime, const AttackTableComponent& component, AttackPartEnd reason, float deltaTime) {
	if (!runtime.isIdleBegun) {
		return;
	}
	runtime.isIdleBegun = false; // OnEnd より先に下ろす（OnEnd の中で Stagger を呼ばれても、2回知らせないように）
	if (AttackPart* part = GetIdlePart(runtime)) {
		part->OnEnd(MakeContext(runtime, component, deltaTime), reason);
	}
}

// ===== 今動いている部品（待ちか攻撃）を止める（怯み・フェーズの移行・撃破。攻撃のクールタイムも数える）=====
void InterruptCurrent(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	EndIdle(runtime, component, AttackPartEnd::Interrupted, deltaTime);
	EndAttack(runtime, component, table, AttackPartEnd::Interrupted, deltaTime);
}

// ===== 今動いている部品（待ちか攻撃）へ、止められたことだけを知らせる（表の作り直し・敵が消えるとき。この後で実行中の値ごと捨てる）=====
void NotifyInterrupted(AttackTableRuntime& runtime, const AttackTableComponent& component) {
	EndIdle(runtime, component, AttackPartEnd::Interrupted, 0.0f);
	NotifyAttackEnd(runtime, component, AttackPartEnd::Interrupted, 0.0f);
}
```

**Build** を置き換える。

```cpp
// ===== 表に合わせて攻撃部品・待ちの部品を作る =====
void Build(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table) {
	runtime.table = component.table;
	runtime.revision = AttackTableManager::GetRevision(component.table);
	runtime.parts.clear();
	for (const AttackData& attack : table.attacks) {
		runtime.parts.push_back(CreatePart(attack.part, std::format("攻撃 \"{}\" ", attack.name)));
	}
	runtime.idleParts.clear();
	for (const AttackPhaseData& phase : table.phases) {
		runtime.idleParts.push_back(CreatePart(phase.idlePart, std::format("フェーズ \"{}\" の待ち", phase.name)));
	}
	ReadAllPartFields(runtime, table);
	runtime.cooldowns.assign(table.attacks.size(), 0.0f);
	runtime.attack = kNoAttack;
	runtime.lastAttack = kNoAttack;
	runtime.repeatCount = 0;
	runtime.requestedAttack = kNoAttack;
	runtime.isIdleBegun = false;
	runtime.phase = CalcPhase(table, 0, component.healthRatio); // 始めから HP が減っていれば、移行の状態を飛ばしてそのフェーズから
	EnterSelecting(runtime, table);
}
```

**PrepareRuntime** の作り直しの条件。

```cpp
	// --- 表が替わった・攻撃の行やフェーズの数が変わった：今の部品を止めて作り直す ---
	if (runtime->table != component.table || runtime->parts.size() != table.attacks.size() || runtime->idleParts.size() != table.phases.size()) {
		NotifyInterrupted(*runtime, component); // 表はもう新しい物なので、クールタイムは数えない（Build で全部 0 に戻る）
		Build(*runtime, component, table);
		return runtime;
	}
```

**UpdateHealth** の `EndAttack(...)` 2か所を `InterruptCurrent(...)` にする。

```cpp
	if (component.healthRatio <= 0.0f) {
		InterruptCurrent(runtime, component, table, deltaTime);
		EnterState(runtime, AttackTableState::Defeated, 0.0f);
		return true;
	}
	const int phase = CalcPhase(table, runtime.phase, component.healthRatio);
	if (phase == runtime.phase) {
		return false;
	}
	InterruptCurrent(runtime, component, table, deltaTime); // 待ちの部品は、移る前のフェーズの物を止める
```

**UpdateSelecting** を丸ごと置き換える。

```cpp
// ===== 選ぶ =====
// Min Idle Seconds が過ぎるまでと、使える攻撃が無い間は、待ちの部品を動かして待つ
void UpdateSelecting(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	runtime.duration = table.phases[runtime.phase].minIdleSeconds; // 再生中に秒を直しても、すぐ効くように
	AttackPart* idle = GetIdlePart(runtime);
	// --- Selecting の最初のフレーム：待ちの部品を始める ---
	if (!runtime.isIdleBegun) {
		runtime.isIdleBegun = true;
		if (idle != nullptr) {
			idle->OnBegin(MakeContext(runtime, component, deltaTime));
		}
		// OnBegin の中で Stagger を呼ばれたら、待ちはもう終わっている
		if (runtime.state != AttackTableState::Selecting) {
			return;
		}
	}
	// --- 待ちの部品を進める ---
	runtime.time += deltaTime;
	if (idle != nullptr) {
		idle->OnUpdate(MakeContext(runtime, component, deltaTime));
	}
	if (runtime.state != AttackTableState::Selecting) {
		return; // OnUpdate の中で Stagger を呼ばれた
	}
	// --- 選ぶ（RequestAttack で決められた攻撃は、Min Idle Seconds を待たない）---
	int attack = runtime.requestedAttack;
	if (attack == kNoAttack && runtime.time >= runtime.duration) {
		static std::vector<AttackCheck> checks; // 毎フレーム配列を作り直さないように使い回す
		CheckAttacks(runtime, table, CalcDistance(runtime.entity, component), checks);
		attack = PickAttack(checks);
	}
	if (attack == kNoAttack) {
		return; // まだ待つ
	}
	// --- 待ちを終えてから、攻撃を始める（待ちの部品が書いた速さなどを、攻撃の OnBegin より先に片付けられるように）---
	EndIdle(runtime, component, AttackPartEnd::Finished, deltaTime);
	if (runtime.state != AttackTableState::Selecting) {
		return; // OnEnd の中で Stagger を呼ばれた（RequestAttack の分は、次に選ぶときへ残す）
	}
	runtime.requestedAttack = kNoAttack;
	BeginAttack(runtime, component, table, attack, deltaTime);
}
```

**UpdateAttacking** の最後（硬直が終わった所）の `EnterState(runtime, AttackTableState::Selecting, 0.0f);` を `EnterSelecting(runtime, table);` にする。

```cpp
		EndAttack(runtime, component, table, AttackPartEnd::Finished, deltaTime);
		EnterSelecting(runtime, table);
```

**UpdateTimedState** に表を渡す。

```cpp
void UpdateTimedState(AttackTableRuntime& runtime, const AttackTable& table, float deltaTime) {
	runtime.time += deltaTime;
	if (runtime.time >= runtime.duration) {
		EnterSelecting(runtime, table);
	}
}
```

**UpdateEntity** の呼ぶ所も。

```cpp
		UpdateTimedState(*runtime, *table, deltaTime);
```

**Stagger** の `EndAttack(...)` を `InterruptCurrent(...)` にする。

```cpp
	InterruptCurrent(*runtime, *component, *table, 0.0f); // 待ちの部品・攻撃の部品のどちらも止める
```

**OnEntityDestroyed** の `NotifyAttackEnd(...)` を `NotifyInterrupted(...)` にする。

```cpp
		NotifyInterrupted(*runtime, *component); // Component はまだ読める。部品が出した物を片付けられるように
```

### C-7 `Editor/Inspector/AttackTableEditor.cpp`

インクルードに `<string_view>` を足す。

```cpp
#include <algorithm>
#include <string_view>
```

**(none) の説明**を2つにする。

```cpp
constexpr const char* kNoPartDescription = "部品を使わない（中身は動かず、時間だけ進む）";         // Part が (none) のときの説明
constexpr const char* kNoIdlePartDescription = "部品を使わない（攻撃を選ぶまで、何もせずに待つ）"; // Idle Part が (none) のときの説明
```

**DrawPartCombo と DrawPartFields** を丸ごと置き換える（`AttackData&` ではなく `AttackPartSlot&` を受け取る。部品の値の欄は ID の枠を分ける）。

```cpp
// ===== Part の選択欄 =====
// 選択肢は ATTACK_PART(...) で登録した部品。選んでいる部品の説明を、欄の下に出す
// 知らない名前（別のゲームで作った部品など）は、消さずに赤で出す
// 攻撃の行の Part と、フェーズの Idle Part の両方がこれを使う（label は欄の名前、noneDescription は (none) の説明）
void DrawPartCombo(const char* label, AttackPartSlot& slot, const std::vector<std::string>& names, const char* noneDescription) {
	const bool isUnknown = !slot.name.empty() && std::ranges::find(names, slot.name) == names.end();
	if (isUnknown) {
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
	}
	const std::string preview = slot.name.empty() ? "(none)" : slot.name;
	if (ImGui::BeginCombo(label, preview.c_str())) {
		if (ImGui::Selectable("(none)", slot.name.empty())) {
			slot = AttackPartSlot{};
		}
		ImGui::SetItemTooltip("%s", noneDescription);
		for (const std::string& name : names) {
			if (ImGui::Selectable(name.c_str(), name == slot.name) && name != slot.name) {
				slot = AttackPartSlot{name}; // 別の部品の値は使えないので、値は空に戻す
			}
			const std::string& description = AttackPartRegistry::GetDescription(name);
			if (!description.empty()) {
				ImGui::SetItemTooltip("%s", description.c_str());
			}
		}
		ImGui::EndCombo();
	}
	if (isUnknown) {
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("この名前の攻撃部品がありません（ATTACK_PART(...) を確かめてください）");
	} else {
		ImGui::SetItemTooltip("ATTACK_PART(...) で登録した攻撃部品から選ぶ");
	}
	// --- 選んでいる部品の説明（長い文でも欄の幅で折り返す）---
	const std::string& description = AttackPartRegistry::GetDescription(slot.name);
	const char* text = slot.name.empty() ? noneDescription : description.c_str();
	if (text[0] != '\0') {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}
}

// ===== 部品の値 =====
// 部品を1つ作って、表の値を読み、Fields を描いて、書き戻す（Fields の実体はゲームのクラスの中にあるので、表だけでは描けない）
// label は警告に出す名前（攻撃の名前・フェーズの名前）
void DrawPartFields(ComponentUI& ui, AttackPartSlot& slot, std::string_view label) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
	if (part == nullptr) {
		return;
	}
	std::set<std::string> warnings;
	const auto describe = [&part](ComponentUI& fields) { part->Fields(fields); };
	ComponentSerializer::ReadFields(label, describe, SceneJson::parse(slot.fields, nullptr, false), warnings);
	// 部品の欄の名前は部品が決めるので、表の欄（Name など）とぶつからないように ID の枠を分ける
	ImGui::PushID("partFields");
	part->Fields(ui);
	ImGui::PopID();
	SceneJson json;
	ComponentSerializer::WriteFields(label, describe, json, warnings);
	slot.fields = json.dump();
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}
```

**DrawPhases** の頭（フェーズ1つを描く所）に Idle Part の欄を足す。

```cpp
void DrawPhases(ComponentUI& ui, AttackTable& table, bool isLocked) {
	const std::vector<std::string> partNames = AttackPartRegistry::GetNames();
	int removePhase = -1;
	for (int i = 0; i < static_cast<int>(table.phases.size()); ++i) {
		AttackPhaseData& phase = table.phases[i];
		ImGui::PushID(i);
		ImGui::SeparatorText(std::format("{}. {}", i + 1, phase.name).c_str());
		AttackTableManager::DescribePhase(ui, phase, i == 0);
		// --- 待ちの部品（攻撃の Part と同じ一覧から選ぶ。再生中は替えられない）---
		ImGui::BeginDisabled(isLocked);
		DrawPartCombo("Idle Part###idlePart", phase.idlePart, partNames, kNoIdlePartDescription);
		ImGui::EndDisabled();
		DrawPartFields(ui, phase.idlePart, phase.name);
		if (i != 0) {
```

`Remove Phase` のボタンから下はそのまま。

**DrawAttacks** の Part の欄を、新しい引数で呼ぶ。

```cpp
			ImGui::SeparatorText("Part");
			ImGui::BeginDisabled(isLocked);
			DrawPartCombo("Part###part", attack.part, partNames, kNoPartDescription);
			ImGui::EndDisabled();
			DrawPartFields(ui, attack.part, attack.name);
```

### C-8 `Editor/Windows/AttackTableWindow.cpp`

**DrawStatus** の「最後に選んだ」の行の後（残り時間の前）に足す。

```cpp
	// --- 待ちの部品（Selecting の間だけ動く。残りの秒は下の棒が Min Idle Seconds の残り）---
	const AttackTable* table = AttackTableManager::Get(component->table);
	const std::string& idlePart = table->phases[status.phase].idlePart.name;
	ImGui::Text("Idle Part: %s", idlePart.empty() ? "-" : idlePart.c_str());
	if (status.state == AttackTableState::Selecting) {
		ImGui::SameLine();
		ImGui::TextDisabled(status.GetRemaining() > 0.0f ? "（Min Idle Seconds の待ち）" : "（使える攻撃を待っている）");
	}
```

ここでビルドできる（今の .attacktable はそのまま読める。Min Idle Seconds 0・Idle Part なしなので、動きも今までと同じ）。

## パートD Sandbox で確かめる

### D-1 `Sandbox/Source/AttackParts/ChaseTarget.cpp`（新しい）

相手の方を向き、遠ければ Character Controller で近づき、近ければ止まる待ちの部品。パートA の角度の関数と、パートB の Play を使う。

```cpp
#include "MyEngine/AI/AttackPart.h"
#include "MyEngine/Components/Physics/CharacterControllerComponent.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Systems/Animation/AnimationSystem.h"


/// <summary>
/// 相手の方を向き、遠ければ Character Controller で近づき、近ければ止まる（Sandbox のテスト用。攻撃表のフェーズの Idle Part に選ぶ）
/// <para>同じ Entity に Character Controller と Animator を付ける。アニメは歩いている間は等速、止まっている間はゆっくり流す</para>
/// </summary>
class ChaseTarget : public AttackPart {
public:
	void OnUpdate(const AttackPartContext& context) override;
	void OnEnd(const AttackPartContext& context, AttackPartEnd reason) override;
	void Fields(ComponentUI& ui) override;

private:
	float moveSpeed_ = 3.0f;       // 近づく速さ（m/s）
	float stopDistance_ = 2.5f;    // 相手とこの距離より近ければ止まる（m。地面に平らに測る）
	float turnSmoothTime_ = 0.15f; // 向きを合わせる速さ（秒。小さいほど速く向く）
	int clipIndex_ = 0;            // 流すアニメの番号
	float stopAnimSpeed_ = 0.25f;  // 止まっている間のアニメの速さ
};
ATTACK_PART(ChaseTarget, "相手の方を向き、遠ければ Character Controller で近づく（Sandbox のテスト用。Idle Part に選ぶ）");


//=============================================================================
// 毎フレーム
//=============================================================================
void ChaseTarget::OnUpdate(const AttackPartContext& context) {
	CharacterControllerComponent* controller = context.self.GetComponent<CharacterControllerComponent>();
	if (controller == nullptr || !context.target) {
		return; // Character Controller が無い・相手が居ない
	}

	// --- 相手への向き（地面に平ら）---
	TransformComponent& transform = context.self.transform;
	Vector3 toTarget = GetWorldPosition(context.target.transform) - GetWorldPosition(transform);
	toTarget.y = 0.0f;
	const float distance = Length(toTarget);

	// --- 向く：今の角度から一番近い回り方で、なめらかに（フレームの速さに左右されない）---
	if (distance > 0.0f) {
		const float yaw = GetEulerAngles(transform).y;
		const float targetYaw = MathUtility::DirectionToYaw(toTarget);
		const float rate = MathUtility::CalcSmoothRate(context.deltaTime, turnSmoothTime_);
		SetEulerAngles(transform, {0.0f, yaw + MathUtility::DeltaAngle(yaw, targetYaw) * rate, 0.0f});
	}

	// --- 近づく・止まる（X・Z だけ書く。Y は Character Controller の重力に任せる）---
	const bool isMoving = (distance > stopDistance_);
	const Vector3 velocity = isMoving ? Normalize(toTarget) * moveSpeed_ : Vector3{0.0f, 0.0f, 0.0f};
	controller->velocity.x = velocity.x;
	controller->velocity.z = velocity.z;

	// --- アニメ：同じ番号を毎フレーム渡しても、最初には戻らない（速さだけ変わる）---
	AnimationSystem::Play(context.self.handle, clipIndex_, true, isMoving ? 1.0f : stopAnimSpeed_);
}

//=============================================================================
// 終わり
//=============================================================================
// 攻撃を選んだときは、その攻撃の OnBegin より先に呼ばれる。ここで速さを消すので、攻撃が始まったフレームに歩きの速さが残らない
void ChaseTarget::OnEnd(const AttackPartContext& context, AttackPartEnd /*reason*/) {
	if (CharacterControllerComponent* controller = context.self.GetComponent<CharacterControllerComponent>()) {
		controller->velocity.x = 0.0f;
		controller->velocity.z = 0.0f;
	}
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void ChaseTarget::Fields(ComponentUI& ui) {
	ui.Field("Move Speed###moveSpeed", moveSpeed_, AtLeast(0.0f).Tip("近づく速さ（m/s）"));
	ui.Field("Stop Distance###stopDistance", stopDistance_, AtLeast(0.0f).Tip("相手とこの距離より近ければ止まる（m。地面に平らに測る）"));
	ui.Field("Turn Smooth Time###turnSmoothTime", turnSmoothTime_, AtLeast(0.0f).Tip("向きを合わせる速さ（秒。小さいほど速く向く。0 ならすぐ向く）"));
	ui.Field("Clip Index###clipIndex", clipIndex_, AtLeast(0.0f).Tip("流すアニメの番号（Animator の Clip と同じ番号）"));
	ui.Field("Stop Anim Speed###stopAnimSpeed", stopAnimSpeed_, AtLeast(0.0f).Tip("止まっている間のアニメの速さ（歩いている間は 1）"));
}
```

### D-2 `Sandbox/Source/AttackParts/SpinAttack.cpp`（新しい）

予備動作の間に、アニメをちょうど1回流しきる攻撃部品。`CalcSpeedToFit` と、同じ番号を最初から流す `Restart` を使う。

```cpp
#include "MyEngine/AI/AttackPart.h"
#include "MyEngine/Systems/Animation/AnimationSystem.h"


/// <summary>
/// 予備動作の間に、アニメをちょうど1回流しきる（AnimationSystem::CalcSpeedToFit の確かめ。Sandbox のテスト用）
/// <para>同じ Entity に Animator を付ける。動きは付けない（攻撃の間、敵が止まっているかを見る）</para>
/// </summary>
class SpinAttack : public AttackPart {
public:
	void OnStage(const AttackPartContext& context) override;
	void Fields(ComponentUI& ui) override;

private:
	int clipIndex_ = 0; // 流すアニメの番号
};
ATTACK_PART(SpinAttack, "予備動作の間に、アニメをちょうど1回流しきる（Sandbox のテスト用）");


//=============================================================================
// 段階に入った
//=============================================================================
void SpinAttack::OnStage(const AttackPartContext& context) {
	if (context.stage != AttackStage::Windup) {
		return;
	}
	// 予備動作の長さで、ちょうど1回（ループしない）
	const Handle<Entity> self = context.self.handle;
	AnimationSystem::Play(self, clipIndex_, false, AnimationSystem::CalcSpeedToFit(self, clipIndex_, context.stageDuration));
	// 待ちの部品が同じ番号のアニメを流していると、Play は速さと loop だけ変える（最初には戻らない）。最初から流したいので Restart
	AnimationSystem::Restart(self);
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void SpinAttack::Fields(ComponentUI& ui) { ui.Field("Clip Index###clipIndex", clipIndex_, AtLeast(0.0f).Tip("流すアニメの番号（Animator の Clip と同じ番号）")); }
```

### D-3 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

Visual Studio のソリューションエクスプローラーで、Sandbox の Source に新しいフィルター AttackParts を作って2つを足してもよい。手で書くなら：

`Sandbox.vcxproj`（`main.cpp` の次）

```xml
    <ClCompile Include="main.cpp" />
    <ClCompile Include="Source\AttackParts\ChaseTarget.cpp" />
    <ClCompile Include="Source\AttackParts\SpinAttack.cpp" />
```

`Sandbox.vcxproj.filters`（フィルターと、`main.cpp` の次）

```xml
    <Filter Include="Source\AttackParts">
      <UniqueIdentifier>{8d4fdb17-5795-4708-aa13-c2c7dd01a5d6}</UniqueIdentifier>
    </Filter>
```

```xml
    <ClCompile Include="main.cpp" />
    <ClCompile Include="Source\AttackParts\ChaseTarget.cpp">
      <Filter>Source\AttackParts</Filter>
    </ClCompile>
    <ClCompile Include="Source\AttackParts\SpinAttack.cpp">
      <Filter>Source\AttackParts</Filter>
    </ClCompile>
```

### D-4 `Sandbox/Resources/AttackTables/IdlePartTest.attacktable`（新しい）

フォルダ `AttackTables` を作って置く（Project ウィンドウの Create → Attack Table で作って、Inspector で同じ値にしてもよい）。

- Phase 1：Min Idle Seconds 1 秒・ChaseTarget（3 m/s）
- Phase 2（HP 50% 以下）：移行 1 秒・Min Idle Seconds 0.3 秒・ChaseTarget（6 m/s・速く向く）
- 攻撃 Spin：距離 3.5 以内・予備動作 0.8 秒・攻撃 0.3 秒・硬直 0.6 秒・SpinAttack

```json
{
	"settings": {
		"maxRepeat": 0
	},
	"phases": [
		{
			"name": "Phase 1",
			"minIdleSeconds": 1.0,
			"idlePart": "ChaseTarget",
			"idlePartFields": {
				"moveSpeed": 3.0,
				"stopDistance": 2.5,
				"turnSmoothTime": 0.15,
				"clipIndex": 0,
				"stopAnimSpeed": 0.25
			}
		},
		{
			"name": "Phase 2",
			"healthThreshold": 0.5,
			"transitionSeconds": 1.0,
			"minIdleSeconds": 0.3,
			"idlePart": "ChaseTarget",
			"idlePartFields": {
				"moveSpeed": 6.0,
				"stopDistance": 2.5,
				"turnSmoothTime": 0.05,
				"clipIndex": 0,
				"stopAnimSpeed": 0.5
			}
		}
	],
	"attacks": [
		{
			"name": "Spin",
			"weight": 1.0,
			"minDistance": 0.0,
			"maxDistance": 3.5,
			"cooldown": 0.0,
			"windupSeconds": 0.8,
			"activeSeconds": 0.3,
			"recoverySeconds": 0.6,
			"phase0": true,
			"phase1": true,
			"part": "SpinAttack",
			"partFields": {
				"clipIndex": 0
			}
		}
	]
}
```

### D-5 `Sandbox/Resources/scenes/IdlePartTest.scene.json`（新しい）

- Player（id 10）：キーボードで動く（CharacterKeyboardMover。W・A・S・D）
- Enemy（id 20）：Character Controller・Attack Table（表は D-4・Target は Player）・AnimatedCube のモデルと Animator（Play On Start は外す。動かすのは部品）。赤い Enemy Nose が前
- カメラは上から（追わない）

```json
{
	"version": 1,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 18.0, -6.0],
					"rotation": [60.0, 0.0, 0.0],
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
					"translation": [0.0, -0.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [40.0, 1.0, 40.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.55, 0.55, 0.55, 1.0]
				}
			}
		},
		{
			"id": 10,
			"name": "Player",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Capsule",
					"center": [0.0, 0.9, 0.0],
					"physicMaterial": "",
					"radius": 0.4,
					"height ": 1.8
				},
				"Character Controller": {
					"enabled": true,
					"slopeLimit": 45.0,
					"stepOffset": 0.3,
					"useGravity": true,
					"gravityScale": 1.0,
					"pushRigidbodies": true,
					"pushMass": 80.0,
					"freezePositionX": false,
					"freezePositionY": false,
					"freezePositionZ": false,
					"velocity": [0.0, 0.0, 0.0]
				},
				"CharacterKeyboardMover": {
					"moveSpeed": 6.0,
					"jumpSpeed": 6.0,
					"dashSpeed": 18.0,
					"dashTime": 0.18
				}
			}
		},
		{
			"id": 11,
			"name": "Player Body",
			"parent": 10,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.9, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.8, 1.8, 0.8]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Sphere",
					"color": [0.95, 0.85, 0.3, 1.0]
				}
			}
		},
		{
			"id": 20,
			"name": "Enemy",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 1.0, 10.0],
					"rotation": [0.0, 180.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Model Renderer": {
					"enabled": true,
					"model": "MyEngine/Resources/Model/AnimatedCube/AnimatedCube.gltf",
					"shadingType": "HalfLambert",
					"color": [1.0, 1.0, 1.0, 1.0],
					"overrideMaterial": false,
					"baseColor": [1.0, 1.0, 1.0, 1.0]
				},
				"Animator": {
					"enabled": true,
					"animation": "MyEngine/Resources/Model/AnimatedCube/AnimatedCube.gltf",
					"clip": "animation_AnimatedCube",
					"speed": 1.0,
					"loop": true,
					"playOnStart": false,
					"previewTime": 0.0
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Capsule",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"radius": 1.0,
					"height ": 2.0
				},
				"Character Controller": {
					"enabled": true,
					"slopeLimit": 45.0,
					"stepOffset": 0.3,
					"useGravity": true,
					"gravityScale": 1.0,
					"pushRigidbodies": true,
					"pushMass": 80.0,
					"freezePositionX": false,
					"freezePositionY": false,
					"freezePositionZ": false,
					"velocity": [0.0, 0.0, 0.0]
				},
				"Attack Table": {
					"enabled": true,
					"table": "resources/AttackTables/IdlePartTest.attacktable",
					"target": 10,
					"healthRatio": 1.0
				}
			}
		},
		{
			"id": 21,
			"name": "Enemy Nose",
			"parent": 20,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.3, 1.3],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.3, 0.3, 0.6]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.25, 0.2, 1.0]
				}
			}
		}
	]
}
```

### D-6 確かめること

Sandbox を起動して、Project ウィンドウから IdlePartTest を開き、Attack Table ウィンドウ（Inspector の隣のタブ）を見ながら Play。

| # | すること | 見えること |
|---|---|---|
| 1 | Play してそのまま見る | Enemy が Player の方を向き（赤い Nose が Player を指す）、3 m/s で近づく。キューブは回っている（歩きのアニメの代わり） |
| 2 | 近づききる | Player から 2.5m で止まり、キューブがゆっくり回る（止まっている間のアニメの速さ 0.25） |
| 3 | Attack Table ウィンドウ | State: Selecting、`Idle Part: ChaseTarget（Min Idle Seconds の待ち）`、棒が 1.00 秒から減る。過ぎると距離 3.5 以内なので Spin を選ぶ |
| 4 | Spin が始まる | Enemy はその場で止まる（歩きの速さが残らない）。キューブが最初から回り、予備動作の 0.8 秒でちょうど1回分回って止まる |
| 5 | Spin が終わる | また Selecting。1 秒待つ間に Player の方へ向き直り、近ければ止まったまま |
| 6 | W・A・S・D で Player を動かす | Enemy が追いかけて向きを変える。350 度 → 10 度のような所でも大回りしない |
| 7 | Selecting の間に Stagger ボタン | 怯んでいる 1 秒は動かない（その前に書いた速さも 0 に戻っている）。終わるとまた追う |
| 8 | Attacks の表の Next（Spin） | Selecting の 1 秒を待たずに、次のフレームで Spin が始まる（距離が遠くても） |
| 9 | Enemy の Attack Table の Health Ratio を 0.4 にする | Phase 2 へ移る（1 秒止まる）。その後は 6 m/s で追い、待ちは 0.3 秒 |
| 10 | Inspector で IdlePartTest.attacktable を選ぶ | 各フェーズに Min Idle Seconds と Idle Part（ChaseTarget の説明と値）が出る。再生中は Idle Part を替えられない（値は直せて、すぐ効く） |
| 11 | Stop | 置いた位置に戻る |

## 2章 AL4_01 での使い方（ゲームの直し方の目安）

エンジンのサブモジュールをこのコミットへ進めたら：

- **PlayerController**：private の `SetClip(clip, loop, speed)` → `AnimationSystem::Play(gameObject.handle, clip, loop, speed)`。`CalcSpeedToFit(clip, seconds)` → `AnimationSystem::CalcSpeedToFit(gameObject.handle, clip, seconds)`。回避ジャンプを続けて出すとき、前の回避が流れている途中なら `Restart` も呼ぶ
- **向き**：`std::remainder(targetYaw - facingYaw_, 360.0f)` → `MathUtility::DeltaAngle(facingYaw_, targetYaw)`、`std::atan2(x, z) * kRadToDeg` → `MathUtility::DirectionToYaw(target)`
- **割合**：`CalcDampFactor(sharpness, deltaTime)` → `MathUtility::CalcSmoothRate(deltaTime, smoothTime)`。値は **smoothTime ＝ 1 / sharpness**（sharpness 10 なら 0.1 秒）。Inspector の項目名も秒にする
- **ボス**：Chase を `ATTACK_PART` で書き（D-1 の形）、攻撃表のフェーズの Idle Part に選ぶ。段階ごとにアニメを分けた攻撃部品は、OnStage で `Play(self, 段階の番号, false, CalcSpeedToFit(self, 段階の番号, context.stageDuration))`
- 前の AttackTable.md の「Script で GetStatus を見て近づける」は、待ちの部品に置き換わる（Script は演出の判定だけにする）

## 3章 次の候補

- **アニメを名前で流す**（`Play(entity, "Run", …)`）：番号はファイルの中の並びなので、Blender で並べ替えると変わる。今は名前の頭に番号を付けて合わせている
- **切り替えの混ぜ合わせ**（クロスフェード）：今は番号が変わると、すぐ次のアニメの 0 秒の形になる
- **待ちの部品を攻撃ごとに**：今はフェーズごとに1つ。「この攻撃の後だけ下がる」のような待ちは、待ちの部品の中で `context` と GetStatus の lastAttackName を見て分ける
- **Min Idle Seconds の幅**（最短〜最長の乱数）：今は決まった秒
- **怯み・フェーズの移行の間の部品**：今は何も呼ばない。のけぞる・咆哮のような動きは、今は Script か Timeline で
