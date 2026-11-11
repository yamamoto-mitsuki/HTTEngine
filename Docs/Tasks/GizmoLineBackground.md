参考：
- Unity の Camera の Background Type（Skybox・Solid Color・Uninitialized）：https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@17.0/manual/camera-component-reference.html
- Unity の Scene ビューの Effects メニュー（Skybox・Fog などを Scene ビューだけで切る）：https://docs.unity3d.com/Manual/ViewModes.html
- Unity のギズモはポストエフェクトの後に描く（Gizmos・Handles。トーンマップ・ブルームを受けない）：https://docs.unity3d.com/ScriptReference/Gizmos.html
- D3D12 の深度を SRV で読む（D24_UNORM_S8_UINT の深度は R24_UNORM_X8_TYPELESS で読む）：https://learn.microsoft.com/ja-jp/windows/win32/direct3d12/subresources
- 前の md：なし（デカールが深度を読むやり方は `Shader/Data/Decal/Decal.hlsli.shader` の `GetDecalSurface`）
- この作業のきっかけ：明るい天球（HDR）の前で、ギズモの線が見づらい

# ギズモの線をポストエフェクトの後に描く・カメラの背景（Skybox / Solid Color）

2つの作業をまとめて出す。どちらも「天球の前でギズモが見づらい」から出た物だが、別々の機能なので、片方だけ写してもビルドできる。

- **パートA ギズモの線をポストエフェクトの後に描く**（`DrawCategory`・`PSOManager`・線のシェーダー・`RenderQueue`・`RenderContext`・`SceneRenderer`・線を描く 11 か所）：ギズモの線を、トーンマップ・ブルーム・色の調整の**後**に、選んだ色のまま描く。物の後ろかどうかは、3D の絵の深度をシェーダーで読んで比べる
- **パートB カメラの背景**（`SceneBackground.h`（新しい）・`Skybox`・`SceneRenderer`・`CameraComponent`・`CameraSystem`・`EditorViewport`・`ImGuiManager`）：Camera Component に **Background**（Skybox・Solid Color）を足す。Scene ビューは View メニューの **Skybox** で、天球と灰色の1色を切り替える
- **パートC Sandbox で確かめる**：テストシーン **GizmoBackgroundTest**
- **パートD `Docs/Reference/`**：08-Rendering（線）・10-Camera（Background）

パートは **A → B → C → D の順に写す**。A と B はどちらを先にしてもビルドできる（C は両方を使う）。

ブランチは `develop` から `feature/GizmoLineBackground` を切る（2026-11-10 の時点で、手元は `feature/GuideSampleCamera` で Player.scene.json が未コミット。それを片付けてから切る）。

**2026-11-11 に本体へ直接入れた**（ユーザーが写し始めた A-1〜A-8・A-13 の続きから。ブランチは `feature/GuideSampleCamera` のまま・未コミット）。3構成（Debug・Development・Release）のビルドでエラー 0。Debug の Sandbox を GizmoBackgroundTest で起動し、落ちない・ログにエラーが無い・Scene ビューでライトの範囲（オレンジ）とカメラの線（黄）がトーンマップを受けずに出ることを見た。C-2 の残り（Game ビューの紫・View → Skybox・Inspector の Background・霧・Release の窓）はまだ見ていない。

## 0章 全体の形

### 0.1 何が困っていたか

ギズモの線（Collider・Light の範囲・カメラの線など）は、今は `SceneRenderer::RenderTransparent` の中で、**HDR の 3D の絵（`postRT_`）に**描いている。そのあとに露出・トーンマップ・ブルーム・色の調整・ビネットが掛かるので、線の色がそのまま画面に出ない。

- ライトの範囲の線はオレンジ `{1, 0.647, 0, 1}` だが、ACES を通ると 1 が 0.8 くらいに下がり、色も濃くなる（Reinhard なら 0.5）
- 空は HDR なので、トーンマップの後は明るく淡い色になる。そこに暗く・濁った線が重なるので、境目が見えにくい
- 彩度（Color Adjust）・ビネット・ブルームも線に掛かる

**天球を消して既定をクリアカラーにしても直らない**（線がトーンマップで暗くなるのは同じ。背景の色によってはまた見づらい）。なので、2つに分ける。

1. ギズモの線は、ポストエフェクトの後に描く（パートA。Unity の Scene ビューのギズモと同じ）
2. 背景は、カメラごとに天球か1色かを選べるようにする（パートB。Unity の Camera の Background Type と同じ。**既定は天球のまま**）

### 0.2 なぜこの形にしたか

**A：線の深度はシェーダーで比べる（深度バッファを結ばない）**

- ポストエフェクトの後の描画先（Scene・Game ビューの `RenderTexture`・ウィンドウ）は、3D の絵（`postRT_`）と**大きさが違う**（`postRT_` は長い辺 1920、ビューはゲームの画面の大きさ）。D3D12 は、大きさの違う色と深度を一緒に結べない（`RenderTexture::BindWithDepthOf` の assert も同じ理由）
- そこで、`postRT_` の深度を SRV（`gTextures[]`）で読み、線のピクセルの奥行きと比べて、物の後ろなら捨てる。デカールが深度を読むやり方（`GetDecalSurface`）と同じ
- 画面の位置は、`SV_POSITION` ではなく**線のワールドの位置を `viewProj` で画面へ戻して**決める。描画先の大きさを知らなくても、深度の絵のどのピクセルかが分かる（黒帯のあるウィンドウでも同じ）
- 比べるのは、カメラからの**距離**（深度の値そのままではない）。深度の値は奥ほど詰まっているので、そのまま比べると、面に沿った線（Collider の箱など）がちらつく。距離なら「0.2% までの差は手前とみなす」と決められる

**A：線を「ギズモ」と決めるのは、描く側の `isGizmo`（層ではない）**

- 層（`DrawLayer::EditorGizmo`）は「どのビューに描くか」。Collider・Hit Shape・骨のギズモは `Default` の層で、Game ビューにも出る（View メニューで出す・消す）
- 「どう描くか（ポストエフェクトの前か後か）」は別の話なので、`LineListConfig::isGizmo` を足す。既定は false（ゲームが `Renderer::DrawLines` で描く線は、今まで通り HDR に描き、ブルームで光る）

**A：新しい描く形 `DrawCategory::GizmoLine`**

- PSO の描画先の形式は、描く形（`DrawCategory`）で決まる（`PSOManager` の `GetTargetFormat`。3D は HDR、2D は SDR）。ギズモの線はトーンマップの後の絵（SDR）に描くので、形を分ける
- 頂点シェーダーは今の `Line3dVS` を使い回す。ピクセルシェーダーだけ新しい（`GizmoLine.PS.shader`）

**B：背景の1色も、天球のシェーダーで塗る（クリアの色を変えない）**

