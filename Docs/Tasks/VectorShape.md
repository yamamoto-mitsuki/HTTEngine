参考：
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- 角丸四角形・平行四辺形の距離の式（Inigo Quilez, 2D distance functions）：https://iquilezles.org/articles/distfunctions2d/
- 楕円の距離の考え方（Inigo Quilez, distance to an ellipse）：https://iquilezles.org/articles/ellipsedist
- 楕円の距離のくり返しの方法（Simple Method for Distance to Ellipse）：https://blog.chatfield.io/simple-method-for-distance-to-ellipse/
- HLSL の `ddx`：https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-ddx
- HLSL の `ddy`：https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-ddy
- `std::hypot`：https://en.cppreference.com/w/cpp/numeric/math/hypot
- Dear ImGui の `ImGui::BeginDisabled`：https://github.com/ocornut/imgui

# UI 段階1：ベクター図形（Vector Shape）

テクスチャを使わず、シェーダーで UI の図形を描く。四角の板（Quad）1枚の上で、ピクセルシェーダーが図形の距離（SDF）を計算する。メッシュは使わない。

作る図形は、四角形・角丸四角形・円・楕円・リング・平行四辺形の6種類。塗り・縁取り・線だけ・グラデーション・ゲージ（直線と円形）を持つ。

パートは **A → B → C の順に写す**。途中で止めてもよい（A の後、B の後でビルドが通ることを確かめた）。D は確認の手順。

作業の前に、develop からブランチを切る。

```bash
git switch -c feature/VectorShape develop
```

## 0章 全体の形

### 0.1 流れ

```
VectorShapeComponent（Size・色・縁取り・ゲージ。毎フレーム読むだけ）
   │  VectorShapeSystem::Draw：Order の順に並べて、値をピクセルの単位に直す
   ▼
Renderer::DrawVectorShape：色をリニアにして、頂点4つを作る
   │
   ▼
RenderQueue の 2D の列（Sprite・Text と同じ列。Order で混ざる）
   │  Flush2d
   ▼
RenderContext::DrawVectorShape：定数と頂点を FrameUploadBuffer に書いて、板を1枚描く
   │
   ▼
Sprite2d.VS → VectorShape.PS：1ピクセルずつ、輪郭までの距離を計算して色を決める
```

### 0.2 Image を拡張せず、新しい Component にした理由

| 見たこと | 判断 |
|---|---|
| `ImageComponent` の項目の半分（`textureHandle`・`columns`・`rows`・`frame`・`useRect`・`rect〜`・`flip〜`）は、画像が無いと意味が無い | 図形の項目を足すと、1つの Inspector に「画像だけの項目」と「図形だけの項目」が混ざる |
| 図形の項目は 20 以上ある（形・縁取り・グラデーション・ゲージ） | Image は保存データも Undo の写しも大きくなる。今の Image を使っているシーンには要らない値 |
| Image の描き方は `Sprite2d.PS`（テクスチャを読む）で、図形は別のシェーダーになる | 中で分岐するより、Component ごとシェーダーを分ける方が素直 |
| 共通の値は `size`・`anchor`・`pivot`・`order`・`blend`・`color` だけ | `ScreenAnchor` は同じ型を使い、`CalcScreenPoint`・`CalcScreenQuad`・`CalcGroupAlpha` もそのまま呼べる |

なので `VectorShapeComponent`（カテゴリ UI・保存名 `Vector Shape`）を新しく作る。Text と同じ「Image の隣にある UI の部品」という扱い。

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| シェーダーの入力 | 頂点の `texcoord` に「図形の真ん中を原点にしたピクセルの場所」を入れる。VS は `Sprite2dVS` をそのまま使う | 新しい頂点の形も VS も要らない |
| 板の大きさ | Size の四角より、縁取りの外の分＋2ピクセル広げる | 輪郭のなめらかな所は、輪郭の外にも半ピクセルはみ出す。板の端で切れないように |
| Scale を掛けたとき | Size は x・y それぞれ、角の丸み・縁取り・リングの太さは x・y の小さい方の倍率 | 全体を拡大すれば全部一緒に大きくなる。横だけ伸ばしても丸みと太さはゆがまない |
| 縁のなめらかさ | `ddx`・`ddy` で「1ピクセル分の距離の変化」を測る | 拡大・回転・ウィンドウの大きさが変わっても、いつも1ピクセル幅になる |
| 楕円 | くり返しで正確な距離を出す（4回） | 近い式（勾配で割る方法）は、細い楕円で縁取りの太さが場所で変わる |
| 平行四辺形の大きさ | Size の四角にちょうど入るように作る | Pivot・Anchor・UI の枠が、見た目と合う |
| 平行四辺形の傾きの上限 | 上の辺が幅の 1 割は残る | 傾けすぎて図形が消えないように |
| 色 | Inspector は sRGB、シェーダーはリニア（ほかの 2D と同じ） | グラデーションもリニアで混ぜる |
| ゲージ | 塗りも縁取りも一緒に切る。背景の枠は別の Vector Shape を後ろに置く | 線だけのリングもゲージにできる |
| 平行四辺形の横のゲージ | 切り口を斜めの辺と平行にする | ゲージの先だけ垂直になると形が崩れて見える |
| Inspector の効かない項目 | 隠さず灰色にする（`ui.BeginDisabled`） | 隠すと保存からも落ちて、形を切り替えたときに値が消える。段階5でも全部の項目に届く |
| 大きさの変更（UI ギズモ） | Vector Shape を選ぶと枠に印が8つ出る。Image・Text は今までどおり移動だけ | この段階の頼みは図形の編集なので、Image は変えない |
| 同じ Order のとき | Image → Vector Shape → Text の順に重なる。同じ種類の中は先に作った物が奥 | `EngineSystems::Draw` の呼ぶ順のまま。違う種類を重ねるときは Order を分ける |
| マウスの当たり（`IsMouseOver`） | Size の四角で調べる（円の角の所も上と数える） | Image と同じ。形どおりに調べるのは要るときに |

### 0.4 あとの段階への備え

| 段階 | 今の形で残した物 |
|---|---|
| UI 段階5（アニメーション） | 値は全部 Component のふつうの `float`・`Vector2`・`Vector4`。作り置きの値が無いので、外から書くだけで次のフレームの絵が変わる。`DescribeVectorShape` は全部の項目をいつも通るので、`###gaugeAmount` のような名前から値の場所を探す仕組みを、あとで `ComponentUI` の係として足せる |
| UI 段階6（折れ線・マスク・模様） | 図形の種類は `VectorShapeType` と `CalcDistance` の `switch` に足す形。折れ線は頂点の数が変わるので別の Component になる見込みで、2D の列（`Request2d`）に種類を足す形はここと同じ |

値を毎フレーム変えても重い処理が無いのは、次の理由。

- `VectorShapeSystem::Draw` は Component を読んで、ローカル変数の `VectorShapeMaterialData` に値を入れるだけ。
- `RenderContext::DrawVectorShape` は `PushConstant`・`PushArray` で、作ってある 64MB の `FrameUploadBuffer` の続きに書くだけ。リソースは作らない。
- 形が変わっても PSO は変わらない。PSO が変わるのは Blend を変えたときだけで、それも1回作れば `PSOManager` が覚えている。

## 1章 解説

### 1.1 SDF（距離の関数）

SDF は「この場所から図形の輪郭まで、どれだけ離れているか」を返す関数。外がプラス、内がマイナス、輪郭がちょうど 0。

円なら `length(p) - 半径` の1行で書ける。

```
d = -30  図形の奥        ┌─── 輪郭 d = 0 ───┐
d =  -1  輪郭のすぐ内側  │                  │
d =  +1  輪郭のすぐ外側  └──────────────────┘
```

距離なので、`d` に足し引きするだけで形を変えられる。

| やりたいこと | 式 |
|---|---|
| 8 ピクセル太らせる | `d - 8` |
| 角を丸める | 形を半径の分だけ小さくして、`d - 半径` |
| 輪郭に沿った太さ `w` の線 | `abs(d + w / 2) - w / 2` |

テクスチャと違って、距離は描くときに毎回計算する。拡大しても元の絵の解像度が無いので、ぼやけない。Text の SDF は C++ が距離をテクスチャに1回焼いて読むが、Vector Shape は焼かずに式で出す。

### 1.2 輪郭をなめらかにする（Coverage）

`ddx(d)`・`ddy(d)` は、隣のピクセルとの `d` の差を返す命令（ピクセルシェーダーでだけ使える）。`length(float2(ddx(d), ddy(d)))` が「画面の1ピクセル進むと `d` がいくつ変わるか」になる。

```hlsl
float32_t pixelWidth = max(length(float32_t2(ddx(d), ddy(d))), 1e-5f);
return saturate(0.5f - d / pixelWidth);
```

`d / pixelWidth` は「輪郭まで何ピクセルか」。`0.5 - それ` を 0〜1 に収めると、ピクセルの真ん中が輪郭の上なら 0.5、半ピクセル内側なら 1、半ピクセル外側なら 0 になる。拡大すると `pixelWidth` が小さくなるので、大きさによらずなめらかな幅がいつも1ピクセルになる。

Text は `fwidth`（`abs(ddx) + abs(ddy)`）を使っているが、これは斜めの輪郭で 1.4 倍くらい広くなる。図形は `length` にした。

### 1.3 Scale と Size

```
Component の Size（100, 50） × Transform の scale（2, 1）
  → シェーダーに渡す図形の半分の大きさ：(100, 25)
  → 角の丸み・縁取りに掛ける倍率：min(2, 1) = 1
```

シェーダーは「描かれるピクセル」の単位だけを見る。だから Size を変えても、Scale を変えても、計算が同じ形になる。板を引き伸ばして絵を伸ばしているのではなく、伸ばした後の大きさで形を計算し直しているので、丸みが楕円にならない。

`std::hypot(x, y)` は `sqrt(x * x + y * y)` と同じ長さを、途中のあふれに強い形で出す標準ライブラリの関数。`m[0][0]`・`m[0][1]` は行列の x 軸なので、その長さが x の Scale になる（回っていても測れる）。

### 1.4 楕円

楕円の正確な距離は、式1つでは出ない。一番近い楕円の上の点を、くり返しで探す。

1. 楕円の上の点 `onEllipse` と、その点の曲率の中心 `evolute`（その点に一番合う円の中心）を出す。
2. `evolute` を中心にした円で楕円を近似して、`p` を円の上へ写す。
3. 写した点の向きを新しい `t` にする。

Perl に写して総当たりの距離と比べた結果、170×100 で最大 0.001 ピクセル、400×20 で最大 0.003 ピクセルの差だった。

### 1.5 平行四辺形の角丸

`SdParallelogram(...) - 半径` は、形を外へ半径の分だけ太らせて角を丸くする。太らせた分だけ Size の四角からはみ出すので、先に同じ分だけ内側へ縮めておく。

- 縦：`half.y - 半径`
- 横：斜めの辺を辺に垂直に半径の分だけ動かすには、横へ `半径 / cos(傾き)` 動かす必要がある

この計算が `WriteShapeParams` の `Parallelogram` の所。

### 1.6 ゲージの切り口

切り口も距離で作る。線の法線 `n` を決めると、`dot(p, n)` がその線からの距離（に比例する値）になる。図形の輪郭と同じ `Coverage` に通すので、切り口もなめらかになる。

- 直線：線は1本。平行四辺形の横のゲージは、`axis = (1, -傾き)` にして切り口を斜めの辺と平行にする。
- 扇形：始まりの線と終わりの線の2本。180 度より狭ければ「両方の内側」（`max`）、広ければ「どちらかの内側」（`min`）。

### 1.7 塗りと縁取りを混ぜる

透明度の違う2色をそのまま `lerp` すると、縁の境目が暗くなる。色に透明度を掛けた形（premultiplied）で足してから、最後に透明度で割り戻す。

```hlsl
float32_t alpha = fillAlpha + strokeAlpha;
float32_t3 rgb = (paint.rgb * fillAlpha + stroke.rgb * strokeAlpha) / max(alpha, 1e-5f);
```

### 1.8 重ね方ごとの直し

板は図形より広いので、図形の外（`alpha` が 0 のピクセル）が背景を変えないようにする。`Normal`・`Add`・`Subtract` は `SrcAlpha` を見るので何もしなくてよいが、`alpha` を見ない重ね方は色で直す。

