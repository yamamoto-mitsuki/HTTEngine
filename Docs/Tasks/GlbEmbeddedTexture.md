参照：[ModelImporter.cpp](../../project/HTTEngine/Graphics/Model/ModelImporter.cpp)・[ModelMaterialImporter.cpp](../../project/HTTEngine/Graphics/Model/ModelMaterialImporter.cpp)・[ModelNodeImporter.cpp](../../project/HTTEngine/Graphics/Model/ModelNodeImporter.cpp)・[ModelManager.cpp](../../project/HTTEngine/Graphics/Model/ModelManager.cpp)・[TextureManager.cpp](../../project/HTTEngine/Graphics/Texture/TextureManager.cpp)・[8章 描画](../Reference/08-Rendering.md)・[glTF 2.0 の決まり（GLB・images）](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#glb-file-format-specification)・前の作業 [SameNodeName.md](SameNodeName.md)

# .glb の埋め込みテクスチャと、ModelManager.cpp の分け方

## 1. 頼まれたこと
- `.glb` に埋め込まれたテクスチャを読めるようにする（前は `TextureManager::Load` の assert で止まった）。
- `ModelManager.cpp`（681 行）が長いので分ける。

## 2. 分け方
assimp を使ってファイルを CPU の形にする所（読み込み）と、番号・GPU を持つ所（管理）を分けた。読み込みはさらに「メッシュ」「マテリアル」「ノードと骨」に分けた。

| ファイル | 行数 | 中身 |
|---|---|---|
| `ModelImporter` | 約 100 | 入口。assimp の `ReadFile`・メッシュ（`ParseMesh`）。マテリアルとノードの係を順に呼ぶ |
| `ModelMaterialImporter` | 約 260 | マテリアル（`ParseMaterial`・OBJ の Kd・使わないマテリアルを外す）と、テクスチャの名前 → パス（`TexturePathResolver`。埋め込みの画像もここ） |
| `ModelNodeImporter` | 約 210 | ノードの木（`LoadNode`・`ConvertAssimpMatrix`）・骨（`LoadSkin`・`PickJointNode`・重み）・ノードの名前の表 |
| `ModelManager` | 約 210 | 番号とキャッシュ・`Load`（GPU のバッファとテクスチャ）・行列パレット・取得 |

- assimp を include するのは3つの Importer の .cpp だけ。ヘッダは `struct aiScene;` の前方宣言だけにした（ゲームが読むヘッダに assimp を出さない）。
- Importer は GPU に触らない（ファイルを読んで `ModelAsset` を作るだけ）。GPU に送るのは `ModelManager::Load`。
- `ModelManager` の型（`ModelAsset`・`Node` など）はそのまま `ModelManager::` の中に置いた。外へ出すと、使っている所を全部直すことになるので、今回はしない。
- Importer は namespace の関数にした（`GizmoLines` と同じ。持つ物が無いので）。中だけで使う関数は無名 namespace。
- 中身はそのまま写した。見出しの段だけ、関数の中を `// --- ---` にそろえ、コメントの「いつ」→「いる」の打ち間違いを直した。

## 3. 埋め込みテクスチャ
### 3.1 形
- assimp は、埋め込みの画像を `aiScene::mTextures` に入れ、マテリアルのテクスチャのパスを `"*0"`（番号）にする。FBX の埋め込みはファイル名のまま。どちらも `GetEmbeddedTextureAndIndex` で中身と番号が引ける。
- `aiTexture::mHeight == 0` なら、中身は画像ファイルそのもの（png・jpg）。そうでなければ、画素（`aiTexel` ＝ B・G・R・A の順）が並んでいる。

### 3.2 決めたこと
| 決めること | 決めた形 | 理由 |
|---|---|---|
| テクスチャの名前 | 「モデルのパス/*番号」（`resources/Model/Robot.glb/*0`） | ファイルのパスと同じ形なので、`ToPathKey` でそろえて同じ表（`TextureManager::textures_`）に入る。モデルの下に付く形なので、エディタでモデルの名前を変えると `MovedPath::Apply` がテクスチャの名前も一緒に付け替える |
| 読む所 | 読み込み（Importer）で画像の中身を写し（`EmbeddedImage`）、`ModelManager::Load` が `TextureManager` に渡す | assimp の中身は `Assimp::Importer` が消えると無くなる。Importer は GPU に触らない決まりなので、写して渡す |
| TextureManager の入口 | `LoadFromMemory(key, 画像ファイルの中身)`・`LoadFromPixels(key, 幅, 高さ, BGRA)` | ファイルから（`Load`）と同じく、sRGB・ミップマップ。後半（GPU に送る・SRV・表に足す）は `Register` にまとめた |
| 読めない画像 | ログに出して 0（テクスチャ無し）。止めない | 埋め込みの形（webp・ktx2 など）は作った人が選びにくい。モデルは描けた方がよい |
| 外の画像とまぎれない | `Load` は埋め込みの名前の表（`embeddedHandles`）を先に見る | 読めなかった埋め込みの画像を、ファイルとして読みに行って assert で止まらないように |
| 「無い」の表し方 | `FindLoaded` は `std::optional` | SRV の 0 番もあり得るので、0 を「無い」に使わない |

### 3.3 Model Renderer の Texture の欄
埋め込みの画像はファイルとして無いので、Texture の欄（resources の下のファイルから選ぶ）には出ない。モデルのマテリアルのテクスチャとしてだけ使う。

## 4. 確かめたこと（2026-11-13）
- Debug・Development・Release のビルドが通る（新しい警告なし）。
- `project/Sandbox/Resources/scenes/EmbeddedTextureTest.scene.json`：左がエンジンの `simpleSkin.gltf`（外の uvChecker.png）、右が同じ物を `.glb` にした `Resources/Model/EmbeddedTexture/SimpleSkin.glb`（uvChecker を埋め込み）。Animator の Preview Time 1 秒で、どちらも同じ曲がり方・同じ市松模様で出る。
- TD2_2 のロボット（`.gltf` を `.glb` にまとめた物、画像2枚）を一時的に置き、2枚ともテクスチャが付いて出るのを見た（チームのモデルなのでエンジンには入れていない）。
- ログ：`Loaded: resources/Model/EmbeddedTexture/SimpleSkin.glb/*0`。画像の形（format）は、同じ png をファイルから読んだときと同じ（uvChecker 91・ロボット 28）。
- `.gltf`（外の画像）は前と同じく読める（左の simpleSkin・前の作業のロボット）。

`.glb` は、Blender の書き出しで「glTF Binary (.glb)」を選べば作れる。手元の `.gltf` をまとめる道具はエンジンには入れていない（今回はスクリプトで作った）。

## 5. 残り
- FBX の埋め込みテクスチャは、仕組みは同じ（`GetEmbeddedTextureAndIndex` がファイル名でも引く）だが、FBX のファイルで確かめていない。
- 埋め込みの画像が DDS・KTX2 のときは WIC で読めないので、テクスチャ無しになる（ファイルから読むときも DDS は読めないので、そろっている）。
