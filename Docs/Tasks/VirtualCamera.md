参考：
- 目標にしている仕組み：Unity Cinemachine の [Cinemachine Brain](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineBrain.html)
- 追いかけ：[Position Composer](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachinePositionComposer.html)
- 注視点：[Rotation Composer](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineRotationComposer.html)
- ノイズの揺れ：[Basic Multi Channel Perlin](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineBasicMultiChannelPerlin.html)
- 衝撃の揺れ：[Cinemachine Impulse](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineImpulse.html)
- カメラごとのポストエフェクト：[Volume Settings](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineVolumeSettings.html)
- パーリンノイズの重みの式（Ken Perlin, Improving Noise）：https://mrl.cs.nyu.edu/~perlin/paper445.pdf
- 減衰振動の式：https://en.wikipedia.org/wiki/Harmonic_oscillator
- 減衰振動をそのまま解いて進める方法（Ryan Juckett, Damped Springs）：https://www.ryanjuckett.com/damped-springs/
- premultiplied alpha：https://en.wikipedia.org/wiki/Alpha_compositing
- `std::optional`：https://en.cppreference.com/w/cpp/utility/optional
- `std::any_of`：https://en.cppreference.com/w/cpp/algorithm/all_any_none_of
- `std::initializer_list`：https://en.cppreference.com/w/cpp/utility/initializer_list
- `std::numbers`：https://en.cppreference.com/w/cpp/numeric/constants

# カメラ段階1：Virtual Camera の基本

複数のカメラを置いて、Priority で切り替えられるようにする。切り替えは位置・向き・視野角をなめらかに補間し、カメラごとのポストエフェクトの値も一緒に補間する。あわせて、平面上の追いかけ、注視点、カメラの揺れを足す。

[VectorShape.md](VectorShape.md)・[GameScreen.md](GameScreen.md) の後に写す（検証はその順で行った）。`ui.BeginDisabled` と `GameScreen` を使うので、この2つが先に要る。

パートは **A → B → C → D の順に写す**。A の後と D の後でビルドが通ることを確かめた。B・C の途中はビルドが通らない。E は確認用のシーンと Script、F は確認の手順。

作業の前に、VectorShape.md・GameScreen.md を写したブランチからブランチを切る。

```bash
git switch -c feature/VirtualCamera
```

## 0章 全体の形

### 0.1 流れ

```
                         Camera Follow
                              │ Transform を動かす（これだけが Transform を書き換える）
                              ▼
Transform（カメラの本来の位置・向き）
    │ 読むだけ
    ▼
EvaluateCamera：カメラ1台分の値を作る
    ・Camera Look At … 向きを置き換える
    ・Camera Volume  … ポストエフェクトの上書きを作る
    ・Camera Shake   … 位置・向きにずれを足す
    │
    ▼
ApplyBlend：切り替えの途中なら、切り替え前の値と混ぜる
    │
    ▼
ApplyOutput：Camera クラスへ写す ＋ SceneRenderer へ上書きを渡す
```

揺れと注視点と補間は、最後に写す値にだけ入る。Transform は書き換えないので、揺れが終われば必ず元の位置に戻る。

1フレームの順番は、今の `EngineSystems::Update` の場所のまま。

| 順番 | 処理 |
|---|---|
| 1 | 物理で動かして、ワールド行列を計算し直す |
| 2 | `CameraSystem::UpdateFollow`：Camera Follow が Transform を動かす |
| 3 | `CameraSystem::Update`：カメラを選ぶ → 注視点と揺れを進める → 写す値を作る |
| 4 | `DepthOfFieldSystem::Update` などが、カメラの位置を使う |
| 5 | 描画：`SceneRenderer` が上書きをポストエフェクトへ混ぜる |

### 0.2 使い方

Hierarchy の ＋ → Camera でカメラを複数置き、Priority と Inspector の項目を決める。

| Component | 役目 |
|---|---|
| Camera | 今までの値 ＋ Blend（Style・Time・Ease）＋ Time Mode |
| Camera Follow | 今までの値 ＋ Plane（XY・XZ・View） |
| Camera Look At（新しい） | 注視点。Entity か、ワールドの座標 |
| Camera Shake（新しい） | ノイズの揺れと、衝撃の受け方 |
| Camera Volume（新しい） | カメラごとのポストエフェクト。チェックを入れた項目だけ使う |

ゲームのコードからは次のように使う。

```cpp
// 切り替え（Priority を変えるだけ。Camera の Blend の設定で補間される）
gameObject.GetComponent<CameraComponent>()->priority = 100;

// 衝撃（強さは最初に振れる幅。m）
CameraSystem::AddImpulse(0.4f);                     // 向きは画面の面の中でランダム
CameraSystem::AddImpulse(0.4f, {1.0f, 0.0f, 0.0f}); // ワールドの +X へ

// 補間の途中か
if (CameraSystem::IsBlending()) { }

// 画面に写っている位置・向き（注視点・揺れ・補間が入っている）
Camera* camera = CameraSystem::GetMainCamera();
```

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| Virtual Camera の型 | 新しい型を作らず、`CameraComponent` をそのまま使う | 今も Priority で選ぶ形になっている。今のシーンがそのまま読める |
| 補間の設定の場所 | 切り替わる先のカメラの Camera Component | Cinemachine の Brain のような Entity をシーンに増やさなくてよい |
| Blend の初期値 | Cut | 今のシーンの動きを変えない |
| Transform の扱い | カメラの本来の位置・向き。動かすのは Camera Follow だけ | 揺れ・注視点・補間が Transform に残らない |
| 切り替えの途中でさらに切り替える | 前のフレームに写した値を始点に止める | 絵が飛ばない。止まった値を始点にするので、途中で速さは変わる |
| 切り替え前のカメラ | 1回目の切り替えでは、補間の間も値を取り直す | 追いかけているカメラの絵が、補間の間に置いていかれない |
| 時間 | Camera の Time Mode 1つで、切り替え・追いかけ・注視点・揺れをまとめて決める | 1台の中で時間が分かれると、調整しづらい |
| Real の時間 | `Time::GetRealDeltaTime` を足した。エディタの一時停止だけ止まる | `GetUnscaleDeltaTime` のままだと、エディタの一時停止でも動く |
| Plane の XY | 今の式のまま | 同じ入力で 1500 フレーム動かして、位置が全部一致した |
| Plane の XZ | カメラが写している地面の点を「画面の真ん中」にする | 傾いたカメラでは、カメラの真下は画面の真ん中ではない（1.2） |
| XZ の横・縦 | カメラの向きから毎フレーム作る | 向きを固定しないゲームでも、Offset・Dead Zone が画面の向きに付いてくる |
| Plane の View | カメラの右・上で動かし、奥行きも保つ | 奥行きを保たないと、カメラが回ったときに追う物が画面から外れる（1.3） |
| 向きの読み先 | Camera Follow は Transform の向きだけを読み、Camera Look At の向きは読まない | 見る物と追う物が同じだと、向きだけ回って位置が動かなくなる |
| 注視点の Target が空のとき | Offset をワールドの座標として見る | Entity を置かずに、決まった点を見られる |
| 衝撃の揺れ | ばねの式で進める | 何回受けても足すだけで済む。`deltaTime` の大きさで結果が変わらない（1.5） |
| 衝撃の強さの単位 | 最初に振れる幅（m） | Frequency・Damping を変えても、強さの意味が変わらない |
| 衝撃を受けるカメラ | 有効な Camera Shake 全部 | 切り替えの途中でも、両方のカメラが揺れる |
| `AddImpulse` の中身 | 頼みを溜めて、次の `Update` で Component へ渡す | Inspector の Test Impulse から呼んでも、Component を書き換えない（Undo に積まれない） |
| Volume の項目 | 項目ごとにチェックを持つ | 指定しない項目は全体の設定のまま |
| Bloom の Mode・ToneMap の Mode | 持たない | 途中の値が無いので、補間できない |
| 上書きの持ち方 | 値 × 効き具合で持つ（`PostOverride`） | 全体の設定を読まずに補間できる（1.6） |
| 全体の設定で切ってある効果 | 強さ 0 として混ぜる | カメラが値を持っていれば掛かり、補間の間も 0 からなめらかに強くなる |
| Scene ビュー | Camera Volume を掛けない | 別のカメラから見るので、ピントが合わない |
| Fog | 持たない | ポストエフェクトではなく、カメラの定数バッファの値。今回の頼みに入っていない |
| ファイル | `CameraFollow`・`CameraShake`・`CameraVolume` に分けた | `CameraSystem.cpp` を、カメラを選ぶ・補間する・写す仕事だけにする |

### 0.4 あとの段階への備え

| 段階 | 今の形で残した物 |
|---|---|
| カメラ2（Timeline） | 切り替えの入口は `StartBlend` の1か所。Timeline のカメラのトラックは、ここへ「このカメラへ切り替える」を足せばよい |
| カメラ2（Timeline） | Camera Volume の `weight`、Camera Shake の `noiseGain`、Camera の `fovY` は全部ふつうの `float` で、毎フレーム読み直す。外から書くだけで次のフレームに効く |
| UI 段階2（カーブ） | `CameraBlendStyle` に Curve を足せば、`ApplyBlend` の `Ease::Apply` の所をカーブに差し替えられる |
| UI 段階5（アニメーション） | Describe の関数は全部の項目をいつも通す。隠さず灰色にしているので、`###名前` から値の場所を探す係を足しても全部届く |

## 1章 解説

### 1.1 切り替えの途中でさらに切り替える

ふつうの補間は「A から B へ t の割合」で計算する。途中で C へ切り替えると、補間の相手が変わるので絵が飛ぶ。

そこで、切り替えるたびに「前のフレームに実際に写した値」を始点として覚える。

```
A → B の途中（t = 0.4）で C へ切り替えた
  始点 ＝ 前のフレームに写した値（A と B の間の値）
  終点 ＝ C
  t は 0 からやり直す
```

t = 0 の絵は前のフレームの絵と同じなので、飛ばない。`CameraBlend::from` がこの始点で、`CameraState::output` が前のフレームに写した値。

1回目の切り替えだけは、切り替え前のカメラ（`fromEntity`）が生きているので、毎フレーム値を取り直す。2回目以降の切り替えの始点は「2つのカメラの間の値」で、取り直す相手がいないので止まった値のまま使う。

### 1.2 XZ の「画面の真ん中」

XY はカメラが +Z を向いている前提なので、カメラの位置がそのまま画面の真ん中になる。

傾けて見下ろすカメラでは、カメラの真下は画面の真ん中ではない。

```
カメラ
  ＼
    ＼   前の向き
      ＼
───────●──── 地面の高さ
   真下   ↑ 画面の真ん中に写る点
```

`CalcGroundCenter` は、カメラの前の向きへ伸ばした線と、追う物の高さの面が交わる点を出す。

```
距離 ＝ (追う物の高さ − カメラの高さ) ÷ 前の向きの Y
交点 ＝ カメラの位置 ＋ 前の向き × 距離
```

この点を追う物へ近づけるので、シーンに置いた通りの見え方のまま追いかける。Offset に傾きの分を入れなくてよい。

Bounds も、傾いたカメラの写す範囲は台形なので、四隅の線を1本ずつ地面と交わらせて測る（`CalcGroundExtent`）。範囲はワールドの X・Z で決まっているので、カメラが回っていても範囲は回らない。

XZ の横・縦の向きは次のように作る。

```
横 ＝ カメラの右を地面に寝かせて、長さ 1 にした向き
縦 ＝ 横 × ワールドの上（左手座標系では、右 × 上 ＝ 前）
```

カメラの前の向きを寝かせる方法だと、真下を見るカメラで長さが 0 になる。右は真下を見ても水平のままなので、右から作る。

### 1.3 View は奥行きも保つ

View の面はカメラの右・上なので、カメラと一緒に回る。横・縦だけ追うと、次のことが起きる。

```
カメラが 90 度回る → 奥にいた物が真横になる → 横へ追う → 追う物がカメラのすぐ横に来る
```

写しで試すと、300 度回した所で追う物がカメラの後ろへ出た。そこで、追いかけ始めの奥行きを `viewDepth` に覚えて、前の向きにも動かす。

```cpp
move += forward * ((depth - follow.viewDepth) * CalcSmoothRate(deltaTime, follow.smoothTime));
```

カメラが回ると、追う物のまわりを回る動きになる。

### 1.4 パーリンノイズ

乱数をそのまま足すと、毎フレーム別の方向へ飛ぶので、揺れではなく震えになる。パーリンノイズは、時間に対してなめらかに変わる乱数。

```
  傾き g0        傾き g1
    ／             ＼
───●───────────────●───  整数の位置（ここでの値は必ず 0）
   0      time      1
```

1. 整数の位置ごとに、ランダムな傾きを決める（`MakeGradient`）。同じ整数なら、いつも同じ傾きになる。
2. 左の傾きの直線と右の傾きの直線の高さを、なめらかな重みで混ぜる（`CalcNoise`）。

重みは `6t^5 - 15t^4 + 10t^3`。端で傾きも曲がり方も 0 になるので、整数の境目が目立たない。

パーリンノイズは整数の位置で必ず 0 になる。全部のチャンネルを同じ時間で読むと、1秒に1回、全部が同時に 0 になって揺れが止まって見える。なので `kChannelShift` でチャンネルごとに読む位置をずらす。

### 1.5 ばねの式

衝撃は、ばねを叩いたときの動き（減衰振動）で作る。ω はばねの強さ、ζ は減衰比。

```
x(t) = e^(-ζωt) × (x0 cos(ω't) + K sin(ω't))
K    = (v0 + ζω x0) / ω'
ω'   = ω √(1 - ζ²)
```

`x0`・`v0` が今のずれと速さ。この式に `deltaTime` を入れて、次のずれと速さを出す。少しずつ足し込む方法（オイラー法）と違って、`deltaTime` の大きさで揺れ方が変わらず、大きな `deltaTime` でも暴れない。

衝撃は速さに足すだけなので、続けて受けても、溜める配列が要らない。

Inspector の強さを「最初に振れる幅」にするには、速さを逆算する。止まっている所へ速さ `v0` を与えると、次の形で動く。

```
x(t) = v0 / ω' × e^(-ζωt) × sin(ω't)
```

この山は `tan(ω't) = ω' / (ζω)` の所で、高さは `v0 / ω × e^(-ζωt)`。高さが `strength` になる `v0` を足す。

```cpp
const float peakTime = std::atan2(spring.dampedOmega, decay) / spring.dampedOmega;
const float speed = strength * shake.impulseGain * spring.omega * std::exp(decay * peakTime);
```

ずれと速さが十分小さくなったら 0 にして止める（`kStopOffset`・`kStopVelocity`）。指数で減るだけだと、ちょうど 0 にはならない。

### 1.6 値 × 効き具合で持つ

