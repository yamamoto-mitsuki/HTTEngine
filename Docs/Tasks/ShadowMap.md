参考：
- シャドウマップの直し方（ずらす量・ピクセル単位で箱を動かす・カメラに合わせた箱）：https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
- 深度のずらし方（DepthBias・SlopeScaledDepthBias の式）：https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage-depth-bias
- Unity の Shadow Distance（カメラからの距離で影を出す範囲を決める）：https://docs.unity3d.com/Manual/shadow-distance.html
- Unity URP の Screen Space Shadows（影を先に画面の大きさの絵にしてから、色を塗るときに読む）：https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/manual/renderer-feature-screen-space-shadows.html
- DXR のインラインレイトレ（RayQuery。CS の中から光線を飛ばす。段階5 で使う）：https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#inline-raytracing

# 影（段階1：シャドウマップ・深度プリパス・影の量の画面）

平行光源の影を出す。作り方はシャドウマップ（ライトから見た深度の絵）。
ただし、色を塗る PS はシャドウマップを直接読まず、先に作った **「影の量の画面」** を読むだけにする。
こうしておくと、あとでレイトレの影にするときは「影の量の画面を作る所」だけを入れ替えればよい（0.2）。

- **パートA RenderStates**：`MeshPass`（色・深度プリパス・影の深度）・影の深度のラスタライザ
- **パートB 影の土台（新しい）**（`Graphics/Shadow/`）：`ShadowSettings.h`・`ShadowMap`
- **パートC PSOManager・RootSignatureManager**：深度だけのパスの PSO（RTV なし・PS は `ModelDepthPS`）
- **パートD ShaderConstants・RenderTexture**：`ShadowReceiverData`・深度だけを結ぶ・描く途中で深度を CS に読ませる
- **パートE RenderQueue・DrawRequest**：影の深度・深度プリパスを描く
- **パートF ShadowRenderer（新しい）**：ライトから見る箱・影の深度・影の量の画面
- **パートG SceneRenderer**：描く順
- **パートH Cast Shadows**：Model Renderer・Primitive の見た目に「影を落とすか」
- **パートI Directional Light の影**：Inspector・保存・LightSystem
- **パートJ シェーダー**：`ModelDepth.PS`（新しい）・`Shadow/ShadowReceiver.hlsli`（新しい）・`Shadow/ShadowMask.CS`（新しい）・ライトの PS 5本
- **パートK vcxproj**
- **パートL Sandbox**（`ShadowTest` シーン）
- **パートM Reference**
- **パートN 確かめ方**

**A → B → C → D → E → F → G → H → I → J** の順に写すとビルドが通る（新しいファイルを作ったら、そのたびに K のその行を足す）。L・M は最後。

ブランチは `feature/ShadowMap`（`develop` 70b52f4 から切った。本体はまだ `feature/GameInput` のまま）。答えは写しのブランチ `answer-shadow-map` にある（Debug・Development・Release でビルドを通し、Sandbox で影を撮って見た）。

## 0章 決めたこと

### 0.1 1つのビューを描く順番

```
SceneRenderer::RenderWorldForPost（ビューごと。エディタは Scene と Game の2回）
 ├ ShadowRenderer::RenderShadowMap   ライトから見た深度 → ShadowMap（D32）      … RenderQueue::FlushShadowCaster
 ├ work->PreDraw                     3D の絵の色と深度を消す
 ├ SceneRenderer::RenderDepthPrepass
 │   ├ 深度プリパス                   カメラから見た深度だけ（RTV なし）            … RenderQueue::FlushDepthPrepass
 │   └ ShadowRenderer::BuildShadowMask CS：深度 → 位置 → ライトの深度と比べる → 影の量の画面（R8）  ← レイトレはここを入れ替える
 ├ 不透明な物の色                     gShadow ＝ 影の量の画面。PS は GetDirectionalShadow で読むだけ
 ├ デカール・空・粒（今までどおり）
 └ 半透明な物                         gShadow ＝ 影なし
```

### 0.2 なぜ「影の量の画面」を挟むか

| 候補 | 選ばない理由・選ぶ理由 |
|---|---|
| 色を塗る PS が、シャドウマップを直接読む | 一番短い。だが PS が「シャドウマップ・ライトの行列・ずらす量」を全部知ることになり、レイトレに変えると PS を全部（Lambert〜PBR の5本・この先のトゥーン）直すことになる |
| **先に画面の大きさの「影の量」を作り、PS はそれを読むだけ（選ぶ）** | PS が知るのは「自分のピクセルの影の量」だけ。作り方（シャドウマップ・レイトレ・両方を混ぜる）は `ShadowRenderer` の中で閉じる。レイトレの影は、ノイズを減らす（デノイズ）のも画面の上でするので、この形がそのまま使える。Unity URP の Screen Space Shadows・Unreal のレイトレの影と同じ形 |

この形には、色を塗る前に深度が要る（どのピクセルに何が写るかを先に知る）。それが深度プリパス（0.3）。
深度プリパスは、この先の SSAO・SSR・TAA（グラフィックの予定の段階4・5）でも要るので、ここで入れておく。

### 0.3 深度プリパス

- 描く物は **不透明な物（`DepthMode::TestWrite`）だけ**。深度を見ない物（`Disable`）は、いつも手前に描くので入れない。
- **VS は、色を塗るパスと同じ物（物の Shader の VS）をそのまま使う。** 別の VS で描くと、計算の順番の違いで深度がほんの少しずれ、色を塗るパスで「同じ深度なのに通らない」所が出る。同じ VS なら、同じ入力から同じ深度になる。
- **PS は `ModelDepthPS`**（切り抜きだけ）。色を塗る PS は `GetModelSurface` の切り抜きと「A が 0 なら捨てる」をしているので、深度プリパスでも同じ所を捨てる。捨てないと、色を塗る PS が捨てた所に深度だけが残り、後ろの物も空も描かれない穴が開く。
- 色を塗るパスの深度の設定は **今のまま（LESS_EQUAL・書く）**。プリパスで書いた深度と同じ所だけが通るので、重なった物の後ろの方は PS が動かない（重い PS を1ピクセル1回にできる）。`DepthMode` に新しい値は足さない（マテリアルの Inspector に、ユーザーが選んではいけない値が出てしまう）。
- 深度だけの PSO は **RTV なし**。描画先に色を結んだままだと、デバッグレイヤーに「PSO と描画先の形が違う」と止められる。なので描画先も深度だけにする（`RenderTexture::BindDepthOnly`）。
- RootSignature は、色を塗るパスと同じ物を使う。`ModelDepthPS` が読む物（`gMaterial`・`gTextures`・`gSampler`）を、Model の RootSignature に足しておく（`RootSignatureManager::AddPixelStage`）。今の Model の PS はどれも `GetModelSurface` で同じ物を読んでいるので、実際には増えない。ゲームの Shader が読まない場合だけ増える。

### 0.4 ライトから見る箱

平行光源は光が平行なので、正射影の箱で見る。箱は **ビューごとに、そのビューのカメラに合わせて** 決める（Scene ビューと Game ビューで、別々に影の深度を描く）。

1. カメラから見える所のうち、手前（Near）から影を出す距離（`Distance`）までを包む **球** を求める。球なら、カメラが回っても大きさが変わらない（影の細かさが変わらない）。
2. ライトの向きから、その球を囲む正方形で見る。奥行きは、ライトの側へ半径の 3 倍まで伸ばす（球の外でも、ライトの側にある物は影を落とす。`kCasterReach`）。
3. 箱の真ん中を、影の深度の絵の **1ピクセル単位** に合わせる。カメラが少し動くたびに箱が半端に動くと、影の縁のピクセルが毎フレーム変わってちらつく。
4. カメラから `Distance` より遠い所は影を出さない。ぷつっと切れて見えないように、最後の 1 割で薄くする（CS の 4.）。

### 0.5 深度のずらし方（バイアス）

ライトから見た深度の絵は1ピクセルが広いので、斜めの面は1ピクセルの中で深度が変わる。そのまま比べると、自分の影が縞になる（シャドウアクネ）。

| ずらし方 | どこで | 値 |
|---|---|---|
| 面の傾きに合わせて奥へ（`SlopeScaledDepthBias`） | 影の深度を描くとき（PSO のラスタライザ） | 1.5 で決め打ち（PSO は作るときに値が決まるので、Inspector から変えると PSO が増える） |
| 傾きに関係なく引く（`Bias`） | 比べるとき（`ShadowMask.CS`） | 平行光源の Inspector の `Bias`（m）。CS には「箱の奥行き」で割った 0〜1 の値で送る |

法線の向きにずらす方法（ノーマルオフセット）は、法線の絵が要るので段階2（深度プリパスで法線も書く）でやる。

### 0.6 何が影を落とし、何が受けるか

| 物 | 落とす | 受ける |
|---|---|---|
| 不透明な物（`TestWrite`）・`Cast Shadows` が入っている・`Default` の層 | ○ | ○（光を使う Shader だけ。Unlit は光を使わないので変わらない） |
| `Cast Shadows` を外した物 | × | ○ |
| 半透明な物（`TestNoWrite`） | × | ×（影の量の画面は、一番手前の不透明な物の影なので使えない。段階2 以降でシャドウマップを直接読む） |
| 深度を見ない物（`Disable`） | × | ○ |
| エディタのグリッド・ギズモ（`Default` 以外の層） | × | ― |
| ポイントライト・スポットライトの光 | ― | 影なし（まだ平行光源だけ） |

### 0.7 既定の値と、今のシーンへの影響

- 平行光源の `Cast Shadows`、Model Renderer・Primitive の `Cast Shadows` は **既定で入っている**（Unity と同じ）。前のシーンのファイルには項目が無いので、既定の値（入っている）になる。**Sandbox・AL4_01・TD2_2 のシーンも、エンジンを更新すると影が出る。** 要らなければ平行光源の `Cast Shadows` を外す。
- `Strength` の既定は 1（光が全く届かない）。**Lambert・Half Lambert は環境光が無いので、影が真っ黒になる。** Primitive の既定は Half Lambert なので、黒すぎるなら `Strength` を下げる（テストシーンは 0.8）。影の色は段階4（トゥーン）で決められるようにする。

### 0.8 この先の段階

| 段階 | やること | この段階の何を使うか |
|---|---|---|
| 2 | PCF（周りの数ピクセルも比べて縁をやわらかく）・ノーマルオフセット（深度プリパスで法線も書く） | `ShadowMask.CS` の 3. を直す。PS は変えない |
| 3 | CSM（カメラの近く・遠くで箱を分ける） | `ShadowRenderer::MakeLightView` を段ごとに。`ShadowMap` を配列に |
| 4 | トゥーン（影の色・境目の幅）・半透明が影を受ける | `GetDirectionalShadow` の値を、トゥーンの PS が使う |
| 5 | DXR（`ID3D12Device5`・BLAS / TLAS・RayQuery） | `BuildShadowMask` の中を「ライトへ光線を飛ばす」に変える。深度プリパス・影の量の画面・PS はそのまま |

### 0.9 確かめたこと（写しで）

- Debug・Development・Release でビルドが通る。
- Sandbox の `ShadowTest`（パートL）で、柱・浮いた箱・球・箱・歩く人（スキンのあるモデル）の影が床に落ちる。半透明の箱・`Cast Shadows` を外した箱は影を落とさない。
- `Map Size` を変えると作り直される（4096 で確認）。`Distance` を短くすると、遠くの影が薄くなって消える。PBR の物にも影が落ちる。
- デバッグレイヤー（警告でも止まる設定）で止まらない。深度だけのパスで `gShadow` を結ばないままでも止まらない。
- Release の exe（エディタなしのウィンドウ）でも影が出る。


## パートA RenderStates

### A-1 `Graphics/Pipeline/RenderStates.h`

`StencilMode` の下（`BillboardMode` の上）に足す。

