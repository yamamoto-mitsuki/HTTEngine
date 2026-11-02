参考：
- Timeline でのカメラの考え方（並べるとカット、重ねると補間）：[Cinemachine and Timeline](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/concept-timeline.html)
- 線に沿って動くカメラ：[Spline Dolly](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineSplineDolly.html)
- カメラ段階1：[VirtualCamera.md](VirtualCamera.md)
- Timeline の再生・ウィンドウ：[Timeline.md](Timeline.md)・[TimelineWindow.md](TimelineWindow.md)
- 減衰振動の式：https://en.wikipedia.org/wiki/Harmonic_oscillator
- `std::basic_string_view::starts_with`：https://en.cppreference.com/w/cpp/string/basic_string_view/starts_with
- `std::basic_string_view::remove_prefix`：https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix
- `std::optional`：https://en.cppreference.com/w/cpp/utility/optional
- `std::ranges::sort`（プロジェクション。パートF）：https://en.cppreference.com/w/cpp/algorithm/ranges/sort
- `std::span::first`（パートF）：https://en.cppreference.com/w/cpp/container/span/first
- ImGui の `###` の ID：https://github.com/ocornut/imgui/blob/master/docs/FAQ.md#q-about-the-id-stack-system

# カメラ段階2：Timeline でのカメラ演出

カメラ段階1の仕組みを Timeline から動かす。指定の時間にカメラを切り替え、クリップを重ねれば補間し、クリップが終わった後の戻し方も選べるようにする。あわせて、カメラの Transform・FOV・揺れ・ポストエフェクトをキーで動かす方法、線に沿ってカメラを動かす機能、止まらない時間での再生、停止中のプレビューを Game ビューに出す機能を足す。カメラの位置にアイコンが出ないバグも直す。

[TimelineWindow.md](TimelineWindow.md) の後に写す。UI 段階4 は終わっているので、Timeline ウィンドウでの編集とプレビューもこの md に入っている。

パートは **A → B → D の順に写す**（C は消した）。A の後と、B まで写した後で、Debug のビルドと起動を確かめた。D は見本（ゲーム側へ写す）。 E は確認の手順。**F は写した後に見つかったバグの直し（2026-11-02 に追記。写経せず直接入れた）。**

作業の前に、develop から切ったブランチへ切り替える（ブランチは作ってある）。

```bash
git switch feature/CameraTimeline
```

## 0章 全体の形

### 0.1 流れ

```
TimelineSystem::Update（Script の後）
    │
    ├─ TimelineValueTrack ……… カメラの Transform・FOV・Camera Volume・Camera Shake・Camera Path の値を書く
    ├─ TimelineCameraShakeTrack … 指定の時間に CameraSystem::AddImpulse
    └─ TimelineCameraTrack …… 毎フレーム CameraSystem::RequestOverride（from → to を weight で混ぜて）
                                    │ 頼みはそのフレームだけ効く
                                    ▼
CameraSystem::Update
    ・通常のカメラを選ぶ（Priority。今までと同じ）
    ・通常のカメラの値を作る（切り替えの補間も今までと同じ）
    ・頼みがあれば、from・to のカメラの値を作って混ぜる
    ・頼みが無くなったフレームは、最後の頼みに書いてあった戻し方で通常のカメラへ戻す
    │
    ▼
Game ビュー（停止中のプレビューも同じ道を通る）
```

Timeline が Transform を書くのは今までのトラックのまま。増えたのは「どのカメラを写すか」を毎フレーム頼む道だけ。

### 0.2 作る物

| 物 | 役目 |
|---|---|
| `TimelineCameraTrack`（新しいトラック） | クリップごとに写すカメラを決める。重ねると補間。終わった後の戻し方を選べる |
| `TimelineCameraShakeTrack`（新しいトラック） | 指定の時間に衝撃を送る |
| Camera Path（新しい Component） | 線の Entity の子を順に通る線の上に、カメラを置く。Position をキーで動かす |
| `CameraSystem::RequestOverride` | このフレームに写すカメラを頼む |
| `/` で始まるパス | 再生する Entity の外のシーンの Entity を、Timeline から指す |
| `DrawLayer::EditorGizmo`・`CameraGizmo` | Scene ビューだけに、ライトとカメラのアイコン・線を描く |

新しいファイル：

| ファイル | 中身 |
|---|---|
| `Components/Camera/CameraPathComponent.h` | Camera Path の Component |
| `Systems/Camera/CameraPath.h`・`.cpp` | Camera Path の Inspector と線の計算 |
| `Timeline/Camera/TimelineCameraTrack.h`・`.cpp` | カメラのトラック |
| `Timeline/Camera/TimelineCameraShakeTrack.h`・`.cpp` | 衝撃のトラック |
| `Editor/Gizmos/CameraGizmo.h`・`.cpp` | カメラのアイコンと線 |

`Timeline` のフォルダは 16 ファイルあるので、`Timeline/Camera` を新しく作った。

### 0.3 使い方

**カメラを置く。** Hierarchy の ＋ → Camera でカメラを置く。演出に使うカメラは、**通常のカメラより Priority を小さくする**（通常のカメラを 10、演出のカメラを 0 など）。無効にはしない（無効の Entity は Camera Look At・Camera Shake が動かない）。

**結び付ける。** 指し方は2つある。どちらも Timeline の中に書くのは文字のパスで、`.timeline` は同じファイルを何個でも使い回せる。

| カメラの置き場所 | 書き方 | 例 |
|---|---|---|
| Timeline を再生する Entity の子孫（Prefab の中） | 再生する Entity からのパス | `Rig Cam A` |
| それ以外のシーンの中 | `/` で始まる、シーンの一番上からのパス | `/Cameras/Cam Wide` |

ウィンドウでは、どちらのパスも自動で作られる。

| 操作 | 結果 |
|---|---|
| Hierarchy の Camera を、カメラのトラックの列へドラッグ | 落とした時間に、そのカメラのクリップができる |
| Hierarchy の Camera を、カメラのトラックの行へドラッグ | 再生位置に、そのカメラのクリップができる |
| クリップを選んで Camera の一覧から選ぶ | クリップのカメラが替わる（Hierarchy からこの欄へドラッグしてもよい） |
| Hierarchy の Entity を、値のトラックの行へドラッグ | 動かす相手になる（シーンのどの Entity でもよい） |

**カメラの切り替え。** Add Track → `TimelineCameraTrack`。

| やりたいこと | クリップの置き方・設定 |
|---|---|
| カット | クリップをすき間なく並べる |
| 補間 | クリップを重ねる。重なった長さが補間の秒。速さの変え方は後のクリップの Blend Ease |
| 通常のカメラから補間で入る | 最初のクリップの Blend In に秒を書く |
| 通常のカメラへすぐ戻す | Exit を Cut |
| 通常のカメラへ補間で戻す | Exit を Ease にして、Exit Time・Exit Ease を決める（この秒は Timeline の長さに入る） |
| 次のクリップまでこのカメラのまま | Exit を Hold。後ろにクリップが無ければ `TimelineSystem::Stop` まで |

**キーで動かす。** Add Track → `TimelineValueTrack`。

| 動かす物 | Component | Property |
|---|---|---|
| カメラの位置・向き | Transform | translation・rotation |
| FOV | Camera | fovY |
| 線の上の位置 | Camera Path | position |
| ずっと続く揺れの強さ | Camera Shake | noiseGain |
| ポストエフェクト全体の効き具合 | Camera Volume | weight |
| ポストエフェクトの値 | Camera Volume | focusDistance・focusRange・maxBlur・exposure・lensRadialBlur など |

Camera Volume の値は、Inspector でチェックを入れた項目だけ効く（チェックが外れた項目はキーを打っても全体の設定のまま）。

**揺れ。** Add Track → `TimelineCameraShakeTrack`。列をダブルクリックして印を置き、Strength を決める。Use Direction を入れると向きをワールドで決められる（入れないとランダム）。

**線に沿って動かす。** 空の Entity を1つ置いて、その子に点の Entity を上から順に並べる（ギズモで動かす）。カメラに Camera Path を付けて、Path へ親の Entity をドラッグし、Position をキーで動かす。線は Scene ビューに水色で出る（View → Camera Paths）。

**止まらない時間。** Timeline Component の Time Mode を Real にする。カメラのトラックが写しているカメラは、Camera の Time Mode に関係なく Timeline の時間で動く。

**プレビュー。** 再生位置をつまむと、Game ビューにカメラの結果が出る。カメラの切り替え・補間・キーの値・ノイズの揺れ・衝撃の揺れが全部出る。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| カメラの切り替え方 | トラックが毎フレーム `RequestOverride` で頼む。頼みはそのフレームだけ | 再生を止める・Entity を消す・Mute・無効にする・ウィンドウを閉じる、どれも「頼まない」だけで済む。戻し忘れが起きない（プレビューの要求と同じ考え） |
| カメラのトラックの Evaluate | 時間が止まっていても毎フレーム呼ぶ（`EvaluatesEveryFrame`）。Stopped の間は呼ばない | 一時停止・Finished の間もカメラを保つ。値のトラックは今までどおり時間が動いたときだけ |
| Priority との関係 | 通常のカメラの選び方は変えない。上書きはその上に乗る | Timeline が終われば何もしなくても元のカメラ。カメラ段階1の動きを変えない |
| 重なりの補間 | クリップの時間から計算する | シーク・プレビューでも同じ絵になる。`StartBlend` の経過時間の補間だと、シークすると途中の絵が出ない |
| 戻す補間（Ease） | クリップの終わりからの時間で計算して、その秒を Timeline の長さに入れる | プレビューで確かめられる。列に斜めの線で見える |
| 途中で止めたとき | 頼みに書いてあった秒で、今見えている絵から通常のカメラへ補間する | `TimelineSystem::Stop` で演出を飛ばしても絵が飛ばない。Cut のクリップならすぐ戻る |
| Hold の Exit Time | `TimelineSystem::Stop` で止めたときに使う | Hold はゲームが Stop を呼んで終わらせる物なので |
| 通常のカメラの Blend の設定 | Timeline から戻るときは使わない | 「指定した方法で戻す」を守る。Timeline の設定だけ見れば動きが分かる |
| 補間の始まりのカメラ | 重なっていれば前のクリップ、Hold のカメラが残っていればそれ、無ければ通常のカメラ | Blend In が絵の飛ばない所から始まる |
| 2つの Timeline が同時に頼んだ | 後の頼みが勝つ（Entity の順） | 優先度を付ける場面が今は無い（0.5） |
| シーンのカメラの指し方 | パスの先頭の `/` | `.timeline` は共有するファイルなので EntityId を書けない。文字なら Prefab の中も外も同じ形で書ける。全部のトラックがそのまま使える |
| カメラのパスの持ち方 | クリップごとに持つ。`Fields` ではなく `WriteExtra` | ウィンドウでは文字の欄ではなく一覧で選ばせる |
| カメラを探す回数 | Entity の数が変わったときだけ | `FindByPath` は全部の Entity を調べる |
| Camera Path の点 | 線の Entity の子 | ギズモ・Undo・Prefab・Hierarchy がそのまま使える。3D の点を編集する道具を作らなくて済む（`PointListGizmo` は X・Y の面だけ） |
| Camera Path が動かす物 | 写す値だけ。Transform は書き換えない | カメラ段階1の決まりのまま。線を外せば置いた位置に戻る |
| 線の上の位置 | 長さに対する割合（0〜1） | 点を動かしてもキーを打ち直さなくて済む。等速はキーを直線にするだけ |
| 線の向き（AlongPath） | 前後 0.25m の2点の差 | 細かい折れ線の1本の向きを使うと、継ぎ目でカクカク向きが変わる |
| Timeline の時間とカメラの時間 | 写しているカメラは Timeline の Time Mode で動かす | Time Mode を Timeline 1か所で決められる。Real の演出の途中で、Game のカメラの注視点だけ止まることがない |
| 衝撃のプレビュー | 受けてからの秒で計算する。`Camera Shake` の値は書き換えない | 減衰振動の式をそのまま解いているので1回で進められる。シークしても同じ絵になる |
| アイコンの層 | `DrawLayer::EditorGizmo` を足して Scene ビューだけに描く。ライトのアイコンと線も同じ層へ移す | Game ビューに出すと演出の絵に写る。アイコンだけ移すと、線が Game ビューに残る |

### 0.5 やらなかったこと（後の項目）

| 項目 | 必要になる場面 |
|---|---|
| 2つの Timeline のカメラの優先度 | 同時に2つの演出を再生する。今は後の頼みが勝つ |
| 3つ以上のクリップの重なり | 今は遅く始まった2つだけを混ぜる |
| 選んだカメラの錐台を Scene ビューに出す | 今は写しているカメラの錐台だけ。画角は Game ビューのプレビューで見る |
| 線の点を Scene ビューでクリックして選ぶ | 今は Hierarchy で選ぶ |
| 別のトラックのヒットストップ・スロー | 今はイベントのトラックから Script で呼ぶ（見本がそうなっている） |
| 通常のカメラの Follow と線の組み合わせ | 線の上の位置が勝つ（Transform は使わない） |
| カメラの名前を変えたときのパスの直し | 値のトラックと同じ。行が赤くなるので選び直す |

## 1章 解説

### 1.1 毎フレーム頼む形にした理由

「切り替える」という命令を Timeline から出すと、「いつ戻すか」を全部の出口に書くことになる。Stop、Entity の破棄、無効にしたとき、Mute、シーンの切り替え、エディタの Stop……1つ忘れると、演出のカメラのまま戻らない。

そこで、プレビューと同じ形にした。カメラのトラックは「今このカメラを写してほしい」と毎フレーム頼み、`CameraSystem` はそのフレームだけ従う。

```cpp
// TimelineCameraTrack::Evaluate（毎フレーム）
CameraSystem::RequestOverride(request);

// CameraSystem::Update の終わり
state->lastOverride = isGameRunning ? state->overrideRequest : std::nullopt;
state->overrideRequest.reset();
```

出口が1つになるので、戻す処理は `Update` の次の1行で全部済む。

