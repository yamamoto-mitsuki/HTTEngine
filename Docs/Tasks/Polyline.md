参考：
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- 前の段階：[VectorShape.md](VectorShape.md)・[Timeline.md](Timeline.md)・[TimelineWindow.md](TimelineWindow.md)・[UIAnimation.md](UIAnimation.md)
- 線の角と端の形の名前（round join・Butt・Round・Square）：[stroke-linejoin（MDN）](https://developer.mozilla.org/en-US/docs/Web/SVG/Reference/Attribute/stroke-linejoin)・[stroke-linecap（MDN）](https://developer.mozilla.org/en-US/docs/Web/SVG/Reference/Attribute/stroke-linecap)
- 耳切り法：[Triangulation by Ear Clipping（David Eberly）](https://www.geometrictools.com/Documentation/TriangulationByEarClipping.pdf)
- HLSL の `nointerpolation`：https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-struct
- 比較の `= default`（C++20）：https://en.cppreference.com/w/cpp/language/default_comparisons
- `std::unordered_map::try_emplace`：https://en.cppreference.com/w/cpp/container/unordered_map/try_emplace
- `std::erase_if`（unordered_map）：https://en.cppreference.com/w/cpp/container/unordered_map/erase_if
- `std::shared_ptr::use_count`：https://en.cppreference.com/w/cpp/memory/shared_ptr/use_count
- `std::function`：https://en.cppreference.com/w/cpp/utility/functional/function

# UI 段階6（1/2）：折れ線（Polyline）

資料の「折れ線」を作る。点をつないだ線で、点ごとに角を円弧で曲げる。線の太さ・端の形を変えられ、閉じれば塗った形にもなる。資料と同じく、メッシュで大まかな形を作り、直線の部分と角の部分の縁はシェーダーが距離で描く。

- 点ごとのカーブの半径・線の太さ・端の形（Butt・Round・Square）・閉じた形の塗り
- 点を Timeline で動かす（今の Value トラックで、`point3` などの項目を指す）
- メッシュの作り直しを、点・半径・太さが変わった時だけにする
- Scene ビューで点を動かす・足す・消すギズモ

段階6 は作業が多いので、2つの md に分けた。**この md を先に写す。**

1. この md：折れ線（描画・Component・頂点のギズモ・Timeline の見本・作り直しの計測）
2. [UIMask.md](UIMask.md)：マスク（入れ子あり）・模様（Image の繰り返し）・見本（波打つ図形＋ドット＋縁取り）

パートは **A → B → C の順に写す**。D は見本（ゲーム側へ写す）、E は確認、F は写しで確かめた結果。

作業の前に、develop からブランチを切る。

```bash
git switch -c feature/Polyline develop
```

## 0章 全体の形

### 0.1 流れ

```
PolylineComponent（点・角の半径・閉じる・塗り・線の太さ・端の形。毎フレーム読むだけ）
   │  PolylineSystem::Draw
   │    ① Scale を掛けた点・半径・太さ（PolylineShape）を作る
   │    ② 前のフレームと同じなら、覚えておいたメッシュをそのまま使う
   │       違えば PolylineMeshBuilder::Build で作り直す（直線の部分・角の部分・端・塗り）
   │    ③ 回転・移動・アンカーは行列、色・透明度は定数で渡す（メッシュに入れない）
   ▼
Renderer::DrawPolyline → RenderQueue の 2D の列（Image・Vector Shape・Text と Order の順に混ざる）
   │  Flush2d
   ▼
RenderContext::DrawPolyline：頂点と番号を FrameUploadBuffer に書いて描く
   │
   ▼
Polyline.VS：点を行列で画面へ写す
Polyline.PS：部分の種類（直線・角・端・塗り）ごとに輪郭までの距離を測り、1 ピクセルの幅でなめらかにする
```

### 0.2 作る物

| ファイル | 役目 |
|---|---|
| `Components/UI/PolylineComponent.h`（新しい） | 折れ線の Component（保存名 `Polyline`、カテゴリ UI） |
| `Systems/UI/PolylineMesh.h`・`.cpp`（新しい） | 点からメッシュを作る（直線の部分・角の部分・端・塗り） |
| `Systems/UI/PolylineSystem.h`・`.cpp`（新しい） | 登録・Inspector・メッシュを覚える・描く |
| `Shader/Data/VectorShape/Polyline.hlsli`・`.VS`・`.PS`（新しい） | 折れ線のシェーダー |
| `Shader/Data/VectorShape/VectorCoverage.hlsli`（新しい） | 縁のなめらかさと重ね方の直し。Vector Shape と共通にした |
| `Editor/Gizmos/PolylineGizmo.h`・`.cpp`（新しい） | Scene ビューで点を編集する |
| `Editor/Gizmos/PointListGizmo.h`・`.cpp`（全体を差し替え） | 点の写し方（カメラ・画面）を差し替えられるようにした。Polygon Shape・Arrow Strip と共通 |
| `Math/Geometry/Polygon.cpp` | 耳切り法を速くした（Polygon Shape も同じ関数を使う） |
| `Systems/Rendering2D/SpriteRenderSystem` | 横・縦の Scale を出す関数を公開した（Vector Shape の中にあった物を移した） |
| ほか | 描く種類・頂点・定数・リクエスト・登録・Hierarchy の Create・UI ギズモ |

### 0.3 使い方

- Hierarchy の ＋ → **Polyline**（または Add Component → UI → Polyline）。最初は 4 点の波の線。
- 点は Inspector の **Point 0, 1, …**（Entity の位置を原点にしたピクセル。Y は上が＋）と **Radius**（その点の角の半径）。開いた線の両端の Radius は使わない（灰色）。
- **Edit Points** を押すと、Scene ビューで ■ をドラッグ・辺の真ん中の ＋ で点を足す・点を選んで Delete で消す・Esc で終わる。1 ピクセルに吸い付く（Ctrl を押している間は吸い付かない）。1回の操作が1件の履歴（Ctrl+Z）。
- **Closed** で閉じた形。**Style** が Fill・Fill And Stroke なら中を塗る（閉じた形だけ）。
- **Stroke Width**・**Stroke Color**・**Cap**（開いた線の端の形）。
- Timeline：Value トラックの Component を `Polyline`、Property を `point3` などにして、y（や x）にキーを打つ。半径は `radius3`、太さは `strokeWidth`、色は `strokeColor`。
- Script：`GetComponent<PolylineComponent>()->points[2].y = std::sin(time) * 40.0f;`（作り直すかは System が決める）

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| Component | Vector Shape に足さず、新しい Polyline | 点（32 個）と半径の配列を持つので、図形の項目と混ぜると Inspector も保存も大きくなる（[VectorShape.md](VectorShape.md) の 0.4 の予定どおり） |
| 点の持ち方 | 決まった大きさの配列（32）と、使う数（Polygon Shape と同じ） | Component は memcpy で写す（Undo・Prefab）ので、vector は持てない |
| 半径の持ち方 | 点とは別の配列 `radii` | 点の編集の部品（PointListGizmo）が Vector2 の配列のまま使える。点を足す・消すと、半径も一緒にずらす |
| 描き方 | 部分ごとにメッシュで囲み、PS が距離から縁をなめらかにする（資料と同じ分担） | 角の頂点の数は半径によらない（45 度ごと）。大きな半径でもカクカクしない |
| 角 | 点で、半径 r の円が2本の辺に接するように曲げる。辺に収まらなければ r を縮める | 半径を大きくしすぎても形が壊れない |
| 半径 0 の角 | 外は太さの半分の丸、内はとがる | SVG の round join と同じ。太さを変えても崩れない |
| 太い線の内側 | 半径が太さの半分より小さい角は、内側の2本の線が交わる点で、直線の部分どうしを分ける | 内側の重なり（半透明だと濃くなる所）を作らない（1.3） |
| 端の形 | Butt・Round・Square（開いた線だけ） | SVG の stroke-linecap と同じ3種類 |
| 塗り | 縁に 2 ピクセルの帯（距離でなめらか）、内側は耳切り法で三角形に分けた多角形 | 塗りの縁もなめらか。帯と内側は重ならない（1.5） |
| 塗りと線 | 1 回の描画（番号は塗りが先） | 線が塗りの上に重なる |
| 点の置き方 | Image と同じ（Entity の位置が原点のピクセル、Anchor から数える） | UI の置き方をそろえる |
| Scale | 点には x・y それぞれ、半径・太さには小さい方の倍率（Vector Shape と同じ） | 横だけ伸ばしても、角の丸みと太さがゆがまない |
| 作り直し | 点・半径・太さ・端の形・閉じる・塗る・Scale が変わった時だけ | 1.6 |
| メッシュの持ち方 | Entity の番号 → メッシュ の `unordered_map`。RenderQueue とは `shared_ptr` で共有 | 毎フレーム頂点を写さない |
| 重ねる順 | Order。同じ Order なら Image → Vector Shape → Polyline → Text（EngineSystems の呼ぶ順） | 今の決まりに足しただけ |
| クリックで選ぶ | 点を囲む四角（線の太さの半分を含む） | Image・Vector Shape と同じく四角 |
| 頂点のギズモ | Polygon Shape の点の編集（PointListGizmo）を使い回し、写し方だけ差し替える | 操作・Undo を同じにする。同じ処理を2つ書かない |

### 0.5 やらなかったこと

| 項目 | 理由・どうするか |
|---|---|
| マイター（外側もとがった角） | 半径 0 は丸い角（round join）。要るなら Join の欄を足す |
| 線のグラデーション・破線 | 今回の頼みに無い。グラデーションは Vector Shape の式を PS に足せばできる |
| 線が自分と交わる所 | 重なった所は2回描かれる（半透明なら濃くなる）。ステンシルで1回にできるが、UIMask.md のマスクとステンシルを分け合うので、今はしない |
| 辺どうしが交差する塗り | 三角形に分けられないので塗らない（Scene ビューの辺が赤くなる） |
| 当たり（`IsMouseOver`） | UI State の当たりは Vector Shape と Image だけ。折れ線のボタンが要るときに足す |
| 半径のギズモ | Inspector の Radius で直す |

## 1章 解説

### 1.1 メッシュとシェーダーの分担

資料の折れ線は「① 頂点の位置とカーブの半径からメッシュを作る ② シェーダーで直線の部分とカーブの部分に違う処理をする」。ここも同じ分け方にした。

```
        ┌───────────────┐               ← 直線の部分（四角）
  点 0  │  ─────────────  │╲              PS：線からの横の距離
        └───────────────┘  ╲ 角の部分（扇）
                             ╲ PS：角の円の中心からの距離
                              │
                              │ ← 次の直線の部分
```

部分の境目は「接点（直線と円弧がつながる所）で線に垂直な線」。となりの部分と同じ頂点を使うので、すき間も重なりもできない。

頂点には、位置のほかに「その部分から見た位置（`local`）」と「部分の種類（`piece`）」を入れる。`local` は位置に対して直線で変わる値なので、三角形の中で補間しても正しい値になる。PS はピクセルごとに、種類に合った式で輪郭までの距離 `d`（外が＋）を出す。

| 部分 | `local` | 距離 `d` |
|---|---|---|
| 直線（Segment） | (進む向きの長さ, 線から左への距離) | `abs(local.y) - 太さの半分` |
| 角（Corner） | 角の円の中心から見た位置 | `abs(length(local) - 半径) - 太さの半分` |
| 端（Cap） | (端の点から外への長さ, 横の距離) | Butt：`max(abs(y) - w, x)` / Round：`length(max(x, 0), y) - w` / Square：`max(abs(y) - w, x - w)` |
| 塗りの内側（FillInside） | 使わない | いつも中（`-100000`） |
| 塗りの縁の直線（FillEdge） | 直線と同じ | `local.y × 向き`（向きは塗りの外が＋になるように） |
| 塗りの縁の角（FillCorner） | 角と同じ | `(length(local) - 半径) × 向き` |

`length(local)` は PS でピクセルごとに計算するので、メッシュが 45 度ごとのカクカクした多角形でも、縁はちゃんと円になる。なめらかさは Vector Shape と同じ `Coverage`（`saturate(0.5 - d / 1ピクセル分の d の変化)`）。

### 1.2 角の円

```
   前の点 ───────●接点 A
                   ╲ ← 半径 r の円弧
                    ●接点 B
       中心 C ●      │
                     │ 次の点
```

点 P で、入ってくる辺の向き `d0`、出ていく辺の向き `d1`、曲がる角度 φ（まっすぐなら 0、折り返すと 180 度）とすると、

- P から接点までの長さ `t = r × tan(φ ÷ 2)`
- `t` が辺に収まらなければ縮めて、`r = t ÷ tan(φ ÷ 2)`。使ってよい長さは、となりも角なら辺の半分（2つの角で分ける）、となりが開いた線の端なら端の分を残した全部
- 中心 `C = 接点 A + 曲がる側の法線 × r`

メッシュは、半径 `r + h`（h は太さの半分＋縁の 2 ピクセル）の円に外から接する多角形と、半径 `r - h` の円の上の点の間を、45 度ごとの四角で埋める。外から接する多角形の頂点は、中心から `(r + h) ÷ cos(45° ÷ 2)`（1.08 倍）の所。多角形の辺が円に接するので、円を全部覆う。

### 1.3 太い線の内側

半径が h より小さいと、内側の輪郭（半径 `r - h`）が中心 C を越えてしまう。このとき線の内側の輪郭は、2本の直線の内側の輪郭（辺から h 離れた線）が交わる点 K でとがる。

```
            外
      ──────────╮
                │  ← 角の部分は C のまわりの扇（外側だけ）
      ─────C    │
          ╱ ╲   │
   直線A ╱ K ╲  │ 直線B
```

K は角の二等分線の上で、P から `h ÷ cos(φ ÷ 2)` の所。直線の部分は K まで伸ばし、二等分線で左右に分ける。二等分線より直線 A の側は直線 A までの距離の方が近いので、それぞれの式で正しい距離になる。

**崩れない目安：**`h × tan(φ ÷ 2)` が、となりの短い方の辺の長さより短いこと。これを越えると K が辺の外へ出るので、K を辺の中に引き戻す（内側の角が少し欠けることがある）。

| 太さ | 曲がる角度 | 要る辺の長さ |
|---|---|---|
| 8 | 90 度 | 6 ピクセル |
| 24 | 90 度 | 14 ピクセル |
| 16 | 150 度（鋭いギザギザ） | 38 ピクセル |
| 48 | 150 度 | 97 ピクセル |

線の太さが辺より太いときに、線どうしが重なる形（J の字に曲がる太い線など）は、どの作り方でも重なる。半透明なら重なった所が濃くなる（0.5 の表）。

### 1.4 端の形

開いた線の両端は、端の点から中へ 2 ピクセル、外へ「太さの半分＋2」（Butt は 2）の四角。中へも入れるのは、端でまっすぐ切る Butt の縁のなめらかさを、端の線の内と外に半分ずつ付けるため。直線の部分は端から 2 ピクセル中の所で終わる。

### 1.5 塗り

閉じた形で Style が Fill・Fill And Stroke のとき、塗りを作る。

1. **縁の帯：**輪郭の内外に 2 ピクセルずつの帯を、線と同じ作り方（直線の部分・角の部分）で作る。PS は符号付きの距離（外が＋）でなめらかにする。
2. **内側：**帯の内側の端の点をつないだ輪を、耳切り法で三角形に分けて塗る。

内側の輪は、内へ曲がる角（とがった角）では、帯の内側の円弧の両端の2点だけを使う（帯の角の部分は、その2点を結ぶ弦から外の多角形までを扇で囲む）。点が少ない方が耳切り法が速い。外へ曲がる角（へこんだ角）は、帯の外側の点（円に外から接する多角形）を全部使う。ただし両端の2点は、1.08 倍の所ではなく、ちょうど `r + 2` の所に置く。写しで確かめたとき、ギザギザの星形で、この2点がとなりの角の点を越えて輪が交差し、塗りが消えた（F 章）。

**耳切り法：**多角形の「耳」（となりの2点と結んだ三角形がとがっていて、中にほかの点が無い角）を1つずつ切り取ると、残りもまた多角形なので、最後に三角形が1つ残るまで続けられる。今回、点を毎フレーム動かす形にも使うので、次のように速くした（Polygon Shape も同じ関数を使う）。

- 切り取った点は、前と次の番号をつなぎ替えて飛ばす（配列から消して詰めない）。
- 耳の中に入りうるのは、へこんだ角の点だけ。とがった角の点が耳の中に入るなら、辺どうしが交差している。なので、へこんだ点だけを調べる（へこんでいるかの印を持ち、切り取ったらとなりの2点だけ調べ直す）。
- 切り取ったら、最初からではなく、切った所の手前から調べ直す。

写しで、2万通りのランダムな多角形（へこみ・一直線の点・時計回りを含む）で前の関数と比べ、三角形に分けられたかが全部同じで、三角形の面積の合計が多角形の面積と一致した。80 点の輪で Release 7.8 → 5.5 マイクロ秒、Debug 187 → 122 マイクロ秒。

### 1.6 作り直しを抑える工夫

1. **メッシュに入れない物：**メッシュは「Scale を掛けた、Entity から見たピクセル」で作る。回転・移動・アンカーは行列（定数）、色・透明度（Alpha Group も）は定数で渡すので、動かしても、色を変えても、フェードしても作り直さない。
2. **前と比べる：**作る元の値（点・半径・数・閉じる・塗る・太さ・端の形）を `PolylineShape` にまとめて覚え、`==` で比べる（`= default` でコンパイラに作らせる）。使っていない点（`pointCount` より後ろ）は入れないので、そこが変わっても作り直さない。
3. **頂点の数が半径によらない：**角は 45 度ごとに分けるだけ。縁の形は PS の距離で決まる。
4. **配列を使い回す：**作り直すときは `clear` してから足す（`clear` はメモリを返さない）。足すときは `resize` で1回広げてから書く（`push_back` を1つずつ呼ぶより Debug で約3倍速かった）。
5. **写さない：**RenderQueue へは `shared_ptr` を渡す。描き終わるまでは RenderQueue も同じメッシュを指す。作り直すときにまだ指されていたら（`use_count` が 2 以上）、新しく作る。
6. **耳切り法を速くした**（1.5）。

測った結果は F 章。

### 1.7 頂点のギズモ

点の編集（PointListGizmo）は、Polygon Shape・Arrow Strip のために 3D のカメラで点を写していた。写し方を `PointMapping`（点 → 絵の上のピクセル、絵の上のピクセル → 点 の2つの関数）にして、外から渡す形にした。

- `MakeCameraMapping`：今までと同じ（Entity の面の上の点を、カメラで写す。マウスはレイと面の交わる所）。
- `MakeScreenMapping`：UI。点 → 画面は `SpriteRenderSystem::CalcScreenPoint`（Image と同じ置き方）。逆は、点 (1, 0)・(0, 1) が画面のどこへ動くか（軸）を求めて、2×2 の連立方程式を解く（クラメルの公式。外積 `Cros` で書ける）。

あわせて、点と一緒にずらす値（`values`。折れ線の半径）と、吸い付く間隔（`gridSize`。3D は 0.25m、UI は 1 ピクセル）を `PointList` に足した。点を足すと、半径はとなりの2つの真ん中の値になる。

### 1.8 あまり使われない C++・HLSL

| 書き方 | 意味 |
|---|---|
| `nointerpolation float32_t2 param : TEXCOORD1;`（HLSL） | 頂点の値を三角形の中で補間しない（三角形の中は全部同じ値）。部分の種類や半径のように、混ぜると意味が無くなる値に付ける |
| `bool operator==(const PolylineShape&) const = default;` | メンバーを上から順に `==` で比べる関数を、コンパイラに作らせる（C++20） |
| `caches.try_emplace(id)` | その番号がまだ無いときだけ、空の値を作って入れる。あれば何もしない。戻り値は（場所, 足したか） |
| `std::erase_if(caches, 条件)` | 条件に合う物を全部消す。`unordered_map` にも使える（C++20） |
| `mesh.use_count()` | その `shared_ptr` の中身を、いくつの `shared_ptr` が指しているか |
| `std::function<bool(const Vector2&, ImVec2&)>` | 関数・ラムダを「呼べる物」として1つの型で持てる。写し方ごとに違うラムダを入れて、同じ編集の処理から呼ぶ |
| `vertices.resize(base + count); vertices.data() + base` | 先に大きさを広げてから、ポインタでまとめて書く |

---

## パートA 描画の土台

折れ線を描く通り道を作る（まだ呼ぶ所は無い）。

### A-1 `Graphics/Pipeline/RenderStates.h`

`DrawCategory` に `Polyline` を足す。

```cpp
enum class DrawCategory {
	Sprite, // Spriteは深度情報の関係で必ず最後に描画すること
	Model,
	Particle,
	Line,
	Text,
	VectorShape, // 画像を使わないUIの図形
	Polyline,    // UIの折れ線（メッシュで形を作り、縁はシェーダーでなめらかにする）
};
```

### A-2 `Graphics/Pipeline/PSOManager.cpp`

`GetTargetFormat` の 2D の所に足す（2D はトーンマップの後の画面に描く）。

```cpp
	case DrawCategory::Sprite:
	case DrawCategory::Text:
	case DrawCategory::VectorShape:
	case DrawCategory::Polyline:
		return RenderTextureFormat::SDR;
```

### A-3 `Graphics/Pipeline/VertexFormat.h`

`VertexLineData` の下に足す。入力の形（InputLayout）はシェーダーから作るので、ほかに書く所は無い。

```cpp
// UIの折れ線の頂点（Polyline.VS が読む。部分の種類ごとに、PS が距離の測り方を変える）
struct VertexPolylineData {
	Vector2 position; // 位置（折れ線の Entity から見たピクセル。Scale は掛けてある。VS が画面へ写す）
	Vector2 local;    // その部分から見た位置（直線・端：x=進む向き y=左へ / 角：円の中心から）
	Vector2 param;    // 部分ごとの値（角：x=半径 y=内外の向き / 端：x=端の形 / 塗りの縁：y=内外の向き）
	uint32_t piece;   // 部分の種類（PolylinePiece の番号）
};
static_assert(sizeof(VertexPolylineData) == 28, "Polyline.VS の VertexShaderInput と大きさが違います");
```

### A-4 `Graphics/Pipeline/ShaderConstants.h`

`VectorShapeMaterialData` の `static_assert` の下に足す。

```cpp
// ===== UIの折れ線のマテリアル（VS と PS の両方が読む。長さは全部描かれるピクセル）=====
struct PolylineMaterialData {
	Matrix4x4 toScreen = MakeIdentity4x4();         // 頂点の位置（Entity から見たピクセル）→ 画面の正規化座標（回転・移動・アンカー）
	Vector4 fillColor = {1.0f, 1.0f, 1.0f, 1.0f};   // 塗りの色
	Vector4 strokeColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 線の色
	float halfWidth = 0.0f;                         // 線の太さの半分
	uint32_t blendMode = 0;                         // 重ね方（BlendMode の順）
	float padA[2] = {};
};
static_assert(sizeof(PolylineMaterialData) == 112, "HLSLのMaterial（Polyline.hlsli）と大きさが違います");
```

### A-5 `Shader/Data/VectorShape/VectorCoverage.hlsli.shader`（新しいファイル）

`VectorShape.PS` の中にあった `Coverage` と、重ね方の直し（手順 4）を、折れ線と共通にするために出した。

```hlsl
#META
stage: Include
path:  VectorShape/VectorCoverage.hlsli
#META_END

#HLSL
// Vector Shape と Polyline が共通で使う、縁のなめらかさと重ね方の直し

// C++ の BlendMode と同じ順の番号
static const uint kBlendNone = 0;
static const uint kBlendMultiply = 4;
static const uint kBlendScreen = 5;

// ===== 輪郭のなめらかさ =====
// 距離 d から「このピクセルのうち、図形に入っている割合」を出す（0〜1）
// ddx・ddy：隣のピクセルとの d の差。どんな大きさ・回転で描いても、1ピクセル分の d の変化が分かるので、ちょうど1ピクセルの幅でなめらかになる
float32_t Coverage(float32_t d)
{
    float32_t pixelWidth = max(length(float32_t2(ddx(d), ddy(d))), 1e-5f);
    return saturate(0.5f - d / pixelWidth);
}

// ===== 重ね方に合わせる =====
// 板・メッシュは図形より広いので、図形の外（alpha が 0）が背景を変えないようにする
void ApplyBlendFix(uint blendMode, inout float32_t3 rgb, float32_t alpha)
{
    switch (blendMode)
    {
        case kBlendNone:
            clip(alpha - 0.5f); // 混ぜないので、半分より外を捨てる
            break;
        case kBlendMultiply:
            rgb = lerp(float32_t3(1.0f, 1.0f, 1.0f), rgb, alpha); // 乗算は alpha を見ないので、外を白（掛けても変わらない色）にする
            break;
        case kBlendScreen:
            rgb *= alpha; // スクリーンも alpha を見ないので、外を黒（足しても変わらない色）にする
            break;
    }
}
#HLSL_END
```

### A-6 `Shader/Data/VectorShape/VectorShape.PS.shader`

1. include に足す。

```hlsl
#include "Sprite/Sprite2d.hlsli"
#include "VectorShape/VectorCoverage.hlsli"
```

2. 番号の `kBlendNone`・`kBlendMultiply`・`kBlendScreen` の3行を消す（VectorCoverage.hlsli にある）。
3. `// ===== 輪郭のなめらかさ =====` から `Coverage` の関数の終わりまでを消す（VectorCoverage.hlsli にある）。
4. `main` の手順 4 の `switch` を、関数の呼び出しに差し替える。

```hlsl
    // ===== 4. 重ね方に合わせる（板は図形より広いので、図形の外が背景を変えないように）=====
    ApplyBlendFix(gMaterial.blendMode, rgb, alpha);
```

### A-7 `Shader/Data/VectorShape/Polyline.hlsli.shader`（新しいファイル）

```hlsl
#META
stage: Include
path:  VectorShape/Polyline.hlsli
#META_END

#HLSL
// 折れ線の VS と PS が共通で使う物

// 折れ線のマテリアル（C++ の PolylineMaterialData と同じ並び。VS は toScreen、PS は残りを読む）
struct Material
{
    float32_t4x4 toScreen; // 頂点の位置（Entity から見たピクセル）→ 画面の正規化座標
    float32_t4 fillColor; // 塗りの色
    float32_t4 strokeColor; // 線の色
    float32_t halfWidth; // 線の太さの半分
    uint blendMode; // 重ね方（BlendMode の順）
    float32_t2 padA;
};
ConstantBuffer<Material> gMaterial : register(b20);

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 local : TEXCOORD0; // その部分から見た位置（三角形の中で直線に補間される）
    nointerpolation float32_t2 param : TEXCOORD1; // 部分ごとの値（三角形の中で同じ値）
    nointerpolation uint piece : TEXCOORD2; // 部分の種類
};
#HLSL_END
```

### A-8 `Shader/Data/VectorShape/Polyline.VS.shader`（新しいファイル）

```hlsl
#META
stage:   VS
path:    VectorShape/Polyline.VS.hlsl
profile: vs_6_0
#META_END

#HLSL
#include "VectorShape/Polyline.hlsli"

// C++ の VertexPolylineData と同じ並び
struct VertexShaderInput
{
    float32_t2 position : POSITION0;
    float32_t2 local : TEXCOORD0;
    float32_t2 param : TEXCOORD1;
    uint piece : TEXCOORD2;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    // メッシュは Entity から見たピクセルで作ってあるので、回転・移動・アンカーはここで掛ける（動かしてもメッシュを作り直さない）
    output.position = mul(float32_t4(input.position, 0.0f, 1.0f), gMaterial.toScreen);
    output.local = input.local;
    output.param = input.param;
    output.piece = input.piece;
    return output;
}
#HLSL_END
```

### A-9 `Shader/Data/VectorShape/Polyline.PS.shader`（新しいファイル）

```hlsl
#META
stage:   PS
path:    VectorShape/Polyline.PS.hlsl
profile: ps_6_0
#META_END

#PROGRAM
category: Polyline
shading:  Unlit
vs:       PolylineVS
#PROGRAM_END

#HLSL
#include "VectorShape/Polyline.hlsli"
#include "VectorShape/VectorCoverage.hlsli"

// C++ の PolylinePiece と同じ順の番号
static const uint kPieceSegment = 0;
static const uint kPieceCorner = 1;
static const uint kPieceCap = 2;
static const uint kPieceFillInside = 3;
static const uint kPieceFillEdge = 4;
static const uint kPieceFillCorner = 5;
// C++ の PolylineCap と同じ順の番号
static const uint kCapButt = 0;
static const uint kCapRound = 1;

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

// ===== 端の距離 =====
// local：x は端から外へ（線の中はマイナス）、y は線の真ん中から横へ
float32_t CapDistance(float32_t2 local, uint cap)
{
    float32_t side = abs(local.y) - gMaterial.halfWidth;
    switch (cap)
    {
        case kCapButt:
            return max(side, local.x); // 端でまっすぐ切る
        case kCapRound:
            return length(float32_t2(max(local.x, 0.0f), local.y)) - gMaterial.halfWidth; // 端の点からの距離（半円）
        default:
            return max(side, local.x - gMaterial.halfWidth); // 太さの半分だけ伸ばして切る（四角）
    }
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // ===== 1. 部分ごとに、輪郭までの距離を出す（外がプラス）=====
    // 直線の部分は線からの距離、角の部分は円からの距離。メッシュが部分を分けてあるので、ピクセルごとに1つ測るだけでよい
    float32_t d = 0.0f;
    bool isStroke = true;
    switch (input.piece)
    {
        case kPieceSegment:
            d = abs(input.local.y) - gMaterial.halfWidth;
            break;
        case kPieceCorner:
            d = abs(length(input.local) - input.param.x) - gMaterial.halfWidth;
            break;
        case kPieceCap:
            d = CapDistance(input.local, (uint) input.param.x);
            break;
        case kPieceFillInside:
            d = -1.0e5f; // 内側は全部塗る
            isStroke = false;
            break;
        case kPieceFillEdge:
            d = input.local.y * input.param.y; // param.y で、塗りの外が＋になる向きにそろえる
            isStroke = false;
            break;
        case kPieceFillCorner:
            d = (length(input.local) - input.param.x) * input.param.y;
            isStroke = false;
            break;
    }

    // ===== 2. 色と、輪郭のなめらかさ =====
    float32_t4 color = isStroke ? gMaterial.strokeColor : gMaterial.fillColor;
    float32_t alpha = color.a * Coverage(d);
    float32_t3 rgb = color.rgb;

    // ===== 3. 重ね方に合わせる（メッシュは輪郭より少し広いので、外が背景を変えないように）=====
    ApplyBlendFix(gMaterial.blendMode, rgb, alpha);
    output.color = float32_t4(rgb, alpha);
    return output;
}
#HLSL_END
```

### A-10 `Graphics/Renderer/DrawRequest.h`

`VectorShapeRequest` の下に足す。

```cpp
// ===== 2Dの折れ線 =====
// 折れ線のメッシュ（PolylineSystem が作って持つ。描き終わるまでは RenderQueue も同じ物を指す）
struct PolylineMeshData {
	std::vector<VertexPolylineData> vertices; // 頂点（位置は折れ線の Entity から見たピクセル）
	std::vector<uint32_t> indices;            // 三角形の頂点の番号（3つで1つ）
};

struct PolylineRequest : DrawTarget {
	// 重ねる順（SpriteRequestのorderと同じ）
	int order = 0;

	// --- ジオメトリ（毎フレーム写さずに、PolylineSystem のメッシュを共有する。indices のうち indexStart から indexCount 個を描く）---
	std::shared_ptr<const PolylineMeshData> mesh;
	uint32_t indexStart = 0;
	uint32_t indexCount = 0;

	// --- Shaderとのバインド情報 ---
	PolylineMaterialData materialData;

	// --- 描画設定 ---
	BlendMode blendMode = BlendMode::Normal;
};
```

### A-11 `Graphics/Renderer/RenderQueue.h`

2D の列に折れ線を足す。

```cpp
	// 2Dのリクエスト1つ（スプライト・文字・図形・折れ線のどれか）
	using Request2d = std::variant<SpriteRequest, TextRequest, VectorShapeRequest, PolylineRequest>;
```

```cpp
	static void Request(VectorShapeRequest&& req);
	static void Request(PolylineRequest&& req);
	static void Request(ParticleRequest&& req);

	// ---  発行（2Dはスプライト・文字・図形・折れ線を、order の小さい順に混ぜて描く）---
```

### A-12 `Graphics/Renderer/RenderQueue.cpp`

図形のリクエストの下に足す。

```cpp
// ===== 折れ線リクエスト（スプライトと同じ列に積む）=====
void RenderQueue::Request(PolylineRequest&& req) { instance_->requests2d_.emplace_back(std::move(req)); }
```

`Flush2d` の最後の図形の所に `continue` を足し、その下に折れ線を足す。

```cpp
		// --- 図形（両面を描く。回して裏返っても消えないように）---
		if (const VectorShapeRequest* shape = std::get_if<VectorShapeRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::VectorShape, shape->blendMode, RasterizerType::SolidNone);
			RenderContext::DrawVectorShape(*shape);
			continue;
		}
		// --- 折れ線（三角形の向きはそろえていないので、両面を描く）---
		if (const PolylineRequest* polyline = std::get_if<PolylineRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::Polyline, polyline->blendMode, RasterizerType::SolidNone);
			RenderContext::DrawPolyline(*polyline);
		}
	}
}
```

### A-13 `Graphics/Renderer/RenderContext.h`

`DrawVectorShape` の下に足す。

```cpp
	/// <summary>
	/// 2Dの折れ線を描く（PolylineSystem が作ったメッシュの、indexStart から indexCount 個の番号）
	/// </summary>
	static void DrawPolyline(const PolylineRequest& req);
```

### A-14 `Graphics/Renderer/RenderContext.cpp`

`DrawVectorShape` の下に足す。

```cpp
//=============================================================================
// 2Dの折れ線描画
//=============================================================================
void RenderContext::DrawPolyline(const PolylineRequest& req) {
	if (req.mesh == nullptr || req.indexCount == 0 || req.mesh->vertices.empty()) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;
	const PolylineMeshData& mesh = *req.mesh;

	// ===== このフレーム用のバッファに書く（頂点は全部、番号は描く範囲だけ）=====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	FrameUploadBuffer::Allocation vertices = upload.PushArray(mesh.vertices.data(), mesh.vertices.size());
	FrameUploadBuffer::Allocation indices = upload.PushArray(mesh.indices.data() + req.indexStart, req.indexCount);

	// ===== ジオメトリ =====
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertices.gpuAddress;
	vbv.SizeInBytes = static_cast<UINT>(vertices.size);
	vbv.StrideInBytes = sizeof(VertexPolylineData);
	D3D12_INDEX_BUFFER_VIEW ibv{};
	ibv.BufferLocation = indices.gpuAddress;
	ibv.SizeInBytes = static_cast<UINT>(indices.size);
	ibv.Format = DXGI_FORMAT_R32_UINT;
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== ShaderConstantsバインド（VS と PS の両方が同じ定数を読む）=====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::Polyline));
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(req.indexCount, 1, 0, 0, 0);
}
```

### A-15 `Graphics/Renderer/Renderer.h`

前方宣言に足す。

```cpp
// 前方宣言
struct Entity;
struct PolylineMeshData;
class RenderContext;
```

`VectorShapeConfig` の下に足す。

```cpp
	//=============================================================================
	// Polyline
	//=============================================================================
	// --- UI の折れ線（メッシュを作って持つのは PolylineSystem。ここでは共有して受け取るだけで、写さない）---
	struct PolylineConfig : DrawTarget {
		std::shared_ptr<const PolylineMeshData> mesh; // メッシュ（位置は折れ線の Entity から見たピクセル）
		uint32_t indexStart = 0;                      // 描く三角形の番号の始まり
		uint32_t indexCount = 0;                      // 描く三角形の番号の数
		PolylineMaterialData material;                // 色・太さ・画面へ写す行列（色は sRGB。中でリニアに直す）
		BlendMode blendMode = BlendMode::Normal;      // ブレンド設定
		int order = 0;                                // 重ねる順（スプライトの order と同じ）
	};
```

`DrawVectorShape` の宣言の下に足す。

```cpp
	/// <summary>
	/// UI の折れ線の描画リクエストを追加する
	/// </summary>
	static void DrawPolyline(const PolylineConfig& config);
```

### A-16 `Graphics/Renderer/Renderer.cpp`

`DrawVectorShape` の下に足す。

```cpp
// ===== Polyline =====
void Renderer::DrawPolyline(const PolylineConfig& config) {
	if (config.mesh == nullptr || config.indexCount == 0) {
		return;
	}
	PolylineRequest req;
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す
	req.order = config.order;
	req.blendMode = config.blendMode;
	req.mesh = config.mesh; // shared_ptr を写すだけ（頂点は写さない）
	req.indexStart = config.indexStart;
	req.indexCount = config.indexCount;
	req.materialData = config.material;
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（A はそのまま）
	req.materialData.fillColor = SrgbToLinear(config.material.fillColor);
	req.materialData.strokeColor = SrgbToLinear(config.material.strokeColor);
	RenderQueue::Request(std::move(req));
}
```

### A-17 プロジェクトに足す

ソリューション エクスプローラーで HTTEngine に、既存の項目として足す（`.shader` は、ほかの `.shader` と同じ場所）。

- `Shader/Data/VectorShape/VectorCoverage.hlsli.shader`・`Polyline.hlsli.shader`・`Polyline.VS.shader`・`Polyline.PS.shader`

---

## パートB Component と System

### B-1 `Components/UI/PolylineComponent.h`（新しいファイル）

```cpp
#pragma once
#include <array>
#include <algorithm>
#include <type_traits>

#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// 点をつないだ UI の線（閉じれば塗った形）。Entityに付けるComponent（カテゴリ：UI）
// 点は Entity の位置を原点にしたピクセル（Y は上が＋）。Transform の回転 z・大きさ・親子が掛かり、Anchor の所から数える（Image と同じ）
// 点ごとに角の半径を持ち、角を円弧で曲げる（0 ならとがった角。線が太いと、外側は太さの半分の丸になる）
// 値は毎フレーム読むだけ。点・半径・太さが変わった時だけメッシュを作り直す（位置・回転・色・透明度を変えても作り直さない）
//   例：GetComponent<PolylineComponent>()->points[2].y = std::sin(time) * 40.0f;

// 点の数の上限（Component は memcpy で写すので、決まった大きさの配列で持つ）
constexpr int kMaxPolylinePoints = 32;
// 点の数の下限（線は 2 点から）
constexpr int kMinPolylinePoints = 2;

/// <summary>
/// 開いた線の端の形
/// </summary>
enum class PolylineCap {
	Butt,   // 端の点でまっすぐ切る
	Round,  // 端の点を中心に、太さの半分の半円
	Square, // 端の点から、太さの半分だけ伸ばして切る
};

/// <summary>
/// 点をつないだ UI の線・形
/// </summary>
struct PolylineComponent {
	bool enabled = true;                        // false の間は描かない
	ScreenAnchor anchor = ScreenAnchor::Center; // 画面のどこから位置を数えるか（Image と同じ）
	int order = 0;                              // 重ねる順（大きいほど手前。Image・Vector Shape・Text の Order と比べる）
	BlendMode blend = BlendMode::Normal;        // 重ね方（Normal・Add など。Image と同じ）
	// --- 形 ---
	bool closed = false; // 最後の点から最初の点へもつなぐ（閉じた形。塗れるのは閉じた形だけ）
	int pointCount = 4;  // 使っている点の数（kMinPolylinePoints〜kMaxPolylinePoints）
	std::array<Vector2, kMaxPolylinePoints> points = {
	    {{-150.0f, 0.0f}, {-50.0f, 60.0f}, {50.0f, -60.0f}, {150.0f, 0.0f}}
    };                                                                         // 点（ピクセル。使うのは先頭から pointCount 個）
	std::array<float, kMaxPolylinePoints> radii = {0.0f, 40.0f, 40.0f, 0.0f}; // 点ごとの角の半径（ピクセル。開いた線の両端の点は使わない）
	// --- 塗り・線 ---
	VectorShapeStyle style = VectorShapeStyle::Stroke; // 塗り方（Fill：塗りだけ / Fill And Stroke：塗りと線 / Stroke：線だけ）
	Vector4 color = {0.3f, 0.6f, 1.0f, 1.0f};          // 塗りの色（閉じた形だけ）
	float strokeWidth = 8.0f;                          // 線の太さ（ピクセル）
	Vector4 strokeColor = {1.0f, 1.0f, 1.0f, 1.0f};    // 線の色
	PolylineCap cap = PolylineCap::Round;              // 開いた線の端の形

	// 使っている点の数（pointCount が範囲の外でも、配列の外は読まない）
	int GetCount() const { return std::clamp(pointCount, 0, kMaxPolylinePoints); }
	// 塗るか（閉じた形で、Style が Fill・Fill And Stroke）
	bool HasFill() const { return closed && style != VectorShapeStyle::Stroke; }
	// 線を描くか（Style が Stroke・Fill And Stroke で、太さがある）
	bool HasStroke() const { return style != VectorShapeStyle::Fill && strokeWidth > 0.0f; }
};

static_assert(std::is_trivially_copyable_v<PolylineComponent>);
```

### B-2 `Systems/UI/PolylineMesh.h`（新しいファイル）

```cpp
#pragma once
#include <array>
#include <cstdint>

#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Math/Vector2.h"

// 前方宣言
struct PolylineMeshData;

/// <summary>
/// 折れ線のメッシュの部分の種類（Polyline.PS の kPiece〜 と同じ番号。PS は種類ごとに距離の測り方を変える）
/// </summary>
enum class PolylinePiece : uint32_t {
	Segment,    // 線の直線の部分（線からの距離）
	Corner,     // 線の角の部分（角の円からの距離）
	Cap,        // 線の端（端の形の距離）
	FillInside, // 塗りの内側（いつも全部塗る）
	FillEdge,   // 塗りの縁の直線の部分（縁からの符号付きの距離）
	FillCorner, // 塗りの縁の角の部分（角の円からの符号付きの距離）
};

/// <summary>
/// メッシュを作る元の値。PolylineSystem は、これが前のフレームと同じならメッシュを作り直さない
/// <para>位置・回転・色・透明度は入れない（VS と定数で掛けるので、変わってもメッシュは同じ）</para>
/// </summary>
struct PolylineShape {
	std::array<Vector2, kMaxPolylinePoints> points{}; // 点（Scale を掛けたピクセル。使わない所は 0 のまま）
	std::array<float, kMaxPolylinePoints> radii{};    // 角の半径（Scale を掛けたピクセル）
	int count = 0;                                    // 使っている点の数
	bool isClosed = false;                            // 閉じた形
	bool hasFill = false;                             // 塗りの部分を作る（閉じた形だけ）
	float halfWidth = 0.0f;                           // 線の太さの半分（0 なら線の部分を作らない）
	PolylineCap cap = PolylineCap::Round;             // 開いた線の端の形

	// 全部の値を比べる（= default：メンバーを順に == で比べる関数を、コンパイラに作らせる）
	bool operator==(const PolylineShape&) const = default;
};

/// <summary>
/// 作ったメッシュの、番号の並び（塗りの部分が先、線の部分が後。1回の描画で全部描けば、線が塗りの上に重なる）
/// </summary>
struct PolylineMeshInfo {
	uint32_t fillIndexCount = 0;   // 番号の先頭から、塗りの部分の数
	uint32_t strokeIndexCount = 0; // その後ろの、線の部分の数
	bool isFillValid = true;       // 塗りを三角形に分けられたか（辺どうしが交差していると false。塗りは作らない）
};

/// <summary>
/// 折れ線のメッシュを作る（直線の部分・角の部分・端に分けて、それぞれを囲む三角形を作る。縁の形は Polyline.PS が距離で決める）
/// </summary>
namespace PolylineMeshBuilder {

/// <summary>
/// shape からメッシュを作る。mesh の配列は clear してから足すので、前の中身の大きさ（capacity）を使い回す
/// </summary>
PolylineMeshInfo Build(const PolylineShape& shape, PolylineMeshData& mesh);
} // namespace PolylineMeshBuilder
```

### B-3 `Systems/UI/PolylineMesh.cpp`（新しいファイル）

1.2〜1.5 の作り方。上から、点の並びを整える → 角（円の中心・半径）→ 角を幅 h で組み立てる → 直線の部分の端の境目 → メッシュに足す係 → 線 → 塗り。

```cpp
#include "PolylineMesh.h"

#include <span>
#include <cmath>
#include <vector>
#include <algorithm>

#include "HTTEngine/Graphics/Renderer/DrawRequest.h"
#include "HTTEngine/Math/Geometry/Polygon.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kEdgeMargin = 2.0f;                      // 輪郭の外へ広げる幅（ピクセル。なめらかな縁がメッシュの外で切れないように。Vector Shape と同じ）
constexpr float kMinLength = 1.0e-3f;                    // これより短い辺は無いことにする（向きが決まらない）
constexpr float kMinTurn = 1.0e-3f;                      // これより小さく曲がる角は、まっすぐとして扱う（ラジアン）
constexpr float kMaxTurn = MathUtility::kPi - 2.0e-4f;   // 曲がる角度の上限（ちょうど折り返すと tan が無限になるので、少し手前で止める）
constexpr float kArcStep = MathUtility::kPi * 0.25f;     // 角の扇を、この角度（45 度）ごとに分ける
constexpr uint32_t kMaxArcSteps = 4;                     // 扇を分ける数の上限（180 度 ÷ 45 度）
constexpr float kMinLoopSpacing = 1.0e-4f;               // 塗りの内側の点が、これより近ければ1つにまとめる（三角形に分けるときに、重なった点で失敗しないように）

/// <summary>
/// 続けて同じ所にある点を除いた、点の並び
/// </summary>
struct CleanPath {
	std::array<Vector2, kMaxPolylinePoints> points{}; // 点
	std::array<float, kMaxPolylinePoints> radii{};    // 角の半径
	int count = 0;                                    // 点の数
	bool isClosed = false;                            // 閉じた形（点が 3 つより少なければ、開いた線として扱う）
};

/// <summary>
/// 1つの角の形（線の太さによらない。角の円の中心・半径と、曲がる向き）
/// </summary>
struct Corner {
	Vector2 point;           // 元の点
	Vector2 incoming;        // 入ってくる辺の向き（長さ 1）
	Vector2 center;          // 角の円の中心（半径 0 なら元の点）
	float radius = 0.0f;     // 角の半径（辺に収まるように縮めた後）
	float turn = 1.0f;       // 曲がる向き（+1：左 / -1：右）
	float angle = 0.0f;      // 曲がる角度（ラジアン。0〜π）
	Vector2 inNormal;        // 入ってくる辺の、曲がる側（内側）の法線
	Vector2 outNormal;       // 出ていく辺の、曲がる側（内側）の法線
	float joinLimit = 0.0f;  // 内側の線どうしが交わる点までの距離の上限（その点が辺の外へ出ないように）
};

/// <summary>
/// 1つの角を、幅 h（輪郭から外へ広げる量）で組み立てた点
/// </summary>
struct CornerRing {
	std::array<Vector2, kMaxArcSteps + 1> outer{}; // 外側の点（円に外から接する多角形の頂点。steps + 1 個）
	std::array<Vector2, kMaxArcSteps + 1> inner{}; // 内側の点（円の中心の側。isFolded でないときだけ使う。steps + 1 個）
	uint32_t steps = 0;                            // 扇を分けた数（まっすぐなら 0）
	bool isFolded = false;                         // 幅が半径より大きく、内側の輪郭が円の中心を越える
	Vector2 join;                                  // isFolded のとき、内側の2本の線が交わる点
};

/// <summary>
/// 直線の部分の端の境目（左から右へ並べた 2〜3 個の点）
/// </summary>
struct EdgeEnd {
	std::array<Vector2, 3> points{};
	uint32_t count = 0;
	void Add(const Vector2& point) { points[count++] = point; }
};

//=============================================================================
// 小さな計算
//=============================================================================
// ===== 2D の向きを左へ 90 度回す =====
Vector2 RotateLeft(const Vector2& v) { return {-v.y, v.x}; }

//=============================================================================
// 点の並びを整える
//=============================================================================
CleanPath MakeCleanPath(const PolylineShape& shape) {
	CleanPath path;
	const int count = std::clamp(shape.count, 0, kMaxPolylinePoints);
	for (int i = 0; i < count; ++i) {
		// 前の点と同じ所なら飛ばす（辺の向きが決まらない）
		if (path.count > 0 && Length(shape.points[i] - path.points[path.count - 1]) <= kMinLength) {
			continue;
		}
		path.points[path.count] = shape.points[i];
		path.radii[path.count] = shape.radii[i];
		++path.count;
	}
	path.isClosed = shape.isClosed;
	// 閉じた形で、最後の点が最初の点と同じなら、最後の点を除く
	if (path.isClosed && path.count >= 2 && Length(path.points[0] - path.points[path.count - 1]) <= kMinLength) {
		--path.count;
	}
	if (path.count < 3) {
		path.isClosed = false; // 2点を閉じても形にならない
	}
	return path;
}

//=============================================================================
// 角
//=============================================================================
// ===== 点 point の角を作る（prev・next はとなりの点。availableIn・availableOut は、角が使ってよい辺の長さ）=====
//
//        prev ──────────●tangentIn
//                         ╲  ← 半径 radius の円弧（中心 center は曲がる側）
//                          ●tangentOut
//                          │
//                         next
//   元の点から tangentIn・tangentOut までの長さは「半径 × tan(曲がる角度 ÷ 2)」。辺に収まらなければ半径を縮める
//
Corner MakeCorner(const Vector2& prev, const Vector2& point, const Vector2& next, float radius, float availableIn, float availableOut) {
	Corner corner;
	corner.point = point;
	corner.incoming = Normalize(point - prev);
	const Vector2 outgoing = Normalize(next - point);
	const float cross = Cros(corner.incoming, outgoing);
	corner.turn = (cross >= 0.0f) ? 1.0f : -1.0f;
	corner.angle = (std::min)(std::atan2(std::abs(cross), Dot(corner.incoming, outgoing)), kMaxTurn);
	corner.inNormal = RotateLeft(corner.incoming) * corner.turn;
	corner.outNormal = RotateLeft(outgoing) * corner.turn;
	const float halfAngle = corner.angle * 0.5f;
	// 内側の線どうしが交わる点を、短い方の辺の長さまでに止める（その点の、辺の上での位置が「距離 × sin(角度 ÷ 2)」）
	corner.joinLimit = (std::min)(Length(point - prev), Length(next - point)) / (std::max)(std::sin(halfAngle), 1.0e-6f);
	corner.center = point;
	if (corner.angle < kMinTurn) {
		return corner; // まっすぐ：曲げない
	}
	const float tanHalf = std::tan(halfAngle);
	const float tangent = (std::min)({(std::max)(radius, 0.0f) * tanHalf, availableIn, availableOut});
	corner.radius = tangent / tanHalf;
	corner.center = point - corner.incoming * tangent + corner.inNormal * corner.radius;
	return corner;
}

// ===== 角を、幅 h で組み立てる（h は輪郭から外へ広げる量。線なら太さの半分＋縁、塗りの縁なら縁の分だけ）=====
// 外側の点は、半径 (r + h) の円に外から接する多角形の頂点（多角形の辺が円に接するので、円を全部覆う。45 度ごとなら 1.08 倍の所）
// 内側の点は半径 (r - h)。r < h のときは内側の輪郭が中心を越える（isFolded）ので、中心と「内側の2本の線が交わる点」を使う
// isOuterEndExact：外側の両端の点を、ちょうど半径 (r + h) の所に置く（塗りの内側の輪に使う側。1.08 倍の所だと、
// 反対に曲がるとなりの角の点を越えて、輪が交差することがある。内側は塗りの内側が全部塗るので、帯が少し欠けてもよい）
CornerRing MakeRing(const Corner& corner, float h, bool isOuterEndExact = false) {
	CornerRing ring;
	ring.steps = (corner.angle < kMinTurn) ? 0u : (std::min)(static_cast<uint32_t>(std::ceil(corner.angle / kArcStep)), kMaxArcSteps);
	const float step = (ring.steps > 0) ? corner.angle / static_cast<float>(ring.steps) : 0.0f;
	const float outerRadius = (corner.radius + h) / std::cos(step * 0.5f);
	// 円の中心から見た向きを、入ってくる辺に接する点の向きから step ずつ回す（cos・sin は1回だけ求めて、回す計算を繰り返す）
	const float stepCos = std::cos(corner.turn * step);
	const float stepSin = std::sin(corner.turn * step);
	Vector2 direction = -corner.inNormal;
	ring.isFolded = corner.radius < h;
	for (uint32_t k = 0; k <= ring.steps; ++k) {
		const bool isEnd = (k == 0 || k == ring.steps);
		ring.outer[k] = corner.center + direction * ((isOuterEndExact && isEnd) ? corner.radius + h : outerRadius);
		ring.inner[k] = corner.center + direction * (corner.radius - h);
		direction = {direction.x * stepCos - direction.y * stepSin, direction.x * stepSin + direction.y * stepCos};
	}
	if (ring.isFolded) {
		// 角の二等分線の上で、元の点から h ÷ cos(角度 ÷ 2) の所（ちょうど折り返すときは、入ってきた辺を戻る向き）
		Vector2 bisector = Normalize(corner.inNormal + corner.outNormal);
		if (LengthSq(bisector) == 0.0f) {
			bisector = -corner.incoming;
		}
		const float distance = (std::min)(h / std::cos(corner.angle * 0.5f), corner.joinLimit);
		ring.join = corner.point + bisector * distance;
	}
	return ring;
}

// ===== 角と直線の部分の境目（入ってくる辺側なら点 0、出ていく辺側なら点 steps の所）を、左から右へ =====
// 曲がる側（内側）が左なら、内 → 外の順が左 → 右
EdgeEnd MakeCornerEnd(const Corner& corner, const CornerRing& ring, bool isIncoming) {
	const uint32_t k = isIncoming ? 0u : ring.steps;
	EdgeEnd end;
	std::array<Vector2, 3> innerToOuter{};
	uint32_t count = 0;
	if (ring.isFolded) {
		innerToOuter = {ring.join, corner.center, ring.outer[k]};
		count = 3;
	} else {
		innerToOuter = {ring.inner[k], ring.outer[k]};
		count = 2;
	}
	for (uint32_t i = 0; i < count; ++i) {
		end.Add(innerToOuter[(corner.turn > 0.0f) ? i : count - 1 - i]);
	}
	return end;
}

// ===== 開いた線の端と直線の部分の境目（端の点から inset だけ線の中へ入った所。左から右へ）=====
// direction は直線の部分の進む向き。inset は進む向きに＋（始まりの端は＋、終わりの端は−を渡す）
EdgeEnd MakeCapEnd(const Vector2& point, const Vector2& direction, float inset, float h) {
	const Vector2 base = point + direction * inset;
	const Vector2 side = RotateLeft(direction) * h;
	EdgeEnd end;
	end.Add(base + side);
	end.Add(base - side);
	return end;
}

//=============================================================================
// メッシュに足す
//=============================================================================
/// <summary>
/// 部分ごとに、頂点と三角形の番号を足す係
/// </summary>
class MeshWriter {
public:
	explicit MeshWriter(PolylineMeshData& mesh) : mesh_(mesh) {}

	// 凸の多角形を、最初の点からの扇で三角形に分けて足す。localOf は「位置 → その部分から見た位置」
	template<class LocalOf>
	void AddFan(std::span<const Vector2> polygon, PolylinePiece piece, const Vector2& param, const LocalOf& localOf) {
		const uint32_t count = static_cast<uint32_t>(polygon.size());
		if (count < 3) {
			return;
		}
		// 配列は1回で広げてから書く（1つずつ push_back するより Debug で速い。clear の後なら、広げてもメモリは取り直さない）
		const uint32_t base = static_cast<uint32_t>(mesh_.vertices.size());
		mesh_.vertices.resize(base + count);
		VertexPolylineData* vertex = mesh_.vertices.data() + base;
		for (uint32_t i = 0; i < count; ++i) {
			vertex[i] = {polygon[i], localOf(polygon[i]), param, static_cast<uint32_t>(piece)};
		}
		const size_t indexBase = mesh_.indices.size();
		mesh_.indices.resize(indexBase + (count - 2) * 3);
		uint32_t* index = mesh_.indices.data() + indexBase;
		for (uint32_t i = 1; i + 1 < count; ++i) {
			*index++ = base;
			*index++ = base + i;
			*index++ = base + i + 1;
		}
	}

	// 直線の部分：start（左→右）から end（右→左に戻る）へ回る多角形。local は (進む向き, 左へ) の距離
	void AddSegment(const EdgeEnd& start, const EdgeEnd& end, const Vector2& origin, const Vector2& direction, PolylinePiece piece, const Vector2& param) {
		std::array<Vector2, 6> polygon{};
		uint32_t count = 0;
		for (uint32_t i = 0; i < start.count; ++i) {
			polygon[count++] = start.points[i];
		}
		for (uint32_t i = end.count; i > 0; --i) {
			polygon[count++] = end.points[i - 1];
		}
		const Vector2 left = RotateLeft(direction);
		AddFan(std::span<const Vector2>(polygon.data(), count), piece, param, [&](const Vector2& p) { return Vector2{Dot(p - origin, direction), Dot(p - origin, left)}; });
	}

	// 角の部分：中心から見た位置を local に入れる（PS は円からの距離を測る）
	void AddCorner(const Corner& corner, const CornerRing& ring, PolylinePiece piece, const Vector2& param, bool usesChord) {
		const auto localOf = [&corner](const Vector2& p) { return p - corner.center; };
		if (ring.steps == 0) {
			return; // まっすぐ：角の部分は無い
		}
		// 折れている：中心からの扇（中心の近くも線の中）
		if (ring.isFolded) {
			std::array<Vector2, kMaxArcSteps + 2> fan{};
			fan[0] = corner.center;
			std::copy_n(ring.outer.begin(), ring.steps + 1, fan.begin() + 1);
			AddFan(std::span<const Vector2>(fan.data(), ring.steps + 2), piece, param, localOf);
			return;
		}
		// 内側を弦（点 0 と点 steps を結ぶ線）にする：塗りの縁で、内側を塗りの内側と1本の線で分けるとき
		if (usesChord) {
			std::array<Vector2, kMaxArcSteps + 3> polygon{};
			polygon[0] = ring.inner[0];
			std::copy_n(ring.outer.begin(), ring.steps + 1, polygon.begin() + 1);
			polygon[ring.steps + 2] = ring.inner[ring.steps];
			AddFan(std::span<const Vector2>(polygon.data(), ring.steps + 3), piece, param, localOf);
			return;
		}
		// 内側と外側の間を、45 度ごとの四角で埋める
		for (uint32_t k = 0; k < ring.steps; ++k) {
			const std::array<Vector2, 4> quad = {ring.inner[k], ring.outer[k], ring.outer[k + 1], ring.inner[k + 1]};
			AddFan(quad, piece, param, localOf);
		}
	}

	uint32_t GetIndexCount() const { return static_cast<uint32_t>(mesh_.indices.size()); }

private:
	PolylineMeshData& mesh_;
};

//=============================================================================
// 全部の角
//=============================================================================
/// <summary>
/// 折れ線の全部の角（開いた線の両端は、角ではない）
/// </summary>
struct PathCorners {
	std::array<Corner, kMaxPolylinePoints> corners{};
	float startInset = 0.0f; // 開いた線：始まりの端を、端の形の分だけ中へ入れる長さ
	float endInset = 0.0f;   // 開いた線：終わりの端を中へ入れる長さ
};

PathCorners MakeCorners(const CleanPath& path) {
	PathCorners result;
	const int n = path.count;
	const auto lengthOf = [&path, n](int from) { return Length(path.points[(from + 1) % n] - path.points[from]); };
	if (!path.isClosed) {
		// 端の形のなめらかな所（端の点の前後 kEdgeMargin）を、直線の部分と分ける。短い辺なら 4 分の 1 まで
		result.startInset = (std::min)(kEdgeMargin, lengthOf(0) * 0.25f);
		result.endInset = (std::min)(kEdgeMargin, lengthOf(n - 2) * 0.25f);
	}
	const int first = path.isClosed ? 0 : 1;
	const int last = path.isClosed ? n - 1 : n - 2;
	for (int i = first; i <= last; ++i) {
		const int prev = (i + n - 1) % n;
		const int next = (i + 1) % n;
		// 角が使ってよい長さ：となりも角なら辺の半分（2つの角で分ける）、となりが開いた線の端なら端の分を残した全部
		const float lengthIn = lengthOf(prev);
		const float lengthOut = lengthOf(i);
		const float availableIn = (!path.isClosed && prev == 0) ? lengthIn - result.startInset : lengthIn * 0.5f;
		const float availableOut = (!path.isClosed && next == n - 1) ? lengthOut - result.endInset : lengthOut * 0.5f;
		result.corners[i] = MakeCorner(path.points[prev], path.points[i], path.points[next], path.radii[i], availableIn, availableOut);
	}
	return result;
}

//=============================================================================
// 線
//=============================================================================
void BuildStroke(const CleanPath& path, const PathCorners& pathCorners, const PolylineShape& shape, MeshWriter& writer) {
	const int n = path.count;
	const float h = shape.halfWidth + kEdgeMargin;
	const auto& corners = pathCorners.corners;
	std::array<CornerRing, kMaxPolylinePoints> rings{};
	const int first = path.isClosed ? 0 : 1;
	const int last = path.isClosed ? n - 1 : n - 2;
	// ===== 角 =====
	for (int i = first; i <= last; ++i) {
		rings[i] = MakeRing(corners[i], h);
		writer.AddCorner(corners[i], rings[i], PolylinePiece::Corner, {corners[i].radius, 0.0f}, false);
	}
	// ===== 直線の部分（点 i → 点 i + 1）=====
	const int segmentCount = path.isClosed ? n : n - 1;
	for (int i = 0; i < segmentCount; ++i) {
		const int next = (i + 1) % n;
		const Vector2 direction = Normalize(path.points[next] - path.points[i]);
		const bool isFirst = !path.isClosed && i == 0;
		const bool isLast = !path.isClosed && next == n - 1;
		const EdgeEnd start = isFirst ? MakeCapEnd(path.points[i], direction, pathCorners.startInset, h) : MakeCornerEnd(corners[i], rings[i], false);
		const EdgeEnd end = isLast ? MakeCapEnd(path.points[next], direction, -pathCorners.endInset, h) : MakeCornerEnd(corners[next], rings[next], true);
		writer.AddSegment(start, end, path.points[i], direction, PolylinePiece::Segment, {0.0f, 0.0f});
	}
	if (path.isClosed) {
		return;
	}
	// ===== 開いた線の両端 =====
	// 端の点から中へ inset、外へ extent の四角。local は (端から外へ, 横へ)、param.x は端の形
	const float extent = (shape.cap == PolylineCap::Butt) ? kEdgeMargin : shape.halfWidth + kEdgeMargin;
	const Vector2 capParam = {static_cast<float>(shape.cap), 0.0f};
	const auto addCap = [&](const Vector2& point, const Vector2& outward, float inset) {
		const Vector2 side = RotateLeft(outward) * h;
		const Vector2 inside = point - outward * inset;
		const Vector2 outside = point + outward * extent;
		const std::array<Vector2, 4> quad = {inside + side, outside + side, outside - side, inside - side};
		writer.AddFan(quad, PolylinePiece::Cap, capParam, [&](const Vector2& p) { return Vector2{Dot(p - point, outward), Dot(p - point, RotateLeft(outward))}; });
	};
	addCap(path.points[0], Normalize(path.points[0] - path.points[1]), pathCorners.startInset);
	addCap(path.points[n - 1], Normalize(path.points[n - 1] - path.points[n - 2]), pathCorners.endInset);
}

//=============================================================================
// 塗り
//=============================================================================
// 縁（輪郭の内外に kEdgeMargin の帯）を距離でなめらかにし、帯の内側の輪を三角形に分けて塗る
// 失敗（辺どうしが交差して三角形に分けられない）なら false で、何も足さない
bool BuildFill(const CleanPath& path, const PathCorners& pathCorners, MeshWriter& writer, PolylineMeshData& mesh) {
	const int n = path.count;
	const float area = Polygon2D::SignedArea(std::span<const Vector2>(path.points.data(), static_cast<size_t>(n)));
	if (std::abs(area) <= kMinLength) {
		return false; // 面積が無い
	}
	const bool isInsideLeft = area > 0.0f; // 左回りなら、進む向きの左が内側
	const auto& corners = pathCorners.corners;
	std::array<CornerRing, kMaxPolylinePoints> rings{};

	// ===== 1. 帯の内側の輪（塗りの内側の多角形）=====
	// 内側へ曲がる角（とがった角）は円の中心の側の点、外側へ曲がる角（へこんだ角）は外の点を使う
	std::vector<Vector2> loop;
	loop.reserve(static_cast<size_t>(n) * (kMaxArcSteps + 1));
	const auto addLoopPoint = [&loop](const Vector2& point) {
		if (loop.empty() || Length(point - loop.back()) > kMinLoopSpacing) {
			loop.push_back(point);
		}
	};
	for (int i = 0; i < n; ++i) {
		const bool isConvex = (corners[i].turn > 0.0f) == isInsideLeft;
		rings[i] = MakeRing(corners[i], kEdgeMargin, !isConvex); // へこんだ角は、外側の点が塗りの内側の輪になる
		if (isConvex && rings[i].isFolded) {
			addLoopPoint(rings[i].join);
		} else if (isConvex) {
			addLoopPoint(rings[i].inner[0]);
			addLoopPoint(rings[i].inner[rings[i].steps]);
		} else {
			for (uint32_t k = 0; k <= rings[i].steps; ++k) {
				addLoopPoint(rings[i].outer[k]);
			}
		}
	}
	if (loop.size() >= 2 && Length(loop.front() - loop.back()) <= kMinLoopSpacing) {
		loop.pop_back();
	}
	std::vector<uint32_t> loopIndices;
	if (!Polygon2D::Triangulate(loop, loopIndices)) {
		return false;
	}

	// ===== 2. 内側（全部塗る）=====
	const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
	for (const Vector2& point : loop) {
		mesh.vertices.push_back({point, {0.0f, 0.0f}, {0.0f, 0.0f}, static_cast<uint32_t>(PolylinePiece::FillInside)});
	}
	for (uint32_t index : loopIndices) {
		mesh.indices.push_back(base + index);
	}

	// ===== 3. 縁の帯（PS は符号付きの距離を測る。param.y を掛けると、塗りの外が＋になる）=====
	for (int i = 0; i < n; ++i) {
		const bool isConvex = (corners[i].turn > 0.0f) == isInsideLeft;
		writer.AddCorner(corners[i], rings[i], PolylinePiece::FillCorner, {corners[i].radius, isConvex ? 1.0f : -1.0f}, isConvex);
	}
	const Vector2 edgeParam = {0.0f, isInsideLeft ? -1.0f : 1.0f}; // 左（local.y が＋）が内側なら、符号を反対にする
	for (int i = 0; i < n; ++i) {
		const int next = (i + 1) % n;
		const Vector2 direction = Normalize(path.points[next] - path.points[i]);
		writer.AddSegment(MakeCornerEnd(corners[i], rings[i], false), MakeCornerEnd(corners[next], rings[next], true), path.points[i], direction, PolylinePiece::FillEdge, edgeParam);
	}
	return true;
}
} // namespace


//=============================================================================
// メッシュを作る
//=============================================================================
PolylineMeshInfo PolylineMeshBuilder::Build(const PolylineShape& shape, PolylineMeshData& mesh) {
	mesh.vertices.clear(); // clear は大きさ（capacity）を残すので、作り直してもメモリを取り直さない
	mesh.indices.clear();
	PolylineMeshInfo info;
	const CleanPath path = MakeCleanPath(shape);
	if (path.count < 2) {
		return info;
	}
	const PathCorners corners = MakeCorners(path);
	MeshWriter writer(mesh);
	// --- 塗りが先（同じ1回の描画で、線が塗りの上に重なるように）---
	if (shape.hasFill && path.isClosed) {
		info.isFillValid = BuildFill(path, corners, writer, mesh);
	}
	info.fillIndexCount = writer.GetIndexCount();
	if (shape.halfWidth > 0.0f) {
		BuildStroke(path, corners, shape, writer);
	}
	info.strokeIndexCount = writer.GetIndexCount() - info.fillIndexCount;
	return info;
}
```

### B-4 `Math/Geometry/Polygon.cpp`

`Triangulate` を全体差し替え（1.5）。上のコメント `// 三角形に分ける（耳切り法）` の見出しはそのまま。

```cpp
// 点を毎フレーム動かす形（Polyline の塗り）でも使うので、調べる量を減らしてある
// ・切り取った点は、前と次の番号（prev・next）をつなぎ替えて飛ばす（配列から消して詰めない）
// ・耳の中に入りうるのは、へこんだ角の点だけ（とがった角の点が入るなら、辺どうしが交差している）。なので、へこんだ点だけ調べる
// ・切り取ったら、最初からではなく、切った所の手前から調べ直す
bool Triangulate(std::span<const Vector2> points, std::vector<uint32_t>& indices) {
	const uint32_t count = static_cast<uint32_t>(points.size());
	if (count < 3) {
		return false;
	}
	// --- 点の番号を反時計回りに並べ、となりの点のつながりを作る（位置 i の点は points[order[i]]）---
	std::vector<uint32_t> order(count);
	std::iota(order.begin(), order.end(), 0u); // 0, 1, 2, … と順に入れる
	if (SignedArea(points) < 0.0f) {
		std::ranges::reverse(order); // 時計回りなら逆に並べる
	}
	std::vector<uint32_t> prev(count);
	std::vector<uint32_t> next(count);
	for (uint32_t i = 0; i < count; ++i) {
		prev[i] = (i + count - 1) % count;
		next[i] = (i + 1) % count;
	}
	const auto at = [&](uint32_t i) -> const Vector2& { return points[order[i]]; };
	// へこんだ角（右回り）・一直線の角か。となりの点を切り取ると変わるので、そのたびに調べ直す
	const auto calcReflex = [&](uint32_t i) { return Orient(at(prev[i]), at(i), at(next[i])) <= 0.0f; };
	std::vector<uint8_t> isReflex(count);
	for (uint32_t i = 0; i < count; ++i) {
		isReflex[i] = calcReflex(i);
	}
	// 耳か：とがった角で、三角形の中（辺の上も入れる）に、ほかのへこんだ点が無い
	const auto isEar = [&](uint32_t i) {
		if (isReflex[i]) {
			return false;
		}
		const Vector2& a = at(prev[i]);
		const Vector2& b = at(i);
		const Vector2& c = at(next[i]);
		for (uint32_t other = next[next[i]]; other != prev[i]; other = next[other]) {
			if (isReflex[other] && IsPointInTriangle(at(other), a, b, c)) {
				return false;
			}
		}
		return true;
	};
	// 点 i を切り離す（前と次をつなぎ、となりの角の形を調べ直す）
	const auto unlink = [&](uint32_t i) {
		next[prev[i]] = next[i];
		prev[next[i]] = prev[i];
		isReflex[prev[i]] = calcReflex(prev[i]);
		isReflex[next[i]] = calcReflex(next[i]);
	};

	// --- 耳を1つずつ切り取る（点が n 個なら、三角形は n-2 個）---
	// 耳：となりの2点と結んだ三角形が、とがっていて（左回り）、中にほかの点が入っていない角。切り取っても、残りは多角形のまま
	uint32_t remaining = count;
	uint32_t current = 0;
	uint32_t missCount = 0; // 続けて耳でなかった点の数（1周しても耳が無いかを数える）
	while (remaining > 3) {
		if (isEar(current)) {
			indices.insert(indices.end(), {order[prev[current]], order[current], order[next[current]]});
			const uint32_t before = prev[current];
			unlink(current);
```

### B-5 `Systems/Rendering2D/SpriteRenderSystem.h`・`.cpp`

`Vector Shape` の中にあった「行列の x 軸・y 軸の長さ」を、折れ線と共通にするために公開する。

`.h` の `CalcScreenQuad` の下に足す。

```cpp
	// 画面に重ねる物の、横・縦の大きさの倍率（ワールド行列の x 軸・y 軸の長さ。Transform の scale に親の scale も掛かった値。回っていても測れる）
	static Vector2 CalcScreenScale(const Matrix4x4& worldMatrix);
```

`.cpp` の include に `#include <cmath>` を足し（`<array>` の下）、`CalcScreenQuad` の下に足す。

```cpp
Vector2 SpriteRenderSystem::CalcScreenScale(const Matrix4x4& worldMatrix) {
	// std::hypot(x, y)：sqrt(x * x + y * y) と同じ長さを、途中であふれにくい形で出す
	const Matrix4x4& m = worldMatrix;
	return {std::hypot(m.m[0][0], m.m[0][1]), std::hypot(m.m[1][0], m.m[1][1])};
}
```

### B-6 `Systems/UI/VectorShapeSystem.cpp`

`CalcAxisScale`（と上のコメントの1行）を消し、`DrawShape` の中の呼び出しを差し替える。

```cpp
	const Vector2 scale = SpriteRenderSystem::CalcScreenScale(transform->worldMatrix);
```

### B-7 `Systems/UI/PolylineSystem.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"


/// <summary>
/// Polyline（点をつないだ UI の線・形）を描く
/// <para>停止中も描く（置いた場所をエディタで見られるように）。Scene ビューにも Image と同じ位置に重なる</para>
/// <para>メッシュ（直線の部分・角の部分・端）は Entity ごとに覚えておき、点・半径・太さ・端の形・Scale が変わった時だけ作り直す。
/// 位置・回転・色・透明度は定数で渡すので、変えても作り直さない</para>
/// </summary>
class PolylineSystem {
public:
	// Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 描画リクエストを積む（EngineSystems から。Vector Shape の後、Text の前）
	static void Draw();

	// ===== エディタから使う =====

	// Scene ビューで、選んでいる Entity の点を動かしているか（Inspector の Edit Points で切り替える）
	static bool IsEditing();
	static void SetEditing(bool isEditing);

	/// <summary>
	/// 点を囲む四角（Entity から見たピクセル。線の太さの半分も含める。Scale は掛けない）
	/// <para>UI ギズモの枠とクリックで選ぶのに使う。点が無ければ false</para>
	/// </summary>
	static bool CalcLocalBounds(const PolylineComponent& polyline, Vector2& min, Vector2& max);
};
```

### B-8 `Systems/UI/PolylineSystem.cpp`（新しいファイル）

```cpp
#include "PolylineSystem.h"

#include <cmath>
#include <format>
#include <memory>
#include <algorithm>
#include <unordered_map>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/DrawRequest.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/UI/PolylineMesh.h"

namespace {
constexpr float kMinScale = 1.0e-4f; // これより小さい Scale は描かない（0 で割らないように）

/// <summary>
/// Entity 1つ分のメッシュ。作ったときの形を覚えておき、変わった時だけ作り直す
/// </summary>
struct PolylineCache {
	PolylineShape shape;                    // 作ったときの形
	std::shared_ptr<PolylineMeshData> mesh; // 作ったメッシュ（描き終わるまで、RenderQueue のリクエストも同じ物を指す）
	PolylineMeshInfo info;                  // 塗り・線の番号の数
	bool isUsed = false;                    // このフレームの Draw で使ったか（使わなかった物は捨てる）
};

/// <summary>
/// PolylineSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PolylineState {
	std::unordered_map<EntityId, PolylineCache> caches; // Entity の番号 → メッシュ（折れ線が多くても1回で探せる）
	bool isEditing = false;                             // Scene ビューで点を編集しているか
};
std::unique_ptr<PolylineState> state;

//=============================================================================
// Component の値 → メッシュ・シェーダーの値
//=============================================================================
// ===== メッシュを作る元の値（Scale を掛けたピクセル）=====
// 横だけ伸ばしても角の丸み・太さがゆがまないように、半径・太さには小さい方の倍率を掛ける（Vector Shape と同じ）
PolylineShape MakeShape(const PolylineComponent& polyline, const Vector2& scale) {
	PolylineShape shape;
	const float uniformScale = (std::min)(scale.x, scale.y);
	shape.count = polyline.GetCount();
	for (int i = 0; i < shape.count; ++i) {
		shape.points[i] = {polyline.points[i].x * scale.x, polyline.points[i].y * scale.y};
		shape.radii[i] = (std::max)(polyline.radii[i], 0.0f) * uniformScale;
	}
	shape.isClosed = polyline.closed;
	shape.hasFill = polyline.HasFill();
	shape.halfWidth = polyline.HasStroke() ? polyline.strokeWidth * uniformScale * 0.5f : 0.0f;
	shape.cap = polyline.cap;
	return shape;
}

// ===== 頂点の位置（Scale を掛けたピクセル）→ 画面の正規化座標 =====
// SpriteRenderSystem::CalcScreenPoint と同じ写し方（回転・移動・アンカー・ピクセル → 正規化座標）を、行列1つにまとめる
// メッシュには Scale を掛けてあるので、行列の x 軸・y 軸は Scale で割って、回転と大きさの向きだけにする
Matrix4x4 MakeToScreen(const Matrix4x4& world, ScreenAnchor anchor, const Vector2& scale) {
	const float toNdcX = 1.0f / (GameScreen::GetWidth() * 0.5f);
	const float toNdcY = 1.0f / (GameScreen::GetHeight() * 0.5f);
	const Vector2 origin = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 0.0f});
	Matrix4x4 m = MakeIdentity4x4();
	m.m[0][0] = world.m[0][0] / scale.x * toNdcX;
	m.m[0][1] = world.m[0][1] / scale.x * toNdcY;
	m.m[1][0] = world.m[1][0] / scale.y * toNdcX;
	m.m[1][1] = world.m[1][1] / scale.y * toNdcY;
	m.m[3][0] = origin.x;
	m.m[3][1] = origin.y;
	return m;
}

