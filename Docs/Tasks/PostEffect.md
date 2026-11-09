# PostEffect系の作業（Bloom / Lens / HDR / 描画サイズ）

## このファイルについて
- ポストエフェクトと、描画先（RenderTexture）の形式・サイズに関する作業を書く。
- 決まったことは「決めたこと」の表に理由と一緒に追記する。消さずに更新し続ける。
- 行番号は2026-09-16時点の本体側（`feature/master` の作業中の状態）のもの。

---

## 予定

| 順番 | 作業 | 状態 |
|---|---|---|
| 1 | HDRレンダリング：シーンの描画先をfloatにする | **済み**（10/26。G-2。`HDRRendering.md` のパートA） |
| 2 | トーンマッピング＋露出のパスを追加する | **済み**（10/26。最後のレンズのパスに入れた。Settings → PostEffect → ToneMap） |
| 3 | 描画サイズを可変にする（出力先に合わせる） | これから |
| 4 | Bloomの小さな修正（使っていない `up_[3]` の削除など） | 3と一緒に |
| 5 | HDR出力（HDR10対応モニタ向け） | 最後 |

---

## 決めたこと

| 決めたこと | 理由 |
|---|---|
| 将来はHDRで描画する | 4Kなどの高画質を再現できるようにしたい |
| HDRレンダリングを先にやり、HDR出力は後にする | HDRレンダリング（内部をfloatで計算し、トーンマッピングで収める）は、普通のSDRモニタでも画質が上がる。HDR出力は対応モニタとOS設定が必要 |
| HDRにしてもライティングはリニア空間のまま | HDRとリニア空間は対立しない。変わるのは「計算結果の置き場（1.0で頭打ちになるか）」と「最後にモニタへ出す部分」だけ |
| 4KとHDRは別々に進める | 4Kは解像度、HDRは明るさの幅の話。4KのSDRも、1080pのHDRもありうる |
| 描画サイズは「上限を上げる（4K固定）」ではなく「出力先と同じ大きさにする」 | 4K固定だと、1280×720のビューで9倍のピクセルを描くことになる。Bloomの縮小テクスチャも全部大きくなる |
| Scene,GameビューはBloomをかける | Gameビューは製品版で見える絵なので、演出はここで確認する（`Editor.md`）。Sceneビューも見えるようにしとき、ON, OFFができるようなトグルを用意する |
| 3D の PSO の描画先は描く形で決める（Model・Particle・Line は HDR、Sprite・Text は SDR）（10/26） | `PSOManager::MakePSO` の1か所で済む。PSO の鍵を増やさなくてよい |
| ポスト無しの道（`PostEffectMode::None`）も HDR の絵に描いて、トーンマップだけ通す（10/26） | 3D の PSO が HDR の形なので、SDR の画面へ直に描けない。どの道も同じ明るさの決まりで画面へ出る |
| 露出・トーンマップは最後のレンズのパスに入れる。風の線・ビネットはトーンマップの後（10/26） | パスを増やさない（Unity URP の Uber と同じ）。前の見た目（1 で切った絵に掛けていた）と同じになる |
| Exposure はブルームの後で掛ける。初めの値は None・Exposure 1（10/26） | Threshold は露出の前の値で決まるので「Intensity が 1 を超えた物だけ光る」が崩れない。初めの値で今の見た目のまま |

---

## 問題点・未解決

### 1. Bloomへの入力が1.0で頭打ちになっている
> **10/26 に解決**（G-2。`postRT_`・`work_`・`depthOfFieldRT_` を HDR にした。`HDRRendering.md`）。下は前の状態のメモ。
- `postRT_` も `work_` もSDR（`R8G8B8A8_UNORM_SRGB`）。
- `Threshold 0.75` だと、光るのは明るさ0.75〜1.0の範囲だけ。「明るさ1.0の場所」と「明るさ10の場所」を区別できないので、Bloomの効きが本来より弱い。
- → 予定1（HDRレンダリング）で解決する。そのときCompositeのPSOの出力形式も合わせて変える。

