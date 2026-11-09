参考：
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- 状態の強さの順（Normal・Highlighted・Pressed・Selected・Disabled）：[Selectable のトランジション（Unity）](https://docs.unity3d.com/Packages/com.unity.ugui@2.0/manual/script-SelectableTransition.html)
- 今の値から動かす Tween・元の値からの差で動かす Tween の考え：[DOTween のドキュメント](https://dotween.demigiant.com/documentation.php)
- Timeline の中で別の Timeline を再生する考え（Control Track）：[Unity Timeline のマニュアル](https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/index.html)
- 前の段階：[VectorShape.md](VectorShape.md)・[Curve.md](Curve.md)・[Timeline.md](Timeline.md)・[TimelineWindow.md](TimelineWindow.md)・[CameraTimeline.md](CameraTimeline.md)
- 集成体を `()` で作る（C++20。`std::make_unique<TimelineAsset>(key, timeline)`）：https://en.cppreference.com/w/cpp/language/aggregate_initialization
- `std::ranges::replace_if`：https://en.cppreference.com/w/cpp/algorithm/ranges/replace
- `std::erase`（C++20）：https://en.cppreference.com/w/cpp/container/vector/erase2
- `std::function`：https://en.cppreference.com/w/cpp/utility/functional/function
- `magic_enum`（`enum_cast`・`enum_count`）：https://github.com/Neargye/magic_enum

# UI 段階5：UI アニメーションの統合

ここまでの図形（段階1）・カーブ（段階2）・Timeline（段階3・4）を、UI の作業の流れにつなぐ。目標は Titan の「すぐプレビューできる」「動的に変更できる」「再利用できる」。

- UI Screen の出し方・消し方を Timeline で作る（コード無し。今の Motion は残す）
- 要素の状態（マウスが乗った・押した・選んだ・使えない・出す・消す）で Timeline を再生する（新しい UI State）
- From → To で書く Tween のトラックと、別の Timeline をプリセットとして並べる Preset のトラック
- 実行中に値（Parameter）と対象を変える入口
- 同じ値を複数の再生が動かすときの順番（層）
- Timeline ウィンドウで、UI Screen の Show・Hide・ボタンの状態の Timeline を開いてプレビューする

[CameraTimeline.md](CameraTimeline.md) の後に写す。

パートは **A → B → C → D → E の順に写す**。A だけではビルドできない（`TimelineSystem` が B のトラックを登録するため）。B まで写せばビルドできる。F は見本（ゲーム側へ写す）、G は確認、H は今の Motion の画面を Timeline へ移す手順（しなくてもよい）、I は写しで確かめた結果。**J は後から足した Timeline Trigger（範囲に入ったら Timeline を再生する）**で、A〜E を写した後に写す（2026-11-02 に追記）。

作業の前に、develop から切ったブランチへ切り替える（ブランチは作ってある）。

```bash
git switch feature/UIAnimation
```

## 0章 全体の形

### 0.1 流れ

```
フレームの頭  UIScreen::Flush …… 前のフレームに Open した画面に Component が付いた → Show の Timeline の再生を頼む
    │
Script
    │
UIStateSystem::Update …… マウスで Hover・Pressed を決める。状態が変わったら、その状態の Timeline の再生を頼む
    │                       クリックの動き（画面を出す・消す・切り替える）
    ▼
TimelineSystem::Update …… 層の順に値を書く（後の層が勝つ）
    │   Component（Timeline Component）→ Script → UIState → UIVisibility → UIScreen
    │   トラック：Value（キー）/ Tween（From → To）/ Preset（別の Timeline）/ Event / Camera …
    ▼
ワールド行列・物理・カメラ・当たり判定 …
    ▼
UIScreen::Update …… Motion の画面は形を当てる。Timeline の画面は「終わったか」を見る（消し終わったら Entity を消す）
```

UI Screen・UI State・ゲームのコードは、どれも `TimelineSystem::PlayLayer` で「この Entity でこの Timeline を再生して」と頼むだけ。値を書くのは全部 `TimelineSystem` なので、プレビュー・イベント・時間の止め方は Timeline Component と同じになる。

### 0.2 作る物

| 物 | 役目 |
|---|---|
| 層（`TimelineLayer`）と `PlayLayer` | Component を使わずに、どの Entity でも Timeline を再生する。層の順に書くので、同じ値は後の層が勝つ |
| `SetParameter`・`SetTargetOverride` | 実行中に、Tween の端の値と、トラックの対象を変える |
| Tween のトラック | From → To を Ease のカーブで動かす。端は Current・Value・Add・Multiply・Parameter |
| Preset のトラック | 別の `.timeline` を、トラックの対象の上で再生する（ずらして並べると順に動く） |
| UI Screen の Show・Hide の Timeline・Time Mode | 出し方・消し方を Timeline で書く。動く時間を選ぶ |
| UI State（新しい Component） | 状態ごとの Timeline と、クリックしたときの動き |
| Timeline ウィンドウの欄 | 選んだ Entity の Timeline の欄を一覧で切り替える。空の欄は New で作る。Preset を対象の上で開く |
| 見本（ゲーム側） | 図形だけの HUD と、メニュー ⇄ 設定の画面の切り替え |
| Timeline Trigger（パートJ） | Collider の範囲に入ったら・出たら Timeline を再生する（層は Trigger） |

新しいファイル：

| ファイル | 中身 |
|---|---|
| `Timeline/Tween/TimelineTweenTrack.h`・`.cpp` | Tween のトラック（元の値もここで覚える） |
| `Timeline/Tween/TimelinePresetTrack.h`・`.cpp` | Preset のトラック |
| `Components/UI/UIStateComponent.h` | UI State の Component |
| `Systems/UI/UIStateSystem.h`・`.cpp` | UI State を動かす係 |

`Timeline` のフォルダは 15 ファイルあるので `Timeline/Tween` を作った（Tween と、Tween のプリセットを並べる Preset）。

### 0.3 使い方

**画面の出し方・消し方を Timeline で作る（コード無し）**

1. 今までどおり Project の Create → UI Screen で画面を作り、Edit UI Screen で中身を置く。Timeline で透明度を動かす要素には Alpha Group を付けておく。
2. Hierarchy で画面の一番上の Entity を選ぶ。Timeline ウィンドウの上の一覧に `UI Screen / Show (none)`・`UI Screen / Hide (none)` が出る。Show を選んで New を押すと、`resources/timelines/画面の名前_Show.timeline` ができて欄に入る（Inspector で選んだのと同じ。Ctrl+Z で外れる）。
3. Add Track → `PresetTrack`。Hierarchy の要素をトラックの行へドラッグして対象にし、列をダブルクリックしてクリップを作る。クリップの Timeline に `UIFadeIn` などのプリセットを選ぶと、長さがプリセットに合う。要素ごとに少しずつずらすと、順に出る。
4. 再生位置をつまむと、Scene ビューにその時間の形が出る（プレビュー）。Inspector・保存・Undo は元の値のまま。
5. Hide も同じ。**Hide の Tween は From を Current にする**（出している途中で閉じても、その形から消える）。
6. Done で画面を保存する（`.timeline` は直すたびに保存されている）。ゲームからは今までどおり `UIScreen::Open`・`Close`。ボタンから出すなら UI State の Click（下）。

Show・Hide の Timeline を選ばなければ、今までの Motion で動く。今ある画面はそのまま動く。

**ボタン（UI State）**

1. 要素に Vector Shape（か Image）と UI State を付ける（Add Component → UI → UI State）。マウスの当たりは、その形。
2. Normal・Hover・Pressed に、見本の `ButtonNormal`・`ButtonHover`・`ButtonPress` を選ぶ。選んでいない状態は Normal を再生する（元の形へ戻すため）。
3. Click の Action：`Open Screen`（Screen の名前の画面を出す）・`Close Screen`・`Close Own Screen`（この要素が入っている画面を消す）・`Switch Screen`（入っている画面を消して、Screen の画面を出す）。
4. 使えなくするのは Interactable を外す（Disabled の Timeline）。Script からは `UIStateSystem::SetSelected`・`SetVisible`・`IsClicked`。

**プリセットを作る。** Tween のトラックの Target を空（self）にして、端を Add・Multiply で書く（元の値からの差・割合）。どの要素に使っても、その要素の置いた位置・大きさから動く。

| やりたいこと | Component・Property | From → To |
|---|---|---|
| フェードイン | Alpha Group・alpha | Multiply 0 → Multiply 1 |
| 左から入る | Transform・translation | Add (-60, 0, 0) → Add (0, 0, 0) |
| ポップ | Transform・scale | Multiply 0.85 → Multiply 1（Ease を OutBack） |
| ホバー | Transform・scale | Current → Multiply 1.08 |
| 元へ戻す | 同じ項目 | Current → Multiply 1 |
| 消す | Alpha Group・alpha | Current → Value 0 |

Ease は右のカーブの絵の Ease Preset で作り直せる。横が時間の割合（0〜1）、縦が From〜To の割合なので、クリップの長さを変えても形はそのまま。

**実行中に値・対象を変える（コード）**

```cpp
TimelineSystem::SetParameter(gauge, "hp", 0.4f);                                       // To を Parameter "hp" にした Tween が読む
TimelineSystem::PlayLayer(gauge, TimelineLayer::Script, {.timeline = gaugeTimeline}); // 今の値から 0.4 へ動く
TimelineSystem::SetTargetOverride(hud, "Gauge", ring);                                // Target が "Gauge" のトラックに、Ring を動かさせる
```

画面の中の値を変えるときは、`UIScreen::GetRoot(画面の名前)` に渡す。

**ポーズ中に動く・止まる。** Time Mode（Timeline Component・UI State・UI Screen）で選ぶ。Real はポーズ中も動き、Game は止まる。ゲームを止める画面（Pause Game）は、いつも Real で動く（自分で止めた時間で止まらないように）。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 今の UIScreenMotion | 残す。役目は「ファイル無しで、直下の子を1つずつ同じ動きで出す・消す」。Show・Hide のどちらかに Timeline を選ぶと、その方だけ Motion の代わりになる | 今の画面が壊れない。Create → UI Screen で作った画面が、ファイル無しですぐ動く。細かく決めたいときだけ Timeline にすればよい。移したいときの手順は H |
| 再生の持ち方 | `TimelineSystem` に層と `PlayLayer` を足して、UI Screen・UI State・コードもここで再生する | プレビュー・イベント・`SetValue`・時間の止め方・ファイルを直したときの作り直しが、Timeline Component と全部同じになる。Entity に Timeline Component は1つしか付かないので、Component を増やす形にはしなかった |
| 同じ値を動かしたとき | 層の順（Component → Script → Trigger（パートJ）→ UIState → UIVisibility → UIScreen）に書くので、後の層が勝つ。同じ層では後から再生を始めた方が勝つ | 画面の出し入れ（UIScreen）は、ボタンのホバーの途中でも勝ってほしい。順番が表1つで決まり、毎回同じ結果になる |
| 終わった再生 | もう値を書かない（最後の値が残る） | 止まっている間に、ほかの層やゲームが書いた値を上書きしない（今までの Timeline の決まりのまま） |
| 同じ層にもう一度頼む | 前の再生を消して、新しく最初から再生する | 状態が変わるたびに、その状態の Timeline を最初から流せばよい。Tween の From を Current にしておけば、途中の値から続く |
| Tween の端 | Current・Value・Add・Multiply・Parameter の5つ | Current で切り替えの飛びを無くし、Add・Multiply で使い回し、Parameter で実行中に変える（Titan の「動的に変更」「再利用」） |
| 元の値（Add・Multiply の基準） | その Entity のその項目を、Tween が初めて動かしたときの値を覚える。エディタの停止・Entity が消える・`ResetBaseValues` で忘れる | ホバーを何回くり返しても、大きさが積み重ならない（今の値を基準にすると 1.08 倍の 1.08 倍…になる） |
| Current の値 | クリップが始まったフレームの今の値（時間が戻ったら取り直す）。プレビューでは置いてある値 | ホバーの途中で離したら、その大きさから戻る |
| 最初のクリップの前 | From が Current なら何も書かない。ほかは From の値を書く | 遅れて出る要素を、出る前は隠しておける。Current の動きは、始まるまで今の値のまま |
| Preset | トラックの対象を「再生する Entity」にして、別の `.timeline` を再生する。クリップの始まりがプリセットの 0 秒。4 段まで | 要素ごとに同じ動きをずらす（Motion の Stagger）を、Timeline でも書ける。プリセットは対象を空にして作れば何にでも使える |
| 自分を指す Preset | 読むときは、読んでいる途中のファイルを読まない（クリップは空になる）。再生では 4 段で止める | 読み続けて止まらなくなるのを防ぐ |
| Timeline のファイルの持ち方 | `unique_ptr` で1つずつ持つ | ファイルを読む途中（Preset のクリップ）や、ウィンドウで別のファイルを読んでも、中身の場所が動かない（`vector` のまま足すと、持っている参照が壊れる） |
| UI State の当たり | 同じ Entity の Vector Shape か Image の形 | ボタンの見た目と当たりが同じになる。今ある `IsMouseOver` をそのまま使う |
| 状態の強さ | Disabled ＞ Pressed ＞ Hover ＞ Selected ＞ Normal | Unity の Selectable と同じ。マウスで触っている間は、キーで選んだ印より強い |
| 最初のフレーム | その状態の Timeline の最後の形をすぐ当てる。マウスは見ない | 出てすぐ動き出さない。最初のフレームはワールド行列がまだ無いので、当たりが画面の真ん中になる（写しで見つけた） |
| ポーズ中のマウス | 開いている画面の中の要素だけが反応する | ポーズ画面の後ろの HUD のボタンが押せないように（Script の呼び方と同じ） |
| クリック | 上で押して、上で離した。Script からは次のフレームに `IsClicked` | 押したまま外へ出たら取り消せる（よくあるボタンの動き） |
| Switch Screen | 出す方を先に頼んでから消す | 先に消すと、ゲームを止める画面が1つも無いフレームができて、1フレームだけゲームが動いた（写しで見つけた）。待っている画面は、今の止め方のまま数える |
| 途中で閉じたとき（Motion） | 閉じた瞬間の形を基準に消す | 前は出きった形へ一瞬戻ってから消えていた |
| 途中で閉じたとき（Timeline） | Hide を同じ層で再生する（Show は置き換わって止まる） | Hide の Tween を Current にすれば、今の値から消える |
| シーンの切り替え | 画面・要素は Entity と一緒に消える。再生・渡した値・元の値も一緒に忘れる。止めていたゲームは動き出す | 消えた Entity を指した物を残さない |
| Timeline ウィンドウの欄 | Timeline Component・UI Screen・UI State の3つの欄を集める（エディタが型を知っている） | 欄を選ぶだけで、どの Timeline もその Entity でプレビューできる |
| New | `resources/timelines/Entity の名前_欄の名前.timeline` に作り、欄に入れる。Undo は Inspector と同じ履歴 | 作ってから選び直す手間を無くす。画面の編集中は画面の履歴に入り、Done で保存される |
| 表示の動きと状態の動き | 別の項目を動かす（見本：出し入れは位置と透明度、ボタンは大きさと色） | 同じ項目を2つの層で動かすと、後の層の最後の値が残る（1.8） |

### 0.5 やらなかったこと（後の項目）

| 項目 | 必要になる場面 |
|---|---|
| 層の値を混ぜる（足し算・重み） | 今は後の層が勝つだけ。ホバー中に出し入れの動きも足したいとき |
| 状態の間のクロスフェード | 今は Current で、途中の値から次の動きを始める |
| Preset の中のトラックを、親の Timeline の上で直す | 今は Open Preset で開いて直す（直すと、使っている全部に効く） |
| Text だけの要素の当たり | 今は透明な Vector Shape を一緒に置く |
| キー・パッドで選ぶメニューの仕組み | 今は Script で `SetSelected` と `IsClicked` を使う |
| 重なった要素の手前だけ Hover | 今は重なっていれば両方 |
| 対象の差し替えを Preset の中にも効かせる | 中のパスは別の Entity から数えるので、今は一番上の Timeline のトラックだけ |
| 折れ線・マスク・模様 | UI 段階6 |

## 1章 解説

### 1.1 層で重ねる

1つの値（ボタンの大きさなど）を、いくつもの再生が動かすことがある。

| 層 | 再生する物 | 例 |
|---|---|---|
| Component | Timeline Component（シーンの演出） | HUD の出だし |
| Script | ゲームのコードの `PlayLayer` | ゲージを動かす・一度ふくらませる |
| Trigger（パートJ） | Timeline Trigger（範囲に入った・出た） | 近づくと扉が開く・看板が出る |
| UIState | UI State の Normal・Hover・Pressed・Selected・Disabled | ボタンのホバー |
| UIVisibility | UI State の Show・Hide（`SetVisible`） | HUD の部品を出す・消す |
| UIScreen | UI Screen の Show・Hide | メニューを出す・消す |

`TimelineSystem::Update` は、この順に全部の再生の値を書く。後に書いた値が残るので、**同じ値は表の下の層が勝つ**。同じ層の中では、後から再生を始めた物が後ろに並ぶので勝つ。

実行中の値は `runtimes` という1つの `vector` に、層の順に並べて持つ。足すときは `std::ranges::upper_bound` で「同じ層の一番後ろ」に入れる。

```cpp
// 層が layer より大きい最初の場所（＝同じ層の一番後ろの次）に入れる
const auto position = std::ranges::upper_bound(runtimes, layer, {}, &TimelineRuntime::layer);
```

Timeline Component の層だけは、今までどおり `ForEach<TimelineComponent>` の順（Entity の順）で動かす。カメラ段階2 の「2つの Timeline は後の頼みが勝つ（Entity の順）」を変えないため。

### 1.2 Tween の端と、元の値

Tween のクリップは「From から To へ、Ease のカーブで進む」。値のトラックのキーと違い、端の値を「どう決めるか」で書く。

| 端の決め方 | 値 | 使いどころ |
|---|---|---|
| Current | クリップが始まったときの今の値 | 途中から続ける（ホバーを離した、出している途中で閉じた） |
| Value | 書いた値そのもの | 0 にする、決まった色にする |
| Add | 元の値 ＋ 書いた値 | 置いた位置からずらして入る |
| Multiply | 元の値 × 書いた値 | 置いた大きさ・透明度に対する割合 |
| Parameter | `SetParameter` で渡した値（渡していなければ書いた値） | HP のゲージ |

**元の値**は、その Entity のその項目を Tween が初めて動かしたときの値。`TimelineTweenTrack.cpp` の `baseValues` に覚え、Entity が消える・エディタの停止で忘れる。

なぜ「初めて」なのか：ホバー（Current → Multiply 1.08）の基準を「今の値」にすると、ホバー中にもう一度ホバーが始まったとき 1.08 × 1.08 になり、くり返すほど大きくなる。最初に覚えた値を基準にすれば、何回くり返しても 1.08 倍で止まる。

その代わり、ゲームが要素の位置を書き換えたら、古い元の値に戻ってしまう。そのときは `TimelineSystem::ResetBaseValues(entity)` を呼ぶ。動かす値はゲームと Timeline で分ける（ゲームは親を動かし、Timeline は子を動かす）のが一番安全。

プレビューでは、毎フレームの終わりに値が元へ戻る（`FinishPreview`）ので、元の値も Current も「置いてある値」を使う。

### 1.3 途中で切り替えても、値が飛ばない

**Timeline の画面・UI State。** 同じ層に再生を頼むと、前の再生は消えて新しい再生が最初から始まる。新しい Tween の From が Current なら、始まったフレームの今の値（＝前の再生が最後に書いた値）から動く。だから「ホバーで 1.05 まで大きくなった所で離した」なら、1.05 から 1.0 へ戻る。

見本の記録（Release、1/60 秒ずつ）。メニューを出している途中（0.3 秒）で閉じた所：

| フレーム | パネルの透明度 | パネルの大きさ | 後ろの暗さ |
|---|---|---|---|
| 253（出している途中） | 0.999 | 1.014 | 1.000 |
| 254（閉じた） | 0.770 | 1.014 | 0.813 |
| 255 | 0.578 | 1.014 | 0.651 |
| 265 | 0.000 | 0.900 | 0.008 |

大きさ 1.014 は OutBack の行き過ぎの途中で、そこから 0.9 へ縮む。閉じたフレームで 1.0 に戻ったりしない。

**Motion の画面。** Motion は「置いた形 ＋ 見えている量」で形を作る。今までは消し始めも置いた形が基準だったので、出している途中で閉じると、一度出きった形へ飛んでから消えていた。`Close` のときに今の形を `closePose` に覚えて、消す動きはそれを基準にした。

```cpp
ApplyElement(element, isShowing ? element.basePose : element.closePose, motion, CalcVisible(...));
```

### 1.4 Preset のトラック

Preset のクリップは、別の `.timeline` を「このトラックの対象を再生する Entity にして」動かす。

```
MenuShow.timeline（再生する Entity：画面の一番上）
  ├ Panel In   （対象 Panel）       0.10 秒から UIPopIn.timeline
  ├ Resume In  （対象 Panel/Resume）0.20 秒から UISlideInLeft.timeline
  └ Settings In（対象 Panel/Settings）0.26 秒から UISlideInLeft.timeline
        UISlideInLeft.timeline の中のトラックは Target が空（self）なので、Panel/Settings を動かす
```

時間の直し方：

| 親の時間 | プリセットの時間 |
|---|---|
| クリップの前 | 0 秒（From が Current でない Tween は、始まりの値を書く＝出る前は隠れている） |
| クリップの中 | 親の時間 − クリップの始まり |
| クリップの後 | クリップの長さ（終わりの値のまま） |

イベントも呼ぶ。このフレームに通った幅（`TimelineRange`）を、クリップの中だけに切ってプリセットの時間へずらす。クリップの始まりをまたいだら、0 秒ちょうどのイベントも呼ぶ。

プリセットのトラックの値（`TimelineStates`）は、クリップごとに Preset のトラックの State が持つ。プリセットのファイルを直したら版が変わるので、次のフレームに作り直す。

**自分を指すとき。** A の中に A を入れると、読むたびに A を読みに行って止まらない。`TimelineManager::Load` が「読んでいる途中のファイル」を覚えておき、同じファイルは読まずに 0 を返す。読み終わった後に A と B がお互いを指すようにした場合は、再生の深さ（4 段）で止める。

### 1.5 UI State

毎フレーム（Script の後）、要素ごとに次を決める。

1. マウスが形の上か（`VectorShapeSystem::IsMouseOver` か `SpriteRenderSystem::IsMouseOver`）
2. 上で左ボタンを押したら「押している」。離したとき上ならクリック
3. 状態：Interactable が外れていれば Disabled、押していて上なら Pressed、上なら Hover、選んだ印があれば Selected、ほかは Normal
4. 状態が変わったら、その状態の Timeline を UIState の層で再生（無ければ Normal の Timeline）

マウスを見ないとき：最初のフレーム（ワールド行列がまだ無い）・消えている（Hide の後）・Interactable が外れている・Use Mouse が外れている・ポーズ中で開いている画面の外。

Show・Hide は UIVisibility の層なので、状態の再生と置き換わらない。消えている間はマウスに反応しない。Start Visible を外すと、最初のフレームに Hide の最後の形を当てる。

### 1.6 UI Screen と Timeline

`BeginShow`（フレームの頭）で Show の Timeline を `PlayLayer(一番上, UIScreen, …)` で頼み、`Update` で `GetStatus` が Finished になったら Shown にする。`Close` は Hide を同じ層で頼み、Finished になったら Entity を消す。消える Entity と一緒に再生も消える（`OnEntityDestroyed`）。

ゲームを止めるかの数え方を1つ直した。`Switch Screen` は「今の画面を消して別の画面を出す」が、`Open` した画面は次のフレームまで Component が無いので、止めるかどうかが分からない。そこで Waiting の画面は「今の止め方のまま」と数える。あわせて Switch Screen は Open を先に呼ぶ。

| 順番 | 消す → 出す | 出す → 消す |
|---|---|---|
| 消したときの数え | 止める画面 0 → ゲームが動く | Waiting の画面が「止めたまま」と数える |
| 結果（写しの記録） | 1フレームだけ `paused=false` | ずっと `paused=true` |

Time Mode：画面の動き（Motion と Timeline）は Time Mode の時間で進む。Pause Game を入れた画面は Real に固定する（Game にすると、自分でゲームを止めた瞬間に自分の動きも止まって、出てこなくなる）。

### 1.7 Timeline ウィンドウの欄

ウィンドウは、選んだ Entity の「Timeline のファイルを選ぶ欄」を全部集める（`CollectSources`）。

| Component | 欄 | Play 中に見る層 |
|---|---|---|
| Timeline | Timeline | Component |
| UI Screen | UI Screen / Show・Hide | UIScreen |
| UI State | UI State / Normal〜Disabled | UIState |
| UI State | UI State / Show・Hide | UIVisibility |

欄ごとに「ファイルを入れる関数」（`std::function<void(uint32_t)>`）を持つ。中身は `EditComponent<T>`：Inspector で値を変えたときと同じく、変える前と後の Component を `EditorHistory::RecordComponentChange` に積む。だから New で入れたファイルは Ctrl+Z で外れ、画面の編集中なら Done で `.uiscreen` に保存される。

開いているファイルが、どの欄にもない（＝別のシーンの同じ番号の Entity かもしれない）ときは、その Entity でプレビューしない。Preset を開いたときだけは、欄に無くても対象でプレビューする（`isExplicitOwner`）。

Play 中にウィンドウの再生ボタンで動かせるのは Timeline Component だけ。UI State・UI Screen の再生は、その係が決めるので、時間を見せるだけにした。

### 1.8 同じ項目を2つの層で動かすと

例：鍵のかかったボタン（Disabled の Timeline で暗くする）を、メニューの Show で左から入れる。

- Disabled（UIState の層）は最初のフレームに「暗い色」を書いて終わる。
- Show（UIScreen の層）は、0.3 秒かけて位置と透明度を書く。

Show が色を書かないので、ボタンは暗いまま入ってくる。もし Show も色を書いていたら、Show の最後の色（元の色）が残り、Disabled の暗さは消える（終わった再生は、もう書かないため）。

なので見本は「出し入れは位置と透明度、状態は大きさと色」と分けた。同じ項目を動かしたいときは、どちらが勝つかを 1.1 の表で決める。

### 1.9 あまり使われない C++

**集成体を `()` で作る（C++20）。** コンストラクタを書いていない struct（集成体）は、C++20 から `()` でも作れる。`std::make_unique<T>(a, b)` の中は `T(a, b)` なので、集成体も `make_unique` で作れるようになった。

```cpp
state->assets.push_back(std::make_unique<TimelineAsset>(key, std::move(timeline))); // TimelineAsset{key, timeline} と同じ
```

**`magic_enum::enum_cast<T>(文字)`。** 文字から enum を探す。知らない文字なら空の `std::optional`。Tween の端の決め方をファイルから読むのに使った。`magic_enum::enum_count<T>()` は enum の数（`kUIElementStateCount`）。

**`std::ranges::replace_if(範囲, 条件, 新しい値)`。** 条件に合う要素を全部置き換える。New で作るファイルの名前から、使えない文字を `_` にする。

**`std::erase(vector, 値)`（C++20）。** 値と同じ要素を全部消す（`erase(remove(...))` と同じ）。

**投影にラムダを渡す。** `std::ranges::find(範囲, 値, 投影)` は、要素を投影に通してから比べる。要素が `unique_ptr` なので、ラムダで中の `path` を取り出す。戻り値を `const std::string&` にしたのは、比べるたびに文字を写さないため（書かないと値で返る）。

```cpp
std::ranges::find(state->assets, key, [](const std::unique_ptr<TimelineAsset>& asset) -> const std::string& { return asset->path; });
```

**仮想関数の `const` 版と非 `const` 版。** `GetPropertyBinding` を2つ書いた。`const TimelineTrack&` から呼ぶと `const` 版、直すときは非 `const` 版が呼ばれる（どちらも同じ `binding` を返す）。

## パートA Timeline の土台

層・実行中の値・項目の指し方を足す。B まで写すとビルドが通る。

### A-1 `Timeline/TimelineTrack.h`

include に足す。

```cpp
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Time/EngineTime.h"
```

`TimelineFrame` を、上に3つの struct を足した形に差し替え、下に `TimelinePropertyBinding` を足す（`TimelineFrame` に `overrides` が1行増えて、コメントの位置がそろった）。

```cpp
/// <summary>
/// 実行中に渡した値1つ（TimelineSystem::SetParameter。Tween の Parameter が名前で読む）
/// </summary>
struct TimelineParameter {
	std::string name;
	Vector4 value;
};

/// <summary>
/// 実行中に差し替えた対象1つ（TimelineSystem::SetTargetOverride）
/// </summary>
struct TimelineTargetOverride {
	std::string path;      // トラックの Target に書いてある文字
	Handle<Entity> target; // 代わりに動かす Entity
};

/// <summary>
/// 再生する Entity に、実行中に渡した値・差し替えた対象（同じ Entity の全部の層の再生が見る）
/// </summary>
struct TimelineOverrides {
	Handle<Entity> owner;                        // 渡した先の、再生する Entity（対象の差し替えは、この Entity を再生する Entity にしたトラックだけに効く）
	std::vector<TimelineParameter> parameters;   // 渡した値
	std::vector<TimelineTargetOverride> targets; // 差し替えた対象

	// 名前の値（無ければ nullptr）
	const Vector4* FindParameter(std::string_view name) const;
	// path の代わりの Entity（無ければ nullptr）
	const Handle<Entity>* FindTarget(std::string_view path) const;
};

/// <summary>
/// 1フレーム分の再生の様子（トラックの Evaluate に渡す）
/// </summary>
struct TimelineFrame {
	Handle<Entity> owner;                            // 再生している Entity
	float time = 0.0f;                               // 今の時間（秒）
	std::span<const TimelineRange> ranges;           // このフレームに通った時間の幅（シーク・停止で値を書き直すだけのときは空）
	std::vector<TimelineEvent>* events = nullptr;    // 呼ぶイベントを足す場所（全部の値を書いた後にまとめて呼ばれる）
	TimeMode timeMode = TimeMode::Game;              // 再生に使っている時間（Timeline Component の Time Mode）
	bool isPreview = false;                          // エディタの停止中のプレビュー
	const TimelineOverrides* overrides = nullptr;    // 実行中に渡した値・差し替えた対象（無ければ nullptr。プレビューも nullptr）
};

/// <summary>
/// Component の1つの項目の指し方（値のトラック・Tween のトラックが持つ。Timeline ウィンドウの Component・Property の欄に使う）
/// </summary>
struct TimelinePropertyBinding {
	std::string component = "Transform";  // Component の名前（Inspector の見出しと同じ。Script の名前でもよい）
	std::string property = "translation"; // 項目の名前

	void Fields(ComponentUI& ui);
};
```

`TimelineTrack` の `EvaluatesEveryFrame` の下に足す。

```cpp
	// 時間が止まっている間も、毎フレーム Evaluate を呼ぶか（カメラの切り替えのように、毎フレーム頼む種類）。再生していない（Stopped）間は呼ばない
	virtual bool EvaluatesEveryFrame() const { return false; }
	// Component の1つの項目を動かす種類なら、その指し方（Timeline ウィンドウが Component・Property の欄を出す）。違えば nullptr
	virtual TimelinePropertyBinding* GetPropertyBinding() { return nullptr; }
	virtual const TimelinePropertyBinding* GetPropertyBinding() const { return nullptr; }
```

### A-2 `Timeline/TimelineTrack.cpp`

`FindTarget` の頭に足す（残りは今のまま）。

```cpp
// ===== 対象を探す =====
Handle<Entity> TimelineTrack::FindTarget(const TimelineFrame& frame, TimelineTrackState& state) const {
	// 実行中に差し替えた対象があれば、それを使う（差し替えた先の Timeline のトラックだけ。Preset の中のパスは別の Entity から数えるので使わない）
	if (frame.overrides != nullptr && frame.overrides->owner == frame.owner) {
		if (const Handle<Entity>* replaced = frame.overrides->FindTarget(target)) {
			return EntityManager::IsAlive(*replaced) ? *replaced : Handle<Entity>{};
		}
	}
	// 覚えている相手が生きていれば、それを使う（探すのは Entity を全部調べるので、毎フレームはしない）
```

`WarnOnce` の下（クリップの大見出しの上）に足す。

```cpp
//=============================================================================
// 実行中に渡した値・差し替えた対象
//=============================================================================
const Vector4* TimelineOverrides::FindParameter(std::string_view name) const {
	const auto found = std::ranges::find(parameters, name, &TimelineParameter::name);
	return (found != parameters.end()) ? &found->value : nullptr;
}

const Handle<Entity>* TimelineOverrides::FindTarget(std::string_view path) const {
	const auto found = std::ranges::find(targets, path, &TimelineTargetOverride::path);
	return (found != targets.end()) ? &found->target : nullptr;
}

//=============================================================================
// Component の項目の指し方
//=============================================================================
void TimelinePropertyBinding::Fields(ComponentUI& ui) {
	ui.Field("Component###component", component, Tip("動かす Component の名前（Inspector の見出しと同じ。Image・Vector Shape など）"));
	ui.Field("Property###property", property, Tip("動かす項目の名前（Transform は translation・rotation・scale。ほかはシーンファイルに書かれている名前）"));
}
```

### A-3 `Timeline/TimelineValueTrack.h`

`component`・`property` を `binding` にまとめる（ファイルの項目の名前は同じなので、前に保存した `.timeline` もそのまま読める）。

```cpp
class TimelineValueTrack : public TimelineTrackOf<TimelineValueClip> {
public:
	TimelinePropertyBinding binding; // 動かす Component と項目

	void Fields(ComponentUI& ui) override { binding.Fields(ui); }
	TimelinePropertyBinding* GetPropertyBinding() override { return &binding; }
	const TimelinePropertyBinding* GetPropertyBinding() const override { return &binding; }
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
	std::span<Curve> GetClipCurves(size_t index) override { return clips[index].curves; }
	bool CanPreview() const override { return true; }
};
```

### A-4 `Timeline/TimelineValueTrack.cpp`

`TimelineValueTrack::Fields`（見せ方の中見出しごと）を消す（`TimelinePropertyBinding::Fields` へ移した）。`Evaluate` の「書く」の `switch` を直す。

```cpp
	// --- 書く ---
	switch (ComponentSerializer::SetValue(entity, binding.component, binding.property, value)) {
	case ComponentValueResult::Written:
		break;
	case ComponentValueResult::NoComponent:
		WarnOnce(state, std::format("\"{}\" に Component \"{}\" がありません", target, binding.component));
		break;
	case ComponentValueResult::NoField:
		WarnOnce(state, std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", binding.component, binding.property));
		break;
	}
```

### A-5 `Timeline/TimelineManager.h`

`Timeline` の上に `TimelineStates` を足し、`Timeline` に `Evaluate` を足す。

```cpp
/// <summary>
/// Timeline 1つ分の、トラックごとの「Entity ごとの値」（再生する側が1つずつ持つ。TimelineSystem・Preset のトラック）
/// </summary>
struct TimelineStates {
	uint32_t timeline = 0;                                   // 作ったときのファイル
	uint32_t revision = 0;                                   // 作ったときのファイルの版
	std::vector<std::unique_ptr<TimelineTrackState>> tracks; // [トラックの番号]

	// handle の今の中身に合わせる。ファイルか版が違えば作り直して true（対象も探し直す）。同じなら何もしないで false
	bool Prepare(uint32_t handle);
};

/// <summary>
/// Timeline（ファイル1つの中身）。トラックの数が変わるので Component には入れられず、ここに持つ
/// </summary>
struct Timeline {
	float length = 1.0f;                                // 長さ（秒）
	std::vector<std::unique_ptr<TimelineTrack>> tracks; // トラック（上から順に動かす）
	std::vector<std::string> unknownTracks;             // 知らない種類のトラック（JSON の文字のまま持ち、保存で消さない）

	// 再生する長さ（length と、一番遅いクリップの終わりの大きい方。クリップが切れないように）
	float GetDuration() const;

	/// <summary>
	/// トラックを上から順に動かす（states は Prepare 済みの物）
	/// <para>isApplying が false のフレームは、毎フレーム動かす種類（EvaluatesEveryFrame）だけ。isStopped ならそれも動かさない。プレビューは CanPreview の種類だけ</para>
	/// </summary>
	void Evaluate(const TimelineFrame& frame, TimelineStates& states, bool isApplying, bool isStopped) const;
};
```

`Load` のコメントに1行足して、`Get` のコメントを直す。

```cpp
	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// <para>読んでいる途中のファイル（Preset のクリップが自分を指している）は読まずに 0 を返す</para>
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ほかのファイルを読んでも場所は変わらないが、ApplyText で中身が作り直される
	static const Timeline* Get(uint32_t handle);
```

### A-6 `Timeline/TimelineManager.cpp`

`TimelineStorage` と `Find` を直す。

```cpp
struct TimelineStorage {
	// 番号 - 1 番目のファイル。unique_ptr で1つずつ持つのは、読んでいる途中に別のファイルを読んでも（Preset のクリップ）、中身の場所が動かないように
	std::vector<std::unique_ptr<TimelineAsset>> assets;
	std::vector<std::string> loadingPaths; // 読んでいる途中のファイル（Preset のクリップが自分を指していても、読み続けないように）
};
std::unique_ptr<TimelineStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
TimelineAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return state->assets[handle - 1].get();
}
```

`Timeline::GetDuration` の下に足す。

```cpp
void Timeline::Evaluate(const TimelineFrame& frame, TimelineStates& states, bool isApplying, bool isStopped) const {
	for (size_t i = 0; i < tracks.size() && i < states.tracks.size(); ++i) {
		const TimelineTrack& track = *tracks[i];
		const bool isEvaluated = frame.isPreview ? track.CanPreview() : (track.EvaluatesEveryFrame() ? !isStopped : isApplying);
		if (isEvaluated && !track.isMuted) {
			track.Evaluate(frame, *states.tracks[i]);
		}
	}
}

//=============================================================================
// トラックの Entity ごとの値
//=============================================================================
bool TimelineStates::Prepare(uint32_t handle) {
	const Timeline* timeline = TimelineManager::Get(handle);
	const uint32_t current = TimelineManager::GetRevision(handle);
	if (timeline == nullptr) {
		*this = {};
		return false;
	}
	if (handle == this->timeline && current == revision && tracks.size() == timeline->tracks.size()) {
		return false;
	}
	this->timeline = handle;
	revision = current;
	tracks.clear();
	for (const std::unique_ptr<TimelineTrack>& track : timeline->tracks) {
		tracks.push_back(track->CreateState());
	}
	return true;
}
```

`Load` の「読み込み済みなら」から「ファイルから読む」までを直す。

```cpp
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, [](const std::unique_ptr<TimelineAsset>& asset) -> const std::string& { return asset->path; });
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- 読んでいる途中のファイル（Preset のクリップが自分を指している）は読まない（読むと、また同じファイルを読み続ける）---
	if (std::ranges::find(state->loadingPaths, key) != state->loadingPaths.end()) {
		LogManager::Warning(std::format("Timeline が自分をプリセットにしています（そのクリップは空にします）: {}", key));
		return 0;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
```

`Load` の終わりの方（`FromJson` の所から `push_back` まで）を直す。

```cpp
	std::set<std::string> warnings;
	state->loadingPaths.push_back(key);
	Timeline timeline = FromJson(json, warnings); // Preset のクリップの中のファイルも、ここで読む
	std::erase(state->loadingPaths, key);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back(std::make_unique<TimelineAsset>(key, std::move(timeline)));
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
```

### A-7 `Systems/Animation/TimelineSystem.h`（全体を差し替え）

`TimelineLayer`・`TimelinePlaySettings` を足し、`TimelineStatus` に `timeline` を足した。

```cpp
#pragma once
#include <cstdint>
#include <string_view>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/Timeline/TimelineEvent.h"
#include "HTTEngine/Timeline/TimelinePlayhead.h"

/// <summary>
/// 再生の層。同じ Entity の同じ層で再生できるのは1つだけ（もう一度頼むと置き換わる）
/// <para>値は並びの順（Component → Script → … → UIScreen）に書くので、同じ値を動かすと後の層が勝つ。同じ層の中では、後から再生を始めた方が勝つ</para>
/// </summary>
enum class TimelineLayer {
	Component,    // Timeline Component の再生（Play・Pause・Stop・Seek）
	Script,       // ゲームのコードから PlayLayer で再生した物
	UIState,      // UI State の Normal・Hover・Pressed・Selected・Disabled
	UIVisibility, // UI State の Show・Hide
	UIScreen,     // UI Screen の Show・Hide
};

/// <summary>
/// PlayLayer の再生の設定
/// </summary>
struct TimelinePlaySettings {
	uint32_t timeline = 0;              // 再生するファイル（AssetType::Timeline の番号）
	bool loop = false;                  // 最後まで行ったら最初へ戻る
	float speed = 1.0f;                 // 再生速度（0 以下は進まない）
	TimeMode timeMode = TimeMode::Game; // 使う時間（Real：ポーズ中も動く）
	bool jumpToEnd = false;             // 動かさずに、最初のフレームで最後の値を書く（最初の形をすぐ当てる）
};

/// <summary>
/// Entity の今の再生の様子（TimelineSystem::GetStatus）。読むたびに作る写しなので、値は古くなる
/// </summary>
struct TimelineStatus {
	bool isValid = false;                                 // 再生する物があり、ファイルが読めている
	uint32_t timeline = 0;                                // 再生しているファイル
	TimelinePlayState state = TimelinePlayState::Stopped; // 再生の状態
	float time = 0.0f;                                    // 今の時間（秒）
	float duration = 0.0f;                                // 長さ（秒）
	int loopCount = 0;                                    // 最初へ戻った回数

	bool IsPlaying() const { return state == TimelinePlayState::Playing; }
	// 終わりまでの割合（0〜1）
	float GetProgress() const { return (duration > 0.0f) ? time / duration : 0.0f; }
};

/// <summary>
/// Timeline を再生する係
/// <para>Timeline のファイルは全部の Entity で共有する。今の時間・対象の Entity はここが Entity と層ごとに持つ（最初に使うときに作り、エディタの Stop で消える）</para>
/// <para>順番：全部の Entity の値を層の順に書く → 全部のイベントを呼ぶ（イベントの中で Stop・Destroy をしても、回している途中の物が壊れない）</para>
/// </summary>
class TimelineSystem {
public:
	// Timeline Component と、エンジンのトラックの種類を登録する。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の Timeline を1フレーム進める。Script の後、ワールド行列の計算の前に呼ぶ（動かした位置を、そのフレームの描画に使うため）
	/// <para>CameraSystem::Update より前に呼ぶ（カメラのトラックの頼みを、そのフレームのカメラに使うため）</para>
	/// </summary>
	/// <param name="isGameRunning">再生中か（エディタの停止中は何も動かさない）</param>
	static void Update(bool isGameRunning);

	// ===== Timeline Component の再生を指示する（ゲーム・カメラ）。どれも Timeline Component が無い・ファイルが無い・エディタの停止中は false =====
	// 再生する。止まっていれば最初から、一時停止中なら続きから
	static bool Play(Handle<Entity> entity);
	// 一時停止する
	static bool Pause(Handle<Entity> entity);
	// 止める。時間を 0 に戻し、次の Update で 0 の値を書く
	static bool Stop(Handle<Entity> entity);
	// time 秒へ移動する。次の Update でその時間の値を書く。イベントは呼ばない
	static bool Seek(Handle<Entity> entity, float time);

	// ===== Component を使わずに再生する（UI State・UI Screen・ゲームのコード）=====
	/// <summary>
	/// owner の layer で、settings.timeline を最初から再生する（前の再生は置き換わる）。最後まで行ったら、最後の値のまま止まる（Loop でなければ）
	/// <para>トラックの対象は owner からのパス。値を書くのは時間が動いたフレームだけ（止まった後は、ほかの層・ゲームが書いた値を上書きしない）</para>
	/// </summary>
	/// <returns>layer が Component・ファイルが無い・owner が無いなら false</returns>
	static bool PlayLayer(Handle<Entity> owner, TimelineLayer layer, const TimelinePlaySettings& settings);
	// owner の layer の再生をやめる（値は今のまま。書くのをやめるだけ）
	static void StopLayer(Handle<Entity> owner, TimelineLayer layer);

	// ===== 外から読む =====
	static TimelineStatus GetStatus(Handle<Entity> entity, TimelineLayer layer = TimelineLayer::Component);

	// ===== 実行中に、値・対象を変える（owner の全部の層の再生に効く。owner が消える・エディタの停止で忘れる）=====
	// Tween の Parameter に渡す値。再生中に変えれば、次のフレームから効く（1つの値の項目は x を使う）
	static void SetParameter(Handle<Entity> owner, std::string_view name, const Vector4& value);
	// 1つの値（全部の成分に同じ値を入れる）
	static void SetParameter(Handle<Entity> owner, std::string_view name, float value);
	/// <summary>
	/// 対象が path のトラックを、target を動かすようにする（無効な Handle を渡すと元へ戻す）
	/// <para>path はトラックの Target に書いてある文字のまま。owner の Timeline のトラックだけに効く（Preset の中のトラックには効かない）</para>
	/// </summary>
	static void SetTargetOverride(Handle<Entity> owner, std::string_view path, Handle<Entity> target);
	// owner に渡した値・差し替えた対象を全部忘れる
	static void ClearOverrides(Handle<Entity> owner);
	// Tween の Add・Multiply の元の値を忘れる（次に動かしたときの値が、新しい元の値になる。ゲームで置き直した要素に使う）
	static void ResetBaseValues(Handle<Entity> entity);

	// ===== プレビュー（エディタの停止中に、再生せずに値を見る）=====
	/// <summary>
	/// 次の Update で、time 秒の値を仮に書く。見るのは1フレームだけなので、見続ける間は毎フレーム呼ぶ（呼ばなくなれば、それだけで元の値のまま）
	/// <para>値を書くのは Update から描画の要求を積み終わる FinishPreview までの間だけ。フレームの頭の保存・Undo と、後の ImGui のウィンドウは元の値を見る</para>
	/// </summary>
	/// <param name="owner">再生する Entity（トラックの対象はここからのパス）</param>
	/// <param name="timeline">見る Timeline の番号</param>
	/// <param name="time">見る時間（秒）</param>
	static void RequestPreview(Handle<Entity> owner, uint32_t timeline, float time);
	// 仮に書いた値を元へ戻す。描画の要求を全部積んだ後に、1フレームに1回呼ぶ
	static void FinishPreview();

	// ===== イベントを受け取る =====
	// Script は OnTimelineEvent を書く。Script 以外で受け取りたいときに、ここへ関数を登録する（登録した関数は Release まで呼ばれる。同じ関数は1回だけ）
	using EventListener = void (*)(const TimelineEvent& event);
	static void AddEventListener(EventListener listener);

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};
```

### A-8 `Systems/Animation/TimelineSystem.cpp`（全体を差し替え）

実行中の値を層つきにし、Timeline Component の再生も、`PlayLayer` の再生も `UpdateRuntime` 1つで進める。トラックを動かすのは `Timeline::Evaluate`（A-6）。

```cpp
#include "TimelineSystem.h"

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <optional>
#include <algorithm>

#include "HTTEngine/Components/Animation/TimelineComponent.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
#include "HTTEngine/Timeline/TimelineEventTrack.h"
#include "HTTEngine/Timeline/TimelineManager.h"
#include "HTTEngine/Timeline/TimelineValueTrack.h"
#include "HTTEngine/Timeline/Camera/TimelineCameraShakeTrack.h"
#include "HTTEngine/Timeline/Camera/TimelineCameraTrack.h"
#include "HTTEngine/Timeline/Tween/TimelinePresetTrack.h"
#include "HTTEngine/Timeline/Tween/TimelineTweenTrack.h"

namespace {
//=============================================================================
// 再生1つ分の実行中の値
//=============================================================================
struct TimelineRuntime {
	Handle<Entity> entity;                          // 再生する Entity
	TimelineLayer layer = TimelineLayer::Component; // どの層の再生か
	TimelinePlaySettings settings;                  // 再生の設定（Component の層は、毎フレーム Component から作り直す）
	TimelineStates states;                          // トラックの Entity ごとの値
	TimelinePlayhead playhead;                      // 時間の位置
	bool needsApply = false;                        // 時間が飛んだ（シーク・停止・再生のし直し）。次の Update で値を書く
};

std::vector<TimelineRuntime> runtimes;                // 再生（層の順に並べる。同じ層の中は、後から再生を始めた物が後ろ）
std::vector<TimelineOverrides> overrides;             // Entity ごとの、実行中に渡した値・差し替えた対象
std::vector<TimelineRange> passedRanges;              // このフレームに通った時間の幅（毎回確保しないように使い回す）
std::vector<TimelineEvent> pendingEvents;             // 全部の値を書いた後に呼ぶイベント
std::vector<TimelineSystem::EventListener> listeners; // AddEventListener の関数
bool isRunning = false;                               // 再生中（エディタの停止中は false）

/// <summary>
/// 頼まれたプレビュー
/// </summary>
struct PreviewRequest {
	Handle<Entity> owner;  // 再生する Entity
	uint32_t timeline = 0; // 見る Timeline
	float time = 0.0f;     // 見る時間（秒）
};
std::optional<PreviewRequest> previewRequest; // 次の Update で見る物（毎フレーム頼まれる）
Handle<Entity> previewOwner;                  // プレビューのトラックの値を作ったときの、再生する Entity
TimelineStates previewStates;                 // プレビューのトラックの値（毎フレーム作らないように持っておく）
bool isPreviewApplied = false;                // 値を仮に書いている（FinishPreview で戻す）

// ===== その Entity・層の実行中の値（まだ作っていなければ nullptr）=====
TimelineRuntime* FindRuntime(Handle<Entity> entity, TimelineLayer layer) {
	const auto found = std::ranges::find_if(runtimes, [&](const TimelineRuntime& runtime) { return runtime.entity == entity && runtime.layer == layer; });
	return (found != runtimes.end()) ? &*found : nullptr;
}

// ===== 実行中の値を足す（同じ層の一番後ろ。値を書く順は、この並びの順）=====
TimelineRuntime& AddRuntime(Handle<Entity> entity, TimelineLayer layer) {
	const auto position = std::ranges::upper_bound(runtimes, layer, {}, &TimelineRuntime::layer);
	return *runtimes.insert(position, TimelineRuntime{.entity = entity, .layer = layer});
}

// ===== Entity に渡した値・差し替えた対象（無ければ nullptr）=====
TimelineOverrides* FindOverrides(Handle<Entity> owner) {
	const auto found = std::ranges::find(overrides, owner, &TimelineOverrides::owner);
	return (found != overrides.end()) ? &*found : nullptr;
}

// ===== Component の設定から、再生の設定を作る =====
TimelinePlaySettings SettingsOf(const TimelineComponent& component) {
	return {.timeline = component.timeline, .loop = component.loop, .speed = component.speed, .timeMode = component.timeMode};
}

// ===== Timeline Component の再生の実行中の値を用意する（Component・ファイルが無ければ nullptr）=====
TimelineRuntime* PrepareRuntime(Handle<Entity> entity) {
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
	const Timeline* timeline = (component != nullptr) ? TimelineManager::Get(component->timeline) : nullptr;
	if (!isRunning || timeline == nullptr) {
		return nullptr;
	}
	TimelineRuntime* runtime = FindRuntime(entity, TimelineLayer::Component);
	if (runtime == nullptr) {
		runtime = &AddRuntime(entity, TimelineLayer::Component);
		runtime->states.Prepare(component->timeline);
		if (component->playOnStart) {
			runtime->playhead.Play();
		}
	} else if (runtime->states.Prepare(component->timeline)) {
		runtime->needsApply = true; // ファイルを替えた・直した
	}
	runtime->settings = SettingsOf(*component);
	return runtime;
}

//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 再生1つを1フレーム進める =====
void UpdateRuntime(TimelineRuntime& runtime) {
	const Timeline* timeline = TimelineManager::Get(runtime.settings.timeline);
	if (timeline == nullptr || !EntityManager::IsActiveInHierarchy(runtime.entity)) {
		return; // ファイルが無い・無効な Entity の間は時間も進めない
	}
	if (runtime.states.Prepare(runtime.settings.timeline)) {
		runtime.needsApply = true; // ファイルを直した
	}
	// --- 時間を進める。通った幅があるか、時間が飛んだときだけ書く（止まっている間は、ゲームが書いた値を上書きしない）---
	const float speed = (runtime.settings.speed > 0.0f) ? runtime.settings.speed : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
	runtime.playhead.Advance(Time::GetDeltaTime(runtime.settings.timeMode) * speed, timeline->GetDuration(), runtime.settings.loop, passedRanges);
	TimelineFrame frame;
	frame.owner = runtime.entity;
	frame.time = runtime.playhead.GetTime();
	frame.ranges = passedRanges;
	frame.events = &pendingEvents;
	frame.timeMode = runtime.settings.timeMode;
	frame.overrides = FindOverrides(runtime.entity);
	timeline->Evaluate(frame, runtime.states, !passedRanges.empty() || runtime.needsApply, runtime.playhead.GetState() == TimelinePlayState::Stopped);
	runtime.needsApply = false;
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

// ===== プレビュー：今の値を写して、見る時間の値を書く =====
void ApplyPreview(const PreviewRequest& request) {
	const Timeline* timeline = TimelineManager::Get(request.timeline);
	if (timeline == nullptr || !EntityManager::IsAlive(request.owner)) {
		return;
	}
	// --- トラックの値を用意する（警告は出さない。キーを動かすたびに作り直すのでログが埋まる。問題は Timeline ウィンドウの行に出す）---
	if (previewOwner != request.owner) {
		previewOwner = request.owner;
		previewStates = {}; // 別の Entity の対象を覚えているので、作り直す
	}
	if (previewStates.Prepare(request.timeline)) {
		for (const std::unique_ptr<TimelineTrackState>& state : previewStates.tracks) {
			state->isWarned = true;
		}
	}
	// --- 書く（イベントは呼ばない。ranges が空なので、通った時間は無い）---
	TimelineFrame frame;
	frame.owner = request.owner;
	frame.time = request.time;
	frame.events = &pendingEvents;
	frame.isPreview = true;
	ComponentSerializer::BeginValueCapture();
	isPreviewApplied = true;
	CameraSystem::BeginPreviewShake(request.time);
	timeline->Evaluate(frame, previewStates, true, false);
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
	TimelineTrackRegistrar<TimelineCameraTrack>("TimelineCameraTrack", "クリップごとに写すカメラを決める。クリップを重ねると補間する");
	TimelineTrackRegistrar<TimelineCameraShakeTrack>("TimelineCameraShakeTrack", "指定の時間に、カメラへ衝撃を送って揺らす");
	TimelineTrackRegistrar<TimelineTweenTrack>("TimelineTweenTrack", "Component の1つの項目を From → To で動かす（今の値から・元の値からの差で書ける）");
	TimelineTrackRegistrar<TimelinePresetTrack>("TimelinePresetTrack", "別の Timeline（プリセット）を、対象の上で再生する。ずらして並べると、順に動く");
}

void TimelineSystem::Release() {
	runtimes.clear();
	overrides.clear();
	pendingEvents.clear();
	listeners.clear();
	previewRequest.reset();
	previewOwner = {};
	previewStates = {};
	isRunning = false;
	TimelineTweenTrack::ClearBaseValues();
}

//=============================================================================
// 毎フレーム
//=============================================================================
void TimelineSystem::Update(bool isGameRunning) {
	isRunning = isGameRunning;
	const std::optional<PreviewRequest> preview = std::exchange(previewRequest, std::nullopt); // 頼まれた1フレーム分だけ
	if (!isGameRunning) {
		// 停止中は動かさない（Play を押すと、また最初から）
		runtimes.clear();
		overrides.clear();
		TimelineTweenTrack::ClearBaseValues();
		if (preview.has_value()) {
			ApplyPreview(*preview);
		}
		return;
	}
	// --- Component を外された Entity を片付ける ---
	std::erase_if(runtimes, [](const TimelineRuntime& runtime) { return runtime.layer == TimelineLayer::Component && EntityManager::Get<TimelineComponent>(runtime.entity) == nullptr; });
	// --- Timeline Component の層（今までと同じ、Entity の順）→ ほかの層（並びの順。後の層ほど後に書くので勝つ）---
	EntityManager::ForEach<TimelineComponent>([](Handle<Entity> entity, TimelineComponent&) {
		if (TimelineRuntime* runtime = PrepareRuntime(entity)) {
			UpdateRuntime(*runtime);
		}
	});
	for (TimelineRuntime& runtime : runtimes) {
		if (runtime.layer != TimelineLayer::Component) {
			UpdateRuntime(runtime);
		}
	}
	SendEvents();
}

//=============================================================================
// Timeline Component の再生を指示する
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
		runtime->playhead.Seek(time, TimelineManager::Get(runtime->settings.timeline)->GetDuration());
		runtime->needsApply = true;
	}
	return runtime != nullptr;
}

//=============================================================================
// Component を使わずに再生する
//=============================================================================
bool TimelineSystem::PlayLayer(Handle<Entity> owner, TimelineLayer layer, const TimelinePlaySettings& settings) {
	const Timeline* timeline = TimelineManager::Get(settings.timeline);
	if (layer == TimelineLayer::Component || timeline == nullptr || !EntityManager::IsAlive(owner)) {
		return false;
	}
	// 前の再生を消して、同じ層の一番後ろに足し直す（後から始めた方が勝つ）。停止中に頼まれても、次の Update で消える
	StopLayer(owner, layer);
	TimelineRuntime& runtime = AddRuntime(owner, layer);
	runtime.settings = settings;
	runtime.states.Prepare(settings.timeline);
	runtime.playhead.Play();
	if (settings.jumpToEnd) {
		runtime.playhead.Seek(timeline->GetDuration(), timeline->GetDuration()); // 再生中のまま最後へ（次の Update で最後の値を書いて、終わる）
	}
	runtime.needsApply = true; // 時間が進まないフレーム（Game の時間でポーズ中）でも、最初の値を書く
	return true;
}

void TimelineSystem::StopLayer(Handle<Entity> owner, TimelineLayer layer) {
	if (layer == TimelineLayer::Component) {
		return; // Component の層は Stop で止める
	}
	std::erase_if(runtimes, [&](const TimelineRuntime& runtime) { return runtime.entity == owner && runtime.layer == layer; });
}

//=============================================================================
// 実行中に、値・対象を変える
//=============================================================================
void TimelineSystem::SetParameter(Handle<Entity> owner, std::string_view name, const Vector4& value) {
	TimelineOverrides* found = FindOverrides(owner);
	if (found == nullptr) {
		found = &overrides.emplace_back(TimelineOverrides{.owner = owner});
	}
	const auto parameter = std::ranges::find(found->parameters, name, &TimelineParameter::name);
	if (parameter != found->parameters.end()) {
		parameter->value = value;
	} else {
		found->parameters.push_back({std::string(name), value});
	}
}

void TimelineSystem::SetParameter(Handle<Entity> owner, std::string_view name, float value) { SetParameter(owner, name, Vector4{value, value, value, value}); }

void TimelineSystem::SetTargetOverride(Handle<Entity> owner, std::string_view path, Handle<Entity> target) {
	TimelineOverrides* found = FindOverrides(owner);
	if (found == nullptr) {
		if (!target.IsValid()) {
			return; // 元へ戻すだけなので、作らない
		}
		found = &overrides.emplace_back(TimelineOverrides{.owner = owner});
	}
	std::erase_if(found->targets, [path](const TimelineTargetOverride& item) { return item.path == path; });
	if (target.IsValid()) {
		found->targets.push_back({std::string(path), target});
	}
}

void TimelineSystem::ClearOverrides(Handle<Entity> owner) {
	std::erase_if(overrides, [owner](const TimelineOverrides& item) { return item.owner == owner; });
}

void TimelineSystem::ResetBaseValues(Handle<Entity> entity) { TimelineTweenTrack::ResetBaseValues(entity); }

//=============================================================================
// プレビュー
//=============================================================================
void TimelineSystem::RequestPreview(Handle<Entity> owner, uint32_t timeline, float time) { previewRequest = PreviewRequest{owner, timeline, time}; }

void TimelineSystem::FinishPreview() {
	if (!isPreviewApplied) {
		return;
	}
	isPreviewApplied = false;
	ComponentSerializer::RestoreCapturedValues();
	EntityManager::UpdateTransforms(); // 動かした物の子のワールド行列も元へ戻す（この後の ImGui のギズモが読む）
}

//=============================================================================
// 外から読む
//=============================================================================
TimelineStatus TimelineSystem::GetStatus(Handle<Entity> entity, TimelineLayer layer) {
	// --- 再生しているファイル（Component の層は、まだ再生していなくても Component のファイル）---
	const TimelineRuntime* runtime = FindRuntime(entity, layer);
	uint32_t handle = (runtime != nullptr) ? runtime->settings.timeline : 0;
	if (layer == TimelineLayer::Component) {
		const TimelineComponent* component = EntityManager::Get<TimelineComponent>(entity);
		handle = (component != nullptr) ? component->timeline : 0;
	}
	const Timeline* timeline = TimelineManager::Get(handle);
	if (timeline == nullptr) {
		return {};
	}
	TimelineStatus status;
	status.isValid = true;
	status.timeline = handle;
	status.duration = timeline->GetDuration();
	if (runtime != nullptr) {
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
	ClearOverrides(entity);
	TimelineTweenTrack::ResetBaseValues(entity);
}
```

## パートB Tween と Preset のトラック

新しいフォルダ `Timeline/Tween` に4つのファイルを作る。ソリューションエクスプローラーで HTTEngine に足す（前の段階の `Timeline/Camera` と同じやり方）。B まで写したら Debug をビルドする。

### B-1 `Timeline/Tween/TimelineTweenTrack.h`（新しいファイル）

```cpp
#pragma once
#include <set>
#include <memory>
#include <string>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Timeline/TimelineTrack.h"
#include "HTTEngine/Timeline/TimelineValueTrack.h"

/// <summary>
/// Tween の端（From・To）の値の決め方
/// </summary>
enum class TimelineTweenMode {
	Current,   // クリップが始まったときの今の値（途中から別の動きに替わっても、値が飛ばない）
	Value,     // Value に書いた値
	Add,       // 元の値 ＋ Value（元の位置からずらす）
	Multiply,  // 元の値 × Value（元の大きさ・透明度に対する割合）
	Parameter, // 実行中に TimelineSystem::SetParameter で渡した値（渡していなければ Value）
};

/// <summary>
/// Tween の端1つ（From か To）
/// </summary>
struct TimelineTweenPoint {
	TimelineTweenMode mode = TimelineTweenMode::Value;
	Vector4 value = {0.0f, 0.0f, 0.0f, 0.0f}; // Value・Add・Multiply・Parameter（渡していないとき）に使う値。1つの値の項目は x だけ使う。色は r, g, b, a の順
	std::string parameter;                    // Parameter のときの名前（TimelineSystem::SetParameter に渡す名前）
};

/// <summary>
/// Tween のクリップ。From から To へ、Ease のカーブで動かす
/// <para>Ease は 横：時間の割合 0〜1（クリップの始まり〜終わり）→ 縦：進んだ割合 0〜1（From〜To）。クリップの長さを変えても形はそのまま。1 を超えると To より行き過ぎる</para>
/// </summary>
struct TimelineTweenClip : TimelineClip {
	TimelineTweenPoint from = {.mode = TimelineTweenMode::Current};
	TimelineTweenPoint to = {.mode = TimelineTweenMode::Value};
	Curve ease = Curve::FromEase(Ease::Type::OutCubic);

	void WriteExtra(SceneJson& out) const;
	void ReadExtra(SceneJson& in, std::set<std::string>& warnings);
};


/// <summary>
/// Component の1つの項目を、From → To の Tween で動かすトラック（値のトラックのキーの代わりに、端の値の決め方で動きを書く）
/// <para>From を Current にすると、動いている途中の値から続けて動く（ホバーの途中で離した・出している途中で閉じた）</para>
/// <para>Add・Multiply は元の値からの差・割合なので、対象を空（再生する Entity 自身）にして作れば、同じファイルをどの要素にも使い回せる</para>
/// <para>元の値は、その Entity のその項目を Tween が初めて動かしたときの値（TimelineSystem::ResetBaseValues で取り直す）。プレビューでは置いてある値</para>
/// <para>最初のクリップの前：From が Current なら何も書かない。ほかは From の値を書く（遅れて出る要素を、出る前は隠しておける）</para>
/// </summary>
class TimelineTweenTrack : public TimelineTrackOf<TimelineTweenClip> {
public:
	TimelinePropertyBinding binding; // 動かす Component と項目

	void Fields(ComponentUI& ui) override { binding.Fields(ui); }
	TimelinePropertyBinding* GetPropertyBinding() override { return &binding; }
	const TimelinePropertyBinding* GetPropertyBinding() const override { return &binding; }
	std::string GetClipLabel(size_t index) const override;
	bool CanPreview() const override { return true; }
	std::unique_ptr<TimelineTrackState> CreateState() const override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;

	// ===== 元の値（Add・Multiply の基準。TimelineSystem から呼ぶ）=====
	// entity の覚えた元の値を忘れる（次に動かしたときの値が、新しい元の値になる）
	static void ResetBaseValues(Handle<Entity> entity);
	// 全部忘れる（エディタの停止）
	static void ClearBaseValues();
};
```

### B-2 `Timeline/Tween/TimelineTweenTrack.cpp`（新しいファイル）

`Evaluate` の流れ：時間が戻ったら Current を取り直す → 今のクリップ（前なら最初のクリップ）→ From が Current でクリップの前なら何もしない → 今の値を読む（使う成分も分かる）→ 始まったときの値・元の値 → 割合 → 端の値を決めて混ぜて書く。

```cpp
#include "TimelineTweenTrack.h"

#include <array>
#include <format>
#include <vector>
#include <optional>
#include <algorithm>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"

namespace {
constexpr const char* kFromKey = "from";     // ファイルの From の項目の名前
constexpr const char* kToKey = "to";         // ファイルの To の項目の名前
constexpr const char* kEaseKey = "ease";     // ファイルの Ease のカーブの項目の名前
constexpr const char* kModeKey = "mode";     // 端の決め方
constexpr const char* kValueKey = "value";   // 端の値
constexpr const char* kParameterKey = "parameter"; // Parameter の名前

/// <summary>
/// Tween のトラックの、Entity ごとの値
/// </summary>
class TimelineTweenState : public TimelineTrackState {
public:
	std::vector<std::optional<ComponentValue>> startValues; // [クリップの番号] 始まったときの今の値（Current に使う。無ければまだ始まっていない）
	float lastTime = 0.0f;                                  // 前に動かした時間（戻ったら、始まったときの値を取り直す）
};

/// <summary>
/// 覚えた元の値1つ（Add・Multiply の基準）
/// </summary>
struct BaseValue {
	Handle<Entity> entity;
	std::string component;
	std::string property;
	ComponentValue value;
};
std::vector<BaseValue> baseValues; // 覚えた元の値（Entity が消える・エディタの停止で忘れる。数が少ないので一列に並べて順に探す）

// ===== Vector4 を成分の並びにする =====
std::array<float, ComponentValue::kChannelCount> ToChannels(const Vector4& value) { return {value.x, value.y, value.z, value.w}; }

// ===== 元の値（初めて聞かれたら、今の値を覚える）=====
ComponentValue FindBaseValue(Handle<Entity> entity, const TimelinePropertyBinding& binding, const ComponentValue& current) {
	const auto found = std::ranges::find_if(baseValues, [&](const BaseValue& base) { return base.entity == entity && base.component == binding.component && base.property == binding.property; });
	if (found != baseValues.end()) {
		return found->value;
	}
	baseValues.push_back({entity, binding.component, binding.property, current});
	return current;
}

// ===== 端の値を決める =====
// start は始まったときの値（Current）、base は元の値（Add・Multiply）
ComponentValue ResolvePoint(const TimelineTweenPoint& point, const ComponentValue& start, const ComponentValue& base, const TimelineOverrides* overrides) {
	const Vector4* parameter = (point.mode == TimelineTweenMode::Parameter && overrides != nullptr) ? overrides->FindParameter(point.parameter) : nullptr;
	const std::array<float, ComponentValue::kChannelCount> value = ToChannels((parameter != nullptr) ? *parameter : point.value);
	ComponentValue result = start; // 使う成分（isUsed）は今の値と同じ
	for (size_t i = 0; i < ComponentValue::kChannelCount; ++i) {
		switch (point.mode) {
		case TimelineTweenMode::Current:
			break; // 始まったときの値のまま
		case TimelineTweenMode::Value:
		case TimelineTweenMode::Parameter:
			result.channels[i] = value[i];
			break;
		case TimelineTweenMode::Add:
			result.channels[i] = base.channels[i] + value[i];
			break;
		case TimelineTweenMode::Multiply:
			result.channels[i] = base.channels[i] * value[i];
			break;
		}
	}
	return result;
}

//=============================================================================
// ファイル
//=============================================================================
// ===== 端1つを書く =====
SceneJson PointToJson(const TimelineTweenPoint& point) {
	SceneJson json = SceneJson::object();
	json[kModeKey] = std::string(magic_enum::enum_name(point.mode));
	json[kValueKey] = SceneJson::array({point.value.x, point.value.y, point.value.z, point.value.w});
	if (!point.parameter.empty()) {
		json[kParameterKey] = point.parameter;
	}
	return json;
}

// ===== 端1つを読む（読めない項目は既定の値のまま）=====
void PointFromJson(const SceneJson& json, TimelineTweenPoint& point, std::set<std::string>& warnings) {
	if (!json.is_object()) {
		warnings.insert("Tween の端（from・to）が読めません");
		return;
	}
	if (const auto mode = json.find(kModeKey); mode != json.end() && mode->is_string()) {
		// enum_cast：名前から enum を探す（知らない名前なら空）
		const auto parsed = magic_enum::enum_cast<TimelineTweenMode>(mode->get_ref<const std::string&>());
		if (parsed.has_value()) {
			point.mode = *parsed;
		} else {
			warnings.insert(std::format("知らない Tween の決め方 \"{}\"", mode->get_ref<const std::string&>()));
		}
	}
	if (const auto value = json.find(kValueKey); value != json.end() && value->is_array() && value->size() == ComponentValue::kChannelCount) {
		std::array<float, ComponentValue::kChannelCount> channels{};
		for (size_t i = 0; i < channels.size(); ++i) {
			channels[i] = (*value)[i].is_number() ? (*value)[i].get<float>() : 0.0f;
		}
		point.value = {channels[0], channels[1], channels[2], channels[3]};
	}
	if (const auto name = json.find(kParameterKey); name != json.end() && name->is_string()) {
		point.parameter = name->get_ref<const std::string&>();
	}
}
} // namespace

//=============================================================================
// クリップのファイル
//=============================================================================
void TimelineTweenClip::WriteExtra(SceneJson& out) const {
	out[kFromKey] = PointToJson(from);
	out[kToKey] = PointToJson(to);
	out[kEaseKey] = CurveManager::ToJson(ease);
}

void TimelineTweenClip::ReadExtra(SceneJson& in, std::set<std::string>& warnings) {
	if (const auto item = in.find(kFromKey); item != in.end()) {
		PointFromJson(*item, from, warnings);
		in.erase(item);
	}
	if (const auto item = in.find(kToKey); item != in.end()) {
		PointFromJson(*item, to, warnings);
		in.erase(item);
	}
	if (const auto item = in.find(kEaseKey); item != in.end()) {
		ease = CurveManager::FromJson(*item, warnings);
		in.erase(item);
	}
}

//=============================================================================
// トラック
//=============================================================================
// ===== クリップの上の名前（Current → Multiply・Parameter は名前）=====
std::string TimelineTweenTrack::GetClipLabel(size_t index) const {
	const auto label = [](const TimelineTweenPoint& point) {
		return (point.mode == TimelineTweenMode::Parameter) ? std::format("${}", point.parameter) : std::string(magic_enum::enum_name(point.mode));
	};
	return std::format("{} → {}", label(clips[index].from), label(clips[index].to));
}

std::unique_ptr<TimelineTrackState> TimelineTweenTrack::CreateState() const { return std::make_unique<TimelineTweenState>(); }

// ===== 動かす =====
void TimelineTweenTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (clips.empty()) {
		return;
	}
	auto& tween = static_cast<TimelineTweenState&>(state);
	tween.startValues.resize(clips.size());
	// --- 時間が戻った（最初から・ループ・前へのシーク）：始まったときの値を取り直す ---
	if (frame.time < tween.lastTime) {
		std::ranges::fill(tween.startValues, std::nullopt);
	}
	tween.lastTime = frame.time;

	// --- 今のクリップ（最初のクリップの前は、最初のクリップの始まり）---
	const TimelineTweenClip* found = FindClip(frame.time);
	const bool isBefore = (found == nullptr);
	const TimelineTweenClip& clip = isBefore ? clips.front() : *found;
	if (isBefore && clip.from.mode == TimelineTweenMode::Current) {
		return; // 始まる前は今の値のまま（何も書かない）
	}
	const Handle<Entity> entity = FindTarget(frame, state);
	if (!entity.IsValid()) {
		return;
	}
	// --- 今の値（使う成分も、ここで分かる）---
	ComponentValue current;
	switch (ComponentSerializer::GetValue(entity, binding.component, binding.property, current)) {
	case ComponentValueResult::Written:
		break;
	case ComponentValueResult::NoComponent:
		WarnOnce(state, std::format("\"{}\" に Component \"{}\" がありません", target, binding.component));
		return;
	case ComponentValueResult::NoField:
		WarnOnce(state, std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", binding.component, binding.property));
		return;
	}
	// --- 始まったときの値・元の値（プレビューは毎フレーム置いてある値に戻るので、いつも今の値）---
	ComponentValue start = current;
	ComponentValue base = current;
	if (!frame.isPreview) {
		std::optional<ComponentValue>& started = tween.startValues[static_cast<size_t>(&clip - clips.data())];
		if (!isBefore && !started.has_value()) {
			started = current;
		}
		start = started.value_or(current);
		base = FindBaseValue(entity, binding, current);
	}
	// --- From から To へ（Ease のキーが無ければ、まっすぐ）---
	const float rate = isBefore ? 0.0f : ((clip.duration > 0.0f) ? std::clamp((frame.time - clip.start) / clip.duration, 0.0f, 1.0f) : 1.0f);
	const float amount = clip.ease.GetKeys().empty() ? rate : clip.ease.Evaluate(rate);
	const ComponentValue from = ResolvePoint(clip.from, start, base, frame.overrides);
	const ComponentValue to = ResolvePoint(clip.to, start, base, frame.overrides);
	ComponentValue value;
	for (size_t i = 0; i < ComponentValue::kChannelCount; ++i) {
		value.isUsed[i] = current.isUsed[i];
		value.channels[i] = from.channels[i] + (to.channels[i] - from.channels[i]) * amount;
	}
	ComponentSerializer::SetValue(entity, binding.component, binding.property, value);
}

//=============================================================================
// 元の値
//=============================================================================
void TimelineTweenTrack::ResetBaseValues(Handle<Entity> entity) {
	std::erase_if(baseValues, [entity](const BaseValue& base) { return base.entity == entity; });
}

void TimelineTweenTrack::ClearBaseValues() { baseValues.clear(); }
```

### B-3 `Timeline/Tween/TimelinePresetTrack.h`（新しいファイル）

```cpp
#pragma once
#include <memory>
#include <string>
#include <cstdint>

#include "HTTEngine/Timeline/TimelineTrack.h"

/// <summary>
/// Preset のクリップ。別の Timeline のファイル（プリセット）を、トラックの対象の上で再生する
/// </summary>
struct TimelinePresetClip : TimelineClip {
	uint32_t timeline = 0; // 再生するファイル（AssetType::Timeline の番号）

	void Fields(ComponentUI& ui);
};


/// <summary>
/// プリセット（別の .timeline）を、ずらして並べて再生するトラック（UIPopIn.timeline を、要素ごとに少しずつ遅らせて並べる、など）
/// <para>プリセットの中のトラックは、このトラックの対象を「再生する Entity」として動く。プリセットは、対象を空（自分）にして作ると何にでも使える</para>
/// <para>時間：クリップの始まりがプリセットの 0 秒。最初のクリップの前はプリセットの 0 秒、クリップの後はクリップの終わりの値のまま</para>
/// <para>イベントも呼ぶ。プリセットの中にプリセットを入れてよい（4 段まで。自分を入れたときは、そこで止まる）</para>
/// </summary>
class TimelinePresetTrack : public TimelineTrackOf<TimelinePresetClip> {
public:
	static constexpr int kMaxDepth = 4; // プリセットの中のプリセットの段の上限

	std::string GetClipLabel(size_t index) const override;
	bool CanPreview() const override { return true; }
	std::unique_ptr<TimelineTrackState> CreateState() const override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
};
```

### B-4 `Timeline/Tween/TimelinePresetTrack.cpp`（新しいファイル）

```cpp
#include "TimelinePresetTrack.h"

#include <vector>
#include <algorithm>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Timeline/TimelineManager.h"

namespace {
int depth = 0; // 今、何段目のプリセットの中を動かしているか（自分を入れたときに止めるため）

/// <summary>
/// Preset のトラックの、Entity ごとの値
/// </summary>
class TimelinePresetState : public TimelineTrackState {
public:
	std::vector<TimelineStates> clips; // [クリップの番号] プリセットのトラックの値
	std::vector<TimelineRange> ranges; // プリセットの時間に直した、このフレームに通った幅（毎回確保しないように使い回す）
};
} // namespace

//=============================================================================
// クリップ
//=============================================================================
void TimelinePresetClip::Fields(ComponentUI& ui) {
	TimelineClip::Fields(ui);
	ui.AssetField("Timeline###timeline", timeline, AssetType::Timeline, Tip("再生するプリセット（.timeline）。中のトラックは、このトラックの対象を「再生する Entity」として動く"));
}

//=============================================================================
// トラック
//=============================================================================
// ===== クリップの上の名前（ファイルの名前）=====
std::string TimelinePresetTrack::GetClipLabel(size_t index) const {
	const std::string& path = TimelineManager::GetPath(clips[index].timeline);
	return path.empty() ? std::string("(none)") : ToUtf8(ToPath(path).stem());
}

std::unique_ptr<TimelineTrackState> TimelinePresetTrack::CreateState() const { return std::make_unique<TimelinePresetState>(); }

// ===== 動かす =====
void TimelinePresetTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (clips.empty()) {
		return;
	}
	if (depth >= kMaxDepth) {
		WarnOnce(state, "プリセットの中のプリセットが深すぎます（プリセットが自分を入れていないか確かめてください）");
		return;
	}
	// --- 今のクリップ（最初のクリップの前は、最初のクリップの 0 秒）---
	const TimelinePresetClip* found = FindClip(frame.time);
	const TimelinePresetClip& clip = (found != nullptr) ? *found : clips.front();
	const Timeline* preset = TimelineManager::Get(clip.timeline);
	if (preset == nullptr) {
		return;
	}
	const Handle<Entity> entity = FindTarget(frame, state);
	if (!entity.IsValid()) {
		return;
	}
	// --- プリセットのトラックの値（プリセットのファイルを直したら作り直す）---
	auto& presetState = static_cast<TimelinePresetState&>(state);
	presetState.clips.resize(clips.size());
	TimelineStates& states = presetState.clips[static_cast<size_t>(&clip - clips.data())];
	if (states.Prepare(clip.timeline) && frame.isPreview) {
		// プレビューは警告を出さない（直すたびに作り直すのでログが埋まる。問題は Timeline ウィンドウの行に出す）
		for (const std::unique_ptr<TimelineTrackState>& track : states.tracks) {
			track->isWarned = true;
		}
	}
	// --- 通った幅を、プリセットの時間に直す（クリップの外の部分は切る。クリップの始まりをまたいだら 0 ちょうども通った）---
	presetState.ranges.clear();
	for (const TimelineRange& range : frame.ranges) {
		const float start = range.start - clip.start;
		const float end = range.end - clip.start;
		if (end < 0.0f || start > clip.duration) {
			continue;
		}
		presetState.ranges.push_back({std::clamp(start, 0.0f, clip.duration), std::clamp(end, 0.0f, clip.duration), range.includesStart || start < 0.0f});
	}
	// --- プリセットを、対象を再生する Entity にして動かす ---
	TimelineFrame local = frame;
	local.owner = entity;
	local.time = std::clamp(frame.time - clip.start, 0.0f, clip.duration);
	local.ranges = presetState.ranges;
	++depth;
	preset->Evaluate(local, states, true, false);
	--depth;
}
```

## パートC UI Screen

### C-1 `Components/UI/UIScreenComponent.h`

include に足して、頭のコメントの3行目を2行に差し替える。

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Time/EngineTime.h"

// 画面（ポーズ画面・クリア画面など）の出し方・消し方。resources/ui/〇〇.uiscreen の一番上の Entity に付ける（カテゴリ：UI）
// 動かすのは UIScreen（UI/UIScreen.h）。UIScreen::Open で出して、UIScreen::Close で消す
// 出し方・消し方は2通り：Timeline（一番上の Entity を再生する Entity にして再生する。要素ごとに好きに動かせる）か、
// Motion（ファイル無しで、一番上の Entity の直下の子を1つずつ同じ動きで動かす。Hierarchy の上から順に、Stagger の秒ずつ遅れて動く）
```

`UIScreenComponent` の `pauseGame` の下に3つ足す（ファイルに無い項目は初期値になるので、今の `.uiscreen` はそのまま読める）。

```cpp
	bool pauseGame = true;                                                                                  // 出している間、ゲームを止める（Time::SetGamePaused。中の Script は動く）
	TimeMode timeMode = TimeMode::Real;                                                                     // 出す・消す動きの時間（Pause Game を入れた画面は、いつも Real）
	uint32_t showTimeline = 0;                                                                              // 出すときに再生する .timeline（選べば Show の Motion の代わりに使う。0 なら Motion）
	uint32_t hideTimeline = 0;                                                                              // 消すときに再生する .timeline（選べば Hide の Motion の代わりに使う。0 なら Motion）
```

### C-2 `UI/UIScreen.h`

クラスの説明の4つ目の `<para>` を直す。

```cpp
/// <para>動きは一番上の Entity の UI Screen Component（出し方・消し方・ゲームを止めるか）。出し方・消し方は Timeline（TimelineLayer::UIScreen で再生）か Motion。時間は Time Mode（ゲームを止める画面は止めない時間）</para>
```

「ゲームから使う」の `Close` から `IsOpen` までを差し替える。

```cpp
	// name の画面を消す（消し方の動きが終わったら、Entity を消す）。出ていなければ false
	// 出している途中に消しても、その時の形から消える（Timeline は From を Current にしたトラックで、Motion は閉じた瞬間の形から）
	static bool Close(const std::string& name);
	// entity が入っている画面を消す（画面の一番上の Entity も含む）。入っていなければ false
	static bool CloseContaining(Handle<Entity> entity);
	// name の画面が出ているか（出している途中も true。消している途中は false）
	static bool IsOpen(const std::string& name);
	// name の画面の一番上の Entity（出ていなければ無効な Handle）。TimelineSystem::SetParameter で、出し方の値を変えるのに使う
	static Handle<Entity> GetRoot(const std::string& name);
```

### C-3 `UI/UIScreen.cpp`（全体を差し替え）

変わった所：要素の形を `UIScreenPose` にまとめ、`closePose` を足した。`BeginShow` で Timeline を頼む・`BeginHide` を足した・`UpdateScreen` に分けた・`UpdatePause` で Waiting を数える・Inspector に Time Mode と Timeline の欄・`CloseContaining`・`GetRoot`。

```cpp
#include "UIScreen.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Components/UI/AlphaGroupComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/UI/UIScreenComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentStorage.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Scene/PrefabManager.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/Scene/SceneSerializer.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"
#include "HTTEngine/Time/EngineTime.h"

namespace {
/// <summary>
/// 開いた画面が今どうしているか
/// </summary>
enum class UIScreenPhase {
	Waiting, // Open した。Component が付くのを待っている（次のフレームの頭の Flush で Showing へ）
	Showing, // 出している途中
	Shown,   // 出きった
	Hiding,  // 消している途中（終わったら Entity を消す）
};

/// <summary>
/// 要素の形（Motion が動かす3つの値）
/// </summary>
struct UIScreenPose {
	Vector3 translation;
	Vector3 scale;
	float alpha = 1.0f; // Alpha Group の Alpha
};

/// <summary>
/// 動く要素1つ（画面の一番上の Entity の直下の子）
/// </summary>
struct UIScreenElement {
	Handle<Entity> entity;
	UIScreenPose basePose;  // 出きったときの形（ファイルに書いてある形。出すときの基準）
	UIScreenPose closePose; // Close したときの形（消すときの基準。出している途中に閉じても、その形から消える）
	UIScreenMotion show;    // 出し方（UI Screen Element で変えていれば、そちら）
	UIScreenMotion hide;    // 消し方
	float showDelay = 0.0f; // 出し始めるまでの秒（Stagger × 何番目か ＋ UI Screen Element の Delay）
	float hideDelay = 0.0f; // 消し始めるまでの秒
};

/// <summary>
/// 開いている画面1つ
/// </summary>
struct OpenUIScreen {
	std::string name;
	Handle<Entity> root; // 一番上の Entity（UI Screen Component が付いている）
	UIScreenPhase phase = UIScreenPhase::Waiting;
	float time = 0.0f;                     // 出し始めて・消し始めてからの秒
	bool pauseGame = false;                // 出している間、ゲームを止めるか
	TimeMode timeMode = TimeMode::Real;    // 出す・消す動きの時間
	uint32_t showTimeline = 0;             // 出すときの Timeline（0 なら Motion）
	uint32_t hideTimeline = 0;             // 消すときの Timeline（0 なら Motion）
	std::vector<UIScreenElement> elements; // 動く要素（Hierarchy の上から順）
	float showEnd = 0.0f;                  // 全部の要素が出きる秒（Motion）
	float hideEnd = 0.0f;                  // 全部の要素が消えきる秒（Motion）
};

/// <summary>
/// UIScreen が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct UIScreenState {
	std::vector<OpenUIScreen> screens;
	bool isPausing = false; // 画面がゲームを止めているか（止めた・動かしたを1回だけ Time に書く）
};
std::unique_ptr<UIScreenState> state;

//=============================================================================
// 小さな道具
//=============================================================================
// ===== 名前から開いている画面（無ければ end）=====
std::vector<OpenUIScreen>::iterator FindScreen(const std::string& name) { return std::ranges::find(state->screens, name, &OpenUIScreen::name); }

// ===== 滑り込む向き（画面のどちら側か → ピクセルの向き。Y は上が＋）=====
Vector3 SideDirection(UIScreenSide side) {
	switch (side) {
	case UIScreenSide::Left:
		return {-1.0f, 0.0f, 0.0f};
	case UIScreenSide::Right:
		return {1.0f, 0.0f, 0.0f};
	case UIScreenSide::Top:
		return {0.0f, 1.0f, 0.0f};
	case UIScreenSide::Bottom:
		return {0.0f, -1.0f, 0.0f};
	}
	return {};
}

// ===== どれだけ見えているか（0：出す前・消えきった ～ 1：出きった）。OutBack などは途中で 1 を少し超える =====
float CalcVisible(const UIScreenMotion& motion, float time, float delay, bool isShowing) {
	float t = 1.0f;
	if (motion.duration > 0.0f) {
		t = std::clamp((time - delay) / motion.duration, 0.0f, 1.0f);
	} else if (time < delay) {
		t = 0.0f; // 0 秒なら、遅れの分を待ってから一度に変える
	}
	const float eased = Ease::Apply(t, motion.ease);
	return isShowing ? eased : 1.0f - eased;
}

// ===== 要素の今の形（Transform か Alpha Group が無ければ false）=====
bool ReadPose(Handle<Entity> entity, UIScreenPose& out) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(entity);
	if (transform == nullptr || group == nullptr) {
		return false;
	}
	out = {transform->translation, transform->scale, group->enabled ? group->alpha : 1.0f}; // 切ってあった Alpha Group は 1 と同じ
	return true;
}

// ===== 要素1つに、pose を基準にして、見えている量 visible の形を当てる（Translation・Scale・Alpha Group の Alpha）=====
void ApplyElement(const UIScreenElement& element, const UIScreenPose& pose, const UIScreenMotion& motion, float visible) {
	TransformComponent* transform = EntityManager::Get<TransformComponent>(element.entity);
	AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(element.entity);
	if (transform == nullptr || group == nullptr) {
		return;
	}
	Vector3 translation = pose.translation;
	Vector3 scale = pose.scale;
	float alpha = pose.alpha;
	switch (motion.type) {
	case UIScreenMotionType::None:
		break; // 動かさない
	case UIScreenMotionType::Fade:
		alpha *= visible;
		break;
	case UIScreenMotionType::Slide:
		// 見えていない分だけ、画面の外の方へずらす（visible が 1 を超えると、少し行き過ぎて戻る）
		translation += SideDirection(motion.side) * (motion.distance * (1.0f - visible));
		alpha *= motion.withFade ? visible : 1.0f;
		break;
	case UIScreenMotionType::Scale:
		scale *= Ease::Lerp(motion.fromScale, 1.0f, visible);
		alpha *= motion.withFade ? visible : 1.0f;
		break;
	}
	transform->translation = translation;
	transform->scale = scale;
	group->enabled = true;
	group->alpha = std::clamp(alpha, 0.0f, 1.0f);
}

// ===== 画面の全部の要素に、今の時間の形を当てる（Motion）=====
void ApplyScreen(const OpenUIScreen& screen) {
	const bool isShowing = (screen.phase != UIScreenPhase::Hiding);
	for (const UIScreenElement& element : screen.elements) {
		const UIScreenMotion& motion = isShowing ? element.show : element.hide;
		const float delay = isShowing ? element.showDelay : element.hideDelay;
		ApplyElement(element, isShowing ? element.basePose : element.closePose, motion, CalcVisible(motion, screen.time, delay, isShowing));
	}
}

// ===== 出す・消す Timeline を再生する =====
void PlayScreenTimeline(const OpenUIScreen& screen, uint32_t timeline) {
	TimelineSystem::PlayLayer(screen.root, TimelineLayer::UIScreen, {.timeline = timeline, .timeMode = screen.timeMode});
}

// ===== 出す・消す Timeline が終わったか（再生していない・ファイルが無いときも終わった扱い）=====
bool IsScreenTimelineFinished(const OpenUIScreen& screen) {
	const TimelineStatus status = TimelineSystem::GetStatus(screen.root, TimelineLayer::UIScreen);
	return !status.isValid || status.state == TimelinePlayState::Finished || status.state == TimelinePlayState::Stopped;
}

// ===== Component が付いた画面の要素を集めて、出し始める（フレームの頭で呼ぶ。Alpha Group をその場で付けるので）=====
void BeginShow(OpenUIScreen& screen) {
	// --- 出し方・消し方（UI Screen Component が無ければ、動かさずに出す）---
	UIScreenComponent settings;
	if (const UIScreenComponent* component = EntityManager::Get<UIScreenComponent>(screen.root)) {
		settings = *component;
	} else {
		LogManager::Warning(std::format("画面 \"{}\" の一番上の Entity に UI Screen Component がありません（動かさずに出します）", screen.name));
		settings.pauseGame = false;
		settings.show.type = UIScreenMotionType::None;
		settings.hide.type = UIScreenMotionType::None;
	}
	screen.pauseGame = settings.pauseGame;
	screen.timeMode = settings.pauseGame ? TimeMode::Real : settings.timeMode; // ゲームを止める画面が、自分の時間で止まらないように
	screen.showTimeline = settings.showTimeline;
	screen.hideTimeline = settings.hideTimeline;

	// --- 要素（直下の子。Hierarchy の上から順）---
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(screen.root, children);
	screen.elements.clear();
	for (size_t i = 0; i < children.size(); ++i) {
		if (EntityManager::Get<TransformComponent>(children[i]) == nullptr) {
			continue;
		}
		// フェードは Alpha Group で掛ける。無ければ今付ける（フレームの頭なので、予約せずにその場で付けてよい）
		if (EntityManager::Get<AlphaGroupComponent>(children[i]) == nullptr) {
			EntityManager::RestoreComponents(children[i], {ComponentSnapshot::Make(AlphaGroupComponent{})});
		}
		UIScreenElement element;
		element.entity = children[i];
		ReadPose(children[i], element.basePose);
		element.closePose = element.basePose;
		element.show = settings.show;
		element.hide = settings.hide;
		float extraDelay = 0.0f;
		if (const UIScreenElementComponent* own = EntityManager::Get<UIScreenElementComponent>(children[i])) {
			element.show = own->overrideShow ? own->show : element.show;
			element.hide = own->overrideHide ? own->hide : element.hide;
			extraDelay = own->delay;
		}
		element.showDelay = settings.showStagger * static_cast<float>(i) + extraDelay;
		element.hideDelay = settings.hideStagger * static_cast<float>(i) + extraDelay;
		screen.showEnd = (std::max)(screen.showEnd, element.showDelay + element.show.duration);
		screen.hideEnd = (std::max)(screen.hideEnd, element.hideDelay + element.hide.duration);
		screen.elements.push_back(element);
	}
	screen.phase = UIScreenPhase::Showing;
	screen.time = 0.0f;
	// --- 描く前に、出し始めの形にしておく（Timeline はこのフレームの TimelineSystem::Update が最初の値を書く）---
	if (screen.showTimeline != 0) {
		PlayScreenTimeline(screen, screen.showTimeline);
	} else {
		ApplyScreen(screen);
	}
}

// ===== 消し始める =====
void BeginHide(OpenUIScreen& screen) {
	screen.phase = UIScreenPhase::Hiding;
	screen.time = 0.0f;
	if (screen.hideTimeline != 0) {
		PlayScreenTimeline(screen, screen.hideTimeline); // 出す Timeline と同じ層なので、出す方は置き換わって止まる
		return;
	}
	// --- Motion：今の形から消す（出す Timeline が書くのもやめる）---
	TimelineSystem::StopLayer(screen.root, TimelineLayer::UIScreen);
	for (UIScreenElement& element : screen.elements) {
		ReadPose(element.entity, element.closePose);
	}
}

// ===== 画面がゲームを止めるか =====
// 止める画面が1つでも出ていれば止める。消している途中の画面は数えない。
// Component が付くのを待っている画面は、止めているかが分からないので今のまま（Switch Screen で消して出す間に、1フレームだけ動き出さないように）
void UpdatePause() {
	const bool wantsPause = std::ranges::any_of(state->screens, [](const OpenUIScreen& screen) {
		switch (screen.phase) {
		case UIScreenPhase::Waiting:
			return state->isPausing;
		case UIScreenPhase::Showing:
		case UIScreenPhase::Shown:
			return screen.pauseGame;
		case UIScreenPhase::Hiding:
			return false;
		}
		return false;
	});
	if (wantsPause != state->isPausing) {
		state->isPausing = wantsPause;
		Time::SetGamePaused(wantsPause);
	}
}

// ===== 画面1つを1フレーム進める（もう忘れてよければ true）=====
bool UpdateScreen(OpenUIScreen& screen, float deltaTime) {
	switch (screen.phase) {
	case UIScreenPhase::Waiting:
	case UIScreenPhase::Shown:
		return false;
	case UIScreenPhase::Showing:
		screen.time += deltaTime;
		if (screen.showTimeline != 0) {
			screen.phase = IsScreenTimelineFinished(screen) ? UIScreenPhase::Shown : screen.phase;
			return false;
		}
		ApplyScreen(screen);
		if (screen.time >= screen.showEnd) {
			screen.phase = UIScreenPhase::Shown;
		}
		return false;
	case UIScreenPhase::Hiding: {
		screen.time += deltaTime;
		bool isFinished = false;
		if (screen.hideTimeline != 0) {
			isFinished = IsScreenTimelineFinished(screen);
		} else {
			ApplyScreen(screen);
			isFinished = screen.time >= screen.hideEnd;
		}
		if (isFinished) {
			EntityManager::Destroy(screen.root); // 子も一緒に消える（Timeline の再生も、Entity と一緒に消える）
		}
		return isFinished;
	}
	}
	return false;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// ===== 出し方・消し方1つ分（id は保存の名前の頭。"show" → "showMotion"・"showDuration" …）=====
// 選んだ動きの値だけ見せる（保存も、使っている方だけが書かれる）
void DescribeMotion(ComponentUI& ui, UIScreenMotion& motion, const char* id) {
	ui.Field(std::format("Motion###{}Motion", id).c_str(), motion.type, Tip("None：動かない / Fade：だんだん見える・消える / Slide：画面の外の方から滑り込む / Scale：小さい所から大きくなる"));
	switch (motion.type) {
	case UIScreenMotionType::None:
		return; // 秒数もイージングも使わない
	case UIScreenMotionType::Fade:
		break;
	case UIScreenMotionType::Slide:
		ui.Field(std::format("Side###{}Side", id).c_str(), motion.side, Tip("出すときは、どちら側から来るか。消すときは、どちら側へ行くか"));
		ui.Field(std::format("Distance###{}Distance", id).c_str(), motion.distance, AtLeast(0.0f).Tip("動く長さ（ピクセル。ゲームの画面のつもりで。画面の外から入れるなら、画面の幅か高さの値）"));
		ui.Field(std::format("With Fade###{}WithFade", id).c_str(), motion.withFade, Tip("動きながらフェードもする"));
		break;
	case UIScreenMotionType::Scale:
		ui.Field(std::format("From Scale###{}FromScale", id).c_str(), motion.fromScale, AtLeast(0.0f).Tip("一番小さいときの大きさ（元の大きさが 1）"));
		ui.Field(std::format("With Fade###{}WithFade", id).c_str(), motion.withFade, Tip("大きさを変えながらフェードもする"));
		break;
	}
	ui.Field(std::format("Duration###{}Duration", id).c_str(), motion.duration, AtLeast(0.0f).Tip("1つの要素が動ききるまでの秒数（ゲームを止めていても進む）"));
	ui.Field(
	    std::format("Ease###{}Ease", id).c_str(), motion.ease,
	    Tip("動きの速さの変え方。出すときは Out〜（速く入って、ゆっくり止まる）、消すときは In〜（ゆっくり動き出して、速く抜ける）が自然。OutBack は少し行き過ぎて戻る"));
}

void DescribeUIScreen(ComponentUI& ui, UIScreenComponent& value) {
	ui.Label("画面のファイルの一番上の Entity に付ける。Timeline を選ぶとそれを再生し、選ばなければ直下の子が上から順に1つずつ Motion で動く");
	ui.Field("Pause Game###pauseGame", value.pauseGame, Tip("出している間、ゲームを止める（時間が0・Script を呼ばない）。この画面の中の Script は動く"));
	ui.BeginDisabled(value.pauseGame);
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("出す・消す動きの時間。Real：ゲームが止まっていても動く / Game：ポーズ・スローで止まる（Pause Game を入れた画面は、いつも Real）"));
	ui.EndDisabled();
	ui.Separator("Show");
	ui.AssetField("Timeline###showTimeline", value.showTimeline, AssetType::Timeline, Tip("出すときに再生する Timeline（この Entity を再生する Entity にする）。選ぶと下の Motion は使わない"));
	ui.BeginDisabled(value.showTimeline != 0);
	DescribeMotion(ui, value.show, "show");
	ui.Field("Stagger###showStagger", value.showStagger, AtLeast(0.0f).Tip("要素1つごとに遅らせる秒数（上から2つ目はこの秒、3つ目は2倍遅れて動き出す）"));
	ui.EndDisabled();
	ui.Separator("Hide");
	ui.AssetField("Timeline###hideTimeline", value.hideTimeline, AssetType::Timeline, Tip("消すときに再生する Timeline。出している途中で消しても値が飛ばないように、Tween の From は Current にする。選ぶと下の Motion は使わない"));
	ui.BeginDisabled(value.hideTimeline != 0);
	DescribeMotion(ui, value.hide, "hide");
	ui.Field("Stagger###hideStagger", value.hideStagger, AtLeast(0.0f).Tip("要素1つごとに遅らせる秒数"));
	ui.EndDisabled();
}

void DescribeUIScreenElement(ComponentUI& ui, UIScreenElementComponent& value) {
	ui.Label("画面の一番上の Entity の直下の子に付ける。この要素だけ動き方を変える（Motion で出す・消すときだけ使う）");
	ui.Field("Override Show###overrideShow", value.overrideShow, Tip("出し方を、ここで決めた物にする"));
	if (value.overrideShow) {
		DescribeMotion(ui, value.show, "show");
	}
	ui.Field("Override Hide###overrideHide", value.overrideHide, Tip("消し方を、ここで決めた物にする"));
	if (value.overrideHide) {
		DescribeMotion(ui, value.hide, "hide");
	}
	ui.Field("Delay###delay", value.delay, AtLeast(0.0f).Tip("Stagger の遅れに足す秒数（出す・消すの両方）"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIScreen::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<UIScreenState>();
	// UI Screen・UI Screen Element の置き場所・保存・Inspector を登録する（見せ方は Describe〜 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<UIScreenComponent>();
	ComponentSerializer::Register<UIScreenComponent>("UI Screen", &DescribeUIScreen);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIScreenComponent>>("UI Screen", "UI", &DescribeUIScreen));
	EntityManager::RegisterComponent<UIScreenElementComponent>();
	ComponentSerializer::Register<UIScreenElementComponent>("UI Screen Element", &DescribeUIScreenElement);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIScreenElementComponent>>("UI Screen Element", "UI", &DescribeUIScreenElement));
}

void UIScreen::Release() { state.reset(); }

//=============================================================================
// ゲームから使う
//=============================================================================
bool UIScreen::Open(const std::string& name) {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	// 停止中はゲームのコードが動かないので、ふつうは来ない（エディタで開いているシーンに、勝手に Entity を足さないように）
	const SceneManager* main = SceneManager::GetMain();
	if (main == nullptr || main->GetPlayState() == PlayState::Editing) {
		LogManager::Log(std::format("UIScreen::Open(\"{}\")：停止中なので出しません（Play 中だけ効きます）", name));
		return false;
	}
	// もう出ている・消している途中なら出さない（Esc を続けて押したときに、2枚重ならないように）
	if (FindScreen(name) != state->screens.end()) {
		return false;
	}
	// 同じファイルは1回だけ読む（2回目からは同じ番号。エディタで保存し直すと、中身も差し替わる）
	const uint32_t prefab = PrefabManager::Load(ToPath(MakeUIScreenFilePath(name)));
	if (prefab == 0) {
		LogManager::Warning(std::format("UIScreen::Open(\"{}\")：{} が読めません", name, MakeUIScreenFilePath(name)));
		return false;
	}
	OpenUIScreen screen;
	screen.name = name;
	screen.root = PrefabManager::Instantiate(prefab, {0.0f, 0.0f, 0.0f}); // 一番上の Entity は原点（UI の位置は子に書く）
	state->screens.push_back(std::move(screen));
	return true;
}

bool UIScreen::Close(const std::string& name) {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = FindScreen(name);
	if (it == state->screens.end() || it->phase == UIScreenPhase::Hiding) {
		return false;
	}
	// まだ Component が付いていない（Open と同じフレームに Close した）なら、動かさずに消す
	if (it->phase == UIScreenPhase::Waiting) {
		EntityManager::Destroy(it->root);
		state->screens.erase(it);
		UpdatePause();
		return true;
	}
	BeginHide(*it);
	UpdatePause(); // 消し始めたら、すぐゲームを動かす（消える動きを待たずに遊べるように）
	return true;
}

bool UIScreen::CloseContaining(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = std::ranges::find_if(state->screens, [entity](const OpenUIScreen& screen) { return entity == screen.root || EntityManager::IsDescendantOf(entity, screen.root); });
	return it != state->screens.end() && Close(std::string(it->name)); // Close が screens を書き換えるので、名前は写して渡す
}

bool UIScreen::IsOpen(const std::string& name) {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = FindScreen(name);
	return it != state->screens.end() && it->phase != UIScreenPhase::Hiding;
}

Handle<Entity> UIScreen::GetRoot(const std::string& name) {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = FindScreen(name);
	return (it != state->screens.end()) ? it->root : Handle<Entity>{};
}

//=============================================================================
// エンジンから使う
//=============================================================================
bool UIScreen::IsInsideOpenScreen(Handle<Entity> entity) {
	return std::ranges::any_of(state->screens, [entity](const OpenUIScreen& screen) { return entity == screen.root || EntityManager::IsDescendantOf(entity, screen.root); });
}

void UIScreen::Flush() {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	for (OpenUIScreen& screen : state->screens) {
		// Open した次のフレームの頭：PrefabManager::Flush で Component が付いたので、出し始める
		if (screen.phase == UIScreenPhase::Waiting && EntityManager::IsAlive(screen.root)) {
			BeginShow(screen);
		}
	}
	UpdatePause();
}

void UIScreen::Update(bool isGameRunning) {
	HTT_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	// 停止中：全部忘れる（Stop でシーンが Play を押した瞬間に戻るので、画面の Entity も無くなる）
	if (!isGameRunning) {
		state->screens.clear();
		state->isPausing = false;
		return;
	}
	// エディタの一時停止中は、画面の動きも止める
	const SceneManager* main = SceneManager::GetMain();
	if (main != nullptr && main->GetPlayState() == PlayState::Paused) {
		return;
	}
	// erase_if：条件に合う物を配列から全部消す（C++20）。true を返した画面は、もう忘れる
	std::erase_if(state->screens, [](OpenUIScreen& screen) {
		// シーンが変わった・誰かが Destroy した
		if (!EntityManager::IsAlive(screen.root)) {
			return true;
		}
		return UpdateScreen(screen, Time::GetDeltaTime(screen.timeMode));
	});
	UpdatePause();
}

//=============================================================================
// エディタから使う
//=============================================================================
bool UIScreen::CreateNewFile(const std::filesystem::path& path) {
	// 一番上の Entity 1つ（名前はファイルの名前、UI Screen Component だけ）。書いていない値は、読むときに初期値になる
	SceneJson item = SceneJson::object();
	item["id"] = EntityId{1};
	item["name"] = ToUtf8(path.stem());
	item["parent"] = EntityId{0};
	item["active"] = true;
	item["components"] = {
	    {"UI Screen", SceneJson::object()}
    };
	SceneJson root = SceneJson::object();
	root["version"] = SceneSerializer::kVersion;
	root["entities"] = SceneJson::array({item});
	if (!SceneSerializer::WriteFile(ToUtf8(path), root)) {
		return false;
	}
	LogManager::Log(std::format("画面を作りました: {}", ToUtf8(path)));
	return true;
}
```

## パートD UI State

### D-1 `Components/UI/UIStateComponent.h`（新しいファイル）

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/String/FixedString.h"
#include "HTTEngine/Time/EngineTime.h"

// UI の要素の状態（マウスが乗った・押した・選んだ・使えない・出す・消す）で、Timeline を再生する。Entityに付けるComponent（カテゴリ：UI）
// 動かすのは UIStateSystem（Systems/UI/UIStateSystem.h）。マウスの当たりは、同じ Entity の Vector Shape か Image の形

/// <summary>
/// 要素の状態（強い順：Disabled ＞ Pressed ＞ Hover ＞ Selected ＞ Normal。Unity の Selectable と同じ）
/// </summary>
enum class UIElementState {
	Normal,   // 何もしていない
	Hover,    // マウスが乗っている
	Pressed,  // 上で押している
	Selected, // コードで選んだ（UIStateSystem::SetSelected。キーで選ぶメニューなど）
	Disabled, // 使えない（Interactable を外した）
};

inline constexpr size_t kUIElementStateCount = magic_enum::enum_count<UIElementState>(); // UIElementState の数

/// <summary>
/// クリックしたときにすること（コードを書かずに、画面を出す・消す）
/// </summary>
enum class UIClickAction {
	None,           // 何もしない（Script で UIStateSystem::IsClicked を見る）
	OpenScreen,     // Screen の名前の画面を出す
	CloseScreen,    // Screen の名前の画面を消す
	CloseOwnScreen, // この要素が入っている画面を消す
	SwitchScreen,   // この要素が入っている画面を消して、Screen の名前の画面を出す（2つの動きは同時に進む）
};

/// <summary>
/// 要素の状態で Timeline を再生する（Timeline の対象を空にすると、この Entity 自身を動かす）
/// <para>状態の Timeline を選んでいなければ、Normal の Timeline を再生する（元の形へ戻すため）</para>
/// </summary>
struct UIStateComponent {
	bool interactable = true;                                       // 外している間は Disabled（マウスに反応しない）
	bool useMouse = true;                                           // マウスで Hover・Pressed になる・クリックできる
	bool startVisible = true;                                       // 最初から見えている（外すと、最初に Hide の最後の形にする）
	TimeMode timeMode = TimeMode::Real;                             // 再生に使う時間（Real：ポーズ中も動く / Game：ポーズ・スローで止まる）
	std::array<uint32_t, kUIElementStateCount> stateTimelines = {}; // [UIElementState] その状態になったときに再生する .timeline
	uint32_t showTimeline = 0;                                      // UIStateSystem::SetVisible(true) で再生する .timeline
	uint32_t hideTimeline = 0;                                      // UIStateSystem::SetVisible(false) で再生する .timeline
	UIClickAction clickAction = UIClickAction::None;                // クリックしたときにすること
	FixedString<64> screen;                                         // Open Screen・Close Screen・Switch Screen の画面の名前（resources/ui/〇〇.uiscreen の 〇〇）
};

static_assert(std::is_trivially_copyable_v<UIStateComponent>);
```

### D-2 `Systems/UI/UIStateSystem.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Components/UI/UIStateComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"


/// <summary>
/// UI State（要素の状態で Timeline を再生する）を動かす
/// <para>毎フレーム：マウスから Hover・Pressed を決めて、状態が変わったら、その状態の Timeline を再生する（TimelineLayer::UIState。前の状態の再生は置き換わる）</para>
/// <para>Show・Hide は別の層（TimelineLayer::UIVisibility）で、状態の再生より後に書く。消えている間はマウスに反応しない</para>
/// <para>最初のフレームは、最初の状態の Timeline の最後の形をすぐ当てる（動かさない）。Start Visible を外していれば Hide の最後の形</para>
/// <para>ゲームが止まっている間（ポーズ中）は、開いている画面（UI Screen）の中の要素だけがマウスに反応する</para>
/// </summary>
class UIStateSystem {
public:
	// UI State を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 毎フレーム（Script の後、TimelineSystem::Update の前。頼んだ再生を、そのフレームの値に使う）。停止中は全部忘れる
	static void Update(bool isGameRunning);

	// ===== ゲームから使う（UI State が無い Entity なら何もしない・初期値を返す）=====
	// 今の状態
	static UIElementState GetState(Handle<Entity> entity);
	// クリックされた（上で押して、上で離した）。決まるのは Script の後なので、Script から見えるのは次のフレームの1回だけ
	static bool IsClicked(Handle<Entity> entity);
	// 選んでいる印を付ける・外す（マウスが乗っていなければ Selected になる）
	static void SetSelected(Handle<Entity> entity, bool isSelected);
	// 出す・消す（Show・Hide の Timeline を再生する。今と同じなら何もしない）
	static void SetVisible(Handle<Entity> entity, bool isVisible);
	static bool IsVisible(Handle<Entity> entity);

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};
```

### D-3 `Systems/UI/UIStateSystem.cpp`（新しいファイル）

```cpp
#include "UIStateSystem.h"

#include <array>
#include <format>
#include <memory>
#include <vector>
#include <algorithm>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/UI/UIScreen.h"

namespace {
constexpr int kLeftButton = 0; // マウスの左ボタン

/// <summary>
/// 状態の Timeline の欄1つ（Inspector の名前・保存の名前・説明）
/// </summary>
struct StateField {
	const char* label;
	const char* key;
	const char* tooltip;
};
constexpr std::array<StateField, kUIElementStateCount> kStateFields = {{
    {"Normal", "normal", "何もしていないときに再生する（ほかの状態から戻るときにも使うので、From を Current にして元の形へ戻す物にする）"},
    {"Hover", "hover", "マウスが乗ったときに再生する（選んでいなければ Normal）"},
    {"Pressed", "pressed", "上で押したときに再生する（選んでいなければ Normal）"},
    {"Selected", "selected", "UIStateSystem::SetSelected で選んだときに再生する（選んでいなければ Normal）"},
    {"Disabled", "disabled", "Interactable を外したときに再生する（選んでいなければ Normal）"},
}};

/// <summary>
/// 要素1つの、実行中の値
/// </summary>
struct UIStateRuntime {
	Handle<Entity> entity;
	UIElementState state = UIElementState::Normal; // 今の状態（最後に再生した状態）
	bool isStarted = false;                        // 最初の状態を当てた
	bool isSelected = false;                       // SetSelected で選んでいる
	bool isPressing = false;                       // 上で押して、まだ離していない
	bool isClicked = false;                        // この前の Update でクリックされた
	bool isVisible = true;                         // 見えている（Hide を再生した後は false）
	bool needsVisibility = false;                  // 次の Update で Show・Hide を再生する
};

/// <summary>
/// UIStateSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct UIStateState {
	std::vector<UIStateRuntime> runtimes; // 動かしている要素（数が少ないので、一列に並べて順に探す）
};
std::unique_ptr<UIStateState> state;

//=============================================================================
// 小さな道具
//=============================================================================
// ===== 実行中の値（無ければ nullptr）=====
UIStateRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(state->runtimes, entity, &UIStateRuntime::entity);
	return (found != state->runtimes.end()) ? &*found : nullptr;
}

