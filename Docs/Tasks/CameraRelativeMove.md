参考：
- Cinemachine の Simple Player Controller（Input Forward を Camera にすると、カメラの前の向きを入力の前にする。このエンジンの CameraRelativeInput と同じ役目）：https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/SimplePlayerController.html
- 前の md：`Docs/Tasks/CameraOrbit.md`（0.10 ロックオンの組み方・0.11 やらないこと。テストシーン CameraOrbitTest）
- この作業のきっかけ：ロックオンのカメラ（CameraOrbitTest の 3）でボスのまわりを回ろうとすると、操作が直感的でない。Sandbox の PlayerController が、ワールドの向き（W＝+Z）で動かしているため

# カメラに合わせた移動：カメラから見た向きでキャラを動かす

- **パートA CameraSystem に「写しているカメラの地面の上の向き」を足す**（`CameraSystem.h/.cpp`）：`CameraSystem::GetMainCameraYaw()`。写す値に「揺れを足す前の向き」を持たせ、補間の後のその向きを、地面に寝かせた角度にする
- **パートB CameraRelativeInput（新しい）**（`Camera/CameraRelativeInput.h/.cpp`・vcxproj）：キャラの Script が1つ持つ小さな型。`ToWorld(input)` で 2D の入力をワールドの地面の向きへ直す。押している間にカメラが切り替わったら、離すまで前の向きを使い続ける
- **パートC Sandbox の PlayerController**：`InputManager::GetMoveAxis()` を `CameraRelativeInput` に通して歩く
- **パートD Reference**（`10-Camera.md` に節を足し、`11-Input.md` と索引の `Reference.md` から指す）
- **パートE 確かめ方**

**A → B → C → D の順**に写す（B は A の `GetMainCameraYaw`、C は B の型を使う）。

ブランチは `feature/CameraRelativeMove`（`develop` 0c2d843 から。この md も未コミットで置いてある）。

**写しで確かめた**（0c2d843 から作った写しで、答えのブランチ `answer-crm`。Debug・Development・Release のビルドでエラー・警告 0。測る Script で 8 台のカメラを Release で走らせて、全部の項目が合った。くわしくは パートE の E-3）。CameraOrbitTest を Play して手で歩くところは、まだ目で見ていない（E-2）。

**2026-11-12 本体へ直接入れた**（ブランチ `feature/CameraRelativeMove`。未コミット）。写しの `0c2d843..answer-crm` の差分（パートA〜D）をそのまま当てた。測る Script（`MoveProbe`）・テストシーン（`MoveProbeTest`）は写しだけで、本体には入れていない。

## 0章 全体の形

### 0.1 今の動き方と困ること

Sandbox の PlayerController は、`GetMoveAxis()` の x をワールドの X、y をワールドの Z にそのまま使っている（W＝+Z・D＝+X）。カメラが +Z を向いて置いてあるシーン（Player.scene・CameraFollowZoneTest）では、W が画面の奥になるので困らない。

Camera Orbit のカメラは追う物のまわりを回るので、カメラが +Z を向いているとは限らない。

```
   上から見た図（ロックオン。ボスが +X の側にいる。カメラは +X を向く）

        ↑ +Z（画面の左）
        │
   ▲ ── ● ─────── ■      → +X（画面の奥）
  カメラ プレイヤー  ボス

   W を押すと +Z へ歩く ＝ 画面の左へ歩く
   D を押すと +X へ歩く ＝ 画面の奥（ボスの方）へ歩く
```

画面のどちらへ歩くかが、カメラが回るたびに変わる。3D のゲームでは、ふつう「W＝画面の奥・D＝画面の右」にする。

### 0.2 2つに分ける

| | 中身 | 誰の仕事 |
|---|---|---|
| ① | カメラから見た向きで動かす（W＝画面の奥、D＝画面の右） | **エンジン**（どの 3D ゲームでも要る。この md） |
| ② | ロックオン中は、ボスを中心に同じ距離で回り、体はボスを向く | ゲーム（操作の決まり。PlayerController。①が入ってから考える） |

①だけでもロックオンは「D で画面の右へ歩く」になるが、カメラの右はボスの方向にほぼ直角なので、D を押し続けると少しずつボスから離れながら回る（円の接線の上を歩くので）。同じ距離で回るのが②。

### 0.3 置き場所：CameraSystem

足りないのは「**今写っているカメラが、地面の上でどちらを向いているか**」。これはカメラにしか分からない（どのカメラを写しているか・Camera Look At の向き・切り替えの補間の途中の向きを持っているのは CameraSystem だけ）。なので `GetMainCamera`・`GetMainCameraEntity` の並びに `GetMainCameraYaw` を足す。

