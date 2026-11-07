参考：
- Unity の Shader の Properties（値の意味・表示名・既定値・範囲を Shader に書く）：https://docs.unity3d.com/Manual/SL-Properties.html
- Unity の MaterialPropertyBlock（物ごとの上書き。今回は作らない）：https://docs.unity3d.com/ScriptReference/MaterialPropertyBlock.html
- Unity URP の Decal Projector（箱で貼るデカール・Draw Order）：https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/manual/renderer-feature-decal.html
- Unreal のデカール（Receives Decals で受けない物を分ける）：https://dev.epicgames.com/documentation/en-us/unreal-engine/decal-actors-in-unreal-engine
- 深度から位置を戻す（MJP）：https://mynameismjp.wordpress.com/2009/03/10/reconstructing-position-from-depth/
- ID3D12ShaderReflectionConstantBuffer（定数バッファの中身のリフレクション）：https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12shaderreflectionconstantbuffer
- ID3D12ShaderReflectionType（構造体のメンバー）：https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12shaderreflectiontype
- HLSL の定数バッファの並び方（16 バイトの行）：https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
- 読むだけの DSV（D3D12_DSV_FLAG_READ_ONLY_DEPTH）：https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_dsv_flags
- リソースの状態（DEPTH_READ と PIXEL_SHADER_RESOURCE は一緒にできる）：https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_states
- HLSL の fwidth・clip：https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-fwidth ・ https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-clip
- 2D の符号付き距離（Inigo Quilez）：https://iquilezles.org/articles/distfunctions2d/
- `std::from_chars`：https://en.cppreference.com/w/cpp/utility/from_chars
- `std::as_bytes`：https://en.cppreference.com/w/cpp/container/span/as_bytes
- `std::stable_sort`：https://en.cppreference.com/w/cpp/algorithm/stable_sort

# マテリアル（Shader＋値）と、床の予告（デカール）

攻撃の予告（床に描く図形）を、線ではなく**塗り**で描き、Texture も Shader も差し替えられるようにする。予告だけの特別な仕組みにせず、エンジンに**汎用のマテリアル**を足して、予告の描画をその最初の使い道にする。層はこう分ける。

- **Shader の層**（`ShaderPackageLoader`・`ShaderCompiler`）：.shader を読む・名前と場所・定数バッファの中身のリフレクション・#PROPERTIES（値の意味）
- **マテリアルの層**（`Material`・`MaterialManager`）：.material（Shader のパス＋値＋描くときの状態の上書き）・値ごとの Curve・描くときに定数バッファの並びに詰める
- **描く層**（`Renderer::DrawDecal`・`RenderQueue`・`RenderContext`）：デカールの箱を描き、深度から床の位置を戻して塗る。受けない物（Receive Decals を外した物）はステンシルで除く
- **描き手**（`DecalRenderSystem`・`HitboxTelegraphSystem`）：何をどこに描くか。描き手ごとの値（予告の形・進み具合）は、描き手ごとの定数バッファ（gDrawData）で Shader へ送る

パートは **A → B → C → D → E の順に写す**。どのパートの後でもビルドできる（A だけなら今までと同じ絵、B まででマテリアルのファイルを作って直せる、C でデカール、D で予告）。E は Sandbox のテスト。

ブランチは `feature/MaterialTelegraph`（develop から）。

## 0章 全体の形

### 0.1 層と流れ

```
Shader の層        .shader（#META・#PROGRAM・#PROPERTIES・#HLSL）
（Pipeline）         場所から名前を決める（エンジン：UnlitPS、ゲーム：Project/shaders/FloorPS）
                     #PROGRAM：category・vs・shading（書いた物だけ enum で引ける）・blend・rasterizer・depth の既定
                     #PROPERTIES：値の意味（種類・表示名・説明・既定・範囲）
                     リフレクション：定数バッファの中身（変数名・場所・大きさ）
                     └→ ShaderPropertyLayout（#PROPERTIES を gMaterial の中の場所に結んだ物）

マテリアルの層     .material（Shader のパス・値・Curve・状態の上書き）── MaterialManager
（Material）         Describe：Inspector とファイルが同じ並び（欄は #PROPERTIES から作る）
                     Resolve(番号, 進み具合)：Curve で値を動かし、gMaterial の並びにバイトで詰める

描く層             Renderer::DrawDecal（マテリアル・箱・描き手の値）
（Renderer）         → RenderQueue（Order の順）→ 不透明な物の後に、深度を読むだけの窓口で結び直して
                     → RenderContext::DrawDecal（箱を描く。gMaterial・gDecal・gDrawData を結ぶ）
                     → PS：深度 → ワールドの位置 → 箱の中なら塗る（受けない物はステンシルで除く）

描き手             Decal Renderer（箱＋マテリアル。Progress は Script・Timeline から）
（System）         Hitbox Telegraph（Hitbox の形から箱と gDrawData を作る。進み具合 = Warning の進み具合）
```

```
1フレームの描画（3D）
  不透明なメッシュ（受けない物はステンシルに印 0x80 を書く）
  デカール                          ← 新：深度を読むだけで結び直し、印の無い所だけ塗る
  空
  粒（被写界深度の前）
  （被写界深度）
  半透明・線・粒
```

### 0.2 作る物・直す物

| ファイル | パート | 役目 |
|---|---|---|
| `Graphics/Pipeline/ShaderCompiler.h`・`.cpp` | A | 定数バッファの中身のリフレクション。ゲームの .shader の置き場の定数 |
| `Graphics/Pipeline/ShaderProperty.h`（新しい） | A | #PROPERTIES の種類と、gMaterial の中の場所に結んだ物 |
| `Graphics/Pipeline/ShaderPackageLoader.h`・`.cpp`（全体を差し替え） | A | 場所から名前と .hlsl の場所を決める・ゲームの .shader・#PROPERTIES・描くときの状態の既定・パスで引く |
| `Shader/Data/**/*.shader`（全部） | A | #META の `path:` の行を消す |
| `Shader/Data/IBL/BrdfLUT.PS.shader`（`BrdfLut.PS.shader` に改名）・`Graphics/IBL/IBLEnvironment.h`・`Engine.cpp` | A | 名前を `BrdfLut` にそろえる・ゲームの .shader を読む |
| `MyEngine.props`・`MyEngine_v1.vcxproj`・`Sandbox/Sandbox.vcxproj` | A | エンジンの .shader・Resources を、ゲームを起動するフォルダへ写す手順を1つにまとめる（消した・改名したファイルを写し先に残さない） |
| `ECS/ComponentUI.h`・`.cpp`・`ComponentSerializer.cpp` | B | アセットの種類に Shader・Material、色の欄に説明（Tip） |
| `Graphics/Material/Material.h`・`MaterialManager.h`・`.cpp`（新しい） | B | .material の中身・読む・保存・Inspector の並び・描く形にする |
| `Editor/Inspector/MaterialAssetEditor.h`・`.cpp`（新しい） | B | .material の Inspector（Undo つき） |
| `Editor/Windows/ProjectWindowActions.cpp` | B | Create → Material |
| `Graphics/Pipeline/RenderStates.h`・`.cpp`・`PSOManager.cpp`・`RootSignatureManager.h`・`.cpp` | C | `DrawCategory::Decal`・ステンシルのビットの分け方・並べ替えのキー・gDecal と gDrawData |
| `Graphics/Pipeline/ShaderConstants.h`・`Shader/Data/Buffers/Camera.hlsli.shader`・`Graphics/Renderer/SceneRenderer.h`・`.cpp` | C | カメラの逆行列・デカールの定数・不透明な物の後にデカール |
| `Graphics/GPU/FrameUploadBuffer.h`・`Graphics/RenderTarget/RenderTexture.h`・`.cpp` | C | バイトのまま定数を書く・深度を読むだけの窓口 |
| `Graphics/Renderer/DrawRequest.h`・`RenderQueue.h`・`.cpp`・`RenderContext.h`・`.cpp`・`Renderer.h`・`.cpp` | C | デカールのリクエスト・並べ替え・描く・DrawDecal・Receive Decals |
| `Shader/Data/Decal/Decal.hlsli.shader`・`DecalTexture.PS.shader`（新しい） | C | 深度から位置を戻す共通の部品・画像を貼る既定の Shader |
| `Components/Rendering3D/DecalRendererComponent.h`・`Systems/Rendering3D/DecalRenderSystem.h`・`.cpp`（新しい） | C | Decal Renderer |
| `Components/Rendering3D/ModelRendererComponent.h`・`PrimitiveRendererComponent.h`・`Components/EngineComponents.cpp`・`Editor/Inspector/ModelRendererEditor.cpp`・`Systems/Rendering3D/ModelRenderSystem.cpp`・`PrimitiveRenderSystem.h`・`.cpp` | C | Receive Decals |
| `Resources/Materials/Decal.material`（新しい） | C | Decal Renderer の既定のマテリアル |
| `Shader/Data/Decal/HitShape.hlsli.shader`・`HitboxTelegraph.hlsli.shader`・`HitboxTelegraph.PS.shader`（新しい） | D | 当たりと同じ式・予告の値・予告の既定の Shader |
| `Components/Combat/HitboxTelegraphComponent.h`・`Systems/Combat/HitboxTelegraphSystem.h`・`.cpp`（新しい） | D | Hitbox Telegraph |
| `Resources/Materials/HitboxTelegraph.material`（新しい） | D | 予告の既定のマテリアル |
| `Systems/EngineSystems.cpp`・`MyEngine_v1.vcxproj`・`.filters` | B・C・D | 初期化・描く順・ファイルを足す |
| `Sandbox/Resources/curves`・`materials`・`shaders`・`scenes`（新しい） | E | テスト用の Curve・マテリアル・ゲームの .shader・シーン |

### 0.3 使い方

**床の予告を出す**：Hitbox（床に置く形：Circle・Sector・Line）と同じ Entity に **Hitbox Telegraph** を付けるだけ。Hitbox が Warning の間、形の中が床・坂・段差に塗られる（Material が (none) なら、エンジンの既定の赤い塗り）。当たっている間も見せたい攻撃は **Show While Active** を入れる。

**キャラに塗らない**：プレイヤー・ボスの体の Model Renderer・Primitive Renderer の **Receive Decals** を外す（不透明なときだけ効く）。

**見た目を変える**：Project ウィンドウの **Create → Material** で .material を作り、Inspector で **Shader** に `MyEngine/Shader/Data/Decal/HitboxTelegraph.PS.shader` を選ぶ。色・縁の太さなどの欄が出るので直して、Hitbox Telegraph の Material で選ぶ。攻撃の種類ごとに .material を分ける（同じファイルを選んだ物は、みんな同じ見た目）。

**時間で値を動かす（コードを書かない）**：値の下の **〜 Curve** に .curve を選ぶと、**End** の欄が出る。Curve の横軸は描き手が渡す 0〜1（予告なら Warning の進み具合）、縦軸 0 で上の値、1 で End の値（間は補間、外ははみ出す）。

**Shader を自分で書く**：ゲームの `resources` の下のどこかに .shader を置く（起動したときに全部読む。名前は `Project/フォルダ/ファイル名`）。予告の Shader は、エンジンの .hlsli を include して、gDrawData の値（形・段階・進み具合・残り秒）を読む（E-1 の `RingTelegraph.PS.shader`）。

```
#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category:   Decal          // デカールの Shader（Model の Shader は、今は描く Component が無い）
vs:         Object3dVS
blend:      Normal         // ここから3つは既定（.material で上書きできる。書かなければ Normal・SolidBack・TestWrite）
rasterizer: SolidFront     // デカールは箱の裏を描く
depth:      Disable        // デカールは深度を書けない
#PROGRAM_END

#PROPERTIES
// 変数名: 種類 "表示名" "説明（省略できる）" 値
ringColor: Color   "Ring Color" "輪の色" 0.3, 0.8, 1, 0.7     // Color：3つか4つ（sRGB）
speed:     Float   "Speed" 1.5                                 // Float：1つ
width:     Range   "Width" 0.2, 0, 1                           // Range：既定・下限・上限
mask:      Texture "Mask"                                      // Texture：値は書かない（選ばなければ白）
#PROPERTIES_END

#HLSL
（gMaterial の構造体に、#PROPERTIES と同じ名前・合う型のメンバーを書く）
#HLSL_END
```

**床に何かを貼る（予告ではない）**：空の Entity に **Decal Renderer** を付ける。箱は Transform の大きさ（Scale 1 で 1辺 1）、上（+Y）から下へ貼る。Material が (none) なら白い画像。**Progress** は Curve の横軸（Script・Timeline から動かす）、**Order** は重なったときの順。

**ギズモ**：Decal Renderer の箱は、Scene ビューに水色の線で出る。予告の形は今までどおり Hit Shapes のギズモ。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| .material が Shader を指す方法 | **.shader のパス**（他のアセットと同じ）。中の名前は表示用 | 名前のぶつかりが無い。ファイルを動かすと切れるのは他のアセットも同じなので、後で番号（GUID）を入れるときにまとめて直せる |
| マテリアルの Shader が乗る描き方 | **今の category に乗る**。`shading:` を書かない物はパスで引く | 頂点の形・描き先・描く順は category が決める。エンジンの Shader と同じ programs_ に入り、PSOKey・並べ替えがそのまま使える |
| 値の意味 | **.shader の #PROPERTIES**（種類・表示名・説明・既定・範囲）。並び・場所はリフレクション | 意味だけを書くので二重にならない。後でグラフが .shader を作るときも、このブロックを書き出すだけ |
| Blend・Rasterizer・Depth | **#PROGRAM に既定、.material で上書き** | 透ける前提かは Shader を書く人が知っている。同じ Shader で加算と通常を分けられる。描く Component には持たせない |
| 描き手の値 | **描き手ごとの定数バッファ**（C++ の構造体＋.hlsli、名前は gDrawData・b22） | 文字の名前で書き込まないので型がずれない。大きさを C++ と比べて写し違いを止める |
| 物ごとの上書き | **今は作らない**（.material を分ける） | Component は trivially_copyable。後で Manager 側の表で足せる |
| 合わない値 | **警告して捨てる**（Inspector の Undo で戻す） | ファイルがいつも今の Shader と一致する |
| 値ごとの Curve | **始まりと終わりを、Curve の出力で補間** | Float・Color・Vector に同じ形で使える。色の合いも変わる |
| ゲームの .shader | **#META の path: をやめて、場所から決める**。resources の下を全部読む（名前は Project/〜） | 同じことを2か所に書かない。エンジンの名前は今のまま |
| BRDF の表の PS の名前 | **`BrdfLut` にそろえる**（ファイル名を `BrdfLut.PS.shader` に。`GetBrdfLUT` も `GetBrdfLut` に） | 命名を統一する。名前はファイル名から作るので、ファイル名を直せばコードは今の `BrdfLutPS` のまま |
| 写す手順 | **`MyEngine.props` に1つにまとめる**（ゲーム＝exe を作るプロジェクトのビルドの後）。.shader は写し先を消してから全部写す、Resources は変わった物だけ写して元に無い物を消す | 上書きで写すだけだと、消した・改名した（大文字だけでも）ファイルが残り、Release で止まる（写しで止まった）。.shader はファイル名から名前を作るので、大文字・小文字までそろえる。Resources はパスで読むので、大文字・小文字の違いは困らない |
| 床への載せ方 | **デカール**（深度から位置を戻す） | 坂・段差・凹凸に張り付く。高さ（Height）の外は塗らないので、当たりとずれない |
| 塗らない物 | **描く Component ごとの Receive Decals**。受けない物はステンシルの一番上のビット | Unity HDRP・Unreal と同じ。UI のマスクは下の 7 ビットだけを使う |
| 描く仕組みの分け方 | **Graphics に汎用のデカール＋Decal Renderer、予告は Combat の Hitbox Telegraph** | Graphics は Hitbox を知らない。影・あと・魔法陣も同じ描画で描ける |
| ジャスト範囲 | **描かない**（ギズモだけ） | ジャストはゲームの絵に出さない |
| 予告を出す段階 | **Warning の間＋Show While Active**。Curve の横軸は Warning の進み具合 | 出す・出さないはゲームの決めごとなので Component、見た目はマテリアル |
| 名前 | `HitboxTelegraphComponent`・`HitboxTelegraphSystem` | 何の値を描くかが分かる。Inspector で Hitbox の下に並ぶ |
| 未選択のとき | **エンジンの既定の .shader と .material で描く** | 付けただけで見える（Physic Material と同じ） |
| デカールの深度 | Depth は TestNoWrite か Disable（TestWrite は警告して描かない） | 深度を読むだけの窓口で結ぶので、書けない |
| 描けないマテリアル | Decal ではない Shader・gDrawData を読む Shader を描き手の値なしで：**1回だけ警告して描かない** | Inspector で選び間違えても止めない。C++ と HLSL の大きさが違うのは写し違いなので止める |
| 箱 | 予告の箱は形を囲む四角（横に 0.1m 広げる）、縦は底の 0.05m 下から形の高さまで | 縁をなめらかにする1ピクセルが切れない。底と同じ高さの床が、深度の誤差で外にならない |

### 0.5 場面ごとの動作

| 場面 | どうなるか |
|---|---|
| Hitbox が Off | 予告は出ない |
| Warning（Warning Seconds 1.2） | 形の中が薄く塗られ、縁が引かれ、根元から先へ濃い色が広がる（1.2 秒で先まで） |
| Warning Seconds が 0（ずっと Warning） | 進み具合は 0 のまま（濃い色は広がらない） |
| Active・Show While Active が外れている | 消える |
| Active・Show While Active が入っている | Active の色で塗る（Curve の横軸は 1） |
| 予告の形の中に坂・段差がある | 坂・段差に張り付いて塗られる。形の高さより上・底より下は塗らない |
| 予告の中にプレイヤー・ボスが立っている（Receive Decals が外れている） | 体には塗られない（床だけ） |
| Receive Decals が入っている箱 | 上の面にも横の面にも塗られる（横は伸びる） |
| 半透明の物 | 塗られない（深度を書かないので） |
| 空 | 塗られない |
| Material が (none) | エンジンの既定（赤） |
| Decal Renderer に予告の Shader のマテリアルを選んだ | 1回だけ警告して描かない |
| .material の Depth を TestWrite で上書き | 1回だけ警告して描かない |
| Shader を変えた | 名前と種類が同じ値は残る。ほかは新しい Shader の既定 |
| .material に Shader に無い値が書いてある | 読むときに警告。次に保存すると消える |
| Range の値が Curve で範囲の外へ | 範囲の中に止める |
| Inspector で値を直す | つまむ・打つのが終わると保存。Undo で戻る（ファイルも） |
| ゲームの .shader の名前がエンジンとぶつかる | ぶつからない（Project/〜）。ゲームの中で2つぶつかったら、起動時に止める |
| 古い .shader に `path:` が残っている | 起動時に止める（消してください、と出る） |
| エンジンの .shader を消した・動かした・改名した | 次のビルドで、ゲームを起動するフォルダの写しも同じになる |
| エンジンの Resources のファイルを消した | 次のビルドで、写しからも消える |

### 0.6 今回やらないこと

3章にまとめた（今の Model・Primitive のマテリアルへの載せ替え・物ごとの上書き・Shader Graph・.shader の作り直しの即時反映・デカールの法線での薄め方 など）。

## 1章 解説

### 1.1 定数バッファの中身のリフレクション

今までのリフレクションは「どの変数名が、どのレジスタか」（結び目）だけだった。マテリアルの Inspector の欄と、描くときに詰める場所を作るには、**定数バッファの中の変数の名前・場所・大きさ**が要る。

`ID3D12ShaderReflection::GetConstantBufferByName` で定数バッファを引き、`GetVariableByIndex` で中の変数を1つずつ見る。`ConstantBuffer<Material> gMaterial` と書いた定数バッファは、中に **構造体の変数が1つだけ**（名前は gMaterial）入っているので、その1つは名前を付けずに開き、構造体のメンバー（`GetMemberTypeByIndex`・`GetMemberTypeName`）を並べる。メンバーの場所は「構造体の先頭からのずれ（`D3D12_SHADER_TYPE_DESC::Offset`）＋ 構造体の場所」。

大きさは「行 × 列 × 4 バイト」（float3 は 12）。HLSL の定数バッファは 16 バイトの行に詰めるので、場所は C++ の構造体と同じになるように書く（例：`float amount; float2 offset; uint flag; float3 tint;` なら 0・4・12・16）。写しの自動テストで、Unlit の gMaterial の場所が `Material3dData` の `offsetof` と全部同じことを確かめた。

### 1.2 #PROPERTIES と gMaterial を結ぶ

#PROPERTIES は「意味」だけを書く。読むときに次を確かめ、合わなければ .shader の書き間違いなので止める。

| 種類 | gMaterial の型 | Inspector | 詰め方 |
|---|---|---|---|
| Float | float | 数 | そのまま |
| Range | float | 範囲のある数（Drag） | 範囲の中に止める |
| Toggle | uint・int・bool | チェック | 0 か 1（Curve で動かしたら半分で切り替わる） |
| Vector | float〜float4 | 成分の数の欄 | そのまま |
| Color | float3・float4 | 色（sRGB） | リニアへ直す（A はそのまま） |
| Texture | uint | 画像を選ぶ | gTextures[] の番号（選んでいなければ白） |

#PROPERTIES に書かなかったメンバーは Inspector に出さず 0 を送る（パディングなど）。

### 1.3 名前と場所（path: をやめる）

今までは #META の `path:` が「作る .hlsl の場所」と「キャッシュのキー」、ファイル名が「名前」だった。同じことを場所と `path:` の2か所に書いていたので、`path:` をやめて**場所から決める**。

| .shader | 作る .hlsl | 名前 | .material が指すパス |
|---|---|---|---|
| エンジンの `Model/Unlit.PS.shader` | `Model/Unlit.PS.hlsl` | `UnlitPS` | `MyEngine/Shader/Data/Model/Unlit.PS.shader` |
| エンジンの `Buffers/Camera.hlsli.shader` | `Buffers/Camera.hlsli`（Include は .hlsl を付けない） | （Include は名前を付けない） | — |
| ゲームの `resources/shaders/Ring.PS.shader` | `Project/shaders/Ring.PS.hlsl` | `Project/shaders/RingPS` | `resources/shaders/Ring.PS.shader` |

エンジンの .shader の `path:` は全部、この決め方と同じだった（`IBL/BrdfLUT.PS.shader` の `path:` だけが `BrdfLut` だったので、ほかの名前に合わせてファイル名を `BrdfLut.PS.shader` に直す）。ゲームの .hlsl は `Generated/Project/` の下に作るので、エンジンの物と場所がぶつからない。include は今までどおり `Generated` からの名前で書けるので、ゲームの .shader から `"Decal/Decal.hlsli"` などのエンジンの部品を使える。

.material が指すパスは、読む場所（Debug はエンジンのフォルダ、Release は exe の隣の写し）に関係なく `MyEngine/Shader/Data/〜` にそろえる。区切り（/ と \）・`./` は `ToPathKey` でそろえてから引く。

**写し先も元とそろえる**：名前をファイル名から作るので、Release が読む写し（ゲームを起動するフォルダの `MyEngine\Shader\Data`）のファイル名が元と違うと、名前も違ってしまう。Windows のファイルの名前は大文字・小文字を区別しないので、上書きで写すと「`BrdfLUT.PS.shader` に `BrdfLut.PS.shader` の中身を書く」ことになり、名前は写し先の古い方（`BrdfLUT`）のまま残る。消した・動かしたファイルも残るので、同じ名前の Shader が2つになって止まる。そこで .shader は**写し先を消してから全部写す**（A-9）。60 個ほどの小さいファイルなので、毎回写しても速い。Resources は大きいファイル（画像・モデル）があり、パスで読むので大文字・小文字の違いは困らないから、変わった物だけ写して元に無い物を消す。

```
ビルド（ゲーム＝exe を作るプロジェクト）
  └→ MyEngine.props の CopyMyEngineRuntimeFiles
        MyEngine\Shader\Data：消す → 全部写す        （名前の大文字・小文字まで元と同じ）
        MyEngine\Resources  ：変わった物だけ写す → 元に無い物を消す
```

### 1.4 マテリアルを描く形にする（`MaterialManager::Resolve`）

```
.material の値（sRGB・Curve・終わりの値）
   │ Curve があれば  value = Lerp(始まり, 終わり, Curve.Evaluate(進み具合))   ← sRGB のまま補間
   │ Range は範囲の中に止める / Toggle は 0.5 で切る
   │ Color はリニアへ（SrgbToLinear）/ Texture は番号（無ければ白）
   ▼
gMaterial の大きさのバイトの並び（#PROPERTIES の場所に書く。ほかは 0）
```

描くたびに作る（同じ .material でも、描き手ごとに進み具合が違うので）。

### 1.5 デカール：深度から位置を戻す

デカールは **1辺 1 の箱**を描く。箱が画面に映ったピクセルごとに、PS が次をする（`Decal/Decal.hlsli` の `GetDecalSurface`）。

1. そのピクセルの深度（不透明な物の一番手前）を読む
2. 画面の位置と深度から、正規化座標（x・y は -1〜+1、y は上が +）を作る
3. カメラの `inverseViewProj` を掛けて w で割る → ワールドの位置
4. 箱のワールド行列の逆行列（`gDecal.worldToDecal`）を掛ける → 箱の中の位置。-0.5〜+0.5 の外なら捨てる（`clip`）

箱の裏の面だけを描く（`SolidFront`）。表を描くと、カメラが箱の中に入ったときに何も描かれない。深度テストはしない（`Disable`。箱の面より手前の床も塗りたいので）。

### 1.6 深度を読みながら描く（読むだけの DSV）

