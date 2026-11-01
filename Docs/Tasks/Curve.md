参考：
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- 3次ベジェでイージングを表す決まり（x の制御点を 0〜1 にする）：CSS Easing Functions Level 1 の cubic-bezier：https://www.w3.org/TR/css-easing-1/#cubic-bezier-easing-functions
- 時間からベジェの媒介変数を解く実装の例（Newton 法のあとに二分法）：WebKit の UnitBezier：https://github.com/WebKit/WebKit/blob/main/Source/WebCore/platform/graphics/UnitBezier.h
- 線の上で分ける方法：de Casteljau's algorithm：https://en.wikipedia.org/wiki/De_Casteljau%27s_algorithm
- 両端の値と傾きから3次式を作る方法：Cubic Hermite spline：https://en.wikipedia.org/wiki/Cubic_Hermite_spline
- Newton 法：https://en.wikipedia.org/wiki/Newton%27s_method
- 片側の点から傾きを求める式：Finite difference coefficient：https://en.wikipedia.org/wiki/Finite_difference_coefficient
- カーブエディタの先例：Unity の AnimationCurve：https://docs.unity3d.com/ScriptReference/AnimationCurve.html
- `std::ranges::upper_bound`：https://en.cppreference.com/w/cpp/algorithm/ranges/upper_bound
- `std::ranges::equal_range`：https://en.cppreference.com/w/cpp/algorithm/ranges/equal_range
- `std::ranges::stable_sort`：https://en.cppreference.com/w/cpp/algorithm/ranges/stable_sort
- `std::optional`：https://en.cppreference.com/w/cpp/utility/optional
- `std::exchange`：https://en.cppreference.com/w/cpp/utility/exchange
- `std::mersenne_twister_engine`（`std::mt19937`）：https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
- Dear ImGui の `SetItemKeyOwner`（`imgui.h` の Inputs Utilities の説明）：https://github.com/ocornut/imgui/blob/master/imgui.h

# UI 段階2：ベジェのカーブとカーブエディタ（Curve）

時間に対して値がどう変わるかを、3次ベジェのカーブで表す。UI 段階3の Timeline が使う部品なので、UI もカメラも知らない汎用の部品にする。

作る物は、カーブのデータと計算（`Curve`）、ファイルの読み書き（`CurveManager`、拡張子 `.curve`）、ImGui のカーブエディタ（`CurveEditor`）、計算の自動の確認（`CurveSelfTest`）の4つ。あわせて、壊れたファイルを選ぶとログが埋まる既存の不具合を `AssetInspector` で直す（1.10）。

パートは **A → B → C の順に写す**。途中で止めてもよい（B の後で Debug のビルドと起動が通ることを確かめた。A は新しいファイルだけなので、ほかのファイルに影響しない）。D は確認の手順。

作業の前に、develop からブランチを切る。

```bash
git switch -c feature/Curve develop
```

## 0章 全体の形

### 0.1 流れ

```
Curve（キーの並び・Evaluate。計算だけ。ImGui も JSON もファイルも知らない）
   ▲ 読む・書き換える
   │
   ├── CurveManager：.curve のファイルを読む・持つ・保存する。ToJson / FromJson
   │
   ├── CurveEditor：ImGui の部品。Curve だけを知る。ファイルも Undo も知らない
   │      │ 1回の操作の前と後（TakeFinishedEdit）
   │      ▼
   └── CurveAssetEditor：Inspector の係。CurveManager へ書き戻して、EditorHistory::Record に積む
```

`Curve` が下の土台で、上の3つが `Curve` を使う。段階3の Timeline は `Curve` と `ToJson`・`FromJson` と `CurveEditor` をそのまま使える。`CurveAssetEditor` は `.curve` のファイル専用で、Timeline は自分の係を書く。

### 0.2 作るファイル

| ファイル | 中身 |
|---|---|
| `Curve/Curve.h`・`.cpp` | カーブのデータ、Evaluate、キーの足し引き、Ease から作る |
| `Curve/CurveManager.h`・`.cpp` | `.curve` の読み書き、JSON、`DescribeKey`（見せ方） |
| `Curve/CurveSelfTest.h`・`.cpp` | 計算の自動の確認（Debug の起動時に1回） |
| `Editor/Widgets/CurveEditor.h`・`.cpp` | ImGui のカーブエディタ |
| `Editor/Inspector/CurveAssetEditor.h`・`.cpp` | `.curve` を Inspector で直す係 |
| `Editor/Inspector/AssetInspector.h`・`.cpp`（直す） | 読めないファイルの扱いを1か所にする |
| `Editor/Inspector/AttackTableEditor.cpp`・`Physics/PhysicMaterialManager.cpp`（直す） | 上に合わせて `CanLoad` を足す |

`Math/` は下のフォルダを除いてもファイルが 15 を超えているので足さず、`Curve/` を新しく作った。`CurveManager` が JSON と `ComponentUI` を使うので、`Math/` に置くと数学のフォルダが保存の仕組みを知ることになる。

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 型の名前 | `Curve`（3D の道の `PathCurve` とは別物） | 計画の言葉のまま短く。`AnimationCurve` にすると、骨のアニメーションの `Animation〜` と紛らわしい |
| データの持ち方 | `Curve` は `vector<CurveKey>` を private で持つクラス | 「時間の順」「有限の値」「ハンドルが範囲内」を、書き換える関数の中で必ず守れる。外から直接並びを壊せないので、`Evaluate` が余計な確認をしなくて済む |
| Component に持たせない | ファイルの番号（`AssetType::Curve`）で持つ | キーの数が変わる `vector` は、Component（バイトで写す）に入れられない。`AttackTable` と同じ |
| ハンドルの持ち方 | キーからの差（x：時間、y：値） | キーを動かすとハンドルも一緒に動く。保存するファイルも読みやすい |
| 補間の種類 | キーごとに持つ。区間の補間は**左のキー**で決める | Blender などと同じ。最後のキーの `interp` は使わない |
| 値の上限 | 時間と値は ±1000万（`kMaxValue`） | 足し算で無限大にならず、計算の途中で NaN にならない |
| NaN・無限大 | 入ってきたら直す（NaN は 0、大きすぎる値は上限へ） | ファイルや入力欄から入っても、以後の計算を守る |
| ハンドルの制限 | 時間の長さを隣のキーまでに収める。縮めるときは値も同じ割合で縮める | 1.3 の条件で時間が逆戻りしない。値も縮めるので傾きが変わらない |
| 同じ時間のキー | 認める。ちょうどその時間は後ろのキーの値 | 値の段差を表せる。Evaluate の規則が1つで済む |
| カーブの端より外 | 端のキーの値のまま | くり返しは Timeline の時間の側で作れる |
| キーが0個 | 認める。Evaluate は 0 | 全部消しても壊れない。ダブルクリックで足し直せる |
| Ease からの作り方 | `Ease::Apply` の値から作る（1.5） | 14 種類を同じ処理で扱える。Ease の種類が増えても、ここを直さなくて済む |
| プリセットの入れ先 | キーを選んでいて次のキーがあれば、その区間。それ以外は全体 | 段階3以降は区間ごとに動きを付ける使い方が主になる |
| キーを追い越したとき | 並びが変わる（番号も変わる）。打ち込みでも同じ | 打った時間を勝手に変えない方が分かりやすい |
| エディタの置き場所 | 今は Inspector（`.curve` を Project で選ぶ）。部品は `CurveEditor` に分ける | 段階4の Timeline ウィンドウに、同じ部品を置ける |
| キーの選択 | 1個だけ | この段階の頼みは1キーの編集。範囲選択は Timeline の操作と一緒に考える |
| 色 | 背景・目盛り・キーはテーマの色、線と選択は既存の水色と橙 | エディタの色はリニアなので、暗い色を `IM_COL32` で書くと明るく出る |
| Undo | 部品が操作の前後を渡し、持ち主が `EditorHistory::Record` に積む | 部品はファイルを知らないので、戻し方は持ち主しか書けない。Timeline は自分の戻し方で積める |
| 保存のタイミング | 既存の `AssetInspector`（どの項目も操作していないとき） | ドラッグの間に毎フレームファイルを書かない |
| 読めないファイル | 読めるかは `AssetInspector` が選んだときに1回だけ調べる。読めなければ係の `Draw` を呼ばない | 3つの係に同じ処理を書かずに済む（1.10） |
| 確認の方法 | Debug の起動時に自動で確かめる（`ComputeSelfTest` と同じ形） | 計算の値は人の目では確かめきれない。壊すと起動で気付ける |

### 0.4 やらなかったこと

| 項目 | 理由 |
|---|---|
| Camera の Blend で `.curve` を選ぶ | カメラ段階2の作業。UI やカメラを知らない部品に留める |
| 既存の `Ease::Type` の項目を Curve へ置き換える | 既存の Component と保存データを変える話で、この段階の頼みではない |
| 複数のキーの選択・コピー | 上の表のとおり。Timeline と一緒に考える |
| 右クリックのメニュー | 補間の種類は下の数値の欄で変えられる。操作の道が2つになる分を減らした |
| 値や時間の吸着（スナップ） | 数値の入力で代わりになる。Timeline のフレームと一緒に考える |

### 0.5 あとの段階への備え

| 段階 | 今の形で残した物 |
|---|---|
| UI 段階3（Timeline の再生） | `Curve` は UI を知らない。`CurveManager::ToJson`・`FromJson` は `.curve` のファイルと関係なく呼べるので、`.timeline` の中にカーブを埋められる。`Evaluate` は `const` で、作り置きの状態を持たない |
| UI 段階4（Timeline ウィンドウ） | `CurveEditor` は置き場所を知らない。絵の高さは引数、数値の欄は `ComponentUI`、Undo は `TakeFinishedEdit` で持ち主に渡す |
| UI 段階5（UI の統合） | 値を決めるのは `Evaluate` だけなので、`VectorShapeComponent` などへ書くのは呼ぶ側の話になる |
| カメラ段階2 | `CameraBlendStyle` に `Curve` を足して、`blendEase` の代わりに `Evaluate` を呼べる。`AssetType::Curve` があるので Inspector でファイルを選べる |

## 1章 解説

### 1.1 カーブの形

キーを時間の順に並べて、隣のキーの間（区間）を補間でつなぐ。区間の補間は左のキーの `interp` で決める。

```
値
│        ●key1 ─ ─ ─ ─ ●key2
│      ／                （key1 の interp が Constant なら、key2 の時間まで同じ値）
│    ／  key0 の interp が Bezier なら、ベジェで曲がる
│  ●key0
└──────────────────────── 時間
```

ベジェの区間は、左のキーの出のハンドルと、右のキーの入りのハンドルを使う。

```
制御点 p0 = key0 の位置
       p1 = key0 の位置 + key0.outHandle
       p2 = key1 の位置 + key1.inHandle
       p3 = key1 の位置
```

### 1.2 時間から値を求める

3次ベジェは、媒介変数 s（0〜1）から点を作る式で、時間から直接は値が出ない。

```
点(s) = (1-s)³ p0 + 3(1-s)²s p1 + 3(1-s)s² p2 + s³ p3
```

なので2段階で求める。

1. 時間の成分だけの式で、`点(s).x = 時間` になる s を解く。3次式を解く作業になる。
2. その s を値の成分の式へ入れる。

計算を楽にするため、区間の時間を 0〜1 に直して解く。時間の制御点は `0, x1, x2, 1` になる（`x1 = out.x ÷ 区間の長さ`、`x2 = 1 + in.x ÷ 区間の長さ`）。CSS の `cubic-bezier(x1, y1, x2, y2)` と同じ形。

### 1.3 時間が逆戻りしない条件

「時間から値を求める」には、1つの時間に1つの値だけが決まらないといけない。ハンドルが長すぎるとカーブが後ろへ折り返し、同じ時間に値が2つ出てしまう。

```
折り返した例（これを防ぐ）

値   ／￣＼
│  ／    ＼         同じ時間の縦の線が、線と3回交わる
│／        ＼
└────────────── 時間
```

**x1 と x2 が 0〜1 の中なら、時間は s に対して減らない。** 時間の式を s で微分すると次の形になる。

```
x'(s) ÷ 3 = (1-s)²・x1 + 2s(1-s)・(x2 - x1) + s²・(1 - x2)
```

- x1 ≤ x2 なら、3つの項が全部 0 以上。
- x1 > x2（ハンドルの先が交差する）でも、s の2次式として判別式を調べると、0〜1 の範囲では負にならない。一番苦しい x1 = 1, x2 = 0 のときに `(1 - 2s)²` となり、s = 0.5 で傾きが 0 に触れるだけ。

なので、出のハンドルの時間を `0〜区間の長さ`、入りのハンドルの時間を `-区間の長さ〜0` に収めれば足りる。向かい合う2本のハンドルの長さの合計を制限する必要は無い。

この「傾きが 0 に触れる」場合があるので、解き方は傾きで割るだけでは足りない（次の節）。

ハンドルを切るときに時間の成分だけ切ると傾きが変わるので、値の成分も同じ割合で縮める（`LimitHandle`）。

### 1.4 Newton 法と二分法

`x(s) = 目標の時間` を解く。

- **Newton 法：** 今の場所の傾きで線を引いて、目標の高さと交わる場所へ進む。`次の s = s - 差 ÷ 傾き`。速いが、傾きが 0 に近いと遠くへ飛ぶ。
- **二分法：** 答えのある範囲を半分ずつ狭める。遅いが必ず近づく。

`SolveBezierParam` は2つを組み合わせる。`x(s)` は s に対して減らないので、差の符号を見れば答えが s の左右どちらにあるか分かる。毎回その範囲（`low`・`high`）を狭めておき、Newton 法の次の場所がその範囲の外へ出るとき（傾きが小さいときを含む）だけ、範囲の真ん中へ進む。

- くり返しの回数に上限（32回）があるので、必ず終わる。float の精度なら、二分法だけでも 24 回で限界まで狭まる。
- 0 で割らない。`next` の初期値 `-1` は範囲の外なので、傾きが小さいときは必ず二分法になる。

最初の予想 `s = target` は、ハンドルが等間隔（x1 = 1/3, x2 = 2/3）なら `x(s) = s` となり、そのまま答えになる。

### 1.5 Ease をカーブに写す

`Ease::Type` の 14 種類の式は、3次式で書ける物と書けない物がある。

| 種類 | 式 | カーブ |
|---|---|---|
| Linear・InQuad・OutQuad・InCubic・OutCubic・InBack・OutBack | 3次以下の1本の式 | 1区間でぴったり |
| InOutCubic | 3次の式2本 | 2区間でぴったり |
| OutBounce・InBounce | 2次の式4本 | 4区間でぴったり |
| InSine・OutSine・InOutSine | 三角関数 | 近似（3〜4区間） |
| OutElastic | 指数と三角関数 | 近似（20区間） |

種類ごとに式を書き分けず、1つの方法で全部を作る。

1. 区間の両端の**値と傾き**を `Ease::Apply` から測る。
2. 両端の値と傾きが合う3次式（Hermite 補間）をベジェで作る。ハンドルの時間を長さの 1/3 にすると、時間が s に比例する（`x(s) = s`）ので、値の式がそのまま時間の3次式になる。傾き m のハンドルは `(長さ÷3, m×長さ÷3)`。
3. 区間の中の 15 点で Ease との差を測り、許容（0.0005）より大きければ区間を半分に分けてやり直す。

3次以下の式はこの方法で1区間で通る。2次の式4本の Bounce は、切れ目で傾きが急に変わるので、式が切り替わる場所を `JointsOf` に書いて最初から分ける。書かなくても半分に分け続ければ近づくが、切れ目のまわりにキーが増える。

**傾きの測り方** が大事な所で、区間の端そのものの値を使わない。

- 外側を読むと、Bounce の切れ目で隣の式の傾きが混ざる。
- 端の点を読むと、Elastic の `t = 1` の値の飛びを拾う（`OutElastic` は式の途中の値が 1.00049 で、`t = 1` のときだけ 1.0 を返す。この差を 1/256 の幅で割ると、傾きが大きく狂う）。

