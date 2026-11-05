参考：
- glTF 2.0 のマテリアル（baseColorFactor × baseColorTexture・色はリニア・alphaMode と alphaCutoff）：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials
- Unity URP の Lit（Base Map と Base Color を掛ける）：https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/manual/lit-shader.html
- Unity URP の LitInput.hlsl（面の色を作る所を Shader の間で共通にしている。`InitializeStandardLitSurfaceData`）：https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/LitInput.hlsl
- Unity の Renderer.materials（メッシュごとのマテリアルの枠。今回は作らない）：https://docs.unity3d.com/ScriptReference/Renderer-materials.html
- assimp 6.0.4 の glTF の読み込み（baseColorFactor の A を OPACITY にも入れる）：https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/glTF2/glTF2Importer.cpp
- assimp 6.0.4 の OBJ のマテリアル（Kd が無いときの 0.6）：https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/Obj/ObjFileData.h
- MTL の書き方（Kd・map_Kd・d）：https://paulbourke.net/dataformats/mtl/
- HLSL の定数バッファの並び方（16 バイトの行）：https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
- `std::unordered_set::contains`：https://en.cppreference.com/w/cpp/container/unordered_set/contains
- `std::basic_string_view::starts_with`・`remove_prefix`：https://en.cppreference.com/w/cpp/string/basic_string_view/starts_with ・ https://en.cppreference.com/w/cpp/string/basic_string_view/remove_prefix
- `ImGui::ColorButton`：https://github.com/ocornut/imgui/blob/master/imgui.h

# モデルのマテリアルの色（Base Color）

モデルのマテリアルの色（glTF の baseColorFactor・OBJ の Kd）が描画に使われておらず、色だけのマテリアルの部品が真っ白になっていた。読み込み・シェーダー・Renderer・Inspector の4つの層で、色の扱いを1つの形にそろえる。

- **読み込みの層**（`ModelManager`）：ファイルの形ごとの違い（OBJ の Kd・d、glTF の baseColorFactor・alphaMode）を、ここだけで `ImportedMaterial` の1つの形にそろえる。色は**リニア**で持つ。マテリアルは**番号**で持つ（後で作るメッシュごとの枠の番号になる）
- **シェーダーの層**（`Model/ModelSurface.hlsli`）：面の色（**tint × Base Color × テクスチャ**）と切り抜きを1か所で作る。6つの PS とこれから作るトゥーンは、光の当て方だけを書く
- **描く層**（`Renderer`）：「枠1つにどのマテリアルを使うか決める」所と「GPU の形に詰める」所を分ける。後で .material の枠を足すときは、決める所だけを直す
- **エディタの層**（Inspector・保存）：色は今までどおり **sRGB**。読み込んだマテリアルを一覧で見せ、上書きの値へ写せる

パートは **A → B → C → D の順に写す**。名前を変える所（`MtlMaterial` → `ImportedMaterial`・`diffuse` → `baseColor`）がパートをまたぐので、**途中ではビルドできない。D まで写してから1回ビルドする**。E（グリッドと Sandbox のテスト）はいつ写してもよい。F は AL4_01 のサブモジュールを進めるときに行う。

ブランチは `feature/ModelBaseColor`（develop から）。

## 0章 全体の形

### 0.1 層と流れ

```
読み込み（ModelManager）
  .gltf / .obj ──assimp──→ ParseMaterial ──→ ImportedMaterial（色はリニア・A は OPACITY から）
                            ├ glTF：alphaMode が OPAQUE なら A = 1（glTF の決まり）
                            ├ OBJ：.mtl に Kd が無いマテリアルは Base Color = 1（assimp の 0.6 を直す）
                            └ どのメッシュも使わないマテリアルを外して、番号を詰める
  ModelAsset::materials[番号]  ←  SubMesh::materialIndex（番号＝マテリアルの枠）

描く（Renderer::DrawModel）
  メッシュごとに
    ResolveModelMaterial(config, mesh.materialIndex)   ← 枠1つのマテリアルを決める（Override なら Inspector の値）
      └→ MaterialValues（色はリニア）
    MakeMaterial3dData(values, tint = Color × Intensity, …) ← GPU の形（Material3dData・b20）に詰める

シェーダー（Model の PS）
  GetModelSurface(uv)  ← ModelSurface.hlsli
    テクスチャを読む → 切り抜き（Base Color の A × テクスチャの A）→ 面の色 = tint × Base Color × テクスチャ
  その後の光の当て方だけを PS ごとに書く（Unlit・Lambert・HalfLambert・Phong・BlinnPhong・PBR・これからのトゥーン）
```

色の空間の境目は1本だけ：**Component・Inspector の色は sRGB、読み込んだ値と GPU の値はリニア**。sRGB → リニアは Renderer（`SrgbToLinear`）、リニア → sRGB は Inspector の Copy（`LinearToSrgb`）だけで行う。

### 0.2 作る物・直す物

| パート | ファイル | 何をするか |
|---|---|---|
| A | `Graphics/Model/ModelManager.h`・`.cpp` | `MtlMaterial` → `ImportedMaterial`（`baseColor` は Vector4・alphaMode）。名前の表 → 番号の配列。Kd の有無を .mtl から読む。使われないマテリアルを外す |
| B | `Shader/Data/Model/ModelSurface.hlsli.shader`（新しい） | マテリアルの構造体（b20）と、面の色を作る `GetModelSurface` |
| B | `Shader/Data/Model/` の6つの PS | 構造体とテクスチャの読み方を消して、`GetModelSurface` を呼ぶ |
| B | `Graphics/Pipeline/ShaderConstants.h` | `Material3dData` を `tint`・`baseColor` にして、大きさを static_assert で止める |
| C | `Graphics/Pipeline/RenderStates.h` | `MaterialParams::diffuse` → `baseColor`（Vector4） |
| C | `Graphics/Renderer/Renderer.h`・`.cpp` | `MaterialValues`・`ResolveModelMaterial`・`MakeMaterial3dData`（`MakeModelMaterial` と `MakeDefaultModelMaterial` の代わり） |
| C | `Systems/Rendering3D/ModelRenderSystem.cpp` | Model Renderer の Intensity を掛ける（今まで効いていなかった） |
| D | `Math/Color.h` | `LinearToSrgb` の Vector3・Vector4 版 |
| D | `Editor/Inspector/ModelRendererEditor.cpp` | Base Color の欄・読み込んだマテリアルの一覧（Copy） |
| D | `Components/EngineComponents.cpp`・`ModelRendererComponent.h` | 保存のキー `baseColor`・コメント |
| E | `Resources/Model/Grid/grid.mtl` | Kd 0.8 → 1.0 |
| E | `Sandbox/Resources/Model/BaseColorTest/`・`scenes/ModelBaseColorTest.scene.json`（新しい） | テストのモデルとシーン |

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 物の色の式 | **Color（Model Renderer・物ごと）× Base Color（マテリアル）× テクスチャ**。RGB も A も掛ける | glTF の `baseColorFactor × baseColorTexture`、Unity の `_BaseColor × _BaseMap` と同じ |
| 名前 | GPU の `color` → `tint`、`diffuse` → `baseColor`（Vector4）。`MaterialParams`・読み込んだマテリアルも `baseColor` | Diffuse は Phong のころの名前。PBR・トゥーンにも使う「物の色」 |
| 掛ける場所 | **シェーダー**（tint と Base Color を別々に送る） | .material の枠を作ると、Base Color はマテリアルの定数バッファ、tint は物ごとのデータに分かれる。CPU で先に掛けると、そのときに分け直す |
| 共通の .hlsli | `Model/ModelSurface.hlsli`（構造体＋`GetModelSurface`） | 6つの PS・トゥーン・将来の .material の Model の Shader が include するだけで同じ色になる |
| 切り抜き | **Base Color の A × テクスチャの A** で比べる。tint の A は入れない | glTF・Unity と同じ。Color の A でフェードさせたとき、穴が開いていく見た目にならない |
| 色の空間をそろえる所 | **読み込み（ParseMaterial）だけ**。読み込んだ色は全部リニア。OBJ の Kd もリニア | glTF は決まりでリニア。Blender の OBJ の書き出しも中の値（リニア）をそのまま書く。assimp も変換しない |
| Inspector の色 | **sRGB のまま**（Renderer で直す）。Copy は `LinearToSrgb` | エンジンのほかの色と同じ決まり。境目は1本 |
| A（不透明度） | **Base Color の A に1つ**。RGB は COLOR_DIFFUSE、A は OPACITY から | glTF は COLOR_DIFFUSE の4つ目にも OPACITY にも同じ A が入る（両方掛けると2回掛かる）。`dissolve` と「上書きでも掛ける」特別扱いは消す |
| glTF の alphaMode | **記録だけ**（OPAQUE なら読み込みで A = 1） | 使うのは .material の枠を作るとき。今使うと切り抜き・ブレンドを決める物が2つになる |
| OBJ で Kd が無い | **.mtl を読んで調べ、無ければ Base Color = 1** | assimp は 0.6 を入れ、書いてあったかを外に出さない。Blender はテクスチャを使うマテリアルに Kd を書かないので、0.6 のままだとテクスチャが暗くなる |
| マテリアルの持ち方 | **番号の配列**（`materials`・`SubMesh::materialIndex`）。名前は表示用 | glTF は同じ名前を許す（名前の表だと後の物が前を消す）。番号はそのままメッシュごとの枠になる |
| 使われないマテリアル | **読み込みで外して番号を詰める** | assimp が足す `DefaultMaterial`（OBJ）・名前なし（glTF）が、一覧・枠に出ないように |
| 名前 | `MtlMaterial` → `ImportedMaterial` | もう OBJ（mtl）だけの物ではない。.material（アセット）とも区別できる |
| 決める所と詰める所 | `ResolveModelMaterial`（枠1つ）と `MakeMaterial3dData` に分ける | .material の枠を足すときに、決める所だけを直せば済む |
| Override | **今のまま全メッシュに同じ値**（Base Color が入るだけ）。メッシュごとの枠は .material の載せ替えで作る | 今メッシュごとの値の置き場所を作ると、枠が来たときに捨てることになる |
| 読み込んだマテリアルの見せ方 | 「Copy From Model」（1つ目のメッシュ）をやめて、**番号順の一覧**（色の見本・テクスチャの有無・Copy） | 1つ目はたまたまの並び。一覧は枠の一覧になる部分 |
| 切り抜き・ブレンド | **今は Component のまま** | MaterialTelegraph で Blend などはマテリアルが持つと決めた。移すのは載せ替えのとき |
| Primitive | **Primitive Material の Color を Base Color**、tint は白 | Primitive には読み込んだマテリアルが無く、Primitive Material そのものがマテリアル |
| Model Renderer の Texture | 今のまま全メッシュのテクスチャを差し替える（Base Color は掛かる） | Unity で `_BaseMap` を替えても `_BaseColor` が残るのと同じ |
| grid.mtl | Kd 0.8 → **1.0** | グリッドの色は EditorGrid の Color で決める。0.8 は Blender の既定が残っていただけ |
| 既定の Base Color | (1, 1, 1, 1) | glTF の既定と同じ |
| 保存のキー | `baseColor`（RGBA）。古い `diffuse` は読まない | 使っていたのは AL4_01 の GameScene だけで、値も色の空間がずれた写し（F で直す） |

