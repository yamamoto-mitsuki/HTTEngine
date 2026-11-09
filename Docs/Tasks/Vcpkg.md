参考：
- マニフェスト（vcpkg.json）：https://learn.microsoft.com/vcpkg/concepts/manifest-mode
- vcpkg.json の書き方：https://learn.microsoft.com/vcpkg/reference/vcpkg-json
- vcpkg-configuration.json の書き方：https://learn.microsoft.com/vcpkg/reference/vcpkg-configuration-json
- トリプレット（x64-windows-static）：https://learn.microsoft.com/vcpkg/concepts/triplets
- バイナリキャッシュ：https://learn.microsoft.com/vcpkg/concepts/binary-caching
- GitHub Actions のキャッシュ：https://github.com/actions/cache

# assimp を vcpkg で用意する

## 0章：何をするか

今は `project/externals/assimp` に、ビルド済みの lib とヘッダーを手で置いている。
lib は `.gitignore` で git に無いので、clone するとゲームのリンクに失敗する。

これを vcpkg に替える。`vcpkg.json` に「assimp を使う」と書くと、ビルドのときに自動で用意される。
lib は git にも LFS にも入れないので、リポジトリは軽いままになる。

| | 今 | vcpkg |
|---|---|---|
| lib の置き場所 | `externals/assimp`（手でコピー） | `externals/vcpkg_installed`（自動） |
| ヘッダー | `externals/assimp/include` | `externals/vcpkg_installed` の中 |
| 何を使うかの記録 | 無い | `externals/vcpkg.json` |
| assimp の版 | 5.3.1 | 6.0.x |
| 最初のビルド | 手でコピーが必要 | 自動。assimp のビルドで約 6 分 |

### 先に試した結果（2026-10-31）

一時フォルダの写しで試した。

| 確かめたこと | 結果 |
|---|---|
| assimp のビルド | 約 6 分（依存を入れて 13 個）。2 回目からはキャッシュで 1.5 秒 |
| 7 モデルの読み比べ（5.3.1 と 6.0） | 6 つは頂点・UV・骨・重み・アニメーションが同じ。`walk.fbx` は 5.3.1 で読めず、6.0 で読めるようになった |
| ゲームのビルド | 古い assimp を外して Debug・Development・Release が通った。リンクの警告 0 |
| ゲームの起動 | Development・Release とも正常に終了。ログの error・warning 0。Development の画面は基準と全画素一致 |
| エンジン単体の sln | 空の状態から Debug・Release が通った（`generated` に置いていたとき） |
| `externals` に置いた形 | ゲームの Debug・Development・Release が通った。Development の画面は基準と全画素一致 |
| 長いパス | 本物より長い 90 文字のパスで、キャッシュ無しで最初からビルドできた（3.9 分） |
| 大きさ | `externals/vcpkg_installed` が約 1.1GB（git には入らない） |

まだ確かめていないこと：**GitHub Actions**（パートE）と、**新しく clone した PC**（メンバーの PC）。どちらも push した後でないと試せない。

### 順番

パートA〜Eを全部やってからビルドする。途中でビルドすると失敗する。

**パートFで確認が終わるまで push しない。** パートAで、まだ push していない LFS のコミットを取り消すので、先に push すると LFS の分が GitHub に上がってしまう。


## パートA：LFS のコミットを取り消す

エンジンのリポジトリ（`C:\Users\K025G\source\repos\HTTEngine`）で作業する。

まず今の状態を見る。`Docs/Tasks/Vcpkg.md`（この md）のほかに変更が無いことを確かめる。

```bash
git status --short
```

`develop` の新しい順に、次の 3 つになっていることを確かめる。

```bash
git log --oneline -3
```

```
17d042d assimp の pdb が無いときの LNK4099 を止める   ← 取り消す
39ea751 assimp の lib を Git LFS に入れる             ← 取り消す
0817760 音のフィルタ・フェードアウト・終了の順番などの不具合を直す   ← ここに戻す
```

上の 2 つを取り消す。

```bash
git reset --hard 0817760
```

これで次の物が元に戻る。