// ===== Entity のメッシュを、今の形にそろえる（形が前と同じなら何もしない）=====
PolylineCache& UpdateCache(Handle<Entity> entity, const PolylineShape& shape) {
	const Entity* owner = EntityManager::Get(entity);
	// try_emplace：その番号がまだ無いときだけ、空の PolylineCache を作って入れる（あれば、あった物を返す）
	auto [it, isAdded] = state->caches.try_emplace((owner != nullptr) ? owner->id : 0);
	PolylineCache& cache = it->second;
	cache.isUsed = true;
	if (!isAdded && cache.mesh != nullptr && cache.shape == shape) {
		return cache; // 形が同じ：作り直さない
	}
	// まだ RenderQueue が前のメッシュを持っていたら（use_count が 2 以上）、新しく作る。持っていなければ、配列を使い回して作り直す
	if (cache.mesh == nullptr || cache.mesh.use_count() > 1) {
		cache.mesh = std::make_shared<PolylineMeshData>();
	}
	cache.shape = shape;
	cache.info = PolylineMeshBuilder::Build(shape, *cache.mesh);
	return cache;
}

//=============================================================================
// 描く
//=============================================================================
void DrawPolyline(Handle<Entity> entity, const PolylineComponent& polyline) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
	const Vector2 scale = SpriteRenderSystem::CalcScreenScale(transform->worldMatrix);
	if (groupAlpha <= 0.0f || scale.x < kMinScale || scale.y < kMinScale) {
		return;
	}
	const PolylineShape shape = MakeShape(polyline, scale);
	const PolylineCache& cache = UpdateCache(entity, shape);
	const uint32_t indexCount = cache.info.fillIndexCount + cache.info.strokeIndexCount;
	if (indexCount == 0) {
		return;
	}
	// --- 塗りと線を1回で描く（番号は塗りが先なので、線が上に重なる）---
	Renderer::PolylineConfig config;
	config.mesh = cache.mesh;
	config.indexStart = 0;
	config.indexCount = indexCount;
	config.material.toScreen = MakeToScreen(transform->worldMatrix, polyline.anchor, scale);
	config.material.fillColor = ClampColor(MultiplyAlpha(polyline.color, groupAlpha));
	config.material.strokeColor = ClampColor(MultiplyAlpha(polyline.strokeColor, groupAlpha));
	config.material.halfWidth = shape.halfWidth;
	config.material.blendMode = static_cast<uint32_t>(polyline.blend);
	config.blendMode = polyline.blend;
	config.order = polyline.order;
	config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	Renderer::DrawPolyline(config);
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Polyline の見せ方。Inspector とシーンファイルの両方がこの並びを使う
// 点は数を先に読むので、シーンファイルから読むときも、その数だけ点を読む（Polygon Shape と同じ）
void DescribePolyline(ComponentUI& ui, PolylineComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Anchor###anchor", value.anchor, Tip("画面のどこから位置を数えるか（位置は Transform の Translation の x・y。ピクセルで、上が＋）"));
	ui.Field("Order###order", value.order, Tip("重ねる順（大きいほど手前。Image・Vector Shape・Text の Order とも比べる）"));
	ui.Field("Blend###blend", value.blend, Tip("重ね方（Normal：透明な所が透ける / Add：足して光る）。None は縁のなめらかさが無くなる"));

	// ===== 点（Edit Points は Inspector だけ。保存・読み込みのときは Button がいつも false）=====
	ui.Separator("Points");
	if (ui.Button(state->isEditing ? "Finish Editing###editPoints" : "Edit Points###editPoints")) {
		state->isEditing = !state->isEditing;
	}
	ui.Label("Scene ビューで ■ をドラッグ・＋ で足す・Delete で消す");
	ui.Field("Closed###closed", value.closed, Tip("最後の点から最初の点へもつなぐ（閉じた形。塗れるのは閉じた形だけ）"));
	const int before = value.GetCount();
	ui.Field("Point Count###pointCount", value.pointCount, Range(static_cast<float>(kMinPolylinePoints), static_cast<float>(kMaxPolylinePoints)).Tip("点の数。Scene ビューで足す・消すこともできる"));
	value.pointCount = std::clamp(value.pointCount, kMinPolylinePoints, kMaxPolylinePoints);
	// 増やした点は、閉じた形なら最後と最初の真ん中、開いた線なら最後の辺を伸ばした所に置く（前に使っていた所や原点に飛んでいかないように）
	for (int i = (std::max)(before, 2); i < value.pointCount; ++i) {
		const Vector2& last = value.points[i - 1];
		value.points[i] = value.closed ? (last + value.points[0]) * 0.5f : last + (last - value.points[i - 2]);
		value.radii[i] = 0.0f;
	}
	for (int i = 0; i < value.pointCount; ++i) {
		ui.Field(std::format("Point {}###point{}", i, i).c_str(), value.points[i], Drag(1.0f)); // 保存の名前は point0, point1, …（Timeline の値のトラックで動かせる）
		// 開いた線の両端は角ではないので、半径は使わない（灰色にするだけで、保存はする）
		ui.BeginDisabled(!value.closed && (i == 0 || i == value.pointCount - 1));
		ui.Field(std::format("Radius {}###radius{}", i, i).c_str(), value.radii[i], AtLeast(0.0f).Drag(0.5f).Tip("角の半径（ピクセル）。となりの辺に収まらなければ、描くときに縮める"));
		ui.EndDisabled();
	}

	ui.Separator("Paint");
	ui.Field("Style###style", value.style, Tip("Fill：塗りだけ / Fill And Stroke：塗りと線 / Stroke：線だけ。塗れるのは閉じた形だけ"));
	ui.BeginDisabled(!value.HasFill());
	ui.ColorField("Color###color", value.color);
	ui.EndDisabled();

	ui.Separator("Stroke");
	ui.BeginDisabled(value.style == VectorShapeStyle::Fill);
	ui.Field("Stroke Width###strokeWidth", value.strokeWidth, AtLeast(0.0f).Drag(0.25f).Tip("線の太さ（ピクセル）。点の真ん中に、両側へ半分ずつ"));
	ui.ColorField("Stroke Color###strokeColor", value.strokeColor);
	ui.EndDisabled();
	ui.BeginDisabled(value.style == VectorShapeStyle::Fill || value.closed);
	ui.Field("Cap###cap", value.cap, Tip("開いた線の端の形（Butt：端でまっすぐ切る / Round：半円 / Square：太さの半分伸ばして切る）"));
	ui.EndDisabled();
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void PolylineSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<PolylineState>();

	// Polyline の置き場所・保存・Inspector を登録する。見せ方は DescribePolyline 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<PolylineComponent>();
	ComponentSerializer::Register<PolylineComponent>("Polyline", &DescribePolyline);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<PolylineComponent>>("Polyline", "UI", &DescribePolyline));
}