// ===== 実行中の値を用意する（UI State が無ければ nullptr）=====
UIStateRuntime* RequireRuntime(Handle<Entity> entity) {
	const UIStateComponent* component = EntityManager::Get<UIStateComponent>(entity);
	if (component == nullptr) {
		return nullptr;
	}
	if (UIStateRuntime* runtime = FindRuntime(entity)) {
		return runtime;
	}
	state->runtimes.push_back({.entity = entity, .isVisible = component->startVisible});
	return &state->runtimes.back();
}

// ===== マウスが要素の形の上にあるか（Vector Shape か Image）=====
bool IsMouseOver(Handle<Entity> entity) { return VectorShapeSystem::IsMouseOver(entity) || SpriteRenderSystem::IsMouseOver(entity); }

// ===== マウスに反応してよいか（ポーズ中は、開いている画面の中だけ）=====
bool CanReceiveInput(Handle<Entity> entity) { return !Time::IsGamePaused() || UIScreen::IsInsideOpenScreen(entity); }

// ===== 今の状態を決める（強い順）=====
UIElementState DecideState(const UIStateComponent& component, const UIStateRuntime& runtime, bool isOver) {
	if (!component.interactable) {
		return UIElementState::Disabled;
	}
	if (runtime.isPressing && isOver) {
		return UIElementState::Pressed;
	}
	if (isOver) {
		return UIElementState::Hover;
	}
	return runtime.isSelected ? UIElementState::Selected : UIElementState::Normal;
}