### 0.4 場面ごとの動作

| 場面 | どうなるか |
|---|---|
| 色だけのマテリアルの glTF（Blender から） | 部品ごとにファイルの色が出る（前は白） |
| テクスチャだけのマテリアル | テクスチャの色のまま（Base Color は 1） |
| 色 × テクスチャのマテリアル | 色の付いたテクスチャ |
| Blender から書き出した、テクスチャ付きの OBJ（Kd が無い） | テクスチャの色のまま（assimp の 0.6 で暗くならない） |
| OBJ で Kd と map_Kd の両方がある | Kd × テクスチャ（MTL の決まり） |
| mtl の無い OBJ | 白（`DefaultMaterial` も Kd が無いので 1） |
| Model Renderer の Color を (1, 0.5, 0.5) | 全部の部品に赤みが乗る（マテリアルの色 × Color） |
| Model Renderer の Intensity を 3 | 3 倍明るくなる（前は効いていなかった） |
| Override を入れる | 全部のメッシュが Base Color の色になる（テクスチャは読み込んだ物のまま） |
| Color の A を下げてフェード | 切り抜きは変わらず、全体が薄くなる |
| Alpha Cutoff を 0.5 | Base Color の A × テクスチャの A が 0.5 以下の所を捨てる |
| glTF の alphaMode が OPAQUE で、Blender で A を下げていた | 不透明のまま（glTF の決まり） |
| Primitive Renderer | 今までと同じ見た目 |
| エディタのグリッド | 今までと同じ見た目（grid.mtl の Kd を 1.0 にしたので） |
| Inspector の Model Materials の Copy | その色（sRGB に直した値）が Base Color に入り、Override が入る |
| 前のシーンの `"diffuse"` | 読まない（Base Color は白のまま） |

### 0.5 今回やらないこと

3章にまとめた（メッシュごとの枠・トゥーン・名前を変えた値を読む仕組み・頂点カラー・読み込み設定 など）。

## 1章 解説

### 1.1 物の色の式と、どこで掛けるか

物の色は3つの掛け算にする。

```
物の色 = tint（物ごと） × Base Color（マテリアル） × テクスチャ
```

- **tint** は Model Renderer の Color × Intensity。同じモデルを何体も置いて、1体だけ赤くしたいときに使う（Unity の MaterialPropertyBlock で `_BaseColor` を上書きするのと同じ役目）
- **Base Color** はマテリアルの色。glTF の baseColorFactor・OBJ の Kd
- **テクスチャ** はマテリアルの色のテクスチャ。glTF の baseColorTexture・OBJ の map_Kd

CPU で `tint × Base Color` を先に掛けて1つの値で送ることもできるが、そうしない。後で .material の枠を作ると、Base Color は「マテリアルの定数バッファ」（同じマテリアルを使う物は全部同じ値）、tint は「物ごとのデータ」に分かれる。今のうちに別々の値で送っておけば、そのときに入れ物を分けるだけで済む。

掛け算はシェーダーの `GetModelSurface`（`ModelSurface.hlsli`）の1か所だけで行う。今まで6つの PS がそれぞれマテリアルの構造体とテクスチャの読み方を書いていた（しかも Lambert だけ metallic・roughness が無いなど、形が少しずつ違っていた）。これからは「面の色を作る所」と「光を当てる所」に分けて、PS は光の当て方だけを書く。Unity URP も `LitInput.hlsl` の `InitializeStandardLitSurfaceData` で面の色を作り、光の当て方は別に書いている。これからトゥーン（ランプ）を足すときも、`GetModelSurface` を呼んだ後にランプの計算を書くだけになる。

切り抜きは **Base Color の A × テクスチャの A** で比べ、tint の A は入れない。tint の A は「物を薄くする」ために使う値なので、これで比べると、フェードの途中で穴が開いていく見た目になる。

### 1.2 色の空間（どこでリニアにそろえるか）

シェーダーの中の計算は全部リニア（光の強さに比例する値）で行う。入ってくる色は3つある。

| 入ってくる所 | 色の空間 | どこでリニアにするか |
|---|---|---|
| テクスチャ（png） | sRGB | GPU（`_SRGB` の形で読むので、シェーダーに届くときにはリニア） |
| モデルファイルの色（Kd・baseColorFactor） | **リニア**（glTF は決まり、OBJ は Blender の書き出しに合わせて） | 直さない（読み込みでリニアのまま持つ） |
| Inspector の色（Color・Base Color など） | sRGB | Renderer（`SrgbToLinear`） |

OBJ の Kd には決まりが無く、ツールによっては sRGB の値を書く。エンジンでは「Blender が書くリニアの値」として扱う。画面の色がファイルとずれて見えたら、エンジンではなく、書き出した側の値を直す。

Blender の版ごとの Kd（2026-11-06 に Blender のソースで確かめた）：

| Blender の版 | OBJ の書き出し | Kd に書く値 | テクスチャがあるとき |
|---|---|---|---|
| 2.7x | Python の書き出し | `diffuse_intensity × diffuse_color`（リニア。Intensity の既定 0.8 が掛かる） | Kd も map_Kd も書く |
| 2.8〜3.2 | Python の書き出し（3.1・3.2 は新しい物が experimental で並ぶ） | Principled BSDF の Base Color の欄の値（リニア） | Kd も書く（つないでいても欄の値。既定なら 0.8） |
| 3.3〜 | C++ の書き出し（`wm.obj_export`。Python の物は legacy） | Base Color の欄の値（リニア） | **Kd を書かない**（map_Kd だけ） |

どの版もリニアの値を書き、sRGB に直して書く版は無い。Blender の OBJ の読み込みも、Kd をそのまま Base Color に入れる（リニアとして扱う）。ただし **3.2 までの Blender で書き出したテクスチャ付きの OBJ** は、Kd に使われていない欄の値（既定 0.8）が入るので、このエンジン（MTL の決まりどおり Kd × テクスチャ）では少し暗くなる。3.3 以降で書き出し直すか、mtl の Kd を 1 に直す。

Kd を sRGB の値として読むツールの例：three.js の MTLLoader（`ColorManagement.toWorkingColorSpace(…, SRGBColorSpace)` で、Kd・Ka・Ks・Ke を sRGB → リニアに直している）。Maya・3ds Max の OBJ の書き出しが Kd をどちらで書くかは、確かめていない（どちらも色の管理の設定で、色の欄の値の意味が変わる）。

Inspector の「Copy」は、読み込んだリニアの値を sRGB の欄に写すので、`LinearToSrgb` を通す。前の「Copy From Model」は直さずに写していたので、写した色が明るく見えていた。

### 1.3 A（不透明度）と alphaMode

assimp は A を2か所に入れる。

| ファイル | COLOR_DIFFUSE | OPACITY |
|---|---|---|
| glTF | baseColorFactor の RGBA（4つ） | baseColorFactor の A |
| OBJ | Kd の RGB（3つ） | d |

COLOR_DIFFUSE を4つで読んでから OPACITY を掛けると、glTF では A を2回掛けることになる。そこで「RGB は COLOR_DIFFUSE の3つ、A は OPACITY」と決めて、どちらのファイルも同じ書き方で読む。

glTF の alphaMode は、OPAQUE（不透明）・MASK（切り抜き）・BLEND（半透明）の3つ。glTF の決まりでは、OPAQUE のときは A を使わない。Blender で A を下げても、ブレンドの設定が不透明なら不透明のまま書き出されるので、読み込みで A を 1 にする。MASK の切り抜きの値（alphaCutoff）と BLEND は、今は `ImportedMaterial` に記録するだけにする。今は切り抜き・ブレンドを Model Renderer の欄で決めているので、ここでファイルの値も使うと、決める物が2つになる。マテリアルの枠を作るときに、枠のマテリアルへ渡す。

### 1.4 OBJ で Kd が書かれていないとき

assimp の OBJ の読み込みは、マテリアルの色の既定を 0.6 にしている（`ObjFileData.h` の `diffuse = aiColor3D(0.6f, 0.6f, 0.6f)`）。Kd が書かれていてもいなくても COLOR_DIFFUSE に値が入るので、「0.6 と書いてある」のか「書いていない」のか、assimp の結果からは分からない。

Blender の OBJ の書き出しは、Base Color にテクスチャをつないだマテリアルに **Kd を書かない**（`map_Kd` だけを書く）。そのままだと、Blender で作ったテクスチャ付きの OBJ が、全部 0.6 倍に暗くなる。

そこで、読み込み（`LoadObjFile`）で .obj の `mtllib` の行から .mtl を見つけ、`newmtl` ごとに Kd の行があるかだけを自分で調べる（`FindMaterialsWithDiffuse`）。Kd が無いマテリアルは Base Color を 1 にする（「色を掛けない」。glTF の baseColorFactor の既定と同じ）。Kd があれば、MTL の決まりどおり Kd × テクスチャ。

1行を読む `ReadKeyword` は、行の頭の空白を飛ばし、`key` の直後が空白のときだけ後ろを返す（`Kd` が `Kdx` のような行に当たらないように）。行の終わりの `\r`（CRLF のファイル）も空白として除く。`mtllib` の後ろは、assimp と同じく「行の残り全部を1つのファイル名」として読む（空白を含むファイル名を読めるように）。

### 1.5 マテリアルの番号（＝メッシュごとの枠の番号）

今まではマテリアルを名前の表（`std::map<std::string, MtlMaterial>`）で持ち、メッシュは名前で指していた。これを番号の配列にする。

- glTF は同じ名前のマテリアルを許す。名前の表だと、後から読んだ物が前の物を消す
- 描くたびに、メッシュごとに名前の文字列で探していた
- assimp はもともと番号（`mMaterialIndex`）でマテリアルを指している。その番号をそのまま使う

番号はそのまま、後で作る「メッシュごとのマテリアルの枠」（Unity の `Renderer.materials` の Element 0, 1, …）になる。Inspector の一覧も番号順に出す。