void PolylineSystem::Release() { state.reset(); }

//=============================================================================
// 描く（停止中も）
//=============================================================================
void PolylineSystem::Draw() {
	HTT_ASSERT_MSG(state != nullptr, "PolylineSystem::Initialize()を先に呼んでください");
	for (auto& [id, cache] : state->caches) {
		cache.isUsed = false;
	}
	// 重ねる順は RenderQueue が Order で並べるので、ここは見つけた順に積む
	EntityManager::ForEach<PolylineComponent>([](Handle<Entity> entity, const PolylineComponent& polyline) {
		if (polyline.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			DrawPolyline(entity, polyline);
		}
	});
	// 描かなかった物（消えた・外した・無効にした）のメッシュを捨てる
	// std::erase_if：条件に合う物を全部消す（map でも使える。C++20）
	std::erase_if(state->caches, [](const auto& pair) { return !pair.second.isUsed; });
}

//=============================================================================
// エディタから使う
//=============================================================================
bool PolylineSystem::IsEditing() { return state != nullptr && state->isEditing; }

void PolylineSystem::SetEditing(bool isEditing) {
	HTT_ASSERT_MSG(state != nullptr, "PolylineSystem::Initialize()を先に呼んでください");
	state->isEditing = isEditing;
}

bool PolylineSystem::CalcLocalBounds(const PolylineComponent& polyline, Vector2& min, Vector2& max) {
	const int count = polyline.GetCount();
	if (count == 0) {
		return false;
	}
	min = max = polyline.points[0];
	for (int i = 1; i < count; ++i) {
		min = {(std::min)(min.x, polyline.points[i].x), (std::min)(min.y, polyline.points[i].y)};
		max = {(std::max)(max.x, polyline.points[i].x), (std::max)(max.y, polyline.points[i].y)};
	}
	// 線は点の外へ太さの半分はみ出す
	const float margin = polyline.HasStroke() ? polyline.strokeWidth * 0.5f : 0.0f;
	min -= Vector2{margin, margin};
	max += Vector2{margin, margin};
	return true;
}
```

### B-9 `Systems/EngineSystems.cpp`

```cpp
// UI
#include "HTTEngine/Systems/UI/UIStateSystem.h"
#include "HTTEngine/Systems/UI/PolylineSystem.h"
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
```

`Initialize`：

```cpp
	VectorShapeSystem::Initialize();      // Vector Shape
	PolylineSystem::Initialize();         // Polyline
