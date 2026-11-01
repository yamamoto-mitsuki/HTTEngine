参考：
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- 仕組みの先例（トラック・クリップ・再生の係）：Unity Timeline：https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/index.html
- 再生の係の先例：Unity PlayableDirector：https://docs.unity3d.com/ScriptReference/Playables.PlayableDirector.html
- `std::span`：https://en.cppreference.com/w/cpp/container/span
- `std::ranges::lower_bound`：https://en.cppreference.com/w/cpp/algorithm/ranges/lower_bound
- `std::fmod`：https://en.cppreference.com/w/cpp/numeric/math/fmod
- `std::lround`：https://en.cppreference.com/w/cpp/numeric/math/round

# UI 段階3：Timeline の再生の仕組み

時間に沿って値を動かし、指定の時間に処理を呼ぶ、汎用の Timeline を作る。UI とカメラの両方で使う。この段階は再生の仕組みだけで、編集の画面は UI 段階4で作る。

作る物は、`.timeline` のファイル（`TimelineManager`）、トラックの基底と種類の登録（`TimelineTrack`）、値のトラックとイベントのトラック、再生の Component と System（`TimelineComponent`・`TimelineSystem`）、自動の確認（`TimelineSelfTest`）、ファイルの仮の Inspector（`TimelineAssetEditor`）。

パートは **A → B → C → D の順に写す**（前のパートの物を使うので、順番は変えられない）。C の後で Debug のビルドが通ることを確かめた。E は確認の手順。

作業の前に、develop からブランチを切る。

```bash
git switch -c feature/Timeline develop
```

## 0章 全体の形

### 0.1 流れ

```
.timeline のファイル（TimelineManager が読む。全部の Entity で共有。再生中は書き換えない）
   Timeline → TimelineTrack（種類ごとのクラス）→ クリップ → CurveKey

Timeline Component（どのファイルをどう再生するか）
   │
   ▼
TimelineSystem::Update（Script の後、UpdateTransforms の前）
   │ 1. Entity ごとに TimelinePlayhead で時間を進めて、通った時間の幅を求める
   │ 2. トラックごとに Evaluate
   │     値：ComponentSerializer::SetValue → Describe 関数 → 本物の Component へ書く
   │     イベント：呼ぶ物を列に積む
   ▼ 3. 全部の Entity が終わったら、列のイベントを Script::OnTimelineEvent へ送る
```

### 0.2 作るファイル

| ファイル | 中身 |
|---|---|
| `Timeline/TimelineEvent.h` | イベント1回分の値 |
| `Timeline/TimelinePlayhead.h`・`.cpp` | 時間の進め方（Timeline も Entity も知らない計算だけ） |
| `Timeline/TimelineTrack.h`・`.cpp` | トラック・クリップの基底、種類の登録、`TIMELINE_TRACK` |
| `Timeline/TimelineValueTrack.h`・`.cpp` | Component の項目をカーブで動かすトラック |
| `Timeline/TimelineEventTrack.h`・`.cpp` | 指定の時間に処理を呼ぶトラック |
| `Timeline/TimelineManager.h`・`.cpp` | `.timeline` の読み書き |
| `Timeline/TimelineSelfTest.h`・`.cpp` | 自動の確認 |
| `Components/Animation/TimelineComponent.h` | 再生の設定 |
| `Systems/Animation/TimelineSystem.h`・`.cpp` | 再生の係 |
| `Editor/Inspector/TimelineAssetEditor.h`・`.cpp` | `.timeline` の仮の Inspector |

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 可変長のデータ | `.timeline` に持つ。Component はファイルの番号と再生の設定だけ | Component はバイトで写すので、数の変わる物を持てない。`AttackTable` と同じ |
| 共有 | トラックはファイル1つにつき1つで、再生中は const。Entity ごとに変わる値は `TimelineTrackState` と `TimelinePlayhead` | 同じファイルを何体でも同時に再生できる。Entity の数だけカーブを持たずに済む |
| トラックの種類 | クラス（`TimelineTrack` の派生）。`TIMELINE_TRACK` か `TimelineTrackRegistrar` で表に登録し、ファイルの `type` から作る | 本体を変えずに足せる。`ATTACK_PART` と同じ形 |
| 値の書き方 | `Describe` 関数を本物の Component に呼び、名前の合う項目へ書く（`SetValue`） | Inspector と保存が使う入口と同じなので、Component ごとの登録が要らない。1.3 |
| 毎フレーム `Describe` を呼ぶ | 値の場所（アドレス）を覚えない | `Transform` の rotation はクォータニオンで、書く先が Component の中に無い。`Describe` の中の書き戻しに任せれば特別扱いが要らない |
| 書ける形 | float・int・bool・Vector2〜4・色。文字・Entity・アセット・enum は書けない | カーブで表せる物だけ。文字の差し替えはイベントで書ける |
| 成分 | クリップに4本のカーブ。キーの無い成分は書かない | `color` の a だけ動かすと r・g・b はそのまま |
| 対象 | 再生する Entity からの子の名前のパス（`Panel/Title`）。空なら自分 | Prefab・`.uiscreen` は作るたびに EntityId が変わる。名前の並びなら同じ物を指す |
| 同じ名前の子 | 先の物 | パスに番号を持たせない。名前を分けるのが一番分かりやすい |
| 対象が無いとき | そのトラックだけ飛ばす。警告はトラックごと・再生している Entity ごとに1回。Entity の数が変わったら探し直す | 1つの対象が無いだけで全部を止めない。あとから出した子にも結び付く |
| 再生の設定 | `playOnStart`・`loop`・`speed`・`timeMode` は Component に持つ | Inspector で直せて、ゲームが書き換えてもすぐ効く |
| 今の時間 | System が持つ | Component に置くと、再生中の値が Undo・保存に混ざる |
| 最初のクリップの前 | そのクリップの始まりの値を書く | 何も書かないと、Stop・ループで前の値が残る |
| クリップの間 | 前のクリップの終わりの値 | 値が飛ばない |
| 止まっている間 | 値を書かない（シーク・Stop の直後の1回だけ書く） | 止めた後にゲームが書いた値を上書きしない |
| 最後まで再生した | `Finished`。最後の値のまま | `Stop` で 0 に戻るのと区別する |
| 速さ | 0 以上。逆再生はしない | 頼みに無く、イベントの決まりが倍に増える |
| 時間の種類 | `TimeMode`（Game・Real）にして、Camera と共有する | enum と切り替えの処理を2つ持たない。保存の文字は同じなので、前のシーンも読める |
| Script の Fields | 動かせる。Script も同じ表に入っているので、`component` に Script の名前を書く | 特別な処理が要らない |
| Inspector | `.timeline` は長さとトラックの設定まで。クリップは手で書く | クリップの編集は UI 段階4 |
| Undo | `.timeline` には無い（`.bak` は残る） | 編集の画面と一緒に作る |

### 0.4 やらなかったこと

| 項目 | 理由 |
|---|---|
| カメラのトラック | カメラ段階2の作業。足し方は 1.10 |
| クリップの編集画面 | UI 段階4 |
| 重なったクリップの混ぜ合わせ | 同じトラックのクリップは重ねない前提。重なったら後に始まった方が勝つ |
| 今の値からの相対（足し算）のトラック | 頼みに無い。`TimelineValueTrack` に項目を1つ足せば後からできる |
| 再生前の値へ戻す Stop | `Stop` は Timeline の 0 の値へ戻す |
| 1つの Entity で2つの Timeline | Component は1 Entity に1つ。子の Entity に付ければ同時に再生できる |
| 逆再生 | 上の表のとおり |

### 0.5 あとの段階への備え

| 段階 | 今の形で残した物 |
|---|---|
| UI 段階4（Timeline ウィンドウ） | クリップは `TimelineTrackOf<Clip>::clips` で読み書きできる。値のカーブは `Curve` なので `CurveEditor` がそのまま使える。`TimelineManager::GetForEdit`・`MarkChanged` で再生中も反映できる。Undo は `ToJson` の文字を前後で持てばよい |
| UI 段階5（UI の統合） | 値は Component の項目の名前で指すので、`VectorShape` の項目もそのまま動く。項目の一覧が要るときは、`SetValue` と同じ形の係を足せる |
| カメラ段階2 | 1.10 |

## 1章 解説

### 1.1 トラック・クリップ・キー

```
Timeline（長さ 2 秒）
├ トラック：Image の color（対象 Title）
│   └ クリップ：0 秒から 1 秒（x・y・z・w のカーブ。キーの時間はクリップの始まりから）
│       └ キー：CurveKey（UI 段階2 と同じ）
├ トラック：Transform の translation（対象 Title）
│   └ クリップ …
└ トラック：イベント
    └ クリップ：0.5 秒 "Half"（長さは無い）
```

ファイルの形。

```json
{
	"settings": {"length": 2.0},
	"tracks": [
		{
			"type": "TimelineValueTrack",
			"settings": {"name": "Fade", "target": "Panel/Title", "isMuted": false, "component": "Image", "property": "color"},
			"clips": [
				{"start": 0.0, "duration": 1.0, "curves": {"w": {"keys": [
					{"time": 0.0, "value": 0.0, "interp": "Linear"},
					{"time": 1.0, "value": 1.0}
				]}}}
			]
		},
		{
			"type": "TimelineEventTrack",
			"settings": {"name": "Calls", "target": ""},
			"clips": [{"start": 0.5, "name": "Half", "value": "何でも"}]
		}
	]
}
```

`component` は Inspector の見出し、`property` はシーンファイルに書かれている項目の名前（`"表示名###名前"` の名前）。Transform は `translation`・`rotation`（度）・`scale`。色は x, y, z, w が r, g, b, a。`keys` の中は `.curve` のファイルと同じ。

知らない `type` のトラックは、動かさずに JSON のまま覚えて、保存でも消さない（別のゲームで作った種類が消えないように）。

### 1.2 共有する物と、Entity ごとの物

同じ `.timeline` を3つの Entity で再生するとき。

```
Timeline（1つ）              TimelineRuntime（Entity ごと）
  トラック0  ─────────────  TimelineTrackState：対象の Title（Entity A の子）
  トラック1                 TimelinePlayhead：今の時間
```

`TimelineTrack::Evaluate` は `const` で、再生中にトラックを書き換えない。Entity ごとに覚える物は、`CreateState` で作った `TimelineTrackState` に持つ。基底が対象の Entity を覚えるので、ふつうの種類は継承しなくてよい。

### 1.3 任意の Component の値を動かす

エンジンの Component は全部 `Describe` 関数を持ち、`ui.Field` の並びが Inspector と保存の両方になっている。`ComponentUI` の係を差し替えると、同じ関数で別のことができる。

| 係 | `ui.Field("Color###color", value.color)` でしていること |
|---|---|
| Inspector | 色の欄を描く |
| `JsonWriterUI` | `"color": [...]` を書く |
| `JsonReaderUI` | `"color"` を読んで `value.color` へ入れる |
| `ValueWriterUI`（今回） | 名前が `color` なら、値を `value.color` へ入れる |

`SetValue` は `ValueWriterUI` を作って、本物の Component に `Describe` を呼ぶ。名前の合う項目で値が書かれて、ほかの項目では何も起きない。

- **Component ごとの登録が要らない。** Inspector に出す項目は、何もしなくても動かせる。
- **Transform の回転も同じ。** `DescribeTransform` は角度を一時変数に作って `ui.Field` に渡し、変わっていたら `SetEulerAngles` で戻す。値を書く係が一時変数を書き換えれば、その書き戻しが動く。
- **保存の `save` は使えない。** `ComponentSerializer::Register` の `save` は写しに `Describe` を呼ぶので、書いても本物が変わらない。なので本物に呼ぶ `visit` を足した。

毎フレーム、トラックごとに `Describe` が1回走る。項目の名前の比較が数十回あるだけで、`string_view` の比較なので確保は無い。

### 1.4 対象の結び付け

| 方法 | 問題 |
|---|---|
| `EntityRef`（EntityId） | Prefab・`.uiscreen` から作るたびに番号が変わる。ファイルに書けない |
| シーン全体の名前で探す | 同じ Prefab を2つ作ると、どちらか分からない |
| 再生する Entity からの名前のパス | 作るたびに同じ形になる。同じファイルを何体で再生しても、それぞれ自分の子を指す |

`EntityManager::FindByPath` は全部の Entity を調べる `GetChildren` を段の数だけ呼ぶので、見つけたら `TimelineTrackState::target` に覚えて、毎フレームは探さない。

| 起きたこと | 動き |
|---|---|
| 対象が見つからない | そのトラックだけ飛ばす。警告を1回出す。ほかのトラック・時間はそのまま進む |
| あとから対象が増えた | Entity の数が変わったフレームに探し直して、結び付く |
| 覚えていた対象が消えた | すぐ探し直す。無ければ上と同じ |
| Component が無い・項目が無い | そのトラックだけ飛ばす。警告を1回出す。毎フレーム試すので、あとから付けば動く |
| 再生する Entity が消えた | 実行中の値を一緒に消す |

### 1.5 時間の進め方とイベント

イベントは「時間がその点を通った」ときに呼ぶ。`TimelinePlayhead::Advance` は、このフレームに通った時間の幅を返す。

```
幅は (前の時間, 今の時間]  …  始まりは含まず、終わりは含む
ただし再生の最初とループの頭だけ [0, 今の時間]
```

こうすると、幅をつなげたときに同じ点を2回通らない。

| 操作 | イベント |
|---|---|
| 再生 | 通ったイベントを1回ずつ。ちょうど止まった時間のイベントも1回 |
| 1フレームで大きく進んだ | 通った全部を、時間の順に1回ずつ |
| 同じ時間のイベント | ファイルの順 |
| Pause | 呼ばない。Play で続きから |
| Seek | **呼ばない。** 飛ばした間も、シークした先ちょうどの物も呼ばない。値はすぐ書く |
| 前へ Seek してから再生 | もう一度通るので、また呼ぶ |
| Stop | 呼ばない。次の Play は 0 ちょうどのイベントも呼ぶ |
| Loop | 周ごとに全部1回ずつ。長さちょうどのイベントと 0 ちょうどのイベントは、別々に1回ずつ呼ぶ |
| 1フレームに何周もした | 周ごとに呼ぶ。上限は `kMaxLoopsPerFrame`（16 周）で、超えた分の周は数えない |
| 最後まで再生した | 最後のイベントまで呼んで `Finished`。Play で最初から |
| 長さ 0 の Timeline | 0 のイベントを1回呼んで `Finished`（Loop でも止まる） |

### 1.6 イベントを最後にまとめて呼ぶ理由

イベントの中で `Stop`・`Destroy`・別の Timeline の `Play` をしても壊れないように、全部の Entity の値を書き終えてから呼ぶ。トラックの途中で呼ぶと、回している `runtimes` の配列が伸びて参照が壊れることがある。

同じフレームに積まれたイベントは、途中で Stop しても呼ばれる。

