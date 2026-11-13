参照：[ModelNodeImporter.cpp](../../project/HTTEngine/Graphics/Model/ModelNodeImporter.cpp)（このときは ModelManager.cpp の中。後で [GlbEmbeddedTexture.md](GlbEmbeddedTexture.md) で分けた）・[AnimationPlayer.cpp](../../project/HTTEngine/Graphics/Animation/AnimationPlayer.cpp)・[23章 アニメーション](../Reference/23-Animation.md)・[glTF 2.0 の決まり（skins・nodes）](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins)

# 同じ名前のノードがあるモデルに Animator を付けると、形がくずれる

## 起きたこと
Blender から書き出したロボットのモデルに Animator を付けると、胸と背中の羽根が消え、足元に青い部品が出た。Animator を外すと正しい形で出る。

## 原因
glTF の中に、名前が `body` のノードが2つあった。

| glTF の番号 | 名前 | 中身 |
|---|---|---|
| 9 | `body` | 骨（waist の子。胸・背中の羽根の頂点はこの骨に付いている） |
| 20 | `body` | メッシュのオブジェクト（スキンのあるメッシュ） |

Blender ではオブジェクトの名前と骨の名前を別々に付けられるので、同じ名前でもぶつからない。書き出すと、どちらも glTF のノードになる。

assimp の骨（`aiBone`）とアニメーションのカーブ（`aiNodeAnim`）は、ノードを**名前でしか**指さない。エンジンは名前 → 番号の表で「先にある方」を使っていたので、木を先にたどるメッシュのノード（20）を骨にしていた。

- 行列パレット：胸の頂点は「逆バインド行列（腰の高さの骨）× メッシュのノードの行列（ほぼ原点）」で動き、腰の高さの分（約 3.4）だけ下へずれる。
- Animator が無いときは曲げずに描く（[Renderer.cpp](../../project/HTTEngine/Graphics/Renderer/Renderer.cpp) の `useSkinning`）ので、気付かなかった。
- 骨の線（View → Skeletons）も、メッシュのノードを骨として原点から線を引いていた。

## 直し方
名前しか無いので、「どちらが骨か」を形から決める。

| 所 | 決まり |
|---|---|
| `ModelManager::LoadSkin` の骨の選び方（`PickJointNode`） | 同じ名前が2つ以上あれば、**スキンのあるメッシュを持つノードは骨にしない**（そのメッシュは骨で置かれ、ノードの行列を掛けない＝`MeshMatrix`。だから骨の役をしない）。全部持つなら先の方 |
| `ModelAsset::nodeIndexByName`（`BuildNodeNameTable`・`FindNodeIndex`） | 名前 → 番号を1つに決めて持つ。同じ名前なら**骨（isJoint）の方**、どちらも骨・どちらも骨でないなら先の方 |
| `AnimationPlayer::Bind` | カーブは `FindNodeIndex` が返す1つのノードだけを動かす（前は同じ名前のノードを全部動かしていた） |
| 読み込みのログ | 同じ名前があれば「骨とアニメーションは N 番のノード（骨）に付けます」と警告する（止めはしない） |

採らなかった方法：
- `aiProcess_PopulateArmatureData` の `aiBone::mNode`：assimp の中でも名前で探すので、同じく先の方になる。
- 名前を付け直す（`body` と `body.001`）：カーブと骨は元の名前のままなので、どちらにつなぐかは結局決めないといけない。

## 確かめたこと（2026-11-12）
- Debug・Development・Release のビルドが通る。
- Sandbox にそのモデル（TD2_2 の `OO_Frame_Remake_actions.gltf`）と Animator（Idle）を置いたシーンを一時的に作り、Scene ビューで胸・背中の羽根が付いた Idle の形で出るのを見た。骨の線も原点へ伸びない。ログに「同じ名前のノードが2つ以上あります「body」。骨とアニメーションは 7 番のノード（骨）に付けます」が1回出た。確かめた後、モデルとシーンは消した（チームのモデルなのでエンジンに入れない）。

## 残り
- `.glb` の埋め込みテクスチャ（パスが `*0` の形）は読めず、`TextureManager::Load` の assert で止まる → [GlbEmbeddedTexture.md](GlbEmbeddedTexture.md) で直した。