- `postRT_` は作るときに `RenderWindow::kClearColor` をクリアの色として渡している。違う色でクリアすると、デバッグレイヤーが毎フレーム警告を出す（作ったときの色と違う、という性能の警告）
- `kClearColor` は IBL のベイク・RenderTexture の作成など、ほかでも使っている共有の値なので変えない（AGENTS.md の決まり）
- そこで、天球（`Skybox::Draw`）が「天球の絵」か「1色」かを選んで塗る。天球と同じ所（物が描かれていない所）だけが塗られる

**B：背景はメインカメラの値を、SceneRenderer に毎フレーム渡す**

- カメラごとのポストエフェクト（`SceneRenderer::SetCameraPostOverrides`）と同じ形。`CameraSystem` が、今使っているカメラの Camera Component から作って渡す
- Scene ビューはデバッグカメラなので、カメラの値は使わず、View メニューの Skybox で決める（`SceneViewOptions::background` で上書き）

### 0.3 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Graphics/Pipeline/RenderStates.h` | `DrawCategory::GizmoLine` を足す |
| A | `Graphics/Pipeline/PSOManager.cpp` | GizmoLine の描画先は SDR・線で描く |
| A | `Graphics/Pipeline/ShaderConstants.h` | `MaterialLineData` の pad を `depthIndex`・`testsDepth` に |
| A | `Shader/Data/Line/Line3d.hlsli.shader` | 線のマテリアル `LineMaterial` と `CalcLineFade` を、2つの PS で共通に |
| A | `Shader/Data/Line/Line3d.PS.shader` | 共通の物を使う形に |
| A | `Shader/Data/Line/GizmoLine.PS.shader`（新しい） | 深度を読んで、物の後ろなら捨てる |
| A | `Graphics/Renderer/DrawRequest.h` | `LineRequest::isGizmo` |
| A | `Graphics/Renderer/Renderer.h`・`.cpp` | `LineListConfig::isGizmo`、リクエストへ写す |
| A | `Graphics/Renderer/RenderContext.h`・`.cpp` | `DrawLines` がシェーダーとマテリアルを受け取る |
| A | `Graphics/Renderer/RenderQueue.h`・`.cpp` | ギズモの線は別の置き場・`FlushGizmoLine` |
| A | `Graphics/Renderer/SceneRenderer.h`・`.cpp` | カメラの定数を先に作る・`RenderGizmoLines`（合成の後・UI の前） |
| A | 線を描く 11 か所 | `isGizmo = true` |
| A | `HTTEngine.vcxproj`・`.filters` | `GizmoLine.PS.shader` |
| B | `Graphics/Renderer/SceneBackground.h`（新しい） | `BackgroundType`・`SceneBackground` |
| B | `Graphics/Pipeline/ShaderConstants.h` | `SkyboxData` に1色を足す |
| B | `Shader/Data/Skybox/Skybox.PS.shader` | 1色のときは、その色を返す |
| B | `Scene/Skybox.h`・`.cpp` | `Draw` が背景を受け取る |
| B | `Graphics/Renderer/SceneRenderer.h`・`.cpp` | `SceneViewOptions::background`・`SetCameraBackground`・`GetViewBackground` |
| B | `Components/Camera/CameraComponent.h` | `backgroundType`・`backgroundColor` |
| B | `Systems/Camera/CameraSystem.cpp` | Inspector の Background・背景を渡す |
| B | `Editor/Viewport/EditorViewport.h`・`.cpp` | `ViewFlags::sceneSkybox`・Scene ビューの灰色 |
| B | `Editor/ImGuiManager.cpp` | View メニューの Skybox |
| B | `HTTEngine.vcxproj`・`.filters` | `SceneBackground.h` |
| C | `Sandbox/Resources/scenes/GizmoBackgroundTest.scene.json`（新しい） | テストシーン |
| D | `Docs/Reference/08-Rendering.md`・`10-Camera.md` | 線の `isGizmo`・カメラの Background |

### 0.4 使い方

**A**：何もしなくてよい。エンジンのギズモ（Collider・Hit Shape・Character Controller・Wind Zone・骨・デカールの箱・ライト・カメラの線・カメラの視錐台・弾む面のデバッグの線）は、全部ポストエフェクトの後に描かれる。

- 線の色は、選んだ色のまま出る（View メニューの Post Effects を切り替えても、線の色が変わらない）
- 物の後ろの所は描かない（今まで通り）。骨・弾む面のように `DepthMode::Disable` の線は、物の後ろでも見える（今まで通り）
- 被写界深度でぼけない・ブルームで光らない・霧が掛からない（霧はもともと線に掛かっていない）
- ゲームが `Renderer::DrawLines` で描く線は、今まで通り HDR に描く（ブルームで光る）。ゲームのデバッグの線をギズモと同じにしたいときは `config.isGizmo = true`

**B**：Camera Component の **Background** の区画。

| 項目 | 中身 |
|---|---|
| Type | `Skybox`（既定。天球）／`SolidColor`（下の Color の1色で塗る） |
| Color | Solid Color のときの色（sRGB で選ぶ。既定は Unity の新しいカメラと同じ青） |

- 塗るのは、物が描かれていない所だけ（天球と同じ）
- Solid Color には霧が掛からない（Unity と同じ。霧の遠くと背景をそろえたいときは、Color を霧の色にする）
- 環境光（IBL）は天球のまま（Unity の Environment Lighting と同じで、背景とは別）
- カメラが切り替わる補間の途中は、切り替わった先のカメラの背景（背景の色は補間しない）
- Camera Component が無いときは天球

**Scene ビュー**：View メニューの Scene View の区画に **Skybox** が増える。外すと、Scene ビューの背景が灰色の1色になる（Game ビューはカメラの Background のまま）。Scene ビューはカメラの Background を見ない（Unity の Scene ビューと同じ）。

### 0.5 1フレームの描く順（パートA の後）

```
RenderToTexture / RenderToWindow
├ ApplyViewOptions          ビューの設定（霧・層・背景）
├ UploadCameraCB            カメラの定数（前は RenderWorldForPost の中。ギズモでも使うので外へ出した）
├ RenderWorldForPost        postRT_（HDR）へ
│  ├ RenderOpaque           不透明 → デカール → 天球（パートB で背景を選ぶ）→ 被写界深度の前の粒
│  ├（被写界深度）
│  └ RenderTransparent      半透明 → 線（ギズモ以外）→ 粒
├ PostProcess::Prepare      ブルーム
├ 描画先を張る
├ PostProcess::Composite    露出・トーンマップ・色の調整・レンズ → 描画先（SDR）
├ RenderGizmoLines          ★ギズモの線（postRT_ の深度を読んで比べる）
└ RenderUI                  2D
```

---

## パートA ギズモの線をポストエフェクトの後に描く

### A-1 `Graphics/Pipeline/RenderStates.h`：描く形を足す

`enum class DrawCategory` の最後に足す（番号はシェーダーを探すキーに使うので、途中に入れない）。