```

`Release`：

```cpp
	VectorShapeSystem::Release();
	PolylineSystem::Release();
	EntityManager::Release();
```

`Draw`（Vector Shape の下、Text の上）：

```cpp
	VectorShapeSystem::Draw(); // 画像を使わない UI の図形（Vector Shape）の描画（Image・Text とは Order の順に重なる）
	PolylineSystem::Draw();    // 点をつないだ UI の線・形（Polyline）の描画（Image・Text とは Order の順に重なる）
	TextRenderSystem::Draw();  // 画面に重ねる文字（Text）の描画（Image とは Order の順に重なる）
```

### B-10 `ECS/GameComponent.h`

ゲームの Script から `PolylineComponent` を使えるように、`// UI` の所に足す。

```cpp
// UI
#include "HTTEngine/Systems/UI/PolylineSystem.h"
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
```

### B-11 `Editor/Windows/HierarchyWindow.cpp`

include に `#include "HTTEngine/Components/UI/PolylineComponent.h"` を足す（`TextComponent.h` の上）。作るメニューの Vector Shape の下に足す。

```cpp
	// --- 画像：3D空間に置く板（Sprite Renderer）・画面に重ねる絵（Image）・図形（Vector Shape）・折れ線（Polyline）・文字（Text）を付けた状態で作る ---
```

