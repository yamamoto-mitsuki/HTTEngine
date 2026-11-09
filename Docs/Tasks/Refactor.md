参考：
- cpprefjp `std::array`：https://cpprefjp.github.io/reference/array/array.html
- cpprefjp `std::to_array`：https://cpprefjp.github.io/reference/array/to_array.html
- cpprefjp `std::clamp`：https://cpprefjp.github.io/reference/algorithm/clamp.html
- cpprefjp `std::format`：https://cpprefjp.github.io/reference/format/format.html
- cpprefjp `std::from_chars`：https://cpprefjp.github.io/reference/charconv/from_chars.html
- cpprefjp `std::numbers`：https://cpprefjp.github.io/reference/numbers.html
- cpprefjp 構造化束縛（`auto [a, b]`）：https://cpprefjp.github.io/lang/cpp17/structured_bindings.html

# リファクタリング（課題の後の整理の手順3）

## このファイルについて
- 2026-10-30、課題の後の整理の **手順3**。CLAUDE.md の決まりに合っていない所を検索で洗い出して、パート A〜F の順に直した。
- **Claude が本体を直接直した**（ユーザーの頼み。今までの写経方式ではない）。何を・なぜ直したかの記録として残す。
- **動きは変えていない**。スタイルの ini の形式・描く線の順番・角の番号も前と同じ。
- 確かめたこと：エンジンを Debug・Development・Release でビルドして通した。**ゲームを起動しての確認はしていない**（6章）。

---

## パートごとの中身

### A：ビルド後の xcopy → 直す所は無かった
- フォルダ分けのとき `project/HTTEngine/Resources` ができたので、エンジンの sln でビルドすると写ってしまうと思っていたが、**間違いだった**。
- ビルド後の処理の最初の行（`if /I "$(SolutionDir)"=="$(ProjectDir)" exit /b 0`）で、エンジンの sln からのビルドでは写さないようになっている。
- できた原因は、Claude が vcxproj を直接ビルドしたとき `SolutionDir` の終わりの `\` を2つにしたこと。[FolderSplit.md](FolderSplit.md) の注意も直した。

### B：π と角度の換算を `MathUtility.h` に
- `kDegToRad`・`kPI`／`kPi`・`kTwoPi` が 10 個のファイルに書いてあった。`MathUtility::kPi`・`kTwoPi`・`kDegToRad`・`kRadToDeg` の4つにまとめた。
- Renderer.cpp の `3.14159265358979f` の手書きも無くなった。`2.0f * kPi` の所は `kTwoPi` にした。
- CollisionSystem.cpp の `kPI` は使われていなかったので消した。
- `GameComponent.h` のグローバルの `kDegToRad`・`kRadToDeg` は**名前を残した**。ゲームのスクリプトが `MathUtility::` を付けずに書くための物で、値は `MathUtility` の物を指す。

### C：`NOMINMAX` を props に1回・`std::clamp`
- `#define NOMINMAX` が 15 個のファイルに1行ずつあったので、`HTTEngine.props` の「全部の構成で共通」に1回だけ書いた。エンジンもゲームも同じ設定になる。
- XInputGamepad の `std::max(0.0f, std::min(1.0f, x))` の5か所を `std::clamp(x, 0.0f, 1.0f)` にした。

**`std::clamp` にしなかった所**
- `std::clamp(値, 下限, 上限)` は、**下限が上限より大きいと未定義動作**になる（Debug だと assert で止まる）。
- 次の2か所はそうなる可能性があるので、`max(min())` のままにした。
  - `Collision.cpp` の `ClosestPointOnAABB`：AABB の min と max が逆の物が来ても、今は落ちない
  - `InspectorWindow.cpp`：画面が狭いと `screenRight - width` が `screenLeft` より小さくなる

### D：SRV のヒープの設定を `DirectXCommon::BindSRVHeap` に
- 「配列を作る → `SetDescriptorHeaps` → 先頭の GPU ハンドルを取る」の2〜3行が 12 か所にあった。
- `DirectXCommon::BindSRVHeap(cmdList)` にまとめた。先頭の GPU ハンドルを返すので、呼ぶ側は `heapStart` を受け取るだけでよい。

### E：箱の角と辺を `AABB`・`OBB` に
- 「番号のビットで角を選ぶ」が6か所、「ビットが1つだけ違う角を結ぶ」が3か所にあった。
- 足した物：
  - `AABB::GetCorners()`・`OBB::GetCorners()`：8つの角を `std::array<Vector3, 8>` で返す（番号の付け方は今までと同じ）
  - `kBoxEdges`（`AABB.h`）：12本の辺の組。今までのループと同じ順番で書いた
  - `PickBounds::GetWorldCorners()`：角をワールド座標で返す（SceneGizmo と ScenePicking で同じことをしていた）