assimp は、どのメッシュも使わないマテリアルを足すことがある（OBJ の `DefaultMaterial`、glTF の名前の無い物）。これが枠に出ないように、読み込みの最後に使われないマテリアルを外し、ファイルの並びのまま番号を詰めて、メッシュの番号を付け直す（`RemoveUnusedMaterials`）。

### 1.6 定数バッファの並び（160 バイト）

HLSL の定数バッファは、16 バイト（float4 の1つ）の行に詰めて並ぶ。float3 の後ろに float を1つ置くと、同じ行に入る。C++ の `Material3dData` と `ModelSurface.hlsli` の `ModelMaterial` を同じ並びにしておく。

```
行 0   tint（float4）
行 1   baseColor（float4）
行 2〜5 uvTransform（float4x4）
行 6   ambient（float3）＋ alphaCutoff
行 7   specular（float3）＋ shininess
行 8   emissive（float3）＋ textureIndex
行 9   metallic・roughness・pad・pad
→ 10 行 × 16 バイト = 160 バイト
```

C++ の側に `static_assert(sizeof(Material3dData) == 160)` を置き、並びを変えて大きさが変わったらビルドで止まるようにする（.hlsli も同じに直すのを忘れないように）。

### 1.7 あまり使わない書き方

- `std::unordered_set<std::string>::contains(name)`：入っているかだけを調べる（C++20）。`find(name) != end()` と同じ
- `std::string_view::starts_with(key)`・`remove_prefix(n)`：文字をコピーせずに、頭が key か調べる・頭を n 文字飛ばす（C++20）
- `std::ranges::transform(s, s.begin(), f)`：文字を1つずつ f で書き換える（拡張子を小文字にする。AssetInspector と同じ書き方）
- `ImGui::ColorButton("##id", color, ImGuiColorEditFlags_NoPicker)`：色の見本だけを出す（押しても色を選ぶ窓は出ない）

## パートA 読み込み（ImportedMaterial・番号・色の空間・A・Kd の有無）

### A-1 `Graphics/Model/ModelManager.h`

`#include <map>` を消す（名前の表をやめるので使わない）。

`MtlMaterial` の構造体を、次の2つに差し替える。

```cpp
	// ===== 透け方（glTF の alphaMode。OBJ には無いので Opaque）=====
	enum class AlphaMode {
		Opaque, // 不透明（Base Color の A は使わない）
		Mask,   // 切り抜き（A が alphaCutoff 以下のピクセルを捨てる）
		Blend,  // 半透明（A の分だけ透ける）
	};

	// ===== モデルファイルから読み込んだマテリアル（OBJ の mtl・glTF の material を、エンジンの1つの形にそろえた物）=====
	// 色はどのファイルから読んでもリニア（glTF は決まりでリニア。OBJ の Kd も、Blender が書くリニアの値として扱う）
	struct ImportedMaterial {
		std::string name;                             // マテリアルの名前（表示・GPU のデバッグ名に使う。ぶつかることもあるので、引くときは番号で）
		Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 物の色（RGB：OBJ の Kd・glTF の baseColorFactor、A：OBJ の d・glTF の baseColorFactor の A）。テクスチャに掛ける
		Vector3 ambient = {0.0f, 0.0f, 0.0f};         // Ka: 環境光色
		Vector3 specular = {1.0f, 1.0f, 1.0f};        // Ks: 鏡面反射色
		Vector3 emissive = {0.0f, 0.0f, 0.0f};        // Ke: 自己発光色
		float shininess = 32.0f;                      // Ns: 鏡面反射指数
		float metallic = 0.0f;                        // 0=非金属 1=金属
		float roughness = 0.5f;                       // 表面の粗さ（0=鏡面, 1=完全拡散）
		AlphaMode alphaMode = AlphaMode::Opaque;      // 透け方（glTF の alphaMode。今は記録だけ。マテリアルの枠を作るときに Blend・切り抜きへ渡す）
		float alphaCutoff = 0.5f;                     // Mask のときの切り抜きの値（glTF の alphaCutoff。今は記録だけ）
		int32_t illum = 1;                            // illum: 照明モデル番号
		std::string baseColorTexturePath;             // 物の色のテクスチャ（OBJ の map_Kd・glTF の baseColorTexture）
		std::string ambientTexFilePath;               // map_Ka: アンビエントテクスチャ
		std::string specularTexFilePath;              // map_Ks: スペキュラテクスチャ
		std::string bumpTexFilePath;                  // map_bump/bump: バンプマップ
		uint32_t baseColorTextureHandle = 0;          // baseColorTexturePath を TextureManager::Load() で読んだ番号（0 はテクスチャ無し）
	};
```

`SubMesh` の `std::string materialName;` の行を、次に差し替える。

```cpp
		uint32_t materialIndex = 0; // 使うマテリアルの番号（ModelAsset::materials の番号＝マテリアルの枠の番号）
```

`ModelAsset` の `std::map<std::string, MtlMaterial> materialMap;` の行を、次に差し替える。

```cpp
		std::vector<ImportedMaterial> materials; // 読み込んだマテリアル（メッシュが使う物だけ、ファイルの並びのまま。番号で引く）
```

`GetMtlMaterial` の宣言（`/// <summary>` から）を、次に差し替える。

```cpp
	/// <summary>
	/// 読み込んだマテリアルを番号で取得
	/// </summary>
	/// <param name="handle">Load が返したモデルハンドル</param>
	/// <param name="materialIndex">マテリアルの番号（SubMesh::materialIndex）</param>
	/// <returns>無いモデル・番号なら nullptr</returns>
	static const ImportedMaterial* GetImportedMaterial(uint32_t handle, uint32_t materialIndex);
```

`MakeMeshBuffer` の宣言の、引数の名前を変えて説明を足す（`/// </summary>` の下）。

```cpp
	/// <param name="bufferName">バッファの名前の頭（「ファイル名/マテリアル名」。PIX などで見分ける）</param>
	static void MakeMeshBuffer(SubMesh& mesh, const std::string& bufferName);
```

### A-2 `Graphics/Model/ModelManager.cpp`

include を次に差し替える（`<map>` を消し、`<cctype>`・`<string_view>`・`<unordered_set>`・`<assimp/GltfMaterial.h>` を足す）。

```cpp
#include <cmath>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <numeric>
#include <iterator>
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <unordered_set>

#include <pix.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
```

無名の namespace（`namespace {` から `} // namespace` まで）を、全体を差し替える。`ParseAlphaMode`・`IsObjFile`・`ReadKeyword`・`FindMaterialsWithDiffuse`・`RemoveUnusedMaterials` が新しく、`ParseMaterial`・`ParseMesh` を直している（`ParseMesh` は引数と `materialIndex` の行だけ）。