不透明な物を描いた後、同じ深度を **SRV で読みながら、DSV としても結んでおく**（ステンシルのテストに使う）。書く DSV のまま SRV で読むことはできないので、`D3D12_DSV_FLAG_READ_ONLY_DEPTH | READ_ONLY_STENCIL` を付けた**読むだけの DSV** を作っておき、デカールの間だけそれで結び直す。リソースの状態は `DEPTH_READ | PIXEL_SHADER_RESOURCE`（この2つは一緒にできる）。描き終わったら `DEPTH_WRITE` に戻し、書く DSV で結び直す。

### 1.7 受けない物をステンシルで除く

```
ステンシル 8 ビット
 [7]  [6 5 4 3 2 1 0]
  │    └ UI のマスク（外にあるマスクの数。入れ子は 8 段までなので 7 ビットで足りる）
  └ デカールを受けない物を描いた所
```

- Receive Decals を外した不透明な物：いつも通し、描いた所の一番上のビットを書く（`MarkNoDecal`。参照値 0x80、書くビット 0x80）
- デカール：一番上のビットだけを読み、参照値（0x80）と**違う**所だけ通す（`DecalTest`）

UI のマスクは別の描き先（画面の絵）のステンシルなので本当はぶつからないが、読み書きするビットを分けておけば、同じ深度に描くことになっても壊れない。ステンシルの値が違う PSO が同じ並べ替えのキーにならないように、`GetSortKey` にステンシルも入れた（入れていないと、PSO を切り替えずに描いてしまう）。

### 1.8 予告：当たりと同じ式で塗る

予告の Shader は `Decal/HitShape.hlsli` の `CalcHitShapeDistance2D` で、当たりと同じ符号付き距離を計算する（C++ の `CalcSectorDistance`・`CalcLineDistance` をそのまま HLSL にした物）。写しで、この .hlsli の文をそのまま C++ としてコンパイルし、`WorldHitShape::CalcSignedDistance2D` と 16 万点で比べて、差が 0 なのを確かめた。

描き手（`HitboxTelegraphSystem`）は、`HitSystem::CalcWorldShape`（当たりと同じ、Entity の大きさを掛けた形）から、次の2つを作る。

- **箱**：形のローカルの軸（right・up・forward）で、円・扇は半径の四角、線は幅 × 長さ。横に 0.1m 広げ、縦は底の 0.05m 下から形の高さまで
- **gDrawData**（`HitboxTelegraphData`）：形の中心・軸・半径・開き・長さ・幅・高さ・種類・段階・進み具合・残り秒・段階に入ってからの秒

PS は `fwidth`（隣のピクセルとの差＝1 ピクセルの長さ）で縁を1ピクセルぶんなめらかにする。段差の所は隣のピクセルが別の面で差が大きくなるので、0.1m で止める。`fwidth` は `clip` で捨てる前に求める（捨てたピクセルとの差は使えない）。

### 1.9 あまり使わない書き方

- **`std::from_chars`**：文字列から数を読む。例外を投げず、ロケール（国の設定で小数点が , になる等）に左右されない。読めた所までのポインタとエラーを返すので、全部読めたかも分かる（#PROPERTIES の値）
- **`std::as_bytes(std::span(&data, 1))`**：構造体1つを、中身を変えずに「バイトの並び」として見る。`std::span` は「先頭と数」だけを持つ軽い入れ物（写さない）。描き手の値を、構造体の型を知らない `Renderer::DrawDecal` に渡す
- **`std::stable_sort`**：同じ順の物は、積んだ順のまま並べる（`std::sort` は入れ替わることがある）。重なったデカールの上下が毎フレーム入れ替わらないように
- **`clip(x)`**：x（の成分のどれか）がマイナスなら、そのピクセルを描かない
- **`fwidth(x)`**：`abs(ddx(x)) + abs(ddy(x))`。x が隣のピクセルでどれだけ変わるか

## パートA Shader の読み込み（場所から名前・#PROPERTIES・定数バッファの中身）

### A-1 `Graphics/Pipeline/ShaderCompiler.h`

include に `<string_view>` を足す。

```cpp
#include <string>
#include <string_view>
#include <vector>
```

`kShaderDataDir` の下に足す。

```cpp
// ゲームの .shader を探すフォルダ（起動するフォルダからの相対。Project ウィンドウ・アセットの一覧と同じ resources）
inline constexpr const wchar_t* kProjectShaderDir = L"resources";
// ゲームの .shader から作る .hlsl の置き場（kShaderGeneratedDir の下。エンジンの .hlsl と場所がぶつからないように分ける）
inline constexpr const wchar_t* kProjectGeneratedSubDir = L"Project";
```

`ShaderReflection` を差し替える（前に2つの構造体を足し、`constantBuffers` と `FindConstantBuffer` を足す）。

```cpp
// 定数バッファの中の変数1つ（ConstantBuffer<構造体> の構造体のメンバー1つ）
struct ShaderConstantVariable {
	std::string name;                                     // 変数名 "baseColor"（入れ子の構造体は "outer.inner"）
	UINT offset = 0;                                      // 定数バッファの先頭からのバイト
	UINT size = 0;                                        // バイト数
	D3D_SHADER_VARIABLE_CLASS typeClass = D3D_SVC_SCALAR; // 形：SCALAR（float）・VECTOR（float4）・MATRIX_ROWS（float4x4）など
	D3D_SHADER_VARIABLE_TYPE type = D3D_SVT_FLOAT;        // 中身の型：FLOAT・UINT・INT・BOOL など
	UINT rows = 1;                                        // 行の数（行列以外は 1）
	UINT columns = 1;                                     // 列の数（float3 なら 3）
	UINT elements = 0;                                    // 配列の要素の数（配列でなければ 0）
};

// 定数バッファ1つの中身（変数名・場所・大きさ）
struct ShaderConstantBuffer {
	std::string name;                              // 変数名 "gMaterial"
	UINT size = 0;                                 // 全体のバイト数（16 の倍数）
	std::vector<ShaderConstantVariable> variables; // 中の変数（場所の順）

	// 名前で変数を探す（無ければ nullptr）
	const ShaderConstantVariable* FindVariable(std::string_view variableName) const;
};

// 1つのシェーダに入ってる情報をまとめたもの
struct ShaderReflection {
	std::vector<ShaderResourceBinding> resources;      // 全Resource情報
	std::vector<ShaderInputParameter> inputs;          // 全Input情報
	std::vector<ShaderConstantBuffer> constantBuffers; // 全定数バッファの中身（使われていない定数バッファはコンパイルで消えるので入らない）
	Microsoft::WRL::ComPtr<IDxcBlob> blob;             // コンパイル結果
	std::array<UINT, 3> threadGroupSize{};             // CSの [numthreades(x, y, z)]。CS以外は0のまま

	// 名前で定数バッファを探す（無い・使われていないなら nullptr）
	const ShaderConstantBuffer* FindConstantBuffer(std::string_view bufferName) const;
};
```

private の `MakeShaderInputParameter` の後に足す。

```cpp
	/// <summary>
	/// リフレクションから定数バッファの中身（変数名・場所・大きさ）を取得する
	/// </summary>
	/// <param name="reflection">CreateReflection関数の戻り値</param>
	/// <param name="binds">MakeShaderResourceBinding関数の戻り値（定数バッファの名前を、ここから拾う）</param>
	static std::vector<ShaderConstantBuffer> MakeShaderConstantBuffers(ID3D12ShaderReflection* reflection, const std::vector<ShaderResourceBinding>& binds);

	/// <summary>
	/// 変数の型を見て、中の変数を out に足す（構造体なら、メンバーを1つずつ。入れ子の構造体も下まで）
	/// </summary>
	/// <param name="baseOffset">この型の先頭の、定数バッファの先頭からのバイト</param>
	/// <param name="prefix">名前の頭に付ける言葉（入れ子の構造体の名前。一番外は空）</param>
	static void AppendConstantVariables(ID3D12ShaderReflectionType* type, UINT baseOffset, const std::string& prefix, std::vector<ShaderConstantVariable>& out);
```

### A-2 `Graphics/Pipeline/ShaderCompiler.cpp`

include に `<algorithm>` を足す。

```cpp
#include <algorithm>
#include <array>
#include <format>
```

静的メンバ変数の後に足す。

```cpp
//=============================================================================
// 名前で探す
//=============================================================================
// ===== 定数バッファの中の変数 =====
const ShaderConstantVariable* ShaderConstantBuffer::FindVariable(std::string_view variableName) const {
	const auto it = std::ranges::find(variables, variableName, &ShaderConstantVariable::name);
	return (it != variables.end()) ? &*it : nullptr;
}

// ===== 定数バッファ =====
const ShaderConstantBuffer* ShaderReflection::FindConstantBuffer(std::string_view bufferName) const {
	const auto it = std::ranges::find(constantBuffers, bufferName, &ShaderConstantBuffer::name);
	return (it != constantBuffers.end()) ? &*it : nullptr;
}
```

`CompileShaderReflection` の `refl.inputs = ...` の次の行に足す。

```cpp
	refl.inputs = MakeShaderInputParameter(reflection.Get());
	refl.constantBuffers = MakeShaderConstantBuffers(reflection.Get(), refl.resources);
```

ファイルの最後に足す。

```cpp
//=============================================================================
// リフレクションから定数バッファの中身を取得する
//=============================================================================
// ===== 定数バッファ1つずつ =====
std::vector<ShaderConstantBuffer> ShaderCompiler::MakeShaderConstantBuffers(ID3D12ShaderReflection* reflection, const std::vector<ShaderResourceBinding>& binds) {
	std::vector<ShaderConstantBuffer> buffers;
	for (const ShaderResourceBinding& bind : binds) {
		if (bind.type != D3D_SIT_CBUFFER) {
			continue; // テクスチャ・サンプラーなどは中身が無い
		}
		// --- 定数バッファの大きさと、中の変数の数 ---
		ID3D12ShaderReflectionConstantBuffer* constantBuffer = reflection->GetConstantBufferByName(bind.name.c_str());
		D3D12_SHADER_BUFFER_DESC bufferDesc{};
		if (FAILED(constantBuffer->GetDesc(&bufferDesc))) {
			continue; // 名前の違う物を引いた（ここには来ないはず）
		}
		ShaderConstantBuffer buffer;
		buffer.name = bind.name;
		buffer.size = bufferDesc.Size;
		// --- 中の変数 ---
		// ConstantBuffer<構造体> gMaterial と書くと、中には構造体の変数が1つだけ（名前は gMaterial）入る
		// その1つは名前を付けずに開き、構造体のメンバーを並べる（"gMaterial.baseColor" ではなく "baseColor"）
		for (UINT i = 0; i < bufferDesc.Variables; ++i) {
			ID3D12ShaderReflectionVariable* variable = constantBuffer->GetVariableByIndex(i);
			D3D12_SHADER_VARIABLE_DESC variableDesc{};
			variable->GetDesc(&variableDesc);
			const bool isWrapper = (bufferDesc.Variables == 1 && bind.name == variableDesc.Name);
			AppendConstantVariables(variable->GetType(), variableDesc.StartOffset, isWrapper ? std::string() : std::string(variableDesc.Name), buffer.variables);
		}
		buffers.push_back(std::move(buffer));
	}
	return buffers;
}

// ===== 型の中の変数（構造体は、メンバーを下まで開く）=====
void ShaderCompiler::AppendConstantVariables(ID3D12ShaderReflectionType* type, UINT baseOffset, const std::string& prefix, std::vector<ShaderConstantVariable>& out) {
	D3D12_SHADER_TYPE_DESC typeDesc{};
	type->GetDesc(&typeDesc);
	// --- 構造体：メンバーを1つずつ（名前は "外の名前.メンバー名"。一番外は prefix が空なのでメンバー名だけ）---
	if (typeDesc.Class == D3D_SVC_STRUCT && typeDesc.Elements == 0) {
		for (UINT i = 0; i < typeDesc.Members; ++i) {
			ID3D12ShaderReflectionType* memberType = type->GetMemberTypeByIndex(i);
			D3D12_SHADER_TYPE_DESC memberDesc{};
			memberType->GetDesc(&memberDesc);
			const std::string memberName = type->GetMemberTypeName(i);
			const std::string name = prefix.empty() ? memberName : prefix + "." + memberName;
			AppendConstantVariables(memberType, baseOffset + memberDesc.Offset, name, out);
		}
		return;
	}
	// --- 1つの値（float・float4・float4x4・その配列）---
	// 大きさは「成分の数 × 4 バイト」。配列は要素ごとに 16 バイトにそろうので、最後の要素以外は 16 バイトずつ
	constexpr UINT kComponentBytes = 4;  // float・uint・int・bool は 4 バイト
	constexpr UINT kRegisterBytes = 16;  // 定数バッファの1行（float4）の大きさ
	const UINT valueBytes = typeDesc.Rows * typeDesc.Columns * kComponentBytes;
	ShaderConstantVariable variable;
	variable.name = prefix;
	variable.offset = baseOffset;
	variable.size = (typeDesc.Elements > 0) ? kRegisterBytes * (typeDesc.Elements - 1) + valueBytes : valueBytes;
	variable.typeClass = typeDesc.Class;
	variable.type = typeDesc.Type;
	variable.rows = typeDesc.Rows;
	variable.columns = typeDesc.Columns;
	variable.elements = typeDesc.Elements;
	out.push_back(std::move(variable));
}
```

### A-3 `Graphics/Pipeline/ShaderProperty.h`（新しい）

```cpp
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "MyEngine/Math/Vector4.h"

// .shader の #PROPERTIES（マテリアルの値の意味）と、それを定数バッファの中の場所に結んだ物
// 並び・場所・大きさはリフレクション（ShaderConstantBuffer）から取り、#PROPERTIES には意味（種類・表示名・既定値・範囲）だけを書く


// マテリアルの値が入る定数バッファの変数名（RootSignatureManager::NameToRole の Material と同じ名前）
inline constexpr std::string_view kMaterialConstantBufferName = "gMaterial";
// 描き手（デカールの予告など）が描く物ごとに送る定数バッファの変数名（NameToRole の DrawData と同じ名前）
inline constexpr std::string_view kDrawDataConstantBufferName = "gDrawData";


/// <summary>
/// #PROPERTIES の値の種類（Inspector の欄の形と、定数バッファに詰める形が決まる）
/// </summary>
enum class ShaderPropertyType {
	Float,   // 数1つ（float）
	Range,   // 範囲のある数1つ（float。min〜max の中だけ）
	Toggle,  // 入れる・外す（uint・int・bool。0 か 1）
	Vector,  // 数 1〜4 個（float〜float4）
	Color,   // 色（float3・float4。Inspector と .material は sRGB、送るときにリニアへ直す）
	Texture, // テクスチャ（uint。gTextures[] の番号を送る。選んでいなければ白）
};


/// <summary>
/// #PROPERTIES の1行（.shader に書いた、値1つの意味）
/// </summary>
struct ShaderPropertyDef {
	std::string name;                                // gMaterial の中の変数名（.material に保存するときの名前にもなる）
	ShaderPropertyType type = ShaderPropertyType::Float; // 種類
	std::string displayName;                         // Inspector に出す名前
	std::string tooltip;                             // マウスを乗せたときの説明（書かなければ空）
	Vector4 defaultValue = {0.0f, 0.0f, 0.0f, 0.0f}; // 既定の値（Float・Range・Toggle は x だけ使う）
	float min = 0.0f;                                // Range の下限
	float max = 0.0f;                                // Range の上限

	bool operator==(const ShaderPropertyDef&) const = default;
};


/// <summary>
/// 定数バッファの中の場所を付けた値1つ
/// </summary>
struct ShaderProperty {
	ShaderPropertyDef def;   // .shader に書いた意味
	uint32_t offset = 0;     // gMaterial の先頭からのバイト
	uint32_t components = 1; // 成分の数（float は 1、float3 の色は 3）
};


/// <summary>
/// マテリアルの値の並び（描画プログラム1つに1つ。ShaderPackageLoader::GetPropertyLayout が作る）
/// </summary>
struct ShaderPropertyLayout {
	uint32_t bufferSize = 0;                // gMaterial の大きさ（バイト。0 なら gMaterial を使わない Shader）
	std::vector<ShaderProperty> properties; // #PROPERTIES に書いた順

	// 変数名で探す（無ければ nullptr）
	const ShaderProperty* Find(std::string_view name) const {
		const auto it = std::ranges::find_if(properties, [name](const ShaderProperty& property) { return property.def.name == name; });
		return (it != properties.end()) ? &*it : nullptr;
	}
};
```

### A-4 `Graphics/Pipeline/ShaderPackageLoader.h`（全体を差し替え）

`Load`（1つだけ読む）・`DeriveName`・`defs_` は使っていなかったので消した。名前・.hlsl の場所・パスは `ApplyLocation` が決める。

```cpp
#pragma once
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "MyEngine/Graphics/Pipeline/ShaderProperty.h"

// #META（全 .shader 共通）
struct ShaderMeta {
	enum class Stage { VS, PS, CS, Include };
	Stage stage{};
	std::wstring path;     // 作る .hlsl の場所（Generated からの相対）。.shader の場所から決める（#META には書かない）
	std::wstring profile;  // Include は空
	std::wstring entry = L"main";
};
// 描くときの状態の既定（#PROGRAM の blend・rasterizer・depth。.material で上書きできる）
struct ProgramStates {
	BlendMode blend = BlendMode::Normal;                   // 重ね方
	RasterizerType rasterizer = RasterizerType::SolidBack; // 裏面
	DepthMode depth = DepthMode::TestWrite;                // 深度
};
// #PROGRAM（描画PSのみ。無ければ has=false）
struct ProgramDef {
	bool has = false;
	DrawCategory category{};
	std::optional<ShadingType> shading; // 書いた物だけ (category, shading) で引ける（エンジンの決まった描き方）。書かない物はパスで引く
	std::string vsName;
	ProgramStates states;               // 描くときの状態の既定
};
// 1つの .shader を解析した結果（一時オブジェクト）
struct ShaderDefinition {
	ShaderMeta meta;
	ProgramDef program;
	std::vector<ShaderPropertyDef> properties; // #PROPERTIES（マテリアルの値の意味）
	std::string hlslBody;
	std::string source;    // 読んだ .shader のファイル名（エラーに出す）
	std::string assetPath; // .material が指すパス（"MyEngine/Shader/Data/Model/Unlit.PS.shader"・"resources/shaders/Floor.PS.shader"）
	std::string name;      // 名前（エンジンの物は "UnlitPS"、ゲームの物は "Project/shaders/FloorPS"）
};
// レジストリ：1コンパイル単位（名前引き）
struct ShaderEntry {
	ShaderMeta::Stage stage{};
	std::wstring path, profile, entry;
	std::string assetPath; // どの .shader から作ったか（名前がぶつかったときに出す）
};
// レジストリ：1描画プログラム（(cat,shading)引き・パス引き）
struct ProgramEntry {
	std::string name;
	DrawCategory category{};
	std::optional<ShadingType> shading;           // エンジンの決まった描き方なら、その種類
	std::string vsName, psName;
	std::string assetPath;                        // PS の .shader のパス（.material が指すパス）
	ProgramStates states;                         // 描くときの状態の既定
	std::vector<ShaderPropertyDef> propertyDefs;  // #PROPERTIES
};

// ゲームの .shader の名前・作る .hlsl の頭に付ける言葉（エンジンの物と、名前・場所がぶつからないように）
inline constexpr std::string_view kProjectShaderNamePrefix = "Project/";


/// <summary>
/// .shaderを読む、見るクラス
/// </summary>
class ShaderPackageLoader {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class ShaderPackageLoader;
	};
	explicit ShaderPackageLoader(ConstructorKey) {} // 鍵を受け取るコンストラクタ


	static void Initialize();
	static void Release();

	/// <summary>
	/// エンジンの .shader を全部 読み込み→作成する（Shader/Generated に作成される）
	/// </summary>
	/// <param name="dataDir">FindDataDir の戻り値（エンジンの Shader/Data）</param>
	static void LoadAll(const std::filesystem::path& dataDir);

	/// <summary>
	/// ゲームの .shader を全部 読み込み→作成する（Shader/Generated/Project に作成される）。LoadAll の後に呼ぶ（エンジンの .hlsli を include できるように）
	/// <para>フォルダが無ければ何もしない</para>
	/// </summary>
	/// <param name="projectDir">ゲームのリソースのフォルダ（kProjectShaderDir）</param>
	static void LoadProject(const std::filesystem::path& projectDir);

	/// <summary>
	/// .shader を読むフォルダを決める
	/// <para>Release 以外：エンジンのフォルダ（MyEngine.props の MYENGINE_ROOT_DIR）を直接読む。.shader を直したら、次の起動で反映される</para>
	/// <para>Release・エンジンのフォルダが無い PC：exe の隣にコピーされた MyEngine/Shader/Data</para>
	/// </summary>
	static std::filesystem::path FindDataDir();

	/// <summary>
	/// 名前からShaderのResource, Input, コンパイル結果を取得
	/// </summary>
	static const ShaderReflection& GetShaderReflection(const std::string& name);

	/// <summary>
	/// 描画設定からシェーダーキャッシュを取り出す
	/// </summary>
	static size_t GetProgramIndex(DrawCategory category, ShadingType type);

	/// <summary>
	/// PS の .shader のパスから描画プログラムの番号を探す（.material が Shader を引く。#PROGRAM の無い .shader・知らないパスなら無し）
	/// </summary>
	/// <param name="assetPath">.shader のパス（"MyEngine/Shader/Data/〜"・"resources/〜"。区切りは / でも \ でもよい）</param>
	static std::optional<size_t> FindProgramByPath(std::string_view assetPath);

	// ===== アセットの番号（AssetType::Shader・.material の Shader）。番号＝描画プログラムの番号 + 1（0 は未選択）=====
	// パスから番号（#PROGRAM の無い .shader・知らないパスなら 0）
	static uint32_t FindProgramHandle(std::string_view assetPath);
	// 番号からパス（0・知らない番号なら空）
	static const std::string& GetProgramPath(uint32_t handle);

	/// <summary>
	/// シェーダーキャッシュからシェーダー情報を取り出す
	/// </summary>
	/// <param name="index">GetProgramIndexの戻り値</param>
	static const ProgramEntry& GetProgramAt(size_t index) { return instance_->programs_[index]; }

	/// <summary>
	/// 描画プログラムのマテリアルの値の並び（#PROPERTIES を gMaterial の中の場所に結んだ物）。初めて聞かれたときに作る
	/// <para>#PROPERTIES の名前が gMaterial に無い・型が合わないときは止める（.shader の書き間違い）</para>
	/// </summary>
	static const ShaderPropertyLayout& GetPropertyLayout(size_t index);

	// シェーダーのペア（vs, psなど）の総数
	static size_t GetProgramCount() { return instance_->programs_.size(); }


private:
	// 読むフォルダの違い（エンジンかゲームか）
	struct FolderOrigin {
		std::wstring generatedPrefix; // 作る .hlsl の場所の頭（エンジンは空、ゲームは "Project/"）
		std::string assetPrefix;      // .material が指すパスの頭（"MyEngine/Shader/Data/"・"resources/"）
		std::string namePrefix;       // 名前の頭（エンジンは空、ゲームは "Project/"）
		bool keepFolderInName = false; // 名前にフォルダを入れるか（ゲームは入れる。エンジンは今までどおりファイル名だけ）
	};

	static void LoadFolder(const std::filesystem::path& dir, const FolderOrigin& origin); // フォルダの下の .shader を全部読む
	static bool Generate(const ShaderDefinition& def); // outRoot/path に書き出す。中身が同じならスキップ
	static ShaderDefinition ParseFile(const std::filesystem::path& path); // ファイルを開いて全部読んでParseText関数に送る
	static ShaderDefinition ParseText(std::string_view text, std::string_view sourceName); // 中身を1行づつ読んでShaderDefinitionに格納
	static void ApplyLocation(ShaderDefinition& def, const std::filesystem::path& relativePath, const FolderOrigin& origin); // 場所から .hlsl の場所・名前・パスを決める
	static uint32_t DrawKey(DrawCategory c, ShadingType s) { return (uint32_t(c) << 8) | uint32_t(s); } // 描画設定をキーにする
	static ShaderMeta ParseMeta(const std::vector<std::string>& metaLines, std::string_view src); // .shaderの#METAを解析
	static ProgramDef ParseProgram(const std::vector<std::string>& lines, std::string_view src);  // .shaderの#PROGRAM解析
	static std::vector<ShaderPropertyDef> ParseProperties(const std::vector<std::string>& lines, std::string_view src); // .shaderの#PROPERTIES解析
	static ShaderPropertyDef ParsePropertyLine(std::string_view line, std::string_view src); // #PROPERTIES の1行
	static ShaderPropertyLayout MakePropertyLayout(const ProgramEntry& program); // #PROPERTIES を gMaterial の中の場所に結ぶ
	static void Register(const ShaderDefinition& def);
	// 描画プログラムを1つ登録（同じ組み合わせは上書き）
	static void AddProgram(const ShaderDefinition& def);
	static std::string_view Trim(std::string_view s);  // 空白を削除
	static std::string Require(const std::unordered_map<std::string, std::string>& kv, std::string_view key, std::string_view src); // 一致しているか
	static std::wstring DefaultProfile(ShaderMeta::Stage s); // ShaderのProfileのデフォルト設定
	static std::unordered_map<std::string, std::string> ToKeyValues(const std::vector<std::string>&, std::string_view);
	template<class E> static E ParseEnum(std::string_view s, std::string_view key, std::string_view src);

	// シングルトン
	friend struct std::default_delete<ShaderPackageLoader>;
	~ShaderPackageLoader() = default;
	ShaderPackageLoader(const ShaderPackageLoader&) = delete;
	ShaderPackageLoader& operator=(const ShaderPackageLoader&) = delete;
	static std::unique_ptr<ShaderPackageLoader> instance_;
	std::unordered_map<std::string, ShaderEntry> shadersByName_;
	std::deque<ProgramEntry> programs_;  // 描画の際に使うShaderファイルの組み合わせを保存する箱
	std::unordered_map<uint32_t, size_t> programIndexByCatShading_;
	std::unordered_map<std::string, size_t> programIndexByPath_;   // PS の .shader のパス（ToPathKey の形）→ 番号
	std::unordered_map<size_t, ShaderPropertyLayout> layouts_;     // 番号 → マテリアルの値の並び（初めて聞かれたときに作る）
};
```