| Blend | 直し方 |
|---|---|
| `Multiply` | 外を白にする（掛けても変わらない） |
| `Screen` | 外を黒にする（足しても変わらない） |
| `None` | 混ぜられないので、半分より外を `clip` で捨てる（縁のなめらかさは無くなる） |

### 1.9 ImGui::BeginDisabled

`ImGui::BeginDisabled(true)` と `ImGui::EndDisabled()` の間の項目は、灰色になって触れなくなる。`false` を渡すと何も変わらないので、`ui.BeginDisabled(条件)` と1行で書ける。

保存の係は `ComponentUI` の何もしない既定の関数を使うので、灰色の項目も保存・読み込みされる。

---

## パートA 描画の土台

ここを写すと、図形を描く通り道ができる（まだ呼ぶ所は無い）。

### A-1 `Graphics/Pipeline/RenderStates.h`

`DrawCategory` に足す。

変更後：

```cpp
	Text,
	VectorShape, // 画像を使わない UI の図形（Sprite・Text と同じく、2D の最後に重ねる）
};
```

### A-2 `Graphics/Pipeline/PSOManager.cpp`

`GetTargetFormat` に足す（2D なので SDR）。

変更後：

```cpp
	case DrawCategory::Sprite:
	case DrawCategory::Text:
	case DrawCategory::VectorShape:
		return RenderTextureFormat::SDR;
```

### A-3 `Graphics/Pipeline/ShaderConstants.h`

`TextMaterialData` の下、「パーティクルのマテリアル」の前に足す。

変更後：

```cpp
// 画像を使わない UI の図形のマテリアル（VectorShape/VectorShape.PS の Material と同じ並び。長さは全部 描かれるピクセル）
struct VectorShapeMaterialData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};         // 塗りの色（グラデーションの始まりの色）
	Vector4 gradientColor = {1.0f, 1.0f, 1.0f, 1.0f}; // グラデーションの終わりの色
	Vector4 strokeColor = {0.0f, 0.0f, 0.0f, 1.0f};   // 縁取りの色
	Vector4 cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f};  // 角の半径（x=左上 y=右上 z=右下 w=左下）
	Vector2 shapeHalf = {0.0f, 0.0f};                 // 図形の半分の大きさ（平行四辺形は、角を丸める前の形の真ん中の幅の半分と、高さの半分）
	Vector2 shapeParam = {0.0f, 0.0f};                // 円：x=半径 / リング：x=太さの真ん中の半径 y=太さの半分 / 平行四辺形：x=上の辺の横のずれ y=角の半径
	Vector2 gradientAxis = {0.0f, 0.0f};              // グラデーションの軸（直線：dot(位置, 軸) + 0.5 / 放射：length(位置 × 軸)）
	float gradientStart = 0.0f;                       // 色が変わり始める場所
	float gradientScale = 1.0f;                       // 1 ÷ 色が変わる範囲
	float strokeInner = 0.0f;                         // 輪郭からの距離がこれより内側が塗り（距離は内側がマイナス）
	float strokeOuter = 0.0f;                         // 輪郭からの距離がこれより内側が図形全体（縁取りを含む）
	float fillOpacity = 1.0f;                         // 塗りを出すか（0 か 1）
	float gaugeOffset = 0.0f;                         // 直線のゲージの切り口の場所
	Vector2 gaugeAxisA = {0.0f, 0.0f};                // ゲージの線の法線 A
	Vector2 gaugeAxisB = {0.0f, 0.0f};                // ゲージの線の法線 B（扇形の終わりの線）
	uint32_t shape = 0;                               // 図形の種類（VectorShapeType の順）
	uint32_t gradientType = 0;                        // グラデーションの種類（VectorShapeGradient の順）
	uint32_t gaugeMode = 0;                           // 0=切らない 1=直線 2=180 度より狭い扇形 3=180 度以上の扇形
	uint32_t blendMode = 0;                           // 重ね方（BlendMode の順）
	uint32_t strokeUsesGradient = 0;                  // 1：縁取りにも塗りのグラデーションを使う
	float padA[3] = {};
};
static_assert(sizeof(VectorShapeMaterialData) == 160, "HLSLのMaterial（VectorShape.PS）と大きさが違います");
```

### A-4 `Graphics/Renderer/DrawRequest.h`

`TextRequest` の下、`// ===== Line =====` の前に足す。

変更後：

```cpp
// ===== 2Dの図形（画像を使わない UI の図形）=====
struct VectorShapeRequest : DrawTarget {
	// 重ねる順（SpriteRequest の order と同じ）
	int order = 0;

	// --- ジオメトリ（左下・左上・右下・右上の順。位置は正規化座標、texcoord は図形の真ん中を原点にしたピクセル）---
	std::array<Vertex2dData, 4> vertices;

	// --- Shaderとのバインド情報 ---
	VectorShapeMaterialData materialData;

	// --- 描画設定 ---
	BlendMode blendMode = BlendMode::Normal;
};
```

### A-5 `Graphics/Renderer/RenderQueue.h`

2か所。発行のコメントも合わせて直す。

変更前：

```cpp
	// 2Dのリクエスト1つ（スプライトか文字のどちらか
	using Request2d = std::variant<SpriteRequest, TextRequest>;
```

変更後：

```cpp
	// 2Dのリクエスト1つ（スプライト・文字・図形のどれか）
	using Request2d = std::variant<SpriteRequest, TextRequest, VectorShapeRequest>;
```

変更後：

```cpp
	static void Request(TextRequest&& req);
	static void Request(VectorShapeRequest&& req);
```

変更後：

```cpp
	// ---  発行（2Dはスプライト・文字・図形を、order の小さい順に混ぜて描く）---
```

### A-6 `Graphics/Renderer/RenderQueue.cpp`

リクエストを足す。`OrderOf`・`TargetOf` は `std::visit` なので、書き換えなくても新しい種類に効く。

変更後：

```cpp
// ===== 図形リクエスト（スプライトと同じ列に積む）=====
void RenderQueue::Request(VectorShapeRequest&& req) { instance_->requests2d_.emplace_back(std::move(req)); }
```

`Flush2d` の文字の所に `continue` を足して、図形の分岐を足す。

変更後：

```cpp
		// --- 文字（両面を描く。回して裏返っても消えないように）---
		if (const TextRequest* text = std::get_if<TextRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::Text, text->blendMode, RasterizerType::SolidNone);
			RenderContext::DrawText2d(*text);
			continue;
		}
		// --- 図形（両面を描く。回して裏返っても消えないように）---
		if (const VectorShapeRequest* shape = std::get_if<VectorShapeRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::VectorShape, shape->blendMode, RasterizerType::SolidNone);
			RenderContext::DrawVectorShape(*shape);
		}
```

コメントを3か所直す。

変更後：

```cpp
	DrawCategory category = DrawCategory::Sprite;      // シェーダー（スプライト・文字・図形）
```

変更後：

```cpp
// 2Dのリクエストの描き分けの情報（どの種類も DrawTarget を受け継ぐので、OrderOf と同じく std::visit で1つにする）
```

変更後：

```cpp
// 描く物に合わせて RootSignature・PSO をセットする（スプライト・文字・図形が交互に来ても、変わった所だけセットし直す）
```

### A-7 `Graphics/Renderer/RenderContext.h`

変更後：

```cpp
	static void DrawText2d(const TextRequest& req);
	// 2Dの図形を描く（四角の板1枚。形はシェーダーが計算する）
	static void DrawVectorShape(const VectorShapeRequest& req);
```

### A-8 `Graphics/Renderer/RenderContext.cpp`

スプライトと図形で同じになる「板1枚をセットする」所を、関数に分ける。`BindConstantIfUsed` の前に足す。

変更後：

```cpp
// 2D の四角の板1枚（頂点4つ）を、このフレーム用のバッファに書いてセットする。戻り値はインデックスの数（スプライトと図形で同じ）
static UINT BindQuad2d(ID3D12GraphicsCommandList* cmdList, FrameUploadBuffer& upload, const std::array<Vertex2dData, 4>& quad) {
	static constexpr std::array<uint32_t, 6> kIndices = {0, 1, 2, 1, 3, 2};
	FrameUploadBuffer::Allocation vertices = upload.PushArray(quad.data(), quad.size());
	FrameUploadBuffer::Allocation indices = upload.PushArray(kIndices.data(), kIndices.size());
	// VertexBufferView
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertices.gpuAddress;
	vbv.SizeInBytes = static_cast<UINT>(vertices.size);
	vbv.StrideInBytes = sizeof(Vertex2dData);
	// IndexBufferView
	D3D12_INDEX_BUFFER_VIEW ibv{};
	ibv.BufferLocation = indices.gpuAddress;
	ibv.SizeInBytes = static_cast<UINT>(indices.size);
	ibv.Format = DXGI_FORMAT_R32_UINT;

	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	return static_cast<UINT>(kIndices.size());
}
```

`DrawSprite` 全体をこの形にする。

変更後：

```cpp
void RenderContext::DrawSprite(const SpriteRequest& req) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== このフレーム用のバッファに書く =====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	const UINT indexCount = BindQuad2d(cmdList, upload, req.vertices);

	// ===== ShaderConstantsバインド =====
	// RootSignatureID
	uint32_t prog = PSOManager::GetShaderProgramID(DrawCategory::Sprite);
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(prog);
	// Material
	cmdList->SetGraphicsRootConstantBufferView(rs.slotOf.at(RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
```

Line3D描画の見出しの前に足す。

変更後：

```cpp
//=============================================================================
// 2Dの図形描画
//=============================================================================
void RenderContext::DrawVectorShape(const VectorShapeRequest& req) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== このフレーム用のバッファに書く（毎フレーム値が変わっても、確保済みのバッファの続きに書くだけ）=====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	const UINT indexCount = BindQuad2d(cmdList, upload, req.vertices);

	// ===== ShaderConstantsバインド =====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::VectorShape));
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
```

### A-9 `Graphics/Renderer/Renderer.h`

`Text2dConfig` の下、Line の見出しの前に足す。

変更後：

```cpp
	//=============================================================================
	// VectorShape
	//=============================================================================
	// --- 画像を使わない UI の図形（値を決めるのは VectorShapeSystem。形はシェーダーが計算する）---
	struct VectorShapeConfig : DrawTarget {
		Vector2 lb;                              // 板の左下の座標（正規化座標）
		Vector2 lt;                              // 板の左上の座標
		Vector2 rb;                              // 板の右下の座標
		Vector2 rt;                              // 板の右上の座標
		Vector2 localHalf;                       // 板の半分の大きさ（描かれるピクセル。シェーダーが読む位置の範囲）
		VectorShapeMaterialData material;        // 図形の値（色は sRGB。中でリニアに直す）
		BlendMode blendMode = BlendMode::Normal; // ブレンド設定
		int order = 0;                           // 重ねる順（スプライトの order と同じ）
	};
```

`DrawText2d` の宣言の下に足す。

変更後：

```cpp
	/// <summary>
	/// 画像を使わない UI の図形の描画リクエストを追加する
	/// </summary>
	static void DrawVectorShape(const VectorShapeConfig& config);
```

### A-10 `Graphics/Renderer/Renderer.cpp`

`// ===== Line =====` の前に足す。

変更後：

```cpp
// ===== VectorShape =====
void Renderer::DrawVectorShape(const VectorShapeConfig& config) {
	VectorShapeRequest req;
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
	req.order = config.order;
	req.blendMode = config.blendMode;
	req.materialData = config.material;
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（A はそのまま）
	req.materialData.color = SrgbToLinear(config.material.color);
	req.materialData.gradientColor = SrgbToLinear(config.material.gradientColor);
	req.materialData.strokeColor = SrgbToLinear(config.material.strokeColor);
	// 頂点：texcoord に図形の真ん中を原点にした場所を入れる（Sprite2d.VS がそのまま PS へ運ぶ。板の中で直線で補間される）
	const Vector2 half = config.localHalf;
	req.vertices[0] = {{config.lb.x, config.lb.y, 0.0f, 1.0f}, {-half.x, -half.y}};
	req.vertices[1] = {{config.lt.x, config.lt.y, 0.0f, 1.0f}, {-half.x, +half.y}};
	req.vertices[2] = {{config.rb.x, config.rb.y, 0.0f, 1.0f}, {+half.x, -half.y}};
	req.vertices[3] = {{config.rt.x, config.rt.y, 0.0f, 1.0f}, {+half.x, +half.y}};
	RenderQueue::Request(std::move(req));
}
```