- `Renderer::MakeBoxGeometry` の引数も `const std::array<Vector3, 8>&` にした。
- `IsIsCollidingSphereSphereAABB`（名前が間違っていて、どこからも呼ばれていなかった）は消した。
- Camera.cpp の視錐台の線は、角の番号の付け方が違う（NDC の順）ので、`kEdge` の表のまま残した。

### F：C 流の文字列と配列
- **`snprintf` → `std::format`**：CollisionProfiler・Profiler の2か所。`char buf[64]` が無くなった。
- **ImGuiManager の SaveStyle・LoadStyle**：`fopen`・`fgets`・`sscanf_s`・`fprintf` をやめて `std::ofstream`・`std::ifstream`・`std::getline`・`std::from_chars` にした。
  - 同じ 23 項目を保存と読み込みで2回ずつ手で並べていたのがコピペだったので、**項目の表 `GetStyleFields` を1つ作って、両方でそれを回す**ようにした。項目を足すときは表に1行足すだけでよい。
  - `imgui_style.ini` の形式は同じ（`{:.3f}` は `%.3f` と同じ書き方）。
  - 確かめたこと：SaveStyle・LoadStyle のコードをそのまま抜き出した小さなプログラムで、AL4_01 の本物の `imgui_style.ini` を読んで書き直し、**1バイトも変わらない**ことを確かめた。壊れた行（値が無い・範囲外の色番号・数でない値）を読んでも落ちず、値も変わらない。
- **使っていない構造体を消した**：SoundManager.h の `ChunkHeader`・`RiffHeader`・`FormatChunk`（`char id[4]` が入っていた物）。
- **C配列 → `std::array`**：Camera・CrashHandler・ComponentUI・SceneGizmo・EditorOverlay・ComponentEditor・ProjectWindow・ControlWindow・DirectXCommon・UploadContext・WindowManager・GraphicsDebug・CubeBakePass・PrefilterPass・IBLConfig・ShaderCompiler・RenderContext・Renderer・RenderWindow・KeyboardInput・XInputGamepad・Matrix4x4・Quaternion・SpriteRenderSystem・GlobalVariables・OBB。
  - D3D12 や Win32 に渡す所は `.data()` と `.size()` で渡す。

---

### G：SoundManager の整理（2026-10-30）
- 900 行 → 638 行＋ `SoundDecoder.cpp` 122 行。**公開している関数は変えていない**ので、呼ぶ側はそのまま。
- `SoundManager::Instance()`：`HTT_ASSERT_MSG(instance_, ...)` と `instance_->` の組（約 40 か所）をまとめた。
- `SoundManager::FindVoice(playHandle)`：「ハンドルが 0 か → `voices_` から探す → null か」の十数行（14 か所）をまとめた。見つからないときの警告は1か所になった。
- `SoundManager::DestroyAllVoices()`：StopAll と ReleaseInternal の同じ処理をまとめた。
- `SoundConfig` 版の関数は、値を渡す版を呼ぶだけにした（9組）。
- `ApplyFilter`・`MakeReverbParameters`（無名の名前空間）：ローパスとハイパスの同じ形、リバーブの 22 行の設定を外へ出した。
- **`SoundDecoder`（新しいファイル）**：音声ファイルを PCM に読む処理を分けた。Media Foundation はこのファイルの中だけで使う。`SoundManager::SoundData` は `SoundDecoder::DecodedSound` になった（外からは使われていなかった）。

**ログだけ変わった所**
- 見つからないときのログは、`Warning` と `Log` が混ざっていたのを `Warning` にそろえた（呼ぶ側は `IsPlaying` で確かめてから呼ぶので、ふつうは出ない）。
- 関数の入口で出していた「こう呼ばれた」のログはやめて、終わったときのログだけにした。

**動きが少しだけ良くなった所**
- `SetReverb`・`FadeIn` は voice が null かを見ていなかったが、`FindVoice` を通るので落ちなくなった（ふつうは null にならない）。

**確かめたこと**：写しのゲームを Development・Release でビルドして起動。リファクタリング前のエンジンと Scene ビューまで全部の画素が同じ（タイトルバーの色だけ、アクティブかどうかで変わる）。Release はタイトルで音を読み込んで鳴らし、ログの error・warning 0、正常終了。**耳で聞いての確認と、Play してからの音は未確認**。