```cpp
// 3D のメッシュを、どのパスで描くか（同じメッシュを、パスごとに違う PSO で何回か描く）
// 色を塗る前に、深度だけのパスを2つ通す（影の深度・深度プリパス）。どちらも RTV を結ばず、PS は切り抜きだけをする ModelDepth.PS
enum class MeshPass {
	Color,        // 色を塗る（ふつう。物の Shader・ブレンド・深度・ステンシルのまま）
	DepthPrepass, // 深度プリパス：カメラから見た深度だけを書く（色を塗る前に、一番手前の深度を全部そろえておく）
	ShadowCaster, // 影の深度：ライトから見た深度だけを書く（影の深度の絵 ShadowMap に。面の傾きに合わせて深度をずらす）
};
```

`class RenderStates` の `MakeRasterizerDesc` の下に足す。

```cpp
	/// <summary>
	/// 影の深度を描くときの RasterizerType の設定（MakeRasterizerDesc に、面の傾きに合わせて深度をずらす量を足した物）
	/// <para>斜めの面は、影の深度の絵の1ピクセルの中で深度が大きく変わる。比べるときに自分の影が縞になる（シャドウアクネ）ので、傾くほど奥へずらす</para>
	/// </summary>
	static D3D12_RASTERIZER_DESC MakeShadowCasterRasterizerDesc(RasterizerType type);
```

### A-2 `Graphics/Pipeline/RenderStates.cpp`

`MakeRasterizerDesc` の下（`DepthMode・StencilMode → D3D12_DEPTH_STENCIL_DESC` の大見出しの上）に足す。

```cpp
//=============================================================================
// 影の深度を描くときの RasterizerType → D3D12_RASTERIZER_DESC
//=============================================================================
D3D12_RASTERIZER_DESC RenderStates::MakeShadowCasterRasterizerDesc(RasterizerType type) {
	// 面の向き（カリング）は物の設定のまま。両面の物は、両面とも影を落とす
	D3D12_RASTERIZER_DESC desc = MakeRasterizerDesc(type);
	// GPU が「面の傾き × SlopeScaledDepthBias」だけ奥へずらす（光に対して傾いた面ほど大きく）
	// 傾きに関係なくずらす分は、比べる CS（ShadowMask.CS）で平行光源の Shadow Bias（m）を引く。Inspector で変えられるように、そちらに置く
	desc.DepthBias = 0;
	desc.SlopeScaledDepthBias = 1.5f;
	desc.DepthBiasClamp = 0.0f;
	return desc;
}
```

> `RasterizerType::SolidFront` にも「シャドウマップ用」の深度のずらしが入っているが、今はデカールの箱（裏の面を描く）に使っている。影の深度ではこの関数が上書きするので、そのままでよい。


## パートB 影の土台（新しい。`Graphics/Shadow/`）

### B-1 `Graphics/Shadow/ShadowSettings.h`（新しい）

```cpp
#pragma once
#include <cstdint>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Math/Vector3.h"

// 影の深度の絵（シャドウマップ）の1辺の大きさ
// magic_enum が数えられる値（-128〜128）に収めるため、値はピクセル数ではなく番号にする。ピクセル数は GetShadowMapPixels
enum class ShadowMapSize {
	Size1024, // 1024 × 1024
	Size2048, // 2048 × 2048
	Size4096, // 4096 × 4096
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ShadowMapSize) {
	using enum ShadowMapSize;
	return MakeEnumTips<ShadowMapSize>({
	    {Size1024, "軽い。影の縁がガタガタしやすい"},
	    {Size2048, "ふつう"},
	    {Size4096, "細かいが重い（深度の絵だけで 64MB）"},
	});
}

// ===== 1辺のピクセル数 =====
constexpr uint32_t GetShadowMapPixels(ShadowMapSize size) {
	switch (size) {
	case ShadowMapSize::Size1024:
		return 1024;
	case ShadowMapSize::Size2048:
		return 2048;
	case ShadowMapSize::Size4096:
		return 4096;
	}
	return 2048;
}

/// <summary>
/// 平行光源の影の調整（Directional Light の Inspector で変える値）
/// </summary>
struct DirectionalShadowParams {
	bool castsShadows = true;                        // 影を出す（物の方も Cast Shadows が入っている物だけが影を落とす）
	ShadowMapSize mapSize = ShadowMapSize::Size2048; // 影の深度の絵の大きさ（大きいほど影の縁が細かい）
	float distance = 30.0f;                          // カメラからこの距離（m）まで影を出す（短いほど、同じ絵の大きさで細かい）
	float strength = 1.0f;                           // 影の濃さ（0：影なし 1：平行光源の光が全く届かない）
	float bias = 0.05f;                              // 深度を比べる前に引く距離（m）。自分の影が縞になる（シャドウアクネ）なら上げる、影が浮くなら下げる
};

/// <summary>
/// このフレームの平行光源の影（LightSystem が Directional Light から作り、SceneRenderer::SetDirectionalShadow で渡す）
/// </summary>
struct DirectionalShadowSettings {
	bool enabled = false;                    // false：影を出さない（平行光源が無い・Cast Shadows を外した）
	Vector3 direction = {0.0f, -1.0f, 0.0f}; // 光が進む向き（正規化済み）
	DirectionalShadowParams params;          // 調整の値
};
```

- `DirectionalShadowParams` は Component（Directional Light）が持つ値、`DirectionalShadowSettings` は描画へ渡す値。既定の値を1か所に書くために、Component はこの構造体をそのまま持つ（パートI）。

### B-2 `Graphics/Shadow/ShadowMap.h`（新しい）

```cpp
#pragma once
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

/// <summary>
/// 影の深度の絵（シャドウマップ）。ライトから見た、一番手前の物までの深度を書く
/// <para>使い方：BeginRender（読む状態 → 書く状態・消す）→ 深度だけの PSO で描く → EndRender（書く状態 → CS が読む状態）→ CS が gTextures[GetSRVSlot()] で読む</para>
/// <para>色は持たない（RTV なし）。深度は 32bit の小数（ステンシルは使わない）</para>
/// </summary>
class ShadowMap {
public:
	// 深度を書く形（PSOManager が、影の深度の PSO の DSVFormat に使う）
	static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

	/// <summary>
	/// 1枚作る（初めは読む状態）
	/// </summary>
	/// <param name="size">1辺のピクセル数（正方形）</param>
	explicit ShadowMap(uint32_t size);
	// 読む窓口のスロットを返す
	~ShadowMap();

	// コピー禁止（スロットを2回返してしまう）
	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	/// <summary>
	/// 描き始める：読む状態 → 書く状態にし、深度だけを描画先に結んで一番奥（1）で消す。ビューポートも絵全体にする
	/// </summary>
	void BeginRender();

	/// <summary>
	/// 描き終わる：書く状態 → CS が読む状態
	/// </summary>
	void EndRender();

	// ===== ゲッター =====
	uint32_t GetSize() const { return size_; }
	// 読む窓口の番号（HLSL の gTextures[番号] で読む。中身は R の 0〜1 の深度）
	uint32_t GetSRVSlot() const { return srvSlot_; }

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;       // 中身（型なしで作り、書くときは D32_FLOAT、読むときは R32_FLOAT として見る）
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;  // 書く窓口（DSV）の置き場
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};               // 書く窓口
	uint32_t srvSlot_ = 0;                                  // 読む窓口の、SRVヒープの番号
	uint32_t size_ = 0;                                     // 1辺のピクセル数
};
```

### B-3 `Graphics/Shadow/ShadowMap.cpp`（新しい）

```cpp
#include "ShadowMap.h"

#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"

//=============================================================================
// 作成・破棄
//=============================================================================
// ===== 作成 =====
ShadowMap::ShadowMap(uint32_t size) : size_(size) {
	ID3D12Device* device = DirectXCommon::GetDevice();

	// --- 中身 ---
	// 型なし（R32_TYPELESS）で作ると、窓口ごとに形を決められる（書く DSV は D32_FLOAT、読む SRV は R32_FLOAT）
	// 初めは読む状態（BeginRender が、読む状態 → 書く状態に切り替えるので、どのフレームも同じ順で切り替えられる）
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = kDepthFormat;
	clearValue.DepthStencil.Depth = 1.0f; // 一番奥で消す
	resource_ = GpuResources::CreateTextureResource(size, size, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
	    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clearValue);
	resource_->SetName(L"ShadowMap");

	// --- 書く窓口（DSV）---
	dsvHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	dsvHeap_->SetName(L"ShadowMap_DSVHeap");
	dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = kDepthFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(resource_.Get(), &dsvDesc, dsvHandle_);

	// --- 読む窓口（SRV）---
	srvSlot_ = DirectXCommon::AllocateSRVSlot();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // 32bit の深度を、そのまま R の小数として読む
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(resource_.Get(), &srvDesc, DirectXCommon::GetCPUDescriptorHandle(DirectXCommon::GetSRVDescriptorHeap(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvSlot_));
}

// ===== 破棄 =====
// スロットを返す（返さないと、大きさを変えて作り直すたびにヒープの空きが減っていく）
ShadowMap::~ShadowMap() { DirectXCommon::FreeSRVSlot(srvSlot_); }

//=============================================================================
// 描く
//=============================================================================
// ===== 描き始める =====
void ShadowMap::BeginRender() {
	auto* cmdList = DirectXCommon::GetCommandList();
	// 読む状態 → 書く状態
	DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	// 深度だけを結ぶ（色は無い。PSO も RTV なし）
	cmdList->OMSetRenderTargets(0, nullptr, false, &dsvHandle_);
	cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	// ビューポート・シザーは絵全体
	const float size = static_cast<float>(size_);
	const D3D12_VIEWPORT viewport{0.0f, 0.0f, size, size, 0.0f, 1.0f};
	const D3D12_RECT scissor{0, 0, static_cast<LONG>(size_), static_cast<LONG>(size_)};
	cmdList->RSSetViewports(1, &viewport);
	cmdList->RSSetScissorRects(1, &scissor);
}

// ===== 描き終わる =====
// CS（ShadowMask.CS）が読むので、PS 以外が読む状態（NON_PIXEL_SHADER_RESOURCE）にする
void ShadowMap::EndRender() { DirectXCommon::TransitionBarrier(resource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); }
```

- 深度を 32bit の小数（`D32_FLOAT`）にするのは、ライトから見る箱の奥行きが長い（球の半径の 4 倍）ため。3D の絵の深度（`D24_UNORM_S8_UINT`）と同じ形にすると、ステンシルの 8bit が無駄になり、深度も 24bit に減る。


## パートC PSOManager・RootSignatureManager

### C-1 `Graphics/Pipeline/PSOManager.h`

`PSOKey` と `PSOHash` に `pass` を足す。

```cpp
// PSOの一意キー
struct PSOKey {
	uint32_t shaderProgramID;
	BlendMode blendMode;
	RasterizerType rasterizerType;
	DepthMode depthMode;
	StencilMode stencilMode = StencilMode::None;
	MeshPass pass = MeshPass::Color; // 深度だけのパス（DepthPrepass・ShadowCaster）は、RTV なし・PS は ModelDepthPS で作る
	bool operator==(const PSOKey&) const = default;
};
struct PSOHash {
	size_t operator()(const PSOKey& k) const {
		size_t h = static_cast<size_t>(k.shaderProgramID);
		h = h * 31 + static_cast<size_t>(k.blendMode);
		h = h * 31 + static_cast<size_t>(k.rasterizerType);
		h = h * 31 + static_cast<size_t>(k.depthMode);
		h = h * 31 + static_cast<size_t>(k.stencilMode);
		h = h * 31 + static_cast<size_t>(k.pass);
		return h;
	}
};
```

`GetPSOKey` の宣言に引数を1つ足す（`<param name="stencilMode">` の下に1行）。

```cpp
	/// <param name="stencilMode">ステンシル設定（UI のマスク）</param>
	/// <param name="pass">どのパスで描くか（Model だけ。深度だけのパスは RTV なしの PSO になる）</param>
	static PSOKey GetPSOKey(DrawCategory drawCategory, ShadingType shadingType, BlendMode blendMode = BlendMode::Normal, RasterizerType rasterizerType = RasterizerType::SolidBack,
	    DepthMode depthMode = DepthMode::TestWrite, StencilMode stencilMode = StencilMode::None, MeshPass pass = MeshPass::Color);
```