```cpp
enum class DrawCategory {
	Sprite, // Spriteは深度情報の関係で必ず最後に描画すること
	Model,
	Particle,
	Line,
	Text,
	VectorShape, // 画像を使わないUIの図形
	Polyline,    // UIの折れ線（メッシュで形を作り、縁はシェーダーでなめらかにする）
	Decal,       // 箱の中にある物（深度に描かれた不透明な物）に塗る。不透明な物の後に描く
	GizmoLine,   // ギズモの線。ポストエフェクトの後の絵（SDR）に描く。物の後ろかは、3D の絵の深度をシェーダーで読んで比べる
};
```

### A-2 `Graphics/Pipeline/PSOManager.cpp`：描画先の形式とトポロジ

`GetTargetFormat` に GizmoLine を足す（2D と同じ SDR）。

```cpp
RenderTextureFormat GetTargetFormat(DrawCategory category) {
	switch (category) {
	case DrawCategory::Model:
	case DrawCategory::Particle:
	case DrawCategory::Line:
	case DrawCategory::Decal:
		return RenderTextureFormat::HDR;
	case DrawCategory::Sprite:
	case DrawCategory::Text:
	case DrawCategory::VectorShape:
	case DrawCategory::Polyline:
	case DrawCategory::GizmoLine: // ギズモの線もトーンマップの後に描く（選んだ色のまま出す）
		return RenderTextureFormat::SDR;
	}
	return RenderTextureFormat::SDR;
}
```

`GetTopologyID` も、GizmoLine を線にする。

```cpp
TopologyID PSOManager::GetTopologyID(DrawCategory category) {
	switch (category) {
	case DrawCategory::Line:      // 線
	case DrawCategory::GizmoLine: // ギズモの線
		return TopologyID::Line;
	default:
		return TopologyID::Triangle;
	}
	return TopologyID::Triangle;
}
```

### A-3 `Graphics/Pipeline/ShaderConstants.h`：線のマテリアル

`MaterialLineData` の pad を、ギズモの線が使う2つに置き換える（大きさは 32 バイトのまま）。

```cpp
struct MaterialLineData {
	Vector3 cameraWorldPos;  // カメラのワールド座標
	float fadeStartDistance; // フェード開始距離
	float fadeEndDistance;   // フェード終了距離(この距離でalpha=0)
	uint32_t depthIndex = 0; // 3D の絵の深度（gTextures[] の番号。ギズモの線だけが読む。RenderQueue::FlushGizmoLine が入れる）
	uint32_t testsDepth = 1; // 1：物の後ろは描かない 0：いつも描く（ギズモの線だけが読む。DepthMode::Disable の線が 0）
	float pad0 = 0.0f;
};
```

### A-4 `Shader/Data/Line/Line3d.hlsli.shader`：2つの PS で共通の物

マテリアルの並びと、距離で薄くする式を、ここへ移す（Line3d.PS と GizmoLine.PS が同じ物を使う）。

```hlsl
#META
stage: Include
#META_END

#HLSL
struct VertexShaderOutput {
    float32_t4 position : SV_POSITION;
    float32_t3 worldPos : POSITION0;
    float32_t4 color    : COLOR0;
};

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

// 線のマテリアル（C++ の MaterialLineData と同じ並び。Line3d.PS と GizmoLine.PS が b20 で受ける）
struct LineMaterial {
    float32_t3 cameraWorldPos;    // 距離を測るカメラの位置
    float32_t  fadeStartDistance; // この距離から薄くなる
    float32_t  fadeEndDistance;   // この距離で見えなくなる
    uint32_t   depthIndex;        // 3D の絵の深度（gTextures[] の番号。GizmoLine.PS だけが読む）
    uint32_t   testsDepth;        // 1：物の後ろは描かない 0：いつも描く（GizmoLine.PS だけが読む）
    float32_t  pad0;
};

// カメラからの距離で薄くする割合（1：そのまま 0：見えない）
float32_t CalcLineFade(LineMaterial material, float32_t3 worldPos) {
    float32_t dist = length(worldPos - material.cameraWorldPos);
    return saturate(1.0f - (dist - material.fadeStartDistance) / max(material.fadeEndDistance - material.fadeStartDistance, 0.0001f));
}
#HLSL_END
```

### A-5 `Shader/Data/Line/Line3d.PS.shader`：共通の物を使う

中身は今までと同じ（式を hlsli へ移しただけ）。

```hlsl
#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Line
shading:  Unlit
vs:       Line3dVS
#PROGRAM_END

#HLSL
#include "Line3d.hlsli"

ConstantBuffer<LineMaterial> gMaterial : register(b20);


PixelShaderOutput main(VertexShaderOutput input) {
    PixelShaderOutput output;
    output.color = input.color;
    output.color.a *= CalcLineFade(gMaterial, input.worldPos);
    if (output.color.a == 0.0f) { discard; }
    return output;
}
#HLSL_END
```

### A-6 `Shader/Data/Line/GizmoLine.PS.shader`（新しい）

```hlsl
#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: GizmoLine
shading:  Unlit
vs:       Line3dVS
depth:    Disable
#PROGRAM_END

#HLSL
// ギズモの線：ポストエフェクトの後の絵（SDR）に、選んだ色のまま描く
// 描画先は 3D の絵と大きさが違うので、深度は結ばない。3D の絵の深度を gTextures[] で読み、ここで前後を比べる
#include "Line3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Resources/Texture.hlsli"

ConstantBuffer<LineMaterial> gMaterial : register(b20);

// 物の面とこの割合（0.2%）までしか離れていない線は、手前とみなす
// 深度の絵は 3D の絵の大きさで、描画先と1ピクセルずつは合わない。面に沿った線（Collider の箱など）がちらつかないように
static const float32_t kDepthTolerance = 0.002f;

// この位置が、3D の絵に描かれた物の後ろか
bool IsBehindSurface(float32_t3 worldPos) {
    // --- 線の位置 → 画面の位置（-1〜+1）→ 深度の絵の UV（0〜1。y は下が +）---
    // SV_POSITION ではなくワールドの位置から戻すので、描画先の大きさ・黒帯があっても、深度の絵の同じ所を指す
    float32_t4 clip = mul(float32_t4(worldPos, 1.0f), gCamera.viewProj);
    float32_t2 ndc = clip.xy / clip.w;
    float32_t2 uv = float32_t2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    // --- そのピクセルの深度（一番手前の不透明な物。1 なら何も無い＝空）---
    uint32_t width, height;
    gTextures[gMaterial.depthIndex].GetDimensions(width, height);
    int32_t2 pixel = clamp(int32_t2(uv * float32_t2(width, height)), int32_t2(0, 0), int32_t2(width - 1, height - 1));
    float32_t depth = gTextures[gMaterial.depthIndex].Load(int32_t3(pixel, 0)).r;
    if (depth >= 1.0f) {
        return false;
    }
    // --- 深度 → 物のワールドの位置（デカールの GetDecalSurface と同じ）---
    float32_t4 surface = mul(float32_t4(ndc, depth, 1.0f), gCamera.inverseViewProj);
    float32_t3 surfaceWorld = surface.xyz / surface.w;
    // --- カメラからの距離で比べる（深度の値は奥ほど詰まっているので、割合で許すには距離の方が向く）---
    float32_t lineDistance = length(worldPos - gCamera.worldPosition);
    float32_t surfaceDistance = length(surfaceWorld - gCamera.worldPosition);
    return lineDistance > surfaceDistance * (1.0f + kDepthTolerance);
}


PixelShaderOutput main(VertexShaderOutput input) {
    if (gMaterial.testsDepth != 0 && IsBehindSurface(input.worldPos)) {
        discard;
    }
    PixelShaderOutput output;
    output.color = input.color;
    output.color.a *= CalcLineFade(gMaterial, input.worldPos);
    if (output.color.a == 0.0f) { discard; }
    return output;
}
#HLSL_END
```