ここでビルドが通る。

---

## パートB 図形の本体

### B-1 `Shader/Data/VectorShape/VectorShape.PS.shader`（新しいファイル）

`.shader` は起動時にフォルダから自動で読むので、ファイルを置くだけで使える。VS で見えるように、プロジェクトにも足しておく。

```hlsl
#META
stage:   PS
path:    VectorShape/VectorShape.PS.hlsl
profile: ps_6_0
#META_END

#PROGRAM
category: VectorShape
shading:  Unlit
vs:       Sprite2dVS
#PROGRAM_END

#HLSL
#include "Sprite/Sprite2d.hlsli"

// 図形のマテリアル（C++ の VectorShapeMaterialData と同じ並び。16 バイトずつに区切られる）
struct Material
{
    float32_t4 color; // 塗りの色（グラデーションの始まりの色）
    float32_t4 gradientColor; // グラデーションの終わりの色
    float32_t4 strokeColor; // 縁取りの色
    float32_t4 cornerRadius; // 角の半径（x=左上 y=右上 z=右下 w=左下）
    float32_t2 shapeHalf; // 図形の半分の大きさ（平行四辺形は、角を丸める前の形の真ん中の幅の半分と、高さの半分）
    float32_t2 shapeParam; // 円：x=半径 / リング：x=太さの真ん中の半径 y=太さの半分 / 平行四辺形：x=上の辺の横のずれ y=角の半径
    float32_t2 gradientAxis; // グラデーションの軸（直線：dot(位置, 軸) + 0.5 / 放射：length(位置 × 軸)）
    float32_t gradientStart; // 色が変わり始める場所
    float32_t gradientScale; // 1 ÷ 色が変わる範囲
    float32_t strokeInner; // 輪郭からの距離がこれより内側が塗り
    float32_t strokeOuter; // 輪郭からの距離がこれより内側が図形全体（縁取りを含む）
    float32_t fillOpacity; // 塗りを出すか（0 か 1）
    float32_t gaugeOffset; // 直線のゲージの切り口の場所
    float32_t2 gaugeAxisA; // ゲージの線の法線 A
    float32_t2 gaugeAxisB; // ゲージの線の法線 B（扇形の終わりの線）
    uint shape; // 図形の種類（VectorShapeType の順）
    uint gradientType; // グラデーションの種類（VectorShapeGradient の順）
    uint gaugeMode; // 0=切らない 1=直線 2=180 度より狭い扇形 3=180 度以上の扇形
    uint blendMode; // 重ね方（BlendMode の順）
    uint strokeUsesGradient; // 1：縁取りにも塗りのグラデーションを使う
    float32_t3 padA;
};
ConstantBuffer<Material> gMaterial : register(b20);

// C++ の enum と同じ順の番号
static const uint kShapeRectangle = 0;
static const uint kShapeRoundedRectangle = 1;
static const uint kShapeCircle = 2;
static const uint kShapeEllipse = 3;
static const uint kShapeRing = 4;
static const uint kShapeParallelogram = 5;
static const uint kGradientLinear = 1;
static const uint kGradientRadial = 2;
static const uint kGaugeLinear = 1;
static const uint kGaugeNarrowWedge = 2;
static const uint kBlendNone = 0;
static const uint kBlendMultiply = 4;
static const uint kBlendScreen = 5;

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

// ===== 距離の関数（SDF）=====
// どの関数も「位置 p から図形の輪郭までの距離」を返す。外がプラス、内がマイナス、輪郭がちょうど 0
// 単位は描かれるピクセルなので、距離に足し引きするだけで、太らせる・細らせる・縁取りができる
// 角丸四角形と平行四辺形の式は Inigo Quilez の 2D distance functions（https://iquilezles.org/articles/distfunctions2d/）

// 角丸四角形（4つの角で半径が違ってよい）
float32_t SdRoundedBox(float32_t2 p, float32_t2 halfSize, float32_t4 radius)
{
    // p のある側の角の半径を選ぶ（右上 y・左上 x・右下 z・左下 w）
    float32_t r = (p.x > 0.0f) ? ((p.y > 0.0f) ? radius.y : radius.z) : ((p.y > 0.0f) ? radius.x : radius.w);
    // 右上の角の丸の中心から測る。q.x と q.y の両方が＋の所が「角の丸」、片方だけ＋の所が「まっすぐな辺」
    float32_t2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

// 楕円：一番近い楕円の上の点を、くり返しで探す（正確な距離なので、縁取りの太さが場所で変わらない）
// t は楕円の上の点の向き（cos, sin）。曲率の中心から見た p の向きへ t を動かすと、4回で近い点に落ち着く
// やり方は Simple Method for Distance to Ellipse（https://blog.chatfield.io/simple-method-for-distance-to-ellipse/）
float32_t SdEllipse(float32_t2 p, float32_t2 radius)
{
    float32_t2 q = abs(p); // 右上の 1/4 だけ考える
    float32_t2 t = float32_t2(0.70710678f, 0.70710678f);
    float32_t k = radius.x * radius.x - radius.y * radius.y;
    for (int i = 0; i < 4; ++i)
    {
        float32_t2 onEllipse = radius * t;
        float32_t2 evolute = float32_t2(k * t.x * t.x * t.x / radius.x, -k * t.y * t.y * t.y / radius.y); // 曲率の中心
        float32_t2 toEllipse = onEllipse - evolute;
        float32_t2 toPoint = q - evolute;
        t = saturate((toPoint * length(toEllipse) / max(length(toPoint), 1e-5f) + evolute) / radius);
        t = normalize(max(t, 1e-5f));
    }
    float32_t distance = length(q - radius * t);
    float32_t2 normalized = q / radius;
    return (dot(normalized, normalized) < 1.0f) ? -distance : distance;
}

// 平行四辺形：halfWidth は真ん中の高さの幅の半分、skew は上の辺の真ん中の横のずれ
float32_t SdParallelogram(float32_t2 p, float32_t halfWidth, float32_t halfHeight, float32_t skew)
{
    float32_t2 edge = float32_t2(skew, halfHeight); // 真ん中から上の辺の真ん中への向き（斜めの辺の向き）
    p = (p.y < 0.0f) ? -p : p; // 点対称なので上半分だけ考える
    // 上の辺までの距離（2乗）と、内外の符号の材料
    float32_t2 w = p - edge;
    w.x -= clamp(w.x, -halfWidth, halfWidth);
    float32_t2 d = float32_t2(dot(w, w), -w.y);
    // 斜めの辺までの距離（2乗）
    float32_t s = p.x * edge.y - p.y * edge.x;
    p = (s < 0.0f) ? -p : p;
    float32_t2 v = p - float32_t2(halfWidth, 0.0f);
    v -= edge * clamp(dot(v, edge) / max(dot(edge, edge), 1e-5f), -1.0f, 1.0f); // 角を丸め切ると edge が 0 になる（カプセルの形）
    d = min(d, float32_t2(dot(v, v), halfWidth * halfHeight - abs(s)));
    return sqrt(d.x) * sign(-d.y);
}

// 今の図形の距離
float32_t CalcDistance(float32_t2 p)
{
    switch (gMaterial.shape)
    {
    case kShapeRectangle:
    case kShapeRoundedRectangle:
        return SdRoundedBox(p, gMaterial.shapeHalf, gMaterial.cornerRadius);
    case kShapeCircle:
        return length(p) - gMaterial.shapeParam.x;
    case kShapeEllipse:
        return SdEllipse(p, max(gMaterial.shapeHalf, 1e-3f));
    case kShapeRing:
        // 太さの真ん中の円からの距離 − 太さの半分
        return abs(length(p) - gMaterial.shapeParam.x) - gMaterial.shapeParam.y;
    case kShapeParallelogram:
        // 形を内側へ縮めた物の距離から半径を引くと、角が丸くなる
        return SdParallelogram(p, gMaterial.shapeHalf.x, gMaterial.shapeHalf.y, gMaterial.shapeParam.x) - gMaterial.shapeParam.y;
    }
    return 1.0f;
}

// ===== 輪郭のなめらかさ =====
// 距離 d から「このピクセルのうち、図形に入っている割合」を出す（0〜1）
// ddx・ddy：隣のピクセルとの d の差。どんな大きさ・回転で描いても、1ピクセル分の d の変化が分かるので、ちょうど1ピクセルの幅でなめらかになる
float32_t Coverage(float32_t d)
{
    float32_t pixelWidth = max(length(float32_t2(ddx(d), ddy(d))), 1e-5f);
    return saturate(0.5f - d / pixelWidth);
}

// ===== 塗りの色（グラデーション）=====
float32_t4 CalcPaintColor(float32_t2 p)
{
    float32_t t = 0.0f;
    switch (gMaterial.gradientType)
    {
    case kGradientLinear:
        t = dot(p, gMaterial.gradientAxis) + 0.5f;
        break;
    case kGradientRadial:
        t = length(p * gMaterial.gradientAxis);
        break;
    default:
        return gMaterial.color;
    }
    return lerp(gMaterial.color, gMaterial.gradientColor, saturate((t - gMaterial.gradientStart) * gMaterial.gradientScale));
}

// ===== ゲージ =====
// 見せる所がマイナス、切る所がプラスの距離（図形の距離と同じ向き）
float32_t CalcGaugeDistance(float32_t2 p)
{
    float32_t a = dot(p, gMaterial.gaugeAxisA);
    if (gMaterial.gaugeMode == kGaugeLinear)
    {
        return a - gMaterial.gaugeOffset;
    }
    float32_t b = dot(p, gMaterial.gaugeAxisB);
    // 2本の線の両方の内側（重なり）か、どちらかの内側（合わせた所）か
    return (gMaterial.gaugeMode == kGaugeNarrowWedge) ? max(a, b) : min(a, b);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t2 p = input.texcoord; // 図形の真ん中を原点にした、描かれるピクセルの場所（Y は上が＋）
    float32_t distance = CalcDistance(p);

    // ===== 1. 塗りと縁取りの色 =====
    float32_t4 paint = CalcPaintColor(p);
    float32_t4 stroke = (gMaterial.strokeUsesGradient != 0) ? paint : gMaterial.strokeColor;

    // ===== 2. 塗りと縁取りの間（strokeInner の線）で、色を切り替える =====
    // 透明度の違う色を混ぜるので、色に透明度を掛けた形（premultiplied）で足す
    float32_t fillMask = Coverage(distance - gMaterial.strokeInner);
    float32_t fillAlpha = paint.a * gMaterial.fillOpacity * fillMask;
    float32_t strokeAlpha = stroke.a * (1.0f - fillMask);
    float32_t alpha = fillAlpha + strokeAlpha;
    float32_t3 rgb = (paint.rgb * fillAlpha + stroke.rgb * strokeAlpha) / max(alpha, 1e-5f);

    // ===== 3. 図形全体の輪郭（strokeOuter の線）とゲージの切り口で、透明にする =====
    alpha *= Coverage(distance - gMaterial.strokeOuter);
    if (gMaterial.gaugeMode != 0)
    {
        alpha *= Coverage(CalcGaugeDistance(p));
    }

    // ===== 4. 重ね方に合わせる =====
    // 板は図形より広いので、図形の外（alpha が 0）が背景を変えないようにする
    switch (gMaterial.blendMode)
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
    output.color = float32_t4(rgb, alpha);
    return output;
}
#HLSL_END
```

### B-2 `Components/UI/VectorShapeComponent.h`（新しいファイル）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector4.h"

// 画像を使わず、シェーダーで計算して描く UI の図形。Entityに付けるComponent（カテゴリ：UI）
// 置き方は Image と同じ：Transform の translation の x・y が画面のピクセル（ゲームの画面のつもり。Y は上が＋）、
// 回転は Inspector の Rotation の z。Anchor・Pivot・Order・Blend も Image と同じ（Order で Image・Text とも重なる）
// 値は毎フレーム読むだけなので、Script や UI のアニメーションから自由に書き換えてよい（バッファを作り直さない）
//   例：GetComponent<VectorShapeComponent>()->gaugeAmount = value / maxValue;
// Transform の scale を掛けると：Size は x・y それぞれ、角の丸み・縁取り・リングの太さは x・y の小さい方の倍率が掛かる（横に伸ばしても形がゆがまない）

