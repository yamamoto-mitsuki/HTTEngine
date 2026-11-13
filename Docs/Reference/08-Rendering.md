> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 8. 描画

## モデルを出す

**一番早いのはエディタで置くこと**：Hierarchy の＋で Entity を作り、Add Component → Rendering3D → Model Renderer、Model の欄でファイルを選んで Save。

コードで置くとき（`CreateDefaultEntities` の中。シーンファイルがまだ無いときの最初の配置）：

```cpp
// ① モデルを読む（返るのは番号。同じパスは2回読まれない）
const uint32_t modelHandle = ModelManager::Load("resources/monsterBall/monsterBall.gltf");

// ② Entityに ModelRendererComponent を付ける
ModelRendererComponent renderer;
renderer.modelHandle = modelHandle;
renderer.shadingType = ShadingType::PBR;
const Handle<Entity> ball = EntityManager::Create("MonsterBall");
EntityManager::RequestAdd<ModelRendererComponent>(ball, renderer);
```

あとは `ModelRenderSystem` が毎フレーム描く。**ゲーム側で `Renderer::DrawModel` を呼ぶ必要はない。** 保存するとモデルはパスで書かれ、次の起動で読み直される。

- 読めるファイル：`.obj`・`.gltf`・`.glb`・`.fbx`。テクスチャは、外の画像ファイル（モデルのフォルダからの相対）でも、ファイルに埋め込まれた画像（`.glb`・FBX の埋め込み・`.gltf` の data URI）でもよい。埋め込みの画像は png・jpg など WIC が読める形だけ（読めない形はログに出て、テクスチャ無しで描く）。
- 埋め込みの画像は、Inspector の Model Materials の一覧（マウスを乗せたとき）やログに「モデルのパス/*番号」（`resources/Model/Robot.glb/*0`）の名前で出る。ファイルとしては無いので、Model Renderer の Texture の欄からは選べない。

| 項目 | 意味 |
|---|---|
| `modelHandle` | `ModelManager::Load` の戻り値。0は未選択 |
| `textureHandle` | 0ならモデルのマテリアルのテクスチャを使う（Inspector の Texture 欄で選べる。選ぶと全部のマテリアルのテクスチャを差し替える） |
| `color` | 物ごとの色（`Vector4`。sRGB の RGBA 0〜1）。マテリアルの Base Color とテクスチャに掛ける（白ならマテリアルの色のまま） |
| `intensity` | 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る） |
| `uvTransform` | テクスチャの UV をずらす（Offset）・くり返す（Tiling） |
| `shadingType` | `Unlit` / `Lambert` / `HalfLambert` / `Phong` / `BlinnPhong` / **`PBR`** |
| `blendMode` | `Normal` / `Add` / `Subtract` / `Multiply` / `Screen` |
| `rasterizerType` | `SolidBack`（通常） / `SolidNone`（両面） |
| `depthMode` | `TestWrite`（不透明） / 半透明は書き込みを切る |
| `billboard` | `None` / `Full` / `AxisY`（カメラの方を向く） |
| `receivesDecals` | false ならデカールを受けない（不透明なときだけ効く） |
| `enabled` | falseで描かない |
| `material`・`overrideMaterial` | Base Color・Ambient〜Roughness・Alpha Cutoff。**`overrideMaterial` が true のときだけ、読み込んだマテリアル（mtl / glTF）の値の代わりに、全部のメッシュに同じ値を使う**（テクスチャは読み込んだ物のまま）。Inspector の Material で値を触ると自動で入る。Material の「Model Materials」に読み込んだマテリアルの一覧が出て、**Copy** でその値を写せる。Alpha Cutoff だけは Override に関係なくいつも効く |

> **PBR のモデルは、シーンが環境光（IBL）を持っていないと描かれない。** `DefaultScene` は持っている。自分で書いたシーンは `IBLEnvironment` を `MakeFromHDR` して、`GetEnvironment()` で返す（2章）。

マテリアルのどの値をどのシェーダーが使うか：

| Shading | 使う値 |
|---|---|
| Unlit | Base Color・Color・テクスチャ |
| Lambert・HalfLambert | ＋ Emissive |
| Phong・BlinnPhong | ＋ Specular（ハイライトの色。黒なら無し）・Shininess・Emissive |
| PBR | ＋ Metallic・Roughness・Emissive（Ambient は IBL が無いときだけ） |

色は「マテリアルの Base Color（mtl の Kd・glTF の baseColorFactor）× `color` × テクスチャ」。

## モデル無しで形を出す（Primitive Renderer）

**Add Component → Rendering3D → Primitive Renderer**（`PrimitiveRendererComponent`）。モデルのファイルを使わずに、球・箱を描く（Unity の 3D Object → Sphere / Cube の見た目の所）。

| 項目 | 中身 |
|---|---|
| Shape | `Sphere`（Scale 1 で直径1）／`Box`（Scale 1 で1辺1。Entity と一緒に回る） |
| Material → Color | 色（RGBA。0〜1）。テクスチャと掛け合わせる |
| Material → Intensity | 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る） |
| Material → Surface | `Opaque`（不透明）／`Transparent`（Color の A の分だけ透ける。氷・水）／`Additive`（色を足して光る）。Transparent・Additive は奥行きを書かずに、不透明な物の後でカメラから遠い順に描く |
| Material → Texture・UV Scale | 貼る画像と、1枚の大きさ（1面を1として。0.5 なら1面に2回くり返す） |
| Material → Shading | `Unlit`（色そのまま）／`HalfLambert`（既定）／`BlinnPhong`（ハイライト）／`PBR`（金属感・粗さ）など。値の使い方は上の表と同じ |
| Material → Specular〜Emissive | 光沢・金属感・自己発光（`MaterialParams`） |

- 大きさは Transform の Scale。Collider の初期値（球の半径 0.5・箱の 1×1×1）と同じ大きさなので、Scale だけ変えれば見た目と当たりがそろう。
- 見た目（`PrimitiveMaterial`）は Elastic Surface の板・支柱と同じ物。PBR は、シーンが環境光を持っていれば（`IScene::GetEnvironment`。DefaultScene は持っている）そのまま、無ければ Half Lambert で描かれる。
- コードで作るなら `PrimitiveRendererComponent renderer; renderer.shape = PrimitiveShape::Sphere; renderer.material.color = {1, 0, 0, 1}; gameObject.AddComponent(renderer);`（`GameComponent.h` に入っている）。何個も作るなら Prefab の方が楽（[3. Entity](03-Entity.md#prefab同じ-entity-を何個でも作る)）。

## 横から見た形の足場（Polygon Shape・Polygon Renderer）

**Hierarchy の ＋ → 3D Object → Polygon** で、形・見た目・当たりの3つが付いた足場ができる（初めは横 4m・高さ 1m の四角）。横から見た形（多角形）を、奥（Z）へ押し出した形。横から見るゲームの、斜め・台形・とがった岩のような足場に使う。

| Component | 中身 |
|---|---|
| **Polygon Shape**（Rendering3D） | 形だけ。外周の点（Entity から見た X・Y。3〜32 個）と Depth（奥行き）。Inspector の **Edit Shape** を押すと、Scene ビューで点を動かせる |
| **Polygon Renderer**（Rendering3D） | 見た目。前の面と横の面を描く（後ろの面は描かない）。Material は Primitive Renderer と同じ。UV Scale は m（2 なら 2m に1枚）。**World Space UV**：テクスチャをワールドの位置で貼る（足場を動かしても伸ばしても絵が伸びず、隣とつながる）。Side Shade：横の面の明るさ |
| **Collider**（Shape：**Polygon**） | 当たり。同じ Entity の Polygon Shape の点を使う。**球・カプセルとだけ当たる**（箱・多角形どうしは当たらない）。へこんだ形もそのまま（三角形に分けないので、つなぎ目で引っかからない） |

**Edit Shape（Scene ビュー）**

| 操作 | 内容 |
|---|---|
| ■ をドラッグ | 点を動かす。0.25m に吸い付く（Ctrl を押している間は吸い付かない）。押しただけでは動かない |
| 辺の真ん中の ＋ を押す | そこに点を足す（そのままドラッグできる。32 個まで） |
| 点を押して Delete | その点を消す（3 個より少なくはできない） |
| Esc・Finish Editing | 編集を終わる（Polygon Shape の無い物を選んでも終わる） |
| Ctrl+Z | ドラッグ・足す・消すを1回ずつ戻す |

- 辺どうしが交差すると、線が赤くなって描かれない（当たりはそのまま）。
- Entity の回転は **Z だけ**使う（X・Y で回すと、見た目は回るが当たりは回らない）。
- 形・置き場所が変わった時だけメッシュを作り直すので、止まっている足場は何個あっても三角形に分け直さない。

## 向きを示す矢印の列（Arrow Strip）

「> > > >」と流れる矢印。線に沿って並ぶ（曲げられる）。Hierarchy の ＋ → 3D Object → **Arrow Strip**、または Add Component → Rendering3D → Arrow Strip。

| 項目 | 中身 |
|---|---|
| 線（Path の Point） | Entity から見た X・Y の点（2〜16 個）。点を順に、なめらかに通る曲線（Catmull-Rom）。2 点ならまっすぐ。初めは右へ 3.4m |
| Edit Path | Scene ビューで点を動かす（操作は Polygon Shape の Edit Shape と同じ。2 個より少なくはできない）。点を結ぶ薄い線と、実際の曲線を描く |
| Start Offset | 線の始まりから、矢印を並べ始めるまでの長さ（m。線に沿って測る。円の外から並べるときなど） |
| Spacing | 矢印の間（m。線に沿って測る）。並べる長さがちょうど割り切れる間に直す（数は長さで決まる。64 個まで） |
| Size・Thickness | 矢印1つの長さ・高さ（m）・線の太さ |
| Scroll Speed | 流れる速さ（m/秒。0 で止まる、マイナスで逆）。端から出た矢印は反対の端から出てくる |
| Fade | 両端で消していく長さ（並べた長さに対する割合。0〜0.5）。**Opaque は小さくして**、Transparent は A を、Additive は RGB を下げて消す |
| Material | Primitive Renderer と同じ（初めは加算・Unlit の緑）。Texture を選ぶと矢印1つに1枚貼る |

- **停止中も流れる**（何も掛けていない時間で動かす）。Inspector・Edit Path で変えると、Scene ビューですぐ見られる。
- 子に置けば親と一緒に回る。
- 線の計算は `PathCurve`（`Math/Geometry/PathCurve.h`）。`ArrowStripSystem::BuildPath(strip, worldMatrix, path)` でワールド座標の線を作り、`path.Evaluate(長さ)` で位置と向きを聞ける。

## 箱の中の床・壁に塗る（Decal Renderer）

**Add Component → Rendering3D → Decal Renderer**（`DecalRendererComponent`）。箱の中にある不透明な物（床・壁・段差）に、マテリアルで塗る（Unity の Decal Projector）。影・血のあと・魔法陣・範囲の印に。

| 項目 | 中身 |
|---|---|
| 箱 | Transform の大きさ（Scale 1 で1辺 1。中心が Entity の位置）。上（ローカルの +Y）から下へ貼り、XZ がテクスチャの UV。Scene ビューに箱が出る |
| Material | マテリアルのファイル（`.material`。Project の Create → Material。Shader は `category: Decal` の物）。選ばなければエンジンの既定（白い画像を貼る） |
| Progress | マテリアルの Curve の横軸（0〜1）。Script・Timeline から動かして、色・大きさを時間で変える |
| Order | 重なったときの順（大きい方が上に塗られる） |

- 塗られたくない物は、Model Renderer・Primitive Renderer の **Receive Decals** を外す（不透明な物だけに塗る。半透明には塗らない）。
- 攻撃の予告を床に塗る Hitbox Telegraph も同じ仕組み（[25章](25-HitDamage.md#床の予告hitbox-telegraph)）。
- マテリアルの欄は Shader の `#PROPERTIES` から作られる。Project でファイルを選ぶと Inspector で直せる。

## 霧（Fog）

**Add Component → Rendering3D → Fog**（`FogComponent`）。シーンに1つ（どの Entity でもよい。2つ以上あれば先に見つかった物）。全部の 3D の物（モデル・Primitive・Polygon・Arrow Strip・Sprite Renderer の板・パーティクル）と空（Skybox）の色に、最後に混ぜる。停止中のエディタでも掛かる。

| 項目 | 中身 |
|---|---|
| Color・Start Distance・Density・Max Opacity | **距離の霧**。カメラから Start Distance（m）より遠い所が、`1 − exp(−Density × 距離)` の割合で Color に近づく（Max Opacity まで） |
| Height Color・Height Base・Height Falloff・Height Max Opacity | **高さの霧**。Height Base（ワールドの Y）より下で一番濃く、上へ `exp(−(Y − Base) × Falloff)` で薄くなる（Height Max Opacity まで）。穴の底を暗くする |

- 空は無限に遠いので、距離の霧はいつも Max Opacity。下を向いた空には高さの霧が一番濃く掛かる（背景の板の下からのぞいても暗い）。
- 色は sRGB で選ぶ（色見本どおりに出る）。混ぜる計算はリニア。

> **色の空間**：Inspector・コードで書く色（Primitive・Sprite・Image・Text・ライト・霧・パーティクル・マテリアルの Ambient〜Emissive）は全部 **sRGB**（見た目の値）。Renderer がシェーダーへ送るときに `Math/Color.h` の `SrgbToLinear` でリニアに直す。画像も sRGB として読むので、色と画像は同じ空間で掛け合わさる。A は直さない。モデルの mtl の値はファイルのまま。
> **色は全部 float の `Vector4`**（Renderer の Config・線の色も。1色8ビットの 0xRRGGBBAA はやめた）。1 より明るくする（光らせる）ときは、色は 0〜1 のまま **Intensity**（倍率）を上げる。Renderer へ渡すときは `ApplyIntensity(色, 倍率)`（リニアで倍にする。sRGB の値にそのまま掛けない）。
- Scene ビューだけ切れる（View → Scene View → Fog）。
- シェーダーでは `#include "Buffers/Camera.hlsli"` の後に `#include "Fog/Fog.hlsli"` を書いて、色が決まった後に `ApplyFog(色, ワールドの位置)`（空は `ApplySkyFog(色, 向き)`）。値はカメラの定数バッファ（b0）の後ろ（C++ の `FogData`）。

## 被写界深度（Depth Of Field）

**Add Component → Rendering3D → Depth Of Field**（`DepthOfFieldComponent`）。シーンに1つ（Fog と同じく、どの Entity でもよい）。ピントの合った奥行きだけはっきり見え、手前と奥がぼける。ポストエフェクト（ブルームより先）。停止中のエディタでも掛かる。

| 項目 | 中身 |
|---|---|
| Focus | **FollowTarget**：メインカメラの Camera Follow・Camera Orbit の Target（遊ぶキャラ）の奥行きにピント（ビューごとに、そのカメラから測る）／**Distance**：カメラから Focus Distance（m） |
| Focus Range | ピントの前後この奥行き（m）までは、ぼけない |
| Far Transition・Near Transition | ピントの範囲の奥・手前から、一番ぼけるまでの奥行き（m）。Near を 0 にすると手前はぼかさない |
| Max Blur | 一番ぼけた所の、ぼけの半径（ピクセル。高さ 720 の画面で。上限 32） |

- ぼけ方は**深度**で決まる。奥行き（深度）を書かない物は、後ろの物と同じだけぼけてしまうので、**半透明（Primitive の Surface が Transparent・Additive）・線・パーティクルは、ぼかした後に描く**（ぼけない）。Particle System は **Use Depth Of Field** に印を付けると、ぼかす前に描いて一緒にぼける（ぼけ方は粒の後ろにある物の距離で決まるので、奥の層の葉・湯気向け）。背景の Sprite Renderer の板は **Alpha Clip を入れる**（入れないと深度を書かず、空と同じだけぼけてしまう）。
- Scene ビューだけ切れる（View → Scene View → Depth of Field。Post Effects を切っていると掛からない）。
- 作り：CS で半分の大きさに縮めてぼけの大きさを作る → CS で黄金角のうずまきに読んでぼかす → PS で元の絵と混ぜる。

## ブルーム（Settings → PostEffect → Bloom）

明るい所をにじませる。シーン全体に1つ（Component ではなく Settings。`Parameters/PostEffect/Bloom.json` に保存）。

| 項目 | 中身 |
|---|---|
| Enabled・Threshold・Knee・Intensity | 掛けるか／これより明るい所だけ光る／しきい値の柔らかさ／合成の強さ |
| Mode | **Dual**：縮めながらぼかし、戻しながら足す（芯の周りに強く）／**Kawase**：川瀬式（MGF）。1/4〜1/32 の段ごとにガウスでぼかして全部足す（外へ広く薄く届く） |
| Weight1〜4 | Kawase の段ごとの重み（1/4・1/8・1/16・1/32）。Weight1 を上げると芯が強く、Weight3・4 を上げると遠くまでにじむ |

- シーンの絵は HDR（float）なので、Threshold 1 なら「1 を超えた所だけ」光る＝ **Intensity を 1 より大きくした物だけ光る**（Additive の物・Add の粒の重なり・強いハイライトは Intensity 1 でも 1 を超えることがある）。

## 露出・トーンマップ（Settings → PostEffect → ToneMap）

3D は HDR（`R16G16B16A16_FLOAT`）の絵に描き、最後のパス（レンズ）で画面の 0〜1 に収める。レンズや Post Effects を切っていても、いつも掛かる（`Parameters/PostEffect/ToneMap.json`）。2D（Sprite・Text・UI）はその後の画面（SDR）に重ねる。

| 項目 | 中身 |
|---|---|
| Mode | **None**：1 で切るだけ（前の見た目。初めの値）／**Reinhard**：x/(1+x)。飛ばないが暗く淡い／**ACES**：映画の標準の近似。明るい所は柔らかく白へ、暗い所は締まる／**GT**：グランツーリスモ（内村式）。真ん中はそのまま、明るい所だけ柔らかく（アニメ調に向く） |
| Exposure | トーンマップの前に掛ける倍率（2 で1段明るい）。ブルームの光り方は変わらない |
| Dither | 1（初めの値）なら、画面の 256 段に丸める前に1段より小さい粒を足して、ビネット・空のようななめらかな明るさが帯（楕円の等高線）に見えないようにする。どのビューでも掛かる。R・G・B に同じ粒なので白黒の絵は灰色のまま |

- 光らせる物：Primitive Renderer（Polygon Renderer・Elastic Surface・Arrow Strip も）・Particle System・Model Renderer・Sprite Renderer・Trail の **Intensity**（Color の下）。
- CS でテクスチャを読み書きするとき：`ComputeTexture`（UAV と SRV を持つ）、`ComputePipeline::SetTextureTable`（`gTextures[]`）・`SetUnorderedAccessTexture`（`RWTexture2D`）。

## 色の調整（Settings → PostEffect → ColorAdjust）

露出の後・トーンマップの前に掛ける（Unity の Color Adjustments と同じ順。`Parameters/PostEffect/ColorAdjust.json`）。UI（Sprite・Text・Image など）には掛からない。

| 項目 | 中身 |
|---|---|
| Saturation | 彩度。1 でそのまま（初めの値）・0 で白黒（同じ明るさの灰色。Rec.709 の重み）・1 より大きいと色が濃くなる |

- Game ビュー・製品版・Scene ビューに掛かる。View → Scene View → Post Effects を切ると、Scene ビューは外れる。
- カメラごとに変えるなら Camera Volume の Color Adjust → Saturation（切り替えで補間する）。作りは `../Tasks/TriggerSaturationScreenPosition.md` のパートB。

## 図形・スプライト

Entityにしないその場限りの描画は `Renderer` を直接呼ぶ（`Draw()` の中で）。

```cpp
Renderer::SphereConfig config;
config.transform.translation = {0.0f, 1.0f, 0.0f};
config.transform.scale = {0.5f, 0.5f, 0.5f};
config.color = {1.0f, 0.0f, 0.0f, 1.0f}; // RGBA（0〜1）
config.shadingType = ShadingType::Lambert;
config.camera = GetCamera().get();
Renderer::DrawSphere(config);
```

どの `***Config` も `transform`（拡縮・回転・移動）・`color`・`textureHandle`・`shadingType`・`blendMode` を同じ名前で持つ。

`DrawModel` / `DrawSprite` / `DrawSprite3d` / `DrawTriangle` / `DrawSphere` / `DrawAABB` / `DrawOBB` / `DrawLines` / `DrawRect3d` / `DrawQuad2d` / `DrawQuad3d` / `DrawParticle` / **`DrawMesh`** がある（ほかに Text・UI・デカール用の `DrawText2d` / `DrawVectorShape` / `DrawPolyline` / `DrawDecal` もあるが、ふつうは Component で描く）。

- `Renderer::DrawLines` の線は、3D の絵（HDR）に描くので、ブルーム・トーンマップ・色の調整を受ける。デバッグの線を**選んだ色のまま**見たいときは `config.isGizmo = true`（ポストエフェクトの後に描く。物の後ろは描かない。`depthMode = DepthMode::Disable` なら物の後ろでも見える）。エンジンのギズモ（Collider・Hit Shape など）は全部 `isGizmo`。

`DrawMesh` は頂点と番号を自分で並べる形（毎フレーム形が変わる物。軌跡の帯は Trail がこれで描いている）。頂点はワールド座標、既定は両面描き。

```cpp
std::vector<Vertex3dData> vertices = {
	{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // 位置（w は 1）, UV, 法線
	{{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	{{0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
};
std::vector<uint32_t> indices = {0, 1, 2}; // 3つで三角形1つ
Renderer::MeshConfig config;
config.color = {1.0f, 0.0f, 1.0f, 1.0f};
config.camera = GetCamera().get(); // Camera*（GetCamera は shared_ptr を返す）
Renderer::DrawMesh(config, std::move(vertices), std::move(indices)); // move で渡すと配列を写さない
```

```cpp
// ✗ だめな例
void GameScene::Update() {
	Renderer::DrawSphere(config); // ✗ 描画は Draw() で積む（Updateで積むと停止中に消える）
}
```