### 2. 描画サイズが1920×1080で固定されている
- `postRT_` は起動時（`Engine.cpp:95`）に1回だけ作られ、その後変わらない。3Dは出力先の大きさに関係なく常に1920×1080で描かれ、最後のLensで出力先の大きさに縮小・拡大される。

| 出力先 | 実際の処理 | 起きること |
|---|---|---|
| Gameビュー 1280×720 | 1920×1080で描いて縮小 | 見えない分まで描いていて無駄（約2.25倍） |
| ウィンドウ 1920×1080 | そのまま | 問題なし |
| ウィンドウ 2560×1440 / 4K | 1920×1080で描いて拡大 | **ぼやける。4Kで表示しても中身は1080p** |

- Gameビュー自体のサイズも `EditorViewport.cpp:12-13` で1280×720に固定されている。
- → 予定3で解決する。

### 3. 描画サイズを可変にするときに解決すること
1. **出力先ごとに `postRT_` とPostProcessを持たせる**
   - 今は全部の出力先で1つだけ共有している。Gameビュー（1280×720）とウィンドウ（1920×1080）は同じフレームに描かれるので、共有したまま大きさを合わせると毎フレーム作り直すことになる。
2. **RenderTextureに作り直す仕組みを作る**
   - `RenderWindow::Resize` はあるが、RenderTextureには無い。
   - GPUが使っている最中のテクスチャを消すと落ちるので、作り直しはフレームの境目で行う（`WindowManager.cpp:261` でスワップチェーンをリサイズしているのと同じタイミング）。
   - 作り直したときにSRVスロットが解放されるかも確認する。
3. **Bloomの縮小テクスチャも一緒に作り直す**

### 4. `up_[3]` が使われていない
- `BloomPass::Initialize` で作っているが、Upのループは `up_[2]` から始まるので一度も使われない。消してよい。

### 5. 1フレームにBloomを使えるビューは4つまで
- 1ビュー8パス、上限 `kMaxPassPerFrame = 32`。`SceneRenderer` の `kMaxViewsPerFrame = 4` と一致しているので今は問題ない。
- どちらかを変えるときは両方そろえる。

### 6. ギズモにBloom / Lensがかかる
- ギズモがポストエフェクトより前に描かれているのが原因。詳細は `Editor.md` の「未解決」。

### 7. Lensの歪み（Distortion）をかけると、画面の端が黄色くなる（2026-09-18に調べた）
**クリアカラーではない。** `work_`（Bloomの結果）の**端のピクセルが引き伸ばされたもの**に、色収差で青だけ別の場所から取ってきた結果。

1. `Lens.PS` は `warped = saturate(uv + offset * dist2 * distortion)`。歪みを強くすると画面の四隅は「テクスチャの外」を指すが、`saturate` で0〜1に丸めるので、**端の1列が引き伸ばされる**（これが余白に見える部分）。
2. 色収差は R を外へ、B を内へずらしている。
   - `color.r = Sample(saturate(warped + offset * shift)).r` → 外向き。すでに端なので**端の色のまま**
   - `color.g = Sample(warped).g` → **端の色**
   - `color.b = Sample(saturate(warped - offset * shift)).b` → 内向きなので、**端ではない別の場所の色**
3. つまり余白では「R・Gは端の明るい色、Bだけ内側の色」になる。青が抜けた明るい色 ＝ **黄色**。端に空や明るい床があるほど強く出る。

- **ビネットが黒いのは、`color *= 1 - saturate(dist2 * vignette)` でRGBを同じ割合で暗くしているだけだから**（色の比率が変わらない）。歪みは「どこを読むか」を変えるので、チャンネルごとにずれ方が違うと色が変わる。
- 確かめ方：`Aberration` を0にすると黄色が消え、端の色がただ引き伸ばされた見た目になる。

| 直し方 | 内容 | 向き / 不向き |
|---|---|---|
| 少し拡大する | 歪ませる前にUVを中心へ縮める（`uv = 0.5f + offset * (1.0f - zoom)`）。四隅でもテクスチャの中を指すようになる | 余白が出ない。普通のゲームはこれ。画面の端が少し切れる |
| 範囲外を黒にする | `warped != saturate(warped)` のときは黒を返す | レンズらしい黒枠になる。絵が切れない |
| 色収差を弱める | 範囲外に近いほど `shift` を0に近づける | 今の見た目のまま黄色だけ消せる |