```cpp
	if (ImGui::MenuItem("Polyline")) {
		EditorHistory::RequestCreate("Polyline", top, {ComponentSnapshot::Make(PolylineComponent{})});
	}
```

### B-12 `Editor/Gizmos/UIGizmo.cpp`

include に足す。

```cpp
#include "HTTEngine/Components/UI/PolylineComponent.h"
```

```cpp
#include "HTTEngine/Systems/UI/PolylineSystem.h"
```

`CalcShape` の Text の上に足す（折れ線は、点を囲む四角で選ぶ）。

```cpp
	// 折れ線は、点を囲む四角（線の太さも入れる）
	if (const PolylineComponent* polyline = EntityManager::Get<PolylineComponent>(entity); polyline != nullptr && polyline->enabled) {
		Vector2 min;
		Vector2 max;
		if (PolylineSystem::CalcLocalBounds(*polyline, min, max)) {
			const auto toScreen = [&m, polyline](float x, float y) { return SpriteRenderSystem::CalcScreenPoint(m, polyline->anchor, {x, y}); };
			shape.quad = {toScreen(min.x, min.y), toScreen(min.x, max.y), toScreen(max.x, min.y), toScreen(max.x, max.y)};
			shape.position = toScreen(0.0f, 0.0f);
			return true;
		}
	}
```