カメラ A だけが Intensity を持っていて、カメラ B へ切り替えるとき、補間の間は「A の値 → 全体の設定」へ動いてほしい。ふつうに書くと、毎フレーム全体の設定の値を読むことになる。

`PostOverride` は「カメラの値 × 効き具合」と「効き具合」の2つを持つ。

```
最後の値 ＝ 全体の値 × (1 − weight) ＋ value
value   ＝ カメラの値 × weight
```

上書きしない項目は `value = 0, weight = 0`、上書きする項目は `value = カメラの値, weight = 1`。補間は、`value` と `weight` をそれぞれ Lerp するだけでよい。

```
A：value = 2.0, weight = 1　　B：value = 0, weight = 0
t = 0.5 → value = 1.0, weight = 0.5
最後の値 ＝ 全体の値 × 0.5 ＋ 1.0 ＝ （全体の値 と 2.0 の真ん中）
```

切り替えの途中でさらに切り替えても同じ形のまま補間できる。半透明の色を「色 × α」で持つ premultiplied alpha と同じ考え方。

### 1.7 std::optional

`std::optional<T>` は「T の値がある」か「何も無い」かを持つ型。ポインタの引数で返すより、「無いことがある」が型で分かる。

```cpp
std::optional<Vector3> point = FindLookAtPoint(lookAt);
if (point.has_value()) {
	Vector3 value = *point; // 中身を読む
}
return std::nullopt;        // 何も無いを返す
```

`CalcVolumeFocusDistance` の `std::optional<float>(depthOf(*lookAtPoint))` は、条件演算子の両側の型をそろえるために書いている。

### 1.8 std::any_of と std::initializer_list

`std::any_of(始め, 終わり, 条件)` は、条件に合う物が1つでもあれば true を返す。`std::initializer_list<T>` は `{a, b, c}` と書いた値の並びを受け取る型。

```cpp
camera.HasAny({PostParam::LensDistortion, PostParam::LensAberration}); // どれかが効いているか
```

---

## パートA 描画の受け口

カメラの本体より先に、時間・向き・ポストエフェクトの受け口を作る。ここまでで見た目は何も変わらない。**ここでビルドが通る。**

### A-1 `Time/EngineTime.h`

`GetUnscaleDeltaTime` の下に足す。

変更後：

```cpp
	static float GetUnscaleDeltaTime() { return instance_->deltaTime_; }
	// 止めない時間の秒（スロー・ヒットストップ・ポーズは掛からない。エディタの一時停止のときだけ0）。カメラのように、ゲームの時間が止まっても動かしたい物に
	static float GetRealDeltaTime() { return instance_->deltaTime_ * instance_->pauseScale_; }
```

### A-2 `Components/Core/TransformComponent.h`

`// ===== 回転を角度（度）で読み書きする` の前に足す。

変更後：

```cpp
// ワールドの向き（親の回転も入る。大きさは入らない）。Unity の transform.rotation と同じ
// 行列から取り出す角度は X → Y → Z の順（ラジアン）で Transform の Z → X → Y（度）と違うので、回転の行列にしてからクォータニオンにする
inline Quaternion GetWorldRotation(const TransformComponent& transform) {
	const Transform world = DecomposeAffineMatrix(transform.worldMatrix);
	return MakeQuaternionFromMatrix(MakeAffineMatrix({1.0f, 1.0f, 1.0f}, world.rotation, {0.0f, 0.0f, 0.0f}));
}
```

### A-3 `ECS/ComponentUI.h`

`EndDisabled` の下に足す。

変更後：

```cpp
	virtual void EndDisabled() {}
	// 直前のチェックの右に、次の項目を並べる（保存には関係しない）
	virtual void SameLineAfterCheck() {}
```

### A-4 `ECS/ComponentUI.cpp`

`InspectorComponentUI` が2つある。`#ifdef USE_IMGUI` の中の、ImGui を使う方の `EndDisabled` の下に足す。

変更後：

```cpp
	void EndDisabled() override { ImGui::EndDisabled(); }
	void SameLineAfterCheck() override {
		const float checkWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - checkWidth); // 値の欄の右端を、ほかの行とそろえる
	}
```

### A-5 `Graphics/PostEffect/PostOverride.h`（新しいファイル）

```cpp
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>

// カメラごとのポストエフェクトの値を、全体の設定へ混ぜるための値
// CameraSystem が毎フレーム作って SceneRenderer へ渡す。全体の設定の値（GlobalVariables・Depth Of Field Component）を読まずに補間できる

/// <summary>
/// 1つの項目の上書き
/// <para>value は「カメラの値 × weight」。掛けた後の値で持つと、2つの上書きを補間するときに value と weight を別々に Lerp するだけで済む（色の premultiplied alpha と同じ）</para>
/// </summary>
struct PostOverride {
	float value = 0.0f;  // カメラの値 × weight
	float weight = 0.0f; // 効き具合（0：全体の設定のまま 〜 1：カメラの値）

	// 全体の設定の値 base に混ぜた値
	float Apply(float base) const { return base * (1.0f - weight) + value; }
};

/// <summary>
/// カメラごとに持てる項目
/// </summary>
enum class PostParam {
	FocusDistance,  // 被写界深度：ピントの合う奥行き
	FocusRange,     // 被写界深度：ぼけない幅
	FarTransition,  // 被写界深度：奥のぼけ始めから一番ぼけるまで
	NearTransition, // 被写界深度：手前のぼけ始めから一番ぼけるまで
	MaxBlur,        // 被写界深度：一番ぼけた所の半径
	BloomThreshold, // ブルーム：しきい値
	BloomKnee,      // ブルーム：しきい値付近の柔らかさ
	BloomIntensity, // ブルーム：強さ
	LensDistortion, // レンズ：歪み
	LensAberration, // レンズ：色収差
	LensRadialBlur, // レンズ：放射ブラー
	LensVignette,   // レンズ：端の暗さ
	Exposure,       // 露出
	Count,          // 項目の数
};

/// <summary>
/// 全部の項目の上書き（初期値は「どれも上書きしない」）
/// </summary>
struct PostOverrides {
	std::array<PostOverride, static_cast<size_t>(PostParam::Count)> items;

	PostOverride& operator[](PostParam param) { return items[static_cast<size_t>(param)]; }
	const PostOverride& operator[](PostParam param) const { return items[static_cast<size_t>(param)]; }
	// 全体の設定の値 base に、その項目の上書きを混ぜた値
	float Apply(PostParam param, float base) const { return (*this)[param].Apply(base); }
	// その項目のどれかが効いているか
	bool HasAny(std::initializer_list<PostParam> params) const {
		return std::any_of(params.begin(), params.end(), [this](PostParam param) { return (*this)[param].weight > 0.0f; });
	}
};

/// <summary>
/// 2つの上書きの補間。t は 0〜1 に収める（行き過ぎるイージングでも、効き具合が 0〜1 の外へ出ないように）
/// </summary>
inline PostOverrides LerpPostOverrides(const PostOverrides& from, const PostOverrides& to, float t) {
	const float rate = std::clamp(t, 0.0f, 1.0f);
	PostOverrides result;
	for (size_t i = 0; i < result.items.size(); ++i) {
		result.items[i].value = from.items[i].value + (to.items[i].value - from.items[i].value) * rate;
		result.items[i].weight = from.items[i].weight + (to.items[i].weight - from.items[i].weight) * rate;
	}
	return result;
}
```

### A-6 `Graphics/PostEffect/DepthOfFieldPass.h`

include を足す。

変更後：

```cpp
#include "HTTEngine/Graphics/Pipeline/RootSignatureManager.h"
#include "HTTEngine/Graphics/PostEffect/PostOverride.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
```

`DepthOfFieldSettings` の最後に足す。

変更後：

```cpp
	float maxBlur = 8.0f;        // 一番ぼけた所の、ぼけの半径（ピクセル。高さ 720 の画面で）
	PostOverride focusOverride;  // カメラごとのピントの奥行きの上書き（focusPoint はビューごとに奥行きが変わるので、測った後に混ぜる）
};
```

### A-7 `Graphics/PostEffect/DepthOfFieldPass.cpp`

変更前：

```cpp
	if (!settings.hasFocusPoint) {
		return settings.focusDistance;
	}
	// ビュー行列の3列目が、カメラの前の向き（SceneRenderer::UploadCameraCB の right・up と同じ取り方）
	const Matrix4x4& view = camera.GetViewMatrix();
	const Vector3 forward = {view.m[0][2], view.m[1][2], view.m[2][2]};
	return (std::max)(Dot(settings.focusPoint - camera.GetTranslation(), forward), 0.0f);
```

変更後：

```cpp
	if (!settings.hasFocusPoint) {
		return settings.focusOverride.Apply(settings.focusDistance);
	}
	// ビュー行列の3列目が、カメラの前の向き（SceneRenderer::UploadCameraCB の right・up と同じ取り方）
	const Matrix4x4& view = camera.GetViewMatrix();
	const Vector3 forward = {view.m[0][2], view.m[1][2], view.m[2][2]};
	return settings.focusOverride.Apply((std::max)(Dot(settings.focusPoint - camera.GetTranslation(), forward), 0.0f));
```

### A-8 `Graphics/PostEffect/PostProcess.h`

include を足す。

変更後：

```cpp
#include "HTTEngine/Graphics/PostEffect/LensPass.h"
#include "HTTEngine/Graphics/PostEffect/PostOverride.h"
```

変更前：

```cpp
	void Prepare(RenderTexture& scene, bool useBloom = true);       // useBloom が false ならブルームを掛けない（ポストエフェクトを切ったビュー）
```

変更後：

```cpp
	void Prepare(RenderTexture& scene, const PostOverrides& cameraOverrides, bool useBloom = true); // cameraOverrides はカメラごとの上書き。useBloom が false ならブルームを掛けない（ポストエフェクトを切ったビュー）
```

変更後：

```cpp
	void ApplyGV();
	void ApplyCameraOverrides(const PostOverrides& camera); // ApplyGV が読んだ全体の設定へ、カメラの上書きを混ぜる
```

`source_` の上に足す。

変更後：

```cpp
	bool isBloomActive_ = true;       // ブルームを掛けるか（全体の設定か、カメラの上書き）
	bool isLensActive_ = true;        // レンズを掛けるか（全体の設定か、カメラの上書き）
	RenderTexture* source_ = nullptr; // Prepare が決めた、レンズへの入力
```

### A-9 `Graphics/PostEffect/PostProcess.cpp`

変更前：

```cpp
void PostProcess::Prepare(RenderTexture& scene, bool useBloom) {
	ApplyGV(); // ImGuiでいじった値がその場で効くように毎フレーム反映する
	source_ = &scene;
```

変更後：

```cpp
void PostProcess::Prepare(RenderTexture& scene, const PostOverrides& cameraOverrides, bool useBloom) {
	ApplyGV(); // ImGuiでいじった値がその場で効くように毎フレーム反映する
	ApplyCameraOverrides(cameraOverrides);
	source_ = &scene;
```

変更前：

```cpp
	if (useBloom && kBloomEnabled != 0) {
```

変更後：

```cpp
	if (useBloom && isBloomActive_) {
```

変更前：

```cpp
	lens_.Render(*source_, rect, useLens && kLensEnabled != 0);
```

変更後：

```cpp
	lens_.Render(*source_, rect, useLens && isLensActive_);
```

`void PostProcess::ApplyGV() {` の前に足す。

変更後：

```cpp
//=============================================================================
// カメラごとの上書き
//=============================================================================
// 全体の設定で切ってある効果は「強さ 0」として混ぜる（上書きしたカメラの間だけ掛かり、補間の間は 0 からなめらかに強くなる）
void PostProcess::ApplyCameraOverrides(const PostOverrides& camera) {
	// --- ブルーム ---
	const bool isBloomEnabled = (kBloomEnabled != 0);
	bloom_.kThreshold = camera.Apply(PostParam::BloomThreshold, bloom_.kThreshold);
	bloom_.kKnee = camera.Apply(PostParam::BloomKnee, bloom_.kKnee);
	bloom_.kIntensity = camera.Apply(PostParam::BloomIntensity, isBloomEnabled ? bloom_.kIntensity : 0.0f);
	isBloomActive_ = isBloomEnabled || camera.HasAny({PostParam::BloomIntensity});
	// --- レンズ ---
	const bool isLensEnabled = (kLensEnabled != 0);
	lens_.kDistortion = camera.Apply(PostParam::LensDistortion, isLensEnabled ? lens_.kDistortion : 0.0f);
	lens_.kAberration = camera.Apply(PostParam::LensAberration, isLensEnabled ? lens_.kAberration : 0.0f);
	lens_.kRadialBlur = camera.Apply(PostParam::LensRadialBlur, isLensEnabled ? lens_.kRadialBlur : 0.0f);
	lens_.kVignette = camera.Apply(PostParam::LensVignette, isLensEnabled ? lens_.kVignette : 0.0f);
	isLensActive_ = isLensEnabled || camera.HasAny({PostParam::LensDistortion, PostParam::LensAberration, PostParam::LensRadialBlur, PostParam::LensVignette});
	// --- 露出（切る設定が無いので、いつも混ぜる）---
	lens_.kExposure = camera.Apply(PostParam::Exposure, lens_.kExposure);
}

void PostProcess::ApplyGV() {
```

`ApplyGV` が毎回 GlobalVariables の値を読み直すので、ビューごとに上書きを掛けても値は積み上がらない。

### A-10 `Graphics/Renderer/SceneRenderer.h`

`SceneViewOptions` の最後に足す。

変更後：

```cpp
	bool sceneHidden = true;  // Hierarchy の目で隠した物（DrawTarget::isSceneHidden）を描く（Scene ビューだけ false）
	bool cameraVolume = true; // カメラごとのポストエフェクト（Camera Volume）を掛ける（Scene ビューだけ false。別のカメラから見るので、ピントが合わない）
```

`SetDepthOfField` の下に足す。

変更後：

```cpp
	static void SetDepthOfField(const DepthOfFieldSettings& settings) { instance_->depthOfField_ = settings; }

	// --- カメラごとのポストエフェクト ---
	// メインカメラの上書き（CameraSystem が毎フレーム書く。Camera Volume が無ければ「上書きなし」を書く）
	static void SetCameraPostOverrides(const PostOverrides& overrides) { instance_->cameraPostOverrides_ = overrides; }
```

`GetViewDepthOfField` の宣言の前に足す。

変更後：

```cpp
	// このビューに掛けるカメラの上書き（SceneViewOptions で切ってあれば「上書きなし」）
	static PostOverrides GetViewPostOverrides();
	// このビューに掛ける被写界深度（View メニューで切ってあれば「ぼかさない」）
	static DepthOfFieldSettings GetViewDepthOfField();
```

`depthOfField_` の下に足す。

変更後：