なので区間の内側の3点から2次式を作り、端まで延ばした傾きを使う（`CalcEaseSlope`）。

```
傾き = (-5 f(端+h) + 8 f(端+2h) - 3 f(端+3h)) ÷ 2h
```

h に負の値を渡すと、終わりの端の傾きも同じ式で求まる。

できあがりは表のとおり（`Evaluate` と `Ease::Apply` を 20001 点で比べた一番大きな差）。

| 種類 | キーの数 | 差の最大 |
|---|---|---|
| Linear | 2 | 0.000000 |
| InQuad | 2 | 0.000000 |
| OutQuad | 2 | 0.000000 |
| InCubic | 2 | 0.000018 |
| OutCubic | 2 | 0.000018 |
| InOutCubic | 3 | 0.000009 |
| InSine | 4 | 0.000368 |
| OutSine | 4 | 0.000383 |
| InOutSine | 5 | 0.000451 |
| InBack | 2 | 0.000054 |
| OutBack | 2 | 0.000058 |
| OutBounce | 5 | 0.000018 |
| InBounce | 5 | 0.000035 |
| OutElastic | 21 | 0.000488（`t = 1` の値の飛びの分） |

### 1.6 線の上にキーを足す

ダブルクリックが線の近くなら、カーブの形を変えずにキーを足す。ベジェは s の場所で2本に分けられる（de Casteljau 法）。制御点の間を s の割合で分ける作業を3回くり返す。

```
p0   p1   p2   p3        元の制御点
  p01  p12  p23          隣どうしを s で分ける
    p012  p123           もう1回
      middle             もう1回。これが線の上の点

前半のベジェ：p0, p01, p012, middle
後半のベジェ：middle, p123, p23, p3
```

`a.outHandle` を `p01 - p0`、足すキーの入りを `p012 - middle`、出を `p123 - middle`、`b.inHandle` を `p23 - p3` にする。2本を合わせた形は元と同じで、新しいキーの入りと出は同じ傾きになる。

### 1.7 同じ時間のキー

`Evaluate` は `upper_bound` で「time より後の最初のキー」を探す。同じ時間のキーが続いても、見つかるのは一番後ろのキーの次なので、`a` は同じ時間のキーの一番後ろになる。これで規則が「ちょうどその時間は後ろのキーの値」の1つで済み、長さ 0 の区間で割り算をすることも無い（`a.time <= time < b.time` なので、`b.time - a.time` は 0 にならない）。

### 1.8 操作の前のカーブから作り直す

キーをドラッグして隣へ近づけると、区間が縮んでハンドルも縮む。そのまま毎フレーム今のカーブを直していくと、離れる方へ戻してもハンドルは短いまま残ってしまう。

そこで `CurveEditor` は、操作を始めたときのカーブ（`editBefore_`）を持ち、毎フレーム**操作の前のカーブから作り直す**。

```
つまんだ：editBefore_ = 今のカーブ
動かした：curve = editBefore_ → SetKey で今のマウスの場所へ（毎フレーム）
離した　：editBefore_ と curve が違えば、1件の操作として渡す
```

途中でハンドルが縮んでも、戻せば元の長さになる。キーが隣を追い越して番号が変わっても、`editBefore_` の中の番号（`editKey_`）は変わらないので迷わない。

操作の終わりは、Component の Undo と同じく「どの項目も操作していない（`IsAnyItemActive` が false）」で決める。ドラッグ・数値の欄・足す・消す・プリセットを1か所で扱える。

ダブルクリックで足した直後だけは、続けてつまめない。`editBefore_` に足したキーが入っていないので、作り直すと消えてしまうため。

### 1.9 Aligned は傾きでそろえる

時間と値は単位が違う（時間 1 秒と値 100 が同じ長さに見えることがある）。画面の見た目の向きでそろえると、拡大縮小で結果が変わるので、**傾き（値 ÷ 時間）** でそろえる。反対のハンドルは時間の長さを変えず、値だけ `時間の長さ × 傾き` にする。

### 1.10 読めないファイルの扱い

今までは、係の `Draw` が毎フレーム `〇〇Manager::Load` を呼んでいた。読めたファイルは番号を覚えているので2回目からは読まないが、読めなかったファイルは覚えていないので、毎フレーム読み直してログにエラーを書く。`.curve` で見つけて、`.attacktable`・`.physmat` も同じ形だった。

3つの係それぞれに「読めなかった印」を持たせると同じ処理が3か所に増えるので、`AssetInspector` にまとめる。

```
クリック → AssetInspector::Select：needsLoadCheck = true
   ▼ 次に描くとき
AssetInspector::Draw：needsLoadCheck なら CanLoad を1回だけ呼ぶ
   ├ 読めた   → いつもの Draw
   └ 読めない → 「読めません」と出す。係の Draw は呼ばない
```

- 係は `CanLoad` に1行書くだけ。`.uiscreen` の係はここで中身を読まないので、書かなくてよい（既定は true）。
- クリックのたびに調べ直すので、ファイルを直してクリックし直せば読める。
- `Manager::Load` の側で「読めなかった」と覚える形にしなかったのは、覚えると直したファイルが読めなくなるのと、3つの Manager を直すことになるため。

### 1.11 使った STL・ImGui の解説

**`std::ranges::upper_bound(keys_, time, {}, &CurveKey::time)`**
並んだ範囲を二分探索して、`time` より大きい最初の要素を返す。`{}` は比べ方（既定の `<`）、最後の引数は射影で、各要素の `.time` を比べる。キーが増えても調べる回数は log₂(キーの数) で済む。

**`std::ranges::equal_range`**
`time` と等しい要素が並ぶ範囲を返す。`SetKey` で「同じ時間のキーの間で、今の番号に一番近い場所」を求めるのに使う。これで値だけ変えたときに、同じ時間のキーの順番が入れ替わらない。

**`std::ranges::stable_sort`**
等しい要素の順番を変えずに並べる。`std::sort` は順番が変わることがあり、同じ時間のキーの段差の向きが逆になってしまう。

**`std::optional<T>`**
「値がある」か「無い」かを持つ。`editBefore_` は操作をしていない間は空。`nullptr` の代わりに使えて、`new` が要らない。

**`std::exchange(a, b)`**
`a` に `b` を入れて、前の `a` の値を返す。`TakeFinishedEdit` で「返して、空にする」を1行で書ける。

**`std::mt19937`**
メルセンヌ・ツイスタという乱数の生成器。種を固定すると毎回同じ乱数の列になるので、自動の確認の結果が実行ごとに変わらない。`std::uniform_real_distribution` が範囲の中の float に直す。

**`(std::min)({a, b, c})`**
`{ }` で渡すと3つ以上を比べられる。`( )` で囲むのは、`windows.h` の `min` マクロに邪魔されないため。

**`ImGui::InvisibleButton`**
見えないボタン。絵を描く場所に置くと、クリックやドラッグを受け取れて、つまんでいる間は `IsItemActive`・`IsAnyItemActive` が true になる。ウィンドウも動かない。

**`ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY)`**
マウスが直前の項目の上にある間、ホイールをその項目の物にする。これが無いと、絵の上でホイールを回したときに Inspector もスクロールしてしまう。

## パートA カーブの計算

ファイルを足したら、VS のソリューションエクスプローラーで `MyEngine_v1` プロジェクトに追加する。フィルタは `project` フォルダで次を実行すると、フォルダの形に作り直される。

```bash
powershell -ExecutionPolicy Bypass -File Tools\UpdateFilters.ps1 MyEngine_v1.vcxproj
```

### A-1 `Curve/Curve.h`（新しいファイル）

```cpp
#pragma once
#include <vector>

#include "MyEngine/Math/Easing.h"
#include "MyEngine/Math/Vector2.h"

/// <summary>
/// キーから次のキーまでの補間の種類
/// </summary>
enum class CurveInterp {
	Constant, // 次のキーの時間まで、今の値のまま
	Linear,   // 次のキーまで直線
	Bezier,   // 3次ベジェ（このキーの出のハンドルと、次のキーの入りのハンドルを使う）
};

/// <summary>
/// 1つのキーの、入りと出のハンドルの関係
/// </summary>
enum class CurveHandleMode {
	Aligned, // 片方を動かすと、もう片方も同じ傾きになる（なめらかにつながる）
	Free,    // 別々に動く（折れ目を作れる）
};

/// <summary>
/// キーのどちら側のハンドルか
/// </summary>
enum class CurveHandleSide {
	In,  // 入り（キーより前の時間）
	Out, // 出（キーより後の時間）
};

/// <summary>
/// カーブのキー1つ
/// </summary>
struct CurveKey {
	float time = 0.0f;                                     // 時間
	float value = 0.0f;                                    // 値
	Vector2 inHandle = {0.0f, 0.0f};                       // 入りのハンドル。キーからの差（x：時間、y：値）。x は 0 以下
	Vector2 outHandle = {0.0f, 0.0f};                      // 出のハンドル。キーからの差（x：時間、y：値）。x は 0 以上
	CurveInterp interp = CurveInterp::Bezier;              // このキーから次のキーまでの補間（最後のキーでは使わない）
	CurveHandleMode handleMode = CurveHandleMode::Aligned; // 入りと出のハンドルの関係

	bool operator==(const CurveKey&) const = default;
};


/// <summary>
/// 時間に対して値がどう変わるかを表すカーブ（キーの間を 3次ベジェ・直線・一定でつなぐ）
/// <para>線に沿って進む PathCurve（3D の道）とは別物。こちらは「時間 → 値」の関数</para>
/// <para>次の3つをいつも守る（書き換えは全部関数を通すので、外から壊せない）</para>
/// <para>・キーは時間の順に並ぶ（同じ時間のキーがあってもよい＝値の段差）</para>
/// <para>・時間・値・ハンドルは有限で、±kMaxValue の中にある</para>
/// <para>・ハンドルの時間の長さは、隣のキーまでの長さを超えない（これで時間が逆戻りしない）</para>
/// </summary>
class Curve {
public:
	static constexpr float kMaxValue = 10000000.0f; // 時間と値の絶対値の上限（足し算で無限大にならず、float の精度が残る大きさ）

	// ===== 作る =====
	Curve() = default;
	// キーの並びから作る（ファイルから読むときなど。並び・NaN・ハンドルの範囲はここで直す）
	explicit Curve(std::vector<CurveKey> keys);
	/// <summary>
	/// Ease::Apply と同じ形のカーブを作る（時間 0〜1、値 0〜1）
	/// <para>1つのベジェで表せない形は、許容の差に収まるまでキーを増やす</para>
	/// </summary>
	static Curve FromEase(Ease::Type type);

	// ===== 読む =====
	/// <summary>
	/// 時間の値を求める。キーが無ければ 0
	/// <para>最初のキーより前は最初の値、最後のキーより後は最後の値。同じ時間のキーでは、ちょうどその時間は後ろのキーの値</para>
	/// <para>NaN を渡すと最初の値を返す</para>
	/// </summary>
	float Evaluate(float time) const;
	const std::vector<CurveKey>& GetKeys() const { return keys_; }

	// ===== 書き換える =====
	/// <summary>
	/// キーを足す。ハンドルは前後のキーを結ぶ傾きで作る
	/// </summary>
	/// <returns>足したキーの番号</returns>
	int AddKey(float time, float value);
	/// <summary>
	/// カーブの形を変えずに、線の上へキーを足す（ベジェは2つに分ける）。ちょうどその時間のキーがあれば、足さない
	/// </summary>
	/// <returns>足したキー（または、もとからあったキー）の番号</returns>
	int InsertKeyOnCurve(float time);
	// キーを消す（知らない番号なら何もしない）
	void RemoveKey(int index);
	/// <summary>
	/// キーの中身を置き換える。時間が変わって隣を追い越したら、並びも変わる（同じ時間の間では順番を変えない）
	/// </summary>
	/// <returns>置き換えた後のキーの番号（知らない番号なら index のまま）</returns>
	int SetKey(int index, const CurveKey& key);
	// ハンドルを動かす（Aligned なら反対のハンドルの傾きもそろえる）。handle はキーからの差
	void SetHandle(int index, CurveHandleSide side, const Vector2& handle);
	// index のキーから次のキーまでを、Ease の形にする（両端のキーの時間と値は変えない。間にキーが増えることがある）
	void ApplyEaseToSegment(int index, Ease::Type type);
	// 全体を Ease の形にする（最初と最後のキーの間。キーが2つより少なければ、時間 0〜1・値 0〜1 で作る）
	void ApplyEaseToAll(Ease::Type type);

	bool operator==(const Curve&) const = default;

private:
	bool IsValidIndex(int index) const { return index >= 0 && index < static_cast<int>(keys_.size()); }
	// time のキーを入れる場所の番号（同じ時間のキーの後ろ）
	int FindInsertIndex(float time) const;
	// 全部のキーのハンドルを、隣のキーまでの長さに収める
	void ClampHandles();

	std::vector<CurveKey> keys_; // 時間の順に並ぶ
};
```

### A-2 `Curve/Curve.cpp`（新しいファイル）