- 色は C++（`Renderer::DrawLines`）でリニアに直してある。描画先は `_SRGB` の形なので、GPU が書くときに sRGB へ戻し、**Inspector で選んだ色がそのまま出る**

### A-7 `Graphics/Renderer/DrawRequest.h`：リクエスト

```cpp
// ===== Line =====
struct LineRequest : DrawTarget {
	// --- ジオメトリ ---
	std::vector<VertexLineData> vertices;

	// --- Shaderとのバインド情報 ---
	ObjectTransformData objectTransformData;
	MaterialLineData materialData;

	// --- 描画設定 ---
	DepthMode depthMode = DepthMode::TestWrite;
	bool isGizmo = false; // ギズモの線（ポストエフェクトの後に描く。RenderQueue::FlushGizmoLine）
};
```

### A-8 `Graphics/Renderer/Renderer.h`・`.cpp`：`LineListConfig::isGizmo`

`Renderer.h` の `LineListConfig` に足す。

```cpp
	struct LineListConfig : DrawTarget {
		std::vector<LineSegment> lines;  // 座標（ワールド座標）
		float fadeStartDistance = 30.0f; // フェード開始距離
		float fadeEndDistance = 50.0f;   // フェード終了距離(この距離でalpha=0)
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};     // 色
		Camera* camera = nullptr;        // カメラ設定
		DepthMode depthMode = DepthMode::TestWrite; // Disable にすると、物の後ろ・中でも見える（骨のギズモなど）
		// true：ギズモの線。トーンマップ・ブルーム・色の調整の後に、選んだ色のまま描く（ブルームで光らない・被写界深度でぼけない）
		// false：ゲームの線。3D の絵（HDR）に描き、ポストエフェクトを受ける
		bool isGizmo = false;
	};
```

`Renderer.cpp` の `Renderer::DrawLines` で、リクエストへ写す（`req.depthMode = config.depthMode;` の次）。

```cpp
	req.depthMode = config.depthMode;
	req.isGizmo = config.isGizmo;
```

### A-9 `Graphics/Renderer/RenderContext.h`・`.cpp`：`DrawLines` の引数

線とギズモの線でシェーダー（RootSignature）とマテリアルが違うので、呼ぶ側から受け取る。

`RenderContext.h`：

```cpp
	/// <summary>
	/// 3Dライン群を描画する（LINELIST。奇数個の頂点は最後を切り捨て）
	/// <para>programId：線のシェーダー（Line か GizmoLine）。material：送るマテリアル（ギズモの線は深度の番号を入れた写し）</para>
	/// </summary>
	static void DrawLines(const LineRequest& req, uint32_t programId, const MaterialLineData& material);
```

`RenderContext.cpp` の `DrawLines`：頭の4行を直す。

```cpp
void RenderContext::DrawLines(const LineRequest& req, uint32_t programId, const MaterialLineData& material) {
	// 奇数は切り捨て。描く線が無ければ何もしない
	size_t vertexCount = req.vertices.size() & ~size_t(1);
	if (vertexCount == 0) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(programId);

	// ===== このフレーム用のバッファに書く =====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(material);
	D3D12_GPU_VIRTUAL_ADDRESS transformAddress = upload.PushConstant(req.objectTransformData);
	// （ここから下は今のまま）
```

### A-10 `Graphics/Renderer/RenderQueue.h`・`.cpp`：ギズモの線は別に描く

`RenderQueue.h`：`FlushLine` の下に足す。

```cpp
	static void FlushLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	// ギズモの線（LineRequest::isGizmo）。ポストエフェクトの後の絵に描く。depthIndex：3D の絵の深度（gTextures[] の番号。物の後ろかを比べる）
	static void FlushGizmoLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex);
```

メンバーに置き場を足す。

```cpp
	std::vector<LineRequest> lineRequests_;
	std::vector<LineRequest> gizmoLineRequests_; // ギズモの線（ポストエフェクトの後に描く）
```

`RenderQueue.cpp`：

`Clear` に足す。

```cpp
	instance_->lineRequests_.clear();
	instance_->gizmoLineRequests_.clear();
```

`Request(LineRequest&&)` を、置き場を分ける形に。

```cpp
// ===== 線のリクエスト =====
// ギズモの線は、ポストエフェクトの後に別に描く（FlushGizmoLine）
void RenderQueue::Request(LineRequest&& req) {
	std::vector<LineRequest>& requests = req.isGizmo ? instance_->gizmoLineRequests_ : instance_->lineRequests_;
	requests.push_back(std::move(req));
}
```

`FlushLine`：`RenderContext::DrawLines(req);` を直す。シェーダーの番号はループの前に1回だけ取る。

```cpp
void RenderQueue::FlushLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// SRVヒープセット
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	const uint32_t program = PSOManager::GetShaderProgramID(DrawCategory::Line); // 線のシェーダー
	// （中略。今のまま。RootSignature を取る所は program を使う）
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(program);
	// （中略）
		// Draw Callする関数へ
		RenderContext::DrawLines(req, program, req.materialData);
	}
```

`FlushLine` の下に `FlushGizmoLine` を足す。