```cpp
namespace {
// ===== glTF の alphaMode の文字（"OPAQUE"・"MASK"・"BLEND"）→ AlphaMode =====
ModelManager::AlphaMode ParseAlphaMode(std::string_view text, const std::string& materialName) {
	if (text == "MASK") {
		return ModelManager::AlphaMode::Mask;
	}
	if (text == "BLEND") {
		return ModelManager::AlphaMode::Blend;
	}
	if (text != "OPAQUE") {
		LogManager::Warning(std::format("{}: 知らない alphaMode \"{}\" です（OPAQUE として読みます）", materialName, text));
	}
	return ModelManager::AlphaMode::Opaque;
}

// ===== OBJ か（拡張子を小文字にして比べる）=====
bool IsObjFile(const std::filesystem::path& filePath) {
	std::string extension = ToUtf8(filePath.extension());
	std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension == ".obj";
}

// ===== OBJ・MTL の1行が key（"mtllib"・"newmtl"・"Kd" など）で始まっていれば、その後ろ（前後の空白を除いた物）を返す。違えば空 =====
// 行の頭の空白は飛ばす。key の直後は空白でないといけない（"Kd" が "Kdx" に当たらないように）
std::string_view ReadKeyword(std::string_view line, std::string_view key) {
	constexpr std::string_view kSpaces = " \t\r";
	const size_t begin = line.find_first_not_of(kSpaces);
	if (begin == std::string_view::npos) {
		return {};
	}
	line.remove_prefix(begin);
	if (!line.starts_with(key) || line.size() <= key.size() || kSpaces.find(line[key.size()]) == std::string_view::npos) {
		return {};
	}
	line.remove_prefix(key.size());
	const size_t valueBegin = line.find_first_not_of(kSpaces);
	if (valueBegin == std::string_view::npos) {
		return {};
	}
	const size_t valueEnd = line.find_last_not_of(kSpaces);
	return line.substr(valueBegin, valueEnd - valueBegin + 1);
}

// ===== OBJ の .mtl を読み、Kd（物の色）が書かれているマテリアルの名前を集める =====
// assimp は Kd が無いと 0.6 を入れ、書いてあったかを外に出さないので、ここで自分で調べる
std::unordered_set<std::string> FindMaterialsWithDiffuse(const std::filesystem::path& objPath) {
	std::unordered_set<std::string> materialNames;
	std::string line;
	// --- .obj の mtllib の行から .mtl を見つける（.obj からの相対。行の残りを1つのファイル名として読む。assimp と同じ）---
	std::vector<std::filesystem::path> mtlPaths;
	std::ifstream obj(objPath);
	while (std::getline(obj, line)) {
		const std::string_view fileName = ReadKeyword(line, "mtllib");
		if (!fileName.empty()) {
			mtlPaths.push_back(objPath.parent_path() / ToPath(std::string(fileName))); // 文字は UTF-8 なので ToPath を通す
		}
	}
	// --- .mtl の newmtl ごとに、Kd の行があるかを見る ---
	for (const std::filesystem::path& mtlPath : mtlPaths) {
		std::ifstream mtl(mtlPath);
		std::string currentName; // 今読んでいるマテリアルの名前（newmtl の後ろ）
		while (std::getline(mtl, line)) {
			if (const std::string_view name = ReadKeyword(line, "newmtl"); !name.empty()) {
				currentName = name;
				continue;
			}
			if (!ReadKeyword(line, "Kd").empty()) {
				materialNames.insert(currentName);
			}
		}
	}
	return materialNames;
}

// ===== マテリアル1つを読む（ファイルの形ごとの違いは、ここで ImportedMaterial の1つの形にそろえる）=====
ModelManager::ImportedMaterial ParseMaterial(const aiMaterial* material, const std::filesystem::path& directoryPath) {
	// テクスチャのパス（モデルファイルからの相対）を、モデルのフォルダにつなぐ
	// path の / でつなぐと、フォルダが空（"cube.obj" だけ渡した）でも先頭に / が付かない。assimp の文字は UTF-8 なので ToPath を通す
	auto makeTexturePath = [&directoryPath](const aiString& texPath) { return ToUtf8(directoryPath / ToPath(texPath.C_Str())); };
	ModelManager::ImportedMaterial mtl;
	// 名前（objのnewmtl名。ない場合assimpが"DefaultMaterial"を作る）
	aiString name;
	material->Get(AI_MATKEY_NAME, name);
	mtl.name = name.C_Str();

	// --- 色（どれもリニアのまま。glTF は決まりでリニア、OBJ の Kd なども Blender が書くリニアの値として扱う）---
	aiColor3D color;
	if (material->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
		mtl.ambient = {color.r, color.g, color.b};
	}
	// 物の色の RGB（OBJ の Kd・glTF の baseColorFactor。OBJ で Kd が書かれていないときは、assimp が 0.6 を入れるので、LoadObjFile で 1 に直す）
	if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
		mtl.baseColor = {color.r, color.g, color.b, 1.0f};
	}
	if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
		mtl.specular = {color.r, color.g, color.b};
	}
	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
		mtl.emissive = {color.r, color.g, color.b};
	}

	// --- 物の色の A（OBJ の d・glTF の baseColorFactor の A）---
	// glTF は COLOR_DIFFUSE の4つ目にも OPACITY にも同じ A が入るので、OPACITY の方だけを読む（両方掛けると2回掛かる）
	material->Get(AI_MATKEY_OPACITY, mtl.baseColor.w);

	// --- 透け方（glTF だけ。OBJ にはキーが無いので Opaque のまま）---
	aiString alphaMode;
	if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
		mtl.alphaMode = ParseAlphaMode(alphaMode.C_Str(), mtl.name);
		material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, mtl.alphaCutoff);
		// glTF の決まり：OPAQUE のときは A を使わない（Blender で A を下げても、不透明のまま）
		if (mtl.alphaMode == ModelManager::AlphaMode::Opaque) {
			mtl.baseColor.w = 1.0f;
		}
	}

	// --- スカラー ---
	material->Get(AI_MATKEY_SHININESS, mtl.shininess);
	material->Get(AI_MATKEY_METALLIC_FACTOR, mtl.metallic);
	material->Get(AI_MATKEY_ROUGHNESS_FACTOR, mtl.roughness);

	// --- テクスチャ（パスはモデルファイルからの相対なので、makeTexturePath でモデルのフォルダにつなぐ）---
	aiString texPath;
	// glTFはbaseColor、OBJ / FBXはdiffuseに入る。あった方を使う
	if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS || material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
		mtl.baseColorTexturePath = makeTexturePath(texPath);
	}
	if (material->GetTexture(aiTextureType_AMBIENT, 0, &texPath) == AI_SUCCESS) {
		mtl.ambientTexFilePath = makeTexturePath(texPath);
	}
	if (material->GetTexture(aiTextureType_SPECULAR, 0, &texPath) == AI_SUCCESS) {
		mtl.specularTexFilePath = makeTexturePath(texPath);
	}
	// OBJのmap_bump/bumpはassimpではHEIGHT、glTFはNORMALSに分類される
	if (material->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS || material->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS) {
		mtl.bumpTexFilePath = makeTexturePath(texPath);
	}
	return mtl;
}

// ===== メッシュ1つを読む =====
ModelManager::SubMesh ParseMesh(const aiMesh* mesh, const std::string& filename) {
	// --- 法線・UVが無いモデルもあるので、無ければ既定値で補う ---
	const bool hasNormals = mesh->HasNormals();
	const bool hasTexcoords = mesh->HasTextureCoords(0);
	if (!hasNormals) {
		LogManager::Warning(filename + ": 法線がないMeshです。(0,1,0)で補完します");
	}
	if (!hasTexcoords) {
		LogManager::Warning(filename + ": TexcoordがないMeshです。(0,0)で補完します");
	}
	// --- SubMesh ---
	ModelManager::SubMesh subMesh;
	subMesh.materialIndex = mesh->mMaterialIndex; // assimp のマテリアルの番号（使わないマテリアルを外したあと、RemoveUnusedMaterials が付け直す）
	subMesh.isSkinned = mesh->HasBones(); // 骨を持つメッシュ（gltfのskin）
	subMesh.vertices.reserve(mesh->mNumVertices);
	for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
		aiVector3D& position = mesh->mVertices[vertexIndex];
		Vertex3dData vertex;
		// 右手系→左手系はx反転で対処（FlipWindingOrderとセット）
		vertex.position = {-position.x, position.y, position.z, 1.0f};
		// 法線
		if (hasNormals) {
			aiVector3D& normal = mesh->mNormals[vertexIndex];
			vertex.normal = {-normal.x, normal.y, normal.z};
		} else {
			vertex.normal = {0.0f, 1.0f, 0.0f};
		}
		// UV
		if (hasTexcoords) {
			aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];
			vertex.texcoord = {texcoord.x, texcoord.y};
		} else {
			vertex.texcoord = {0.0f, 0.0f};
		}
		subMesh.vertices.push_back(vertex);
	}
	// 頂点インデックス。Faceの中身をそのまま積む
	subMesh.indices.reserve(mesh->mNumFaces * 3);
	for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
		aiFace& face = mesh->mFaces[faceIndex];
		MY_ASSERT_MSG(face.mNumIndices == 3, "ポリゴンは三角形のみサポートしています");
		for (uint32_t element = 0; element < face.mNumIndices; ++element) {
			subMesh.indices.push_back(face.mIndices[element]);
		}
	}
	return subMesh;
}

// ===== どのメッシュも使わないマテリアルを外す =====
// assimp は、どのメッシュも使わない既定のマテリアルを足すことがある（OBJ の "DefaultMaterial"・glTF の名前の無い物）
// マテリアルの番号は、そのままマテリアルの枠（Inspector の一覧）になるので、使う物だけをファイルの並びのまま詰めて、メッシュの番号を付け直す
void RemoveUnusedMaterials(ModelManager::ModelAsset& asset) {
	// --- どれが使われているか ---
	std::vector<bool> isUsed(asset.materials.size(), false);
	for (const ModelManager::SubMesh& mesh : asset.meshes) {
		isUsed[mesh.materialIndex] = true;
	}
	// --- 使う物だけを並びのまま詰め、古い番号 → 新しい番号の表を作る ---
	std::vector<ModelManager::ImportedMaterial> usedMaterials;
	std::vector<uint32_t> newIndices(asset.materials.size(), 0);
	for (size_t oldIndex = 0; oldIndex < asset.materials.size(); ++oldIndex) {
		if (isUsed[oldIndex]) {
			newIndices[oldIndex] = static_cast<uint32_t>(usedMaterials.size());
			usedMaterials.push_back(std::move(asset.materials[oldIndex]));
		}
	}
	// --- メッシュの番号を付け直す ---
	for (ModelManager::SubMesh& mesh : asset.meshes) {
		mesh.materialIndex = newIndices[mesh.materialIndex];
	}
	asset.materials = std::move(usedMaterials);
}
} // namespace
```

`GetMtlMaterial` の関数を、全体を次に差し替える。

```cpp
// ===== 読み込んだマテリアル（番号で引く）=====
const ModelManager::ImportedMaterial* ModelManager::GetImportedMaterial(uint32_t handle, uint32_t materialIndex) {
	// --- 早期リターン（無いモデル）---
	const ModelAsset* model = GetModelAsset(handle);
	if (model == nullptr) {
		return nullptr;
	}
	// --- 番号が範囲の外（SubMesh::materialIndex は読み込みで assimp の番号のまま入るので、外れるのは呼ぶ側の間違い）---
	if (materialIndex >= model->materials.size()) {
		MY_ASSERT_MSG(false, std::format("{}: マテリアルの番号 {} は範囲の外です（{} 個）", model->name, materialIndex, model->materials.size()));
		return nullptr;
	}
	return &model->materials[materialIndex];
}
```

`Load` の中の、テクスチャを読むループと、GPU のバッファを作るループを次に差し替える。

```cpp
	for (ImportedMaterial& mat : ModelAsset.materials) {
		if (!mat.baseColorTexturePath.empty()) {
			mat.baseColorTextureHandle = TextureManager::Load(ToPath(mat.baseColorTexturePath));
		}
	}

	// GPU常駐バッファを構築（転送を予約。バッファの名前は「ファイル名/マテリアル名」）
	for (SubMesh& mesh : ModelAsset.meshes) {
		MakeMeshBuffer(mesh, filename + "/" + ModelAsset.materials[mesh.materialIndex].name);
	}
```

`LoadObjFile` の `// --- Material解析 ---` から `// --- Node解析` の前までを、次に差し替える。

```cpp
	// --- Material解析 ---
	// assimp と同じ並びで読む（メッシュは番号 mMaterialIndex でマテリアルを指すので、そのまま SubMesh::materialIndex になる）
	// 名前で持たないのは、glTF は同じ名前のマテリアルを許すから（名前の表だと、後から読んだ物が前の物を消す）
	modelAsset.materials.reserve(scene->mNumMaterials);
	// ループ
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		modelAsset.materials.push_back(ParseMaterial(scene->mMaterials[materialIndex], directoryPath));
	}
	// OBJ：Kd が書かれていないマテリアルは、物の色を白にする（書いていない＝色を掛けない。glTF の baseColorFactor の既定 1 と同じ）
	// Blender はテクスチャを使うマテリアルに Kd を書かないので、assimp の 0.6 のままだと、テクスチャが暗くなる
	if (IsObjFile(filePath)) {
		const std::unordered_set<std::string> withDiffuse = FindMaterialsWithDiffuse(filePath);
		for (ImportedMaterial& material : modelAsset.materials) {
			if (!withDiffuse.contains(material.name)) {
				material.baseColor = {1.0f, 1.0f, 1.0f, material.baseColor.w}; // A（OBJ の d）はそのまま
			}
		}
	}

	// --- Mesh解析 ---
	// assimpは「1メッシュ=1マテリアル」に分割済みなので、aiMeshをそのままSubMeshにする
	modelAsset.meshes.reserve(scene->mNumMeshes);
	// ループ
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		modelAsset.meshes.push_back(ParseMesh(scene->mMeshes[meshIndex], filename));
	}
	// どのメッシュも使わないマテリアルを外す（番号はファイルの並びのまま詰める）
	RemoveUnusedMaterials(modelAsset);
```

`MakeMeshBuffer` の頭の2行を次に差し替える（バッファの名前は呼ぶ側で作るようにした）。