// ===== 状態の Timeline を再生する（選んでいなければ Normal。Normal も無ければ、前の状態の再生をやめる）=====
void PlayState(Handle<Entity> entity, const UIStateComponent& component, UIElementState elementState, bool jumpToEnd) {
	uint32_t timeline = component.stateTimelines[static_cast<size_t>(elementState)];
	if (timeline == 0) {
		timeline = component.stateTimelines[static_cast<size_t>(UIElementState::Normal)];
	}
	if (timeline == 0) {
		TimelineSystem::StopLayer(entity, TimelineLayer::UIState);
		return;
	}
	TimelineSystem::PlayLayer(entity, TimelineLayer::UIState, {.timeline = timeline, .timeMode = component.timeMode, .jumpToEnd = jumpToEnd});
}

// ===== Show・Hide の Timeline を再生する（選んでいなければ、前の再生をやめるだけ）=====
void PlayVisibility(Handle<Entity> entity, const UIStateComponent& component, bool isVisible, bool jumpToEnd) {
	const uint32_t timeline = isVisible ? component.showTimeline : component.hideTimeline;
	if (timeline == 0) {
		TimelineSystem::StopLayer(entity, TimelineLayer::UIVisibility);
		return;
	}
	TimelineSystem::PlayLayer(entity, TimelineLayer::UIVisibility, {.timeline = timeline, .timeMode = component.timeMode, .jumpToEnd = jumpToEnd});
}