| 置かない所 | 理由 |
|---|---|
| Input（InputManager） | 入力はカメラを知らない方がよい（キー・パッドを読むだけの層にカメラが入ると、UI の入力・メニューにもカメラがついてくる） |
| Character Controller | AI・動く床など、カメラと関係ない物も動かす。カメラの向きを読むかは、動かす側（プレイヤーの Script）が決める |
| Math（MathUtility） | 計算そのもの（`YawToDirection`・`DirectionToYaw`）はもうある。足りないのはカメラの向きの方 |

- Input Action（入力の割り当て）は無くてよい。①は「読んだ後の 2D の値をワールドの向きに直す」所なので、Input Action ができても `GetMoveAxis()` の代わりにそれを渡すだけで、①は変わらない
- 2D の入力 → ワールドの向きの直し方と「切り替えた瞬間」の扱い（0.6）は、パートB の `CameraRelativeInput` に置く。覚えておく値があるので CameraSystem の関数1つにはできない。プレイヤーが 2 人いれば、それぞれが1つ持つ

### 0.4 どの向きを使うか：写している向き（揺れの前）

**Transform の向きではなく、写しているカメラの向き**を使う。

- **Camera Look At の後**：ロックオンのカメラは、Transform は回る中心（プレイヤー）を向き、写す向きは Camera Look At がボスへ向ける。Transform の向きだと、ボスが画面の少し上・Yaw 10 の肩越しで、W がボスの方と少しずれる。写す向きなら、W はいつも画面の奥（ボスの方）
- **切り替えの補間の後**：Ease で切り替えている 0.5 秒の間も、画面に写っている向きで動く（Transform の向きだと、補間の途中で写っていないカメラの向きになる）
- **Timeline の上書きの後**：演出のカメラに写っている間も同じ
- **揺れ（Camera Shake）の前**：揺れは見た目の演出で、進む向きが揺れると、まっすぐ歩けなくなる（ノイズの揺れ 5° のカメラで、写す向きは 4° ぶれるが、進む向きは 0° のまま。E-3）。そのために、写す値（`CameraOutput`）に `steadyRotation`（揺れを足す前の向き）を足し、補間・上書きでも `rotation` と同じように混ぜる

前の話（CameraOrbit.md のチャット）では「Camera Look At・切り替えの補間が入った後」と決めていた。揺れを除くのはこの md で足した判断（Player.scene のカメラにも衝撃の揺れ（Impulse Rotation 2°）が付いていて、除かないと、攻撃が当たるたびに歩く向きが 1° ほどぶれる）。

### 0.5 地面に寝かせる・真下と真上

地面の上の向き ＝ カメラの前の向きの Y を 0 にした向き（`DirectionToYaw` で角度にする）。

- **真下を見るカメラ**は、前の向きを寝かせると長さが 0 になる。そのときは**カメラの上の向き**を寝かせて使う（真下を見ると、画面の上が「奥」）
- **真上を見るカメラ**は、上の向きが「少し見上げたときの奥」の反対を向くので、裏返して使う（少し見上げたときの向きとつながる。真上を見ると地面は写らないので、どちらでもよいが、跳ねないように）
- どちらも長さ 0.001 より短いと使わない（真下・真上から約 0.06° の内側だけ上の向きになる。その外側では、前の向きと上の向きは同じ向きを指す）
- 上の向きも寝かせられない（横に 90° 傾けて真下を見る）・数でない向きのフレームは、**前の向きのまま**
- Camera Component が無いときは、既定のカメラ（+Z を向く）なので 0

### 0.6 カメラを切り替えた瞬間：離すまで前の向き

カメラを切り替えると（Priority・Entity の有効・無効・Timeline）、押している向きのまま、進む向きが急に変わる。例：W で奥へ走っている間に、横から写すカメラへ切り替わると、急に画面の奥（さっきの右）へ曲がる。

**押し続けている間にカメラが切り替わったら、離すまで切り替わる前の向きを使い続ける**（3D のアクションで、固定カメラを切り替えるゲームの決まった手）。

- 「切り替わった」は、**写すカメラの Entity**（`GetMainCameraEntity`）が前のフレームと違うこと。Cut でも Ease でも同じ（Ease の 0.5 秒の間も前の向き）
- 同じカメラが回る間（Camera Orbit の回り込み・右スティックで回す）は、毎フレーム今の向きを使う。いつも使い続けると、スティックでカメラを回しながら歩けなくなる
- 離したら、次に押したときは今のカメラの向き
- 押し始めたフレームに切り替わったときは使い続けない（前のフレームに押していなかったので、前の向きで歩いていない）