### A-5 `Graphics/Pipeline/ShaderPackageLoader.cpp`（全体を差し替え）

```cpp
#include "ShaderPackageLoader.h"

#include <charconv>
#include <format>
#include <fstream>
#include <unordered_map>

#include <externals/magic_enum/magic_enum.hpp>

#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/String/ConvertString.h"

// 静的メンバ変数
std::unique_ptr<ShaderPackageLoader> ShaderPackageLoader::instance_ = nullptr;

namespace {
constexpr std::wstring_view kShaderExtension = L".shader";     // 読むファイルの拡張子
constexpr std::wstring_view kHlslExtension = L".hlsl";         // 作るファイルの拡張子（Include 以外）

// 種類の型と、定数バッファの変数の型が合うか（成分の数は components に入れる）
bool MatchPropertyType(ShaderPropertyType type, const ShaderConstantVariable& variable, uint32_t& components) {
	// 配列・行列は値1つとして扱えない
	if (variable.elements != 0 || variable.rows != 1) {
		return false;
	}
	components = variable.columns;
	const bool isFloat = (variable.type == D3D_SVT_FLOAT);
	const bool isInteger = (variable.type == D3D_SVT_UINT || variable.type == D3D_SVT_INT || variable.type == D3D_SVT_BOOL);
	switch (type) {
	case ShaderPropertyType::Float:
	case ShaderPropertyType::Range:
		return isFloat && components == 1;
	case ShaderPropertyType::Toggle:
		return isInteger && components == 1;
	case ShaderPropertyType::Vector:
		return isFloat && components >= 1 && components <= 4;
	case ShaderPropertyType::Color:
		return isFloat && (components == 3 || components == 4);
	case ShaderPropertyType::Texture:
		return (variable.type == D3D_SVT_UINT) && components == 1;
	}
	return false;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void ShaderPackageLoader::Initialize() {
	instance_ = std::make_unique<ShaderPackageLoader>(ConstructorKey());
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void ShaderPackageLoader::Release() {
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// ゲッター
//=============================================================================
size_t ShaderPackageLoader::GetProgramIndex(DrawCategory c, ShadingType s) {
	auto it = instance_->programIndexByCatShading_.find(DrawKey(c, s));
	MY_ASSERT_MSG(it != instance_->programIndexByCatShading_.end(), std::format("program ({}/{}) が未登録", magic_enum::enum_name(c), magic_enum::enum_name(s)));
	return it->second;
}

std::optional<size_t> ShaderPackageLoader::FindProgramByPath(std::string_view assetPath) {
	// 区切り（/ と \）・「./」の違いをそろえてから引く（登録も同じ ToPathKey の形）
	const auto it = instance_->programIndexByPath_.find(ToPathKey(ToPath(std::string(assetPath))));
	if (it == instance_->programIndexByPath_.end()) {
		return std::nullopt;
	}
	return it->second;
}

uint32_t ShaderPackageLoader::FindProgramHandle(std::string_view assetPath) {
	const std::optional<size_t> index = FindProgramByPath(assetPath);
	return index ? static_cast<uint32_t>(*index) + 1 : 0;
}

const std::string& ShaderPackageLoader::GetProgramPath(uint32_t handle) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	if (handle == 0 || handle > instance_->programs_.size()) {
		return kEmpty;
	}
	return instance_->programs_[handle - 1].assetPath;
}

const ShaderReflection& ShaderPackageLoader::GetShaderReflection(const std::string& name) {
	auto it = instance_->shadersByName_.find(name);
	MY_ASSERT_MSG(it != instance_->shadersByName_.end(), std::format("shader '{}' が未登録です", name));
	const ShaderEntry& s = it->second;
	return ShaderCompiler::CompileShaderReflection(s.path, s.profile, s.entry);
}

const ShaderPropertyLayout& ShaderPackageLoader::GetPropertyLayout(size_t index) {
	auto& layouts = instance_->layouts_;
	if (auto it = layouts.find(index); it != layouts.end()) {
		return it->second;
	}
	return layouts.emplace(index, MakePropertyLayout(instance_->programs_[index])).first->second;
}


//=============================================================================
// .shader → .hlsl作成
//=============================================================================
// ===== エンジン =====
void ShaderPackageLoader::LoadAll(const std::filesystem::path& dataDir) {
	// .material が指すパスは、読む場所（エンジンのフォルダか exe の隣か）に関係なく "MyEngine/Shader/Data/〜" にそろえる
	FolderOrigin origin;
	origin.assetPrefix = ToUtf8(std::filesystem::path(kShaderDataDir)) + "/";
	LoadFolder(dataDir, origin);
}

// ===== ゲーム =====
void ShaderPackageLoader::LoadProject(const std::filesystem::path& projectDir) {
	std::error_code error;
	if (!std::filesystem::is_directory(projectDir, error)) {
		return; // ゲームのリソースのフォルダが無い（エンジンだけで起動した）
	}
	FolderOrigin origin;
	origin.generatedPrefix = std::wstring(kProjectGeneratedSubDir) + L"/";
	origin.assetPrefix = ToUtf8(projectDir) + "/";
	origin.namePrefix = std::string(kProjectShaderNamePrefix);
	origin.keepFolderInName = true;
	LoadFolder(projectDir, origin);
}

// ===== フォルダの下の .shader を全部 =====
void ShaderPackageLoader::LoadFolder(const std::filesystem::path& dir, const FolderOrigin& origin) {
	std::vector<ShaderDefinition> defs;
	// 再帰的にフォルダを見て .shader だけ拾う
	// std::filesystem::recursive_directory_iterator は、指定したディレクトリ以下を再帰的にすべて探索する
	for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
		if (entry.is_regular_file() && entry.path().extension() == kShaderExtension) {
			ShaderDefinition def = ParseFile(entry.path());
			ApplyLocation(def, entry.path().lexically_relative(dir), origin);
			defs.push_back(std::move(def));
		}
	}
	for (auto& d : defs) {
		Generate(d); // hlsl作成
	}
	// PS以外（VS・CS・Include）を先に登録
	for (auto& d : defs) {
		if (d.meta.stage != ShaderMeta::Stage::PS) {
			Register(d);
		}
	}
	// PS
	for (auto& d : defs) {
		if (d.meta.stage == ShaderMeta::Stage::PS) {
			Register(d);
		}
	}
	LogManager::Log(std::format("[shader] {}: {} shaders / {} programs", ToUtf8(dir), instance_->shadersByName_.size(), instance_->programs_.size()));
}


//=============================================================================
// .shader を読むフォルダを決める
//=============================================================================
std::filesystem::path ShaderPackageLoader::FindDataDir() {
	std::filesystem::path dataDir = kShaderDataDir; // 見つからないときはexeの隣をコピー
#ifdef MYENGINE_ROOT_DIR
	// Release以外: エンジンのフォルダの.shaderを直接読む
	const std::filesystem::path engineDataDir = std::filesystem::path(MYENGINE_ROOT_DIR) / kShaderDataDir;
	std::error_code error;
	if (std::filesystem::is_directory(engineDataDir, error)) {
		dataDir = engineDataDir;
	}
#endif
	LogManager::Log(std::format("[shader] data: {}", ToUtf8(dataDir)));
	return dataDir;
}


//=============================================================================
// 場所から .hlsl の場所・名前・パスを決める
//=============================================================================
// "Model/Unlit.PS.shader"（エンジン）     → .hlsl "Model/Unlit.PS.hlsl"          名前 "UnlitPS"               パス "MyEngine/Shader/Data/Model/Unlit.PS.shader"
// "shaders/Floor.PS.shader"（ゲーム）     → .hlsl "Project/shaders/Floor.PS.hlsl" 名前 "Project/shaders/FloorPS" パス "resources/shaders/Floor.PS.shader"
// "Buffers/Camera.hlsli.shader"（Include）→ .hlsl ではなく "Buffers/Camera.hlsli"（include で書く名前のまま）
void ShaderPackageLoader::ApplyLocation(ShaderDefinition& def, const std::filesystem::path& relativePath, const FolderOrigin& origin) {
	// --- 作る .hlsl の場所（.shader を外し、Include 以外は .hlsl を付ける）---
	std::filesystem::path generated = relativePath;
	generated.replace_extension(); // "Model/Unlit.PS"・"Buffers/Camera.hlsli"
	std::wstring generatedPath = origin.generatedPrefix + generated.generic_wstring();
	if (def.meta.stage != ShaderMeta::Stage::Include) {
		generatedPath += kHlslExtension;
	}
	def.meta.path = std::move(generatedPath);

	// --- 名前（ファイル名の「.」を外す。ゲームの物はフォルダも入れて、頭に Project/ を付ける）---
	std::string stem = ToUtf8(generated.filename()); // "Unlit.PS"
	std::erase(stem, '.');
	const std::string folder = origin.keepFolderInName && generated.has_parent_path() ? ToUtf8(generated.parent_path()) + "/" : std::string();
	def.name = origin.namePrefix + folder + stem;

	// --- .material が指すパス ---
	def.assetPath = ToPathKey(ToPath(origin.assetPrefix + ToUtf8(relativePath)));
}


//=============================================================================
// outRoot/path に書き出す。中身が前回と同じならスキップ
//=============================================================================
bool ShaderPackageLoader::Generate(const ShaderDefinition& def) {
	std::filesystem::path outPath = std::filesystem::path(kShaderGeneratedDir) / def.meta.path;
	// --- .hlsl と中身が同じならスキップ ---
	if (std::filesystem::exists(outPath)) {
		std::ifstream in(outPath, std::ios::binary);
		std::string prev((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (prev == def.hlslBody) {
			return false;
		}
	}
	// 生成
	if (outPath.has_parent_path()) {
		std::filesystem::create_directories(outPath.parent_path());
	}
	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	out << def.hlslBody;
	return true;
}


//=============================================================================
// 追加登録
//=============================================================================
void ShaderPackageLoader::Register(const ShaderDefinition& def) {
	// Includeはコンパイルしないので名前登録しない
	if (def.meta.stage != ShaderMeta::Stage::Include) {
		// 同じ名前が2つあると、後の方が前の方を隠してしまう（どちらが使われるか分からない）ので止める
		const auto existing = instance_->shadersByName_.find(def.name);
		MY_ASSERT_MSG(existing == instance_->shadersByName_.end(),
		    std::format("Shader の名前 '{}' が2つあります（{} と {}）。ファイル名を変えてください", def.name, existing != instance_->shadersByName_.end() ? existing->second.assetPath : "", def.assetPath));
		instance_->shadersByName_.emplace(def.name, ShaderEntry{def.meta.stage, def.meta.path, def.meta.profile, def.meta.entry, def.assetPath});
		LogManager::Log(std::format("ShaderName: {}", def.name));
	}
	// #PROGRAM があれば描画プログラム
	if (def.program.has) {
		AddProgram(def);
	}
}

// ===== 描画プログラムを1つ登録 =====
void ShaderPackageLoader::AddProgram(const ShaderDefinition& def) {
	ProgramEntry p;
	p.category = def.program.category;
	p.shading = def.program.shading;
	p.vsName = def.program.vsName;
	p.psName = def.name;
	p.assetPath = def.assetPath;
	p.states = def.program.states;
	p.propertyDefs = def.properties;
	// 名前（PSO・GPU の計測に出す）：決まった描き方は "ModelUnlit"、パスで引く物は Shader の名前
	p.name = p.shading ? std::format("{}{}", magic_enum::enum_name(p.category), magic_enum::enum_name(*p.shading)) : def.name;

	// --- 番号を決める（同じ (cat,shading) は上書きして番号を変えない。新しい物は末尾＝今までの番号は動かない）---
	size_t index = instance_->programs_.size();
	if (p.shading) {
		const uint32_t key = DrawKey(p.category, *p.shading);
		if (auto e = instance_->programIndexByCatShading_.find(key); e != instance_->programIndexByCatShading_.end()) {
			index = e->second;
		}
		instance_->programIndexByCatShading_[key] = index;
	}
	if (index == instance_->programs_.size()) {
		instance_->programIndexByPath_[p.assetPath] = index;
		instance_->programs_.push_back(std::move(p));
	} else {
		// 上書きした物のパスでは、もう引けないようにする（同じ番号を2つのパスが指さないように）
		instance_->programIndexByPath_.erase(instance_->programs_[index].assetPath);
		instance_->programIndexByPath_[p.assetPath] = index;
		instance_->programs_[index] = std::move(p);
	}
}


//=============================================================================
// #PROPERTIES を gMaterial の中の場所に結ぶ
//=============================================================================
ShaderPropertyLayout ShaderPackageLoader::MakePropertyLayout(const ProgramEntry& program) {
	ShaderPropertyLayout layout;
	// --- gMaterial を探す（ふつうは PS。PS が読まず VS だけが読む Shader もあるので、無ければ VS）---
	const ShaderConstantBuffer* buffer = GetShaderReflection(program.psName).FindConstantBuffer(kMaterialConstantBufferName);
	if (buffer == nullptr) {
		buffer = GetShaderReflection(program.vsName).FindConstantBuffer(kMaterialConstantBufferName);
	}
	if (buffer == nullptr) {
		if (!program.propertyDefs.empty()) {
			LogManager::Warning(std::format("{}: #PROPERTIES がありますが、シェーダーが {} を読んでいないので、値は送りません", program.assetPath, kMaterialConstantBufferName));
		}
		return layout;
	}
	layout.bufferSize = buffer->size;

	// --- #PROPERTIES の1行ずつ、場所と成分の数を決める（名前が無い・型が合わないのは .shader の書き間違いなので止める）---
	for (const ShaderPropertyDef& def : program.propertyDefs) {
		const ShaderConstantVariable* variable = buffer->FindVariable(def.name);
		MY_ASSERT_MSG(variable != nullptr, std::format("{}: #PROPERTIES の '{}' が {} の中にありません", program.assetPath, def.name, kMaterialConstantBufferName));
		uint32_t components = 1;
		MY_ASSERT_MSG(MatchPropertyType(def.type, *variable, components),
		    std::format("{}: #PROPERTIES の '{}'（{}）と、{} の中の型が合いません（Float・Range は float、Toggle は uint、Vector は float〜float4、Color は float3・float4、Texture は uint）", program.assetPath, def.name,
		        magic_enum::enum_name(def.type), kMaterialConstantBufferName));
		layout.properties.push_back({def, variable->offset, components});
	}
	return layout;
}


//=============================================================================
// .shaderの内容を見る
//=============================================================================
ShaderDefinition ShaderPackageLoader::ParseFile(const std::filesystem::path& path) {
	// バイナリモードで開く
	std::ifstream ifs(path, std::ios::binary);
	MY_ASSERT_MSG(ifs.is_open(), std::format(".shaderを開けません: {}", ToUtf8(path)));
	// ファイルを先頭から終端まで見る
	std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return ParseText(content, ToUtf8(path.filename()));
}


//=============================================================================
// .shaderの内容を解析
//=============================================================================
ShaderDefinition ShaderPackageLoader::ParseText(std::string_view text, std::string_view sourceName) {
	ShaderDefinition def;
	def.source = std::string(sourceName);
	std::vector<std::string> metaLines, programLines, propertyLines;
	std::string hlslBody;
	enum class Block { None, Meta, Program, Properties, Hlsl } block = Block::None;

	// --- 全文解析 ---
	size_t pos = 0;
	while (pos < text.size()) {
		// 空白無視
		size_t eol = text.find('\n', pos);
		std::string_view line = text.substr(pos, (eol == std::string_view::npos ? text.size() : eol) - pos);
		pos = (eol == std::string_view::npos) ? text.size() : eol + 1;
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1);
		}
		// #～を分類分け
		std::string_view t = Trim(line);
		if (t == "#META") {
			block = Block::Meta;
			continue;
		}
		if (t == "#META_END") {
			block = Block::None;
			continue;
		}
		if (t == "#PROGRAM") {
			block = Block::Program;
			continue;
		}
		if (t == "#PROGRAM_END") {
			block = Block::None;
			continue;
		}
		if (t == "#PROPERTIES") {
			block = Block::Properties;
			continue;
		}
		if (t == "#PROPERTIES_END") {
			block = Block::None;
			continue;
		}
		if (t == "#HLSL") {
			block = Block::Hlsl;
			continue;
		}
		if (t == "#HLSL_END") {
			block = Block::None;
			continue;
		}
		// 分類分け結果を1行づつ見る
		switch (block) {
		case Block::Meta:
			metaLines.emplace_back(line);
			break;
		case Block::Program:
			programLines.emplace_back(line);
			break;
		case Block::Properties:
			propertyLines.emplace_back(line);
			break;
		case Block::Hlsl:
			hlslBody += line;
			hlslBody += '\n';
			break;
		default:
			break;
		}
	}
	def.meta = ParseMeta(metaLines, sourceName);
	def.program = ParseProgram(programLines, sourceName); // 空なら has=false
	def.properties = ParseProperties(propertyLines, sourceName);
	def.hlslBody = std::move(hlslBody);
	return def;
}


//=============================================================================
// .shaderの #～ を解析
//=============================================================================
// ===== #META =====
ShaderMeta ShaderPackageLoader::ParseMeta(const std::vector<std::string>& lines, std::string_view src) {
	auto kv = ToKeyValues(lines, src);
	// 作る .hlsl の場所は .shader の場所から決める（2か所に書くと、片方だけ直したときにずれる）
	MY_ASSERT_MSG(!kv.contains("path"), std::format("{}: #META の path: はもう使いません。行を消してください（.hlsl の場所は .shader の場所から決めます）", src));
	ShaderMeta m;
	m.stage = ParseEnum<ShaderMeta::Stage>(Require(kv, "stage", src), "stage", src);

	auto e = kv.find("entry");
	if (e != kv.end()) {
		m.entry = ConvertString(e->second);
	}

	if (m.stage != ShaderMeta::Stage::Include) {
		m.profile = (kv.contains("profile")) ? ConvertString(kv.at("profile")) : DefaultProfile(m.stage);
	}

	return m;
}

// ===== #PROGRAM =====
ProgramDef ShaderPackageLoader::ParseProgram(const std::vector<std::string>& lines, std::string_view src) {
	ProgramDef p;
	// #PROGRAM 無し
	if (lines.empty()) {
		return p;
	}

	auto kv = ToKeyValues(lines, src);
	p.has = true;
	p.category = ParseEnum<DrawCategory>(Require(kv, "category", src), "category", src);
	p.vsName = Require(kv, "vs", src);
	if (auto s = kv.find("shading"); s != kv.end()) {
		p.shading = ParseEnum<ShadingType>(s->second, "shading", src);
	}
	// --- 描くときの状態の既定（書かなければ Normal・SolidBack・TestWrite）---
	if (auto b = kv.find("blend"); b != kv.end()) {
		p.states.blend = ParseEnum<BlendMode>(b->second, "blend", src);
	}
	if (auto r = kv.find("rasterizer"); r != kv.end()) {
		p.states.rasterizer = ParseEnum<RasterizerType>(r->second, "rasterizer", src);
	}
	if (auto d = kv.find("depth"); d != kv.end()) {
		p.states.depth = ParseEnum<DepthMode>(d->second, "depth", src);
	}
	return p;
}

// ===== #PROPERTIES =====
std::vector<ShaderPropertyDef> ShaderPackageLoader::ParseProperties(const std::vector<std::string>& lines, std::string_view src) {
	std::vector<ShaderPropertyDef> defs;
	for (const std::string& raw : lines) {
		const std::string_view line = Trim(raw);
		if (line.empty() || line.starts_with("//")) {
			continue;
		}
		ShaderPropertyDef def = ParsePropertyLine(line, src);
		const bool isDuplicate = std::ranges::any_of(defs, [&def](const ShaderPropertyDef& other) { return other.name == def.name; });
		MY_ASSERT_MSG(!isDuplicate, std::format("{}: #PROPERTIES の '{}' が2つあります", src, def.name));
		defs.push_back(std::move(def));
	}
	return defs;
}

// ===== #PROPERTIES の1行 =====
// 変数名: 種類 "表示名" "説明（省略できる）" 値
//   fillColor:  Color   "Fill Color" "塗りの色" 1, 0.2, 0.1, 0.4   … Color は 3つか4つ（sRGB）
//   edgeWidth:  Range   "Edge Width" 0.15, 0, 1                    … Range は 既定・下限・上限
//   pulseSpeed: Float   "Pulse Speed" 2                            … Float は1つ（書かなければ 0）
//   useTexture: Toggle  "Use Texture" 1                            … Toggle は 0 か 1
//   offset:     Vector  "Offset" 0, 0                              … Vector は 1〜4 個
//   mainTex:    Texture "Texture"                                  … Texture は値を書かない（選ばなければ白）
ShaderPropertyDef ShaderPackageLoader::ParsePropertyLine(std::string_view line, std::string_view src) {
	ShaderPropertyDef def;
	// --- 変数名 ---
	const size_t colon = line.find(':');
	MY_ASSERT_MSG(colon != std::string_view::npos, std::format("{}: #PROPERTIES に ':' がありません -> '{}'", src, std::string(line)));
	def.name = std::string(Trim(line.substr(0, colon)));
	std::string_view rest = Trim(line.substr(colon + 1));

	// --- 種類（次の空白まで）---
	const size_t typeEnd = rest.find_first_of(" \t");
	def.type = ParseEnum<ShaderPropertyType>(rest.substr(0, typeEnd), "#PROPERTIES の種類", src);
	rest = (typeEnd == std::string_view::npos) ? std::string_view() : Trim(rest.substr(typeEnd));

	// --- "表示名" と "説明"（" で囲んだ物。説明は無くてもよい）---
	const auto takeQuoted = [&rest, src](std::string& out) {
		if (!rest.starts_with('"')) {
			return false;
		}
		const size_t close = rest.find('"', 1);
		MY_ASSERT_MSG(close != std::string_view::npos, std::format("{}: #PROPERTIES の \" が閉じていません", src));
		out = std::string(rest.substr(1, close - 1));
		rest = Trim(rest.substr(close + 1));
		return true;
	};
	MY_ASSERT_MSG(takeQuoted(def.displayName), std::format("{}: #PROPERTIES の '{}' に \"表示名\" がありません", src, def.name));
	takeQuoted(def.tooltip);

	// --- 値（, か空白で区切った数）---
	std::vector<float> numbers;
	size_t pos = 0;
	while (pos < rest.size()) {
		const size_t begin = rest.find_first_not_of(", \t", pos);
		if (begin == std::string_view::npos) {
			break;
		}
		const size_t end = rest.find_first_of(", \t", begin);
		const std::string_view token = rest.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
		float value = 0.0f;
		// std::from_chars：文字列から数を読む（例外を投げず、ロケールにも左右されない）。読めた所までのポインタと、エラーを返す
		const auto [last, result] = std::from_chars(token.data(), token.data() + token.size(), value);
		MY_ASSERT_MSG(result == std::errc() && last == token.data() + token.size(), std::format("{}: #PROPERTIES の '{}' の値 '{}' が数ではありません", src, def.name, std::string(token)));
		numbers.push_back(value);
		pos = (end == std::string_view::npos) ? rest.size() : end;
	}

	// --- 種類ごとの値の数 ---
	const auto require = [&](bool isValid, std::string_view expected) {
		MY_ASSERT_MSG(isValid, std::format("{}: #PROPERTIES の '{}'（{}）の値は {} にしてください", src, def.name, magic_enum::enum_name(def.type), expected));
	};
	switch (def.type) {
	case ShaderPropertyType::Float:
		require(numbers.size() <= 1, "1つ（書かなければ 0）");
		def.defaultValue.x = numbers.empty() ? 0.0f : numbers[0];
		break;
	case ShaderPropertyType::Range:
		require(numbers.size() == 3 && numbers[1] < numbers[2], "既定・下限・上限の3つ（下限 < 上限）");
		def.defaultValue.x = numbers[0];
		def.min = numbers[1];
		def.max = numbers[2];
		break;
	case ShaderPropertyType::Toggle:
		require(numbers.size() <= 1, "0 か 1");
		def.defaultValue.x = (!numbers.empty() && numbers[0] != 0.0f) ? 1.0f : 0.0f;
		break;
	case ShaderPropertyType::Vector:
		require(numbers.size() <= 4, "1〜4 個");
		for (size_t i = 0; i < numbers.size(); ++i) {
			(&def.defaultValue.x)[i] = numbers[i]; // Vector4 は x・y・z・w が並んでいるので、番号で書ける
		}
		break;
	case ShaderPropertyType::Color:
		require(numbers.empty() || numbers.size() == 3 || numbers.size() == 4, "3つか4つ（書かなければ白）");
		def.defaultValue = {1.0f, 1.0f, 1.0f, 1.0f};
		for (size_t i = 0; i < numbers.size(); ++i) {
			(&def.defaultValue.x)[i] = numbers[i];
		}
		break;
	case ShaderPropertyType::Texture:
		require(numbers.empty(), "書かない（選ばなければ白）");
		break;
	}
	return def;
}


//=============================================================================
// ヘルパー
//=============================================================================
std::string_view ShaderPackageLoader::Trim(std::string_view s) {
	constexpr const char* ws = " \t\r\n";
	size_t b = s.find_first_not_of(ws);
	if (b == std::string_view::npos) {
		return {};
	}
	return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

std::string ShaderPackageLoader::Require(const std::unordered_map<std::string, std::string>& kv, std::string_view key, std::string_view src) {
	auto it = kv.find(std::string(key));
	MY_ASSERT_MSG(it != kv.end(), std::format("{}: 必須フィールド '{}' がありません", src, std::string(key)));
	return it->second;
}

std::wstring ShaderPackageLoader::DefaultProfile(ShaderMeta::Stage s) {
	switch (s) {
	case ShaderMeta::Stage::VS:
		return L"vs_6_0";
	case ShaderMeta::Stage::PS:
		return L"ps_6_0";
	case ShaderMeta::Stage::CS:
	default:
		return L""; // Include はコンパイルしない
	}
}

template<class E> E ShaderPackageLoader::ParseEnum(std::string_view s, std::string_view key, std::string_view src) {
	auto e = magic_enum::enum_cast<E>(s);
	MY_ASSERT_MSG(e.has_value(), std::format("{}: {}='{}' は無効な値です", src, std::string(key), std::string(s)));
	return *e;
}

std::unordered_map<std::string, std::string> ShaderPackageLoader::ToKeyValues(const std::vector<std::string>& lines, std::string_view src) {
	std::unordered_map<std::string, std::string> kv;
	for (const auto& raw : lines) {
		std::string_view line = Trim(raw);
		if (line.empty() || line.starts_with("//")) {
			continue;
		}

		size_t colon = line.find(':');
		MY_ASSERT_MSG(colon != std::string_view::npos, std::format("{}: ':' がありません -> '{}'", src, std::string(line)));
		kv[std::string(Trim(line.substr(0, colon)))] = std::string(Trim(line.substr(colon + 1)));
	}
	return kv;
}
```

