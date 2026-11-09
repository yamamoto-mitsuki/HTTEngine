> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 21. カーブと Timeline

時間で値を変える（Curve）・時間に沿って動かす（Timeline）。

## 時間で値を変えるカーブ（Curve）

時間に対して値がどう変わるかを、キーでつないだカーブで表す（`#include <HTTEngine/Curve/CurveManager.h>`）。補間はキーごとに Constant・Linear・Bezier。カーブの端より外は端のキーの値。

```cpp
const Curve* curve = CurveManager::Get(handle);   // .curve のファイルの番号から（選んでいなければ nullptr）
const float value = curve->Evaluate(time);        // 時間の値

Curve bounce = Curve::FromEase(Ease::Type::OutBounce); // Ease と同じ形（時間 0〜1・値 0〜1）
```

- ファイルは Project の **Create → Curve** で作り、選ぶと Inspector で直せる（17章）。Script の `Fields` に `ui.AssetField("Curve", handle, AssetType::Curve);` と書くと選べる。
- ほかのファイルの中に入れるときは `CurveManager::ToJson`・`FromJson` を使う。
- キーは時間の順に並び、値は有限で、ハンドルは隣のキーまでの長さに収まる（書き換えは全部 `Curve` の関数を通る）。同じ時間のキーは値の段差になる。
- 作りと確認の表は [Tasks/Curve.md](../Tasks/Curve.md)。

## 時間に沿って動かす（Timeline）

`.timeline` のファイルに、トラック → クリップ → キーの形で動きを書き、Entity の **Timeline** Component で再生する（Add Component → Animation。`#include <HTTEngine/Systems/Animation/TimelineSystem.h>`）。同じファイルを何体でも同時に再生できる。

```cpp
TimelineSystem::Play(entity);       // 止まっていれば最初から、一時停止中なら続きから
TimelineSystem::Pause(entity);
TimelineSystem::Stop(entity);       // 時間を 0 に戻して、次の Update で 0 の値を書く
TimelineSystem::Seek(entity, 1.5f); // 次の Update でその時間の値を書く。イベントは呼ばない
const TimelineStatus status = TimelineSystem::GetStatus(entity); // state・time・duration・loopCount
```

- **値のトラック：** `component`（Inspector の見出し）と `property`（シーンファイルの項目の名前）で、Component の数の項目を `Curve` で動かす。Script の `Fields` も動かせる。
- **イベントのトラック：** 指定の時間に、対象の Entity の Script の `OnTimelineEvent(const TimelineEvent&)` を呼ぶ。シークでは呼ばない。
- **カメラのトラック・衝撃のトラック：** 写すカメラを時間で切り替える（`TimelineCameraTrack`）・指定の時間にカメラへ衝撃を送って揺らす（`TimelineCameraShakeTrack`。ずっと続く揺れは値のトラックで Camera Shake の Gain を動かす）。[10章の「Timeline でカメラを動かす」](10-Camera.md#timeline-でカメラを動かす)。
- **Tween のトラック：** Component の項目を From → To で動かす（Ease はカーブ）。端は Current（始まったときの今の値）・Value・Add（元の値 ＋）・Multiply（元の値 ×）・Parameter（`SetParameter` で渡す値）。対象を空にして Add・Multiply で書くと、どの要素にも使えるプリセットになる。
- **Preset のトラック：** 別の `.timeline` を、トラックの対象を再生する Entity にして再生する。ずらして並べると順に動く。手順と決まりは [Tasks/UIAnimation.md](../Tasks/UIAnimation.md)。
- **Component を使わずに再生する：** `TimelineSystem::PlayLayer(entity, TimelineLayer::Script, {.timeline = 番号})`。層は Component → Script → Trigger → UIState → UIVisibility → UIScreen の順に書くので、同じ値は後の層が勝つ。実行中の値は `SetParameter(entity, "名前", 値)`、対象の差し替えは `SetTargetOverride(entity, "パス", 別の Entity)`。
- **範囲に入ったら再生する：** **Timeline Trigger**（Add Component → Gameplay）。同じ Entity の Collider（Is Trigger）に Source（Trigger Activator と同じ）が入ったら On Enter、出たら On Exit の Timeline を Target（空なら自分）で再生する（層は Trigger。Once でそれぞれ1回だけ）。[Tasks/UIAnimation.md](../Tasks/UIAnimation.md) のパートJ。
- **対象：** 再生する Entity から子の名前を `/` でつないだパス（`Panel/Title`。空なら自分）。先頭が `/` ならシーンの一番上からのパス（`/Cameras/Close`）。見つからなければ、そのトラックだけ飛ばして警告を1回出す。
- **Time Mode：** Game はスロー・ヒットストップ・ポーズで止まる。Real はゲームの時間が止まっても動く。
- **新しい種類：** `TimelineTrackOf<クリップ>` を継承して `Evaluate` を書き、ゲーム側の `.cpp` に `TIMELINE_TRACK(型, "説明");`。手順と決まりは [Tasks/Timeline.md](../Tasks/Timeline.md)。
- **直す：** Timeline ウィンドウで、トラック・クリップ・キーを直し、停止中に再生位置をつまんで結果を見る。Timeline の付いた Entity を選ぶか、Project で `.timeline` をダブルクリックして開く。使い方は [Tasks/TimelineWindow.md](../Tasks/TimelineWindow.md) の 1.4。ゲーム側のトラックに `GetClipCurves`・`CanPreview` を書くと、キーとプレビューも使える。