```cpp
#include "Curve.h"

#include <array>
#include <cmath>
#include <span>
#include <utility>
#include <algorithm>

namespace {
constexpr int kMaxSolveIterations = 32;      // ベジェを解く回数の上限（float なら、二分法だけでも 24 回で精度の限界まで狭まる）
constexpr float kSolveTolerance = 0.000001f; // 解けたとみなす時間の差（区間の長さを 1 とした割合）
constexpr float kMinSolveSlope = 0.000001f;  // 傾きがこれより小さいと Newton 法の割り算をしない

constexpr float kFitTolerance = 0.0005f;       // Ease を写すときの、Ease::Apply との差の許容（値は 0〜1）
constexpr int kMaxFitDepth = 8;                // Ease を写すときに、区間を半分にする回数の上限
constexpr int kFitChecks = 16;                 // 1つの区間で、差を測る点の数
constexpr float kFitSlopeStep = 1.0f / 256.0f; // 傾きを測る幅（区間の長さに対する割合）

// 式が切り替わる所（Bounce は跳ねた瞬間に傾きが急に変わる。Easing.cpp の式の切り替わりと同じ値）
constexpr std::array<float, 3> kOutBounceJoints = {1.0f / 2.75f, 2.0f / 2.75f, 2.5f / 2.75f};
constexpr std::array<float, 3> kInBounceJoints = {1.0f - 2.5f / 2.75f, 1.0f - 2.0f / 2.75f, 1.0f - 1.0f / 2.75f};
constexpr std::array<float, 1> kMiddleJoint = {0.5f};

//=============================================================================
// 値を守る
//=============================================================================
// ===== NaN は 0、大きすぎる値は上限へ =====
float ClampValue(float value) {
	if (std::isnan(value)) {
		return 0.0f;
	}
	return std::clamp(value, -Curve::kMaxValue, Curve::kMaxValue);
}

// ===== ハンドルの時間の長さを limit までにする =====
// 時間だけ切ると傾きが変わるので、値も同じ割合で縮める
Vector2 LimitHandle(const Vector2& handle, float limit) {
	const float length = std::abs(handle.x);
	if (length <= limit) {
		return handle;
	}
	return handle * (limit / length);
}

// ===== キー1つの値を守る =====
CurveKey SanitizeKey(CurveKey key) {
	key.time = ClampValue(key.time);
	key.value = ClampValue(key.value);
	key.inHandle = {(std::min)(ClampValue(key.inHandle.x), 0.0f), ClampValue(key.inHandle.y)};
	key.outHandle = {(std::max)(ClampValue(key.outHandle.x), 0.0f), ClampValue(key.outHandle.y)};
	return key;
}

//=============================================================================
// 3次ベジェ
//=============================================================================
// ===== 媒介変数 s（0〜1）での位置 =====
float CubicBezier(float p0, float p1, float p2, float p3, float s) {
	const float u = 1.0f - s;
	return u * u * u * p0 + 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 + s * s * s * p3;
}

// ===== s での傾き（CubicBezier を s で微分した物）=====
float CubicBezierSlope(float p0, float p1, float p2, float p3, float s) {
	const float u = 1.0f - s;
	return 3.0f * (u * u * (p1 - p0) + 2.0f * u * s * (p2 - p1) + s * s * (p3 - p2));
}

// ===== 時間の割合 target（0〜1）になる s を求める =====
// 時間の制御点は 0, x1, x2, 1。x1 と x2 が 0〜1 の中なら、時間は s に対して減らない（解は1つ）
// Newton 法で速く近づき、傾きが 0 に近くて飛び出すときだけ二分法にする。回数の上限があるので必ず終わる
float SolveBezierParam(float x1, float x2, float target) {
	float low = 0.0f;   // 答えはここより右
	float high = 1.0f;  // 答えはここより左
	float s = target;   // 最初の予想。ハンドルが等間隔なら、これがそのまま答え
	for (int i = 0; i < kMaxSolveIterations; ++i) {
		const float error = CubicBezier(0.0f, x1, x2, 1.0f, s) - target;
		if (std::abs(error) < kSolveTolerance) {
			break;
		}
		// --- 答えのある範囲を狭める ---
		if (error > 0.0f) {
			high = s;
		} else {
			low = s;
		}
		// --- Newton 法の次の場所。範囲の外へ出るなら、範囲の真ん中にする ---
		const float slope = CubicBezierSlope(0.0f, x1, x2, 1.0f, s);
		const float next = (slope > kMinSolveSlope) ? s - error / slope : -1.0f;
		s = (next > low && next < high) ? next : (low + high) * 0.5f;
	}
	return s;
}

// ===== a から b までのベジェで、time になる s を求める（a.time < b.time のとき）=====
float CalcBezierParam(const CurveKey& a, const CurveKey& b, float time) {
	const float duration = b.time - a.time;
	return SolveBezierParam(a.outHandle.x / duration, 1.0f + b.inHandle.x / duration, (time - a.time) / duration);
}

//=============================================================================
// Ease をカーブに写す
//=============================================================================
// ===== 式が切り替わる所 =====
std::span<const float> JointsOf(Ease::Type type) {
	switch (type) {
	case Ease::Type::OutBounce:
		return kOutBounceJoints;
	case Ease::Type::InBounce:
		return kInBounceJoints;
	case Ease::Type::InOutCubic:
		return kMiddleJoint;
	default:
		return {};
	}
}

// ===== 区間の端の傾きを、内側の3点から求める =====
// 3点を通る2次式を端まで延ばしたときの傾き。inward は内側へ進む1歩（終わりの端なら負の値）
// 外側を読むと、Bounce の跳ねた所で隣の区間の傾きが混ざる。端の点そのものを読むと、Elastic の t = 1 の値の飛びを拾う
float CalcEaseSlope(Ease::Type type, float edge, float inward) {
	const float first = Ease::Apply(edge + inward, type);
	const float second = Ease::Apply(edge + inward * 2.0f, type);
	const float third = Ease::Apply(edge + inward * 3.0f, type);
	return (-5.0f * first + 8.0f * second - 3.0f * third) / (inward * 2.0f);
}

// ===== start から end までを、Ease の値と傾きが両端で合うベジェにする =====
// 差が許容より大きければ、半分に分けてやり直す。keys の最後のキーが start の所にある状態で呼ぶ
void FitEase(std::vector<CurveKey>& keys, Ease::Type type, float start, float end, int depth) {
	const float duration = end - start;
	const float step = duration * kFitSlopeStep;
	const float startValue = Ease::Apply(start, type);
	const float endValue = Ease::Apply(end, type);
	const float startSlope = CalcEaseSlope(type, start, step);
	const float endSlope = CalcEaseSlope(type, end, -step);

	// --- ハンドルの時間を長さの 1/3 にすると、ベジェの時間が s に比例する（s と時間の割合が同じ）---
	const float third = duration / 3.0f;
	const float control1 = startValue + startSlope * third;
	const float control2 = endValue - endSlope * third;

	// --- Ease との差を測る ---
	float worst = 0.0f;
	for (int i = 1; i < kFitChecks; ++i) {
		const float s = static_cast<float>(i) / static_cast<float>(kFitChecks);
		worst = (std::max)(worst, std::abs(CubicBezier(startValue, control1, control2, endValue, s) - Ease::Apply(start + duration * s, type)));
	}
	if (worst > kFitTolerance && depth < kMaxFitDepth) {
		const float middle = start + duration * 0.5f;
		FitEase(keys, type, start, middle, depth + 1);
		FitEase(keys, type, middle, end, depth + 1);
		return;
	}

	// --- 区間を確定する ---
	keys.back().outHandle = {third, startSlope * third};
	CurveKey key;
	key.time = end;
	key.value = endValue;
	key.inHandle = {-third, -endSlope * third};
	key.handleMode = CurveHandleMode::Free; // 跳ねた所の折れ目を残す
	keys.push_back(key);
}
} // namespace


//=============================================================================
// 作る
//=============================================================================
Curve::Curve(std::vector<CurveKey> keys) : keys_(std::move(keys)) {
	std::ranges::transform(keys_, keys_.begin(), SanitizeKey);
	std::ranges::stable_sort(keys_, {}, &CurveKey::time); // 同じ時間のキーの順番を変えない
	ClampHandles();
}

Curve Curve::FromEase(Ease::Type type) {
	std::vector<CurveKey> keys(1);
	keys[0].value = Ease::Apply(0.0f, type);
	keys[0].handleMode = CurveHandleMode::Free;
	float start = 0.0f;
	for (const float joint : JointsOf(type)) {
		FitEase(keys, type, start, joint, 0);
		start = joint;
	}
	FitEase(keys, type, start, 1.0f, 0);
	return Curve(std::move(keys));
}


//=============================================================================
// 読む
//=============================================================================
float Curve::Evaluate(float time) const {
	if (keys_.empty()) {
		return 0.0f;
	}
	// 比べる前に NaN を除く（NaN はどの比較も false になり、下の探し方が使えない）
	if (std::isnan(time) || time < keys_.front().time) {
		return keys_.front().value;
	}
	// --- time より後の最初のキーを探す。a は time 以前の最後のキーなので、a.time <= time < b.time ---
	const auto next = std::ranges::upper_bound(keys_, time, {}, &CurveKey::time);
	if (next == keys_.end()) {
		return keys_.back().value;
	}
	const CurveKey& a = *(next - 1);
	const CurveKey& b = *next;
	switch (a.interp) {
	case CurveInterp::Constant:
		return a.value;
	case CurveInterp::Linear:
		return Ease::Lerp(a.value, b.value, (time - a.time) / (b.time - a.time));
	case CurveInterp::Bezier:
		break;
	}
	return CubicBezier(a.value, a.value + a.outHandle.y, b.value + b.inHandle.y, b.value, CalcBezierParam(a, b, time));
}


//=============================================================================
// 書き換える
//=============================================================================
// ===== キーを足す =====
int Curve::AddKey(float time, float value) {
	CurveKey key;
	key.time = ClampValue(time);
	key.value = ClampValue(value);
	const int index = FindInsertIndex(key.time);
	const int count = static_cast<int>(keys_.size());
	const bool hasPrevious = index > 0;
	const bool hasNext = index < count;

	// --- 傾きは前後のキーを結ぶ向き、ハンドルの長さは隣までの 1/3 ---
	float slope = 0.0f;
	if (hasPrevious && hasNext && keys_[index].time > keys_[index - 1].time) {
		slope = (keys_[index].value - keys_[index - 1].value) / (keys_[index].time - keys_[index - 1].time);
	}
	const float inLength = hasPrevious ? (key.time - keys_[index - 1].time) / 3.0f : 0.0f;
	const float outLength = hasNext ? (keys_[index].time - key.time) / 3.0f : 0.0f;
	key.inHandle = {-inLength, -inLength * slope};
	key.outHandle = {outLength, outLength * slope};

	// --- 隣のキーの、こちらを向くハンドルが長さ 0 なら伸ばす（カーブの端のキーのハンドルは 0 のままなので、つかめなくなる）---
	if (hasPrevious) {
		key.interp = keys_[index - 1].interp;
		if (keys_[index - 1].outHandle.x == 0.0f) {
			keys_[index - 1].outHandle.x = inLength;
		}
	}
	if (hasNext && keys_[index].inHandle.x == 0.0f) {
		keys_[index].inHandle.x = -outLength;
	}
	keys_.insert(keys_.begin() + index, key);
	ClampHandles();
	return index;
}

// ===== 線の上へキーを足す =====
int Curve::InsertKeyOnCurve(float time) {
	const float clampedTime = ClampValue(time);
	const int index = FindInsertIndex(clampedTime);
	// --- ちょうどその時間のキーがあれば、それを返す ---
	if (index > 0 && keys_[index - 1].time == clampedTime) {
		return index - 1;
	}
	// --- カーブの外なら、端の値でふつうに足す ---
	const int count = static_cast<int>(keys_.size());
	if (index == 0 || index == count) {
		return AddKey(clampedTime, Evaluate(clampedTime));
	}

	CurveKey& a = keys_[index - 1];
	CurveKey& b = keys_[index];
	CurveKey key;
	key.time = clampedTime;
	key.value = Evaluate(clampedTime);
	key.interp = a.interp;
	switch (a.interp) {
	case CurveInterp::Constant:
		break;
	case CurveInterp::Linear: {
		// 線の傾きのままハンドルを付ける（あとで Bezier にしても折れない）
		const float slope = (b.value - a.value) / (b.time - a.time);
		const float inLength = (clampedTime - a.time) / 3.0f;
		const float outLength = (b.time - clampedTime) / 3.0f;
		key.inHandle = {-inLength, -inLength * slope};
		key.outHandle = {outLength, outLength * slope};
		break;
	}
	case CurveInterp::Bezier: {
		// de Casteljau 法：制御点の間を s で分けることを3回くり返すと、同じ形のベジェ2本になる
		const float s = CalcBezierParam(a, b, clampedTime);
		const Vector2 p0 = {a.time, a.value};
		const Vector2 p1 = p0 + a.outHandle;
		const Vector2 p3 = {b.time, b.value};
		const Vector2 p2 = p3 + b.inHandle;
		const Vector2 p01 = p0 + (p1 - p0) * s;
		const Vector2 p12 = p1 + (p2 - p1) * s;
		const Vector2 p23 = p2 + (p3 - p2) * s;
		const Vector2 p012 = p01 + (p12 - p01) * s;
		const Vector2 p123 = p12 + (p23 - p12) * s;
		const Vector2 middle = p012 + (p123 - p012) * s;
		a.outHandle = p01 - p0;
		b.inHandle = p23 - p3;
		key.inHandle = p012 - middle;
		key.outHandle = p123 - middle;
		key.value = middle.y;
		break;
	}
	}
	keys_.insert(keys_.begin() + index, key);
	ClampHandles();
	return index;
}

// ===== キーを消す =====
void Curve::RemoveKey(int index) {
	if (IsValidIndex(index)) {
		keys_.erase(keys_.begin() + index);
	}
}

// ===== キーを置き換える =====
int Curve::SetKey(int index, const CurveKey& key) {
	if (!IsValidIndex(index)) {
		return index;
	}
	const CurveKey sanitized = SanitizeKey(key);
	keys_.erase(keys_.begin() + index);
	// equal_range：同じ時間のキーが並ぶ範囲。今の番号に一番近い場所へ戻すので、時間を変えなければ順番は変わらない
	const auto range = std::ranges::equal_range(keys_, sanitized.time, {}, &CurveKey::time);
	const int first = static_cast<int>(range.begin() - keys_.begin());
	const int last = static_cast<int>(range.end() - keys_.begin());
	const int newIndex = std::clamp(index, first, last);
	keys_.insert(keys_.begin() + newIndex, sanitized);
	ClampHandles();
	return newIndex;
}

// ===== ハンドルを動かす =====
void Curve::SetHandle(int index, CurveHandleSide side, const Vector2& handle) {
	if (!IsValidIndex(index)) {
		return;
	}
	const bool isOut = side == CurveHandleSide::Out;
	CurveKey& key = keys_[index];
	(isOut ? key.outHandle : key.inHandle) = handle;
	key = SanitizeKey(key);
	ClampHandles();

	// --- Aligned：反対のハンドルの時間の長さを変えず、傾きだけそろえる（時間と値は単位が違うので、向きより傾きで比べる）---
	if (key.handleMode == CurveHandleMode::Aligned) {
		const Vector2 moved = isOut ? key.outHandle : key.inHandle;
		Vector2& other = isOut ? key.inHandle : key.outHandle;
		if (moved.x != 0.0f) {
			other.y = ClampValue(other.x * (moved.y / moved.x));
		}
	}
}

// ===== 区間を Ease の形にする =====
void Curve::ApplyEaseToSegment(int index, Ease::Type type) {
	if (!IsValidIndex(index) || !IsValidIndex(index + 1)) {
		return;
	}
	const Vector2 start = {keys_[index].time, keys_[index].value};
	const Vector2 size = {keys_[index + 1].time - start.x, keys_[index + 1].value - start.y};
	if (size.x <= 0.0f) {
		return; // 同じ時間のキーの間には、形を入れられない
	}
	// --- 0〜1 の形を、この区間の大きさへ広げる ---
	std::vector<CurveKey> shape = FromEase(type).keys_;
	for (CurveKey& key : shape) {
		key.time = start.x + key.time * size.x;
		key.value = start.y + key.value * size.y;
		key.inHandle = {key.inHandle.x * size.x, key.inHandle.y * size.y};
		key.outHandle = {key.outHandle.x * size.x, key.outHandle.y * size.y};
	}
	// --- 両端のキーは残して、区間の内側を向くハンドルと補間だけ入れ替える（外側とは傾きが合わないので Free にする）---
	CurveKey& first = keys_[index];
	first.outHandle = shape.front().outHandle;
	first.interp = CurveInterp::Bezier;
	first.handleMode = CurveHandleMode::Free;
	CurveKey& last = keys_[index + 1];
	last.inHandle = shape.back().inHandle;
	last.handleMode = CurveHandleMode::Free;
	keys_.insert(keys_.begin() + index + 1, shape.begin() + 1, shape.end() - 1);
	ClampHandles();
}

// ===== 全体を Ease の形にする =====
void Curve::ApplyEaseToAll(Ease::Type type) {
	if (keys_.size() < 2) {
		*this = FromEase(type);
		return;
	}
	keys_.erase(keys_.begin() + 1, keys_.end() - 1); // 最初と最後のキーだけ残す
	ApplyEaseToSegment(0, type);
}


//=============================================================================
// 内部
//=============================================================================
int Curve::FindInsertIndex(float time) const {
	return static_cast<int>(std::ranges::upper_bound(keys_, time, {}, &CurveKey::time) - keys_.begin());
}

void Curve::ClampHandles() {
	for (size_t i = 0; i + 1 < keys_.size(); ++i) {
		const float duration = keys_[i + 1].time - keys_[i].time;
		keys_[i].outHandle = LimitHandle(keys_[i].outHandle, duration);
		keys_[i + 1].inHandle = LimitHandle(keys_[i + 1].inHandle, duration);
	}
}
```

## パートB 保存と自動の確認

### B-1 `ECS/ComponentUI.h`

`AssetType` の最後に足す。

```cpp
	AttackTable,    // AttackTableManager::Load の番号（.attacktable）
	Curve,          // CurveManager::Load の番号（.curve）
};
```

### B-2 `ECS/ComponentUI.cpp`

include に足す。

```cpp
#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Curve/CurveManager.h"
```

`GetAssetPath` の `switch` に足す。

```cpp
	case AssetType::AttackTable:
		return AttackTableManager::GetPath(handle);
	case AssetType::Curve:
		return CurveManager::GetPath(handle);
	}
```

`LoadAsset` の `switch` に足す。