覚えておく値（前のフレームのカメラ・使った向き・押していたか）があるので、関数1つではなく**キャラが持つ小さな型**にした（`CameraRelativeInput`）。

- 置き場所は `HTTEngine/Camera/`（`Camera`・`DebugCamera` と同じ。Component でも System でもない、カメラの分野の道具。`Systems/Camera/` は 14 ファイルで、15 を超えるのも避けた）
- 入力を読むのは呼ぶ側（`GetMoveAxis()` を渡す）。型は入力を読まない（Input Action ができたら、そこから読んだ値を渡す）

### 0.7 1フレームの遅れ

Script はカメラ（`CameraSystem::Update`）より先に動くので、Script が読むのは**前のフレームのカメラの向き**。1フレーム（60fps で 0.017 秒）なので、見て分かるずれは無い。

- カメラの後に Script を動かす案はとらない（キャラが動いた後の位置をカメラが追うので、順番は今のままが正しい）
- 切り替えた瞬間のフレームも、まだ前のカメラの向きを読むので、使い続ける（0.6）までの間に向きが飛ぶフレームは無い（E-3 で確かめた）

### 0.8 今あるシーンへの影響

PlayerController を使うシーン（Player.prefab の PlayerController）：

| シーン | カメラ | 動き |
|---|---|---|
| Player.scene（ガイド「はじめてのゲーム作り」のお手本） | GameCamera：Rotation (30, 0, 0)・Camera Follow（View）・Camera Shake（衝撃だけ） | **変わらない**（横の向きが 0 なので W＝+Z のまま。衝撃の揺れも入らない） |
| CameraFollowZoneTest | Rotation (8, 0, 0)・Camera Follow | **変わらない** |
| CameraOrbitTest | 4 台の Camera Orbit | **変わる**（これを直したい。W がいつも画面の奥） |

- Player.prefab の保存の項目は変わらない（`CameraRelativeInput` は Inspector に出さない、実行中だけの値）
- Sandbox の `CharacterKeyboardMover`（ワールドの向きで歩くテスト用。説明にもそう書いてある・今はどのシーンも使っていない）は変えない
- AL4_01 の PlayerController はゲーム側の物なので、この md では変えない（使うなら同じ直し方）
- 学習サイトのガイド段階2（gd-player）に Sandbox の PlayerController のコードを載せているなら、パートC と同じに直す（この md ではやらない）

### 0.9 やらないこと・知っておくこと

- **②ロックオン中にボスのまわりを同じ距離で回る・体をボスへ向ける**：ゲーム側（0.2）
- **大きく倒し直したら、使い続けるのをやめる**（離さなくても、スティックの向きを 45° 以上変えたら今のカメラの向きにする、など）：今は「離すまで」だけ。要るようになったら `CameraRelativeInput` に値を足す
- **TargetForward のカメラで横に歩き続けると、ずっと回る**：カメラはキャラの後ろへ回り込み、カメラの右も回るので、D を押し続けるとキャラは円を描く。カメラから見た向きで動かすと、どのエンジンでもそうなる（Heading Smooth Time を長くすると、円が大きくなる）。キャラの後ろに付くカメラは、横に歩くゲームでは World ＋ スティックで回す方が向く
- 地面が傾いていても（坂）、向きは地面（XZ）の上。坂を上る・下るのは Character Controller が決める
- `CameraSystem.cpp` は 658 行から 692 行になる（500 行を超えたまま）。見直した：増えたのは地面の向きを出す関数1つと、`steadyRotation` を写す 3 行・Get 1つで、コピペは無い

## パートA CameraSystem に「写しているカメラの地面の上の向き」を足す

### A-1 `Systems/Camera/CameraSystem.h`

`GetMainCameraEntity` の後に足す。