`Script::OnTimelineEvent` はトラックの Target の Entity に付いている、動いている Script に来る。Real の Timeline はポーズ中も動くので、ポーズ中に呼ばれることがある。Script 以外で受け取るときは `TimelineSystem::AddEventListener`。

### 1.7 時間の種類

| TimeMode | スロー | ヒットストップ・ポーズ | エディタの一時停止 |
|---|---|---|---|
| Game | 遅くなる | 止まる | 止まる |
| Real | そのまま | 動く | 止まる |

ポーズ画面の `.uiscreen` は Real にする。

### 1.8 使う・読む

```cpp
TimelineSystem::Play(entity);        // 止まっていれば最初から、一時停止中なら続きから
TimelineSystem::Pause(entity);
TimelineSystem::Stop(entity);        // 時間を 0 に戻して、0 の値を書く
TimelineSystem::Seek(entity, 1.5f);

const TimelineStatus status = TimelineSystem::GetStatus(entity);
status.state;     // Stopped・Playing・Paused・Finished
status.time;      // 今の時間
status.duration;  // 長さ
status.loopCount; // 最初へ戻った回数
status.IsPlaying();
status.GetProgress(); // 0〜1
```

`Loop`・`Speed` は Component を書き換えるとすぐ効く。

```cpp
GetComponent<TimelineComponent>()->speed = 0.5f;
```

イベントを受け取る。

```cpp
class Door : public Script {
public:
	void OnTimelineEvent(const TimelineEvent& event) override {
		if (event.name == "Open") {
			// event.value・event.time・event.owner・event.target が読める
		}
	}
};
```

### 1.9 新しいトラックを足す手順

本体のファイルは1つも変えない。見本は `TimelineTest/TimelineTester.cpp` の `ImpulseTrack`。

**1. クリップを書く。** `TimelineClip` を継承して、値と `Fields` を足す。`Fields` の並びがファイルの項目になる。

```cpp
#include <MyEngine/Timeline/TimelineTrack.h>

struct ImpulseClip : TimelineClip {
	float strength = 0.5f; // 揺れの強さ

	void Fields(ComponentUI& ui) {
		TimelineClip::Fields(ui); // start・duration
		ui.Field("Strength###strength", strength, AtLeast(0.0f));
	}
};
```

**2. トラックを書く。** `TimelineTrackOf<クリップ>` を継承して `Evaluate` を書く。ファイルの読み書きは基底がする。

```cpp
class ImpulseTrack : public TimelineTrackOf<ImpulseClip> {
public:
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& /*state*/) const override {
		ForEachStarted(frame.ranges, [](const ImpulseClip& clip) { CameraSystem::AddImpulse(clip.strength); });
	}
};
```

| 使える物 | 用途 |
|---|---|
| `ForEachStarted(frame.ranges, 関数)` | このフレームに始まったクリップ（イベントと同じ決まり） |
| `FindClip(frame.time)` | 今の時間のクリップ（値のように、毎フレーム決める物） |
| `FindTarget(frame, state)` | 対象の Entity（見つからなければ無効な Handle。警告は出してある） |
| `WarnOnce(state, "文")` | 警告を1回だけ出す |
| `frame.events->push_back(...)` | イベントを積む |

**3. 登録する。** ゲーム側の `.cpp` に1行書く。クラス名がファイルの `type` になる。

```cpp
TIMELINE_TRACK(ImpulseTrack, "クリップの始まる時間にカメラを揺らす");
```

エンジンの中に足すときは、`.lib` の中の `TIMELINE_TRACK` はリンカに捨てられるので、`TimelineSystem::Initialize` に `TimelineTrackRegistrar<型>("名前", "説明");` と書く。

**4. ファイルに書く。** Inspector の Add Track にも出る。

```json
{"type": "ImpulseTrack", "settings": {"name": "Shake"}, "clips": [{"start": 0.5, "duration": 0.0, "strength": 1.0}]}
```

**任意：Entity ごとの値が要るとき。** `TimelineTrackState` を継承して `CreateState` で作り、`Evaluate` で `static_cast` する。

**任意：ComponentUI で書けないクリップの値。** `Curve` のように `Fields` で書けない物は、クリップに `WriteExtra`・`ReadExtra` を書く（`TimelineValueClip` が見本）。

### 1.10 カメラ切り替えの足し方（カメラ段階2）

ここでは作らない。上と同じ手順で、エンジンに1種類足せば済む。

```
クリップ：start・duration・カメラの Entity のパス
Evaluate：ForEachStarted で始まったクリップのカメラへ切り替える（CameraSystem の切り替えの入口を呼ぶ）
```

対象のカメラはシーンの Entity なので、再生する Entity の子でないときは、パスの代わりの指し方をカメラ段階2で決める。`Timeline` 本体と `TimelineSystem` は変えない。

### 1.11 使った STL の解説

**`std::span<const TimelineRange>`**
配列の場所と数だけを持つ、写さない入れ物。`std::vector` をそのまま渡せる。トラックに通った幅を渡すのに使う。持ち続けると元の配列が動いたときに壊れるので、`Evaluate` の間だけ使う。

**`std::ranges::lower_bound`・`upper_bound`**
`lower_bound` は値以上の最初、`upper_bound` は値より大きい最初を、二分探索で返す。イベントの幅の始まりを含むかどうかで使い分ける。

**`std::fmod(a, b)`**
小数の割り算の余り。ループで長さを超えた分を戻すのに使う。引き算をくり返すと、大きな秒で回数が増える。

**`std::lround(x)`**
四捨五入して整数にする。`static_cast<int>` は切り捨てなので、カーブの 2.6 が 2 になる。

**`std::is_base_of_v`・`static_assert`**
型の条件をコンパイルのときに確かめる。`ATTACK_PART` と同じく、間違えると分かりやすいエラーが出る。

## パートA 土台

新しいファイルは VS で `MyEngine_v1` プロジェクトに追加して、最後に次を実行するとフィルタがフォルダの形になる（`project` フォルダで）。

```bash
powershell -ExecutionPolicy Bypass -File Tools\UpdateFilters.ps1 MyEngine_v1.vcxproj
```

### A-1 `Time/EngineTime.h`

`// 時間計測クラス` の上に足す。

```cpp
/// <summary>
/// 動くのに使う時間の種類（Component の Time Mode の項目）
/// </summary>
enum class TimeMode {
	Game, // ゲームの時間（スローで遅くなり、ヒットストップ・ポーズで止まる）
	Real, // 止まらない時間（スロー・ヒットストップ・ポーズの間も同じ速さで動く。エディタの一時停止では止まる）
};

// 時間計測クラス
```

`GetRealDeltaTime` の下に足す。

```cpp
	static float GetRealDeltaTime() { return instance_->deltaTime_ * instance_->pauseScale_; }
	// 種類を選んで読む
	static float GetDeltaTime(TimeMode mode) { return (mode == TimeMode::Real) ? GetRealDeltaTime() : GetDeltaTime(); }
```

### A-2 `Components/Camera/CameraComponent.h`

include に足す。

```cpp
#include "MyEngine/Math/Easing.h"
#include "MyEngine/Time/EngineTime.h"
```

`enum class CameraTimeMode` を、上の `/// <summary>` ごと消す。`timeMode` の行の型を変える（行の右のコメントはそのまま）。

```cpp
	TimeMode timeMode = TimeMode::Game;
```

### A-3 `Systems/Camera/CameraSystem.cpp`

補間の情報の `timeMode` の行の型を変える（行の右のコメントはそのまま）。

```cpp
	TimeMode timeMode = TimeMode::Game;
```

`GetCameraDeltaTime` の2つを、次の1つにする。

```cpp
// ===== その Entity のカメラが使う時間の秒（Camera Component が無ければゲームの時間）=====
float GetCameraDeltaTime(Handle<Entity> entity) {
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return Time::GetDeltaTime(camera ? camera->timeMode : TimeMode::Game);
}
```

`blend.elapsed` を足す行を変える。

```cpp
	blend.elapsed += Time::GetDeltaTime(blend.timeMode);
```

### A-4 `ECS/ComponentSerializer.h`

include に足す。

```cpp
#include <set>
#include <array>
#include <string>
```

`EntityIdMap` の下に足す。

```cpp
/// <summary>
/// Component の1つの項目へ書く値（SetValue）。1つの float なら x、色なら r, g, b, a の順
/// </summary>
struct ComponentValue {
	static constexpr size_t kChannelCount = 4; // 成分の数の上限

	std::array<float, kChannelCount> channels{}; // [x, y, z, w] 書く値
	std::array<bool, kChannelCount> isUsed{};    // [x, y, z, w] false の成分は今の値のまま
};

/// <summary>
/// SetValue の結果
/// </summary>
enum class ComponentValueResult {
	Written,     // 書いた
	NoComponent, // その Entity に Component が無い（名前が違う）
	NoField,     // 項目が無い。または書けない形（文字・Entity・アセット・enum）
};
```

`Register` の `RegisterErased` の最後に、3つ目の関数を足す。

```cpp
			    T value{};
			    describe(reader, value);
			    EntityManager::RestoreComponents(entity, {ComponentSnapshot::Make(value)});
		    },
		    // --- 本物に呼ぶ：持っていれば、本物に対して見せ方の関数を呼ぶ（SetValue。渡された係が書き換える）---
		    [describe](Handle<Entity> entity, ComponentUI& ui) {
			    T* component = EntityManager::Get<T>(entity);
			    if (component == nullptr) {
				    return false; // 持っていない
			    }
			    describe(ui, *component);
			    return true;
		    });
	}
```

`Load` の下に足す。

```cpp
	/// <summary>
	/// Component の1つの項目へ値を書く（Timeline）。見せ方の関数を本物に呼ぶので、Inspector に出せる項目なら Component の種類を問わず書ける
	/// <para>書けるのは float・int（四捨五入）・bool（0.5 以上で true）・Vector2〜4・色。Transform の rotation は度で書く</para>
	/// </summary>
	/// <param name="typeName">保存の名前（Inspector の見出しと同じ。"Image"・Script の名前）</param>
	/// <param name="key">項目の保存の名前（"表示名###名前" の名前）</param>
	static ComponentValueResult SetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, const ComponentValue& value);
```

`RegisterErased` の宣言を変える。

```cpp
	// 型を消した形で表に入れる（名前順に並べる）。save・load の中で、渡された ComponentUI に見せ方の関数を呼ぶ
	// visit は本物に見せ方の関数を呼ぶ（持っていなければ false。save と違い、写しに呼んではいけない）
	static void RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit);
```

### A-5 `ECS/ComponentSerializer.cpp`

include に足す。

```cpp
#include <cmath>
#include <algorithm>
#include <filesystem>
```

`Entry` に足す。

```cpp
	std::function<void(Handle<Entity>, ComponentUI&)> load; // 読んでその場で付ける
	std::function<bool(Handle<Entity>, ComponentUI&)> visit; // 持っていれば本物に見せ方の関数を呼んで true
};
```

`KeyOf` を2つに分ける（値を書く係は毎フレーム呼ぶので、文字を作らない形を足す）。

```cpp
// "新しい表示名###前の名前" と書けば、前に保存したファイルもそのまま読める
std::string_view KeyViewOf(const char* label) {
	const std::string_view text(label);
	const size_t mark = text.find("###");
	return (mark == std::string_view::npos) ? text : text.substr(mark + 3);
}

std::string KeyOf(const char* label) { return std::string(KeyViewOf(label)); }
```

`JsonReaderUI` の下、`} // namespace` の前に足す。

```cpp

//=============================================================================
// 値を書く係（見せ方の関数に渡すと、名前の合う項目にだけ値を書く）
//=============================================================================
class ValueWriterUI final : public ComponentUI {
public:
	ValueWriterUI(std::string_view key, const ComponentValue& value) : key_(key), value_(value) {}

	bool IsWritten() const { return isWritten_; }

	void Field(const char* label, float& value, FieldStyle) override { WriteFloats(label, &value, 1); }
	void Field(const char* label, int& value, FieldStyle) override {
		if (Match(label) && value_.isUsed[0]) {
			value = static_cast<int>(std::lround(value_.channels[0]));
		}
	}
	void Field(const char* label, bool& value, FieldStyle) override {
		if (Match(label) && value_.isUsed[0]) {
			value = value_.channels[0] >= 0.5f;
		}
	}
	void Field(const char* label, Vector2& value, FieldStyle) override { WriteFloats(label, &value.x, 2); }
	void Field(const char* label, Vector3& value, FieldStyle) override { WriteFloats(label, &value.x, 3); }
	void Field(const char* label, Vector4& value, FieldStyle) override { WriteFloats(label, &value.x, 4); }
	void ColorField(const char* label, Vector3& rgb) override { WriteFloats(label, &rgb.x, 3); }
	void ColorField(const char* label, Vector4& rgba) override { WriteFloats(label, &rgba.x, 4); }

	// 数で表せない項目は書かない（isWritten_ が false のままなので、NoField になる）
	void Field(const char*, EntityRef&, FieldStyle) override {}
	void Field(const char*, std::string&, FieldStyle) override {}
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	void EnumField(const char*, size_t&, std::span<const std::string_view>, FieldStyle) override {}

private:
	// 名前が合えば、書いたことにする
	bool Match(const char* label) {
		if (KeyViewOf(label) != key_) {
			return false;
		}
		isWritten_ = true;
		return true;
	}

	// 使う成分だけ書く（x, y, z, w は並んでいるので、先頭のポインタから順に書ける）
	void WriteFloats(const char* label, float* out, size_t count) {
		if (!Match(label)) {
			return;
		}
		for (size_t i = 0; i < count; ++i) {
			if (value_.isUsed[i]) {
				out[i] = value_.channels[i];
			}
		}
	}

	std::string_view key_;        // 書く項目の名前
	const ComponentValue& value_; // 書く値
	bool isWritten_ = false;      // 書いた
};
} // namespace
```

`RegisterErased` の定義を変える。

```cpp
void ComponentSerializer::RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit) {
```

同じ関数の `entries.insert` の行を変える。

```cpp
	entries.insert(position, Entry{name, std::move(save), std::move(load), std::move(visit)});
```

「見せ方の関数1つ分を書く・読む」の大見出しの上に足す。

```cpp
//=============================================================================
// 値を書く（Timeline）
//=============================================================================
ComponentValueResult ComponentSerializer::SetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, const ComponentValue& value) {
	const Entry* entry = FindEntry(typeName);
	ValueWriterUI writer(key, value);
	if (entry == nullptr || !entry->visit(entity, writer)) {
		return ComponentValueResult::NoComponent;
	}
	return writer.IsWritten() ? ComponentValueResult::Written : ComponentValueResult::NoField;
}
```

### A-6 `ECS/EntityManager.h`

include に足す。

```cpp
#include <string>
#include <string_view>
#include <type_traits>
```

`GetChildren` の下に足す。