```cpp
	case AssetType::AttackTable:
		return AttackTableManager::Load(filePath);
	case AssetType::Curve:
		return CurveManager::Load(filePath);
	}
```

拡張子の一覧に足す。

```cpp
constexpr std::array<std::string_view, 1> kAttackTableExtensions = {AttackTableManager::kFileExtension};       // ".attacktable"
constexpr std::array<std::string_view, 1> kCurveExtensions = {CurveManager::kFileExtension};                   // ".curve"
```

`ExtensionsOf` の `switch` に足す。

```cpp
	case AssetType::AttackTable:
		return kAttackTableExtensions;
	case AssetType::Curve:
		return kCurveExtensions;
	}
```

### B-3 `Curve/CurveManager.h`（新しいファイル）

```cpp
#pragma once
#include <set>
#include <string>
#include <cstdint>
#include <string_view>
#include <filesystem>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/ECS/ComponentSerializer.h"


/// <summary>
/// カーブのファイル（.curve）を読む・持つ・保存する（AttackTableManager と同じ形）
/// <para>同じファイルを選んだ物はみんな同じカーブを見るので、ファイル1つを直すと、使っている全部に効く</para>
/// <para>中身は Inspector で直す（Project ウィンドウでファイルを選ぶ → CurveAssetEditor）</para>
/// <para>ほかのファイルの中にカーブを入れたいときは、ToJson・FromJson をそのまま使う</para>
/// </summary>
class CurveManager {
public:
	static constexpr std::string_view kFileExtension = ".curve"; // カーブのファイルの拡張子

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
	static const Curve* Get(uint32_t handle);

	// 中身を書き換える（Inspector から。ファイルにはまだ書かない）
	static void Set(uint32_t handle, const Curve& curve);

	// ファイルに書く（前のファイルは .bak として1つ残る）
	static bool Save(uint32_t handle);

	// 新しいファイルを、既定の中身で作る（Project ウィンドウの Create → Curve）。同じ名前のファイルがあれば作らない
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ===== JSON（ファイルの形は { "keys": [{...}, ...] }。1つの中身は DescribeKey の項目）=====
	static SceneJson ToJson(const Curve& curve);
	// 読めない項目は warnings に書いて、既定の値のままにする。並びや範囲の外の値は Curve が直す
	static Curve FromJson(const SceneJson& json, std::set<std::string>& warnings);

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	// isInUsed・isOutUsed が false のハンドルは灰色にする（保存には関係しない）
	static void DescribeKey(ComponentUI& ui, CurveKey& value, bool isInUsed = true, bool isOutUsed = true);
};
```

### B-4 `Curve/CurveManager.cpp`（新しいファイル）

```cpp
#include "CurveManager.h"

#include <memory>
#include <format>
#include <vector>
#include <fstream>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Curve"; // 警告に出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct CurveAsset {
	std::string path; // ファイル（ToPathKey の形）
	Curve curve;      // 中身
};

/// <summary>
/// CurveManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct CurveStorage {
	std::vector<CurveAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<CurveStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
CurveAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// ファイルに書く（前のファイルは .bak として1つ残る）
bool WriteFile(const std::string& path, const Curve& curve) {
	if (!SceneSerializer::WriteFile(path, CurveManager::ToJson(curve))) {
		LogManager::Error(std::format("カーブを保存できませんでした: {}", path));
		return false;
	}
	return true;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void CurveManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<CurveStorage>();
}

void CurveManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t CurveManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "CurveManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &CurveAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("カーブのファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded() || !json.is_object()) {
		LogManager::Error(std::format("カーブのファイルが壊れています: {}", key));
		return 0;
	}
	std::set<std::string> warnings;
	Curve curve = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(curve)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded curve: {} -> {}", key, handle));
	return handle;
}

const std::string& CurveManager::GetPath(uint32_t handle) {
	const CurveAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const Curve* CurveManager::Get(uint32_t handle) {
	const CurveAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->curve : nullptr;
}

//=============================================================================
// 書き換え・保存
//=============================================================================
void CurveManager::Set(uint32_t handle, const Curve& curve) {
	if (CurveAsset* asset = Find(handle)) {
		asset->curve = curve;
	}
}

bool CurveManager::Save(uint32_t handle) {
	const CurveAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->curve);
}

bool CurveManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), Curve::FromEase(Ease::Type::Linear));
}

//=============================================================================
// JSON
//=============================================================================
SceneJson CurveManager::ToJson(const Curve& curve) {
	std::set<std::string> warnings;
	SceneJson keys = SceneJson::array();
	for (CurveKey key : curve.GetKeys()) { // 見せ方の関数は書き換えられる参照を受け取るので、写しを渡す
		SceneJson item;
		ComponentSerializer::WriteFields(kTypeName, [&key](ComponentUI& ui) { DescribeKey(ui, key); }, item, warnings);
		keys.push_back(std::move(item));
	}
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
	SceneJson root = SceneJson::object();
	root["keys"] = std::move(keys);
	return root;
}

Curve CurveManager::FromJson(const SceneJson& json, std::set<std::string>& warnings) {
	const auto keys = json.find("keys");
	if (keys == json.end() || !keys->is_array()) {
		warnings.insert("\"keys\" の配列がありません（空のカーブにします）");
		return Curve{};
	}
	std::vector<CurveKey> result;
	for (const SceneJson& item : *keys) {
		if (!item.is_object()) {
			warnings.insert("\"keys\" の中に、キーではない物があります（読み飛ばします）");
			continue;
		}
		CurveKey key;
		ComponentSerializer::ReadFields(kTypeName, [&key](ComponentUI& ui) { DescribeKey(ui, key); }, item, warnings);
		result.push_back(key);
	}
	return Curve(std::move(result));
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void CurveManager::DescribeKey(ComponentUI& ui, CurveKey& value, bool isInUsed, bool isOutUsed) {
	ui.Field("Time###time", value.time, Drag(0.01f).Tip("キーの時間"));
	ui.Field("Value###value", value.value, Drag(0.01f).Tip("キーの値"));
	ui.Field("Interpolation###interp", value.interp, Tip("このキーから次のキーまでのつなぎ方（Constant：次のキーまで今の値のまま / Linear：直線 / Bezier：ハンドルで曲げる）"));
	ui.Field("Handle Mode###handleMode", value.handleMode, Tip("Aligned：入りと出の傾きをそろえる（なめらかにつながる） / Free：別々に動かす（折れ目を作れる）"));
	ui.BeginDisabled(!isInUsed);
	ui.Field("In Handle###inHandle", value.inHandle, Drag(0.01f).Tip("入りのハンドル。キーからの差（x：時間、y：値）。時間は 0 以下で、前のキーまでの長さを超えない"));
	ui.EndDisabled();
	ui.BeginDisabled(!isOutUsed);
	ui.Field("Out Handle###outHandle", value.outHandle, Drag(0.01f).Tip("出のハンドル。キーからの差（x：時間、y：値）。時間は 0 以上で、次のキーまでの長さを超えない"));
	ui.EndDisabled();
}
```

### B-5 `Systems/EngineSystems.cpp`

include に足す。

```cpp
#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Curve/CurveManager.h"
```

`Initialize` に足す。

```cpp
	AttackTableSystem::Initialize();      // Attack Table
	CurveManager::Initialize();           // カーブ（.curve）
```

`Release` に足す。

```cpp
	AttackTableManager::Release();
	CurveManager::Release();
```

### B-6 `Curve/CurveSelfTest.h`（新しいファイル）

```cpp
#pragma once

/// <summary>
/// カーブの計算が合っているかを、起動時に1回だけ確かめる（ComputeSelfTest と同じ形）
/// <para>代表的な値・カーブの端・キーが0個と1個・同じ時間のキー・壊れた入力を確かめる。合わなければログに出して assert で止める</para>
/// </summary>
namespace CurveSelfTest {
/// <summary>
/// 確かめる。Engine::Initialize の中で呼ぶ（Debug だけ）
/// </summary>
void Run();
} // namespace CurveSelfTest
```

### B-7 `Curve/CurveSelfTest.cpp`（新しいファイル）

`TestSolver` の「値の制御点を時間の制御点と同じにする」は、正しい答えを知らなくても確かめられる方法。値の式と時間の式が同じなので、解いた s が正しければ `Evaluate(t) = t` になる。`TestBezierValues` の値は、別のプログラム（200 回の二分法）で求めた。