// ===== クリックしたときにすること =====
void RunClickAction(Handle<Entity> entity, const UIStateComponent& component) {
	const std::string screen = component.screen.ToString();
	switch (component.clickAction) {
	case UIClickAction::None:
		break;
	case UIClickAction::OpenScreen:
		UIScreen::Open(screen);
		break;
	case UIClickAction::CloseScreen:
		UIScreen::Close(screen);
		break;
	case UIClickAction::CloseOwnScreen:
		UIScreen::CloseContaining(entity);
		break;
	case UIClickAction::SwitchScreen:
		// 出す方を先に頼む（先に消すと、ゲームを止める画面が1つも無いフレームができて、1フレームだけゲームが動く）
		UIScreen::Open(screen);
		UIScreen::CloseContaining(entity);
		break;
	}
}

// ===== 要素1つを1フレーム進める =====
void UpdateEntity(Handle<Entity> entity, const UIStateComponent& component) {
	UIStateRuntime& runtime = *RequireRuntime(entity);
	runtime.isClicked = false;
	if (!EntityManager::IsActiveInHierarchy(entity)) {
		runtime.isPressing = false;
		return; // 無効の間は状態を変えない（有効に戻ったら、その時の状態の Timeline を再生する）
	}
	// --- マウス（上で押して、上で離したらクリック）---
	// 最初のフレームは見ない（まだワールド行列が計算されていないので、形がどこにあるか分からない）
	const bool canInput = runtime.isStarted && runtime.isVisible && component.interactable && component.useMouse && CanReceiveInput(entity);
	const bool isOver = canInput && IsMouseOver(entity);
	if (isOver && InputManager::IsMouseButtonTriggered(kLeftButton)) {
		runtime.isPressing = true;
	}
	if (runtime.isPressing && !InputManager::IsMouseButtonPressed(kLeftButton)) {
		runtime.isPressing = false;
		runtime.isClicked = isOver;
	}
	if (!canInput) {
		runtime.isPressing = false;
	}
	// --- 状態が変わったら、その状態の Timeline を再生する（最初のフレームは、動かさずに最後の形を当てる）---
	const UIElementState next = DecideState(component, runtime, isOver);
	if (!runtime.isStarted) {
		runtime.isStarted = true;
		PlayState(entity, component, next, true);
		if (!runtime.isVisible) {
			PlayVisibility(entity, component, false, true);
		}
		runtime.needsVisibility = false;
	} else if (next != runtime.state) {
		PlayState(entity, component, next, false);
	}
	runtime.state = next;
	// --- 出す・消す（SetVisible で頼まれた）---
	if (runtime.needsVisibility) {
		runtime.needsVisibility = false;
		PlayVisibility(entity, component, runtime.isVisible, false);
	}
	if (runtime.isClicked) {
		RunClickAction(entity, component);
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeUIState(ComponentUI& ui, UIStateComponent& value) {
	ui.Label("状態が変わったら、その状態の Timeline を再生する（Timeline の対象を空にすると、この Entity を動かす）。マウスの当たりは、この Entity の Vector Shape か Image の形");
	ui.Field("Interactable###interactable", value.interactable, Tip("外すと Disabled（マウスに反応しない）"));
	ui.Field("Use Mouse###useMouse", value.useMouse, Tip("マウスで Hover・Pressed になる・クリックできる"));
	ui.Field("Start Visible###startVisible", value.startVisible, Tip("最初から見えている。外すと、最初に Hide の最後の形にする（UIStateSystem::SetVisible(true) で出す）"));
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("Real：ゲームが止まっていても動く / Game：ポーズ・スロー・ヒットストップで止まる"));
	ui.Separator("States");
	for (size_t i = 0; i < kStateFields.size(); ++i) {
		const StateField& field = kStateFields[i];
		ui.AssetField(std::format("{}###{}", field.label, field.key).c_str(), value.stateTimelines[i], AssetType::Timeline, Tip(field.tooltip));
	}
	ui.Separator("Show / Hide");
	ui.AssetField("Show###show", value.showTimeline, AssetType::Timeline, Tip("UIStateSystem::SetVisible(true) で再生する"));
	ui.AssetField("Hide###hide", value.hideTimeline, AssetType::Timeline, Tip("UIStateSystem::SetVisible(false) で再生する。消えている間はマウスに反応しない"));
	ui.Separator("Click");
	ui.Field("Action###clickAction", value.clickAction,
	         Tip("クリックしたときにすること。None：何もしない（Script で UIStateSystem::IsClicked を見る）/ Open Screen・Close Screen：Screen の画面を出す・消す / Close Own Screen：この要素が入っている画面を消す / Switch Screen：この要素が入っている画面を消して、Screen の画面を出す"));
	const bool usesScreen = value.clickAction == UIClickAction::OpenScreen || value.clickAction == UIClickAction::CloseScreen || value.clickAction == UIClickAction::SwitchScreen;
	ui.BeginDisabled(!usesScreen);
	ui.Field("Screen###screen", value.screen, Tip("画面の名前（resources/ui/〇〇.uiscreen の 〇〇）"));
	ui.EndDisabled();
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIStateSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<UIStateState>();
	// UI State の置き場所・保存・Inspector を登録する（見せ方は DescribeUIState 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<UIStateComponent>();
	ComponentSerializer::Register<UIStateComponent>("UI State", &DescribeUIState);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIStateComponent>>("UI State", "UI", &DescribeUIState));
	EntityManager::AddDestroyListener(&UIStateSystem::OnEntityDestroyed);
}

void UIStateSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void UIStateSystem::Update(bool isGameRunning) {
	HTT_ASSERT_MSG(state != nullptr, "UIStateSystem::Initialize()を先に呼んでください");
	// 停止中：全部忘れる（Play を押すと、また最初の状態から）
	if (!isGameRunning) {
		state->runtimes.clear();
		return;
	}
	// エディタの一時停止中は、状態も変えない
	const SceneManager* main = SceneManager::GetMain();
	if (main != nullptr && main->GetPlayState() == PlayState::Paused) {
		return;
	}
	// --- UI State を外された Entity を片付ける ---
	std::erase_if(state->runtimes, [](const UIStateRuntime& runtime) { return EntityManager::Get<UIStateComponent>(runtime.entity) == nullptr; });
	EntityManager::ForEach<UIStateComponent>([](Handle<Entity> entity, UIStateComponent& component) { UpdateEntity(entity, component); });
}

//=============================================================================
// ゲームから使う
//=============================================================================
UIElementState UIStateSystem::GetState(Handle<Entity> entity) {
	const UIStateRuntime* runtime = FindRuntime(entity);
	return (runtime != nullptr) ? runtime->state : UIElementState::Normal;
}

bool UIStateSystem::IsClicked(Handle<Entity> entity) {
	const UIStateRuntime* runtime = FindRuntime(entity);
	return runtime != nullptr && runtime->isClicked;
}

void UIStateSystem::SetSelected(Handle<Entity> entity, bool isSelected) {
	if (UIStateRuntime* runtime = RequireRuntime(entity)) {
		runtime->isSelected = isSelected;
	}
}

void UIStateSystem::SetVisible(Handle<Entity> entity, bool isVisible) {
	UIStateRuntime* runtime = RequireRuntime(entity);
	if (runtime == nullptr || runtime->isVisible == isVisible) {
		return;
	}
	runtime->isVisible = isVisible;
	runtime->isPressing = false;
	runtime->needsVisibility = true; // 再生するのは次の Update（最初の Update の前なら、最初の形として当てる）
}

bool UIStateSystem::IsVisible(Handle<Entity> entity) {
	const UIStateRuntime* runtime = FindRuntime(entity);
	if (runtime != nullptr) {
		return runtime->isVisible;
	}
	const UIStateComponent* component = EntityManager::Get<UIStateComponent>(entity);
	return component != nullptr && component->startVisible;
}

//=============================================================================
// Entity の破棄
//=============================================================================
void UIStateSystem::OnEntityDestroyed(Handle<Entity> entity) {
	if (state != nullptr) {
		std::erase_if(state->runtimes, [entity](const UIStateRuntime& runtime) { return runtime.entity == entity; });
	}
}
```

### D-4 `Systems/EngineSystems.cpp`

include（UI のかたまり）に足す。

```cpp
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/UIStateSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
```

`Initialize` の `UIScreen::Initialize` の下に足す。

```cpp
	UIScreen::Initialize();               // UI Screen・UI Screen Element
	UIStateSystem::Initialize();          // UI State（要素の状態で Timeline を再生する）
```

`Release` の `UIScreen::Release` の上に足す。

```cpp
	UIStateSystem::Release();
	UIScreen::Release();
```

`Update` の `TimelineSystem::Update` の上に足す。

```cpp
	AnimationSystem::Update(isGameRunning, Time::GetDeltaTime());   // アニメーション。停止中もInspectorのPreview Timeのポーズを見せる。
	UIStateSystem::Update(isGameRunning);                           // UI State（マウスで状態を決めて、状態の Timeline の再生を頼む。Script の後・Timeline の前）
	TimelineSystem::Update(isGameRunning);                          // Timeline（Script とアニメーションの後。書いた値を、このフレームのワールド行列・描画に使う。時間は Component ごとに選ぶ）
```

`UIScreen::Update` のコメントを直す。

```cpp
	UIScreen::Update(isGameRunning);        // 画面（ポーズ画面など）の出す・消す動きを進めて、ゲームを止める・動かす（Motion の動きと、Timeline が終わったかを見る）
```

## パートE Timeline ウィンドウ

### E-1 `Editor/Timeline/TimelineEditorState.h`

include に2つ足す。

```cpp
#include <cstdint>
#include <functional>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Editor/Widgets/CurveEditor.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"
#include "HTTEngine/Timeline/TimelineManager.h"
```

`TimelineDrag` の下に2つ足す。

```cpp
/// <summary>
/// 再生する Entity の、Timeline のファイルを選ぶ欄1つ（Timeline Component の Timeline・UI Screen の Show・UI State の Hover など）
/// </summary>
struct TimelineSource {
	std::string label;                              // 欄の名前（Timeline・Show・Hover など）
	uint32_t handle = 0;                            // 欄のファイル（0 なら空）
	TimelineLayer layer = TimelineLayer::Component; // Play 中に、再生の様子を読む層
	std::function<void(uint32_t)> assign;           // 欄にファイルを入れる（Inspector で変えたときと同じく Undo に積む）
};

/// <summary>
/// 前に開いていた物（Preset を開いた後に Back で戻る）
/// </summary>
struct TimelineBackItem {
	uint32_t handle = 0;
	EntityId ownerId = 0;
	int source = -1;
	bool isExplicitOwner = false;
};
```

`TimelineEditorState` の「開いている物」を差し替える。

```cpp
	// ===== 開いている物 =====
	uint32_t handle = 0;                     // 開いているファイル（0 なら開いていない）
	EntityId ownerId = 0;                    // 再生する Entity。Play・Stop で作り直されると Handle は変わるが、番号は変わらない
	Handle<Entity> owner;                    // ownerId から毎フレーム引き直す（居なければ無効。プレビューできない）
	std::vector<TimelineSource> sources;     // 再生する Entity の Timeline の欄（毎フレーム集め直す）
	int source = -1;                         // 開いているファイルの欄の番号（-1：欄からではない。Project から開いた・Preset を開いた）
	bool isExplicitOwner = false;            // 欄に無いファイルでも、owner でプレビューする（Preset を、その対象の上で開いた）
	std::vector<TimelineBackItem> backStack; // Back で戻る先（Preset を開くたびに積む）
	uint32_t openRequest = 0;                // 次に描くときの頭で開くファイル（詳細の Preset の Open。0 なら頼まれていない）
	EntityId openOwnerId = 0;                // そのときの、再生する Entity
```

「調べる」の `Snap` の下に足す。

```cpp
	// Play 中に再生の様子を読む層（欄から開いていなければ Component）
	TimelineLayer GetLayer() const { return (source >= 0 && source < static_cast<int>(sources.size())) ? sources[source].layer : TimelineLayer::Component; }
```

### E-2 `Editor/Timeline/TimelineEditorState.cpp`

`SeekTo` の Play 中の所を直す。

```cpp
	if (isGameRunning) {
		// Play 中に動かせるのは Timeline Component の再生だけ（UI State・UI Screen の再生は、その係が時間を決める）
		if (GetLayer() == TimelineLayer::Component) {
			TimelineSystem::Seek(owner, time);
		}
		return;
	}
```

### E-3 `Editor/Timeline/TimelineDetails.cpp`（全体を差し替え）

`dynamic_cast<TimelineValueTrack*>` を `GetPropertyBinding()` にして、Tween のトラックでも Component・Property の欄が出るようにした。Tween のクリップの From・To（成分の数だけ）、Ease のカーブ、Preset のクリップの Fit Length・Open Preset を足した。

```cpp
#ifdef USE_IMGUI
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>

#include <externals/imgui/imgui.h>
#include <externals/imgui/imgui_internal.h>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Timeline/TimelineEditorState.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Timeline/TimelineEdit.h"
#include "HTTEngine/Timeline/TimelineValueTrack.h"
#include "HTTEngine/Timeline/Camera/TimelineCameraTrack.h"
#include "HTTEngine/Timeline/Tween/TimelinePresetTrack.h"
#include "HTTEngine/Timeline/Tween/TimelineTweenTrack.h"
#include "HTTEngine/Components/Camera/CameraComponent.h"


// Timeline ウィンドウの下の詳細（トラックの結び付け・クリップ・キー・カーブ）