- ついでに：サンプラーは全シェーダー共通で **WRAP**（`RootSignatureManager::MakeStaticSampler`）。`Lens.PS` は自分で `saturate` しているので今は問題ないが、`saturate` を忘れると反対側の端が回り込む。CLAMPのサンプラーも選べるようにしておくと安全。

---

## HDR化の手順（予定1・2・5の中身）

### 用語
- **HDRレンダリング**：描画先をfloat（`R16G16B16A16_FLOAT`）にして、1.0を超える明るさを残したまま計算する。最後にトーンマッピングで表示できる範囲に収める。SDRモニタでも画質が上がる。
- **HDR出力（HDR10）**：HDR対応モニタへ、1.0を超える明るさのまま送る。スワップチェーンの形式と色空間の設定変更が必要。

### 今の対応状況
| もの | 形式 |
|---|---|
| IBLのキューブマップ、Skybox、Bloomの縮小テクスチャ | float（HDR） |
| `postRT_`（SceneRenderer）、`work_`・`depthOfFieldRT_`（PostProcess） | **HDR**（10/26 に変えた） |
| `sceneRT_` / `gameRT_`（EditorViewport） | SDR（トーンマップの後の画面。2D もここに重ねる） |
| トーンマッピング | **最後のレンズのパス**（None／Reinhard／ACES／GT・Exposure）（10/26） |
| スワップチェーン | `R8G8B8A8_UNORM`（RTVは `_SRGB`） |

### 手順
1. `postRT_` と `work_` をfloatにする（CompositeのPSOの出力形式も変える）
2. Bloom合成の後、最終出力の前に、トーンマッピングと露出のパスを追加する
3. トーンマッピングの結果を、今の `_SRGB` 出力のまま画面に出す。**ここまでで普通のモニタでも画質が大きく上がる**
4. HDR出力に対応する
   - スワップチェーンを `R10G10B10A2` + PQ色空間（またはfloat + scRGB）にする
   - モニタがHDRに対応しているか確認する
   - ImGuiを合成するときの明るさを調整する

### Light側との関係
- ライトの色はsRGBでComponentに持ち、GPUへ送るときにリニアへ変換する（`Light.md` Step 5）。HDRにしてもこの形は変わらない。
- ライトの明るさ（1.0超え）は `intensity` で表す。HDRレンダリングにして初めて、1.0を超えた分がBloomとトーンマッピングに正しく反映される。
---

## G-2 の下調べ（2026-10-25。AL4_01 のゴールだけ光らせる → HDR に決めた）

> **10/26 に作った → `HDRRendering.md`**（パートA：HDR の描画先とトーンマップ／B：色を float に／C：Intensity）。下は作る前のメモ（「トーンマップを入れる場所の候補」はブルームの合成ではなく、最後のレンズのパスにした）。

新しいチャットで始めるためのメモ。前提：`ParticleSystem.md`（E）と `KawaseBloom.md`（G-1）を写した後のエンジン。

### 決めたこと
| 決めたこと | 理由 |
|---|---|
| ゴールだけ光らせるのは、グロー層ではなく **HDR** | 色が 1 を超えた物だけが光る（Bloom の Threshold 1 で、空・円などの 1 以下は光らない）。光る物を2回描かない。エンジンとして広く使える（この表の予定1・2）。ユーザーが挑戦したい |
| **色は float にする**（Renderer の Config・リクエストの `uint32_t` 0xRRGGBBAA → `Vector4`）。Component・Inspector は「色（0〜1 の色見本）＋ Intensity（明るさの倍率。初めは 1）」で持ち、Renderer へ渡すときに掛ける（10/25 に変えた） | ユーザーの判断「変更が少ないより、クオリティ優先」。8 ビットの色コードは 1 を超えられず、HDR の値を運べないのが根っこ。float にすれば、どの Renderer も明るさをそのまま運べ、色を動かすときの段（1/255）も無くなる。色見本は 0〜1 の方が選びやすく、Intensity を分けておくと色合いを変えずに明るさだけ上げ下げできる（Unity の HDR カラーと同じ）。保存は今の色＋Intensity なので、前のシーンもそのまま読める |