`PickUI` の Vector Shape の下に足す。

```cpp
	EntityManager::ForEach<PolylineComponent>([&consider](Handle<Entity> entity, const PolylineComponent& polyline) { consider(entity, polyline.order); });
```

`IsScreenUI` を差し替える。

```cpp
bool UIGizmo::IsScreenUI(Handle<Entity> entity) {
	return EntityManager::Get<ImageComponent>(entity) != nullptr || EntityManager::Get<VectorShapeComponent>(entity) != nullptr || EntityManager::Get<PolylineComponent>(entity) != nullptr ||
	       EntityManager::Get<TextComponent>(entity) != nullptr;
}
```

`UIGizmo.h` のコメント2か所（クラスの説明と `IsScreenUI`）の「Image・Vector Shape・Text」に Polyline を足す（説明だけ）。

### B-13 プロジェクトに足す

- `Components/UI/PolylineComponent.h`
- `Systems/UI/PolylineMesh.h`・`PolylineMesh.cpp`・`PolylineSystem.h`・`PolylineSystem.cpp`

---

## パートC 頂点のギズモ

### C-1 `Editor/Gizmos/PointListGizmo.h`（全体を差し替え）

`PointMapping` と `MakeCameraMapping`・`MakeScreenMapping` を足し、`PointList` の `planeZ` を消して `values`・`gridSize` を足した。`Draw` の引数が、行列とカメラから写し方に変わった。

```cpp
#pragma once
#include <span>
#include <functional>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;
class ComponentEditor;

/// <summary>
/// 点の写し方（Entity から見た X・Y ⇔ Scene ビューの絵の上のピクセル）
/// <para>3D の物はカメラで写し（MakeCameraMapping）、画面に重ねる UI は画面のピクセルで写す（MakeScreenMapping）</para>
/// <para>std::function：関数・ラムダを「呼べる物」として1つの型で持てる入れ物。写し方ごとに違うラムダを入れて、同じ編集の処理から呼ぶ</para>
/// </summary>
struct PointMapping {
	std::function<bool(const Vector2& local, ImVec2& pixel)> toPixel; // 点 → 絵の上のピクセル（写っていなければ false）
	std::function<bool(const ImVec2& pixel, Vector2& local)> toLocal; // 絵の上のピクセル → 点（マウスの所。決まらなければ false）
};

/// <summary>
/// Scene ビューで編集する点の並び（Polygon Shape の外周・Arrow Strip の線・Polyline の点）。点は Entity から見た X・Y
/// <para>持ち主（PolygonShapeGizmo・ArrowStripGizmo・PolylineGizmo）が、毎フレーム Component から作って渡す</para>
/// </summary>
struct PointList {
	std::span<Vector2> storage;              // 点の置き場所（全部。足すと、後ろの点を1つずつずらす）
	std::span<float> values;                 // 点と一緒にずらす値（Polyline の角の半径。無ければ空。足した点は、となりの2つの真ん中の値）
	int* count = nullptr;                    // 使っている点の数（足す・消すと変わる）
	int minCount = 2;                        // これより少なくは消せない
	bool isClosed = false;                   // 最後の点から最初の点へも辺がある（多角形）
	float gridSize = 0.25f;                  // 点が吸い付く間隔（Entity から見た長さ。Ctrl を押している間は吸い付かない）
	bool isValid = true;                     // false なら、辺を赤く描く（形がおかしい）
	std::span<const Vector3> curve;          // 点の間を通る線（Entity から見た座標。z は使わない。あれば、辺の代わりにこれを太く描く）
	const ComponentEditor* editor = nullptr; // Undo の写しを取る（Inspector と同じ ComponentEditor::Save）
	const void* component = nullptr;         // 写しを取る Component（点を持っている物）
};


/// <summary>
/// Scene ビューで、点を動かす・足す・消す（形・線の編集の共通部分）
/// <para>操作：■ をドラッグ（gridSize に吸い付く。Ctrl で吸い付かない）／辺の真ん中の ＋ を押すと点を足す（そのままドラッグできる）／
/// 点を選んで Delete で消す（minCount より少なくはできない）／Esc で編集を終わる</para>
/// <para>1回のドラッグ・足す・消すで、1件の履歴になる（Ctrl+Z で戻せる）</para>
/// <para>選んだ点などの状態は1つだけ持つ。別の物を渡した・前のフレームに呼ばれなかったら、状態を捨てる</para>
/// </summary>
class PointListGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に描いて、マウス・キーで編集する。持ち主の Draw から毎フレーム1回
	/// </summary>
	/// <param name="entity">点を持っている Entity（Undo の記録に使う）</param>
	/// <param name="list">編集する点</param>
	/// <param name="mapping">点の写し方（MakeCameraMapping・MakeScreenMapping で作る）</param>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	/// <returns>編集を続けるか（Esc を押したら false。持ち主は編集中の印を消す）</returns>
	static bool Draw(Handle<Entity> entity, const PointList& list, const PointMapping& mapping, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);

	/// <summary>
	/// 3D の物の点の写し方。点は Entity の面（Entity から見た Z が planeZ）の上で、カメラから見た絵の上へ写す
	/// <para>返した物は、camera を参照で持つ。そのフレームの Draw にだけ渡す</para>
	/// </summary>
	static PointMapping MakeCameraMapping(const Matrix4x4& world, const Camera& camera, float planeZ, const ImVec2& imageMin, const ImVec2& imageSize);

	/// <summary>
	/// 画面に重ねる UI の点の写し方。点は Entity の位置を原点にしたピクセル（Image と同じ置き方。anchor の所から数える）
	/// </summary>
	static PointMapping MakeScreenMapping(const Matrix4x4& world, ScreenAnchor anchor, const ImVec2& imageMin, const ImVec2& imageSize);
};
```

### C-2 `Editor/Gizmos/PointListGizmo.cpp`（全体を差し替え）

位置の変換（`ToWorld`・`WorldToPixel`・`MouseToLocal`）を、写し方の関数を呼ぶ形にした。中身は `MakeCameraMapping` へ移した。点を足す・消すときに `values` も一緒にずらす。