```cpp
//=============================================================================
// ギズモの線の描画を発行
//=============================================================================
// ポストエフェクトの後の絵（SDR）に描く。深度は結ばず（描画先と 3D の絵は大きさが違う）、シェーダーが depthIndex の深度を読んで比べる
void RenderQueue::FlushGizmoLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex) {
	auto* cmdList = DirectXCommon::GetCommandList();
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	const uint32_t program = PSOManager::GetShaderProgramID(DrawCategory::GizmoLine);
	// 深度はシェーダーで比べるので、PSO はいつも Disable（線の DepthMode は、testsDepth にして送る）
	const PSOKey psoKey = PSOManager::GetPSOKey(DrawCategory::GizmoLine, ShadingType::Unlit, BlendMode::Normal, RasterizerType::SolidBack, DepthMode::Disable);
	bool isStateSet = false;          // 最初の1本でだけ、RootSignature・PSO を結ぶ
	uint32_t scopeIndex = UINT32_MAX; // GPUProfiler用

	for (const LineRequest& req : instance_->gizmoLineRequests_) {
		// このビューで描かない物は飛ばす（別のウィンドウ・切ってある層・目で隠した物）
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// --- 最初のみ RootSignature, PipelineState を結ぶ ---
		if (!isStateSet) {
			scopeIndex = GPUProfiler::Begin(cmdList, PSOManager::GetStateName(psoKey));
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(program);
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			// カメラ（VS の位置・PS の深度から戻す行列）
			if (auto camSlot = rs.slotOf.find(RootBind::Camera); cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			// テクスチャ（深度を gTextures[] で読む）
			if (auto texSlot = rs.slotOf.find(RootBind::BindlessTexture); texSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
			}
			cmdList->SetPipelineState(PSOManager::GetPSO(psoKey).Get());
			isStateSet = true;
		}
		// --- 深度の番号と、比べるかどうかを入れた写しを送る ---
		MaterialLineData material = req.materialData;
		material.depthIndex = depthIndex;
		material.testsDepth = (req.depthMode != DepthMode::Disable) ? 1u : 0u;
		RenderContext::DrawLines(req, program, material);
	}
	// GPUProfiler: 計測終了
	if (scopeIndex != UINT32_MAX) {
		GPUProfiler::End(cmdList, scopeIndex);
	}
}
```

### A-11 `Graphics/Renderer/SceneRenderer.h`・`.cpp`：合成の後に描く

カメラの定数（`UploadCameraCB`）を、`RenderWorldForPost` の中から呼ぶ側へ出す（ギズモの線でも同じ物を使う）。

`SceneRenderer.h` の private：

```cpp
	static RenderTexture* RenderWorldForPost(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle);
	static void RenderOpaque(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, RenderTexture& work);
	static void RenderTransparent(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle);
	// ギズモの線（合成の後・UI の前。3D の絵の深度を読んで前後を比べる）
	static void RenderGizmoLines(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, const ViewRect& rect);
	static void RenderUI(const std::wstring& windowTitle, const ViewRect& rect);
```

`SceneRenderer.cpp`：

`RenderWorldForPost`：引数で受け取り、中の `UploadCameraCB` の行を消す。

```cpp
RenderTexture* SceneRenderer::RenderWorldForPost(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle) {
	RenderTexture* work = instance_->postRT_.get();
	const ViewRect workRect = {0.0f, 0.0f, static_cast<float>(work->GetWidth()), static_cast<float>(work->GetHeight())};
	const DepthOfFieldSettings depthOfField = GetViewDepthOfField();
	// （ここから下は今のまま）
```

`RenderTransparent` の下（`RenderUI` の上）に足す。

```cpp
// ===== ギズモの線。トーンマップの後に重ねるので、選んだ色のまま出る（ブルームで光らない・被写界深度でぼけない）=====
// 描画先は 3D の絵（postRT_）と大きさが違うので、深度は結べない。postRT_ の深度を SRV で読める状態にして、シェーダーで比べる
void SceneRenderer::RenderGizmoLines(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, const ViewRect& rect) {
	if (camCB == 0) {
		return; // カメラが無い（線の位置を画面へ戻せない）
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	SetViewportAndScissor(rect);
	GPU_SCOPE(cmdList, "Gizmo Line");
	// ここでの postRT_ は、色が PS で読む状態・深度が書く状態（被写界深度を掛けたときも、DepthOfFieldPass が戻している）
	RenderTexture* scene = instance_->postRT_.get();
	scene->BeginShaderRead();
	RenderQueue::FlushGizmoLine(windowTitle, camCB, scene->GetDepthSRVSlot());
	scene->EndShaderRead();
}
```

`RenderToTexture`：カメラの定数を先に作り、合成の後にギズモの線を描く。

```cpp
void SceneRenderer::RenderToTexture(const Camera* camera, RenderTexture* target, const std::wstring& windowTitle, PostEffectMode mode, const SceneViewOptions& options) {
	HTT_ASSERT_MSG(instance_->postProcess_ != nullptr, "InitializePost を先に呼んでください（3D は HDR の絵に描いてから画面へ写すので、その置き場が要る）");
	// ポストエフェクトを掛けないビューは、被写界深度も掛けない（ブルーム・レンズは下で外す）
	SceneViewOptions viewOptions = options;
	if (mode == PostEffectMode::None) {
		viewOptions.depthOfField = false;
	}
	ApplyViewOptions(viewOptions);
	const ViewRect rect = {0.0f, 0.0f, static_cast<float>(target->GetWidth()), static_cast<float>(target->GetHeight())};
	// カメラの定数（霧はビューの設定で決まるので、ApplyViewOptions の後。3D とギズモの線の両方が使う）
	const D3D12_GPU_VIRTUAL_ADDRESS camCB = camera ? UploadCameraCB(camera) : 0;
	// 一旦 HDR の絵へ描き、光の層を作ってから target へ合成する（露出・トーンマップで 0〜1 に収める）
	RenderTexture* scene = RenderWorldForPost(camera, camCB, windowTitle);
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides(), mode); // 張り替える処理はここで全部済ませる
	target->PreDraw();                                                      // その後に描画先を張る
	instance_->postProcess_->Composite(rect, mode);
	RenderGizmoLines(camCB, windowTitle, rect); // ギズモの線は効果の外側（UI の下）
	RenderUI(windowTitle, rect);                // UIは効果の外側に重ねる
	target->PostDraw();
}
```

`RenderToWindow` も同じ。

```cpp
void SceneRenderer::RenderToWindow(const Camera* camera, RenderWindow* window, const std::wstring& windowTitle, float windowWidth, float windowHeight) {
	HTT_ASSERT_MSG(instance_->postProcess_ != nullptr, "InitializePost を先に呼んでください（3D は HDR の絵に描いてから画面へ写すので、その置き場が要る）");
	ApplyViewOptions({}); // ウィンドウ（製品版のゲーム）は全部掛ける
	const D3D12_GPU_VIRTUAL_ADDRESS camCB = camera ? UploadCameraCB(camera) : 0; // 3D とギズモの線の両方が使う
	RenderTexture* scene = RenderWorldForPost(camera, camCB, windowTitle);

	// 効果のパスでRTVが切り替わっているので、ウィンドウへ張り直す
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides());
	window->BindRenderTarget(); // 描画先はウィンドウ
	// ウィンドウの中の、ゲームの比率の四角にだけ描く（外は帯の色で塗る）
	const ViewRect rect = GameScreen::CalcFitRect(windowWidth, windowHeight);
	window->ClearBars(rect, windowWidth, windowHeight);
	instance_->postProcess_->Composite(rect);
	RenderGizmoLines(camCB, windowTitle, rect); // ギズモの線（Collider のギズモなどを出したとき）
	RenderUI(windowTitle, rect);                // UIは効果の外側に重ねる
}
```

`RenderWorldForPost` の上のコメントの「半透明・線・パーティクルは〜」はそのままでよい（ゲームの線は今まで通りそこで描く）。

### A-12 線を描く 11 か所：`isGizmo = true`

どれも1行足すだけ。初期化で設定を書いている所は初期化に、毎回作る所は作る所に。