### C-2 `Graphics/Pipeline/PSOManager.cpp`

include に1行足す（`RenderTexture.h` の下）。

```cpp
#include "HTTEngine/Graphics/Shadow/ShadowMap.h"
```

`namespace {` のすぐ下（`GetTargetFormat` の上）に足す。

```cpp
// 深度だけのパス（深度プリパス・影の深度）の PS。Model の切り抜き（ModelSurface.hlsli）だけをして、色は出さない
// VS は物の Shader の物をそのまま使う（色を塗るパスと同じ VS なので、深度がぴったり同じになる）
const std::string kDepthOnlyPSName = "ModelDepthPS";

// ===== その PSO の PS の名前（深度だけのパスは ModelDepthPS）=====
const std::string& GetPixelShaderName(const ProgramEntry& program, MeshPass pass) { return (pass == MeshPass::Color) ? program.psName : kDepthOnlyPSName; }
```

`GetSortKey` の頭を差し替える（`pass` を一番上に足し、Shader の番号を 28 ビットに収める）。

```cpp
	// PSOKey の全部を入れる（入れていない物だけが違う2つが同じキーになると、PSO を切り替えずに描いてしまう）
	// 上のビットほど先に比べる。パスは描く順番そのもの（影の深度 → 深度プリパス → 色）なので一番上、次に RootSignature が変わる Shader
	static constexpr uint32_t kShaderProgramBits = 28; // Shader の番号に使うビット（32〜59。上の 4 ビットはパス）
	HTT_ASSERT_MSG(key.shaderProgramID < (1u << kShaderProgramBits), "Shader の数が SortKey に入る数を超えました");
	return (static_cast<uint64_t>(key.pass)            << 60) // MeshPass（色・深度プリパス・影の深度）
	     | (static_cast<uint64_t>(key.shaderProgramID) << 32) // Shader
	     | (static_cast<uint64_t>(key.stencilMode)     << 24) // StencilMode（デカールを受けない印）
```

- 今は1回の Flush で1つのパスしか描かないので、`pass` をどこに置いても並ぶ順は変わらない。それでも一番上に置くのは、パスが「効率のための順」ではなく「守らないと絵が壊れる順」だから。この先、全部のパスの描く物を1つの列にまとめて並べる形（描く物を GPU で選ぶ・コマンドリストを分けて並列に積む など）にしても、並べただけで順番が守られる。
- パスが変わっても RootSignature は変わらない（深度だけのパスも同じ RootSignature。C-2 の `GetRootSignatureInfo`）。変わるのは PSO だけ。

`GetRootSignatureInfo` の `MergeStages` の下に足す。

```cpp
		auto merged = RootSignatureManager::MergeStages(vs.resources, ps.resources);
		// Model は、深度だけのパスの PSO も同じ RootSignature で作る。ModelDepthPS が読む物（gMaterial・gTextures・gSampler）も入れておく
		if (p.category == DrawCategory::Model) {
			RootSignatureManager::AddPixelStage(merged, ShaderPackageLoader::GetShaderReflection(kDepthOnlyPSName).resources);
		}
		slot = RootSignatureManager::MakeRootSignatureInfo(merged);
```

`GetPSOKey` を差し替える。

```cpp
PSOKey PSOManager::GetPSOKey(DrawCategory category, ShadingType shading, BlendMode blend, RasterizerType raster, DepthMode depth, StencilMode stencil, MeshPass pass) {
	return PSOKey{GetShaderProgramID(category, shading), blend, raster, depth, stencil, pass};
}
```

`MakePSO` の頭の4行（`ProgramEntry` 〜 `RootSignature`）を差し替える。

```cpp
	const ProgramEntry& p = ShaderPackageLoader::GetProgramAt(key.shaderProgramID);
	const bool isDepthOnly = (key.pass != MeshPass::Color); // 深度だけのパス（RTV なし）
	HTT_ASSERT_MSG(!isDepthOnly || p.category == DrawCategory::Model, "深度だけのパスで描けるのは Model だけです");
	const ShaderReflection& vs = ShaderPackageLoader::GetShaderReflection(p.vsName);
	const ShaderReflection& ps = ShaderPackageLoader::GetShaderReflection(GetPixelShaderName(p, key.pass));
	// RootSignature（深度だけのパスも、色を塗るパスと同じ物。GetRootSignatureInfo で ModelDepthPS の分も入れてある）
	const RootSignatureInfo& rs = GetRootSignatureInfo(key.shaderProgramID);
```

`MakePSO` の `RasterizerState` と、`NumRenderTargets` 〜 `DSVFormat` を差し替える。

```cpp
	desc.BlendState = RenderStates::MakeBlendDesc(key.blendMode);
	desc.RasterizerState = (key.pass == MeshPass::ShadowCaster) ? RenderStates::MakeShadowCasterRasterizerDesc(key.rasterizerType) : RenderStates::MakeRasterizerDesc(key.rasterizerType);
	desc.DepthStencilState = RenderStates::MakeDepthStencilDesc(key.depthMode, key.stencilMode);
	desc.PrimitiveTopologyType = topology;
	// --- 描画先：色を塗るパスは RTV 1枚（3D は HDR、2D は SDR）。深度だけのパスは RTV なし ---
	desc.NumRenderTargets = isDepthOnly ? 0 : 1;
	desc.RTVFormats[0] = isDepthOnly ? DXGI_FORMAT_UNKNOWN : RenderTexture::ToDXGIFormat(GetTargetFormat(p.category));
	// --- 深度：影の深度は ShadowMap の形（ステンシルなし）。ほかは 3D の絵・画面と同じ ---
	desc.DSVFormat = (key.pass == MeshPass::ShadowCaster) ? ShadowMap::kDepthFormat : DXGI_FORMAT_D24_UNORM_S8_UINT;
```

### C-3 `Graphics/Pipeline/RootSignatureManager.h`

`RootBind` の最後に足す。

```cpp
	DrawData,             // 描き手が描く物ごとに送る値（形・進み具合など。中身は描き手ごとの構造体。PS）
	Shadow,               // 影の受け方（影の量の画面の番号。Model の PS）
};
```

`MergeStages` の宣言の下に足す。

```cpp
	/// <summary>
	/// PS 1本分のリソースを足す（同じ RootSignature で、別の PS の PSO も作るとき。Model の深度だけのパスの ModelDepthPS）
	/// <para>もうある物は足さない（VS だけが使っていた物は、PS からも見えるようにする）</para>
	/// </summary>
	static void AddPixelStage(std::vector<MergedBind>& binds, const std::vector<ShaderResourceBinding>& ps);
```

### C-4 `Graphics/Pipeline/RootSignatureManager.cpp`

`MergeStages` の PS の for を `AddPixelStage` に出す（関数ごと差し替え）。

```cpp
std::vector<MergedBind> RootSignatureManager::MergeStages(const std::vector<ShaderResourceBinding>& vs, const std::vector<ShaderResourceBinding>& ps) {
	std::vector<MergedBind> out;
	out.reserve(vs.size() + ps.size());
	// VS側は全部 VERTEX として入れる
	for (const auto& v : vs) {
		out.push_back({v, D3D12_SHADER_VISIBILITY_VERTEX});
	}
	// PS側を足す
	AddPixelStage(out, ps);
	return out;
}

// ===== PS 1本分を足す =====
void RootSignatureManager::AddPixelStage(std::vector<MergedBind>& binds, const std::vector<ShaderResourceBinding>& ps) {
	// 同じリソース(type/register/space が一致)があれば、VS だけの物は ALL に昇格（PS だけの物はそのまま）
	// 無ければ PS専用として追加
	for (const auto& p : ps) {
		auto it = std::find_if(binds.begin(), binds.end(), [&](const MergedBind& m) {
			return m.bind.name == p.name &&                     // 変数名が一致しているか
			       m.bind.type == p.type &&                     // CBufferなど型が一致しているか
			       m.bind.registerNumber == p.registerNumber && // レジスタ番号が一致しているか
			       m.bind.space == p.space; });                 // space番号が一致しているか
		if (it == binds.end()) {
			binds.push_back({p, D3D12_SHADER_VISIBILITY_PIXEL}); // PS専用
		} else if (it->visibility == D3D12_SHADER_VISIBILITY_VERTEX) {
			it->visibility = D3D12_SHADER_VISIBILITY_ALL; // VSにもPSにもある
		}
	}
}
```

- 前は「見つかったら ALL」だった。PS を2本足すと、1本目の PS だけが使う物（PIXEL）を2本目の PS も使ったときに ALL になってしまう。VS だけの物（VERTEX）のときだけ ALL にする。

`NameToRole` の表の最後に足す。

```cpp
	    {"gShadow",               RootBind::Shadow              },
```


## パートD ShaderConstants・RenderTexture

### D-1 `Graphics/Pipeline/ShaderConstants.h`

`IBLParamsData` の下（`光源` の大見出しの上）に足す。

```cpp
// ===== 影の受け方（gShadow。Model の PS が、平行光源の光にどれだけ影が掛かるかを読む）=====
// Shader/Data/Shadow/ShadowReceiver.hlsli の ShadowReceiver と同じ並び
// 影の作り方（シャドウマップ・レイトレ）が変わっても、PS が読むのは「影の量の画面」の番号だけ
struct ShadowReceiverData {
	uint32_t maskIndex = 0; // 影の量の画面（gTextures の番号。1 で光が届く、0 で影）
	uint32_t hasMask = 0;   // 1：影の量の画面を読む 0：影なし（影を出さないビュー・半透明な物）
	float padA[2] = {};
};
static_assert(sizeof(ShadowReceiverData) == 16, "HLSLのShadowReceiverと大きさが違います");
```

- 影なしを「白いテクスチャの番号」で表さないのは、PS が `Load`（ピクセルの位置で読む）を使うため。白いテクスチャは小さいので、画面のピクセルの位置で読むと絵の外になり 0（真っ黒な影）が返る。

### D-2 `Graphics/RenderTarget/RenderTexture.h`

`BindWithDepthOf` の下に足す。

```cpp
	/// <summary>
	/// 深度だけを描画先にする（色は結ばない。クリアしない）
	/// <para>深度プリパスで使う。PSO が RTV なし（NumRenderTargets 0）なので、色を結んだままだとデバッグレイヤーに止められる。PreDraw の後に呼ぶ</para>
	/// </summary>
	void BindDepthOnly();

	/// <summary>
	/// 色と深度を描画先に結び直す（クリアしない。PreDraw で結んだ形に戻す）
	/// </summary>
	void BindColorAndDepth();

	/// <summary>
	/// 深度を CS から読める状態にする（色は描画先のまま。isDepthReadable で作ったときだけ）
	/// <para>描いている途中（PreDraw と PostDraw の間）に、それまでの深度を CS で読むときに使う（影の量の画面）。読み終わったら EndDepthComputeRead</para>
	/// </summary>
	void BeginDepthComputeRead();

	/// <summary>
	/// BeginDepthComputeRead の前の状態（深度を書く状態）に戻す
	/// </summary>
	void EndDepthComputeRead();
```

### D-3 `Graphics/RenderTarget/RenderTexture.cpp`

`BindWithDepthOf` の下に足す。

```cpp
//=============================================================================
// 深度だけ・色と深度を結び直す
//=============================================================================
// ===== 深度だけ =====
void RenderTexture::BindDepthOnly() {
	HTT_ASSERT_MSG(useDepth_, "深度だけを結ぶには、深度あり（useDepth）で作ってください");
	DirectXCommon::GetCommandList()->OMSetRenderTargets(0, nullptr, false, &dsvHandle_);
}

// ===== 色と深度 =====
void RenderTexture::BindColorAndDepth() { DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandle_, false, useDepth_ ? &dsvHandle_ : nullptr); }

//=============================================================================
// 描いている途中に、深度を CS で読む
//=============================================================================
// ===== 読める状態にする =====
void RenderTexture::BeginDepthComputeRead() {
	HTT_ASSERT_MSG(isDepthReadable_, "深度を読むには、isDepthReadable で作ってください");
	// 書く状態 → CS が読む状態（NON_PIXEL_SHADER_RESOURCE：PS 以外のシェーダーが読む）
	DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

// ===== 書く状態に戻す =====
void RenderTexture::EndDepthComputeRead() {
	DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}
```