- `.gitattributes` の LFS の 2 行
- `.gitignore` の assimp の行
- `README.md` の「取ってくるとき」
- `HTTEngine.props` の `/ignore:4099`
- `Refactor.md` の assimp の行（パートEで書き直す）

Git の管理に入れた `assimp-vc143-mtd.lib` と `assimp-vc143-mt.lib` は、このとき消える。パートDで全部消すので、問題ない。

この md はまだ git に入っていないので、消えない。


## パートB：vcpkg のファイルを足す

外部の物は `externals` にまとめる決まりなので、`project/externals` に 2 つのファイルを新しく作る。自分で足すファイルなので、`externals` に置いてよい。

**`project/externals/vcpkg.json`**

使うライブラリの一覧。

```json
{
  "$comment": "このエンジンが使う外部ライブラリの一覧。ビルドのときに vcpkg が用意する",
  "dependencies": [
    "assimp"
  ]
}
```

**`project/externals/vcpkg-configuration.json`**

どの時点の vcpkg のライブラリ一覧を使うかを決める。この行があるので、誰がいつビルドしても同じ版になる。

```json
{
  "$comment": "baseline は microsoft/vcpkg のコミット（2026-07-27 時点）。誰がいつビルドしても同じ版になるように固定している",
  "default-registry": {
    "kind": "git",
    "baseline": "1460b31b08c42cc2e9ac2c79f45ec8707e2675e2",
    "repository": "https://github.com/microsoft/vcpkg"
  }
}
```

JSON には `//` のコメントが書けない（vcpkg もエラーになる）。vcpkg では、`$` で始まる項目は読み飛ばされるので、`$comment` を説明に使う。

**`.gitignore`**

一番下に足す。`externals/vcpkg_installed` は各 PC で作る物なので、git に入れない。

```gitignore

# vcpkg が各 PC で用意する外部ライブラリ（externals/vcpkg.json を見て作る）
project/externals/vcpkg_installed/
```


## パートC：HTTEngine.props を直す

`project/HTTEngine.props` を 5 か所直す。

### C-1：場所

`<!-- ===== 場所 ===== -->` の `<HTTEngineRoot>` の次の行に足す。

```xml
		<HTTEngineRoot>$(MSBuildThisFileDirectory)</HTTEngineRoot>
		<!-- vcpkg が外部ライブラリを入れる先（externals の下。.gitignore で git には入らない。末尾に \ を付けない）-->
		<VcpkgInstallRoot>$(HTTEngineRoot)externals\vcpkg_installed</VcpkgInstallRoot>
		<!-- x64-windows-static：64 ビット・lib に全部入れる形・/MT（エンジンの RuntimeLibrary と同じ）-->
		<VcpkgPackageDir>$(VcpkgInstallRoot)\x64-windows-static\</VcpkgPackageDir>
```

### C-2：include

`AdditionalIncludeDirectories` の行の、最後の 1 つを替える。

```xml
<!-- 前 -->
$(HTTEngineRoot);$(HTTEngineRoot)HTTEngine;$(HTTEngineSubDirs);$(HTTEngineRoot)externals;$(HTTEngineRoot)externals\imgui;$(HTTEngineRoot)externals\assimp\include;
<!-- 後 -->
$(HTTEngineRoot);$(HTTEngineRoot)HTTEngine;$(HTTEngineSubDirs);$(HTTEngineRoot)externals;$(HTTEngineRoot)externals\imgui;$(VcpkgPackageDir)include;
```

### C-3：Debug のリンク

`Condition="'$(Configuration)'=='Debug'"` の `<Link>` の中身を替える。

```xml
		<Link>
			<AdditionalDependencies>assimp-vc145-mtd.lib;kubazip.lib;minizipsd.lib;poly2tri.lib;polyclipping.lib;pugixml.lib;zsd.lib;%(AdditionalDependencies)</AdditionalDependencies>
			<AdditionalLibraryDirectories>$(VcpkgPackageDir)debug\lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
		</Link>
```

### C-4：Debug 以外のリンク

`Condition= "'$(Configuration)'!='Debug'"` の `<Link>` の中身を替える。