namespace {
constexpr float kFieldsWidth = 280.0f;             // 項目の列（トラック・クリップ）1つの幅
constexpr float kItemLabelWidth = 90.0f;           // 項目の右の名前に残す幅
constexpr float kCurveMinHeight = 80.0f;           // カーブの絵の高さの下限
constexpr float kCurveFooterHeight = 120.0f;       // 欄が狭いとき、カーブの絵の下の数値の欄に残す高さ
constexpr float kCurveWideWidth = 640.0f;          // 欄の幅がこれ以上なら、数値の欄を絵の右に置く
constexpr float kCurveFieldsWidth = 280.0f;        // カーブの絵の右の数値の欄の幅
constexpr const char* kDefaultTrackName = "Track"; // トラックの名前の既定の値（結び付けを選ぶと自動の名前に替える）
constexpr std::array<const char*, ComponentValue::kChannelCount> kVectorNames = {"x", "y", "z", "w"}; // 成分の名前
constexpr std::array<const char*, ComponentValue::kChannelCount> kColorNames = {"r", "g", "b", "a"};  // 色の成分の名前

// ===== パスの一番下の名前 =====
std::string_view LeafName(std::string_view path) {
	const size_t slash = path.rfind('/');
	return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

// ===== 自動の名前（Title.color）=====
std::string MakeAutoName(const TimelineTrack& track, const TimelinePropertyBinding& binding) {
	return track.target.empty() ? binding.component + "." + binding.property : std::string(LeafName(track.target)) + "." + binding.property;
}

// ===== 結び付けを変えた後、名前が自動のままなら付け直す =====
void UpdateAutoName(TimelineTrack& track, const TimelinePropertyBinding& binding, const std::string& oldAutoName) {
	if (track.name == kDefaultTrackName || track.name == oldAutoName) {
		track.name = MakeAutoName(track, binding);
	}
}

// ===== 対象の Entity の項目の形（項目を動かす種類でない・分からなければ空）=====
std::optional<ComponentValueField> FindField(const TimelineEditorState& state, const TimelineTrack& track) {
	const TimelinePropertyBinding* binding = track.GetPropertyBinding();
	const Handle<Entity> target = (binding != nullptr) ? EntityManager::FindByPath(state.owner, track.target) : Handle<Entity>{};
	if (!target.IsValid()) {
		return std::nullopt;
	}
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, binding->component);
	const auto found = std::ranges::find(fields, binding->property, &ComponentValueField::key);
	return (found != fields.end()) ? std::optional(*found) : std::nullopt;
}

// ===== 対象の今の値（読めなければ全部 0 で、最初の成分だけ使う）=====
ComponentValue ReadTargetValue(const TimelineEditorState& state, const TimelineTrack& track) {
	ComponentValue value;
	value.isUsed[0] = true;
	if (const TimelinePropertyBinding* binding = track.GetPropertyBinding()) {
		const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
		if (target.IsValid()) {
			ComponentSerializer::GetValue(target, binding->component, binding->property, value);
		}
	}
	return value;
}

// ===== 成分の名前 =====
std::string ChannelName(const std::optional<ComponentValueField>& field, size_t channel) {
	if (!field.has_value()) {
		return kVectorNames[channel];
	}
	if (field->channelCount == 1) {
		return "value";
	}
	return field->isColor ? kColorNames[channel] : kVectorNames[channel];
}

// ===== シーンのカメラを選択肢に出す（onSelect は選んだ Entity を受け取る。isOutsideOnly なら、再生する Entity の外のカメラだけ）=====
template<class Function>
void DrawCameraChoices(const TimelineEditorState& state, const std::string& current, bool isOutsideOnly, Function&& onSelect) {
	int id = 0;
	EntityManager::ForEach<CameraComponent>([&](Handle<Entity> entity, const CameraComponent&) {
		const std::string path = EntityManager::MakeBindingPath(state.owner, entity);
		if (isOutsideOnly && !path.starts_with('/')) {
			return;
		}
		ImGui::PushID(id++); // 同じ名前のカメラがあっても項目が衝突しないように、数えた番号で分ける
		if (ImGui::Selectable(path.empty() ? "(self)" : path.c_str(), path == current)) {
			onSelect(entity);
		}
		ImGui::PopID();
	});
}

//=============================================================================
// トラック
//=============================================================================
// ===== 再生する Entity の子孫を選択肢に出す =====
void DrawTargetChoices(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> parent, int& id) {
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(parent, children);
	for (const Handle<Entity> child : children) {
		const std::string path = EntityManager::MakePath(state.owner, child).value_or("");
		ImGui::PushID(id++); // 同じ名前の兄弟があっても項目が衝突しないように、数えた番号で分ける
		if (ImGui::Selectable(path.c_str(), path == track.target)) {
			SetTimelineTarget(state, track, child);
		}
		ImGui::PopID();
		DrawTargetChoices(state, track, child, id);
	}
}

// ===== 対象 =====
void DrawTarget(TimelineEditorState& state, TimelineTrack& track) {
	if (!state.owner.IsValid()) {
		GetInspectorUI().Field("Target###target", track.target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く。空なら再生する Entity 自身。先頭が / ならシーンの一番上から"));
		return;
	}
	if (ImGui::BeginCombo("Target###target", track.target.empty() ? "(self)" : track.target.c_str())) {
		if (ImGui::Selectable("(self)", track.target.empty())) {
			SetTimelineTarget(state, track, state.owner);
		}
		int id = 0;
		DrawTargetChoices(state, track, state.owner, id);
		ImGui::SeparatorText("Scene Cameras");
		DrawCameraChoices(state, track.target, true, [&](Handle<Entity> entity) { SetTimelineTarget(state, track, entity); });
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす相手（再生する Entity とその子孫・シーンのカメラ）。Hierarchy の Entity をトラックの行へドラッグすると、シーンのどの Entity でも選べる");
}

// ===== Component と項目 =====
void DrawPropertyBinding(TimelineEditorState& state, TimelineTrack& track, TimelinePropertyBinding& binding) {
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		binding.Fields(GetInspectorUI()); // 対象が無い間は、文字で直せるようにする
		return;
	}
	const std::string oldAutoName = MakeAutoName(track, binding);
	// --- Component（値を動かせる項目がある物だけ）---
	if (ImGui::BeginCombo("Component###component", binding.component.c_str())) {
		for (const std::string& name : ComponentSerializer::GetComponentNames(target)) {
			if (ComponentSerializer::GetValueFields(target, name).empty()) {
				continue;
			}
			if (ImGui::Selectable(name.c_str(), name == binding.component)) {
				binding.component = name;
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす Component");
	// --- 項目 ---
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, binding.component);
	if (ImGui::BeginCombo("Property###property", binding.property.c_str())) {
		for (const ComponentValueField& field : fields) {
			if (ImGui::Selectable(std::format("{}###{}", field.label.empty() ? field.key : field.label, field.key).c_str(), field.key == binding.property)) {
				binding.property = field.key;
				UpdateAutoName(track, binding, oldAutoName);
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす項目（数で表せる物：float・int・bool・Vector・色）");
}

// ===== トラック全体 =====
void DrawTrackFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText(std::string(TimelineTrackRegistry::GetDisplayName(track.typeName)).c_str());
	ImGui::PushID("track"); // クリップの項目と名前が重なる（Name###name）ので分ける
	GetInspectorUI().Field("Name###name", track.name, Tip("トラックの名前（一覧と警告に出す）"));
	if (track.UsesTarget()) {
		DrawTarget(state, track);
	}
	if (TimelinePropertyBinding* binding = track.GetPropertyBinding()) {
		DrawPropertyBinding(state, track, *binding);
	} else {
		track.Fields(GetInspectorUI());
	}
	ImGui::PopID();
}

//=============================================================================
// クリップ
//=============================================================================
// ===== 成分（チェック：キーがある＝動かす / 名前：カーブの絵に出す）=====
void DrawChannels(TimelineEditorState& state, TimelineTrack& track, std::span<Curve> curves) {
	const std::optional<ComponentValueField> field = FindField(state, track);
	const size_t count = field.has_value() ? (std::min)(field->channelCount, curves.size()) : curves.size();
	state.selection.channel = (std::min)(state.selection.channel, static_cast<int>(count) - 1);
	// --- 選んでいる成分にキーが無ければ、キーのある最初の成分を絵に出す ---
	if (curves[state.selection.channel].GetKeys().empty()) {
		const auto used = std::ranges::find_if(curves, [](const Curve& curve) { return !curve.GetKeys().empty(); });
		if (used != curves.end()) {
			state.selection.channel = static_cast<int>(used - curves.begin());
			state.curveEditor = CurveEditor{};
		}
	}
	const auto hasKeys = [](const Curve& curve) { return !curve.GetKeys().empty(); };
	const ptrdiff_t usedCount = std::ranges::count_if(curves.first(count), hasKeys); // キーのある成分の数
	ImGui::PushID("channels"); // 下のキーの値の欄と同じ名前（a など）になるので分ける
	for (size_t i = 0; i < count; ++i) {
		ImGui::PushID(static_cast<int>(i));
		bool isUsed = hasKeys(curves[i]);
		// 最後の1つは外せない（外すとクリップが何も動かさなくなる。要らなければクリップを消す）
		const bool isLastUsed = isUsed && usedCount == 1;
		ImGui::BeginDisabled(isLastUsed);
		if (ImGui::Checkbox("##used", &isUsed)) {
			if (isUsed) {
				// 入れ直したら、対象の今の値の平らな線から（0 から始めると、FOV などが一瞬で壊れた値になる）
				TimelineEdit::FillFlat(curves[i], ReadTargetValue(state, track).channels[i], track.GetClipDuration(state.selection.clip));
			} else {
				curves[i] = Curve();
			}
			state.curveEditor = CurveEditor{};
			state.MarkEdited();
		}
		ImGui::EndDisabled();
		// 押せないときも説明を出す（SetItemTooltip は押せない項目では出ない）
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", isLastUsed ? "最後の1つは外せない（動かさないなら、クリップを消す）" : "この成分を動かす（外すとキーを全部消す。入れ直すと今の値の平らな線になる）");
		}
		ImGui::SameLine();
		if (ImGui::Selectable(ChannelName(field, i).c_str(), state.selection.channel == static_cast<int>(i), 0, ImVec2(40.0f, 0.0f))) {
			state.selection.channel = static_cast<int>(i);
			state.curveEditor = CurveEditor{};
		}
		ImGui::PopID();
		if (i + 1 < count) {
			ImGui::SameLine();
		}
	}
	ImGui::PopID();
}

// ===== 選んでいるキーの値（全部の成分を並べて打てる）=====
void DrawKeyFields(TimelineEditorState& state, TimelineTrack& track, std::span<Curve> curves) {
	TimelineSelection& selection = state.selection;
	// --- カーブの絵で動かしたり消したりしたら、もう選んでいない ---
	const std::vector<float> times = TimelineEdit::CollectKeyTimes(curves);
	const bool isFound = std::ranges::any_of(times, [&selection](float time) { return std::abs(time - selection.keyTime) <= TimelineEdit::kSameKeyTime; });
	if (!selection.hasKey || !isFound) {
		selection.hasKey = false;
		return;
	}
	ImGui::SeparatorText("Key");
	const float duration = track.GetClipDuration(selection.clip);
	float keyTime = selection.keyTime;
	if (ImGui::DragFloat("Time###keyTime", &keyTime, 0.01f, 0.0f, duration, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
		TimelineEdit::MoveKeys(curves, selection.keyTime, keyTime);
		selection.keyTime = keyTime;
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("キーの時間（クリップの始まりからの秒）");
	// --- その時間にキーのある成分の値 ---
	const std::optional<ComponentValueField> field = FindField(state, track);
	ImGui::PushID("keyValues");
	for (size_t i = 0; i < curves.size(); ++i) {
		const std::vector<CurveKey>& keys = curves[i].GetKeys();
		const auto key = std::ranges::find_if(keys, [&selection](const CurveKey& item) { return std::abs(item.time - selection.keyTime) <= TimelineEdit::kSameKeyTime; });
		if (key == keys.end()) {
			continue;
		}
		ImGui::PushID(static_cast<int>(i));
		float value = key->value;
		if (ImGui::DragFloat(ChannelName(field, i).c_str(), &value, 0.01f)) {
			CurveKey edited = *key;
			edited.value = value;
			curves[i].SetKey(static_cast<int>(key - keys.begin()), edited);
			state.MarkEdited();
		}
		ImGui::PopID();
	}
	ImGui::PopID();
}

// ===== カメラのクリップの、写すカメラ =====
void DrawClipCamera(TimelineEditorState& state, TimelineCameraTrack& track) {
	TimelineCameraClip& clip = track.clips[state.selection.clip];
	const auto select = [&](Handle<Entity> entity) {
		clip.camera = EntityManager::MakeBindingPath(state.owner, entity);
		state.MarkEdited();
	};
	if (ImGui::BeginCombo("Camera###camera", clip.camera.empty() ? "(self)" : clip.camera.c_str())) {
		DrawCameraChoices(state, clip.camera, false, select);
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("このクリップの間に写すカメラ。Hierarchy の Camera をここへドラッグしても選べる。再生する Entity の外のカメラは / で始まるパスになる");
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
			select(*static_cast<const Handle<Entity>*>(payload->Data));
		}
		ImGui::EndDragDropTarget();
	}
}

// ===== Tween の端1つ（決め方・値・Parameter の名前）=====
void DrawTweenPoint(TimelineEditorState& state, const char* label, TimelineTweenPoint& point, const std::optional<ComponentValueField>& field) {
	ImGui::PushID(label);
	ImGui::SeparatorText(label);
	GetInspectorUI().Field("Mode###mode", point.mode, Tip("Current：始まったときの今の値 / Value：下の値 / Add：元の値 ＋ 下の値 / Multiply：元の値 × 下の値 / Parameter：TimelineSystem::SetParameter で渡す値"));
	if (point.mode == TimelineTweenMode::Parameter) {
		GetInspectorUI().Field("Name###parameter", point.parameter, Tip("TimelineSystem::SetParameter に渡す名前。渡していないときは下の値"));
	}
	// --- 値（項目の成分の数だけ。Current は使わない）---
	const size_t count = field.has_value() ? field->channelCount : ComponentValue::kChannelCount;
	std::array<float*, ComponentValue::kChannelCount> channels = {&point.value.x, &point.value.y, &point.value.z, &point.value.w};
	ImGui::BeginDisabled(point.mode == TimelineTweenMode::Current);
	for (size_t i = 0; i < count; ++i) {
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::DragFloat(ChannelName(field, i).c_str(), channels[i], 0.01f)) {
			state.MarkEdited();
		}
		ImGui::PopID();
	}
	ImGui::EndDisabled();
	ImGui::PopID();
}

// ===== Tween のクリップ（From・To）=====
void DrawClipTween(TimelineEditorState& state, TimelineTweenTrack& track) {
	TimelineTweenClip& clip = track.clips[state.selection.clip];
	const std::optional<ComponentValueField> field = FindField(state, track);
	DrawTweenPoint(state, "From", clip.from, field);
	DrawTweenPoint(state, "To", clip.to, field);
	ImGui::TextDisabled("動き方は右のカーブ（横：時間の割合 0〜1 / 縦：From〜To の割合）");
}

// ===== Preset のクリップ（長さを合わせる・プリセットを開く）=====
void DrawClipPreset(TimelineEditorState& state, TimelinePresetTrack& track, uint32_t before) {
	TimelinePresetClip& clip = track.clips[state.selection.clip];
	const Timeline* preset = TimelineManager::Get(clip.timeline);
	// --- ファイルを選び直したら、長さをプリセットに合わせる ---
	if (clip.timeline != before && preset != nullptr) {
		state.selection.clip = static_cast<int>(track.SetClipTime(state.selection.clip, clip.start, preset->GetDuration()));
		state.MarkEdited();
		return;
	}
	ImGui::BeginDisabled(preset == nullptr);
	if (ImGui::SmallButton("Fit Length")) {
		state.selection.clip = static_cast<int>(track.SetClipTime(state.selection.clip, clip.start, preset->GetDuration()));
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("クリップの長さを、プリセットの長さにする");
	ImGui::SameLine();
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	ImGui::BeginDisabled(!target.IsValid());
	if (ImGui::SmallButton("Open Preset")) {
		state.openRequest = clip.timeline;
		state.openOwnerId = EntityManager::Get(target)->id;
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("プリセットを、このトラックの対象の上で開く（Back で戻る）。直したプリセットは、使っている全部の場所に効く");
	ImGui::EndDisabled();
}

// ===== クリップ全体 =====
void DrawClipFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText("Clip");
	ImGui::PushID("clip");
	if (auto* cameraTrack = dynamic_cast<TimelineCameraTrack*>(&track)) {
		DrawClipCamera(state, *cameraTrack);
	}
	auto* presetTrack = dynamic_cast<TimelinePresetTrack*>(&track);
	const uint32_t presetBefore = (presetTrack != nullptr) ? presetTrack->clips[state.selection.clip].timeline : 0;
	state.selection.clip = static_cast<int>(track.DescribeClip(state.selection.clip, GetInspectorUI()));
	if (presetTrack != nullptr) {
		DrawClipPreset(state, *presetTrack, presetBefore);
	}
	if (auto* tweenTrack = dynamic_cast<TimelineTweenTrack*>(&track)) {
		DrawClipTween(state, *tweenTrack);
	}
	const std::span<Curve> curves = track.GetClipCurves(state.selection.clip);
	if (!curves.empty()) {
		DrawChannels(state, track, curves);
		if (ImGui::SmallButton("Add Key At Playhead")) {
			const float keyTime = std::clamp(state.time - track.GetClipStart(state.selection.clip), 0.0f, track.GetClipDuration(state.selection.clip));
			TimelineEdit::AddKeys(curves, keyTime);
			state.SelectKey(state.selection.track, state.selection.clip, keyTime);
			state.MarkEdited();
		}
		ImGui::SetItemTooltip("再生位置に、形を変えずにキーを足す（列の上のクリップをダブルクリックでも足せる）");
		DrawKeyFields(state, track, curves);
	}
	ImGui::PopID();
}

// ===== カーブの絵（描く高さを決めて、絵を描く）=====
void DrawCurveCanvas(TimelineEditorState& state, Curve& curve) {
	// 広ければ数値の欄を右に置いて、上の並びを除いた高さを全部カーブの絵に使う
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const bool isWide = available.x >= kCurveWideWidth;
	const float footerHeight = isWide ? 0.0f : kCurveFooterHeight;
	const float canvasHeight = (std::max)(available.y - ImGui::GetFrameHeightWithSpacing() - footerHeight, kCurveMinHeight);
	if (state.curveEditor.Draw("curve", curve, canvasHeight, GetInspectorUI(), isWide ? kCurveFieldsWidth : 0.0f)) {
		state.MarkEdited();
	}
	state.curveEditor.TakeFinishedEdit(); // Undo は TimelineWindow がファイル全体でまとめて積むので、使わず受け取る
}

// ===== カーブの絵（値のトラック）=====
void DrawCurvePanel(TimelineEditorState& state, std::span<Curve> curves) {
	if (curves.empty()) {
		ImGui::TextDisabled("このクリップにカーブはありません");
		return;
	}
	if (curves[state.selection.channel].GetKeys().empty()) {
		ImGui::TextDisabled("動かす成分にチェックを入れると、ここでカーブを直せる");
		return;
	}
	DrawCurveCanvas(state, curves[state.selection.channel]);
}

// ===== Tween の動き方のカーブ（Ease の形は、カーブの絵の Ease Preset で作り直せる）=====
void DrawTweenEasePanel(TimelineEditorState& state, TimelineTweenClip& clip) {
	ImGui::TextDisabled("動き方（横：時間の割合 0〜1 / 縦：From〜To の割合。1 を超えると To より行き過ぎる）");
	DrawCurveCanvas(state, clip.ease);
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
void DrawTimelineDetails(TimelineEditorState& state, float height) {
	TimelineTrack* track = state.GetSelectedTrack();
	if (track == nullptr) {
		ImGui::TextDisabled("トラックを選ぶと、ここで結び付けとキーを直せる");
		const Timeline* timeline = state.GetTimeline();
		if (!timeline->unknownTracks.empty()) {
			ImGui::TextDisabled("知らない種類のトラックが %d 個あります（動かしません。保存しても消えません）", static_cast<int>(timeline->unknownTracks.size()));
		}
		return;
	}
	// --- 左：トラックの結び付け ---
	ImGui::BeginChild("##trackFields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	DrawTrackFields(state, *track);
	ImGui::PopItemWidth();
	ImGui::EndChild();
	// --- 真ん中：クリップとキー（1列にまとめると、キーの値がスクロールの下へ隠れる）---
	ImGui::SameLine();
	ImGui::BeginChild("##clipFields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	if (state.selection.clip >= 0) {
		DrawClipFields(state, *track);
	} else {
		ImGui::TextDisabled("クリップを選ぶと、ここで直せる");
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();
	// ComponentUI の項目は「変えたか」を返さないので、ImGui の「今操作している項目を変えた」印を見る（打っている文字にも効く）
	if (ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame) {
		state.MarkEdited();
	}
	// --- 右：カーブ（Tween は動き方のカーブ）---
	ImGui::SameLine();
	ImGui::BeginChild("##curve", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
	auto* tweenTrack = dynamic_cast<TimelineTweenTrack*>(track);
	if (state.selection.clip < 0) {
		ImGui::TextDisabled("クリップを選ぶと、ここでカーブを直せる（列の何も無い所をダブルクリックでクリップを足す）");
	} else if (tweenTrack != nullptr) {
		DrawTweenEasePanel(state, tweenTrack->clips[state.selection.clip]);
	} else {
		DrawCurvePanel(state, state.GetSelectedCurves());
	}
	ImGui::EndChild();
}

//=============================================================================
// 部品の間で使う
//=============================================================================
void FillNewClipCurves(const TimelineEditorState& state, TimelineTrack& track, size_t clip) {
	const ComponentValue value = ReadTargetValue(state, track);
	// --- Tween：今の値から、今の値へ（何も動かない形から直していく）---
	if (auto* tweenTrack = dynamic_cast<TimelineTweenTrack*>(&track)) {
		tweenTrack->clips[clip].to.value = {value.channels[0], value.channels[1], value.channels[2], value.channels[3]};
		tweenTrack->clips[clip].from.value = tweenTrack->clips[clip].to.value;
		return;
	}
	// --- 値のトラック：対象の今の値で平らなキー ---
	const std::span<Curve> curves = track.GetClipCurves(clip);
	for (size_t i = 0; i < curves.size(); ++i) {
		if (value.isUsed[i]) {
			TimelineEdit::FillFlat(curves[i], value.channels[i], track.GetClipDuration(clip));
		}
	}
}

std::string FindTimelineBindingProblem(const TimelineEditorState& state, const TimelineTrack& track) {
	if (!state.owner.IsValid()) {
		return {}; // 再生する Entity が分からないので、調べられない
	}
	if (!track.UsesTarget()) {
		return track.FindClipProblem(state.owner);
	}
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		return std::format("対象が見つかりません: \"{}\"", track.target);
	}
	const TimelinePropertyBinding* binding = track.GetPropertyBinding();
	if (binding == nullptr) {
		return {};
	}
	ComponentValue value;
	switch (ComponentSerializer::GetValue(target, binding->component, binding->property, value)) {
	case ComponentValueResult::Written:
		return {};
	case ComponentValueResult::NoComponent:
		return std::format("\"{}\" に Component \"{}\" がありません", track.target, binding->component);
	case ComponentValueResult::NoField:
		return std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", binding->component, binding->property);
	}
	return {};
}

void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity) {
	const TimelinePropertyBinding* binding = track.GetPropertyBinding();
	const std::string oldAutoName = (binding != nullptr) ? MakeAutoName(track, *binding) : std::string();
	track.target = EntityManager::MakeBindingPath(state.owner, entity);
	if (binding != nullptr) {
		UpdateAutoName(track, *binding, oldAutoName);
	}
	state.MarkEdited();
}
#endif
```

### E-4 `Editor/Timeline/TimelineWindow.h`

クラスの説明の「開く」の `<para>` を差し替える。

```cpp
/// <para>開く：Hierarchy で Timeline の欄（Timeline Component・UI Screen の Show/Hide・UI State の状態）のある Entity を選ぶ（その Entity でプレビューできる。欄は上の一覧で切り替え、空の欄は New で作る）
/// / Project で .timeline をダブルクリック（同じファイルを欄で選んでいる Entity があれば、それでプレビューできる）/ Preset のクリップの Open Preset（対象の上で開く。Back で戻る）</para>
/// <para>直した物は、操作が終わるたびにファイルへ書いて Undo に積む（.curve と同じ）</para>
```

### E-5 `Editor/Timeline/TimelineWindow.cpp`（全体を差し替え）

足した所：欄を集める `CollectSources`・`EditComponent`・`MakeNewTimelinePath`、上の欄の一覧と New・Back（`DrawSourceBar`）、`ResolveOwner` の欄と Preset の Open、Play 中の層、`Open` で欄から再生する Entity を探す。

```cpp
#ifdef USE_IMGUI
#include "TimelineWindow.h"

#include <array>
#include <format>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <filesystem>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Components/Animation/TimelineComponent.h"
#include "HTTEngine/Components/UI/UIScreenComponent.h"
#include "HTTEngine/Components/UI/UIStateComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Timeline/TimelineEditorState.h"
#include "HTTEngine/Editor/Widgets/ToolIcons.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"

namespace {
constexpr float kFirstWidth = 960.0f;       // 最初のウィンドウの幅
constexpr float kFirstHeight = 520.0f;      // 最初のウィンドウの高さ
constexpr float kMinRowsHeight = 60.0f;     // トラックの一覧の高さの下限
constexpr float kMinDetailsHeight = 120.0f; // 詳細の高さの下限
constexpr float kSplitterHeight = 8.0f;     // 詳細との境目の高さ
constexpr float kTimeFieldWidth = 80.0f;    // 再生位置の欄の幅
constexpr float kLengthFieldWidth = 70.0f;  // 長さの欄の幅
constexpr float kSnapFieldWidth = 80.0f;    // スナップの欄の幅
constexpr float kSourceFieldWidth = 260.0f; // 欄を選ぶ一覧の幅
constexpr const char* kNewTimelineFolder = "resources/timelines"; // New で作る Timeline の置き場所

/// <summary>
/// スナップの選択肢1つ
/// </summary>
struct SnapChoice {
	const char* name;
	float step;
};
constexpr std::array<SnapChoice, 6> kSnapChoices = {
    {{"Off", 0.0f}, {"1 s", 1.0f}, {"0.5 s", 0.5f}, {"0.1 s", 0.1f}, {"1/30 s", 1.0f / 30.0f}, {"1/60 s", 1.0f / 60.0f}}
};

TimelineEditorState state;               // ウィンドウの状態
Handle<Entity> lastSelected;             // 前に見た Hierarchy の選択（選び直したときだけファイルを開き直す）
Handle<Entity> lastScreenRoot;           // 前に見た編集中の画面（UI Screen）
float rowsLimit = 180.0f;                // トラックの一覧の高さの上限（境目をつまんで変える。トラックが少なければ低く出して、詳細に譲る）
bool needsFocus = false;                 // 次に描くとき、ウィンドウを前に出す
std::vector<TimelineRange> passedRanges; // プレビューで通った時間（使わないが、Advance が書く場所）

//=============================================================================
// 欄（再生する Entity の Timeline を選ぶ所）
//=============================================================================
// ===== Component を Inspector で変えたときと同じく、前後を履歴に積んで変える =====
template<class T, class Edit>
void EditComponent(Handle<Entity> entity, Edit&& edit) {
	T* component = EntityManager::Get<T>(entity);
	const ComponentEditor* editor = ComponentEditorRegistry::Find<T>();
	if (component == nullptr || editor == nullptr) {
		return;
	}
	const std::vector<std::byte> before = editor->Save(component);
	edit(*component);
	EditorHistory::RecordComponentChange(entity, *editor, before, editor->Save(component));
	EditorHistory::CommitComponentChanges();
}

// ===== Entity の、Timeline のファイルを選ぶ欄を全部集める =====
std::vector<TimelineSource> CollectSources(Handle<Entity> entity) {
	std::vector<TimelineSource> sources;
	if (const TimelineComponent* timeline = EntityManager::Get<TimelineComponent>(entity)) {
		sources.push_back({"Timeline", timeline->timeline, TimelineLayer::Component,
		                   [entity](uint32_t handle) { EditComponent<TimelineComponent>(entity, [handle](TimelineComponent& value) { value.timeline = handle; }); }});
	}
	if (const UIScreenComponent* screen = EntityManager::Get<UIScreenComponent>(entity)) {
		sources.push_back({"UI Screen / Show", screen->showTimeline, TimelineLayer::UIScreen,
		                   [entity](uint32_t handle) { EditComponent<UIScreenComponent>(entity, [handle](UIScreenComponent& value) { value.showTimeline = handle; }); }});
		sources.push_back({"UI Screen / Hide", screen->hideTimeline, TimelineLayer::UIScreen,
		                   [entity](uint32_t handle) { EditComponent<UIScreenComponent>(entity, [handle](UIScreenComponent& value) { value.hideTimeline = handle; }); }});
	}
	if (const UIStateComponent* ui = EntityManager::Get<UIStateComponent>(entity)) {
		for (size_t i = 0; i < kUIElementStateCount; ++i) {
			const std::string label = std::format("UI State / {}", magic_enum::enum_name(static_cast<UIElementState>(i)));
			sources.push_back({label, ui->stateTimelines[i], TimelineLayer::UIState,
			                   [entity, i](uint32_t handle) { EditComponent<UIStateComponent>(entity, [handle, i](UIStateComponent& value) { value.stateTimelines[i] = handle; }); }});
		}
		sources.push_back({"UI State / Show", ui->showTimeline, TimelineLayer::UIVisibility,
		                   [entity](uint32_t handle) { EditComponent<UIStateComponent>(entity, [handle](UIStateComponent& value) { value.showTimeline = handle; }); }});
		sources.push_back({"UI State / Hide", ui->hideTimeline, TimelineLayer::UIVisibility,
		                   [entity](uint32_t handle) { EditComponent<UIStateComponent>(entity, [handle](UIStateComponent& value) { value.hideTimeline = handle; }); }});
	}
	return sources;
}

// ===== New で作るファイルの場所（Entity の名前_欄の最後の名前.timeline。"UI Screen / Show" なら Show。使われていれば後ろに番号）=====
std::filesystem::path MakeNewTimelinePath(const std::string& ownerName, const std::string& label) {
	constexpr std::string_view kSeparator = " / "; // 欄の名前の区切り（"UI Screen / Show"）
	const size_t separator = label.rfind(kSeparator);
	const std::string_view leaf = (separator == std::string::npos) ? std::string_view(label) : std::string_view(label).substr(separator + kSeparator.size());
	std::string base = std::format("{}_{}", ownerName, leaf);
	// ファイルの名前に使えない文字と空白は _ にする
	std::ranges::replace_if(base, [](char c) { return std::string_view(" /\\:*?\"<>|").find(c) != std::string_view::npos; }, '_');
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::string name = (number == 0) ? base : std::format("{}{}", base, number);
		const std::filesystem::path path = std::filesystem::path(kNewTimelineFolder) / ToPath(name + std::string(TimelineManager::kFileExtension));
		if (!std::filesystem::exists(path, error)) {
			return path;
		}
	}
}

//=============================================================================
// 開く
//=============================================================================
// ===== ファイルを開く =====
void OpenFile(uint32_t handle) {
	state.handle = handle;
	state.selection = {};
	state.curveEditor = CurveEditor{};
	state.playhead.Stop();
	state.time = 0.0f;
	state.isPreview = false;
	state.needsFit = true;
	state.hasEdit = false;
	state.committedText = TimelineManager::ToText(handle);
	state.seenRevision = TimelineManager::GetRevision(handle);
}

// ===== プレビューをやめる（次のフレームから要求を出さない＝値は元のまま）=====
void StopPreview() {
	state.playhead.Stop();
	state.time = 0.0f;
	state.isPreview = false;
}

// ===== 開く物と、再生する Entity を決める =====
void ResolveOwner() {
	// --- 詳細の Preset の Open：今の物を Back に積んで、プリセットを対象の上で開く ---
	if (state.openRequest != 0) {
		state.backStack.push_back({state.handle, state.ownerId, state.source, state.isExplicitOwner});
		OpenFile(std::exchange(state.openRequest, 0));
		state.ownerId = state.openOwnerId;
		state.source = -1;
		state.isExplicitOwner = true;
	}
	// --- Hierarchy で Timeline の欄のある Entity を選んだら、それを開く ---
	// Component が付くのを待てるように、付いていない間は覚えない（画面（UI Screen）の一番上は、選ばれた次のフレームに Component が付く）
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	const std::vector<TimelineSource> selectedSources = CollectSources(selected);
	if (selectedSources.empty()) {
		lastSelected = {};
	} else if (selected != lastSelected) {
		lastSelected = selected;
		state.ownerId = EntityManager::Get(selected)->id;
		state.isExplicitOwner = false;
		state.backStack.clear();
		// 開いているファイルが欄にあれば、そのまま。無ければ、ファイルの入っている最初の欄（全部空なら最初の欄）
		const auto isOpened = [](const TimelineSource& source) { return source.handle != 0 && source.handle == state.handle; };
		const auto hasFile = [](const TimelineSource& source) { return source.handle != 0; };
		auto chosen = std::ranges::find_if(selectedSources, isOpened);
		chosen = (chosen != selectedSources.end()) ? chosen : std::ranges::find_if(selectedSources, hasFile);
		chosen = (chosen != selectedSources.end()) ? chosen : selectedSources.begin();
		state.source = static_cast<int>(chosen - selectedSources.begin());
		if (chosen->handle != state.handle) {
			OpenFile(chosen->handle);
		}
	}
	// --- 番号から今の Handle を引いて、欄を集め直す ---
	const Handle<Entity> found = EntityManager::FindById(state.ownerId);
	state.sources = CollectSources(found);
	if (state.source >= static_cast<int>(state.sources.size())) {
		state.source = -1;
	}
	// --- 開いている欄のファイルが変わった（Inspector で選び直した・New で作った・Undo）：開き直す ---
	if (state.source >= 0 && state.sources[state.source].handle != state.handle) {
		OpenFile(state.sources[state.source].handle);
	}
	// --- 別のシーンの同じ番号の Entity と間違えないように、欄にそのファイルがあるかも見る（Preset は対象の上で開いたので見ない）---
	const bool isReferenced = std::ranges::any_of(state.sources, [](const TimelineSource& source) { return source.handle != 0 && source.handle == state.handle; });
	state.owner = (state.handle != 0 && (isReferenced || state.isExplicitOwner)) ? found : Handle<Entity>{};
	// --- 再生する Entity が居なくなった（シーンの切り替え・消した）---
	if (!state.owner.IsValid()) {
		state.isPreview = false;
	}
	// --- 画面（UI Screen）の編集に入る・出るときは切る（中身が入れ替わる。編集中は画面の中の Entity を選べばプレビューできる）---
	const Handle<Entity> screenRoot = UIScreenEditor::GetRoot();
	if (screenRoot != lastScreenRoot) {
		lastScreenRoot = screenRoot;
		StopPreview();
	}
}

// ===== Undo で中身が変わっていたら、知っている中身を合わせる =====
void SyncRevision() {
	const uint32_t revision = TimelineManager::GetRevision(state.handle);
	if (revision != state.seenRevision) {
		state.seenRevision = revision;
		state.committedText = TimelineManager::ToText(state.handle);
		state.hasEdit = false;
		state.ClampSelection();
		state.curveEditor = CurveEditor{}; // カーブの中身も入れ替わったので、見ている範囲を合わせ直す（前の範囲のままだと、戻ったキーが絵の外に出て見えない）
	}
}

// ===== 再生位置を進めて、プレビューを頼む =====
void UpdateTime(const Timeline& timeline) {
	const SceneManager* sceneManager = SceneManager::GetMain();
	const bool isGameRunning = sceneManager != nullptr && sceneManager->GetPlayState() != PlayState::Editing;
	if (isGameRunning && !state.isGameRunning) {
		StopPreview(); // Play を押した（このフレームから要求を出さないので、Play の退避は元の値を写す）
	}
	state.isGameRunning = isGameRunning;
	state.duration = timeline.GetDuration();

	// --- Play 中：TimelineSystem の時間を見せる（その層で別のファイルを再生していれば 0）---
	if (isGameRunning) {
		const TimelineStatus status = TimelineSystem::GetStatus(state.owner, state.GetLayer());
		state.time = (status.timeline == state.handle) ? status.time : 0.0f;
		return;
	}
	// --- 停止中：ウィンドウの中で時間を進める ---
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(state.owner);
	const bool usesComponentSpeed = component != nullptr && state.GetLayer() == TimelineLayer::Component;
	const float speed = (usesComponentSpeed && component->speed > 0.0f) ? component->speed : 1.0f;
	if (state.playhead.GetState() == TimelinePlayState::Playing) {
		state.playhead.Advance(ImGui::GetIO().DeltaTime * speed, state.duration, state.isLoop, passedRanges);
		state.time = state.playhead.GetTime();
	}
	state.time = (std::min)(state.time, state.duration); // 縮めた長さの外へ出ない
	if (state.isPreview && state.owner.IsValid()) {
		TimelineSystem::RequestPreview(state.owner, state.handle, state.time);
	}
}

//=============================================================================
// 描く
//=============================================================================
// ===== 押している間は色を変えるボタン =====
bool ToggleButton(const char* label, bool isOn, const char* tooltip) {
	if (isOn) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
	}
	const bool isPressed = ImGui::Button(label);
	if (isOn) {
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip("%s", tooltip);
	return isPressed;
}

// ===== 欄を選ぶ・空の欄にファイルを作る・Back =====
void DrawSourceBar() {
	// --- Back（Preset を開く前に戻る）---
	if (!state.backStack.empty()) {
		if (ImGui::Button("Back")) {
			const TimelineBackItem back = state.backStack.back();
			state.backStack.pop_back();
			OpenFile(back.handle);
			state.ownerId = back.ownerId;
			state.source = back.source;
			state.isExplicitOwner = back.isExplicitOwner;
		}
		ImGui::SetItemTooltip("Preset を開く前に開いていた Timeline に戻る");
		ImGui::SameLine();
	}
	const Entity* owner = EntityManager::Get(EntityManager::FindById(state.ownerId));
	if (state.sources.empty() || owner == nullptr) {
		return;
	}
	// --- 欄の一覧（欄の名前 と ファイルの名前）---
	const auto describe = [](const TimelineSource& source) {
		const std::string& path = TimelineManager::GetPath(source.handle);
		return std::format("{}  ({})", source.label, path.empty() ? std::string("none") : ToUtf8(ToPath(path).filename()));
	};
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", owner->name.c_str());
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kSourceFieldWidth);
	const std::string current = (state.source >= 0) ? describe(state.sources[state.source]) : std::string("(Project から開いたファイル)");
	if (ImGui::BeginCombo("##source", current.c_str())) {
		for (size_t i = 0; i < state.sources.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(describe(state.sources[i]).c_str(), static_cast<int>(i) == state.source)) {
				state.source = static_cast<int>(i);
				state.isExplicitOwner = false;
				OpenFile(state.sources[i].handle);
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("この Entity の、Timeline を選ぶ欄（Timeline Component・UI Screen・UI State）。選ぶと、その欄のファイルを開く");
	// --- 空の欄：New で作って入れる（停止中だけ。Play 中に入れても Stop で戻る）---
	if (state.source >= 0 && state.sources[state.source].handle == 0) {
		ImGui::SameLine();
		ImGui::BeginDisabled(state.isGameRunning);
		if (ImGui::Button("New")) {
			const std::filesystem::path path = MakeNewTimelinePath(owner->name, state.sources[state.source].label);
			if (TimelineManager::CreateNewFile(path)) {
				const uint32_t handle = TimelineManager::Load(path);
				state.sources[state.source].assign(handle); // Inspector で選んだのと同じ（Undo で外れる）
				OpenFile(handle);
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s に新しい Timeline を作って、この欄に入れる", kNewTimelineFolder);
	}
}

// ===== 再生・一時停止・停止・ループ =====
void DrawTransport() {
	// Play 中に動かせるのは Timeline Component の再生だけ（UI State・UI Screen は、その係が再生する）
	const bool isComponentLayer = state.GetLayer() == TimelineLayer::Component;
	const bool canPlay = state.owner.IsValid() && (!state.isGameRunning || isComponentLayer);
	const TimelineStatus status = TimelineSystem::GetStatus(state.owner, state.GetLayer());
	const bool isPlaying = state.isGameRunning ? (status.IsPlaying() && status.timeline == state.handle) : state.playhead.GetState() == TimelinePlayState::Playing;
	ImGui::BeginDisabled(!canPlay);
	if (ToolIcons::Button("play", ToolIcon::Play, isPlaying, state.isGameRunning ? "この Entity の Timeline を再生する" : "プレビューを再生する（イベントは呼ばない）")) {
		if (state.isGameRunning) {
			TimelineSystem::Play(state.owner);
		} else {
			state.playhead.Seek(state.time, state.duration);
			state.playhead.Play();
			state.isPreview = true;
		}
	}
	ImGui::SameLine();
	if (ToolIcons::Button("pause", ToolIcon::Pause, false, "一時停止")) {
		if (state.isGameRunning) {
			TimelineSystem::Pause(state.owner);
		} else {
			state.playhead.Pause();
		}
	}
	ImGui::SameLine();
	if (ToolIcons::Button("stop", ToolIcon::Stop, false, state.isGameRunning ? "止めて最初へ戻す" : "プレビューをやめて最初へ戻す（画面は元の値へ戻る）")) {
		if (state.isGameRunning) {
			TimelineSystem::Stop(state.owner);
		} else {
			StopPreview();
		}
	}
	ImGui::SameLine();
	// --- ループ：Play 中は Component の値（Stop で元へ戻る）、停止中はプレビューだけ ---
	TimelineComponent* component = isComponentLayer ? EntityManager::Get<TimelineComponent>(state.owner) : nullptr;
	const bool isLoop = (state.isGameRunning && component != nullptr) ? component->loop : state.isLoop;
	if (ToggleButton("Loop", isLoop, state.isGameRunning ? "最後まで行ったら最初へ戻す（Play の間だけ。Stop で元へ戻る）" : "プレビューをループする（保存しない）")) {
		if (state.isGameRunning && component != nullptr) {
			component->loop = !component->loop;
		} else {
			state.isLoop = !state.isLoop;
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(state.isGameRunning);
	if (ToggleButton("Preview", state.isPreview, "停止中に、再生位置の値を画面に出す（切ると元の値へ戻る。Inspector・保存・Undo はいつも元の値を見る）")) {
		state.isPreview = !state.isPreview;
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();
}

// ===== 上の並び =====
void DrawToolbar(Timeline& timeline) {
	DrawTransport();
	// --- 再生位置 ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kTimeFieldWidth);
	float time = state.time;
	if (ImGui::DragFloat("##time", &time, 0.01f, 0.0f, state.duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp)) {
		state.SeekTo(time);
	}
	ImGui::SetItemTooltip("再生位置（秒）");
	// --- 長さ ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kLengthFieldWidth);
	if (ImGui::DragFloat("##length", &timeline.length, 0.01f, 0.0f, Curve::kMaxValue, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("長さ（秒）。クリップがこれより長ければ、クリップの終わりまで再生する");
	// --- スナップ ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kSnapFieldWidth);
	const auto current = std::ranges::find(kSnapChoices, state.snapStep, &SnapChoice::step);
	if (ImGui::BeginCombo("##snap", (current != kSnapChoices.end()) ? current->name : "Off")) {
		for (const SnapChoice& choice : kSnapChoices) {
			if (ImGui::Selectable(choice.name, choice.step == state.snapStep)) {
				state.snapStep = choice.step;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かすときに目盛りへそろえる間隔（Ctrl を押している間はそろえない）");
	ImGui::SameLine();
	if (ImGui::Button("Fit")) {
		state.needsFit = true;
	}
	ImGui::SetItemTooltip("全体が見える範囲にする（F）");
	// --- 開いている物 ---
	ImGui::SameLine();
	const Entity* owner = EntityManager::Get(state.owner);
	const std::string& path = TimelineManager::GetPath(state.handle);
	ImGui::TextDisabled("%s  /  %s", ToUtf8(ToPath(path).filename()).c_str(), (owner != nullptr) ? owner->name.c_str() : "再生する Entity なし");
	ImGui::SetItemTooltip("%s", (owner != nullptr) ? path.c_str() : "Timeline の欄でこのファイルを選んだ Entity を Hierarchy で選ぶと、プレビューできる");
}

// ===== 詳細との境目（つまんで高さを変える）=====
void DrawSplitter(float rowsHeight, float maxRowsHeight) {
	ImGui::InvisibleButton("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, kSplitterHeight));
	const bool isHovered = ImGui::IsItemHovered();
	const bool isActive = ImGui::IsItemActive();
	if (isHovered || isActive) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (isActive) {
		// 見えている高さから動かす（トラックが少なくて上限より低く出ていても、すぐ動く）
		rowsLimit = std::clamp(rowsHeight + ImGui::GetIO().MouseDelta.y, kMinRowsHeight, maxRowsHeight);
	}
	// --- つまめる場所だと分かるように線を引く ---
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const float lineY = (min.y + max.y) * 0.5f;
	const ImU32 lineColor = ImGui::GetColorU32(isActive ? ImGuiCol_SeparatorActive : (isHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
	ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, lineY), ImVec2(max.x, lineY), lineColor, 2.0f);
}

// ===== 操作が終わったら、保存して Undo に積む =====
void CommitEdit() {
	if (!state.hasEdit || ImGui::IsAnyItemActive()) {
		return;
	}
	state.hasEdit = false;
	std::string after = TimelineManager::ToText(state.handle);
	if (after == state.committedText) {
		return;
	}
	TimelineManager::Save(state.handle);
	const auto apply = [handle = state.handle](const std::string& text) { return TimelineManager::ApplyText(handle, text) && TimelineManager::Save(handle); };
	EditorHistory::Record([apply, before = std::move(state.committedText)] { return apply(before); }, [apply, value = after] { return apply(value); });
	state.committedText = std::move(after);
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
void TimelineWindow::Draw() {
	ImGui::SetNextWindowSize(ImVec2(kFirstWidth, kFirstHeight), ImGuiCond_FirstUseEver);
	if (std::exchange(needsFocus, false)) {
		ImGui::SetNextWindowFocus();
	}
	// ウィンドウの中のスクロールは部品ごとに持つ。隠れていると Begin が false で何も頼まないので、プレビューは止まり値は元のまま
	if (ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		ResolveOwner();
		SyncRevision();
		DrawSourceBar();
		if (Timeline* timeline = state.GetTimeline()) {
			UpdateTime(*timeline);
			DrawToolbar(*timeline);
			DrawTimelineRuler(state);
			// --- 一覧は行の分だけ（1行は空けて、足す場所を残す）。残りは全部、詳細に使う ---
			const float available = ImGui::GetContentRegionAvail().y;
			const float maxRowsHeight = (std::max)(available - kMinDetailsHeight - kTimelineScrollbarHeight - kSplitterHeight, kMinRowsHeight);
			const float fitHeight = static_cast<float>(timeline->tracks.size() + 1) * kTimelineRowHeight;
			const float rowsHeight = std::clamp((std::min)(rowsLimit, fitHeight), kMinRowsHeight, maxRowsHeight);
			DrawTimelineRows(state, rowsHeight);
			DrawTimelineScrollbar(state);
			DrawSplitter(rowsHeight, maxRowsHeight);
			DrawTimelineDetails(state, ImGui::GetContentRegionAvail().y);
			CommitEdit();
		} else if (!state.sources.empty()) {
			ImGui::TextDisabled("この欄にはまだファイルがありません。New で作るか、Inspector で .timeline を選ぶ");
		} else {
			ImGui::TextDisabled("Hierarchy で Timeline の欄（Timeline・UI Screen・UI State）のある Entity を選ぶか、Project で .timeline をダブルクリックする");
		}
	}
	ImGui::End();
}

void TimelineWindow::Open(const std::filesystem::path& filePath) {
	const uint32_t handle = TimelineManager::Load(filePath);
	if (handle == 0) {
		return;
	}
	if (handle != state.handle) {
		OpenFile(handle);
	}
	// 同じファイルを欄で選んでいる Entity があれば、それをプレビューに使う
	state.ownerId = 0;
	state.source = -1;
	state.isExplicitOwner = false;
	state.backStack.clear();
	const auto findOwner = [handle](Handle<Entity> entity) {
		if (state.ownerId != 0) {
			return;
		}
		const std::vector<TimelineSource> sources = CollectSources(entity);
		const auto found = std::ranges::find(sources, handle, &TimelineSource::handle);
		if (found != sources.end()) {
			state.ownerId = EntityManager::Get(entity)->id;
			state.source = static_cast<int>(found - sources.begin());
		}
	};
	EntityManager::ForEach<TimelineComponent>([&](Handle<Entity> entity, TimelineComponent&) { findOwner(entity); });
	EntityManager::ForEach<UIScreenComponent>([&](Handle<Entity> entity, UIScreenComponent&) { findOwner(entity); });
	EntityManager::ForEach<UIStateComponent>([&](Handle<Entity> entity, UIStateComponent&) { findOwner(entity); });
	needsFocus = true;
}
#endif
```

## パートF 見本（ゲーム側）

テクスチャを使わず、Vector Shape・Text と Timeline（Tween・Preset・ベジェのカーブ）だけで作った HUD と画面の切り替え。エンジンには入れない。`Docs/Tasks/UIAnimationTest/` にある物を AL4_01 へ写す。

### F-1 置き場所

| Docs/Tasks/UIAnimationTest/ | AL4_01 の写し先 |
|---|---|
| `scenes/UIAnimationTest.scene.json` | `project/Resources/scenes/` |
| `ui/UIAnimMenu.uiscreen`・`ui/UIAnimSettings.uiscreen` | `project/Resources/ui/` |
| `timelines/UIPresets/`（10 ファイル） | `project/Resources/timelines/UIPresets/` |
| `timelines/UIAnimationTest/`（8 ファイル） | `project/Resources/timelines/UIAnimationTest/` |
| `UIAnimationTester.cpp` | `project/Source/Scripts/UIAnimationTest/`（AL4_01.vcxproj に足す） |

Debug・Development は `Parameters/Editor/LastScene.json` を `UIAnimationTest` にするか、Project で開く。Release は `main.cpp` の `Engine::Initialize(config, "GameScene")` を一時的に `"UIAnimationTest"` にする。

### F-2 中身

**シーン（UIAnimationTest）**

| Entity | 中身 |
|---|---|
| Background | 画面いっぱいの四角（Radial のグラデーション） |
| HUD | Timeline Component（HUDIntro。Play On Start・Game） |
| HUD/TopBar | 平行四辺形とタイトルの文字 |
| HUD/Gauge/GaugeFill | Linear のゲージ（1/2/3 のキーで Parameter "hp" に動く） |
| HUD/Ring・HUD/RealRing | Radial のリング。同じ Spin.timeline を Loop で再生。Ring は Game、RealRing は Real（メニューを出すと Ring だけ止まる） |
| HUD/MenuButton | UI State（Normal・Hover・Pressed、Click：Open Screen `UIAnimMenu`） |
| Tester | UIAnimationTester（1/2/3：HP、4：Pulse、5：Pulse の対象を替える、M：メニュー） |

**画面**

| 画面 | Show | Hide | ボタン |
|---|---|---|---|
| UIAnimMenu（ゲームを止める） | Dim を UIFadeIn・Wipe が横切る（Tween）・Panel を UIPopIn・ボタンを 0.06 秒ずつずらして UISlideInLeft | Current → 0（Dim・Panel の透明度）、Panel を 0.9 倍、Wipe を右へ | Resume（Close Own Screen）・Settings（Switch Screen `UIAnimSettings`）・Locked（Interactable を外して Disabled） |
| UIAnimSettings（ゲームを止める） | Dim を UIFadeIn・Panel を UISlideInRight・メーターを 0 → 元の値（Tween） | Dim を Current → 0・Panel を UISlideOutLeft | Back（Switch Screen `UIAnimMenu`） |

**プリセット（`timelines/UIPresets/`）**：どれも Target が空（self）の Tween で、Add・Multiply・Current で書いてある。

| ファイル | 動き |
|---|---|
| UIFadeIn | 透明度 Multiply 0 → 1 |
| UIPopIn | 大きさ Multiply 0.85 → 1（OutBack）＋ UIFadeIn |
| UISlideInLeft・Right・Top | 位置 Add で外から → Add 0 ＋ フェード |
| UISlideOutLeft | 位置 Current → Add (-500)・透明度 Current → 0 |
| ButtonNormal・Hover・Press・Disabled | 大きさと色を Current → Multiply（1・1.08/1.35・0.94/0.75・1/0.45） |

### F-3 `Source/Scripts/UIAnimationTest/UIAnimationTester.cpp`（新しいファイル）

ゲージと Pulse は「いつ・どの値で」再生するかだけをコードで決める例。メニューの開け閉めはボタン（UI State）だけでできるので、コードは要らない。

```cpp
#include <HTTEngine/ECS/EntityManager.h>
#include <HTTEngine/ECS/Script.h>
#include <HTTEngine/Input/InputManager.h>
#include <HTTEngine/Systems/Animation/TimelineSystem.h>
#include <HTTEngine/UI/UIScreen.h>

// UI アニメーションの見本を、キーで動かす Script（UIAnimationTest シーンの Tester に付ける）
// 1 / 2 / 3：HP のゲージを 100% / 50% / 15% へ動かす（Tween の Parameter "hp" に渡す）
// 4：Pulse（HUD の Gauge を一度ふくらませる）/ 5：Pulse の対象を Gauge ⇔ Ring に替える / M：メニューを出す
// ゲージ・Pulse の動きは .timeline に書いてあり、ここは「いつ・どの値で」再生するかだけ

namespace {
constexpr const char* kMenuScreen = "UIAnimMenu"; // M で出す画面
constexpr const char* kPulseTarget = "Gauge";     // Pulse.timeline のトラックの Target に書いてある文字
} // namespace

class UIAnimationTester : public Script {
public:
	void Update() override {
		// --- HP：Parameter に渡してから再生する（同じ層で再生し直すと、今の値から続けて動く）---
		if (InputManager::IsKeyTriggered(DIK_1)) {
			PlayGauge(1.0f);
		}
		if (InputManager::IsKeyTriggered(DIK_2)) {
			PlayGauge(0.5f);
		}
		if (InputManager::IsKeyTriggered(DIK_3)) {
			PlayGauge(0.15f);
		}
		// --- Pulse：HUD を再生する Entity にして、Script の層で再生する ---
		const Handle<Entity> hud = EntityManager::FindByName("HUD");
		if (InputManager::IsKeyTriggered(DIK_4)) {
			TimelineSystem::PlayLayer(hud, TimelineLayer::Script, {.timeline = pulseTimeline_});
		}
		// --- 対象の差し替え：Target が "Gauge" のトラックに、Ring を動かさせる ---
		if (InputManager::IsKeyTriggered(DIK_5)) {
			isPulseOnRing_ = !isPulseOnRing_;
			TimelineSystem::SetTargetOverride(hud, kPulseTarget, isPulseOnRing_ ? EntityManager::FindByName("Ring") : Handle<Entity>{});
		}
		// --- メニュー（MENU のボタンと同じ。開いている間はゲームが止まり、この Script は呼ばれない）---
		if (InputManager::IsKeyTriggered(DIK_M)) {
			UIScreen::Open(kMenuScreen);
		}
	}

	void Fields(ComponentUI& ui) override {
		ui.Label("1/2/3：HP / 4：Pulse / 5：Pulse の対象を替える / M：メニュー");
		ui.AssetField("Gauge Timeline", gaugeTimeline_, AssetType::Timeline, Tip("ゲージを動かす Timeline（Tween の To が Parameter \"hp\"）"));
		ui.AssetField("Pulse Timeline", pulseTimeline_, AssetType::Timeline, Tip("HUD の Gauge をふくらませる Timeline"));
	}

private:
	// ゲージを value へ動かす
	void PlayGauge(float value) {
		const Handle<Entity> gauge = EntityManager::FindByName("GaugeFill");
		TimelineSystem::SetParameter(gauge, "hp", value);
		TimelineSystem::PlayLayer(gauge, TimelineLayer::Script, {.timeline = gaugeTimeline_});
	}

	uint32_t gaugeTimeline_ = 0; // ゲージの Timeline
	uint32_t pulseTimeline_ = 0; // Pulse の Timeline
	bool isPulseOnRing_ = false; // Pulse の対象を Ring に替えている
};
SCRIPT(UIAnimationTester, "UI");
```

## G 確認

### G-1 ビルド

Debug・Development・Release をビルドして、エラーと（今回写したファイルの）警告が 0 か見る。

### G-2 動かして見る

| # | すること | 見る所 |
|---|---|---|
| 1 | AL4_01 で UIAnimationTest を開いて Play | 上の帯・ゲージ・リング・MENU が順に出る。外のリングと内のリングが回る |
| 2 | 1・2・3 を押す | ゲージが今の長さから 100%・50%・15% へなめらかに動く（途中で別のキーを押しても飛ばない） |
| 3 | 4 を押す・5 を押してから 4 | ゲージが一度ふくらむ。5 の後はリングがふくらむ |
| 4 | MENU にマウスを乗せる・離す | 少し大きく明るくなる・戻る。乗せたり離したりを速くくり返しても、大きさが積み重ならない |
| 5 | MENU を押したまま外へ出して離す | 押した形から戻り、メニューは出ない |
| 6 | MENU をクリック | 後ろが暗くなり、帯が横切り、パネルとボタンが順に入る。外のリング（Game）は止まり、内のリング（Real）は回り続ける |
| 7 | LOCKED に乗せる | 反応しない（暗いまま） |
| 8 | SETTINGS をクリック | メニューが消えながら設定が右から入る。メーターが 0 から伸びる。ゲームは止まったまま |
| 9 | BACK をクリック | 設定が左へ抜けて、メニューが入り直す |
| 10 | メニューが出ている途中（0.3 秒以内）に RESUME | 出かけた形からそのまま消える（一度出きらない）。ゲームが動き出す |
| 11 | メニューを出して Stop・もう一度 Play | 最初の状態から始まる（暗いまま・止まったままにならない） |
| 12 | 停止中に Project の `ui/UIAnimMenu.uiscreen` をダブルクリック | Timeline ウィンドウが `UI Screen / Show (MenuShow.timeline)` を開く |
| 13 | 再生位置をつまむ | Scene ビューにその時間の形が出る。Inspector の値は変わらない |
| 14 | 一覧で `UI Screen / Hide` を選び、つまむ | 消える動きが見える |
| 15 | Panel In のクリップを選んで Open Preset | `UIPopIn.timeline / Panel` が開き、つまむと Panel だけが動く。Back で MenuShow に戻る |
| 16 | Hierarchy で Resume を選ぶ | 一覧が `UI State / Normal (ButtonNormal.timeline)` になる。Hover を選んでつまむと、ホバーの形が見える |
| 17 | 一覧で `UI State / Selected (none)` を選んで New | `resources/timelines/Resume_Selected.timeline` ができて、Inspector の Selected に入る。Ctrl+Z で外れ、Ctrl+Y で戻る |
| 18 | Cancel で画面の編集を終える | `.uiscreen` は元のまま（17 で作った `.timeline` のファイルは残るので、要らなければ消す） |
| 19 | 今の Motion の画面（AL4_01 の `Test.uiscreen` など）を開く | 今までどおり動く |

## H Motion の画面を Timeline へ移す（しなくてもよい）

Motion は残したので、移さなくても動く。Motion では書けない動き（要素ごとに違う動き・図形の値・カーブ）にしたい画面だけ移す。

1. Edit UI Screen で画面を開き、一番上の Entity を選ぶ。
2. 直下の子に Alpha Group が無ければ付ける（Motion は実行中に自動で付けていたが、Timeline は付けない）。
3. Timeline ウィンドウで `UI Screen / Show` を選んで New。直下の子ごとに PresetTrack を足して、下の表のプリセットを、Stagger の秒ずつずらして置く。
4. Hide も同じ（From が Current のプリセットを使う）。
5. 再生位置をつまんで形を確かめ、Done で保存する。Inspector の Motion の欄は灰色になる（値は残る。Timeline を外せば Motion に戻る）。

| Motion | 代わりのプリセット |
|---|---|
| Fade | UIFadeIn（消すときは、Alpha Group の alpha を Current → Value 0 にした Tween） |
| Slide（Bottom・Distance 60） | UISlideInLeft を写して、Add を (0, -60, 0) にした物 |
| Scale（From Scale 0.6） | UIPopIn を写して、Multiply を 0.6 にした物 |
| Stagger 0.05 | Preset のクリップを 0.05 秒ずつずらす |
| UI Screen Element の Override・Delay | その子だけ別のプリセット・ずらす秒を変える |

## I 検証の結果（写しで確かめた）

写し（AL4_01 の写しのサブモジュールを cb53d90 にそろえた物）で、答えのコードを入れて確かめた。

| 確かめたこと | 結果 |
|---|---|
| Debug・Development・Release のビルド | エラー 0、今回のファイルの警告 0 |
| 見本を決まった入力で 422 フレーム動かした記録（マウスの位置とボタンを写しだけの仕掛けで流し、時間を 1/60 秒に固定） | Debug と Release で全行一致。ゲージ・ホバー・押す・クリックで画面を出す・ポーズで Game のリングだけ止まる・切り替え・出している途中に閉じる・出した直後に閉じる・Motion の画面を途中で閉じる・画面を出している途中のシーンの切り替え |
| カメラ段階2 の記録（840 フレーム） | 前の記録と全フレーム一致（Timeline Component の再生の順は変わっていない） |
| エディタ（Development、ImGui へ入力を流した） | UI Screen の編集で Show が開く・つまむとプレビュー・Inspector は元の値・Hide へ切り替え・Resume の欄・New → Ctrl+Z → Ctrl+Y・Preset と Tween の詳細・Open Preset → プレビュー → Back |
| ログ | 警告・エラー無し |

**確かめる途中で見つけて直した物：**

- 最初のフレームの当たり：ワールド行列がまだ無く、画面の真ん中にマウスがあると Hover になった → 最初のフレームはマウスを見ない。
- Switch Screen で1フレームだけゲームが動いた → 出す方を先に頼み、Waiting の画面は今の止め方のまま数える（1.6）。
- New で作るファイルの名前が `Resume_UI_State___Selected` になった → 欄の最後の名前だけ使う（`Resume_Selected`）。
- Tween の詳細に Ease Preset が2つ出た（カーブの部品にもともとある）→ 足した方を消した。

**確かめていない物：**

- 本物のマウスでの操作（G-2 の全部）。写しでは、マウスの位置とボタンを仕掛けで流した。
- AL4_01 の実物での実行。
- Image を使った要素の UI State（当たりは今ある `SpriteRenderSystem::IsMouseOver`。見本は Vector Shape だけ）。
- Script の `UIStateSystem::IsClicked`・`SetSelected`・`SetVisible`（コンパイルだけ。見本はクリックの動きだけを使った）。
- Event のトラックを入れたプリセット（範囲の直し方はコードを読んで確かめただけ）。
- 自分を指すプリセット（読み込みの止め方はコードを読んで確かめただけ）。

## パートJ Timeline Trigger（範囲に入ったら Timeline を再生する）

2026-11-02 に足した。A〜E を写した後に写す（A〜E の中身は変えていない）。

Collider の範囲に入ったら・出たら、指定の Timeline を再生する Component。近づいたら扉が開く・看板が出る・ボス部屋に入ったらカメラ演出、を Script を書かずに置ける。

### J-1 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 置き方 | 新しい Component（Gameplay の Timeline Trigger）。Trigger Activator には足さない | Trigger Activator は「Target を出す・消す＋フェード」専用。Timeline の項目を混ぜると、使わない項目が増えて分かりにくい |
| 範囲と相手 | Trigger Activator と同じ（同じ Entity の Collider・Source は FollowTarget / Entity / AnyRigidbody） | 判定は `TriggerActivatorSystem::IsSourceInside` に切り出して、2つの Component で共有する（同じ処理を2回書かない） |
| 再生の仕方 | `PlayLayer(Target, TimelineLayer::Trigger, …)`。Target が空なら自分 | Script の層と分けた（同じ層だと、ゲームのコードの再生と置き換え合う）。並びは Script の後・UIState の前 |
| 出たとき | 出たときの Timeline を再生する（同じ層なので、入ったときの再生は置き換わる）。選んでいなければ何もしない（入ったときの再生は最後まで流れる） | 出たときの Tween を From = Current にすれば、入ったときの動きの途中で出ても、その値から戻る |
| Once | 入ったとき・出たときを、それぞれ1回だけ | Trigger Activator と同じ |
| 動き出すフレーム | 入ったフレームの次のフレームから | 当たり判定（`CollisionSystem::Update`）は `TimelineSystem::Update` の後なので。Trigger Activator のすぐ後に呼ぶ |
| シーンの物の指し方 | 再生する Entity の外の物は `/` で始まるパス（`/Door`） | カメラ段階2 の決まりのまま。範囲の Entity を Target にしたまま、シーンのどの物でも動かせる |

### J-2 `Systems/Animation/TimelineSystem.h`

`TimelineLayer` の `Script` の下に足す（`UIState` の行は、コメントの位置をそろえるだけ）。

```cpp
	Script,       // ゲームのコードから PlayLayer で再生した物
	Trigger,      // Timeline Trigger（範囲に入った・出た）
	UIState,      // UI State の Normal・Hover・Pressed・Selected・Disabled
```

### J-3 `Systems/Gameplay/TriggerActivatorSystem.h`

include に足す。

```cpp
#include "HTTEngine/Components/Gameplay/TriggerActivatorComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
```

`Update` の下に足す。

```cpp
	static void Update(bool isGameRunning, float deltaTime);

	/// <summary>
	/// 今、反応する相手が entity の Collider の範囲の中にいるか（Enter・Stay の当たりに、相手が1つでもあれば中）。Timeline Trigger も使う
	/// </summary>
	/// <param name="source">誰が入ったら反応するか</param>
	/// <param name="sourceEntity">source が Entity のときの相手</param>
	static bool IsSourceInside(Handle<Entity> entity, TriggerSource source, EntityRef sourceEntity);
```

### J-4 `Systems/Gameplay/TriggerActivatorSystem.cpp`

`IsSource` が Component ではなく `source`・`sourceEntity` を受け取るようにして、無名名前空間の `IsSourceInside` を消す（下の public の関数へ移す）。

```cpp
// other が、反応する相手か
bool IsSource(TriggerSource source, EntityRef sourceEntity, Handle<Entity> other) {
	switch (source) {
	case TriggerSource::FollowTarget:
		return IsSelfOrDescendant(other, CameraSystem::GetFollowTargetEntity());
	case TriggerSource::Entity:
		return IsSelfOrDescendant(other, EntityManager::Find(sourceEntity));
	case TriggerSource::AnyRigidbody:
		return EntityManager::Get<RigidbodyComponent>(other) != nullptr;
	}
	return false;
}
```

`Release` の下（「毎フレーム」の大見出しの上）に足す。

```cpp
//=============================================================================
// 範囲に入ったか
//=============================================================================
bool TriggerActivatorSystem::IsSourceInside(Handle<Entity> entity, TriggerSource source, EntityRef sourceEntity) {
	// Exit の知らせを待たずに毎フレーム数え直すので、中にいた相手が消えても（Exit が来なくても）、次のフレームには「出た」になる
	return std::ranges::any_of(CollisionSystem::GetHits(entity), [&](const CollisionHit& hit) { return hit.phase != HitPhase::Exit && IsSource(source, sourceEntity, hit.other); });
}
```

`Update` の中の呼び方を直す。

```cpp
			const bool isInside = IsSourceInside(entity, activator.source, activator.sourceEntity);
```

### J-5 `Components/Gameplay/TimelineTriggerComponent.h`（新しいファイル）

実行中だけの値（`isInside` など）も Component に持つのは Trigger Activator と同じ（Stop で、Play を押した瞬間の値に戻る）。

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Components/Gameplay/TriggerActivatorComponent.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Time/EngineTime.h"

// 同じ Entity の Collider の範囲に入ったら・出たら、Timeline を再生する。Entityに付けるComponent（カテゴリ：Gameplay）
// 範囲・誰が入ったら反応するかは Trigger Activator と同じ（Collider は Is Trigger にする）。Script を書かずに、エディタだけで演出を置ける
// 再生は TimelineSystem::PlayLayer（TimelineLayer::Trigger）。Target を再生する Entity にするので、トラックの対象は Target からのパス
// 動かすのは TimelineTriggerSystem（再生中だけ）。停止中は何もしない

/// <summary>
/// 範囲に入ったら・出たら、Timeline を再生する（Unity なら、OnTriggerEnter で PlayableDirector.Play を呼ぶ Script を書く所）
/// </summary>
struct TimelineTriggerComponent {
	bool enabled = true;                                // false の間は反応しない（再生中の Timeline は最後まで流れる）
	EntityRef target;                                   // 再生する Entity（Hierarchy からドラッグ。空なら自分）
	TriggerSource source = TriggerSource::FollowTarget; // 誰が入ったら反応するか
	EntityRef sourceEntity;                             // source が Entity のとき：反応する相手
	uint32_t enterTimeline = 0;                         // 入ったときに再生する .timeline（0 なら何もしない）
	uint32_t exitTimeline = 0;                          // 出たときに再生する .timeline（0 なら何もしない。入ったときの再生は最後まで流れる）
	bool once = false;                                  // true：入ったとき・出たときの再生を、それぞれ1回だけにする
	TimeMode timeMode = TimeMode::Game;                 // 再生に使う時間（Real：ポーズ・スローでも動く）

	// --- 実行中だけの値（TimelineTriggerSystem が書く。保存しない）---
	bool isInside = false;    // 前のフレームに、Source が範囲の中にいたか（今と比べて、入った・出たを決める）
	bool isEnterUsed = false; // 入ったときの再生をしたか（Once のとき、2回目をしないため）
	bool isExitUsed = false;  // 出たときの再生をしたか（同じ）
};

static_assert(std::is_trivially_copyable_v<TimelineTriggerComponent>);
```

### J-6 `Systems/Gameplay/TimelineTriggerSystem.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Components/Gameplay/TimelineTriggerComponent.h"

/// <summary>
/// Timeline Trigger を登録し、範囲に入った・出たを見て、Timeline の再生を頼む
/// <para>入ったかどうかは Trigger Activator と同じ（TriggerActivatorSystem::IsSourceInside）。再生は TimelineLayer::Trigger なので、入る・出るをくり返すと前の再生は置き換わる</para>
/// <para>出たときの Timeline の Tween を From = Current にしておけば、入ったときの動きの途中で出ても、その値から戻る</para>
/// </summary>
class TimelineTriggerSystem {
public:
	// TimelineTriggerComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 入った・出たを見て、Timeline の再生を頼む（再生中だけ。停止中は何もしない）
	/// <para>CollisionSystem::Update の後に呼ぶ（そのフレームの当たりで決める。値が動くのは、次のフレームの TimelineSystem::Update から）</para>
	/// </summary>
	/// <param name="isGameRunning">再生中か</param>
	static void Update(bool isGameRunning);
};
```

### J-7 `Systems/Gameplay/TimelineTriggerSystem.cpp`（新しいファイル）

```cpp
#include "TimelineTriggerSystem.h"

#include <memory>

#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"
#include "HTTEngine/Systems/Gameplay/TriggerActivatorSystem.h"

namespace {
//=============================================================================
// 再生する
//=============================================================================
// 入ったとき・出たときの再生。Once なら、それぞれ1回だけ（isUsed に、したことを覚える）。timeline が 0 なら何もしない
void React(uint32_t timeline, bool& isUsed, const TimelineTriggerComponent& trigger, Handle<Entity> owner) {
	if (timeline == 0 || (trigger.once && isUsed)) {
		return;
	}
	isUsed = true;
	TimelineSystem::PlayLayer(owner, TimelineLayer::Trigger, {.timeline = timeline, .timeMode = trigger.timeMode});
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeTimelineTrigger(ComponentUI& ui, TimelineTriggerComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと反応しない（再生中の Timeline は最後まで流れる）"));
	ui.Label("範囲は同じ Entity の Collider（Is Trigger にする）。再生中だけ動く");
	ui.Field("Target###target", value.target, Tip("Timeline を再生する Entity（Hierarchy からドラッグ）。トラックの対象は、ここからのパス。空なら自分"));
	// ===== 誰が入ったら =====
	ui.Separator("Source");
	ui.Field(
	    "Source###source", value.source,
	    Tip("誰が入ったら反応するか。FollowTarget：カメラが追っている物（プレイヤー） / Entity：Source Entity に入れた物 / AnyRigidbody：Rigidbody を付けた物なら、どれでも"));
	// --- 選んだ相手の値だけを見せる（Trigger Activator と同じ）---
	switch (value.source) {
	case TriggerSource::FollowTarget:
		break; // 選ぶ物は無い（メインカメラの Camera Follow の Target を使う）
	case TriggerSource::Entity:
		ui.Field("Source Entity###sourceEntity", value.sourceEntity, Tip("反応する相手（Hierarchy からドラッグ）。子に付けた Collider も、この Entity として数える"));
		break;
	case TriggerSource::AnyRigidbody:
		break; // 選ぶ物は無い
	}
	// ===== 再生する Timeline =====
	ui.Separator("Timeline");
	ui.AssetField("On Enter###enterTimeline", value.enterTimeline, AssetType::Timeline, Tip("入ったときに再生する Timeline（選ばなければ何もしない）"));
	ui.AssetField("On Exit###exitTimeline", value.exitTimeline, AssetType::Timeline,
	              Tip("出たときに再生する Timeline（選ばなければ何もしない。入ったときの再生は最後まで流れる）。Tween の From を Current にすると、途中で出てもその値から戻る"));
	ui.Field("Once###once", value.once, Tip("入ったとき・出たときの再生を、それぞれ1回だけにする（一度見せた演出は、もう流さない）"));
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("Game：ポーズ・スロー・ヒットストップで止まる / Real：ゲームの時間が止まっても動く"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void TimelineTriggerSystem::Initialize() {
	// Timeline Trigger の置き場所・保存・Inspector を登録する（見せ方は DescribeTimelineTrigger 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<TimelineTriggerComponent>();
	ComponentSerializer::Register<TimelineTriggerComponent>("Timeline Trigger", &DescribeTimelineTrigger);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<TimelineTriggerComponent>>("Timeline Trigger", "Gameplay", &DescribeTimelineTrigger));
}

void TimelineTriggerSystem::Release() {} // 持っている物は無い（実行中の値は、全部 Component の中。再生は TimelineSystem が持つ）

//=============================================================================
// 毎フレーム
//=============================================================================
void TimelineTriggerSystem::Update(bool isGameRunning) {
	if (!isGameRunning) {
		return; // 停止中は何もしない（Stop で、Play を押した瞬間の状態に戻る）
	}
	EntityManager::ForEach<TimelineTriggerComponent>([](Handle<Entity> entity, TimelineTriggerComponent& trigger) {
		// --- 入った・出たを見る（Enabled を外した・Entity が無効の間は見ない）---
		if (!trigger.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const Handle<Entity> target = EntityManager::Find(trigger.target);
		const Handle<Entity> owner = target.IsValid() ? target : entity; // 空なら自分
		const bool isInside = TriggerActivatorSystem::IsSourceInside(entity, trigger.source, trigger.sourceEntity);
		if (isInside && !trigger.isInside) {
			React(trigger.enterTimeline, trigger.isEnterUsed, trigger, owner);
		} else if (!isInside && trigger.isInside) {
			React(trigger.exitTimeline, trigger.isExitUsed, trigger, owner);
		}
		trigger.isInside = isInside;
	});
}
```

### J-8 `Systems/EngineSystems.cpp`

include（Gameplay のかたまり）に足す。

```cpp
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"
#include "HTTEngine/Systems/Gameplay/TimelineTriggerSystem.h"
#include "HTTEngine/Systems/Gameplay/TriggerActivatorSystem.h"
```

`Initialize`・`Release` の Trigger Activator の下に足す。

```cpp
	TriggerActivatorSystem::Initialize(); // Trigger Activator
	TimelineTriggerSystem::Initialize();  // Timeline Trigger（範囲に入ったら Timeline を再生する）
```

```cpp
	TriggerActivatorSystem::Release();
	TimelineTriggerSystem::Release();
```

`Update` の `TriggerActivatorSystem::Update` の下に足す。

```cpp
	TriggerActivatorSystem::Update(isGameRunning, Time::GetDeltaTime()); // 範囲に入った・出たで、Target を出す・消す（当たり判定の後。入ったフレームのうちに出し始める）
	TimelineTriggerSystem::Update(isGameRunning);                        // 範囲に入った・出たで、Timeline の再生を頼む（当たり判定の後。値が動くのは次のフレームの TimelineSystem::Update から）
```

新しい3つのファイルは、ソリューションエクスプローラーで HTTEngine に足す。

### J-9 見本（ゲーム側）

`Docs/Tasks/UIAnimationTest/` から AL4_01 へ写す。

| Docs/Tasks/UIAnimationTest/ | AL4_01 の写し先 |
|---|---|
| `scenes/TimelineTriggerTest.scene.json` | `project/Resources/scenes/` |
| `timelines/UIAnimationTest/SignEnter.timeline`・`SignExit.timeline` | `project/Resources/timelines/UIAnimationTest/` |
| `TimelineTriggerTester.cpp` | `project/Source/Scripts/UIAnimationTest/`（AL4_01.vcxproj に足す） |

| Entity | 中身 |
|---|---|
| Player | 緑の箱・Collider（Box）・TimelineTriggerTester（←→ で横に動く） |
| SignArea | Collider（Box 3×2×3・Is Trigger）・Timeline Trigger（Target 空＝自分、Source：Entity の Player、On Enter：SignEnter、On Exit：SignExit）。子の Marker は床の黄色い印 |
| Door | 橙の箱（SignEnter で 3 m 上がる） |
| SignPanel | 画面の上の角丸の四角と文字（Alpha Group の Alpha 0 で置いておく） |

SignEnter・SignExit は、再生する Entity が SignArea なので、扉とパネルは `/Door`・`/SignPanel`（シーンの一番上からのパス）で指している。

| Timeline | トラック |
|---|---|
| SignEnter | `/Door` の位置 Current → Add (0, 3, 0)・`/SignPanel` の透明度 Current → Value 1・大きさ Multiply 0.85 → 1（OutBack） |
| SignExit | `/Door` の位置 Current → Add (0, 0, 0)・`/SignPanel` の透明度 Current → Value 0・大きさ Current → Multiply 0.9 |

```cpp
#include <HTTEngine/ECS/Script.h>
#include <HTTEngine/Input/InputManager.h>
#include <HTTEngine/Time/EngineTime.h>

// Timeline Trigger の見本（TimelineTriggerTest シーンの Player に付ける）。←→ で自分を横に動かすだけ
// 範囲に入った・出たときの演出（扉・パネル）は、SignArea の Timeline Trigger と .timeline に書いてあり、ここには書かない

class TimelineTriggerTester : public Script {
public:
	void Update() override {
		float direction = 0.0f;
		if (InputManager::IsKeyPressed(DIK_LEFT)) {
			direction -= 1.0f;
		}
		if (InputManager::IsKeyPressed(DIK_RIGHT)) {
			direction += 1.0f;
		}
		transform.translation.x += direction * moveSpeed_ * Time::GetDeltaTime();
	}

	void Fields(ComponentUI& ui) override {
		ui.Label("←→ で横に動く");
		ui.Field("Move Speed", moveSpeed_, AtLeast(0.0f).Tip("1秒に動く長さ（m）"));
	}

private:
	float moveSpeed_ = 5.0f; // 1秒に動く長さ（m）
};
SCRIPT(TimelineTriggerTester, "UI");
```

### J-10 確認

| # | すること | 見る所 |
|---|---|---|
| 1 | Debug・Development・Release をビルド | エラー・警告 0 |
| 2 | TimelineTriggerTest を開いて Play、→ で黄色い範囲へ入る | 扉が上がり、パネルがポップして出る |
| 3 | ← で範囲から出る | 扉が下がり、パネルが消える |
| 4 | 入ってすぐ出る（扉が上がる途中） | 扉が途中の高さからそのまま下がる（一度上がりきらない） |
| 5 | SignArea の Once を入れて、入る・出る・もう一度入る | 2回目は何も起きない |
| 6 | 前からある Trigger Activator のシーン | 今までどおり出る・消える（判定を切り出しただけ） |

### J-11 検証の結果（写しで確かめた）

| 確かめたこと | 結果 |
|---|---|
| Debug・Development・Release のビルド | エラー 0、警告 0 |
| Player を決まった通りに動かした 462 フレームの記録（ゆっくり入る → 止まる → ゆっくり出る → 入ってすぐ出る） | Debug と Release で全行一致。入ったフレームの次から扉が 1.5 → 4.5 m・パネルが 0 → 1、出ると戻る。入ってすぐ出たときは、扉が 3.61 m からそのまま下がった |
| 段階5 の見本の記録（422 フレーム） | パートJ の前と全行一致（層を1つ足しても、ほかの再生の順は変わらない） |

**確かめていない物：** 本物のキーでの操作（J-10 の 2〜5）、Once、Source が FollowTarget・AnyRigidbody のとき、Trigger Activator の見本（判定を切り出しただけで、コードを読んで確かめた）。
