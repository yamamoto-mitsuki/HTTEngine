参考：
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- 前の段階：[Polyline.md](Polyline.md)・[VectorShape.md](VectorShape.md)・[UIAnimation.md](UIAnimation.md)
- 子を図形の形で切り抜く考え：[Mask（Unity uGUI）](https://docs.unity3d.com/Packages/com.unity.ugui@2.0/manual/script-Mask.html)
- ステンシルの設定：[D3D12_DEPTH_STENCIL_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_depth_stencil_desc)・[D3D12_STENCIL_OP](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_stencil_op)・[OMSetStencilRef](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetstencilref)
- テクスチャの繰り返し（WRAP）：[D3D12_TEXTURE_ADDRESS_MODE](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_texture_address_mode)
- 比較の `= default`（`<=>`、C++20）：https://en.cppreference.com/w/cpp/language/default_comparisons
- `std::ranges::stable_sort`（比べ方と射影）：https://en.cppreference.com/w/cpp/algorithm/ranges/stable_sort
- `std::copy_n`：https://en.cppreference.com/w/cpp/algorithm/copy_n

# UI 段階6（2/2）：マスクと模様（UI Mask・Image の繰り返し）

資料のホーム背景の「うにょうにょ」は、① UV で作るドット模様 ② 折れ線のマスク ③ 折れ線のアウトライン ④ Titan で頂点を動かす、の4つで作っている。この md で ①・② を作り、③ はマスクの図形の縁取りを子の上に描くことで代わりにする。④ は [Polyline.md](Polyline.md) で作った。

- **UI Mask**：同じ Entity の Vector Shape か Polyline の形で、子の UI を切り抜く。入れ子にすると共通部分だけになる
- **Image の繰り返し**：Tiled・Tile Size・Tile Offset。ドットなどの模様を、Size を変えても伸ばさずに並べる。Tile Offset を Timeline で動かすと流れる
- **見本**：波打つ図形とドット模様と縁取りを組み合わせた背景（WaveBackground）と、マスクの確認（MaskTest）

**[Polyline.md](Polyline.md) の後に写す**（マスクの形に Polyline を使う）。ブランチは Polyline.md の `feature/Polyline` のまま続ける（2つの md で1つの段階）。

パートは **A → B の順に、続けて写す**。A だけではビルドできない（2D の Config の `order` を `drawKey` に変えるので、B で呼ぶ側を直すまで通らない）。C は見本（ゲーム側へ写す）、D は確認、E は写しで確かめた結果。

## 0章 全体の形

### 0.1 流れ

```
Image・Vector Shape・Polyline・Text の System
   │  UIMaskSystem に「この Component の描き方」を聞く
   │    ・重ねる順のキー（外にあるマスクの Order から順に並べた段＋自分）
   │    ・ステンシル（マスクの中なら「値が、外にあるマスクの数と同じ所だけ」に描く）
   │    ・マスクの図形なら4回：塗り → 形を書く（+1）→（ここに子が入る）→ 形を消す（−1）→ 縁取り
   ▼
RenderQueue::Flush2d：drawKey の順に並べる（マスクの子は、マスクの始まりと終わりの間に入る）
   │  ステンシルの使い方が変われば PSO を、参照値が変われば OMSetStencilRef をセットし直す
   ▼
画面（ステンシルは、Scene・Game ビュー・ウィンドウの絵を描く前に、深度と一緒に 0 にしてある）
```

### 0.2 作る物

| ファイル | 役目 |
|---|---|
| `Components/UI/UIMaskComponent.h`（新しい） | UI Mask（保存名 `UI Mask`、カテゴリ UI）。Enabled・Show Graphic |
| `Systems/UI/UIMaskSystem.h`・`.cpp`（新しい） | UI Mask の登録。UI の描き方（重ねる順のキー・ステンシル・図形のどこを描くか）を決める |
| `Graphics/Pipeline/RenderStates` | ステンシルの使い方（`StencilMode`）・重ねる順のキー（`UIDrawKey`）・図形のどこを描くか（`UIDrawPart`）・2D の描き分け（`Draw2dTarget`） |
| `Graphics/Pipeline/PSOManager` | PSO のキーにステンシルを足す |
| `Graphics/Renderer/DrawRequest.h`・`Renderer`・`RenderQueue` | 2D の `order` を `drawKey` にする。並べ方とステンシルのセット |
| `Graphics/RenderTarget/RenderTexture.cpp`・`RenderWindow.cpp` | 深度と一緒にステンシルも 0 にする |
| `VectorShape.PS`・`Polyline.PS` | 塗りだけ・縁取りだけ・形だけを描き分ける（`drawPart`） |
| `Systems/Rendering2D/SpriteRenderSystem.cpp` | Image をマスクの中で描く。繰り返し（Tiled） |
| `Components/UI/ImageComponent.h` | Tiled・Tile Size・Tile Offset |
| `VectorShapeSystem`・`PolylineSystem`・`TextRenderSystem`・`SceneTransition`・`UIGizmo` | 描き方を UIMaskSystem に聞く |
| `Systems/Rendering2D/UIDrawOrder.h`（消す） | 並べ方は RenderQueue が drawKey でするので、要らなくなった |

### 0.3 使い方

- 切り抜く形にする Entity に、Vector Shape か Polyline と、Add Component → UI → **UI Mask** を付ける。切り抜かれる物は、その子（孫も）にする。
- **Show Graphic**：図形も描くか。外すと、子を切り抜くだけで図形は見えない。
- 図形の縁取り（Vector Shape の Stroke・Polyline の線）は、子より手前に描く。切り口はなめらかにならないので、縁取りで隠す。
- 模様：Image の **Tiled** をオンにして、**Tile Size**（1 枚の大きさのピクセル。0 なら画像の大きさ）、**Tile Offset**（1 で1枚分ずれる）。

資料（Unity）の作り方との対応：

| 資料 | ここ |
|---|---|
| RawImage の UV Rect の W・H でドット模様 | Image の Tiled・Tile Size |
| uGUI の Mask と折れ線で、ほかの UI をくり抜く | UI Mask と Polyline（閉じた形） |
| 折れ線のアウトラインを、自動で合わせる Component | マスクの Polyline の線（Fill And Stroke か Stroke）。子の上に描くので、別の Component は要らない |
| Titan で頂点を動かす | Timeline の Value トラック（`point2` など。Polyline.md） |

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 切り抜き方 | ステンシル（uGUI の Mask と同じ） | 図形・折れ線・ゲージのどんな形も同じ方法で切り抜ける。子は Image・図形・折れ線・Text のどれでも、シェーダーを変えずに切り抜ける |
| 切り口 | なめらかにならない（ピクセルの真ん中が形の中か外か） | ステンシルは中か外かしか持てない。縁取りを子の上に描いて隠す（資料もアウトラインを重ねている） |
| 入れ子 | 内のマスクは、外のマスクの中だけを切り抜く（共通部分）。8 段まで | ステンシルの値＝外にあるマスクの数。内のマスクは「値が自分の外の数と同じ所」だけを 1 増やす（1.1） |
| 重ねる順 | マスクの子は、マスクの図形の Order の所にまとめて描く。子どうしは子の Order | ステンシルの値は描く順に積み上がるので、マスクの始まりと終わりの間に、ほかの物を挟めない（Unity の Canvas と同じ考え） |
| マスクの外の重ねる順 | 今までと同じ（Order → Image・Vector Shape・Polyline・Text の順 → 先に作った物が奥） | ただし、同じ Order の Text どうしは、今までの「見つけた順」から「先に作った物が奥」に変わる（Image・Vector Shape と同じになった） |
| 同じ Entity のほかの UI | 中身として切り抜く | Vector Shape のマスクに Text を付けると、Text も形で切り抜ける。子を作らずに済む |
| 図形が両方ある Entity | Vector Shape を切り抜く形にする | 決めておく |
| 図形が無い・無効 | 切り抜かない（子はそのまま描く） | uGUI と同じ |
| マスクの図形の描き方 | 塗りを子の奥、縁取りを子の手前に分けて描く | 縁取りで切り口を隠す |
| 形だけの手順 | 色を出さない（Normal の重ね方で透明）。Hierarchy の目で隠しても描く | 目で隠したマスクの子が、Scene ビューで消えないように |
| ステンシルを 0 にする所 | Scene・Game ビュー・ウィンドウの絵を描く前（深度と一緒にクリア） | 3D はステンシルを使っていないので、2D の前に 0 になっていればよい |
| 閉じた折れ線のマスク | 塗りの内側で切り抜く。Style が Stroke でも、切り抜く形として塗りを作る | 縁取りだけの形でも、中を切り抜ける |
| 開いた折れ線のマスク | 線の太さの所（帯）で切り抜く | 帯で切り抜ける |
| 模様 | Image に足す（Tiled・Tile Size・Tile Offset）。画像全体を並べる | 資料と同じく UV で繰り返す。サンプラーが WRAP なので、UV を伸ばすだけで繰り返す |
| Tile Size | ピクセル（0 は画像の大きさ） | Size を変えても模様は伸びず、並ぶ数が変わる |
| Tile Offset | 1 で1枚分 | 0 → 1 を繰り返せば、つなぎ目無く流れる |

### 0.5 入れ子にしたときの動作

| 場面 | どうなるか |
|---|---|
| マスク A の子にマスク B | B は A の中だけを切り抜く。B の子は A と B の両方の中（共通部分）だけに出る |
| B の図形（塗り・縁取り） | A の中だけに出る（B 自身も A の中身） |
| A の子で、B の外の物 | A の中に出る（B は関係ない） |
| 重なった兄弟のマスク A・C | それぞれの子は、それぞれの形の中だけ。A の子が C の形の中に漏れない（A を描き終わると、A の形を消してから C を描く） |
| 9 段目より内のマスク | 切り抜かない（ふつうの図形として描く）。その子は 8 段目までの中に出る |
| マスクの Order と子の Order | 子は親のマスクの Order の所に描く。子の Order が大きくても、マスクの外の、マスクより大きい Order の物は子より手前 |
| UI Mask を外す・Enabled を外す | 子は切り抜かれない |
| マスクの図形が描かれない（Size 0・ゲージの Amount 0・Alpha Group 0 など） | 形が無いので、子も出ない（Alpha Group 0 は子も透明） |
| マスクの図形の色の Alpha が 0 | 形はあるので、子は切り抜かれて出る（見えないマスク） |

## 1章 解説

### 1.1 ステンシルで切り抜く

ステンシルは、ピクセルごとに 0〜255 の数を持つ置き場所（深度と同じ D24S8 の中の 8 ビット）。描くときに「この数が参照値と同じなら描く」と調べ、描いたピクセルの数を増やす・減らすことができる。

マスクの図形は、次の順で描く（`UIMaskSystem::PlanShapeSteps`）。L は外にあるマスクの数（入れ子の深さ）。

| 順 | 描く物 | ステンシル |
|---|---|---|
| 1 | 図形の塗り（Show Graphic のとき） | 値が L の所だけ描く |
| 2 | 図形の形（色は出さない） | 値が L の所を L + 1 にする |
| 3 | 子（何個でも） | 値が L + 1 の所だけ描く |
| 4 | 図形の形（色は出さない） | 値が L + 1 の所を L に戻す |
| 5 | 図形の縁取り（Show Graphic のとき） | 値が L の所だけ描く |

```
外のマスク A（L=0）:   0 0 1 1 1 1 0 0     ← A の形を書いた後（A の中が 1）
内のマスク B（L=1）:   0 0 1 2 2 1 0 0     ← B は「値が 1 の所」だけ 2 にする（A の外は 0 のまま）
B の子：値が 2 の所だけ                   ← A と B の両方の中
B の形を消す：         0 0 1 1 1 1 0 0
A の形を消す：         0 0 0 0 0 0 0 0     ← 次のマスクは 0 から始まる
```

「値が L と同じ所だけ増やす」ので、同じピクセルに形を2回描いても、2回目は値が L + 1 になっていて通らない（2 にならない）。減らすときも同じ。

形を描くときは、PS がピクセルの真ん中が形の中か外かを決め、外のピクセルを `clip` で捨てる（捨てたピクセルはステンシルも変わらない）。だから切り口はなめらかにならない。

### 1.2 重ねる順のキー

今までの 2D は `order`（int）だけで並べていた。マスクの子を「マスクの始まりと終わりの間」に入れるために、キー（`UIDrawKey`）にした。

- 段（`parts`）：外のマスクから順に、各マスクの図形の (Order, 種類, Entity の番号)。最後が自分。
- `phase`：マスクの図形の段の「始まり」（-1）と「終わり」（+1）。ふつうの物は 0。
- `step`：同じ段・同じ phase の中の順（始まりは 塗り → 形を書く、終わりは 形を消す → 縁取り）。

比べ方：外の段から順に (Order, 種類, 番号) を比べる。全部同じで段の数が違うなら、段の少ない方はマスクの図形で、多い方はその中身なので、マスクの始まりなら前、終わりなら後。

例（M はマスク、Order 0、子 C は Order 10、マスクの外の X は Order 5）：

| 描く物 | キー | 順 |
|---|---|---|
| M の塗り | [(0, Vector Shape, M)] phase -1 step 0 | 1 |
| M の形を書く | [(0, Vector Shape, M)] phase -1 step 1 | 2 |
| C | [(0, Vector Shape, M), (10, Image, C)] | 3 |
| M の形を消す | [(0, Vector Shape, M)] phase +1 step 0 | 4 |
| M の縁取り | [(0, Vector Shape, M)] phase +1 step 1 | 5 |
| X | [(5, Image, X)] | 6 |

マスクを使わない物は段が1つなので、今までの `order` の並びと同じ（同じ Order なら Image → Vector Shape → Polyline → Text → 番号の小さい方）。

### 1.3 図形のどこを描くか

マスクの図形は、塗り・形・縁取りを分けて描くので、シェーダーに `drawPart` を渡す。

| `drawPart` | Vector Shape.PS | Polyline.PS |
|---|---|---|
| All | 今までどおり（塗りと縁取り） | PolylineSystem が塗りと線の三角形を両方描く |
| Fill | 縁取りの透明度を 0 にする | 塗りの三角形だけ描く |
| Stroke | 塗りの透明度を 0 にする | 線の三角形だけ描く |
| MaskRegion | 輪郭（とゲージ）の内側が半分より少ないピクセルを捨て、透明を出す | 閉じた形は塗り、開いた線は線の三角形を描き、半分より外を捨てて透明を出す |

透明（`(0, 0, 0, 0)`）を Normal の重ね方で描くと、色も透明度も変わらない（Normal の透明度は「出す方 0・元の方 1」で足す設定）。重ね方が None だと透明がそのまま書かれてしまうので、形だけの手順は、図形の Blend によらず Normal で描く。

### 1.4 模様（UV の繰り返し）

Image の UV を、画像1枚分（0〜1）ではなく「並べる枚数」まで伸ばす。サンプラーが WRAP（1 を超えた UV は 0 に戻る）なので、それだけで画像が繰り返す。

```
u の範囲 = Tile Offset.x 〜 Tile Offset.x + Size.x ÷ Tile Size.x
v の範囲 = Tile Offset.y 〜 Tile Offset.y + Size.y ÷ Tile Size.y
```

Size を 2 倍にすると、模様は伸びずに並ぶ数が 2 倍になる。Tile Offset を 0 → 1 に動かすと1枚分ずれて元の絵に戻るので、ループの Timeline でつなぎ目無く流れる。Fill・Flip X・Flip Y は今までどおり効く。Sheet・Rect は使わない（画像全体を並べる）。

### 1.5 あまり使われない C++

| 書き方 | 意味 |
|---|---|
| `auto operator<=>(const UIDrawKeyPart&) const = default;` | メンバーを上から順に比べる比較（`<`・`==` など）を、コンパイラに作らせる（C++20）。`std::tie` で並べて比べるのと同じ |
| `std::ranges::stable_sort(requests, 比べ方, KeyOf)` | 3つ目（射影）で、並べる物から比べる値（キー）を取り出してから比べる。stable なので、同じキーの物（文字の影と文字）は積んだ順のまま |
| `std::copy_n(from, count, to)` | 先頭から count 個を写す |

---

## パートA ステンシルと描く順の土台

**A だけではビルドできない。続けて B を写す。**

### A-1 `Graphics/Pipeline/RenderStates.h`

include を差し替える。

```cpp
#pragma once
#include <array>
#include <string>
#include <compare>
#include <cstdint>

#include <d3d12.h>
```

`DepthMode` の下に足す。

```cpp
// ステンシルの使い方（UI のマスク。値は「外にあるマスクの数」で、参照値と同じ所だけ通す）
enum class StencilMode {
	None,      // 使わない（マスクの外の UI・3D）
	Test,      // 値が参照値と同じ所だけ描く（マスクの中の UI）
	Increment, // 値が参照値と同じ所を 1 増やす（マスクの形を書く。入れ子なら、外のマスクの中だけが増える）
	Decrement, // 値が参照値と同じ所を 1 減らす（子を描き終わったマスクの形を消す）
};
```

`DrawTarget` の下に足す。

```cpp
/// <summary>
/// 2D を重ねる順の1段（Order → 種類 → Entity の番号の順に比べる。小さい方が奥）
/// </summary>
struct UIDrawKeyPart {
	int order = 0;     // Component の Order
	uint32_t kind = 0; // 同じ Order のときの順（Image → Vector Shape → Polyline → Text。UIDrawKind の番号）
	uint64_t id = 0;   // 同じ Order・種類なら、Entity の番号の小さい方（先に作った物）が奥

	// <=> を = default にすると、メンバーを上から順に比べる比較（<・== など）をコンパイラが作る
	auto operator<=>(const UIDrawKeyPart&) const = default;
};

/// <summary>
/// 2D の描く順。マスクの入れ子の数だけ段（parts）を持つ（外のマスクから順に並べ、最後が自分）
/// <para>マスクの子は、マスクの段を頭に持つので、マスクの「始まり」と「終わり」の間に並ぶ（マスクの Order の所にまとまる）</para>
/// <para>マスクの外の物は、段が1つだけ（自分の Order）。今までの Order の並びと同じになる</para>
/// </summary>
struct UIDrawKey {
	static constexpr size_t kMaxDepth = 9;        // 段の数の上限（マスクの入れ子 8 段＋自分）
	std::array<UIDrawKeyPart, kMaxDepth> parts{}; // 段（使うのは先頭から depth 個）
	uint32_t depth = 1;                           // 使っている段の数
	int32_t phase = 0;                            // マスクの図形の段：-1 始まり（子より前）/ 0 ふつう / +1 終わり（子より後）
	uint32_t step = 0;                            // 同じキーの中の順（マスクの始まりは 塗り → 形を書く、終わりは 形を消す → 縁取り）

	// Order だけの順（マスクを使わない物。シーンを切り替える幕など）
	static UIDrawKey FromOrder(int order) {
		UIDrawKey key;
		key.parts[0].order = order;
		return key;
	}
	// このキーの物が、other より先に（奥に）描かれるか
	bool operator<(const UIDrawKey& other) const;
};

// 2D の図形・折れ線のどこを描くか（VectorShape.PS・Polyline.PS の drawPart と同じ番号）
enum class UIDrawPart : uint32_t {
	All,        // 塗りと線の全部（ふつう）
	Fill,       // 塗りだけ（マスクの図形を、子より奥に描く）
	Stroke,     // 線だけ（マスクの図形の縁取りを、子より手前に描く）
	MaskRegion, // 色は出さず、形の内側だけを残す（マスクの形をステンシルに書く・消す）
};

/// <summary>
/// 2D の描き分け（DrawTarget に、重ねる順とマスクのステンシルを足した物）。2D の Config とリクエストが受け継ぐ
/// </summary>
struct Draw2dTarget : DrawTarget {
	UIDrawKey drawKey;                       // 重ねる順（RenderQueue が、この順に並べて描く）
	StencilMode stencil = StencilMode::None; // ステンシルの使い方
	uint32_t stencilRef = 0;                 // ステンシルの参照値（外にあるマスクの数）
};
```

`MakeDepthStencilDesc` の宣言を差し替える。

```cpp
	/// <summary>
	/// 引数にいれた DepthMode・StencilMode の設定を返す
	/// </summary>
	static D3D12_DEPTH_STENCIL_DESC MakeDepthStencilDesc(DepthMode mode, StencilMode stencil = StencilMode::None);
```

### A-2 `Graphics/Pipeline/RenderStates.cpp`

include に `#include <algorithm>` を足す（自分のヘッダーの下に1行あけて）。`MakeDepthStencilDesc` を全体差し替えし、下に `UIDrawKey` の比べ方を足す。

```cpp
//=============================================================================
// DepthMode・StencilMode → D3D12_DEPTH_STENCIL_DESC
//=============================================================================
D3D12_DEPTH_STENCIL_DESC RenderStates::MakeDepthStencilDesc(DepthMode mode, StencilMode stencil) {
	D3D12_DEPTH_STENCIL_DESC desc{};
	desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // 既定（手前を採用）
	
	switch (mode) {
	case DepthMode::TestWrite: // 不透明3D
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		break;

	case DepthMode::TestNoWrite: // 半透明3D（不透明の深度に対してテストはするが書かない）
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		break;

	case DepthMode::Disable: // 2D / UI / フルスクリーン
		desc.DepthEnable = FALSE;
		desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		break;
	}

	// --- ステンシル（UI のマスク）。値が参照値と同じ所だけ通し、通った所を増やす・減らす・そのまま ---
	if (stencil == StencilMode::None) {
		return desc;
	}
	D3D12_DEPTH_STENCILOP_DESC face{};
	face.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	face.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	face.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	switch (stencil) {
	case StencilMode::Increment:
		face.StencilPassOp = D3D12_STENCIL_OP_INCR_SAT; // 1 増やす（255 で止まる）。同じピクセルに2回描いても、2回目は値が違うので通らない
		break;
	case StencilMode::Decrement:
		face.StencilPassOp = D3D12_STENCIL_OP_DECR_SAT; // 1 減らす（0 で止まる）
		break;
	default:
		face.StencilPassOp = D3D12_STENCIL_OP_KEEP; // 調べるだけ
		break;
	}
	desc.StencilEnable = TRUE;
	desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	desc.StencilWriteMask = (stencil == StencilMode::Test) ? 0 : D3D12_DEFAULT_STENCIL_WRITE_MASK;
	desc.FrontFace = face; // 2D は両面を描くので、裏も同じにする
	desc.BackFace = face;
	return desc;
}


//=============================================================================
// 2D を重ねる順
//=============================================================================
bool UIDrawKey::operator<(const UIDrawKey& other) const {
	// --- 外の段から順に比べる（Order → 種類 → 番号）---
	const uint32_t common = (std::min)(depth, other.depth);
	for (uint32_t i = 0; i < common; ++i) {
		if (parts[i] != other.parts[i]) {
			return parts[i] < other.parts[i];
		}
	}
	// --- 段の数が同じ：同じ物の、マスクの始まり → 終わり、その中の手順の順 ---
	if (depth == other.depth) {
		return (phase != other.phase) ? phase < other.phase : step < other.step;
	}
	// --- 片方の段が、もう片方の頭と同じ：段の少ない方がマスクの図形で、多い方はその中身 ---
	// マスクの始まり（phase が −）なら中身より先、終わりなら後
	return (depth < other.depth) ? (phase < 0) : (other.phase > 0);
}
```

### A-3 `Graphics/Pipeline/PSOManager.h`

`PSOKey` と `PSOHash` にステンシルを足す。

```cpp
struct PSOKey {
	uint32_t shaderProgramID;
	BlendMode blendMode;
	RasterizerType rasterizerType;
	DepthMode depthMode;
	StencilMode stencilMode = StencilMode::None; // ステンシル（UI のマスク）
	bool operator==(const PSOKey&) const = default;
};
struct PSOHash {
	size_t operator()(const PSOKey& k) const {
		size_t h = static_cast<size_t>(k.shaderProgramID);
		h = h * 31 + static_cast<size_t>(k.blendMode);
		h = h * 31 + static_cast<size_t>(k.rasterizerType);
		h = h * 31 + static_cast<size_t>(k.depthMode);
		h = h * 31 + static_cast<size_t>(k.stencilMode);
		return h;
	}
};
```

`GetPSOKey` の宣言に引数を足す。

```cpp
	/// <param name="depthMode">深度設定</param>
	/// <param name="stencilMode">ステンシル設定（UI のマスク）</param>
	static PSOKey GetPSOKey(DrawCategory drawCategory, ShadingType shadingType, BlendMode blendMode = BlendMode::Normal,
		RasterizerType rasterizerType = RasterizerType::SolidBack, DepthMode depthMode = DepthMode::TestWrite, StencilMode stencilMode = StencilMode::None);
```

### A-4 `Graphics/Pipeline/PSOManager.cpp`

```cpp
PSOKey PSOManager::GetPSOKey(DrawCategory category, ShadingType shading, BlendMode blend, RasterizerType raster, DepthMode depth, StencilMode stencil) {
	return PSOKey{GetShaderProgramID(category, shading), blend, raster, depth, stencil};
}
```

`MakePSO` の深度の行：

```cpp
	desc.DepthStencilState = RenderStates::MakeDepthStencilDesc(key.depthMode, key.stencilMode);
```

### A-5 `Graphics/Renderer/DrawRequest.h`

頭のコメントに1行足す。

```cpp
// どのリクエストも DrawTarget（どのウィンドウ・どの層・目で隠したか）を受け継ぐ。Renderer が Config から写す
// 2D のリクエストは Draw2dTarget（重ねる順の drawKey・マスクのステンシル）を受け継ぐ。RenderQueue が drawKey の順に並べて描く
```

`SpriteRequest`・`TextRequest`・`VectorShapeRequest`・`PolylineRequest` の4つを、`Draw2dTarget` を受け継ぐ形にして、`int order` とその上のコメントを消す（`drawKey` に入った）。

```cpp
struct SpriteRequest : Draw2dTarget {
	// --- ジオメトリ ---
	std::array<Vertex2dData, 4> vertices;
```

```cpp
struct TextRequest : Draw2dTarget {
	// --- ジオメトリ（1文字につき4つ。左下・左上・右下・右上の順。位置は正規化座標）---
```

```cpp
struct VectorShapeRequest : Draw2dTarget {
	// --- ジオメトリ（左下・左上・右下・右上の順。位置は正規化座標、texcoord は図形の真ん中を原点にしたピクセル）---
```

```cpp
struct PolylineRequest : Draw2dTarget {
	// --- ジオメトリ（毎フレーム写さずに、PolylineSystem のメッシュを共有する。indices のうち indexStart から indexCount 個を描く）---
```

### A-6 `Graphics/Renderer/Renderer.h`

`SpriteConfig`・`Quad2dConfig`・`Text2dConfig`・`VectorShapeConfig`・`PolylineConfig` の5つを、`DrawTarget` から `Draw2dTarget` を受け継ぐ形にして、最後の `int order = 0; // 重ねる順…` の行を消す。

```cpp
	struct SpriteConfig : Draw2dTarget {
```

```cpp
	struct Quad2dConfig : Draw2dTarget {
```

```cpp
	struct Text2dConfig : Draw2dTarget {
```

```cpp
	struct VectorShapeConfig : Draw2dTarget {
```

```cpp
	struct PolylineConfig : Draw2dTarget {
```

### A-7 `Graphics/Renderer/Renderer.cpp`

`PushSprite`・`DrawText2d`・`DrawVectorShape`・`DrawPolyline` の4か所で、`static_cast<DrawTarget&>(req) = config;` と `req.order = config.order;` の2行を、1行に差し替える（重ねる順とステンシルも一緒に写す）。

```cpp
	static_cast<Draw2dTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか・重ねる順・ステンシル）をまとめて写す
```

### A-8 `Graphics/Renderer/RenderQueue.h`

`Flush2d` の上のコメントを直す。

```cpp
	// ---  発行（2Dはスプライト・文字・図形・折れ線を、drawKey の順に混ぜて描く。マスクの子は、マスクの始まりと終わりの間）---
	static void Flush2d(const std::wstring& windowTitle);
```

### A-9 `Graphics/Renderer/RenderQueue.cpp`

`// 2dの描画を並び変えて発行` の `namespace { … }` を全体差し替え（`OrderOf` を消して `KeyOf` に、`Bind2d` にステンシルを足した）。

```cpp
namespace {
// ===== 2Dの描画で、今コマンドリストにセットしている物（同じ間はセットし直さない）=====
struct State2d {
	bool isSet = false;                                // まだ何もセットしていなければ false
	DrawCategory category = DrawCategory::Sprite;      // シェーダー（スプライト・文字・図形）
	BlendMode blend = BlendMode::Normal;               // PSO のブレンド
	RasterizerType raster = RasterizerType::SolidBack; // PSO のラスタライザ
	StencilMode stencil = StencilMode::None;           // PSO のステンシル（UI のマスク）
	bool isStencilRefSet = false;                      // ステンシルの参照値をセットしたか
	uint32_t stencilRef = 0;                           // セットしたステンシルの参照値
};

// ===== 2Dのリクエストの描き分けの情報（どの種類も Draw2dTarget を受け継ぐので、std::visit で1つにする）=====
const Draw2dTarget& TargetOf(const RenderQueue::Request2d& request) {
	return std::visit([](const auto& r) -> const Draw2dTarget& { return r; }, request);
}

// ===== 2Dのリクエストの重ねる順 =====
const UIDrawKey& KeyOf(const RenderQueue::Request2d& request) { return TargetOf(request).drawKey; }

// ===== 描く物に合わせて RootSignature・PSO・ステンシルの参照値をセットする（スプライト・文字・図形が交互に来ても、変わった所だけセットし直す）=====
void Bind2d(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE heapStart, State2d& current, DrawCategory category, BlendMode blend, RasterizerType raster, const Draw2dTarget& target) {
	// --- シェーダーが変わったら、RootSignature とテクスチャの表を付け替える ---
	const bool isCategoryChanged = !current.isSet || current.category != category;
	if (isCategoryChanged) {
		const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(category));
		cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
		auto texSlot = rs.slotOf.find(RootBind::BindlessTexture);
		if (texSlot != rs.slotOf.end()) {
			cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
		}
	}
	// --- PSO（2dは深度無効なので DepthMode::Disable 固定。ステンシルはマスクの使い方で変わる）---
	if (isCategoryChanged || current.blend != blend || current.raster != raster || current.stencil != target.stencil) {
		cmdList->SetPipelineState(PSOManager::GetPSO(PSOManager::GetPSOKey(category, ShadingType::Unlit, blend, raster, DepthMode::Disable, target.stencil)).Get());
	}
	// --- ステンシルの参照値（PSO ではなくコマンドリストに持つ値。マスクの入れ子の段で変わる）---
	if (target.stencil != StencilMode::None && (!current.isStencilRefSet || current.stencilRef != target.stencilRef)) {
		cmdList->OMSetStencilRef(target.stencilRef);
		current.isStencilRefSet = true;
		current.stencilRef = target.stencilRef;
	}
	current.isSet = true;
	current.category = category;
	current.blend = blend;
	current.raster = raster;
	current.stencil = target.stencil;
}
} // namespace
```

`Flush2d` の並べる所を差し替える。

```cpp
	// drawKey の順（奥から）に並べる。ウィンドウの数だけ呼ばれるが、2回目からは並んでいるので順番は変わらない
	// 同じ drawKey（文字の影と文字など）は積んだ順のまま（stable_sort）
	std::ranges::stable_sort(instance_->requests2d_, [](const UIDrawKey& a, const UIDrawKey& b) { return a < b; }, KeyOf);
```

`Bind2d` の4つの呼び出しの最後に、リクエスト（の `Draw2dTarget`）を足す。

```cpp
			Bind2d(cmdList, heapStart, current, DrawCategory::Sprite, sprite->blendMode, sprite->rasterizerType, *sprite);
```

```cpp
			Bind2d(cmdList, heapStart, current, DrawCategory::Text, text->blendMode, RasterizerType::SolidNone, *text);
```

```cpp
			Bind2d(cmdList, heapStart, current, DrawCategory::VectorShape, shape->blendMode, RasterizerType::SolidNone, *shape);
```

```cpp
			Bind2d(cmdList, heapStart, current, DrawCategory::Polyline, polyline->blendMode, RasterizerType::SolidNone, *polyline);
```

### A-10 `Graphics/RenderTarget/RenderTexture.cpp`・`RenderWindow.cpp`

`PreDraw` のクリアに、ステンシルを足す。

`RenderTexture.cpp`：

```cpp
		// 深度と一緒にステンシルも 0 にする（UI のマスクは、外にあるマスクの数をステンシルに数える）
		cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
```

`RenderWindow.cpp`：

```cpp
	// 深度と一緒にステンシルも 0 にする（UI のマスクは、外にあるマスクの数をステンシルに数える）
	DirectXCommon::GetCommandList()->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
```

### A-11 `Scene/SceneTransition.cpp`

幕の重ねる順を差し替える。

```cpp
	config.drawKey = UIDrawKey::FromOrder((std::numeric_limits<int>::max)()); // 一番手前（どの UI の Order よりも大きい）
```

---

## パートB UI Mask と Image の模様

### B-1 `Components/UI/UIMaskComponent.h`（新しいファイル）

```cpp
#pragma once
#include <type_traits>

// 子の UI を、この Entity の図形（Vector Shape か Polyline）の形で切り抜く。Entityに付けるComponent（カテゴリ：UI）（Unity の uGUI の Mask）
// 切り抜く形：Vector Shape は輪郭の内側（ゲージで切った所も）、閉じた Polyline は塗りの内側、開いた Polyline は線の太さの所
// 両方ある Entity は Vector Shape を使う。どちらも無い・無効なら、何も切り抜かない（子はそのまま描く）
// 子（孫も）は、この図形の Order の所にまとめて描く（子どうしは子の Order の順）。同じ Entity の、ほかの UI（Text など）も中身になる
// 入れ子にすると、外のマスクと内のマスクの重なった所だけに描く（8 段まで。それより内のマスクは切り抜かない）
// 切り口はなめらかにならない（ステンシルで切るので、ピクセルごとに中か外か）。図形の縁取り（Stroke）は子より手前に描くので、切り口を隠せる

/// <summary>
/// 子の UI を図形の形で切り抜く
/// </summary>
struct UIMaskComponent {
	bool enabled = true;     // false の間は切り抜かない（図形はふつうに描く）
	bool showGraphic = true; // 切り抜く図形も描く（外すと、切り抜くだけで図形は見えない。縁取りも描かない）
};

static_assert(std::is_trivially_copyable_v<UIMaskComponent>);
```

### B-2 `Systems/UI/UIMaskSystem.h`（新しいファイル）

```cpp
#pragma once
#include <array>
#include <cstdint>

#include "HTTEngine/Components/UI/UIMaskComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"

/// <summary>
/// 画面に重ねる UI の種類（同じ Order のときに重ねる順。UIDrawKeyPart::kind に入れる）
/// </summary>
enum class UIDrawKind : uint32_t {
	Image,       // Image（一番奥）
	VectorShape, // Vector Shape
	Polyline,    // Polyline
	Text,        // Text（一番手前）
};

/// <summary>
/// UI を1回描く分の、重ねる順・ステンシル・図形のどこを描くか
/// </summary>
struct UIDrawStep {
	UIDrawKey key;                           // 重ねる順
	StencilMode stencil = StencilMode::None; // ステンシルの使い方
	uint32_t stencilRef = 0;                 // ステンシルの参照値（外にあるマスクの数）
	UIDrawPart part = UIDrawPart::All;       // 図形のどこを描くか（Image・Text は All のまま）
	bool isGraphic = true;                   // 色を出す手順か（false はステンシルだけ。Hierarchy の目で隠しても、子を切り抜くために描く）

	// Config の重ねる順とステンシルに写す
	void ApplyTo(Draw2dTarget& target) const {
		target.drawKey = key;
		target.stencil = stencil;
		target.stencilRef = stencilRef;
	}
};

/// <summary>
/// 図形（Vector Shape・Polyline）を描く手順（マスクの図形なら最大 4 回、そうでなければ 1 回）
/// </summary>
struct UIShapeSteps {
	std::array<UIDrawStep, 4> steps{}; // 手順（積む順）
	uint32_t count = 0;                // 使っている数
};


/// <summary>
/// UI Mask（子の UI を図形の形で切り抜く）と、画面に重ねる UI の描く順を決める
/// <para>マスクの図形は、塗り → 形をステンシルに書く（1 増やす）→ 子 → 形を消す（1 減らす）→ 縁取り の順に描く</para>
/// <para>子は「ステンシルの値が、外にあるマスクの数と同じ所」だけに描く。入れ子なら内のマスクは外のマスクの中だけを増やすので、重なった所だけになる</para>
/// <para>描く順は、外のマスクの Order から順に比べるキー（UIDrawKey）で決める。Image・Vector Shape・Polyline・Text の System がここで聞いてから積む</para>
/// </summary>
class UIMaskSystem {
public:
	// UI Mask を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// UI の1つの Component の描き方（マスクの中ならステンシルで切り抜く）
	/// <para>order・kind はその Component の Order と種類。切り抜きに使う図形なら、キーは図形の「始まり」の位置（UI ギズモのクリックで選ぶ順に使う）</para>
	/// </summary>
	static UIDrawStep MakeStep(Handle<Entity> entity, int order, UIDrawKind kind);

	/// <summary>
	/// 図形（Vector Shape・Polyline）を描く手順。マスクの図形なら、塗り・形を書く・形を消す・縁取りの最大 4 回
	/// </summary>
	/// <param name="hasFill">塗りがあるか（無ければ塗りの手順を作らない）</param>
	/// <param name="hasStroke">縁取り・線があるか（無ければ縁取りの手順を作らない）</param>
	static UIShapeSteps PlanShapeSteps(Handle<Entity> entity, int order, UIDrawKind kind, bool hasFill, bool hasStroke);
};
```

### B-3 `Systems/UI/UIMaskSystem.cpp`（新しいファイル）

```cpp
#include "UIMaskSystem.h"

#include <memory>
#include <algorithm>

#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"

namespace {
constexpr uint32_t kMaxMaskDepth = static_cast<uint32_t>(UIDrawKey::kMaxDepth) - 1; // マスクの入れ子の上限（キーの段の1つは自分の分）
constexpr uint32_t kMaxSearchDepth = 64;                                             // 親をたどって数えるマスクの上限（これより深い入れ子は数えない）

/// <summary>
/// 外にあるマスク（外から順）
/// </summary>
struct MaskChain {
	std::array<UIDrawKeyPart, kMaxMaskDepth> parts{}; // マスクの図形の段（Order・種類・番号）
	uint32_t count = 0;                               // マスクの数
};

//=============================================================================
// マスクを探す
//=============================================================================
// ===== Entity の切り抜きに使う図形（UI Mask が有効で、有効な Vector Shape か Polyline があれば true）=====
bool FindMaskShape(Handle<Entity> entity, UIDrawKind& kind, int& order) {
	const UIMaskComponent* mask = EntityManager::Get<UIMaskComponent>(entity);
	if (mask == nullptr || !mask->enabled) {
		return false;
	}
	if (const VectorShapeComponent* shape = EntityManager::Get<VectorShapeComponent>(entity); shape != nullptr && shape->enabled) {
		kind = UIDrawKind::VectorShape;
		order = shape->order;
		return true;
	}
	if (const PolylineComponent* polyline = EntityManager::Get<PolylineComponent>(entity); polyline != nullptr && polyline->enabled) {
		kind = UIDrawKind::Polyline;
		order = polyline->order;
		return true;
	}
	return false;
}

// ===== Entity の番号（Handle が古ければ 0）=====
EntityId IdOf(Handle<Entity> entity) {
	const Entity* owner = EntityManager::Get(entity);
	return (owner != nullptr) ? owner->id : 0;
}

// ===== 親 =====
Handle<Entity> ParentOf(Handle<Entity> entity) {
	const Entity* owner = EntityManager::Get(entity);
	return (owner != nullptr) ? owner->parent : Handle<Entity>{};
}

// ===== 外にあるマスクを、外から順に集める（includeSelf なら entity 自身のマスクも入れる）=====
// 8 段より多ければ外の 8 段だけ（内のマスクは切り抜かない。そのマスクの図形も、ふつうの図形として描かれる）
MaskChain CollectMasks(Handle<Entity> entity, bool includeSelf) {
	// --- 内から外へたどって集める ---
	std::array<UIDrawKeyPart, kMaxSearchDepth> innerFirst{};
	uint32_t found = 0;
	for (Handle<Entity> current = includeSelf ? entity : ParentOf(entity); current.IsValid() && found < kMaxSearchDepth; current = ParentOf(current)) {
		UIDrawKind kind = UIDrawKind::Image;
		int order = 0;
		if (FindMaskShape(current, kind, order)) {
			innerFirst[found++] = {order, static_cast<uint32_t>(kind), IdOf(current)};
		}
	}
	// --- 外から順に並べ直す ---
	MaskChain chain;
	chain.count = (std::min)(found, kMaxMaskDepth);
	for (uint32_t i = 0; i < chain.count; ++i) {
		chain.parts[i] = innerFirst[found - 1 - i];
	}
	return chain;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeUIMask(ComponentUI& ui, UIMaskComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと切り抜かない（図形はふつうに描く）"));
	ui.Field("Show Graphic###showGraphic", value.showGraphic, Tip("切り抜く図形も描く。外すと、子を切り抜くだけで、図形（縁取りも）は見えない"));
	ui.Label("同じ Entity の Vector Shape（無ければ Polyline）の形で、子の UI を切り抜く。縁取りは子より手前に描く");
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIMaskSystem::Initialize() {
	// UI Mask の置き場所・保存・Inspector を登録する（見せ方は DescribeUIMask 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<UIMaskComponent>();
	ComponentSerializer::Register<UIMaskComponent>("UI Mask", &DescribeUIMask);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIMaskComponent>>("UI Mask", "UI", &DescribeUIMask));
}

void UIMaskSystem::Release() {} // 持っている物は無い（描く順は毎フレーム、Component から求める）

//=============================================================================
// 描き方
//=============================================================================
UIDrawStep UIMaskSystem::MakeStep(Handle<Entity> entity, int order, UIDrawKind kind) {
	// --- この Component が、自分の切り抜きに使う図形か（なら自分のマスクの外。違えば、同じ Entity の中身として自分のマスクの中）---
	UIDrawKind maskKind = UIDrawKind::Image;
	int maskOrder = 0;
	const bool isMaskShape = FindMaskShape(entity, maskKind, maskOrder) && maskKind == kind;
	const MaskChain chain = CollectMasks(entity, !isMaskShape);

	// --- キー：外のマスクの段 → 自分の段 ---
	UIDrawStep step;
	std::copy_n(chain.parts.begin(), chain.count, step.key.parts.begin());
	step.key.parts[chain.count] = {order, static_cast<uint32_t>(kind), IdOf(entity)};
	step.key.depth = chain.count + 1;
	if (isMaskShape && chain.count < kMaxMaskDepth) {
		step.key.phase = -1; // マスクの図形の位置は「始まり」（子より奥）
	}
	// --- マスクの中なら、ステンシルの値が外のマスクの数と同じ所だけに描く ---
	step.stencil = (chain.count > 0) ? StencilMode::Test : StencilMode::None;
	step.stencilRef = chain.count;
	return step;
}

UIShapeSteps UIMaskSystem::PlanShapeSteps(Handle<Entity> entity, int order, UIDrawKind kind, bool hasFill, bool hasStroke) {
	UIShapeSteps plan;
	UIDrawStep base = MakeStep(entity, order, kind);
	// --- マスクの図形でなければ1回（入れ子が深すぎるマスクも、ふつうの図形として描く）---
	const UIMaskComponent* mask = EntityManager::Get<UIMaskComponent>(entity);
	if (base.key.phase >= 0 || mask == nullptr) {
		base.key.phase = 0;
		plan.steps[plan.count++] = base;
		return plan;
	}
	// --- マスクの図形：子より奥に塗り → 形を書く → （子）→ 形を消す → 子より手前に縁取り ---
	const uint32_t level = base.stencilRef; // 外にあるマスクの数（この図形自身は、外のマスクの中に描く）
	const StencilMode outer = (level > 0) ? StencilMode::Test : StencilMode::None;
	const auto add = [&](int32_t phase, uint32_t stepIndex, StencilMode stencil, uint32_t stencilRef, UIDrawPart part, bool isGraphic) {
		UIDrawStep& step = plan.steps[plan.count++];
		step = base;
		step.key.phase = phase;
		step.key.step = stepIndex;
		step.stencil = stencil;
		step.stencilRef = stencilRef;
		step.part = part;
		step.isGraphic = isGraphic;
	};
	if (mask->showGraphic && hasFill) {
		add(-1, 0, outer, level, UIDrawPart::Fill, true);
	}
	add(-1, 1, StencilMode::Increment, level, UIDrawPart::MaskRegion, false);
	add(+1, 0, StencilMode::Decrement, level + 1, UIDrawPart::MaskRegion, false);
	if (mask->showGraphic && hasStroke) {
		add(+1, 1, outer, level, UIDrawPart::Stroke, true);
	}
	return plan;
}
```

### B-4 `Systems/Rendering2D/UIDrawOrder.h`（消す）

ファイルを消して、プロジェクトからも外す。並べ方は RenderQueue が `drawKey` でするので、`CollectUIDrawItems` は要らなくなった。

### B-5 `Graphics/Pipeline/ShaderConstants.h`

`VectorShapeMaterialData` の最後と、`PolylineMaterialData` の最後に `drawPart` を足す（詰め物を減らすので、大きさは変わらない）。

```cpp
	uint32_t strokeUsesGradient = 0;                  // 1：縁取りにも塗りのグラデーションを使う
	uint32_t drawPart = 0;                            // 何を描くか（UIDrawPart の順。マスクの図形は、塗り・縁取り・形だけを分けて描く）
	float padA[2] = {};
};
static_assert(sizeof(VectorShapeMaterialData) == 160, "HLSLのMaterial（VectorShape.PS）と大きさが違います");
```

```cpp
	uint32_t blendMode = 0;                         // 重ね方（BlendMode の順）
	uint32_t drawPart = 0;                          // 何を描くか（UIDrawPart の順。マスクの形だけを描くときは MaskRegion）
	float padA = 0.0f;
};
static_assert(sizeof(PolylineMaterialData) == 112, "HLSLのMaterial（Polyline.hlsli）と大きさが違います");
```

### B-6 `Shader/Data/VectorShape/VectorShape.PS.shader`

`Material` の最後：

```hlsl
    uint strokeUsesGradient; // 1：縁取りにも塗りのグラデーションを使う
    uint drawPart; // 何を描くか（UIDrawPart の順）
    float32_t2 padA;
};
```

番号の `kGaugeNarrowWedge` の下に足す。

```hlsl
static const uint kPartFill = 1;
static const uint kPartStroke = 2;
static const uint kPartMaskRegion = 3;
```

`main` の `float32_t distance = CalcDistance(p);` の下に足す。

```hlsl
    // ===== 0. マスクの形（色は出さず、輪郭とゲージの内側で、ピクセルの半分より中だけを残す。ステンシルに書く・消す）=====
    if (gMaterial.drawPart == kPartMaskRegion)
    {
        float32_t region = Coverage(distance);
        if (gMaterial.gaugeMode != 0)
        {
            region *= Coverage(CalcGaugeDistance(p));
        }
        clip(region - 0.5f);
        output.color = float32_t4(0.0f, 0.0f, 0.0f, 0.0f); // 透明（Normal の重ね方なので、色は変わらない）
        return output;
    }
```

手順 2 の `strokeAlpha` の下に足す。

```hlsl
    // マスクの図形は、塗り（子より奥）と縁取り（子より手前）を分けて描く
    if (gMaterial.drawPart == kPartFill)
    {
        strokeAlpha = 0.0f;
    }
    else if (gMaterial.drawPart == kPartStroke)
    {
        fillAlpha = 0.0f;
    }
```

### B-7 `Shader/Data/VectorShape/Polyline.hlsli.shader`・`Polyline.PS.shader`

`Polyline.hlsli` の `Material` の最後：

```hlsl
    uint blendMode; // 重ね方（BlendMode の順）
    uint drawPart; // 何を描くか（UIDrawPart の順。マスクの形だけを描くときは MaskRegion）
    float32_t padA;
};
```

`Polyline.PS` の番号に足す。

```hlsl
// C++ の UIDrawPart と同じ順の番号
static const uint kPartMaskRegion = 3;
```

`main` の手順 2・3 を差し替える（手順 3 だった重ね方は 4 になる）。

```hlsl
    float32_t coverage = Coverage(d);

    // ===== 2. マスクの形（色は出さず、ピクセルの半分より中だけを残す。ステンシルに書く・消す）=====
    // 描く三角形（塗りの部分か線の部分か）は PolylineSystem が選ぶので、ここは形の内側だけを残せばよい
    if (gMaterial.drawPart == kPartMaskRegion)
    {
        clip(coverage - 0.5f);
        output.color = float32_t4(0.0f, 0.0f, 0.0f, 0.0f); // 透明（Normal の重ね方なので、色は変わらない）
        return output;
    }

    // ===== 3. 色と、輪郭のなめらかさ =====
    float32_t4 color = isStroke ? gMaterial.strokeColor : gMaterial.fillColor;
    float32_t alpha = color.a * coverage;
    float32_t3 rgb = color.rgb;
```

### B-8 `Systems/Rendering2D/SpriteRenderSystem.cpp`

1. include の `#include "HTTEngine/Systems/Rendering2D/UIDrawOrder.h"` を `#include "HTTEngine/Systems/UI/UIMaskSystem.h"` に差し替える。
2. `SpriteRenderState` と `state` を消し（Image を並べる配列は要らなくなった）、定数を1つ足す。

```cpp
constexpr float kDefaultPixelsPerUnit = 100.0f; // Pixels Per Unit に0以下が入っていたときに使う値
constexpr float kMinTileSize = 1.0e-3f;         // Tile Size の下限（ピクセル。0 で割らないように）
```

3. `DrawImages` を、`CalcImageUV`（繰り返しの UV）と `DrawImage`（1 枚描く）に差し替える。

```cpp
// ===== Image の UV。Tiled なら、画像全体を Tile Size ごとに並べて繰り返す =====
// サンプラーは WRAP（1 を超えた UV は画像の頭に戻る）なので、UV を「並べる枚数」まで伸ばすだけで繰り返す
FrameUV CalcImageUV(const ImageComponent& image) {
	if (!image.tiled || image.textureHandle == 0) {
		return CalcSourceUV(image);
	}
	const Vector2 textureSize = TextureManager::GetTextureSize(image.textureHandle);
	const Vector2 tile = {
	    (std::max)((image.tileSize.x > 0.0f) ? image.tileSize.x : textureSize.x, kMinTileSize),
	    (std::max)((image.tileSize.y > 0.0f) ? image.tileSize.y : textureSize.y, kMinTileSize),
	};
	FrameUV uv;
	uv.min = image.tileOffset;
	uv.max = {image.tileOffset.x + image.size.x / tile.x, image.tileOffset.y + image.size.y / tile.y};
	return FlipUV(uv, image.flipX, image.flipY);
}

// ===== Image：画面に重ねる（重ねる順・マスクの中かは UIMaskSystem が決め、RenderQueue が並べる）=====
void DrawImage(Handle<Entity> entity, const ImageComponent& image) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
	if (groupAlpha <= 0.0f) {
		return;
	}
	const ScreenQuad quad = CalcImageQuad(image, *transform);
	const FrameUV uv = CalcImageUV(image);
	// Fill の分だけ、右の端の U も縮める（画面の左から、Fill の割合だけ貼る。反転していても同じ）
	const float uRight = uv.min.x + (uv.max.x - uv.min.x) * std::clamp(image.fill, 0.0f, 1.0f);

	Renderer::Quad2dConfig config;
	config.lb = quad.lb;
	config.lt = quad.lt;
	config.rb = quad.rb;
	config.rt = quad.rt;
	config.uvLb = {uv.min.x, uv.max.y};
	config.uvLt = {uv.min.x, uv.min.y};
	config.uvRb = {uRight, uv.max.y};
	config.uvRt = {uRight, uv.min.y};
	config.color = ClampColor(MultiplyAlpha(image.color, groupAlpha));
	config.textureHandle = image.textureHandle;
	config.blendMode = image.blend;
	config.rasterizerType = RasterizerType::SolidNone; // 反転・回転で裏返っても消えないように、両面を描く
	UIMaskSystem::MakeStep(entity, image.order, UIDrawKind::Image).ApplyTo(config); // Order の順に重なる。マスクの中なら切り抜く
	config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	Renderer::DrawQuad2d(config);
}
```

4. `DescribeImage` の Blend の下を差し替える（Tile を足し、Tiled の間は Sheet・Rect を灰色にする）。

```cpp
	// --- 繰り返し（模様）。オンの間は Sheet・Rect を使わない（灰色にするだけで、保存はする）---
	ui.Separator("Tile");
	ui.Field("Tiled###tiled", value.tiled, Tip("画像全体を並べて繰り返す（ドット・しま模様など）。Size を変えても模様は伸びず、並ぶ数が変わる"));
	ui.BeginDisabled(!value.tiled);
	ui.Field("Tile Size###tileSize", value.tileSize, AtLeast(0.0f).Drag(1.0f).Tip("1枚の大きさ（ピクセル）。0 の向きは画像の大きさ"));
	ui.Field("Tile Offset###tileOffset", value.tileOffset, Drag(0.01f).Tip("模様をずらす量（1 で1枚分。Timeline で動かすと模様が流れる）"));
	ui.EndDisabled();
	ui.BeginDisabled(value.tiled);
	DescribeSource(ui, value);
	ui.EndDisabled();
}
```

5. `Initialize` の最初の2行（`HTT_ASSERT_MSG` と `state = …`）と空行を消し、`Release`・`Draw` を差し替える。

```cpp
void SpriteRenderSystem::Release() {} // 持っている物は無い（Image の重ねる順は RenderQueue が並べる）

//=============================================================================
// 描く（停止中も）
//=============================================================================
void SpriteRenderSystem::Draw(Camera* camera) {
	DrawSprites(camera);
	// Image：見つけた順に積む（重ねる順は drawKey で RenderQueue が並べる）
	EntityManager::ForEach<ImageComponent>([](Handle<Entity> entity, const ImageComponent& image) {
		if (image.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			DrawImage(entity, image);
		}
	});
}
```

### B-9 `Components/UI/ImageComponent.h`

`blend` の下に足す。

```cpp
	// --- 繰り返し（模様）---
	bool tiled = false;                         // true：画像全体を tileSize ごとに並べて繰り返す（Sheet・Rect は使わない）
	Vector2 tileSize = {0.0f, 0.0f};            // 1枚の大きさ（ピクセル。0 の向きは画像の大きさ）
	Vector2 tileOffset = {0.0f, 0.0f};          // 模様をずらす量（1 で1枚分。動かすと模様が流れる）
```

### B-10 `Systems/UI/TextRenderSystem.cpp`

include に `#include "HTTEngine/Systems/UI/UIMaskSystem.h"` を足し（`SpriteRenderSystem.h` の下）、`config.order = text.order;` を差し替える。

```cpp
		UIMaskSystem::MakeStep(entity, text.order, UIDrawKind::Text).ApplyTo(config); // Image・図形とも Order の順に重なる。マスクの中なら切り抜く
```

### B-11 `Systems/UI/VectorShapeSystem.cpp`

1. include の `<vector>`・`HTTAssert.h` を消し、`UIDrawOrder.h` を `#include "HTTEngine/Systems/UI/UIMaskSystem.h"` に差し替える。
2. `VectorShapeState` と `state` を消す。
3. `DrawShape` を全体差し替え（Entity と Component を受け取る形にして、最後を描く手順のループにした）。

```cpp
void DrawShape(Handle<Entity> entity, const VectorShapeComponent& shape) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr || shape.size.x <= 0.0f || shape.size.y <= 0.0f) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
	const Vector2 scale = SpriteRenderSystem::CalcScreenScale(transform->worldMatrix);
	if (groupAlpha <= 0.0f || scale.x < kMinScale || scale.y < kMinScale) {
		return;
	}
	Renderer::VectorShapeConfig config;
	if (!MakeMaterial(shape, scale, groupAlpha, config.material)) {
		return;
	}

	// --- 板の4つの角：Size の四角を、縁取りと縁のなめらかな所が入るだけ広げる ---
	// margin は描かれるピクセルで決めるので、Transform の scale が掛かる前の長さ（ローカル）へ割り戻す
	const float margin = (std::max)(config.material.strokeOuter, 0.0f) + kEdgeMargin;
	const Vector2 localMargin = {margin / scale.x, margin / scale.y};
	const Vector2 center = {(0.5f - shape.pivot.x) * shape.size.x, (0.5f - shape.pivot.y) * shape.size.y};
	const Vector2 localHalf = {shape.size.x * 0.5f + localMargin.x, shape.size.y * 0.5f + localMargin.y};
	const auto toScreen = [&](float signX, float signY) {
		return SpriteRenderSystem::CalcScreenPoint(transform->worldMatrix, shape.anchor, {center.x + signX * localHalf.x, center.y + signY * localHalf.y});
	};
	config.lb = toScreen(-1.0f, -1.0f);
	config.lt = toScreen(-1.0f, +1.0f);
	config.rb = toScreen(+1.0f, -1.0f);
	config.rt = toScreen(+1.0f, +1.0f);
	// シェーダーへは、図形の真ん中を原点にした描かれるピクセルの場所を渡す（UV の代わり。板の中でなめらかにつながる）
	config.localHalf = {localHalf.x * scale.x, localHalf.y * scale.y};

	// --- 描く手順（ふつうは1回。マスクの図形なら、塗り → 形を書く → 子 → 形を消す → 縁取り）---
	const bool hasFill = shape.style != VectorShapeStyle::Stroke;
	const bool hasStroke = shape.style != VectorShapeStyle::Fill && shape.strokeWidth > 0.0f;
	const bool isHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	const UIShapeSteps plan = UIMaskSystem::PlanShapeSteps(entity, shape.order, UIDrawKind::VectorShape, hasFill, hasStroke);
	for (uint32_t i = 0; i < plan.count; ++i) {
		const UIDrawStep& step = plan.steps[i];
		step.ApplyTo(config);
		config.material.drawPart = static_cast<uint32_t>(step.part);
		// 形だけの手順は、色を出さない（Normal なら透明で、背景を変えない）。目で隠していても、子を切り抜くために描く
		config.blendMode = step.isGraphic ? shape.blend : BlendMode::Normal;
		config.material.blendMode = static_cast<uint32_t>(config.blendMode);
		config.isSceneHidden = isHidden && step.isGraphic;
		Renderer::DrawVectorShape(config);
	}
}
```

4. `Initialize` の最初の2行と空行を消し、`Release`・`Draw` を差し替える。

```cpp
void VectorShapeSystem::Release() {} // 持っている物は無い（重ねる順は RenderQueue が並べる）

//=============================================================================
// 描く（停止中も）
//=============================================================================
void VectorShapeSystem::Draw() {
	// 見つけた順に積む（重ねる順は drawKey で RenderQueue が並べる）
	EntityManager::ForEach<VectorShapeComponent>([](Handle<Entity> entity, const VectorShapeComponent& shape) {
		if (shape.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			DrawShape(entity, shape);
		}
	});
}
```

### B-12 `Systems/UI/PolylineSystem.cpp`

include に `#include "HTTEngine/Systems/UI/UIMaskSystem.h"` を足す（`PolylineMesh.h` の下）。

`MakeShape` に引数 `hasFill` を足す（閉じた形のマスクは、塗らなくても塗りの部分を作る）。

```cpp
// ===== メッシュを作る元の値（Scale を掛けたピクセル）。hasFill は塗りの部分を作るか（閉じた形のマスクは、塗らなくても作る）=====
// 横だけ伸ばしても角の丸み・太さがゆがまないように、半径・太さには小さい方の倍率を掛ける（Vector Shape と同じ）
PolylineShape MakeShape(const PolylineComponent& polyline, const Vector2& scale, bool hasFill) {
	PolylineShape shape;
	const float uniformScale = (std::min)(scale.x, scale.y);
	shape.count = polyline.GetCount();
	for (int i = 0; i < shape.count; ++i) {
		shape.points[i] = {polyline.points[i].x * scale.x, polyline.points[i].y * scale.y};
		shape.radii[i] = (std::max)(polyline.radii[i], 0.0f) * uniformScale;
	}
	shape.isClosed = polyline.closed;
	shape.hasFill = hasFill;
	shape.halfWidth = polyline.HasStroke() ? polyline.strokeWidth * uniformScale * 0.5f : 0.0f;
	shape.cap = polyline.cap;
	return shape;
}
```

`DrawPolyline` を全体差し替えし、上に `SelectRange` を足す。

```cpp
// ===== 描く手順ごとの、三角形の番号の範囲（番号は 塗り → 線 の順に並んでいる）=====
// 形だけの手順（マスクの形）は、閉じた形なら塗りの部分、開いた線なら線の部分
void SelectRange(const PolylineComponent& polyline, const PolylineMeshInfo& info, UIDrawPart part, uint32_t& start, uint32_t& count) {
	const uint32_t total = info.fillIndexCount + info.strokeIndexCount;
	switch (part) {
	case UIDrawPart::All:
		start = polyline.HasFill() ? 0 : info.fillIndexCount; // 塗らない形は、マスクのために作った塗りの部分を飛ばす
		count = total - start;
		break;
	case UIDrawPart::Fill:
		start = 0;
		count = info.fillIndexCount;
		break;
	case UIDrawPart::Stroke:
		start = info.fillIndexCount;
		count = info.strokeIndexCount;
		break;
	case UIDrawPart::MaskRegion:
		start = polyline.closed ? 0 : info.fillIndexCount;
		count = polyline.closed ? info.fillIndexCount : info.strokeIndexCount;
		break;
	}
}

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
	// --- 描く手順（ふつうは1回で塗りと線。マスクの図形なら、塗り → 形を書く → 子 → 形を消す → 線）---
	const UIShapeSteps plan = UIMaskSystem::PlanShapeSteps(entity, polyline.order, UIDrawKind::Polyline, polyline.HasFill(), polyline.HasStroke());
	const bool isMask = plan.count > 1; // マスクの図形だけ、手順が2回以上ある
	// 閉じた形のマスクは、塗らない形（Stroke）でも、切り抜く形として塗りの部分を作る
	const PolylineShape shape = MakeShape(polyline, scale, polyline.HasFill() || (isMask && polyline.closed));
	const PolylineCache& cache = UpdateCache(entity, shape);

	Renderer::PolylineConfig config;
	config.mesh = cache.mesh;
	config.material.toScreen = MakeToScreen(transform->worldMatrix, polyline.anchor, scale);
	config.material.fillColor = ClampColor(MultiplyAlpha(polyline.color, groupAlpha));
	config.material.strokeColor = ClampColor(MultiplyAlpha(polyline.strokeColor, groupAlpha));
	config.material.halfWidth = shape.halfWidth;
	const bool isHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	for (uint32_t i = 0; i < plan.count; ++i) {
		const UIDrawStep& step = plan.steps[i];
		SelectRange(polyline, cache.info, step.part, config.indexStart, config.indexCount);
		if (config.indexCount == 0) {
			continue;
		}
		step.ApplyTo(config);
		config.material.drawPart = static_cast<uint32_t>(step.part);
		// 形だけの手順は、色を出さない（Normal なら透明で、背景を変えない）。目で隠していても、子を切り抜くために描く
		config.blendMode = step.isGraphic ? polyline.blend : BlendMode::Normal;
		config.material.blendMode = static_cast<uint32_t>(config.blendMode);
		config.isSceneHidden = isHidden && step.isGraphic;
		Renderer::DrawPolyline(config);
	}
}
```

### B-13 `Editor/Gizmos/UIGizmo.cpp`

include の `<tuple>` を消し、`#include "HTTEngine/Systems/UI/UIMaskSystem.h"` を足す（`TextRenderSystem.h` の下）。`PickUI` を全体差し替え（クリックで選ぶ順も、描く順のキーで比べる）。

```cpp
// point の下にある UI のうち、一番手前に描いてある物（描く順と同じ：UIMaskSystem が決める重ねる順のキーが大きい物）
// マスクの子は、マスクの図形の Order の所にまとまって描かれるので、子の Order だけでは比べられない
Handle<Entity> PickUI(const Vector2& point) {
	Handle<Entity> best;
	UIDrawKey bestKey;
	const auto consider = [&](Handle<Entity> entity, int order, UIDrawKind kind) {
		UIShape shape;
		if (!CalcShape(entity, shape) || !SpriteRenderSystem::IsInsideQuad(point, shape.quad)) {
			return;
		}
		const UIDrawKey key = UIMaskSystem::MakeStep(entity, order, kind).key;
		if (!best.IsValid() || bestKey < key) {
			best = entity;
			bestKey = key;
		}
	};
	EntityManager::ForEach<ImageComponent>([&consider](Handle<Entity> entity, const ImageComponent& image) { consider(entity, image.order, UIDrawKind::Image); });
	EntityManager::ForEach<VectorShapeComponent>([&consider](Handle<Entity> entity, const VectorShapeComponent& shape) { consider(entity, shape.order, UIDrawKind::VectorShape); });
	EntityManager::ForEach<PolylineComponent>([&consider](Handle<Entity> entity, const PolylineComponent& polyline) { consider(entity, polyline.order, UIDrawKind::Polyline); });
	EntityManager::ForEach<TextComponent>([&consider](Handle<Entity> entity, const TextComponent& text) { consider(entity, text.order, UIDrawKind::Text); });
	return best;
}
```

### B-14 `Systems/EngineSystems.cpp`

```cpp
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/UIMaskSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
```

`Initialize`（Polyline の下）：

```cpp
	PolylineSystem::Initialize();         // Polyline
	UIMaskSystem::Initialize();           // UI Mask（子の UI を図形の形で切り抜く）
```

`Release`：

```cpp
	PolylineSystem::Release();
	UIMaskSystem::Release();
```

### B-15 プロジェクト

- 足す：`Components/UI/UIMaskComponent.h`・`Systems/UI/UIMaskSystem.h`・`UIMaskSystem.cpp`
- 外す：`Systems/Rendering2D/UIDrawOrder.h`

---

## パートC 見本（ゲーム側）

資料のホーム背景のような、波打つ図形にドット模様を入れて縁取りを重ねた背景（WaveBackground）と、マスクの動作を並べたシーン（MaskTest）。エンジンには入れない。`Docs/Tasks/UIMaskTest/` にある物を AL4_01 へ写す。

| Docs/Tasks/UIMaskTest/ | AL4_01 の写し先 |
|---|---|
| `textures/dot.png` | `project/Resources/textures/`（32×32 の白い丸。写しで作った） |
| `scenes/WaveBackground.scene.json`・`scenes/MaskTest.scene.json` | `project/Resources/scenes/` |
| `timelines/WaveBackground/WaveBackground.timeline` | `project/Resources/timelines/WaveBackground/` |

Debug・Development は `Parameters/Editor/LastScene.json` を `WaveBackground` か `MaskTest` にするか、Project で開く（WaveBackground は Play で動く）。Release は `main.cpp` の `Engine::Initialize(config, "GameScene")` を一時的にシーンの名前にする。

### C-1 WaveBackground の作り

```
Background（Timeline：WaveBackground.timeline、Play On Start・Loop）
├ Gradient          Vector Shape（画面全体・直線のグラデーション・Order -10）
├ TopLeftBack       Polyline（閉じた波・塗りだけ・薄いピンク・Order 0）← 少しずらした影
├ TopLeft           Polyline（閉じた波・塗り（白）と線（ピンク 6）・Order 1）＋ UI Mask
│ └ Dots            Image（dot.png・Tiled・Tile Size 22・ピンク）← 波の形で切り抜かれる
├ BottomRightBack   Polyline（右下に反対向きの波・薄い青）
└ BottomRight       Polyline（塗り（白）と線（青 6））＋ UI Mask
  └ Dots            Image（dot.png・Tiled・青）
```

- 波の縁は点 2〜7（半径 70）。Timeline の Value トラック 24 本（4 つの Polyline の点 2〜7 の y。それぞれ違う向きにずらした波）が上下に動かす。
- `TopLeft/Dots`・`BottomRight/Dots` の Tile Offset を、4 秒で 1 枚分ずらす（ループでつなぎ目無く流れる）。
- 縁取りは子より手前に描かれるので、ドットの切り口のギザギザが線の下に隠れる。

### C-2 MaskTest の中身

| 名前 | 確かめる物 |
|---|---|
| M1_Circle | 円のマスク（塗りと縁取り）。子のドット（Tiled）と Text が円で切れて、縁取りが上に出る |
| M2_Rect・M2_Circle | 入れ子。M2_Fill（赤）は四角と円の重なった所だけ。円の縁取りも四角の中だけ |
| M3_Wave | 閉じた Polyline のマスク（Style は Stroke）。中を切り抜き、線は上に出る |
| M4_Mask・M4_Child・M4_Other | 重ねる順。子（赤・Order 10）は、マスクの外の緑（Order 5）より奥 |
| M5_Band | 開いた Polyline（太さ 60）のマスク、Show Graphic なし。ドットが帯の中だけ |
| M6_Gauge | 円のゲージ（Radial・0.6）のマスク。扇の形で切り抜く |
| M7_Off | UI Mask の Enabled なし。子は切れない |
| M8_A・M8_C | 重なった兄弟のマスク。黄色は A の中だけ、水色は C の中だけ（重なった所は後に作った C が手前） |
| M9_Level1〜9 | 9 段の入れ子（だんだん小さい四角）。一番内の子（白）は 8 段目の四角で切れて、9 段目は切り抜かない |

---

## D 確認

### D-1 ビルド

Debug・Development・Release をビルドして、エラーと（今回写したファイルの）警告が 0 か見る。

### D-2 動かして見る

| # | すること | 見る所 |
|---|---|---|
| 1 | MaskTest を開く | C-2 の表のとおり。Scene ビューと Game ビューで同じ |
| 2 | M2_Circle を動かす（UI ギズモでドラッグ） | 赤は、いつも四角と円の重なった所だけに出る |
| 3 | M1_Circle の UI Mask の Show Graphic を外す | 円の塗りと縁取りが消え、ドットと文字は円で切れたまま |
| 4 | M1_Circle の Hierarchy の目を閉じる | Scene ビューで円が消えても、ドットは円で切れたまま（Game ビューは変わらない） |
| 5 | M6_Gauge の Amount を 0 → 1 にドラッグ | 扇が広がり、ドットが見える所も広がる。0 で何も出ない |
| 6 | M4_Child の Order を 100 にする | それでも緑（Order 5）より奥 |
| 7 | M4_Mask の Order を 10 にする | 赤と灰色の四角が、緑より手前になる |
| 8 | Scene ビューでドットの所をクリック | 子のドット（手前）が選ばれる（マスクの図形ではなく） |
| 9 | WaveBackground を開いて Play | 波が上下に動き、ドットが斜めに流れる。ドットは波の中だけ、縁取りが上 |
| 10 | Image の Tiled・Tile Size・Tile Offset を触る | Size を変えても模様が伸びない。Tile Offset で模様がずれる。Tiled を外すと1枚の画像に戻る |
| 11 | 今までのシーン（Image・Text・Vector Shape・UI Screen・シーンの切り替え） | 今までどおりの重なり。幕は一番手前 |
| 12 | 保存して開き直す | UI Mask・Tiled の値が残る |

---

## E 検証の結果（写しで確かめた）

写し（AL4_01 の写しのサブモジュールを 0b7ca68 にそろえ、Polyline.md の答えを入れた物）に、答えのコードを入れて確かめた。

| 確かめたこと | 結果 |
|---|---|
| Debug・Development・Release のビルド | エラー 0、警告 0。Polyline.md だけを入れた途中の状態も、Development でエラー 0、警告 0 |
| MaskTest の絵（Release） | C-2 の表のとおり（円・入れ子の共通部分・折れ線の形・Order のまとまり・開いた線の帯・ゲージの扇・無効なら切らない・兄弟・9 段） |
| MaskTest をエディタで開いた（Debug。M8・M9 を足す前の M1〜M7 で） | Scene ビュー・Game ビューとも Release と同じ切り抜き。Inspector に Tile の欄（Tiled の間は Sheet・Rect が灰色） |
| WaveBackground を決まった時間（1/60 秒）で 240 フレーム動かした記録 | 4 本の Polyline の全部の点・2 枚の Image の Tile Offset・頂点の数が、Debug と Release で全行一致 |
| WaveBackground の速さ（毎フレーム 4 本を作り直す） | PolylineSystem::Draw が Release 0.09 ms・Debug 0.32 ms（1フレームの平均） |
| WaveBackground を Timeline ウィンドウで開いた | トラック `TopLeft 2`〜`7` などとキーが出る |
| Polyline.md の見本の記録（240 フレーム） | マスクを入れた後も、Debug と Release で全行一致 |

**確かめていない物：**

- 本物のマウスでの操作（D-2 の 2〜8・10）。
- Hierarchy の目で隠したマスク（D-2 の 4）。コードを読んで決めただけ（形だけの手順は、目で隠しても描く）。
- UI Screen（`.uiscreen`）の中のマスク。Entity の親子だけで決めるので、同じに動くはず。
- マスクの中の Text の影・光（Text は2回積むが、同じキーなので順は変わらない。コードを読んだだけ）。
- AL4_01 の実物での実行。
