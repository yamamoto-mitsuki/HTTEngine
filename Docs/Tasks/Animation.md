# Animation：アニメーションで何ができるか・どう作るか・学校との分担

## このファイルについて

- 2026-09-29 の依頼：「アニメーションのシステムも作りたい。将来は Skeleton・Skinning をやりたいが、**学校の課題の範囲なので自分でやりたい**。そもそもアニメーションで何ができるようになるか、どういう実装をするか」。
- **今回は設計・分担・順番だけ**（写経用のコードはまだ無い）。
- **学校の範囲（クオータニオン・キーの読み込みと補間・Skeleton・Skinning）は、この md にコードを書かない**。ここでは「エンジンのどこに置くか」「エンジンにつなぐ所」だけを決める。つなぐ所（Component・System・Inspector・保存・描画への渡し方）は、始めるときに md で出す。
- 2026-09-30：5章の「決めること」は全部「はい」。**学校の課題はずっと先なので、締め切りは考えなくてよい** → **SceneFlow（SF0〜SF4）と S5 小さなゲームの後**に始める（4章）。
- 話題ごとに1ファイルで、**終わっても消さずに更新し続ける**。
- 関連：[Model.md](Model.md)（assimp の読み込み・ノード階層。Step 4 がこのファイルになった）、`SceneFlow.md`（先にやる作業）、[Compute.md](Compute.md)（CS の土台。GPU スキニングで要る）。