**読んでいて見つけた不具合（直していない。動きが変わるので別に直す）**
- **ローパス・ハイパスが効いていないはず**：`SetFilterParameters` は、voice を `XAUDIO2_VOICE_USEFILTER` 付きで作らないと失敗する。今は `CreateSourceVoice` の flags が 0 で、戻り値も見ていない。エンジンでもゲームでも使っていないので、今は誰も困っていない。
- **`FadeOut` で目標を 0 にすると、voice が残り続ける**：フェードの終わりで `Stop` するだけで破棄しない。バッファは積まれたままなので `CleanupSourceVoices` でも消えず、`IsPlaying` は true のまま。AL4_01 は「目標を 0 にしない」と書いて避けている。

### H：ProjectWindow をファイルで分ける（2026-10-30）
- 一番長い関数でも 68 行で、くり返しも少なかったので、**中身は変えずにファイルを分けた**（907 行に3つの仕事が入っていた）。
- `Editor/Assets/ProjectFiles.h/.cpp`（新規・163 行）：ファイルの道具（種類の判定・空いている名前・ごみ箱・エクスプローラー）。状態を持たない関数だけ。
- `Editor/Windows/ProjectWindow.cpp`（377 行）：読む・描く側。
- `Editor/Windows/ProjectWindowActions.cpp`（新規・405 行）：操作の側（クリック・メニュー・開く・作る・名前の変更・ごみ箱）。
- `ProjectWindow` は1つのクラスのまま。`.cpp` を2つに分けただけなので、呼ぶ側は変わらない。
- 関数は行番号で切り出して動かした。変えたのは `ProjectFiles::` を付けた所と、`kSourceFolder`・`kRootFolders` を `IsInsideSource`・`GetRootFolders` にした所だけ。

### I：DirectXCommon の初期化とリソースの生成を分ける（2026-10-30）
- `InitInternal` を無くした。112 行だった中身は、`Initialize` が順に呼ぶ関数7つ（`CreateFactory`・`SelectAdapter`・`CreateDevice`・`CreateCommandObjects`・`CreateFence`・`CreateDxc`・`CreateDescriptorObjects`）に分けた。**順番は変えていない**。
- `Graphics/GPU/GpuResources.h/.cpp`（新規）：バッファ・テクスチャ・ディスクリプタヒープの生成を移した（呼ぶ側 19 ファイル・32 か所は `GpuResources::` に）。デバイスしか使わず、状態を持たない関数だけ。`DirectXCommon.cpp` は 526 → 386 行。
- **`DirectXCommon` に残した物**：コマンドキュー・リスト・フェンス・`WaitForGPU`・バリア・SRV の番号の窓口。GPU を待たない作り（FrameLoop.md）にするときに変わるのは、主にここと `DescriptorAllocator`。リソースの生成は変わらないので、外に出した。
- `CheckCreated(hr, message)`：「失敗なら Error Code のログを出して assert」の4行（9 回）をまとめた。`DirectXCommon.cpp` の中だけの関数。
  - assert の出す行番号は `CheckCreated` の中になる。メッセージがそれぞれ違うので、どこで失敗したかは分かる。
- `kFeatureLevels`：別々の配列だった機能レベルと名前を、1つの表にして range-based for で回す。
- `Error Code` のログは、ほかのファイルに 13 か所（`RenderWindow` に4か所など）、`DirectXCommon.cpp` の生成の関数に3か所残っている。今回は `InitInternal` の分だけ直した。

### J：ImGuiManager::Begin を分ける（2026-10-30）
- 142 行 → 36 行。`DrawDockSpace`（画面全体のウィンドウ）・`DrawMenuBar`・メニューごとの関数に分けた。呼ぶ順番は変えていない。
- File・Edit・View のメニューは `ImGuiManager` の中身を使わないので、無名の名前空間の関数にした。Setting のメニューは `SaveStyle`・`LoadStyle` を呼ぶので private の関数。
- コードの行は、関数の呼び出しと宣言のほかは同じ（並べ替えて比べて確かめた）。Release は `USE_IMGUI` が無いので、この部分はビルドされない。