```cpp
void ModelManager::MakeMeshBuffer(SubMesh& mesh, const std::string& bufferName) {
```

```cpp
	const std::wstring base = ConvertString(bufferName);
```

## パートB シェーダー（ModelSurface.hlsli・6つの PS・Material3dData）

### B-1 `Shader/Data/Model/ModelSurface.hlsli.shader`（新しい）

```hlsl
#META
stage: Include
#META_END

#HLSL
// モデルの面の色（光を当てる前の、物そのものの色）を作る所。Model の PS はどれもここを通す
// 物の色 = tint（物ごとの色）× baseColor（マテリアルの色）× テクスチャ（glTF の baseColorFactor × baseColorTexture と同じ形）
// 光の当て方（Lambert・PBR・トゥーンなど）は、それぞれの PS が書く
#include "Resources/Texture.hlsli"

// ===== マテリアル =====
// C++ の Material3dData（ShaderConstants.h）と同じ並び。大きさは C++ の static_assert で 160 バイトに止めている
struct ModelMaterial
{
    float32_t4 tint;         // 物ごとの色（Model Renderer の Color × Intensity。リニア）
    float32_t4 baseColor;    // マテリアルの色（リニア。A はマテリアルの不透明度）
    float4x4 uvTransform;    // UV をずらす・回す行列
    float32_t3 ambient;      // 影の所の明るさ（PBR で、環境光（IBL）が無いときだけ使う）
    float alphaCutoff;       // これ以下のアルファ（マテリアルの A × テクスチャの A）のピクセルを捨てる
    float32_t3 specular;     // ハイライトの色（Phong・BlinnPhong）
    float shininess;         // ハイライトの鋭さ（Phong・BlinnPhong）
    float32_t3 emissive;     // 自己発光の色
    uint textureIndex;       // 物の色のテクスチャ（gTextures の番号。無いときは白）
    float metallic;          // 金属っぽさ（PBR）
    float roughness;         // 表面の粗さ（PBR）
    float2 pad;
};
ConstantBuffer<ModelMaterial> gMaterial : register(b20);

// ===== 面の色 =====
struct ModelSurface
{
    float32_t3 baseColor; // tint × マテリアルの色 × テクスチャ（リニア）。光を当てる前の色
    float alpha;          // tint の A × マテリアルの A × テクスチャの A
};

// --- テクスチャを読み、色を掛けて、切り抜く（切り抜いたピクセルは、ここで捨てる）---
// 切り抜きはマテリアルの A（baseColor の A × テクスチャの A）で比べ、tint の A は入れない
// （Color の A で物をフェードさせたとき、穴が開いていく見た目にならないように。glTF・Unity も同じ）
ModelSurface GetModelSurface(float2 texcoord)
{
    float2 uv = mul(float32_t4(texcoord, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    float32_t4 texColor = gTextures[gMaterial.textureIndex].Sample(gSampler, uv);
    float materialAlpha = gMaterial.baseColor.a * texColor.a;
    if (materialAlpha <= gMaterial.alphaCutoff) // alphaCutoff が 0 なら、ちょうど 0 のピクセルだけ捨てる
    {
        discard;
    }
    ModelSurface surface;
    surface.baseColor = gMaterial.tint.rgb * gMaterial.baseColor.rgb * texColor.rgb;
    surface.alpha = gMaterial.tint.a * materialAlpha;
    return surface;
}
#HLSL_END
```

### B-2 `Shader/Data/Model/Unlit.PS.shader`

`#HLSL` から下を、全体を差し替える。

```hlsl
#HLSL
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Fog/Fog.hlsli"


PixelShaderOutput main(VertexShaderOutput input) {
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜きもここ）
    ModelSurface surface = GetModelSurface(input.texcoord);

    // Color（光を当てないので、面の色そのまま）
    float32_t3 finalColor = ApplyFog(surface.baseColor, input.worldPosition); // 霧（光を使わない物にも掛ける。背景の板も、ここを通る）

    PixelShaderOutput output;
    output.color = float32_t4(finalColor, surface.alpha);
    if (output.color.a == 0.0)
    {
        discard;
    }
    return output;
}

#HLSL_END
```

### B-3 `Shader/Data/Model/Lambert.PS.shader`

include から `main` の頭までを差し替える（`#include "Resources/Texture.hlsli"` とマテリアルの構造体を消して、`ModelSurface.hlsli` を include する）。

```hlsl
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"

static const float PI = 3.141596535f;


PixelShaderOutput main(VertexShaderOutput input)
{
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜いたピクセルは、光の計算の前に捨てる）
    ModelSurface surface = GetModelSurface(input.texcoord);
```

光の計算の後の、テクスチャを読む所から `finalColor` までを、次に差し替える。

```hlsl
    float32_t4 finalColor;
    finalColor.rgb = surface.baseColor * diffuseLighting + gMaterial.emissive;
    finalColor.a = surface.alpha;
```

### B-4 `Shader/Data/Model/HalfLambert.PS.shader`

include から `main` の頭までを差し替える。

```hlsl
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"

// 円周率
static const float PI = 3.14159265f;


PixelShaderOutput main(VertexShaderOutput input) {
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜いたピクセルは、光の計算の前に捨てる）
    ModelSurface surface = GetModelSurface(input.texcoord);

    PixelShaderOutput output;
```

`// ===== Texture =====` から `output.color.a = ...;` までを、次に差し替える。

```hlsl
    // Output
    output.color.rgb = surface.baseColor * diffuseLighting + gMaterial.emissive;
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    output.color.a = surface.alpha;
```

### B-5 `Shader/Data/Model/Phong.PS.shader`

include から `main` の頭までを差し替える。

```hlsl
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"



PixelShaderOutput main(VertexShaderOutput input)
{
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜いたピクセルは、光の計算の前に捨てる）
    ModelSurface surface = GetModelSurface(input.texcoord);

    PixelShaderOutput output;
```

`// ===== Texture =====` から `output.color.a = ...;` までを、次に差し替える。

```hlsl
    // ===== 出力色 =====
    diffuseLighting *= surface.baseColor; // 拡散反射は物の色（tint × マテリアルの色 × テクスチャ）
    specularLighting *= gMaterial.specular; // ハイライトはマテリアルの Specular の色（黒ならハイライト無し）
    
    output.color.rgb = (diffuseLighting + specularLighting) + gMaterial.emissive; // 拡散反射 + 鏡面反射
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    output.color.a = surface.alpha;
```

### B-6 `Shader/Data/Model/BlinnPhong.PS.shader`

include から `main` の頭までを差し替える。

```hlsl
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"



PixelShaderOutput main(VertexShaderOutput input)
{
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜いたピクセルは、光の計算の前に捨てる）
    ModelSurface surface = GetModelSurface(input.texcoord);

    PixelShaderOutput output;
```

`// ===== Texture =====` から `output.color.a = ...;` までを、次に差し替える。

```hlsl
    // ===== 出力色 =====
    diffuseLighting *= surface.baseColor; // 拡散反射は物の色（tint × マテリアルの色 × テクスチャ）
    specularLighting *= gMaterial.specular; // ハイライトはマテリアルの Specular の色（黒ならハイライト無し）

    output.color.rgb = diffuseLighting + specularLighting + gMaterial.emissive; // 拡散反射 + 鏡面反射 + 自己発光
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    output.color.a = surface.alpha;
```

### B-7 `Shader/Data/Model/PBR.PS.shader`

include とマテリアルの構造体を、次に差し替える（`// IBLの索引` の上まで）。

```hlsl
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"
#include "Resources/TextureCube.hlsli"
```

`// ===== アルベド（素の色） =====` の所を、次に差し替える。

```hlsl
    // ===== アルベド（素の色） =====
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜きもここ）
    ModelSurface surface = GetModelSurface(input.texcoord);
    float32_t3 albedo = surface.baseColor;
```

一番下の出力の行の A を差し替える。

```hlsl
    output.color = float32_t4(Lo + ambient + gMaterial.emissive, surface.alpha);
```

### B-8 `Graphics/Pipeline/ShaderConstants.h`

`Material3dData` を、次に差し替える。

```cpp
// ===== モデル用のPS =====
// Shader/Data/Model/ModelSurface.hlsli の ModelMaterial と同じ並び（物の色 = tint × baseColor × テクスチャ。色はどれもリニア）
struct Material3dData {
	Vector4 tint = {1.0f, 1.0f, 1.0f, 1.0f};      // 物ごとの色（Model Renderer の Color × Intensity）。マテリアルの色に掛ける
	Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // マテリアルの色（A はマテリアルの不透明度。切り抜きはこの A × テクスチャの A で比べる）
	Matrix4x4 uvTransform;                        // UV変換行列
	Vector3 ambient;                              // Ka: 環境光色
	float alphaCutoff = 0.0f;                     // これ以下のアルファ（マテリアルの A × テクスチャの A）のピクセルを捨てる
	Vector3 specular;                             // Ks: 鏡面反射色
	float shininess;                              // Ns: 鏡面反射指数
	Vector3 emissive;                             // Ke: 自己発光色
	uint32_t textureIndex = 0;                    // 使用するテクスチャのインデックス（0はテクスチャなし）
	float metallic = 0.0f;                        // 0=非金属 1=金属
	float roughness = 0.5f;                       // 表面の粗さ（0=鏡面, 1=完全拡散）
	float padA = 0.0f;
	float padB = 0.0f;
};
// HLSL の定数バッファは 16 バイトずつに詰めるので、並びを変えたら .hlsli も同じに直す（大きさがずれたら、ここで止まる）
static_assert(sizeof(Material3dData) == 160, "Material3dData の大きさが ModelSurface.hlsli の ModelMaterial と違います");
```

### B-9 `MyEngine_v1.vcxproj`・`.filters`

`ModelSurface.hlsli.shader` を足す（Visual Studio の「既存の項目を追加」で、`Shader\Data\Model` のフィルターへ）。

```xml
    <None Include="MyEngine\Shader\Data\Model\ModelSurface.hlsli.shader" />
```

```xml
    <None Include="MyEngine\Shader\Data\Model\ModelSurface.hlsli.shader">
      <Filter>MyEngine\Shader\Data\Model</Filter>
    </None>
```

## パートC Renderer（枠1つのマテリアルを決める・GPU の形に詰める）

### C-1 `Graphics/Pipeline/RenderStates.h`

`MaterialParams` の頭のコメントと、`diffuse` の行を差し替える（`diffuse` を `baseColor` の Vector4 にする）。