```cpp
	// 今使っている Camera Component の Entity（無ければ無効な Handle）。補間の途中は、切り替わった先のカメラ
	static Handle<Entity> GetMainCameraEntity();

	/// <summary>
	/// 画面に写っているカメラが、地面の上でどちらを向いているか（Y のまわりの角度。度。+Z で 0、+X で 90。MathUtility::DirectionToYaw と同じ測り方）
	/// <para>画面の奥が、地面の上のどちらか。キャラを「カメラから見た向き」で動かすのに使う（ふつうは CameraRelativeInput から使う）</para>
	/// <para>Camera Look At・切り替えの補間・Timeline の上書きが入った、写している向き。揺れは入れない（画面が揺れても進む向きが揺れないように）</para>
	/// <para>真下・真上を見るカメラは、画面の上の向きを使う。それも決まらないフレームは前の向きのまま。Camera Component が無ければ 0</para>
	/// <para>Update で決まる値なので、Script（Update より前に動く）が読むのは前のフレームの向き</para>
	/// </summary>
	static float GetMainCameraYaw();
```

### A-2 `Systems/Camera/CameraSystem.cpp`

**1. `CameraOutput` に1行**（`rotation` の後）：

```cpp
	Quaternion rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 向き（ワールド）
	Quaternion steadyRotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 揺れを足す前の向き（ワールド。キャラを動かす向きに使う。画面が揺れても進む向きが揺れないように）
```

**2. `CameraState` に1行**（`output` の後）：

```cpp
	CameraOutput output;                           // 前のフレームに写した値（切り替えの途中でさらに切り替えるときは、ここから始める）
	float mainYaw = 0.0f;                          // 写しているカメラの、地面の上の向き（度。GetMainCameraYaw が返す）
```

**3. `UseDefaultCamera` の最後に1行**（既定のカメラは +Z を向く。Initialize と、Camera Component が無いフレームの両方で通る）：

```cpp
	ApplyProjection(CameraComponent{}.fovY, CameraComponent{}.nearZ, CameraComponent{}.farZ);
	state->mainYaw = 0.0f; // +Z を向く
}
```

**4. 名前の無い namespace の `MakeBackground` の後**（「時間」の大見出しの前）に、大見出しごと足す（0.5）：

```cpp
//=============================================================================
// 地面の上の向き
//=============================================================================
// ===== カメラの向き → 地面の上で、画面の奥がどちらか（Y のまわりの角度。度。決まらなければ nullopt）=====
// 前の向きを地面に寝かせた向き。真下・真上を見ると長さが 0 になるので、そのときはカメラの上の向き（画面の上）を寝かせて使う
// 真下を見るカメラの上は、見下ろす前に向いていた側（画面の奥）。真上を見るカメラの上は、その反対の側なので裏返す（少しだけ傾けた所の向きとつながる）
std::optional<float> CalcGroundYaw(const Quaternion& rotation) {
	constexpr float kMinGroundLength = 0.001f; // これより短い向きは使わない（真下・真上から約 0.06° の内側）
	const Vector3 forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	Vector3 ground = {forward.x, 0.0f, forward.z};
	if (Length(ground) < kMinGroundLength) {
		const Vector3 up = RotateVector({0.0f, 1.0f, 0.0f}, rotation);
		const float side = (forward.y < 0.0f) ? 1.0f : -1.0f; // 真下を見る：上がそのまま奥 ／ 真上を見る：上の反対が奥
		ground = {up.x * side, 0.0f, up.z * side};
	}
	// 上の向きも寝かせられない（横に 90° 傾けて真下を見る）・数でない向きなら、決まらない（否定で書くのは NaN も弾くため）
	if (!(Length(ground) >= kMinGroundLength)) {
		return std::nullopt;
	}
	return MathUtility::DirectionToYaw(ground);
}
```

- `!(Length(ground) >= kMinGroundLength)` は `Length(ground) < kMinGroundLength` と同じに見えるが、長さが NaN（数でない）のときに違う。NaN はどの比べ方も false なので、`<` だと素通りして NaN の角度を返す。`!(>=)` なら弾ける

**5. `EvaluateCamera` の揺れの前に1行**（`// --- 揺れ：カメラの向きから見たずれを足す ---` の次）：

```cpp
	// --- 揺れ：カメラの向きから見たずれを足す ---
	output.steadyRotation = output.rotation;
	const CameraShakeComponent* shake = EntityManager::Get<CameraShakeComponent>(entity);
```

Camera Path・Camera Look At で置き換えた後で、揺れを足す前の向き。停止中・揺れが無いカメラは `rotation` と同じになる。

**6. `LerpOutput` に1行**（`rotation` の次。切り替えの補間・Timeline の上書きで、`rotation` と同じように混ぜる）：

```cpp
	output.rotation = Slerp(from.rotation, to.rotation, t);
	output.steadyRotation = Slerp(from.steadyRotation, to.steadyRotation, t);
```

**7. `CameraSystem::Update` の 5. の `state->mainEntity = …` の次に2行**：