```cpp
#include "CurveSelfTest.h"

#include <set>
#include <cmath>
#include <format>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"

namespace {
constexpr float kExact = 0.00002f;      // 式で答えが決まる値の許容
constexpr float kEaseTolerance = 0.001f; // Ease::Apply との差の許容
constexpr int kEaseSamples = 2000;       // Ease と比べる点の数
constexpr int kRandomCurves = 2000;      // でたらめなカーブの数
constexpr int kRandomSamples = 64;       // でたらめなカーブ1本で確かめる時間の数
constexpr int kMaxErrorLogs = 10;        // 合わなかった項目をログに出す数（全部出すと読めない）
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();

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
			LogManager::Error(std::format("[curve] self test: {}", name));
		}
		++failed;
	}
	void Near(float actual, float expected, float tolerance, const std::string& name) {
		Check(std::abs(actual - expected) <= tolerance, std::format("{}: {} （正しい値 {}）", name, actual, expected));
	}
};

// キーを作る（ハンドルは省略できる）
CurveKey MakeKey(float time, float value, CurveInterp interp = CurveInterp::Bezier, Vector2 inHandle = {0.0f, 0.0f}, Vector2 outHandle = {0.0f, 0.0f}) {
	CurveKey key;
	key.time = time;
	key.value = value;
	key.interp = interp;
	key.inHandle = inHandle;
	key.outHandle = outHandle;
	return key;
}

// 2つのキーの1区間のベジェを作る（CSS の cubic-bezier(x1, y1, x2, y2) と同じ指定。時間も値も 0〜1）
Curve MakeCubicBezier(float x1, float y1, float x2, float y2) {
	return Curve({MakeKey(0.0f, 0.0f, CurveInterp::Bezier, {}, {x1, y1}), MakeKey(1.0f, 1.0f, CurveInterp::Bezier, {x2 - 1.0f, y2 - 1.0f}, {})});
}

//=============================================================================
// 確かめる項目
//=============================================================================
// ===== キーが0個・1個 =====
void TestFewKeys(Tester& t) {
	const Curve empty;
	t.Near(empty.Evaluate(0.5f), 0.0f, 0.0f, "キー0個は0");
	const Curve single({MakeKey(2.0f, 3.0f)});
	t.Near(single.Evaluate(-1.0f), 3.0f, 0.0f, "キー1個：前");
	t.Near(single.Evaluate(2.0f), 3.0f, 0.0f, "キー1個：ちょうど");
	t.Near(single.Evaluate(9.0f), 3.0f, 0.0f, "キー1個：後ろ");
}

// ===== カーブの端 =====
void TestEnds(Tester& t) {
	const Curve curve({MakeKey(1.0f, 10.0f, CurveInterp::Linear), MakeKey(3.0f, 20.0f, CurveInterp::Linear)});
	t.Near(curve.Evaluate(0.0f), 10.0f, 0.0f, "最初のキーより前");
	t.Near(curve.Evaluate(1.0f), 10.0f, 0.0f, "最初のキーちょうど");
	t.Near(curve.Evaluate(3.0f), 20.0f, 0.0f, "最後のキーちょうど");
	t.Near(curve.Evaluate(4.0f), 20.0f, 0.0f, "最後のキーより後");
}

// ===== 補間の種類 =====
void TestInterp(Tester& t) {
	const Curve constant({MakeKey(0.0f, 1.0f, CurveInterp::Constant), MakeKey(1.0f, 5.0f)});
	t.Near(constant.Evaluate(0.999f), 1.0f, 0.0f, "Constant：次のキーの手前は今の値");
	t.Near(constant.Evaluate(1.0f), 5.0f, 0.0f, "Constant：次のキーちょうどは次の値");
	const Curve linear({MakeKey(0.0f, 1.0f, CurveInterp::Linear), MakeKey(2.0f, 5.0f)});
	t.Near(linear.Evaluate(0.5f), 2.0f, kExact, "Linear：途中");
	// 補間は左のキーで決まる（右のキーの interp は見ない）
	const Curve mixed({MakeKey(0.0f, 0.0f, CurveInterp::Linear), MakeKey(1.0f, 1.0f, CurveInterp::Constant), MakeKey(2.0f, 3.0f)});
	t.Near(mixed.Evaluate(0.5f), 0.5f, kExact, "左のキーの補間：Linear");
	t.Near(mixed.Evaluate(1.5f), 1.0f, kExact, "左のキーの補間：Constant");
}

// ===== ベジェの代表的な値（別の計算で求めた値と比べる。CSS の ease・ease-in・ease-in-out）=====
void TestBezierValues(Tester& t) {
	const Curve ease = MakeCubicBezier(0.25f, 0.1f, 0.25f, 1.0f);
	t.Near(ease.Evaluate(0.25f), 0.4085106f, kExact, "ease 0.25");
	t.Near(ease.Evaluate(0.5f), 0.8024034f, kExact, "ease 0.5");
	const Curve easeIn = MakeCubicBezier(0.42f, 0.0f, 1.0f, 1.0f);
	t.Near(easeIn.Evaluate(0.5f), 0.3153568f, kExact, "ease-in 0.5");
	const Curve easeInOut = MakeCubicBezier(0.42f, 0.0f, 0.58f, 1.0f);
	t.Near(easeInOut.Evaluate(0.25f), 0.1291619f, kExact, "ease-in-out 0.25");
	t.Near(easeInOut.Evaluate(0.5f), 0.5f, kExact, "ease-in-out 0.5");
	// 時間が 0〜1 でない区間でも、同じ形になる
	const Curve scaled({MakeKey(10.0f, 100.0f, CurveInterp::Bezier, {}, {0.5f, 2.0f}), MakeKey(12.0f, 120.0f, CurveInterp::Bezier, {-1.5f, 0.0f}, {})});
	t.Near(scaled.Evaluate(11.0f), 100.0f + 20.0f * 0.8024034f, 0.001f, "広げた区間の ease 0.5");
}

// ===== 同じ時間のキー =====
void TestSameTime(Tester& t) {
	const Curve step({MakeKey(0.0f, 0.0f, CurveInterp::Linear), MakeKey(1.0f, 1.0f, CurveInterp::Linear), MakeKey(1.0f, 5.0f, CurveInterp::Linear), MakeKey(2.0f, 6.0f)});
	t.Near(step.Evaluate(0.999f), 0.999f, 0.001f, "段差の手前");
	t.Near(step.Evaluate(1.0f), 5.0f, 0.0f, "段差ちょうどは後ろのキー");
	t.Near(step.Evaluate(1.5f), 5.5f, kExact, "段差の後");
	const Curve allSame({MakeKey(1.0f, 2.0f), MakeKey(1.0f, 3.0f), MakeKey(1.0f, 4.0f)});
	t.Near(allSame.Evaluate(0.0f), 2.0f, 0.0f, "全部同じ時間：前は最初のキー");
	t.Near(allSame.Evaluate(1.0f), 4.0f, 0.0f, "全部同じ時間：ちょうどは最後のキー");
	t.Near(allSame.Evaluate(2.0f), 4.0f, 0.0f, "全部同じ時間：後ろ");
	const CurveKey handleKey = MakeKey(1.0f, 0.0f, CurveInterp::Bezier, {-5.0f, 1.0f}, {5.0f, 1.0f});
	const Curve handles({handleKey, handleKey});
	t.Check(handles.GetKeys()[0].outHandle.x == 0.0f && handles.GetKeys()[1].inHandle.x == 0.0f, "同じ時間のキーの間のハンドルは長さ 0");
}

// ===== 壊れた入力 =====
void TestBadInput(Tester& t) {
	const Curve curve({MakeKey(0.0f, 1.0f, CurveInterp::Linear), MakeKey(1.0f, 2.0f)});
	t.Near(curve.Evaluate(kNaN), 1.0f, 0.0f, "NaN の時間は最初の値");
	t.Near(curve.Evaluate(kInfinity), 2.0f, 0.0f, "+無限大の時間は最後の値");
	t.Near(curve.Evaluate(-kInfinity), 1.0f, 0.0f, "-無限大の時間は最初の値");

	const Curve broken({MakeKey(kNaN, kInfinity, CurveInterp::Bezier, {kNaN, kNaN}, {kInfinity, -kInfinity}), MakeKey(1.0f, -kInfinity), MakeKey(kInfinity, kNaN)});
	for (const CurveKey& key : broken.GetKeys()) {
		const bool isFinite = std::isfinite(key.time) && std::isfinite(key.value) && std::isfinite(key.inHandle.x) && std::isfinite(key.inHandle.y) && std::isfinite(key.outHandle.x) && std::isfinite(key.outHandle.y);
		t.Check(isFinite, "NaN・無限大のキーは有限の値に直る");
	}
	t.Check(std::isfinite(broken.Evaluate(0.5f)), "直したカーブの値は有限");

	const Curve unsorted({MakeKey(2.0f, 2.0f), MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f)});
	t.Check(unsorted.GetKeys()[0].time == 0.0f && unsorted.GetKeys()[2].time == 2.0f, "ばらばらの順のキーは時間の順に並ぶ");

	const Curve huge({MakeKey(-1.0e30f, -1.0e30f, CurveInterp::Bezier, {}, {1.0e30f, 1.0e30f}), MakeKey(1.0e30f, 1.0e30f, CurveInterp::Bezier, {-1.0e30f, -1.0e30f}, {})});
	t.Check(std::isfinite(huge.Evaluate(0.0f)), "とても大きな値でも有限");

	Curve invalidIndex({MakeKey(0.0f, 1.0f)});
	const Curve copy = invalidIndex;
	invalidIndex.RemoveKey(-1);
	invalidIndex.RemoveKey(1);
	invalidIndex.SetHandle(5, CurveHandleSide::Out, {1.0f, 1.0f});
	invalidIndex.ApplyEaseToSegment(0, Ease::Type::OutCubic); // 次のキーが無い
	t.Check(invalidIndex == copy && invalidIndex.SetKey(3, MakeKey(0.0f, 0.0f)) == 3, "知らない番号は何もしない");
}

// ===== ハンドルの制限 =====
void TestHandleLimit(Tester& t) {
	Curve curve({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f)});
	curve.SetHandle(0, CurveHandleSide::Out, {3.0f, 6.0f});
	t.Near(curve.GetKeys()[0].outHandle.x, 1.0f, 0.0f, "出のハンドルは区間の長さまで");
	t.Near(curve.GetKeys()[0].outHandle.y, 2.0f, kExact, "縮めても傾きは同じ");
	curve.SetHandle(0, CurveHandleSide::Out, {-1.0f, 1.0f});
	t.Near(curve.GetKeys()[0].outHandle.x, 0.0f, 0.0f, "出のハンドルは前の時間へ行かない");
	curve.SetHandle(1, CurveHandleSide::In, {1.0f, 1.0f});
	t.Near(curve.GetKeys()[1].inHandle.x, 0.0f, 0.0f, "入りのハンドルは後の時間へ行かない");
	// 区間が縮むと、はみ出たハンドルも縮む
	Curve shrink({MakeKey(0.0f, 0.0f, CurveInterp::Bezier, {}, {2.0f, 2.0f}), MakeKey(3.0f, 1.0f)});
	CurveKey moved = shrink.GetKeys()[1];
	moved.time = 1.0f;
	shrink.SetKey(1, moved);
	t.Near(shrink.GetKeys()[0].outHandle.x, 1.0f, 0.0f, "区間が縮むとハンドルも縮む");
	t.Near(shrink.GetKeys()[0].outHandle.y, 1.0f, kExact, "区間が縮んでも傾きは同じ");
}

// ===== 解き方 =====
void TestSolver(Tester& t) {
	// 接線が真上を向く（x1 = 1, x2 = 0）：s = 0.5 で傾きが 0 になる。Newton 法だけだと 0 で割る
	const Curve vertical = MakeCubicBezier(1.0f, 0.0f, 0.0f, 1.0f);
	t.Near(vertical.Evaluate(0.25f), 0.0297246f, kExact, "真上を向く接線 0.25");
	float previous = vertical.Evaluate(0.0f);
	bool isIncreasing = true;
	for (int i = 1; i <= kEaseSamples; ++i) {
		const float value = vertical.Evaluate(static_cast<float>(i) / static_cast<float>(kEaseSamples));
		isIncreasing = isIncreasing && value >= previous - kExact;
		previous = value;
	}
	t.Check(isIncreasing, "真上を向く接線でも値が増え続ける");

	// 値の制御点を時間の制御点と同じにすると、正しく解ければ Evaluate(t) = t になる
	std::mt19937 random(1234);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	float worst = 0.0f;
	for (int i = 0; i < kRandomCurves; ++i) {
		const float x1 = unit(random);
		const float x2 = unit(random);
		const Curve curve = MakeCubicBezier(x1, x1, x2, x2);
		for (int j = 0; j <= kRandomSamples; ++j) {
			const float time = static_cast<float>(j) / static_cast<float>(kRandomSamples);
			worst = (std::max)(worst, std::abs(curve.Evaluate(time) - time));
		}
	}
	t.Near(worst, 0.0f, kExact, "どのハンドルでも時間を解ける");
}

// ===== でたらめなカーブ =====
void TestRandom(Tester& t) {
	std::mt19937 random(5678);
	std::uniform_real_distribution<float> position(-10.0f, 10.0f);
	std::uniform_real_distribution<float> handle(-30.0f, 30.0f);
	std::uniform_int_distribution<int> keyCount(1, 6);
	std::uniform_int_distribution<int> interp(0, 2);
	bool isFinite = true;
	bool isHandleInside = true;
	for (int i = 0; i < kRandomCurves; ++i) {
		std::vector<CurveKey> keys(keyCount(random));
		for (CurveKey& key : keys) {
			key = MakeKey(position(random), position(random), static_cast<CurveInterp>(interp(random)), {handle(random), handle(random)}, {handle(random), handle(random)});
		}
		const Curve curve(std::move(keys));
		const std::vector<CurveKey>& sorted = curve.GetKeys();
		for (size_t k = 0; k + 1 < sorted.size(); ++k) {
			const float duration = sorted[k + 1].time - sorted[k].time;
			isHandleInside = isHandleInside && sorted[k].outHandle.x >= 0.0f && sorted[k].outHandle.x <= duration * 1.0001f;
			isHandleInside = isHandleInside && sorted[k + 1].inHandle.x <= 0.0f && -sorted[k + 1].inHandle.x <= duration * 1.0001f;
		}
		for (int j = 0; j <= kRandomSamples; ++j) {
			isFinite = isFinite && std::isfinite(curve.Evaluate(-12.0f + 24.0f * static_cast<float>(j) / static_cast<float>(kRandomSamples)));
		}
	}
	t.Check(isFinite, "でたらめなカーブの値は有限");
	t.Check(isHandleInside, "でたらめなカーブのハンドルは区間の中");
}

// ===== Ease と同じ形 =====
void TestEase(Tester& t) {
	size_t maxKeys = 0;
	for (const Ease::Type type : magic_enum::enum_values<Ease::Type>()) {
		const Curve curve = Curve::FromEase(type);
		float worst = 0.0f;
		for (int i = 0; i <= kEaseSamples; ++i) {
			const float time = static_cast<float>(i) / static_cast<float>(kEaseSamples);
			worst = (std::max)(worst, std::abs(curve.Evaluate(time) - Ease::Apply(time, type)));
		}
		t.Near(worst, 0.0f, kEaseTolerance, std::format("Ease {} との差", magic_enum::enum_name(type)));
		maxKeys = (std::max)(maxKeys, curve.GetKeys().size());
	}
	LogManager::Log(std::format("[curve] 1つの Ease に使ったキーの最大数：{}", maxKeys));

	// 区間だけを Ease にする（隣の区間と、両端のキーは変わらない）
	Curve curve({MakeKey(0.0f, 0.0f, CurveInterp::Linear), MakeKey(1.0f, 10.0f, CurveInterp::Linear), MakeKey(3.0f, 30.0f, CurveInterp::Linear), MakeKey(4.0f, 40.0f)});
	curve.ApplyEaseToSegment(1, Ease::Type::OutBounce);
	t.Near(curve.Evaluate(0.5f), 5.0f, kExact, "区間の Ease：前の区間は変わらない");
	t.Near(curve.Evaluate(3.5f), 35.0f, kExact, "区間の Ease：後ろの区間は変わらない");
	t.Near(curve.Evaluate(1.0f), 10.0f, kExact, "区間の Ease：始まりのキー");
	t.Near(curve.Evaluate(3.0f), 30.0f, kExact, "区間の Ease：終わりのキー");
	t.Near(curve.Evaluate(2.0f), 10.0f + 20.0f * Ease::Apply(0.5f, Ease::Type::OutBounce), 0.03f, "区間の Ease：途中");

	Curve whole({MakeKey(2.0f, 1.0f), MakeKey(3.0f, 9.0f), MakeKey(4.0f, 3.0f)});
	whole.ApplyEaseToAll(Ease::Type::InQuad);
	t.Near(whole.Evaluate(3.0f), 1.0f + 2.0f * 0.25f, 0.001f, "全体の Ease");
	Curve none;
	none.ApplyEaseToAll(Ease::Type::Linear);
	t.Check(none.GetKeys().size() == 2, "キーが無いカーブの全体の Ease は 0〜1 で作る");
}

// ===== キーの足し引き =====
void TestEdit(Tester& t) {
	// 線の上へ足しても形が変わらない
	for (const CurveInterp interp : magic_enum::enum_values<CurveInterp>()) {
		Curve curve({MakeKey(0.0f, 0.0f, interp, {}, {0.8f, 0.0f}), MakeKey(1.0f, 1.0f, interp, {-0.1f, 0.0f}, {})});
		const Curve before = curve;
		const int index = curve.InsertKeyOnCurve(0.3f);
		t.Check(index == 1 && curve.GetKeys().size() == 3, "線の上へ足す");
		float worst = 0.0f;
		for (int i = 0; i <= kRandomSamples; ++i) {
			const float time = static_cast<float>(i) / static_cast<float>(kRandomSamples);
			worst = (std::max)(worst, std::abs(curve.Evaluate(time) - before.Evaluate(time)));
		}
		t.Near(worst, 0.0f, 0.001f, std::format("線の上へ足しても形が同じ（{}）", magic_enum::enum_name(interp)));
		t.Check(curve.InsertKeyOnCurve(0.3f) == index && curve.GetKeys().size() == 3, "同じ時間のキーがあれば足さない");
	}
	Curve outside({MakeKey(1.0f, 2.0f)});
	t.Check(outside.InsertKeyOnCurve(0.0f) == 0 && outside.InsertKeyOnCurve(3.0f) == 2, "カーブの外へ足す");

	// AddKey：端へ足すと、つかめる長さのハンドルが付く
	Curve added({MakeKey(0.0f, 0.0f)});
	const int addedIndex = added.AddKey(3.0f, 1.0f);
	t.Check(addedIndex == 1 && added.GetKeys()[0].outHandle.x > 0.0f && added.GetKeys()[1].inHandle.x < 0.0f, "端へ足すとハンドルが付く");

	// 追い越すと並びが変わり、番号が返る
	Curve order({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f), MakeKey(2.0f, 2.0f)});
	CurveKey moved = order.GetKeys()[0];
	moved.time = 1.5f;
	t.Check(order.SetKey(0, moved) == 1, "隣を追い越すと番号が変わる");
	t.Near(order.GetKeys()[1].time, 1.5f, 0.0f, "追い越した場所に入る");

	// 同じ時間のキーの順番は、値を変えただけでは入れ替わらない
	Curve same({MakeKey(1.0f, 1.0f), MakeKey(1.0f, 2.0f)});
	CurveKey changed = same.GetKeys()[0];
	changed.value = 7.0f;
	t.Check(same.SetKey(0, changed) == 0 && same.GetKeys()[0].value == 7.0f, "同じ時間のキーは順番が変わらない");

	// Aligned は反対のハンドルの傾きをそろえる。Free はそろえない
	Curve aligned({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f, CurveInterp::Bezier, {-0.5f, 0.0f}, {0.25f, 0.0f}), MakeKey(2.0f, 2.0f)});
	aligned.SetHandle(1, CurveHandleSide::Out, {0.5f, 1.0f});
	t.Near(aligned.GetKeys()[1].inHandle.x, -0.5f, 0.0f, "Aligned：反対の時間の長さはそのまま");
	t.Near(aligned.GetKeys()[1].inHandle.y, -1.0f, kExact, "Aligned：反対の傾きがそろう");
	CurveKey freeKey = aligned.GetKeys()[1];
	freeKey.handleMode = CurveHandleMode::Free;
	aligned.SetKey(1, freeKey);
	aligned.SetHandle(1, CurveHandleSide::Out, {0.5f, 0.0f});
	t.Near(aligned.GetKeys()[1].inHandle.y, -1.0f, kExact, "Free：反対は動かない");

	// キーを消す
	Curve removal({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f)});
	removal.RemoveKey(0);
	removal.RemoveKey(0);
	t.Near(removal.Evaluate(1.0f), 0.0f, 0.0f, "全部消すとキー0個のカーブ");
}

// ===== 保存の形 =====
void TestJson(Tester& t) {
	// --- 書いて読むと同じカーブになる（文字にしてから読み直す）---
	std::vector<CurveKey> keys = Curve::FromEase(Ease::Type::OutBack).GetKeys();
	keys.push_back(MakeKey(2.0f, 0.1f, CurveInterp::Constant));
	keys.push_back(MakeKey(3.0f, 0.2f, CurveInterp::Linear));
	const Curve curve(std::move(keys));
	std::set<std::string> warnings;
	const Curve loaded = CurveManager::FromJson(SceneJson::parse(CurveManager::ToJson(curve).dump()), warnings);
	t.Check(loaded == curve && warnings.empty(), "書いて読むと同じカーブになる");
	const Curve emptyLoaded = CurveManager::FromJson(SceneJson::parse(CurveManager::ToJson(Curve{}).dump()), warnings);
	t.Check(emptyLoaded.GetKeys().empty() && warnings.empty(), "キー0個も書いて読める");

	// --- 壊れた形でも止まらない ---
	std::set<std::string> broken;
	t.Check(CurveManager::FromJson(SceneJson::parse("{}"), broken).GetKeys().empty() && !broken.empty(), "keys が無いファイル");
	broken.clear();
	t.Check(CurveManager::FromJson(SceneJson::parse(R"({"keys": 5})"), broken).GetKeys().empty() && !broken.empty(), "keys が配列でないファイル");
	broken.clear();
	const Curve partial = CurveManager::FromJson(SceneJson::parse(R"({"keys": [1, "a", {"time": "x", "value": 2}, {"time": 1, "interp": "Nope", "inHandle": [1]}]})"), broken);
	t.Check(partial.GetKeys().size() == 2 && !broken.empty(), "壊れた項目は既定の値のまま読む");
	t.Check(std::isfinite(partial.Evaluate(0.5f)), "壊れた項目を読んだカーブの値は有限");
	broken.clear();
	const Curve reversed = CurveManager::FromJson(SceneJson::parse(R"({"keys": [{"time": 2, "value": 2}, {"time": 1, "value": 1}]})"), broken);
	t.Check(reversed.GetKeys()[0].time == 1.0f, "ファイルの順が逆でも時間の順に直る");
}
} // namespace

//=============================================================================
// 確かめる
//=============================================================================
void CurveSelfTest::Run() {
	Tester tester;
	TestFewKeys(tester);
	TestEnds(tester);
	TestInterp(tester);
	TestBezierValues(tester);
	TestSameTime(tester);
	TestBadInput(tester);
	TestHandleLimit(tester);
	TestSolver(tester);
	TestRandom(tester);
	TestEase(tester);
	TestEdit(tester);
	TestJson(tester);
	MY_ASSERT_MSG(tester.failed == 0, std::format("カーブの計算が合いません（{} 項目。ログを見てください）", tester.failed));
	LogManager::Log(std::format("[curve] self test OK ({} / {})", tester.passed, tester.passed + tester.failed));
}
```