### K：ModelManager::LoadObjFile・InspectorWindow::DrawAddComponent を分ける（2026-10-30）
- `LoadObjFile`：125 行 → 約 30 行。マテリアル1つを読む `ParseMaterial`、メッシュ1つを読む `ParseMesh` を無名の名前空間に分けた。
- `DrawAddComponent`：115 行 → 約 20 行。場所の計算 `PlaceAddComponentPopup`、中身 `DrawAddComponentPopup`、左右の列 `DrawCategoryColumn`・`DrawComponentColumn` に分けた。
- `EndPopup` は `DrawAddComponent` の1か所にまとめた（途中の return で閉じ忘れない）。
- コードの行は、関数の呼び出しと宣言のほかは同じ（並べ替えて比べて確かめた）。
- `LoadObjFile` は名前に Obj とあるが、glTF も読む。名前は今回変えていない。

### L：System の呼び出しと統計を Engine・WindowManager から分ける（2026-10-30）
- `Systems/EngineSystems.h/.cpp`（新規）：全 System の初期化・終了・更新・描画の順番表。状態を持たないので namespace。System を足すときに直すのは、このファイルだけになった。
- `Engine::Initialize`・`Finalize`：System の呼び出しを `EngineSystems::Initialize`・`ReleaseScripts`・`Release` に移した。
- `WindowManager::UpdateAll`・`DrawAll`：`EngineSystems::FlushPending`・`Update`・`Draw` を呼ぶだけにした。`isGameRunning` とカメラ探しは `windows_` を見るので `WindowManager` に残した。同じカメラ探しが2か所あったので `FindGameScene` にまとめた。
- `Editor/FrameStats.h/.cpp`（新規）：`Engine::EndFrame` の統計（GPU の時間・FPS・メモリ・Draw Call など）を移した。Update・Render の時間は `Engine` の時刻を使うので `EndFrame` に残した。
- 呼び出しの順番は変えていない。`ISystem` の基底クラスや自動登録は作らない（`Update` の引数が System ごとに違い、順番を上から読める方が安全なため）。

### M：残りの不具合を直す（2026-10-30）
- `DIRECTINPUT_VERSION`：`#define` が5か所にあり、`dinput.h` の後で定義する所もあった（C4005）。`HTTEngine.props` の1か所にまとめた。
- ローパス・ハイパス：`XAUDIO2_VOICE_USEFILTER` が無く、`SetFilterParameters` が `0x88960001` で失敗していた。フラグを足して、失敗をログに出すようにした。
- ハイパス：`1.0f - cutoff` で逆になっていた（実測で Frequency 0.0 が全部通り、1.0 が無音）。反転をやめた。
- `FadeOut`：目標 0 のとき `Stop` だけで、`voices_` に残り続けていた。破棄するようにした。
- `TextureManager`・`ModelManager`・`SoundManager`・`InputManager` など：`Engine` が初期化して `WindowManager::Finalize` が片付けていた。`Engine::Finalize` に移して、ウィンドウを消した後に片付けるようにした。
- assimp の lib：`.gitignore` で git に無く、clone するとゲームのリンクが失敗した。`externals/assimp` をやめて、vcpkg で用意するようにした（Docs/Tasks/Vcpkg.md）。

## 解説：あまり使っていない STL

**`std::to_array`**
- `std::array` は要素の数を書く必要がある。`std::to_array<型>({...})` は、**数を数えずに** `std::array` を作れる。
- `std::array<std::string_view, 10> x = {...}` と違って、項目を足しても数を直さなくてよい。

```cpp
constexpr auto kTopLevelOrder = std::to_array<std::string_view>({"Core", "Camera"}); // std::array<std::string_view, 2>
```

**`std::array` の中括弧が2重になる所**
- `std::array` の中身は「C配列を1つ持つ構造体」なので、要素が `Vector3` のような構造体のときは `{{ ... }}` と2重に書く。
- 1重だと「最初の `{` が中の C配列に対応する」ことになり、`{1, 0, 0}` の組が多すぎるとエラーになる。

```cpp
std::array<Vector3, 2> a = {{ {1, 0, 0}, {0, 1, 0} }};
```

**`const auto& [from, to] : kBoxEdges`（構造化束縛）**
- `std::pair` の `first`・`second` に名前を付けて受け取る書き方。`edge.first` と書くより、何の番号かが読んで分かる。

**`std::from_chars`**
- 文字列から数を読む。`sscanf` と違って、書式の文字列が要らず、言語の設定（小数点が `,` の国など）の影響を受けない。
- 返り値の `ptr` が「読み終わった場所」、`ec` が「読めたかどうか」。読めなかったときは、出力先の変数を書き換えない。

```cpp
float value = 0.0f;
const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
if (error != std::errc{}) { /* 読めなかった */ }
```