```cpp
	// root から子の名前を / でつないだパス（"Panel/Title"）で探す。空なら root。同じ名前の子が2つあれば先の物。無ければ無効なHandle
	// Prefab・画面（UI Screen）から作るたびに EntityId が変わる物を、作り直しても同じ相手として指すのに使う。全部の Entity を調べるので、毎フレームは呼ばない
	static Handle<Entity> FindByPath(Handle<Entity> root, std::string_view path);
```

### A-7 `ECS/EntityManager.cpp`

`GetRoots` の上に足す。

```cpp
Handle<Entity> EntityManager::FindByPath(Handle<Entity> root, std::string_view path) {
	Handle<Entity> current = root;
	std::vector<Handle<Entity>> children;
	while (!path.empty() && IsAlive(current)) {
		// --- 次の / までが、1段下の子の名前 ---
		const size_t slash = path.find('/');
		const std::string_view name = path.substr(0, slash);
		GetChildren(current, children);
		const auto found = std::ranges::find_if(children, [name](Handle<Entity> child) { return Get(child)->name == name; });
		if (found == children.end()) {
			return {};
		}
		current = *found;
		path = (slash == std::string_view::npos) ? std::string_view{} : path.substr(slash + 1);
	}
	return IsAlive(current) ? current : Handle<Entity>{};
}
```

### A-8 `Timeline/TimelineEvent.h`（新しいファイル）

```cpp
#pragma once
#include <string>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"

/// <summary>
/// Timeline のイベント1回分（Script::OnTimelineEvent・TimelineSystem::AddEventListener に渡す）
/// </summary>
struct TimelineEvent {
	Handle<Entity> owner;  // 再生している Entity
	Handle<Entity> target; // トラックの対象の Entity（Target が空なら owner と同じ）
	std::string name;      // イベントの名前
	std::string value;     // イベントに付けた文字（使い方は受け取る側が決める）
	float time = 0.0f;     // イベントの時間（秒）
};
```

### A-9 `ECS/Script.h`

include の最後に足す。

```cpp
#include "MyEngine/Time/EngineTime.h"
#include "MyEngine/Timeline/TimelineEvent.h"
```

`// ===== Inspector に出す・保存する値 =====` の上に足す。

```cpp
	// ===== Timeline =====
	// Timeline のイベントトラックの時間になった（トラックの Target の Entity に付いている Script に来る）
	virtual void OnTimelineEvent(const TimelineEvent& /*event*/) {}
```

### A-10 `Systems/Gameplay/ScriptSystem.h`

前方宣言に足す。

```cpp
class ComponentUI;
struct TimelineEvent;
```

`RequestDestroy` の上に足す。

```cpp
	// entity に付いている動いている Script 全部の OnTimelineEvent を呼ぶ（TimelineSystem から）
	static void SendTimelineEvent(Handle<Entity> entity, const TimelineEvent& event);
```

### A-11 `Systems/Gameplay/ScriptSystem.cpp`

`RegisterType` の保存の登録を、次にする（書く関数を `visit` に取り出して、2か所で使う）。

```cpp
	// --- 保存（シーンファイル・Play を押した瞬間の退避）。データの Component と同じ表に、同じ形で入る ---
	// 書く：付いていれば、見せ方の関数に係を渡す（書く係は値を変えないので、本物に呼んでよい。SetValue の係もそのまま使える）
	const auto visit = [type](Handle<Entity> entity, ComponentUI& ui) {
		Script* script = Get(entity, type);
		if (script == nullptr) {
			return false; // 付いていない
		}
		Describe(*script, ui);
		return true;
	};
	ComponentSerializer::RegisterErased(
	    name, visit,
	    // 読む：付けてから、ファイルにある項目だけ上書きする
	    [type](Handle<Entity> entity, ComponentUI& reader) {
		    if (Script* script = Add(entity, type)) {
			    Describe(*script, reader);
		    }
	    },
	    visit);
```

`FlushDestroy` の上に足す。

```cpp
void ScriptSystem::SendTimelineEvent(Handle<Entity> entity, const TimelineEvent& event) {
	// 呼んだ先で Script が増えても大丈夫なように、添字で回す（Update と同じ）
	const size_t count = scripts.size();
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (script.gameObject_.handle == entity && IsRunning(script)) {
			script.OnTimelineEvent(event);
		}
	}
}
```

## パートB Timeline 本体

### B-1 `Timeline/TimelinePlayhead.h`（新しいファイル）

```cpp
#pragma once
#include <vector>

/// <summary>
/// 再生の状態
/// </summary>
enum class TimelinePlayState {
	Stopped,  // 止まっている（時間は 0）
	Playing,  // 再生中
	Paused,   // 一時停止中（Play で続きから）
	Finished, // 最後まで再生した（Loop でないとき。最後の値のまま）
};

/// <summary>
/// このフレームに通った時間の幅。イベントはこの幅に入った物を呼ぶ
/// </summary>
struct TimelineRange {
	float start = 0.0f;         // 始まり
	float end = 0.0f;           // 終わり（含む）
	bool includesStart = false; // 始まりちょうどを含むか（再生の最初とループの頭だけ true）
};


/// <summary>
/// 再生している時間の位置を進める係（Timeline の中身も Entity も知らない。時間の計算だけ）
/// <para>Advance が返す幅を全部つなぐと、同じ時間を2回通らず、通っていない時間も無い</para>
/// </summary>
class TimelinePlayhead {
public:
	static constexpr int kMaxLoopsPerFrame = 16; // 1フレームに数える周の上限（とても短い Timeline でも、ここで止まる）

	// 再生する。Stopped・Finished なら最初から、Paused なら続きから
	void Play();
	// 一時停止（再生中のときだけ）
	void Pause();
	// 止めて時間を 0 に戻す
	void Stop();
	// time へ移動する（duration の中に収める）。この時間を通ったことにはしない
	// Playing は再生を続ける。それ以外は Paused になる（Play で続きから）
	void Seek(float time, float duration);

	/// <summary>
	/// 時間を進めて、通った幅を out に入れる（out は最初に空にする）
	/// </summary>
	/// <param name="deltaTime">進める秒（0 以下・NaN なら進めない）</param>
	/// <param name="duration">Timeline の長さ（秒）</param>
	/// <param name="isLoop">最後まで行ったら最初へ戻るか</param>
	void Advance(float deltaTime, float duration, bool isLoop, std::vector<TimelineRange>& out);

	TimelinePlayState GetState() const { return state_; }
	float GetTime() const { return time_; }
	int GetLoopCount() const { return loopCount_; } // 最初へ戻った回数

private:
	TimelinePlayState state_ = TimelinePlayState::Stopped;
	float time_ = 0.0f;         // 今の時間（秒）
	bool includesStart_ = true; // 次に進めるとき、今の時間ちょうどのイベントも呼ぶか
	int loopCount_ = 0;         // 最初へ戻った回数
};
```

### B-2 `Timeline/TimelinePlayhead.cpp`（新しいファイル）

```cpp
#include "TimelinePlayhead.h"

#include <cmath>
#include <algorithm>

//=============================================================================
// 操作
//=============================================================================
void TimelinePlayhead::Play() {
	switch (state_) {
	case TimelinePlayState::Playing:
		return;
	case TimelinePlayState::Paused:
		break; // 続きから
	case TimelinePlayState::Stopped:
	case TimelinePlayState::Finished:
		time_ = 0.0f;
		includesStart_ = true;
		loopCount_ = 0;
		break;
	}
	state_ = TimelinePlayState::Playing;
}

void TimelinePlayhead::Pause() {
	if (state_ == TimelinePlayState::Playing) {
		state_ = TimelinePlayState::Paused;
	}
}

void TimelinePlayhead::Stop() {
	state_ = TimelinePlayState::Stopped;
	time_ = 0.0f;
	includesStart_ = true;
	loopCount_ = 0;
}

void TimelinePlayhead::Seek(float time, float duration) {
	time_ = std::isnan(time) ? 0.0f : std::clamp(time, 0.0f, (std::max)(duration, 0.0f));
	includesStart_ = false; // シークした先のイベントは呼ばない
	if (state_ != TimelinePlayState::Playing) {
		state_ = TimelinePlayState::Paused;
	}
}

//=============================================================================
// 進める
//=============================================================================
void TimelinePlayhead::Advance(float deltaTime, float duration, bool isLoop, std::vector<TimelineRange>& out) {
	out.clear();
	if (state_ != TimelinePlayState::Playing || !(deltaTime > 0.0f)) {
		return; // !(> 0) なので NaN もここで止まる
	}
	// --- 長さ 0 の Timeline：0 ちょうどの物だけ呼んで終わる（割り算を避ける）---
	if (duration <= 0.0f) {
		if (includesStart_) {
			out.push_back({0.0f, 0.0f, true});
		}
		time_ = 0.0f;
		includesStart_ = false;
		state_ = TimelinePlayState::Finished;
		return;
	}
	// --- 最後まで届かない ---
	const float end = time_ + deltaTime;
	if (end < duration) {
		out.push_back({time_, end, includesStart_});
		time_ = end;
		includesStart_ = false;
		return;
	}
	// --- 最後まで届いた ---
	out.push_back({time_, duration, includesStart_});
	includesStart_ = false;
	if (!isLoop) {
		time_ = duration;
		state_ = TimelinePlayState::Finished;
		return;
	}
	// --- ループ：丸ごと通った周の分だけ全体の幅を足して、最後に余りの幅を足す ---
	const float remaining = end - duration;
	const int wholeLoops = (std::min)(static_cast<int>(std::floor(remaining / duration)), kMaxLoopsPerFrame);
	for (int i = 0; i < wholeLoops; ++i) {
		out.push_back({0.0f, duration, true});
		++loopCount_;
	}
	++loopCount_;
	time_ = std::fmod(remaining, duration);
	out.push_back({0.0f, time_, true});
}
```

### B-3 `Timeline/TimelineTrack.h`（新しいファイル）

```cpp
#pragma once
#include <set>
#include <span>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <type_traits>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Timeline/TimelineEvent.h"
#include "MyEngine/Timeline/TimelinePlayhead.h"

// Timeline のトラックを書くための基底クラス。ゲーム側で新しい種類を足すときは、このヘッダ1つをインクルードする
//
//   // FlashTrack.cpp
//   struct FlashClip : TimelineClip {
//       float strength = 1.0f;
//       void Fields(ComponentUI& ui) { TimelineClip::Fields(ui); ui.Field("Strength###strength", strength); }
//   };
//   class FlashTrack : public TimelineTrackOf<FlashClip> {
//   public:
//       void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override { ... }
//   };
//   TIMELINE_TRACK(FlashTrack, "画面を光らせる"); // これでファイルの "type": "FlashTrack" から作れる
//
// トラックは Timeline のファイル1つにつき1つで、再生している全部の Entity が同じ物を見る。Evaluate は const で、再生中に書き換えない
// Entity ごとに変わる値は TimelineTrackState を継承して持つ


/// <summary>
/// 1フレーム分の再生の様子（トラックの Evaluate に渡す）
/// </summary>
struct TimelineFrame {
	Handle<Entity> owner;                  // 再生している Entity
	float time = 0.0f;                     // 今の時間（秒）
	std::span<const TimelineRange> ranges; // このフレームに通った時間の幅（シーク・停止で値を書き直すだけのときは空）
	std::vector<TimelineEvent>* events = nullptr; // 呼ぶイベントを足す場所（全部の値を書いた後にまとめて呼ばれる）
};

/// <summary>
/// トラック1つの、Entity ごとの値。増やしたいときは継承して、CreateState で作る
/// </summary>
class TimelineTrackState {
public:
	virtual ~TimelineTrackState() = default;

	Handle<Entity> target;       // 見つけた対象（FindTarget が覚えておく）
	size_t searchedEntities = 0; // 対象を探したときの Entity の数（増減していなければ、探し直さない）
	bool isSearched = false;     // 1回でも探した
	bool isWarned = false;       // 対象についての警告を出した（1回だけ出す）
};


/// <summary>
/// トラック（時間に沿って1つの物を動かす列）の基底クラス
/// <para>持つ物：名前・対象・クリップ。動かし方は種類ごとに Evaluate へ書く</para>
/// </summary>
class TimelineTrack {
public:
	virtual ~TimelineTrack() = default;

	std::string typeName;      // 種類の名前（TimelineTrackRegistry::Create が入れる。ファイルの "type"）
	std::string name = "Track"; // トラックの名前（確認の画面・警告に出す）
	std::string target;        // 対象。再生する Entity から子の名前を / でつないだパス（空なら再生する Entity 自身）
	bool isMuted = false;      // true の間は動かさない

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	void DescribeCommon(ComponentUI& ui);
	// 種類ごとの設定（使わなければ書かなくてよい）
	virtual void Fields(ComponentUI& /*ui*/) {}

	// ===== クリップ（TimelineTrackOf が書いてくれる）=====
	virtual void WriteClips(SceneJson& out, std::set<std::string>& warnings) const = 0;
	virtual void ReadClips(const SceneJson& in, std::set<std::string>& warnings) = 0;
	virtual size_t GetClipCount() const = 0;
	virtual float GetEndTime() const = 0; // 一番遅いクリップの終わり（秒）

	// ===== 再生 =====
	// Entity ごとの値を作る（再生を始めるときに1回）
	virtual std::unique_ptr<TimelineTrackState> CreateState() const { return std::make_unique<TimelineTrackState>(); }
	/// <summary>
	/// 今の時間の分だけ動かす。再生中は毎フレーム、シーク・停止のときは ranges を空にして呼ばれる
	/// </summary>
	virtual void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const = 0;

protected:
	/// <summary>
	/// 対象の Entity を探す（見つけたら state に覚える）
	/// <para>見つからなければ、警告を1回出して無効な Handle を返す。Entity の数が変わったら探し直す</para>
	/// </summary>
	Handle<Entity> FindTarget(const TimelineFrame& frame, TimelineTrackState& state) const;
	// 警告を1回だけ出す（state.isWarned）
	void WarnOnce(TimelineTrackState& state, std::string_view message) const;
};


/// <summary>
/// クリップ（トラックの上に置く、時間の区間）の共通の値。継承して、種類ごとの値を足す
/// <para>Fields に足した値はファイルへ書かれる。ComponentUI で書けない値は WriteExtra・ReadExtra で書く</para>
/// </summary>
struct TimelineClip {
	float start = 0.0f;    // 始まる時間（秒）
	float duration = 0.0f; // 長さ（秒）

	float GetEnd() const { return start + duration; }
	void Fields(ComponentUI& ui);
	void WriteExtra(SceneJson& /*out*/) const {}
	// 読んだ項目は in から消す（残すと「読む所がない」と警告が出る）
	void ReadExtra(SceneJson& /*in*/, std::set<std::string>& /*warnings*/) {}
};


/// <summary>
/// Clip の並びを持つトラック。クリップのファイルの読み書きと、時間からの検索を持っている
/// </summary>
template<class Clip> class TimelineTrackOf : public TimelineTrack {
	static_assert(std::is_base_of_v<TimelineClip, Clip>, "クリップは TimelineClip を継承してください");

public:
	std::vector<Clip> clips; // 始まる時間の順に並ぶ

	void WriteClips(SceneJson& out, std::set<std::string>& warnings) const override {
		out = SceneJson::array();
		for (Clip clip : clips) { // 見せ方の関数は書き換えられる参照を受け取るので、写しを渡す
			SceneJson item;
			ComponentSerializer::WriteFields(typeName, [&clip](ComponentUI& ui) { clip.Fields(ui); }, item, warnings);
			clip.WriteExtra(item);
			out.push_back(std::move(item));
		}
	}

	void ReadClips(const SceneJson& in, std::set<std::string>& warnings) override {
		clips.clear();
		for (const SceneJson& item : in) {
			if (!item.is_object()) {
				warnings.insert("\"clips\" の中に、クリップではない物があります（読み飛ばします）");
				continue;
			}
			SceneJson fields = item; // ReadExtra が読んだ項目を消すので写す
			Clip clip;
			clip.ReadExtra(fields, warnings);
			ComponentSerializer::ReadFields(typeName, [&clip](ComponentUI& ui) { clip.Fields(ui); }, fields, warnings);
			clip.start = (clip.start >= 0.0f) ? clip.start : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
			clip.duration = (clip.duration >= 0.0f) ? clip.duration : 0.0f;
			clips.push_back(std::move(clip));
		}
		std::ranges::stable_sort(clips, {}, &Clip::start);
	}

	size_t GetClipCount() const override { return clips.size(); }

	float GetEndTime() const override {
		float end = 0.0f;
		for (const Clip& clip : clips) {
			end = (std::max)(end, clip.GetEnd());
		}
		return end;
	}

protected:
	// time 以前に始まる、最後のクリップ（無ければ nullptr）
	const Clip* FindClip(float time) const {
		const auto next = std::ranges::upper_bound(clips, time, {}, &Clip::start);
		return (next == clips.begin()) ? nullptr : &*(next - 1);
	}

	// このフレームに通った時間の幅の中で始まるクリップを、順に function へ渡す
	template<class Function> void ForEachStarted(std::span<const TimelineRange> ranges, Function&& function) const {
		for (const TimelineRange& range : ranges) {
			const auto first = range.includesStart ? std::ranges::lower_bound(clips, range.start, {}, &Clip::start) : std::ranges::upper_bound(clips, range.start, {}, &Clip::start);
			const auto last = std::ranges::upper_bound(clips, range.end, {}, &Clip::start);
			for (auto clip = first; clip < last; ++clip) {
				function(*clip);
			}
		}
	}
};


/// <summary>
/// トラックの種類の表（名前 → 作り方）。TIMELINE_TRACK(...) とエンジンの TimelineSystem::Initialize が登録する
/// </summary>
class TimelineTrackRegistry {
public:
	using CreateFunction = std::unique_ptr<TimelineTrack> (*)();

	static void Register(const char* name, CreateFunction create, const char* description);
	// 名前から作る（知らない名前なら nullptr）
	static std::unique_ptr<TimelineTrack> Create(std::string_view name);
	// 名前の一覧（名前の順）
	static std::vector<std::string> GetNames();
	// 説明（知らない名前・書いていなければ空）
	static const std::string& GetDescription(std::string_view name);
};


/// <summary>
/// TIMELINE_TRACK(...) が1つ作る物。作られたときに表へ登録する
/// </summary>
template<class T> class TimelineTrackRegistrar {
	static_assert(std::is_base_of_v<TimelineTrack, T>, "TIMELINE_TRACK(...) に書けるのは、TimelineTrack を継承したクラスだけです");
	static_assert(std::is_default_constructible_v<T>, "トラックは引数の無いコンストラクタで作れるようにしてください（エンジンが名前から作るため）");

public:
	TimelineTrackRegistrar(const char* name, const char* description) { TimelineTrackRegistry::Register(name, &Create, description); }

private:
	static std::unique_ptr<TimelineTrack> Create() { return std::make_unique<T>(); }
};


/// <summary>
/// トラックの種類を登録する。.cpp に TIMELINE_TRACK(クラス名); か TIMELINE_TRACK(クラス名, "説明"); と1行書く
/// <para>クラス名がそのまま、ファイルの "type" になる（変えると、前に保存したファイルのトラックは読めなくなる）</para>
/// <para>必ずゲーム側の .cpp に書く（エンジンの .lib の中に書くと、誰も参照しないのでリンカに捨てられる）</para>
/// </summary>
// "" __VA_ARGS__ は、説明を書かなければ ""、書けば "" "説明" ＝ "説明"（並んだ文字列はつながる。ATTACK_PART と同じ）
#define TIMELINE_TRACK(Type, ...) inline const TimelineTrackRegistrar<Type> timelineTrackRegistrarOf_##Type(#Type, "" __VA_ARGS__)
```