```xml
		<Link>
			<AdditionalDependencies>assimp-vc145-mt.lib;kubazip.lib;minizips.lib;poly2tri.lib;polyclipping.lib;pugixml.lib;zs.lib;%(AdditionalDependencies)</AdditionalDependencies>
			<AdditionalLibraryDirectories>$(VcpkgPackageDir)lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
		</Link>
```

Debug の `mtd`・`minizipsd`・`zsd` と、Release の `mt`・`minizips`・`zs` は、**名前が違う**ので写し間違えないように。

### C-5：vcpkg を動かす

ファイルの一番下、`</Project>` のすぐ上に足す。

```xml
	<!-- ===== vcpkg ===== -->
	<!-- vcpkg.json に書いたライブラリを、コンパイルの前に入れる。Inputs・Outputs で、vcpkg.json を変えたときと初回だけ動く -->
	<Target Name="InstallVcpkgPackages" BeforeTargets="ClCompile" Inputs="$(HTTEngineRoot)externals\vcpkg.json;$(HTTEngineRoot)externals\vcpkg-configuration.json" Outputs="$(VcpkgInstallRoot)\vcpkg\manifest-info.json">
		<Error Condition="!Exists('$(VCInstallDir)vcpkg\vcpkg.exe')" Text="Visual Studio に vcpkg がありません。Visual Studio Installer で「vcpkg パッケージ マネージャー」を入れてください。" />
		<Exec Command="&quot;$(VCInstallDir)vcpkg\vcpkg.exe&quot; install --triplet x64-windows-static --clean-after-build --x-manifest-root=&quot;$(HTTEngineRoot)externals&quot; --x-install-root=&quot;$(VcpkgInstallRoot)&quot;" />
	</Target>
</Project>
```

`</Project>` はファイルの途中（`ProjectReference` の中）にもある。足すのは**一番下**の `</Project>` の上。


## パートD：古い assimp を消す

git に入っているヘッダーを消す。

```bash
git rm -r --quiet project/externals/assimp
```

`project/externals/assimp` のフォルダが残っていたら、エクスプローラーで消す（`.gitignore` で git に入っていない lib が残っている）。

`externals` の中の物を消すが、これはライブラリのソースを書き換えるのではなく、手で置いていた物をやめる作業。assimp は `externals/vcpkg_installed` に入り直す。


## パートE：CI と md を直す

### E-1：CI のキャッシュ

CI は毎回新しい PC で動くので、そのままだと毎回 assimp のビルドに数分かかる。キャッシュで使い回す。

`.github/workflows/DebugBuild.yml` と `.github/workflows/ReleaseBuild.yml` の **2 つとも**、`Checkout` の次に足す。

```yaml
      # --- リポジトリを取ってくる ---
      - name: Checkout
        uses: actions/checkout@v6

      # --- vcpkg の lib を前回から使い回す（無いと、毎回 assimp のビルドに数分かかる）---
      - name: Cache vcpkg
        uses: actions/cache@v4
        with:
          path: ~\AppData\Local\vcpkg\archives
          key: vcpkg-${{ runner.os }}-${{ hashFiles('project/externals/vcpkg.json', 'project/externals/vcpkg-configuration.json') }}
```

ここは手元で試せていない。push した後に Actions の結果を見て、失敗したら 7 章を見る。

### E-2：README.md

一番下に足す。

````markdown
## 取ってくるとき
外部ライブラリは `project/externals` にまとめている。assimp は vcpkg で用意する（`project/externals/vcpkg.json`）。clone してビルドするだけでよい。

- Visual Studio に「vcpkg パッケージ マネージャー」が入っている必要がある（Visual Studio Installer の「C++ によるデスクトップ開発」の中）
- 最初のビルドは、assimp のビルドで数分かかる。ネットにつながっている必要がある
- 2 回目からは `%LOCALAPPDATA%\vcpkg\archives` のキャッシュを使うので、すぐ終わる
````

### E-3：Docs/Tasks/Refactor.md

`### M：残りの不具合を直す（2026-10-30）` の一番下の行の次に足す。