- 前からある `BeginShaderRead` は、色も「PS が読む状態」から切り替える（描き終わった後に使う物）。深度プリパスの後は色がまだ描画先なので、深度だけを切り替える関数を別に作る。


## パートE RenderQueue・DrawRequest

### E-1 `Graphics/Renderer/DrawRequest.h`

`MeshRequest` の `receivesDecals` の下に足す。

```cpp
	bool castsShadows = true;                                  // false：影を落とさない（影の深度に描かない。不透明な物だけ落とす）
```

### E-2 `Graphics/Renderer/RenderQueue.h`

`// 前方宣言` に1行足す（`PSOKey` は private の関数の戻り値にだけ使う。戻り値の型は、宣言だけなら中身を知らなくてよい）。

```cpp
// 前方宣言
class RenderWindow;
struct PSOKey;
```

`FlushTransparentMesh`・`FlushOpaqueMesh` の2行を差し替える。

```cpp
	// shadowCB：gShadow（影の受け方。ShadowRenderer::PushOpaqueReceiver・PushTransparentReceiver）
	static void FlushTransparentMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB);
	static void FlushOpaqueMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB);
	// 深度プリパス：不透明な物（深度を書く物）の深度だけを描く。色を塗る前に呼ぶ（描画先は深度だけを結んでおく）
	static void FlushDepthPrepass(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	// 影の深度：影を落とす不透明な物の深度だけを、ライトから見て描く（描画先は ShadowMap）。lightCameraCB：ライトから見たカメラの定数
	static void FlushShadowCaster(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS lightCameraCB);
```

private の `FlushMeshList` を差し替え、3つ足す。

```cpp
	// MeshのRootSignature, PSO切り替え（pass：色を塗るか、深度だけか。shadowCB：0 なら gShadow を結ばない＝深度だけのパス）
	static void FlushMeshList(const std::vector<MeshRequest>& meshes, const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB, MeshPass pass, const char* name);
	// そのパスで描く物か（深度だけのパスは、深度を書く物だけ。影の深度は、さらに影を落とす物だけ）
	static bool IsDrawnInPass(const MeshRequest& req, MeshPass pass);
	// そのパスの PSOKey（色を塗るパスは物の設定のまま。深度だけのパスは、ブレンド・ステンシルを使わない形にそろえる）
	static PSOKey MakeMeshPSOKey(const MeshRequest& req, MeshPass pass);
	// 不透明な物を、状態の切り替えが少ない順（sortKey）に並べる。並べ終わっていれば何もしない
	static void SortOpaqueMeshes();
```

メンバーの `opaqueMeshRequests_` の下に足す。

```cpp
	bool isOpaqueSorted_ = false; // opaqueMeshRequests_ を並べ終えたか（1ビューで3回描くので、並べるのは足した後の1回だけ）
```

### E-3 `Graphics/Renderer/RenderQueue.cpp`

include に1行足す（`<algorithm>` の下）。

```cpp
#include <optional>
```

`Clear` に1行足す（`opaqueMeshRequests_.clear()` の下）。

```cpp
	instance_->isOpaqueSorted_ = false;
```

`Request(MeshRequest&&)` の不透明の所に1行足す。

```cpp
	} else {
		instance_->opaqueMeshRequests_.push_back(std::move(req)); // 不透明(TestWrite)
		instance_->isOpaqueSorted_ = false;                       // 足したので、並べ直す
	}
```

`Mesh` の大見出しの中（`FlushMeshList` 〜 `FlushTransparentMesh`）を全部差し替える。

```cpp
// ===== RootSignature, PSOソート =====
void RenderQueue::FlushMeshList(const std::vector<MeshRequest>& meshes, const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, 
	D3D12_GPU_VIRTUAL_ADDRESS shadowCB, MeshPass pass, const char* name) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// SRVセット
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	// ステンシルの参照値（デカールを受けない物が書く印。印を書かない PSO はステンシルを使わないので、全部に同じ値でよい）
	cmdList->OMSetStencilRef(kStencilNoDecalBit);
	// キー初期化
	std::optional<PSOKey> currentKey; // 今セットしている PSO（まだ無ければ空）
	uint32_t scopeIndex = UINT32_MAX;
	uint32_t currentShaderProg = static_cast<uint32_t>(UINT32_MAX);

	// リクエスト分ループ
	for (const MeshRequest& req : meshes) {
		// このビューで描かない物は飛ばす（別のウィンドウ・切ってある層・目で隠した物）
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// このパスで描かない物は飛ばす（深度だけのパスの、深度を書かない物・影を落とさない物）
		if (!IsDrawnInPass(req, pass)) {
			continue;
		}
		// --- シェーダー(RootSignature)が変わったとき ---
		uint32_t shaderProg = PSOManager::GetShaderProgramID(req.category, req.shadingType);
		if (shaderProg != currentShaderProg) {
			// GPUProfiler
			if (scopeIndex != UINT32_MAX) {
				GPUProfiler::End(cmdList, scopeIndex);
			}
			// RootSignature
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(shaderProg);
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			// CBuffer: Camera
			auto camSlot = rs.slotOf.find(RootBind::Camera);
			if (cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			// CBuffer: Texture
			auto texSlot = rs.slotOf.find(RootBind::BindlessTexture);
			if (texSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
			}
			// CBuffer: TextureCube
			auto texCubeSlot = rs.slotOf.find(RootBind::BindlessTextureCube);
			if (texCubeSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texCubeSlot->second, heapStart);
			}
			// CBuffer: Shadow（影の受け方。色を塗るパスだけ。深度だけのパスの PS は読まない）
			auto shadowSlot = rs.slotOf.find(RootBind::Shadow);
			if (shadowCB && shadowSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(shadowSlot->second, shadowCB);
			}
			currentShaderProg = shaderProg;
		}
		// --- PSOが変わったとき ---
		const PSOKey psoKey = MakeMeshPSOKey(req, pass);
		if (currentKey != psoKey) {
			// GPUProfiler
			if (scopeIndex != UINT32_MAX) {
				GPUProfiler::End(cmdList, scopeIndex);
			}
			// PSO
			cmdList->SetPipelineState(PSOManager::GetPSO(psoKey).Get());
			scopeIndex = GPUProfiler::Begin(cmdList, (std::string(name) + "/" + PSOManager::GetStateName(psoKey)).c_str());
			currentKey = psoKey;
		}

		RenderContext::DrawMesh(req);
	}
	if (scopeIndex != UINT32_MAX) {
		GPUProfiler::End(cmdList, scopeIndex);
	}
}

// ===== そのパスで描く物か =====
bool RenderQueue::IsDrawnInPass(const MeshRequest& req, MeshPass pass) {
	switch (pass) {
	case MeshPass::Color:
		return true;
	case MeshPass::DepthPrepass:
		// 深度を書く物だけ（深度を見ない Disable の物は、いつも手前に描くので、深度に入れない）
		return req.depthMode == DepthMode::TestWrite;
	case MeshPass::ShadowCaster:
		// 深度を書く物のうち、影を落とす物だけ。エディタのグリッド・ギズモ（Default 以外の層）は落とさない
		return req.depthMode == DepthMode::TestWrite && req.castsShadows && req.layer == DrawLayer::Default;
	}
	return false;
}

// ===== そのパスの PSOKey =====
PSOKey RenderQueue::MakeMeshPSOKey(const MeshRequest& req, MeshPass pass) {
	if (pass == MeshPass::Color) {
		return PSOManager::GetPSOKey(req.category, req.shadingType, req.blendMode, req.rasterizerType, req.depthMode, req.stencilMode);
	}
	// 深度だけのパスは色を出さないので、ブレンドは何でもよい。ステンシル（デカールを受けない印）は、色を塗るパスで書く
	// そろえておくと、ブレンド・ステンシルだけが違う物が同じ PSO になる
	return PSOManager::GetPSOKey(req.category, req.shadingType, BlendMode::Normal, req.rasterizerType, DepthMode::TestWrite, StencilMode::None, pass);
}

// ===== 不透明な物を並べる =====
void RenderQueue::SortOpaqueMeshes() {
	if (instance_->isOpaqueSorted_) {
		return;
	}
	auto& meshes = instance_->opaqueMeshRequests_;
	// 状態切替を減らす＝状態キーでソート
	std::sort(meshes.begin(), meshes.end(), [](const MeshRequest& a, const MeshRequest& b) { return a.sortKey < b.sortKey; });
	instance_->isOpaqueSorted_ = true;
}

// ===== 不透明 =====
void RenderQueue::FlushOpaqueMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB) {
	SortOpaqueMeshes();
	FlushMeshList(instance_->opaqueMeshRequests_, windowTitle, cameraCB, shadowCB, MeshPass::Color, "Opaque");
}

// ===== 深度プリパス =====
void RenderQueue::FlushDepthPrepass(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	SortOpaqueMeshes();
	FlushMeshList(instance_->opaqueMeshRequests_, windowTitle, cameraCB, 0, MeshPass::DepthPrepass, "Depth Prepass");
}

// ===== 影の深度 =====
void RenderQueue::FlushShadowCaster(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS lightCameraCB) {
	SortOpaqueMeshes();
	FlushMeshList(instance_->opaqueMeshRequests_, windowTitle, lightCameraCB, 0, MeshPass::ShadowCaster, "Shadow Caster");
}

// ===== 半透明 =====
void RenderQueue::FlushTransparentMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB) {
	auto& meshes = instance_->transparentMeshRequests_;
	// 正しくブレンド＝奥→手前（距離²の降順）
	std::sort(meshes.begin(), meshes.end(), [](const MeshRequest& a, const MeshRequest& b) { return a.cameraDistanceSq > b.cameraDistanceSq; });
	FlushMeshList(meshes, windowTitle, cameraCB, shadowCB, MeshPass::Color, "Transparent");
}
```

- PSO が変わったかは、前は `sortKey` で比べていた。深度だけのパスはブレンド・ステンシルをそろえる（同じ `sortKey` でなくても同じ PSO になる）ので、`PSOKey` そのもので比べる。
- 影の深度でライトの「カメラ」を結ぶのは、`Object3dVS` が `gCamera.viewProj`（とビルボードの右・上）だけで位置を決めるため。ライトから見た行列を `gCamera` に入れれば、VS を変えずにライトから見た深度が描ける。ビルボードの物は、ライトの方を向いた形で影を落とす。
- 深度だけのパスでは `gShadow` を結ばない（0 を渡す）。RootSignature にはあるが、`ModelDepthPS` は読まないので、結ばなくてもデバッグレイヤーは止めない（写しで確かめた）。


## パートF ShadowRenderer（新しい）

### F-1 `Graphics/Shadow/ShadowRenderer.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include <d3d12.h>

#include "HTTEngine/Graphics/Compute/ComputeTexture.h"
#include "HTTEngine/Graphics/Shadow/ShadowMap.h"
#include "HTTEngine/Graphics/Shadow/ShadowSettings.h"
#include "HTTEngine/Math/MathIncludes.h"

// 前方宣言
class Camera;
class RenderTexture;

/// <summary>
/// 平行光源の影を作るクラス（SceneRenderer が1つ持ち、ビューごとに呼ぶ）
/// <para>作る物は「影の量の画面」（3D の絵と同じ大きさ。1 で光が届く、0 で影）。Model の PS はこれを読むだけ</para>
/// <para>今の作り方はシャドウマップ（ライトから見た深度の絵）。レイトレの影に変えるときは、BuildShadowMask の中身だけを入れ替える</para>
/// <para>1ビューの順番：RenderShadowMap → （深度プリパス）→ BuildShadowMask → PushOpaqueReceiver・PushTransparentReceiver を結んで色を塗る</para>
/// </summary>
class ShadowRenderer {
public:
	/// <summary>
	/// 影の量の画面を作る
	/// </summary>
	/// <param name="width">3D の絵の幅（SceneRenderer の postRT_ と同じ）</param>
	/// <param name="height">3D の絵の高さ</param>
	ShadowRenderer(uint32_t width, uint32_t height);