### B-4 `Timeline/TimelineTrack.cpp`（新しいファイル）

```cpp
#include "TimelineTrack.h"

#include <format>
#include <algorithm>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"

namespace {
/// <summary>
/// トラックの種類1つ
/// </summary>
struct TrackType {
	std::string name;
	TimelineTrackRegistry::CreateFunction create = nullptr;
	std::string description;
};

// 関数の中の static なので、ゲームの TIMELINE_TRACK（main より前に作られる）との初期化の順番に左右されない
std::vector<TrackType>& Types() {
	static std::vector<TrackType> types;
	return types;
}

const std::string kEmptyDescription; // 知らない名前の説明
} // namespace


//=============================================================================
// トラック
//=============================================================================
// ===== 見せ方 =====
void TimelineTrack::DescribeCommon(ComponentUI& ui) {
	ui.Field("Name###name", name, Tip("トラックの名前（警告に出す）"));
	ui.Field("Target###target", target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く（Panel/Title）。空なら再生する Entity 自身"));
	ui.Field("Muted###isMuted", isMuted, Tip("このトラックを動かさない"));
}

// ===== 対象を探す =====
Handle<Entity> TimelineTrack::FindTarget(const TimelineFrame& frame, TimelineTrackState& state) const {
	// 覚えている相手が生きていれば、それを使う（探すのは Entity を全部調べるので、毎フレームはしない）
	if (EntityManager::IsAlive(state.target)) {
		return state.target;
	}
	// 見つからなかった後は、Entity の数が変わるまで探し直さない。覚えていた相手が消えたときは、すぐ探す
	const size_t entityCount = EntityManager::GetCount();
	if (state.isSearched && !state.target.IsValid() && state.searchedEntities == entityCount) {
		return {};
	}
	state.isSearched = true;
	state.searchedEntities = entityCount;
	state.target = EntityManager::FindByPath(frame.owner, target);
	if (!state.target.IsValid()) {
		WarnOnce(state, std::format("対象が見つかりません: \"{}\"", target));
	}
	return state.target;
}

// ===== 警告を1回だけ出す =====
void TimelineTrack::WarnOnce(TimelineTrackState& state, std::string_view message) const {
	if (!state.isWarned) {
		state.isWarned = true;
		LogManager::Warning(std::format("Timeline のトラック \"{}\"：{}", name, message));
	}
}


//=============================================================================
// クリップ
//=============================================================================
void TimelineClip::Fields(ComponentUI& ui) {
	ui.Field("Start###start", start, AtLeast(0.0f).Tip("始まる時間（秒）"));
	ui.Field("Duration###duration", duration, AtLeast(0.0f).Tip("長さ（秒）"));
}


//=============================================================================
// 種類の表
//=============================================================================
void TimelineTrackRegistry::Register(const char* name, CreateFunction create, const char* description) {
	std::vector<TrackType>& types = Types();
	if (std::ranges::find(types, std::string_view(name), &TrackType::name) != types.end()) {
		return; // 同じ名前は1回だけ（エンジンを作り直しても、登録が重ならないように）
	}
	types.push_back({name, create, description});
}

std::unique_ptr<TimelineTrack> TimelineTrackRegistry::Create(std::string_view name) {
	const std::vector<TrackType>& types = Types();
	const auto found = std::ranges::find(types, name, &TrackType::name);
	if (found == types.end()) {
		return nullptr;
	}
	std::unique_ptr<TimelineTrack> track = found->create();
	track->typeName = found->name;
	return track;
}

std::vector<std::string> TimelineTrackRegistry::GetNames() {
	std::vector<std::string> names;
	for (const TrackType& type : Types()) {
		names.push_back(type.name);
	}
	std::ranges::sort(names);
	return names;
}

const std::string& TimelineTrackRegistry::GetDescription(std::string_view name) {
	const std::vector<TrackType>& types = Types();
	const auto found = std::ranges::find(types, name, &TrackType::name);
	return (found != types.end()) ? found->description : kEmptyDescription;
}
```

### B-5 `Timeline/TimelineValueTrack.h`（新しいファイル）

```cpp
#pragma once
#include <array>
#include <set>
#include <string>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// 値のクリップ。成分ごとのカーブを持つ（カーブの時間はクリップの始まりからの秒）
/// <para>キーの無い成分は書かない（color の a だけを動かすと、r・g・b はそのまま）</para>
/// </summary>
struct TimelineValueClip : TimelineClip {
	std::array<Curve, ComponentValue::kChannelCount> curves; // [x, y, z, w]。色は r, g, b, a の順

	void WriteExtra(SceneJson& out) const;
	void ReadExtra(SceneJson& in, std::set<std::string>& warnings);
};


/// <summary>
/// Component の1つの項目を、カーブで動かすトラック
/// <para>項目は保存の名前で指す（Describe 関数の "表示名###名前" の名前）。Transform の translation・rotation・scale、Image の color など</para>
/// <para>書けるのは float・int・bool・Vector2〜4・色</para>
/// <para>時間の全部を決める：最初のクリップの前はそのクリップの始まりの値、クリップの間は前のクリップの終わりの値（ループ・Stop で前の値が残らないように）</para>
/// </summary>
class TimelineValueTrack : public TimelineTrackOf<TimelineValueClip> {
public:
	std::string component = "Transform";  // Component の名前（Inspector の見出しと同じ。Script の名前でもよい）
	std::string property = "translation"; // 項目の名前

	void Fields(ComponentUI& ui) override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
};
```

### B-6 `Timeline/TimelineValueTrack.cpp`（新しいファイル）

```cpp
#include "TimelineValueTrack.h"

#include <format>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/ECS/ComponentUI.h"

namespace {
constexpr std::array<const char*, ComponentValue::kChannelCount> kChannelNames = {"x", "y", "z", "w"}; // ファイルの成分の名前
constexpr const char* kCurvesKey = "curves";                                                           // ファイルのカーブの項目の名前
} // namespace


//=============================================================================
// クリップのファイル
//=============================================================================
// ===== 書く（キーのある成分だけ）=====
void TimelineValueClip::WriteExtra(SceneJson& out) const {
	SceneJson channels = SceneJson::object();
	for (size_t i = 0; i < curves.size(); ++i) {
		if (!curves[i].GetKeys().empty()) {
			channels[kChannelNames[i]] = CurveManager::ToJson(curves[i]);
		}
	}
	out[kCurvesKey] = std::move(channels);
}

// ===== 読む =====
void TimelineValueClip::ReadExtra(SceneJson& in, std::set<std::string>& warnings) {
	const auto channels = in.find(kCurvesKey);
	if (channels == in.end()) {
		return;
	}
	for (size_t i = 0; i < curves.size(); ++i) {
		if (const auto channel = channels->find(kChannelNames[i]); channel != channels->end()) {
			curves[i] = CurveManager::FromJson(*channel, warnings);
		}
	}
	in.erase(channels);
}


//=============================================================================
// トラック
//=============================================================================
// ===== 見せ方 =====
void TimelineValueTrack::Fields(ComponentUI& ui) {
	ui.Field("Component###component", component, Tip("動かす Component の名前（Inspector の見出しと同じ。Image・Vector Shape など）"));
	ui.Field("Property###property", property, Tip("動かす項目の名前（Transform は translation・rotation・scale。ほかはシーンファイルに書かれている名前）"));
}

// ===== 動かす =====
void TimelineValueTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (clips.empty()) {
		return;
	}
	// --- 最初のクリップの前は、そのクリップの始まりの値 ---
	const TimelineValueClip* found = FindClip(frame.time);
	const TimelineValueClip* clip = (found != nullptr) ? found : &clips.front();
	// --- 成分ごとにカーブを読む（クリップの終わりを過ぎたら、終わりの値のまま）---
	const float clipTime = std::clamp(frame.time - clip->start, 0.0f, clip->duration);
	ComponentValue value;
	for (size_t i = 0; i < clip->curves.size(); ++i) {
		if (!clip->curves[i].GetKeys().empty()) {
			value.channels[i] = clip->curves[i].Evaluate(clipTime);
			value.isUsed[i] = true;
		}
	}
	const Handle<Entity> entity = FindTarget(frame, state);
	if (!entity.IsValid()) {
		return;
	}
	// --- 書く ---
	switch (ComponentSerializer::SetValue(entity, component, property, value)) {
	case ComponentValueResult::Written:
		break;
	case ComponentValueResult::NoComponent:
		WarnOnce(state, std::format("\"{}\" に Component \"{}\" がありません", target, component));
		break;
	case ComponentValueResult::NoField:
		WarnOnce(state, std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", component, property));
		break;
	}
}
```

### B-7 `Timeline/TimelineEventTrack.h`（新しいファイル）

```cpp
#pragma once
#include <string>

#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// イベントのクリップ（長さは無い。start の時間ちょうどの1点）
/// </summary>
struct TimelineEventClip : TimelineClip {
	std::string name;  // イベントの名前
	std::string value; // イベントに付ける文字

	void Fields(ComponentUI& ui);
};


/// <summary>
/// 指定の時間に、対象の Script の OnTimelineEvent と TimelineSystem::AddEventListener の関数を呼ぶトラック
/// <para>呼ばれるのは、時間がそのイベントを通ったとき（通った時間の幅の決まりは TimelinePlayhead）。シークでは呼ばない</para>
/// </summary>
class TimelineEventTrack : public TimelineTrackOf<TimelineEventClip> {
public:
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
};
```

### B-8 `Timeline/TimelineEventTrack.cpp`（新しいファイル）

```cpp
#include "TimelineEventTrack.h"

#include "MyEngine/ECS/ComponentUI.h"

//=============================================================================
// クリップ
//=============================================================================
void TimelineEventClip::Fields(ComponentUI& ui) {
	ui.Field("Time###start", start, AtLeast(0.0f).Tip("呼ぶ時間（秒）"));
	ui.Field("Name###name", name, Tip("イベントの名前"));
	ui.Field("Value###value", value, Tip("イベントに付ける文字（使い方は受け取る側が決める）"));
}

//=============================================================================
// トラック
//=============================================================================
void TimelineEventTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (frame.ranges.empty()) {
		return; // シーク・停止では呼ばない（対象も探さない）
	}
	ForEachStarted(frame.ranges, [&](const TimelineEventClip& clip) {
		const Handle<Entity> entity = FindTarget(frame, state);
		if (entity.IsValid()) {
			frame.events->push_back({frame.owner, entity, clip.name, clip.value, clip.start});
		}
	});
}
```