```cpp
} else if (!request.has_value() && state->lastOverride.has_value()) {
	StartExitBlend(*state->lastOverride);
}
```

頼みの中に `exitTime` が入っているのは、**頼むのをやめた後に使う値を、頼んでいる間に渡しておく**ため。止められる側は「いつ止められるか」を知らないので、毎フレーム「今止められたらこう戻して」と添えておく。

順番も大事で、`TimelineSystem::Update` は `CameraSystem::Update` より前に動く（`EngineSystems::Update` の順番のまま）。なので頼んだフレームのカメラにそのまま効く。`TimelineSystem::Update` はプレビューの値もここで書くので、プレビューも同じ道を通る。

### 1.2 時間から決める

カメラのトラックは過去を覚えない。`Select(time)` は時間だけから「どのクリップをどの割合で」を返す。

```
時間      0    1    2    3    4    5
クリップA ■■■■■■■■■■■■■■■■
クリップB              ■■■■■■■■■■■■■■■■
                       ↑     ↑
                     B の    A の
                     始まり  終わり
                       この間で A → B を混ぜる
```

重なりの割合は `(時間 - B の始まり) / (A の終わり - B の始まり)`。これにイージングを掛ける。

クリップを探す手順は次の通り。クリップは始まる時間の順に並んでいる（`TimelineTrackOf` が守る）。

| 探す物 | 決まり |
|---|---|
| `newest` | 今の時間に掛かっているクリップの中で、一番遅く始まった物 |
| `older` | その次に遅く始まった物 |
| `ended` | もう終わったクリップの中で、一番遅く終わった物 |

`newest` があればクリップの中、無ければ `ended` の戻し方を見る。

終わりちょうどの時間は「終わった」側に入る（`time < end` で調べる）。2つのクリップを隙間なく並べると、ちょうどの時間は後のクリップになる。これがカットになる。`Finished` の時間は Timeline の長さちょうどなので、最後のクリップが Cut なら再生が終わった瞬間に通常のカメラへ戻る。

### 1.3 戻し方

| Exit | クリップの終わった後 | `TimelineSystem::Stop` で止めたとき |
|---|---|---|
| Cut | 頼まない | すぐ戻る |
| Ease | `from` をクリップ、`to` を通常のカメラにして、時間で混ぜる | 残りの秒で、止めた所の絵から補間する |
| Hold | そのカメラを頼み続ける | Exit Time の秒で補間する |

Ease の間に頼む `exitTime` は「残りの秒」にしてある。戻し終わる直前のフレームでは残りはほぼ 0 なので、頼みが無くなったフレームの補間はすぐ終わる。もし Exit Time のままにすると、戻し終わった所からもう1回、止まった絵からの補間が始まって、通常のカメラが動いていると遅れて見える。

`GetEndTime` は Ease の秒を足して返す。`Timeline::GetDuration` はトラックの `GetEndTime` の一番大きい値を使うので、戻す補間の途中で再生が終わることはない。

### 1.4 通常のカメラを持ち続ける

`CameraState` の `mainEntity` は「写しているカメラ」で、`normalEntity` は「Priority で選んだカメラ」。分けたのは、上書きの間も通常のカメラの選び方を動かし続けるため。分けないと、Timeline が終わった瞬間に「カメラが変わった」と判断されて、通常のカメラの Blend の設定で補間が始まってしまう。

`from`・`to` の無効な Handle は「通常のカメラ」という意味にした。Blend In と Ease の戻しは、片方が通常のカメラになるだけで、重なりと同じ式で書ける。

### 1.5 止まらない時間

Timeline の Time Mode は UI 段階3 からある。足したのは次の1か所。

```cpp
float GetCameraDeltaTime(Handle<Entity> entity) {
	const std::optional<CameraOverride>& request = state->overrideRequest;
	if (request.has_value() && (entity == request->from || entity == request->to)) {
		return Time::GetDeltaTime(request->timeMode);
	}
	// ここから下は今までどおり
```

これが無いと、Real の Timeline の中で Game のカメラを写したとき、ヒットストップの間だけ Camera Look At・Camera Shake が止まる。カメラを1台ずつ Real にする必要もなくなる。

`CameraSystem::UpdateFollow` もこの関数を使うので、Camera Follow も同じ時間で動く。

### 1.6 `/` で始まるパス

`.timeline` は同じファイルを何体でも再生するので、EntityId は書けない（Prefab から作るたびに変わり、シーンをまたぐと別の物を指す）。そこで今までのパスを広げた。

| パス | 探す場所 |
|---|---|
| `Panel/Title` | 再生する Entity の子孫（今まで） |
| 空 | 再生する Entity 自身（今まで） |
| `/Cameras/Close` | シーンの一番上（新しい） |

`EntityManager::FindByPath` の中で足したので、値のトラック・イベントのトラックも、対象に `/` のパスをそのまま書ける。`MakeBindingPath` は逆向きで、子孫なら今までのパス、外なら `/` のパスを作る。ウィンドウはこれだけを呼ぶ。

### 1.7 Camera Path

`PathCurve` は点を順に通る Catmull-Rom スプラインを細かい折れ線にして、線に沿った長さで位置を聞ける（Arrow Strip が使っている物）。

```
Dolly Line（線の Entity）
  ├ P0 ──●
  ├ P1 ────●───────●── P2
  └ P3 ──────────────●
          ↑ Position 0.0 〜 1.0 は、線の長さに対する割合
```

`Position` は長さの割合なので、キーを直線にすれば等速で動く。ゆっくり動き出したいときはキーのカーブで書く。

カメラの写す値を作る `EvaluateCamera` の順番はこうなる。

```
Transform → Camera Path（位置を置き換える）→ Camera Look At（向きを置き換える）→ Camera Volume → Camera Shake
```

Look At より前に置いたので、線の上から注視点を見られる。

### 1.8 揺れのプレビュー

揺れは再生中だけ動く値（`noiseTime`・`impulseOffset`）で作っている。停止中はこの値が進まないので、そのままではプレビューに揺れが出ない。

衝撃は減衰振動の式をそのまま解いている（カメラ段階1 の 1.5）。式を解く方法は `deltaTime` の大きさで結果が変わらないので、**受けてからの秒を1回で渡せば、その時間の揺れがそのまま出る**。

```cpp
CameraShakeComponent single = shake;                 // 写しの上で
CameraShake::AddImpulse(single, strength, direction); // 受けて
CameraShake::Update(single, age);                     // age 秒を1回で進める
preview.impulseOffset += single.impulseOffset;        // 衝撃が複数あれば足す
```

揺れは足し算で重ねられる（ばねの式は線形）ので、衝撃ごとに別々に計算して足せる。ノイズは `noiseTime` を見ている時間から作るだけ。どちらも写しの上で計算するので、Component は書き換えない。

ランダムの向きの衝撃は、プレビューでは決まった向き（カメラの右上）にした。つまむたびに向きが変わると、絵が確かめられないため。

### 1.9 アイコンが出なかった原因

`Resources/Textures/Camera.png` はあるのに、読み込むコードが無かった。`ScenePicking` にはカメラをクリックで選ぶ処理があり、コメントに「カメラはいつも錐台の線が出ている」と書いてあるが、錐台を描くのは写しているカメラ1台だけなので、ほかのカメラは見えない場所をクリックしないと選べなかった。

ライトのアイコンと線は、今まで Game ビューにも出ていた。カメラのアイコンを同じにすると、演出のカットに別のカメラのアイコンが写る。そこで `DrawLayer::EditorGizmo` を足して Scene ビューだけに描き、ライトのアイコンと線も同じ層へ移した。`RenderQueue` はビューごとに層を飛ばせるので、描く物は1回積むだけで済む。

~~Camera Path を付けたカメラのアイコンは Transform の位置ではなく線の上に出す。~~ → パートF の F-2 で、ほかの Entity と同じ Transform の位置に戻した。線の上の写す位置には、黄色の短い線と、アイコンからそこへの黄色の線を出す。

### 1.10 ウィンドウの描き方

カメラのトラックのクリップには、3つの見せ方を足した。

| 足した物 | 仮想関数 | 中身 |
|---|---|---|
| クリップの名前 | `GetClipLabel` | カメラのパスの一番下の名前 |
| 補間の斜めの線 | `GetClipFade` | 始まりの補間（重なり・Blend In）を左上の三角、戻す補間を右へ細くなる三角で見せる |
| 問題の表示 | `FindClipProblem` | カメラが見つからなければ行を赤くする |

`TimelineTrack` の仮想関数にしたのは、UI 段階4 と同じ理由で、ウィンドウがクリップの型を知らなくて済むようにするため。

重ねたクリップは後のクリップが上に描かれるので、前のクリップの右端が隠れて長さを変えられなかった。`FindHit` で「右端のすぐ外」を全部のクリップについて先に調べるようにした。

### 1.11 あまり使われない STL

**`std::string_view::starts_with`・`remove_prefix`**
`starts_with` は先頭が指定の文字かを調べる。`remove_prefix(n)` は見ている範囲の先頭を n 文字縮める（元の文字は変えない）。`/` を確かめて飛ばすのに使う。

**`std::optional<CameraOverride>`**
「値があるか無いか」を持てる入れ物（カメラ段階1でも使った）。`state->lastOverride = isGameRunning ? state->overrideRequest : std::nullopt;` のように、「無い」は `std::nullopt` で書く。

**`dynamic_cast`**
ウィンドウがカメラのトラックだけに出す欄で使う（値のトラックと同じ）。基底クラスの参照から実際の型を調べて、違えば `nullptr` を返す。

**`std::ranges::sort` の3つ目の引数（プロジェクション）**（パートF）
`std::ranges::sort(names, {}, 関数)` は、要素そのものではなく「関数に通した値」で比べて並べる。2つ目の `{}` は比べ方（既定の `<`）。トラックの名前を、頭の Timeline を省いた名前の順に並べるのに使う。参考：https://en.cppreference.com/w/cpp/algorithm/ranges/sort

### 1.12 ImpulseTrack と CameraShakeTrack の違い（質問の答え）

説明が似ているが、出どころが違う。**演出で使うのは CameraShakeTrack。**

| | ImpulseTrack | CameraShakeTrack |
|---|---|---|
| どこにある | ゲーム側。UI 段階3（[Timeline.md](Timeline.md)）の見本 `Source/Scripts/TimelineTest/TimelineTester.cpp` の最後 | エンジン（このカメラ段階2 の A-11・A-12） |
| 作った理由 | 「ゲーム側で `TIMELINE_TRACK(...)` を書けば新しいトラックを足せる」ことの見本 | カメラの演出の部品 |
| クリップ | 長さがある（長さは使っていない）・Strength だけ | 長さの無い1点・Strength・Use Direction・Direction |
| 向き | いつもランダム | ランダムか、決めた向き |
| 停止中のプレビュー | 揺れない（`CanPreview` が false） | 揺れが Game ビューに出る（その時間の揺れを計算して見せる） |

どちらも中で `CameraSystem::AddImpulse` を呼ぶので、Play 中の揺れ方は同じ。**2026-11-02 に ImpulseTrack を消した**（見本の `Docs/Tasks/TimelineTest/TimelineTester.cpp` と AL4_01 の `Source/Scripts/TimelineTest/TimelineTester.cpp`。使っていた AL4_01 の `Resources/timelines/NewTimeline.timeline` はどこからも使われていなかったのでファイルごと消した）。新しいトラックの書き方は [Timeline.md](Timeline.md) の 1.9 に例として残っている。

ずっと続く揺れ（地鳴りなど）は、どちらでもなく ValueTrack で Camera Shake の Gain（`noiseGain`）にキーを打つ。

### 1.13 Vector Shape をカメラと同じ Timeline で動かす（質問の答え）

ValueTrack は Component の数の項目なら何でも動かせるので、Vector Shape も同じ Director の Timeline にトラックを足すだけで動く。カメラのトラックと同じ時間で進むので、カットに合わせてゲージや枠を動かせる。

1. Vector Shape の Entity をシーンに置く（GameScreen の Canvas の下など。Director の子でなくてよい）。
2. Director を選んで Timeline ウィンドウを開き、Add Track → **ValueTrack**。
3. Hierarchy の Vector Shape の Entity を、足したトラックの**行（左の名前の所）**へドラッグする。Target が `/Canvas/Gauge` のような `/` で始まるパスになる（Target の一覧には Director の子孫とカメラしか出ないので、ドラッグで選ぶ）。
4. Component に `Vector Shape`、Property に動かす項目を選ぶ。
5. 列をダブルクリックでクリップを足し、キーを打つ。目盛りをつまむと、停止中もプレビューで動く。

| 動かしたい物 | Component / Property |
|---|---|
| ゲージの伸び | Vector Shape / Amount（`gaugeAmount`。0〜1） |
| 色・透明度 | Vector Shape / Color（a が透明度） |
| 角の丸み・平行四辺形の傾き | Vector Shape / Corner Radius・Skew Angle |
| 縁取りの太さ | Vector Shape / Stroke Width |
| 位置・大きさ・回転 | Transform / translation・scale・rotation（Inspector の Position・Scale・Rotation） |

**気を付けること：** UI Screen（`.uiscreen`）の中の Vector Shape は、`UIScreen::Open` するまでシーンに無いので、シーンの Timeline から結び付けられない（停止中に対象が見つからず、プレビューもできない）。画面ごと演出に入れる形は、次の UI 段階5（UI アニメ統合）で扱う。今はシーンに直接置いた物だけ。

## パートA 再生の仕組み

カメラの切り替え・揺れ・線をエンジンへ足す。写し終わるとビルドが通り、`.timeline` を手で書けば再生できる（ウィンドウの対応はパートB）。

### A-1 `ECS/EntityManager.h`

`FindByPath` のコメントに1行足して、`MakePath` の下に足す。