/// <summary>
/// 図形の種類
/// </summary>
enum class VectorShapeType {
	Rectangle,        // 四角形
	RoundedRectangle, // 角丸四角形
	Circle,           // 円（Size の短い方が直径）
	Ellipse,          // 楕円（Size がそのまま横と縦の直径）
	Ring,             // リング（Size の短い方が外の直径）
	Parallelogram,    // 平行四辺形（Size の四角にちょうど入る形）
};

/// <summary>
/// 塗り方
/// </summary>
enum class VectorShapeStyle {
	Fill,          // 塗りだけ
	FillAndStroke, // 塗りと縁取り
	Stroke,        // 縁取りの線だけ
};

/// <summary>
/// 縁取りを、図形の輪郭のどちら側に付けるか
/// </summary>
enum class VectorShapeStrokeAlign {
	Inside,  // 輪郭の内側（図形の大きさが変わらない）
	Center,  // 輪郭をまたぐ
	Outside, // 輪郭の外側
};

/// <summary>
/// グラデーションの種類
/// </summary>
enum class VectorShapeGradient {
	None,   // 無し（Color の1色）
	Linear, // 直線
	Radial, // 真ん中から外へ
};

/// <summary>
/// ゲージ（図形のどこまで見せるか）の形
/// </summary>
enum class VectorShapeGauge {
	None,   // 全部見せる
	Linear, // 直線で切る
	Radial, // 真ん中のまわりに扇形で切る
};

/// <summary>
/// 直線のゲージが伸びる向き
/// </summary>
enum class VectorShapeGaugeDirection {
	LeftToRight, // 左から右へ
	RightToLeft, // 右から左へ
	BottomToTop, // 下から上へ
	TopToBottom, // 上から下へ
};

/// <summary>
/// 画像を使わない UI の図形
/// </summary>
struct VectorShapeComponent {
	bool enabled = true;                                       // false の間は描かない（マウスも当たらない）
	VectorShapeType shape = VectorShapeType::RoundedRectangle; // 図形の種類
	Vector2 size = {100.0f, 100.0f};                           // 大きさ（ピクセル）
	ScreenAnchor anchor = ScreenAnchor::Center;                // 画面のどこから位置を数えるか（Image と同じ）
	Vector2 pivot = {0.5f, 0.5f};                              // 図形のどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）
	int order = 0;                                             // 重ねる順（大きいほど手前。Image・Text の Order と比べる）
	BlendMode blend = BlendMode::Normal;                       // 重ね方（Normal・Add など。Image と同じ）
	// --- 形 ---
	Vector4 cornerRadius = {16.0f, 16.0f, 16.0f, 16.0f}; // 角の半径（ピクセル。x=左上 y=右上 z=右下 w=左下。平行四辺形は x だけ使う）
	float ringThickness = 16.0f;                         // リングの太さ（ピクセル）
	float skewAngle = 20.0f;                             // 平行四辺形の傾き（度。正で上の辺が右へずれる）
	// --- 塗り ---
	VectorShapeStyle style = VectorShapeStyle::Fill;     // 塗り方
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};            // 塗りの色（グラデーションの始まりの色）
	// --- 縁取り ---
	float strokeWidth = 4.0f;                            // 縁取りの太さ（ピクセル）
	VectorShapeStrokeAlign strokeAlign = VectorShapeStrokeAlign::Inside; // 縁取りを輪郭のどちら側に付けるか
	Vector4 strokeColor = {0.0f, 0.0f, 0.0f, 1.0f};      // 縁取りの色
	bool strokeUsesGradient = false;                     // 縁取りにも塗りのグラデーションを使う（Stroke Color を使わない）
	// --- グラデーション ---
	VectorShapeGradient gradient = VectorShapeGradient::None; // グラデーションの種類
	Vector4 gradientColor = {0.3f, 0.6f, 1.0f, 1.0f};    // 終わりの色
	float gradientAngle = 0.0f;                          // 直線の向き（度。0 で左から右、90 で下から上）
	Vector2 gradientRange = {0.0f, 1.0f};                // 色が変わる範囲（図形の中の場所。0〜1。x までは始まりの色、y から先は終わりの色）
	// --- ゲージ ---
	VectorShapeGauge gauge = VectorShapeGauge::None;     // ゲージの形
	float gaugeAmount = 1.0f;                            // 見せる割合（0〜1）
	VectorShapeGaugeDirection gaugeDirection = VectorShapeGaugeDirection::LeftToRight; // 直線のゲージが伸びる向き
	float gaugeStartAngle = 0.0f;                        // 扇形の始まりの角度（度。0 で真上、時計回りが＋）
	float gaugeSweep = 360.0f;                           // Amount が 1 のときの扇形の広さ（度。270 なら下が開いたメーター）
	bool gaugeClockwise = true;                          // 扇形が時計回りに伸びる
};

static_assert(std::is_trivially_copyable_v<VectorShapeComponent>);
```

### B-3 `Systems/Rendering2D/UIDrawOrder.h`（新しいファイル）

Image の並べ替えと同じ処理が要るので、テンプレートにして共有する。

```cpp
#pragma once
#include <algorithm>
#include <tuple>
#include <vector>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/ECS/EntityManager.h"

/// <summary>
/// 画面に重ねる順に並べるための1件（Image・Vector Shape の Draw の中だけで使う）
/// </summary>
struct UIDrawItem {
	int order = 0;         // Component の Order（小さい方が奥）
	EntityId id = 0;       // 同じ Order なら、先に作った物（番号が小さい物）が奥
	Handle<Entity> entity; // 持ち主
};