### B-9 `Timeline/TimelineManager.h`（新しいファイル）

```cpp
#pragma once
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <string_view>
#include <filesystem>

#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/Timeline/TimelineTrack.h"


/// <summary>
/// Timeline（ファイル1つの中身）。トラックの数が変わるので Component には入れられず、ここに持つ
/// </summary>
struct Timeline {
	float length = 1.0f;                                // 長さ（秒）
	std::vector<std::unique_ptr<TimelineTrack>> tracks; // トラック（上から順に動かす）
	std::vector<std::string> unknownTracks;             // 知らない種類のトラック（JSON の文字のまま持ち、保存で消さない）

	// 再生する長さ（length と、一番遅いクリップの終わりの大きい方。クリップが切れないように）
	float GetDuration() const;
};


/// <summary>
/// Timeline のファイル（.timeline）を読む・持つ・保存する（CurveManager と同じ形）
/// <para>同じファイルを再生する Entity はみんな同じ Timeline を見る。Entity ごとの値は TimelineSystem が持つ</para>
/// </summary>
class TimelineManager {
public:
	static constexpr std::string_view kFileExtension = ".timeline"; // Timeline のファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ポインタは使い捨て
	static const Timeline* Get(uint32_t handle);

	// 直すための中身（エディタから。直したら MarkChanged を呼ぶ）
	static Timeline* GetForEdit(uint32_t handle);
	// 中身を直したと知らせる。版が1つ増えて、再生中の Entity がトラックの値を作り直す
	static void MarkChanged(uint32_t handle);

	// 中身の版（TimelineSystem が、作り直すかを決めるのに使う）
	static uint32_t GetRevision(uint32_t handle);

	// ファイルに書く（前のファイルは .bak として1つ残る）
	static bool Save(uint32_t handle);

	// 新しいファイルを、既定の中身で作る（Project ウィンドウの Create → Timeline）。同じ名前のファイルがあれば作らない
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ===== JSON（ファイルの形は { "settings": {...}, "tracks": [{ "type", "settings", "clips" }] }）=====
	static SceneJson ToJson(const Timeline& timeline);
	// 読めない項目は warnings に書いて、既定の値のままにする
	static Timeline FromJson(const SceneJson& json, std::set<std::string>& warnings);

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	static void DescribeLength(ComponentUI& ui, float& length);
};
```

### B-10 `Timeline/TimelineManager.cpp`（新しいファイル）

```cpp
#include "TimelineManager.h"

#include <format>
#include <fstream>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Timeline"; // 警告に出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct TimelineAsset {
	std::string path;      // ファイル（ToPathKey の形）
	Timeline timeline;     // 中身
	uint32_t revision = 0; // 中身の版（MarkChanged のたびに増える）
};

/// <summary>
/// TimelineManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct TimelineStorage {
	std::vector<TimelineAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<TimelineStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
TimelineAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// ファイルに書く（前のファイルは .bak として1つ残る）
bool WriteFile(const std::string& path, const Timeline& timeline) {
	if (!SceneSerializer::WriteFile(path, TimelineManager::ToJson(timeline))) {
		LogManager::Error(std::format("Timeline を保存できませんでした: {}", path));
		return false;
	}
	return true;
}

// ===== トラック1つを JSON にする =====
SceneJson TrackToJson(TimelineTrack& track, std::set<std::string>& warnings) {
	const std::string& typeName = track.typeName;
	SceneJson settings;
	ComponentSerializer::WriteFields(
	    typeName,
	    [&track](ComponentUI& ui) {
		    track.DescribeCommon(ui);
		    track.Fields(ui);
	    },
	    settings, warnings);
	SceneJson clips;
	track.WriteClips(clips, warnings);

	SceneJson json = SceneJson::object();
	json["type"] = typeName;
	json["settings"] = std::move(settings);
	json["clips"] = std::move(clips);
	return json;
}

// ===== トラック1つを読む（知らない種類なら、JSON の文字のまま覚える）=====
void ReadTrack(const SceneJson& json, Timeline& timeline, std::set<std::string>& warnings) {
	const auto type = json.find("type");
	if (type == json.end() || !type->is_string()) {
		warnings.insert("\"type\" の無いトラックを飛ばしました");
		return;
	}
	const std::string& typeName = type->get_ref<const std::string&>();
	std::unique_ptr<TimelineTrack> track = TimelineTrackRegistry::Create(typeName);
	if (track == nullptr) {
		warnings.insert(std::format("知らない種類のトラック \"{}\" は動かしません（TIMELINE_TRACK(...) を確かめてください。ファイルからは消しません）", typeName));
		timeline.unknownTracks.push_back(json.dump());
		return;
	}
	if (const auto settings = json.find("settings"); settings != json.end()) {
		TimelineTrack* pointer = track.get();
		ComponentSerializer::ReadFields(
		    typeName,
		    [pointer](ComponentUI& ui) {
			    pointer->DescribeCommon(ui);
			    pointer->Fields(ui);
		    },
		    *settings, warnings);
	}
	if (const auto clips = json.find("clips"); clips != json.end() && clips->is_array()) {
		track->ReadClips(*clips, warnings);
	}
	timeline.tracks.push_back(std::move(track));
}
} // namespace


//=============================================================================
// Timeline
//=============================================================================
float Timeline::GetDuration() const {
	float duration = length;
	for (const std::unique_ptr<TimelineTrack>& track : tracks) {
		duration = (std::max)(duration, track->GetEndTime());
	}
	return duration;
}


//=============================================================================
// 初期化 / 解放
//=============================================================================
void TimelineManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<TimelineStorage>();
}

void TimelineManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t TimelineManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "TimelineManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &TimelineAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Timeline のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded() || !json.is_object()) {
		LogManager::Error(std::format("Timeline のファイルが壊れています: {}", key));
		return 0;
	}
	std::set<std::string> warnings;
	Timeline timeline = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(timeline)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded timeline: {} -> {}", key, handle));
	return handle;
}

const std::string& TimelineManager::GetPath(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const Timeline* TimelineManager::Get(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->timeline : nullptr;
}

//=============================================================================
// 書き換え・保存
//=============================================================================
Timeline* TimelineManager::GetForEdit(uint32_t handle) {
	TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->timeline : nullptr;
}

void TimelineManager::MarkChanged(uint32_t handle) {
	if (TimelineAsset* asset = Find(handle)) {
		++asset->revision;
	}
}

uint32_t TimelineManager::GetRevision(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->revision : 0;
}

bool TimelineManager::Save(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->timeline);
}

bool TimelineManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), Timeline{});
}

//=============================================================================
// JSON
//=============================================================================
SceneJson TimelineManager::ToJson(const Timeline& timeline) {
	std::set<std::string> warnings;
	float length = timeline.length;
	SceneJson settings;
	ComponentSerializer::WriteFields(kTypeName, [&length](ComponentUI& ui) { DescribeLength(ui, length); }, settings, warnings);

	SceneJson tracks = SceneJson::array();
	for (const std::unique_ptr<TimelineTrack>& track : timeline.tracks) {
		tracks.push_back(TrackToJson(*track, warnings));
	}
	for (const std::string& text : timeline.unknownTracks) {
		tracks.push_back(SceneJson::parse(text, nullptr, false));
	}
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
	SceneJson root = SceneJson::object();
	root["settings"] = std::move(settings);
	root["tracks"] = std::move(tracks);
	return root;
}

Timeline TimelineManager::FromJson(const SceneJson& json, std::set<std::string>& warnings) {
	Timeline timeline;
	if (const auto settings = json.find("settings"); settings != json.end()) {
		ComponentSerializer::ReadFields(kTypeName, [&timeline](ComponentUI& ui) { DescribeLength(ui, timeline.length); }, *settings, warnings);
		timeline.length = (timeline.length >= 0.0f) ? timeline.length : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
	}
	if (const auto tracks = json.find("tracks"); tracks != json.end() && tracks->is_array()) {
		for (const SceneJson& item : *tracks) {
			if (item.is_object()) {
				ReadTrack(item, timeline, warnings);
			} else {
				warnings.insert("\"tracks\" の中に、トラックではない物があります（読み飛ばします）");
			}
		}
	}
	return timeline;
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void TimelineManager::DescribeLength(ComponentUI& ui, float& length) {
	ui.Field("Length###length", length, AtLeast(0.0f).Tip("長さ（秒）。クリップがこれより長ければ、クリップの終わりまで再生する"));
}
```

### B-11 `ECS/ComponentUI.h`

`AssetType` の最後に足す。

```cpp
	Curve,          // CurveManager::Load の番号（.curve）
	Timeline,       // TimelineManager::Load の番号（.timeline）
};
```

### B-12 `ECS/ComponentUI.cpp`

include に足す。

```cpp
#include "MyEngine/String/FileNameRule.h"
#include "MyEngine/Timeline/TimelineManager.h"
```

`GetAssetPath` の `switch` に足す。

```cpp
	case AssetType::Curve:
		return CurveManager::GetPath(handle);
	case AssetType::Timeline:
		return TimelineManager::GetPath(handle);
	}
```

`LoadAsset` の `switch` に足す。

```cpp
	case AssetType::Curve:
		return CurveManager::Load(filePath);
	case AssetType::Timeline:
		return TimelineManager::Load(filePath);
	}
```

拡張子の一覧に足す。

```cpp
constexpr std::array<std::string_view, 1> kCurveExtensions = {CurveManager::kFileExtension};                   // ".curve"
constexpr std::array<std::string_view, 1> kTimelineExtensions = {TimelineManager::kFileExtension};             // ".timeline"
```

`ExtensionsOf` の `switch` に足す。

```cpp
	case AssetType::Curve:
		return kCurveExtensions;
	case AssetType::Timeline:
		return kTimelineExtensions;
	}
```

## パートC 再生と自動の確認

### C-1 `Components/Animation/TimelineComponent.h`（新しいファイル）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "MyEngine/Time/EngineTime.h"

// Timeline を再生する。Entityに付けるComponent（カテゴリ：Animation）
// 中身はファイル（.timeline）に書く。ここは「どのファイルをどう再生するか」だけを持つ
// 今の時間・再生の状態は TimelineSystem が持つ（数が変わる物はバイト列で写す Component に入れられず、同じファイルを何体でも再生できるように）


/// <summary>
/// Timeline の再生の設定
/// </summary>
struct TimelineComponent {
	uint32_t timeline = 0;              // 再生するファイル（AssetType::Timeline の番号）
	bool playOnStart = true;            // 再生が始まったら、すぐ再生する（false なら TimelineSystem::Play を呼ぶまで止まっている）
	bool loop = false;                  // 最後まで行ったら最初へ戻る
	float speed = 1.0f;                 // 再生速度（1 で普通。0 の間は進まない。負の値は 0 と同じ）
	TimeMode timeMode = TimeMode::Game; // 使う時間
};

static_assert(std::is_trivially_copyable_v<TimelineComponent>);
```

### C-2 `Systems/Animation/TimelineSystem.h`（新しいファイル）

```cpp
#pragma once
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Timeline/TimelineEvent.h"
#include "MyEngine/Timeline/TimelinePlayhead.h"

/// <summary>
/// Entity の今の再生の様子（TimelineSystem::GetStatus）。読むたびに作る写しなので、値は古くなる
/// </summary>
struct TimelineStatus {
	bool isValid = false;                                  // Timeline Component があり、ファイルが読めている
	TimelinePlayState state = TimelinePlayState::Stopped;  // 再生の状態
	float time = 0.0f;                                     // 今の時間（秒）
	float duration = 0.0f;                                 // 長さ（秒）
	int loopCount = 0;                                     // 最初へ戻った回数

	bool IsPlaying() const { return state == TimelinePlayState::Playing; }
	// 終わりまでの割合（0〜1）
	float GetProgress() const { return (duration > 0.0f) ? time / duration : 0.0f; }
};


/// <summary>
/// Timeline を再生する係
/// <para>Timeline のファイルは全部の Entity で共有する。今の時間・対象の Entity はここが Entity ごとに持つ（最初に使うときに作り、エディタの Stop で消える）</para>
/// <para>順番：全部の Entity の値を書く → 全部のイベントを呼ぶ（イベントの中で Stop・Destroy をしても、回している途中の物が壊れない）</para>
/// </summary>
class TimelineSystem {
public:
	// Timeline Component と、エンジンのトラックの種類を登録する。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の Timeline を1フレーム進める。Script の後、ワールド行列の計算の前に呼ぶ（動かした位置を、そのフレームの描画に使うため）
	/// </summary>
	/// <param name="isGameRunning">再生中か（エディタの停止中は何も動かさない）</param>
	static void Update(bool isGameRunning);

	// ===== 外から指示する（ゲーム・カメラ）。どれも Timeline Component が無い・ファイルが無い・エディタの停止中は false =====
	// 再生する。止まっていれば最初から、一時停止中なら続きから
	static bool Play(Handle<Entity> entity);
	// 一時停止する
	static bool Pause(Handle<Entity> entity);
	// 止める。時間を 0 に戻し、次の Update で 0 の値を書く
	static bool Stop(Handle<Entity> entity);
	// time 秒へ移動する。次の Update でその時間の値を書く。イベントは呼ばない
	static bool Seek(Handle<Entity> entity, float time);

	// ===== 外から読む =====
	static TimelineStatus GetStatus(Handle<Entity> entity);

	// ===== イベントを受け取る =====
	// Script は OnTimelineEvent を書く。Script 以外で受け取りたいときに、ここへ関数を登録する（登録した関数は Release まで呼ばれる。同じ関数は1回だけ）
	using EventListener = void (*)(const TimelineEvent& event);
	static void AddEventListener(EventListener listener);

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};
```

### C-3 `Systems/Animation/TimelineSystem.cpp`（新しいファイル）

```cpp
#include "TimelineSystem.h"

#include <memory>
#include <vector>
#include <cstdint>
#include <algorithm>

#include "MyEngine/Components/Animation/TimelineComponent.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Systems/Gameplay/ScriptSystem.h"
#include "MyEngine/Timeline/TimelineEventTrack.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"