| ファイル | 足す所 | 足す行 |
|---|---|---|
| `Editor/Gizmos/LightGizmo.cpp` | `Initialize` の `rangeLines_.layer = ...` の次 | `rangeLines_.isGizmo = true;` |
| `Editor/Gizmos/CameraGizmo.cpp` | `Initialize` の `lines.layer = ...` の次 | `lines.isGizmo = true;` |
| `Camera/Camera.cpp` | `DrawFrustum` の `config.camera = viewCamera;` の次 | `config.isGizmo = true;` |
| `Systems/Physics/CollisionSystem.cpp` | `Initialize` の `fadeEndDistance` の次 | `state->gizmoLines.isGizmo = true;` |
| `Systems/Combat/HitSystem.cpp` | `Initialize` の `fadeEndDistance` の次 | `state->gizmoLines.isGizmo = true;` |
| `Systems/Animation/AnimationSystem.cpp` | `Initialize` の `depthMode = DepthMode::Disable;` の次 | `state->gizmoLines.isGizmo = true;` |
| `Systems/Physics/CharacterControllerSystem.cpp` | `Initialize` の `fadeEndDistance` の次 | `state->gizmoLines.isGizmo = true;` |
| `Systems/Rendering3D/DecalRenderSystem.cpp` | `Initialize` の `layer = ...` の次 | `state->gizmoLines.isGizmo = true;` |
| `Systems/Physics/WindZoneSystem.cpp` | `DrawGizmos` の `gizmo.camera = camera;` の次 | `gizmo.isGizmo = true;` |
| `Systems/Physics/ElasticSurfaceSystem.cpp` | `DrawDebugLines` の `config.depthMode = DepthMode::Disable;` の次 | `config.isGizmo = true;` |

エディタのグリッドは線ではなくモデルなので、ここには無い（最後の「やらなかったこと」）。

行の右のコメントは、ギズモの所に1つだけ付ければよい（例：`// ポストエフェクトの後に、選んだ色のまま描く`）。

### A-13 `HTTEngine.vcxproj`・`.filters`

`HTTEngine.vcxproj`：`Line3d.VS.shader` の行の次に。

```xml
    <None Include="HTTEngine\Shader\Data\Line\GizmoLine.PS.shader" />
```

`HTTEngine.vcxproj.filters`：`Line3d.VS.shader` の `</None>` の次に。

```xml
    <None Include="HTTEngine\Shader\Data\Line\GizmoLine.PS.shader">
      <Filter>HTTEngine\Shader\Data\Line</Filter>
    </None>
```

---

## パートB カメラの背景（Skybox / Solid Color）

### B-1 `Graphics/Renderer/SceneBackground.h`（新しい）

```cpp
#pragma once
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// 3D の絵の背景（物が描かれていない所）の塗り方（Unity の Camera の Background Type）
/// </summary>
enum class BackgroundType {
	Skybox,     // 天球（SceneRenderer の Skybox の絵）
	SolidColor, // 1色で塗る（霧は掛けない）
};

/// <summary>
/// 背景の塗り方。SceneRenderer が Skybox::Draw に渡す
/// <para>メインカメラの Camera Component から CameraSystem が作る。Scene ビューは EditorViewport が View メニューから作る</para>
/// </summary>
struct SceneBackground {
	BackgroundType type = BackgroundType::Skybox;
	Vector3 color = {0.0f, 0.0f, 0.0f}; // SolidColor のときの色（リニア。1 を超えるとブルームで光る）
};
```

### B-2 `Graphics/Pipeline/ShaderConstants.h`：`SkyboxData`

後ろに1色を足す（HLSL の並びと合わせる。`padA` の後が 16 バイトの区切りなので、`Vector3` がそのまま入る）。

```cpp
struct SkyboxData {
	Matrix4x4 world;
	Matrix4x4 viewProj;
	float intensity = 1.0f;
	int cubeIndex;
	float padA[2];
	Vector3 solidColor = {0.0f, 0.0f, 0.0f}; // 背景が1色のときの色（リニア）
	uint32_t useSolidColor = 0;              // 1：天球の絵の代わりに solidColor で塗る
};
```

### B-3 `Shader/Data/Skybox/Skybox.PS.shader`

```hlsl
struct Skybox
{
    float4x4 world;
    float4x4 viewProj;
    float    intensity;
    uint     cubeIndex;
    float2   pad;
    float3   solidColor;    // 背景が1色のときの色（リニア）
    uint     useSolidColor; // 1：天球の絵の代わりに solidColor で塗る
};
ConstantBuffer<Skybox> gSkybox : register(b20);

struct VSOutput
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

float4 main(VSOutput i) : SV_TARGET
{
    // 1色の背景（Camera の Background が Solid Color・Scene ビューの Skybox を切ったとき）。霧は掛けない（Unity と同じ）
    if (gSkybox.useSolidColor != 0)
    {
        return float4(gSkybox.solidColor, 1.0f);
    }
    float3 direction = normalize(i.dir);
    float3 c = gTexturesCube[gSkybox.cubeIndex].Sample(gSampler, direction).rgb * gSkybox.intensity;
    c = ApplySkyFog(c, direction); // 霧（板の背景の下から空がのぞいても、霧の色・穴の底の暗い色になる）
    return float4(c, 1.0f);
}
```

### B-4 `Scene/Skybox.h`・`.cpp`：`Draw` が背景を受け取る

`Skybox.h`：include と宣言。

```cpp
#include "HTTEngine/Graphics/Renderer/SceneBackground.h"
```

```cpp
	void Initialize();
	// 背景を塗る（background が SolidColor なら、天球の絵の代わりにその色。塗る所は物が描かれていない所だけ）
	void Draw(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, const SceneBackground& background);
```

クラスのコメントも直す。

```cpp
/// <summary>
/// 天球（キューブマップ方式）。.png / .hdrどちらでも。背景が1色のときも、ここで塗る（クリアの色は変えない）
/// </summary>
```

`Skybox.cpp` の `Draw`：

```cpp
void Skybox::Draw(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, const SceneBackground& background) {
	// （中略。今のまま）
	cb->intensity = intensity_;
	cb->cubeIndex = cube_->GetSRVSlot();
	// 背景が1色のとき（天球と同じ所を、その色で塗る。postRT_ のクリアの色は作ったときの色から変えない）
	cb->solidColor = background.color;
	cb->useSolidColor = (background.type == BackgroundType::SolidColor) ? 1u : 0u;
	++slot_;
	// （ここから下は今のまま）
```

### B-5 `Graphics/Renderer/SceneRenderer.h`・`.cpp`：ビューの背景

`SceneRenderer.h`：include（`<optional>` と SceneBackground.h）。

```cpp
#include <optional>
```

```cpp
#include "HTTEngine/Graphics/Renderer/SceneBackground.h"
```

`SceneViewOptions` の最後に足す（`EditorViewport` が指定付きの初期化子 `.名前 =` で書くので、並びの最後）。