### B-8 `Engine.cpp`

include に足す。

```cpp
#include "MyEngine/Collision/Profiling/CollisionProfiler.h"
// Curve
#include "MyEngine/Curve/CurveSelfTest.h"
```

`Initialize` の `ComputeSelfTest::Run` の下に足す。

```cpp
#ifdef _DEBUG
	ComputeSelfTest::Run(); // CSの土台が動くかを確かめる（Debugだけ。.hlsl を作った後に）
	CurveSelfTest::Run();   // カーブの計算が合っているかを確かめる（Debugだけ）
#endif
```

ここまでで Debug を起動すると、ログに `[curve] self test OK` の行が出る。

## パートC カーブエディタ

### C-1 `Editor/Widgets/CurveEditor.h`（新しいファイル）

```cpp
#pragma once
#include <optional>

#include <externals/imgui/imgui.h>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Math/Vector2.h"

// 前方宣言
class ComponentUI;


/// <summary>
/// 1回の操作の前と後のカーブ（Undo に積むための物）
/// </summary>
struct CurveEdit {
	Curve before; // 操作の前
	Curve after;  // 操作の後
};


/// <summary>
/// カーブを直す ImGui の部品（Inspector・ウィンドウのどこにでも置ける）
/// <para>知っているのは Curve だけ。ファイルへの保存と Undo への記録は、持ち主が行う（TakeFinishedEdit）</para>
/// <para>操作：クリックで選ぶ / つまんで動かす / ダブルクリックでキーを足す（線の近くなら形を変えない） / Delete で消す /
/// ホイールで拡大縮小（Ctrl：時間だけ、Shift：値だけ） / 中ボタンか Alt + 左ボタンで移動 / F で全体を見る</para>
/// </summary>
class CurveEditor {
public:
	/// <summary>
	/// 描いて、操作を curve に反映する（毎フレーム呼ぶ）
	/// </summary>
	/// <param name="id">ImGui の ID（同じウィンドウに2つ置くときは変える）</param>
	/// <param name="curve">直すカーブ</param>
	/// <param name="canvasHeight">カーブの絵の高さ（ピクセル）</param>
	/// <param name="ui">選んでいるキーの数値を描く係</param>
	/// <returns>このフレームで curve を変えたか</returns>
	bool Draw(const char* id, Curve& curve, float canvasHeight, ComponentUI& ui);

	/// <summary>
	/// 操作（つまんで離す・打って確定する・足す・消す）が終わっていれば、その前と後を返す。1回の操作につき1回だけ返る
	/// </summary>
	std::optional<CurveEdit> TakeFinishedEdit();

private:
	/// <summary>
	/// つまんでいる物
	/// </summary>
	enum class DragTarget {
		None,
		Key,
		InHandle,
		OutHandle,
	};

	/// <summary>
	/// マウスの下にある物
	/// </summary>
	struct Hit {
		DragTarget target = DragTarget::None;
		int key = -1; // キーの番号（無ければ -1）
	};

	// ===== 上の並び =====
	void DrawToolbar(Curve& curve);
	void ApplyPreset(Curve& curve, Ease::Type type);

	// ===== カーブの絵 =====
	void DrawCanvas(Curve& curve, float canvasHeight);
	void HandleView();
	void HandleMouse(Curve& curve);
	void HandleKeyboard(Curve& curve);
	void FrameAll(const Curve& curve);
	Hit FindHit(const Curve& curve, const ImVec2& mouse) const;
	void AddKeyAt(Curve& curve, const Vector2& position, float mouseY);
	void RemoveSelectedKey(Curve& curve);
	void DrawGridLines(ImDrawList* drawList, bool isTime) const;
	void DrawCurve(ImDrawList* drawList, const Curve& curve) const;
	void DrawKeys(ImDrawList* drawList, const Curve& curve) const;

	// ===== 数値の入力 =====
	void DrawKeyFields(Curve& curve, ComponentUI& ui);

	// ===== 座標の変換 =====
	ImVec2 ToScreen(const Vector2& position) const;
	Vector2 ToCurve(const ImVec2& screen) const;

	// ===== 1回の操作 =====
	void BeginEdit(const Curve& curve);
	void FinishEdit(const Curve& curve);

	// ===== 見ている範囲 =====
	Vector2 viewMin_ = {0.0f, 0.0f}; // 左下（時間, 値）
	Vector2 viewMax_ = {1.0f, 1.0f}; // 右上（時間, 値）
	bool needsFrame_ = true;         // 次に描くとき、全体が見える範囲にする
	ImVec2 canvasMin_ = {0.0f, 0.0f};  // 絵の左上（画面のピクセル）
	ImVec2 canvasSize_ = {1.0f, 1.0f}; // 絵の大きさ（ピクセル）

	// ===== 選択・つまんでいる物 =====
	int selectedKey_ = -1;                 // 選んでいるキー（無ければ -1）
	DragTarget drag_ = DragTarget::None;   // つまんでいる物
	Vector2 dragOffset_ = {0.0f, 0.0f};    // つまんだ物とマウスのずれ（カーブの単位）

	// ===== 1回の操作 =====
	// 操作の間は「操作の前のカーブ」から毎回作り直す。途中で区間が縮んでハンドルが縮んでも、戻せば元の長さへ戻る
	std::optional<Curve> editBefore_;       // 操作の前のカーブ（操作をしていなければ空）
	int editKey_ = -1;                      // editBefore_ の中での、直しているキーの番号
	std::optional<CurveEdit> finishedEdit_; // 終わった操作（TakeFinishedEdit で渡す）
};
```

### C-2 `Editor/Widgets/CurveEditor.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "CurveEditor.h"

#include <cmath>
#include <format>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>

#include <externals/magic_enum/magic_enum.hpp>

#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/ECS/ComponentUI.h"

namespace {
constexpr float kKeySize = 5.0f;           // キーの印の大きさ（ピクセル）
constexpr float kHandleSize = 4.0f;        // ハンドルの先の丸の大きさ
constexpr float kHitDistance = 9.0f;       // キー・ハンドルをつかめる距離
constexpr float kOnCurveDistance = 6.0f;   // ダブルクリックを「線の上」とみなす距離
constexpr float kCurveThickness = 2.0f;    // カーブの線の太さ
constexpr float kGridMinPixels = 56.0f;    // 目盛りの間隔の下限（ピクセル）
constexpr int kMaxGridLines = 128;         // 目盛りの線の数の上限
constexpr float kZoomStep = 1.15f;         // ホイール1目盛りの拡大率
constexpr float kFrameMargin = 0.12f;      // 全体を見るときの余白（見る幅に対する割合）
constexpr float kMinViewSpan = 0.001f;     // 見る幅の下限
constexpr float kViewPrecision = 0.00001f; // 見る幅の下限（見ている場所の大きさに対する割合。これより狭いと float で両端が同じ値になる）
constexpr float kPresetWidth = 130.0f;     // Ease Preset の欄の幅

constexpr float kGridAlpha = 0.25f;        // 目盛りの線の濃さ（灰色の文字の色に掛ける）
constexpr float kAxisAlpha = 0.7f;         // 0 の線の濃さ

// 背景・目盛り・キーはテーマの色を使う（エディタの色はリニアなので、暗い色を IM_COL32 で書くと明るく出る）
constexpr ImU32 kCurveColor = IM_COL32(90, 200, 255, 255);    // カーブの線（UI Screen の枠と同じ水色）
constexpr ImU32 kOutsideColor = IM_COL32(90, 200, 255, 90);   // カーブの端より外の線
constexpr ImU32 kSelectedColor = IM_COL32(255, 160, 40, 255); // 選んでいるキーとハンドル（選んだ物の枠と同じ橙）
constexpr ImU32 kHandleColor = IM_COL32(255, 160, 40, 170);   // ハンドルの線

// ===== そのキーの入りのハンドルを使っているか =====
// 区間の補間は左のキーで決まるので、1つ前のキーを見る
bool HasInHandle(const std::vector<CurveKey>& keys, int index) { return index > 0 && keys[index - 1].interp == CurveInterp::Bezier; }

// ===== そのキーの出のハンドルを使っているか =====
bool HasOutHandle(const std::vector<CurveKey>& keys, int index) { return index + 1 < static_cast<int>(keys.size()) && keys[index].interp == CurveInterp::Bezier; }

// ===== 見ている範囲を守る =====
// 幅が 0 になると割り算ができず、float で表せないほど狭いと両端が同じ値になる
void ClampViewAxis(float& min, float& max) {
	const float center = std::clamp((min + max) * 0.5f, -Curve::kMaxValue, Curve::kMaxValue);
	const float minHalf = (std::max)(kMinViewSpan, std::abs(center) * kViewPrecision) * 0.5f;
	const float half = std::clamp((max - min) * 0.5f, minHalf, Curve::kMaxValue);
	min = center - half;
	max = center + half;
}

// ===== 目盛りの間隔 =====
// 1・2・5 × 10 の n 乗 の中で、minStep 以上の一番小さい値（切りの良い数字が並ぶ）
float CalcGridStep(float minStep) {
	const float base = std::pow(10.0f, std::floor(std::log10(minStep)));
	for (const float multiplier : {1.0f, 2.0f, 5.0f}) {
		if (base * multiplier >= minStep) {
			return base * multiplier;
		}
	}
	return base * 10.0f;
}
} // namespace


//=============================================================================
// 描く
//=============================================================================
bool CurveEditor::Draw(const char* id, Curve& curve, float canvasHeight, ComponentUI& ui) {
	ImGui::PushID(id);
	const Curve original = curve;
	// Undo でキーが減ったときに、消えたキーを選んだままにしない
	if (selectedKey_ >= static_cast<int>(curve.GetKeys().size())) {
		selectedKey_ = -1;
		drag_ = DragTarget::None;
	}
	DrawToolbar(curve);
	DrawCanvas(curve, canvasHeight);
	DrawKeyFields(curve, ui);
	FinishEdit(curve);
	ImGui::PopID();
	return curve != original;
}

std::optional<CurveEdit> CurveEditor::TakeFinishedEdit() { return std::exchange(finishedEdit_, std::nullopt); }


//=============================================================================
// 上の並び
//=============================================================================
// ===== プリセットと Frame =====
void CurveEditor::DrawToolbar(Curve& curve) {
	// キーを選んでいて次のキーがあれば、その区間だけに入れる。それ以外は全体
	const bool isSegment = selectedKey_ >= 0 && selectedKey_ + 1 < static_cast<int>(curve.GetKeys().size());
	ImGui::SetNextItemWidth(kPresetWidth);
	if (ImGui::BeginCombo("##easePreset", "Ease Preset", ImGuiComboFlags_HeightLarge)) {
		for (const Ease::Type type : magic_enum::enum_values<Ease::Type>()) {
			const std::string name(magic_enum::enum_name(type));
			if (ImGui::Selectable(name.c_str())) {
				ApplyPreset(curve, type);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Ease と同じ形を入れる（キーを選んでいれば次のキーまでの区間に、選んでいなければ全体に）");
	ImGui::SameLine();
	if (ImGui::Button("Frame")) {
		needsFrame_ = true;
	}
	ImGui::SetItemTooltip("全体が見える範囲にする（F）");
	ImGui::SameLine();
	ImGui::TextDisabled("%s", isSegment ? "Preset: Segment" : "Preset: Whole");
}

// ===== プリセットを入れる =====
void CurveEditor::ApplyPreset(Curve& curve, Ease::Type type) {
	BeginEdit(curve);
	if (selectedKey_ >= 0 && selectedKey_ + 1 < static_cast<int>(curve.GetKeys().size())) {
		curve.ApplyEaseToSegment(selectedKey_, type);
		return;
	}
	curve.ApplyEaseToAll(type);
	selectedKey_ = -1;
	needsFrame_ = true;
}


//=============================================================================
// カーブの絵
//=============================================================================
// ===== 全体 =====
void CurveEditor::DrawCanvas(Curve& curve, float canvasHeight) {
	// ボタンを置くと、つまんでいる間 IsAnyItemActive が true になる（保存と Undo の区切りに使う）
	canvasMin_ = ImGui::GetCursorScreenPos();
	canvasSize_ = ImVec2((std::max)(ImGui::GetContentRegionAvail().x, 1.0f), (std::max)(canvasHeight, 1.0f));
	ImGui::InvisibleButton("canvas", canvasSize_, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // ホイールを、ウィンドウのスクロールより先に受け取る

	// --- 操作（IsItem〜 は直前の項目＝上のボタンを見るので、間に項目を置かない）---
	if (needsFrame_) {
		FrameAll(curve);
	}
	HandleView();
	HandleMouse(curve);
	HandleKeyboard(curve);

	// --- 描く（絵の外にはみ出さないように切る）---
	const ImVec2 canvasMax(canvasMin_.x + canvasSize_.x, canvasMin_.y + canvasSize_.y);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(canvasMin_, canvasMax, true);
	drawList->AddRectFilled(canvasMin_, canvasMax, ImGui::GetColorU32(ImGuiCol_FrameBg));
	DrawGridLines(drawList, true);
	DrawGridLines(drawList, false);
	DrawCurve(drawList, curve);
	DrawKeys(drawList, curve);
	// --- マウスの時間での値（カーブの計算の確認にも使える）---
	if (ImGui::IsItemHovered()) {
		const float time = ToCurve(ImGui::GetIO().MousePos).x;
		const std::string text = std::format("time {:.3f}  value {:.3f}", time, curve.Evaluate(time));
		drawList->AddText(ImVec2(canvasMax.x - ImGui::CalcTextSize(text.c_str()).x - 6.0f, canvasMin_.y + 4.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
	}
	drawList->PopClipRect();
}

// ===== 拡大縮小と移動 =====
void CurveEditor::HandleView() {
	const ImGuiIO& io = ImGui::GetIO();
	// --- 移動（中ボタン・Alt + 左ボタン）---
	const bool isPanning = ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
	if (isPanning) {
		const Vector2 delta = {-io.MouseDelta.x * (viewMax_.x - viewMin_.x) / canvasSize_.x, io.MouseDelta.y * (viewMax_.y - viewMin_.y) / canvasSize_.y};
		viewMin_ += delta;
		viewMax_ += delta;
	}
	// --- 拡大縮小（マウスの下の場所が動かないように、マウスを中心に広げる）---
	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
		const float zoom = std::pow(kZoomStep, -io.MouseWheel);
		const Vector2 center = ToCurve(io.MousePos);
		if (!io.KeyShift) {
			viewMin_.x = center.x + (viewMin_.x - center.x) * zoom;
			viewMax_.x = center.x + (viewMax_.x - center.x) * zoom;
		}
		if (!io.KeyCtrl) {
			viewMin_.y = center.y + (viewMin_.y - center.y) * zoom;
			viewMax_.y = center.y + (viewMax_.y - center.y) * zoom;
		}
	}
	ClampViewAxis(viewMin_.x, viewMax_.x);
	ClampViewAxis(viewMin_.y, viewMax_.y);
}

// ===== マウス（選ぶ・つまむ・足す）=====
void CurveEditor::HandleMouse(Curve& curve) {
	const ImGuiIO& io = ImGui::GetIO();
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		drag_ = DragTarget::None;
	}
	if (!ImGui::IsMousePosValid()) {
		return;
	}
	const Vector2 mouse = ToCurve(io.MousePos);

	// --- 押した ---
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
		const Hit hit = FindHit(curve, io.MousePos);
		selectedKey_ = hit.key;
		drag_ = hit.target;
		if (hit.target == DragTarget::None) {
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				AddKeyAt(curve, mouse, io.MousePos.y);
			}
		} else {
			const CurveKey& key = curve.GetKeys()[hit.key];
			Vector2 position = {key.time, key.value};
			if (hit.target == DragTarget::InHandle) {
				position += key.inHandle;
			} else if (hit.target == DragTarget::OutHandle) {
				position += key.outHandle;
			}
			dragOffset_ = position - mouse;
		}
	}

	// --- つまんで動かす（押した場所から少し動いてから。クリックだけでは値を変えない）---
	if (drag_ == DragTarget::None || !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		return;
	}
	const Vector2 target = mouse + dragOffset_;
	BeginEdit(curve);
	curve = *editBefore_;
	const CurveKey& key = editBefore_->GetKeys()[editKey_];
	switch (drag_) {
	case DragTarget::Key: {
		CurveKey moved = key;
		moved.time = target.x;
		moved.value = target.y;
		selectedKey_ = curve.SetKey(editKey_, moved);
		break;
	}
	case DragTarget::InHandle:
		curve.SetHandle(editKey_, CurveHandleSide::In, target - Vector2{key.time, key.value});
		break;
	case DragTarget::OutHandle:
		curve.SetHandle(editKey_, CurveHandleSide::Out, target - Vector2{key.time, key.value});
		break;
	case DragTarget::None:
		break;
	}
}

// ===== キーボード =====
void CurveEditor::HandleKeyboard(Curve& curve) {
	if (!ImGui::IsItemHovered() && !ImGui::IsItemFocused()) {
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		needsFrame_ = true;
	}
	if (selectedKey_ >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		RemoveSelectedKey(curve);
	}
}

// ===== 全体を見る =====
void CurveEditor::FrameAll(const Curve& curve) {
	needsFrame_ = false;
	const std::vector<CurveKey>& keys = curve.GetKeys();
	if (keys.empty()) {
		viewMin_ = {0.0f, 0.0f};
		viewMax_ = {1.0f, 1.0f};
		return;
	}
	// --- 値はハンドルの先まで入れる（ベジェは制御点の内側を通るので、これではみ出さない）---
	Vector2 low = {keys.front().time, keys.front().value};
	Vector2 high = {keys.back().time, keys.front().value};
	for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
		const CurveKey& key = keys[i];
		const float inValue = key.value + (HasInHandle(keys, i) ? key.inHandle.y : 0.0f);
		const float outValue = key.value + (HasOutHandle(keys, i) ? key.outHandle.y : 0.0f);
		low.y = (std::min)({low.y, key.value, inValue, outValue});
		high.y = (std::max)({high.y, key.value, inValue, outValue});
	}
	const Vector2 margin = {(std::max)(high.x - low.x, kMinViewSpan) * kFrameMargin, (std::max)(high.y - low.y, kMinViewSpan) * kFrameMargin};
	viewMin_ = low - margin;
	viewMax_ = high + margin;
}

// ===== マウスの下にある物 =====
CurveEditor::Hit CurveEditor::FindHit(const Curve& curve, const ImVec2& mouse) const {
	const std::vector<CurveKey>& keys = curve.GetKeys();
	Hit hit;
	float nearest = kHitDistance * kHitDistance;
	const auto check = [&](DragTarget target, int index, const Vector2& position) {
		const ImVec2 screen = ToScreen(position);
		const float distance = (screen.x - mouse.x) * (screen.x - mouse.x) + (screen.y - mouse.y) * (screen.y - mouse.y);
		if (distance <= nearest) {
			nearest = distance;
			hit = {target, index};
		}
	};
	// --- ハンドルは選んでいるキーの分だけ（キーを後に調べるので、長さ 0 のハンドルがキーの邪魔をしない）---
	if (selectedKey_ >= 0) {
		const CurveKey& key = keys[selectedKey_];
		const Vector2 position = {key.time, key.value};
		if (HasInHandle(keys, selectedKey_)) {
			check(DragTarget::InHandle, selectedKey_, position + key.inHandle);
		}
		if (HasOutHandle(keys, selectedKey_)) {
			check(DragTarget::OutHandle, selectedKey_, position + key.outHandle);
		}
	}
	for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
		check(DragTarget::Key, i, {keys[i].time, keys[i].value});
	}
	return hit;
}