namespace {
//=============================================================================
// Entity 1つ分の実行中の値
//=============================================================================
struct TimelineRuntime {
	Handle<Entity> entity;                                   // どの Entity か
	uint32_t timeline = 0;                                   // 作ったときのファイル（Component のファイルが替わったら作り直す）
	uint32_t revision = 0;                                   // 読んだファイルの版（変わったら作り直す）
	std::vector<std::unique_ptr<TimelineTrackState>> states; // [トラックの番号] トラックの Entity ごとの値
	TimelinePlayhead playhead;                               // 時間の位置
	bool needsApply = false;                                 // 時間が飛んだ（シーク・停止）。次の Update で値を書く
};

std::vector<TimelineRuntime> runtimes;         // 再生を使った Entity（数が少ないので、一列に並べて順に探す）
std::vector<TimelineRange> passedRanges;       // このフレームに通った時間の幅（毎回確保しないように使い回す）
std::vector<TimelineEvent> pendingEvents;      // 全部の値を書いた後に呼ぶイベント
std::vector<TimelineSystem::EventListener> listeners; // AddEventListener の関数
bool isRunning = false;                        // 再生中（エディタの停止中は false）

// ===== その Entity の実行中の値（まだ作っていなければ nullptr）=====
TimelineRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(runtimes, entity, &TimelineRuntime::entity);
	return (found != runtimes.end()) ? &*found : nullptr;
}

// ===== トラックの Entity ごとの値を作り直す（対象も探し直す）=====
void BuildStates(TimelineRuntime& runtime, uint32_t handle, const Timeline& timeline) {
	runtime.timeline = handle;
	runtime.revision = TimelineManager::GetRevision(handle);
	runtime.states.clear();
	for (const std::unique_ptr<TimelineTrack>& track : timeline.tracks) {
		runtime.states.push_back(track->CreateState());
	}
}

// ===== 実行中の値を用意する（Component・ファイルが無ければ nullptr）=====
TimelineRuntime* PrepareRuntime(Handle<Entity> entity) {
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
	const Timeline* timeline = (component != nullptr) ? TimelineManager::Get(component->timeline) : nullptr;
	if (!isRunning || timeline == nullptr) {
		return nullptr;
	}
	TimelineRuntime* runtime = FindRuntime(entity);
	if (runtime == nullptr) {
		runtimes.push_back({.entity = entity});
		runtime = &runtimes.back();
		BuildStates(*runtime, component->timeline, *timeline);
		if (component->playOnStart) {
			runtime->playhead.Play();
		}
	} else if (runtime->timeline != component->timeline || runtime->revision != TimelineManager::GetRevision(component->timeline)) {
		BuildStates(*runtime, component->timeline, *timeline);
		runtime->needsApply = true;
	}
	return runtime;
}

//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 全部のトラックを動かす =====
void EvaluateTracks(TimelineRuntime& runtime, const Timeline& timeline) {
	TimelineFrame frame;
	frame.owner = runtime.entity;
	frame.time = runtime.playhead.GetTime();
	frame.ranges = passedRanges;
	frame.events = &pendingEvents;
	for (size_t i = 0; i < timeline.tracks.size(); ++i) {
		const TimelineTrack& track = *timeline.tracks[i];
		if (!track.isMuted) {
			track.Evaluate(frame, *runtime.states[i]);
		}
	}
}

// ===== Entity 1つ =====
void UpdateEntity(Handle<Entity> entity, const TimelineComponent& component) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		return; // ファイルが無い・無効な Entity の間は時間も進めない
	}
	const Timeline& timeline = *TimelineManager::Get(runtime->timeline);
	// --- 時間を進める。通った幅があるか、時間が飛んだときだけ書く（止まっている間は、ゲームが書いた値を上書きしない）---
	const float speed = (component.speed > 0.0f) ? component.speed : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
	runtime->playhead.Advance(Time::GetDeltaTime(component.timeMode) * speed, timeline.GetDuration(), component.loop, passedRanges);
	if (!passedRanges.empty() || runtime->needsApply) {
		runtime->needsApply = false;
		EvaluateTracks(*runtime, timeline);
	}
}

// ===== 集めたイベントを呼ぶ =====
// 全部の Entity の値を書いた後に呼ぶ。中で Stop・Seek・Destroy をしても、回している途中の配列が動かない
void SendEvents() {
	for (const TimelineEvent& event : pendingEvents) {
		for (const TimelineSystem::EventListener listener : listeners) {
			listener(event);
		}
		ScriptSystem::SendTimelineEvent(event.target, event);
	}
	pendingEvents.clear();
}

//=============================================================================
// Inspector・保存
//=============================================================================
void DescribeTimelineComponent(ComponentUI& ui, TimelineComponent& value) {
	ui.AssetField("Timeline###timeline", value.timeline, AssetType::Timeline, Tip("再生するファイル（.timeline）。Project ウィンドウの Create → Timeline で作る"));
	ui.Field("Play On Start###playOnStart", value.playOnStart, Tip("再生が始まったら、すぐ再生する（外すと TimelineSystem::Play を呼ぶまで止まっている）"));
	ui.Field("Loop###loop", value.loop, Tip("最後まで行ったら最初へ戻る"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("再生速度（1 で普通）"));
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("Game：スロー・ヒットストップ・ポーズで止まる / Real：ゲームの時間が止まっても動く（エディタの一時停止では止まる）"));
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void TimelineSystem::Initialize() {
	// Timeline の置き場所・保存・Inspector を登録する（見せ方は DescribeTimelineComponent 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<TimelineComponent>();
	ComponentSerializer::Register<TimelineComponent>("Timeline", &DescribeTimelineComponent);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TimelineComponent>>("Timeline", "Animation", &DescribeTimelineComponent));
	EntityManager::AddDestroyListener(&TimelineSystem::OnEntityDestroyed);
	// エンジンのトラック（.lib の中の TIMELINE_TRACK はリンカに捨てられるので、ここで登録する）
	TimelineTrackRegistrar<TimelineValueTrack>("TimelineValueTrack", "Component の1つの項目をカーブで動かす");
	TimelineTrackRegistrar<TimelineEventTrack>("TimelineEventTrack", "指定の時間に Script の OnTimelineEvent を呼ぶ");
}

void TimelineSystem::Release() {
	runtimes.clear();
	pendingEvents.clear();
	listeners.clear();
	isRunning = false;
}

//=============================================================================
// 毎フレーム
//=============================================================================
void TimelineSystem::Update(bool isGameRunning) {
	isRunning = isGameRunning;
	if (!isGameRunning) {
		runtimes.clear(); // 停止中は動かさない（Play を押すと、また最初から）
		return;
	}
	// --- Component を外された Entity を片付ける ---
	std::erase_if(runtimes, [](const TimelineRuntime& runtime) { return EntityManager::Get<TimelineComponent>(runtime.entity) == nullptr; });
	EntityManager::ForEach<TimelineComponent>([](Handle<Entity> entity, TimelineComponent& component) { UpdateEntity(entity, component); });
	SendEvents();
}

//=============================================================================
// 外から指示する
//=============================================================================
bool TimelineSystem::Play(Handle<Entity> entity) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Play();
	}
	return runtime != nullptr;
}

bool TimelineSystem::Pause(Handle<Entity> entity) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Pause();
	}
	return runtime != nullptr;
}

bool TimelineSystem::Stop(Handle<Entity> entity) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Stop();
		runtime->needsApply = true;
	}
	return runtime != nullptr;
}

bool TimelineSystem::Seek(Handle<Entity> entity, float time) {
	TimelineRuntime* runtime = PrepareRuntime(entity);
	if (runtime != nullptr) {
		runtime->playhead.Seek(time, TimelineManager::Get(runtime->timeline)->GetDuration());
		runtime->needsApply = true;
	}
	return runtime != nullptr;
}

//=============================================================================
// 外から読む
//=============================================================================
TimelineStatus TimelineSystem::GetStatus(Handle<Entity> entity) {
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
	const Timeline* timeline = (component != nullptr) ? TimelineManager::Get(component->timeline) : nullptr;
	if (timeline == nullptr) {
		return {};
	}
	TimelineStatus status;
	status.isValid = true;
	status.duration = timeline->GetDuration();
	if (const TimelineRuntime* runtime = FindRuntime(entity)) {
		status.state = runtime->playhead.GetState();
		status.time = runtime->playhead.GetTime();
		status.loopCount = runtime->playhead.GetLoopCount();
	}
	return status;
}

void TimelineSystem::AddEventListener(EventListener listener) {
	// 同じ関数は1回だけ（Script の Start から呼ぶと、Play のたびに呼ばれるので）
	if (std::ranges::find(listeners, listener) == listeners.end()) {
		listeners.push_back(listener);
	}
}

//=============================================================================
// Entity の破棄
//=============================================================================
void TimelineSystem::OnEntityDestroyed(Handle<Entity> entity) {
	std::erase_if(runtimes, [entity](const TimelineRuntime& runtime) { return runtime.entity == entity; });
}
```

### C-4 `Systems/EngineSystems.cpp`

include に足す。

```cpp
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Timeline/TimelineManager.h"
```

```cpp
#include "MyEngine/Systems/Animation/AnimationSystem.h"
#include "MyEngine/Systems/Animation/TimelineSystem.h"
```

`Initialize` に足す。

```cpp
	CurveManager::Initialize();           // カーブ（.curve）
	TimelineManager::Initialize();        // Timeline（.timeline）。TimelineSystem が読むので先に
	TimelineSystem::Initialize();         // Timeline
```

`Release` に足す。

```cpp
	CurveManager::Release();
	TimelineSystem::Release();
	TimelineManager::Release();
```

`Update` に足す。

```cpp
	AnimationSystem::Update(isGameRunning, Time::GetDeltaTime());   // アニメーション。停止中もInspectorのPreview Timeのポーズを見せる。
	TimelineSystem::Update(isGameRunning);  // Timeline（Script とアニメーションの後。書いた値を、このフレームのワールド行列・描画に使う。時間は Component ごとに選ぶ）
```

### C-5 `Timeline/TimelineSelfTest.h`（新しいファイル）

```cpp
#pragma once

/// <summary>
/// Timeline の時間の進め方・イベントの呼ばれ方・ファイル・値の書き込みが合っているかを、起動時に1回だけ確かめる（CurveSelfTest と同じ形）
/// <para>合わなければログに出して assert で止める。Entity を一時的に作るので、EngineSystems::Initialize の後に呼ぶ</para>
/// </summary>
namespace TimelineSelfTest {
/// <summary>
/// 確かめる。Engine::Initialize の中で呼ぶ（Debug だけ）
/// </summary>
void Run();
} // namespace TimelineSelfTest
```

### C-6 `Timeline/TimelineSelfTest.cpp`（新しいファイル）

`TestValueTrack` の値の確認は、同じトラックを2つの Entity で別の時間に動かして、共有していても混ざらないことを見ている。

```cpp
#include "TimelineSelfTest.h"

#include <set>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/ImageComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Timeline/TimelineEventTrack.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"

namespace {
constexpr float kExact = 0.001f;  // 式で答えが決まる値の許容
constexpr int kMaxErrorLogs = 10; // 合わなかった項目をログに出す数（全部出すと読めない）
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

/// <summary>
/// 確かめた数と、合わなかった数を数える
/// </summary>
struct Tester {
	int passed = 0;
	int failed = 0;