```cpp
	bool cameraVolume = true; // カメラごとのポストエフェクト（Camera Volume）を掛ける（Scene ビューだけ false。別のカメラから見るので、ピントが合わない）
	std::optional<SceneBackground> background; // このビューの背景（無ければメインカメラの背景。Scene ビューは View メニューの Skybox で決める）
};
```

public の「カメラごとのポストエフェクト」の下に足す。

```cpp
	// --- 背景 ---
	// メインカメラの背景（CameraSystem が毎フレーム書く。Camera Component が無ければ天球を書く）
	static void SetCameraBackground(const SceneBackground& background) { instance_->cameraBackground_ = background; }
```

private に足す。

```cpp
	// このビューの背景（SceneViewOptions で決めてあればそれ、無ければメインカメラの背景）
	static SceneBackground GetViewBackground();
```

```cpp
	PostOverrides cameraPostOverrides_; // カメラごとのポストエフェクトの上書き（初期値は上書きなし）
	SceneBackground cameraBackground_;  // メインカメラの背景（初期値は天球）
```

`SceneRenderer.cpp`：`GetViewDepthOfField` の下に足す。

```cpp
// ===== このビューの背景 =====
SceneBackground SceneRenderer::GetViewBackground() { return instance_->viewOptions_.background.value_or(instance_->cameraBackground_); }
```

`RenderOpaque` の天球：

```cpp
	if (instance_->skybox_) {
		GPU_SCOPE(cmdList, "Skybox");
		instance_->skybox_->Draw(camera, camCB, GetViewBackground()); // 天球か1色（Camera の Background・Scene ビューの Skybox）
	}
```

### B-6 `Components/Camera/CameraComponent.h`

include を足す。

```cpp
#include "HTTEngine/Graphics/Renderer/SceneBackground.h"
#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Time/EngineTime.h"
```

`priority` の次に足す。

```cpp
	int priority = 0;     // 有効な Camera が複数あるとき、大きい方を使う（同じなら先に見つけた方）
	// --- 背景（物が描かれていない所）---
	BackgroundType backgroundType = BackgroundType::Skybox; // Skybox：天球 / SolidColor：backgroundColor の1色
	Vector3 backgroundColor = {0.192f, 0.302f, 0.475f};     // SolidColor のときの色（sRGB。既定は Unity の新しいカメラと同じ青）
```

### B-7 `Systems/Camera/CameraSystem.cpp`

include を足す。

```cpp
#include "HTTEngine/Math/Color.h"
```

`DescribeCamera` の Priority の次に足す。

```cpp
	ui.Separator("Background");
	ui.Field("Type###backgroundType", value.backgroundType, Tip("物が描かれていない所の塗り方。Skybox：天球 / SolidColor：下の Color の1色（霧は掛からない）"));
	ui.BeginDisabled(value.backgroundType != BackgroundType::SolidColor);
	ui.ColorField("Color###backgroundColor", value.backgroundColor, Tip("SolidColor のときの色。環境光（IBL）は天球のまま"));
	ui.EndDisabled();
```

namespace の中、`UseDefaultCamera` の下に足す。

```cpp
// Camera Component の背景 → 描くときの背景（色は Inspector で選んだ sRGB → シェーダーで塗るリニアへ。Component が無ければ天球）
SceneBackground MakeBackground(const CameraComponent* camera) {
	if (camera == nullptr) {
		return {};
	}
	return {camera->backgroundType, SrgbToLinear(camera->backgroundColor)};
}
```

`CameraSystem::Update`：

「2. 無ければ既定のカメラ」の中、`SceneRenderer::SetCameraPostOverrides({});` の次に。

```cpp
		SceneRenderer::SetCameraPostOverrides({});
		SceneRenderer::SetCameraBackground({}); // Camera が無ければ天球
```

「5.」の `state->mainEntity = ...` の次に。

```cpp
	state->mainEntity = (request.has_value() && request->to.IsValid()) ? request->to : best;
	// 背景は今使っているカメラの物（補間の途中は切り替わった先。色は補間しない）
	SceneRenderer::SetCameraBackground(MakeBackground(EntityManager::Get<CameraComponent>(state->mainEntity)));
```

### B-8 `Editor/Viewport/EditorViewport.h`・`.cpp`：Scene ビューの Skybox

`EditorViewport.h` の `ViewFlags`：`sceneFog` の次に足す。

```cpp
		bool sceneFog = true;          // Scene ビューにも霧（Fog Component）を掛ける。Game ビューにはいつも掛ける
		bool sceneSkybox = true;       // Scene ビューに天球を描く（外すと kSceneViewBackgroundColor の1色。Game ビューはカメラの Background のまま）
```

`EditorViewport.cpp`：include と定数。

```cpp
#include "HTTEngine/Math/Color.h"
```

```cpp
namespace {
constexpr const char* kGVGroup = "Scene View"; // ImGui登録グループ名
constexpr Vector3 kSceneViewBackgroundColor = {0.28f, 0.28f, 0.28f}; // Scene ビューで Skybox を切ったときの背景（sRGB の灰色。線の色が分かりやすい明るさ）
} // namespace
```

`Render` の `sceneOptions`：

```cpp
	// 背景は Skybox を切ったら灰色の1色（Scene ビューはカメラの Background を見ない。Unity の Scene ビューと同じ）
	const SceneBackground sceneBackground = flags.sceneSkybox ? SceneBackground{} : SceneBackground{BackgroundType::SolidColor, SrgbToLinear(kSceneViewBackgroundColor)};
	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .editorGizmo = true, .sceneHidden = false, .cameraVolume = false, .background = sceneBackground};
```

### B-9 `Editor/ImGuiManager.cpp`：View メニュー

`DrawViewMenu` の Scene View の区画、`Fog` の次に足す。

```cpp
		ImGui::MenuItem("Fog", nullptr, &viewFlags.sceneFog);
		ImGui::MenuItem("Skybox", nullptr, &viewFlags.sceneSkybox);
```

### B-10 `HTTEngine.vcxproj`・`.filters`

`HTTEngine.vcxproj`：`SceneRenderer.h` の行の前に。

```xml
    <ClInclude Include="HTTEngine\Graphics\Renderer\SceneBackground.h" />
```

`HTTEngine.vcxproj.filters`：`SceneRenderer.h` の `<ClInclude>` の前に。

```xml
    <ClInclude Include="HTTEngine\Graphics\Renderer\SceneBackground.h">
      <Filter>HTTEngine\Graphics\Renderer</Filter>
    </ClInclude>
```

---

## パートC Sandbox で確かめる

### C-1 `project/Sandbox/Resources/scenes/GizmoBackgroundTest.scene.json`（新しい）

カメラは Solid Color（濃い紫。天球との違いが分かる色）。箱に Collider（ギズモの線）、点のライト（範囲の線）。