```cpp
	state->mainEntity = (request.has_value() && request->to.IsValid()) ? request->to : best;
	// 地面の上の向き（揺れの前・補間の後。決まらないフレームは前の向きのまま）
	state->mainYaw = CalcGroundYaw(output.steadyRotation).value_or(state->mainYaw);
```

**8. 取得の大見出しの `GetMainCameraEntity` の後に足す**：

```cpp
float CameraSystem::GetMainCameraYaw() {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->mainYaw;
}
```

## パートB CameraRelativeInput（新しい）

### B-1 `Camera/CameraRelativeInput.h`（新しい）

```cpp
#pragma once
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"


/// <summary>
/// 移動の入力（2D）を、画面に写っているカメラから見た地面の向き（ワールド）へ直す。動かすキャラ（プレイヤーの Script など）が1つずつ持つ
/// <para>上（W・スティックの上）＝画面の奥、右（D・スティックの右）＝画面の右。カメラが回り込んでも、いつも画面に合った向きに動く</para>
/// <para>押している間にカメラが切り替わったら、離すまで切り替わる前の向きを使い続ける（切り替えた瞬間に、進む向きが急に変わらないように）</para>
/// <para>カメラが回る（同じカメラのまま向きが変わる）間は、毎フレーム今の向きを使う。使い続けるのは、写すカメラ（Camera Component の Entity）が変わったときだけ</para>
/// </summary>
class CameraRelativeInput {
public:
	/// <summary>
	/// 2D の入力 → ワールドの地面の向き（Y は 0。長さは入力の長さのまま。スティックを半分倒せば半分）
	/// <para>毎フレーム1回呼ぶ（押しているか・カメラが変わったかを、呼ぶたびに前に呼んだときと比べる）</para>
	/// </summary>
	/// <param name="input">x：右が＋、y：奥（上）が＋。InputManager::GetMoveAxis の値</param>
	Vector3 ToWorld(const Vector2& input);

	/// <summary>
	/// 覚えている向き・カメラを忘れる（次に呼んだときは、今のカメラの向きから始める）。ワープ・シーンの切り替えの後など、前の向きを持ち越したくないときに
	/// </summary>
	void Reset();

	// 今、切り替わる前のカメラの向きを使い続けているか
	bool IsHolding() const { return isHolding_; }

	// 前に呼んだときに使った向き（度。CameraSystem::GetMainCameraYaw と同じ測り方）
	float GetYaw() const { return yaw_; }

private:
	Handle<Entity> camera_;   // 前に呼んだときの、写していたカメラ（Camera Component の Entity）
	float yaw_ = 0.0f;        // 前に呼んだときに使った向き（度）
	bool wasPressed_ = false; // 前に呼んだときに押していたか
	bool isHolding_ = false;  // 切り替わる前のカメラの向きを使い続けているか
};
```

- カメラは `Handle<Entity>` で覚える（Script はエンジンの中の物を、生ポインタでフレームをまたいで持たない。coding-style の「ポインタとメモリ」）

### B-2 `Camera/CameraRelativeInput.cpp`（新しい）

```cpp
#include "CameraRelativeInput.h"

#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"


//=============================================================================
// 入力 → ワールドの向き
//=============================================================================
// ===== 2D の入力をワールドの地面の向きへ =====
Vector3 CameraRelativeInput::ToWorld(const Vector2& input) {
	const Handle<Entity> camera = CameraSystem::GetMainCameraEntity();
	const bool isPressed = (input.x != 0.0f || input.y != 0.0f);

	// --- どの向きを使うか ---
	// 離したら、使い続けるのをやめる（次に押したときは、今のカメラの向き）
	// 押し続けている間にカメラが変わったら、前に使った向き（切り替わる前のカメラの向き）を使い続ける
	if (!isPressed) {
		isHolding_ = false;
	} else if (wasPressed_ && camera != camera_) {
		isHolding_ = true;
	}
	if (!isHolding_) {
		yaw_ = CameraSystem::GetMainCameraYaw();
	}
	camera_ = camera;
	wasPressed_ = isPressed;

	// --- 奥の向き × 上 ＋ 右の向き × 右（右の向きは、奥の向きを上から見て時計回りに 90° 回した向き）---
	const Vector3 forward = MathUtility::YawToDirection(yaw_);
	const Vector3 right = MathUtility::YawToDirection(yaw_ + 90.0f);
	return {right.x * input.x + forward.x * input.y, 0.0f, right.z * input.x + forward.z * input.y};
}

// ===== 忘れる =====
void CameraRelativeInput::Reset() {
	camera_ = {};
	yaw_ = 0.0f;
	wasPressed_ = false;
	isHolding_ = false;
}
```