```markdown
- assimp の lib：`.gitignore` で git に無く、clone するとゲームのリンクが失敗した。`externals/assimp` をやめて、vcpkg で用意するようにした（Docs/Tasks/Vcpkg.md）。
```

### E-4：Docs/Tasks/README.md

話題の表に足す。

```markdown
| [Vcpkg.md](Vcpkg.md) | assimp を vcpkg で用意する手順と、試した結果 |
```


## パートF：確かめる

### F-1：エンジン

`project/HTTEngine.sln` を開いて、`Debug`・`Release` をビルドする。

この PC は試したときのキャッシュがあるので、vcpkg の部分は数秒で終わる。出力に次の行が出れば、vcpkg が動いている。

```
All requested installations completed successfully in: ...
```

`C4865`（`dxgi.h` など）の警告は、DirectXTex を作り直したときに出る。vcpkg とは関係ない。

### F-2：コミットして push

確認できたらコミットする。

```bash
git add -A project Docs README.md .github .gitignore
```

`git status` で `project/externals/assimp` の削除、`project/externals/vcpkg.json`、`project/externals/vcpkg-configuration.json` が入っていて、`project/externals/vcpkg_installed` が**入っていない**ことを見てからコミットする。

```bash
git status --short
```

```bash
git commit -m "assimp を vcpkg で用意するようにする"
```

```bash
git push origin develop
```

push したら、GitHub の Actions で `DebugBuild`・`ReleaseBuild` が通ることを確かめる。


## パートG：AL4_01 を最新にする

手元の AL4_01 のサブモジュールは、パートAで取り消した `17d042d` を指している。push した後の `develop` に合わせ直す。

`C:\Users\K025G\source\repos\AL4_01\HTTEngine` で作業する。

```bash
git fetch origin
```

```bash
git reset --hard origin/develop
```

`project/externals/assimp` のフォルダが残っていたら、エクスプローラーで消す。

`C:\Users\K025G\source\repos\AL4_01` に戻って、VS で `project/AL4_01.slnx` を開き、`Debug`・`Development`・`Release` をビルドして起動する。

確認できたらコミットする。`project/main.cpp` の変更（タイトルの文字列）は、**含めるかどうかを自分で決める**。下のコマンドでは含めない。

```bash
git add HTTEngine project/Source
```

```bash
git commit -m "エンジンを最新にして include を新しいパスに直す"
```

```bash
git push
```


## 確認の一覧

- [ ] エンジンの sln で Debug・Release がビルドできる
- [ ] GitHub Actions の DebugBuild・ReleaseBuild が通る
- [ ] AL4_01 で Debug・Development・Release がビルドできる
- [ ] AL4_01 が起動して、画面が前と同じ
- [ ] Model Renderer でスキンのモデル（`walk.gltf` など）が表示できて、アニメーションする
- [ ] 別のフォルダに clone して、ゲームをビルドできる（`git clone --recurse-submodules`）


## 1章：なぜ vcpkg にするのか

C++ には、ほかの言語の npm や pip にあたる標準の仕組みが無い。vcpkg はその代わりになるツールで、Microsoft が作っていて、Visual Studio に付いてくる。

`vcpkg.json` は MSBuild でも CMake でも読める。将来 CMake に移しても、このファイルはそのまま使える。

CMake はビルドの手順を書く仕組みで、ライブラリを取ってくる仕組みではない。CMake にしても、assimp をどう用意するかは別に決める必要がある。

### 自分の成果と外部の物の区別

外部の物は `externals` にまとめる。vcpkg にしても、これは変わらない。

- GitHub で見ると、assimp のファイルは出ない。代わりに `externals/vcpkg.json` に「assimp を使っている」と書いてある
- 手元では `externals/vcpkg_installed` に入るので、自分のコードと混ざらない
- `HTTEngine` の下には、外部の物を置かない


## 2章：トリプレット（x64-windows-static）

vcpkg では、「どの形でビルドするか」を**トリプレット**と呼ぶ。

| トリプレット | CPU | ライブラリ | ランタイム |
|---|---|---|---|
| `x64-windows` | 64 ビット | dll | /MD |
| `x64-windows-static` | 64 ビット | lib | /MT |