```json
{
	"version": 1,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 3.0, -8.0],
					"rotation": [15.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0,
					"backgroundType": "SolidColor",
					"backgroundColor": [0.25, 0.15, 0.35]
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
					"translation": [0.0, 3.0, 0.0],
					"rotation": [50.0, -30.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Directional Light": {
					"color": [1.0, 1.0, 1.0]
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
					"scale": [20.0, 0.1, 20.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.6, 0.6, 0.6, 1.0]
				}
			}
		},
		{
			"id": 4,
			"name": "Cube",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.5, 0.0],
					"rotation": [0.0, 30.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.5, 0.2, 1.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				}
			}
		},
		{
			"id": 5,
			"name": "Tall Box",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [2.5, 1.5, 1.5],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 3.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.3, 0.6, 0.9, 1.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": true,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				}
			}
		},
		{
			"id": 6,
			"name": "Point Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-2.0, 1.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Point Light": {
					"color": [1.0, 0.8, 0.6],
					"intensity": 3.0,
					"radius": 3.0,
					"decay": 1.0
				}
			}
		}
	]
}
```

Project ウィンドウからこのシーンを開く。

### C-2 確かめること

**A ギズモの線**（Scene ビュー。View メニューの Colliders・Light Ranges を入れる）

1. View → Post Effects を入れても切っても、Collider・Trigger（Tall Box）・ライトの範囲（オレンジ）の線の色が変わらない（前は、入れると暗く・濃くなっていた）
2. PostEffect の調整項目でブルームの Threshold を下げても、線が光らない。Exposure を上げても線は明るくならない
3. 箱の後ろにある線（Tall Box の奥の辺・地面の下）が描かれない。箱の面にある線がちらつかない（カメラを近づけ・遠ざけて見る）
4. View → Skeletons（骨のあるモデルを置いたとき）は、物の後ろでも見える（DepthMode::Disable）
5. 被写界深度（Depth Of Field Component を置いて Max Blur を上げる）で、物はぼけても線はぼけない
6. Game ビューでも Colliders を入れると、線が同じように出る（Game ビューは Default 層の線だけ）
7. デバッグレイヤーの警告・エラーが出ない（出力ウィンドウ。状態の切り替え `BeginShaderRead` の抜けがあると止まる）
8. ゲームの線（`config.isGizmo` を付けない `Renderer::DrawLines`）は、今まで通りブルームで光る（Script で1本描いて見る。任意）

**B 背景**

9. Game ビューの背景が紫の1色。地面・箱は今まで通り（環境光は天球のまま）
10. Main Camera の Inspector → Background → Type を Skybox に戻すと、天球になる。Color は Type が SolidColor のときだけ触れる
11. Save → シーンを読み直すと、Type・Color が戻る（前のシーンファイル（`backgroundType` が無い）を開くと Skybox）
12. Scene ビューは、カメラが SolidColor でも天球（View → Skybox を外すと灰色の1色）
13. Fog Component を置くと、物は霧に沈むが、紫の背景は霧の色にならない（Skybox なら空も霧の色になる）
14. Release で起動して（エディタ無し）、ウィンドウの背景も紫

---

## パートD `Docs/Reference/`

### D-1 `Docs/Reference/08-Rendering.md`

`DrawLines` を並べた段落の下に足す。

```markdown
- `Renderer::DrawLines` の線は、3D の絵（HDR）に描くので、ブルーム・トーンマップ・色の調整を受ける。デバッグの線を**選んだ色のまま**見たいときは `config.isGizmo = true`（ポストエフェクトの後に描く。物の後ろは描かない。`depthMode = DepthMode::Disable` なら物の後ろでも見える）。エンジンのギズモ（Collider・Hit Shape など）は全部 `isGizmo`。
```

### D-2 `Docs/Reference/10-Camera.md`

表の Priority の次に足す。

```markdown
| Background（Type・Color） | 物が描かれていない所の塗り方。`Skybox`（天球）／`SolidColor`（Color の1色。霧は掛からない）。環境光（IBL）は天球のまま。Scene ビューは見ない（View → Skybox で天球と灰色を切り替える） |
```

---

## パートE グラデーションの背景（2026-11-11 に足した・本体へ直接）

ユーザーの言う「クリアカラーが複数」は、Unity の空のように上と下で色が変わる背景のことだった（カメラを重ねる Depth Only・Don't Clear ではない）。Background の Type に **Gradient** を足した。

- `SceneBackground.h`：`BackgroundType::Gradient`、`skyColor`・`horizonColor`・`groundColor`・`horizonWidth`（色はリニア）
- `ShaderConstants.h` の `SkyboxData`：`useSolidColor` をやめて `backgroundType`（BackgroundType の番号のまま）にし、Gradient の3色と horizonWidth を足した（16 バイトずつの並び）
- `Skybox.PS.shader`：`backgroundType` を switch で分け、Gradient は `CalcGradientColor`。見る向きの y（真上 +1・真横 0・真下 -1）で、`lerp(Horizon, y>=0 ? Sky : Ground, smoothstep(0, horizonWidth, |y|))`。はじめは pow(|y|, Exponent) だったが、Exponent が 1 より小さいと地平線のすぐそばで急に変わって2色の境目に見えたので、地平線の色が残る幅と、両端がなだらかな smoothstep に変えた
- `CameraComponent.h`：`gradientSkyColor`・`gradientHorizonColor`・`gradientGroundColor`（sRGB）・`gradientHorizonWidth`（既定 0.15。上下に約 9°）
- `CameraSystem.cpp`：Inspector に Sky・Horizon・Ground・Horizon Width（Type が Gradient のときだけ触れる。保存のため、使わないときも並べる）、`MakeBackground` でリニアへ
- Scene ビュー：View → Skybox を外したときは、灰色の1色ではなくエディタのグラデーション（`EditorViewport.cpp` の `kSceneViewSkyColor`・`kSceneViewHorizonColor`・`kSceneViewGroundColor`・`MakeSceneViewBackground`。くすんだ青・明るい灰色・暗い灰色）。`ViewFlags::sceneSkybox` の初めの値を false にして、起動したときからグラデーションにした（Unity の Scene ビューのように、ギズモが見やすい落ち着いた背景）

## やらなかったこと・この先

- **ギズモのアイコン（Light・Camera の絵）とエディタのグリッド**：どちらもモデル（`MeshRequest`）なので、今も HDR に描いてトーンマップを受ける。同じように後に描くには、モデルの PSO にも SDR の形が要る（形ごとに描画先を決めている `GetTargetFormat` を、リクエストごとに選べるようにするなど）。線で困らなくなったら考える
- **物の後ろの線を薄く描く**（Unity の Handles の zTest のように、隠れた所を半透明で見せる）：GizmoLine.PS の `discard` を、薄くする式に変えればできる。今は前と同じく描かない
- **背景の色の補間**：カメラの切り替えの補間で、背景の色は補間しない（切り替わった先の物になる）。要るなら `CameraOutput` に背景を入れて `ApplyBlend` で混ぜる
- **シーンごとの天球の絵**（今は全シーンで既定の skybox.hdr）：SceneFlow の SF4 の話。Background とは別