```cpp
	// root から子の名前を / でつないだパス（"Panel/Title"）で探す。空なら root。同じ名前の子が2つあれば先の物。無ければ無効なHandle
	// 先頭が / なら root を見ず、シーンの一番上から探す（"/Cameras/Close"）
	// Prefab・画面（UI Screen）から作るたびに EntityId が変わる物を、作り直しても同じ相手として指すのに使う。全部の Entity を調べるので、毎フレームは呼ばない
	static Handle<Entity> FindByPath(Handle<Entity> root, std::string_view path);
	// FindByPath の逆。root から entity までの名前のパス（entity が root なら空。root の子孫でなければ std::nullopt）
	static std::optional<std::string> MakePath(Handle<Entity> root, Handle<Entity> entity);
	// root から entity を指すパス。子孫なら root からのパス、それ以外はシーンの一番上からのパス（/ で始まる）。どちらも FindByPath で同じ Entity に戻る
	static std::string MakeBindingPath(Handle<Entity> root, Handle<Entity> entity);
```

### A-2 `ECS/EntityManager.cpp`

`FindByPath` の頭と `while` の中を直す（`found` から下は今のまま）。

```cpp
Handle<Entity> EntityManager::FindByPath(Handle<Entity> root, std::string_view path) {
	Handle<Entity> current = root;
	std::vector<Handle<Entity>> children;
	// --- 先頭が / なら、最初の1段はシーンの一番上から探す ---
	bool isFromScene = path.starts_with('/');
	if (isFromScene) {
		path.remove_prefix(1);
	}
	while (!path.empty() && (isFromScene || IsAlive(current))) {
		// --- 次の / までが、1段下の子の名前 ---
		const size_t slash = path.find('/');
		const std::string_view name = path.substr(0, slash);
		if (isFromScene) {
			GetRoots(children);
		} else {
			GetChildren(current, children);
		}
		isFromScene = false;
		const auto found = std::ranges::find_if(children, [name](Handle<Entity> child) { return Get(child)->name == name; });
```

`MakePath` の下に足す。

```cpp
std::string EntityManager::MakeBindingPath(Handle<Entity> root, Handle<Entity> entity) {
	if (const std::optional<std::string> path = MakePath(root, entity)) {
		return *path;
	}
	// 親へ上りながら、名前を前につなぐ（先頭にも / が付く）
	std::string path;
	for (Handle<Entity> current = entity; IsAlive(current); current = Get(current)->parent) {
		path = "/" + Get(current)->name + path;
	}
	return path;
}
```

### A-3 `Timeline/TimelineTrack.h`

include に足す。

```cpp
#include "MyEngine/Time/EngineTime.h"
```

`target` のコメントを直す。

```cpp
	std::string target;         // 対象。再生する Entity から子の名前を / でつないだパス（空なら再生する Entity 自身。先頭が / ならシーンの一番上から）
```

`Timeline/TimelineTrack.cpp` の `DescribeCommon` の Target の行も直す。

```cpp
	ui.Field("Target###target", target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く（Panel/Title）。空なら再生する Entity 自身。先頭が / ならシーンの一番上から（/Cameras/Close）"));
```

`TimelineFrame` に足して、その下に `TimelineClipFade` を足す。

```cpp
	std::vector<TimelineEvent>* events = nullptr; // 呼ぶイベントを足す場所（全部の値を書いた後にまとめて呼ばれる）
	TimeMode timeMode = TimeMode::Game;           // 再生に使っている時間（Timeline Component の Time Mode）
	bool isPreview = false;                       // エディタの停止中のプレビュー
};

/// <summary>
/// クリップの両端の補間の長さ（Timeline ウィンドウの列に斜めの線で見せる）
/// </summary>
struct TimelineClipFade {
	float in = 0.0f;  // 始まりの補間の秒
	float out = 0.0f; // 終わった後の補間の秒
};
```

`GetClipCurves` の下から `CanPreview` までを直す。

```cpp
	// クリップの上に出す名前（無ければ空）
	virtual std::string GetClipLabel(size_t /*index*/) const { return {}; }
	// クリップの両端の補間（Timeline ウィンドウの見た目だけ）
	virtual TimelineClipFade GetClipFade(size_t /*index*/) const { return {}; }

	// 対象（Target の欄）を使うか。クリップごとに相手を持つ種類・相手の要らない種類は false にする
	virtual bool UsesTarget() const { return true; }
	// クリップの持つ相手の問題（無ければ空。Timeline ウィンドウの行に出す。UsesTarget が false の種類だけ呼ばれる）
	virtual std::string FindClipProblem(Handle<Entity> /*owner*/) const { return {}; }

	// プレビューで Evaluate を呼んでよいか。true にするなら、Component の値は ComponentSerializer::SetValue で書く（書いた値を元へ戻せるのは SetValue だけ）
	virtual bool CanPreview() const { return false; }
	// 時間が止まっている間も、毎フレーム Evaluate を呼ぶか（カメラの切り替えのように、毎フレーム頼む種類）。再生していない（Stopped）間は呼ばない
	virtual bool EvaluatesEveryFrame() const { return false; }
```

### A-4 `Components/Camera/CameraPathComponent.h`（新しいファイル）

```cpp
#pragma once
#include <type_traits>

#include "MyEngine/ECS/Entity.h"

// カメラを線に沿って動かす。Camera Component と同じ Entity に付けるComponent（カテゴリ：Camera）
// 線は Path の Entity の子を上から順に通る、なめらかな線（PathCurve）。点が Entity なので、Scene ビューのギズモで動かせて、Prefab にもそのまま入る
// Position を Timeline のキーで動かすと、決めた時間で線の上を動く
// 変えるのは写す位置だけで、Transform は書き換えない（Camera Look At・Camera Shake は線の上の位置から掛かる）

/// <summary>
/// 線の上でのカメラの向き
/// </summary>
enum class CameraPathRotation {
	Keep,      // 変えない（Transform の向き。Camera Look At があれば注視点を見る）
	AlongPath, // 線の進む向きを見る
};


/// <summary>
/// 線に沿って動くカメラ（Unity の Cinemachine の Spline Dolly の小さい版）
/// </summary>
struct CameraPathComponent {
	bool enabled = true;                                    // false の間は Transform の位置のまま
	EntityRef path;                                         // 線の Entity（Hierarchy からドラッグ）。子の Entity が点になる
	float position = 0.0f;                                  // 線の上の位置（0：始まり 〜 1：終わり。長さに対する割合）
	CameraPathRotation rotation = CameraPathRotation::Keep; // 向きの決め方
};

static_assert(std::is_trivially_copyable_v<CameraPathComponent>);
```

### A-5 `Systems/Camera/CameraPath.h`（新しいファイル）

```cpp
#pragma once
#include "MyEngine/Components/Camera/CameraPathComponent.h"
#include "MyEngine/Math/Geometry/PathCurve.h"
#include "MyEngine/Math/Quaternion.h"
#include "MyEngine/Math/Vector3.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// Camera Path の中身（CameraSystem から使う）
/// </summary>
namespace CameraPath {
// Inspector とシーンファイルの並び
void Describe(ComponentUI& ui, CameraPathComponent& value);

// 線の Entity の子から線を作る（点が 2 つより少なければ空の線）。座標はワールド
void BuildLine(const CameraPathComponent& path, PathCurve& out);

// position・rotation を、線の上の位置と向きへ置き換える（線が無ければ変えない）
void Apply(const CameraPathComponent& path, Vector3& position, Quaternion& rotation);
} // namespace CameraPath
```

### A-6 `Systems/Camera/CameraPath.cpp`（新しいファイル）

```cpp
#include "CameraPath.h"

#include <algorithm>
#include <vector>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"

namespace {
constexpr float kDirectionSpan = 0.25f; // 向きを測る前後の長さ（m）。細かい折れ線の1本で測ると、線の継ぎ目で向きがカクカク変わる

PathCurve line;                       // 今のカメラの線（毎回確保しないように使い回す）
std::vector<Handle<Entity>> children; // 線の Entity の子（使い回す）
std::vector<Vector3> points;          // 線の点（使い回す）
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
void CameraPath::Describe(ComponentUI& ui, CameraPathComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと Transform の位置のまま"));
	ui.Field("Path###path", value.path, Tip("線の Entity（Hierarchy からドラッグ）。子の Entity を上から順に通る。子が 2 つ以上要る"));
	ui.Field("Position###position", value.position, Range(0.0f, 1.0f).Tip("線の上の位置。0：始まり 〜 1：終わり（長さに対する割合）。Timeline のキーで動かす"));
	ui.Field("Rotation###rotation", value.rotation, Tip("向きの決め方。Keep：Transform の向き（Camera Look At があれば注視点を見る） / AlongPath：線の進む向きを見る"));
}

//=============================================================================
// 線
//=============================================================================
void CameraPath::BuildLine(const CameraPathComponent& path, PathCurve& out) {
	points.clear();
	const Handle<Entity> root = EntityManager::Find(path.path);
	if (root.IsValid()) {
		EntityManager::GetChildren(root, children);
		for (const Handle<Entity> child : children) {
			if (const TransformComponent* transform = EntityManager::Get<TransformComponent>(child)) {
				points.push_back(GetWorldPosition(*transform));
			}
		}
	}
	out.Build(points);
}

void CameraPath::Apply(const CameraPathComponent& path, Vector3& position, Quaternion& rotation) {
	if (!path.enabled) {
		return;
	}
	BuildLine(path, line);
	if (line.IsEmpty()) {
		return;
	}
	const float distance = std::clamp(path.position, 0.0f, 1.0f) * line.GetLength();
	position = line.Evaluate(distance).position;
	if (path.rotation == CameraPathRotation::AlongPath) {
		const Vector3 direction = line.Evaluate(distance + kDirectionSpan).position - line.Evaluate(distance - kDirectionSpan).position;
		if (LengthSq(direction) > 0.0f) {
			rotation = MakeLookRotation(direction);
		}
	}
}
```

### A-7 `Systems/Camera/CameraSystem.h`

include を直す。

```cpp
#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/Components/Camera/CameraFollowComponent.h"
#include "MyEngine/Components/Camera/CameraLookAtComponent.h"
#include "MyEngine/Components/Camera/CameraPathComponent.h"
#include "MyEngine/Components/Camera/CameraShakeComponent.h"
#include "MyEngine/Components/Camera/CameraVolumeComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Easing.h"
#include "MyEngine/Math/Vector3.h"
#include "MyEngine/Time/EngineTime.h"
```

`struct TransformComponent;` の下に足す。

```cpp
/// <summary>
/// 通常のカメラ（Priority で選ぶカメラ）の代わりに写すカメラの頼み。無効な Handle は「通常のカメラ」という意味
/// </summary>
struct CameraOverride {
	Handle<Entity> from;                      // 混ぜる前のカメラ
	Handle<Entity> to;                        // 混ぜた先のカメラ
	float weight = 1.0f;                      // to の割合（0：from 〜 1：to）
	float exitTime = 0.0f;                    // 頼むのをやめたら、この秒をかけて通常のカメラへ戻す（0 ならすぐ）
	Ease::Type exitEase = Ease::Type::Linear; // 戻すときの速さの変え方
	TimeMode timeMode = TimeMode::Game;       // from・to のカメラの動きと、戻す補間に使う時間
};
```

クラスの説明の「1台分の値」の行を直して、`<para>` の最後に1行足す。

```cpp
/// <para>1台分の値 ＝ Transform ＋ Camera Path（位置）＋ Camera Look At（向き）＋ Camera Shake（揺れ）＋ Camera Volume（ポストエフェクト）。Transform を動かすのは Camera Follow と Timeline のキーだけで、ほかは写す値にだけ入る</para>
```

```cpp
/// <para>Timeline のカメラのトラックは RequestOverride で、そのフレームに写すカメラを頼む（頼まなくなれば通常のカメラへ戻る）</para>
```

`IsBlending` のコメントを直して、下に足す。

```cpp
	// カメラの切り替えの補間の途中か（Timeline のクリップの重なりの間も true）
	static bool IsBlending();
	// そのカメラの本来の位置（Camera Path があれば線の上。注視点・揺れ・補間は入らない。エディタのアイコンに使う）
	static Vector3 CalcCameraPosition(Handle<Entity> entity);
```

クラスの一番下に足す。

```cpp
	// ===== Timeline から使う（どれも頼んだフレームの Update だけ効く。TimelineSystem::Update の中から呼ぶ）=====

	/// <summary>
	/// このフレームに写すカメラを頼む（停止中のプレビューでも効く）。2つ頼まれたら、後の頼みが勝つ
	/// </summary>
	static void RequestOverride(const CameraOverride& request);

	/// <summary>
	/// 停止中のプレビューで揺れを見せる。ノイズは time 秒の所を読む
	/// </summary>
	static void BeginPreviewShake(float time);

	/// <summary>
	/// BeginPreviewShake の後に呼ぶ。age 秒前に受けた衝撃を、今の揺れに足して見せる
	/// </summary>
	/// <param name="strength">最初に振れる幅（m）</param>
	/// <param name="direction">最初に振れる向き</param>
	/// <param name="isWorldDirection">true：direction はワールドの向き false：カメラの右・上・前の向き</param>
	/// <param name="age">受けてからの秒</param>
	static void AddPreviewImpulse(float strength, const Vector3& direction, bool isWorldDirection, float age);
```

### A-8 `Systems/Camera/CameraSystem.cpp`

include に足す。

```cpp
#include "MyEngine/Systems/Camera/CameraPath.h"
```

`PendingImpulse` の下に足す。

```cpp
/// <summary>
/// プレビューで見せる衝撃1つ
/// </summary>
struct PreviewImpulse {
	PendingImpulse impulse; // 受けた衝撃
	float age = 0.0f;       // 受けてからの秒
};

/// <summary>
/// 停止中のプレビューの揺れ
/// </summary>
struct PreviewShake {
	float time = 0.0f;                    // 見ている時間（秒）
	std::vector<PreviewImpulse> impulses; // 受けた衝撃
};
```

`CameraState` の `mainEntity` から下を差し替える。