### コードを見て分かったこと
- **3D の PSO の出力の形式は1か所で SDR に決め打ち**：`PSOManager::MakePSO` の `desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`。DrawCategory（Model・Particle・Line は 3D、Sprite・Text は 2D）で分ければ、3D だけ HDR にできる。
- ほかに SDR 決め打ちの PSO：`Skybox.cpp`・`DepthOfFieldPass.cpp`（合成）・`BloomPass.cpp`（合成の2つ）・`LensPass.cpp`。描画先は `SceneRenderer` の `postRT_`・`PostProcess` の `work_`・`depthOfFieldRT_`（どれも `RenderTextureFormat::SDR`）。
- **ポスト無しの道**：`SceneRenderer::RenderToTexture` の `PostEffectMode::None`（と `postProcess_` が無いとき）は、3D を SDR の描画先へ直に描く。3D の PSO を HDR にするなら、ここも HDR の絵 → トーンマップを通す形にする。
- 2D（Sprite・Text・UI）はポストの後に SDR の描画先へ重ねるので、今のままでよい。
- トーンマップを入れる場所の候補：ブルームの合成（`BloomComposite` / `BloomKawaseComposite`。HDR のシーン＋光 → SDR の `work_`）。ブルームを切っているときの道も要る。初めの値は「None（1 で切る＝今の見た目のまま）」にして、ACES などを選べるようにする。
- Bloom の今の値は `Parameters/PostEffect/Bloom.json` で **Threshold 1.0**（AL4_01）。SDR では 1 を超えないので、今はほとんど光らない。
- Intensity を足す候補：Particle System（`ParticleInstancesConfig.color` は float の Vector4 なので、そのまま 1 を超えられる）・Primitive Renderer（ゴールの扉）。Renderer の多くの Config の色は `uint32_t`（0xRRGGBBAA）なので、倍率を別の値で渡す。
- Release の exe は `AL4_01/project/HTTEngine/Shader` の写しを読む（エンジンをビルドし直すと自動で写る。.shader だけ直したときはリビルド）。
- 作業用コピーで確かめるときの道具（前のチャットで使った）：AL4_01 を一時フォルダへ clone（サブモジュールは `git -c protocol.file.allow=always submodule update --init` でローカルの HTTEngine から）・assimp の lib を写す・MSBuild（VS 18）で `AL4_01.slnx` をビルド・PowerShell の `PrintWindow`（`SetProcessDPIAware` を先に）で窓を撮る・エディタの操作は `ImGuiManager::Begin` に一時的な入力の仕掛け（md には載せない・終わったら外す）。

### G-3（スター）の目標（ユーザーが見せてくれた絵・10/25）
- DirectX SDK の HDRLighting のサンプルと同じ形（川瀬式のスター）：明るい点から十字・×・6本などの光の筋が伸び、**筋の色が虹色に変わる（Twinkle Star (Spectral)）**。芯は白く、筋の先へ行くほど薄く色付く。
- 調整項目：**Star Type**（筋の本数と角度の組。Cross＝4本・Snow Cross＝6本・Sunny Cross＝8本・Vertical＝2本・Twinkle Star＝虹色の4本 など）・**Star Threshold**（ブルームとは別のしきい値）・**Star Angle**（筋の向き）・**Star Exposure**（強さ）。あわせて Bloom の **Exposure**（露出）。
- 作り：1本の筋は「向きに沿って4点を読んで足す」を、読む間隔を 4 倍ずつ広げて3回（1・4・16 ピクセル → 長さ 64）。回ごとに色（波長）を変えると虹色の筋になる。本数分作って足す。
- **HDR が先**：絵の「白い芯と色の筋」は、1 を大きく超える明るさをしきい値で抜き出して作っている（SDR では、どの明るい所にも同じだけ筋が出て、芯も白く飛ばない）。なので G-2（HDR）→ G-3（スター）の順。