- 右の向き：Yaw は上から見て時計回りに ＋（0 で +Z、90 で +X）なので、奥の向きに 90 を足すと右。Yaw 0 なら奥 (0, 0, 1)・右 (1, 0, 0) で、今の PlayerController（x→X・y→Z）と同じになる
- 押していないかは `== 0` で見る（`GetMoveAxis` はデッドゾーンの内側を 0 にして返す。PlayerController の向く所と同じ聞き方）
- 使い続けている間に、さらに別のカメラへ切り替わっても、最初に使い続けた向きのまま（離すまで）

### B-3 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

`HTTEngine.vcxproj`：`Camera\Camera.cpp`・`Camera\Camera.h` の行の次に1行ずつ。

```xml
    <ClCompile Include="HTTEngine\Camera\Camera.cpp" />
    <ClCompile Include="HTTEngine\Camera\CameraRelativeInput.cpp" />
```

```xml
    <ClInclude Include="HTTEngine\Camera\Camera.h" />
    <ClInclude Include="HTTEngine\Camera\CameraRelativeInput.h" />
```

`HTTEngine.vcxproj.filters`：同じく `Camera.cpp`・`Camera.h` のまとまりの次に。

```xml
    <ClCompile Include="HTTEngine\Camera\CameraRelativeInput.cpp">
      <Filter>HTTEngine\Camera</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Camera\CameraRelativeInput.h">
      <Filter>HTTEngine\Camera</Filter>
    </ClInclude>
```

## パートC Sandbox の PlayerController

### C-1 `project/Sandbox/Source/Scripts/Player/PlayerController.h`

include を1行足し、頭の説明に1行足す。

```cpp
#pragma once
#include <HTTEngine/Camera/CameraRelativeInput.h>
#include <HTTEngine/ECS/Script.h>

//=============================================================================
// プレイヤーの操作（歩く・跳ぶ・進む向きを向く）
//=============================================================================
// 同じ Entity に Collider（Capsule）と Character Controller を付けて使う
// 歩く向きは、画面に写っているカメラから見た向き（W・スティックの上＝画面の奥）
```

「実行中のみ使用」に1行足す（`facingYaw_` の説明も、画面ではなくワールドの向きなので直す）。

```cpp
	// ===== 実行中のみ使用 =====
	float facingYaw_ = 0.0f;         // 今の向き（度。0 で +Z、90 で +X）
	CameraRelativeInput moveInput_; // 移動の入力 → カメラから見た地面の向き（カメラが切り替わったら、離すまで前の向き）
```

`Fields` には足さない（保存しない・Inspector に出さない。Player.prefab は変わらない）。

### C-2 `project/Sandbox/Source/Scripts/Player/PlayerController.cpp`

「歩く」のまとまり：

```cpp
	// --- 歩く：WASD・矢印キー・左スティック（-1〜1）を、カメラから見た地面の向きへ直す ---
	// velocity の X・Z は毎フレーム書く（離すと 0 になって止まる）
	const Vector3 move = moveInput_.ToWorld(InputManager::GetMoveAxis());
	controller->velocity.x = move.x * moveSpeed_;
	controller->velocity.z = move.z * moveSpeed_;
```

「向く」のまとまりの頭の2行（`move` が Vector3 になったので、`y` → `z`）：

```cpp
	if (move.x != 0.0f || move.z != 0.0f) {
		const float targetYaw = MathUtility::DirectionToYaw(move);
```

- `ToWorld` は「早期リターン」（ポーズ・ヒットストップで `dt <= 0`）の後で呼ぶ。止まっている間は呼ばれないが、離したかは次に呼んだときの入力で決まるので困らない

## パートD Reference

### D-1 `Docs/Reference/10-Camera.md`

「追う物のまわりを回るカメラ（Camera Orbit）」の節の最後（コードの例の後）の次、「注視点（Camera Look At）」の前に、節ごと足す。

````markdown
## カメラから見た向きで動かす（`CameraRelativeInput`）

移動の入力（`InputManager::GetMoveAxis()`）を、画面に写っているカメラから見た地面の向きへ直す。**W・スティックの上＝画面の奥、D・右＝画面の右**。カメラが回り込んでも（Camera Orbit・ロックオン）、いつも画面に合った向きに動く。動かすキャラの Script が1つずつ持つ。