```cpp
// モデルマテリアルの調整用パラメータ（色は Inspector で選ぶ sRGB。Renderer がリニアに直す）
struct MaterialParams {
	Vector3 ambient = {0.2f, 0.2f, 0.2f};         // Ka: 環境光色
	Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 物の色（A は不透明度）。Model Renderer の Override のときだけ、読み込んだ色の代わりに使う（Primitive は Color の方を使う）
```

### C-2 `Graphics/Renderer/Renderer.h`

`ModelConfig` の `color` と `overrideMaterial` のコメントを直す。

```cpp
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 物ごとの色（sRGB。読み込んだマテリアルの色に掛ける）
```

```cpp
		bool overrideMaterial = false;                             // true：material を、読み込んだマテリアルの値の代わりに使う（全部のメッシュに同じ値。テクスチャは読み込んだ物のまま）
```

`MakeModelMaterial` の宣言（コメントの2行と宣言）を、次に差し替える。

```cpp
	// ===== マテリアル1つぶんの値 =====
	// 読み込んだマテリアル・Inspector の上書き・Primitive のどこから来ても、この形（色はリニア）にそろえてから GPU の形に詰める
	// 既定の値は「マテリアルを渡されなかった形（Triangle・Plane など）」の見た目（光沢・環境光なし）
	struct MaterialValues {
		Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // マテリアルの色（リニア。A は不透明度）
		Vector3 ambient = {0.0f, 0.0f, 0.0f};         // Ka: 環境光色
		Vector3 specular = {0.0f, 0.0f, 0.0f};        // Ks: 鏡面反射色
		Vector3 emissive = {0.0f, 0.0f, 0.0f};        // Ke: 自己発光色
		float shininess = 1.0f;                       // Ns: 鏡面反射指数
		float metallic = 0.0f;                        // 0=非金属 1=金属
		float roughness = 0.5f;                       // 表面の粗さ
		uint32_t baseColorTexture = 0;                // 物の色のテクスチャ（TextureManager の番号。0 は白）
	};

	// Inspector の値（MaterialParams。色は sRGB）→ MaterialValues（色はリニア）。テクスチャは持たないので 0
	static MaterialValues ToMaterialValues(const MaterialParams& params);

	// モデルのマテリアルの枠1つ（= メッシュが使うマテリアルの番号）に使うマテリアルを決める
	// 今は「Override なら Inspector の値、でなければ読み込んだ値」。Model Renderer の Texture を選んでいれば、テクスチャだけ差し替える
	// 後でマテリアルの枠（.material）を作るときは、ここに「枠に .material があればそれ」を足す
	static MaterialValues ResolveModelMaterial(const ModelConfig& config, uint32_t materialIndex);

	// MaterialValues と物ごとの色（tint。リニア）を、GPU の定数バッファの形に詰める
	static Material3dData MakeMaterial3dData(const MaterialValues& values, const Vector4& tint, const Transform& uvTransform, float alphaCutoff);
```

### C-3 `Graphics/Renderer/Renderer.cpp`

`MakeDefaultModelMaterial`（`// ===== Primitive Material作成ヘルパー =====` から）と `MakeModelMaterial` の2つを消して、次の3つに差し替える。

```cpp
// ===== Inspector の値 → マテリアル1つぶんの値 =====
Renderer::MaterialValues Renderer::ToMaterialValues(const MaterialParams& params) {
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（読み込んだマテリアルは、もうリニア）
	MaterialValues values;
	values.baseColor = SrgbToLinear(params.baseColor); // Base Color 物の色（A は不透明度。そのまま）
	values.ambient = SrgbToLinear(params.ambient);     // Ka: Ambient   環境光（影になっている部分の明るさ）
	values.specular = SrgbToLinear(params.specular);   // Ks: Specular  鏡面反射（ハイライトの強さ）
	values.emissive = SrgbToLinear(params.emissive);   // Ke: Emissive  自己発光（光源がなくても発光する色）
	values.shininess = params.shininess;               // Ns: Shininess 光沢（値が大きいほどハイライトが小さく鋭くなる）
	values.metallic = params.metallic;
	values.roughness = params.roughness;
	return values;
}

// ===== モデルのマテリアルの枠1つに使うマテリアルを決める =====
Renderer::MaterialValues Renderer::ResolveModelMaterial(const ModelConfig& config, uint32_t materialIndex) {
	const ModelManager::ImportedMaterial* imported = ModelManager::GetImportedMaterial(config.modelHandle, materialIndex);
	MaterialValues values;
	// --- どのマテリアルか（Override なら Inspector の値、でなければ読み込んだ値）---
	if (config.overrideMaterial || imported == nullptr) {
		values = ToMaterialValues(config.material);
	} else {
		values.baseColor = imported->baseColor; // 読み込みでリニアにそろえてある
		values.ambient = imported->ambient;
		values.specular = imported->specular;
		values.emissive = imported->emissive;
		values.shininess = imported->shininess;
		values.metallic = imported->metallic;
		values.roughness = imported->roughness;
	}
	// --- テクスチャ（Inspector の上書きにテクスチャは無いので、Override でも読み込んだ物を使う）---
	if (imported != nullptr) {
		values.baseColorTexture = imported->baseColorTextureHandle;
	}
	// Model Renderer の Texture を選んでいれば、全部の枠のテクスチャを差し替える（マテリアルの色はそのまま掛かる）
	if (config.textureHandle != 0) {
		values.baseColorTexture = config.textureHandle;
	}
	return values;
}

// ===== GPU の定数バッファの形に詰める =====
Material3dData Renderer::MakeMaterial3dData(const MaterialValues& values, const Vector4& tint, const Transform& uvTransform, float alphaCutoff) {
	Material3dData material;
	material.tint = tint;
	material.baseColor = values.baseColor;
	material.uvTransform = MakeUVTransformMatrix(uvTransform);
	material.ambient = values.ambient;
	material.alphaCutoff = alphaCutoff;
	material.specular = values.specular;
	material.shininess = values.shininess;
	material.emissive = values.emissive;
	material.textureIndex = ResolveTextureIndex(values.baseColorTexture);
	material.metallic = values.metallic;
	material.roughness = values.roughness;
	return material;
}
```

`PushMesh`（billboard を受け取る方）の、`// --- 色変換（sRGB → リニア）---` から `req.objectTransformData = ...` の上までを、次に差し替える。

```cpp
	// --- bind情報、頂点データ ---
	MeshRequest req;
	req.vertices = std::move(vertices);
	req.indices = std::move(indices);
	// --- マテリアル：Config が material を持っていて、何かを指していれば、その値（光沢・金属感・自己発光など）---
	MaterialValues values;
	if constexpr (requires { config.material; }) {
		if (config.material != nullptr) {
			values = ToMaterialValues(*config.material);
		}
	}
	// 形（Primitive など）には読み込んだマテリアルが無く、Config の Color と Texture がマテリアルの色とテクスチャ（sRGB → リニア）
	// そのため MaterialParams の Base Color は使わず、物ごとの色（tint）は白
	values.baseColor = SrgbToLinear(config.color);
	values.baseColorTexture = config.textureHandle;
	req.materialData = MakeMaterial3dData(values, {1.0f, 1.0f, 1.0f, 1.0f}, config.uvTransform, config.alphaCutoff); // 切り抜きは、どの Config にもある alphaCutoff の方を使う
```

`DrawModel` の `// マテリアル構築` の3行を、次に差し替える。

```cpp
			// マテリアル構築（枠＝メッシュが使うマテリアルの番号で決めて、物ごとの色（Color。sRGB → リニア）と一緒に詰める）
			// 切り抜きは読み込んだマテリアルに無い設定なので、いつも Inspector の値（MaterialParams）から取る
			const MaterialValues materialValues = ResolveModelMaterial(config, mesh.materialIndex);
			req.materialData = MakeMaterial3dData(materialValues, SrgbToLinear(config.color), config.uvTransform, config.material.alphaCutoff);
```

`DrawModel` の一番下の `req.debugSubName` の行を、次に差し替える。

```cpp
			req.debugSubName = &asset->materials[mesh.materialIndex].name;
```

### C-4 `Systems/Rendering3D/ModelRenderSystem.cpp`

`config.color = ClampColor(render.color);` を、次に差し替える（Model Renderer の Intensity が今まで掛かっていなかった。Primitive と同じ `ApplyIntensity` を使う）。

```cpp
		config.color = ApplyIntensity(render.color, render.intensity); // 物ごとの色。明るさの倍率はリニアで掛ける（Color.h）
```

## パートD Inspector と保存

### D-1 `Math/Color.h`

`LinearToSrgb(float)` の下に足す。

```cpp
// RGB（読み込んだマテリアルの色などを、Inspector の sRGB の欄へ写すとき）
inline Vector3 LinearToSrgb(const Vector3& color) { return {LinearToSrgb(color.x), LinearToSrgb(color.y), LinearToSrgb(color.z)}; }

// RGBA（A は透け方の割合なので、直さない）
inline Vector4 LinearToSrgb(const Vector4& color) { return {LinearToSrgb(color.x), LinearToSrgb(color.y), LinearToSrgb(color.z), color.w}; }
```

### D-2 `Editor/Inspector/ModelRendererEditor.cpp`（全体を差し替え）