```cpp
	Handle<Entity> mainEntity;                     // 今使っている Camera Component の Entity（上書きの間はそのカメラ）
	Handle<Entity> normalEntity;                   // 通常のカメラ（priority で選んだカメラ）
	bool hasLoggedNoCamera = false;                // 「Camera Component が無い」を知らせたか（続けて何度も出さない）
	bool hasOutput = false;                        // output に前のフレームの値が入っているか（false なら、次の切り替えは補間しない）
	CameraOutput output;                           // 前のフレームに写した値（切り替えの途中でさらに切り替えるときは、ここから始める）
	CameraBlend blend;                             // 切り替えの補間
	std::vector<PendingImpulse> impulses;          // 送られた衝撃
	std::optional<CameraOverride> overrideRequest; // このフレームの上書きの頼み
	std::optional<CameraOverride> lastOverride;    // 前のフレームの上書き（頼みが無くなったフレームに、戻し方を読む）
	std::optional<PreviewShake> previewShake;      // このフレームのプレビューの揺れ
};
```

`GetCameraDeltaTime` の頭に足す。

```cpp
float GetCameraDeltaTime(Handle<Entity> entity) {
	// Timeline が写しているカメラは Timeline の時間で動かす（Timeline を Real にするだけで、ヒットストップの間も動く）
	const std::optional<CameraOverride>& request = state->overrideRequest;
	if (request.has_value() && (entity == request->from || entity == request->to)) {
		return Time::GetDeltaTime(request->timeMode);
	}
```

`カメラ1台分の値` の見出しの上に足す。

```cpp
//=============================================================================
// 揺れのプレビュー
//=============================================================================
// ===== 見ている時間の揺れの値を持った写しを作る =====
// 衝撃は減衰振動の式をそのまま解いて進めるので（CameraShake::Update）、受けてからの秒を1回で進められる。受けた衝撃ごとに進めて足す
CameraShakeComponent MakePreviewShake(const CameraShakeComponent& shake, const Quaternion& rotation) {
	CameraShakeComponent preview = shake;
	preview.noiseTime = state->previewShake->time * shake.noiseFrequency;
	preview.impulseOffset = {0.0f, 0.0f, 0.0f};
	for (const PreviewImpulse& item : state->previewShake->impulses) {
		const PendingImpulse& impulse = item.impulse;
		CameraShakeComponent single = shake;
		single.impulseOffset = {0.0f, 0.0f, 0.0f};
		single.impulseVelocity = {0.0f, 0.0f, 0.0f};
		CameraShake::AddImpulse(single, impulse.strength, impulse.isWorldDirection ? RotateVector(impulse.direction, Conjugate(rotation)) : impulse.direction);
		CameraShake::Update(single, item.age);
		preview.impulseOffset += single.impulseOffset;
	}
	return preview;
}
```

`EvaluateCamera` の、注視点の上に足す。

```cpp
	// --- 線：位置を線の上へ置き換える ---
	if (const CameraPathComponent* path = EntityManager::Get<CameraPathComponent>(entity)) {
		CameraPath::Apply(*path, output.position, output.rotation);
	}
```

`EvaluateCamera` の揺れの2行を直す。

```cpp
	if ((isGameRunning || state->previewShake.has_value()) && shake != nullptr && shake->enabled) {
		const CameraShakeOffset offset = CameraShake::CalcOffset(isGameRunning ? *shake : MakePreviewShake(*shake, output.rotation));
```

`RefreshBlendSource` の上に足す。

```cpp
// ===== 上書きが終わったら、頼まれていた戻し方で通常のカメラへ補間する =====
void StartExitBlend(const CameraOverride& last) {
	CameraBlend& blend = state->blend;
	blend.isActive = last.exitTime > 0.0f;
	blend.from = state->output;
	blend.fromEntity = {}; // 2つのカメラの間の値かもしれないので、止まった値のまま使う
	blend.elapsed = 0.0f;
	blend.duration = last.exitTime;
	blend.ease = last.exitEase;
	blend.timeMode = last.timeMode;
}
```

`ApplyBlend` の下に足す。

```cpp
// ===== 上書きのカメラ1台の値（無効な Handle・カメラでない Entity なら通常のカメラの値）=====
CameraOutput EvaluateOverrideCamera(Handle<Entity> entity, const CameraOutput& normal, bool isGameRunning) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const CameraComponent* camera = EntityManager::Get<CameraComponent>(entity);
	return (transform != nullptr && camera != nullptr) ? EvaluateCamera(entity, *transform, *camera, isGameRunning) : normal;
}

// ===== 上書きを掛ける =====
CameraOutput ApplyOverride(const CameraOverride& request, const CameraOutput& normal, bool isGameRunning) {
	const CameraOutput to = EvaluateOverrideCamera(request.to, normal, isGameRunning);
	if (request.weight >= 1.0f) {
		return to;
	}
	return LerpOutput(EvaluateOverrideCamera(request.from, normal, isGameRunning), to, request.weight);
}
```

`Initialize` の最後に足す。

```cpp
	EntityManager::RegisterComponent<CameraPathComponent>();
	ComponentSerializer::Register<CameraPathComponent>("Camera Path", &CameraPath::Describe);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<CameraPathComponent>>("Camera Path", "Camera", &CameraPath::Describe));
```

`Update` と `ResetOutput` を差し替える。

```cpp
void CameraSystem::Update(bool isGameRunning) {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");

	// ===== 1. 通常のカメラを選ぶ（有効な物の中で priority が一番大きい物。同じなら先に見つけた物）=====
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
	const Handle<Entity> previous = state->normalEntity;
	state->normalEntity = best;

	// ===== 2. 無ければ既定のカメラ =====
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(best);
	if (transform == nullptr) {
		if (!state->hasLoggedNoCamera) {
			LogManager::Log("Camera Component が無いので、既定のカメラ（(0, 0, -10) から +Z を向く）で撮ります。Hierarchy の ＋ → Camera で置けます");
			state->hasLoggedNoCamera = true;
		}
		state->mainEntity = {};
		UseDefaultCamera();
		SceneRenderer::SetCameraPostOverrides({});
		ResetOutput();
		return;
	}
	state->hasLoggedNoCamera = false;

	// ===== 3. カメラが変わったら、補間を始める =====
	// 停止中と、シーンの最初のフレームは補間しない（エディタで値を変えた結果・置いた通りの絵をすぐ見せる）
	const std::optional<CameraOverride>& request = state->overrideRequest;
	if (!isGameRunning) {
		state->blend.isActive = false;
		state->impulses.clear();
	} else if (!request.has_value() && state->lastOverride.has_value()) {
		StartExitBlend(*state->lastOverride);
	} else if (best != previous && state->hasOutput) {
		StartBlend(previous, bestComponent);
	}

	// ===== 4. 注視点と揺れを進める =====
	if (isGameRunning) {
		UpdateCameraMotion();
	}

	// ===== 5. 写す値を作って、カメラへ写す =====
	// 大きさは写さない（ビュー行列に大きさが混ざると、絵が伸び縮みする）
	CameraOutput output = ApplyBlend(EvaluateCamera(best, *transform, bestComponent, isGameRunning), isGameRunning);
	if (request.has_value()) {
		output = ApplyOverride(*request, output, isGameRunning);
	}
	ApplyOutput(output);
	state->output = output;
	state->hasOutput = true;
	state->mainEntity = (request.has_value() && request->to.IsValid()) ? request->to : best;

	// ===== 6. 頼みは1フレームだけ（頼まなくなれば、次のフレームから通常のカメラ）=====
	state->lastOverride = isGameRunning ? state->overrideRequest : std::nullopt;
	state->overrideRequest.reset();
	state->previewShake.reset();
}

void CameraSystem::ResetOutput() {
	state->hasOutput = false;
	state->blend.isActive = false;
	state->overrideRequest.reset();
	state->lastOverride.reset();
	state->previewShake.reset();
}
```

`IsBlending` を差し替えて、下に足す。

```cpp
bool CameraSystem::IsBlending() {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	return state->blend.isActive || (state->lastOverride.has_value() && state->lastOverride->weight < 1.0f);
}

Vector3 CameraSystem::CalcCameraPosition(Handle<Entity> entity) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}
	Vector3 position = GetWorldPosition(*transform);
	Quaternion rotation = GetWorldRotation(*transform);
	if (const CameraPathComponent* path = EntityManager::Get<CameraPathComponent>(entity)) {
		CameraPath::Apply(*path, position, rotation);
	}
	return position;
}
```

ファイルの一番下に足す。

```cpp
//=============================================================================
// Timeline から使う
//=============================================================================
void CameraSystem::RequestOverride(const CameraOverride& request) {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	state->overrideRequest = request;
}

void CameraSystem::BeginPreviewShake(float time) {
	MY_ASSERT_MSG(state != nullptr, "CameraSystem::Initialize()を先に呼んでください");
	state->previewShake = PreviewShake{.time = time};
}

void CameraSystem::AddPreviewImpulse(float strength, const Vector3& direction, bool isWorldDirection, float age) {
	MY_ASSERT_MSG(state != nullptr && state->previewShake.has_value(), "BeginPreviewShake を先に呼んでください");
	state->previewShake->impulses.push_back({
	    {strength, direction, isWorldDirection},
        age
    });
}
```

### A-9 `Timeline/Camera/TimelineCameraTrack.h`（新しいファイル）

```cpp
#pragma once
#include <set>
#include <string>

#include "MyEngine/Math/Easing.h"
#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// クリップが終わった後のカメラの戻し方
/// </summary>
enum class TimelineCameraExit {
	Cut,  // すぐ通常のカメラへ戻す
	Ease, // Exit Time の秒をかけて、通常のカメラへ補間する（Timeline の長さに入る）
	Hold, // 次のクリップまで、このカメラのまま。後ろにクリップが無ければ TimelineSystem::Stop まで
};


/// <summary>
/// カメラのクリップ。この時間の間に写すカメラを持つ
/// <para>camera は再生する Entity からのパス（Prefab の中のカメラ）か、/ で始まるシーンの一番上からのパス（シーンのカメラ）</para>
/// </summary>
struct TimelineCameraClip : TimelineClip {
	std::string camera;                                // 写すカメラのパス（空なら再生する Entity 自身）
	float blendIn = 0.0f;                              // 前のクリップと重なっていないとき、この秒をかけて補間する（0 ならすぐ切り替える）
	Ease::Type blendEase = Ease::Type::InOutCubic;     // 始まりの補間と、前のクリップとの重なりの補間の速さの変え方
	TimelineCameraExit exit = TimelineCameraExit::Cut; // 終わった後の戻し方
	float exitTime = 1.0f;                             // 戻す補間の秒（Ease のとき。Hold は TimelineSystem::Stop で止めたときに使う）
	Ease::Type exitEase = Ease::Type::InOutCubic;      // 戻す補間の速さの変え方

	void Fields(ComponentUI& ui);
	void WriteExtra(SceneJson& out) const;
	void ReadExtra(SceneJson& in, std::set<std::string>& warnings);
};


/// <summary>
/// ある時間のカメラの選び方（Entity を知らない計算の結果）
/// </summary>
struct TimelineCameraSelection {
	static constexpr int kNormalCamera = -1; // 通常のカメラ（クリップの番号の代わり）

	bool isActive = false;                    // false なら何も頼まない（通常のカメラのまま）
	int from = kNormalCamera;                 // 混ぜる前のクリップの番号
	int to = kNormalCamera;                   // 混ぜた先のクリップの番号
	float weight = 1.0f;                      // to の割合（イージングを掛けた後の値）
	float exitTime = 0.0f;                    // ここで止められたときに、通常のカメラへ戻す秒
	Ease::Type exitEase = Ease::Type::Linear; // 戻す速さの変え方
};


/// <summary>
/// クリップごとに写すカメラを決めるトラック
/// <para>通常のカメラ（Priority で選ぶカメラ）の代わりに写すカメラを、CameraSystem::RequestOverride で毎フレーム頼む。頼むのをやめれば通常のカメラへ戻る</para>
/// <para>重なったクリップは、後のクリップの始まりから前のクリップの終わりまでの間に混ぜる。時間から決まる値なので、シークしてもプレビューでも同じ絵になる</para>
/// </summary>
class TimelineCameraTrack : public TimelineTrackOf<TimelineCameraClip> {
public:
	TimelineCameraTrack() { name = "Camera"; }

	// time 秒のカメラの選び方
	TimelineCameraSelection Select(float time) const;

	float GetEndTime() const override;
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
	std::unique_ptr<TimelineTrackState> CreateState() const override;
	std::string GetClipLabel(size_t index) const override;
	TimelineClipFade GetClipFade(size_t index) const override;
	bool UsesTarget() const override { return false; }
	std::string FindClipProblem(Handle<Entity> owner) const override;
	bool CanPreview() const override { return true; }
	bool EvaluatesEveryFrame() const override { return true; }
};
```

### A-10 `Timeline/Camera/TimelineCameraTrack.cpp`（新しいファイル）