```cpp
#include <HTTEngine/Camera/CameraRelativeInput.h>

class PlayerController : public Script {
	// …
	CameraRelativeInput moveInput_; // メンバに持つ（前のフレームを覚えるので、毎回作らない）
};

void PlayerController::Update() {
	const Vector3 move = moveInput_.ToWorld(InputManager::GetMoveAxis()); // Y は 0。長さは入力のまま（スティックを半分倒せば半分）
	controller->velocity.x = move.x * moveSpeed_;
	controller->velocity.z = move.z * moveSpeed_;
	if (move.x != 0.0f || move.z != 0.0f) {
		facingYaw_ = MathUtility::DirectionToYaw(move); // 進む向きを向く
	}
}
```

- 押している間にカメラが切り替わったら（Priority・Entity の有効・無効・Timeline のカメラのトラック）、**離すまで切り替わる前のカメラの向き**で動く（切り替えた瞬間に、進む向きが急に変わらない）。同じカメラが回る間は、毎フレーム今の向き。使い続けているかは `IsHolding()`。
- 使う向きは写している向き（Camera Look At・切り替えの補間・Timeline の上書きが入った後）。揺れ（Camera Shake）は入れない（画面が揺れても、まっすぐ歩ける）。
- 真下・真上を見るカメラは、画面の上の向きを奥にする。
- `ToWorld` は毎フレーム1回呼ぶ。ワープ・やり直しの後に前の向きを持ち越したくないときは `Reset()`。
- 読むのは前のフレームのカメラの向き（Script はカメラより先に動く）。1フレームなので、見て分かるずれは無い。
- 向きだけ欲しいとき（コンパスの UI など）は `CameraSystem::GetMainCameraYaw()`（度。+Z で 0、+X で 90。`MathUtility::YawToDirection` で向きにできる）。
- AI・動く床など、カメラと関係ない物は使わない（ワールドの向きで動かす）。
- ロックオン中に相手のまわりを同じ距離で回る・体を相手へ向けるのは、ゲームの操作の決まりなので Script で。`ToWorld` だけだと、D で画面の右（相手の方向にほぼ直角）へ歩くので、少しずつ相手から離れながら回る。
- Camera Orbit の Heading が TargetForward のカメラで横に歩き続けると、カメラが後ろへ回り込むたびに右も回るので、キャラは円を描く。

```cpp
// ✗ だめな例
const Vector2 move = InputManager::GetMoveAxis();
controller->velocity.x = move.x * moveSpeed_; // ✗ 入力をそのままワールドの X・Z にすると、カメラが回ったときに W が画面の奥にならない
controller->velocity.z = move.y * moveSpeed_;
```
````

### D-2 `Docs/Reference/11-Input.md`

1つ目のコードの後の箇条書き（「キーボードとパッドは、…」の前）に1行足す。

```markdown
- `GetMoveAxis()` は入力の値（x が右・y が上）で、ワールドの向きではない。キャラをカメラから見た向き（W＝画面の奥）で動かすなら `CameraRelativeInput` に通す（[10 カメラ](10-Camera.md) の「カメラから見た向きで動かす」）。
```

### D-3 `Docs/Reference.md`（索引）

10 カメラ・11 入力の行：

```markdown
| **カメラを置く・切り替える・追いかける・回り込む（ロックオン）・揺らす・カメラから見た向きで動かす** | [10 カメラ](Reference/10-Camera.md) |
| キー・マウス・パッド（**Game ビューのマウス位置・移動の入力**。カメラから見た向きへ直すのは 10 章） | [11 入力](Reference/11-Input.md) |
```

## パートE 確かめ方

### E-1 ビルド

Debug・Development・Release の 3 つ（`MSBuild.exe project/HTTEngine.slnx -m -p:Configuration=Debug -p:Platform=x64` の Configuration を変えて）。警告 0 になる。

### E-2 Sandbox で見る

Sandbox を起動して、Project から `CameraOrbitTest` を開き、Play。

1. **1（World）**：Q・E でカメラを回してから W を押すと、いつも画面の奥へ歩く（前はいつも +Z）。回しながら W を押し続けても、画面の奥へ曲がっていく
2. **2（TargetForward）**：W で奥へまっすぐ歩ける。A・D を押し続けると、カメラが後ろへ回り込むので円を描く（0.9）
3. **3（ロックオン）**：W でボスへ向かって歩く（ボスが画面の少し上の真ん中なので、ほぼボスの方）。S で離れる。A・D で画面の左右へ歩き、カメラがボスの反対側へ回り込む（少しずつ離れながら回るのは②で直す）
4. **4（肩越し）**：3 と同じ。Yaw 25 でも W は画面の奥（ボスの方ではない）
5. **切り替え**：W を押し続けたまま 1 → 3 を押すと、0.5 秒かけてカメラが移る間も、離すまで前と同じ向き（ワールドで同じ向き）へ歩き続ける。離して押し直すと、新しいカメラの画面の奥へ歩く
6. Player.scene を開いて Play：前と同じ動き（W＝+Z・画面の奥）