### A-6 `Engine.cpp`

`LoadAll` の次の行に足す。

```cpp
	ShaderPackageLoader::LoadAll(ShaderPackageLoader::FindDataDir()); // .hlsl生成
	ShaderPackageLoader::LoadProject(kProjectShaderDir);             // ゲームの resources の下の .shader（エンジンの .hlsli を include できるように、エンジンの後）
```

### A-7 `Shader/Data` の全部の .shader から `path:` の行を消す

55 個全部にあるので、PowerShell で消す（エンジンのリポジトリの一番上で）。`path :`（空白あり）の物もある。

```powershell
Get-ChildItem project/MyEngine/Shader/Data -Recurse -Filter *.shader | ForEach-Object { $lines = Get-Content -LiteralPath $_.FullName -Encoding UTF8; $kept = $lines | Where-Object { $_ -notmatch '^\s*path\s*:' }; if ($kept.Count -ne $lines.Count) { [System.IO.File]::WriteAllLines($_.FullName, [string[]]$kept, (New-Object System.Text.UTF8Encoding $false)) } }
```

消し残しがあると、起動時に「#META の path: はもう使いません」で止まる（どのファイルかが出る）。

### A-8 BRDF の表の PS の名前を `BrdfLut` にそろえる

ほかは全部 `BrdfLut`（`BrdfLut.VS.shader`・`BrdfLutPass`・`brdfLut_`・`kBrdfLutSize`）なのに、PS のファイル名と `IBLEnvironment::GetBrdfLUT()` だけが `LUT` だった。名前は場所（ファイル名）から作るようになったので、ファイル名を直すと名前も `BrdfLutPS` になる（`BrdfLutPass.cpp` は直さなくてよい）。

大文字だけの改名なので、`git mv` で行う（Windows でもできる。エクスプローラーで名前を変えると、git が改名と分からないことがある）。エンジンのリポジトリの一番上で。

```powershell
git mv project/MyEngine/Shader/Data/IBL/BrdfLUT.PS.shader project/MyEngine/Shader/Data/IBL/BrdfLut.PS.shader
```

`MyEngine_v1.vcxproj`・`.filters` の `MyEngine\Shader\Data\IBL\BrdfLUT.PS.shader` も `BrdfLut.PS.shader` に直す。

`Graphics/IBL/IBLEnvironment.h`（使っている所は無い）

```cpp
	RenderTexture* GetBrdfLut() const { return brdfLut_.get(); }
```

### A-9 エンジンのファイルを写す手順を `MyEngine.props` に1つにまとめる

今は、エンジンの .shader と Resources を「ゲームを起動するフォルダの `MyEngine\`」へ写す手順が2か所にある（エンジンの lib のビルド後＝ゲームのソリューションから使うとき、と Sandbox のビルド後）。どちらも上書きで写すだけなので、**消した・動かした・名前を変えた（大文字だけでも）ファイルが、写し先に古いまま残る**。Release はこの写しを読むので、A-8 の改名をすると `BrdfLUT.PS.shader` の名前のまま残って止まり、フォルダを動かすと同じ名前の Shader が2つになって止まる（どちらも写しで起きた）。

ゲームが読む `MyEngine.props` に1つにまとめ、写し先が元とそろうようにする（1.3）。

`MyEngine.props`：vcpkg の `Target` の後（`</Project>` の前）に足す。

```xml
	<!-- ===== エンジンの実行に要るファイルを、ゲームを起動するフォルダへ写す ===== -->
	<!-- ゲーム（exe を作るプロジェクト）をビルドした後に、エンジンの .shader と Resources を、起動するフォルダ（ゲームのプロジェクトのフォルダ）の MyEngine\ へ写す -->
	<!-- Release とエンジンのフォルダが無い PC は、ここを読む（Debug・Development はエンジンのフォルダの .shader を直接読む。ShaderPackageLoader::FindDataDir） -->
	<!-- 写し先は、ゲームのリポジトリの .gitignore で git に入れない -->
	<PropertyGroup>
		<MyEngineRuntimeDir>$(MSBuildProjectDirectory)\MyEngine\</MyEngineRuntimeDir>
	</PropertyGroup>
	<Target Name="CopyMyEngineRuntimeFiles" AfterTargets="Build" Condition="'$(ConfigurationType)'=='Application'">
		<!-- ~~~ 写す元（** はフォルダの下を全部。%(RecursiveDir) は ** に当たったフォルダの並び）~~~ -->
		<ItemGroup>
			<MyEngineShaderFile Include="$(MyEngineRoot)MyEngine\Shader\Data\**\*" />
			<MyEngineResourceFile Include="$(MyEngineRoot)Resources\**\*" />
		</ItemGroup>

		<!-- ~~~ .shader：写し先を消してから、全部写す ~~~ -->
		<!-- ファイル名から Shader の名前を作るので、元と同じ名前にそろえる（上書きで写すと、大文字・小文字だけ変えた名前は写し先の古い方のまま残る） -->
		<!-- 消した・動かした .shader も残らない（残ると、同じ名前の Shader が2つになって起動時に止まる）。60 個ほどの小さいファイルなので、毎回写しても速い -->
		<RemoveDir Directories="$(MyEngineRuntimeDir)Shader\Data" />
		<Copy SourceFiles="@(MyEngineShaderFile)" DestinationFiles="@(MyEngineShaderFile->'$(MyEngineRuntimeDir)Shader\Data\%(RecursiveDir)%(Filename)%(Extension)')" />

		<!-- ~~~ Resources：変わった物だけ写し、元に無い物を消す（画像・モデルは大きいので、毎回は写さない）~~~ -->
		<!-- パスで読むので、大文字・小文字の違いは読むときに困らない（Windows のファイルの名前は大文字・小文字を区別しない） -->
		<Copy SourceFiles="@(MyEngineResourceFile)" DestinationFiles="@(MyEngineResourceFile->'$(MyEngineRuntimeDir)Resources\%(RecursiveDir)%(Filename)%(Extension)')" SkipUnchangedFiles="true" />
		<ItemGroup>
			<MyEngineCopiedResource Include="$(MyEngineRuntimeDir)Resources\**\*" />
			<MyEngineStaleResource Include="@(MyEngineCopiedResource)" Exclude="@(MyEngineResourceFile->'$(MyEngineRuntimeDir)Resources\%(RecursiveDir)%(Filename)%(Extension)')" />
		</ItemGroup>
		<Delete Files="@(MyEngineStaleResource)" />
	</Target>
```

`MyEngine_v1.vcxproj`：3つの構成（Debug・Release・Development）にある `<PostBuildEvent>`（`if /I "$(SolutionDir)"=="$(ProjectDir)" exit /b 0` と xcopy の2行）を、`<PostBuildEvent>` から `</PostBuildEvent>` まで消す。

`Sandbox/Sandbox.vcxproj`：ビルド後の xcopy の2行と、そのコメントを直す（dll を写す2行は残す）。

```xml
    <!-- ビルドの後：シェーダーのコンパイラ（dxcompiler.dll・dxil.dll）を exe の隣へ -->
    <!-- エンジンの .shader と Resources を起動するフォルダ（このフォルダ）の MyEngine\ へ写すのは、MyEngine.props の CopyMyEngineRuntimeFiles（ゲームと同じ） -->
    <PostBuildEvent>
      <Command>
copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"
copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxil.dll" "$(TargetDir)dxil.dll"
      </Command>
    </PostBuildEvent>
```

AL4_01 などのゲームは、エンジン（サブモジュール）を新しくすれば、この手順が自動で入る（写し先は今と同じ `project\MyEngine\`）。

### A-10 `MyEngine_v1.vcxproj`・`.filters`

`MyEngine\Graphics\Pipeline\ShaderProperty.h`（ClInclude）を足す。

### A-11 確かめること

- 3構成でビルドが通る
- 起動して、今までと同じ絵（Console に `[shader] 〜Shader/Data: 38 shaders / 12 programs`、`ShaderName: BrdfLutPS`）
- Release でビルドすると、`Sandbox/MyEngine/Shader/Data/IBL/` が `BrdfLut.PS.shader` になる。Release で起動して止まらない

## パートB マテリアル（.material・MaterialManager・Inspector）

### B-1 `ECS/ComponentUI.h`

`AssetType` の `Timeline` の下に足す。

```cpp
	Timeline,       // TimelineManager::Load の番号（.timeline）
	Shader,         // 描画プログラムの番号 + 1（ShaderPackageLoader::FindProgramHandle。#PROGRAM のある .shader）
	Material,       // MaterialManager::Load の番号（.material）
```

色の欄（`ColorField`）に、ほかの欄と同じ `FieldStyle` を足す（今までは説明を渡す口が無く、#PROPERTIES の説明が色の欄だけ出なかった）。`style` は説明（Tip）だけ効く。既定の値が `{}` なので、今までの呼び方はそのまま使える。

```cpp
	// ===== 色（0〜1。色見本を押すと色を選べる。style は説明（Tip）だけ効く）=====
	virtual void ColorField(const char* label, Vector3& rgb, FieldStyle style = {}) = 0;
	virtual void ColorField(const char* label, Vector4& rgba, FieldStyle style = {}) = 0;
```

### B-2 `ECS/ComponentUI.cpp`

include に足す。

```cpp
#include "MyEngine/Physics/PhysicMaterialManager.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Graphics/Text/FontManager.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Graphics/Animation/AnimationManager.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
```

`GetAssetPath` の `case AssetType::Timeline:` の後に足す。

```cpp
	case AssetType::Shader:
		return ShaderPackageLoader::GetProgramPath(handle);
	case AssetType::Material:
		return MaterialManager::GetPath(handle);
```

`LoadAsset` の `case AssetType::Timeline:` の後に足す。

```cpp
	case AssetType::Shader: {
		// .shader はエンジンが起動したときに全部読んである。番号は描画プログラムの番号 + 1（#PROGRAM の無い .shader は選べない）
		const uint32_t handle = ShaderPackageLoader::FindProgramHandle(path);
		if (handle == 0) {
			LogManager::Warning(std::format("#PROGRAM のある .shader ではないので、Shader に選べません（未選択として扱います）: {}", path));
		}
		return handle;
	}
	case AssetType::Material:
		return MaterialManager::Load(filePath);
```

拡張子の一覧の `kTimelineExtensions` の下に足す。

```cpp
constexpr std::array<std::string_view, 1> kShaderExtensions = {".shader"};                                    // ".shader"（#PROGRAM の無い物は、一覧に出さない）
constexpr std::array<std::string_view, 1> kMaterialExtensions = {MaterialManager::kFileExtension};             // ".material"
```

`ExtensionsOf` の `case AssetType::Timeline:` の後に足す。

```cpp
	case AssetType::Shader:
		return kShaderExtensions;
	case AssetType::Material:
		return kMaterialExtensions;
```

`SearchRootsOf`（`#ifdef USE_IMGUI` の中）を直す（コメントと、Shader の置き場）。

```cpp
// その種類のファイルを探すフォルダ（フォントは Windows のフォントのフォルダ、Shader はエンジンの .shader の置き場も足す）
std::vector<std::filesystem::path> SearchRootsOf(AssetType type) {
	std::vector<std::filesystem::path> roots(std::begin(kAssetSearchRoots), std::end(kAssetSearchRoots));
	if (type == AssetType::Font) {
		const std::vector<std::filesystem::path> systemFolders = FontManager::GetSystemFontFolders();
		roots.insert(roots.end(), systemFolders.begin(), systemFolders.end());
	}
	// Shader は、エンジンの .shader の置き場（MyEngine/Shader/Data）も探す（エンジンの既定の Shader も選べるように）
	if (type == AssetType::Shader) {
		roots.emplace_back(kShaderDataDir);
	}
	return roots;
}
```

`AssetFiles` の `IsAssetFile` で飛ばす所の後に足す。

```cpp
			if (!IsAssetFile(path, type)) {
				continue;
			}
			// Shader は、#PROGRAM のある .shader（.material が選べる物）だけ。Include・VS・ポストエフェクトの PS は出さない
			if (type == AssetType::Shader && ShaderPackageLoader::FindProgramHandle(path) == 0) {
				continue;
			}
```


`InspectorComponentUI` の `ColorField` を直す（2つある。`#ifdef USE_IMGUI` の中は説明を出し、`#else` の中は受け取るだけ）。

```cpp
	void ColorField(const char* label, Vector3& rgb, FieldStyle style) override {
		ImGui::ColorEdit3(label, &rgb.x);
		DrawTooltip(style);
	}
	void ColorField(const char* label, Vector4& rgba, FieldStyle style) override {
		ImGui::ColorEdit4(label, &rgba.x);
		DrawTooltip(style);
	}
```

```cpp
	void ColorField(const char*, Vector3&, FieldStyle) override {}
	void ColorField(const char*, Vector4&, FieldStyle) override {}
```

`ECS/ComponentSerializer.cpp` の5つのクラス（`JsonWriterUI`・`JsonReaderUI`・`RefRemapUI`・`ValueWriterUI`・`ValueListUI`）の `ColorField` に、使わない `FieldStyle` を足す（保存と読み込みには説明が要らない）。

```cpp
	void ColorField(const char* label, Vector3& rgb, FieldStyle) override { Field(label, rgb, {}); }
	void ColorField(const char* label, Vector4& rgba, FieldStyle) override { Field(label, rgba, {}); }
```

```cpp
	void ColorField(const char* label, Vector3& rgb, FieldStyle) override { ReadFloats(label, &rgb.x, 3); }
	void ColorField(const char* label, Vector4& rgba, FieldStyle) override { ReadFloats(label, &rgba.x, 4); }
```

```cpp
	void ColorField(const char*, Vector3&, FieldStyle) override {}
	void ColorField(const char*, Vector4&, FieldStyle) override {}
```

```cpp
	void ColorField(const char* label, Vector3& rgb, FieldStyle) override { WriteFloats(label, &rgb.x, 3); }
	void ColorField(const char* label, Vector4& rgba, FieldStyle) override { WriteFloats(label, &rgba.x, 4); }
```

```cpp
	void ColorField(const char* label, Vector3& rgb, FieldStyle) override { Add(label, &rgb.x, 3, true); }
	void ColorField(const char* label, Vector4& rgba, FieldStyle) override { Add(label, &rgba.x, 4, true); }
```

### B-3 `Graphics/Material/Material.h`（新しい）

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Pipeline/ShaderProperty.h"
#include "MyEngine/Math/Vector4.h"

// マテリアル（.material の中身）＝ 使う Shader ＋ その Shader の #PROPERTIES の値 ＋ 描くときの状態の上書き
// 同じファイルを選んだ物はみんな同じ見た目になる（物ごとに変えたいときは、ファイルを分ける）
// Shader はパスで指すので、手で書いた .shader でも、後でグラフが作った .shader でも同じに扱える


/// <summary>
/// マテリアルの値1つ（Shader の #PROPERTIES の1行に1つ）
/// </summary>
struct MaterialValue {
	std::string name;                                 // 変数名（#PROPERTIES の名前。.material に保存するときの名前）
	ShaderPropertyType type = ShaderPropertyType::Float; // 種類（#PROPERTIES と同じ）
	Vector4 value = {0.0f, 0.0f, 0.0f, 0.0f};         // 値（Float・Range・Toggle は x だけ。Color は sRGB）。Curve を付けたら「始まりの値」
	uint32_t texture = 0;                             // Texture の番号（TextureManager::Load。0 なら白）
	uint32_t curve = 0;                               // 付けた Curve（CurveManager::Load の番号。0 なら動かさない）。Texture には付けられない
	Vector4 endValue = {0.0f, 0.0f, 0.0f, 0.0f};      // 終わりの値（Curve の出力が 0 なら value、1 なら endValue。間は補間、外ははみ出す）

	bool operator==(const MaterialValue&) const = default;
};


/// <summary>
/// マテリアル（.material の中身）
/// </summary>
struct Material {
	uint32_t shader = 0; // 使う Shader（AssetType::Shader の番号＝描画プログラムの番号 + 1。0 は未選択）

	// --- 描くときの状態（入れた物だけ、Shader の #PROGRAM の既定を上書きする）---
	bool overrideBlend = false;                            // true：blend を使う
	BlendMode blend = BlendMode::Normal;                   // 重ね方
	bool overrideRasterizer = false;                       // true：rasterizer を使う
	RasterizerType rasterizer = RasterizerType::SolidBack; // 裏面
	bool overrideDepth = false;                            // true：depth を使う
	DepthMode depth = DepthMode::TestWrite;                // 深度

	std::vector<MaterialValue> values; // 値（Shader の #PROPERTIES の順）

	bool operator==(const Material&) const = default;
};


/// <summary>
/// 描くときの形（MaterialManager::Resolve が、マテリアルと Curve の横軸から作る）
/// </summary>
struct ResolvedMaterial {
	uint32_t programId = 0;                                // 描画プログラムの番号（PSOKey の shaderProgramID）
	DrawCategory category{};                               // Shader の #PROGRAM の category（描き手が、自分の描き方の Shader か確かめる）
	BlendMode blend = BlendMode::Normal;                   // 重ね方（上書きが無ければ Shader の既定）
	RasterizerType rasterizer = RasterizerType::SolidBack; // 裏面
	DepthMode depth = DepthMode::TestWrite;                // 深度
	std::vector<std::byte> constants;                      // gMaterial に送る中身（gMaterial の並びどおり。空なら送らない）
};
```

### B-4 `Graphics/Material/MaterialManager.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "MyEngine/Graphics/Material/Material.h"

// 前方宣言
class ComponentUI;


/// <summary>
/// マテリアルのファイル（.material）を読む・持つ・保存する（PhysicMaterialManager と同じ形）
/// <para>同じファイルを選んだ物はみんな同じ番号になるので、ファイル1つを直すと、使っている全部に効く</para>
/// <para>中身は Inspector で直す（Project ウィンドウでファイルを選ぶ → MaterialAssetEditor）。欄は Shader の #PROPERTIES から作る</para>
/// </summary>
class MaterialManager {
public:
	static constexpr std::string_view kFileExtension = ".material"; // マテリアルのファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// <para>Shader の #PROPERTIES に無い値は、警告を出して捨てる（次に保存したときにファイルから消える）</para>
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ポインタは使い捨て（Load で増えると動く）
	static const Material* Get(uint32_t handle);

	// 中身を書き換える（Inspector から。ファイルにはまだ書かない）
	static void Set(uint32_t handle, const Material& material);

	// ファイルに書く（前のファイルは .bak として1つ残る）
	static bool Save(uint32_t handle);

	/// <summary>
	/// 新しいファイルを作る（Project ウィンドウの Create → Material）。Shader は未選択
	/// <para>名前を CreateFile にしないのは、windows.h に同じ名前のマクロ（CreateFileW に置き換わる）があるため</para>
	/// </summary>
	/// <returns>作れたか（同じ名前のファイルがあれば作らない）</returns>
	static bool CreateNewFile(const std::filesystem::path& filePath);

	/// <summary>
	/// 描くときの形にする（Shader の番号・描くときの状態・gMaterial に詰めた中身）
	/// <para>Curve を付けた値は、progress（Curve の横軸。描き手が渡す 0〜1）で始まりと終わりの間を補間する</para>
	/// </summary>
	/// <param name="progress">Curve の横軸（0〜1。何を渡すかは描き手が決める）</param>
	/// <returns>描けるか（知らない番号・Shader が未選択なら false）</returns>
	static bool Resolve(uint32_t handle, float progress, ResolvedMaterial& out);

	/// <summary>
	/// 見せ方（Inspector とファイルの両方がこの並びを使う。Component の Describe〜 と同じ書き方）
	/// <para>Shader を変えたら、値を新しい Shader の #PROPERTIES に合わせ直す（名前と種類が同じ値は残す）</para>
	/// </summary>
	static void Describe(ComponentUI& ui, Material& material);

	/// <summary>
	/// 値を Shader の #PROPERTIES に合わせ直す（名前と種類が同じ値は残し、無い値は既定の値で足し、合わない値は捨てる）
	/// </summary>
	static void MatchValues(Material& material);
};
```

### B-5 `Graphics/Material/MaterialManager.cpp`（新しい）

`Describe` は Physic Material と同じく、Inspector とファイルが同じ並びを使う。ファイルから読むときは、最初に `shader` を読んだ所で `MatchValues` が値の並び（既定の値）を作り、その後の欄がファイルの値で上書きする。Shader に無い値は、ComponentSerializer が「読む所がありません」と警告する（保存すると消える）。

```cpp
#include "MaterialManager.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Math/Color.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Material"; // 警告・Inspector の見出しに出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct MaterialAsset {
	std::string path;  // ファイル（ToPathKey の形）
	Material material; // 中身
};

/// <summary>
/// MaterialManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct MaterialState {
	std::vector<MaterialAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<MaterialState> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
MaterialAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// Shader の番号の描画プログラム（未選択・知らない番号なら nullptr）
const ProgramEntry* FindProgram(uint32_t shader) {
	if (shader == 0 || shader > ShaderPackageLoader::GetProgramCount()) {
		return nullptr;
	}
	return &ShaderPackageLoader::GetProgramAt(shader - 1);
}

// 中身を JSON にする（見せ方 Describe 1つで、Inspector と同じ並び）
SceneJson ToJson(Material material) {
	SceneJson json;
	std::set<std::string> warnings;
	ComponentSerializer::WriteFields(kTypeName, [&material](ComponentUI& ui) { MaterialManager::Describe(ui, material); }, json, warnings);
	return json;
}

// ファイルに書く（前のファイルは .bak として1つ残る。Project ウィンドウには出ない）
bool WriteFile(const std::string& path, const Material& material) {
	if (!SceneSerializer::WriteFile(path, ToJson(material))) {
		LogManager::Error(std::format("Material を保存できませんでした: {}", path));
		return false;
	}
	return true;
}

//=============================================================================
// 見せ方の部品
//=============================================================================
// ===== 描くときの状態1つ（チェックを入れると、右の値で Shader の既定を上書きする）=====
// 外しているときは Shader の既定を灰色で見せる（値は変えない。保存には今の値を書く）
template<class E>
void DescribeState(ComponentUI& ui, const char* name, const char* key, bool& isOverride, E& value, E shaderDefault) {
	ui.Field(std::format("###{}Override", key).c_str(), isOverride, Tip("チェックを入れると、右の値で Shader の既定（#PROGRAM）を上書きする"));
	ui.SameLineAfterCheck();
	ui.BeginDisabled(!isOverride);
	E shown = isOverride ? value : shaderDefault;
	ui.Field(std::format("{}###{}", name, key).c_str(), shown);
	if (isOverride) {
		value = shown;
	}
	ui.EndDisabled();
}

// ===== 種類に合わせた欄1つ（値・終わりの値で共通。成分の数で形を変える）=====
void DescribeValueField(ComponentUI& ui, const std::string& label, Vector4& value, const ShaderProperty& property, FieldStyle style) {
	const ShaderPropertyDef& def = property.def;
	switch (def.type) {
	case ShaderPropertyType::Float:
		ui.Field(label.c_str(), value.x, style);
		break;
	case ShaderPropertyType::Range: {
		FieldStyle range = Range(def.min, def.max);
		range.tooltip = style.tooltip;
		ui.Field(label.c_str(), value.x, range);
		break;
	}
	case ShaderPropertyType::Toggle: {
		bool isOn = (value.x != 0.0f);
		ui.Field(label.c_str(), isOn, style);
		value.x = isOn ? 1.0f : 0.0f;
		break;
	}
	case ShaderPropertyType::Vector:
		// 成分の数に合わせた欄（使わない成分は 0 のまま）
		if (property.components == 1) {
			ui.Field(label.c_str(), value.x, style);
		} else if (property.components == 2) {
			Vector2 v = {value.x, value.y};
			ui.Field(label.c_str(), v, style);
			value.x = v.x;
			value.y = v.y;
		} else if (property.components == 3) {
			Vector3 v = {value.x, value.y, value.z};
			ui.Field(label.c_str(), v, style);
			value = {v.x, v.y, v.z, value.w};
		} else {
			ui.Field(label.c_str(), value, style);
		}
		break;
	case ShaderPropertyType::Color:
		// float3 の色は A を出さない
		if (property.components == 3) {
			Vector3 rgb = {value.x, value.y, value.z};
			ui.ColorField(label.c_str(), rgb, style);
			value = {rgb.x, rgb.y, rgb.z, 1.0f};
		} else {
			ui.ColorField(label.c_str(), value, style);
		}
		break;
	case ShaderPropertyType::Texture:
		break; // テクスチャは値ではなく番号（DescribeValue で欄を出す）
	}
}