エンジンは `HTTEngine.props` で `MultiThreaded`（/MT）にしているので、`x64-windows-static` を使う。

ここが合わないと、リンクで `LNK2038`（`RuntimeLibrary` が違う）になる。

`externals/vcpkg_installed` に `x64-windows` のフォルダもできる。これは vcpkg がビルドのときに使う道具用で、リンクには使わない。


## 3章：baseline

`vcpkg-configuration.json` の `baseline` は、vcpkg のライブラリ一覧の**コミット**（`microsoft/vcpkg` リポジトリの、git のコミットの番号）。

これを固定すると、全員が同じ版の assimp を使う。固定しないと、ビルドした日によって版が変わることがある。

assimp を新しくしたいときは、`baseline` を新しいコミットに替える。そのときは、モデルの読み込みを確かめ直す。


## 4章：Target の Inputs・Outputs

MSBuild の `Target` は、`Inputs` と `Outputs` を書くと、**`Outputs` のファイルが `Inputs` より新しければ飛ばす**。

```xml
Inputs="$(HTTEngineRoot)externals\vcpkg.json;$(HTTEngineRoot)externals\vcpkg-configuration.json"
Outputs="$(VcpkgInstallRoot)\vcpkg\manifest-info.json"
```

`manifest-info.json` は vcpkg がインストールのたびに書くファイル。これで、毎回のビルドで vcpkg を動かさずに済む。

動く順番は `BeforeTargets="ClCompile"` で決めている。コンパイルより前に、ヘッダーと lib がそろう。

`--x-manifest-root` は `vcpkg.json` のあるフォルダ、`--x-install-root` は入れる先。どちらも末尾に `\` を付けない。`"C:\foo\"` と書くと、`\"` が「文字としての `"`」と読まれて、コマンドが壊れるため。

`--clean-after-build` は、ビルドの作業用ファイルを消す。これを付けないと 2GB ほど残る。


## 5章：lib が 7 個ある理由

assimp は、ほかのライブラリを使っている。今までの lib は 1 つにまとめてあったが、vcpkg では別々になる。

| lib | 役割 |
|---|---|
| `assimp-vc145-mt` | assimp 本体 |
| `kubazip`・`minizips`・`zs` | zip の読み書き |
| `poly2tri`・`polyclipping` | 多角形の三角形分割 |
| `pugixml` | XML の読み込み |

`vc145` は Visual Studio 2026（v145）でビルドしたという意味。VS を新しくしても vcpkg が作り直すが、名前が変わったときは `HTTEngine.props` も直す。


## 6章：ライブラリを足すとき

1. `project/externals/vcpkg.json` の `dependencies` に足す
2. `HTTEngine.props` の C-3・C-4 に lib の名前を足す

ヘッダーだけのライブラリなら、2 は要らない。


## 7章：うまくいかないとき

| 症状 | 原因 | 直し方 |
|---|---|---|
| `Visual Studio に vcpkg がありません` | VS の機能が入っていない | Visual Studio Installer で「vcpkg パッケージ マネージャー」を入れる |
| `LNK1104: assimp-vc145-mt.lib を開けません` | vcpkg が動いていない | `externals/vcpkg_installed` を消してビルドし直す |
| `LNK2038` | ランタイムが合っていない | 2章を見る |
| `LNK2019`（zip や xml の関数） | C-3・C-4 の lib が足りない | 写し間違いを見る。Debug と Release で名前が違う |
| CI で `vcpkg.exe` が無いと言われる | CI の VS に vcpkg が入っていない | `$(VCInstallDir)` の代わりに `VCPKG_INSTALLATION_ROOT` の環境変数を使う形に直す |
| CI が遅い | キャッシュが効いていない | E-1 の `path` を見る。最初の 1 回は必ず遅い |
| エンジンのビルドで vcpkg が動かない | `manifest-info.json` が新しい | `externals/vcpkg_installed` を消す |
| git の変更に `vcpkg_installed` が大量に出る | `.gitignore` の足し忘れ | パートBの `.gitignore` を見る |