```cpp
	DepthOfFieldSettings depthOfField_; // 被写界深度の値（初期値はぼかさない）
	PostOverrides cameraPostOverrides_; // カメラごとのポストエフェクトの上書き（初期値は上書きなし）
```

### A-11 `Graphics/Renderer/SceneRenderer.cpp`

`GetViewDepthOfField` を、次の2つの関数に置き換える。

変更前：

```cpp
// ===== このビューに掛ける被写界深度 =====
DepthOfFieldSettings SceneRenderer::GetViewDepthOfField() {
	DepthOfFieldSettings settings = instance_->depthOfField_;
	settings.enabled = settings.enabled && instance_->viewOptions_.depthOfField;
	return settings;
}
```

変更後：

```cpp
// ===== このビューに掛けるカメラの上書き =====
PostOverrides SceneRenderer::GetViewPostOverrides() { return instance_->viewOptions_.cameraVolume ? instance_->cameraPostOverrides_ : PostOverrides{}; }

// ===== このビューに掛ける被写界深度 =====
DepthOfFieldSettings SceneRenderer::GetViewDepthOfField() {
	DepthOfFieldSettings settings = instance_->depthOfField_;
	const PostOverrides camera = GetViewPostOverrides();
	settings.focusOverride = camera[PostParam::FocusDistance];
	settings.focusRange = camera.Apply(PostParam::FocusRange, settings.focusRange);
	settings.farTransition = camera.Apply(PostParam::FarTransition, settings.farTransition);
	settings.nearTransition = camera.Apply(PostParam::NearTransition, settings.nearTransition);
	// 全体で切ってある被写界深度は「ぼけ 0」として混ぜる（Max Blur を上書きしたカメラの間だけ掛かる）
	settings.maxBlur = camera.Apply(PostParam::MaxBlur, settings.enabled ? settings.maxBlur : 0.0f);
	if (camera.HasAny({PostParam::MaxBlur})) {
		settings.enabled = settings.maxBlur > 0.0f;
	}
	settings.enabled = settings.enabled && instance_->viewOptions_.depthOfField;
	return settings;
}
```

`Prepare` の呼び出しを2か所直す。

変更前：

```cpp
	instance_->postProcess_->Prepare(*scene, mode != PostEffectMode::None); // 張り替える処理はここで全部済ませる
```

変更後：

```cpp
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides(), mode != PostEffectMode::None); // 張り替える処理はここで全部済ませる
```

変更前：

```cpp
	instance_->postProcess_->Prepare(*scene);
```

変更後：

```cpp
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides());
```

### A-12 `Editor/Viewport/EditorViewport.cpp`

変更前：

```cpp
	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .sceneHidden = false};
```

変更後：

```cpp
	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .sceneHidden = false, .cameraVolume = false};
```

ここでビルドが通る。絵は今までと同じ。

---

## パートB Component

ここから D の終わりまで、ビルドは通らない。

### B-1 `Components/Camera/CameraComponent.h`（全体を差し替え）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/Math/Easing.h"

// ゲームの絵を撮るカメラ。Entityに付けるComponent（カテゴリ：Camera）
// 位置・向きは Transform（親の下に置けば一緒に動く）。ここに持つのは写り方と、このカメラへ切り替わるときの動き方
// 実際のカメラ（Camera クラス）は CameraSystem が1つ持ち、毎フレーム一番 priority の高い Camera の値を写す
// Transform は「カメラの本来の位置・向き」。注視点・揺れ・切り替えの補間は写す値にだけ入れて、Transform は書き換えない

inline constexpr float kMinCameraFovY = 1.0f;   // 視野角の下限（度）
inline constexpr float kMaxCameraFovY = 179.0f; // 視野角の上限（度）。180度以上は透視投影で作れない

/// <summary>
/// このカメラへの切り替え方
/// </summary>
enum class CameraBlendStyle {
	Cut,  // すぐ切り替える
	Ease, // Blend Time の秒をかけて、イージングで補間する
};

/// <summary>
/// カメラが動くのに使う時間
/// </summary>
enum class CameraTimeMode {
	Game, // ゲームの時間（スローで遅くなり、ヒットストップ・ポーズで止まる）
	Real, // 止まらない時間（スロー・ヒットストップ・ポーズの間も同じ速さで動く。エディタの一時停止では止まる）
};


/// <summary>
/// カメラ
/// </summary>
struct CameraComponent {
	float fovY = 45.0f;   // 縦の視野角（度）。小さいほど望遠
	float nearZ = 0.1f;   // これより近い物は映らない
	float farZ = 1000.0f; // これより遠い物は映らない
	int priority = 0;     // 有効な Camera が複数あるとき、大きい方を使う（同じなら先に見つけた方）
	// --- このカメラへ切り替わるときの動き方 ---
	CameraBlendStyle blendStyle = CameraBlendStyle::Cut; // 切り替え方
	float blendTime = 1.0f;                              // 補間にかける秒（Ease のとき）
	Ease::Type blendEase = Ease::Type::InOutCubic;       // 補間の速さの変え方（Ease のとき）
	// --- 時間 ---
	CameraTimeMode timeMode = CameraTimeMode::Game; // 切り替え・追いかけ・注視点・揺れに使う時間
};

static_assert(std::is_trivially_copyable_v<CameraComponent>);
```

### B-2 `Components/Camera/CameraFollowComponent.h`（全体を差し替え）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// カメラが Entity を追いかける。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// どの面の上で動かすかは Plane で決める。カメラの向きは変えない
// 動かすのは CameraSystem::UpdateFollow（再生中だけ。物理で動いた後・カメラの絵を決める前）。停止中は置いた位置のまま
// 画面の真ん中に置きたい点（focus）＝ 追う物の位置 ＋ Offset ＋ 追い方（Mode）で決まるずれ。カメラは、focus が「遊び」の外へ出た分だけ、なめらかに追う

/// <summary>
/// 追い方（focus をどこに置くか）。Inspector では、選んだ追い方の値だけが出る
/// </summary>
enum class CameraFollowMode {
	Center,    // 追う物を真ん中に（Offset の分だけずらす）
	LookAhead, // 進む向きの先を見せる（追う物の速さ × Look Ahead Time だけ先を真ん中に。止まると真ん中に戻る）
};

/// <summary>
/// カメラを動かす面。Offset・Dead Zone・Max Lag・Look Ahead の X・Y は、下の「横」と「縦」の向きになる
/// </summary>
enum class CameraFollowPlane {
	XY,   // ワールドの X・Y（横から見るゲーム）。横 = X・縦 = Y
	XZ,   // 地面の X・Z（見下ろしのゲーム）。横 = カメラの右、縦 = カメラの前を地面に寝かせた向き（カメラが回ると一緒に回る）
	View, // カメラから見た位置を保つ（どの向きのカメラでも）。横 = カメラの右、縦 = カメラの上。奥行きも、追いかけ始めの長さに保つ
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
	Vector2 deadZone = {0.5f, 1.5f};                     // 遊び（m）。画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない
	bool groundSnapY = true;              // true：追う物が地面にいる間は、縦の遊びを使わずに合わせる（XY のとき。跳んでも画面は揺れず、段に乗ると上がる）。追う物の Rigidbody の isGrounded を見る
	float smoothTime = 0.15f;             // 追いつくまでの時間の目安（秒。0 ならすぐ合わせる）
	Vector2 maxLag = {6.0f, 4.0f};        // 遅れの上限（m）。focus と画面の真ん中が、これより離れない（速く動いても画面から出ないように）
	bool useBounds = false;               // true：画面の端が boundsMin〜boundsMax の外を写さない（ステージの端でカメラが止まる。XY・XZ のとき）
	Vector2 boundsMin = {-10.0f, -10.0f}; // 写してよい範囲の左下（XY はワールドの X・Y、XZ はワールドの X・Z。追う物の奥行き・高さで測る）
	Vector2 boundsMax = {100.0f, 30.0f};  // 写してよい範囲の右上
	// --- Look Ahead のときだけ使う ---
	Vector2 lookAheadTime = {0.4f, 0.0f}; // 何秒先を見せるか（先読み ＝ 追う物の速さ × これ。Y を 0 にすると、縦には先読みしない）
	Vector2 lookAheadMax = {3.0f, 2.0f};  // 先読みの上限（m）。吹っ飛びのような速い動きで、先へ行きすぎないように
	float lookAheadSmoothTime = 0.3f;     // 先読みが変わるまでの時間の目安（秒。速さが急に変わっても、画面がガクッと動かないように）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isFollowing = false;                     // 追いかけ始めたか（false なら、次のフレームで focus へすぐ合わせる）
	float viewDepth = 0.0f;                       // View のとき、保つ奥行き（m。追いかけ始めの、カメラの前の向きに測った長さ）
	Vector2 currentLookAhead = {0.0f, 0.0f};      // 今の先読み（m。横・縦が＋。Center の間は 0 へ戻っていく）
	Vector3 lastTargetPosition = {0.0f, 0.0f, 0.0f}; // 前のフレームの、追う物の位置（速さを出すのに使う。カメラが回ると横・縦の向きが変わるので、ワールドの位置で持つ）
};

static_assert(std::is_trivially_copyable_v<CameraFollowComponent>);
```

### B-3 `Components/Camera/CameraLookAtComponent.h`（新しいファイル）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// カメラに注視点を見させる。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// 変えるのは写す向きだけで、Transform の向きは変えない（Camera Look At を外すと、置いた向きに戻る）
// 停止中も見る向きが Game ビューに出る。なめらかに動くのは再生中だけ

/// <summary>
/// 注視点（Unity の Cinemachine の Rotation Composer の小さい版）
/// </summary>
struct CameraLookAtComponent {
	bool enabled = true;                  // false の間は Transform の向きのまま
	EntityRef target;                     // 見る Entity（Hierarchy からドラッグ）。空なら Offset をワールドの座標として見る
	Vector3 offset = {0.0f, 0.0f, 0.0f};  // 見る点のずれ（m。ワールドの向き）
	float smoothTime = 0.0f;              // 見る点が追いつくまでの時間の目安（秒。0 ならすぐ合わせる）

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	bool isAiming = false;                      // 見始めたか（false なら、次のフレームで見る点へすぐ合わせる）
	Vector3 currentPoint = {0.0f, 0.0f, 0.0f};  // 今見ている点（ワールド）
};

static_assert(std::is_trivially_copyable_v<CameraLookAtComponent>);
```

### B-4 `Components/Camera/CameraShakeComponent.h`（新しいファイル）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/Math/Vector3.h"

// カメラの揺れ。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// ノイズ：ずっと続く小さな揺れ（手持ちカメラ・地鳴りなど）
// 衝撃：CameraSystem::AddImpulse で受けて、ばねのように揺れながら収まる揺れ
// どちらも写す値にだけ足して、Transform は書き換えない（揺れが無くなれば、必ず元の位置・向きに戻る）
// 動くのは再生中だけ。時間は Camera の Time Mode に従う

/// <summary>
/// カメラの揺れ（Unity の Cinemachine の Noise と Impulse Listener の小さい版）
/// </summary>
struct CameraShakeComponent {
	bool enabled = true; // false の間は揺らさない
	// --- ノイズ ---
	Vector3 noisePosition = {0.0f, 0.0f, 0.0f}; // 位置の揺れ幅（m。カメラの右・上・前）
	Vector3 noiseRotation = {0.0f, 0.0f, 0.0f}; // 向きの揺れ幅（度。X：上下の首振り Y：左右の首振り Z：傾き）
	float noiseFrequency = 1.0f;                // 1秒に揺れる回数の目安
	float noiseGain = 1.0f;                     // 揺れ幅に掛ける倍率（0 で止まる。少しずつ変えると、揺れを強めたり弱めたりできる）
	// --- 衝撃 ---
	float impulseGain = 1.0f;      // 受けた強さに掛ける倍率（0 なら受けない）
	float impulseFrequency = 8.0f; // 1秒に揺れる回数
	float impulseDamping = 0.2f;   // 収まる速さ（減衰比。0 に近いほど長く揺れ、1 に近いほどすぐ止まる）
	float impulseRotation = 2.0f;  // 1m ずれたときに一緒に回す角度（度）。遠くの景色も揺れて見えるように

	// --- 実行中だけの値（CameraSystem が書く。保存しない）---
	float noiseTime = 0.0f;                        // ノイズを読む位置（秒 × 回数）
	Vector3 impulseOffset = {0.0f, 0.0f, 0.0f};    // 衝撃の今のずれ（m。カメラの右・上・前）
	Vector3 impulseVelocity = {0.0f, 0.0f, 0.0f};  // 衝撃の今の速さ（m/秒）
};

static_assert(std::is_trivially_copyable_v<CameraShakeComponent>);
```

### B-5 `Components/Camera/CameraVolumeComponent.h`（新しいファイル）

```cpp
#pragma once
#include <type_traits>

// カメラごとのポストエフェクトの値。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// チェックを入れた項目だけ、このカメラの値を使う。外した項目は全体の設定（Settings → PostEffect・Depth Of Field Component）のまま
// カメラが切り替わるときは、位置・向きと一緒に同じ速さで値も補間する
// Bloom の Mode・ToneMap の Mode のように途中の値が無い項目は持たない（全体の設定のまま）

/// <summary>
/// 上書きできる値1つ
/// </summary>
struct CameraVolumeValue {
	bool isOverride = false; // true なら、このカメラの値を使う
	float value = 0.0f;      // このカメラの値
};

/// <summary>
/// ピントの合わせ方
/// </summary>
enum class CameraVolumeFocus {
	Global,       // 全体の設定（Depth Of Field Component の Focus）のまま
	Distance,     // カメラから Focus Distance（m）の奥行き
	LookAtTarget, // Camera Look At が見ている点の奥行き
	FollowTarget, // このカメラの Camera Follow の Target の奥行き
};


/// <summary>
/// カメラごとのポストエフェクトの値（Unity の Cinemachine の Volume Settings の小さい版）
/// </summary>
struct CameraVolumeComponent {
	bool enabled = true; // false の間は全部全体の設定のまま
	float weight = 1.0f; // 全部の項目の効き具合（0：全体の設定のまま 〜 1：このカメラの値）
	// --- Depth Of Field ---
	CameraVolumeFocus focus = CameraVolumeFocus::Global; // ピントの合わせ方
	float focusDistance = 10.0f;                         // カメラからピントの合う所までの奥行き（m。Distance のとき）
	CameraVolumeValue focusRange = {false, 3.0f};        // ピントの前後この奥行き（m）までは、ぼけない
	CameraVolumeValue farTransition = {false, 40.0f};    // その奥から、一番ぼけるまでの奥行き（m）
	CameraVolumeValue nearTransition = {false, 4.0f};    // その手前から、一番ぼけるまでの奥行き（m）
	CameraVolumeValue maxBlur = {false, 8.0f};           // 一番ぼけた所の、ぼけの半径（ピクセル。0 ならぼかさない）
	// --- Bloom ---
	CameraVolumeValue bloomThreshold = {false, 1.0f}; // これより明るい所だけ光る
	CameraVolumeValue bloomKnee = {false, 0.25f};     // しきい値付近の柔らかさ
	CameraVolumeValue bloomIntensity = {false, 0.9f}; // 合成の強さ（0 なら光らない）
	// --- Lens ---
	CameraVolumeValue lensDistortion = {false, 0.0f}; // 画面端の歪み
	CameraVolumeValue lensAberration = {false, 0.0f}; // 色収差
	CameraVolumeValue lensRadialBlur = {false, 0.0f}; // 中心へ向かって流れるブラー
	CameraVolumeValue lensVignette = {false, 0.35f};  // 端の暗さ
	// --- ToneMap ---
	CameraVolumeValue exposure = {false, 1.0f}; // 露出（2 で1段明るい・0.5 で1段暗い）
};

static_assert(std::is_trivially_copyable_v<CameraVolumeComponent>);
```