```cpp
#include "TimelineCameraTrack.h"

#include <algorithm>
#include <format>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr const char* kCameraKey = "camera"; // ファイルのカメラの項目の名前
constexpr const char* kSelfLabel = "(self)"; // 再生する Entity 自身を指すときの表示

/// <summary>
/// カメラのトラックの、Entity ごとの値
/// </summary>
class TimelineCameraState : public TimelineTrackState {
public:
	std::vector<Handle<Entity>> cameras; // [クリップの番号] 見つけたカメラ（見つからなければ無効）
};

// ===== パスの先がカメラなら Handle を返す =====
Handle<Entity> FindCamera(Handle<Entity> owner, const std::string& path) {
	const Handle<Entity> entity = EntityManager::FindByPath(owner, path);
	return (EntityManager::Get<CameraComponent>(entity) != nullptr) ? entity : Handle<Entity>{};
}

// ===== クリップの番号のカメラ（通常のカメラなら無効な Handle）=====
Handle<Entity> CameraOf(const TimelineCameraState& state, int clip) { return (clip == TimelineCameraSelection::kNormalCamera) ? Handle<Entity>{} : state.cameras[clip]; }
} // namespace

//=============================================================================
// クリップ
//=============================================================================
// ===== 見せ方 =====
void TimelineCameraClip::Fields(ComponentUI& ui) {
	TimelineClip::Fields(ui);
	ui.Separator("Blend");
	ui.Field("Blend In###blendIn", blendIn, AtLeast(0.0f).Tip("前のクリップと重なっていないとき、この秒をかけて補間する。0 ならすぐ切り替える"));
	ui.Field("Blend Ease###blendEase", blendEase, Tip("始まりの補間と、前のクリップとの重なりの補間の速さの変え方"));
	ui.Separator("Exit");
	ui.Field("Exit###exit", exit, Tip("終わった後の戻し方。Cut：すぐ通常のカメラへ / Ease：Exit Time の秒で補間する / Hold：次のクリップか TimelineSystem::Stop まで、このカメラのまま"));
	ui.BeginDisabled(exit == TimelineCameraExit::Cut);
	ui.Field("Exit Time###exitTime", exitTime, AtLeast(0.0f).Tip("戻す補間の秒。Ease のときは Timeline の長さに入る。Hold は TimelineSystem::Stop で止めたときに使う"));
	ui.Field("Exit Ease###exitEase", exitEase, Tip("戻す補間の速さの変え方"));
	ui.EndDisabled();
}

// ===== ファイル（カメラは Timeline ウィンドウで一覧から選ぶので、Fields には並べない）=====
void TimelineCameraClip::WriteExtra(SceneJson& out) const { out[kCameraKey] = camera; }

void TimelineCameraClip::ReadExtra(SceneJson& in, std::set<std::string>& warnings) {
	const auto found = in.find(kCameraKey);
	if (found == in.end()) {
		return;
	}
	if (found->is_string()) {
		camera = found->get<std::string>();
	} else {
		warnings.insert("\"camera\" が文字ではないので、再生する Entity 自身を使います");
	}
	in.erase(found);
}

//=============================================================================
// カメラの選び方
//=============================================================================
TimelineCameraSelection TimelineCameraTrack::Select(float time) const {
	// --- 今の時間に掛かっているクリップを、遅く始まった方から2つ。終わったクリップは、一番遅く終わった物 ---
	int newest = -1;
	int older = -1;
	int ended = -1;
	for (int i = 0; i < static_cast<int>(clips.size()) && clips[i].start <= time; ++i) {
		if (time < clips[i].GetEnd()) {
			older = newest;
			newest = i;
		} else if (ended < 0 || clips[i].GetEnd() >= clips[ended].GetEnd()) {
			ended = i;
		}
	}
	TimelineCameraSelection selection;
	// --- クリップの中 ---
	if (newest >= 0) {
		const TimelineCameraClip& clip = clips[newest];
		const float elapsed = time - clip.start;
		selection.isActive = true;
		selection.to = newest;
		selection.exitTime = (clip.exit == TimelineCameraExit::Cut) ? 0.0f : clip.exitTime;
		selection.exitEase = clip.exitEase;
		if (older >= 0) {
			// 重なり：後のクリップの始まりから、前のクリップの終わりまでで混ぜ終わる
			selection.from = older;
			selection.weight = Ease::Apply(elapsed / (clips[older].GetEnd() - clip.start), clip.blendEase);
		} else if (elapsed < clip.blendIn) {
			// Hold で残っているカメラがあれば、そこから補間する
			selection.from = (ended >= 0 && clips[ended].exit == TimelineCameraExit::Hold) ? ended : TimelineCameraSelection::kNormalCamera;
			selection.weight = Ease::Apply(elapsed / clip.blendIn, clip.blendEase);
		}
		return selection;
	}
	// --- 最初のクリップの前は何も頼まない ---
	if (ended < 0) {
		return selection;
	}
	// --- 終わった後 ---
	const TimelineCameraClip& clip = clips[ended];
	const float remaining = clip.GetEnd() + clip.exitTime - time;
	switch (clip.exit) {
	case TimelineCameraExit::Cut:
		break;
	case TimelineCameraExit::Hold:
		selection.isActive = true;
		selection.to = ended;
		selection.exitTime = clip.exitTime;
		selection.exitEase = clip.exitEase;
		break;
	case TimelineCameraExit::Ease:
		if (remaining > 0.0f) {
			selection.isActive = true;
			selection.from = ended;
			selection.weight = Ease::Apply(1.0f - remaining / clip.exitTime, clip.exitEase);
			selection.exitTime = remaining; // 途中で止められても、残りの秒で戻す
			selection.exitEase = clip.exitEase;
		}
		break;
	}
	return selection;
}

float TimelineCameraTrack::GetEndTime() const {
	float end = 0.0f;
	for (const TimelineCameraClip& clip : clips) {
		end = (std::max)(end, clip.GetEnd() + ((clip.exit == TimelineCameraExit::Ease) ? clip.exitTime : 0.0f));
	}
	return end;
}

//=============================================================================
// 再生
//=============================================================================
std::unique_ptr<TimelineTrackState> TimelineCameraTrack::CreateState() const { return std::make_unique<TimelineCameraState>(); }

void TimelineCameraTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	auto& cameraState = static_cast<TimelineCameraState&>(state);
	// --- カメラを探す（Entity の数が変わったときだけ。探すのは Entity を全部調べるので、毎フレームはしない）---
	const size_t entityCount = EntityManager::GetCount();
	if (!state.isSearched || state.searchedEntities != entityCount || cameraState.cameras.size() != clips.size()) {
		state.isSearched = true;
		state.searchedEntities = entityCount;
		cameraState.cameras.clear();
		for (const TimelineCameraClip& clip : clips) {
			cameraState.cameras.push_back(FindCamera(frame.owner, clip.camera));
			if (!cameraState.cameras.back().IsValid()) {
				WarnOnce(state, std::format("カメラが見つかりません: \"{}\"", clip.camera));
			}
		}
	}
	// --- 頼む（頼まなければ、通常のカメラのまま）---
	const TimelineCameraSelection selection = Select(frame.time);
	if (!selection.isActive) {
		return;
	}
	CameraOverride request;
	request.from = CameraOf(cameraState, selection.from);
	request.to = CameraOf(cameraState, selection.to);
	request.weight = selection.weight;
	request.exitTime = selection.exitTime;
	request.exitEase = selection.exitEase;
	request.timeMode = frame.timeMode;
	CameraSystem::RequestOverride(request);
}

//=============================================================================
// Timeline ウィンドウ
//=============================================================================
std::string TimelineCameraTrack::GetClipLabel(size_t index) const {
	const std::string& path = clips[index].camera;
	const size_t slash = path.rfind('/');
	return path.empty() ? kSelfLabel : ((slash == std::string::npos) ? path : path.substr(slash + 1));
}

TimelineClipFade TimelineCameraTrack::GetClipFade(size_t index) const {
	const TimelineCameraClip& clip = clips[index];
	TimelineClipFade fade;
	fade.in = clip.blendIn;
	fade.out = (clip.exit == TimelineCameraExit::Ease) ? clip.exitTime : 0.0f;
	// 前のクリップと重なっていれば、重なった長さで混ぜる
	for (size_t i = 0; i < index; ++i) {
		if (clips[i].GetEnd() > clip.start && clips[i].GetEnd() < clip.GetEnd()) {
			fade.in = clips[i].GetEnd() - clip.start;
		}
	}
	return fade;
}

std::string TimelineCameraTrack::FindClipProblem(Handle<Entity> owner) const {
	for (const TimelineCameraClip& clip : clips) {
		if (!FindCamera(owner, clip.camera).IsValid()) {
			return std::format("カメラが見つかりません: \"{}\"", clip.camera.empty() ? kSelfLabel : clip.camera);
		}
	}
	return {};
}
```

### A-11 `Timeline/Camera/TimelineCameraShakeTrack.h`（新しいファイル）

```cpp
#pragma once
#include "MyEngine/Math/Vector3.h"
#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// 衝撃のクリップ（長さは無い。start の時間ちょうどの1点）
/// </summary>
struct TimelineCameraShakeClip : TimelineClip {
	static constexpr bool kHasDuration = false; // 1点
	float strength = 0.3f;                      // 最初に振れる幅（m）
	bool useDirection = false;                  // false：向きは画面の面の中でランダム / true：Direction の向き
	Vector3 direction = {1.0f, 0.0f, 0.0f};     // 最初に振れる向き（ワールド）

	void Fields(ComponentUI& ui);
};


/// <summary>
/// 指定の時間に、カメラへ衝撃を送るトラック（CameraSystem::AddImpulse。有効な Camera Shake 全部が受ける）
/// <para>ずっと続く揺れは TimelineValueTrack で Camera Shake の Gain（noiseGain）を動かす</para>
/// <para>プレビューでは送らず、その時間の揺れの値を計算して見せる</para>
/// </summary>
class TimelineCameraShakeTrack : public TimelineTrackOf<TimelineCameraShakeClip> {
public:
	TimelineCameraShakeTrack() { name = "Camera Shake"; }

	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override;
	bool UsesTarget() const override { return false; }
	bool CanPreview() const override { return true; }
};
```

### A-12 `Timeline/Camera/TimelineCameraShakeTrack.cpp`（新しいファイル）

```cpp
#include "TimelineCameraShakeTrack.h"

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"

namespace {
const Vector3 kPreviewDirection = {1.0f, 1.0f, 0.0f}; // プレビューでの、ランダムの代わりの向き（カメラの右上。動かすたびに絵が変わらないように）
} // namespace

//=============================================================================
// クリップ
//=============================================================================
void TimelineCameraShakeClip::Fields(ComponentUI& ui) {
	ui.Field("Time###start", start, AtLeast(0.0f).Tip("衝撃を送る時間（秒）"));
	ui.Field("Strength###strength", strength, AtLeast(0.0f).Tip("最初に振れる幅（m）。Camera Shake の Gain が掛かる"));
	ui.Field("Use Direction###useDirection", useDirection, Tip("外すと、向きは画面の面の中でランダム"));
	ui.BeginDisabled(!useDirection);
	ui.Field("Direction###direction", direction, Tip("最初に振れる向き（ワールド）。長さは1でなくてよい"));
	ui.EndDisabled();
}

//=============================================================================
// トラック
//=============================================================================
void TimelineCameraShakeTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& /*state*/) const {
	// --- プレビュー：今の時間までに始まった衝撃を、受けてからの秒と一緒に渡す ---
	if (frame.isPreview) {
		for (const TimelineCameraShakeClip& clip : clips) {
			if (clip.start <= frame.time) {
				CameraSystem::AddPreviewImpulse(clip.strength, clip.useDirection ? clip.direction : kPreviewDirection, clip.useDirection, frame.time - clip.start);
			}
		}
		return;
	}
	// --- 再生：このフレームに通った衝撃を送る ---
	ForEachStarted(frame.ranges, [](const TimelineCameraShakeClip& clip) {
		if (clip.useDirection) {
			CameraSystem::AddImpulse(clip.strength, clip.direction);
		} else {
			CameraSystem::AddImpulse(clip.strength);
		}
	});
}
```

### A-13 `Systems/Animation/TimelineSystem.h`

`Update` の説明に1行足す。

```cpp
	/// <para>CameraSystem::Update より前に呼ぶ（カメラのトラックの頼みを、そのフレームのカメラに使うため）</para>
```

### A-14 `Systems/Animation/TimelineSystem.cpp`

include に足す。

```cpp
#include "MyEngine/Systems/Camera/CameraSystem.h"
#include "MyEngine/Timeline/Camera/TimelineCameraShakeTrack.h"
#include "MyEngine/Timeline/Camera/TimelineCameraTrack.h"
```

`EvaluateTracks` を差し替える。

```cpp
// ===== トラックを動かす（isApplying が false のフレームは、毎フレーム動かす種類だけ）=====
void EvaluateTracks(TimelineRuntime& runtime, const Timeline& timeline, TimeMode timeMode, bool isApplying) {
	const bool isStopped = runtime.playhead.GetState() == TimelinePlayState::Stopped;
	TimelineFrame frame;
	frame.owner = runtime.entity;
	frame.time = runtime.playhead.GetTime();
	frame.ranges = passedRanges;
	frame.events = &pendingEvents;
	frame.timeMode = timeMode;
	for (size_t i = 0; i < timeline.tracks.size(); ++i) {
		const TimelineTrack& track = *timeline.tracks[i];
		const bool isEvaluated = track.EvaluatesEveryFrame() ? !isStopped : isApplying;
		if (isEvaluated && !track.isMuted) {
			track.Evaluate(frame, *runtime.states[i]);
		}
	}
}
```

`UpdateEntity` の最後を差し替える（`Advance` の行は今のまま）。

```cpp
	EvaluateTracks(*runtime, timeline, component.timeMode, !passedRanges.empty() || runtime->needsApply);
	runtime->needsApply = false;
}
```

`ApplyPreview` の `frame` に1行、`isPreviewApplied = true;` の下に1行足す。

```cpp
	frame.isPreview = true;
```

```cpp
	CameraSystem::BeginPreviewShake(request.time);
```

`Initialize` の最後に足す。

```cpp
	TimelineTrackRegistrar<TimelineCameraTrack>("TimelineCameraTrack", "クリップごとに写すカメラを決める。クリップを重ねると補間する");
	TimelineTrackRegistrar<TimelineCameraShakeTrack>("TimelineCameraShakeTrack", "指定の時間に、カメラへ衝撃を送って揺らす");
```

### A-15 プロジェクトに足す

Visual Studio で次のファイルを追加する（`Timeline/Camera` のフォルダは新しく作る）。

```
Components/Camera/CameraPathComponent.h
Systems/Camera/CameraPath.h
Systems/Camera/CameraPath.cpp
Timeline/Camera/TimelineCameraTrack.h
Timeline/Camera/TimelineCameraTrack.cpp
Timeline/Camera/TimelineCameraShakeTrack.h
Timeline/Camera/TimelineCameraShakeTrack.cpp
```

ここで Debug をビルドする。

## パートB エディタ

カメラのアイコンと線、Timeline ウィンドウの対応を足す。

### B-1 `Graphics/Pipeline/RenderStates.h`

`DrawLayer` に足す。

```cpp
	Default,     // ふつうの物（いつも描く）
	EditorGrid,  // エディタのグリッド（Scene・Game ビューで別々に切れる。View メニュー）
	EditorGizmo, // エディタのギズモ（Scene ビューだけ描く。カメラのアイコンなど、ゲームの絵に入れない物）
```