	// コピー禁止（GPU の物を持つ）
	ShadowRenderer(const ShadowRenderer&) = delete;
	ShadowRenderer& operator=(const ShadowRenderer&) = delete;

	/// <summary>
	/// このビューの影の深度を描く（ビューのカメラから見える所に合わせて、ライトから見る箱を決める）
	/// <para>ビューを描き始めるときに毎回呼ぶ。影を出さないとき（平行光源が無い・カメラが無い）は描かず、このビューは影なしになる</para>
	/// </summary>
	/// <param name="camera">このビューのカメラ（nullptr なら影なし）</param>
	/// <param name="settings">このフレームの平行光源の影</param>
	/// <param name="windowTitle">描くウィンドウ（RenderQueue が、ほかのウィンドウの物を飛ばす）</param>
	void RenderShadowMap(const Camera* camera, const DirectionalShadowSettings& settings, const std::wstring& windowTitle);

	/// <summary>
	/// 影の量の画面を作る（CS）。深度プリパスの後・色を塗る前に呼ぶ
	/// <para>3D の絵の深度から位置を戻し、ライトから見た深度と比べる。RenderShadowMap で描かなかったビューは何もしない</para>
	/// </summary>
	/// <param name="scene">3D の絵（深度プリパスを描き終えた物。描画先に結んだままでよい）</param>
	/// <param name="cameraCB">このビューのカメラの定数（深度から位置へ戻す inverseViewProj を読む）</param>
	void BuildShadowMask(RenderTexture& scene, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);

	/// <summary>
	/// 不透明な物の gShadow を、このフレームの定数バッファに書いて場所を返す（影の量の画面。影の無いビューは影なし）
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS PushOpaqueReceiver() const;

	/// <summary>
	/// 半透明な物の gShadow を書いて場所を返す（いつも影なし）
	/// <para>影の量の画面は、一番手前の不透明な物の影なので、その前にある半透明な物には使えない</para>
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS PushTransparentReceiver() const;

private:
	// ライトから見る箱（1ビュー分）
	struct LightView {
		Matrix4x4 viewProj = MakeIdentity4x4(); // ワールド → ライトから見た画面（x・y が -1〜1、z が 0〜1）
		Vector3 position;                       // 箱の手前の面の真ん中（ライトの位置の代わり）
		Vector3 right;                          // ライトから見た右（影の深度を描くときの、ビルボードの向き）
		Vector3 up;                             // ライトから見た上
		float depthRange = 1.0f;                // 箱の奥行き（m）。Bias（m）を深度の 0〜1 に直すのに使う
	};

	/// <summary>
	/// カメラから影を出す距離までを包む球を求め、その球をライトの向きから見る箱を作る
	/// </summary>
	/// <param name="mapPixels">影の深度の絵の1辺（箱の位置を1ピクセルずつ動かして、カメラが動いても影の縁がちらつかないようにする）</param>
	static LightView MakeLightView(const Camera& camera, const DirectionalShadowSettings& settings, uint32_t mapPixels);

	std::unique_ptr<ShadowMap> shadowMap_;    // 影の深度の絵（大きさは平行光源の Map Size。変わったら作り直す）
	std::unique_ptr<ComputeTexture> mask_;    // 影の量の画面（R8。3D の絵と同じ大きさ）
	DirectionalShadowSettings settings_;      // このビューの影の設定（RenderShadowMap で覚え、BuildShadowMask で使う）
	LightView lightView_;                     // このビューのライトから見る箱
	bool hasShadowMap_ = false;               // このビューで影の深度を描いたか
	bool hasMask_ = false;                    // このビューで影の量の画面を作ったか
};
```

### F-2 `Graphics/Shadow/ShadowRenderer.cpp`（新しい）

```cpp
#include "ShadowRenderer.h"

#include <algorithm>
#include <cmath>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Pipeline/PSOManager.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Profiling/GPUScope.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/Graphics/Renderer/RenderQueue.h"

namespace {
// 影の量の画面を作る CS（Shader/Data/Shadow/ShadowMask.CS.shader）
constexpr const char* kShadowMaskCS = "ShadowMaskCS";
// ライトの方へ、球の真ん中から半径の何倍まで箱を伸ばすか（球の外にある、ライトの側の物の影も落とすため）
constexpr float kCasterReach = 3.0f;

// ===== ShadowMask.CS の gShadowMask（Shader/Data/Shadow/ShadowMask.CS.shader の ShadowMaskParams と同じ並び）=====
struct ShadowMaskParams {
	Matrix4x4 lightViewProj = MakeIdentity4x4(); // ワールド → ライトから見た画面（x・y が -1〜1、z が 0〜1）
	uint32_t depthIndex = 0;                     // 3D の絵の深度（gTextures の番号）
	uint32_t shadowMapIndex = 0;                 // 影の深度の絵（gTextures の番号）
	uint32_t shadowMapSize = 0;                  // 影の深度の絵の1辺（ピクセル）
	float depthBias = 0.0f;                      // 比べる前に引く深度（ライトの深度の 0〜1 で）
	uint32_t maskWidth = 0;                      // 影の量の画面の幅（ピクセル）
	uint32_t maskHeight = 0;                     // 影の量の画面の高さ（ピクセル）
	float strength = 1.0f;                       // 影の濃さ（0：影なし 1：光が全く届かない）
	float distance = 0.0f;                       // カメラからこの距離（m）まで影を出す（最後の 1 割で薄くして消す）
};
static_assert(sizeof(ShadowMaskParams) == 96, "HLSLのShadowMaskParamsと大きさが違います");
} // namespace


//=============================================================================
// 作成
//=============================================================================
ShadowRenderer::ShadowRenderer(uint32_t width, uint32_t height) {
	// 影の量は 0〜1 の1つの値なので、8bit 1チャンネルで足りる
	mask_ = std::make_unique<ComputeTexture>(width, height, DXGI_FORMAT_R8_UNORM, L"ShadowMask");
	// CS の PSO を先に作っておく（最初に影を出したフレームで止まらないように）
	PSOManager::GetComputePipeline(kShadowMaskCS);
}


//=============================================================================
// 影の深度
//=============================================================================
void ShadowRenderer::RenderShadowMap(const Camera* camera, const DirectionalShadowSettings& settings, const std::wstring& windowTitle) {
	// 前のビューの物は使わない
	hasShadowMap_ = false;
	hasMask_ = false;
	settings_ = settings;
	if (!settings.enabled || camera == nullptr) {
		return;
	}

	// --- 影の深度の絵（大きさが変わったら作り直す。前のフレームの GPU の仕事は終わっているので、すぐ消してよい）---
	const uint32_t mapPixels = GetShadowMapPixels(settings.params.mapSize);
	if (!shadowMap_ || shadowMap_->GetSize() != mapPixels) {
		shadowMap_ = std::make_unique<ShadowMap>(mapPixels);
	}

	// --- ライトから見る箱と、そこから見たカメラの定数 ---
	lightView_ = MakeLightView(*camera, settings, mapPixels);
	CameraData lightCamera{};
	lightCamera.viewProj = lightView_.viewProj;
	lightCamera.inverseViewProj = Inverse(lightView_.viewProj);
	lightCamera.worldPosition = lightView_.position;
	lightCamera.right = lightView_.right;
	lightCamera.up = lightView_.up;
	const D3D12_GPU_VIRTUAL_ADDRESS lightCameraCB = RenderContext::PushFrameConstant(lightCamera);

	// --- 描く（影を落とす不透明な物の、深度だけ）---
	auto* cmdList = DirectXCommon::GetCommandList();
	GPU_SCOPE(cmdList, "Shadow Map");
	shadowMap_->BeginRender();
	RenderQueue::FlushShadowCaster(windowTitle, lightCameraCB);
	shadowMap_->EndRender();
	hasShadowMap_ = true;
}


//=============================================================================
// 影の量の画面
//=============================================================================
void ShadowRenderer::BuildShadowMask(RenderTexture& scene, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	if (!hasShadowMap_ || cameraCB == 0) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	GPU_SCOPE(cmdList, "Shadow Mask");

	// --- 定数 ---
	ShadowMaskParams params;
	params.lightViewProj = lightView_.viewProj;
	params.depthIndex = scene.GetDepthSRVSlot();
	params.shadowMapIndex = shadowMap_->GetSRVSlot();
	params.shadowMapSize = shadowMap_->GetSize();
	params.depthBias = (std::max)(settings_.params.bias, 0.0f) / lightView_.depthRange; // m → ライトの深度の 0〜1
	params.maskWidth = mask_->GetWidth();
	params.maskHeight = mask_->GetHeight();
	params.strength = std::clamp(settings_.params.strength, 0.0f, 1.0f);
	params.distance = settings_.params.distance;
	const D3D12_GPU_VIRTUAL_ADDRESS paramsCB = RenderContext::PushFrameConstant(params);

	// --- 3D の絵の深度を CS で読める状態にして、1ピクセル 1スレッドで比べる ---
	scene.BeginDepthComputeRead();
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	const ComputePipeline& pipeline = PSOManager::GetComputePipeline(kShadowMaskCS);
	mask_->BeginWrite();
	pipeline.Bind(cmdList);
	pipeline.SetConstantBuffer(cmdList, "gCamera", cameraCB);
	pipeline.SetConstantBuffer(cmdList, "gShadowMask", paramsCB);
	pipeline.SetTextureTable(cmdList, "gTextures", heapStart);
	pipeline.SetUnorderedAccessTexture(cmdList, "gOutput", mask_->GetUAVGPUHandle());
	pipeline.Dispatch(cmdList, mask_->GetWidth(), mask_->GetHeight());
	mask_->EndWrite(); // 書き終わってから、色を塗る PS が読む
	scene.EndDepthComputeRead();
	hasMask_ = true;
}


//=============================================================================
// 影の受け方
//=============================================================================
// ===== 不透明な物 =====
D3D12_GPU_VIRTUAL_ADDRESS ShadowRenderer::PushOpaqueReceiver() const {
	ShadowReceiverData data;
	data.maskIndex = mask_->GetSRVSlot();
	data.hasMask = hasMask_ ? 1u : 0u;
	return RenderContext::PushFrameConstant(data);
}

// ===== 半透明な物 =====
D3D12_GPU_VIRTUAL_ADDRESS ShadowRenderer::PushTransparentReceiver() const {
	ShadowReceiverData data; // hasMask が 0（影なし）
	return RenderContext::PushFrameConstant(data);
}


