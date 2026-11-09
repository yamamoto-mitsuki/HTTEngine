> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 13. 時間

```cpp
const float deltaTime = Time::GetDeltaTime();         // 前フレームからの秒（一時停止・スロー・ヒットストップが掛かる）
const float raw = Time::GetUnscaleDeltaTime();        // 何も掛かっていない方（UIの演出などに）
const float real = Time::GetRealDeltaTime();          // スロー・ヒットストップ・ポーズは掛からず、エディタの一時停止だけ 0（カメラの Time Mode が Real のとき）
Time::SetGameTimeScale(0.1f);                         // スロー（1で普通）
Time::HitStop(0.06f);                                 // 0.06秒だけ止める（重ねて呼んだら長い方）
```

`GetDeltaTime()` ＝ 前フレームからの秒 × 一時停止（0か1）× ゲームのスロー ×（ヒットストップ中・ゲームのポーズ中なら0）。`GetDeltaTime(TimeMode::Real)` のように種類を選んでも読める（`Game` か `Real`）。ヒットストップ中かは `Time::IsHitStopping()`。**`deltaTime` を掛けて動かしていれば、ポーズもスローも自分で書かなくても効く**（SYSTEM の最後の `float` 引数も同じ値）。

- 一時停止はエンジン（Pause ボタン）、スローとヒットストップはゲームが決める。別々に持っているので、ゲームのスローがエンジンに書き直されることはない。
- 1フレームの経過時間は **`Time::kMaxDeltaTime`（0.1 秒）で止める**（ブレークポイントで止めて再開しても、物が飛んでいかない）。上限を使う所（物理の「何歩に分けるか」）もこの値を読む。
- ヒットストップは**何も掛けていない時間**で数える（スロー中でも同じ長さだけ止まる）。
- **Play / Stop / Restart・シーンの切り替えで、スローとヒットストップは戻る**（スロー中に Stop しても、エディタはスローにならない）。
- **ポーズ**（`Time::SetGamePaused(true)`・`IsGamePaused()`）：時間が 0 になり、**Script の Update も呼ばれない**（ポーズ画面の操作でプレイヤーが動かないように）。ポーズ画面（Screen の Pause Game）が開いたときに書く。**ポーズ中も、開いている画面の中の Script は呼ばれる**。止めない時間（`GetUnscaleDeltaTime`）で動く物（幕・Text の演出）は止まらない。Play / Stop / Restart・シーンの切り替えで外れる。

```cpp
// ✗ だめな例
transform->translation.z += 0.1f;                    // ✗ フレームレートで速度が変わる／ポーズで止まらない
// ○
transform->translation.z += 5.0f * Time::GetDeltaTime();
```