// ===== 値1つ（値・Curve・終わりの値）=====
void DescribeValue(ComponentUI& ui, MaterialValue& value, const ShaderProperty& property) {
	const ShaderPropertyDef& def = property.def;
	const FieldStyle style = Tip(def.tooltip.empty() ? nullptr : def.tooltip.c_str());
	const std::string label = std::format("{}###{}", def.displayName, def.name); // 保存の名前は変数名
	// --- テクスチャ（Curve は付けられない）---
	if (def.type == ShaderPropertyType::Texture) {
		ui.AssetField(label.c_str(), value.texture, AssetType::Texture, style);
		return;
	}
	// --- 値（Curve を付けたら、始まりの値）---
	DescribeValueField(ui, label, value.value, property, style);
	// --- Curve と終わりの値（Curve を付けたときだけ、終わりの値を出す）---
	ui.AssetField(std::format("{} Curve###{}Curve", def.displayName, def.name).c_str(), value.curve, AssetType::Curve,
	    Tip("付けると、描き手が渡す進み具合（0〜1）を横軸にして、上の値（Curve の出力 0）から End（出力 1）へ動かす。(none) なら動かさない"));
	if (value.curve != 0) {
		DescribeValueField(ui, std::format("{} End###{}End", def.displayName, def.name), value.endValue, property, Tip("Curve の出力が 1 のときの値"));
	}
}

// ===== 送る形に詰める（float を1つずつ・uint を1つ）=====
void WriteFloats(std::vector<std::byte>& constants, uint32_t offset, const Vector4& value, uint32_t components) {
	std::memcpy(constants.data() + offset, &value.x, sizeof(float) * components); // Vector4 は x・y・z・w が並んでいる
}
void WriteUint(std::vector<std::byte>& constants, uint32_t offset, uint32_t value) { std::memcpy(constants.data() + offset, &value, sizeof(uint32_t)); }
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void MaterialManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<MaterialState>();
}

void MaterialManager::Release() { state.reset(); }


//=============================================================================
// 読み込み
//=============================================================================
uint32_t MaterialManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "MaterialManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &MaterialAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い値は、Shader の #PROPERTIES の既定のまま。Shader に無い値は警告して捨てる）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Material のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded()) {
		LogManager::Error(std::format("Material のファイルが壊れています: {}", key));
		return 0;
	}
	Material material;
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(kTypeName, [&material](ComponentUI& ui) { Describe(ui, material); }, json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(material)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded material: {} -> {}", key, handle));
	return handle;
}

const std::string& MaterialManager::GetPath(uint32_t handle) {
	const MaterialAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const Material* MaterialManager::Get(uint32_t handle) {
	const MaterialAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->material : nullptr;
}


//=============================================================================
// 書き換え・保存
//=============================================================================
void MaterialManager::Set(uint32_t handle, const Material& material) {
	if (MaterialAsset* asset = Find(handle)) {
		asset->material = material;
	}
}

bool MaterialManager::Save(uint32_t handle) {
	const MaterialAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->material);
}

bool MaterialManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), Material{});
}


//=============================================================================
// 描くときの形にする
//=============================================================================
bool MaterialManager::Resolve(uint32_t handle, float progress, ResolvedMaterial& out) {
	const MaterialAsset* asset = Find(handle);
	if (asset == nullptr) {
		return false;
	}
	const Material& material = asset->material;
	const ProgramEntry* program = FindProgram(material.shader);
	if (program == nullptr) {
		return false; // Shader が未選択
	}
	// --- Shader と、描くときの状態（上書きした物だけマテリアルの値）---
	out.programId = material.shader - 1;
	out.category = program->category;
	out.blend = material.overrideBlend ? material.blend : program->states.blend;
	out.rasterizer = material.overrideRasterizer ? material.rasterizer : program->states.rasterizer;
	out.depth = material.overrideDepth ? material.depth : program->states.depth;

	// --- gMaterial に詰める（#PROPERTIES に書かなかったメンバーは 0）---
	const ShaderPropertyLayout& layout = ShaderPackageLoader::GetPropertyLayout(out.programId);
	out.constants.assign(layout.bufferSize, std::byte{0});
	for (const MaterialValue& value : material.values) {
		const ShaderProperty* property = layout.Find(value.name);
		if (property == nullptr || property->def.type != value.type) {
			continue; // Shader と合わない（MatchValues で合わせ直しているので、ふつうは来ない）
		}
		// Curve を付けた値は、始まりと終わりの間（Curve の出力 0 で始まり、1 で終わり）
		Vector4 current = value.value;
		if (const Curve* curve = (value.curve != 0) ? CurveManager::Get(value.curve) : nullptr) {
			current = Lerp(value.value, value.endValue, curve->Evaluate(progress));
		}
		switch (value.type) {
		case ShaderPropertyType::Float:
		case ShaderPropertyType::Vector:
			WriteFloats(out.constants, property->offset, current, property->components);
			break;
		case ShaderPropertyType::Range:
			current.x = std::clamp(current.x, property->def.min, property->def.max); // Curve ではみ出しても範囲の中
			WriteFloats(out.constants, property->offset, current, 1);
			break;
		case ShaderPropertyType::Toggle:
			WriteUint(out.constants, property->offset, (current.x >= 0.5f) ? 1u : 0u); // Curve で動かしたときは半分で切り替わる
			break;
		case ShaderPropertyType::Color:
			// Inspector の色は sRGB。シェーダーはリニアで計算するので直して送る（A はそのまま）
			WriteFloats(out.constants, property->offset, SrgbToLinear(current), property->components);
			break;
		case ShaderPropertyType::Texture:
			// gTextures[] の番号（選んでいなければ白。テクスチャの番号は SRV の番号）
			WriteUint(out.constants, property->offset, (value.texture != 0) ? value.texture : TextureManager::GetWhiteTextureHandle());
			break;
		}
	}
	return true;
}


//=============================================================================
// 値を Shader の #PROPERTIES に合わせ直す
//=============================================================================
void MaterialManager::MatchValues(Material& material) {
	std::vector<MaterialValue> matched;
	if (const ProgramEntry* program = FindProgram(material.shader)) {
		for (const ShaderPropertyDef& def : program->propertyDefs) {
			// 名前と種類が同じ値があれば、そのまま使う
			const auto same = std::ranges::find_if(material.values, [&def](const MaterialValue& value) { return value.name == def.name && value.type == def.type; });
			if (same != material.values.end()) {
				matched.push_back(*same);
				continue;
			}
			// 無ければ既定の値（終わりの値も同じ値から始める）
			MaterialValue value;
			value.name = def.name;
			value.type = def.type;
			value.value = def.defaultValue;
			value.endValue = def.defaultValue;
			matched.push_back(std::move(value));
		}
	}
	material.values = std::move(matched);
}


//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void MaterialManager::Describe(ComponentUI& ui, Material& material) {
	// --- Shader（変えたら、値を新しい Shader に合わせ直す。ファイルから読むときも、ここで値の並びができる）---
	const uint32_t before = material.shader;
	ui.AssetField("Shader###shader", material.shader, AssetType::Shader, Tip("使う Shader（#PROGRAM のある .shader）。下の欄は Shader の #PROPERTIES から作る。変えると、名前と種類が同じ値だけ残る"));
	if (material.shader != before) {
		MatchValues(material);
	}
	const ProgramEntry* program = FindProgram(material.shader);
	if (program == nullptr) {
		ui.Label("Shader を選ぶと、値の欄が出る");
		return;
	}

	// --- 描くときの状態 ---
	ui.Separator("Render States");
	DescribeState(ui, "Blend", "blend", material.overrideBlend, material.blend, program->states.blend);
	DescribeState(ui, "Rasterizer", "rasterizer", material.overrideRasterizer, material.rasterizer, program->states.rasterizer);
	DescribeState(ui, "Depth", "depth", material.overrideDepth, material.depth, program->states.depth);

	// --- 値（#PROPERTIES の順）---
	const ShaderPropertyLayout& layout = ShaderPackageLoader::GetPropertyLayout(material.shader - 1);
	if (layout.properties.empty()) {
		return;
	}
	ui.Separator("Properties");
	for (MaterialValue& value : material.values) {
		if (const ShaderProperty* property = layout.Find(value.name)) {
			DescribeValue(ui, value, *property);
		}
	}
}
```

### B-6 `Editor/Inspector/MaterialAssetEditor.h`・`.cpp`（新しい）

Curve の Inspector と同じく、操作が終わったら Undo に積む（ファイルも一緒に戻す）。

```cpp
#pragma once

/// <summary>
/// .material のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>欄は MaterialManager::Describe（Shader の #PROPERTIES から作る）。ここはファイルへの書き戻しと Undo への記録だけを行う</para>
/// </summary>
class MaterialAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};
```

```cpp
#ifdef USE_IMGUI
#include "MaterialAssetEditor.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <externals/imgui/imgui.h>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"

namespace {
constexpr std::string_view kTypeName = "Material"; // Inspector の見出しに出す名前

/// <summary>
/// .material のファイルを Inspector で直す係
/// </summary>
class MaterialFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return MaterialManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return MaterialManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = MaterialManager::Load(filePath);
		const Material* current = MaterialManager::Get(handle);
		if (current == nullptr) {
			return false;
		}
		// 別のファイルへ移ったら、直している途中の記録を捨てる（前のファイルの分は、移る前に Undo に積んである）
		if (handle != handle_) {
			handle_ = handle;
			before_.reset();
		}
		// --- 写しを直して、変わっていたら書き戻す（実行中なら、次に描くときからすぐ効く）---
		Material material = *current;
		MaterialManager::Describe(ui, material);
		ui.Space();
		ui.Label("このファイルを選んだ物みんなに効く（つまむ・打つのが終わると保存。Undo で戻せる）");
		const bool isChanged = !(material == *MaterialManager::Get(handle));
		if (isChanged) {
			if (!before_) {
				before_ = *MaterialManager::Get(handle); // 操作を始める前の中身（つまんでいる間は、最初の1回だけ）
			}
			MaterialManager::Set(handle, material);
		}
		// --- 操作が終わったら Undo に積む（値はもう変わっているので Record。ファイルも一緒に戻す）---
		// どの項目も操作していなくなった（つまむ・打つのが終わった）ときが、1回の操作の終わり
		if (before_ && !ImGui::IsAnyItemActive()) {
			const auto apply = [handle](const Material& value) {
				MaterialManager::Set(handle, value);
				return MaterialManager::Save(handle);
			};
			EditorHistory::Record([apply, before = std::move(*before_)] { return apply(before); }, [apply, after = *MaterialManager::Get(handle)] { return apply(after); });
			before_.reset();
		}
		return isChanged;
	}

	void Save(const std::filesystem::path& filePath) override { MaterialManager::Save(MaterialManager::Load(filePath)); }

private:
	uint32_t handle_ = 0;            // 今直しているファイル
	std::optional<Material> before_; // 操作を始める前の中身（操作していないときは空）
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void MaterialAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<MaterialFileEditor>()); }
#endif
```

### B-7 `Systems/EngineSystems.cpp`

include に足す。

```cpp
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"
```

```cpp
#include "MyEngine/Editor/Inspector/CurveAssetEditor.h"
#include "MyEngine/Editor/Inspector/MaterialAssetEditor.h"
```

`Initialize` の `CurveManager::Initialize();` の下・`CurveAssetEditor::Initialize();` の下に足す。

```cpp
	MaterialManager::Initialize();        // マテリアル（.material）。Shader の #PROPERTIES の値と Curve を使う
```

```cpp
	MaterialAssetEditor::Initialize(); // .material のファイルを Inspector で直す
```

`Release` の `CurveManager::Release();` の上に足す。

```cpp
	MaterialManager::Release();