**`std::optional`**
- 「値があるか、無いか」を持つ型。`SoundDecoder::Decode` は、読めたら `DecodedSound` を返し、読めなければ `std::nullopt`（無い）を返す。
- 「失敗のときは空の物を返す」と違って、`if (!decoded)` と書かないと中身を使いにくいので、確かめ忘れが減る。中身は `decoded->pcm` のように読む。

**指示付き初期化（`{.startVolume = 0.0f, .timer = 0.0f}`）**
- 構造体のメンバを名前で指定して作る書き方（C++20）。並びを覚えていなくても、どの値がどのメンバか読んで分かる。宣言と同じ順番で書く。

**`std::ranges::find(fields, key, &StyleField::key)`（射影）**
- 3つ目の引数は「比べる前に各要素に当てる物」。ここでは `StyleField` の `key` を取り出してから `key` と比べる。自分でラムダを書かなくてよい。

---

## 直さなかった物

| 物 | 理由 |
|---|---|
| 添字の for（68 か所） | 見た限り、ほとんどが添字の要る書き方（隣の要素・2つの配列を同じ番号で・回している途中で足す幅優先の探索）。幅優先の所を range-based for にすると、足したときに中の場所が変わって壊れる |
| `const char*` の引数（112 か所） | ほとんどが ImGui に渡す名前。ImGui は終わりに `\0` が要るので、`std::string_view` にすると、渡すたびに `std::string` を作り直すことになる。直すなら別に相談する |
| GPU に渡す構造体の中の配列（ShaderConstants の padding・BloomPass の定数 など） | HLSL 側と並びを見比べて読む物。`std::array` にしても並びは同じだが、得る物が少ない |
| `Matrix4x4::m[4][4]` | ゲームからも `m[i][j]` で大量に使う型の中身。ImGuizmo にも `&m[0][0]` で渡している |
| ProjectWindow の if / else if | enum だけではなく `isFolder` やファイルの種類と混ざった条件なので、switch にならない |

---

## 残っている物

- `#define DIRECTINPUT_VERSION 0x0800` が5か所にあり、warning C4005（再定義）が2件出ている。`NOMINMAX` と同じ直し方（props に1回）ができる。今回のパートには入れなかった。
- `(std::max)` とかっこで包んだ書き方が 114 か所ある。`NOMINMAX` を props にまとめたので、かっこは要らなくなった。動きは同じなので、触らずに残した。

---

## 6. ゲーム（AL4_01）への影響

- **今は何も起きない**。AL4_01 のサブモジュールは `58dd708` に止まっている。
- サブモジュールを進めるときに、ゲーム側で直す所は **include のパスだけ**（23 ファイル・36 か所。[FolderSplit.md](FolderSplit.md) の6章）。`NOMINMAX` はゲームに書いていなかった。
- ゲームのコードは `IsIsCollidingSphereSphereAABB`・`MathUtility` の using・`kPi` を使っていないことを確かめた（`AL4_01/project/Source` を検索）。

---

## 7. 動作確認（2026-10-30）

AL4_01 を一時フォルダに写し、エンジンを cd7cf31 にして確かめた。AL4_01 本体は触っていない。

| 確かめたこと | 結果 |
|---|---|
| ゲームのビルド（Development・Release） | include を直せば通る |
| Development の起動（エディタ・Level13） | ログの error・warning 0、正常終了 |
| リファクタリング前（58dd708＋BloomUp の1行）との比べ | 同じシーンの画面を2回ずつ撮って比べ、**タイトルバーの下は全部の画素が同じ** |
| Release の起動（タイトル画面） | 霧・DOF・粒・文字の光が出る。ログの error・warning 0、正常終了 |

**確かめていない物**
- キー入力・Play してからの動き（遊んでいる間の絵・Wind Zone の線・ゲームパッド・音）
- スタイルの ini の Save / Load を、エディタのボタンから行う（抜き出したコードでは確かめた）

**見つかった物：`BloomUp.PS.shader` の1行**
- エンジン本体の `BloomUp.PS.shader` は `gSampler` のままで、**起動すると Bloom の PSO の作成で assert して落ちる**。
- 58dd708 で Bloom のシェーダーを `gSamplerClamp` にそろえたとき、このファイルだけコミットから漏れていた。AL4_01 のサブモジュールの中では直してあるが、未コミット。
- `Sample(gSampler, ...)` を `Sample(gSamplerClamp, ...)` にすれば起動する（写しで確かめた）。