```cpp
#include "ModelRendererEditor.h"

#include <format>
#include <string>

#include <externals/imgui/imgui.h>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Math/Color.h"


namespace {
//=============================================================================
// 読み込んだマテリアル
//=============================================================================
// ===== 1つを上書きの値（render.material）に写す =====
// 上書きの値を、モデルの見た目から始められるように。読み込んだ色はリニア、Inspector の欄は sRGB なので直して写す
// 切り抜きは読み込んだマテリアルに無い値なので、そのまま
void CopyImportedMaterial(const ModelManager::ImportedMaterial& imported, MaterialParams& params) {
	params.ambient = LinearToSrgb(imported.ambient);
	params.baseColor = LinearToSrgb(imported.baseColor); // A（不透明度）はそのまま
	params.specular = LinearToSrgb(imported.specular);
	params.emissive = LinearToSrgb(imported.emissive);
	params.shininess = imported.shininess;
	params.metallic = imported.metallic;
	params.roughness = imported.roughness;
}

// ===== 一覧（番号・色の見本・名前・テクスチャの有無・Copy）=====
// 番号はマテリアルの枠の番号（メッシュが使うマテリアルの番号）。後で枠ごとに .material を選べるようにするときは、この行に欄が並ぶ
void DrawImportedMaterials(ModelRendererComponent& render) {
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(render.modelHandle);
	if (asset == nullptr) {
		ImGui::TextDisabled("(モデルが選ばれていない)");
		return;
	}
	for (size_t index = 0; index < asset->materials.size(); ++index) {
		const ModelManager::ImportedMaterial& imported = asset->materials[index];
		ImGui::PushID(static_cast<int>(index));
		// --- 色の見本（読むだけ。見本の色は sRGB で見せる）---
		const Vector4 swatch = LinearToSrgb(imported.baseColor);
		ImGui::ColorButton("##baseColor", ImVec4(swatch.x, swatch.y, swatch.z, swatch.w), ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf);
		ImGui::SameLine();
		// --- 番号・名前・テクスチャの有無 ---
		const std::string label = std::format("{}  {}{}", index, imported.name, imported.baseColorTexturePath.empty() ? "" : "  (Texture)");
		ImGui::TextUnformatted(label.c_str());
		if (!imported.baseColorTexturePath.empty()) {
			ImGui::SetItemTooltip("%s", imported.baseColorTexturePath.c_str());
		}
		ImGui::SameLine();
		// --- 上書きの値に写して、Override を入れる ---
		if (ImGui::SmallButton("Copy")) {
			CopyImportedMaterial(imported, render.material);
			render.overrideMaterial = true;
		}
		ImGui::SetItemTooltip("このマテリアルの値を下の上書きの値に写して、Override を入れる");
		ImGui::PopID();
	}
}
} // namespace

//=============================================================================
// Inspectorの中身
//=============================================================================
void ModelRendererEditor::DrawComponent(ModelRendererComponent& render) const {
	ImGui::Checkbox("Enabled", &render.enabled);

	// --- どのモデル・テクスチャを使うか（resources以下のファイルから選ぶ。ComponentUIのアセット欄と同じ物）---
	ComponentUI& ui = GetInspectorUI();
	ui.AssetField("Model", render.modelHandle, AssetType::Model);
	ui.AssetField("Texture", render.textureHandle, AssetType::Texture, Tip("(none) なら、モデルのマテリアルのテクスチャを使う（選ぶと全部のマテリアルのテクスチャを差し替える。マテリアルの色は掛かる）"));

	// --- 描画設定（enumはmagic_enumが名前を作る）---
	EditorWidgets::EnumCombo("Shading", render.shadingType);
	EditorWidgets::EnumCombo("Blend", render.blendMode);
	EditorWidgets::EnumCombo("Rasterizer", render.rasterizerType);
	EditorWidgets::EnumCombo("Depth", render.depthMode);
	EditorWidgets::EnumCombo("Billboard", render.billboard);
	ImGui::Checkbox("Receive Decals", &render.receivesDecals);
	ImGui::SetItemTooltip("外すと、デカールの箱の中にあっても、このモデルには塗られない（不透明なときだけ効く）");

	// --- 色（sRGB の 0〜1。物ごとの色で、マテリアルの色に掛ける）---
	ImGui::ColorEdit4("Color", &render.color.x);
	ImGui::SetItemTooltip("このモデルだけの色合い。マテリアルの色（Base Color）とテクスチャに掛ける（白ならマテリアルの色のまま）");
	ImGui::DragFloat("Intensity", &render.intensity, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("明るさの倍率（色はそのまま）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る");

	// --- マテリアル（読み込んだマテリアルの一覧と、Override を入れたときに代わりに使う値）---
	if (ImGui::TreeNode("Material")) {
		// ===== 読み込んだマテリアル =====
		ImGui::SeparatorText("Model Materials");
		DrawImportedMaterials(render);

		// ===== 上書き =====
		ImGui::Separator();
		ImGui::Checkbox("Override", &render.overrideMaterial);
		ImGui::SetItemTooltip("入れると、下の値を読み込んだマテリアルの値の代わりに使う（全部のメッシュに同じ値。テクスチャは読み込んだ物のまま）\n色の違うマテリアルを持つモデルでは、全部が同じ色になる（下の値を触ると、自動で入る）");

		// ===== 値（どのシェーディングで使うかは、マウスを乗せると出る）=====
		bool isEdited = false; // どれか1つでも触ったか
		isEdited |= ImGui::ColorEdit4("Base Color", &render.material.baseColor.x);
		ImGui::SetItemTooltip("物の色（読み込んだマテリアルの色の代わり。Color とテクスチャに掛ける）。A は不透明度");
		isEdited |= ImGui::ColorEdit3("Ambient", &render.material.ambient.x);
		ImGui::SetItemTooltip("影の所の明るさ（PBR で、環境光（IBL）が無いときだけ使う）");
		isEdited |= ImGui::ColorEdit3("Specular", &render.material.specular.x);
		ImGui::SetItemTooltip("ハイライトの色（Phong・BlinnPhong。黒ならハイライト無し）");
		isEdited |= ImGui::ColorEdit3("Emissive", &render.material.emissive.x);
		ImGui::SetItemTooltip("自己発光の色（Unlit 以外。ライトが無くても光る）");
		isEdited |= ImGui::DragFloat("Shininess", &render.material.shininess, 0.5f, 0.0f, 256.0f);
		ImGui::SetItemTooltip("ハイライトの鋭さ（Phong・BlinnPhong。大きいほど小さく鋭い）");
		isEdited |= ImGui::DragFloat("Metallic", &render.material.metallic, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("金属っぽさ（PBR。0=プラスチック・木 1=金属）");
		isEdited |= ImGui::DragFloat("Roughness", &render.material.roughness, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("表面の粗さ（PBR。0=鏡のよう 1=ざらざら）");
		// 値を触ったら、すぐ見た目に出るように上書きを入れる（入れ忘れて「効かない」とならないように）
		if (isEdited) {
			render.overrideMaterial = true;
		}
		// 切り抜きは読み込んだマテリアルに無いので、Override に関係なくいつも効く
		ImGui::DragFloat("Alpha Cutoff", &render.material.alphaCutoff, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("これ以下のアルファ（マテリアルの A × テクスチャの A。Color の A は入らない）のピクセルを捨てる（Override に関係なく効く）");
		ImGui::TreePop();
	}

	// --- テクスチャのUVをずらす・回す（Vector3の先頭アドレスを渡してx,yだけ触る）---
	if (ImGui::TreeNode("UV Transform")) {
		ImGui::DragFloat2("Offset", &render.uvTransform.translation.x, 0.01f);
		ImGui::DragFloat2("Tiling", &render.uvTransform.scale.x, 0.01f);
		ImGui::DragFloat("Rotate", &render.uvTransform.rotation.z, 0.01f);
		ImGui::TreePop();
	}
}
```

### D-3 `Components/EngineComponents.cpp`

`DescribeModelRenderer` の `ui.ColorField("diffuse", value.material.diffuse);` を、次に差し替える。

```cpp
	ui.ColorField("baseColor", value.material.baseColor); // 物の色（RGBA。前の "diffuse" は読まない）
```

### D-4 `Components/Rendering3D/ModelRendererComponent.h`

`color` と `overrideMaterial` のコメントを直す。

```cpp
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 物ごとの色（sRGB。RGBA 0〜1。マテリアルの色とテクスチャに掛ける）
```

```cpp
	bool overrideMaterial = false; // true：material を、読み込んだマテリアルの値の代わりに使う（全部のメッシュに同じ値）
```

### D-5 確かめること（ここで初めてビルドする）

- 3構成（Debug・Development・Release）でビルドが通る（`static_assert` で止まらない）
- 起動して、Console にシェーダーのエラーが出ない
- 今までのシーン（Primitive だけ）の見た目が変わらない

## パートE グリッドと Sandbox のテスト

### E-1 `Resources/Model/Grid/grid.mtl`

`Kd 0.800000 0.800000 0.800000` を、次に差し替える。

```
Kd 1.000000 1.000000 1.000000
```

### E-2 テストのモデル（Blender で作る）

同じ形（球が4つ）を glTF と OBJ に書き出す。部品は4つ。

| 部品 | Base Color（リニア） | テクスチャ |
|---|---|---|
| ColorDark | (0.05, 0.20, 0.60) | なし |
| ColorBright | (1.00, 0.55, 0.10) | なし |
| TextureOnly | 白 | 市松（白と sRGB 0.5 の灰色） |
| ColorTexture | (0.20, 0.80, 0.30) | 市松 |

`Docs/Tasks/ModelBaseColorTest/make_test_model.py` に置いてある。エンジンのリポジトリの一番上で、Blender を画面なしで動かす。

```powershell
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --factory-startup --python Docs/Tasks/ModelBaseColorTest/make_test_model.py
```

`project/Sandbox/Resources/Model/BaseColorTest/` に `BaseColorTest.gltf`・`.bin`・`.obj`・`.mtl`・`checker.png` ができる。

次に、`BaseColorTest.mtl` の `newmtl ColorTexture` の段の一番下（`illum 2` の下）に1行足す。Blender の OBJ の書き出しは、「色 × テクスチャ」（Mix ノード）のマテリアルには map_Kd を書かないので、手で足す（エンジンではなく、書き出す側の都合）。

```
map_Kd checker.png
```

`.gitignore` の `*.obj` に当たるので、OBJ は `-f` で足す。

```powershell
git add -f project/Sandbox/Resources/Model/BaseColorTest/BaseColorTest.obj
```

### E-3 `Sandbox/Resources/scenes/ModelBaseColorTest.scene.json`（新しい）

縦に6つの Shading（Unlit・Lambert・HalfLambert・Phong・BlinnPhong・PBR）、横に5つの列を並べる。

| 列 | 何を確かめるか |
|---|---|
| glTF | ファイルの色が出る |
| OBJ | glTF と同じ色になる（OBJ の Kd もリニア・Kd の無い TextureOnly も暗くならない） |
| Color | Color (1, 0.5, 0.5) が全部の部品に掛かる |
| Override | Base Color (0.5, 0.5, 1) が全部の部品に入る（テクスチャは残る） |
| Primitive | Color だけ・Color × テクスチャ。今までと同じ見た目 |

38 個の Entity で同じ形がくり返すので、PowerShell で作る（エンジンのリポジトリの一番上で）。