/// <summary>
/// 描く物（有効で、親まで有効な物）を Order の順に集める。T は enabled・order を持つ Component
/// <para>RenderQueue は同じ Order の物を積んだ順に描くので、先に作った物が奥になるように並べてから積む</para>
/// </summary>
template<class T>
void CollectUIDrawItems(std::vector<UIDrawItem>& items) {
	items.clear();
	EntityManager::ForEach<T>([&items](Handle<Entity> entity, const T& component) {
		if (!component.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const Entity* owner = EntityManager::Get(entity);
		items.push_back({component.order, owner ? owner->id : 0, entity});
	});
	// Order の小さい順。同じなら番号の小さい順（std::tie で「1つ目を比べて、同じなら2つ目」をまとめて書ける）
	std::ranges::sort(items, [](const UIDrawItem& a, const UIDrawItem& b) { return std::tie(a.order, a.id) < std::tie(b.order, b.id); });
}
```

### B-4 `Systems/Rendering2D/SpriteRenderSystem.cpp`

include を2か所直す。`<tuple>` は使わなくなる。

変更前：

```cpp
#include <memory>
#include <tuple>
#include <utility>
```

変更後：

```cpp
#include <memory>
#include <utility>
```

変更後：

```cpp
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Systems/Rendering2D/UIDrawOrder.h"
```

`struct ImageDrawItem` を消して、`SpriteRenderState` を直す。

変更後：

```cpp
struct SpriteRenderState {
	std::vector<UIDrawItem> images; // 並べる作業用（配列のメモリを使い回す）
};
```

`DrawImages` の頭を直す（`for` の中はそのまま）。

変更前：

```cpp
// ===== Image：Order の順に、画面に重ねる（RenderQueue は同じ Order の物を積んだ順に描くので、先に作った物が奥になるように並べてから積む）=====
void DrawImages() {
	std::vector<ImageDrawItem>& items = state->images;
	items.clear();
	EntityManager::ForEach<ImageComponent>([&items](Handle<Entity> entity, const ImageComponent& image) {
		if (!image.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const Entity* owner = EntityManager::Get(entity);
		items.push_back({image.order, owner ? owner->id : 0, entity});
	});
	// Order の小さい順。同じなら番号の小さい順（std::tie で「1つ目を比べて、同じなら2つ目」をまとめて書ける）
	std::ranges::sort(items, [](const ImageDrawItem& a, const ImageDrawItem& b) { return std::tie(a.order, a.id) < std::tie(b.order, b.id); });

	for (const ImageDrawItem& item : items) {
```

変更後：

```cpp
// ===== Image：Order の順に、画面に重ねる =====
void DrawImages() {
	CollectUIDrawItems<ImageComponent>(state->images);
	for (const UIDrawItem& item : state->images) {
```

### B-5 `ECS/ComponentUI.h`

`// ===== ボタン` の前に足す。

変更後：

```cpp
	// ===== 今の設定では効かない項目を灰色にする（Begin と End の間の項目。保存には関係しない＝値は書く・読む）=====
	// 隠す（if で ui.Field を呼ばない）と保存からも落ちるので、値を残したい項目はこちらを使う
	virtual void BeginDisabled(bool isDisabled) { (void)isDisabled; }
	virtual void EndDisabled() {}
```

### B-6 `ECS/ComponentUI.cpp`

`InspectorComponentUI` が2つある。`#ifdef USE_IMGUI` の中の、ImGui を使う方の `ColorField` の下に足す。

変更後：

```cpp
	void ColorField(const char* label, Vector4& rgba) override { ImGui::ColorEdit4(label, &rgba.x); }

	void BeginDisabled(bool isDisabled) override { ImGui::BeginDisabled(isDisabled); }
	void EndDisabled() override { ImGui::EndDisabled(); }
```

### B-7 `Systems/UI/VectorShapeSystem.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// Vector Shape（画像を使わない UI の図形）を描く
/// <para>停止中も描く（置いた場所をエディタで見られるように）。Scene ビューにも Image と同じ位置に重なる</para>
/// <para>図形の形はシェーダーが1ピクセルずつ計算する（VectorShape.PS）。ここは Component の値を、シェーダーの単位（画面のピクセル）に直して渡すだけ</para>
/// </summary>
class VectorShapeSystem {
public:
	// Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 描画リクエストを積む（EngineSystems から。Image の後、Text の前）
	static void Draw();

	// ===== ゲームから使う =====

	/// <summary>
	/// マウスがその図形の Size の四角の上にあるか（ボタンに使う。円でも角の所は上と数える）
	/// <para>図形が無い・無効・マウスがゲーム画面の外なら false</para>
	/// </summary>
	static bool IsMouseOver(Handle<Entity> entity);
};
```

### B-8 `Systems/UI/VectorShapeSystem.cpp`（新しいファイル）

```cpp
#include "VectorShapeSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Math/MathUtility.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/Rendering2D/UIDrawOrder.h"

namespace {
constexpr float kEdgeMargin = 2.0f;         // 四角の板を図形より広げる量（ピクセル。縁のなめらかな所が板の外で切れないように）
constexpr float kMinScale = 1.0e-4f;        // これより小さい Scale は描かない（0 で割らないように）
constexpr float kNoStrokeEdge = 1.0e5f;     // 縁取りが無いときの、塗りの内側の線（図形の外まで全部塗りにする）
constexpr float kMaxSkewDegrees = 80.0f;    // 平行四辺形の傾きの上限（度。90 に近づくと横の長さが無限になる）
constexpr float kMaxTopShift = 0.9f;        // 平行四辺形の上の辺が横へずれる上限（幅の半分に対して。これで上の辺が幅の 1 割は残り、図形が消えない）

/// <summary>
/// VectorShapeSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct VectorShapeState {
	std::vector<UIDrawItem> items; // 並べる作業用（配列のメモリを使い回す）
};
std::unique_ptr<VectorShapeState> state;

//=============================================================================
// Component の値 → シェーダーの値
//=============================================================================
// 行列の x 軸・y 軸の長さ（Transform の scale に、親の scale も掛かった値。回っていても測れる）
Vector2 CalcAxisScale(const Matrix4x4& m) { return {std::hypot(m.m[0][0], m.m[0][1]), std::hypot(m.m[1][0], m.m[1][1])}; }

// 2Dの向き（x・y）を、左へ 90 度回す
Vector2 RotateLeft(const Vector2& v) { return {-v.y, v.x}; }

// 図形の範囲の半分（ピクセル。ゲージが端から端まで動く範囲）
Vector2 CalcShapeExtent(VectorShapeType shape, const Vector2& half) {
	switch (shape) {
	case VectorShapeType::Circle:
	case VectorShapeType::Ring: {
		const float radius = (std::min)(half.x, half.y);
		return {radius, radius};
	}
	default:
		return half;
	}
}

// --- 形 ---
// 角の半径を、シェーダーが読める値に直す（どの角も、短い方の辺の半分まで）
Vector4 ClampCornerRadius(const Vector4& radius, float scale, float maxRadius) {
	const auto clampOne = [scale, maxRadius](float value) { return std::clamp(value * scale, 0.0f, maxRadius); };
	return {clampOne(radius.x), clampOne(radius.y), clampOne(radius.z), clampOne(radius.w)};
}

// 図形の形の値を、シェーダーが読む shapeHalf・shapeParam・cornerRadius に入れる
// half は図形の Size の半分、uniformScale は角の丸み・太さに掛ける倍率。戻り値は平行四辺形の傾き（横のずれ ÷ 縦。ほかの形は 0）
float WriteShapeParams(const VectorShapeComponent& shape, const Vector2& half, float uniformScale, VectorShapeMaterialData& material) {
	const float shortHalf = (std::min)(half.x, half.y);
	material.shapeHalf = half;
	switch (shape.shape) {
	case VectorShapeType::Rectangle:
		material.cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f}; // 角丸四角形の計算を半径 0 で使う
		break;
	case VectorShapeType::RoundedRectangle:
		material.cornerRadius = ClampCornerRadius(shape.cornerRadius, uniformScale, shortHalf);
		break;
	case VectorShapeType::Circle:
		material.shapeParam.x = shortHalf;
		break;
	case VectorShapeType::Ellipse:
		break; // shapeHalf だけ使う
	case VectorShapeType::Ring: {
		// x：太さの真ん中の半径 / y：太さの半分
		const float thickness = std::clamp(shape.ringThickness * uniformScale, 0.0f, shortHalf);
		material.shapeParam = {shortHalf - thickness * 0.5f, thickness * 0.5f};
		break;
	}
	case VectorShapeType::Parallelogram: {
		// 上の辺の真ん中が、図形の真ん中から横へ topShift だけずれる。Size の四角にちょうど入るように、真ん中の高さの半分の幅 middleHalf を決める
		const float angle = std::clamp(shape.skewAngle, -kMaxSkewDegrees, kMaxSkewDegrees) * MathUtility::kDegToRad;
		const float slope = std::tan(angle);
		const float maxShift = half.x * kMaxTopShift;
		const float topShift = std::clamp(half.y * slope, -maxShift, maxShift);
		const float clampedSlope = (half.y > 0.0f) ? topShift / half.y : 0.0f;
		const float middleHalf = half.x - std::abs(topShift);
		// 角を丸める：形を半径の分だけ内側へ縮めて、外へ半径の分だけ太らせる（縮めるとき、斜めの辺は 半径 ÷ cos だけ横へ動く）
		const float cosAngle = 1.0f / std::hypot(1.0f, clampedSlope);
		const float radius = std::clamp(shape.cornerRadius.x * uniformScale, 0.0f, (std::min)(half.y, middleHalf * cosAngle));
		const float insetHalfY = half.y - radius;
		material.shapeHalf = {middleHalf - radius / cosAngle, insetHalfY};
		material.shapeParam = {insetHalfY * clampedSlope, radius};
		return clampedSlope;
	}
	}
	return 0.0f;
}

// --- 塗り方 ---
// 図形の輪郭からの距離で、どこまでを塗り・どこまでを図形全体にするかを決める（距離は内側がマイナス）
void WriteStroke(const VectorShapeComponent& shape, float uniformScale, VectorShapeMaterialData& material) {
	const float width = (std::max)(shape.strokeWidth * uniformScale, 0.0f);
	material.fillOpacity = (shape.style == VectorShapeStyle::Stroke) ? 0.0f : 1.0f;
	material.strokeUsesGradient = shape.strokeUsesGradient ? 1u : 0u;
	if (shape.style == VectorShapeStyle::Fill || width <= 0.0f) {
		material.strokeInner = kNoStrokeEdge;
		material.strokeOuter = 0.0f;
		return;
	}
	switch (shape.strokeAlign) {
	case VectorShapeStrokeAlign::Inside:
		material.strokeInner = -width;
		material.strokeOuter = 0.0f;
		break;
	case VectorShapeStrokeAlign::Center:
		material.strokeInner = -width * 0.5f;
		material.strokeOuter = width * 0.5f;
		break;
	case VectorShapeStrokeAlign::Outside:
		material.strokeInner = 0.0f;
		material.strokeOuter = width;
		break;
	}
}

// --- グラデーション ---
void WriteGradient(const VectorShapeComponent& shape, const Vector2& half, VectorShapeMaterialData& material) {
	material.gradientType = static_cast<uint32_t>(shape.gradient);
	material.gradientStart = shape.gradientRange.x;
	const float range = shape.gradientRange.y - shape.gradientRange.x;
	material.gradientScale = 1.0f / ((std::abs(range) > 1.0e-4f) ? range : 1.0e-4f); // 範囲が 0 でも、0 で割らずにパキッと分かれる
	switch (shape.gradient) {
	case VectorShapeGradient::None:
		break;
	case VectorShapeGradient::Linear: {
		// 図形の四角の、向きの方の端から端までを 0〜1 にする（dot(位置, 軸) + 0.5 で読む）
		const float angle = shape.gradientAngle * MathUtility::kDegToRad;
		const Vector2 direction = {std::cos(angle), std::sin(angle)};
		const float length = 2.0f * (std::abs(direction.x) * half.x + std::abs(direction.y) * half.y);
		material.gradientAxis = direction / (std::max)(length, 1.0e-4f);
		break;
	}
	case VectorShapeGradient::Radial:
		// 真ん中が 0、四角の辺の真ん中が 1（length(位置 × 軸) で読む）
		material.gradientAxis = {1.0f / (std::max)(half.x, 1.0e-4f), 1.0f / (std::max)(half.y, 1.0e-4f)};
		break;
	}
}

// --- ゲージ ---
// 直線：gaugeAxisA 方向に測って gaugeOffset より手前を見せる
void WriteLinearGauge(const VectorShapeComponent& shape, const Vector2& extent, float slope, float outerMargin, float amount, VectorShapeMaterialData& material) {
	const bool isHorizontal = (shape.gaugeDirection == VectorShapeGaugeDirection::LeftToRight) || (shape.gaugeDirection == VectorShapeGaugeDirection::RightToLeft);
	const bool isReversed = (shape.gaugeDirection == VectorShapeGaugeDirection::RightToLeft) || (shape.gaugeDirection == VectorShapeGaugeDirection::TopToBottom);
	// 横のゲージは、切り口も図形の傾きに合わせる（平行四辺形のゲージの先が、斜めの辺と平行になる）
	const Vector2 axis = isHorizontal ? Vector2{1.0f, -slope} : Vector2{0.0f, 1.0f};
	const float slopedExtent = isHorizontal ? extent.x - std::abs(extent.y * slope) : extent.y;
	const float range = slopedExtent + outerMargin;
	material.gaugeMode = 1;
	material.gaugeAxisA = isReversed ? axis * -1.0f : axis;
	material.gaugeOffset = -range + 2.0f * range * amount;
}

// 扇形：始まりの線と終わりの線の間を見せる（180 度より狭ければ2つの半平面の重なり、広ければ合わせた所）
void WriteRadialGauge(const VectorShapeComponent& shape, float amount, VectorShapeMaterialData& material) {
	const float sweep = std::clamp(shape.gaugeSweep * MathUtility::kDegToRad, 0.0f, MathUtility::kTwoPi) * amount;
	if (sweep >= MathUtility::kTwoPi) {
		return; // 1周なら全部見せる
	}
	const float start = shape.gaugeStartAngle * MathUtility::kDegToRad;
	const float end = start + (shape.gaugeClockwise ? sweep : -sweep);
	// 角度は真上が 0・時計回りが＋なので、向きは (sin, cos)
	const Vector2 startDirection = {std::sin(start), std::cos(start)};
	const Vector2 endDirection = {std::sin(end), std::cos(end)};
	// 線の法線は「見せない側」を向ける（シェーダーは 法線・位置 が負の所を見せる）
	const float side = shape.gaugeClockwise ? 1.0f : -1.0f;
	material.gaugeAxisA = RotateLeft(startDirection) * side;
	material.gaugeAxisB = RotateLeft(endDirection) * -side;
	material.gaugeMode = (sweep < MathUtility::kPi) ? 2u : 3u;
}

// Amount が 0 で何も見えないときは false
bool WriteGauge(const VectorShapeComponent& shape, const Vector2& half, float slope, VectorShapeMaterialData& material) {
	if (shape.gauge == VectorShapeGauge::None) {
		return true;
	}
	const float amount = std::clamp(shape.gaugeAmount, 0.0f, 1.0f);
	if (amount <= 0.0f) {
		return false;
	}
	switch (shape.gauge) {
	case VectorShapeGauge::None:
		break;
	case VectorShapeGauge::Linear:
		if (amount < 1.0f) {
			const float outerMargin = (std::max)(material.strokeOuter, 0.0f); // 外へ出た縁取りも含めて、端から端まで動かす
			WriteLinearGauge(shape, CalcShapeExtent(shape.shape, half), slope, outerMargin, amount, material);
		}
		break;
	case VectorShapeGauge::Radial:
		WriteRadialGauge(shape, amount, material);
		break;
	}
	return true;
}

// Component の値をシェーダーの値にする。値が変わっても、ここを毎フレーム通るだけで、作り直す物は無い。何も見えないときは false
bool MakeMaterial(const VectorShapeComponent& shape, const Vector2& scale, float groupAlpha, VectorShapeMaterialData& material) {
	if (shape.shape == VectorShapeType::Ring && shape.ringThickness <= 0.0f) {
		return false; // 太さ 0 のリングは何も見えない
	}
	const Vector2 half = {shape.size.x * scale.x * 0.5f, shape.size.y * scale.y * 0.5f};
	const float uniformScale = (std::min)(scale.x, scale.y); // 横だけ伸ばしても、丸み・太さがゆがまないように、小さい方の倍率を掛ける
	material.shape = static_cast<uint32_t>(shape.shape);
	material.blendMode = static_cast<uint32_t>(shape.blend);
	material.color = ClampColor(MultiplyAlpha(shape.color, groupAlpha));
	material.gradientColor = ClampColor(MultiplyAlpha(shape.gradientColor, groupAlpha));
	material.strokeColor = ClampColor(MultiplyAlpha(shape.strokeColor, groupAlpha));
	const float slope = WriteShapeParams(shape, half, uniformScale, material);
	WriteStroke(shape, uniformScale, material);
	WriteGradient(shape, half, material);
	return WriteGauge(shape, half, slope, material);
}

//=============================================================================
// 描く
//=============================================================================
void DrawShape(const UIDrawItem& item) {
	const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(item.entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(item.entity);
	if (shape == nullptr || transform == nullptr || shape->size.x <= 0.0f || shape->size.y <= 0.0f) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(item.entity);
	const Vector2 scale = CalcAxisScale(transform->worldMatrix);
	if (groupAlpha <= 0.0f || scale.x < kMinScale || scale.y < kMinScale) {
		return;
	}
	Renderer::VectorShapeConfig config;
	if (!MakeMaterial(*shape, scale, groupAlpha, config.material)) {
		return;
	}

	// --- 板の4つの角：Size の四角を、縁取りと縁のなめらかな所が入るだけ広げる ---
	// margin は描かれるピクセルで決めるので、Transform の scale が掛かる前の長さ（ローカル）へ割り戻す
	const float margin = (std::max)(config.material.strokeOuter, 0.0f) + kEdgeMargin;
	const Vector2 localMargin = {margin / scale.x, margin / scale.y};
	const Vector2 center = {(0.5f - shape->pivot.x) * shape->size.x, (0.5f - shape->pivot.y) * shape->size.y};
	const Vector2 localHalf = {shape->size.x * 0.5f + localMargin.x, shape->size.y * 0.5f + localMargin.y};
	const auto toScreen = [&](float signX, float signY) {
		return SpriteRenderSystem::CalcScreenPoint(transform->worldMatrix, shape->anchor, {center.x + signX * localHalf.x, center.y + signY * localHalf.y});
	};
	config.lb = toScreen(-1.0f, -1.0f);
	config.lt = toScreen(-1.0f, +1.0f);
	config.rb = toScreen(+1.0f, -1.0f);
	config.rt = toScreen(+1.0f, +1.0f);
	// シェーダーへは、図形の真ん中を原点にした描かれるピクセルの場所を渡す（UV の代わり。板の中でなめらかにつながる）
	config.localHalf = {localHalf.x * scale.x, localHalf.y * scale.y};
	config.blendMode = shape->blend;
	config.order = shape->order;
	config.isSceneHidden = SceneVisibility::IsHidden(item.entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	Renderer::DrawVectorShape(config);
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Vector Shape の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Image と同じ書き方）
// 今の設定で効かない項目は隠さず灰色にする（隠すと保存からも落ちて、別の形へ切り替えたときに値が消えるので）
void DescribeVectorShape(ComponentUI& ui, VectorShapeComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Shape###shape", value.shape, Tip("図形の種類"));
	ui.Field("Size###size", value.size, AtLeast(0.0f).Drag(1.0f).Tip("大きさ（ピクセル。ゲームの画面のつもりで）。円とリングは短い方が直径"));
	ui.Field("Anchor###anchor", value.anchor, Tip("画面のどこから位置を数えるか（位置は Transform の Translation の x・y。ピクセルで、上が＋）"));
	ui.Field("Pivot###pivot", value.pivot, Tip("図形のどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）"));
	ui.Field("Order###order", value.order, Tip("重ねる順（大きいほど手前。Image・Text の Order とも比べる）"));
	ui.Field("Blend###blend", value.blend, Tip("重ね方（Normal：透明な所が透ける / Add：足して光る）。None は縁のなめらかさが無くなる"));

	ui.Separator("Shape");
	const bool usesCorner = (value.shape == VectorShapeType::RoundedRectangle) || (value.shape == VectorShapeType::Parallelogram);
	ui.BeginDisabled(!usesCorner);
	ui.Field("Corner Radius###cornerRadius", value.cornerRadius, AtLeast(0.0f).Drag(0.5f).Tip("角の半径（ピクセル）。左上・右上・右下・左下の順。平行四辺形は左の値を全部の角に使う"));
	ui.EndDisabled();
	ui.BeginDisabled(value.shape != VectorShapeType::Ring);
	ui.Field("Ring Thickness###ringThickness", value.ringThickness, AtLeast(0.0f).Drag(0.5f).Tip("リングの太さ（ピクセル）"));
	ui.EndDisabled();
	ui.BeginDisabled(value.shape != VectorShapeType::Parallelogram);
	ui.Field("Skew Angle###skewAngle", value.skewAngle, Range(-kMaxSkewDegrees, kMaxSkewDegrees).Tip("傾き（度）。正で上の辺が右へずれる。図形は Size の四角の中に収まる"));
	ui.EndDisabled();

	ui.Separator("Paint");
	ui.Field("Style###style", value.style, Tip("Fill：塗りだけ / Fill And Stroke：塗りと縁取り / Stroke：線だけ"));
	ui.ColorField("Color###color", value.color);

	ui.Separator("Stroke");
	ui.BeginDisabled(value.style == VectorShapeStyle::Fill);
	ui.Field("Stroke Width###strokeWidth", value.strokeWidth, AtLeast(0.0f).Drag(0.25f).Tip("縁取りの太さ（ピクセル）。Size を変えても変わらない"));
	ui.Field("Stroke Align###strokeAlign", value.strokeAlign, Tip("輪郭のどちら側に付けるか（Inside：図形の大きさが変わらない）"));
	ui.ColorField("Stroke Color###strokeColor", value.strokeColor);
	ui.Field("Stroke Uses Gradient###strokeUsesGradient", value.strokeUsesGradient, Tip("縁取りにも塗りのグラデーションを使う（Stroke Color を使わない）"));
	ui.EndDisabled();

	ui.Separator("Gradient");
	ui.Field("Gradient###gradient", value.gradient, Tip("None：Color の1色 / Linear：直線 / Radial：真ん中から外へ"));
	ui.BeginDisabled(value.gradient == VectorShapeGradient::None);
	ui.ColorField("Gradient Color###gradientColor", value.gradientColor);
	ui.Field("Gradient Range###gradientRange", value.gradientRange, Tip("色が変わる範囲（図形の中の場所。0〜1）。x までは Color、y から先は Gradient Color"));
	ui.EndDisabled();
	ui.BeginDisabled(value.gradient != VectorShapeGradient::Linear);
	ui.Field("Gradient Angle###gradientAngle", value.gradientAngle, Drag(1.0f).Tip("直線の向き（度。0 で左から右、90 で下から上）"));
	ui.EndDisabled();

	ui.Separator("Gauge");
	ui.Field("Gauge###gauge", value.gauge, Tip("図形のどこまで見せるか（None：全部 / Linear：直線で切る / Radial：真ん中のまわりに扇形で切る）。縁取りも一緒に切る"));
	ui.BeginDisabled(value.gauge == VectorShapeGauge::None);
	ui.Field("Amount###gaugeAmount", value.gaugeAmount, Range(0.0f, 1.0f).Tip("見せる割合（0〜1）"));
	ui.EndDisabled();
	ui.BeginDisabled(value.gauge != VectorShapeGauge::Linear);
	ui.Field("Direction###gaugeDirection", value.gaugeDirection, Tip("直線のゲージが伸びる向き"));
	ui.EndDisabled();
	ui.BeginDisabled(value.gauge != VectorShapeGauge::Radial);
	ui.Field("Start Angle###gaugeStartAngle", value.gaugeStartAngle, Drag(1.0f).Tip("扇形の始まりの角度（度。0 で真上、時計回りが＋）"));
	ui.Field("Sweep###gaugeSweep", value.gaugeSweep, Range(0.0f, 360.0f).Tip("Amount が 1 のときの扇形の広さ（度。270 なら下が開いたメーターになる）"));
	ui.Field("Clockwise###gaugeClockwise", value.gaugeClockwise, Tip("時計回りに伸びる"));
	ui.EndDisabled();
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void VectorShapeSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<VectorShapeState>();

	// Vector Shape の置き場所・保存・Inspector を登録する。見せ方は DescribeVectorShape 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<VectorShapeComponent>();
	ComponentSerializer::Register<VectorShapeComponent>("Vector Shape", &DescribeVectorShape);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<VectorShapeComponent>>("Vector Shape", "UI", &DescribeVectorShape));
}

void VectorShapeSystem::Release() { state.reset(); }

//=============================================================================
// 描く（停止中も）
//=============================================================================
void VectorShapeSystem::Draw() {
	HTT_ASSERT_MSG(state != nullptr, "VectorShapeSystem::Initialize()を先に呼んでください");
	CollectUIDrawItems<VectorShapeComponent>(state->items);
	for (const UIDrawItem& item : state->items) {
		DrawShape(item);
	}
}

//=============================================================================
// ゲームから使う
//=============================================================================
bool VectorShapeSystem::IsMouseOver(Handle<Entity> entity) {
	const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (shape == nullptr || transform == nullptr || !shape->enabled || !EntityManager::IsActiveInHierarchy(entity) || !InputManager::IsMouseInGame()) {
		return false;
	}
	const ScreenQuad quad = SpriteRenderSystem::CalcScreenQuad(transform->worldMatrix, shape->anchor, shape->pivot, shape->size);
	return SpriteRenderSystem::IsInsideQuad(InputManager::GetMousePositionInGame(), quad);
}
```

### B-9 `Systems/EngineSystems.cpp`

4か所足す。

変更後：

```cpp
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
```

変更後：

```cpp
	TextRenderSystem::Initialize();       // Text
	VectorShapeSystem::Initialize();      // Vector Shape
```

変更後：

```cpp
	TextRenderSystem::Release();
	VectorShapeSystem::Release();
```

変更後：

```cpp
	SpriteRenderSystem::Draw(gameCamera); // 画像の板（Sprite Renderer）と、画面に重ねる絵（Image）の描画
	VectorShapeSystem::Draw(); // 画像を使わない UI の図形（Vector Shape）の描画（Image・Text とは Order の順に重なる）
```

### B-10 `ECS/GameComponent.h`

ゲームから `VectorShapeComponent`・`VectorShapeSystem` を使えるようにする。

変更後：

```cpp
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
```

ここでビルドが通る。シーンファイルに手で書けば描ける（D-1 のシーンがそのまま使える）。

---

## パートC エディタ

### C-1 `Editor/Windows/HierarchyWindow.cpp`

include を足す。

変更後：

```cpp
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
```

＋ メニューの Image の下に足す。

変更後：

```cpp
	// --- 画像：3D空間に置く板（Sprite Renderer）・画面に重ねる絵（Image）・図形（Vector Shape）・文字（Text）を付けた状態で作る ---
```

変更後：

```cpp
	if (ImGui::MenuItem("Vector Shape")) {
		EditorHistory::RequestCreate("Vector Shape", top, {ComponentSnapshot::Make(VectorShapeComponent{})});
	}
```

### C-2 `Editor/Gizmos/UIGizmo.h`

コメントを3か所直す。

変更後：

```cpp
/// Scene ビューで、画面に重ねる UI（Image・Vector Shape・Text）をクリックで選ぶ・ドラッグで動かす（Unity の Rect Tool の小さい版）
```

変更後：

```cpp
/// <para>Vector Shape を選ぶと、枠に大きさを変える印が8つ出る。印をドラッグすると Size が変わる（反対の辺は動かない）。Transform と Size をまとめて1件の履歴にする</para>
```

変更後：

```cpp
	// 画面に重ねる UI か（Image・Vector Shape・Text のどれかが付いている）。位置がピクセルなので、3D の移動のギズモは出さない
```

### C-3 `Editor/Gizmos/UIGizmo.cpp`（全体を差し替え）

変えた所は次のとおり。

- `CalcShape`・`PickUI`・`IsScreenUI` に Vector Shape を足した。
- `UIDragState` に「つかんだ印」を足した。
- `UpdateResize`：マウスの動きを Entity の横・縦の軸に分けて Size を変える。反対の辺が動かないように、伸びた分のうち Pivot の割合だけ Translation も動かす。
- `EndPress`：Transform と Vector Shape の変更を1件の履歴にする。
- `CalcHandlePosition`・`FindHandle`・`DrawHandles`：印の場所・当たり・描画。

```cpp
#ifdef USE_IMGUI
#include "UIGizmo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <vector>

#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/EditorViewport.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/UI/TextRenderSystem.h"

namespace {
constexpr ImU32 kSelectedColor = IM_COL32(255, 160, 40, 255); // 選んだ UI の枠（3D の物の枠と同じ色）
constexpr ImU32 kHoveredColor = IM_COL32(255, 160, 40, 110);  // マウスが乗っている UI の枠（うすく。クリックすると選べる物）
constexpr float kOutlineThickness = 1.5f;                     // 枠の線の太さ（画面のピクセル）
constexpr float kPivotRadius = 3.5f;                          // 位置（Pivot の所）の印の半径（画面のピクセル）
constexpr float kHandleHalfSize = 4.0f;                       // 大きさを変える印（四角）の半分の大きさ（画面のピクセル）
constexpr float kHandleHitRadius = 8.0f;                      // 印をつかめる距離（画面のピクセル）
constexpr float kMinAxisLength = 1.0e-4f;                     // これより小さい Scale の軸は 0 で割らないように止める

// 大きさを変える印の場所（x：-1=左 0=真ん中 1=右 / y：-1=下 0=真ん中 1=上）
constexpr std::array<Vector2, 8> kHandles = {{{-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}};

/// <summary>
/// 画面に重ねる UI の置いた形（正規化座標）
/// </summary>
struct UIShape {
	ScreenQuad quad;  // 四角の4つの角
	Vector2 position; // Entity の位置（Pivot の所）
};

/// <summary>
/// UI を押してから離すまでの状態（離したときに、押す前と後を1件の履歴にする）
/// </summary>
struct UIDragState {
	bool isPressed = false;        // UI の上で押して、まだ離していないか
	bool isDragging = false;       // 押したまま少し動かしたか（動かすまでは位置を変えない。クリックで選ぶだけのとき、ずれないように）
	Handle<Entity> entity;         // 動かしている物
	std::vector<std::byte> before; // 押したときの Transform（ComponentEditor::Save の写し）
	Vector3 startTranslation;      // 押したときの Translation
	Matrix4x4 parentInverse;       // 親のワールド行列の逆行列（画面で動かした量を、親から見た量に直す）
	ImVec2 startMouse;             // 押したときのマウスの位置（画面の座標）
	// --- 大きさを変えるときだけ使う ---
	Vector2 handle = {0.0f, 0.0f};      // つかんだ印（kHandles の値。0,0 なら位置を動かす）
	std::vector<std::byte> beforeShape; // 押したときの Vector Shape（ComponentEditor::Save の写し）
	Vector2 startSize;                  // 押したときの Size
};
UIDragState drag;

//=============================================================================
// UI の形
//=============================================================================
// 画面に重ねる UI の形（Image・Vector Shape があれば Size の四角、無ければ Text の文字のかたまり）
// どれも無い・描いていない（無効・目で隠した）・選べない（画面の編集中の、画面の外の物）物は false
bool CalcShape(Handle<Entity> entity, UIShape& shape) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity) || !SceneVisibility::IsPickable(entity)) {
		return false;
	}
	const Matrix4x4& m = transform->worldMatrix;
	if (const ImageComponent* image = EntityManager::Get<ImageComponent>(entity); image != nullptr && image->enabled) {
		shape.quad = SpriteRenderSystem::CalcScreenQuad(m, image->anchor, image->pivot, image->size); // Fill は掛けない（Image の大きさ全部）
		shape.position = SpriteRenderSystem::CalcScreenPoint(m, image->anchor, {0.0f, 0.0f});
		return true;
	}
	if (const VectorShapeComponent* vectorShape = EntityManager::Get<VectorShapeComponent>(entity); vectorShape != nullptr && vectorShape->enabled) {
		shape.quad = SpriteRenderSystem::CalcScreenQuad(m, vectorShape->anchor, vectorShape->pivot, vectorShape->size);
		shape.position = SpriteRenderSystem::CalcScreenPoint(m, vectorShape->anchor, {0.0f, 0.0f});
		return true;
	}
	if (const TextComponent* text = EntityManager::Get<TextComponent>(entity); text != nullptr && text->enabled) {
		shape.quad = SpriteRenderSystem::CalcScreenQuad(m, text->anchor, text->pivot, TextRenderSystem::CalcTextSize(entity));
		shape.position = SpriteRenderSystem::CalcScreenPoint(m, text->anchor, {0.0f, 0.0f});
		return true;
	}
	return false;
}

// 画面の座標（Scene ビューの絵の上。Y は下向き）→ 正規化座標（真ん中が0、右と上が＋、端が±1）
Vector2 ToNdc(const ImVec2& point, const ImVec2& imageMin, const ImVec2& imageSize) { return {(point.x - imageMin.x) / imageSize.x * 2.0f - 1.0f, 1.0f - (point.y - imageMin.y) / imageSize.y * 2.0f}; }

// 正規化座標 → 画面の座標（ToNdc の逆）
ImVec2 ToImage(const Vector2& ndc, const ImVec2& imageMin, const ImVec2& imageSize) { return {imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x, imageMin.y + (0.5f - ndc.y * 0.5f) * imageSize.y}; }

//=============================================================================
// クリックで選ぶ
//=============================================================================
// point の下にある UI のうち、一番手前に描いてある物（描く順と同じ：Order が大きい物。同じなら番号が大きい＝後に作った物）
Handle<Entity> PickUI(const Vector2& point) {
	Handle<Entity> best;
	int bestOrder = 0;
	EntityId bestId = 0;
	const auto consider = [&](Handle<Entity> entity, int order) {
		UIShape shape;
		if (!CalcShape(entity, shape) || !SpriteRenderSystem::IsInsideQuad(point, shape.quad)) {
			return;
		}
		const Entity* owner = EntityManager::Get(entity);
		const EntityId id = (owner != nullptr) ? owner->id : 0;
		// std::tie：「1つ目を比べて、同じなら2つ目」をまとめて書ける（描く順の並べ方と同じ）
		if (!best.IsValid() || std::tie(order, id) > std::tie(bestOrder, bestId)) {
			best = entity;
			bestOrder = order;
			bestId = id;
		}
	};
	EntityManager::ForEach<ImageComponent>([&consider](Handle<Entity> entity, const ImageComponent& image) { consider(entity, image.order); });
	EntityManager::ForEach<VectorShapeComponent>([&consider](Handle<Entity> entity, const VectorShapeComponent& shape) { consider(entity, shape.order); });
	EntityManager::ForEach<TextComponent>([&consider](Handle<Entity> entity, const TextComponent& text) { consider(entity, text.order); });
	return best;
}

//=============================================================================
// ドラッグで動かす
//=============================================================================
// 押した：今の Transform を覚える（動かし始めるのは、少し動かしてから）。handle は大きさを変える印（0,0 なら位置を動かす）
void BeginPress(Handle<Entity> entity, const Vector2& handle = {0.0f, 0.0f}) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const ComponentEditor* editor = ComponentEditorRegistry::Find<TransformComponent>();
	if (transform == nullptr || editor == nullptr) {
		return;
	}
	drag.isPressed = true;
	drag.isDragging = false;
	drag.entity = entity;
	drag.before = editor->Save(transform);
	drag.startTranslation = transform->translation;
	// 親のワールド行列（親がいなければ単位行列）の逆行列
	const Entity* owner = EntityManager::Get(entity);
	const TransformComponent* parent = (owner != nullptr) ? EntityManager::Get<TransformComponent>(owner->parent) : nullptr;
	drag.parentInverse = (parent != nullptr) ? Inverse(parent->worldMatrix) : MakeIdentity4x4();
	drag.startMouse = ImGui::GetMousePos();
	// --- 大きさを変えるときは、Vector Shape も覚える ---
	const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(entity);
	const ComponentEditor* shapeEditor = ComponentEditorRegistry::Find<VectorShapeComponent>();
	const bool isResize = (handle.x != 0.0f) || (handle.y != 0.0f);
	if (isResize && shape != nullptr && shapeEditor != nullptr) {
		drag.handle = handle;
		drag.beforeShape = shapeEditor->Save(shape);
		drag.startSize = shape->size;
	}
}

// 画面の上で動かした量（Y は上が＋）を、親から見た量に直す（親が回っている・大きくしてある物も、マウスについてくるように）
// 動かした量は「向き」なので、点の変換の差（移動の分が引かれて消える）で、回転と大きさだけを戻す
Vector3 ToParentMove(const Vector3& worldMove) { return TransformPoint(worldMove, drag.parentInverse) - TransformPoint({0.0f, 0.0f, 0.0f}, drag.parentInverse); }

// 大きさを変える：つかんだ辺だけがマウスについてきて、反対の辺は動かない
void UpdateResize(TransformComponent& transform, VectorShapeComponent& shape, const Vector3& worldMove) {
	// --- Entity の横・縦の軸（画面の上の向き。長さが scale）に、マウスの動きを分ける（回っていても、つかんだ辺の向きに伸びる）---
	const Matrix4x4& m = transform.worldMatrix;
	const Vector2 axisX = {m.m[0][0], m.m[0][1]};
	const Vector2 axisY = {m.m[1][0], m.m[1][1]};
	const float lengthSqX = (std::max)(axisX.x * axisX.x + axisX.y * axisX.y, kMinAxisLength);
	const float lengthSqY = (std::max)(axisY.x * axisY.x + axisY.y * axisY.y, kMinAxisLength);
	const float moveX = (worldMove.x * axisX.x + worldMove.y * axisX.y) / lengthSqX; // Size の単位（scale を掛ける前のピクセル）
	const float moveY = (worldMove.x * axisY.x + worldMove.y * axisY.y) / lengthSqY;
	// --- 1 ピクセルずつ。つかんだ辺の外向きに動かすと大きくなる ---
	shape.size = {
	    (std::max)(std::round(drag.startSize.x + drag.handle.x * moveX), 0.0f),
	    (std::max)(std::round(drag.startSize.y + drag.handle.y * moveY), 0.0f),
	};
	// --- 位置は Pivot の所なので、反対の辺が動かないように、伸びた分のうち Pivot の割合だけ動かす ---
	// 右（上）の辺をつかんだ：伸びた分 × pivot / 左（下）の辺をつかんだ：伸びた分 × (pivot − 1)
	const auto calcShift = [](float grown, float handle, float pivot) { return (handle == 0.0f) ? 0.0f : grown * ((handle > 0.0f) ? pivot : pivot - 1.0f); };
	const float shiftX = calcShift(shape.size.x - drag.startSize.x, drag.handle.x, shape.pivot.x);
	const float shiftY = calcShift(shape.size.y - drag.startSize.y, drag.handle.y, shape.pivot.y);
	const Vector3 worldShift = {axisX.x * shiftX + axisY.x * shiftY, axisX.y * shiftX + axisY.y * shiftY, 0.0f};
	transform.translation = drag.startTranslation + ToParentMove(worldShift);
}

// 押している間：押した所からマウスが動いた分だけ、Translation を動かす（印をつかんでいたら、大きさを変える）
void UpdateDrag(const ImVec2& imageSize) {
	TransformComponent* transform = EntityManager::Get<TransformComponent>(drag.entity);
	if (transform == nullptr) {
		drag = {}; // ドラッグ中に消された（Undo など）
		return;
	}
	// 少し動かすまでは動かさない（IsMouseDragging：押した所から決まった距離より動いたか。ImGui の io.MouseDragThreshold）
	if (!drag.isDragging && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		return;
	}
	drag.isDragging = true;
	// --- マウスが動いた量（画面のピクセル）→ 1280×720 の画面のピクセル（UI の位置の単位。Y は上が＋）---
	const ImVec2 mouse = ImGui::GetMousePos();
	const Vector3 worldMove = {
	    (mouse.x - drag.startMouse.x) / imageSize.x * Renderer::kSpriteBaseWidth,
	    -(mouse.y - drag.startMouse.y) / imageSize.y * Renderer::kSpriteBaseHeight,
	    0.0f,
	};
	// --- 大きさを変える ---
	if (drag.handle.x != 0.0f || drag.handle.y != 0.0f) {
		if (VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(drag.entity)) {
			UpdateResize(*transform, *shape, worldMove);
		}
		return;
	}
	// --- 位置を動かす ---
	const Vector3 localMove = ToParentMove(worldMove);
	// 1 ピクセルずつ（Inspector の数字が 150.37 のようにならないように）。Z はそのまま
	transform->translation = {
	    std::round(drag.startTranslation.x + localMove.x),
	    std::round(drag.startTranslation.y + localMove.y),
	    drag.startTranslation.z,
	};
}

// 離した：動かしていたら、押す前と後を1件の履歴にする（Inspector で値を変えたときと同じ入口）
void EndPress() {
	const ComponentEditor* editor = ComponentEditorRegistry::Find<TransformComponent>();
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(drag.entity);
	if (drag.isDragging && editor != nullptr && transform != nullptr) {
		EditorHistory::RecordComponentChange(drag.entity, *editor, drag.before, editor->Save(transform));
		// 大きさを変えたときは Vector Shape も（Commit までの変更が1件の履歴になる）
		const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(drag.entity);
		const ComponentEditor* shapeEditor = ComponentEditorRegistry::Find<VectorShapeComponent>();
		if (!drag.beforeShape.empty() && shape != nullptr && shapeEditor != nullptr) {
			EditorHistory::RecordComponentChange(drag.entity, *shapeEditor, drag.beforeShape, shapeEditor->Save(shape));
		}
		EditorHistory::CommitComponentChanges();
	}
	drag = {};
}

//=============================================================================
// 大きさを変える印
//=============================================================================
// 印の場所（正規化座標）。四角の4つの角を混ぜて出す（回っていても、そのままの形で出る）
Vector2 CalcHandlePosition(const ScreenQuad& quad, const Vector2& handle) {
	const float u = (handle.x + 1.0f) * 0.5f;
	const float v = (handle.y + 1.0f) * 0.5f;
	const Vector2 bottom = quad.lb * (1.0f - u) + quad.rb * u;
	const Vector2 top = quad.lt * (1.0f - u) + quad.rt * u;
	return bottom * (1.0f - v) + top * v;
}

// マウスの下の印（無ければ 0,0）
Vector2 FindHandle(const ScreenQuad& quad, const ImVec2& imageMin, const ImVec2& imageSize) {
	const ImVec2 mouse = ImGui::GetMousePos();
	for (const Vector2& handle : kHandles) {
		const ImVec2 point = ToImage(CalcHandlePosition(quad, handle), imageMin, imageSize);
		if (std::hypot(point.x - mouse.x, point.y - mouse.y) <= kHandleHitRadius) {
			return handle;
		}
	}
	return {0.0f, 0.0f};
}

//=============================================================================
// 枠を描く
//=============================================================================
void DrawShape(ImDrawList* drawList, const UIShape& shape, const ImVec2& imageMin, const ImVec2& imageSize, ImU32 color, bool drawPivot) {
	const auto toImage = [&imageMin, &imageSize](const Vector2& ndc) { return ToImage(ndc, imageMin, imageSize); };
	// 左下 → 右下 → 右上 → 左上 の順に回る四角（回っていても、そのままの形で描ける）
	drawList->AddQuad(toImage(shape.quad.lb), toImage(shape.quad.rb), toImage(shape.quad.rt), toImage(shape.quad.lt), color, kOutlineThickness);
	if (drawPivot) {
		drawList->AddCircle(toImage(shape.position), kPivotRadius, color, 0, kOutlineThickness);
	}
}

// 大きさを変える印を描く（白い四角。枠の色で縁取る）
void DrawHandles(ImDrawList* drawList, const ScreenQuad& quad, const ImVec2& imageMin, const ImVec2& imageSize) {
	for (const Vector2& handle : kHandles) {
		const ImVec2 point = ToImage(CalcHandlePosition(quad, handle), imageMin, imageSize);
		const ImVec2 min = {point.x - kHandleHalfSize, point.y - kHandleHalfSize};
		const ImVec2 max = {point.x + kHandleHalfSize, point.y + kHandleHalfSize};
		drawList->AddRectFilled(min, max, IM_COL32_WHITE);
		drawList->AddRect(min, max, kSelectedColor);
	}
}
} // namespace

//=============================================================================
// 描画
//=============================================================================
bool UIGizmo::Draw(Handle<Entity> selected, const ImVec2& imageMin, const ImVec2& imageSize, bool canUseMouse) {
	// --- 押している間は動かす。離したら履歴に積む（マウスが絵の外へ出ても、離すまで続ける）---
	if (drag.isPressed) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			UpdateDrag(imageSize);
		} else {
			EndPress();
		}
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true); // 絵の外（黒い余白）には描かない
	// --- マウスの下の UI（Pick UI がオンのときだけ。押している間は探さない）---
	const bool canPick = EditorViewport::GetViewFlags().scenePickUI && canUseMouse && !drag.isPressed;
	const Handle<Entity> hovered = canPick ? PickUI(ToNdc(ImGui::GetMousePos(), imageMin, imageSize)) : Handle<Entity>{};
	UIShape shape;
	if (hovered.IsValid() && hovered != selected && CalcShape(hovered, shape)) {
		DrawShape(drawList, shape, imageMin, imageSize, kHoveredColor, false);
	}
	// --- 選んでいる UI の枠と、位置（Pivot の所）の印。Vector Shape には大きさを変える印も出す ---
	Vector2 hoveredHandle = {0.0f, 0.0f};
	if (CalcShape(selected, shape)) {
		DrawShape(drawList, shape, imageMin, imageSize, kSelectedColor, true);
		if (EntityManager::Get<VectorShapeComponent>(selected) != nullptr) {
			DrawHandles(drawList, shape.quad, imageMin, imageSize);
			hoveredHandle = canPick ? FindHandle(shape.quad, imageMin, imageSize) : Vector2{0.0f, 0.0f};
		}
	}
	drawList->PopClipRect();

	// --- クリック：印の上なら大きさを変える（手前の UI より先に見る。印は枠の外へ少し出るので）---
	if ((hoveredHandle.x != 0.0f || hoveredHandle.y != 0.0f) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		BeginPress(selected, hoveredHandle);
		return true;
	}
	// --- クリック：UI の上なら、それを選んで、ドラッグで動かせるようにする ---
	if (hovered.IsValid() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		HierarchyWindow::SelectAndReveal(hovered); // Hierarchy の一覧でも見えるようにする
		BeginPress(hovered);
		return true;
	}
	return drag.isPressed;
}

bool UIGizmo::IsScreenUI(Handle<Entity> entity) {
	return EntityManager::Get<ImageComponent>(entity) != nullptr || EntityManager::Get<VectorShapeComponent>(entity) != nullptr || EntityManager::Get<TextComponent>(entity) != nullptr;
}
#endif
```

### C-4 `Editor/Gizmos/SceneGizmo.cpp`

コメントだけ。

変更後：

```cpp
	// 画面に重ねる UI（Image・Vector Shape・Text）は出さない（位置が画面のピクセルなので、3D の矢印では動かせない。UIGizmo がドラッグで動かす）
```

---

## パートD 確認

### D-1 確認用のシーン

`Docs/Tasks/VectorShapeTest.scene.json` に、全部の図形を並べたシーンを置いた（43 個）。

1. ゲームの `project/resources/scenes/` に写す。
2. Project ウィンドウでダブルクリックして開く（または `main.cpp` の `Engine::Initialize` の名前を `"VectorShapeTest"` にする）。

| 高さ | 中身 |
|---|---|
| 1段目 | 6種類の塗り |
| 2段目 | 塗り＋縁取り（Inside・Center・Outside の順に2周）＋直線のグラデーション |
| 3段目 | 線だけ |
| 4段目 | ゲージ（直線・平行四辺形・縦・リング・扇形・メーター）、Order の重なり、回転、横に伸ばした Scale |
| 5段目 | Alpha Group、Add、Multiply、Screen、放射のグラデーション |
| 左上・右下 | Anchor（TopLeft・BottomRight） |

自分で1から作るときは、次の順。

1. Hierarchy の ＋ → Vector Shape。
2. Inspector の Shape で形を選ぶ（効かない項目は灰色になる）。
3. Style で塗り方を選ぶ。線だけなら `Stroke`。
4. ゲージにするなら Gauge を `Linear` か `Radial` にして、Amount を動かす。
5. Scene ビューで図形を選んで、枠の白い印をドラッグする。
6. Control の Save を押す。

`.uiscreen` の確認には、次のファイルを `resources/ui/ShapeUI.uiscreen` として置く。Project ウィンドウでダブルクリックすると編集でき、ゲームからは `UIScreen::Open("ShapeUI")` で出る。

```json
{
"version": 1,
"entities": [
{"id": 1, "name": "ShapeUI", "parent": 0, "active": true, "components": {"Transform": {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]}, "UI Screen": {}}},
{"id": 2, "name": "Panel", "parent": 1, "active": true, "components": {"Transform": {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]}, "Vector Shape": {"shape":"Parallelogram","size":[420.0,120.0],"skewAngle":25.0,"cornerRadius":[10.0,0.0,0.0,0.0],"style":"FillAndStroke","strokeWidth":6.0,"strokeColor":[1.0,1.0,1.0,1.0],"color":[0.1,0.6,0.9,0.9],"order":20}}},
{"id": 3, "name": "Label", "parent": 1, "active": true, "components": {"Transform": {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]}, "Text": {"text":"UI Screen","fontSize":44.0,"order":21}}}
]
}
```

Script から値を動かすときは、書くだけでよい。

```cpp
if (VectorShapeComponent* shape = GetComponent<VectorShapeComponent>()) {
	shape->gaugeAmount = value / maxValue; // 次のフレームの絵に出る
}
```

### D-2 確認項目

「写しで確認」は、一時フォルダの写しで実際に動かした結果。写経の後に、同じ項目を確かめる。

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 1 | 6種類が出る | シーンを開く | Debug・Development・Release で表示、ログにエラーなし |
| 2 | 塗り・縁取り・線だけ | 1〜3段目 | OK |
| 3 | 縁取りの太さが場所で変わらない | 2段目の楕円の縁を拡大する | 上の端・右の端とも指定の 8 ピクセル |
| 4 | 楕円の距離が正しい | （写しの Perl で総当たりと比較） | 最大 0.003 ピクセルの差 |
| 5 | Inside・Center・Outside | 2段目 | OK |
| 6 | 角ごとの半径 | 2段目の角丸四角形 | OK |
| 7 | 直線・放射のグラデーション | 2段目・5段目 | OK |
| 8 | 直線のゲージ | 4段目の左 | OK。平行四辺形の切り口が斜め |
| 9 | 円形のゲージ | リング・扇形・メーター | OK。時計回り・反時計回り・270 度 |
| 10 | Size を変えても丸みと太さが変わらない | Inspector か印で Size を変える | OK |
| 11 | Scale でゆがまない | `ScaleWide` | OK（Scale 2, 0.5 で丸みと縁取りがゆがまない） |
| 12 | 回転 | `Rot` | OK |
| 13 | Image・Text と Order で重なる | 4段目の右（Image → 図形 → 文字 → 図形） | OK |
| 14 | Alpha Group | 5段目の左 | OK（子の図形と文字が一緒に薄い） |
| 15 | Anchor・Pivot | 左上・右下の白い図形 | OK |
| 16 | Blend | 5段目 | Add・Multiply・Screen で板の四角が見えない |
| 17 | 画面の大きさ（16:9） | ウィンドウの大きさを変える | 800×450 くらいと 1760×990 くらいで崩れない |
| 18 | クリックで選ぶ・動かす | Scene ビューでドラッグ | OK |
| 19 | 印で大きさを変える | 白い印をドラッグ | 反対の辺が動かない |
| 20 | Undo・Redo | Ctrl+Z・Ctrl+Y | 大きさの変更（Transform と Size）が1回で戻る・進む |
| 21 | ＋ メニュー | Hierarchy の ＋ → Vector Shape | OK |
| 22 | 保存して読み直す | Save → 起動し直す | 足した図形の所以外、全ピクセル一致 |
| 23 | Prefab | Save As Prefab → 置く | 置いた物を保存し直して、id と位置以外の行が一致 |
| 24 | `.uiscreen` | ダブルクリックで編集 → 印で大きさを変える → Done | ファイルに Size と位置が保存された |
| 25 | `UIScreen::Open` | ゲームから出す | 図形と文字が出る |
| 26 | Play / Stop | Game ビューで押す | 図形が出たまま、ログにエラーなし |
| 27 | 毎フレーム値を変える | Script で値を書く | ゲージ・角の丸み・Size・傾き・線の太さ・グラデーションの向きが動く。PSO は増えない |
| 28 | 極端な値 | 半径 999・傾き 80・太さ 0・Amount 0 と 5・Size 0・Scale 0・Scale マイナス | 落ちない。NaN の点は無い。消えるべき物は消える |

### D-3 検証の結果

- **ビルド：** Debug・Development・Release の3構成が通った。A の後、A＋B の後も Development で通った。
- **実行：** 3構成とも起動と終了を確かめた（終了コード 0）。ログに警告とエラーは無かった。
- **エディタの操作：** ImGui へ入力を流して確かめた（本物のマウス・キーボードは使っていない）。

### D-4 未確認のこと・気を付けること

- **16:9 以外のウィンドウ：** この md だけだと、Release のウィンドウを 16:9 以外にしたとき、図形・Image・Text・3D の全部が一緒に伸びる。[GameScreen.md](GameScreen.md)（黒帯とゲームの画面の比率）を写すと直る。
- **`VectorShapeSystem::IsMouseOver`：** 呼んで確かめていない（中身は Image の `IsMouseOver` と同じ形）。
- **UI Screen の Scale の動きと、Alpha Group のフェードの動き：** 動いている途中の絵は見ていない。止まった値では確かめた。
- **ビルドの警告：** エラーだけを見た。警告の数は確かめていない。
- **同じ Order の Image と Text：** UI ギズモのクリックは番号の大きい物を選ぶが、描く順は Image → Vector Shape → Text。重ねるときは Order を分ける（今の Image と Text と同じ）。
- **Release のシェーダー：** ゲームのビルドの後に xcopy で `HTTEngine/Shader` へ写る。`.shader` だけ直したときは、ビルドし直すまで Release には入らない。