//=============================================================================
// ライトから見る箱
//=============================================================================
ShadowRenderer::LightView ShadowRenderer::MakeLightView(const Camera& camera, const DirectionalShadowSettings& settings, uint32_t mapPixels) {
	// ===== 1. カメラから見える所のうち、影を出す範囲（手前 near 〜 奥 far）を包む球 =====
	// 奥へ z 進んだ所の四角の角は、カメラの向きの線から z × √(tanX² + tanY²) 離れている
	const float nearZ = camera.GetNearZ();
	const float farZ = (std::max)((std::min)(camera.GetFarZ(), settings.params.distance), nearZ + 0.01f);
	const float tanY = std::tan(camera.GetFovY() * 0.5f);
	const float tanX = tanY * camera.GetAspectRatio();
	const float spreadSq = tanX * tanX + tanY * tanY; // 1 進むごとの、角の広がりの2乗
	// 球の真ん中（カメラの向きの線の上）：near の角と far の角から同じ距離になる所。far を超えるなら far
	const float centerZ = (std::min)((farZ + nearZ) * 0.5f * (1.0f + spreadSq), farZ);
	const float toFarCorner = std::sqrt((farZ - centerZ) * (farZ - centerZ) + farZ * farZ * spreadSq);
	const float toNearCorner = std::sqrt((centerZ - nearZ) * (centerZ - nearZ) + nearZ * nearZ * spreadSq);
	const float radius = (std::max)(toFarCorner, toNearCorner);
	// カメラの前（ビュー行列の3列目。SceneRenderer::UploadCameraCB の右・上と同じ取り方）
	const Matrix4x4& view = camera.GetViewMatrix();
	const Vector3 forward = Normalize(Vector3{view.m[0][2], view.m[1][2], view.m[2][2]});
	const Vector3 center = camera.GetTranslation() + forward * centerZ;

	// ===== 2. ライトの向き（+Z を光の進む向きへ）=====
	// lightRotation の行は、ライトの 右・上・前。純粋な回転なので、転置が逆（ワールド → ライトの向き）
	const Matrix4x4 lightRotation = MakeRotateMatrix(MakeLookRotation(settings.direction));
	const Matrix4x4 worldToLight = Transpose(lightRotation);

	// ===== 3. 箱の真ん中を、影の深度の絵の1ピクセルずつに合わせる =====
	// カメラが少し動くたびに箱が半端な量だけ動くと、影の縁のピクセルが毎フレーム変わってちらつく。1ピクセル単位で動かせば、縁は動かない
	Vector3 centerInLight = TransformPoint(center, worldToLight);
	const float texelSize = (radius * 2.0f) / static_cast<float>(mapPixels);
	centerInLight.x = std::floor(centerInLight.x / texelSize) * texelSize;
	centerInLight.y = std::floor(centerInLight.y / texelSize) * texelSize;

	// ===== 4. 正射影（平行光源は、光が平行なので遠近が無い）=====
	// 奥行きは、ライトの側へ kCasterReach 倍まで伸ばす（球の外にある、ライトの側の物も影を落とす）
	const float nearInLight = centerInLight.z - radius * kCasterReach;
	const float farInLight = centerInLight.z + radius;
	const Matrix4x4 projection = MakeOrthographicMatrix(centerInLight.x - radius, centerInLight.y + radius, centerInLight.x + radius, centerInLight.y - radius, nearInLight, farInLight);

	LightView result;
	result.viewProj = worldToLight * projection;
	result.right = {lightRotation.m[0][0], lightRotation.m[0][1], lightRotation.m[0][2]};
	result.up = {lightRotation.m[1][0], lightRotation.m[1][1], lightRotation.m[1][2]};
	result.position = TransformPoint({centerInLight.x, centerInLight.y, nearInLight}, lightRotation);
	result.depthRange = farInLight - nearInLight;
	return result;
}
```

- 球の真ん中の式：真ん中をカメラの向きの線の上の `c` に置くと、far の角までの距離² は `(far − c)² + far²·k²`、near の角までは `(c − near)² + near²·k²`（`k² = tanX² + tanY²`）。2つが同じになる `c` を解くと `(far + near)(1 + k²) / 2`。far より奥になるとき（広角のカメラ）は far に置く。
- 球の大きさは、カメラの視野角・比率・Near・`Distance` だけで決まる。カメラが動いても回っても変わらないので、影の細かさも変わらない（Unity の「Stable Fit」と同じ考え）。
- 影の深度の絵は、ビューごとに描き直す（Scene ビューと Game ビューで箱が違う）。エディタは2回、製品版は1回。


## パートG SceneRenderer

### G-1 `Graphics/Renderer/SceneRenderer.h`

include に2行足す（`SceneBackground.h` の下）。

```cpp
#include "HTTEngine/Graphics/Shadow/ShadowRenderer.h"
#include "HTTEngine/Graphics/Shadow/ShadowSettings.h"
```

`SetFog` の下に足す。

```cpp
	// --- 影（ShadowRenderer が、ビューごとに影の深度・影の量の画面を作る）---
	// 平行光源の影（LightSystem が、シーンの Directional Light から毎フレーム書く。平行光源が無ければ「影なし」を書く）
	static void SetDirectionalShadow(const DirectionalShadowSettings& settings) { instance_->directionalShadow_ = settings; }
```

private の `RenderOpaque` の下に足す。

```cpp
	// 深度プリパスと影の量の画面（不透明な物の色を塗る前。描画先は深度だけにして、終わったら色と深度に戻す）
	static void RenderDepthPrepass(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, RenderTexture& work);
```

メンバーに2つ足す（`postProcess_` の下と `fog_` の下）。

```cpp
	std::unique_ptr<ShadowRenderer> shadow_;   // 平行光源の影（影の量の画面は postRT_ と同じ大きさ）
```

```cpp
	DirectionalShadowSettings directionalShadow_; // 平行光源の影（初期値は影なし）
```

### G-2 `Graphics/Renderer/SceneRenderer.cpp`

`InitializePost` の最後に足す。

```cpp
	// 影（影の量の画面は、3D の絵のピクセルごとに読むので、同じ大きさ）
	instance_->shadow_ = std::make_unique<ShadowRenderer>(width, height);
```

`RenderWorldForPost` の `// --- 不透明な物と空 ---` の上に足す。

```cpp
	// --- 影の深度（ライトから見た深度。ライトから見る箱は、このビューのカメラに合わせて決める）---
	instance_->shadow_->RenderShadowMap(camera, instance_->directionalShadow_, windowTitle);

```

`RenderOpaque` の頭の `FlushOpaqueMesh` の行を差し替える。

```cpp
	auto* cmdList = DirectXCommon::GetCommandList();
	// 先に手前の深度をそろえ、影の量の画面を作る（色を塗る PS が、自分のピクセルの影の量を読めるように）
	if (camCB != 0) {
		RenderDepthPrepass(camCB, windowTitle, work);
	}
	RenderQueue::FlushOpaqueMesh(windowTitle, camCB, instance_->shadow_->PushOpaqueReceiver()); // 不透明Model（関数内で計測済み）
```

`RenderTransparent` の上に `RenderDepthPrepass` を足し、`RenderTransparent` の `FlushTransparentMesh` の行を差し替える。

```cpp
// ===== 深度プリパスと影の量の画面 =====
void SceneRenderer::RenderDepthPrepass(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, RenderTexture& work) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// --- 不透明な物の深度だけ（深度だけの PSO は RTV なしなので、描画先も深度だけにする）---
	{
		GPU_SCOPE(cmdList, "Depth Prepass");
		work.BindDepthOnly();
		RenderQueue::FlushDepthPrepass(windowTitle, camCB);
	}
	// --- 影の量の画面（今の深度を CS で読む）---
	instance_->shadow_->BuildShadowMask(work, camCB);
	// --- 色を塗る描画先に戻す（色を塗るパスは、同じ深度で LESS_EQUAL なので、プリパスで書いた一番手前の物だけが塗られる）---
	work.BindColorAndDepth();
}

// ===== 深度を書かない物（半透明・線・パーティクル）=====
void SceneRenderer::RenderTransparent(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle) {
	auto* cmdList = DirectXCommon::GetCommandList();
	RenderQueue::FlushTransparentMesh(windowTitle, camCB, instance_->shadow_->PushTransparentReceiver()); // 半透明Model（関数内で計算済み）
```

- 影の深度は `work->PreDraw` の前に描く。`ShadowMap::BeginRender` が描画先・ビューポートを影の深度の絵に変えるが、その後の `PreDraw`・`SetViewportAndScissor` が 3D の絵に戻す。
- 被写界深度を掛けるビューも掛けないビューも、`RenderOpaque` を通るので、どちらも影が出る。


## パートH Cast Shadows（物が影を落とすか）

### H-1 `Graphics/Renderer/Renderer.h`

`receivesDecals` を持つ Config（`ModelConfig`・`SphereConfig`・`MeshConfig`・`OBBConfig` の4つ）の、`receivesDecals` の行の下に足す。

```cpp
		bool castsShadows = true;                                  // false：影を落とさない（不透明なときだけ落とす）
```

### H-2 `Graphics/Renderer/Renderer.cpp`

`PushMesh`（引数に `billboard` がある方）の、デカールの `if constexpr` の下に足す。

```cpp
	// --- 影を落とすか：Config が castsShadows を持っていれば、その値（持たない形は落とす）---
	if constexpr (requires { config.castsShadows; }) {
		req.castsShadows = config.castsShadows;
	}
```

`DrawModel` の `req.receivesDecals = config.receivesDecals;` の下に足す。

```cpp
			req.castsShadows = config.castsShadows;
```

### H-3 `Components/Rendering3D/ModelRendererComponent.h`

`receivesDecals` の下に足す。

```cpp
	bool castsShadows = true;                      // false：影を落とさない（平行光源の影。不透明なときだけ落とす）
```

### H-4 `Editor/Inspector/ModelRendererEditor.cpp`

`Receive Decals` の Tooltip の下に足す。

```cpp
	ImGui::Checkbox("Cast Shadows", &render.castsShadows);
	ImGui::SetItemTooltip("外すと、このモデルは平行光源の影を落とさない（影を受けるのはそのまま。Depth が TestWrite のときだけ落とす）");
```

### H-5 `Components/EngineComponents.cpp`（Model Renderer の保存）

`DescribeModelRenderer` の `receivesDecals` の下に足す。

```cpp
	ui.Field("castsShadows", value.castsShadows);     // 影を落とすか（前のファイルには無い＝落とす）
```

### H-6 `Systems/Rendering3D/ModelRenderSystem.cpp`

`config.receivesDecals = render.receivesDecals;` の下に足す。

```cpp
		config.castsShadows = render.castsShadows;
```

### H-7 `Components/Rendering3D/PrimitiveRendererComponent.h`（`PrimitiveMaterial`）

`receivesDecals` の下に足す。

```cpp
	bool castsShadows = true;                           // false：影を落とさない（平行光源の影。Opaque のときだけ落とす）
```

### H-8 `Systems/Rendering3D/PrimitiveRenderSystem.h`（`ApplyMaterial`）

`config.receivesDecals = material.receivesDecals;` の下に足す。

```cpp
		config.castsShadows = material.castsShadows;
```

### H-9 `Systems/Rendering3D/PrimitiveRenderSystem.cpp`（`DescribeMaterial`）

`Receive Decals` の行の下に足す。

```cpp
	// ===== 影 =====
	ui.Field(label("Cast Shadows", "castsShadows").c_str(), material.castsShadows, Tip("外すと、この形は平行光源の影を落とさない（影を受けるのはそのまま。Opaque のときだけ落とす）"));
```

- `PrimitiveMaterial` は Primitive Renderer・Elastic Surface・Polygon Renderer などが共通で持つので、全部に `Cast Shadows` が出る。


## パートI Directional Light の影

### I-1 `Components/Lighting/LightComponent.h`

include に1行足す（一番上）。

```cpp
#include "HTTEngine/Graphics/Shadow/ShadowSettings.h"
```

`DirectionalLightComponent` の `intensity` の下に足す。

```cpp
	DirectionalShadowParams shadow;     // 影（出すか・範囲・濃さ・ずらす量）
```

### I-2 `Editor/Inspector/LightEditor.cpp`

include に1行足す（`imgui.h` の下に1行空けて）。

```cpp
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
```

`namespace {` の中の定数と、影の項目を描く関数を足す（`kMaxIntensity` の行を差し替え）。