```powershell
$shadings = @("Unlit", "Lambert", "HalfLambert", "Phong", "BlinnPhong", "PBR")
$rowY = @(5, 3, 1, -1, -3, -5)
$model = "resources/Model/BaseColorTest/BaseColorTest"
# label, x, extension, Color, Override, Base Color
$columns = @(
    @("glTF",     -17, "gltf", "[1.0, 1.0, 1.0, 1.0]", "false", "[1.0, 1.0, 1.0, 1.0]"),
    @("OBJ",       -8, "obj",  "[1.0, 1.0, 1.0, 1.0]", "false", "[1.0, 1.0, 1.0, 1.0]"),
    @("Color",      1, "gltf", "[1.0, 0.5, 0.5, 1.0]", "false", "[1.0, 1.0, 1.0, 1.0]"),
    @("Override",  10, "gltf", "[1.0, 1.0, 1.0, 1.0]", "true",  "[0.5, 0.5, 1.0, 1.0]")
)
$script:id = 0
function F([double]$v) { return $v.ToString("0.0", [System.Globalization.CultureInfo]::InvariantCulture) }
function Entity([string]$name, [double[]]$pos, [double]$scale, [double[]]$rot, [string]$component) {
    $script:id++
    $s = F $scale
    return "`t`t{`n`t`t`t`"id`": $script:id,`n`t`t`t`"name`": `"$name`",`n`t`t`t`"parent`": 0,`n`t`t`t`"active`": true,`n`t`t`t`"components`": {`n" +
        "`t`t`t`t`"Transform`": {`n`t`t`t`t`t`"translation`": [$(F $pos[0]), $(F $pos[1]), $(F $pos[2])],`n`t`t`t`t`t`"rotation`": [$(F $rot[0]), $(F $rot[1]), $(F $rot[2])],`n`t`t`t`t`t`"scale`": [$s, $s, $s]`n`t`t`t`t},`n" +
        $component + "`n`t`t`t}`n`t`t}"
}
$entities = @()
$entities += Entity "Main Camera" @(0.5, 0, -24) 1 @(0, 0, 0) "`t`t`t`t`"Camera`": {`n`t`t`t`t`t`"fovY`": 60.0,`n`t`t`t`t`t`"nearZ`": 0.1,`n`t`t`t`t`t`"farZ`": 1000.0,`n`t`t`t`t`t`"priority`": 0`n`t`t`t`t}"
$entities += Entity "Directional Light" @(0, 8, -8) 1 @(35, 25, 0) "`t`t`t`t`"Directional Light`": {`n`t`t`t`t`t`"color`": [1.0, 1.0, 1.0]`n`t`t`t`t}"
for ($r = 0; $r -lt $shadings.Count; $r++) {
    $shading = $shadings[$r]; $y = $rowY[$r]
    foreach ($c in $columns) {
        $entities += Entity "$($c[0]) $shading" @($c[1], $y, 0) 1 @(0, 0, 0) ("`t`t`t`t`"Model Renderer`": {`n`t`t`t`t`t`"enabled`": true,`n`t`t`t`t`t`"model`": `"$model.$($c[2])`",`n" +
            "`t`t`t`t`t`"shadingType`": `"$shading`",`n`t`t`t`t`t`"color`": $($c[3]),`n`t`t`t`t`t`"overrideMaterial`": $($c[4]),`n`t`t`t`t`t`"baseColor`": $($c[5])`n`t`t`t`t}")
    }
    $entities += Entity "Primitive $shading" @(16.5, $y, 0) 1.6 @(0, 0, 0) ("`t`t`t`t`"Primitive Renderer`": {`n`t`t`t`t`t`"enabled`": true,`n`t`t`t`t`t`"shape`": `"Sphere`",`n" +
        "`t`t`t`t`t`"color`": [0.9, 0.5, 0.2, 1.0],`n`t`t`t`t`t`"shadingType`": `"$shading`"`n`t`t`t`t}")
    $entities += Entity "Primitive Texture $shading" @(19.5, $y, 0) 1.6 @(0, 0, 0) ("`t`t`t`t`"Primitive Renderer`": {`n`t`t`t`t`t`"enabled`": true,`n`t`t`t`t`t`"shape`": `"Sphere`",`n" +
        "`t`t`t`t`t`"color`": [0.6, 0.9, 1.0, 1.0],`n`t`t`t`t`t`"texture`": `"resources/Model/BaseColorTest/checker.png`",`n`t`t`t`t`t`"shadingType`": `"$shading`"`n`t`t`t`t}")
}
$json = "{`n`t`"version`": 1,`n`t`"entities`": [`n" + ($entities -join ",`n") + "`n`t]`n}`n"
[System.IO.File]::WriteAllText("$PWD\project\Sandbox\Resources\scenes\ModelBaseColorTest.scene.json", $json, (New-Object System.Text.UTF8Encoding $false))
```

### E-4 試し方

- Sandbox を起動して、Project ウィンドウで `scenes/ModelBaseColorTest` を開く
- 左の4つの列は、1つ目の列（glTF）と2つ目の列（OBJ）が同じ色。Unlit の行は、球の真ん中がファイルの色（リニア → sRGB）になる（ToneMap の ACES とブルームが掛かるので、少しずれる）
- 3つ目の列は赤み、4つ目の列は青になる
- `OBJ Lambert` を選んで、Inspector の Material を開く。Model Materials に4つ（`0 ColorBright` から `3 TextureOnly (Texture)`）が並ぶ。`ColorDark` の Copy を押すと、Base Color が (63, 124, 203) になり、Override が入って、その行の球が全部青になる

## パートF AL4_01 の移し方（サブモジュールを進めるとき）

AL4_01 の `GameScene.scene.json` の Player は、Override が入っていて、`"diffuse"` に Body の色（リニアの値を sRGB の欄に写した物）が入っている。エンジンを進めると、Override が全メッシュに同じ Base Color（保存のキーが変わったので白）を入れるので、全部の部品が Color（0.598, 0.513, 0.513）の色になる。

1. サブモジュールを進める（エンジンのこのブランチを develop に入れて push した後）
2. AL4_01 を開いて、Player の Model Renderer の Material を開き、**Override を外す**（部品ごとに glTF の色が出る）
3. Color（0.598, 0.513, 0.513）は、前に全体を Body の色で塗っていたときに合わせた値かもしれない。見て、白に戻すかを決める
4. Override で入れていた Specular 1・Shininess 0・Roughness 0 も、外すとファイルの値（Specular 1・Shininess 250・Roughness 0.5 など）になる。Player の Shading（PBR）では Roughness が効くので、光り方が変わる
5. シーンを保存して、AL4_01 の側でコミットする（古い `"diffuse"` のキーは、保存し直すと消える）

Boss・Staff は、Model Renderer を保存しているシーンがまだ無いので、移す物は無い。色が思ったより明るく見えたら、Blender の値を直す（glTF の色はリニア。Boss・Staff の色は 0.42 のような丸い数で、sRGB のつもりで書いた値かもしれない）。

写しの AL4_01（エンジンをこのブランチにした物）で確かめた：Override を入れたままだと、Player の全部の部品が Color の桃色1色になる。外すと、帽子（紫）・帽子の帯・杖・箒が glTF の色で出る。

## 2章 確かめたこと・確かめていないこと

確かめたこと（エンジンを写したフォルダで。テスト用のコードは写しだけに入れて、このブランチには入れていない）：

- 3構成（Debug・Development・Release）で、このブランチの状態（テスト用のコードなし）のビルドが通る（自分のファイルの警告 0）
- 読み込んだ値を数で確かめた（36 項目）：glTF・OBJ の Base Color がファイルの値のまま（リニア）／glTF のテクスチャだけのマテリアルは 1／OBJ の Kd の無いマテリアル・mtl の無い OBJ は 1（assimp の 0.6 ではない）／行の頭に空白があり CRLF の mtl でも Kd と d（0.5）を読める／alphaMode OPAQUE と alphaCutoff 0.5 の記録／使われないマテリアル（DefaultMaterial・名前なし）が外れて4つになる／メッシュの番号が範囲の中で、同じマテリアルを指す／同じ png は同じテクスチャの番号／grid.obj の Base Color が 1／`LinearToSrgb` と `SrgbToLinear` の行き来／`Material3dData` が 160 バイトで、メンバーの場所が .hlsli の並びと同じ
- Release で ModelBaseColorTest：元のエンジンでは色だけの球が全部白。直した後は6つの Shading すべてで色が出る
- Primitive の列は、元のエンジンと最大差 1/255（7 画素だけ）
- glTF だけ・OBJ だけを同じ位置に置いて撮り比べた（ToneMap は None）：色だけの球は差 0〜1。テクスチャの球は真ん中が同じ（差 12 は市松の境目で、2つの書き出しの UV が少し違うため）
- Debug の Inspector：Model Materials に4つ並ぶ。ColorDark の Copy で Base Color が (63, 124, 203)（ファイルのリニア値 (0.05, 0.2, 0.6) を sRGB に直した値）になり、Override が入って、その行の球が全部 ColorDark の色になる
- AL4_01（写し）：F のとおり
- E-2 の Blender のスクリプトと E-3 の PowerShell で、写しと同じモデル・同じシーンができる

確かめていないこと：

- エディタのグリッドの画面での撮り比べ（読み込んだ Base Color が 1 になることだけを確かめた。シェーダーでは Color × 1 なので、元と同じ）
- Inspector の Copy を手で押す操作（写しでは、テスト用のコードで押した扱いにして確かめた）・Undo で戻ること
- Prefab の中の Model Renderer の Base Color の上書き
- 3ds Max など、Blender 以外で書き出した OBJ（Kd を sRGB で書くツールでは、少し暗く見える）
- 骨のあるモデル（スキン）の色（マテリアルの経路は同じ）

## 3章 後で足す候補

- **メッシュごとのマテリアルの枠（.material の載せ替え）**：Model Renderer に枠の一覧（番号＝`ImportedMaterial` の番号）を持たせ、枠ごとに .material を選べるようにする。`Renderer::ResolveModelMaterial` に「枠に .material があればそれ」を足す。決めること：
  - Component は trivially copyable なので、枠の .material を Manager 側の表（Entity → 枠ごとのマテリアル）で持つか、決まった数の配列で持つか
  - モデルを作り直してマテリアルの数・並びが変わったときの扱い（番号がずれる。名前で合わせ直すか）
  - 読み込んだ glTF の alphaMode・alphaCutoff を、枠のマテリアルの Blend・切り抜きへ渡す
  - Shading（陰の付け方）を Component からマテリアル（Shader）へ移す
- **トゥーン（ランプ）のシェーダー**：`ModelSurface.hlsli` の `GetModelSurface` を呼んだ後に、ランプの計算を書く
- **物ごとの上書き**（MaterialPropertyBlock）：tint 以外の値も物ごとに変える
- **名前を変えた値を読む仕組み**（Unity の `FormerlySerializedAs`）：ComponentSerializer に「古いキー」を書けるようにする（今回の `diffuse` → `baseColor` のような改名で、前のファイルを読めるように）
- **頂点カラー**（glTF の COLOR_0）：glTF の決まりでは Base Color に掛ける
- **モデルの読み込み設定**（Unity の Import Settings）：「Kd は sRGB」のようなファイルごとの設定。読み込み設定のファイル（.meta）の仕組みから