### E-3 写しで確かめたこと（測る Script）

写しの `test-crm` ブランチ（答えの `answer-crm` ＋ 測る Script `Sandbox/Source/TestOnly/MoveProbe` ＋ テストシーン `MoveProbeTest`）。歩く物（Walker）に `MoveProbe` を付け、8 台のカメラを 2 秒ずつ順に写しながら（Blend は全部 Ease 0.5 秒）、回る入力（長さ 0.8）で `CameraRelativeInput` を使って歩かせた。区間の頭 0.6 秒は前の区間から押し続け（＝押したまま切り替える）、0.6〜0.75 秒は離す。Release で走らせ、毎フレーム「前のフレームに写したカメラのビュー行列」で測った。

| カメラ | 中身 | W のずれ | D のずれ | 決まった向きとの差 | 揺れ | 使い続けた |
|---|---|---|---|---|---|---|
| World30 | Camera Orbit：World・Yaw 30・Pitch 20 | 0.00002° | 0.00003° | — | 0° | （最初のカメラ） |
| TargetForward | TargetForward・Heading Smooth Time 0.6（歩く向きを向く） | 0.00002° | 0.00004° | — | 0° | 35 フレーム・ずれ 0° |
| LockOn | LookAt・Yaw 10 ＋ Camera Look At（Boss・(0, 0.15)） | 0° | 0.00003° | — | 0° | 35 フレーム・ずれ 0° |
| Shoulder | LookAt・Yaw 25・Distance 3.5・Pitch 8 ＋ Camera Look At（(-0.2, 0.1)） | 0.00001° | 0.00002° | — | 0° | 36 フレーム・ずれ 0° |
| TopDown | Rotation (90, 45, 0)（真下） | 0° | 0° | 0°（45°） | — | 36 フレーム・ずれ 0° |
| LookUp | Rotation (-90, -60, 0)（真上） | 0.00001° | 0.00002° | 0.00001°（-60°） | — | 36 フレーム・ずれ 0° |
| ShakeOrbit | World・Yaw -40 ＋ Camera Shake（ノイズの回転 5°） | — | — | 0°（Camera Orbit の currentYaw） | 写す向きは最大 3.96° ぶれる | 36 フレーム・ずれ 0° |
| PlayerLike | Player.scene の GameCamera と同じ形（(30, 0, 0)・Follow View・衝撃の揺れ）＋ 0.4 秒ごとに AddImpulse(0.6) | — | — | 0°（0°） | 写す向きは最大 1.24° ぶれる | 35 フレーム・ずれ 0° |

- 「W のずれ」：`ToWorld({0, 1})` をビュー行列でカメラの右・上・前へ直し、右の成分から測った角度（0 なら画面の左右へずれない）。World30〜Shoulder は W のカメラの前の成分がどれも 0.94 以上で、画面の奥へ向かう。TopDown は W がカメラの上（画面の上）そのもの（1.000）。LookUp は W がカメラの下（1.000。0.5 のとおり、少し見上げたときと同じ側）
- 「D のずれ」：`ToWorld({1, 0})` がカメラの右からずれた角度
- 「決まった向きとの差」：`GetMainCameraYaw()` と、置いた向き・Camera Orbit の `currentYaw`（揺れの前の向き）との差
- 「揺れ」：写したカメラ（揺れ入り）の前の向きを寝かせた角度と、`GetMainCameraYaw()` の差。揺れが写す向きにだけ入り、進む向きに入らないこと
- 「使い続けた」：押したまま切り替えた後、離すまでの間に `IsHolding()` が true で、使った向きが切り替えたフレームの向きと同じだったフレーム。抜け（使い続けなかった）・離した後も使い続けた、はどのカメラも 0 フレーム。切り替えの補間（0.5 秒）は使い続けた 0.6 秒の中に入る
- どのカメラも、使い続けていない間の向き＝`GetMainCameraYaw()`（差 0）、向きの長さ＝入力の長さ（差 0）
- 測る Script はエンジンに入れない（写しだけ）