	void Check(bool isOk, const std::string& name) {
		if (isOk) {
			++passed;
			return;
		}
		if (failed < kMaxErrorLogs) {
			LogManager::Error(std::format("[timeline] self test: {}", name));
		}
		++failed;
	}
	void Near(float actual, float expected, const std::string& name) { Check(std::abs(actual - expected) <= kExact, std::format("{}: {} （正しい値 {}）", name, actual, expected)); }
};

// イベントのトラックを作る（times の時間に、"e0", "e1", ... という名前で置く）
std::unique_ptr<TimelineEventTrack> MakeEventTrack(const std::vector<float>& times) {
	auto track = std::make_unique<TimelineEventTrack>();
	for (size_t i = 0; i < times.size(); ++i) {
		TimelineEventClip clip;
		clip.start = times[i];
		clip.name = std::format("e{}", i);
		track->clips.push_back(clip);
	}
	return track;
}

// 時間を deltas の分だけ進めて、呼ばれたイベントの名前を順につないで返す（シークを挟めるように、playhead は外で持つ）
std::string RunEvents(TimelinePlayhead& playhead, const TimelineEventTrack& track, Handle<Entity> owner, const std::vector<float>& deltas, float duration, bool isLoop) {
	std::vector<TimelineRange> ranges;
	std::vector<TimelineEvent> events;
	TimelineTrackState state;
	for (const float delta : deltas) {
		playhead.Advance(delta, duration, isLoop, ranges);
		track.Evaluate({owner, playhead.GetTime(), ranges, &events}, state);
	}
	std::string result;
	for (const TimelineEvent& event : events) {
		result += event.name + " ";
	}
	return result;
}

// 値を書く
ComponentValueResult SetChannel(Handle<Entity> entity, std::string_view component, std::string_view key, size_t channel, float value) {
	ComponentValue componentValue;
	componentValue.channels[channel] = value;
	componentValue.isUsed[channel] = true;
	return ComponentSerializer::SetValue(entity, component, key, componentValue);
}

//=============================================================================
// 確かめる項目
//=============================================================================
// ===== 時間の進み方 =====
void TestPlayhead(Tester& t) {
	std::vector<TimelineRange> ranges;
	TimelinePlayhead playhead;
	t.Check(playhead.GetState() == TimelinePlayState::Stopped, "最初は止まっている");
	playhead.Advance(0.5f, 1.0f, false, ranges);
	t.Check(ranges.empty() && playhead.GetTime() == 0.0f, "止まっている間は進まない");

	playhead.Play();
	playhead.Advance(0.25f, 1.0f, false, ranges);
	t.Check(ranges.size() == 1 && ranges[0].includesStart && playhead.GetTime() == 0.25f, "最初の幅は 0 ちょうどを含む");
	playhead.Advance(0.0f, 1.0f, false, ranges);
	t.Check(ranges.empty(), "0 秒なら進まない");
	playhead.Advance(kNaN, 1.0f, false, ranges);
	t.Check(ranges.empty() && playhead.GetTime() == 0.25f, "NaN なら進まない");
	playhead.Advance(0.25f, 1.0f, false, ranges);
	t.Check(ranges.size() == 1 && !ranges[0].includesStart, "2回目の幅は始まりを含まない");

	playhead.Pause();
	t.Check(playhead.GetState() == TimelinePlayState::Paused, "一時停止");
	playhead.Advance(0.25f, 1.0f, false, ranges);
	t.Check(ranges.empty() && playhead.GetTime() == 0.5f, "一時停止中は進まない");
	playhead.Play();
	t.Check(playhead.GetState() == TimelinePlayState::Playing && playhead.GetTime() == 0.5f, "一時停止の続きから");

	playhead.Advance(2.0f, 1.0f, false, ranges);
	t.Check(playhead.GetState() == TimelinePlayState::Finished && playhead.GetTime() == 1.0f, "最後まで行くと終わる");
	playhead.Play();
	t.Check(playhead.GetState() == TimelinePlayState::Playing && playhead.GetTime() == 0.0f, "終わった後の Play は最初から");

	playhead.Advance(0.5f, 1.0f, false, ranges);
	playhead.Stop();
	t.Check(playhead.GetState() == TimelinePlayState::Stopped && playhead.GetTime() == 0.0f, "Stop で 0 に戻る");

	// --- シーク ---
	playhead.Seek(0.5f, 1.0f);
	t.Check(playhead.GetState() == TimelinePlayState::Paused && playhead.GetTime() == 0.5f, "止まっている間のシークは一時停止になる");
	playhead.Seek(5.0f, 1.0f);
	t.Check(playhead.GetTime() == 1.0f, "シークは長さの中に収まる");
	playhead.Seek(-1.0f, 1.0f);
	t.Check(playhead.GetTime() == 0.0f, "シークは 0 より前へ行かない");
	playhead.Seek(kNaN, 1.0f);
	t.Check(playhead.GetTime() == 0.0f, "NaN のシークは 0");
	playhead.Play();
	playhead.Seek(0.5f, 1.0f);
	t.Check(playhead.GetState() == TimelinePlayState::Playing, "再生中のシークは再生を続ける");

	// --- ループ ---
	TimelinePlayhead loop;
	loop.Play();
	loop.Advance(1.25f, 1.0f, true, ranges);
	t.Near(loop.GetTime(), 0.25f, "ループの余り");
	t.Check(loop.GetLoopCount() == 1 && loop.GetState() == TimelinePlayState::Playing, "ループしても再生を続ける");
	loop.Advance(1000000.0f, 1.0f, true, ranges);
	t.Check(ranges.size() <= TimelinePlayhead::kMaxLoopsPerFrame + 2, "とても大きな秒でも周の数は上限まで");
	t.Check(std::isfinite(loop.GetTime()) && loop.GetTime() < 1.0f, "とても大きな秒でも長さの中に収まる");

	// --- 長さ 0 ---
	TimelinePlayhead zero;
	zero.Play();
	zero.Advance(0.1f, 0.0f, true, ranges);
	t.Check(ranges.size() == 1 && zero.GetState() == TimelinePlayState::Finished, "長さ 0 は1回で終わる（Loop でも止まる）");
}

// ===== イベント =====
void TestEvents(Tester& t, Handle<Entity> owner) {
	const std::unique_ptr<TimelineEventTrack> track = MakeEventTrack({0.0f, 0.5f, 1.0f});

	TimelinePlayhead play;
	play.Play();
	t.Check(RunEvents(play, *track, owner, {0.25f, 0.25f, 0.25f, 0.25f}, 1.0f, false) == "e0 e1 e2 ", "全部のイベントが1回ずつ（端と、ちょうど止まる時間も）");

	TimelinePlayhead big;
	big.Play();
	t.Check(RunEvents(big, *track, owner, {5.0f}, 1.0f, false) == "e0 e1 e2 ", "1フレームで全部通っても1回ずつ");

	TimelinePlayhead loop;
	loop.Play();
	t.Check(RunEvents(loop, *track, owner, {0.75f, 0.75f, 0.75f}, 1.0f, true) == "e0 e1 e2 e0 e1 e2 e0 ", "ループは周ごとに1回ずつ");

	TimelinePlayhead wraps;
	wraps.Play();
	t.Check(RunEvents(wraps, *track, owner, {2.25f}, 1.0f, true) == "e0 e1 e2 e0 e1 e2 e0 ", "1フレームに2周しても周ごとに1回ずつ");

	TimelinePlayhead seek;
	seek.Play();
	t.Check(RunEvents(seek, *track, owner, {0.25f}, 1.0f, false) == "e0 ", "シークの前");
	seek.Seek(0.75f, 1.0f);
	t.Check(RunEvents(seek, *track, owner, {0.5f}, 1.0f, false) == "e2 ", "シークで飛ばしたイベントは呼ばない");

	TimelinePlayhead exact;
	exact.Play();
	exact.Seek(0.5f, 1.0f);
	t.Check(RunEvents(exact, *track, owner, {0.25f}, 1.0f, false).empty(), "シークした先ちょうどのイベントは呼ばない");

	TimelinePlayhead back;
	back.Play();
	RunEvents(back, *track, owner, {0.75f}, 1.0f, false);
	back.Seek(0.25f, 1.0f);
	t.Check(RunEvents(back, *track, owner, {0.5f}, 1.0f, false) == "e1 ", "前へ戻ると、もう一度通ったイベントは呼ぶ");

	TimelinePlayhead again;
	again.Play();
	RunEvents(again, *track, owner, {0.25f}, 1.0f, false);
	again.Stop();
	again.Play();
	t.Check(RunEvents(again, *track, owner, {0.25f}, 1.0f, false) == "e0 ", "Stop の後の Play は 0 のイベントをもう一度呼ぶ");

	TimelinePlayhead paused;
	paused.Play();
	RunEvents(paused, *track, owner, {0.25f}, 1.0f, false);
	paused.Pause();
	t.Check(RunEvents(paused, *track, owner, {0.5f}, 1.0f, false).empty(), "一時停止中は呼ばない");
	paused.Play();
	t.Check(RunEvents(paused, *track, owner, {0.5f}, 1.0f, false) == "e1 ", "一時停止の続きは1回だけ呼ぶ");

	const std::unique_ptr<TimelineEventTrack> same = MakeEventTrack({0.5f, 0.5f});
	TimelinePlayhead sameTime;
	sameTime.Play();
	t.Check(RunEvents(sameTime, *same, owner, {1.0f}, 1.0f, false) == "e0 e1 ", "同じ時間のイベントはファイルの順");

	const std::unique_ptr<TimelineEventTrack> zero = MakeEventTrack({0.0f});
	TimelinePlayhead zeroLength;
	zeroLength.Play();
	t.Check(RunEvents(zeroLength, *zero, owner, {0.1f, 0.1f}, 0.0f, true) == "e0 ", "長さ 0 の Timeline のイベントは1回");
}

// ===== パス =====
void TestPath(Tester& t, Handle<Entity> root, Handle<Entity> panel, Handle<Entity> title) {
	t.Check(EntityManager::FindByPath(root, "") == root, "空のパスは自分");
	t.Check(EntityManager::FindByPath(root, "Panel") == panel, "子");
	t.Check(EntityManager::FindByPath(root, "Panel/Title") == title, "子の子");
	t.Check(!EntityManager::FindByPath(root, "Panel/Nope").IsValid(), "無い子");
	t.Check(!EntityManager::FindByPath(root, "Title").IsValid(), "飛ばした段は見つからない");
	t.Check(!EntityManager::FindByPath(Handle<Entity>{}, "Panel").IsValid(), "無効な Entity から探す");
}

// ===== 値の書き込み =====
void TestSetValue(Tester& t, Handle<Entity> entity) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const ImageComponent* image = EntityManager::Get<ImageComponent>(entity);

	t.Check(SetChannel(entity, "Transform", "translation", 0, 5.0f) == ComponentValueResult::Written, "translation を書ける");
	t.Check(transform->translation.x == 5.0f && transform->translation.y == 0.0f, "使わない成分は変わらない");
	t.Check(SetChannel(entity, "Transform", "scale", 1, 2.0f) == ComponentValueResult::Written && transform->scale.y == 2.0f && transform->scale.x == 1.0f, "scale の y だけ");
	t.Check(SetChannel(entity, "Transform", "rotation", 2, 90.0f) == ComponentValueResult::Written, "rotation を書ける（中はクォータニオンでも、度で書く）");
	t.Near(GetEulerAngles(*transform).z, 90.0f, "rotation の z");
	t.Check(transform->rotation.w != 1.0f, "クォータニオンも変わる");

	t.Check(SetChannel(entity, "Image", "color", 3, 0.25f) == ComponentValueResult::Written, "色を書ける");
	t.Check(image->color.w == 0.25f && image->color.x == 1.0f, "色の a だけ");
	t.Check(SetChannel(entity, "Image", "size", 0, 40.0f) == ComponentValueResult::Written && image->size.x == 40.0f, "Vector2");
	t.Check(SetChannel(entity, "Image", "fill", 0, 0.5f) == ComponentValueResult::Written && image->fill == 0.5f, "float");
	t.Check(SetChannel(entity, "Image", "frame", 0, 2.6f) == ComponentValueResult::Written && image->frame == 3, "int は四捨五入");
	t.Check(SetChannel(entity, "Image", "enabled", 0, 0.0f) == ComponentValueResult::Written && !image->enabled, "bool");

	t.Check(SetChannel(entity, "Nope", "color", 0, 1.0f) == ComponentValueResult::NoComponent, "知らない Component");
	t.Check(SetChannel(entity, "Text", "color", 0, 1.0f) == ComponentValueResult::NoComponent, "付いていない Component");
	t.Check(SetChannel(entity, "Image", "nope", 0, 1.0f) == ComponentValueResult::NoField, "無い項目");
	t.Check(SetChannel(entity, "Image", "anchor", 0, 1.0f) == ComponentValueResult::NoField, "数でない項目（enum）");
	t.Check(SetChannel(Handle<Entity>{}, "Image", "color", 0, 1.0f) == ComponentValueResult::NoComponent, "無効な Entity");
}

// ===== 値のトラック =====
void TestValueTrack(Tester& t, Handle<Entity> rootA, Handle<Entity> titleA, Handle<Entity> rootB, Handle<Entity> titleB) {
	TimelineValueTrack track;
	track.target = "Panel/Title";
	track.component = "Image";
	track.property = "color";
	TimelineValueClip clip;
	clip.start = 1.0f;
	clip.duration = 1.0f;
	clip.curves[3] = Curve({CurveKey{.time = 0.0f, .value = 0.0f, .interp = CurveInterp::Linear}, CurveKey{.time = 1.0f, .value = 1.0f}});
	track.clips.push_back(clip);

	const std::unique_ptr<TimelineTrackState> stateA = track.CreateState();
	const std::unique_ptr<TimelineTrackState> stateB = track.CreateState();
	const ImageComponent* imageA = EntityManager::Get<ImageComponent>(titleA);
	const ImageComponent* imageB = EntityManager::Get<ImageComponent>(titleB);

	// 同じトラックを2つの Entity で、別の時間に動かす
	track.Evaluate({rootA, 0.5f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 0.0f, "最初のクリップの前は、そのクリップの始まりの値");
	track.Evaluate({rootA, 1.5f, {}, nullptr}, *stateA);
	track.Evaluate({rootB, 1.25f, {}, nullptr}, *stateB);
	t.Near(imageA->color.w, 0.5f, "クリップの途中");
	t.Near(imageB->color.w, 0.25f, "同じトラックを別の Entity で別の時間に");
	t.Check(stateA->target == titleA && stateB->target == titleB, "対象は Entity ごとに見つかる");
	track.Evaluate({rootA, 5.0f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 1.0f, "クリップの後は終わりの値のまま");

	// 2つのクリップの間は、前のクリップの終わりの値のまま
	TimelineValueClip second = clip;
	second.start = 4.0f;
	second.curves[3] = Curve({CurveKey{.time = 0.0f, .value = 0.25f}});
	track.clips.push_back(second);
	track.Evaluate({rootA, 3.0f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 1.0f, "クリップの間は前のクリップの終わりの値");
	track.Evaluate({rootA, 4.5f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 0.25f, "次のクリップに入る");

	// キーの無いトラックは何も書かない
	TimelineValueTrack empty;
	empty.target = "Panel/Title";
	empty.component = "Image";
	empty.property = "color";
	const std::unique_ptr<TimelineTrackState> stateEmpty = empty.CreateState();
	empty.Evaluate({rootA, 0.0f, {}, nullptr}, *stateEmpty);
	t.Near(imageA->color.w, 0.25f, "クリップの無いトラックは書かない");
}

// ===== ファイル =====
void TestJson(Tester& t) {
	const char* text = R"({
		"settings": {"length": 2.0},
		"tracks": [
			{"type": "TimelineValueTrack", "settings": {"name": "Fade", "target": "Panel", "component": "Image", "property": "color"},
			 "clips": [
				{"start": 3.0, "duration": 1.0, "curves": {"w": {"keys": [{"time": 0.0, "value": 1.0}, {"time": 1.0, "value": 0.0}]}}},
				{"start": 0.5, "duration": 1.0, "curves": {"w": {"keys": [{"time": 0.0, "value": 0.0}, {"time": 1.0, "value": 1.0}]}}}
			 ]},
			{"type": "TimelineEventTrack", "settings": {"name": "Calls"}, "clips": [{"start": 1.0, "name": "Hit", "value": "a"}]},
			{"type": "NotRegistered", "settings": {"name": "Later"}, "clips": []}
		]
	})";
	std::set<std::string> warnings;
	const Timeline timeline = TimelineManager::FromJson(SceneJson::parse(text), warnings);
	t.Check(timeline.tracks.size() == 2 && timeline.unknownTracks.size() == 1, "知らない種類は動かさず覚えておく");
	t.Check(warnings.size() == 1, "警告は知らない種類の1つだけ");
	t.Near(timeline.GetDuration(), 4.0f, "長さはクリップの終わりまで伸びる");
	const auto* value = dynamic_cast<const TimelineValueTrack*>(timeline.tracks[0].get());
	t.Check(value != nullptr && value->component == "Image" && value->clips.size() == 2, "値のトラックを読める");
	t.Check(value != nullptr && value->clips[0].start == 0.5f, "クリップは時間の順に並ぶ");
	t.Check(value != nullptr && value->clips[0].curves[3].GetKeys().size() == 2 && value->clips[0].curves[0].GetKeys().empty(), "使う成分だけカーブがある");

	// --- 書いて読むと同じ文字になる ---
	const std::string first = TimelineManager::ToJson(timeline).dump();
	std::set<std::string> ignored;
	const std::string second = TimelineManager::ToJson(TimelineManager::FromJson(SceneJson::parse(first), ignored)).dump();
	t.Check(first == second, "書いて読むと同じ中身になる");
	t.Check(first.find("NotRegistered") != std::string::npos, "知らない種類は保存しても消えない");

	// --- 壊れた形でも止まらない ---
	std::set<std::string> broken;
	t.Check(TimelineManager::FromJson(SceneJson::parse("{}"), broken).tracks.empty(), "空のファイル");
	t.Check(TimelineManager::FromJson(SceneJson::parse(R"({"tracks": 5})"), broken).tracks.empty(), "tracks が配列でない");
	t.Check(TimelineManager::FromJson(SceneJson::parse(R"({"tracks": [1, {"name": "x"}]})"), broken).tracks.empty(), "type の無いトラック");
	const Timeline odd = TimelineManager::FromJson(SceneJson::parse(R"({"tracks": [{"type": "TimelineEventTrack", "clips": [1, {"start": "x"}, {"start": -3.0}]}]})"), broken);
	t.Check(odd.tracks.size() == 1 && odd.tracks[0]->GetClipCount() == 2, "壊れたクリップは飛ばす");
	t.Check(!broken.empty(), "壊れた物は警告を出す");

	t.Check(TimelineTrackRegistry::Create("TimelineValueTrack") != nullptr && TimelineTrackRegistry::Create("TimelineEventTrack") != nullptr, "エンジンのトラックが登録されている");
	t.Check(TimelineTrackRegistry::Create("NotRegistered") == nullptr, "知らない名前は作らない");
}

// ===== root・Panel・Title（Image 付き）の親子を作る =====
Handle<Entity> MakeTestEntities(Handle<Entity>& panel, Handle<Entity>& title) {
	const Handle<Entity> root = EntityManager::Create("TimelineTest");
	panel = EntityManager::Create("Panel", root);
	title = EntityManager::Create("Title", panel);
	EntityManager::RestoreComponents(title, {ComponentSnapshot::Make(ImageComponent{})});
	return root;
}
} // namespace

//=============================================================================
// 確かめる
//=============================================================================
void TimelineSelfTest::Run() {
	Tester tester;
	Handle<Entity> panelA;
	Handle<Entity> titleA;
	Handle<Entity> panelB;
	Handle<Entity> titleB;
	const Handle<Entity> rootA = MakeTestEntities(panelA, titleA);
	const Handle<Entity> rootB = MakeTestEntities(panelB, titleB);

	TestPlayhead(tester);
	TestEvents(tester, rootA);
	TestPath(tester, rootA, panelA, titleA);
	TestValueTrack(tester, rootA, titleA, rootB, titleB);
	TestSetValue(tester, titleA);
	TestJson(tester);

	// 作った Entity をすぐ消す（シーンを作る前なので、残さない）
	EntityManager::Destroy(rootA);
	EntityManager::Destroy(rootB);
	EntityManager::FlushDestroy();

	MY_ASSERT_MSG(tester.failed == 0, std::format("Timeline の確認が合いません（{} 項目。ログを見てください）", tester.failed));
	LogManager::Log(std::format("[timeline] self test OK ({} / {})", tester.passed, tester.passed + tester.failed));
}
```

### C-7 `Engine.cpp`

include に足す。

```cpp
#include "MyEngine/Curve/CurveSelfTest.h"
// Timeline
#include "MyEngine/Timeline/TimelineSelfTest.h"
```

`CurveSelfTest::Run` の下に足す。

```cpp
	CurveSelfTest::Run();   // カーブの計算が合っているかを確かめる（Debugだけ）
	TimelineSelfTest::Run(); // Timeline の時間の進め方と、値の書き込みを確かめる（Debugだけ）
```

ここまでで Debug を起動すると、ログに `[timeline] self test OK` の行が出る。

## パートD ファイルの仮の Inspector

### D-1 `Editor/Inspector/TimelineAssetEditor.h`（新しいファイル）

```cpp
#pragma once

/// <summary>
/// .timeline のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>長さと、トラックの足し引き・設定まで。クリップとキーを直す画面はまだ無いので、ファイルを手で書く</para>
/// </summary>
class TimelineAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};
```

### D-2 `Editor/Inspector/TimelineAssetEditor.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "TimelineAssetEditor.h"