```

### B-8 `Editor/Windows/ProjectWindowActions.cpp`

include に足す。

```cpp
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"
```

`DrawCreateMenu` の Curve の後（Timeline の前）に足す。

```cpp
		// Material
		if (ImGui::MenuItem("Material")) {
			CreateAssetFile("NewMaterial", MaterialManager::kFileExtension, &MaterialManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（Shader と値。Decal Renderer・Hitbox Telegraph の Material で選ぶ）");
```

### B-9 `MyEngine_v1.vcxproj`・`.filters`

`MyEngine\Graphics\Material\Material.h`・`MaterialManager.h`・`MaterialManager.cpp`・`MyEngine\Editor\Inspector\MaterialAssetEditor.h`・`MaterialAssetEditor.cpp` を足す（フィルターは `MyEngine\Graphics\Material` を作る）。

### B-10 確かめること

- 3構成でビルドが通る
- Project の **Create → Material** で .material ができる。選ぶと Inspector に Shader の欄が出る
- Shader に `MyEngine/Shader/Data/Model/Unlit.PS.shader` を選ぶと Render States だけ出る（Unlit には #PROPERTIES が無い）。Shader の一覧に Include・VS・ポストエフェクトの PS が出ない

## パートC デカール（描く流れ・Decal Renderer・Receive Decals）

### C-1 `Graphics/Pipeline/RenderStates.h`

`DrawCategory` の `Polyline` の下に足す。

```cpp
	Polyline,    // UIの折れ線（メッシュで形を作り、縁はシェーダーでなめらかにする）
	Decal,       // 箱の中にある物（深度に描かれた不透明な物）に塗る。不透明な物の後に描く
```

`StencilMode` を差し替える（前にビットの分け方を足し、`MarkNoDecal`・`DecalTest` を足す）。

```cpp
// ステンシルのビットの分け方（1つの値の中を、使い道ごとにビットで分ける。分けた所しか読まない・書かない）
inline constexpr uint8_t kStencilUIMaskBits = 0x7F;  // 下の 7 ビット：UI のマスク（外にあるマスクの数。入れ子は 8 段までなので足りる）
inline constexpr uint8_t kStencilNoDecalBit = 0x80;  // 一番上のビット：デカールを受けない物（Receive Decals を外した物）を描いた所

// ステンシルの使い方
// UI のマスク：値は「外にあるマスクの数」で、参照値と同じところだけ通す（kStencilUIMaskBits だけ使う）
// デカール：受けない物が kStencilNoDecalBit を立て、デカールはそのビットの無い所だけ塗る（参照値は kStencilNoDecalBit）
enum class StencilMode {
	None,        // 使わない（マスクの外の UI・3D）
	Test,        // 値が参照値と同じ所だけ描く（マスクの中の UI）
	Increment,   // 値が参照値と同じ所を 1 増やす（マスクの形を書く。入れ子なら、外のマスクの中だけが増える）
	Decrement,   // 値が参照値と同じ所を 1 減らす（子を描き終わったマスクの形を消す）
	MarkNoDecal, // 描いた所に kStencilNoDecalBit を立てる（デカールを受けない不透明な物）
	DecalTest,   // kStencilNoDecalBit の無い所だけ描く（デカール）
};
```

### C-2 `Graphics/Pipeline/RenderStates.cpp`

`MakeDepthStencilDesc` のステンシルの所（`// --- ステンシル（UI のマスク）。` から最後まで）を差し替える。

```cpp
	// --- ステンシル（UI のマスク・デカールを受けない印）---
	if (stencil == StencilMode::None) {
		return desc;
	}
	D3D12_DEPTH_STENCILOP_DESC face{};
	face.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	face.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	face.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	desc.StencilEnable = TRUE;
	switch (stencil) {
	// UI のマスク：下の 7 ビットだけを見て、値が参照値と同じ所だけ通す
	case StencilMode::Test: // 調べるだけ（書かない）
		face.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		desc.StencilReadMask = kStencilUIMaskBits;
		desc.StencilWriteMask = 0;
		break;
	case StencilMode::Increment: // 通った所を 1 増やす。同じピクセルに2回描いても、2回目は値が違うので通らない
		face.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		face.StencilPassOp = D3D12_STENCIL_OP_INCR_SAT;
		desc.StencilReadMask = kStencilUIMaskBits;
		desc.StencilWriteMask = kStencilUIMaskBits;
		break;
	case StencilMode::Decrement: // 通った所を 1 減らす（0 で止まる）
		face.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
		face.StencilPassOp = D3D12_STENCIL_OP_DECR_SAT;
		desc.StencilReadMask = kStencilUIMaskBits;
		desc.StencilWriteMask = kStencilUIMaskBits;
		break;
	// デカールを受けない物：いつも通し、描いた所の一番上のビットを参照値（kStencilNoDecalBit）で書く
	case StencilMode::MarkNoDecal:
		face.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		face.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		desc.StencilReadMask = 0;
		desc.StencilWriteMask = kStencilNoDecalBit;
		break;
	// デカール：一番上のビットが参照値（立っている）と違う所＝立っていない所だけ通す。書かない
	case StencilMode::DecalTest:
		face.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;
		desc.StencilReadMask = kStencilNoDecalBit;
		desc.StencilWriteMask = 0;
		break;
	case StencilMode::None:
		break;
	}
	desc.FrontFace = face; // 2D・デカールの箱は裏も描くので、裏も同じにする
	desc.BackFace = face;
	return desc;
}
```

### C-3 `Graphics/Pipeline/PSOManager.cpp`

`GetTargetFormat` の `case DrawCategory::Line:` の下に足す。

```cpp
	case DrawCategory::Line:
	case DrawCategory::Decal:
		return RenderTextureFormat::HDR;
```

`GetSortKey` を差し替える（ステンシルを入れる。1.7）。

```cpp
uint64_t PSOManager::GetSortKey(const PSOKey& key) {
	// PSOKey の全部を入れる（入れていない物だけが違う2つが同じキーになると、PSO を切り替えずに描いてしまう）
	return (static_cast<uint64_t>(key.shaderProgramID) << 32) // Shader
	     | (static_cast<uint64_t>(key.stencilMode)     << 24) // StencilMode（デカールを受けない印）
	     | (static_cast<uint64_t>(key.blendMode)       << 16) // BlendMode
	     | (static_cast<uint64_t>(key.rasterizerType)  << 8)  // Rasterizer
	     |  static_cast<uint64_t>(key.depthMode);              // DepthMode
}
```

### C-4 `Graphics/Pipeline/RootSignatureManager.h`・`.cpp`

`RootBind` の `BindlessTextureCube` の下に足す。

```cpp
	Decal,                // デカールの箱（ワールド → 箱の中の行列・深度の番号）。全部のデカールで共通（PS）
	DrawData,             // 描き手が描く物ごとに送る値（形・進み具合など。中身は描き手ごとの構造体。PS）
```

`NameToRole` の表の `gTexturesCube` の下に足す。

```cpp
	    {"gDecal",                RootBind::Decal               },
	    {"gDrawData",             RootBind::DrawData            },
```

### C-5 `Graphics/Pipeline/ShaderConstants.h`

`CameraData` の最後に足す（大きさが 224 になる）。

```cpp
	FogData fog;                // 霧（3D のシェーダーの最後で、色に混ぜる。Fog.hlsli の ApplyFog）
	Matrix4x4 inverseViewProj;  // viewProj の逆行列（画面の位置と深度から、ワールドの位置へ戻す。デカールが使う）
};
static_assert(sizeof(CameraData) == 224, "HLSLのCameraと大きさが違います");
```

`ParticleData` の後に足す。

```cpp
// ===== デカールの箱（全部のデカールで共通。描き手ごとの値は gDrawData に別に送る）=====
// 箱は Transform の大きさ 1 で 1辺 1（中心が原点、-0.5〜+0.5）。箱の XZ がテクスチャの UV、Y が貼る向き（上から下へ貼る）
struct DecalData {
	Matrix4x4 worldToDecal = MakeIdentity4x4(); // ワールド → 箱の中（箱のワールド行列の逆行列。中なら x・y・z が -0.5〜+0.5）
	uint32_t depthIndex = 0;                    // 不透明な物の深度（gTextures[] の番号）
	float padA[3] = {};
};
static_assert(sizeof(DecalData) == 80, "HLSLのDecal（Decal/Decal.hlsli）と大きさが違います");
```

### C-6 `Shader/Data/Buffers/Camera.hlsli.shader`

`Camera` の最後（`heightFogMaxOpacity` の下）に足す。

```cpp
    float heightFogMaxOpacity; // 高さの霧の一番濃い所（0 なら高さの霧なし）
    // --- 画面から戻す（C++ の inverseViewProj）---
    float4x4 inverseViewProj;  // viewProj の逆行列（画面の位置と深度から、ワールドの位置へ戻す。デカールが使う）
```

### C-7 `Graphics/Renderer/SceneRenderer.h`・`.cpp`

`RenderOpaque` に描く先を渡す。

```cpp
	static void RenderOpaque(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, RenderTexture& work);
```

`RenderWorldForPost` の呼ぶ所。

```cpp
	RenderOpaque(camera, camCB, windowTitle, *work);
```

`RenderOpaque` の見出しから `if (instance_->skybox_) {` の前までを差し替える（引数に描く先を足し、`FlushOpaqueMesh` の後にデカール）。

```cpp
// ===== 不透明な物・デカール・空 =====
void SceneRenderer::RenderOpaque(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, RenderTexture& work) {
	auto* cmdList = DirectXCommon::GetCommandList();
	RenderQueue::FlushOpaqueMesh(windowTitle, camCB); // 不透明Model（関数内で計測済み）
	// --- デカール：不透明な物の深度を読んで、箱の中にある所に塗る（空・粒より先。どちらも深度を書かないので塗られない）---
	// 深度は読むだけの窓口で結び直す（同じ深度を SRV で読みながら、ステンシルで受けない物を除く）
	if (camCB != 0 && RenderQueue::HasDecal()) {
		GPU_SCOPE(cmdList, "Decal");
		work.BeginDepthRead();
		RenderQueue::FlushDecal(windowTitle, camCB, work.GetDepthSRVSlot());
		work.EndDepthRead();
	}
```

`UploadCameraCB` の `dst->viewProj = ...` の次の行に足す。

```cpp
	dst->viewProj = v * camera->GetProjectionMatrix();
	dst->inverseViewProj = Inverse(dst->viewProj); // デカールが、深度からワールドの位置へ戻すのに使う
```

### C-8 `Graphics/GPU/FrameUploadBuffer.h`

include に足す。

```cpp
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
```

`PushConstant` の後（`PushArray` の前）に足す。

```cpp
	/// <summary>
	/// 定数バッファ1個分を、バイトの並びのまま書き込み、GPUに渡す場所を返す（中身の型が実行中に決まる物。マテリアルの値など）
	/// <para>開始位置も大きさも256バイトの倍数にする（PushConstant と同じ）</para>
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS PushConstantBytes(std::span<const std::byte> bytes) {
		Allocation allocation = Allocate(AlignUp((std::max)(bytes.size(), size_t{1}), kConstantBufferAlignment), kConstantBufferAlignment);
		if (!bytes.empty()) {
			std::memcpy(allocation.cpuAddress, bytes.data(), bytes.size());
		}
		return allocation.gpuAddress;
	}
```

### C-9 `Graphics/RenderTarget/RenderTexture.h`・`.cpp`

コンストラクタのコメントを直す。

```cpp
	// 1枚作る（isDepthReadable：深度をシェーダーから読めるようにする。被写界深度が奥行きを知る・デカールが塗る所を知るのに使う。useDepth が true のときだけ）
```

`EndShaderRead` の後（`ToDXGIFormat` の前）に足す。

```cpp
	/// <summary>
	/// 深度を読みながら描く（デカール）。深度は読むだけの窓口（読むだけの DSV）で結び直し、同じ深度を SRV でも読める状態にする
	/// <para>PreDraw の後に呼ぶ（isDepthReadable で作ったときだけ）。描き終わったら EndDepthRead（深度を書く状態に戻す）</para>
	/// <para>読むだけの間は、深度もステンシルも書けない（書く PSO で描くとデバッグレイヤーに止められる）</para>
	/// </summary>
	void BeginDepthRead();

	/// <summary>
	/// BeginDepthRead の前の状態（深度を書く窓口で結んだ、書く状態）に戻す
	/// </summary>
	void EndDepthRead();
```

メンバーの `dsvHandle_` の下に足す。

```cpp
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_ = {};
	D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsvHandle_ = {}; // 深度を読むだけの窓口（isDepthReadable_ のときだけ。BeginDepthRead で結ぶ）
```

`.cpp` の `EndShaderRead` の後（`CreateResource` の大見出しの前）に足す。

```cpp
//=============================================================================
// 深度を読みながら描く
//=============================================================================
// ===== 読むだけで結ぶ =====
void RenderTexture::BeginDepthRead() {
	MY_ASSERT_MSG(isDepthReadable_, "深度を読むには、isDepthReadable で作ってください");
	// 書く状態 → 深度テストで読む ＋ PS で読む（DEPTH_READ と PIXEL_SHADER_RESOURCE は一緒にできる）
	DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandle_, false, &readOnlyDsvHandle_);
}

// ===== 書く状態に戻す =====
void RenderTexture::EndDepthRead() {
	DirectXCommon::TransitionBarrier(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtvHandle_, false, &dsvHandle_);
}
```

`CreateDSVResource` の DSV のヒープを2つにし、`SetName` の後に読むだけの窓口を作る。

```cpp
	// DSVDescriptorHeap（深度を読めるときは、読むだけの窓口の分も入れて2つ）
	dsvDescriptorHeap_ = GpuResources::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, isDepthReadable_ ? 2 : 1, false);
	dsvHandle_ = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	// DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	DirectXCommon::GetDevice()->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsvHandle_);
	dsvDescriptorHeap_->SetName(L"RenderTexture_DSVHeap");

	// ===== 深度を読むだけの窓口（DSV）=====
	// READ_ONLY を付けた DSV は、深度テスト・ステンシルテストには使えるが、書けない。その代わり、同じ深度を SRV で読みながら結んでおける
	if (isDepthReadable_) {
		readOnlyDsvHandle_ = DirectXCommon::GetCPUDescriptorHandle(dsvDescriptorHeap_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
		D3D12_DEPTH_STENCIL_VIEW_DESC readOnlyDesc = dsvDesc;
		readOnlyDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		DirectXCommon::GetDevice()->CreateDepthStencilView(depthResource_.Get(), &readOnlyDesc, readOnlyDsvHandle_);
	}
```

### C-10 `Graphics/Renderer/DrawRequest.h`

include に `<cstddef>` を足す。

```cpp
#include <array>
#include <cstddef>
#include <memory>
```

`MeshRequest` の `depthMode` の下に足す。

```cpp
	DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
	bool receivesDecals = true;                                // false：デカールを受けない（不透明な物だけ。ステンシルに印を付ける）
	StencilMode stencilMode = StencilMode::None;               // ステンシル（RenderQueue::Request が receivesDecals から決める）
```

`MeshRequest` の後（パーティクルの前）に足す。

```cpp
// ===== デカール（箱の中にある、不透明な物の深度の所に塗る）=====
// Shader・状態・gMaterial の中身は、マテリアルから作る（Renderer::DrawDecal が MaterialManager::Resolve で作る）
struct DecalRequest : DrawTarget {
	// --- 描き方（マテリアルの Shader と、描くときの状態）---
	uint32_t programId = 0;                                     // 描画プログラムの番号（category が Decal の Shader）
	BlendMode blendMode = BlendMode::Normal;                    // ブレンド設定
	RasterizerType rasterizerType = RasterizerType::SolidFront; // ラスタライザ設定（箱の裏の面を描く。カメラが箱の中にあっても塗れる）
	DepthMode depthMode = DepthMode::Disable;                   // 深度設定（深度は読むだけの窓口で結ぶので、書く TestWrite は使えない）
	int order = 0;                                              // 重なったときの順（小さい方が先＝下）

	// --- 箱 ---
	Matrix4x4 worldMatrix = MakeIdentity4x4(); // 箱のワールド行列（大きさ 1 で 1辺 1）

	// --- Shaderとのバインド情報 ---
	std::vector<std::byte> materialConstants; // gMaterial の中身（マテリアルの値。空なら結ばない）
	std::vector<std::byte> drawData;          // gDrawData の中身（描き手の値。形など。描き手が無い物は空）
};
```

### C-11 `Graphics/Renderer/RenderQueue.h`・`.cpp`

`.h`：リクエストと発行と、入れ物を足す。

```cpp
	static void Request(ParticleRequest&& req);
	static void Request(DecalRequest&& req);
```

```cpp
	static void FlushLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	// デカール（不透明な物の後。深度は読むだけの窓口で結び、depthIndex の SRV で読む）
	static void FlushDecal(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex);
	// デカールを積んだか（無ければ、深度を読む状態に切り替えなくてよい）
	static bool HasDecal();
```

```cpp
	std::vector<LineRequest> lineRequests_;
	std::vector<DecalRequest> decalRequests_;
```

`.cpp`：include に足す。

```cpp
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Graphics/Renderer/RenderContext.h"
```

`Clear` に足す。

```cpp
	instance_->lineRequests_.clear();
	instance_->decalRequests_.clear();
```

`Request(MeshRequest&&)` の `// PSOKey → SortKey` から `// リクエストに積む` の前までを差し替える（ステンシルを決めて、PSOKey に入れる）。

```cpp
	// デカールを受けない不透明な物は、描いた所のステンシルに印を付ける（半透明な物は深度を書かないので、デカールはもともと塗らない）
	req.stencilMode = (!req.receivesDecals && req.depthMode == DepthMode::TestWrite) ? StencilMode::MarkNoDecal : StencilMode::None;
	// PSOKey → SortKey
	PSOKey psoKey = PSOManager::GetPSOKey(req.category, req.shadingType, req.blendMode, req.rasterizerType, req.depthMode, req.stencilMode);
	req.sortKey = PSOManager::GetSortKey(psoKey);
```

`Request(LineRequest&&)` の後に足す。

```cpp
// ===== デカールのリクエスト =====
void RenderQueue::Request(DecalRequest&& req) {
	// 深度は読むだけの窓口で結んで描くので、書くことはできない（書く PSO で描くとデバッグレイヤーに止められる）
	MY_ASSERT_MSG(req.depthMode != DepthMode::TestWrite, "デカールの深度は TestNoWrite か Disable にしてください（深度を読みながら描くので、書けません）");
	instance_->decalRequests_.push_back(std::move(req));
}
```

`FlushMeshList`：`BindSRVHeap` の後にステンシルの参照値を足し、PSO の所の `GetPSOKey` に `req.stencilMode` を足す。

```cpp
	// ステンシルの参照値（デカールを受けない物が書く印。印を書かない PSO はステンシルを使わないので、全部に同じ値でよい）
	cmdList->OMSetStencilRef(kStencilNoDecalBit);
```

```cpp
			PSOKey psoKey = PSOManager::GetPSOKey(req.category, req.shadingType, req.blendMode, req.rasterizerType, req.depthMode, req.stencilMode);
```

`FlushTransparentMesh` の後に足す。

```cpp
//=============================================================================
// Decal
//=============================================================================
// ===== 積んだか（無ければ、深度を読む状態に切り替えなくてよい）=====
bool RenderQueue::HasDecal() { return !instance_->decalRequests_.empty(); }

// ===== 発行（Order の順。同じ Order は Shader・状態の順にまとめる）=====
void RenderQueue::FlushDecal(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex) {
	auto* cmdList = DirectXCommon::GetCommandList();
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	// ステンシルの参照値（受けない物の印。印の無い所だけ塗る）
	cmdList->OMSetStencilRef(kStencilNoDecalBit);

	// --- 並べる：重なりの順（Order）を先に守り、同じ Order の中は PSO が同じ物を続ける ---
	auto& decals = instance_->decalRequests_;
	const auto psoKeyOf = [](const DecalRequest& req) { return PSOKey{req.programId, req.blendMode, req.rasterizerType, req.depthMode, StencilMode::DecalTest}; };
	// std::stable_sort：同じ順の物は、積んだ順のまま並べる（毎フレーム、重なりの上下が入れ替わらないように）
	std::stable_sort(decals.begin(), decals.end(), [&psoKeyOf](const DecalRequest& a, const DecalRequest& b) {
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return PSOManager::GetSortKey(psoKeyOf(a)) < PSOManager::GetSortKey(psoKeyOf(b));
	});

	uint32_t currentShaderProg = UINT32_MAX;
	uint64_t currentKey = UINT64_MAX;
	for (const DecalRequest& req : decals) {
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// --- シェーダー(RootSignature)が変わったとき：カメラ・テクスチャを結び直す ---
		if (req.programId != currentShaderProg) {
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(req.programId);
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			if (auto camSlot = rs.slotOf.find(RootBind::Camera); cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			if (auto texSlot = rs.slotOf.find(RootBind::BindlessTexture); texSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
			}
			currentShaderProg = req.programId;
			currentKey = UINT64_MAX; // RootSignature を変えたら PSO も結び直す
		}
		// --- PSO が変わったとき ---
		const PSOKey psoKey = psoKeyOf(req);
		const uint64_t sortKey = PSOManager::GetSortKey(psoKey);
		if (sortKey != currentKey) {
			cmdList->SetPipelineState(PSOManager::GetPSO(psoKey).Get());
			currentKey = sortKey;
		}
		RenderContext::DrawDecal(req, depthIndex);
	}
}
```

### C-12 `Graphics/Renderer/RenderContext.h`・`.cpp`

`.h`：`DrawMesh` の後に足す。

```cpp
	/// <summary>
	/// デカールを1つ描画する（1辺 1 の箱を描き、PS が深度から塗る所を決める）
	/// </summary>
	/// <param name="depthIndex">不透明な物の深度の SRV の番号（gDecal に入れる）</param>
	static void DrawDecal(const DecalRequest& req, uint32_t depthIndex);
```

`.cpp`：`DrawMesh` の後（パーティクルの前）に足す。

```cpp
//=============================================================================
// デカール描画
//=============================================================================
void RenderContext::DrawDecal(const DecalRequest& req, uint32_t depthIndex) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== 箱（1辺 1 の立方体。角 i の x・y・z は、i のビット 1・2・4 が立っていれば +0.5、無ければ -0.5）=====
	// 三角形は外から見て時計回り（表）。描くときは表を捨てて裏を描く（SolidFront）ので、カメラが箱の中にあっても塗れる
	static const std::array<Vertex3dData, 8> kCorners = [] {
		std::array<Vertex3dData, 8> corners{};
		for (size_t i = 0; i < corners.size(); ++i) {
			corners[i].position = {(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f};
		}
		return corners;
	}();
	static constexpr std::array<uint32_t, 36> kIndices = {
	    0, 2, 1, 2, 3, 1, // -Z
	    4, 5, 6, 5, 7, 6, // +Z
	    0, 4, 2, 4, 6, 2, // -X
	    1, 3, 5, 3, 7, 5, // +X
	    0, 1, 4, 1, 5, 4, // -Y
	    2, 6, 3, 6, 7, 3, // +Y
	};
	FrameUploadBuffer::Allocation vertices = upload.PushArray(kCorners.data(), kCorners.size());
	FrameUploadBuffer::Allocation indices = upload.PushArray(kIndices.data(), kIndices.size());
	D3D12_VERTEX_BUFFER_VIEW vbv{vertices.gpuAddress, static_cast<UINT>(vertices.size), sizeof(Vertex3dData)};
	D3D12_INDEX_BUFFER_VIEW ibv{indices.gpuAddress, static_cast<UINT>(indices.size), DXGI_FORMAT_R32_UINT};
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== 定数（シェーダーが使っている物だけ結ぶ）=====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(req.programId);
	// 箱の位置（VS）
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::ObjectTransform), upload.PushConstant(MakeObjectTransform(req.worldMatrix)));
	// 箱の中へ戻す行列と深度（PS。全部のデカールで共通）
	DecalData decal;
	decal.worldToDecal = Inverse(req.worldMatrix);
	decal.depthIndex = depthIndex;
	BindConstantIfUsed(cmdList, rs, RootBind::Decal, upload.PushConstant(decal));
	// マテリアルの値（gMaterial の並びに詰めた物）
	if (!req.materialConstants.empty()) {
		BindConstantIfUsed(cmdList, rs, RootBind::Material, upload.PushConstantBytes(req.materialConstants));
	}
	// 描き手の値（形など。読む Shader か・大きさが合うかは Renderer::DrawDecal で確かめてある）
	if (!req.drawData.empty()) {
		BindConstantIfUsed(cmdList, rs, RootBind::DrawData, upload.PushConstantBytes(req.drawData));
	}

	// ===== DrawCall =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(static_cast<UINT>(kIndices.size()), 1, 0, 0, 0);
}
```

### C-13 `Graphics/Renderer/Renderer.h`・`.cpp`

`.h`：include に `<cstddef>` を足す。

```cpp
#include <array>
#include <cstddef>
#include <span>
```

`ModelConfig`・`SphereConfig`・`MeshConfig`・`OBBConfig` の最後に足す（4つとも同じ1行。`ModelConfig` は `billboard` の下、ほかの3つは `camera` の下）。

```cpp
		bool receivesDecals = true;                                // false：デカールを受けない（不透明なときだけ効く）
```

`LineListConfig` の後に足す。

```cpp
	//=============================================================================
	// Decal
	//=============================================================================
	// --- デカール（箱の中にある不透明な物に、マテリアルで塗る）---
	// 箱は大きさ 1 で 1辺 1（worldMatrix の大きさが箱の大きさ）。上（+Y）から下へ貼る。箱の XZ がテクスチャの UV
	struct DecalConfig : DrawTarget {
		uint32_t material = 0;                     // マテリアル（MaterialManager::Load の番号。Shader の #PROGRAM の category が Decal の物）
		float progress = 0.0f;                     // マテリアルの Curve の横軸（0〜1。描き手が決める）
		Matrix4x4 worldMatrix = MakeIdentity4x4(); // 箱のワールド行列
		int order = 0;                             // 重なったときの順（小さい方が先＝下）
	};
```

`DrawLines` の宣言の後に足す。

```cpp
	/// <summary>
	/// デカールを描く（不透明な物の後に、その深度の所へ塗る）
	/// <para>drawData は描き手の値（Shader の gDrawData と同じ並びの構造体のバイト）。Shader が gDrawData を読むなら、大きさが合わないと止める</para>
	/// <para>マテリアルが無い・Shader が Decal の物でないときは描かない（警告は1回だけ）</para>
	/// </summary>
	static void DrawDecal(const DecalConfig& config, std::span<const std::byte> drawData = {});
```

`.cpp`：include に足す。

```cpp
#include <algorithm>
#include <unordered_set>
```

```cpp
#include "MyEngine/Graphics/Material/MaterialManager.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
```

`PushMesh` の `req.depthMode = config.depthMode;` の後に足す。

```cpp
	// --- デカールを受けるか：Config が receivesDecals を持っていれば、その値（持たない形は受ける）---
	if constexpr (requires { config.receivesDecals; }) {
		req.receivesDecals = config.receivesDecals;
	}
```

`DrawModel` の `req.depthMode = config.depthMode;` の後に足す。

```cpp
			req.receivesDecals = config.receivesDecals;
```

ファイルの最後に足す。

```cpp
//=============================================================================
// デカール
//=============================================================================
void Renderer::DrawDecal(const DecalConfig& config, std::span<const std::byte> drawData) {
	// --- マテリアルを描く形にする（Curve を付けた値は、進み具合で動かす）---
	ResolvedMaterial material;
	if (!MaterialManager::Resolve(config.material, config.progress, material)) {
		return; // マテリアルが無い・Shader が未選択
	}
	// --- 描けないマテリアルは、1回だけ警告して描かない（Inspector で選び間違えても止めない。毎フレーム警告しない）---
	static std::unordered_set<uint32_t> warnedMaterials; // 警告を出したマテリアルの番号
	const auto skip = [&config](std::string_view reason) {
		if (warnedMaterials.insert(config.material).second) {
			LogManager::Warning(std::format("{}: {}", MaterialManager::GetPath(config.material), reason));
		}
	};
	// Model の Shader などを選ぶと、頂点の形・描く所が合わない
	if (material.category != DrawCategory::Decal) {
		skip("Shader が Decal の物ではないので、デカールに使えません（#PROGRAM が category: Decal の .shader を選んでください）");
		return;
	}
	// 深度は読むだけの窓口で結んで描くので、書けない
	if (material.depth == DepthMode::TestWrite) {
		skip("デカールの Depth は TestNoWrite か Disable にしてください（深度を読みながら描くので、書けません）");
		return;
	}
	// 描き手の値（gDrawData）を読む Shader には、その値を送る描き手が要る
	const ProgramEntry& program = ShaderPackageLoader::GetProgramAt(material.programId);
	const ShaderConstantBuffer* drawDataBuffer = ShaderPackageLoader::GetShaderReflection(program.psName).FindConstantBuffer(kDrawDataConstantBufferName);
	if (drawDataBuffer != nullptr && drawData.empty()) {
		skip(std::format("この Shader は描き手の値（{}）を読みます。その値を送る描き手（Hitbox Telegraph など）で使ってください", kDrawDataConstantBufferName));
		return;
	}
	// 描き手の構造体と HLSL の構造体の大きさが違うのは、写し間違い（コードを直すまで正しく描けない）なので止める
	if (drawDataBuffer != nullptr) {
		constexpr size_t kRegisterBytes = 16; // 定数バッファは 16 バイトずつ
		const size_t alignedSize = (drawData.size() + kRegisterBytes - 1) / kRegisterBytes * kRegisterBytes;
		MY_ASSERT_MSG(alignedSize == drawDataBuffer->size,
		    std::format("{}: 描き手の値の大きさが違います（C++ {} バイト / HLSL の {} {} バイト）。構造体の並びをそろえてください", program.assetPath, drawData.size(), kDrawDataConstantBufferName, drawDataBuffer->size));
	}

	// --- リクエスト ---
	DecalRequest req;
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
	req.programId = material.programId;
	req.blendMode = material.blend;
	req.rasterizerType = material.rasterizer;
	req.depthMode = material.depth;
	req.order = config.order;
	req.worldMatrix = config.worldMatrix;
	req.materialConstants = std::move(material.constants);
	if (drawDataBuffer != nullptr) {
		req.drawData.assign(drawData.begin(), drawData.end()); // 読まない Shader には送らない
	}
	RenderQueue::Request(std::move(req));
}
```

### C-14 `Shader/Data/Decal/Decal.hlsli.shader`・`DecalTexture.PS.shader`（新しい）

フォルダ `Decal` を作る。

```cpp
#META
stage: Include
#META_END

#HLSL
// デカールの PS が使う共通の物（全部のデカールの Shader が include する）
// 箱は Transform の大きさ 1 で 1辺 1。PS は、そのピクセルに描かれた不透明な物の位置を深度から戻し、箱の中なら塗る
// "Buffers/Camera.hlsli" と "Resources/Texture.hlsli" をここで include するので、デカールの Shader では include しない（2回 include すると、同じ名前が2つになって止まる）
#include "Buffers/Camera.hlsli"
#include "Resources/Texture.hlsli"

// 箱（C++ の DecalData と同じ並び）
struct Decal
{
    float4x4 worldToDecal; // ワールド → 箱の中（中なら x・y・z が -0.5〜+0.5）
    uint32_t depthIndex;   // 不透明な物の深度（gTextures[] の番号）
    float3 padA;
};
ConstantBuffer<Decal> gDecal : register(b23);

// そのピクセルに描かれた不透明な物の位置
struct DecalSurface
{
    float32_t3 world; // ワールドの位置
    float32_t3 local; // 箱の中の位置（中なら -0.5〜+0.5。XZ が貼る面、Y が貼る向き）
};

// 深度から、そのピクセルに描かれた物の位置を戻す（svPosition は PS の SV_POSITION。xy がピクセルの真ん中）
DecalSurface GetDecalSurface(float32_t4 svPosition)
{
    // --- そのピクセルの深度（0〜1。一番手前の不透明な物）---
    int32_t2 pixel = int32_t2(svPosition.xy);
    float depth = gTextures[gDecal.depthIndex].Load(int32_t3(pixel, 0)).r;
    // --- 画面の位置 → 正規化座標（x・y は -1〜+1。y は上が +、画面のピクセルは下が +）---
    uint32_t width, height;
    gTextures[gDecal.depthIndex].GetDimensions(width, height);
    float32_t2 uv = svPosition.xy / float32_t2(width, height);
    float32_t4 ndc = float32_t4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    // --- 正規化座標 → ワールド（viewProj の逆行列を掛けて、w で割る）---
    float32_t4 world = mul(ndc, gCamera.inverseViewProj);
    DecalSurface surface;
    surface.world = world.xyz / world.w;
    surface.local = mul(float32_t4(surface.world, 1.0f), gDecal.worldToDecal).xyz;
    return surface;
}

// 箱の外なら捨てる（clip：引数のどれか1つでもマイナスなら、このピクセルを描かない）
void ClipOutsideDecal(float32_t3 local)
{
    clip(0.5f - abs(local));
}
#HLSL_END
```

```cpp
#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category:   Decal
vs:         Object3dVS
blend:      Normal
rasterizer: SolidFront
depth:      Disable
#PROGRAM_END

#PROPERTIES
color:   Color   "Color"   "色（テクスチャの色に掛ける。A の分だけ透ける）" 1, 1, 1, 1
texture: Texture "Texture" "貼る画像（箱の XZ が UV。上が箱の +Z。選ばなければ白）"
#PROPERTIES_END

#HLSL
// デカールの既定の Shader：箱の中にある不透明な物に、画像を上から貼る（Decal Renderer の既定のマテリアル）
#include "Model/Object3d.hlsli"
#include "Decal/Decal.hlsli"
#include "Fog/Fog.hlsli"

struct Material
{
    float32_t4 color; // 色（リニア）
    uint32_t texture; // 貼る画像（gTextures[] の番号）
};
ConstantBuffer<Material> gMaterial : register(b20);

PixelShaderOutput main(VertexShaderOutput input)
{
    DecalSurface surface = GetDecalSurface(input.position);
    ClipOutsideDecal(surface.local);

    // 箱の XZ を UV に（左が 0、上（+Z）が 0）
    // 深度の段差の所で UV が飛ぶので、ミップを自動で選ばない（SampleLevel の 0）。縁が端の色で伸びないように CLAMP
    float32_t2 uv = float32_t2(surface.local.x + 0.5f, 0.5f - surface.local.z);
    float32_t4 color = gMaterial.color * gTextures[gMaterial.texture].SampleLevel(gSamplerClamp, uv, 0.0f);
    color.rgb = ApplyFog(color.rgb, surface.world); // 下の物と同じだけ霧を掛ける（掛けないと、霧の中でデカールだけくっきり見える）

    PixelShaderOutput output;
    output.color = color;
    return output;
}
#HLSL_END
```

### C-15 `Components/Rendering3D/DecalRendererComponent.h`・`Systems/Rendering3D/DecalRenderSystem.h`・`.cpp`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

// 箱の中にある不透明な物（床・壁・段差）に、マテリアルで塗る。Entity に付ける Component（カテゴリ：Rendering3D）。Unity の Decal Projector に当たる
// 箱は Transform の大きさ（Scale 1 で 1辺 1。中心が Entity の位置）。上（ローカルの +Y）から下へ貼り、XZ がテクスチャの UV
// 描くのは DecalRenderSystem。デカールを受けない物は、Model Renderer・Primitive Renderer の Receive Decals を外す

/// <summary>
/// デカールの設定
/// </summary>
struct DecalRendererComponent {
	bool enabled = true;   // false の間は描かない
	uint32_t material = 0; // マテリアル（MaterialManager::Load の番号。Shader は category: Decal の物。0 ならエンジンの既定＝白い画像を貼る）
	float progress = 0.0f; // マテリアルの Curve の横軸（0〜1。Script・Timeline から動かす）
	int order = 0;         // 重なったときの順（大きい方が上に塗られる）
};

static_assert(std::is_trivially_copyable_v<DecalRendererComponent>);
```

```cpp
#pragma once
#include <cstdint>

// 前方宣言
class Camera;

/// <summary>
/// DecalRendererComponent を持つ Entity の箱に、マテリアルで塗る（Renderer::DrawDecal）
/// <para>マテリアルを選んでいなければ、エンジンの既定のマテリアル（白い画像を貼る）で描く</para>
/// </summary>
class DecalRenderSystem {
public:
	// DecalRendererComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 描画リクエストを積む（EngineSystems::Draw から。停止中も描く）
	/// </summary>
	static void Draw();

	/// <summary>
	/// 箱の形を線で描く（エディタの Scene ビューだけ）
	/// </summary>
	static void DrawGizmos(Camera* camera);

	// エンジンの既定のデカールのマテリアル（初めて呼んだときに読む。シェーダーを読んだ後に呼ぶ）
	static uint32_t GetDefaultMaterial();
};
```

```cpp
#include "DecalRenderSystem.h"

#include <array>
#include <memory>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/Rendering3D/DecalRendererComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"

namespace {
constexpr const char* kDefaultMaterialPath = "MyEngine/Resources/Materials/Decal.material"; // マテリアルを選んでいないときに使う（エンジンの Resources。白い画像を貼る）
constexpr Vector4 kGizmoColor = {0.4f, 0.8f, 1.0f, 0.8f}; // 箱のギズモの色（水色）
constexpr float kNoFadeStart = 10000.0f;                   // 距離でフェードさせないための値（Collider のギズモと同じ）
constexpr float kNoFadeEnd = 20000.0f;
constexpr float kUnitHalfSize = 0.5f;                      // Scale が 1 のときの、箱の中心から面までの長さ

/// <summary>
/// DecalRenderSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct DecalRenderState {
	uint32_t defaultMaterial = 0;         // 既定のマテリアルの番号（初めて使うときに読む）
	bool isDefaultMaterialLoaded = false; // 読みに行ったか（無いファイルを毎フレーム読みに行かないように）
	Renderer::LineListConfig gizmoLines;  // 箱のギズモ（毎フレーム作り直す。入れ物は使い回す）
};
std::unique_ptr<DecalRenderState> state;

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeDecalRenderer(ComponentUI& ui, DecalRendererComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	ui.AssetField("Material###material", value.material, AssetType::Material,
	    Tip("塗るマテリアル（.material。Shader は #PROGRAM が category: Decal の物。Project の Create → Material で作る）。(none) なら白い画像を貼る"));
	ui.Field("Progress###progress", value.progress, Range(0.0f, 1.0f).Tip("マテリアルの Curve の横軸（0〜1。Script・Timeline から動かす）"));
	ui.Field("Order###order", value.order, Tip("重なったときの順（大きい方が上に塗られる）"));
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void DecalRenderSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<DecalRenderState>();
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;
	state->gizmoLines.layer = DrawLayer::EditorGizmo; // Scene ビューだけ（ゲームの絵に入れない）

	// Decal Renderer の置き場所・保存・Inspector を登録する（見せ方は DescribeDecalRenderer 1つ）
	EntityManager::RegisterComponent<DecalRendererComponent>();
	ComponentSerializer::Register<DecalRendererComponent>("Decal Renderer", &DescribeDecalRenderer);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<DecalRendererComponent>>("Decal Renderer", "Rendering3D", &DescribeDecalRenderer));
}

void DecalRenderSystem::Release() { state.reset(); }


//=============================================================================
// 既定のマテリアル
//=============================================================================
uint32_t DecalRenderSystem::GetDefaultMaterial() {
	MY_ASSERT_MSG(state != nullptr, "DecalRenderSystem::Initialize()を先に呼んでください");
	if (!state->isDefaultMaterialLoaded) {
		state->isDefaultMaterialLoaded = true;
		state->defaultMaterial = LoadAsset(AssetType::Material, kDefaultMaterialPath); // 無ければ警告が出て 0（描かない）
	}
	return state->defaultMaterial;
}


//=============================================================================
// 描画
//=============================================================================
void DecalRenderSystem::Draw() {
	EntityManager::ForEach<DecalRendererComponent>([](Handle<Entity> entity, const DecalRendererComponent& decal) {
		if (!decal.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		Renderer::DecalConfig config;
		config.material = (decal.material != 0) ? decal.material : GetDefaultMaterial();
		config.progress = decal.progress;
		config.worldMatrix = transform->worldMatrix; // 箱は大きさ 1 で 1辺 1 なので、ワールド行列がそのまま箱になる
		config.order = decal.order;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		Renderer::DrawDecal(config);
	});
}


//=============================================================================
// ギズモ（箱の 12 本の辺）
//=============================================================================
void DecalRenderSystem::DrawGizmos(Camera* camera) {
	MY_ASSERT_MSG(state != nullptr, "DecalRenderSystem::Initialize()を先に呼んでください");
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	EntityManager::ForEach<DecalRendererComponent>([&gizmo](Handle<Entity> entity, const DecalRendererComponent& decal) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (!decal.enabled || transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		// 角 i の x・y・z は、i のビット 1・2・4 が立っていれば +0.5、無ければ -0.5
		std::array<Vector3, 8> corners{};
		for (size_t i = 0; i < corners.size(); ++i) {
			const Vector3 local = {(i & 1) ? kUnitHalfSize : -kUnitHalfSize, (i & 2) ? kUnitHalfSize : -kUnitHalfSize, (i & 4) ? kUnitHalfSize : -kUnitHalfSize};
			corners[i] = TransformPoint(local, transform->worldMatrix);
		}
		// 辺は、1つのビットだけが違う2つの角を結ぶ（角ごとに、立っていないビットを立てた角へ）
		for (size_t i = 0; i < corners.size(); ++i) {
			for (size_t bit : {1u, 2u, 4u}) {
				if ((i & bit) == 0) {
					gizmo.lines.push_back({corners[i], corners[i | bit], kGizmoColor});
				}
			}
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}
```

### C-16 Receive Decals（Model Renderer・Primitive Renderer）

`Components/Rendering3D/ModelRendererComponent.h`：`billboard` の下に足す。

```cpp
	bool receivesDecals = true;                    // false：デカールを受けない（箱の中にあっても塗られない。不透明なときだけ効く）
```

`Components/EngineComponents.cpp`（`DescribeModelRenderer`）：`billboard` の下に足す。

```cpp
	ui.Field("receivesDecals", value.receivesDecals); // デカールを受けるか（前のファイルには無い＝受ける）
```

`Editor/Inspector/ModelRendererEditor.cpp`：`Billboard` の下に足す。

```cpp
	ImGui::Checkbox("Receive Decals", &render.receivesDecals);
	ImGui::SetItemTooltip("外すと、デカールの箱の中にあっても、このモデルには塗られない（不透明なときだけ効く）");
```

`Systems/Rendering3D/ModelRenderSystem.cpp`：`config.billboard = ...` の下に足す。

```cpp
		config.receivesDecals = render.receivesDecals;
```

`Components/Rendering3D/PrimitiveRendererComponent.h`（`PrimitiveMaterial`）：`params` の下に足す。

```cpp
	bool receivesDecals = true;                         // false：デカールを受けない（箱の中にあっても塗られない。Opaque のときだけ効く）
```

`Systems/Rendering3D/PrimitiveRenderSystem.cpp`（`DescribeMaterial`）：最後（`Emissive` の下）に足す。

```cpp
	// ===== デカール =====
	ui.Field(label("Receive Decals", "receivesDecals").c_str(), material.receivesDecals, Tip("外すと、デカールの箱の中にあっても、この形には塗られない（Opaque のときだけ効く）"));
```

`Systems/Rendering3D/PrimitiveRenderSystem.h`（`ApplyMaterial`）：`config.env = environment;` の下に足す。

```cpp
		config.receivesDecals = material.receivesDecals;
```

### C-17 `Systems/EngineSystems.cpp`

```cpp
#include "MyEngine/Systems/Rendering3D/ArrowStripSystem.h"
#include "MyEngine/Systems/Rendering3D/DecalRenderSystem.h"
```

`Initialize` の `ArrowStripSystem::Initialize();` の下、`Release` の `ArrowStripSystem::Release();` の下に足す。

```cpp
	DecalRenderSystem::Initialize();      // Decal Renderer
```

```cpp
	DecalRenderSystem::Release();
```

`Draw` の `ArrowStripSystem::Draw(...)` の下と、ギズモの `WindZoneSystem::DrawGizmos(...)` の下に足す。

```cpp
	DecalRenderSystem::Draw();                                // 箱の中の床・壁に塗る（Decal Renderer）の描画
```

```cpp
	DecalRenderSystem::DrawGizmos(gameCamera); // デカールの箱（Scene ビューだけ）
```

### C-18 `Resources/Materials/Decal.material`（新しい）

エンジンの `project/Resources` の下（ゲームを起動すると `MyEngine/Resources` に写る）。

```json
{
	"shader": "MyEngine/Shader/Data/Decal/DecalTexture.PS.shader",
	"blendOverride": false,
	"blend": "Normal",
	"rasterizerOverride": false,
	"rasterizer": "SolidFront",
	"depthOverride": false,
	"depth": "Disable",
	"color": [1.0, 1.0, 1.0, 1.0],
	"colorCurve": "",
	"texture": ""
}
```

### C-19 `MyEngine_v1.vcxproj`・`.filters`

`MyEngine\Components\Rendering3D\DecalRendererComponent.h`・`MyEngine\Systems\Rendering3D\DecalRenderSystem.h`・`.cpp` と、`None` の `MyEngine\Shader\Data\Decal\Decal.hlsli.shader`・`DecalTexture.PS.shader` を足す（フィルター `MyEngine\Shader\Data\Decal` を作る）。

### C-20 確かめること

- 3構成でビルドが通る
- 空の Entity に Decal Renderer を付け、Scale を (4, 2, 4) にすると、床が白く塗られる。Scene ビューに水色の箱。Material に E-1 の `CheckerDecal.material` を選ぶと市松模様。箱を床の上の箱に重ねると、上の面と横の面にも塗られる

## パートD 床の予告（Hitbox Telegraph）

### D-1 `Shader/Data/Decal/HitShape.hlsli.shader`・`HitboxTelegraph.hlsli.shader`・`HitboxTelegraph.PS.shader`（新しい）

`HitShape.hlsli` は C++ の `HitShape.cpp` と同じ式（1.8）。片方を直したら、もう片方も同じに直す。

```cpp
#META
stage: Include
#META_END

#HLSL
// 攻撃の形の、床の面（ローカルの XZ）の中での符号付き距離（形の外なら縁までの距離、中ならマイナス）
// C++ の Math/Geometry/HitShape.cpp（CalcSectorDistance・CalcLineDistance・WorldHitShape::CalcSignedDistance2D）と同じ式
// 当たり・ジャスト判定・ギズモと同じ式で塗るので、塗った所と当たる所がずれない。どちらかを直したら、もう片方も同じに直す

static const float kHitShapePi = 3.14159265f; // 円周率（円は「開き 360 度の扇」＝開きの半分が π）

// 形の種類（C++ の HitShapeType と同じ番号。床に置く形だけ）
static const uint32_t kHitShapeCircle = 0; // 円（内側の半径でドーナツ）
static const uint32_t kHitShapeSector = 1; // 扇（前 +Z を真ん中に開く）
static const uint32_t kHitShapeLine = 2;   // 線（幅 width の長方形。z = 0 から length まで）

// ===== 円・ドーナツ・扇・扇のドーナツ（どれも「半径 ri〜R、前 +Z から ±a の範囲」の形）=====
// 左右が同じ形なので、x を正の側にそろえて考える。q＝(|x|, z)、φ＝q の +Z からの角度
//   φ ≦ a（開きの中）：外側の弧・内側の弧・横の辺のうち、一番近い物までの距離
//   φ ＞ a（開きの外）：横の辺（ri・e 〜 R・e の線分。e は開きの端の向き）までの距離
float CalcSectorDistance(float32_t2 p, float outerRadius, float innerRadius, float halfAngle)
{
    float32_t2 q = float32_t2(abs(p.x), p.y);
    float radial = length(q);
    // 弧だけを見た距離（中ならマイナス）。内側の弧は、内側の半径があるときだけ
    float radialDistance = (innerRadius > 0.0f) ? max(radial - outerRadius, innerRadius - radial) : radial - outerRadius;
    if (halfAngle >= kHitShapePi)
    {
        return radialDistance; // 円・ドーナツ（横の辺が無い）
    }
    // 横の辺（開きの端の向き e に、内側の半径から外側の半径までの線分）までの距離
    float32_t2 edge = float32_t2(sin(halfAngle), cos(halfAngle));
    float along = clamp(dot(q, edge), innerRadius, outerRadius);
    float edgeDistance = length(q - edge * along);
    float phi = atan2(q.x, q.y); // +Z からの角度（0〜π）
    if (phi > halfAngle)
    {
        return edgeDistance; // 開きの外：一番近いのは横の辺
    }
    if (radialDistance > 0.0f)
    {
        return radialDistance; // 開きの中で、弧の外
    }
    return -min(-radialDistance, edgeDistance); // 中：一番近い縁までの距離をマイナスで
}

// ===== 線（幅 width の長方形。z = 0 から length まで）=====
float CalcLineDistance(float32_t2 p, float width, float lineLength)
{
    float32_t2 halfSize = float32_t2(width * 0.5f, lineLength * 0.5f);
    float32_t2 d = float32_t2(abs(p.x) - halfSize.x, abs(p.y - halfSize.y) - halfSize.y);
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

// ===== 種類で分ける（p はローカルの (x, z)）=====
float CalcHitShapeDistance2D(uint32_t shapeType, float32_t2 p, float radius, float innerRadius, float halfAngle, float width, float lineLength)
{
    if (shapeType == kHitShapeLine)
    {
        return CalcLineDistance(p, width, lineLength);
    }
    return CalcSectorDistance(p, radius, innerRadius, halfAngle); // 円も扇も同じ式（円は開きの半分が π）
}
#HLSL_END
```

`HitboxTelegraph.hlsli` は gDrawData（C++ の `HitboxTelegraphData` と同じ並び）と、予告の Shader が使う関数。

```cpp
#META
stage: Include
#META_END

#HLSL
// Hitbox Telegraph（床の予告）の描き手が、描く物ごとに送る値（C++ の HitboxTelegraphData と同じ並び。16 バイトずつ）
// 予告の Shader は、"Decal/HitShape.hlsli" の後にこれを include して、当たりと同じ式で塗る
struct HitboxTelegraph
{
    float32_t3 origin;  // 形の中心（ワールド。底の真ん中）
    float radius;       // 円・扇の外側の半径
    float32_t3 right;   // 形のローカル +X（長さ 1）
    float innerRadius;  // 円・扇の内側の半径（0 なら穴なし）
    float32_t3 up;      // 形のローカル +Y（長さ 1）
    float halfAngle;    // 扇の開きの半分（ラジアン。円は π）
    float32_t3 forward; // 形のローカル +Z（長さ 1）
    float lineLength;   // 線の長さ
    float width;        // 線の幅
    float height;       // 形の高さ（底から上へ）
    uint32_t shapeType; // 形の種類（kHitShapeCircle・Sector・Line）
    uint32_t phase;     // 段階（C++ の HitboxPhase と同じ番号。1 が Warning、2 が Active）
    float progress;     // Warning の進み具合（0→1。Active の間は 1、Warning の長さが 0 なら 0 のまま）
    float remaining;    // Warning の残りの秒
    float phaseTime;    // 今の段階に入ってからの秒
    float padA;
};
ConstantBuffer<HitboxTelegraph> gDrawData : register(b22);

static const uint32_t kHitboxPhaseWarning = 1; // C++ の HitboxPhase::Warning
static const uint32_t kHitboxPhaseActive = 2;  // C++ の HitboxPhase::Active

// ワールドの点 → 形のローカル（origin が原点、向きは right・up・forward）
float32_t3 ToHitShapeLocal(float32_t3 world)
{
    float32_t3 offset = world - gDrawData.origin;
    return float32_t3(dot(offset, gDrawData.right), dot(offset, gDrawData.up), dot(offset, gDrawData.forward));
}

// 床の面での符号付き距離（外ならプラス、中ならマイナス）
float CalcTelegraphDistance(float32_t3 local)
{
    return CalcHitShapeDistance2D(gDrawData.shapeType, local.xz, gDrawData.radius, gDrawData.innerRadius, gDrawData.halfAngle, gDrawData.width, gDrawData.lineLength);
}

// 根元から先までの、どこにいるか（0 が根元、1 が先。円・扇は中心からの距離 ÷ 半径、線は前への長さ ÷ 長さ）
float CalcTelegraphReach(float32_t3 local)
{
    if (gDrawData.shapeType == kHitShapeLine)
    {
        return local.z / max(gDrawData.lineLength, 1.0e-4f);
    }
    return length(local.xz) / max(gDrawData.radius, 1.0e-4f);
}

// 根元から先までの長さ（m。進み具合の境目をなめらかにするのに使う）
float GetTelegraphReachLength()
{
    return (gDrawData.shapeType == kHitShapeLine) ? gDrawData.lineLength : gDrawData.radius;
}
#HLSL_END
```

```cpp
#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category:   Decal
vs:         Object3dVS
blend:      Normal
rasterizer: SolidFront
depth:      Disable
#PROGRAM_END

#PROPERTIES
fillColor:     Color   "Fill Color"     "形の中の色（A の分だけ透ける）" 1, 0.25, 0.15, 0.2
progressColor: Color   "Progress Color" "Warning の進み具合で、根元から先へ広がる所の色（Warning が終わると、先まで届く）" 1, 0.3, 0.15, 0.45
edgeColor:     Color   "Edge Color"     "縁の色" 1, 0.45, 0.25, 0.9
edgeWidth:     Range   "Edge Width"     "縁の太さ（m）" 0.12, 0, 1
activeColor:   Color   "Active Color"   "Active の間の色（Hitbox Telegraph の Show While Active を入れたときだけ）" 1, 0.15, 0.1, 0.6
texture:       Texture "Texture"        "形の中の色に掛ける画像（形を囲む四角に貼る。選ばなければ白）"
#PROPERTIES_END

#HLSL
// 床の予告の既定の Shader（Hitbox Telegraph の既定のマテリアル）
// 当たりと同じ符号付き距離で、形の中を塗り、縁を引き、Warning の進み具合で根元から先へ色を広げる
#include "Model/Object3d.hlsli"
#include "Decal/Decal.hlsli"
#include "Decal/HitShape.hlsli"
#include "Decal/HitboxTelegraph.hlsli"
#include "Fog/Fog.hlsli"

struct Material
{
    float32_t4 fillColor;     // 形の中の色（リニア）
    float32_t4 progressColor; // 進み具合で広がる所の色
    float32_t4 edgeColor;     // 縁の色
    float32_t4 activeColor;   // Active の間の色
    float edgeWidth;          // 縁の太さ（m）
    uint32_t texture;         // 形の中の色に掛ける画像
};
ConstantBuffer<Material> gMaterial : register(b20);

PixelShaderOutput main(VertexShaderOutput input)
{
    // --- 床の位置（箱は形を囲む大きさで、高さは形の高さ）---
    DecalSurface surface = GetDecalSurface(input.position);
    float32_t3 local = ToHitShapeLocal(surface.world);

    // --- 形の縁までの距離と、進み具合の境目までの距離（m）---
    // fwidth：隣のピクセルとの差＝1 ピクセルの長さ。縁を 1 ピクセルぶんなめらかにするのに使う（clip で捨てる前に求める）
    // 段差の所は隣のピクセルが別の面なので差が大きくなる。ぼけすぎないように kMaxSoftness で止める
    static const float kMaxSoftness = 0.1f; // なめらかにする幅の上限（m）
    float distance = CalcTelegraphDistance(local);
    float pixel = clamp(fwidth(distance), 1.0e-4f, kMaxSoftness);
    float reachDistance = (CalcTelegraphReach(local) - gDrawData.progress) * GetTelegraphReachLength();
    float reachPixel = clamp(fwidth(reachDistance), 1.0e-4f, kMaxSoftness);

    // --- 形の中か（中で 1、外で 0）---
    ClipOutsideDecal(surface.local); // 箱の外は捨てる（fwidth の後。捨てたピクセルとの差は使えないので）
    float coverage = saturate(0.5f - distance / pixel);
    clip(coverage - 1.0e-3f);

    // --- 色：中の色 → 進み具合の所 → 縁（Active の間は Active の色）---
    float32_t4 color = gMaterial.fillColor;
    if (gDrawData.phase == kHitboxPhaseActive)
    {
        color = gMaterial.activeColor;
    }
    else
    {
        // 根元からの長さが、進み具合の所まで届いているか（進み具合が 0 なら届かない）
        float reached = saturate(0.5f - reachDistance / reachPixel);
        color = lerp(color, gMaterial.progressColor, reached * step(1.0e-4f, gDrawData.progress));
    }
    // 縁から edgeWidth の内側で 1（縁の太さが 0 なら縁なし）
    float edge = saturate(0.5f + (distance + gMaterial.edgeWidth) / pixel);
    color = lerp(color, gMaterial.edgeColor, edge * step(1.0e-4f, gMaterial.edgeWidth));

    // --- 画像（形を囲む四角＝箱の XZ に貼る）・形の縁のなめらかさ・霧 ---
    float32_t2 uv = float32_t2(surface.local.x + 0.5f, 0.5f - surface.local.z);
    color *= gTextures[gMaterial.texture].SampleLevel(gSamplerClamp, uv, 0.0f);
    color.a *= coverage;
    color.rgb = ApplyFog(color.rgb, surface.world); // 下の物と同じだけ霧を掛ける

    PixelShaderOutput output;
    output.color = color;
    return output;
}
#HLSL_END
```

### D-2 `Components/Combat/HitboxTelegraphComponent.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

// 攻撃の予告を床に描く。Hitbox と同じ Entity に付ける Component（カテゴリ：Combat。見た目の層）
// 形は Hitbox の Shape（床に置く形：円・扇・線）。当たりと同じ符号付き距離の式で、デカールとして床・坂・段差に塗る
// 出すのは Hitbox が Warning の間（Show While Active を入れたら Active の間も）。描くのは HitboxTelegraphSystem
// 見た目はマテリアル（Shader と値）で変える。Curve の横軸は Warning の進み具合（0→1）

/// <summary>
/// 床の予告の設定
/// </summary>
struct HitboxTelegraphComponent {
	bool enabled = true;          // false の間は描かない
	uint32_t material = 0;        // マテリアル（MaterialManager::Load の番号。Shader は category: Decal で gDrawData に HitboxTelegraph を読む物。0 ならエンジンの既定＝赤い塗り）
	bool showWhileActive = false; // true：Active の間も出す（当たっている間の範囲も見せる攻撃）
	int order = 0;                // ほかのデカールと重なったときの順（大きい方が上に塗られる）
};

static_assert(std::is_trivially_copyable_v<HitboxTelegraphComponent>);
```

### D-3 `Systems/Combat/HitboxTelegraphSystem.h`・`.cpp`（新しい）

```cpp
#pragma once
#include <cstdint>

#include "MyEngine/Math/Vector3.h"

/// <summary>
/// 床の予告の描き手が、描く物ごとに Shader へ送る値（gDrawData。Shader の Decal/HitboxTelegraph.hlsli と同じ並び。16 バイトずつ）
/// <para>形の値は WorldHitShape（Entity の大きさを掛けた後）。予告の Shader はこれで、当たりと同じ符号付き距離を計算する</para>
/// </summary>
struct HitboxTelegraphData {
	Vector3 origin = {0.0f, 0.0f, 0.0f};  // 形の中心（ワールド。底の真ん中）
	float radius = 0.0f;                  // 円・扇の外側の半径
	Vector3 right = {1.0f, 0.0f, 0.0f};   // 形のローカル +X（長さ 1）
	float innerRadius = 0.0f;             // 円・扇の内側の半径
	Vector3 up = {0.0f, 1.0f, 0.0f};      // 形のローカル +Y（長さ 1）
	float halfAngle = 0.0f;               // 扇の開きの半分（ラジアン。円は π）
	Vector3 forward = {0.0f, 0.0f, 1.0f}; // 形のローカル +Z（長さ 1）
	float length = 0.0f;                  // 線の長さ
	float width = 0.0f;                   // 線の幅
	float height = 0.0f;                  // 形の高さ
	uint32_t shapeType = 0;               // 形の種類（HitShapeType の番号）
	uint32_t phase = 0;                   // 段階（HitboxPhase の番号）
	float progress = 0.0f;                // Warning の進み具合（0→1。Active の間は 1、Warning の長さが 0 なら 0 のまま）
	float remaining = 0.0f;               // Warning の残りの秒
	float phaseTime = 0.0f;               // 今の段階に入ってからの秒
	float padA = 0.0f;
};
static_assert(sizeof(HitboxTelegraphData) == 96, "HLSLのHitboxTelegraph（Decal/HitboxTelegraph.hlsli）と大きさが違います");


/// <summary>
/// HitboxTelegraphComponent を持つ Entity の Hitbox の形を、床にデカールとして描く（Renderer::DrawDecal）
/// <para>マテリアルを選んでいなければ、エンジンの既定のマテリアル（赤い塗り・縁・進み具合）で描く</para>
/// </summary>
class HitboxTelegraphSystem {
public:
	// HitboxTelegraphComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 描画リクエストを積む（EngineSystems::Draw から。停止中も描く＝Inspector で Phase を Warning にすると、置きながら見られる）
	/// </summary>
	static void Draw();

	// エンジンの既定の予告のマテリアル（初めて呼んだときに読む。シェーダーを読んだ後に呼ぶ）
	static uint32_t GetDefaultMaterial();
};
```

```cpp
#include "HitboxTelegraphSystem.h"

#include <algorithm>
#include <memory>
#include <span>

#include "MyEngine/Components/Combat/HitboxComponent.h"
#include "MyEngine/Components/Combat/HitboxTelegraphComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Systems/Combat/HitSystem.h"

namespace {
constexpr const char* kDefaultMaterialPath = "MyEngine/Resources/Materials/HitboxTelegraph.material"; // マテリアルを選んでいないときに使う（エンジンの Resources）
constexpr float kEdgeMargin = 0.1f;      // 箱を形より横に広げる長さ（m。縁をなめらかにする 1 ピクセルの分が、箱で切れないように）
constexpr float kBelowBaseDepth = 0.05f; // 箱を形の底より下へ伸ばす長さ（m。底と同じ高さの床が、深度の誤差で箱の外にならないように）

/// <summary>
/// HitboxTelegraphSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct HitboxTelegraphState {
	uint32_t defaultMaterial = 0;         // 既定のマテリアルの番号（初めて使うときに読む）
	bool isDefaultMaterialLoaded = false; // 読みに行ったか（無いファイルを毎フレーム読みに行かないように）
};
std::unique_ptr<HitboxTelegraphState> state;

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeHitboxTelegraph(ComponentUI& ui, HitboxTelegraphComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと描かない"));
	ui.AssetField("Material###material", value.material, AssetType::Material,
	    Tip("予告のマテリアル（.material。Shader は Decal/HitboxTelegraph.PS.shader のように、gDrawData で予告の値を読む物）。(none) なら赤い塗り（エンジンの既定）"));
	ui.Field("Show While Active###showWhileActive", value.showWhileActive, Tip("入れると、Active（当たっている）間も出す。外すと Warning の間だけ"));
	ui.Field("Order###order", value.order, Tip("ほかのデカールと重なったときの順（大きい方が上に塗られる）"));
}

//=============================================================================
// 描く値
//=============================================================================
// ===== 予告を出す段階か =====
bool IsShown(const HitboxComponent& hitbox, const HitboxTelegraphComponent& telegraph) {
	switch (hitbox.phase) {
	case HitboxPhase::Off:
		return false;
	case HitboxPhase::Warning:
		return true;
	case HitboxPhase::Active:
		return telegraph.showWhileActive;
	}
	return false;
}

// ===== Warning の進み具合（0→1。Active は 1。Warning の長さが 0＝ずっと Warning なら 0 のまま）=====
float CalcProgress(const HitboxComponent& hitbox) {
	if (hitbox.phase == HitboxPhase::Active) {
		return 1.0f;
	}
	return (hitbox.warningSeconds > 0.0f) ? std::clamp(hitbox.phaseTime / hitbox.warningSeconds, 0.0f, 1.0f) : 0.0f;
}

// ===== Shader に送る値 =====
HitboxTelegraphData MakeData(const WorldHitShape& shape, const HitboxComponent& hitbox, float progress) {
	HitboxTelegraphData data;
	data.origin = shape.origin;
	data.radius = shape.radius;
	data.right = shape.right;
	data.innerRadius = shape.innerRadius;
	data.up = shape.up;
	data.halfAngle = shape.halfAngle;
	data.forward = shape.forward;
	data.length = shape.length;
	data.width = shape.width;
	data.height = shape.height;
	data.shapeType = static_cast<uint32_t>(shape.type);
	data.phase = static_cast<uint32_t>(hitbox.phase);
	data.progress = progress;
	data.remaining = hitbox.GetWarningRemaining();
	data.phaseTime = hitbox.phaseTime;
	return data;
}

// ===== 形を囲む箱（デカールの箱。大きさ 1 で 1辺 1 の箱を、形のローカルの軸で置く）=====
// 横（XZ）は形を囲む四角、縦（Y）は底の少し下から形の高さまで（高さの外は当たらないので塗らない）
bool MakeBox(const WorldHitShape& shape, Matrix4x4& outWorld) {
	// --- 形のローカルでの箱の範囲 ---
	Vector3 boxMin = {-shape.radius, -kBelowBaseDepth, -shape.radius}; // 円・扇：半径の四角
	Vector3 boxMax = {shape.radius, shape.height, shape.radius};
	if (shape.type == HitShapeType::Line) {
		boxMin.x = -shape.width * 0.5f; // 線：幅 × 長さ（中心から前 +Z へ）
		boxMax.x = shape.width * 0.5f;
		boxMin.z = 0.0f;
		boxMax.z = shape.length;
	}
	boxMin.x -= kEdgeMargin;
	boxMin.z -= kEdgeMargin;
	boxMax.x += kEdgeMargin;
	boxMax.z += kEdgeMargin;
	const Vector3 size = boxMax - boxMin;
	if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f) {
		return false; // 大きさ 0（見えない）
	}
	// --- ワールド行列（1〜3行目が軸 × 大きさ、4行目が箱の中心）---
	const Vector3 center = (boxMin + boxMax) * 0.5f;
	const Vector3 axisX = shape.right * size.x;
	const Vector3 axisY = shape.up * size.y;
	const Vector3 axisZ = shape.forward * size.z;
	const Vector3 position = shape.ToWorld(center);
	outWorld = MakeIdentity4x4();
	outWorld.m[0][0] = axisX.x;
	outWorld.m[0][1] = axisX.y;
	outWorld.m[0][2] = axisX.z;
	outWorld.m[1][0] = axisY.x;
	outWorld.m[1][1] = axisY.y;
	outWorld.m[1][2] = axisY.z;
	outWorld.m[2][0] = axisZ.x;
	outWorld.m[2][1] = axisZ.y;
	outWorld.m[2][2] = axisZ.z;
	outWorld.m[3][0] = position.x;
	outWorld.m[3][1] = position.y;
	outWorld.m[3][2] = position.z;
	return true;
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void HitboxTelegraphSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<HitboxTelegraphState>();
	// Hitbox Telegraph の置き場所・保存・Inspector を登録する（見せ方は DescribeHitboxTelegraph 1つ）
	EntityManager::RegisterComponent<HitboxTelegraphComponent>();
	ComponentSerializer::Register<HitboxTelegraphComponent>("Hitbox Telegraph", &DescribeHitboxTelegraph);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HitboxTelegraphComponent>>("Hitbox Telegraph", "Combat", &DescribeHitboxTelegraph));
}

void HitboxTelegraphSystem::Release() { state.reset(); }


//=============================================================================
// 既定のマテリアル
//=============================================================================
uint32_t HitboxTelegraphSystem::GetDefaultMaterial() {
	MY_ASSERT_MSG(state != nullptr, "HitboxTelegraphSystem::Initialize()を先に呼んでください");
	if (!state->isDefaultMaterialLoaded) {
		state->isDefaultMaterialLoaded = true;
		state->defaultMaterial = LoadAsset(AssetType::Material, kDefaultMaterialPath); // 無ければ警告が出て 0（描かない）
	}
	return state->defaultMaterial;
}


//=============================================================================
// 描画
//=============================================================================
void HitboxTelegraphSystem::Draw() {
	EntityManager::ForEach<HitboxTelegraphComponent>([](Handle<Entity> entity, const HitboxTelegraphComponent& telegraph) {
		if (!telegraph.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		// --- 同じ Entity の Hitbox が、予告を出す段階で、床に置く形か ---
		const HitboxComponent* hitbox = EntityManager::Get<HitboxComponent>(entity);
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (hitbox == nullptr || transform == nullptr || !IsShown(*hitbox, telegraph) || !IsFloorShape(hitbox->shape.type)) {
			return;
		}
		// --- 当たりと同じ形（Entity の大きさを掛けた後）で、箱と Shader に送る値を作る ---
		const WorldHitShape shape = HitSystem::CalcWorldShape(*hitbox, *transform);
		Renderer::DecalConfig config;
		if (!MakeBox(shape, config.worldMatrix)) {
			return;
		}
		const float progress = CalcProgress(*hitbox);
		const HitboxTelegraphData data = MakeData(shape, *hitbox, progress);
		config.material = (telegraph.material != 0) ? telegraph.material : GetDefaultMaterial();
		config.progress = progress; // マテリアルの Curve の横軸も、Warning の進み具合
		config.order = telegraph.order;
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		// std::as_bytes：構造体1つを、中身を変えずにバイトの並びとして見る（std::span で「data 1個分」を渡す）
		Renderer::DrawDecal(config, std::as_bytes(std::span(&data, 1)));
	});
}
```

### D-4 `Systems/EngineSystems.cpp`

```cpp
#include "MyEngine/Systems/Combat/HitSystem.h"
#include "MyEngine/Systems/Combat/HitboxTelegraphSystem.h"
```

`Initialize` の `DamageSystem::Initialize();` の下、`Release` の `DamageSystem::Release();` の下、`Draw` の `DecalRenderSystem::Draw();` の下に足す。

```cpp
	HitboxTelegraphSystem::Initialize();  // Hitbox Telegraph（攻撃の予告を床に描く。Hitbox の形を読む）
```

```cpp
	HitboxTelegraphSystem::Release();
```

```cpp
	HitboxTelegraphSystem::Draw();                            // 攻撃の予告を床に塗る（Hitbox Telegraph）の描画
```

### D-5 `Resources/Materials/HitboxTelegraph.material`（新しい）

```json
{
	"shader": "MyEngine/Shader/Data/Decal/HitboxTelegraph.PS.shader",
	"blendOverride": false,
	"blend": "Normal",
	"rasterizerOverride": false,
	"rasterizer": "SolidFront",
	"depthOverride": false,
	"depth": "Disable",
	"fillColor": [1.0, 0.25, 0.15, 0.2],
	"fillColorCurve": "",
	"progressColor": [1.0, 0.3, 0.15, 0.45],
	"progressColorCurve": "",
	"edgeColor": [1.0, 0.45, 0.25, 0.9],
	"edgeColorCurve": "",
	"edgeWidth": 0.12,
	"edgeWidthCurve": "",
	"activeColor": [1.0, 0.15, 0.1, 0.6],
	"activeColorCurve": "",
	"texture": ""
}
```

### D-6 `MyEngine_v1.vcxproj`・`.filters`

`MyEngine\Components\Combat\HitboxTelegraphComponent.h`・`MyEngine\Systems\Combat\HitboxTelegraphSystem.h`・`.cpp` と、`None` の `MyEngine\Shader\Data\Decal\HitShape.hlsli.shader`・`HitboxTelegraph.hlsli.shader`・`HitboxTelegraph.PS.shader` を足す。

### D-7 確かめること

- 3構成でビルドが通る
- HitDamageTest の Circle Attack に Hitbox Telegraph を付け、Hitbox の Phase を Warning にすると、停止中でも赤い円が床に出る

## パートE Sandbox のテスト

### E-1 Curve・マテリアル・ゲームの .shader（新しい）

`Sandbox/Resources` の下に `curves`・`materials`・`shaders` を作る。

`curves/TelegraphFade.curve`（0 → 1 の直線）

```json
{
	"keys": [
		{
			"time": 0.0,
			"value": 0.0,
			"interp": "Linear",
			"handleMode": "Aligned",
			"inHandle": [0.0, 0.0],
			"outHandle": [0.0, 0.0]
		},
		{
			"time": 1.0,
			"value": 1.0,
			"interp": "Linear",
			"handleMode": "Aligned",
			"inHandle": [0.0, 0.0],
			"outHandle": [0.0, 0.0]
		}
	]
}
```

`materials/CheckerDecal.material`（Decal Renderer の見本。エンジンの uvChecker を貼る）

```json
{
	"shader": "MyEngine/Shader/Data/Decal/DecalTexture.PS.shader",
	"blendOverride": false,
	"blend": "Normal",
	"rasterizerOverride": false,
	"rasterizer": "SolidFront",
	"depthOverride": false,
	"depth": "Disable",
	"color": [1.0, 1.0, 1.0, 0.85],
	"colorCurve": "",
	"texture": "MyEngine/Resources/Textures/uvChecker.png"
}
```

`materials/LineTelegraph.material`（Fill Color に Curve。黄色の薄い色 → 赤の濃い色）

```json
{
	"shader": "MyEngine/Shader/Data/Decal/HitboxTelegraph.PS.shader",
	"blendOverride": false,
	"blend": "Normal",
	"rasterizerOverride": false,
	"rasterizer": "SolidFront",
	"depthOverride": false,
	"depth": "Disable",
	"fillColor": [1.0, 0.9, 0.2, 0.1],
	"fillColorCurve": "resources/curves/TelegraphFade.curve",
	"fillColorEnd": [1.0, 0.1, 0.1, 0.6],
	"progressColor": [1.0, 0.3, 0.15, 0.0],
	"progressColorCurve": "",
	"edgeColor": [1.0, 0.9, 0.3, 0.9],
	"edgeColorCurve": "",
	"edgeWidth": 0.08,
	"edgeWidthCurve": "",
	"activeColor": [1.0, 0.15, 0.1, 0.6],
	"activeColorCurve": "",
	"texture": ""
}
```

`materials/RingTelegraph.material`（ゲームの .shader を使う）

```json
{
	"shader": "resources/shaders/RingTelegraph.PS.shader",
	"blendOverride": false,
	"blend": "Normal",
	"rasterizerOverride": false,
	"rasterizer": "SolidFront",
	"depthOverride": false,
	"depth": "Disable",
	"baseColor": [0.2, 0.5, 1.0, 0.15],
	"baseColorCurve": "",
	"ringColor": [0.3, 0.8, 1.0, 0.7],
	"ringColorCurve": "",
	"ringCount": 4.0,
	"ringCountCurve": "",
	"speed": 1.5,
	"speedCurve": ""
}
```

`shaders/RingTelegraph.PS.shader`（ゲームの resources に置いた予告の Shader。輪が先から根元へ進む）

```cpp
#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category:   Decal
vs:         Object3dVS
blend:      Normal
rasterizer: SolidFront
depth:      Disable
#PROGRAM_END

#PROPERTIES
baseColor: Color "Base Color" "形の中の色" 0.2, 0.5, 1, 0.15
ringColor: Color "Ring Color" "輪の色" 0.3, 0.8, 1, 0.7
ringCount: Float "Ring Count" "根元から先までの輪の数" 4
speed:     Float "Speed"      "輪が先から根元へ進む速さ（1 秒に輪 1 つぶん × これ）" 1.5
#PROPERTIES_END

#HLSL
// Sandbox の見本：ゲームの resources に置いた予告の Shader（エンジンの .hlsli を include して、当たりと同じ形で塗る）
// 輪が、形の先から根元へ向かって進む
#include "Model/Object3d.hlsli"
#include "Decal/Decal.hlsli"
#include "Decal/HitShape.hlsli"
#include "Decal/HitboxTelegraph.hlsli"
#include "Fog/Fog.hlsli"

struct Material
{
    float32_t4 baseColor; // 形の中の色（リニア）
    float32_t4 ringColor; // 輪の色
    float ringCount;      // 輪の数
    float speed;          // 輪の進む速さ
};
ConstantBuffer<Material> gMaterial : register(b20);

PixelShaderOutput main(VertexShaderOutput input)
{
    DecalSurface surface = GetDecalSurface(input.position);
    float32_t3 local = ToHitShapeLocal(surface.world);
    float distance = CalcTelegraphDistance(local);
    float pixel = clamp(fwidth(distance), 1.0e-4f, 0.1f);
    ClipOutsideDecal(surface.local);
    float coverage = saturate(0.5f - distance / pixel);
    clip(coverage - 1.0e-3f);

    // 根元からの位置 × 輪の数 ＋ 時間 の小数の所が 0.3 より小さい所が輪（時間が進むと、輪が根元へ動く）
    float stripe = frac(CalcTelegraphReach(local) * gMaterial.ringCount + gDrawData.phaseTime * gMaterial.speed);
    float32_t4 color = lerp(gMaterial.baseColor, gMaterial.ringColor, step(stripe, 0.3f));
    color.a *= coverage;
    color.rgb = ApplyFog(color.rgb, surface.world);

    PixelShaderOutput output;
    output.color = color;
    return output;
}
#HLSL_END
```

### E-2 `Sandbox/Resources/scenes/MaterialTelegraphTest.scene.json`（新しい）

HitDamageTest を元に、坂（Slope）・段差（Step）・Decal Renderer の見本（Decal Sample と Decal Box）・ボスの足元の円（Boss Stomp）を足し、攻撃に Hitbox Telegraph を付けた。プレイヤーの体・ボスの体・弱点は Receive Decals を外してある。

| Entity | 予告 |
|---|---|
| Circle Attack（坂の上） | 既定のマテリアル |
| Sector Attack（段差の上） | `RingTelegraph.material`（ゲームの .shader） |
| Line Attack | `LineTelegraph.material`（Curve） |
| Multi Hit Zone | 既定・Show While Active |
| Boss Stomp（ボスの足元） | 既定（ボスの体に塗られないか） |

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
					"translation": [0.0, 16.0, -16.0],
					"rotation": [45.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
				},
				"Camera Follow": {
					"enabled": true,
					"target": 4,
					"plane": "XZ",
					"mode": "Center",
					"offset": [0.0, 0.0],
					"deadZone": [1.0, 1.0],
					"groundSnapY": true,
					"smoothTime": 0.15,
					"maxLag": [6.0, 6.0],
					"useBounds": false
				},
				"Camera Shake": {}
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
					"translation": [0.0, -0.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [40.0, 1.0, 40.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.55, 0.55, 0.55, 1.0]
				}
			}
		},
		{
			"id": 4,
			"name": "Player",
			"parent": 0,
			"active": true,
			"layer": 1,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, -6.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Capsule",
					"center": [0.0, 0.9, 0.0],
					"physicMaterial": "",
					"radius": 0.4,
					"height ": 1.8
				},
				"Character Controller": {
					"enabled": true
				},
				"Hurtbox": {
					"enabled": true,
					"shape": "Capsule",
					"center": [0.0, 0.9, 0.0],
					"radius": 0.3,
					"height": 1.6,
					"damageMultiplier": 1.0
				},
				"Health": {
					"maxHealth": 100.0,
					"health": 100.0,
					"invincible": false,
					"invincibleAfterHit": 0.5,
					"immuneTypes": 0
				},
				"CharacterKeyboardMover": {
					"moveSpeed": 6.0,
					"jumpSpeed": 6.0,
					"dashSpeed": 18.0,
					"dashTime": 0.18
				},
				"HitTestPlayer": {
					"guardRate": 0.2,
					"bulletSpeed": 30.0,
					"bulletDamage": 20.0
				}
			}
		},
		{
			"id": 5,
			"name": "Player Body",
			"parent": 4,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.9, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.8, 1.8, 0.8]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Sphere",
					"color": [0.3, 0.6, 1.0, 1.0],
					"receivesDecals": false
				}
			}
		},
		{
			"id": 6,
			"name": "Boss",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 10.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hurtbox": {
					"enabled": true,
					"shape": "Capsule",
					"center": [0.0, 1.5, 0.0],
					"radius": 1.0,
					"height": 3.0,
					"damageMultiplier": 1.0
				},
				"Health": {
					"maxHealth": 500.0,
					"health": 500.0,
					"invincible": false,
					"invincibleAfterHit": 0.0,
					"immuneTypes": 0
				}
			}
		},
		{
			"id": 7,
			"name": "Boss Body",
			"parent": 6,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 1.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [2.0, 3.0, 2.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.4, 0.4, 1.0],
					"receivesDecals": false
				}
			}
		},
		{
			"id": 8,
			"name": "Boss Weak Point",
			"parent": 6,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 1.0, -1.2],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hurtbox": {
					"enabled": true,
					"shape": "Sphere",
					"center": [0.0, 0.0, 0.0],
					"radius": 0.5,
					"damageMultiplier": 2.0
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Sphere",
					"color": [1.0, 0.9, 0.2, 1.0],
					"receivesDecals": false
				}
			}
		},
		{
			"id": 9,
			"name": "Circle Attack",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [-6.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Circle",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 2.5,
					"shapeInnerRadius": 0.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 10.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.08,
					"shake": 0.5
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 0.5
				},
				"Hitbox Telegraph": {
					"enabled": true,
					"material": "",
					"showWhileActive": false,
					"order": 0
				}
			}
		},
		{
			"id": 10,
			"name": "Sector Attack",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 4.0],
					"rotation": [0.0, 180.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Sector",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 6.0,
					"shapeInnerRadius": 1.5,
					"shapeAngle": 90.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 15.0,
					"damageType": 1,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.08,
					"shake": 0.5
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 1.5
				},
				"Hitbox Telegraph": {
					"enabled": true,
					"material": "resources/materials/RingTelegraph.material",
					"showWhileActive": false,
					"order": 0
				}
			}
		},
		{
			"id": 11,
			"name": "Line Attack",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [6.0, 0.0, 8.0],
					"rotation": [0.0, 180.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Line",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeWidth": 1.5,
					"shapeLength": 14.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 20.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.1,
					"shake": 0.8
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 2.5
				},
				"Hitbox Telegraph": {
					"enabled": true,
					"material": "resources/materials/LineTelegraph.material",
					"showWhileActive": false,
					"order": 0
				}
			}
		},
		{
			"id": 12,
			"name": "Multi Hit Zone",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [-6.0, 0.0, -8.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Circle",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 1.5,
					"shapeInnerRadius": 0.0,
					"shapeHeight": 1.0,
					"targets": 2,
					"instigator": 0,
					"phase": "Active",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.5,
					"maxTargets": 0,
					"justEnabled": false,
					"justMargin": 0.0,
					"justLeadTime": 0.3
				},
				"Damage Source": {
					"damage": 2.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.0,
					"shake": 0.0
				},
				"Hitbox Telegraph": {
					"enabled": true,
					"material": "",
					"showWhileActive": true,
					"order": 0
				}
			}
		},
		{
			"id": 13,
			"name": "Turret",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [14.0, 1.0, -3.0],
					"rotation": [0.0, -90.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.7, 0.3, 0.9, 1.0]
				},
				"TestBulletSpawner": {
					"interval": 1.0,
					"speed": 40.0,
					"damage": 5.0,
					"targets": 2
				}
			}
		},
		{
			"id": 14,
			"name": "Slope",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-7.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 12.0],
					"scale": [6.0, 1.0, 4.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.45, 0.6, 0.45, 1.0]
				}
			}
		},
		{
			"id": 15,
			"name": "Step",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.25, 0.5],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [5.0, 0.5, 1.5]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.6, 0.55, 0.45, 1.0]
				}
			}
		},
		{
			"id": 16,
			"name": "Decal Sample",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [8.0, 0.0, -8.0],
					"rotation": [0.0, 30.0, 0.0],
					"scale": [4.0, 2.0, 4.0]
				},
				"Decal Renderer": {
					"enabled": true,
					"material": "resources/materials/CheckerDecal.material",
					"progress": 0.0,
					"order": 0
				}
			}
		},
		{
			"id": 17,
			"name": "Decal Box",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [8.5, 0.4, -7.5],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 0.8, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.8, 0.8, 0.8, 1.0]
				}
			}
		},
		{
			"id": 18,
			"name": "Boss Stomp",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 10.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Circle",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 3.5,
					"shapeInnerRadius": 0.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 10.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.08,
					"shake": 0.5
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 0.2
				},
				"Hitbox Telegraph": {
					"enabled": true,
					"material": "",
					"showWhileActive": false,
					"order": 0
				}
			}
		}
	]
}
```

### E-3 試し方

1. Sandbox を起動し、File → Open Scene で `MaterialTelegraphTest` を開く
2. 停止中：右下の床に市松模様（Decal Sample）。上に置いた箱の上の面と横の面にも塗られている。Scene ビューに水色の箱
3. Play：円・扇・線・ボスの足元に予告が出る（Warning の 1.2 秒で、根元から先へ濃い色が広がる）
   - 円は坂の上に張り付く。扇は段差の上も塗られ、輪が先から根元へ進む
   - 線は黄色の薄い色から、赤の濃い色へ変わる（Curve）
   - 左下の円（Multi Hit Zone）は Active の間も出る
   - ボスの体・弱点には塗られない（Receive Decals を外してある）
4. Project の `materials/LineTelegraph.material` を選び、Inspector で Fill Color End を変えると、すぐ予告の色が変わる。Ctrl+Z で戻る
5. Shader を `DecalTexture.PS.shader` に変えると、欄が Color と Texture になる（名前の違う値は消える）。Ctrl+Z で戻る
6. Line Attack の Hitbox Telegraph の Material を `CheckerDecal.material` にすると……描けるが、予告の形にはならない（Decal の Shader なら何でも選べる）。Decal Renderer に `LineTelegraph.material` を選ぶと、Console に「この Shader は描き手の値（gDrawData）を読みます」が1回出て、描かれない

## 2章 確かめたこと・確かめていないこと

写し（`answer-mat` のタグ）で確かめた。

- 3構成（Debug・Development・Release）で、足した・直したファイルから警告が出ない
- パートA・B・C・D の後で、それぞれビルドが通る（Debug。パートA だけの状態は Release も、起動まで）
- 写す手順：古い写し（`BrdfLUT.PS.shader` の名前のまま・消したはずのフォルダ・元に無い Resources のファイル）を置いてビルドすると、写し先が元と同じになる（`BrdfLut.PS.shader` に直り、古い物は消え、Resources の数が元と同じ）
- 起動して、今までの Shader が全部読める（エンジンの名前は今までどおり。ゲームの .shader は `Project/shaders/RingTelegraphPS`）
- Release（exe の隣の写しを読む）で起動して、予告・デカールが出る
- Debug の Play（D3D12 のデバッグレイヤーあり）でエラーが出ない
- 自動テスト 74 項目（写しだけ。md には入れない）
  - 名前・パス（/ と \、`./` `..` があっても同じ番号）、VS・Include は選べない、(cat, shading) とパスが同じ番号
  - 定数バッファのリフレクション：Unlit の gMaterial の場所が `Material3dData` と全部同じ、gCamera・gDecal・gDrawData の大きさが C++ と同じ
  - #PROPERTIES：全部の種類（Float・Range・Toggle・Vector の float2・Color の float3・Texture）の場所・成分の数・既定・説明の省略
  - 詰め方：sRGB → リニア・白のテクスチャ・Range の止め・状態の上書き
  - Curve：0 で始まり・1 で終わり・0.5 で sRGB のまま補間
  - 書いて読み直すと同じ・End は Curve があるときだけ書く・Shader に無い値は警告して捨てる
  - Shader を変えたら、名前と種類が同じ値だけ残る
  - 描けない組み合わせ（予告の Shader を描き手の値なしで・Depth を TestWrite）で止まらず、警告が1回出る
- HLSL の `HitShape.hlsli` と C++ の `CalcSignedDistance2D` が 16 万点で同じ値（差 0）
- 画面で：坂・段差・箱に張り付く、ボスの体・弱点に塗られない、線の予告とギズモの四角が重なる、Curve で色が変わる、ゲームの .shader の輪が動く

確かめていないこと：

- Inspector の操作（.material の欄を直す・Undo・Create → Material・Shader の一覧）を手で触ること（自動テストは Describe とファイルの読み書きまで）
- Prefab の中の Hitbox Telegraph・Decal Renderer の上書き
- AL4_01（サブモジュールでエンジンを読むゲーム）での起動
- GPU の上で、塗りの縁とギズモの線が1ピクセルの単位で重なるか（式は同じ。画面で大きくずれていないことだけ）

## 3章 後で足す候補

- **今の Model・Primitive をマテリアルへ載せ替える**：Model Renderer・Primitive Renderer に Material の欄を足し、`DrawCategory::Model` の Shader を .material で選べるようにする（描く流れは C で作った物を Model の列にも使う）
- **物ごとの上書き**（MaterialPropertyBlock）：Manager 側の Entity ごとの表で
- **Shader Graph**：ノード → .shader（#PROPERTIES と #HLSL）を書き出す道具。マテリアルの側は今のまま
- **.shader を直したら、起動し直さずに反映する**
- **デカールの向きで薄める**：床に貼るデカールが壁の横の面で伸びる（深度の差から法線を作り、上を向いていない所を薄くする）
- **Decal Renderer の Progress を Timeline のトラックで動かす**（今は Script・Timeline の SetValue）