---

## パートC 部品

`Systems/Camera` に6つのファイルを足す。

### C-1 `Systems/Camera/CameraFollow.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Components/Camera/CameraFollowComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Follow の中身（CameraSystem から使う）
/// </summary>
namespace CameraFollow {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraFollowComponent& value);

// カメラ1つ分の追いかけ。Transform を動かしたら true
bool Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime);

// なめらかに近づけるときの、1フレームに進む割合（残りの差 × これ だけ進む）。smoothTime が 0 なら 1（すぐ合わせる）
float CalcSmoothRate(float deltaTime, float smoothTime);
} // namespace CameraFollow
```

### C-2 `Systems/Camera/CameraFollow.cpp`（新しいファイル）

今の `CameraSystem.cpp` の Camera Follow の部分を、こちらへ移す形。式は 1.2・1.3 を見ながら写す。

```cpp
#include "CameraFollow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/Physics/RigidbodyComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kMinVerticalForward = 0.001f; // 前の向きの Y の大きさがこれより小さければ、カメラは水平（地面と交わらない）
constexpr float kMinFlatLength = 0.0001f;     // 地面に寝かせた向きの長さの2乗がこれより小さければ、向きが決まらない

/// <summary>
/// カメラを動かす面の、横と縦の向き（どちらも長さ1）
/// </summary>
struct FollowAxes {
	Vector3 horizontal; // 横（画面の右に当たる向き）
	Vector3 vertical;   // 縦（画面の上に当たる向き）

	// ワールドの点を、面の上の座標にする
	Vector2 ToPlane(const Vector3& point) const { return {Dot(point, horizontal), Dot(point, vertical)}; }
};

/// <summary>
/// 画面の四隅が、真ん中の点からどれだけ離れた所を写すか（ワールドの2つの軸で測る。ApplyBounds に渡す）
/// </summary>
struct ViewExtent {
	Vector2 min; // 真ん中より小さい方へのずれ（マイナス）
	Vector2 max; // 真ん中より大きい方へのずれ（プラス）
};