| 読む順番 | 何が書いてあるか |
|---|---|
| [1. 何ができるようになるか](#1-何ができるようになるか) | 段階ごとの「できること」と例 |
| [2. どう作るか](#2-どう作るかこのエンジンでの形) | 流れの図と、部品ごとの置き場所・誰が書くか |
| [3. 学校のサンプルとの違い](#3-エンジンに入れるときに気を付けること学校のサンプルとの違い) | エンジンに入れるときの落とし穴 |
| [4. 順番](#4-順番) | AN1〜AN7 |
| [5. 決めること](#5-決めること) | 始める前に教えてほしいこと |
| **[6. AN2-1：AnimationManager](#6-an2-1animationmanagergltf-のアニメーションを読み込んで番号で持つ2026-10-14)** | **番号管理にするか・読み込みのコード全部（2026-10-14）** |

---

## 進捗

| Step | 作業 | 誰が書くか | 状態 |
|---|---|---|---|
| AN1 | クオータニオン（`Math/Quaternion`） | 自分（学校） | これから |
| AN2 | ノードアニメーション（キーの読み込み・補間）＋エンジンにつなぐ（Animator Component・AnimationSystem） | 読み込み・補間＝自分（学校）／つなぐ所＝md | **AN2-1 読み込み（AnimationManager）＝写経待ち（6章）**。補間・再生はこれから |
| AN3 | Skeleton（骨の階層）＋骨の線を Scene ビューに | Skeleton＝自分（学校）／線＝md でも可 | 設計済み |
| AN4 | Skinning（頂点を骨で曲げる） | 自分（学校）／パイプラインと渡し方＝md でも可 | 設計済み |
| AN5 | 切り替えを滑らかに（クロスフェード） | 相談 | 後 |
| AN6 | イベント（足音・攻撃判定）・状態の切り替え（ステートマシン） | 相談 | 後 |
| AN7 | エディタで作るアニメ（Component の値にキーを打つ） | md | 後 |

---

## 1. 何ができるようになるか

| 段 | できること | ゲームでの例 | 中で何をしているか |
|---|---|---|---|
| 0 | （今でもできる）値を時間で動かす | UI が滑って出てくる、点滅、跳ねる | `Easing.h`（`Ease::Type`）＋ SYSTEM で自分で書く |
| **1** | **ノードアニメーション**：モデルの部品が動く | 扉が開く、風車・プロペラ、ロボットの腕（硬い部品） | glTF の中のキー（時間ごとの位置・回転・大きさ）を補間して、ノードの行列を作り直す |
| **2** | **スケルトン＋スキニング**：体が曲がる | キャラの待機・歩き・走り・攻撃・やられ | 骨（Joint）の階層を 1 と同じく動かし、各頂点を「近くの骨4本の重み付き」で曲げる |
| 3 | 切り替えと混ぜる | 待機 → 歩き が滑らか、速さで歩きと走りを混ぜる、上半身だけ攻撃 | 2つのポーズを割合で混ぜる（クロスフェード・ブレンド・レイヤー） |
| 4 | 状態と切り替えの条件をエディタで | 「速さ > 0.1 で Idle → Run」「攻撃ボタンで Attack、終わったら Idle」 | ステートマシン（Unity の Animator Controller） |
| 5 | イベント | 足が着いたフレームで足音、振り下ろした瞬間だけ攻撃の当たり判定 | 決めた時間を過ぎたら知らせる → **AudioSystem・CollisionSystem とつながる** |
| 6 | エディタで作るアニメ（どの値でも） | 動く床、ライトの点滅、カメラの演出、UI の演出 | Unity の Animation ウィンドウ。Component の値にキーを打つ（`ComponentUI` の項目名がそのまま使える） |
| 後 | IK（足を地面に合わせる）・ルートモーション・別のモデルへの付け替え（リターゲット）・GPU（CS）スキニング | | |

**学校の課題は 1 と 2**。ゲームとして「動いている感じ」が出るのは 2 から、「遊べる感じ」が出るのは 3・5 から。

---

## 2. どう作るか（このエンジンでの形）

### 流れ

```
[ファイル]  glTF の animations ──assimp──▶ AnimationClip（素材。モデルと一緒に ModelManager が持つ）
                                              ・ノード名ごとに「位置のキー・回転のキー・大きさのキー」
[Entity]    Animator Component（どのクリップ・速さ・ループ・Play On Start）＋ Model Renderer
                │
[AnimationSystem] 毎フレーム（Entity ごと）
                │ ① 時間を進める（deltaTime × 速さ。ループなら巻き戻す）
                │ ② サンプル：時間 t の前後のキーを探して補間（位置・大きさは Lerp、回転は Slerp）＝ポーズ
                ├─ 段1：ポーズ → ノードの行列（親から順に掛ける）→ ModelRenderSystem がノードごとに描く
                └─ 段2：ポーズ → Skeleton（骨の行列）→ 行列パレット（骨の数だけの行列）→ VS で頂点を曲げる
```

### 部品・置き場所・誰が書くか

| 部品 | 何をする | 置き場所（案） | 誰が |
|---|---|---|---|
| Quaternion | 回転を4つの数で。掛け算・Slerp・行列にする | `HTTEngine/Math/Quaternion.h/.cpp` | 自分（学校） |
| AnimationClip | キーの並び（ノード名ごと）・長さ | `HTTEngine/Graphics/Model/`（`ModelAsset` に `animations` を足す） | 自分（学校） |
| 補間（サンプル） | 時間 t の値を、前後のキーから作る | 同じ所 | 自分（学校） |
| Skeleton | 骨（Joint）の配列・親の番号・親から順に行列を作る | `HTTEngine/Graphics/Model/Skeleton.h/.cpp` | 自分（学校） |
| SkinCluster | 頂点ごとの骨4本と重み・逆バインド行列・行列パレット | `HTTEngine/Graphics/Model/SkinCluster.h/.cpp` | 自分（学校） |
| スキン用の VS | `Object3d.VS` のスキン版（パレットで頂点と法線を曲げる） | `Shader/Data/Model/` | 自分（学校） |
| **Animator Component** | クリップ・再生するか・速さ・ループ・Play On Start | `HTTEngine/Components/Animation/AnimatorComponent.h` | md |
| **AnimationSystem** | Entity ごとの「動いている状態」（時間・ポーズ・パレット）を持ち、毎フレーム ①② を回す | `HTTEngine/Systems/Animation/AnimationSystem.h/.cpp` | md |
| 描画への渡し方 | ノードの行列・パレットを `DrawModel` に渡す。スキン用の PSO | `ModelRenderSystem`・`Renderer`・`PSOManager` | md（学校の VS を差し込む） |
| Inspector | クリップの選択（モデルの中のクリップ名のコンボ）・停止中に時間のスライダーでプレビュー・骨の線 | `AnimationSystem.cpp` の Describe | md |
| 保存 | クリップは名前で保存（番号はファイルが変わるとずれる） | 同上 | md |

> 学校の資料の名前（`Animation`・`NodeAnimation`・`Skeleton`・`Joint`・`SkinCluster` など）はそのまま使ってよい。上の表の名前は置き場所を決めるための仮の名前。

---

## 3. エンジンに入れるときに気を付けること（学校のサンプルとの違い）

学校のサンプルは「モデル1つ・アニメ1つ」を main の近くで持つ形が多い。エンジンでは **同じモデルを何体も置き、Play / Stop・Undo・保存がある**ので、次の所が変わる。

1. **素材と「動いている状態」を分ける**
   - 素材（クリップのキー・骨の初期形・頂点の重み・逆バインド行列）は **モデルごとに1つ**（ModelManager が持つ。何体置いても共有）。
   - 動いている状態（今の時間・ポーズ・パレット）は **Entity ごと**。これは **Component に入れず、AnimationSystem が持つ**（AudioSystem の `SourceState` と同じ理由：Component はコピー・Undo・保存で丸ごと写されるので、`std::vector` やポインタを入れられない）。
2. **座標の向き**：エンジンは読み込むときに「転置＋X を反転」している（Model.md Step 1）。キーも同じにそろえる：位置は `x` を反転、回転（クオータニオン）は `y` と `z` を反転、大きさはそのまま（学校の資料と同じ）。
3. **Entity の回転（Transform）は今のままオイラー角でよい**。クオータニオンは「アニメの中（ノード・骨）」だけで使う。Entity の回転をクオータニオンにするのは、真上を向いて壊れる（ジンバルロック）ので困ってから（保存の版を上げて読み替える。Serialize.md）。
4. **ノードの行列**：今の `ModelAsset::Node` は `localMatrix`・`worldMatrix` を読み込むときに1回だけ計算している（動かない前提）。アニメさせる Entity は、Entity ごとに「ノードの行列の配列」を作り直して `DrawModel` に渡す（渡さなければ今までどおり素材の行列）。
5. **パレットの置き場所と SRV**：パレット（骨の行列の並び）は Entity ごとに GPU へ送る。
   - おすすめ：**毎フレーム、フレームごとの一時バッファ（`FrameUploadBuffer`）に書いて、そのアドレスを直接渡す**（ルート SRV。ディスクリプタのスロットを使わないので返し忘れが起きない）。**パーティクルがもう同じやり方をしている**（`RenderContext::DrawParticles` の `upload.PushArray(...)` → `SetGraphicsRootShaderResourceView`）。
   - スロット（`AllocateSRVSlot`）を使うなら、**Entity を消したら・Stop したら `FreeSRVSlot` で返す**（Play / Stop 9回で落ちた SRV 漏れ＝Collision.md と同じ罠）。
6. **1フレームの順番**：ゲームの SYSTEM（「走れ」と決める）→ **AnimationSystem** → `UpdateTransforms` → … → 描画。`UpdateTransforms` より前に置くのは、後で「骨に剣を持たせる（子 Entity を骨についていかせる）」をやるため。Pause（`timeScale` 0）では `deltaTime` が 0 なので自然に止まる。停止中（Editing）は進めない（Inspector のプレビューだけ）。
7. **スキンのあるメッシュ**：glTF の決まりで、スキンのあるメッシュにはノードの行列を掛けない（骨の行列だけで置く）。1体のモデルに「スキンのあるメッシュ」と「無いメッシュ（武器など）」が混ざることがある。
8. **GPU（CS）スキニングは後**：エンジンにまだ Compute Shader の土台が無い（Compute.md）。VS でのスキニングを先に。

### ノードアニメで動かすのは「モデルの中のノード」か「Entity」か

| 案 | 中身 | 良い所 | 困る所 |
|---|---|---|---|
| **A（おすすめ）** | モデルの中のノード（`ModelAsset::Node`）を動かす。Entity は1つのまま | 学校と同じ形。速い。スキニング（段2）と同じ流れ | 部品を Hierarchy で見られない・部品に物を付けられない |
| B | ノードを Entity として取り込み（Model.md Step 2）、Entity の Transform を動かす | Hierarchy で部品が見える。部品に Collider や子を付けられる | Entity の回転はオイラー角なので、クオータニオン → オイラー角の変換が要る（真上付近で壊れる） |

A で始めて、B は Model.md Step 2 をやるときに考える。

---

## 4. 順番

| 順 | Step | やること | 確認 |
|---|---|---|---|
| 1 | AN1 | クオータニオン（学校） | X→Y→Z の順に回したクオータニオンを行列にすると、今の `MakeAffineMatrix` の回転（`MakeRotateXMatrix`・`Y`・`Z` を掛けた物）と同じになる |
| 2 | AN2 | ノードアニメ：学校のキーの読み込み・補間 ＋ md で Animator Component・AnimationSystem・`DrawModel` にノードの行列を渡す | 学校の確認用モデル（キーで回る箱など）を置いて Play → 動く。Stop で最初の形に戻る。2体置いて速さを変えると別々に動く |
| 3 | AN3 | Skeleton（学校）＋ md で骨の線を Scene ビューに | 骨の線がアニメに合わせて動く |
| 4 | AN4 | Skinning（学校）＋ md でスキン用の PSO・パレットの渡し方 | キャラが歩く。Play / Stop を10回以上繰り返しても落ちない（SRV） |
| 5 | AN5〜 | クロスフェード → イベント → ステートマシン → エディタで作るアニメ | 小さなゲーム（S5）で欲しくなった物から |

- AN2 の md は「学校で書いた関数をそのまま差し込める形」にしたいので、**始めるときに学校で書いたコード（関数の名前と引数）を教えてほしい**。そこに合わせてつなぐ所を書く。
- SceneFlow.md とぶつかるファイルはほぼ無い（`WindowManager.cpp` の1フレームの順番に1行足すくらい）。
- **始める時期（2026-09-30 決定）**：学校の課題はずっと先なので締め切りは考えず、**SceneFlow（SF0〜SF4）→ S5 小さなゲーム → このファイル**の順にする（`SceneFlow.md` の4章）。

---

## 5. 決めること

**答え（2026-09-30）：上の3つは全部「はい」。4つ目は「学校のアニメーションの課題はずっと先」→ SceneFlow と S5 の後に始める。**

| 決めること | おすすめ | 理由 | 答え |
|---|---|---|---|
| 学校の範囲（AN1・AN2 の読み込みと補間・AN3・AN4）は md にコードを書かない | はい（依頼どおり） | 自分で書く。md は置き場所とつなぐ所だけ | はい |
| つなぐ所（Component・System・Inspector・保存・描画への渡し方）は md で出してよいか | はい | エンジンの中の決まり（状態を Component に入れない・SRV を返す・保存は名前で）が多く、学校の資料には無い所 | はい |
| ノードアニメは「モデルの中のノード」を動かす（案 A） | はい | 学校と同じ形で、スキニングにそのままつながる | はい |
| 学校は今どこまで進んでいるか | — | ノードアニメが終わっていれば AN2 から、まだならクオータニオン（AN1）から。締め切りも教えてほしい（SceneFlow との順番を決める） | 課題はずっと先。締め切りは考えなくてよい → SceneFlow・S5 の後。**始めるときに、学校で書いた所まで（関数の名前と引数）を教えてほしい** |

---

## 6. AN2-1：AnimationManager（glTF のアニメーションを読み込んで、番号で持つ）（2026-10-14）

- **説明付きで読みたいときは `AnimationManager.md`**（同じコードに、番号管理と読み込みの仕組みの説明を付けた物）。
- 2026-10-14 の依頼：「gltf を読んで再生。まだコンポーネント化はしない」「途中まで作った AnimationManager を md に。サウンド・モデルみたいな番号管理の形がいいか迷っている」。
- **5章では「学校の範囲（読み込みと補間）は md にコードを書かない」としていたが、今回は依頼なので読み込みまで書いた。** 補間（時刻 t の値を取り出す）と再生はまだ書いていない（6.6）。
- このコードは worktree で `Animation.h`（作りかけの物）・`AnimationManager.h` の続きとして書き、エンジンを Development・Release でビルドして 0エラーを確かめた。**アニメ入りの glTF がリポジトリに無いので、読み込みの実行はしていない**（6.5）。

### 6.1 番号管理にするか → する（おすすめ）

| 案 | 中身 | 良い所 | 困る所 |
|---|---|---|---|
| **A（おすすめ）番号管理** | `AnimationManager::Load(パス)` が番号を返す。同じパスなら読み直さない。使う側は番号で `GetAnimation(番号)` | ModelManager・TextureManager・SoundManager と同じ使い方になる。同じアニメを何体で使っても1回しか読まない。後でコンポーネントにしたとき、Component に入れるのは番号だけ（保存は `GetPath` でパスに直す）。解放は Release 1か所 | ファイルが少し増える（今回書いた分） |
| B `Animation` を値で返すだけ | `Animation LoadAnimationFile(パス)` を呼んだ人が持つ（学校のサンプルの形） | 短い | 同じファイルを2回読む。誰が持っているか散らばる。コンポーネントにすると Component に `std::map` が入ってしまう（Component はコピー・Undo・保存で丸ごと写されるので、大きな物は入れない決まり） |
| C ModelAsset の中に持たせる | モデルを読むときに一緒にアニメも読む | モデルとアニメが同じ glTF なら1回で済む | アニメだけのファイル（同じ骨で「歩き.gltf」「走り.gltf」を分けて書き出す）を使えない。ModelManager.cpp がさらに大きくなる |

**A にする。** B の `LoadAnimationFile` は、A の中の private 関数として残す（学校の形のまま中で使える）。

**1つのファイルに複数のアニメーションが入っていることがある**（Blender で「待機」「歩き」を1つの glTF に書き出すなど）。番号は **ファイルに1つ** 付けて、中のアニメーションは `index`（0, 1, 2…）で選ぶ。名前で選びたくなったら `Animation::name` を見る。

### 6.2 Animation.h（作りかけの物に足す所だけ）

初期値の無いメンバに初期値を書く（SmartPointer のときと同じ理由。初期値が無いと、何が入っているか分からない値から始まる）。`Animation` に名前を足す。

```cpp
template <typename T> 
struct Keyframe {
	T value{};         // 値
	float time = 0.0f; // 時刻（秒）
};
```

```cpp
struct Animation {
	std::string name;       // アニメーションの名前（Blender のアクション名など。1ファイルに複数あるとき見分ける）
	float duration = 0.0f;  // 全体の尺（秒）
	std::map<std::string, NodeAnimation> nodeAnimations; // NodeAnimationの集合
};
```

### 6.3・6.4 AnimationManager.h・.cpp（全部）

**コードは `AnimationManager.md` の5章・6章に移した**（2026-10-14、パスを `std::filesystem::path` で受け取る形に変えたので、1か所にまとめた）。仕組みの説明もそちら。

### 6.5 エンジンに登録する

**HTTEngine.vcxproj**（ソリューションエクスプローラーで `Graphics/Animation` フィルタに `AnimationManager.cpp` を追加、でもよい）：

```xml
    <ClCompile Include="HTTEngine\Graphics\Animation\AnimationManager.cpp" />
```

**Engine.cpp**：

```cpp
#include "HTTEngine/Graphics/Animation/AnimationManager.h"
```

```cpp
	ModelManager::Initialize();
	AnimationManager::Initialize();
```

```cpp
	// --- 今まで Release を呼んでいなかった物（作りっぱなしで、終わるときに消していなかった）---
	AnimationManager::Release();
	IBLBaker::Finalize();
```

- CPU のデータだけ（GPU の物を持たない）なので、消す順番はどこでもよい。シーンが消えた後（Finalize の下の方）に置いた。

**確認**：アニメ入りの glTF（例：Khronos の glTF サンプルの `AnimatedCube`・`BoxAnimated`、学校の配布物）を `resources/` に置いて、どこか（シーンの Initialize など）で

```cpp
uint32_t handle = AnimationManager::Load("resources/models/AnimatedCube/AnimatedCube.gltf");
const Animation* animation = AnimationManager::GetAnimation(handle);
```

を呼ぶ。Log ウィンドウに `読み込み完了 ... アニメーション数=1` が出て、`animation->duration`（秒）と `animation->nodeAnimations.size()` がそれらしい値になっていればよい（ブレークポイントで中を見る）。同じパスで2回 Load して、同じ番号が返ることも見る。

### 6.6 次（再生）

まだコンポーネントにしないので、次は次の3つ。置き場所は同じ `Graphics/Animation/`。

1. **補間**：`Vector3 CalculateValue(const AnimationCurve<Vector3>& curve, float time)` と、Quaternion 版（`Slerp` を使う）。時刻 t の前後のキーを探して混ぜる（学校の範囲）。
2. **再生の状態**：今の時刻を持って、毎フレーム `deltaTime` を足し、`duration` を超えたら巻き戻す（ループ）。コンポーネントにするまでは、シーンのメンバに持つ小さなクラス（例：`AnimationPlayer`）でよい。
3. **描画へつなぐ**：ノード名で `NodeAnimation` を探し、S・R・T から作った行列を `localMatrix` の代わりにして、親から順に掛ける → `DrawModel` に渡す。今の `DrawModel` は素材の `worldMatrix` しか使わないので、ここは md で出す（3章の4）。