// ===== キーを足す =====
void CurveEditor::AddKeyAt(Curve& curve, const Vector2& position, float mouseY) {
	BeginEdit(curve);
	const float curveY = ToScreen({position.x, curve.Evaluate(position.x)}).y;
	const bool isOnCurve = !curve.GetKeys().empty() && std::abs(curveY - mouseY) <= kOnCurveDistance;
	selectedKey_ = isOnCurve ? curve.InsertKeyOnCurve(position.x) : curve.AddKey(position.x, position.y);
	drag_ = DragTarget::None; // この操作は「足す」だけ（editBefore_ には足したキーが無いので、続けてつまめない）
}

// ===== 選んでいるキーを消す =====
void CurveEditor::RemoveSelectedKey(Curve& curve) {
	BeginEdit(curve);
	curve.RemoveKey(selectedKey_);
	selectedKey_ = -1;
	drag_ = DragTarget::None;
}

// ===== 目盛り =====
// 線の数に上限を付ける（float の足し算で進めると、大きな場所では値が進まず止まらなくなるので、番号から計算する）
void CurveEditor::DrawGridLines(ImDrawList* drawList, bool isTime) const {
	const float min = isTime ? viewMin_.x : viewMin_.y;
	const float max = isTime ? viewMax_.x : viewMax_.y;
	const float pixels = isTime ? canvasSize_.x : canvasSize_.y;
	const float step = CalcGridStep(kGridMinPixels * (max - min) / pixels);
	const float first = std::ceil(min / step);
	const float lineHeight = ImGui::GetTextLineHeight();
	const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, kGridAlpha);
	const ImU32 axisColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, kAxisAlpha);
	const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	for (int i = 0; i < kMaxGridLines; ++i) {
		const float position = (first + static_cast<float>(i)) * step;
		if (position > max) {
			break;
		}
		const ImVec2 screen = ToScreen({position, position});
		const ImVec2 start = isTime ? ImVec2(screen.x, canvasMin_.y) : ImVec2(canvasMin_.x, screen.y);
		const ImVec2 end = isTime ? ImVec2(screen.x, canvasMin_.y + canvasSize_.y) : ImVec2(canvasMin_.x + canvasSize_.x, screen.y);
		const bool isZero = std::abs(position) < step * 0.5f;
		drawList->AddLine(start, end, isZero ? axisColor : gridColor);
		const ImVec2 labelPosition(start.x + 3.0f, isTime ? end.y - lineHeight : start.y);
		drawList->AddText(labelPosition, labelColor, std::format("{:.6g}", isZero ? 0.0f : position).c_str());
	}
}

// ===== カーブの線 =====
void CurveEditor::DrawCurve(ImDrawList* drawList, const Curve& curve) const {
	const std::vector<CurveKey>& keys = curve.GetKeys();
	if (keys.empty()) {
		return;
	}
	// --- 端より外（値は端のキーのまま）---
	drawList->AddLine(ToScreen({viewMin_.x, keys.front().value}), ToScreen({keys.front().time, keys.front().value}), kOutsideColor, kCurveThickness);
	drawList->AddLine(ToScreen({keys.back().time, keys.back().value}), ToScreen({viewMax_.x, keys.back().value}), kOutsideColor, kCurveThickness);
	// --- 区間ごと ---
	for (size_t i = 0; i + 1 < keys.size(); ++i) {
		const CurveKey& a = keys[i];
		const CurveKey& b = keys[i + 1];
		const Vector2 start = {a.time, a.value};
		const Vector2 end = {b.time, b.value};
		switch (a.interp) {
		case CurveInterp::Constant:
			drawList->AddLine(ToScreen(start), ToScreen({b.time, a.value}), kCurveColor, kCurveThickness);
			drawList->AddLine(ToScreen({b.time, a.value}), ToScreen(end), kCurveColor, kCurveThickness);
			break;
		case CurveInterp::Linear:
			drawList->AddLine(ToScreen(start), ToScreen(end), kCurveColor, kCurveThickness);
			break;
		case CurveInterp::Bezier:
			drawList->AddBezierCubic(ToScreen(start), ToScreen(start + a.outHandle), ToScreen(end + b.inHandle), ToScreen(end), kCurveColor, kCurveThickness);
			break;
		}
	}
}

// ===== キーとハンドルの印 =====
void CurveEditor::DrawKeys(ImDrawList* drawList, const Curve& curve) const {
	const std::vector<CurveKey>& keys = curve.GetKeys();
	const ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_Text);
	// --- 選んでいるキーのハンドル ---
	if (selectedKey_ >= 0) {
		const CurveKey& key = keys[selectedKey_];
		const Vector2 position = {key.time, key.value};
		const auto drawHandle = [&](const Vector2& handle) {
			drawList->AddLine(ToScreen(position), ToScreen(position + handle), kHandleColor, 1.0f);
			drawList->AddCircleFilled(ToScreen(position + handle), kHandleSize, kSelectedColor);
		};
		if (HasInHandle(keys, selectedKey_)) {
			drawHandle(key.inHandle);
		}
		if (HasOutHandle(keys, selectedKey_)) {
			drawHandle(key.outHandle);
		}
	}
	// --- キー（ひし形）---
	for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
		const ImVec2 center = ToScreen({keys[i].time, keys[i].value});
		const ImVec2 top(center.x, center.y - kKeySize);
		const ImVec2 right(center.x + kKeySize, center.y);
		const ImVec2 bottom(center.x, center.y + kKeySize);
		const ImVec2 left(center.x - kKeySize, center.y);
		drawList->AddQuadFilled(top, right, bottom, left, (i == selectedKey_) ? kSelectedColor : keyColor);
	}
}


//=============================================================================
// 数値の入力
//=============================================================================
void CurveEditor::DrawKeyFields(Curve& curve, ComponentUI& ui) {
	if (selectedKey_ < 0) {
		ui.Label("キーを選ぶと、ここで数値を打てる");
		return;
	}
	const std::vector<CurveKey>& keys = curve.GetKeys();
	const CurveKey key = keys[selectedKey_];
	CurveKey edited = key;
	CurveManager::DescribeKey(ui, edited, HasInHandle(keys, selectedKey_), HasOutHandle(keys, selectedKey_));
	if (edited != key) {
		BeginEdit(curve);
		curve = *editBefore_;
		if (edited.inHandle != key.inHandle) {
			curve.SetHandle(editKey_, CurveHandleSide::In, edited.inHandle);
		} else if (edited.outHandle != key.outHandle) {
			curve.SetHandle(editKey_, CurveHandleSide::Out, edited.outHandle);
		} else {
			// 触っていないハンドルは操作の前の値を使う（時間を動かして区間が一度縮んでも、戻せば元の長さへ戻る）
			const CurveKey& base = editBefore_->GetKeys()[editKey_];
			edited.inHandle = base.inHandle;
			edited.outHandle = base.outHandle;
			selectedKey_ = curve.SetKey(editKey_, edited);
		}
	}
	if (ImGui::SmallButton("Delete Key")) {
		RemoveSelectedKey(curve);
	}
	ImGui::SetItemTooltip("選んでいるキーを消す（Delete）");
}


//=============================================================================
// 座標の変換
//=============================================================================
// ===== カーブの座標 → 画面のピクセル（値は上が大きいので、y を逆にする）=====
ImVec2 CurveEditor::ToScreen(const Vector2& position) const {
	return ImVec2(canvasMin_.x + (position.x - viewMin_.x) / (viewMax_.x - viewMin_.x) * canvasSize_.x, canvasMin_.y + (viewMax_.y - position.y) / (viewMax_.y - viewMin_.y) * canvasSize_.y);
}

// ===== 画面のピクセル → カーブの座標 =====
Vector2 CurveEditor::ToCurve(const ImVec2& screen) const {
	return {viewMin_.x + (screen.x - canvasMin_.x) / canvasSize_.x * (viewMax_.x - viewMin_.x), viewMax_.y - (screen.y - canvasMin_.y) / canvasSize_.y * (viewMax_.y - viewMin_.y)};
}


//=============================================================================
// 1回の操作
//=============================================================================
// ===== 始める（もう始まっていれば何もしない）=====
void CurveEditor::BeginEdit(const Curve& curve) {
	if (!editBefore_.has_value()) {
		editBefore_ = curve;
		editKey_ = selectedKey_;
	}
}

// ===== どの項目も操作していなければ、終わりにする =====
void CurveEditor::FinishEdit(const Curve& curve) {
	if (!editBefore_.has_value() || ImGui::IsAnyItemActive()) {
		return;
	}
	if (*editBefore_ != curve) {
		finishedEdit_ = CurveEdit{std::move(*editBefore_), curve};
	}
	editBefore_.reset();
}
#endif
```

### C-3 `Editor/Inspector/AssetInspector.h`

`AssetEditor` の `GetTypeName` の下に足す。

```cpp
	virtual std::string_view GetTypeName() const = 0;

	// ファイルを読めるか調べる（クリックで選ばれたときに1回だけ呼ばれる）。読めないと Draw は呼ばれない。ここで中身を読まない種類は書かなくてよい
	virtual bool CanLoad(const std::filesystem::path& filePath) {
		(void)filePath;
		return true;
	}
```

### C-4 `Editor/Inspector/AssetInspector.cpp`

上の変数に足す。

```cpp
std::filesystem::path unsaved;  // 直して、まだ保存していないファイル（空なら無し）
bool needsLoadCheck = false;    // 選んだファイルを読めるか、まだ調べていない
bool isBroken = false;          // 選んでいるファイルが読めない
```

`Select` の最後に足す。

```cpp
	selected = filePath;
	needsLoadCheck = true; // クリックのたびに調べ直す（直したファイルは、クリックし直せば読める）
}
```

`Draw` の `ImGui::Separator();` と「中身。変えたら覚えておき」のコメントの間に足す。

```cpp
	ImGui::Separator();

	// --- 読めるかは選んだときに1回だけ調べる（読めないファイルを毎フレーム読むと、ログがエラーで埋まる）---
	if (needsLoadCheck) {
		needsLoadCheck = false;
		isBroken = !editor->CanLoad(selected);
	}
	if (isBroken) {
		GetInspectorUI().Label("このファイルは読めません（無い・JSON が壊れている。直したらクリックし直す）");
		return;
	}

	// --- 中身。変えたら覚えておき、つまむ・打つのが終わったら（どの項目も操作していない）保存する ---
```

### C-5 `Editor/Inspector/AttackTableEditor.cpp`

`AttackTableAssetEditor` の `GetTypeName` の下に1行足して、`Draw` の `ui.Label` の行を消す。

```cpp
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return AttackTableManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = AttackTableManager::Load(filePath);
		const AttackTable* current = AttackTableManager::Get(handle);
		if (current == nullptr) {
			return false;
		}
```

### C-6 `Physics/PhysicMaterialManager.cpp`

`PhysicMaterialEditor` も同じ。

```cpp
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return PhysicMaterialManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = PhysicMaterialManager::Load(filePath);
		if (handle == 0) {
			return false;
		}
```

### C-7 `Editor/Inspector/CurveAssetEditor.h`（新しいファイル）

```cpp
#pragma once

/// <summary>
/// .curve のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>絵と操作は CurveEditor に任せて、ここはファイルへの書き戻しと Undo への記録だけを行う</para>
/// </summary>
class CurveAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};
```

### C-8 `Editor/Inspector/CurveAssetEditor.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "CurveAssetEditor.h"

#include <memory>
#include <utility>
#include <cstdint>
#include <optional>

#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Editor/Widgets/CurveEditor.h"