```cpp
#ifdef USE_IMGUI
#include "PointListGizmo.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <externals/imgui/ImGuizmo.h>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"

namespace {
constexpr float kHandleHalfSize = 5.0f;                     // 点の印（■）の大きさの半分（ピクセル）
constexpr float kPlusHalfSize = 4.0f;                       // 辺の真ん中の印（＋）の大きさの半分（ピクセル）
constexpr float kPickRadius = 9.0f;                         // マウスがこれより近ければ、点・＋を掴める（ピクセル）
constexpr float kLineThickness = 2.0f;                      // 辺・線の太さ（ピクセル）
constexpr float kGuideThickness = 1.0f;                     // 線があるときの、辺（点と点を結ぶ補助線）の太さ（ピクセル）
constexpr float kMinDeterminant = 1.0e-12f;                 // 画面の軸が潰れている（大きさ 0）と見なす行列式の大きさ
constexpr ImU32 kEdgeColor = IM_COL32(80, 220, 255, 255);   // 辺・線（水色）
constexpr ImU32 kGuideColor = IM_COL32(80, 220, 255, 110);  // 線があるときの辺（薄い水色）
constexpr ImU32 kInvalidColor = IM_COL32(255, 70, 70, 255); // 形がおかしい（辺が交差している）ときの辺（赤）
constexpr ImU32 kPointColor = IM_COL32(255, 255, 255, 255); // 点（白）
constexpr ImU32 kActiveColor = IM_COL32(255, 160, 40, 255); // 選んだ点・マウスが乗っている点と ＋（選んだ物の枠と同じオレンジ）
constexpr ImU32 kPlusColor = IM_COL32(80, 220, 255, 200);   // 辺の真ん中の ＋（水色）

/// <summary>
/// 編集の状態（エディタに1つだけ）
/// </summary>
struct EditState {
	Handle<Entity> entity;           // 編集している Entity
	const void* component = nullptr; // 編集している Component（同じ Entity の別の Component を渡されたら、状態を捨てる）
	int lastFrame = -1;              // 最後に Draw が呼ばれた ImGui のフレーム（とんでいたら、編集をやめていたので状態を捨てる）
	int selectedPoint = -1;          // 選んでいる点（Delete で消す。-1 なら無し）
	int dragPoint = -1;              // ドラッグしている点（-1 ならしていない）
	std::vector<std::byte> before;   // ドラッグ・足す・消すの前の Component（ComponentEditor::Save の写し。Undo の「前」）
};

/// <summary>
/// 点の、Scene ビューの絵の上の位置
/// </summary>
struct ScreenPoints {
	std::vector<ImVec2> pixels;  // 絵の上のピクセル
	std::vector<bool> isVisible; // 写っているか（カメラの後ろの点は false）
	int count = 0;               // 点の数
};

EditState edit;

// ===== 画面の位置（真ん中0、右と上が＋、端が±1）↔ Scene ビューの絵の上のピクセル（Y は下向き）=====
ImVec2 ToPixel(const Vector2& ndc, const ImVec2& imageMin, const ImVec2& imageSize) {
	return ImVec2(imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x, imageMin.y + (0.5f - ndc.y * 0.5f) * imageSize.y);
}
Vector2 ToNdc(const ImVec2& pixel, const ImVec2& imageMin, const ImVec2& imageSize) { return {(pixel.x - imageMin.x) / imageSize.x * 2.0f - 1.0f, 1.0f - (pixel.y - imageMin.y) / imageSize.y * 2.0f}; }

// 2点の距離の2乗（ピクセル）
float DistanceSq(const ImVec2& a, const ImVec2& b) {
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

// 2点の真ん中（ピクセル）
ImVec2 Midpoint(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

// 辺の数（多角形は点の数と同じ。線は1つ少ない）。辺 i は 点 i → 点 (i + 1) % count
int EdgeCount(const PointList& list, int count) { return list.isClosed ? count : (std::max)(count - 1, 0); }

// 足せる余りがあるか
bool CanInsert(const PointList& list) { return *list.count < static_cast<int>(list.storage.size()); }

//=============================================================================
// 履歴（Undo）
//=============================================================================
// ===== 変える前の中身を覚える =====
void Remember(const PointList& list) {
	if (list.editor != nullptr) {
		edit.before = list.editor->Save(list.component);
	}
}

// ===== 覚えた中身と今の中身を、1件の履歴にする（Inspector で値を変えたときと同じ入口。変わっていなければ積まれない）=====
void Commit(const PointList& list) {
	if (list.editor != nullptr && !edit.before.empty()) {
		EditorHistory::RecordComponentChange(edit.entity, *list.editor, edit.before, list.editor->Save(list.component));
		EditorHistory::CommitComponentChanges();
	}
	edit.before.clear();
}

//=============================================================================
// 位置の変換
//=============================================================================
// ===== 点の、絵の上の位置 =====
ScreenPoints CalcScreenPoints(const PointList& list, const PointMapping& mapping) {
	ScreenPoints result;
	result.count = *list.count;
	result.pixels.resize(result.count);
	result.isVisible.resize(result.count);
	for (int i = 0; i < result.count; ++i) {
		result.isVisible[i] = mapping.toPixel(list.storage[i], result.pixels[i]);
	}
	return result;
}

// ===== グリッドに吸い付ける（Ctrl を押している間はしない）=====
Vector2 Snap(const Vector2& point, float gridSize) {
	if (ImGui::GetIO().KeyCtrl || gridSize <= 0.0f) {
		return point;
	}
	return {std::round(point.x / gridSize) * gridSize, std::round(point.y / gridSize) * gridSize};
}

//=============================================================================
// 操作
//=============================================================================
// ===== マウスの近くの点（無ければ -1）=====
int FindHoveredPoint(const ScreenPoints& screen, const ImVec2& mouse) {
	int found = -1;
	float best = kPickRadius * kPickRadius;
	for (int i = 0; i < screen.count; ++i) {
		const float distanceSq = DistanceSq(mouse, screen.pixels[i]);
		if (screen.isVisible[i] && distanceSq < best) {
			found = i;
			best = distanceSq;
		}
	}
	return found;
}

// ===== マウスの近くの ＋（辺 i の真ん中。無ければ -1）=====
int FindHoveredPlus(const PointList& list, const ScreenPoints& screen, const ImVec2& mouse) {
	int found = -1;
	float best = kPickRadius * kPickRadius;
	for (int i = 0; i < EdgeCount(list, screen.count); ++i) {
		const int next = (i + 1) % screen.count;
		if (!screen.isVisible[i] || !screen.isVisible[next]) {
			continue;
		}
		const float distanceSq = DistanceSq(mouse, Midpoint(screen.pixels[i], screen.pixels[next]));
		if (distanceSq < best) {
			found = i;
			best = distanceSq;
		}
	}
	return found;
}

// ===== 辺 edge の真ん中に点を足す。足した点の番号を返す =====
int InsertPoint(const PointList& list, int edge) {
	const int count = *list.count;
	const int insert = edge + 1;
	const int next = (edge + 1) % count;
	const Vector2 middle = (list.storage[edge] + list.storage[next]) * 0.5f;
	// insert 番から後ろを1つずつ後ろへずらして、空いた所に入れる（copy_backward：後ろから写すので、重なっていても壊れない）
	std::copy_backward(list.storage.begin() + insert, list.storage.begin() + count, list.storage.begin() + count + 1);
	list.storage[insert] = middle;
	// 点と一緒の値（角の半径）も同じようにずらす。足した点は、となりの2つの真ん中の値
	if (!list.values.empty()) {
		const float middleValue = (list.values[edge] + list.values[next]) * 0.5f;
		std::copy_backward(list.values.begin() + insert, list.values.begin() + count, list.values.begin() + count + 1);
		list.values[insert] = middleValue;
	}
	*list.count = count + 1;
	return insert;
}

// ===== 点 index を消す（後ろの点を1つずつ前へ詰める）=====
void ErasePoint(const PointList& list, int index) {
	std::copy(list.storage.begin() + index + 1, list.storage.begin() + *list.count, list.storage.begin() + index);
	if (!list.values.empty()) {
		std::copy(list.values.begin() + index + 1, list.values.begin() + *list.count, list.values.begin() + index);
	}
	--*list.count;
}

// ===== マウス：押す（点を掴む・＋で足す・何も無い所で選ぶのをやめる）・ドラッグ・離す =====
void HandleMouse(const PointList& list, const PointMapping& mapping, const ScreenPoints& screen, bool isHovered) {
	// --- 押した（右上の軸の上は、そちらの操作）---
	const bool canGrab = isHovered && edit.dragPoint < 0 && !ImGuizmo::IsViewManipulateHovered();
	if (canGrab && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const ImVec2 mouse = ImGui::GetMousePos();
		const int hoveredPoint = FindHoveredPoint(screen, mouse);
		const int hoveredPlus = CanInsert(list) ? FindHoveredPlus(list, screen, mouse) : -1;
		if (hoveredPoint >= 0) {
			Remember(list);
			edit.selectedPoint = edit.dragPoint = hoveredPoint;
		} else if (hoveredPlus >= 0) {
			Remember(list); // 足す前を覚える（足してドラッグして離すまでが1件）
			edit.selectedPoint = edit.dragPoint = InsertPoint(list, hoveredPlus);
		} else {
			edit.selectedPoint = -1;
		}
	}
	if (edit.dragPoint < 0) {
		return;
	}
	// --- ドラッグ中：マウスの所へ（グリッドに吸い付く）。離したら1件の履歴にする ---
	// 少し動かすまで（IsMouseDragging：押した所から数ピクセル）は点を動かさない。押しただけで、グリッドに吸い付いて位置が変わらないように
	if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		Vector2 local;
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && mapping.toLocal(ImGui::GetMousePos(), local)) {
			list.storage[edit.dragPoint] = Snap(local, list.gridSize);
		}
	} else {
		Commit(list);
		edit.dragPoint = -1;
	}
}

// ===== キー：Delete で選んだ点を消す（minCount より少なくはしない）=====
void HandleDelete(const PointList& list) {
	if (edit.dragPoint >= 0 || edit.selectedPoint < 0 || edit.selectedPoint >= *list.count || !ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		return;
	}
	if (*list.count <= list.minCount) {
		return;
	}
	Remember(list);
	ErasePoint(list, edit.selectedPoint);
	Commit(list);
	edit.selectedPoint = -1;
}

//=============================================================================
// 描く
//=============================================================================
// ===== 点の間を通る線（Entity から見た座標の折れ線を、絵の上へ）=====
void DrawCurve(ImDrawList* drawList, const PointList& list, const PointMapping& mapping) {
	ImVec2 previous;
	bool hasPrevious = false;
	for (const Vector3& point : list.curve) {
		ImVec2 pixel;
		const bool isVisible = mapping.toPixel({point.x, point.y}, pixel);
		if (isVisible && hasPrevious) {
			drawList->AddLine(previous, pixel, kEdgeColor, kLineThickness);
		}
		previous = pixel;
		hasPrevious = isVisible;
	}
}

void DrawPoints(const PointList& list, const PointMapping& mapping, const ScreenPoints& screen, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true); // 絵の外（黒い余白）には描かない
	const ImVec2 mouse = ImGui::GetMousePos();
	const int count = screen.count;
	const int edgeCount = EdgeCount(list, count);
	// --- 辺（形がおかしいと赤）。線があるときは、辺は薄い補助線にして、線を太く描く ---
	const bool hasCurve = !list.curve.empty();
	const ImU32 edgeColor = !list.isValid ? kInvalidColor : (hasCurve ? kGuideColor : kEdgeColor);
	const float edgeThickness = hasCurve ? kGuideThickness : kLineThickness;
	for (int i = 0; i < edgeCount; ++i) {
		const int next = (i + 1) % count;
		if (screen.isVisible[i] && screen.isVisible[next]) {
			drawList->AddLine(screen.pixels[i], screen.pixels[next], edgeColor, edgeThickness);
		}
	}
	if (hasCurve) {
		DrawCurve(drawList, list, mapping);
	}
	// --- 辺の真ん中の ＋（点を足せる間だけ。ドラッグ中は出さない）---
	if (CanInsert(list) && edit.dragPoint < 0) {
		const int hoveredPlus = isHovered ? FindHoveredPlus(list, screen, mouse) : -1;
		for (int i = 0; i < edgeCount; ++i) {
			const int next = (i + 1) % count;
			if (!screen.isVisible[i] || !screen.isVisible[next]) {
				continue;
			}
			const ImVec2 center = Midpoint(screen.pixels[i], screen.pixels[next]);
			const ImU32 color = (i == hoveredPlus) ? kActiveColor : kPlusColor;
			drawList->AddLine(ImVec2(center.x - kPlusHalfSize, center.y), ImVec2(center.x + kPlusHalfSize, center.y), color, kLineThickness);
			drawList->AddLine(ImVec2(center.x, center.y - kPlusHalfSize), ImVec2(center.x, center.y + kPlusHalfSize), color, kLineThickness);
		}
	}
	// --- 点（■。選んだ点・マウスが乗っている点はオレンジ）---
	const int hoveredPoint = (isHovered && edit.dragPoint < 0) ? FindHoveredPoint(screen, mouse) : -1;
	for (int i = 0; i < count; ++i) {
		if (!screen.isVisible[i]) {
			continue;
		}
		const ImVec2& p = screen.pixels[i];
		const bool isActive = (i == edit.selectedPoint || i == hoveredPoint);
		drawList->AddRectFilled(ImVec2(p.x - kHandleHalfSize, p.y - kHandleHalfSize), ImVec2(p.x + kHandleHalfSize, p.y + kHandleHalfSize), isActive ? kActiveColor : kPointColor);
	}
	drawList->PopClipRect();
}
} // namespace

//=============================================================================
// 毎フレーム
//=============================================================================
bool PointListGizmo::Draw(Handle<Entity> entity, const PointList& list, const PointMapping& mapping, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	// --- 別の物を渡された・前のフレームに呼ばれなかった（編集をやめていた）：選んだ点などは捨てる ---
	const int frame = ImGui::GetFrameCount();
	if (edit.entity != entity || edit.component != list.component || edit.lastFrame != frame - 1) {
		edit = {};
		edit.entity = entity;
		edit.component = list.component;
	}
	edit.lastFrame = frame;
	// --- キー（Scene ビューを触っている時だけ。文字の入力中は何もしない）---
	const bool isKeyTarget = (isHovered || ImGui::IsWindowFocused()) && !ImGui::GetIO().WantTextInput;
	if (isKeyTarget && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (edit.dragPoint >= 0) {
			Commit(list); // ドラッグ中なら、そこまでを1件にする
		}
		edit = {};
		return false;
	}
	if (isKeyTarget) {
		HandleDelete(list);
	}
	// --- マウス（変えた後の点で描くように、描くための位置は後でもう一度出す）---
	HandleMouse(list, mapping, CalcScreenPoints(list, mapping), isHovered);
	DrawPoints(list, mapping, CalcScreenPoints(list, mapping), imageMin, imageSize, isHovered);
	return true;
}

//=============================================================================
// 点の写し方
//=============================================================================
// ===== 3D の物：Entity の面の上の点を、カメラで写す =====
PointMapping PointListGizmo::MakeCameraMapping(const Matrix4x4& world, const Camera& camera, float planeZ, const ImVec2& imageMin, const ImVec2& imageSize) {
	PointMapping mapping;
	// --- 点 → ワールド（点を動かす面の上）→ カメラで正規化座標 → 絵の上のピクセル ---
	mapping.toPixel = [world, &camera, planeZ, imageMin, imageSize](const Vector2& local, ImVec2& pixel) {
		Vector2 ndc;
		if (!camera.WorldToScreenPoint(TransformPoint({local.x, local.y, planeZ}, world), &ndc)) {
			return false;
		}
		pixel = ToPixel(ndc, imageMin, imageSize);
		return true;
	};
	// --- ピクセルの所から奥へ向かうレイと、点を動かす面が交わる所 → Entity から見た位置 ---
	mapping.toLocal = [world, &camera, planeZ, imageMin, imageSize](const ImVec2& pixel, Vector2& local) {
		const Ray ray = camera.ScreenPointToRay(ToNdc(pixel, imageMin, imageSize));
		// 点を動かす面（向きは Entity の Z 軸。Plane は「法線 · 点 ＝ distance」で表す）
		const Vector3 normal = Normalize(TransformNormal({0.0f, 0.0f, 1.0f}, world));
		if (LengthSq(normal) == 0.0f) {
			return false; // Z の大きさが 0（面の向きが決まらない）
		}
		const Vector3 onPlane = TransformPoint({0.0f, 0.0f, planeZ}, world);
		float t = 0.0f;
		if (!Collision::RayPlane(ray, {normal, Dot(normal, onPlane)}, &t)) {
			return false; // 面と平行・面がカメラの後ろ
		}
		const Vector3 hit = TransformPoint(Collision::RayPoint(ray, t), Inverse(world));
		if (!std::isfinite(hit.x) || !std::isfinite(hit.y)) {
			return false; // 大きさが 0 の物（逆行列が作れない）
		}
		local = {hit.x, hit.y};
		return true;
	};
	return mapping;
}

// ===== 画面に重ねる UI：Image と同じ置き方（SpriteRenderSystem::CalcScreenPoint）で写す =====
PointMapping PointListGizmo::MakeScreenMapping(const Matrix4x4& world, ScreenAnchor anchor, const ImVec2& imageMin, const ImVec2& imageSize) {
	PointMapping mapping;
	mapping.toPixel = [world, anchor, imageMin, imageSize](const Vector2& local, ImVec2& pixel) {
		pixel = ToPixel(SpriteRenderSystem::CalcScreenPoint(world, anchor, local), imageMin, imageSize);
		return true;
	};
	// --- 逆向き：点 (1,0)・(0,1) が画面のどこへ動くか（軸）から、2×2 の連立方程式を解く ---
	// 画面の位置 = 原点 + x × 横の軸 + y × 縦の軸 なので、外積（Cros）で x・y を取り出す（クラメルの公式）
	mapping.toLocal = [world, anchor, imageMin, imageSize](const ImVec2& pixel, Vector2& local) {
		const Vector2 origin = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 0.0f});
		const Vector2 axisX = SpriteRenderSystem::CalcScreenPoint(world, anchor, {1.0f, 0.0f}) - origin;
		const Vector2 axisY = SpriteRenderSystem::CalcScreenPoint(world, anchor, {0.0f, 1.0f}) - origin;
		const float determinant = Cros(axisX, axisY);
		if (std::abs(determinant) < kMinDeterminant) {
			return false; // 大きさが 0（向きが潰れている）
		}
		const Vector2 offset = ToNdc(pixel, imageMin, imageSize) - origin;
		local = {Cros(offset, axisY) / determinant, Cros(axisX, offset) / determinant};
		return true;
	};
	return mapping;
}
#endif
```