#include <format>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <externals/imgui/imgui.h>
#include <externals/nlohmann/json.hpp>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Timeline/TimelineManager.h"

namespace {
constexpr std::string_view kTypeName = "Timeline"; // Inspector の見出しに出す名前

// ===== トラック1つの中身 =====
// 消すボタンが押されたら true
bool DrawTrack(ComponentUI& ui, TimelineTrack& track) {
	track.DescribeCommon(ui);
	track.Fields(ui);
	ui.Label(std::format("クリップ：{} 個（直すのはファイルの clips）", track.GetClipCount()).c_str());
	return ImGui::SmallButton("Remove Track");
}

// ===== トラックを足す =====
void DrawAddTrack(Timeline& timeline) {
	if (!ImGui::BeginCombo("##addTrack", "Add Track")) {
		return;
	}
	for (const std::string& name : TimelineTrackRegistry::GetNames()) {
		if (ImGui::Selectable(name.c_str())) {
			timeline.tracks.push_back(TimelineTrackRegistry::Create(name));
		}
		const std::string& description = TimelineTrackRegistry::GetDescription(name);
		if (!description.empty()) {
			ImGui::SetItemTooltip("%s", description.c_str());
		}
	}
	ImGui::EndCombo();
}

/// <summary>
/// .timeline のファイルを Inspector で直す係
/// </summary>
class TimelineFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return TimelineManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return TimelineManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = TimelineManager::Load(filePath);
		Timeline* timeline = TimelineManager::GetForEdit(handle);
		if (timeline == nullptr) {
			return false;
		}
		// トラックは写せないので、前後の JSON を比べて、直したかを調べる
		const std::string before = TimelineManager::ToJson(*timeline).dump();

		TimelineManager::DescribeLength(ui, timeline->length);
		ImGui::SeparatorText("Tracks");
		int removeTrack = -1;
		for (int i = 0; i < static_cast<int>(timeline->tracks.size()); ++i) {
			TimelineTrack& track = *timeline->tracks[i];
			ImGui::PushID(i);
			// ### の後ろを固定する（名前を打ち換えても、開いたままにする）
			if (ImGui::CollapsingHeader(std::format("{}  ({})###track", track.name, track.typeName).c_str()) && DrawTrack(ui, track)) {
				removeTrack = i;
			}
			ImGui::PopID();
		}
		if (removeTrack >= 0) {
			timeline->tracks.erase(timeline->tracks.begin() + removeTrack);
		}
		DrawAddTrack(*timeline);
		if (!timeline->unknownTracks.empty()) {
			ui.Label(std::format("知らない種類のトラックが {} 個あります（動かしません。保存しても消えません）", timeline->unknownTracks.size()).c_str());
		}
		ui.Label("クリップとキーはファイルの clips を手で書く。Undo は無い。1つ前は .bak に残る");

		if (TimelineManager::ToJson(*timeline).dump() == before) {
			return false;
		}
		TimelineManager::MarkChanged(handle);
		return true;
	}

	void Save(const std::filesystem::path& filePath) override { TimelineManager::Save(TimelineManager::Load(filePath)); }
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void TimelineAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<TimelineFileEditor>()); }
#endif
```

### D-3 `Systems/EngineSystems.cpp`

`#ifdef USE_IMGUI` の中の include に足す。

```cpp
#include "MyEngine/Editor/Inspector/CurveAssetEditor.h"
#include "MyEngine/Editor/Inspector/TimelineAssetEditor.h"
```

`Initialize` の `#ifdef USE_IMGUI` の中に足す。

```cpp
	CurveAssetEditor::Initialize();  // .curve のファイルを Inspector で直す
	TimelineAssetEditor::Initialize(); // .timeline のファイルを Inspector で直す
```

### D-4 `Editor/Windows/ProjectWindowActions.cpp`

include に足す。

```cpp
#include "MyEngine/Physics/PhysicMaterialManager.h"
#include "MyEngine/Timeline/TimelineManager.h"
```

`DrawCreateMenu` の Curve の下に足す。

```cpp
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に対して値がどう変わるかのカーブ）");
		// Timeline
		if (ImGui::MenuItem("Timeline")) {
			CreateAssetFile("NewTimeline", TimelineManager::kFileExtension, &TimelineManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に沿って Component の値を動かす。Timeline Component で再生する）");
```

## パートE 確認

### E-1 確認用のファイル

`Docs/Tasks/TimelineTest/` に置いた。ゲームの `project` フォルダへ写す。

| ファイル | 写す先 |
|---|---|
| `TimelineTest.timeline` | `Resources/timelines/` |
| `TimelineTest.scene.json` | `Resources/scenes/` |
| `TimelineTestPanel.prefab` | `Resources/prefabs/` |
| `TimelineTestScreen.uiscreen` | `Resources/ui/` |
| `TimelineTester.cpp` | `Source/Scripts/TimelineTest/`（ゲームのプロジェクトに追加） |

### E-2 確認の手順

1. Debug をビルドして起動する。ログに `[timeline] self test OK (81 / 81)` が出れば、時間とファイルの確認は通っている。
2. Project ウィンドウから `TimelineTest` を開いて Play を押す。左上に Game Panel と Real Panel の状態が出る。
3. 下の表の順に確かめる。キーは 1：Play / 2：Pause / 3：Stop / 4：1 秒へシーク / 5：Loop / 6：速さ / 7：画面を開く・閉じる / 8：Prefab を2つ / H：ヒットストップ。

### E-3 確認項目

「写しで確認」は、一時フォルダの写しで実際に動かした結果。写経の後に、同じ項目を確かめる。

**自動の確認（`TimelineSelfTest`）**

| # | 区分 | 確かめること | 写しで確認 |
|---|---|---|---|
| 1 | 時間 | 再生・一時停止・続き・終わり・Stop・シークの範囲・NaN・0 秒 | 合う |
| 2 | ループ | 余りの時間、とても大きな秒、長さ 0 | 周の数は上限まで。止まらない |
| 3 | イベント | 1.5 の表の全部の行 | 全部1回ずつ |
| 4 | パス | 空・子・子の子・無い子・飛ばした段・無効な Entity | 合う |
| 5 | 値の書き込み | Transform の3つ、Image の色・Vector2・float・int・bool、使わない成分 | 合う |
| 6 | 書けない物 | 知らない・付いていない Component、無い項目、enum、無効な Entity | 書かずに結果で分かる |
| 7 | 値のトラック | クリップの前・途中・間・後、キーの無いトラック、同じトラックを2つの Entity で別の時間に | 合う。混ざらない |
| 8 | ファイル | 書いて読むと同じ、クリップの並び、知らない種類の保持、壊れた形 | 合う。止まらない |

**実際の再生（Release で流したシナリオ）**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 9 | 手書きの `.timeline` を読む | ファイルを置いて再生 | 全部のトラックを読めた |
| 10 | Transform の位置・回転・大きさ | 再生 | 0.187 秒で x 18.7、回転 z 8.4、scale 1.09・1.19（式と一致） |
| 11 | Image・Text・Vector Shape | 再生 | 色の a・`fontSize`・`gaugeAmount` が動く。r・g は動かない |
| 12 | 一時停止と続き | 2 → 1 | 止まっている間は同じ値。続きから動く |
| 13 | 再生中のシーク | 4 | 1.5 秒へ飛んで値も飛び、再生は続く |
| 14 | 一時停止中のシーク | 2 → 4 | 0.9 秒の値（a 0.900）がすぐ出る |
| 15 | Stop | 3 | 時間 0。全部 0 の値へ戻る（`fontSize` は 20） |
| 16 | 最後まで再生 | 待つ | `Finished`、最後の値のまま |
| 17 | 速さ | 6 | 2 倍の設定で時間が2倍 |
| 18 | ループ | 5 | `loops` が増える |
| 19 | イベントの順 | Loop で流し続ける | 450 回以上のイベントで `A B C` の順に欠けも重なりも無い |
| 20 | 子の Script へのイベント | Target を子に | 子の `OnTimelineEvent` に来た |
| 21 | イベントの中で Stop | `Stop` を呼ぶ Script | 止まる。後ろのイベントは呼ばれない |
| 22 | 同じファイルを同時に | Game Panel・Real Panel・Prefab 2 つ・画面の5つ | それぞれ別の時間で動く |
| 23 | Prefab | 8 | EntityId が違っても `Title` へ結び付く。片方だけ止められる |
| 24 | `.uiscreen` でゲームを止める | 7 | ゲームの時間が止まり、画面の Timeline（Real）は最後まで動く |
| 25 | ポーズ | `SetGamePaused(true)` | Real は進み、Game は 0.185 秒のまま止まる |
| 26 | ヒットストップ | H | Real は進み、Game は止まる |
| 27 | 対象が無い | Target を無い名前に | 警告1回。ほかのトラックは動く |
| 28 | Component・項目が無い | 名前を違える | 警告1回。ほかのトラックは動く |
| 29 | 再生中に対象を消す | 子を消す | 警告1回で動き続ける |
| 30 | 再生中に再生する Entity を消す | 親を消す | 落ちない。状態は `isValid` が false |
| 31 | ゲーム側のトラック | `TIMELINE_TRACK` で足す | ファイルから作られて、`Fields` の値も読めた |
| 32 | 知らない種類のトラック | `type` を変える | 警告1回。動かさない |
| 33 | Camera の Time Mode | 前のシーンを読む | `"timeMode": "Real"` のシーンを警告なしで読めた |

**エディタ（Debug で ImGui へ入力を流した）**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 34 | ファイルを作る | Project → Create → Timeline | `NewTimeline.timeline` ができて Inspector に出る |
| 35 | トラックを足す | Add Track | エンジンの2種類とゲーム側の種類が出る。選ぶとファイルに書かれる |
| 36 | Play と Stop | 再生 → 停止 → 再生 | 停止でシーンの値へ戻る。2回目も最初から動く |

**ビルド**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 37 | 3 構成のビルド | Debug・Development・Release | エラー 0。今回コンパイルしたファイルの警告は 0 |
| 38 | 3 構成の自己テスト | Debug の起動。Development・Release は一時的に呼ぶように変えて起動 | どれも 81 / 81 |
| 39 | パートC の後 | パートD のファイルと行を除いた状態 | Debug のビルドが通る |

### E-4 検証の結果

- **写し：** AL4_01 の形の一時フォルダで、エンジンを `b63b4da` に合わせて確かめた。md の新しいファイルのコードは、そのフォルダでコンパイルしたファイルから埋め込んだ。
- **シナリオ：** 表の 9〜33 は、確認用の Script で時間を見ながら Play・Stop などを呼び、値をファイルへ書いて確かめた。E-1 の `TimelineTester.cpp` そのものはコンパイルして起動し、Real Panel の再生と状態の表示まで見た。
- **確認の中で見つけて直した物：**
  - 最初のクリップの前を「書かない」にしていたので、Stop・ループで前の値が残った。そのクリップの始まりの値を書くようにした。
  - `AddEventListener` を `Script::Start` から呼ぶと、Play のたびに増えた。同じ関数は1回だけにした。

### E-5 未確認のこと・気を付けること

- **本物のキーボード：** E-2 のキーは押していない。同じ関数を確認用の Script から呼んで確かめた。
- **AL4_01 の実物：** 写しのゲームは `main.cpp` とシーンが違う。エンジンのコードは同じ。
- **パートA・B の後：** ここだけのビルドはしていない。C の後で通ることを確かめた。
- **`ImpulseTrack`：** コンパイルは通った。動かしたのは同じ形の別のトラック（表の 31）。
- **`.timeline` の壊れたファイル：** Inspector の `CanLoad` は `.curve` と同じ形だが、`.timeline` では試していない。
- **Real の Timeline のイベント：** ポーズ中も `OnTimelineEvent` が呼ばれる（1.6）。
- **同じ時間のフレームのイベント：** イベントの中で Stop しても、同じフレームに積まれた物は呼ばれる（1.6）。
- **Stop：** 再生前の値ではなく、Timeline の 0 の値へ戻る（0.4）。
- **パスの名前：** Hierarchy で名前を変えるとパスも直す必要がある。直す仕組みは無い。