namespace {
constexpr std::string_view kTypeName = "Curve"; // Inspector の見出しに出す名前
constexpr float kCanvasHeight = 280.0f;         // カーブの絵の高さ（ピクセル）

/// <summary>
/// .curve のファイルを Inspector で直す係
/// </summary>
class CurveFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return CurveManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return CurveManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = CurveManager::Load(filePath);
		const Curve* current = CurveManager::Get(handle);
		if (current == nullptr) {
			return false;
		}
		// 別のファイルへ移ったら、見ている範囲と選択をやり直す
		if (handle != handle_) {
			handle_ = handle;
			editor_ = CurveEditor{};
		}
		// 写しを直して、変わっていたら書き戻す
		Curve curve = *current;
		const bool isChanged = editor_.Draw("curve", curve, kCanvasHeight, ui);
		if (isChanged) {
			CurveManager::Set(handle, curve);
		}
		// 操作が終わったら Undo に積む（値はもう変わっているので Record。ファイルも一緒に戻す）
		if (std::optional<CurveEdit> edit = editor_.TakeFinishedEdit()) {
			const auto apply = [handle](const Curve& value) {
				CurveManager::Set(handle, value);
				return CurveManager::Save(handle);
			};
			EditorHistory::Record([apply, before = std::move(edit->before)] { return apply(before); }, [apply, after = std::move(edit->after)] { return apply(after); });
		}
		ui.Label("ダブルクリック：キーを足す / Delete：消す / F：全体を見る");
		ui.Label("ホイール：拡大縮小（Ctrl：時間だけ、Shift：値だけ） / 中ボタン：移動");
		return isChanged;
	}

	void Save(const std::filesystem::path& filePath) override { CurveManager::Save(CurveManager::Load(filePath)); }

private:
	uint32_t handle_ = 0; // 今直しているファイル
	CurveEditor editor_;  // 絵と操作
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void CurveAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<CurveFileEditor>()); }
#endif
```

### C-9 `Systems/EngineSystems.cpp`

`#ifdef USE_IMGUI` の中の include に足す。

```cpp
#include "MyEngine/Editor/Inspector/AttackTableEditor.h"
#include "MyEngine/Editor/Inspector/CurveAssetEditor.h"
```

`Initialize` の `#ifdef USE_IMGUI` の中に足す。

```cpp
	AttackTableEditor::Initialize(); // .attacktable のファイルを Inspector で直す
	CurveAssetEditor::Initialize();  // .curve のファイルを Inspector で直す
```

### C-10 `Editor/Windows/ProjectWindowActions.cpp`

include に足す。

```cpp
#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Curve/CurveManager.h"
```

`DrawCreateMenu` の Attack Table の下に足す。

```cpp
		ImGui::SetItemTooltip("今のフォルダに作ります（敵のフェーズと攻撃。Attack Table の Table で選ぶ）");
		// Curve
		if (ImGui::MenuItem("Curve")) {
			CreateAssetFile("NewCurve", CurveManager::kFileExtension, &CurveManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に対して値がどう変わるかのカーブ）");
```

## パートD 確認

### D-1 確認の手順

1. Debug をビルドして起動する。ログに `[curve] self test OK (93 / 93)` が出れば、計算の確認は通っている。合わないと assert で止まり、合わなかった項目がログに出る。
2. Project ウィンドウで `resources` の下のフォルダを開いて、右クリック → **Create → Curve**。
3. できた `NewCurve.curve` を選ぶと、Inspector にカーブが出る。下の D-3 の表の順に確かめる。

### D-2 使い方

ファイルをゲームから使うとき。`Fields` に `AssetField` を書くと、Inspector で `.curve` を選べて、シーンにも保存される。カーブの値を Entity の高さへ書く見本（ゲーム側のコード）。

```cpp
#include <MyEngine/Curve/CurveManager.h>
#include <MyEngine/ECS/Script.h>

// カーブの値を、この Entity の高さへ書く見本
class CurveMover : public Script {
public:
	void Update() override {
		time_ += Time::GetDeltaTime();
		const Curve* curve = CurveManager::Get(curve_); // 選んでいなければ nullptr
		if (curve != nullptr) {
			transform.translation.y = curve->Evaluate(time_);
		}
	}

	void Fields(ComponentUI& ui) override { ui.AssetField("Curve", curve_, AssetType::Curve); }

private:
	uint32_t curve_ = 0; // カーブのファイル（AssetType::Curve の番号）
	float time_ = 0.0f;  // 始まってからの時間
};
SCRIPT(CurveMover);
```

ほかのファイルの中に入れるとき（段階3の Timeline はこの形）。

```cpp
SceneJson json = CurveManager::ToJson(curve);          // 書く
std::set<std::string> warnings;
Curve loaded = CurveManager::FromJson(json, warnings); // 読む
```

### D-3 確認項目

「写しで確認」は、一時フォルダの写し（AL4_01 と同じ形）で実際に動かした結果。写経の後に、同じ項目を確かめる。

**計算（`CurveSelfTest`。起動すると自動で流れる）**

| # | 区分 | 確かめること | 写しで確認 |
|---|---|---|---|
| 1 | キーが0個・1個 | 0個はいつも 0。1個はどの時間でもその値 | 合う |
| 2 | カーブの端 | 最初のキーより前・ちょうど、最後のキーちょうど・より後 | 端のキーの値 |
| 3 | 補間の種類 | Constant は次のキーの手前まで今の値で、ちょうどで次の値。Linear の途中。補間は左のキーで決まる | 合う |
| 4 | ベジェの代表的な値 | CSS の ease（0.25 → 0.4085106、0.5 → 0.8024034）・ease-in・ease-in-out と、時間と値を広げた区間 | 別の計算で求めた値と 0.00002 以内（広げた区間は 0.001 以内） |
| 5 | 同じ時間のキー | 段差の手前・ちょうど・後。全部同じ時間のキー。間のハンドルの長さが 0 になる | ちょうどは後ろのキーの値 |
| 6 | 壊れた入力 | NaN・±無限大の時間、NaN・無限大のキー、ばらばらの順のキー、1e30 の値、知らない番号 | 値は全部有限。並びは時間の順。知らない番号は何もしない |
| 7 | ハンドルの制限 | 区間より長いハンドル、逆向きのハンドル、区間が縮んだとき | 区間の長さと 0 で止まる。傾きは変わらない |
| 8 | 解き方 | 接線が真上を向く場合（傾きが 0 に触れる）。でたらめな x1・x2 を 2000 組、65 点ずつ | 止まらず、値は増え続ける。解いた時間の差は 0.00002 以内 |
| 9 | でたらめなカーブ | キー 1〜6 個、ハンドル ±30 のカーブを 2000 本、65 点ずつ | 値は全部有限。ハンドルは全部区間の中 |
| 10 | Ease と同じ形 | 14 種類を 2001 点で `Ease::Apply` と比べる | 全部 0.001 以内（1.5 の表）。一番多いキーの数は 21 |
| 11 | 区間・全体の Ease | 隣の区間と、両端のキーが変わらない。キーが無いカーブは 0〜1 で作る | 合う |
| 12 | キーの足し引き | 線の上へ足す（3種類の補間）、同じ時間へ足す、カーブの外へ足す、追い越し、同じ時間のキーの順番、Aligned と Free、全部消す | 形の差は 0.001 以内。番号も合う |
| 13 | 保存の形 | 文字にしてから読み直す。キー0個。`keys` が無い・配列でない、壊れた項目、逆の順 | 同じカーブになる。壊れた物は警告を出して止まらない |

**エディタの操作**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 1 | ファイルを作る | Project で右クリック → Create → Curve | `NewCurve.curve` ができて、Inspector にカーブが出る（2つ目は `NewCurve1.curve`） |
| 2 | 保存したカーブが残る | 直してから起動し直して、ファイルを選ぶ | 保存した形で出る |
| 3 | キーを選ぶ | キーをクリック | 橙になり、ハンドルと数値の欄が出る。使わないハンドルの欄は灰色 |
| 4 | ハンドルを動かす | 丸をドラッグして離す | 離すとファイルに書かれる |
| 5 | ハンドルの制限 | 丸を次のキーより右へ、キーより左へドラッグ | 次のキーの時間で止まり、傾きは保つ。左は時間 0 で止まる |
| 6 | Undo・Redo | 4 の後に Ctrl+Z・Ctrl+Y | カーブもファイルも前の値に戻り、やり直せる |
| 7 | 何も無い所へキーを足す | ダブルクリック | その場所に Aligned のキーが足される。前のキーのハンドルは傾きを保って縮む |
| 8 | 線の上へキーを足す | 線の近くをダブルクリック | 形が変わらない。入りと出の傾きが同じ（-0.0775 ÷ -0.1276 = 0.0287 ÷ 0.0472） |
| 9 | キーを動かす | つまんで隣のキーを越え、戻して離す | 越えている間は並びが変わる。戻すと隣のハンドルの長さが元の値に戻る |
| 10 | キーを消す | キーを選んで Delete | 消えてファイルに書かれる（`.bak` がキー3個）。Ctrl+Z で戻る |
| 11 | Delete Key のボタン | キーを選んでボタン | 消える。Ctrl+Z で戻る |
| 12 | 拡大縮小 | ホイール | マウスの下を中心に広がる。目盛りの数字も細かくなる |
| 13 | 片方の軸だけ | Ctrl+ホイール・Shift+ホイール | Ctrl は時間だけ、Shift は値だけ変わる |
| 14 | 移動 | 中ボタン・Alt+左ボタンのドラッグ | 見る場所が動く。Alt のときキーは選ばれない |
| 15 | 全体を見る | F・Frame のボタン | 全体が入る |
| 16 | 全体のプリセット | 何も選ばず Ease Preset → OutBounce | キー5個で Ease と同じ形。表示も合わせ直す |
| 17 | 区間のプリセット | キーを選んで Ease Preset → InCubic | `Preset: Segment` と出て、その区間だけ変わる |
| 18 | 補間の種類 | Interpolation を Constant・Linear・Bezier | 線が段・直線・ベジェに変わり、使わない出のハンドルは消える |
| 19 | Aligned | 真ん中のキーの出のハンドルを動かす | 入りも同じ傾き（0.0728 ÷ -0.0610 = -0.0591 ÷ 0.0495）。入りの時間の長さは変わらない |
| 20 | 時間を打つ | Time に 0.8 と打って Enter | 隣のキーを越えて並びが変わる。選択は同じキーのまま |
| 21 | 数値の欄をつまむ | Time の欄を横へドラッグ | キーが動く。1回の Ctrl+Z で全部戻る |
| 22 | キー0個・1個 | キーを全部消して、足し直す | 壊れない。線は端のキーの値のまま。Undo で0個へ戻せる |
| 23 | Undo でキーが減る | 最後のキーを選んだまま Ctrl+Z | 選択が外れる |
| 24 | 別のファイルへ移る | 別の `.curve` を選ぶ | 見ている範囲と選択がやり直される |
| 25 | 別のファイルを見ながら Undo | 24 の後に Ctrl+Z | 前のファイルが戻る（見えているカーブは変わらない） |
| 26 | とても大きな値 | Value に 1e30 と打つ | 1e+07 に収まる。F で全体が入る |
| 27 | NaN | Value に nan と打つ | 0 になる |
| 28 | 極端な拡大縮小 | ホイールを大きな量で何度も回す | 止まらない。範囲の限界で止まる |
| 29 | 壊れたファイル | 中身が JSON でない `.curve`・`.attacktable`・`.physmat` を選ぶ | どれも「読めません」と出る。ログのエラーはクリックのたびに1回 |
| 30 | 変な中身のファイル | キーでない物・形の違う項目・知らない名前が混ざった `.curve` を選ぶ | 読めるキーだけ出る。警告は1つにつき1回 |
| 31 | 壊れたファイルを直す | 選んだままファイルを直して、同じファイルをクリックし直す | カーブが出る |
| 32 | ほかの種類は今までどおり | 正しい `.attacktable`・`.physmat`、`.uiscreen` を選ぶ | いつもの Inspector が出る |

**ビルドと製品版**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 33 | 3 構成のビルド | Debug・Development・Release | エラー 0。今回コンパイルしたファイルの警告は 0 |
| 34 | 起動・終了 | Debug を起動して閉じる | 終了コード 0。ログの error・warning は 0 |
| 35 | 3 構成の自己テスト | Debug の起動。Development・Release は一時的に呼ぶように変えて起動 | どれも 93 / 93 |
| 36 | 製品版で読む | Release で `LoadAsset(AssetType::Curve, 〜)` と `Evaluate` を呼ぶ | 読めた（キー3個、`Evaluate(0.6)` = 0.642369） |
| 37 | ゲームからヘッダを読む | ゲームの `main.cpp` から `#include <MyEngine/Curve/CurveManager.h>` | コンパイルが通る |
| 38 | パートB の後 | パートC のファイルと行を除いた状態 | Debug のビルドと起動が通り、自己テストは 93 / 93 |
| 39 | シーンから使う | D-2 の `CurveMover` を付けたシーンを Release で動かす | シーンのパスから `.curve` を読み、0.6 秒で高さが `Evaluate` と同じ 0.642592 になった |

### D-4 検証の結果

- **ビルド：** Debug・Development・Release の3構成が通った。新しい5つの `.cpp` と、直した `ComponentUI.cpp`・`ProjectWindowActions.cpp`・`EngineSystems.cpp`・`Engine.cpp`・`AssetInspector.cpp`・`AttackTableEditor.cpp`・`PhysicMaterialManager.cpp` のコンパイルの警告は 0 だった。
- **写し：** AL4_01 の形の一時フォルダで、エンジンを `ab28b2c` に合わせて確かめた。md のコードは、そのフォルダでコンパイルしたファイルから埋め込んだ。
- **計算：** 上の 13 区分、全 93 項目。Debug の起動で通っている。Development・Release は `Engine.cpp` を一時的に変えて流し、同じく通った（md の形では Debug だけで呼ぶ）。
- **エディタの操作：** ImGui へ入力を流して確かめた。5・11・15（ボタン）・18・29〜32 は Development、それ以外は Debug で流した。保存の結果はファイルの中身で、絵は撮った画面で見た。`AssetInspector` の直しは読めるかの判定だけなので、1〜28・30 と Development・Release の自己テストは直しの前の結果で、直しの後は 29・31・32 とカーブの表示、Debug の自己テスト・起動・終了を流した。
- **確認の中で見つけて直した物：**
  - OutElastic だけ最後の区間が 0.027 ずれた。`t = 1` の値の飛びを傾きの計算で拾っていたので、区間の内側の3点から測るようにした（1.5）。
  - 壊れたファイルを選ぶと、Inspector が毎フレーム読み直してログが埋まった。`.attacktable`・`.physmat` も同じだったので、`AssetInspector` の1か所で直した（1.10）。
  - 背景と目盛りが明るく出た。エディタの色はリニアなので、テーマの色を使うようにした。
  - 説明の1行が右端で切れたので、2行に分けた。

### D-5 未確認のこと・気を付けること

- **本物のマウスとキーボード：** 表の操作を手で通していない。
- **AL4_01 の実物：** 写しのゲームは `main.cpp` とシーンが違う。エンジンのコードは同じ。
- **パートA の後：** `Curve.cpp` は単体のコンパイルで確かめた。エンジン全体のビルドはパートB の後で確かめた（パートA は新しいファイルだけなので、ほかのファイルに影響しない）。
- **Inspector のスクロール：** 中身が画面に収まる高さだったので、絵の上のホイールで Inspector がスクロールしないことは確かめていない。
- **`.curve` のアイコン：** 専用の絵は作っていない。Project ではふつうの書類の絵で出る。
- **Release のリンクの警告：** LNK4020 が 7 件出た。写しのゲーム側の `BossTestController.obj` などの `.pdb` についての物で、今回コンパイルしたファイルではない。
- **長さ 0 のハンドル：** キーと重なるとキーが優先されるので、マウスではつかめない。下の数値の欄で直す。
- **全体のプリセット：** 最初と最後のキーの値の間に形を入れるので、2つの値が同じだと平らになる。
- **別のファイルを見ながらの Ctrl+Z：** 見えていないファイルが戻る（表の 25）。
- **消したファイルの Undo：** Project でファイルを消した後に Ctrl+Z を押すと、コードの上ではファイルが書き直される。試していない。