```cpp
constexpr float kMaxIntensity = 100.0f;       // 強さの上限（ドラッグで行き過ぎないように）
constexpr float kMaxShadowDistance = 1000.0f; // 影を出す距離の上限（m）

// 平行光源の影
void DrawDirectionalShadow(DirectionalShadowParams& shadow) {
	ImGui::SeparatorText("Shadow");
	ImGui::PushID("shadow");
	ImGui::Checkbox("Cast Shadows", &shadow.castsShadows);
	ImGui::SetItemTooltip("影を出す（Model Renderer・Primitive の Cast Shadows が入っている、不透明な物が影を落とす）");
	EditorWidgets::EnumCombo("Map Size", shadow.mapSize);
	ImGui::SetItemTooltip("ライトから見た深度の絵の大きさ。大きいほど影の縁が細かいが重い");
	ImGui::DragFloat("Distance", &shadow.distance, 0.1f, 1.0f, kMaxShadowDistance, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("カメラからこの距離（m）まで影を出す（最後の 1 割で薄くなって消える）。短いほど、同じ Map Size で影が細かい");
	ImGui::DragFloat("Strength", &shadow.strength, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("影の濃さ。1 で平行光源の光が全く届かない。Lambert・Half Lambert は環境光が無いので、1 だと真っ黒になる");
	ImGui::DragFloat("Bias", &shadow.bias, 0.001f, 0.0f, 1.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("深度を比べる前に引く距離（m）。自分の影が縞になる（シャドウアクネ）なら上げる。影が物から浮く（ピーターパン）なら下げる");
	ImGui::PopID();
}
```

`DirectionalLightEditor::DrawComponent` の `Intensity` の下に1行足す。

```cpp
	DrawDirectionalShadow(light.shadow);
```

### I-3 `Components/EngineComponents.cpp`（Directional Light の保存）

`DescribeDirectionalLight` を差し替える。

```cpp
void DescribeDirectionalLight(ComponentUI& ui, DirectionalLightComponent& value) {
	ui.ColorField("color", value.color); // 見た目の色（sRGB）。リニアへの変換は LightSystem が集めるときに行う
	ui.Field("intensity", value.intensity);
	// --- 影（前のファイルには無い＝既定の値）---
	ui.Field("castsShadows", value.shadow.castsShadows);
	ui.Field("shadowMapSize", value.shadow.mapSize);
	ui.Field("shadowDistance", value.shadow.distance);
	ui.Field("shadowStrength", value.shadow.strength);
	ui.Field("shadowBias", value.shadow.bias);
	DescribeGizmo(ui, value.gizmo);
}
```

### I-4 `Systems/Lighting/LightSystem.cpp`

include に1行足す（`Renderer.h` の下）。

```cpp
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
```

`CollectForGPU` の平行光源の所を差し替える（`DirectionalLightData directional;` 〜 `ForEach` の終わり）。

```cpp
	DirectionalLightData directional;
	directional.intensity = 0.0f; // 1つも無ければ照らさない
	DirectionalShadowSettings shadow; // 影も同じ平行光源から（1つも無ければ影なし）
	uint32_t directionalCount = 0;
	EntityManager::ForEach<DirectionalLightComponent>([&](Handle<Entity> entity, const DirectionalLightComponent& light) {
		const TransformComponent* transform = FindActiveTransform(entity);
		if (!transform) {
			return;
		}
		if (directionalCount == 0) {
			directional.color = ToLightColor(light.color);
			directional.intensity = light.intensity;
			directional.direction = LightDirection(*transform);
			shadow.enabled = light.shadow.castsShadows;
			shadow.direction = directional.direction;
			shadow.params = light.shadow;
		}
		++directionalCount;
	});
	SceneRenderer::SetDirectionalShadow(shadow);
```

- 影の設定は、霧（`FogSystem` → `SceneRenderer::SetFog`）と同じく、System が毎フレーム SceneRenderer へ書く。光の色・向き（`Renderer::SetFrameLights`）は GPU に送る定数で、影は「描く順番」を変える物なので、置き場所が違う。


## パートJ シェーダー

### J-1 `Shader/Data/Model/ModelDepth.PS.shader`（新しい）

```hlsl
#META
stage:   PS
profile: ps_6_0
#META_END

#HLSL
// 深度だけのパス（深度プリパス・影の深度）の PS。色は出さず、色を塗る PS と同じピクセルを捨てるだけ
// VS は物の Shader の VS（Object3dVS）をそのまま使う（PSOManager::MakePSO）。RTV が無いので、戻り値も無い
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"

void main(VertexShaderOutput input)
{
    // 切り抜き（GetModelSurface の中で捨てる）と、色を塗る PS の「A が 0 なら捨てる」を、ここでも同じにする
    // ここで捨てないピクセルに深度を書くと、色を塗る PS が捨てた所に、後ろの物も空も描かれない穴が開く
    ModelSurface surface = GetModelSurface(input.texcoord);
    if (surface.alpha == 0.0f)
    {
        discard;
    }
}
#HLSL_END
```

- `#PROGRAM` は書かない（物の Shader として選ばれる物ではない。PSOManager が名前 `ModelDepthPS` で引く）。

### J-2 `Shader/Data/Shadow/ShadowReceiver.hlsli.shader`（新しい。`Shadow` フォルダも新しい）

```hlsl
#META
stage: Include
#META_END

#HLSL
// 影の受け方。Model の PS が、平行光源の光にどれだけ影が掛かるかを読む
// 影の作り方（シャドウマップ・レイトレ）に関係なく、PS は「影の量の画面」の自分のピクセルを読むだけ
// gTextures（Resources/Texture.hlsli）を使うので、ModelSurface.hlsli の後に include する（Texture.hlsli を2回入れると、同じ名前が2つになる）

// C++ の ShadowReceiverData（ShaderConstants.h）と同じ並び
struct ShadowReceiver
{
    uint maskIndex; // 影の量の画面（gTextures の番号）
    uint hasMask;   // 1：影の量の画面を読む 0：影なし（影を出さないビュー・半透明な物）
    float2 pad;
};
ConstantBuffer<ShadowReceiver> gShadow : register(b5);

// ===== 平行光源の光が届く割合（1：届く 0：影）=====
// svPosition：SV_POSITION（PS では、描いているピクセルの位置。影の量の画面は 3D の絵と同じ大きさなので、そのまま読める）
float GetDirectionalShadow(float4 svPosition)
{
    if (gShadow.hasMask == 0)
    {
        return 1.0f;
    }
    return gTextures[gShadow.maskIndex].Load(int3(svPosition.xy, 0)).r;
}
#HLSL_END
```

- `b5` は空いている番号（`b0` カメラ・`b1` 物の行列・`b2`〜`b4` ライト・`b20` マテリアル・`b21` IBL・`b22` 描き手の値・`b23` デカール）。

### J-3 `Shader/Data/Shadow/ShadowMask.CS.shader`（新しい）

```hlsl
#META
stage:   CS
profile: cs_6_0
#META_END

#HLSL
// ===== 影の量の画面を作る（1スレッド = 3D の絵の1ピクセル）=====
// 3D の絵の深度から、そのピクセルに写っている物のワールドの位置を戻し、ライトから見た深度の絵（シャドウマップ）と比べる
// 1：光が届く 0：影（Strength で薄める）。Model の PS は、この画面の自分のピクセルを読むだけ（Shadow/ShadowReceiver.hlsli）
// レイトレの影に変えるときは、3. の「比べる」を「ライトへ光線を飛ばす」に変える（1. 2. 4. と、PS の側はそのまま）
#include "Resources/Texture.hlsli"
#include "Buffers/Camera.hlsli"

// C++ の ShadowMaskParams（ShadowRenderer.cpp）と同じ並び
struct ShadowMaskParams
{
    float4x4 lightViewProj; // ワールド → ライトから見た画面（x・y が -1〜1、z が 0〜1）
    uint depthIndex;        // 3D の絵の深度（gTextures の番号）
    uint shadowMapIndex;    // 影の深度の絵（gTextures の番号）
    uint shadowMapSize;     // 影の深度の絵の1辺（ピクセル）
    float depthBias;        // 比べる前に引く深度（ライトの深度の 0〜1 で）
    uint2 maskSize;         // 影の量の画面の大きさ（ピクセル）
    float strength;         // 影の濃さ（0：影なし 1：光が全く届かない）
    float distance;         // カメラからこの距離（m）まで影を出す（最後の 1 割で薄くして消す）
};
ConstantBuffer<ShadowMaskParams> gShadowMask : register(b1);

RWTexture2D<float> gOutput : register(u0); // 影の量の画面（ShadowRenderer の mask_）

[numthreads(8, 8, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t2 pixel = dispatchThreadId.xy;
    // 最後のグループには、絵の外のスレッドがいる。何もせず帰る
    if (any(pixel >= gShadowMask.maskSize))
    {
        return;
    }

    // --- 1. 深度から、ワールドの位置へ戻す（デカールと同じ戻し方。Decal/Decal.hlsli）---
    float depth = gTextures[gShadowMask.depthIndex].Load(int32_t3(pixel, 0)).r;
    if (depth >= 1.0f)
    {
        gOutput[pixel] = 1.0f; // 何も描いていない所（空）
        return;
    }
    float2 uv = (float2(pixel) + 0.5f) / float2(gShadowMask.maskSize); // ピクセルの真ん中
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(ndc, gCamera.inverseViewProj);
    float3 worldPosition = world.xyz / world.w;

    // --- 2. ライトから見た画面の、どこに写るか（正射影なので w は 1）---
    float4 lightClip = mul(float4(worldPosition, 1.0f), gShadowMask.lightViewProj);
    float2 shadowUV = float2(lightClip.x * 0.5f + 0.5f, 0.5f - lightClip.y * 0.5f);
    // 箱の外は影の深度が無いので、光が届くことにする
    if (any(shadowUV < 0.0f) || any(shadowUV >= 1.0f) || lightClip.z > 1.0f)
    {
        gOutput[pixel] = 1.0f;
        return;
    }

    // --- 3. 比べる：ライトから見て、自分より手前に物があれば影 ---
    uint32_t2 texel = min(uint32_t2(shadowUV * gShadowMask.shadowMapSize), gShadowMask.shadowMapSize - 1);
    float casterDepth = gTextures[gShadowMask.shadowMapIndex].Load(int32_t3(texel, 0)).r;
    float lit = (lightClip.z - gShadowMask.depthBias <= casterDepth) ? 1.0f : 0.0f;

    // --- 4. 濃さと、遠くで薄めて消す（影を出す距離の所で、影がぷつっと切れて見えないように）---
    float shadow = (1.0f - lit) * gShadowMask.strength;
    float fadeStart = gShadowMask.distance * 0.9f;
    float toCamera = length(worldPosition - gCamera.worldPosition);
    shadow *= 1.0f - saturate((toCamera - fadeStart) / max(gShadowMask.distance - fadeStart, 0.0001f));
    gOutput[pixel] = 1.0f - shadow;
}
#HLSL_END
```

- 名前はファイル名から `ShadowMaskCS` になる（`ShadowRenderer.cpp` の `kShadowMaskCS`）。
- `gCamera` はこのビューのカメラの定数（`SceneRenderer::UploadCameraCB` が作った物）を、そのまま結ぶ。深度から位置へ戻す `inverseViewProj` と、遠くで薄めるためのカメラの位置が入っている。

### J-4 ライトの PS 5本（`Shader/Data/Model/`）

5本とも、include に1行足す（`ModelSurface.hlsli` の下。`gTextures` を使うので、その後に置く）。

```hlsl
#include "Shadow/ShadowReceiver.hlsli"
```

平行光源の所だけ、光に影の量を掛ける。

`Lambert.PS.shader`

```hlsl
        float NdotL = saturate(dot(N,lightDir));
        float shadow = GetDirectionalShadow(input.position); // 光が届く割合（影の量の画面）
        diffuseLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * NdotL * shadow;
```

`HalfLambert.PS.shader`

```hlsl
        float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
        float shadow = GetDirectionalShadow(input.position); // 光が届く割合（影の量の画面）
        
        diffuseLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * cos * shadow;
```

`Phong.PS.shader`（拡散反射にも鏡面反射にも掛ける）

```hlsl
        float NdotL = saturate(dot(N, L));
        float shadow = GetDirectionalShadow(input.position); // 光が届く割合（影の量の画面。拡散反射にも鏡面反射にも掛ける）
        // 拡散反射
        diffuseLighting += gDirectionalLight.color.rgb * NdotL * gDirectionalLight.intensity * shadow;
        
        // --- Phong ---
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        float3 reflectLight = reflect(-L, N);
        float RdotE = dot(reflectLight, toEye);
        float specularPow = pow(saturate(RdotE), gMaterial.shininess); // 反射強度
        // 鏡面反射
        specularLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * shadow;
```