### B-2 `Graphics/Renderer/SceneRenderer.h`

`SceneViewOptions` の `editorGrid` の下に足す。

```cpp
	bool editorGizmo = false; // エディタのギズモ（DrawLayer::EditorGizmo）を描く（Scene ビューだけ true）
```

### B-3 `Graphics/Renderer/SceneRenderer.cpp`

`ApplyViewOptions` の `EditorGrid` の行の下に足す。

```cpp
	RenderQueue::SetLayerVisible(DrawLayer::EditorGizmo, options.editorGizmo);
```

### B-4 `Editor/Viewport/EditorViewport.cpp`

`sceneOptions` の行を直す（`.sceneHidden` の前に `.editorGizmo = true` を足す。指示付き初期化は宣言の順に書く決まり）。

```cpp
	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .editorGizmo = true, .sceneHidden = false, .cameraVolume = false};
```

### B-5 `Editor/Gizmos/CameraGizmo.h`（新しいファイル）

```cpp
#pragma once

// 前方宣言
class Camera;

/// <summary>
/// カメラをエディタの Scene ビューで見せるためのギズモ（Game ビューには出さない）
/// <para>Camera Component の置いてある所のアイコンと、Camera Path の線を描く</para>
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
};
```

### B-6 `Editor/Gizmos/CameraGizmo.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "CameraGizmo.h"

#include <cstdint>
#include <span>

#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Math/Geometry/PathCurve.h"
#include "MyEngine/Systems/Camera/CameraPath.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr float kNoFadeStart = 10000.0f;                     // 距離でフェードさせないための値
constexpr float kNoFadeEnd = 20000.0f;
constexpr Vector4 kPathColor = {0.3f, 0.8f, 1.0f, 1.0f};     // 線の色（水色）
constexpr Vector4 kPositionColor = {1.0f, 0.8f, 0.2f, 1.0f}; // 線の上の今の位置に立てる線の色
constexpr float kPositionMarkHeight = 0.5f;                  // 今の位置に立てる線の高さ（m）