//=============================================================================
// 面の計算
//=============================================================================
// ===== 面の、横と縦の向き =====
FollowAxes MakeFollowAxes(CameraFollowPlane plane, const Quaternion& rotation) {
	const Vector3 right = RotateVector({1.0f, 0.0f, 0.0f}, rotation);
	switch (plane) {
	case CameraFollowPlane::XY:
		return {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
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
	return {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
}

// ===== 画面の真ん中が写している、高さ height の点 =====
// 傾けて見下ろすカメラでは、カメラの真下は画面の真ん中ではない。カメラの前の向きへ伸ばした線と、高さ height の面が交わる点が真ん中
Vector3 CalcGroundCenter(const Vector3& cameraPosition, const Vector3& forward, float height) {
	if (std::abs(forward.y) < kMinVerticalForward) {
		return cameraPosition; // 水平のカメラは地面と交わらないので、カメラの位置をそのまま使う
	}
	const float distance = (height - cameraPosition.y) / forward.y;
	return (distance > 0.0f) ? cameraPosition + forward * distance : cameraPosition;
}

// ===== 地面で画面の四隅が写す範囲（X・Z）=====
// 傾いたカメラの写す範囲は台形なので、四隅の線を1本ずつ地面と交わらせて測る。地平線より上を写す隅は地面と交わらないので数えない
std::optional<ViewExtent> CalcGroundExtent(const Vector3& cameraPosition, const Quaternion& rotation, const Vector3& center, float fovY) {
	const float halfHeight = std::tan(fovY * 0.5f);
	const float halfWidth = halfHeight * GameScreen::GetAspectRatio();
	const std::array<Vector3, 4> corners = {{
	    {-halfWidth, -halfHeight, 1.0f},
	    {halfWidth, -halfHeight, 1.0f},
	    {-halfWidth, halfHeight, 1.0f},
	    {halfWidth, halfHeight, 1.0f},
	}};
	std::optional<ViewExtent> extent;
	for (const Vector3& corner : corners) {
		const Vector3 direction = RotateVector(corner, rotation);
		if (direction.y >= -kMinVerticalForward && cameraPosition.y >= center.y) {
			continue; // 上から見ていて、この隅は地平線より上を向いている
		}
		if (direction.y <= kMinVerticalForward && cameraPosition.y < center.y) {
			continue; // 下から見ていて、この隅は地平線より下を向いている
		}
		const Vector3 hit = cameraPosition + direction * ((center.y - cameraPosition.y) / direction.y);
		const Vector2 offset = {hit.x - center.x, hit.z - center.z};
		if (!extent.has_value()) {
			extent = ViewExtent{offset, offset};
		}
		extent->min = {(std::min)(extent->min.x, offset.x), (std::min)(extent->min.y, offset.y)};
		extent->max = {(std::max)(extent->max.x, offset.x), (std::max)(extent->max.y, offset.y)};
	}
	return extent;
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

// ===== 1つの軸で、遊びの外へ出た分だけ真ん中を動かす（focus が「真ん中 ± 遊び」の中なら、真ん中のまま）=====
float ApplyDeadZone(float center, float focus, float deadZone) {
	if (focus > center + deadZone) {
		return focus - deadZone; // 右（上）へはみ出した：はみ出した分だけ追う
	}
	if (focus < center - deadZone) {
		return focus + deadZone; // 左（下）へはみ出した
	}
	return center; // 遊びの中：動かない
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

// ===== カメラの視野角（ラジアン）=====
float GetFovY(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return std::clamp(camera ? camera->fovY : CameraComponent{}.fovY, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraFollow::Describe(ComponentUI& ui, CameraFollowComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと追わない（置いた位置のまま）"));
	ui.Field("Target###target", value.target, Tip("追う Entity（Hierarchy からドラッグ）。再生中だけ追う。動かすのは Plane の面の上だけ（カメラの向きは置いたまま）"));
	ui.Field("Plane###plane", value.plane, Tip("カメラを動かす面。XY：ワールドの X・Y（横から見る）/ XZ：地面（見下ろし。カメラの向きが変わると横・縦も回る）/ View：カメラの右・上（どの向きのカメラでも。奥行きも保つ）"));
	ui.Field("Mode###mode", value.mode, Tip("追い方。Center：追う物を真ん中に / LookAhead：進む向きの先を見せる（速さ × Time。止まると真ん中に戻る）"));
	// --- 選んだ追い方の値だけを見せる（保存も、使っている方だけが書かれる。Collider の Shape と同じ）---
	switch (value.mode) {
	case CameraFollowMode::Center:
		break; // Center だけの値は無い
	case CameraFollowMode::LookAhead:
		ui.Field("Look Ahead Time###lookAheadTime", value.lookAheadTime, AtLeast(0.0f).Tip("何秒先を見せるか（先読み ＝ 追う物の速さ × これ）。X 0.4 なら 7m/s で 2.8m 先。Y を 0 にすると、縦には先読みしない"));
		ui.Field("Look Ahead Max###lookAheadMax", value.lookAheadMax, AtLeast(0.0f).Tip("先読みの上限（m）。吹っ飛びのような速い動きで、先へ行きすぎないように"));
		ui.Field("Look Ahead Smooth Time###lookAheadSmoothTime", value.lookAheadSmoothTime, AtLeast(0.0f).Tip("先読みが変わるまでの時間の目安（秒）。速さが急に変わっても（振り返る・止まる）、画面がガクッと動かない。0 ならすぐ変わる"));
		break;
	}
	ui.Separator("Follow");
	ui.Field("Offset###offset", value.offset, Tip("追う物から、画面の真ん中をどれだけずらすか（m）。X が横、Y が縦。XY で Y を上げると、足元が広く見える"));
	ui.Field("Dead Zone###deadZone", value.deadZone, AtLeast(0.0f).Tip("遊び（m）。画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない（跳ぶたびに画面が上下しないように）"));
	ui.BeginDisabled(value.plane != CameraFollowPlane::XY);
	ui.Field("Ground Snap Y###groundSnapY", value.groundSnapY, Tip("追う物が地面にいる間は、縦の遊びを使わずに合わせる（跳んでも画面は揺れず、段に乗ると上がる・降りると下がる）。追う物に Rigidbody が要る。Plane が XY のときだけ"));
	ui.EndDisabled();
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる"));
	ui.Field("Max Lag###maxLag", value.maxLag, AtLeast(0.0f).Tip("遅れの上限（m）。速く動いても、追う物が画面の真ん中からこれより離れない"));
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
float CameraFollow::CalcSmoothRate(float deltaTime, float smoothTime) { return (smoothTime > 0.0f) ? 1.0f - std::exp(-deltaTime / smoothTime) : 1.0f; }

bool CameraFollow::Update(Handle<Entity> entity, CameraFollowComponent& follow, float deltaTime) {
	// --- 早期リターン ---
	const Handle<Entity> targetEntity = EntityManager::Find(follow.target);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(targetEntity);
	if (transform == nullptr || targetTransform == nullptr) {
		return false; // 追う物が入っていない・もう居ない
	}
	const Vector3 cameraPosition = GetWorldPosition(*transform);
	const Vector3 targetPosition = GetWorldPosition(*targetTransform);
	// 向きは毎フレーム Transform から読む（カメラを回しても、横・縦が画面の向きに付いてくる）
	const Quaternion rotation = GetWorldRotation(*transform);
	const FollowAxes axes = MakeFollowAxes(follow.plane, rotation);
	const Vector2 target = axes.ToPlane(targetPosition);

	// --- 1. 先読み（追い方で決まる focus のずれ）---
	// 速さは位置の変化から出す（Rigidbody が無い物・スクリプトで動かす物でも使える。壁に止められている間は 0）
	// 目標の先読みへ、指数でなめらかに近づける（速さはフレームごとにガタつくので、そのままだと画面が震える）
	if (follow.isFollowing) {
		const Vector2 velocity = (target - axes.ToPlane(follow.lastTargetPosition)) / deltaTime;
		const Vector2 wanted = CalcWantedLookAhead(follow, velocity);
		follow.currentLookAhead += (wanted - follow.currentLookAhead) * CalcSmoothRate(deltaTime, follow.lookAheadSmoothTime);
	}
	follow.lastTargetPosition = targetPosition;

	// --- 2. 画面の真ん中に置きたい点（focus）---
	const Vector2 focus = target + follow.offset + follow.currentLookAhead;

	// --- 3. 新しい真ん中 ---
	// XY・View はカメラの位置がそのまま真ん中。XZ は、カメラが写している地面の点を真ん中にする
	const Vector3 forward = RotateVector({0.0f, 0.0f, 1.0f}, rotation);
	const float depth = Dot(targetPosition - cameraPosition, forward); // 追う物の奥行き（カメラの前の向きに測った長さ）
	const bool isGround = (follow.plane == CameraFollowPlane::XZ);
	const Vector3 viewCenter = isGround ? CalcGroundCenter(cameraPosition, forward, targetPosition.y) : cameraPosition;
	const Vector2 oldCenter = axes.ToPlane(viewCenter);
	Vector2 center = oldCenter;
	if (!follow.isFollowing) {
		// 追いかけ始め（Play・シーンを読んだ直後）：置いた位置から流れてこないように、すぐ focus へ
		center = focus;
		follow.viewDepth = depth;
		follow.isFollowing = true;
	} else {
		// 遊びの外へ出た分だけ追う。Y は、追う物が地面にいる間は遊びを使わずに合わせる（Ground Snap Y）
		// 遊びだけだと、跳んで遊びの上へはみ出した分カメラが上がり、着地しても遊びの中なので戻らない（跳ぶたびに少しずつずれていく）
		const RigidbodyComponent* targetBody = EntityManager::Get<RigidbodyComponent>(targetEntity);
		const bool isSnapY = (follow.plane == CameraFollowPlane::XY) && follow.groundSnapY && targetBody != nullptr && targetBody->isGrounded;
		const Vector2 wanted = {ApplyDeadZone(center.x, focus.x, follow.deadZone.x), isSnapY ? focus.y : ApplyDeadZone(center.y, focus.y, follow.deadZone.y)};
		// なめらかに近づける（指数補間。1フレームに「残りの差 × t」だけ進む）
		center += (wanted - center) * CalcSmoothRate(deltaTime, follow.smoothTime);
		// 遅れすぎないように縛る（吹っ飛びのような速い動きでも、追う物が画面から出ない）
		center.x = std::clamp(center.x, focus.x - follow.maxLag.x, focus.x + follow.maxLag.x);
		center.y = std::clamp(center.y, focus.y - follow.maxLag.y, focus.y + follow.maxLag.y);
	}

	// --- 4. ステージの端で止める（画面の端が、範囲の外を写さない）---
	if (follow.useBounds) {
		switch (follow.plane) {
		case CameraFollowPlane::XY: {
			// 追う物の奥行きでの、画面の半分の高さ・幅（カメラは +Z を向いている前提）。半分の高さ ＝ 距離 × tan(縦の視野角の半分)
			const float halfHeight = std::abs(targetPosition.z - cameraPosition.z) * std::tan(GetFovY(entity) * 0.5f);
			const float halfWidth = halfHeight * GameScreen::GetAspectRatio();
			center.x = ApplyBounds(center.x, -halfWidth, halfWidth, follow.boundsMin.x, follow.boundsMax.x);
			center.y = ApplyBounds(center.y, -halfHeight, halfHeight, follow.boundsMin.y, follow.boundsMax.y);
			break;
		}
		case CameraFollowPlane::XZ:
			// 範囲はワールドの X・Z で決まっているので、いったんワールドへ戻して縛り、面の座標へ戻す（カメラが回っていても、範囲は回らない）
			if (const std::optional<ViewExtent> extent = CalcGroundExtent(cameraPosition, rotation, viewCenter, GetFovY(entity))) {
				Vector3 moved = viewCenter + axes.horizontal * (center.x - oldCenter.x) + axes.vertical * (center.y - oldCenter.y);
				moved.x = ApplyBounds(moved.x, extent->min.x, extent->max.x, follow.boundsMin.x, follow.boundsMax.x);
				moved.z = ApplyBounds(moved.z, extent->min.y, extent->max.y, follow.boundsMin.y, follow.boundsMax.y);
				center = axes.ToPlane(moved);
			}
			break;
		case CameraFollowPlane::View:
			break; // 面がカメラと一緒に回るので、範囲を持たない
		}
	}

	// --- 5. 動いた量を Transform へ（親の下に置いたカメラでも、ワールドで動いた量だけ動く）---
	Vector3 move = axes.horizontal * (center.x - oldCenter.x) + axes.vertical * (center.y - oldCenter.y);
	// View の面はカメラと一緒に回るので、横・縦だけでは奥行きがずれていく（90 度回ると、奥にいた物が真横になる）。保つ奥行きへ戻す
	if (follow.plane == CameraFollowPlane::View) {
		move += forward * ((depth - follow.viewDepth) * CalcSmoothRate(deltaTime, follow.smoothTime));
	}
	if (LengthSq(move) == 0.0f) {
		return false;
	}
	EntityManager::TranslateWorld(entity, move);
	return true;
}
```

### C-3 `Systems/Camera/CameraShake.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Components/Camera/CameraShakeComponent.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// 揺れのずれ（カメラの向きから見た値）
/// </summary>
struct CameraShakeOffset {
	Vector3 position = {0.0f, 0.0f, 0.0f}; // 位置のずれ（m。カメラの右・上・前）
	Vector3 rotation = {0.0f, 0.0f, 0.0f}; // 向きのずれ（度。オイラー角）
};

/// <summary>
/// Camera Shake の中身（CameraSystem から使う）
/// </summary>
namespace CameraShake {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraShakeComponent& value);

// 揺れを deltaTime 秒進める
void Update(CameraShakeComponent& shake, float deltaTime);

// 今のずれ（読むだけ。Transform には足さず、写す値にだけ足す）
CameraShakeOffset CalcOffset(const CameraShakeComponent& shake);

/// <summary>
/// 衝撃を受ける
/// </summary>
/// <param name="strength">最初に振れる幅（m）。止まっているカメラなら、ちょうどこの幅まで振れる</param>
/// <param name="direction">最初に振れる向き（カメラの右・上・前）。長さは1でなくてよい</param>
void AddImpulse(CameraShakeComponent& shake, float strength, const Vector3& direction);
} // namespace CameraShake
```

### C-4 `Systems/Camera/CameraShake.cpp`（新しいファイル）

```cpp
#include "CameraShake.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr float kMinDamping = 0.01f;       // 減衰比の下限（0 だと永遠に揺れる）
constexpr float kMaxDamping = 0.99f;       // 減衰比の上限（1 以上は振れずに戻る別の式になる）
constexpr float kMinImpulseFrequency = 0.1f; // 衝撃の回数の下限
constexpr float kStopOffset = 0.0001f;     // ずれ（m）と
constexpr float kStopVelocity = 0.001f;    // 速さ（m/秒）がこれより小さくなったら、0 にして止める
constexpr float kTestStrength = 0.3f;      // Inspector の試しのボタンの強さ（m）
constexpr float kChannelShift = 0.37f;     // チャンネルごとにノイズを読む位置をずらす量（パーリンノイズは整数の所で必ず 0 になるので、全部が同時に 0 にならないように）

// ===== ノイズのチャンネル（1つずつ別の揺れ方をする）=====
enum NoiseChannel : uint32_t {
	kNoisePositionX,
	kNoisePositionY,
	kNoisePositionZ,
	kNoiseRotationX,
	kNoiseRotationY,
	kNoiseRotationZ,
};

// ===== 減衰振動の値 =====
struct SpringParam {
	float omega = 0.0f;       // ばねの強さ（1秒に回る角度。2π × 回数）
	float damping = 0.0f;     // 減衰比
	float dampedOmega = 0.0f; // 減衰があるときの、実際に揺れる速さ
};

SpringParam MakeSpringParam(const CameraShakeComponent& shake) {
	const float frequency = (std::max)(shake.impulseFrequency, kMinImpulseFrequency);
	const float damping = std::clamp(shake.impulseDamping, kMinDamping, kMaxDamping);
	const float omega = 2.0f * std::numbers::pi_v<float> * frequency;
	return {omega, damping, omega * std::sqrt(1.0f - damping * damping)};
}

//=============================================================================
// ノイズ（1次元のパーリンノイズ）
//=============================================================================
// ===== 整数の番号から、-1〜1 の傾きを作る（同じ番号なら、いつも同じ値）=====
float MakeGradient(int32_t index, uint32_t channel) {
	uint32_t hash = static_cast<uint32_t>(index) * 374761393u + channel * 668265263u;
	hash = (hash ^ (hash >> 13)) * 1274126177u;
	hash ^= hash >> 16;
	return static_cast<float>(hash) / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f;
}

// ===== time の所のノイズ（-1〜1。time をなめらかに動かすと、なめらかに変わる）=====
float CalcNoise(float time, uint32_t channel) {
	const float shifted = time + static_cast<float>(channel) * kChannelShift;
	const float cell = std::floor(shifted);
	const float local = shifted - cell; // マスの中の位置（0〜1）
	const int32_t index = static_cast<int32_t>(cell);
	// マスの左右の端に傾きを置いて、それぞれの傾きの直線の高さを、なめらかな重みで混ぜる
	const float left = MakeGradient(index, channel) * local;
	const float right = MakeGradient(index + 1, channel) * (local - 1.0f);
	const float weight = local * local * local * (local * (local * 6.0f - 15.0f) + 10.0f); // 6t^5 - 15t^4 + 10t^3（端で傾きも曲がりも0になるので、マスの境目が目立たない）
	return Ease::Lerp(left, right, weight) * 2.0f;                                         // 1次元のパーリンノイズの値は ±0.5 に収まるので、2倍して ±1 にする
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraShake::Describe(ComponentUI& ui, CameraShakeComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと揺らさない"));
	ui.Separator("Noise");
	ui.Field("Position###noisePosition", value.noisePosition, AtLeast(0.0f).Tip("位置の揺れ幅（m）。X：カメラの右 Y：上 Z：前"));
	ui.Field("Rotation###noiseRotation", value.noiseRotation, AtLeast(0.0f).Tip("向きの揺れ幅（度）。X：上下の首振り Y：左右の首振り Z：傾き"));
	ui.Field("Frequency###noiseFrequency", value.noiseFrequency, AtLeast(0.0f).Tip("1秒に揺れる回数の目安"));
	ui.Field("Gain###noiseGain", value.noiseGain, AtLeast(0.0f).Tip("揺れ幅に掛ける倍率。0 で止まる"));
	ui.Separator("Impulse");
	ui.Label("CameraSystem::AddImpulse(強さ) で受ける。強さは最初に振れる幅（m）");
	ui.Field("Gain###impulseGain", value.impulseGain, AtLeast(0.0f).Tip("受けた強さに掛ける倍率。0 なら受けない"));
	ui.Field("Frequency###impulseFrequency", value.impulseFrequency, AtLeast(kMinImpulseFrequency).Tip("1秒に揺れる回数"));
	ui.Field("Damping###impulseDamping", value.impulseDamping, Range(kMinDamping, kMaxDamping).Tip("収まる速さ（減衰比）。0 に近いほど長く揺れ、1 に近いほどすぐ止まる"));
	ui.Field("Rotation###impulseRotation", value.impulseRotation, AtLeast(0.0f).Tip("1m ずれたときに一緒に回す角度（度）。遠くの景色も揺れて見える。0 なら位置だけ揺れる"));
	if (ui.Button("Test Impulse")) {
		CameraSystem::AddImpulse(kTestStrength); // 再生中だけ揺れる
	}
}

//=============================================================================
// 揺れを進める
//=============================================================================
// 衝撃は減衰振動（ばね）の式をそのまま解いて進める。1歩ずつ足し込む方法と違い、deltaTime の大きさで結果が変わらない
//   x(t) = e^(-ζωt) × (x0 cos(ω'·t) + K sin(ω'·t))
//   K = (v0 + ζω x0) / ω'　　ω' = ω√(1 - ζ²)
//   v(t) = e^(-ζωt) × (v0 cos(ω'·t) - (ζω K + ω' x0) sin(ω'·t))
void CameraShake::Update(CameraShakeComponent& shake, float deltaTime) {
	shake.noiseTime += deltaTime * shake.noiseFrequency;
	if (LengthSq(shake.impulseOffset) == 0.0f && LengthSq(shake.impulseVelocity) == 0.0f) {
		return; // 衝撃を受けていない
	}
	const SpringParam spring = MakeSpringParam(shake);
	const float decay = spring.damping * spring.omega;
	const float exponent = std::exp(-decay * deltaTime);
	const float cosine = std::cos(spring.dampedOmega * deltaTime);
	const float sine = std::sin(spring.dampedOmega * deltaTime);
	const Vector3 k = (shake.impulseVelocity + shake.impulseOffset * decay) * (1.0f / spring.dampedOmega);
	const Vector3 offset = (shake.impulseOffset * cosine + k * sine) * exponent;
	const Vector3 velocity = (shake.impulseVelocity * cosine - (k * decay + shake.impulseOffset * spring.dampedOmega) * sine) * exponent;
	const bool isStopped = LengthSq(offset) < kStopOffset * kStopOffset && LengthSq(velocity) < kStopVelocity * kStopVelocity;
	shake.impulseOffset = isStopped ? Vector3{0.0f, 0.0f, 0.0f} : offset;
	shake.impulseVelocity = isStopped ? Vector3{0.0f, 0.0f, 0.0f} : velocity;
}

//=============================================================================
// 今のずれ
//=============================================================================
CameraShakeOffset CameraShake::CalcOffset(const CameraShakeComponent& shake) {
	const float time = shake.noiseTime;
	const float gain = shake.noiseGain;
	CameraShakeOffset offset;
	// --- ノイズ ---
	offset.position = {
	    shake.noisePosition.x * CalcNoise(time, kNoisePositionX) * gain,
	    shake.noisePosition.y * CalcNoise(time, kNoisePositionY) * gain,
	    shake.noisePosition.z * CalcNoise(time, kNoisePositionZ) * gain,
	};
	offset.rotation = {
	    shake.noiseRotation.x * CalcNoise(time, kNoiseRotationX) * gain,
	    shake.noiseRotation.y * CalcNoise(time, kNoiseRotationY) * gain,
	    shake.noiseRotation.z * CalcNoise(time, kNoiseRotationZ) * gain,
	};
	// --- 衝撃：位置のずれに合わせて向きも回す（右へずれたら右を向く＝画面が同じ向きへ流れて、揺れが強く見える）---
	offset.position += shake.impulseOffset;
	offset.rotation += Vector3{-shake.impulseOffset.y, shake.impulseOffset.x, shake.impulseOffset.z} * shake.impulseRotation;
	return offset;
}

//=============================================================================
// 衝撃を受ける
//=============================================================================
// x(t) = v0 / ω' × e^(-ζωt) × sin(ω't) の山の高さが strength になる v0 を、速さに足す
//   山は tan(ω't) = ω' / (ζω) の所で、高さは v0 / ω × e^(-ζωt)
void CameraShake::AddImpulse(CameraShakeComponent& shake, float strength, const Vector3& direction) {
	const SpringParam spring = MakeSpringParam(shake);
	const float decay = spring.damping * spring.omega;
	const float peakTime = std::atan2(spring.dampedOmega, decay) / spring.dampedOmega;
	const float speed = strength * shake.impulseGain * spring.omega * std::exp(decay * peakTime);
	shake.impulseVelocity += Normalize(direction) * speed;
}
```

### C-5 `Systems/Camera/CameraVolume.h`（新しいファイル）

```cpp
#pragma once
#include <optional>

#include "HTTEngine/Components/Camera/CameraVolumeComponent.h"
#include "HTTEngine/Graphics/PostEffect/PostOverride.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Volume の中身（CameraSystem から使う）
/// </summary>
namespace CameraVolume {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraVolumeComponent& value);

/// <summary>
/// このカメラの上書きを作る
/// </summary>
/// <param name="focusDistance">ピントの奥行き（m）。全体の設定のままなら nullopt</param>
PostOverrides MakeOverrides(const CameraVolumeComponent& volume, std::optional<float> focusDistance);
} // namespace CameraVolume
```

### C-6 `Systems/Camera/CameraVolume.cpp`（新しいファイル）

```cpp
#include "CameraVolume.h"

#include <algorithm>
#include <format>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Graphics/PostEffect/DepthOfFieldPass.h"

namespace {
// 1項目を、左のチェックと右の値を1行に並べて見せる。保存の名前は key（チェックは key + "Override"）
// 外した項目は灰色にするだけで値は残す（隠すと保存から落ちて、チェックを入れ直したときに値が消える）
void DescribeItem(ComponentUI& ui, const char* name, const char* key, CameraVolumeValue& item, FieldStyle style) {
	ui.Field(std::format("###{}Override", key).c_str(), item.isOverride, Tip("チェックを入れると、このカメラの値を使う。外すと全体の設定のまま"));
	ui.SameLineAfterCheck();
	ui.BeginDisabled(!item.isOverride);
	ui.Field(std::format("{}###{}", name, key).c_str(), item.value, style);
	ui.EndDisabled();
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
void CameraVolume::Describe(ComponentUI& ui, CameraVolumeComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと、このカメラも全体の設定のまま"));
	ui.Field("Weight###weight", value.weight, Range(0.0f, 1.0f).Tip("全部の項目の効き具合。0：全体の設定のまま 〜 1：このカメラの値"));
	ui.Label("チェックを入れた項目だけ、このカメラの値を使う。切り替えると一緒に補間する");
	// ===== 被写界深度 =====
	ui.Separator("Depth Of Field");
	ui.Field("Focus###focus", value.focus, Tip("ピントの合わせ方。Global：全体の設定のまま / Distance：カメラから Focus Distance / LookAtTarget：Camera Look At が見ている点 / FollowTarget：このカメラの Camera Follow の Target"));
	ui.BeginDisabled(value.focus != CameraVolumeFocus::Distance);
	ui.Field("Focus Distance###focusDistance", value.focusDistance, AtLeast(0.0f).Tip("カメラからピントの合う所までの奥行き（m）"));
	ui.EndDisabled();
	DescribeItem(ui, "Focus Range", "focusRange", value.focusRange, AtLeast(0.0f).Tip("ピントの前後この奥行き（m）までは、ぼけない"));
	DescribeItem(ui, "Far Transition", "farTransition", value.farTransition, AtLeast(0.01f).Tip("ピントの範囲の奥から、一番ぼけるまでの奥行き（m）"));
	DescribeItem(ui, "Near Transition", "nearTransition", value.nearTransition, AtLeast(0.0f).Tip("ピントの範囲の手前から、一番ぼけるまでの奥行き（m）。0 なら手前はぼかさない"));
	DescribeItem(ui, "Max Blur", "maxBlur", value.maxBlur, Range(0.0f, DepthOfFieldPass::kMaxBlurLimit).Tip("一番ぼけた所の、ぼけの半径（ピクセル）。0 ならぼかさない。Depth Of Field Component が無いシーンでも、ここを上書きすればぼける"));
	// ===== ブルーム =====
	ui.Separator("Bloom");
	DescribeItem(ui, "Threshold", "bloomThreshold", value.bloomThreshold, AtLeast(0.0f).Tip("これより明るい所だけ光る"));
	DescribeItem(ui, "Knee", "bloomKnee", value.bloomKnee, AtLeast(0.0f).Tip("しきい値付近の柔らかさ"));
	DescribeItem(ui, "Intensity", "bloomIntensity", value.bloomIntensity, AtLeast(0.0f).Tip("合成の強さ。0 なら光らない。全体の設定で切ってあっても、ここを上書きすれば掛かる"));
	// ===== レンズ =====
	ui.Separator("Lens");
	DescribeItem(ui, "Distortion", "lensDistortion", value.lensDistortion, Tip("画面端の歪み。0.2 で気づく程度。全体の設定で切ってあっても、どれかを上書きすれば掛かる"));
	DescribeItem(ui, "Aberration", "lensAberration", value.lensAberration, Tip("色収差。0.02 で十分"));
	DescribeItem(ui, "Radial Blur", "lensRadialBlur", value.lensRadialBlur, AtLeast(0.0f).Tip("中心へ向かって流れるブラー。0.10 で強め"));
	DescribeItem(ui, "Vignette", "lensVignette", value.lensVignette, AtLeast(0.0f).Tip("端の暗さ"));
	// ===== 露出 =====
	ui.Separator("ToneMap");
	DescribeItem(ui, "Exposure", "exposure", value.exposure, AtLeast(0.0f).Tip("露出。2 で1段明るい・0.5 で1段暗い"));
}

//=============================================================================
// 上書きを作る
//=============================================================================
PostOverrides CameraVolume::MakeOverrides(const CameraVolumeComponent& volume, std::optional<float> focusDistance) {
	PostOverrides overrides;
	if (!volume.enabled) {
		return overrides;
	}
	const float weight = std::clamp(volume.weight, 0.0f, 1.0f);
	if (focusDistance.has_value()) {
		overrides[PostParam::FocusDistance] = {*focusDistance * weight, weight};
	}
	const auto setItem = [&overrides, weight](PostParam param, const CameraVolumeValue& item) {
		if (item.isOverride) {
			overrides[param] = {item.value * weight, weight};
		}
	};
	setItem(PostParam::FocusRange, volume.focusRange);
	setItem(PostParam::FarTransition, volume.farTransition);
	setItem(PostParam::NearTransition, volume.nearTransition);
	setItem(PostParam::MaxBlur, volume.maxBlur);
	setItem(PostParam::BloomThreshold, volume.bloomThreshold);
	setItem(PostParam::BloomKnee, volume.bloomKnee);
	setItem(PostParam::BloomIntensity, volume.bloomIntensity);
	setItem(PostParam::LensDistortion, volume.lensDistortion);
	setItem(PostParam::LensAberration, volume.lensAberration);
	setItem(PostParam::LensRadialBlur, volume.lensRadialBlur);
	setItem(PostParam::LensVignette, volume.lensVignette);
	setItem(PostParam::Exposure, volume.exposure);
	return overrides;
}
```

---

## パートD CameraSystem

### D-1 `Systems/Camera/CameraSystem.h`（全体を差し替え）

```cpp
#pragma once
#include <memory>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Camera/CameraFollowComponent.h"
#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Components/Camera/CameraShakeComponent.h"
#include "HTTEngine/Components/Camera/CameraVolumeComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;
struct TransformComponent;


/// <summary>
/// CameraComponent を持つEntityから、ゲームの絵を撮るカメラを決める（Unity の Cinemachine の Brain と Virtual Camera の小さい版）
/// <para>カメラ（Camera クラス）はここが1つだけ持ち、毎フレーム、有効な Camera Component の中で priority が一番大きい物の値をそこへ写す。
/// シーンの GetCamera() は、上書きしなければこのカメラを返す</para>
/// <para>1台分の値 ＝ Transform ＋ Camera Look At（向き）＋ Camera Shake（揺れ）＋ Camera Volume（ポストエフェクト）。Transform を動かすのは Camera Follow だけで、ほかは写す値にだけ入る</para>
/// <para>priority が一番大きいカメラが変わると、Camera の Blend の設定で、今見えている値からそのカメラの値へ補間する</para>
/// </summary>
class CameraSystem {
public:
	// カメラの Component を全部登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// Camera Follow を付けた Entity（カメラ）の Transform を、追う物へ近づける（再生中だけ。EngineSystems から1フレームに1回）
	/// <para>物理で動いた後（ワールド行列を計算し直した後）、Update の前に呼ぶ。追う物の、そのフレームの位置を追うため</para>
	/// <para>時間はカメラごとに Camera の Time Mode で決まる</para>
	/// </summary>
	/// <returns>動かしたカメラがあったか（あれば、呼んだ側でワールド行列を計算し直す）</returns>
	static bool UpdateFollow();

	// 毎フレーム1回、EntityManager::UpdateTransforms の後に呼ぶ（親の下に置いたカメラも、そのフレームの位置で撮るため）
	// isGameRunning が false（エディタの停止中）なら、補間も揺れも無しで、置いた通りの絵を撮る
	static void Update(bool isGameRunning);

	// シーンを入れ替えるときに呼ぶ（SceneManager から）。次の Update は、前のシーンのカメラから補間せずに、新しいカメラをそのまま撮る
	static void ResetOutput();

	// ゲームの絵を撮るカメラ。Camera Component が1つも無いときは、既定の位置（(0, 0, -10) から +Z を向く）のカメラ
	// nullptr にはならない。借りるだけ（その場で使う）ならこちら。注視点・揺れ・補間が入った、画面に写っている値を持つ
	static Camera* GetMainCamera();
	// 同じカメラを shared_ptr で返す。フレームをまたいで覚えておきたい物は、これを weak_ptr にして持つ
	// （CameraSystem::Release でカメラが消えても、weak_ptr なら「もう無い」と分かる）
	static std::shared_ptr<Camera> GetSharedMainCamera();
	// 今使っている Camera Component の Entity（無ければ無効な Handle）。補間の途中は、切り替わった先のカメラ
	static Handle<Entity> GetMainCameraEntity();
	// カメラの切り替えの補間の途中か
	static bool IsBlending();

	// メインカメラの Camera Follow が追っている物の Transform（Camera Follow・Target が無ければ nullptr。停止中も返す）
	// 風の線（WindZoneSystem）・被写界深度のピント（DepthOfFieldSystem）が使う
	static const TransformComponent* GetFollowTargetTransform();

	// 同じ物の Entity（無ければ無効な Handle）。Trigger Activator が「入ったのは追っている物か」を見るのに使う
	static Handle<Entity> GetFollowTargetEntity();

	// ===== ゲームから使う =====

	/// <summary>
	/// 有効な Camera Shake 全部へ、衝撃を送る（Unity の Cinemachine の Impulse Source の GenerateImpulse と同じ）。向きは画面の面の中でランダム
	/// </summary>
	/// <param name="strength">最初に振れる幅（m）。Camera Shake の Gain が掛かる</param>
	static void AddImpulse(float strength);

	/// <summary>
	/// 向きを決めて衝撃を送る
	/// </summary>
	/// <param name="strength">最初に振れる幅（m）。Camera Shake の Gain が掛かる</param>
	/// <param name="worldDirection">最初に振れる向き（ワールド）。長さは1でなくてよい</param>
	static void AddImpulse(float strength, const Vector3& worldDirection);
};
```

### D-2 `Systems/Camera/CameraSystem.cpp`（全体を差し替え）

```cpp
#include "CameraSystem.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/PostEffect/PostOverride.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Math/RandomEngine.h"
#include "HTTEngine/Systems/Camera/CameraFollow.h"
#include "HTTEngine/Systems/Camera/CameraShake.h"
#include "HTTEngine/Systems/Camera/CameraVolume.h"
#include "HTTEngine/Time/EngineTime.h"

namespace {
const Vector3 kDefaultPosition = {0.0f, 0.0f, -10.0f}; // Camera Component が無いときの位置（原点を正面から見る）
constexpr float kMinLookDistanceSq = 1e-6f;            // 見る点がカメラとほとんど同じ位置なら、向きが決まらないので Transform の向きのままにする

/// <summary>
/// 1フレーム分の、カメラが写す値
/// </summary>
struct CameraOutput {
	Vector3 position = kDefaultPosition;             // 位置（ワールド）
	Quaternion rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 向き（ワールド）
	float fovY = CameraComponent{}.fovY;             // 縦の視野角（度）
	float nearZ = CameraComponent{}.nearZ;           // 近い方の端
	float farZ = CameraComponent{}.farZ;             // 遠い方の端
	PostOverrides post;                              // ポストエフェクトの上書き
};

/// <summary>
/// カメラの切り替えの補間
/// </summary>
struct CameraBlend {
	bool isActive = false;                           // 補間の途中か
	Handle<Entity> fromEntity;                       // 切り替え前のカメラ（無効なら from は止まった値のまま）
	CameraOutput from;                               // 補間の始まりの値（fromEntity が有効な間は毎フレーム取り直す）
	float elapsed = 0.0f;                            // 経った秒
	float duration = 0.0f;                           // 補間にかける秒
	Ease::Type ease = Ease::Type::Linear;            // 速さの変え方
	CameraTimeMode timeMode = CameraTimeMode::Game;  // 使う時間
};

/// <summary>
/// 送られた衝撃（次の Update で Camera Shake へ渡す）
/// </summary>
struct PendingImpulse {
	float strength = 0.0f;                  // 最初に振れる幅（m）
	Vector3 direction = {0.0f, 0.0f, 0.0f}; // 最初に振れる向き
	bool isWorldDirection = false;          // true：direction はワールドの向き false：カメラの右・上・前の向き
};

/// <summary>
/// CameraSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct CameraState {
	// ゲームの絵を撮るカメラ（GetMainCamera はいつもこれを返す）
	// shared_ptr にするのは、パーティクル（ParticleManager）が weak_ptr で「見ているだけ」にできるように
	std::shared_ptr<Camera> camera = std::make_shared<Camera>();
	Handle<Entity> mainEntity;      // 今使っている Camera Component の Entity
	bool hasLoggedNoCamera = false; // 「Camera Component が無い」を知らせたか（続けて何度も出さない）
	bool hasOutput = false;         // output に前のフレームの値が入っているか（false なら、次の切り替えは補間しない）
	CameraOutput output;            // 前のフレームに写した値（切り替えの途中でさらに切り替えるときは、ここから始める）
	CameraBlend blend;              // 切り替えの補間
	std::vector<PendingImpulse> impulses; // 送られた衝撃
};
std::unique_ptr<CameraState> state;

//=============================================================================
// 写す
//=============================================================================
// 写り方をカメラへ（視野角は度で持っているので、ラジアンに直して渡す）
void ApplyProjection(float fovYDegrees, float nearZValue, float farZValue) {
	const float fovY = std::clamp(fovYDegrees, kMinCameraFovY, kMaxCameraFovY) * MathUtility::kDegToRad;
	const float nearZ = (std::max)(nearZValue, 0.001f);
	const float farZ = (std::max)(farZValue, nearZ + 0.01f);                    // far が near より手前だと、何も映らない
	state->camera->Initialize(fovY, GameScreen::GetAspectRatio(), nearZ, farZ); // Initialize の中で行列も作り直す
}

// 1フレーム分の値をカメラへ
void ApplyOutput(const CameraOutput& output) {
	// Camera クラスの回転は X → Y → Z の順の角度（ラジアン）なので、行列にしてから取り出す
	const Transform pose = DecomposeAffineMatrix(MakeAffineMatrix({1.0f, 1.0f, 1.0f}, output.rotation, output.position));
	state->camera->SetTranslation(pose.translation);
	state->camera->SetRotation(pose.rotation);
	state->camera->SetScale({1.0f, 1.0f, 1.0f});
	ApplyProjection(output.fovY, output.nearZ, output.farZ);
	SceneRenderer::SetCameraPostOverrides(output.post);
}

// Camera Component が無いときの、既定のカメラ
void UseDefaultCamera() {
	state->camera->SetTranslation(kDefaultPosition);
	state->camera->SetRotation({0.0f, 0.0f, 0.0f});
	state->camera->SetScale({1.0f, 1.0f, 1.0f});
	ApplyProjection(CameraComponent{}.fovY, CameraComponent{}.nearZ, CameraComponent{}.farZ);
}

//=============================================================================
// 時間
//=============================================================================
// ===== 時間の種類ごとの、このフレームの秒 =====
float GetCameraDeltaTime(CameraTimeMode mode) {
	switch (mode) {
	case CameraTimeMode::Game:
		return Time::GetDeltaTime();
	case CameraTimeMode::Real:
		return Time::GetRealDeltaTime();
	}
	HTT_ASSERT_MSG(false, "知らない CameraTimeMode です");
	return 0.0f;
}

// ===== その Entity のカメラが使う時間の秒（Camera Component が無ければゲームの時間）=====
float GetCameraDeltaTime(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return GetCameraDeltaTime(camera ? camera->timeMode : CameraTimeMode::Game);
}

//=============================================================================
// Inspector と保存
//=============================================================================
// ===== Camera =====
// Inspector とシーンファイルの両方がこの並びを使う（保存の名前はメンバ名）
void DescribeCamera(ComponentUI& ui, CameraComponent& value) {
	ui.Field("Field Of View###fovY", value.fovY, Range(kMinCameraFovY, kMaxCameraFovY).Tip("縦の視野角（度）。小さいほど望遠"));
	ui.Field("Near###nearZ", value.nearZ, AtLeast(0.001f).Tip("これより近い物は映らない"));
	ui.Field("Far###farZ", value.farZ, AtLeast(0.01f).Tip("これより遠い物は映らない"));
	ui.Field("Priority###priority", value.priority, Tip("有効な Camera が複数あるとき、大きい方を使う（切り替えはこの値か、Entity の有効・無効で）"));
	ui.Separator("Blend");
	ui.Field("Style###blendStyle", value.blendStyle, Tip("このカメラへ切り替わるときの動き方。Cut：すぐ切り替える / Ease：Time の秒をかけて補間する（Camera Volume の値も一緒に）"));
	ui.BeginDisabled(value.blendStyle != CameraBlendStyle::Ease);
	ui.Field("Time###blendTime", value.blendTime, AtLeast(0.0f).Tip("補間にかける秒。0 ならすぐ切り替える"));
	ui.Field("Ease###blendEase", value.blendEase, Tip("補間の速さの変え方"));
	ui.EndDisabled();
	ui.Separator("Time");
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("このカメラの切り替え・Camera Follow・Camera Look At・Camera Shake が使う時間。Game：スローで遅くなり、ヒットストップ・ポーズで止まる / Real：止まらない"));
}

// ===== Camera Look At =====
// 実行中だけの値は並べない＝保存しない
void DescribeCameraLookAt(ComponentUI& ui, CameraLookAtComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと Transform の向きのまま"));
	ui.Field("Target###target", value.target, Tip("見る Entity（Hierarchy からドラッグ）。空なら Offset をワールドの座標として見る"));
	ui.Field("Offset###offset", value.offset, Tip("見る点のずれ（m。ワールドの向き）"));
	ui.Field("Smooth Time###smoothTime", value.smoothTime, AtLeast(0.0f).Tip("見る点が追いつくまでの時間の目安（秒）。大きいほどゆっくり。0 ならすぐ合わせる（再生中だけ効く）"));
}

//=============================================================================
// 注視点
//=============================================================================
// ===== 見たい点（Target が居なくなったら nullopt）=====
std::optional<Vector3> FindLookAtPoint(const CameraLookAtComponent& lookAt) {
	if (!lookAt.target.IsSet()) {
		return lookAt.offset;
	}
	const TransformComponent* target = EntityManager::Get<TransformComponent>(lookAt.target);
	if (target == nullptr) {
		return std::nullopt;
	}
	return GetWorldPosition(*target) + lookAt.offset;
}

// ===== 今見ている点を、見たい点へ近づける =====
void UpdateLookAt(CameraLookAtComponent& lookAt, float deltaTime) {
	const std::optional<Vector3> point = FindLookAtPoint(lookAt);
	if (!lookAt.enabled || !point.has_value()) {
		lookAt.isAiming = false;
		return;
	}
	if (!lookAt.isAiming) {
		lookAt.currentPoint = *point; // 見始め：カメラの向きが置いた向きから流れないように、すぐ合わせる
		lookAt.isAiming = true;
		return;
	}
	lookAt.currentPoint += (*point - lookAt.currentPoint) * CameraFollow::CalcSmoothRate(deltaTime, lookAt.smoothTime);
}

// ===== 今見る点。再生中はなめらかにした点、停止中は見たい点そのもの（Inspector で変えた値がすぐ見えるように）=====
std::optional<Vector3> GetLookAtPoint(const CameraLookAtComponent& lookAt, bool isGameRunning) {
	if (!lookAt.enabled) {
		return std::nullopt;
	}
	if (isGameRunning && lookAt.isAiming) {
		return lookAt.currentPoint;
	}
	return FindLookAtPoint(lookAt);
}

//=============================================================================
// カメラ1台分の値
//=============================================================================
// ===== Camera Volume のピントの奥行き（m。全体の設定のままなら nullopt）=====
std::optional<float> CalcVolumeFocusDistance(Handle<Entity> entity, const CameraVolumeComponent& volume, const CameraOutput& output, const std::optional<Vector3>& lookAtPoint) {
	// カメラの前の向きに測った長さ（真ん中から外れた所でも、同じ奥行きの面にピントが合う）
	const auto depthOf = [&output](const Vector3& point) { return (std::max)(Dot(point - output.position, RotateVector({0.0f, 0.0f, 1.0f}, output.rotation)), 0.0f); };
	switch (volume.focus) {
	case CameraVolumeFocus::Global:
		return std::nullopt;
	case CameraVolumeFocus::Distance:
		return volume.focusDistance;
	case CameraVolumeFocus::LookAtTarget:
		return lookAtPoint.has_value() ? std::optional<float>(depthOf(*lookAtPoint)) : std::nullopt;
	case CameraVolumeFocus::FollowTarget: {
		const CameraFollowComponent* follow = EntityManager::Get<CameraFollowComponent>(entity);
		const TransformComponent* target = follow ? EntityManager::Get<TransformComponent>(follow->target) : nullptr;
		return target ? std::optional<float>(depthOf(GetWorldPosition(*target))) : std::nullopt;
	}
	}
	HTT_ASSERT_MSG(false, "知らない CameraVolumeFocus です");
	return std::nullopt;
}

// ===== カメラ1台の、このフレームの値 =====
// Transform は読むだけ。注視点・揺れはここで足すので、Transform には残らない
CameraOutput EvaluateCamera(Handle<Entity> entity, const TransformComponent& transform, const CameraComponent& camera, bool isGameRunning) {
	CameraOutput output;
	output.position = GetWorldPosition(transform);
	output.rotation = GetWorldRotation(transform);
	output.fovY = camera.fovY;
	output.nearZ = camera.nearZ;
	output.farZ = camera.farZ;

	// --- 注視点：向きだけ置き換える ---
	std::optional<Vector3> lookAtPoint;
	if (const CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(entity)) {
		lookAtPoint = GetLookAtPoint(*lookAt, isGameRunning);
	}
	if (lookAtPoint.has_value() && LengthSq(*lookAtPoint - output.position) > kMinLookDistanceSq) {
		output.rotation = MakeLookRotation(*lookAtPoint - output.position);
	}
	// --- ポストエフェクト（揺れを足す前の位置で測る。揺れでピントが動かないように）---
	if (const CameraVolumeComponent* volume = EntityManager::Get<CameraVolumeComponent>(entity)) {
		output.post = CameraVolume::MakeOverrides(*volume, CalcVolumeFocusDistance(entity, *volume, output, lookAtPoint));
	}
	// --- 揺れ：カメラの向きから見たずれを足す ---
	const CameraShakeComponent* shake = EntityManager::Get<CameraShakeComponent>(entity);
	if (isGameRunning && shake != nullptr && shake->enabled) {
		const CameraShakeOffset offset = CameraShake::CalcOffset(*shake);
		output.position += RotateVector(offset.position, output.rotation);
		output.rotation = Normalize(output.rotation * MakeQuaternionFromEuler(offset.rotation)); // 右に掛けた方が先に回る＝カメラの軸のまわりに回る
	}
	return output;
}

//=============================================================================
// 切り替えの補間
//=============================================================================
// ===== 2つの値の間 =====
CameraOutput LerpOutput(const CameraOutput& from, const CameraOutput& to, float t) {
	CameraOutput output;
	output.position = Lerp(from.position, to.position, t);
	output.rotation = Slerp(from.rotation, to.rotation, t);
	output.fovY = Ease::Lerp(from.fovY, to.fovY, t);
	output.nearZ = Ease::Lerp(from.nearZ, to.nearZ, t);
	output.farZ = Ease::Lerp(from.farZ, to.farZ, t);
	output.post = LerpPostOverrides(from.post, to.post, t);
	return output;
}

// ===== 切り替わったカメラへの補間を始める =====
void StartBlend(Handle<Entity> previous, const CameraComponent& next) {
	CameraBlend& blend = state->blend;
	const bool wasBlending = blend.isActive;
	blend.isActive = (next.blendStyle == CameraBlendStyle::Ease) && next.blendTime > 0.0f;
	// 始まりは「今見えている値」（前のフレームに写した値）。補間の途中でも、途中の値から始まるので絵が飛ばない
	blend.from = state->output;
	// 切り替え前のカメラが動いていれば、補間の間も一緒に動かす（追いかけているカメラの絵が置いていかれないように）
	// 補間の途中で切り替えたときは、2つのカメラの間の値なので、止まった値のまま使う
	blend.fromEntity = wasBlending ? Handle<Entity>{} : previous;
	blend.elapsed = 0.0f;
	blend.duration = next.blendTime;
	blend.ease = next.blendEase;
	blend.timeMode = next.timeMode;
}

// ===== 切り替え前のカメラの今の値を取り直す =====
void RefreshBlendSource(bool isGameRunning) {
	CameraBlend& blend = state->blend;
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(blend.fromEntity);
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(blend.fromEntity);
	if (transform == nullptr || camera == nullptr) {
		blend.fromEntity = {}; // 途中で消えた：最後に取った値のまま使う
		return;
	}
	blend.from = EvaluateCamera(blend.fromEntity, *transform, *camera, isGameRunning);
}

// ===== 補間を進めて、今写す値を返す =====
CameraOutput ApplyBlend(const CameraOutput& target, bool isGameRunning) {
	CameraBlend& blend = state->blend;
	if (!blend.isActive) {
		return target;
	}
	blend.elapsed += GetCameraDeltaTime(blend.timeMode);
	if (blend.elapsed >= blend.duration) {
		blend.isActive = false;
		return target;
	}
	RefreshBlendSource(isGameRunning);
	return LerpOutput(blend.from, target, Ease::Apply(blend.elapsed / blend.duration, blend.ease));
}

//=============================================================================
// 揺れ
//=============================================================================
// ===== 送られた衝撃を Camera Shake へ渡す =====
// AddImpulse を Inspector のボタンからも呼ぶので、そこでは Component を書き換えない（書き換えると Undo の履歴に積まれる）
void ApplyPendingImpulses() {
	for (const PendingImpulse& impulse : state->impulses) {
		EntityManager::ForEach<CameraShakeComponent>([&impulse](Handle<Entity> entity, CameraShakeComponent& shake) {
			const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
			if (!shake.enabled || !EntityManager::IsActiveInHierarchy(entity) || transform == nullptr) {
				return;
			}
			// ワールドの向きは、カメラごとの右・上・前の向きに直す（長さ1の回転の逆は共役）
			const Vector3 direction = impulse.isWorldDirection ? RotateVector(impulse.direction, Conjugate(GetWorldRotation(*transform))) : impulse.direction;
			CameraShake::AddImpulse(shake, impulse.strength, direction);
		});
	}
	state->impulses.clear();
}

// ===== 注視点と揺れを進める（どのカメラも。補間の間は切り替え前のカメラも写るので）=====
void UpdateCameraMotion() {
	ApplyPendingImpulses();
	EntityManager::ForEach<CameraLookAtComponent>([](Handle<Entity> entity, CameraLookAtComponent& lookAt) {
		if (EntityManager::IsActiveInHierarchy(entity)) {
			UpdateLookAt(lookAt, GetCameraDeltaTime(entity));
		}
	});
	EntityManager::ForEach<CameraShakeComponent>([](Handle<Entity> entity, CameraShakeComponent& shake) {
		if (shake.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			CameraShake::Update(shake, GetCameraDeltaTime(entity));
		}
	});
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void CameraSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<CameraState>();
	UseDefaultCamera(); // 最初の Update より前（シーンの Initialize）に使われても、ちゃんとしたカメラを返せるように

	EntityManager::RegisterComponent<CameraComponent>();
	ComponentSerializer::Register<CameraComponent>("Camera", &DescribeCamera);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraComponent>>("Camera", "Camera", &DescribeCamera));

	EntityManager::RegisterComponent<CameraFollowComponent>();
	ComponentSerializer::Register<CameraFollowComponent>("Camera Follow", &CameraFollow::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraFollowComponent>>("Camera Follow", "Camera", &CameraFollow::Describe));

	EntityManager::RegisterComponent<CameraLookAtComponent>();
	ComponentSerializer::Register<CameraLookAtComponent>("Camera Look At", &DescribeCameraLookAt);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraLookAtComponent>>("Camera Look At", "Camera", &DescribeCameraLookAt));

	EntityManager::RegisterComponent<CameraShakeComponent>();
	ComponentSerializer::Register<CameraShakeComponent>("Camera Shake", &CameraShake::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraShakeComponent>>("Camera Shake", "Camera", &CameraShake::Describe));

	EntityManager::RegisterComponent<CameraVolumeComponent>();
	ComponentSerializer::Register<CameraVolumeComponent>("Camera Volume", &CameraVolume::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraVolumeComponent>>("Camera Volume", "Camera", &CameraVolume::Describe));
}

void CameraSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
bool CameraSystem::UpdateFollow() {
	bool isMoved = false;
	EntityManager::ForEach<CameraFollowComponent>([&isMoved](Handle<Entity> entity, CameraFollowComponent& follow) {
		if (!follow.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
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

void CameraSystem::Update(bool isGameRunning) {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");

	// ===== 1. 使う Camera を選ぶ（有効な物の中で priority が一番大きい物。同じなら先に見つけた物）=====
	Handle<Entity> best;
	CameraComponent bestComponent;
	EntityManager::ForEach<CameraComponent>([&](Handle<Entity> entity, const CameraComponent& component) {
		if (!EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		if (!best.IsValid() || component.priority > bestComponent.priority) {
			best = entity;
			bestComponent = component;
		}
	});
	const Handle<Entity> previous = state->mainEntity;
	state->mainEntity = best;

	// ===== 2. 無ければ既定のカメラ =====
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(best);
	if (transform == nullptr) {
		if (!state->hasLoggedNoCamera) {
			LogManager::Log("Camera Component が無いので、既定のカメラ（(0, 0, -10) から +Z を向く）で撮ります。Hierarchy の ＋ → Camera で置けます");
			state->hasLoggedNoCamera = true;
		}
		UseDefaultCamera();
		SceneRenderer::SetCameraPostOverrides({});
		ResetOutput();
		return;
	}
	state->hasLoggedNoCamera = false;

	// ===== 3. カメラが変わったら、補間を始める =====
	// 停止中と、シーンの最初のフレームは補間しない（エディタで値を変えた結果・置いた通りの絵をすぐ見せる）
	if (!isGameRunning) {
		state->blend.isActive = false;
		state->impulses.clear();
	} else if (best != previous && state->hasOutput) {
		StartBlend(previous, bestComponent);
	}

	// ===== 4. 注視点と揺れを進める =====
	if (isGameRunning) {
		UpdateCameraMotion();
	}

	// ===== 5. 写す値を作って、カメラへ写す =====
	// 大きさは写さない（ビュー行列に大きさが混ざると、絵が伸び縮みする）
	const CameraOutput output = ApplyBlend(EvaluateCamera(best, *transform, bestComponent, isGameRunning), isGameRunning);
	ApplyOutput(output);
	state->output = output;
	state->hasOutput = true;
}

void CameraSystem::ResetOutput() {
	state->hasOutput = false;
	state->blend.isActive = false;
}

//=============================================================================
// 取得
//=============================================================================
Camera* CameraSystem::GetMainCamera() {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->camera.get();
}

std::shared_ptr<Camera> CameraSystem::GetSharedMainCamera() {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->camera;
}

Handle<Entity> CameraSystem::GetMainCameraEntity() {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->mainEntity;
}

bool CameraSystem::IsBlending() {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->blend.isActive;
}

const TransformComponent* CameraSystem::GetFollowTargetTransform() {
	// Target の Entity → Transform（どこかで無ければ nullptr）
	return EntityManager::Get<TransformComponent>(GetFollowTargetEntity());
}

Handle<Entity> CameraSystem::GetFollowTargetEntity() {
	// メインカメラの Camera Follow → その Target の Entity（どこかで無ければ無効な Handle）
	const CameraFollowComponent* follow = EntityManager::Get<CameraFollowComponent>(GetMainCameraEntity());
	return (follow != nullptr) ? EntityManager::Find(follow->target) : Handle<Entity>{};
}

//=============================================================================
// 衝撃
//=============================================================================
void CameraSystem::AddImpulse(float strength) {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	// 画面の面の中で、向きをランダムに決める
	const float angle = RandomEngine::GetFloat(0.0f, 2.0f * std::numbers::pi_v<float>);
	state->impulses.push_back({strength, {std::cos(angle), std::sin(angle), 0.0f}, false});
}

void CameraSystem::AddImpulse(float strength, const Vector3& worldDirection) {
	HTT_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	state->impulses.push_back({strength, worldDirection, true});
}
```

### D-3 `Systems/EngineSystems.cpp`

変更前：

```cpp
	if (isGameRunning && CameraSystem::UpdateFollow(Time::GetDeltaTime())) {
		EntityManager::UpdateTransforms();
	}

	CameraSystem::Update();                 // Camera Component の位置・向きをゲームのカメラへ写す（ワールド行列の後、使う人たちの前）
```

変更後：

```cpp
	if (isGameRunning && CameraSystem::UpdateFollow()) {
		EntityManager::UpdateTransforms();
	}

	CameraSystem::Update(isGameRunning);    // Camera Component の位置・向きをゲームのカメラへ写す（ワールド行列の後、使う人たちの前）
```

### D-4 `Scene/SceneManager.cpp`

include を足す。

変更後：

```cpp
#include "HTTEngine/Systems/Audio/AudioSystem.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
```

`ChangeScene` の中。

変更前：

```cpp
	Time::ResetGameTime();
	const bool oldUsesFile = currentScene_ && currentScene_->GetSceneFile() != nullptr;
```

変更後：

```cpp
	Time::ResetGameTime();
	CameraSystem::ResetOutput(); // 前のシーンのカメラから補間しないように
	const bool oldUsesFile = currentScene_ && currentScene_->GetSceneFile() != nullptr;
```

ここでビルドが通る。

---

## パートE 確認用のシーンと Script

ゲーム側の物。確認が終わったら消してよい。

### E-1 シーン

`Docs/Tasks/VirtualCameraTest.scene.json` を、ゲームの `resources/scenes/` へ写す。

| Entity | 中身 |
|---|---|
| Cam Top | 見下ろし。Follow XZ・LookAhead・Bounds ±30。Ease 1 秒 |
| Cam Side | 横から。Follow XY。Ease 0.6 秒 |
| Cam Close | 寄り。Look At Player・ノイズ・Volume（被写界深度・ブルーム・レンズ・露出）。Ease 1.5 秒・Time Mode Real |
| Cam Free | 斜めから。Follow View。Cut |
| Player | 緑の箱。`VirtualCameraTester` が付いている |
| Light Ball | 明るい球（ブルームの変化を見る） |
| Far Pillar・Near Pillar | ピントの奥と手前の柱 |

### E-2 `Source/CameraTest/VirtualCameraTester.cpp`（ゲーム側の新しいファイル）

```cpp
#include <array>

#include <HTTEngine/ECS/Script.h>
#include <HTTEngine/Input/InputManager.h>

// Virtual Camera を確かめるための Script。動かす Entity に付ける
// 1〜4：カメラの切り替え / WASD：動かす / Q・E：今のカメラを回す
// Space：衝撃 / H：1秒のヒットストップ / T：時間の倍率（1 → 0.2 → 0 → 1）
class VirtualCameraTester : public Script {
public:
	void Update() override {
		Move();
		RotateCamera();
		SelectCamera();
		if (InputManager::IsKeyTriggered(DIK_SPACE)) {
			CameraSystem::AddImpulse(impulseStrength_);
		}
		if (InputManager::IsKeyTriggered(DIK_H)) {
			Time::HitStop(1.0f);
		}
		if (InputManager::IsKeyTriggered(DIK_T)) {
			ChangeTimeScale();
		}
	}

	void Fields(ComponentUI& ui) override {
		ui.Field("Camera 1", cameras_[0]);
		ui.Field("Camera 2", cameras_[1]);
		ui.Field("Camera 3", cameras_[2]);
		ui.Field("Camera 4", cameras_[3]);
		ui.Field("Move Speed", moveSpeed_, AtLeast(0.0f));
		ui.Field("Impulse Strength", impulseStrength_, AtLeast(0.0f).Tip("Space の衝撃の強さ（最初に振れる幅。m）"));
	}

private:
	// WASD で地面の上を動かす
	void Move() {
		const float deltaTime = Time::GetDeltaTime();
		const float moveX = (InputManager::IsKeyPressed(DIK_D) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_A) ? 1.0f : 0.0f);
		const float moveZ = (InputManager::IsKeyPressed(DIK_W) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_S) ? 1.0f : 0.0f);
		transform.translation.x += moveX * moveSpeed_ * deltaTime;
		transform.translation.z += moveZ * moveSpeed_ * deltaTime;
	}

	// Q・E で今のカメラの Transform を Y のまわりに回す（Camera Follow の XZ・View が向きに付いてくるかを見る）
	void RotateCamera() {
		const float turn = (InputManager::IsKeyPressed(DIK_E) ? 1.0f : 0.0f) - (InputManager::IsKeyPressed(DIK_Q) ? 1.0f : 0.0f);
		TransformComponent* camera = EntityManager::Get<TransformComponent>(CameraSystem::GetMainCameraEntity());
		if (turn == 0.0f || camera == nullptr) {
			return;
		}
		Vector3 euler = GetEulerAngles(*camera);
		euler.y += turn * kTurnSpeed * Time::GetUnscaleDeltaTime();
		SetEulerAngles(*camera, euler);
	}

	// 1〜4 で、選んだカメラの Priority を一番上にする
	void SelectCamera() {
		const std::array<uint8_t, 4> keys = {DIK_1, DIK_2, DIK_3, DIK_4};
		for (size_t i = 0; i < keys.size(); ++i) {
			if (InputManager::IsKeyTriggered(keys[i])) {
				SetPriorities(i);
			}
		}
	}

	void SetPriorities(size_t selected) {
		for (size_t i = 0; i < cameras_.size(); ++i) {
			if (CameraComponent* camera = cameras_[i].GetComponent<CameraComponent>()) {
				camera->priority = (i == selected) ? kSelectedPriority : 0;
			}
		}
	}

	// 時間の倍率を順に変える
	void ChangeTimeScale() {
		const float scale = Time::GetGameTimeScale();
		if (scale >= 1.0f) {
			Time::SetGameTimeScale(0.2f);
		} else if (scale > 0.0f) {
			Time::SetGameTimeScale(0.0f);
		} else {
			Time::SetGameTimeScale(1.0f);
		}
	}

	static constexpr int kSelectedPriority = 100; // 選んだカメラの Priority
	static constexpr float kTurnSpeed = 60.0f;    // カメラを回す速さ（度/秒）

	std::array<GameObject, 4> cameras_; // 切り替えるカメラ
	float moveSpeed_ = 6.0f;            // 動く速さ（m/秒）
	float impulseStrength_ = 0.4f;      // 衝撃の強さ（m）
};
SCRIPT(VirtualCameraTester, "CameraTest");
```

| キー | 動き |
|---|---|
| 1〜4 | Cam Top・Side・Close・Free へ切り替える |
| WASD | Player を動かす |
| Q・E | 今のカメラの Transform を回す |
| Space | 衝撃 |
| H | 1秒のヒットストップ |
| T | 時間の倍率（1 → 0.2 → 0 → 1） |

---

## パートF 確認

### F-1 確認の手順

1. ゲームをビルドして、エディタの Project ウィンドウから `VirtualCameraTest` を開く。
2. Game ビューを開いて Play を押す。下の表の順に確かめる。
3. 製品版（Release）は `main.cpp` の最初のシーンを `"VirtualCameraTest"` にして確かめる。確認の後に戻す。

### F-2 確認項目

「写しで確認」は、一時フォルダの写しで実際に動かした結果。写経の後に、同じ項目を確かめる。

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 1 | 横スクロールの追従が変わらない | 今までの Camera Follow のシーンを Play | 固定の時間で 4 台（遊び・Bounds・傾き・Ground Snap）を 1500 フレーム動かし、変更の前後で全部の位置が一致 |
| 2 | Priority で滑らかに切り替わる | 2 → 1 | 0.6 秒で到着。到着後は Transform と同じ位置 |
| 3 | 切り替えの途中でさらに切り替えても飛ばない | 3 の直後に 1 | 切り替えのフレームの位置の差は 0.00025 m。補間の間で一番速いフレームは 0.65 m |
| 4 | すぐ切り替える | 4 | 1 フレームで切り替わる |
| 5 | 見下ろしの追従 | 1 にして WASD | 追う物が画面の真ん中付近に残る |
| 6 | 見下ろしの Bounds | 1 にして W・D を押し続ける | 画面の端が ±30 で止まる（奥の端 z = 30.0、右の端 x = 30.0 を計算で確認） |
| 7 | 向きを固定しないカメラ | 1 や 4 にして Q・E | 毎秒 30 度回しながら追う物を動かして、画面の位置が XZ は 0.16 以内、View は 0.23 以内（端が 1） |
| 8 | 真下を見るカメラ | Cam Top の Rotation X を 90 にして Play | 画面の位置が 0.19 以内 |
| 9 | 注視点 | 3 | Player が画面の真ん中に残る（Offset の分だけ下） |
| 10 | 注視点は Transform を変えない | 3 の間の Inspector | 写す向きは Y 51 度付近、Transform の Rotation は (0, 0, 0) のまま |
| 11 | ノイズの揺れ | 3 | 1 秒ごとに止まらない（0.65 秒の間で位置のずれの最小が 0.0006 m）。Transform は動かない |
| 12 | 衝撃の揺れ | 1 にして Space | 強さ 0.5 で最初の揺れが 0.4815 m（式の値と一致。60 フレームの区切りで読んだ値） |
| 13 | 揺れの後に元の位置へ戻る | Space の 1 秒後 | 0.9 秒で位置の差がちょうど 0。Game ビューの絵も揺れる前と一致 |
| 14 | Volume が切り替えと一緒に変わる | 1 → 3 | Bloom の強さが 0.9 → 2.0、Vignette が 0.35 → 0.8、Exposure が 1 → 1.2 へなめらかに変わる。途中で切り替えても飛ばない |
| 15 | カメラごとの被写界深度 | 3 | Player だけはっきり見えて、奥と手前の柱がぼける。シーンに Depth Of Field Component が無くても掛かる |
| 16 | 指定しない項目は全体の設定 | Settings → PostEffect の Bloom の Knee を変える | Cam Close の間に全体の Knee を 0.25 → 0.5 にすると、ブルームの値も 0.5 になる（Knee のチェックを外してある） |
| 17 | 全体の設定で切ってある効果 | Settings で Bloom と Lens を切る | Cam Top ではブルームとビネットが消え、Cam Close では掛かる |
| 18 | ヒットストップ（Game のカメラ） | 2 の直後に H | 補間が止まり、終わると続きから動く |
| 19 | 時間を止めても Real のカメラは動く | T を2回（倍率 0）→ 3 | 補間が進み、ノイズも動く |
| 20 | Game のカメラは時間を止めると止まる | T を2回（倍率 0）→ 1 | 補間が進まない |
| 21 | ポーズ中の Real のカメラ | `Time::SetGamePaused(true)` | ノイズが動く |
| 22 | エディタの一時停止 | Cam Close で Pause | Real のカメラも止まる（1 秒あけた2枚の絵が一致） |
| 23 | シーンを読み直す | 補間の途中で Restart | 補間の途中で `SceneManager::ReloadScene` を呼んで、補間せずに読み直した後のカメラから始まる |
| 24 | 使っているカメラを消す | 補間の途中で Entity を消す | 飛ばずに次のカメラへ補間する |
| 25 | 停止中の見え方 | Cam Close の Priority を一番上にして Game ビュー | 注視点と Volume が効く。1 秒あけた2枚の絵が一致して、揺れは出ない |
| 26 | Scene ビュー | Cam Close が使われている間の Scene ビュー | 再生中も停止中も、Camera Volume が掛からない（絵を目で確認） |
| 27 | Inspector | Cam Close の Camera Volume | チェックと値が1行に並ぶ。外すと灰色。Undo・Redo・保存が効く |
| 28 | Add Component | Camera の欄 | Camera Look At・Camera Shake・Camera Volume が出る |
| 29 | Test Impulse | 再生中に Camera Shake のボタン | 揺れて、元の絵に戻る |
| 30 | 3 構成 | Debug・Development・Release | ビルドと起動・終了が通る |

### F-3 検証の結果

- **ビルド：** Debug・Development・Release の3構成が通った。パートA の後も Development でエンジンのビルドが通った。
- **動き：** 固定の時間の Script でシナリオを流して、毎フレームの値をファイルへ書いて確かめた。Release と Debug で同じ数値になった。Debug のログに assert・警告・エラーは無かった。
- **Debug の実行：** シナリオを流した後に、使っていない include とコメントを1か所ずつ直した。その後の Debug はビルドだけ確かめた。Release はその後にシナリオを流し直した。
- **エディタの操作：** ImGui へ入力を流して確かめた。
- **キー：** 確認用の Script のキーは、キーボードの状態を写しの中で差し替えて確かめた。

### F-4 未確認のこと・気を付けること

- **本物のキーボード：** 表の操作を、手で押して通していない。
- **ビルドの警告：** エラーだけを見た。
- **表の 21：** Script からではなく検証用の処理から `SetGamePaused` を呼んで確かめた。確認用の Script にはキーを付けていない（ポーズ中は Script が止まるので、解除できない）。
- **Real のカメラの Camera Follow：** ヒットストップの間に Follow が動くことは、コードの通り道だけ確かめた（追う物も止まるので、絵は変わらない）。
- **XZ で追う物が跳ぶ：** 追う物の高さの面で測るので、傾いたカメラでは跳ぶと横にも少し動く。Dead Zone の中なら動かない。
- **強い手前のぼけ：** Max Blur 16 で手前の柱の上の端に、細い点線のような跡が出た。`DepthOfFieldPass` の値の範囲の中で、今回の変更では触っていない部分。
- **Near・Far の補間：** 値が大きく違うカメラの間では、補間の途中で映る範囲も動く。
- **Camera Look At の向き：** `GetMainCamera()` は注視点の入った向きを持つが、Transform は置いた向きのまま。カメラの子の Entity は Transform に付いていくので、注視点の向きには回らない。