`BlinnPhong.PS.shader`（拡散反射にも鏡面反射にも掛ける）

```hlsl
        float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
        float shadow = GetDirectionalShadow(input.position); // 光が届く割合（影の量の画面。拡散反射にも鏡面反射にも掛ける）
        // 拡散反射
        diffuseLighting += gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity * shadow;
        
        // --- BlinnPhong ---
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        float3 halfVector = normalize(lightDir + toEye);
        float NdotH = dot(N, halfVector);
        float specularPow = pow(saturate(NdotH), gMaterial.shininess); // 反射強度
        // 鏡面反射
        specularLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * shadow;
```

`PBR.PS.shader`（輝度に掛ける）

```hlsl
        // 影は届く光の量を減らすので、輝度に掛ける（拡散反射も鏡面反射も同じだけ減る）
        float32_t3 radiance = gDirectionalLight.color.rgb * gDirectionalLight.intensity * GetDirectionalShadow(input.position);
        Lo += CookTorranceLighting(N, V, L, albedo, F0, radiance);
```

- `input.position` は VS の `SV_POSITION`。PS の中では、描いているピクセルの位置（左上が 0、ピクセルの真ん中は +0.5）になる。
- 環境光（PBR の IBL・`ambient`）と自己発光には掛けない（平行光源の光だけを遮る）。


## パートK vcxproj

### K-1 `project/HTTEngine.vcxproj`

```xml
    <ClCompile Include="HTTEngine\Graphics\Shadow\ShadowMap.cpp" />
    <ClCompile Include="HTTEngine\Graphics\Shadow\ShadowRenderer.cpp" />
```

```xml
    <ClInclude Include="HTTEngine\Graphics\Shadow\ShadowMap.h" />
    <ClInclude Include="HTTEngine\Graphics\Shadow\ShadowRenderer.h" />
    <ClInclude Include="HTTEngine\Graphics\Shadow\ShadowSettings.h" />
```

```xml
    <None Include="HTTEngine\Shader\Data\Model\ModelDepth.PS.shader" />
    <None Include="HTTEngine\Shader\Data\Shadow\ShadowMask.CS.shader" />
    <None Include="HTTEngine\Shader\Data\Shadow\ShadowReceiver.hlsli.shader" />
```

それぞれ、`Graphics\RenderTarget\` の行の後・`Model\ModelSurface` の行の前・`Skybox\` の行の前に置くと、名前の順になる。

### K-2 `project/HTTEngine.vcxproj.filters`

フォルダ（Filter）を2つ足す（`Graphics\RenderTarget` の後と、`Shader\Data\Resources` の後）。

```xml
    <Filter Include="HTTEngine\Graphics\Shadow">
      <UniqueIdentifier>{e5479646-81d6-45ef-aa68-890b5a4ed18c}</UniqueIdentifier>
    </Filter>
```

```xml
    <Filter Include="HTTEngine\Shader\Data\Shadow">
      <UniqueIdentifier>{76df940d-f549-4f30-95a1-4c431a9403c8}</UniqueIdentifier>
    </Filter>
```

ファイル。

```xml
    <ClCompile Include="HTTEngine\Graphics\Shadow\ShadowMap.cpp">
      <Filter>HTTEngine\Graphics\Shadow</Filter>
    </ClCompile>
    <ClCompile Include="HTTEngine\Graphics\Shadow\ShadowRenderer.cpp">
      <Filter>HTTEngine\Graphics\Shadow</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Graphics\Shadow\ShadowMap.h">
      <Filter>HTTEngine\Graphics\Shadow</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Graphics\Shadow\ShadowRenderer.h">
      <Filter>HTTEngine\Graphics\Shadow</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Graphics\Shadow\ShadowSettings.h">
      <Filter>HTTEngine\Graphics\Shadow</Filter>
    </ClInclude>
```

```xml
    <None Include="HTTEngine\Shader\Data\Model\ModelDepth.PS.shader">
      <Filter>HTTEngine\Shader\Data\Model</Filter>
    </None>
```

```xml
    <None Include="HTTEngine\Shader\Data\Shadow\ShadowMask.CS.shader">
      <Filter>HTTEngine\Shader\Data\Shadow</Filter>
    </None>
    <None Include="HTTEngine\Shader\Data\Shadow\ShadowReceiver.hlsli.shader">
      <Filter>HTTEngine\Shader\Data\Shadow</Filter>
    </None>
```


## パートL Sandbox

### L-1 `project/Sandbox/Resources/scenes/ShadowTest.scene.json`（新しい）

床・箱（BlinnPhong）・浮いた箱（Lambert）・球（Phong）・柱・半透明の箱・`Cast Shadows` を外した箱・歩く人（スキンのあるモデル）。

```json
{
	"version": 5,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 5.0, -11.0],
					"rotation": [22.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
				}
			}
		},
		{
			"id": 2,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 6.0, 0.0],
					"rotation": [45.0, -35.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Directional Light": {
					"color": [1.0, 1.0, 1.0],
					"intensity": 1.0,
					"castsShadows": true,
					"shadowMapSize": "Size2048",
					"shadowDistance": 30.0,
					"shadowStrength": 0.8,
					"shadowBias": 0.05
				}
			}
		},
		{
			"id": 3,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, -0.05, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [24.0, 0.1, 24.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.7, 0.7, 0.7, 1.0],
					"shadingType": "HalfLambert"
				}
			}
		},
		{
			"id": 4,
			"name": "Cube (BlinnPhong)",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.75, 0.0],
					"rotation": [0.0, 30.0, 0.0],
					"scale": [1.5, 1.5, 1.5]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.5, 0.2, 1.0],
					"shadingType": "BlinnPhong",
					"specular": [0.5, 0.5, 0.5]
				}
			}
		},
		{
			"id": 5,
			"name": "Floating Box (Lambert)",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [3.5, 2.5, 1.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.3, 0.6, 0.9, 1.0],
					"shadingType": "Lambert"
				}
			}
		},
		{
			"id": 6,
			"name": "Sphere (Phong)",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-3.5, 1.0, 0.5],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [2.0, 2.0, 2.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Sphere",
					"color": [0.4, 0.85, 0.4, 1.0],
					"shadingType": "Phong",
					"specular": [0.5, 0.5, 0.5]
				}
			}
		},
		{
			"id": 7,
			"name": "Pillar",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [1.5, 2.5, 5.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.6, 5.0, 0.6]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.85, 0.85, 0.8, 1.0],
					"shadingType": "HalfLambert"
				}
			}
		},
		{
			"id": 8,
			"name": "Transparent Box (no shadow)",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-3.0, 0.75, -4.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.5, 1.5, 1.5]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [1.0, 0.3, 0.3, 0.5],
					"surface": "Transparent",
					"shadingType": "HalfLambert"
				}
			}
		},
		{
			"id": 9,
			"name": "Cast Shadows Off Box",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [3.0, 0.75, -4.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.5, 1.5, 1.5]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.9, 0.3, 1.0],
					"shadingType": "HalfLambert",
					"castsShadows": false
				}
			}
		},
		{
			"id": 10,
			"name": "Walker (skinned)",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-1.5, 0.0, 3.0],
					"rotation": [0.0, 200.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Animator": {
					"enabled": true,
					"animation": "HTTEngine/Resources/Model/human/walk.gltf",
					"clip": "Armature|mixamo.com|Layer0",
					"speed": 1.0,
					"loop": true,
					"playOnStart": true,
					"previewTime": 0.3
				},
				"Model Renderer": {
					"enabled": true,
					"model": "HTTEngine/Resources/Model/human/walk.gltf",
					"texture": "",
					"shadingType": "HalfLambert"
				}
			}
		}
	]
}
```

- エディタの Project → `scenes` から開く。一度 Save すると、ほかの項目も書き足される。


## パートM Reference

### M-1 `Docs/Reference/09-Light.md`

一番下に足す。

````markdown
## 影（平行光源）

**平行光源の `shadow` で影を出す。** 物の方は Model Renderer・Primitive の `Cast Shadows`（`castsShadows`）が入っている不透明な物が影を落とす。どちらも既定で入っている。

| 項目（`DirectionalLightComponent::shadow`） | 意味 |
|---|---|
| `castsShadows` | 影を出す |
| `mapSize` | ライトから見た深度の絵の大きさ（`Size1024`・`Size2048`・`Size4096`） |
| `distance` | カメラからこの距離（m）まで影を出す。短いほど影が細かい |
| `strength` | 影の濃さ（0〜1） |
| `bias` | 深度を比べる前に引く距離（m）。自分の影が縞になるなら上げる、影が浮くなら下げる |

```cpp
// 影を出さない物（足元の目印・光る板など）
ModelRendererComponent* render = EntityManager::Get<ModelRendererComponent>(marker);
render->castsShadows = false;
```

- 影を受けるのは、光を使う Shader（Lambert〜PBR）の不透明な物。半透明な物は影を落とさず、受けもしない。
- ポイントライト・スポットライトの影はまだ無い。
- Lambert・Half Lambert は環境光が無いので、`strength` が 1 だと影が真っ黒になる。

```cpp
// ✗ だめな例
light->intensity = 0.0f; // ✗ 影だけ消したいのに、光も消える（shadow.castsShadows を外す）
```
````

### M-2 `Docs/Reference/19-NotYet.md`

表に1行足す。

```markdown
| ポイントライト・スポットライトの影、半透明な物の影、やわらかい影 | 平行光源の、縁のくっきりした影だけ（9章）。やわらかい影・遠くまでの影は段階を分けて足す |
```

### M-3 `Docs/Reference.md`

索引の「ライト」の行を差し替える。

```markdown
| ライト・**影（平行光源の影・物が影を落とすか）** | [09 ライト](Reference/09-Light.md) |
```


## パートN 確かめ方

### N-1 写したらすぐ見る所

1. Sandbox を Debug でビルドして起動し、Project → `scenes` → `ShadowTest` を開く。
2. Scene ビュー・Game ビューの両方で、柱・浮いた箱・球・箱・歩く人の影が床に落ちている。
3. 半透明の赤い箱・`Cast Shadows Off Box`（黄色）は影を落としていない（黄色の箱の、光の反対側の面は暗い。自分の面の陰なので正しい）。
4. 平行光源の Inspector の `Shadow`：
   - `Cast Shadows` を外す → 影が全部消える。
   - `Map Size` を `Size1024`・`Size4096` にする → 影の縁の細かさが変わる。止まらない。
   - `Distance` を 15 くらいにする → カメラから遠い影が薄くなって消える。
   - `Strength` を 0 にする → 影が消える。1 にする → Lambert の浮いた箱の影の所が真っ黒になる。
   - `Bias` を 0 にする → 床・箱の面に縞（シャドウアクネ）が出る。1 くらいにすると、影が物から浮く。
5. 平行光源を回す（Transform の Rotation）→ 影の向きが変わる。
6. Play → 歩く人の影も歩く。
7. Scene ビューのカメラを動かす → 影の縁がちらつかない（少しずつ動かすと、影の縁のピクセルが同じ所に残る）。
8. Profiler の GPU に `Shadow Map`・`Depth Prepass`・`Shadow Mask` が出る。

### N-2 前と変わっていないか

- `SampleScene`・`Player` を開く。影が増えた以外は前と同じ見た目（色・デカール・空・粒・半透明・ギズモの線）。
- デカールの `Receive Decals` を外した物に、デカールが塗られない（ステンシルの印は色を塗るパスで書いている）。
- Release でビルドして exe を起動する（エディタなしのウィンドウでも影が出る）。

### N-3 写しで確かめていない所

- 本物のマウスで Inspector の値を動かすこと（写しでは、シーンのファイルの値を書き換えて起動し直して見た）。
- AL4_01・TD2_2 のシーンでの見た目（既定で影が出るので、エンジンを更新したら見て、`Strength`・`Bias` を合わせる）。