### C-3 `Editor/Gizmos/PolygonShapeGizmo.cpp`

`list.planeZ = ...` の行を消し、`Draw` の呼び出しを差し替える。

```cpp
	list.isClosed = true;
	list.isValid = Polygon2D::IsSimple(shape->GetPoints());
	list.editor = ComponentEditorRegistry::Find<PolygonShapeComponent>();
	list.component = shape;
	const float planeZ = -std::abs(shape->depth) * 0.5f;
	const PointMapping mapping = PointListGizmo::MakeCameraMapping(transform->worldMatrix, camera, planeZ, imageMin, imageSize);
	if (!PointListGizmo::Draw(selected, list, mapping, imageMin, imageSize, isHovered)) {
```

### C-4 `Editor/Gizmos/ArrowStripGizmo.cpp`

同じく `list.planeZ = 0.0f;` を消し、`Draw` の呼び出しを差し替える。

```cpp
	list.isClosed = false;
	list.curve = preview.GetPolyline();
	list.editor = ComponentEditorRegistry::Find<ArrowStripComponent>();
	list.component = strip;
	const PointMapping mapping = PointListGizmo::MakeCameraMapping(transform->worldMatrix, camera, 0.0f, imageMin, imageSize);
	if (!PointListGizmo::Draw(selected, list, mapping, imageMin, imageSize, isHovered)) {
```

### C-5 `Editor/Gizmos/PolylineGizmo.h`（新しいファイル）

```cpp
#pragma once
#include <externals/imgui/imgui.h>


/// <summary>
/// Scene ビューで、Polyline の点を動かす・足す・消す（Inspector の Edit Points を押している間だけ）
/// <para>動かすのは、選んでいる Entity の点。点は画面のピクセルで動かす（1 ピクセルに吸い付く。Ctrl で吸い付かない）</para>
/// <para>操作・履歴は PointListGizmo（Polygon Shape・Arrow Strip と共通）。ここは Polyline から「動かす点」と「画面の写し方」を作って渡すだけ</para>
/// <para>点を足すと、角の半径はとなりの2つの真ん中の値になる。2 点より少なくはできない</para>
/// </summary>
class PolylineGizmo {
public:
	/// <summary>
	/// Scene ビューの絵の上に描いて、マウスで編集する。SceneGizmo::Draw から毎フレーム1回
	/// </summary>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	/// <returns>点を編集しているか（true なら、SceneGizmo は移動のギズモ・UI の移動・クリックで選び直すのをしない）</returns>
	static bool Draw(const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);
};
```

### C-6 `Editor/Gizmos/PolylineGizmo.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "PolylineGizmo.h"

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Gizmos/PointListGizmo.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Math/Geometry/Polygon.h"
#include "HTTEngine/Systems/UI/PolylineSystem.h"

namespace {
constexpr float kGridSize = 1.0f; // 点が吸い付く間隔（ピクセル。Inspector の数字が 150.37 のようにならないように）
} // namespace

//=============================================================================
// 毎フレーム
//=============================================================================
bool PolylineGizmo::Draw(const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered) {
	if (!PolylineSystem::IsEditing()) {
		return false;
	}
	// --- 選んでいる物に Polyline が無ければ、編集を終わる（選ぶのをやめた・ほかの物を選んだ）---
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	PolylineComponent* polyline = EntityManager::Get<PolylineComponent>(selected);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(selected);
	if (polyline == nullptr || transform == nullptr) {
		PolylineSystem::SetEditing(false);
		return false;
	}
	// --- 点と角の半径（足す・消すと、半径も一緒にずれる）。塗る形で辺が交差していたら赤（塗りが描かれない形）---
	PointList list;
	list.storage = polyline->points;
	list.values = polyline->radii;
	list.count = &polyline->pointCount;
	list.minCount = kMinPolylinePoints;
	list.isClosed = polyline->closed;
	list.gridSize = kGridSize;
	list.isValid = !polyline->HasFill() || Polygon2D::IsSimple({polyline->points.data(), static_cast<size_t>(polyline->pointCount)});
	list.editor = ComponentEditorRegistry::Find<PolylineComponent>();
	list.component = polyline;
	const PointMapping mapping = PointListGizmo::MakeScreenMapping(transform->worldMatrix, polyline->anchor, imageMin, imageSize);
	if (!PointListGizmo::Draw(selected, list, mapping, imageMin, imageSize, isHovered)) {
		PolylineSystem::SetEditing(false); // Esc で終わった
		return false;
	}
	return true;
}
#endif
```

### C-7 `Editor/Gizmos/SceneGizmo.cpp`

include に `#include "HTTEngine/Editor/Gizmos/PolylineGizmo.h"` を足し（`PolygonShapeGizmo.h` の下）、`Draw` の編集中の所を差し替える。

```cpp
	// 形・線の編集中（Polygon Shape の Edit Shape・Arrow Strip の Edit Path・Polyline の Edit Points）は、点を動かすのがマウスの仕事。移動のギズモ・クリックで選び直すのはしない
	// || は左から調べて true で止まるので、2つの編集が同じフレームに動くことはない
	if (PolygonShapeGizmo::Draw(camera, imageMin, imageSize, isHovered) || ArrowStripGizmo::Draw(camera, imageMin, imageSize, isHovered) || PolylineGizmo::Draw(imageMin, imageSize, isHovered)) {
		return;
	}
```

### C-8 プロジェクトに足す

- `Editor/Gizmos/PolylineGizmo.h`・`PolylineGizmo.cpp`（フィルタは `HTTEngine\Editor\Gizmos`）

---

## パートD 見本（ゲーム側）

折れ線の形をいろいろ並べたシーンと、波の点を Timeline で動かす見本。エンジンには入れない。`Docs/Tasks/PolylineTest/` にある物を AL4_01 へ写す。

| Docs/Tasks/PolylineTest/ | AL4_01 の写し先 |
|---|---|
| `scenes/PolylineTest.scene.json` | `project/Resources/scenes/` |
| `timelines/PolylineTest/PolylineWave.timeline` | `project/Resources/timelines/PolylineTest/` |

Debug・Development は `Parameters/Editor/LastScene.json` を `PolylineTest` にするか、Project で開く（Play で波が動く）。Release は `main.cpp` の `Engine::Initialize(config, "GameScene")` を一時的に `"PolylineTest"` にする。

| 名前 | 中身 |
|---|---|
| A_Thin | 細い線（4）、半径 0、端 Round |
| B_ThickSharp | 太い線（24）、半径 0（外は丸く、内はとがる）、端 Butt |
| C_ThickRound | 太い線（24）、半径 30、端 Square |
| D_Wave | 波（半径 40）。Timeline で点 1〜5 の y が動く |
| E_RoundRect | 閉じた四角（半径 20）、塗りと線 |
| F_StarFill | 星（半径 0）、塗りだけ |
| G_StarStroke | 星（半径 8）、塗りと半透明の線（重なりがあれば濃く見える） |
| H_BlobRot | へこんだ形（半径 30）。回転 20 度・Scale (1.3, 0.8) |
| I_Hairpin | 折り返す線（太さ 20） |
| J_ThickFolded | 太い線（40）・小さい半径（10）・半透明（1.3 の内側の分け方） |
| K_Spikes | 鋭いギザギザ（太さ 16・半透明） |
| L_WaveFillGroup | 波の形の塗りと線、Alpha Group 0.5 |

`PolylineWave.timeline` は Value トラック5本（Component `Polyline`、Property `point1`〜`point5`、y だけにキー）。Timeline ウィンドウで開くと、トラックの名前が `Point 1`〜`Point 5`。

---

## E 確認

### E-1 ビルド

Debug・Development・Release をビルドして、エラーと（今回写したファイルの）警告が 0 か見る。

### E-2 動かして見る

| # | すること | 見る所 |
|---|---|---|
| 1 | PolylineTest を開く | 12 個の形が D 章の表のとおりに出る。縁がなめらか |
| 2 | B_ThickSharp の Stroke Width を 2 → 80 までドラッグ | 角の外は丸いまま、内はとがったまま。途中で穴・濃い所が出ない |
| 3 | C_ThickRound の Radius 1〜3 を 0 → 200 までドラッグ | 角が丸くなっていき、辺に収まらない所で止まる（形が壊れない） |
| 4 | Cap を Butt・Round・Square に切り替える | 端の形が変わる。Closed にすると灰色になる |
| 5 | G_StarStroke の線（半透明）をよく見る | 角と直線の境目に、線や濃い点が出ない |
| 6 | H_BlobRot を回す・Scale を変える | 形がそのまま回る・伸びる。角の丸みは横に伸ばしてもつぶれない |
| 7 | Play | D_Wave の波が動く。Stop で元の形に戻る |
| 8 | D_Wave を選んで Edit Points | ■ と辺の真ん中の ＋ が出る |
| 9 | ■ をドラッグ・＋ を押す・選んで Delete・Ctrl+Z・Esc | 点が動く（1 ピクセルずつ）・増える（半径はとなりの真ん中）・減る・1回ずつ戻る・編集が終わる |
| 10 | Polygon Shape の Edit Shape・Arrow Strip の Edit Path | 今までどおり動く（点の写し方を差し替えた所） |
| 11 | Hierarchy の ＋ → Polyline | 波の線が1本できる |
| 12 | Timeline ウィンドウで Add Track → Value、Hierarchy から D_Wave を行へドラッグし、Component を Polyline、Property を point2 | point2 の x・y のカーブが出る。キーを打つと点が動く |
| 13 | 保存して開き直す | 点・半径・閉じる・塗り・端の形が残る |

---

## F 検証の結果（写しで確かめた）

写し（AL4_01 の写しのサブモジュールを 0b7ca68 にそろえた物）に、答えのコードを入れて確かめた。CPU は Core i7-13620H、GPU は GeForce RTX 4060 Laptop（エンジンは性能の高い方を選ぶ）。

| 確かめたこと | 結果 |
|---|---|
| Debug・Development・Release のビルド | エラー 0、警告 0（パートA〜C を全部入れた状態。A だけ・B までの途中は確かめていない） |
| 見本の絵（Release） | 12 個とも形どおり。半透明の線（G・J・K）を拡大して、継ぎ目の線・濃い点が無い |
| 太さを 1/4 倍・3 倍にした絵 | 細い線（1 ピクセル）もなめらか。3 倍でも角が崩れない。J（太さ 120）は線どうしが重なる形なので、重なった所だけ濃い（0.5 の表のとおり） |
| Timeline で点を動かした記録（240 フレーム、時間を 1/60 秒に固定） | 全部の折れ線の点と、頂点の数が Debug と Release で全行一致 |
| エディタ（Debug、ImGui へ入力を流した） | Edit Points → ■ をドラッグ（-2, 99 に 1 ピクセルずつ）→ ＋ で点を足す（真ん中・半径 40）→ Delete → Ctrl+Z 3回で元の形 → Esc で終わる。保存の JSON で値を確かめた |
| 耳切り法の差し替え | 2万通りの多角形で、前の関数と結果（分けられたか・面積）が全部同じ |

### F-1 速さ（作り直しの計測）

100 本の閉じた折れ線（各 32 点・半径 6・塗りと線、合わせて 83,600 頂点）を、600 フレームずつ3通りに動かして、PolylineSystem::Draw にかかった時間（最初の 60 フレームを除いた平均）と、GPU の 2D の時間を測った。

| 動かし方 | 作り直し / フレーム | Release の CPU | Debug の CPU | Release の GPU（2D） |
|---|---|---|---|---|
| 毎フレーム全部の点を動かす | 100 | 4.19 ms（最大 13.2） | 33.9 ms | 0.27 ms |
| 何も変えない | 0 | 0.13 ms | 0.67 ms | 0.27 ms |
| Transform だけ動かす（位置） | 0 | 0.14 ms | 0.64 ms | 0.28 ms |

- 1 本（32 点・塗りと線）を作り直すのは、Release で約 40 マイクロ秒、Debug で約 340 マイクロ秒。点・半径・太さを毎フレーム動かす折れ線が 10 本なら、Release で 0.4 ms。
- 位置・回転・色・透明度だけを動かすなら、作り直さない（0 回）。
- UIMask.md の見本（4 本を毎フレーム作り直す）は Release 0.09 ms、Debug 0.32 ms。Polyline.md の見本（1 本を毎フレーム作り直す）は Release 0.03 ms、Debug 0.22 ms。
- Debug の Scene・Game ビューは2回描くので、GPU は約 3 ms（2 ビュー分）。

**計測の途中で直した物：**

- 塗りが出ない形があった：へこんだ角の帯の外側の両端の点（1.08 倍の所）が、となりのとがった角の点を越え、内側の輪が交差して耳切り法が失敗していた。両端だけちょうど `r + 2` に置いた（1.5）。
- 作り直しが遅かった：耳切り法（1 本 186 マイクロ秒、Debug）と、`push_back` を1つずつ呼ぶ所が重かった。耳切り法を速くし（1.5）、配列は `resize` してからまとめて書くようにした。Debug で 1 本 452 → 226 マイクロ秒（単体で測った値）。
- 角の向きを回すたびに cos・sin を呼んでいたのを、1回だけ求めて回す計算を繰り返す形にした。

**確かめていない物：**

- 本物のマウスでのギズモの操作（E-2 の 8〜9）。写しでは ImGui へ入力を流した。
- Polygon Shape の Edit Shape・Arrow Strip の Edit Path の操作（写し方を差し替えた所。コンパイルと、関数の中身が前と同じ計算になっていることを読んだだけ）。
- Timeline ウィンドウで Polyline の項目を選ぶ操作（E-2 の 12。見本の .timeline を開いて、トラックとキーが出るのは見た）。
- AL4_01 の実物での実行。