uint32_t iconTextureHandle = 0; // カメラのアイコン
bool isShowIcons = true;        // アイコンを出すか
bool isShowPaths = true;        // Camera Path の線を出すか
Renderer::LineListConfig lines; // 線の置き場（配列のメモリを使い回す）
PathCurve pathLine;             // 描く線（使い回す）

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
void PushPath(Handle<Entity> entity, const CameraPathComponent& path) {
	CameraPath::BuildLine(path, pathLine);
	const std::span<const Vector3> polyline = pathLine.GetPolyline();
	for (size_t i = 1; i < polyline.size(); ++i) {
		lines.lines.push_back({polyline[i - 1], polyline[i], kPathColor});
	}
	if (!pathLine.IsEmpty()) {
		const Vector3 position = CameraSystem::CalcCameraPosition(entity);
		lines.lines.push_back({position, position + Vector3{0.0f, kPositionMarkHeight, 0.0f}, kPositionColor});
	}
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void CameraGizmo::Initialize() {
	iconTextureHandle = TextureManager::Load("MyEngine/Resources/Textures/Camera.png");
	lines.fadeStartDistance = kNoFadeStart;
	lines.fadeEndDistance = kNoFadeEnd;
	lines.layer = DrawLayer::EditorGizmo;
}

//=============================================================================
// 描く
//=============================================================================
void CameraGizmo::Draw(Camera* camera) {
	if (isShowIcons) {
		EntityManager::ForEach<CameraComponent>([camera](Handle<Entity> entity, const CameraComponent&) {
			if (EntityManager::IsActiveInHierarchy(entity)) {
				DrawIcon(CameraSystem::CalcCameraPosition(entity), camera);
			}
		});
	}
	if (isShowPaths) {
		lines.camera = camera;
		lines.lines.clear();
		EntityManager::ForEach<CameraPathComponent>([](Handle<Entity> entity, const CameraPathComponent& path) {
			if (path.enabled && EntityManager::IsActiveInHierarchy(entity)) {
				PushPath(entity, path);
			}
		});
		Renderer::DrawLines(lines); // 線が0本なら中で何もしない
		lines.camera = nullptr;     // フレームをまたいで持たない
	}
}

//=============================================================================
// 表示の設定
//=============================================================================
bool& CameraGizmo::GetShowIcons() { return isShowIcons; }

bool& CameraGizmo::GetShowPaths() { return isShowPaths; }
#endif
```

### B-7 `Engine.cpp`

include に足す。

```cpp
#include "MyEngine/Editor/Gizmos/CameraGizmo.h"
```

`LightGizmo::Initialize();` の下に足す。

```cpp
#ifdef USE_IMGUI
	CameraGizmo::Initialize();
#endif
```

### B-8 `Systems/EngineSystems.cpp`

`#ifdef USE_IMGUI` の中の include に足す。

```cpp
#include "MyEngine/Editor/Gizmos/CameraGizmo.h"
```

`Draw` の `LightSystem::DrawGizmos` の下に足す。

```cpp
	CameraGizmo::Draw(gameCamera); // カメラのアイコンと Camera Path の線（Scene ビューだけ。Timeline のプレビューの値で描く）
```

### B-9 `Editor/Viewport/ScenePicking.cpp`

include に足す。

```cpp
#include "MyEngine/Editor/Gizmos/CameraGizmo.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"
```

`PickIcons` の頭を直す（`float t = 0.0f;` から下は今のまま）。

```cpp
// ===== ライト・カメラ：形が無いので、アイコンの所の小さい球 =====
// その種類の Component を持つ Entity を、球で調べる。showIcon が false の物（アイコンを出していない物）は飛ばす
template<class T> void PickIcons(const Ray& ray, ClosestHit& closest, bool (*showIcon)(const T&), Vector3 (*iconPosition)(Handle<Entity>)) {
	EntityManager::ForEach<T>([&](Handle<Entity> entity, const T& component) {
		if (!showIcon(component) || !EntityManager::IsActiveInHierarchy(entity) || EntityManager::Get<TransformComponent>(entity) == nullptr) {
			return;
		}
		const Sphere sphere = {iconPosition(entity), kIconRadius};
```

`IsCameraShown` を消して、次の2つにする。

```cpp
// ===== ライトのアイコンの位置 =====
Vector3 CalcLightIconPosition(Handle<Entity> entity) { return GetWorldPosition(*EntityManager::Get<TransformComponent>(entity)); }

// ===== カメラのアイコンを出しているか =====
bool IsCameraIconShown(const CameraComponent&) { return CameraGizmo::GetShowIcons(); }
```

`Pick` の4行を直す。

```cpp
	PickIcons<DirectionalLightComponent>(worldRay, closest, &IsLightIconShown<DirectionalLightComponent>, &CalcLightIconPosition);
	PickIcons<PointLightComponent>(worldRay, closest, &IsLightIconShown<PointLightComponent>, &CalcLightIconPosition);
	PickIcons<SpotLightComponent>(worldRay, closest, &IsLightIconShown<SpotLightComponent>, &CalcLightIconPosition);
	PickIcons<CameraComponent>(worldRay, closest, &IsCameraIconShown, &CameraSystem::CalcCameraPosition);
```

### B-10 `Editor/ImGuiManager.cpp`

include に足す。

```cpp
#include "MyEngine/Editor/Gizmos/CameraGizmo.h"
```

`DrawViewMenu` の `Light Ranges` の下に足す。

```cpp
		ImGui::MenuItem("Camera Icons", nullptr, &CameraGizmo::GetShowIcons());
		ImGui::MenuItem("Camera Paths", nullptr, &CameraGizmo::GetShowPaths());
```

### B-11 `Editor/Timeline/TimelineEditorState.h`

`SetTimelineTarget` のコメントを直す。

```cpp
// トラックの対象を entity にする（再生する Entity の子孫でなければ、/ で始まるシーンのパスで指す）
```

### B-12 `Editor/Timeline/TimelineDetails.cpp`

include を直す（`LogManager.h` は使わなくなるので消す）。

```cpp
#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Timeline/Camera/TimelineCameraTrack.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"
```

`ChannelName` の下に足す。

```cpp
// ===== シーンのカメラを選択肢に出す（onSelect は選んだ Entity を受け取る。isOutsideOnly なら、再生する Entity の外のカメラだけ）=====
template<class Function> void DrawCameraChoices(const TimelineEditorState& state, const std::string& current, bool isOutsideOnly, Function&& onSelect) {
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
```

`DrawTarget` の、再生する Entity が無いときの Target の行を直す。

```cpp
		GetInspectorUI().Field("Target###target", track.target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く。空なら再生する Entity 自身。先頭が / ならシーンの一番上から"));
```

`DrawTarget` の Combo の中を直す。

```cpp
		int id = 0;
		DrawTargetChoices(state, track, state.owner, id);
		ImGui::SeparatorText("Scene Cameras");
		DrawCameraChoices(state, track.target, true, [&](Handle<Entity> entity) { SetTimelineTarget(state, track, entity); });
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす相手（再生する Entity とその子孫・シーンのカメラ）。Hierarchy の Entity をトラックの行へドラッグすると、シーンのどの Entity でも選べる");
```

`DrawValueBinding` の項目の一覧で、名前の無い項目（Camera Volume のチェック）は保存の名前を出す。

```cpp
			if (ImGui::Selectable(std::format("{}###{}", field.label.empty() ? field.key : field.label, field.key).c_str(), field.key == track.property)) {
```

`DrawTrackFields` の `DrawTarget` を直す。

```cpp
	if (track.UsesTarget()) {
		DrawTarget(state, track);
	}
```

`DrawClipFields` の上に足して、`DrawClipFields` の頭に3行足す。

```cpp
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

// ===== クリップ全体 =====
void DrawClipFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText("Clip");
	ImGui::PushID("clip");
	if (auto* cameraTrack = dynamic_cast<TimelineCameraTrack*>(&track)) {
		DrawClipCamera(state, *cameraTrack);
	}
```

`FindTimelineBindingProblem` の頭に足す（`owner` を調べた下）。

```cpp
	if (!track.UsesTarget()) {
		return track.FindClipProblem(state.owner);
	}
```

`SetTimelineTarget` を差し替える。

```cpp
void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity) {
	auto* valueTrack = dynamic_cast<TimelineValueTrack*>(&track);
	const std::string oldAutoName = (valueTrack != nullptr) ? MakeAutoName(*valueTrack) : std::string();
	track.target = EntityManager::MakeBindingPath(state.owner, entity);
	if (valueTrack != nullptr) {
		UpdateAutoName(*valueTrack, oldAutoName);
	}
	state.MarkEdited();
}
```

### B-13 `Editor/Timeline/TimelineRows.cpp`

include に足す。

```cpp
#include "MyEngine/Components/Camera/CameraComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Timeline/Camera/TimelineCameraTrack.h"
```

定数に足す。

```cpp
constexpr float kClipLabelPadding = 6.0f;                // クリップの左端と名前のすき間
```

```cpp
constexpr ImU32 kFadeColor = IM_COL32(0, 0, 0, 90);           // クリップの補間の部分（斜めの線の外を暗くする）
constexpr ImU32 kFadeOutColor = IM_COL32(60, 130, 170, 110);  // クリップの終わった後の補間
```

`AddClipAt` の下に足す。

```cpp
// ===== カメラのクリップを足す（Hierarchy からドラッグした Camera。カメラのトラックでなければ何もしない）=====
void AddCameraClipAt(TimelineEditorState& state, int trackIndex, float time, Handle<Entity> camera) {
	auto* track = dynamic_cast<TimelineCameraTrack*>(state.GetTimeline()->tracks[trackIndex].get());
	if (track == nullptr || EntityManager::Get<CameraComponent>(camera) == nullptr) {
		return;
	}
	AddClipAt(state, trackIndex, time);
	track->clips[state.selection.clip].camera = EntityManager::MakeBindingPath(state.owner, camera);
}
```

`AcceptRowDrop` の Hierarchy の所を直す。

```cpp
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
		const Handle<Entity> entity = *static_cast<const Handle<Entity>*>(payload->Data);
		TimelineTrack& track = *state.GetTimeline()->tracks[index];
		if (track.UsesTarget()) {
			SetTimelineTarget(state, track, entity);
		} else {
			AddCameraClipAt(state, index, state.time, entity);
		}
	}
```

`FindHit` の、クリップを回す所の頭を直す。右端の判定を別のループへ出す。

```cpp
	// --- 右端のすぐ外は長さを変える（終わりにキーがあっても、後ろのクリップの下に隠れていても、ここならつかめる）---
	for (int clip = static_cast<int>(track.GetClipCount()) - 1; clip >= 0; --clip) {
		const float right = ClipRight(state, area, track, clip);
		if (track.HasClipDuration() && mouse.x > right && mouse.x <= right + kEndHitWidth) {
			hit.clip = clip;
			hit.isEnd = true;
			return hit;
		}
	}
	// --- 上に描かれている物（後ろのクリップ）から調べる ---
	for (int clip = static_cast<int>(track.GetClipCount()) - 1; clip >= 0; --clip) {
		const float left = ClipLeft(state, area, track, clip);
		const float right = ClipRight(state, area, track, clip);
```

`DrawClip` の「長さのあるクリップ」の所を直す。

```cpp
	const ImVec2 min(left, rowTop + kClipPadding);
	const ImVec2 max(ClipRight(state, area, track, clip), rowTop + kTimelineRowHeight - kClipPadding);
	drawList->AddRectFilled(min, max, kClipColor, kClipRounding);
	// --- 補間の部分：始まりは左上を斜めに暗くする。終わった後は、右へ細くなる三角 ---
	const TimelineClipFade fade = track.GetClipFade(clip);
	if (fade.in > 0.0f) {
		const float fadeX = (std::min)(min.x + fade.in * state.view.pixelsPerSecond, max.x);
		drawList->AddTriangleFilled(min, ImVec2(fadeX, min.y), ImVec2(min.x, max.y), kFadeColor);
	}
	if (fade.out > 0.0f) {
		drawList->AddTriangleFilled(ImVec2(max.x, min.y), ImVec2(max.x + fade.out * state.view.pixelsPerSecond, max.y), max, kFadeOutColor);
	}
	if (isSelected) {
		drawList->AddRect(min, max, kSelectedColor, kClipRounding, 0, 2.0f);
	}
	// --- 名前 ---
	const std::string label = track.GetClipLabel(clip);
	if (!label.empty()) {
		drawList->PushClipRect(min, max, true);
		drawList->AddText(ImVec2(min.x + kClipLabelPadding, centerY - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
		drawList->PopClipRect();
	}
```

`DrawLaneMenu` の上に足す。

```cpp
// ===== 列のドロップ：Hierarchy の Camera を、落とした時間のクリップにする =====
void AcceptLaneDrop(TimelineEditorState& state, const LaneArea& area) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int row = static_cast<int>(std::floor((mouse.y - area.min.y) / kTimelineRowHeight));
		if (row >= 0 && row < static_cast<int>(state.GetTimeline()->tracks.size())) {
			AddCameraClipAt(state, row, state.view.ToTime(mouse.x - area.min.x), *static_cast<const Handle<Entity>*>(payload->Data));
		}
	}
	ImGui::EndDragDropTarget();
}
```

`DrawTimelineRows` の `HandleView` の上に足す。

```cpp
	AcceptLaneDrop(state, area);
```

### B-14 `Editor/Gizmos/LightGizmo.h`・`.cpp`

ライトのアイコンと線も Scene ビューだけに出す。

`LightGizmo.h` のクラスの説明を直す。

```cpp
/// ライトをエディタ上で見せるための表示用のギズモ（Scene ビューだけ。Game ビューには出さない）
```

`LightGizmo.cpp` の `Initialize` の最後に足す。

```cpp
	rangeLines_.layer = DrawLayer::EditorGizmo; // Scene ビューだけに描く
```

`DrawIcon` の `icon.camera = camera_;` の下に足す。

```cpp
	icon.layer = DrawLayer::EditorGizmo; // Scene ビューだけに描く
```

### B-15 プロジェクトに足す

```
Editor/Gizmos/CameraGizmo.h
Editor/Gizmos/CameraGizmo.cpp
```

## パートC 自己テスト（消した）

最初は `Timeline/TimelineSelfTest.cpp` にカメラの確認を足していたが、2026-11-02 に SelfTest を全部エンジンから消した（`CurveSelfTest`・`TimelineSelfTest`・`ComputeSelfTest` と `ComputeTest.CS.shader`）。写し違いは答えとの比較で見つけ、確認は Claude の作業用の写しで行う。写すことは無い。

## パートD 見本

見本は `Docs/Tasks/CameraTimelineTest/` に置いた。エンジンには入れない。ゲームのプロジェクトへ写して使う。

| ファイル | 置き場所（ゲームの `project/` から） | 中身 |
|---|---|---|
| `CameraTimelineTest.scene.json` | `Resources/scenes/` | カメラ6台・線・主役・ボス・Director |
| `CameraTimelineTest.timeline` | `Resources/timelines/` | 5カットの演出 |
| `CameraRig.timeline` | `Resources/timelines/` | Prefab の中のカメラの演出 |
| `CameraRig.prefab` | `Resources/prefabs/` | カメラ2台と Timeline をまとめた Prefab |
| `CameraTimelineTester.cpp` | `Source/Scripts/CameraTest/` | キーの操作・ヒットストップ・画面の文字（プロジェクトに追加する） |

### D-1 演出の中身

| カット | 時間 | カメラ | 見どころ |
|---|---|---|---|
| 1 | 0〜2.5 秒 | `/Cameras/Cam Wide` | 通常のカメラから Blend In 0.6 秒。FOV のキーで寄る |
| 2 | 2〜5 秒 | `/Cameras/Cam Dolly` | 重なり 0.5 秒で補間。Camera Path の線に沿って動き、Camera Look At でボスを見る。被写界深度 |
| 3 | 5〜6 秒 | `/Cameras/Cam Hero` | カット。Transform のキーで寄り、Max Blur のキーでぼけを強くする |
| 4 | 6〜7.5 秒 | `/Cameras/Cam Impact` | カット。ヒットストップ 0.4 秒・衝撃・ノイズ・露出・放射ブラー |
| 5 | 7.2〜9 秒 | `/Cameras/Cam Final` | 重なり 0.3 秒で補間。Exit が Ease で 1.2 秒かけて通常のカメラへ戻る |

Director の Timeline は Time Mode が Real なので、ヒットストップの間も演出は進み、ゲームの時間で回っているボスだけが止まる。

### D-2 キー

| キー | 動き |
|---|---|
| 1 | 最初から再生 |
| 2 | 一時停止 |
| 3 | 止める（クリップの戻し方で通常のカメラへ） |
| 4 | 5.5 秒へシーク |
| 5 | Prefab のカメラを置いて再生（`Camera Rig`。パスは再生する Entity からの相対） |
| 6 | Timeline の Time Mode を Game ↔ Real（Game にすると、ヒットストップの間は演出も止まる） |
| H | 1秒のヒットストップ |

## パートE 確認

### E-1 準備

1. パートD のファイルをゲームへ写す。
2. `CameraTimelineTester.cpp` をプロジェクトへ追加する。
3. シーン `CameraTimelineTest` を開く（`Parameters/Editor/LastScene.json` か File → Open）。

### E-2 確認項目

| # | 操作 | 正しい動き |
|---|---|---|
| 1 | Debug を起動する | シーンが開き、警告が増えない |
| 2 | シーンを開いて Scene ビューを見る | カメラの位置にカメラのアイコンが出る。ライトのアイコンと線も出る。Game ビューには、どちらも出ない |
| 3 | Scene ビューのアイコンをクリック | そのカメラが選ばれる |
| 4 | View → Camera Icons を外す | アイコンが消えて、クリックでも選ばれない |
| 5 | Scene ビューを見る | Dolly Line の線が水色で出る。Cam Dolly のアイコンは Transform の位置に出て、そこから線の上の写す位置へ黄色の線が引かれ、写す位置に黄色の短い線が立つ（F-2） |
| 6 | Director を選ぶ | Timeline ウィンドウに、カメラの名前の付いたクリップが5つ並ぶ。始まりの補間が暗い三角、最後のクリップの後ろに戻す補間の三角が出る |
| 7 | 目盛りをつまんで動かす | Game ビューがカメラの結果になる。Scene ビューの錐台も動く。Inspector の値は変わらない |
| 8 | 2.25 秒の所を見る | Cam Wide と Cam Dolly の間の絵になる |
| 9 | 3〜5 秒をつまんで動かす | Cam Dolly が線に沿って動き、ボスを見続ける。背景がぼける |
| 10 | 5〜6 秒をつまんで動かす | Cam Hero が寄り、ぼけが強くなる |
| 11 | 6.03 秒の所を見る | Cam Impact に切り替わり、露出と放射ブラーが掛かり、衝撃で揺れている |
| 12 | 9.6 秒の所を見る | Cam Final と Main Camera の間の絵になる |
| 13 | Preview を切る | Game ビューが Main Camera に戻る |
| 14 | クリップを選ぶ | 詳細に Camera の一覧が出る。シーンのカメラが全部 `/` のパスで並ぶ。Target の欄は出ない |
| 15 | Hierarchy の Cam Hero を、カメラのトラックの列へドラッグ | 落とした時間にクリップができる |
| 16 | Ctrl+Z | クリップが消える |
| 17 | Hierarchy の Cam Impact を、カメラのトラックの行へドラッグ | 再生位置にクリップができる |
| 18 | Hierarchy の Camera を、値のトラックの列へドラッグ | 何も起きない |
| 19 | 重なったクリップの、前のクリップの右端のすぐ外をつまむ | 前のクリップの長さが変わる |
| 20 | クリップのカメラのパスを、ファイルで無い名前にする | トラックの行が赤くなり、マウスを乗せると理由が出る |
| 21 | 値のトラックの Target の一覧を開く | Scene Cameras の下にカメラが並ぶ |
| 22 | Play を押す | 通常のカメラから Cam Wide へ補間で入り、5カットが順に流れる |
| 23 | 6 秒の所を見る | ボスの回転だけ 0.4 秒止まる（画面の左上に「ヒットストップ中」）。カメラと揺れは止まらない |
| 24 | 最後まで待つ | 1.2 秒かけて Main Camera へ戻る。左上のカメラの名前が Main Camera になる |
| 25 | 1 のキー | 最初から再生する |
| 26 | 再生の途中で 2 のキー | 時間が止まり、カメラはそのカットのまま |
| 27 | 再生の途中で 3 のキー | クリップの Exit の設定で Main Camera へ戻る（Cut はすぐ、Cam Final は補間） |
| 28 | 4 のキー | 5.5 秒へ飛び、Cam Hero になる。戻った値がそのまま出る |
| 29 | 5 のキー | `Camera Rig` ができて、Rig Cam A → Rig Cam B と流れて戻る |
| 30 | 6 のキーで Game にして 1 のキー | ヒットストップの間は演出も止まる |
| 31 | 再生中に H のキー | Real の演出は止まらず、ボスだけ1秒止まる |
| 32 | Stop を押す | カメラが Main Camera に戻る。Cam Wide の FOV などのキーの値も元に戻る |
| 33 | Save する | シーンの値にプレビューの値が入らない |
| 34 | Release で起動する | 同じ演出が流れて Main Camera へ戻る |
| 35 | Hierarchy で Cam Dolly を選ぶ（F-2） | ギズモが Cam Dolly のアイコンの所に出る |
| 36 | Dolly Line の P0 の所を見る（F-2） | カメラのアイコンは無い（黄色の線の先と短い線だけ） |
| 37 | Wide FOV のクリップを選び、詳細の value のチェックを見る（F-3） | 押せない（最後の1つ）。マウスを乗せると理由が出る |
| 38 | Hero Push（translation）のクリップで、y のチェックを外して入れ直す（F-3） | 入れ直した y は 0 ではなく Cam Hero の今の y の平らな線になる |
| 39 | 38 の後に Ctrl+Z を2回 | 1回目でチェックを外した後（y のキー無し）、2回目で元のカーブに戻り、カーブの絵にキーが全部見える |
| 40 | Add Track を開く（F-4） | `CameraShakeTrack`・`CameraTrack`・`EventTrack`・`ValueTrack` の順に並ぶ（頭に Timeline が付かない。ImpulseTrack は消した）。足したトラックの詳細の見出しも同じ名前 |
| 41 | 40 で足したトラックを Save して、`.timeline` を開く | `"type"` は `TimelineValueTrack` など元の名前のまま |

### E-3 検証の結果

確認は写しのプロジェクト（AL4_01 の写し）で行った。

| 確認 | 結果 |
|---|---|
| Debug・Development・Release のビルド | 3構成ともエラー 0・コンパイルの警告 0（LNK4020 は写しのゲーム側の物） |
| パートA だけのビルドと起動 | Debug で成功 |
| 自己テスト（作業用の写しだけ。エンジンからは消した） | 251 / 251（Debug・Development・Release） |
| Debug と Release で同じ動き | 確認用の Script で `Time::SetDeltaTime(1.0f / 60.0f)` に固定して 840 フレーム流し、写しているカメラの名前・位置・向き・FOV・補間中か・ヒットストップ中か・再生の状態を小数4桁まで書き出した。**全フレーム一致** |
| 再生の流れ（上の記録） | 通常のカメラ → Cam Wide（Blend In）→ Cam Dolly（重なり 2〜2.5 秒）→ Cam Hero（5 秒でカット）→ Cam Impact（6 秒でカット）→ Cam Final（重なり 7.2〜7.5 秒）→ 9〜10.2 秒で Main Camera へ戻る。10.2 秒で Main Camera の位置に一致 |
| ヒットストップ | ヒットストップの間も Timeline の時間が進み、カメラが動く。Game の Timeline は止まる（自己テスト） |
| Prefab のカメラ | `CameraRig.prefab` を作ると Rig Cam A → Rig Cam B と流れて Main Camera へ戻る |
| プレビュー | 1・2.25・3.9・5.5・6.03・6.4・9.6 秒をつまんで、Game ビューの絵を撮って確認。切り替え・補間・線の移動・ぼけ・露出・放射ブラー・揺れ・戻す途中が出た。Preview を切ると Main Camera に戻る |
| カメラのアイコン | Scene ビューに出て、クリックで選べる。Game ビューには出ない。~~Camera Path のカメラは線の上に出る~~（F-2 で Transform の位置に変えた） |
| パートF（2026-11-02） | 写しで Debug・Development・Release のビルドが通り、Debug で見本のシーンが開くことを確認。**ウィンドウの操作（E-2 の 35〜41）は写しでは試していない** |
| ライトのアイコンと線 | ライトが画面に入るシーンで、Scene ビューに出て Game ビューに出ないことを絵で確認 |
| ウィンドウ | ImGui へ入力を流して確認。カメラの一覧・列と行へのドラッグ・値のトラックの列では何も起きない・Undo・重なって隠れた右端の長さ変更・Target の一覧・Camera Volume の項目の一覧・壊れたパスの赤い行 |
| ID の衝突 | Timeline ウィンドウをなぞって 0 件 |
| Stop の後 | Cam Wide の fovY・Cam Hero の translation・Cam Dolly の position がシーンの値に戻る |
| 壊れたカメラのパス | 警告は1回だけ。その時間は通常のカメラが写る |

### E-4 未確認のこと・気を付けること

**確かめていないこと**

| 項目 | 状態 |
|---|---|
| 本物のマウス・キーボード | エディタの操作は ImGui へ入力を流して確認した。Script のキー（1〜6・H）は押していない。中で呼ぶ `TimelineSystem` の操作とヒットストップは自己テストで確認した |
| AL4_01 の実物 | 確認は写しのプロジェクトで行った |
| 2つの Timeline が同時にカメラを頼む場面 | 後の頼みが勝つ、というコードの通り道だけ確認 |
| Camera Follow を付けたカメラを Real の Timeline で写す場面 | `GetCameraDeltaTime` の通り道だけ確認。見本のカメラは Camera Look At だけ |

**気を付けること**

- 値のトラックが動かしたカメラの Transform は、再生が終わっても最後のキーの値のまま（Stop で元へ戻る）。通常のカメラ自身をキーで動かすと、戻った後もその位置になる。演出には専用のカメラを置く。
- Camera Volume の値は、Inspector でチェックを入れた項目だけ効く。キーを打っても絵が変わらないときはチェックを見る。
- 衝撃の向きをランダムにすると、Play のたびに向きが変わる（プレビューはカメラの右上で固定）。演出で決まった向きにしたいときは Use Direction を入れる。
- Camera Path を付けたカメラは Transform の位置を使わない（線が無いとき・外したときだけ使う）。Camera Follow と一緒に付けると、線の上の位置が勝つ。アイコンとギズモは Transform の位置に出るので、ギズモで動かしても写す位置は変わらない（写す位置は黄色の線の先）。
- 見本の `CameraTimelineTester.cpp` はゲーム側のコードなので、エンジンの説明の決まり（特定のゲームを書かない）の外。

## パートF 直し（2026-11-02）

A・B を写して AL4_01 で動かしたときに見つかったバグを直す。F-1 は写し違い（答えと比べて見つけた）、F-2〜F-4 はコードの直し。写した後に E-2 の 35〜41 を確かめる。

**このパートは写経しない。** ユーザーの指示で、2026-11-02 に本体の作業ツリーと AL4_01 のサブモジュールへ直接入れた（未コミット）。下は何を変えたかの記録。

| # | 見つかったこと | 直す所 |
|---|---|---|
| F-1 | 写し違い2つ（Game ビューにアイコン・一時停止でカメラが戻る） | `SceneRenderer.h`・`TimelineSystem.cpp`（`TimelineRows.cpp` は動きに関わらない） |
| F-2 | Cam Dolly を選ぶとアイコンの無い所にギズモが出る / P0 の所に選べないカメラのアイコンがある | `CameraGizmo.cpp`・`ScenePicking.cpp`・`CameraSystem.h` |
| F-3 | value のチェックを外して入れ直すとカーブが直線になり、Ctrl+Z で点が1つも無くなる | `TimelineDetails.cpp`・`TimelineWindow.cpp` |
| F-4 | Add Track の名前に Timeline が付いていて長い | `TimelineTrack.h`・`.cpp`・`TimelineRows.cpp`・`TimelineDetails.cpp` |

### F-1 写し違い

**`Graphics/Renderer/SceneRenderer.h`**：`editorGizmo` の初期値を `false` にする。`true` だと Game ビューにもカメラとライトのアイコンが出る（`EditorViewport.cpp` が Scene ビューだけ `true` にする）。

```cpp
	bool editorGizmo = false; // エディタのギズモ（DrawLayer::EditorGizmo）を描く（Scene ビューだけ true）
```

**`Systems/Animation/TimelineSystem.cpp`**：`UpdateEntity` の最後の `if` を外す（中の2行は残す）。カメラのトラックは時間が止まっていても毎フレーム頼まないとカメラが戻る（1.1）。`if` があると、一時停止・最後まで行った後に頼みが止まって、通常のカメラへ戻ってしまう。値を書くかどうかは `EvaluateTracks` の4つ目の引数が決める。

```cpp
	runtime->playhead.Advance(Time::GetDeltaTime(component.timeMode) * speed, timeline.GetDuration(), component.loop, passedRanges);
	EvaluateTracks(*runtime, timeline, component.timeMode, !passedRanges.empty() || runtime->needsApply);
	runtime->needsApply = false;
}
```

**`Editor/Timeline/TimelineRows.cpp`**（動きは変わらない。消さなくても良い）：`FindHit` の、クリップを後ろから回す `for` の中の次の6行を消す。右端は、この `for` の前に全部のクリップで先に調べている（1.10）ので、ここは通らない。

```cpp
		// 右端のすぐ外は長さを変える（終わりにキーがあっても、ここならつかめる）
		if (track.HasClipDuration() && mouse.x > right && mouse.x <= right + kEndHitWidth) {
			hit.clip = clip;
			hit.isEnd = true;
			return hit;
		}
```

### F-2 カメラのアイコンを Transform の位置に

**原因：** Camera Path を付けたカメラのアイコンを、線の上の写す位置（`CameraSystem::CalcCameraPosition`）に出していた。移動のギズモは Transform の位置に出るので、Cam Dolly を選ぶとアイコンの無い所にギズモが出る。P0 の所のアイコンは Cam Dolly の物（Position が 0 なので線の始まり＝P0）で、クリックすると Cam Dolly が選ばれてギズモは別の所に出るので、選べないように見えた。

**直し方：** アイコンとクリックの位置を、ほかの Entity と同じ Transform の位置に戻す。写す位置には今までの黄色の短い線を残し、アイコンからそこへ黄色の線を引いて、どのカメラの位置か分かるようにする。

アイコンを線の上に残してギズモもそこへ動かす形はやめた。Camera Path のカメラは Transform の位置を使わないので、ギズモで動かしても何も変わらず、壊れているように見える。Transform に線の上の位置を書き込む形（Cinemachine の Dolly と同じ）は、プレビューの「Stop で元の値に戻る」・保存・Prefab の上書きに全部関わるのでやめた。

**`Editor/Gizmos/CameraGizmo.cpp`**

include に足す（`EntityManager.h` の上）。

```cpp
#include "MyEngine/Components/Core/TransformComponent.h"
```

`kPositionColor` のコメントを直す。

```cpp
constexpr Vector4 kPositionColor = {1.0f, 0.8f, 0.2f, 1.0f}; // 線の上の今の位置に立てる線と、アイコンからそこへ引く線の色
```

`PushPath` を置き換える（引数に `transform` を足して、最後に1本線を足す）。

```cpp
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
		lines.lines.push_back({position, position + Vector3{0.0f, kPositionMarkHeight, 0.0f}, kPositionColor});
		lines.lines.push_back({GetWorldPosition(transform), position, kPositionColor});
	}
}
```

`CameraGizmo::Draw` の2つの `ForEach` を置き換える（`lines.camera = camera;` などの前後はそのまま）。

```cpp
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
```

**`Editor/Viewport/ScenePicking.cpp`**

B-9 で足した include のうち、`CameraSystem.h` を消す（`CameraGizmo.h` は残す）。

```cpp
#include "MyEngine/Systems/Camera/CameraSystem.h" // ← この行を消す
```

`PickIcons` の頭を、B-9 の前の形に戻す（`float t = 0.0f;` から下は今のまま）。位置を選ぶ引数が要らなくなる。

```cpp
// ===== ライト・カメラ：形が無いので、置いてある所（アイコンの所）の小さい球 =====
// その種類の Component を持つ Entity を、球で調べる。showIcon が false の物（アイコンを出していない物）は飛ばす
// カメラも Transform の位置（Camera Path で写す位置が変わっても、ギズモと同じ所）
template<class T> void PickIcons(const Ray& ray, ClosestHit& closest, bool (*showIcon)(const T&)) {
	EntityManager::ForEach<T>([&](Handle<Entity> entity, const T& component) {
		if (!showIcon(component) || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		const Sphere sphere = {GetWorldPosition(*transform), kIconRadius};
```

`CalcLightIconPosition`（`// ===== ライトのアイコンの位置 =====` とその下の1行）を消す。`IsCameraIconShown` は残す。

`Pick` の4行を直す（最後の引数を消す）。

```cpp
	PickIcons<DirectionalLightComponent>(worldRay, closest, &IsLightIconShown<DirectionalLightComponent>);
	PickIcons<PointLightComponent>(worldRay, closest, &IsLightIconShown<PointLightComponent>);
	PickIcons<SpotLightComponent>(worldRay, closest, &IsLightIconShown<SpotLightComponent>);
	PickIcons<CameraComponent>(worldRay, closest, &IsCameraIconShown);
```

**`Systems/Camera/CameraSystem.h`**：`CalcCameraPosition` のコメントを直す（アイコンには使わなくなった）。

```cpp
	// そのカメラの本来の位置（Camera Path があれば線の上。注視点・揺れ・補間は入らない。エディタの Camera Path の印に使う）
```

### F-3 成分のチェックと Undo

**原因（3つ重なっていた）：**

1. チェックを入れ直すと、値 **0** の平らな線を作っていた。FOV が 0 の直線になる（絵も壊れる）。
2. 成分が1つの項目（FOV など）でもチェックを外せた。外すとキーが全部消えて、クリップが何も動かさなくなる。
3. Undo で中身が戻っても、カーブの絵の見ている範囲（`CurveEditor` が最初に描くときに決める）は前のまま。入れ直したときに 0 の付近に合わせた範囲が残るので、元のカーブに戻ってもキーが絵の外にあり、点が1つも見えない。

Undo は1回で1つの操作を戻す。「外す → 入れる」の後の Ctrl+Z の1回目は「外した後」（キーが無い）に戻り、2回目で元のカーブに戻る。2回目で戻ったカーブが見えなかったのが 3 の原因。

**直し方：** 入れ直したら対象の今の値の平らな線にする（新しいクリップと同じ）。キーのある成分が1つだけなら、そのチェックは押せなくする。Undo で中身が変わったら `CurveEditor` を作り直して、見ている範囲を合わせ直す。

**`Editor/Timeline/TimelineDetails.cpp`**

`FindField` の下に足す（`FillNewClipCurves` の中にあった「今の値を読む」を関数にした）。

```cpp
// ===== 対象の今の値（読めなければ全部 0 で、最初の成分だけ使う）=====
ComponentValue ReadTargetValue(const TimelineEditorState& state, const TimelineTrack& track) {
	ComponentValue value;
	value.isUsed[0] = true;
	if (const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track)) {
		const Handle<Entity> target = EntityManager::FindByPath(state.owner, valueTrack->target);
		if (target.IsValid()) {
			ComponentSerializer::GetValue(target, valueTrack->component, valueTrack->property, value);
		}
	}
	return value;
}
```

`DrawChannels` の `ImGui::PushID("channels");` から `ImGui::SameLine();`（チェックの後、名前の `Selectable` の前）までを置き換える。

```cpp
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
```

- `curves.first(count)`：`std::span` の先頭 count 個だけを見る span。項目の成分の数（FOV なら 1）より後ろの成分は数えない。
- `BeginDisabled`〜`EndDisabled` の間の項目は押せず、薄く描かれる。押せない項目は `SetItemTooltip` では説明が出ないので、`ImGuiHoveredFlags_AllowWhenDisabled` を付けて自分で調べる。

`FillNewClipCurves` の「今の値を読む」所（`// --- 対象の今の値を読む` から `}` まで の9行）を、1行にする。

```cpp
	const ComponentValue value = ReadTargetValue(state, track);
	for (size_t i = 0; i < curves.size(); ++i) {
```

**`Editor/Timeline/TimelineWindow.cpp`**：`SyncRevision` の `state.ClampSelection();` の下に足す。

```cpp
		state.curveEditor = CurveEditor{}; // カーブの中身も入れ替わったので、見ている範囲を合わせ直す（前の範囲のままだと、戻ったキーが絵の外に出て見えない）
```

`SyncRevision` は、版の番号が自分の知らない形で増えたとき（Undo・Redo）だけ中に入る。自分で直したときは `MarkEdited` が版を覚えるので入らない。カーブの絵をドラッグして離すたびに範囲が戻ることは無い。

### F-4 トラックの名前

ファイルの `"type"` はクラス名のまま変えない（変えると、前に保存した `.timeline` のトラックが「知らない種類」になる）。ウィンドウに出すときだけ、頭の `Timeline` を省く。ゲーム側の `ImpulseTrack` は頭に付いていないのでそのまま。並びも省いた名前の順にする。

**`Timeline/TimelineTrack.h`**：`TimelineTrackRegistry` の `GetNames` のコメントを直して、`GetDescription` の下に足す。

```cpp
	// 名前の一覧（GetDisplayName の順）
	static std::vector<std::string> GetNames();
	// 説明（知らない名前・書いていなければ空）
	static const std::string& GetDescription(std::string_view name);
	// ウィンドウに出す名前（頭の "Timeline" を省く。"TimelineValueTrack" → "ValueTrack"。ファイルの "type" は元の名前のまま）
	static std::string_view GetDisplayName(std::string_view name);
```

**`Timeline/TimelineTrack.cpp`**

無名 namespace の `kEmptyDescription` の下に足す。

```cpp
constexpr std::string_view kOmittedPrefix = "Timeline"; // ウィンドウに出すときに省く名前の頭（エンジンのトラックは Timeline〇〇Track）
```

`GetNames` の `std::ranges::sort(names);` を直す（1.11 のプロジェクション）。

```cpp
	std::ranges::sort(names, {}, [](const std::string& name) { return GetDisplayName(name); }); // ウィンドウに出す名前の順に並べる
```

ファイルの最後に足す。

```cpp
std::string_view TimelineTrackRegistry::GetDisplayName(std::string_view name) {
	// "Timeline" そのものは、省くと空になるのでそのまま
	return (name.starts_with(kOmittedPrefix) && name.size() > kOmittedPrefix.size()) ? name.substr(kOmittedPrefix.size()) : name;
}
```

**`Editor/Timeline/TimelineRows.cpp`**：`DrawTimelineRuler` の Add Track の `Selectable` の行を直す。

```cpp
			// 見せるのは頭の Timeline を省いた名前。ID は元の名前（省いた名前がゲームの種類と同じになっても衝突しない）
			if (ImGui::Selectable(std::format("{}###{}", TimelineTrackRegistry::GetDisplayName(name), name).c_str())) {
```

`###` の後ろが ImGui の ID になる（前は見せる文字だけ）。ゲームが `ValueTrack` という種類を足すと、見せる名前が `TimelineValueTrack` と同じになるが、ID は元の名前なのでぶつからない。

**`Editor/Timeline/TimelineDetails.cpp`**：`DrawTrackFields` の見出しも同じ名前にする。

```cpp
	ImGui::SeparatorText(std::string(TimelineTrackRegistry::GetDisplayName(track.typeName)).c_str());
```

（`string_view` は最後に `\0` がある保証が無いので、`c_str()` のために `std::string` にする。）
