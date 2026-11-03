参考：
- 新しいソリューションファイル（`.slnx`）：https://devblogs.microsoft.com/visualstudio/new-simpler-solution-file-format/
- `.slnx` の書き方（`Build`・`DefaultStartup` など）：https://github.com/microsoft/vs-solutionpersistence
- ビルド イベント（ビルド後のイベント）：https://learn.microsoft.com/ja-jp/cpp/build/specifying-build-events
- `xcopy`（`/D`・`/S`・`/I`・`/Y`）：https://learn.microsoft.com/ja-jp/windows-server/administration/windows-commands/xcopy
- `robocopy`（フォルダごと写す）：https://learn.microsoft.com/ja-jp/windows-server/administration/windows-commands/robocopy
- `Directory.Build.props`（フォルダの下の全プロジェクトが読む）：https://learn.microsoft.com/ja-jp/visualstudio/msbuild/customize-by-directory
- `.gitignore`：https://git-scm.com/docs/gitignore
- git のサブモジュール：https://git-scm.com/book/ja/v2/Git-のさまざまなツール-サブモジュール

# エンジンの Sandbox（起動確認・機能テスト用の exe）

エンジン（`MyEngine_v1.vcxproj`）はスタティックライブラリなので、それだけでは起動できない。今までは個人のゲーム AL4_01 で起動して確かめていたが、エンジンのリポジトリの中に **起動確認・テスト用の exe（Sandbox）** を持つ。

- **場所**：`project/Sandbox/`。エンジンと同じリポジトリの中のふつうのフォルダ（サブモジュールではない）
- **中身**：`main.cpp`・`Sandbox.vcxproj`・**ふつうのシーン**（カメラ・平行光源・地面・箱だけの `SampleScene`）
- **エンジンのソリューションに足して、スタートアップにする**。F5 でエンジンをビルドして Sandbox が起動する
- **AL4_01 にあるエンジンのテスト**（Script と、テストのシーン・Timeline・UI 画面など）を Sandbox へ移す。AL4_01 は個人のゲームだけに戻る
- エンジンの機能を足したときは、**エンジンの直しとテストのシーンを同じコミットに入れられる**
- **clone → `project/MyEngine_v1.slnx` を開く → F5 で Sandbox が起動する**。手で写す・置く物は無い（0.6）。exe は git に入れない（ビルドすれば作られる）

> **2026-11-04：パートA は写経せず、本体に直接入れた**（md のコードそのまま）。**パートB はやらない**（AL4_01 にあるテストはもう確かめ終わった物なので、Sandbox にはふつうのシーンだけを置く。AL4_01 のテストのファイルはそのまま）。パートC は、チームのゲームを始めるときに使う。0章の B に関わる行（テストを移す・BossTest など）は、その前提で書いた物として読む。

## 0章 全体の形

### 0.1 流れ

```
エンジンのリポジトリ（MyEngine_v1）
├─ .gitignore                    ← Sandbox が書くファイルを外す（パートA）
├─ .github/workflows/*.yml       ← .sln → .slnx（パートA）
└─ project/
   ├─ MyEngine_v1.slnx           ← .sln をやめて .slnx に。Sandbox を足してスタートアップに（パートA）
   ├─ MyEngine.props             ← そのまま（Sandbox もゲームと同じくこれを読む）
   ├─ MyEngine_v1.vcxproj        ← そのまま
   ├─ MyEngine/・Resources/      ← エンジン
   └─ Sandbox/                   ← 新しい
      ├─ Sandbox.vcxproj(.filters)
      ├─ main.cpp
      ├─ Source/Scripts/…        ← AL4_01 から移すテストの Script（パートB）
      ├─ Resources/scenes/SampleScene.scene.json  ← ふつうのシーン
      ├─ Resources/…             ← AL4_01 から移すテストのシーン・Timeline など（パートB）
      ├─ Parameters/PostEffect/ToneMap.json  ← トーンマップを ACES に（ほかは起動すると作られる。Editor/ は git に入れない）
      └─ MyEngine/               ← ビルドのたびにエンジンの Resources・.shader を写す先（git に入れない）
```

1. **パートA**（エンジン側）：Sandbox を作り、ソリューションに足す。ふつうのシーンが開くところまで。
2. **パートB**（エンジン側とゲーム側）：AL4_01 のテストを Sandbox へ移し、AL4_01 から消す。
3. **パートC**（いつか・チームのゲームを始めるとき）：Sandbox を写してゲームのひな形にする。

### 0.2 作る物・直す物

| ファイル | 何をするか |
|---|---|
| `project/Sandbox/main.cpp`（新しい） | `Engine::Initialize(config, "SampleScene")` とループだけ（AL4_01 の `main.cpp` と同じ形） |
| `project/Sandbox/Sandbox.vcxproj`（新しい） | exe。`..\MyEngine.props` を読む。3構成。ビルドの後にエンジンの Resources・.shader を写す |
| `project/Sandbox/Sandbox.vcxproj.filters`（新しい） | ソリューション エクスプローラーのフォルダ分け |
| `project/Sandbox/Resources/scenes/SampleScene.scene.json`（新しい） | ふつうのシーン |
| `project/Sandbox/Parameters/PostEffect/ToneMap.json`（新しい） | トーンマップを ACES に（無いと空が白く飛ぶ） |
| `project/MyEngine_v1.sln` → `project/MyEngine_v1.slnx` | 構成を3つにして1対1に。Sandbox を足して `DefaultStartup` |
| `.github/workflows/DebugBuild.yml`・`ReleaseBuild.yml` | ビルドするファイルを `.slnx` に |
| `.gitignore` | Sandbox が起動中に書くファイル・エディタの `.bak` などを外す |
| `README.md` | 「`.slnx` を開いて F5 で Sandbox が起動する」を足す |
| AL4_01（パートB） | テストの Script・シーンなどを消す。`GameScene` をふつうのシーンに |

### 0.3 使い方

1. `project/MyEngine_v1.slnx` を Visual Studio で開く。
2. 構成（Debug・Development・Release）を選んで F5。エンジン → Sandbox の順にビルドされ、Sandbox が起動する。
3. エディタ（Debug・Development）は、前回最後に開いていたシーンから始まる。初めては `SampleScene`。
4. 機能を足したら、Sandbox にテストのシーン・Script を足して確かめ、エンジンの直しと一緒にコミットする。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 名前・場所 | `project/Sandbox/`。プロジェクト名も `Sandbox` | `MyEngine.props` を `..\MyEngine.props` で読める。`project/MyEngine/` の外なので、props の「MyEngine の下のフォルダを全部 include に足す」に入らない |
| 起動するフォルダ | `project/Sandbox/`（vcxproj のフォルダ。Visual Studio の既定のまま） | ゲームと同じに、`resources/…`・`Parameters/…` を起動したフォルダから読む。`.vcxproj.user` を作らなくてよい |
| エンジンの Resources・.shader | Sandbox のビルドの後に `project/Sandbox/MyEngine/` へ写す（`xcopy /D`） | エンジンはアイコン・グリッド・空などを `MyEngine/Resources/…` で読む（起動したフォルダから見た場所）。エンジンのビルドの後の写しは、エンジンのソリューションでは動かない（1.1） |
| ソリューション | `.sln` をやめて `.slnx` にする | 構成を3つにして1対1に直す必要がある（1.2）。`.slnx` は短く手で書けて、`DefaultStartup` でスタートアップを決められる。AL4_01 も `.slnx` |
| 構成 | Debug・Development・Release を、全部のプロジェクトで同じ名前の構成に | 今の `.sln` は「Debug」でエンジンの Development をビルドしていた。Sandbox の Debug と混ぜるとリンクできない（1.2） |
| ImGui の Release | ビルドしない（`.sln` と同じ） | Release は ImGui を使わない（props の参照も Release 以外だけ） |
| トーンマップ | Sandbox の `Parameters/PostEffect/ToneMap.json` で ACES（Mode 2）・露出 1。エンジンの既定（None）は変えない | 既定の None だと、空（HDR）の明るい所が白く飛んで、地面との境も見えにくい（D の表）。AL4_01 も同じ値を `Parameters/` に置いていた。既定はエンジンを使う全部のゲームの見た目を変えるので触らない |
| include の書き方 | `#include <MyEngine/Engine.h>`（`<>`） | Sandbox はエンジンを「使う側」。ゲームと同じ書き方にすると、パートC でそのまま写せる |
| vcxproj の共通の設定 | 条件の無い `ItemDefinitionGroup` に1回だけ書く | AL4_01 の vcxproj は同じ設定（ビルドの後のコピーなど）を3構成に3回書いている。1か所なら直し忘れが無い（1.4） |
| テストを移すか | 移す。AL4_01 は個人のゲームだけに戻す | エンジンの直しとテストを同じコミットに入れられる。AL4_01 のテストのファイルは全部 git に入っていない（なくすと戻せない）ので、エンジンの git に入れて守る |
| BossTest | Sandbox へ移す。シーンは `BossTest` という名前に | 攻撃表（Attack Table）のテスト。2本目の課題でボスを作るときは、ゲームの Script として作り直す |
| `Resources/Model/Player/` | AL4_01 に残す | 次のゲームのプレイヤーのモデルで、テストのシーンからは使っていない |
| `.bak` | 持っていかない。エンジンの `.gitignore` で外す | エディタが保存のたびに書く「1つ前の中身」。人ごとに違う |
| ゲームのひな形 | Sandbox を写して使える形にする（パートC）。今はやらない | `main.cpp`・vcxproj・ふつうのシーンはゲームと同じ形。違うのは props の場所と、ビルドの後の写しだけ |

### 0.5 ゲームがエンジンをサブモジュールで読むとき

AL4_01 のように、ゲームのリポジトリがエンジンをサブモジュールで読むと、`MyEngine_v1/project/Sandbox/` もいっしょに入ってくる。邪魔にならないかを調べた。

| 心配 | どうなるか |
|---|---|
| Sandbox がビルドされる | されない。ゲームの `.slnx` に並ぶのはエンジン・DirectXTex・ImGui の vcxproj だけ。Sandbox の vcxproj はだれも参照しない |
| include の場所が増える | 増えない。props が include に足すのは `project/MyEngine/` の下だけ。Sandbox は `project/Sandbox/` |
| Sandbox の Script がゲームに入る | 入らない。Script は Sandbox の exe のソースで、エンジンの lib には入らない。ゲームの Add Component にも出ない |
| 大きいファイル | 入れない。テストのファイルは全部で 0.3 MB くらい（Timeline・シーン・UI 画面の JSON と、小さい画像1枚）。モデルを足すときは、エンジンの `project/Resources/Model/` にある物を使う |
| `project/Sandbox/MyEngine/`（エンジンの Resources の写し） | Sandbox をビルドしたときだけできる。`.gitignore` で外すので、サブモジュールの中身にも入らない |
| `project/Directory.Build.props` | DirectXTex だけに効く（Sandbox にもゲームにも関係ない） |

### 0.6 clone してビルドするだけで動くか

「エンジンのリポジトリを新しく clone → `.slnx` を開いてビルド → Sandbox が起動する」までに、手で写す物が無いかを調べた。本当に clone し直した写しで確かめた結果は D の表。

| 要る物 | どう用意されるか |
|---|---|
| assimp の lib・ヘッダー | vcpkg が最初のビルドで作る（`externals/vcpkg.json`・`MyEngine.props` の `InstallVcpkgPackages`。Vcpkg.md で入れた物）。手でのコピーはもう要らない |
| `grid.obj`（エディタのグリッド） | git に入っている（`.gitignore` の `*.obj` の後の `!/project/Resources/**/*.obj` で外さない）。ほかのエンジンの Resources も全部 git にある |
| `cube.obj` | エンジンは使っていない（AL4_01 の物だった）。Sandbox のシーンは Primitive Renderer の箱なので、モデルのファイルは要らない |
| .shader | git に入っている（`project/MyEngine/Shader/Data`） |
| DirectXTex・ImGui・spdlog・magic_enum・nlohmann | `project/externals` に git で入っている。DirectXTex のシェーダーはビルドのときに作られる |
| エンジンの Resources・.shader の写し（`project/Sandbox/MyEngine/`） | Sandbox のビルドの後に写す（1.1） |
| `dxcompiler.dll`・`dxil.dll` | Sandbox のビルドの後に、Windows SDK から exe の隣へ写す |
| ふつうのシーン・トーンマップ | `SampleScene.scene.json`・`ToneMap.json` を git に入れる（A-4・A-5） |
| `Parameters/Editor/`・`imgui.ini` | 無くてよい（起動すると作る。初めはエディタのウィンドウが重なって出る） |

**PC に要る物**（README.md の「取ってくるとき」と同じ）：Visual Studio 2026（「C++ によるデスクトップ開発」・v145・Windows SDK・「vcpkg パッケージ マネージャー」）と、最初のビルドのときのネット（vcpkg が assimp を取ってくる）。

## 1章 解説

### 1.1 エンジンの Resources・.shader は、起動したフォルダから読む

エンジンは、自分の画像やモデルを **起動したフォルダから見た場所** で読む。

| 読む物 | 場所（コードに書いてある） | 例 |
|---|---|---|
| アイコン・グリッド・白の画像・空 | `MyEngine/Resources/…` | `TextureManager.cpp` の `"MyEngine/Resources/Textures/white.png"` |
| .shader（Release） | `MyEngine/Shader/Data`（`kShaderDataDir`） | Release 以外は、props の `MYENGINE_ROOT_DIR` からエンジンの .shader を直接読む |
| .shader から作った .hlsl | `MyEngine/Shader/Generated/`（`kShaderGeneratedDir`） | 起動のたびに書く |
| ゲームのシーン・Timeline など | `resources/…` | `resources/scenes/SampleScene.scene.json` |

ゲーム（AL4_01）では、**エンジンのビルドの後** に `$(SolutionDir)MyEngine\` へ Resources と .shader を写している（`MyEngine_v1.vcxproj` のビルド後のイベント）。AL4_01 の `.slnx` は `AL4_01/project/` にあるので、写し先は `AL4_01/project/MyEngine/`＝起動するフォルダの `MyEngine/`。

```bat
if /I "$(SolutionDir)"=="$(ProjectDir)" exit /b 0
xcopy /Y /S /I "$(ProjectDir)MyEngine\Shader\*" "$(SolutionDir)MyEngine\Shader\"
xcopy /Y /S /I "$(ProjectDir)Resources" "$(SolutionDir)MyEngine\Resources\"
```

エンジンのソリューションでは、`$(SolutionDir)`（`project/`）と `$(ProjectDir)`（`project/`）が同じなので、1行目で何もせずに終わる（自分の上に写さないため）。だから Sandbox は、**自分のビルドの後に、自分のフォルダへ写す**。

```bat
xcopy /Y /S /I /D "$(MyEngineRoot)MyEngine\Shader\Data" "$(ProjectDir)MyEngine\Shader\Data\"
xcopy /Y /S /I /D "$(MyEngineRoot)Resources" "$(ProjectDir)MyEngine\Resources\"
```

- `$(MyEngineRoot)` は `MyEngine.props` が決める「props のあるフォルダ」（`project/`）。
- `/D` は「写し先より新しいファイルだけ写す」。エンジンの Resources は 35 MB あるので、2回目からは変わった物だけになる。
- 写すのは `Shader\Data`（.shader）だけ。`Generated` は起動するたびに Sandbox の中に作られる。
- **消したファイルは写しに残る**（`/D` は足す・上書きするだけ）。エンジンの .shader を消した・名前を変えたときに Release がおかしくなったら、`project/Sandbox/MyEngine/` を消してビルドし直す。

### 1.2 今の .sln の「Debug」は、エンジンの Development をビルドしていた

今の `MyEngine_v1.sln` は、ソリューションの構成が Debug・Release の2つで、こう対応付けていた。

| ソリューションの構成 | MyEngine_v1 | DirectXTex | imgui |
|---|---|---|---|
| Debug | **Development** | **Development** | **Development** |
| Release | Release | Release | （ビルドしない） |

エンジンだけなら困らなかったが、exe（Sandbox）を足すと困る。Sandbox の Debug は `/MTd`（デバッグ用のランタイム）、エンジンの Development は `/MT` なので、混ぜるとリンクで `RuntimeLibrary の不一致` のエラーになる。Sandbox も Development にすると、今度は「Debug を選んだのに Debug で動かない」になる。

そこで、構成を3つにして **全部のプロジェクトで同じ名前の構成** に直す（AL4_01 の `.slnx` と同じ）。

> GitHub Actions の DebugBuild は、これからはエンジンも Debug でビルドする（今までは Development）。Sandbox もビルドされるので、props の参照・ライブラリの抜けでリンクできなくなったことにも気づける。

### 1.3 .slnx

`.slnx` は Visual Studio 2022（17.13 から）・2026 で使える、新しいソリューションファイル。XML で、`.sln` の GUID の表が無い。

```xml
<Solution>
  <Configurations>                 <!-- ソリューションの構成 -->
    <BuildType Name="Debug" />
    <Platform Name="x64" />
  </Configurations>
  <Folder Name="/externals/">      <!-- ソリューション エクスプローラーのフォルダ -->
    <Project Path="…" Id="…">
      <Build Solution="Release|*" Project="false" />   <!-- Release のときはビルドしない -->
    </Project>
  </Folder>
  <Project Path="Sandbox/Sandbox.vcxproj" DefaultStartup="true" />   <!-- F5 で起動する -->
</Solution>
```

- 構成の対応付けを書かなければ、**同じ名前の構成** をビルドする。
- `Id` はプロジェクトの GUID（vcxproj の `ProjectGuid` と同じ）。`MyEngine.props` の参照（`ProjectReference`）が同じ GUID を書いているので、そろえておく。
- `.sln` のときは「一番上に書いたプロジェクト」がスタートアップになり、Visual Studio が `.vs/` に覚えた物があればそちらが勝つ。`.slnx` は `DefaultStartup` で決まる。

### 1.4 条件の無い ItemDefinitionGroup

vcxproj の設定は、`<ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">` のように構成ごとに書くのが Visual Studio の作る形。全部の構成で同じ物（警告のレベル・SubSystem・ビルドの後のイベントなど）を3回書くことになる。

`Condition` を書かない `ItemDefinitionGroup` は、**全部の構成に効く**。後に書いた構成ごとの `ItemDefinitionGroup` は、そこへ足す・上書きする。Sandbox はこの形で、構成ごとの所には違う物（`_DEBUG`・最適化）だけを書く。`MyEngine.props` も同じ書き方をしている。

> Visual Studio のプロパティ ページで値を変えると、Visual Studio は構成ごとの所に書き足す。そのときは、共通の所と二重になっていないか見る。

## パートA Sandbox を作る（エンジン側）

### A-1 `project/Sandbox/main.cpp`（新しいファイル）

```cpp
#include <MyEngine/Engine.h>


//=============================================================================
// エンジンの起動確認・機能テスト用の exe（Sandbox）
//=============================================================================
// ===== 入口 =====
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	// --- ウィンドウの設定 ---
	WindowConfig config;
	config.title = L"MyEngine Sandbox"; // タイトルバーの文字
	config.width = 1280;                // ウィンドウの幅（ピクセル）
	config.height = 720;                // ウィンドウの高さ（ピクセル）
#ifdef USE_IMGUI
	config.isImGui = true; // Debug・Development はエディタ（ImGui）を出す
#endif

	// --- 起動 ---
	// 最初のシーンは resources/scenes/SampleScene.scene.json
	// エディタ（Debug・Development）は、前回最後に開いていたシーンから始まる（Parameters/Editor/LastScene.json）
	Engine::Initialize(config, "SampleScene");

	// --- ゲームループ ---
	while (Engine::ProcessMessage()) {
		Engine::BeginFrame();
		Engine::GetWindowManager()->UpdateAll();
		Engine::GetWindowManager()->DrawAll();
		Engine::EndFrame();
	}

	// --- 終了 ---
	Engine::Finalize();
	return 0;
}
```

### A-2 `project/Sandbox/Sandbox.vcxproj`（新しいファイル）

Visual Studio の「新しいプロジェクト」で作らずに、このファイルをそのまま置く（新しいプロジェクトの画面で作ると、構成が Debug・Release の2つになり、設定も構成ごとに3回書かれる）。`ProjectGuid` はこのままでよい（ほかのプロジェクトとぶつからない、でたらめな番号）。

```xml
<?xml version="1.0" encoding="utf-8"?>
<!--
=============================================================================
 Sandbox.vcxproj
 エンジンの起動確認・機能テスト用の exe（エンジンはスタティックライブラリなので、動かすには exe が要る）
 ゲームと同じく MyEngine.props を読む。エンジン・DirectXTex・ImGui の参照とライブラリは props が全部決める
=============================================================================
-->
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">

  <!-- ===== 構成（エンジンと同じ3つ） ===== -->
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Development|x64">
      <Configuration>Development</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>

  <!-- ===== ソースファイル ===== -->
  <ItemGroup>
    <ClCompile Include="main.cpp" />
  </ItemGroup>

  <!-- ===== プロジェクトの情報 ===== -->
  <PropertyGroup Label="Globals">
    <VCProjectVersion>18.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{fceff50c-6fda-43ab-89da-b5e1efce7dc8}</ProjectGuid>
    <RootNamespace>Sandbox</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <!-- ===== exe の種類（Microsoft.Cpp.props より前に決める） ===== -->
  <!-- 全部の構成で共通 -->
  <PropertyGroup Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>v145</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <!-- Debug だけデバッグ用のランタイム -->
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'!='Debug'" Label="Configuration">
    <UseDebugLibraries>false</UseDebugLibraries>
  </PropertyGroup>
  <!-- Release だけ全体の最適化（エンジンの Release も /GL なので、リンクで /LTCG になる） -->
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />

  <!-- ===== プロパティシート ===== -->
  <ImportGroup Label="PropertySheets">
    <!-- エンジンの場所・include・ライブラリ・参照（同じリポジトリの中なので、1つ上のフォルダ） -->
    <Import Project="..\MyEngine.props" />
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />

  <!-- ===== 全部の構成で共通 ===== -->
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <!-- Sandbox のフォルダ：Script の .h を "Source/Scripts/～.h" で読めるように -->
      <AdditionalIncludeDirectories>$(ProjectDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
    </Link>
    <Manifest>
      <EnableSegmentHeap>true</EnableSegmentHeap>
    </Manifest>
    <!-- ビルドの後 -->
    <!-- 1・2行目：シェーダーのコンパイラ（dxcompiler.dll・dxil.dll）を exe の隣へ -->
    <!-- 3・4行目：エンジンの .shader と Resources（アイコン・グリッド・白の画像・空）を、起動するフォルダ（このフォルダ）の MyEngine\ へ -->
    <!--           /D は「写し先より新しいファイルだけ」写す（2回目からは速い）。写し先は .gitignore で git に入れない -->
    <PostBuildEvent>
      <Command>
copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"
copy /Y "$(WindowsSdkDir)bin\$(TargetPlatformVersion)\x64\dxil.dll" "$(TargetDir)dxil.dll"
xcopy /Y /S /I /D "$(MyEngineRoot)MyEngine\Shader\Data" "$(ProjectDir)MyEngine\Shader\Data\"
xcopy /Y /S /I /D "$(MyEngineRoot)Resources" "$(ProjectDir)MyEngine\Resources\"
      </Command>
    </PostBuildEvent>
  </ItemDefinitionGroup>

  <!-- ===== Debug ===== -->
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>

  <!-- ===== Development（エディタあり・最適化なし。Script をブレークポイントで止めやすい） ===== -->
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Development'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
    </ClCompile>
  </ItemDefinitionGroup>

  <!-- ===== Release ===== -->
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <PreprocessorDefinitions>NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />
</Project>
```

- `UseDebugLibraries`・`WholeProgramOptimization` は、AL4_01 の vcxproj と同じ値（書く場所をまとめただけ）。
- Development の `Optimization` を `Disabled` にするのも AL4_01 と同じ。エンジンの Development も最適化なし。
- `_CONSOLE` は書かない（AL4_01 の vcxproj にあるが、ウィンドウのアプリなので意味が無い）。

### A-3 `project/Sandbox/Sandbox.vcxproj.filters`（新しいファイル）

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClCompile Include="main.cpp" />
  </ItemGroup>
</Project>
```

> ファイルを足したら、エンジンのフィルタを作り直す道具がそのまま使える（`project/Sandbox` で `powershell -ExecutionPolicy Bypass -File ..\Tools\UpdateFilters.ps1 Sandbox.vcxproj`）。パートB で使う。

### A-4 `project/Sandbox/Resources/scenes/SampleScene.scene.json`（新しいファイル）

ふつうのシーン。カメラ・平行光源・地面（薄い箱）・箱の4つ。モデルのファイルを使わない（Primitive Renderer の箱）ので、Resources にモデルを置かなくてよい。

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
					"translation": [0.0, 4.0, -10.0],
					"rotation": [20.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
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
				}
			}
		}
	]
}
```

- 地面は 20 m 四方・厚さ 0.1 m の箱。上の面が高さ 0 になるように、0.05 m 下げる。
- 箱は1辺 1 m。床に乗るように 0.5 m 上げ、Y に 30 度回して面が2つ見えるようにする。
- シーンの `"version": 1` と小さい番号（1〜4）は、PrefabInstance.md（でたらめな番号・版 2）を入れた後もそのまま読める（PrefabInstance.md 0.4 の「番号の配り方」「版」）。エディタで保存すると、そのときの形で書き直される。
- Hierarchy の「Create」で作れる物だけなので、エディタで作って Save しても同じ物ができる。手で書くのは、最初の1回をコミットしておくため。

### A-5 `project/Sandbox/Parameters/PostEffect/ToneMap.json`（新しいファイル）

```json
{
    "Exposure": 1.0,
    "Mode": 2
}
```

- `Mode` はトーンマップの曲線の番号（`LensPass.h` の `ToneMapMode`：0 None・1 Reinhard・**2 ACES**・3 GT）。
- エディタの Settings（PostEffect → ToneMap）で変えて保存しても、このファイルに書かれる。Bloom・Lens はエンジンの既定のままでよいので、ファイルを置かない。

### A-6 `project/MyEngine_v1.sln` を消して、`project/MyEngine_v1.slnx` を作る

Visual Studio を閉じてから行う。

1. `project/MyEngine_v1.slnx` を作る（新しいファイル）。

```xml
<Solution>
  <Configurations>
    <BuildType Name="Debug" />
    <BuildType Name="Development" />
    <BuildType Name="Release" />
    <Platform Name="x64" />
  </Configurations>
  <Folder Name="/externals/">
    <Project Path="externals/DirectXTex/DirectXTex_Desktop_2022_Win10.vcxproj" Id="371b9fa9-4c90-4ac6-a123-aced756d6c77" />
    <Project Path="externals/imgui/imgui.vcxproj" Id="86a1cafe-73b6-4e0a-8c54-ef98278a3806">
      <Build Solution="Release|*" Project="false" />
    </Project>
  </Folder>
  <Project Path="MyEngine_v1.vcxproj" Id="1d0f63d3-16bb-43b8-985e-6bb516a1d662" />
  <Project Path="Sandbox/Sandbox.vcxproj" Id="fceff50c-6fda-43ab-89da-b5e1efce7dc8" DefaultStartup="true" />
</Solution>
```

2. `project/MyEngine_v1.sln` を git から消す。

```bash
git rm project/MyEngine_v1.sln
```

> `.slnx` に XML のコメントは書かない。Visual Studio はソリューションを保存するとき（プロジェクトを足した・フォルダを動かしたなど）に、`.slnx` を書き直してコメントを消す。説明はこの md と 1.3 に置く。
>
> `.vs/` に `.sln` のときの設定（開いていたファイルなど）が残っていても、`.slnx` では使われないだけで害は無い。

### A-7 `.github/workflows/DebugBuild.yml`・`ReleaseBuild.yml`

ビルドするソリューションを `.slnx` に変える。DebugBuild はコメントも直す（1.2 のとおり、もう Development の対応付けは無い）。

**DebugBuild.yml**（`env:` の2行）

```yaml
env:
  SOLUTION_FILE_PATH: project\MyEngine_v1.slnx # ビルドするソリューション（リポジトリのルートから見た場所）
  CONFIGURATION: Debug                         # 構成（エンジンも Sandbox も Debug でビルドされる）
```

**ReleaseBuild.yml**（`env:` の1行目）

```yaml
  SOLUTION_FILE_PATH: project\MyEngine_v1.slnx # ビルドするソリューション（リポジトリのルートから見た場所）
```

> `windows-2025-vs2026` の MSBuild は `.slnx` をそのまま読める。
>
> `CheckUnwantedFiles.yml` は直さない。`imgui.ini`・`imgui_style.ini`・`logs` を見つけると失敗にするので、Sandbox が書くそれらは次の `.gitignore` で外す。

### A-8 `.gitignore`（最後に足す）

```gitignore

# Sandbox（エンジンのテスト用の exe）が書くファイル
/project/Sandbox/MyEngine/
/project/Sandbox/imgui.ini
/project/Sandbox/imgui_style.ini
/project/Sandbox/saves/

# エディタが書く、人ごとに違うファイル（git に入れると、保存するたびにぶつかる）
*.bak
*.broken
/project/Sandbox/Parameters/Editor/
```

| 行 | 何か |
|---|---|
| `/project/Sandbox/MyEngine/` | ビルドの後に写すエンジンの Resources・.shader と、起動のたびに作る .hlsl |
| `imgui.ini`・`imgui_style.ini` | ImGui のウィンドウの位置・見た目（人ごとに違う。CheckUnwantedFiles でも失敗になる） |
| `saves/` | ゲームの進み具合の記録（GameProgress） |
| 下の3行 | PrefabInstance.md 2.1 の3行と同じ。`Parameters/Editor/` だけ場所を Sandbox に合わせた |

- ログ（`logs/`）は、前からある `[Ll]ogs/` の行で外れている。
- `Parameters/` のほかのファイル（A-5 の ToneMap.json・ポストエフェクトの値など）は git に入れる（テストのシーンの見た目を、だれが起動してもそろえるため）。

### A-9 `README.md`

1行目の説明の下に1行足す。

```markdown
# MyEngine_v1
DirectX 12 で作っている自作ゲームエンジン（静的ライブラリ）。ゲームは別のリポジトリから参照して使う。
エンジンの起動確認・機能テストは、同じリポジトリの `project/Sandbox/`（exe）で行う。
```

「## 取ってくるとき」の最初の段落の後ろに、番号の付いた2行を足す（下の3つの「-」はそのまま）。

```markdown
## 取ってくるとき
外部ライブラリは `project/externals` にまとめている。assimp は vcpkg で用意する（`project/externals/vcpkg.json`）。clone してビルドするだけでよい。

1. `project/MyEngine_v1.slnx` を Visual Studio で開く
2. 構成（Debug・Development・Release）を選んで F5。エンジンと Sandbox がビルドされ、Sandbox が起動する（最初のシーンは `SampleScene`）
```

### A-10 確かめる

1. `project/MyEngine_v1.slnx` を開く。ソリューション エクスプローラーに `externals`（DirectXTex・imgui）・`MyEngine_v1`・`Sandbox` が並び、`Sandbox` が太字（スタートアップ）。
2. 構成のリストに Debug・Development・Release の3つがある。
3. Development で F5。エンジン → Sandbox の順にビルドされ、エディタが開く。Hierarchy に Main Camera・Directional Light・Ground・Cube の4つ。Game ビューに灰色の地面とオレンジの箱が出る。
4. Debug・Release でも F5。Release はエディタ無しで、Game ビューと同じ絵がウィンドウに出る。
5. `git status` に `project/Sandbox/MyEngine/`・`imgui.ini`・`Parameters/Editor/`・`logs/` が出ない。

> **初めての起動は、エディタのウィンドウが重なって出る**（並びを覚える `imgui.ini` がまだ無いため。エンジンの前からの動き）。ドラッグして並べれば、次からは `project/Sandbox/imgui.ini` に覚える。AL4_01 の `project/imgui.ini` を `project/Sandbox/` へ写すと、AL4_01 と同じ並びで始まる。
>
> Scene ビューのカメラ（エディタのカメラ）は、初めは地面の高さにあるので、地面が線に見える。動かすと `Parameters/Editor/Scene View/` に覚える（人ごとなので git に入れない）。

## パートB AL4_01 のテストを Sandbox へ移す

AL4_01 のテストのファイルは **全部 git に入っていない**（`git status` で `??`）。消すと戻せないので、**写す → Sandbox で確かめる → AL4_01 から消す** の順にする。

### B-1 移す物・残す物

| AL4_01 の `project/` の下 | どうするか |
|---|---|
| `Source/Scripts/BossTest/`・`CameraTest/`・`TimelineTest/`・`UIAnimationTest/` | Sandbox へ移す |
| `Resources/scenes/` の `GameScene` 以外（10個） | Sandbox へ移す |
| `Resources/scenes/GameScene.scene.json`（ボスのテスト） | Sandbox へ移して、名前を `BossTest.scene.json` に。AL4_01 にはふつうのシーンを置き直す（B-5） |
| `Resources/boss/`・`curve/`・`prefabs/`・`textures/`・`timelines/`・`ui/` | Sandbox へ移す（`.bak` は持っていかない） |
| `Resources/Model/Player/` | **AL4_01 に残す**（次のゲームのモデル。テストのシーンから使っていない） |
| `Parameters/Editor/LastScene.json` | 移さない（Sandbox は起動すると作る） |

> テストのシーンは、ファイルを `resources/timelines/…` のように `resources/` から書いているので、同じ並びで移せばそのまま読める。

### B-2 写す（PowerShell）

```powershell
$game = "C:\Users\K025G\source\repos\AL4_01\project"
$sandbox = "C:\Users\K025G\source\repos\MyEngine_v1\project\Sandbox"
# Script（フォルダごと）
robocopy "$game\Source\Scripts" "$sandbox\Source\Scripts" /E
# Resources（/XF *.bak で .bak を外す）
foreach ($folder in "boss", "curve", "prefabs", "textures", "timelines", "ui", "scenes") {
	robocopy "$game\Resources\$folder" "$sandbox\Resources\$folder" /E /XF *.bak
}
# ボスのテストのシーンは、Sandbox では BossTest という名前に
Rename-Item "$sandbox\Resources\scenes\GameScene.scene.json" "BossTest.scene.json"
```

- `robocopy 元 先 /E` は、フォルダの中を下の階層まで全部写す（空のフォルダも）。元は消さない。
- 写せたファイルは 53 個（Script 7・シーン 11・Timeline 26・UI 画面 4・Prefab 2・攻撃表 1・カーブ 1・画像 1）。

### B-3 `Sandbox.vcxproj` に Script を足す

`<!-- ===== ソースファイル ===== -->` の `ItemGroup` を、これに置き換える。

```xml
  <!-- ===== ソースファイル ===== -->
  <ItemGroup>
    <ClCompile Include="main.cpp" />
    <ClCompile Include="Source\Scripts\BossTest\BossTestAttacks.cpp" />
    <ClCompile Include="Source\Scripts\BossTest\BossTestController.cpp" />
    <ClCompile Include="Source\Scripts\CameraTest\CameraTimelineTester.cpp" />
    <ClCompile Include="Source\Scripts\CameraTest\VirtualCameraTester.cpp" />
    <ClCompile Include="Source\Scripts\TimelineTest\TimelineTester.cpp" />
    <ClCompile Include="Source\Scripts\UIAnimationTest\TimelineTriggerTester.cpp" />
    <ClCompile Include="Source\Scripts\UIAnimationTest\UIAnimationTester.cpp" />
  </ItemGroup>
```

フィルタは道具で作り直す（`project/Sandbox` で）。

```powershell
powershell -ExecutionPolicy Bypass -File ..\Tools\UpdateFilters.ps1 Sandbox.vcxproj
```

`8 files, 6 filters -> …\Sandbox.vcxproj.filters` と出る（フォルダ `Source`・`Source\Scripts`・その下の4つ）。

> Script の .cpp はエンジンのヘッダーを `<MyEngine/…>` で読んでいるので、中身は直さなくてよい。

### B-4 Sandbox で確かめる

Development で F5 して、エディタの「File → Open Scene」（シーンの一覧）で、移したシーンを開く。

| シーン | 見る所 |
|---|---|
| `BossTest` | Play で、ボスの球が攻撃表のとおりに動く（BossTestController・Attack Table） |
| `VirtualCameraTest`・`CameraTimelineTest` | Play で、カメラが切り替わる・Timeline のカメラ演出が動く |
| `TimelineTest`・`TimelineWindowTest`・`TimelineTriggerTest` | Timeline ウィンドウでトラックが出る・Play で動く |
| `UIAnimationTest`・`VectorShapeTest`・`PolylineTest`・`MaskTest`・`WaveBackground` | UI の図形・アニメ・マスクが出る |

Add Component の Script の一覧に `BossTestController`・`VirtualCameraTester` などが出ていれば、Script も入っている。

### B-5 AL4_01 から消す（ゲーム側の作業）

Sandbox で確かめてから行う。

1. エクスプローラーで、AL4_01 の `project/` の下の次を消す（ごみ箱へ）。
   - `Source/Scripts/` の `BossTest`・`CameraTest`・`TimelineTest`・`UIAnimationTest`
   - `Resources/` の `boss`・`curve`・`prefabs`・`textures`・`timelines`・`ui`
   - `Resources/scenes/` の `GameScene.scene.json` 以外の全部
2. `AL4_01.vcxproj` の `ClCompile` から、`Source\Scripts\…` の7行を消す（`main.cpp` だけ残す）。フィルタも作り直す（`AL4_01/project` で）。

```powershell
powershell -ExecutionPolicy Bypass -File ..\MyEngine_v1\project\Tools\UpdateFilters.ps1 AL4_01.vcxproj
```

3. `Resources/scenes/GameScene.scene.json` を、A-4 の `SampleScene.scene.json` と同じ中身にする（ボスの BossTestController が無くなるので、そのままだと読めない Component が残る）。AL4_01 の `Parameters/PostEffect/` は 10-31 に消してある（未コミット）ので、同じ見た目にしたいときは A-5 の `ToneMap.json` も `Parameters/PostEffect/` に置く。
4. エンジンのコミットを push した後、AL4_01 のサブモジュールを新しくする（`git -C MyEngine_v1 pull` など、いつもの手順）。サブモジュールに `project/Sandbox/` が入ってくるが、AL4_01 のビルドには関係しない（0.5）。
5. AL4_01 を Development でビルドして起動し、ふつうのシーンが開くのを見る。

> `Parameters/Editor/LastScene.json` が消したシーンを指していても、起動すると `GameScene` から始まる（`SceneManager::GetLastOpenedScene` が、無いシーンなら最初のシーンにする）。

## パートC チームのゲームのひな形として使う（いつか）

チーム制作のゲームのリポジトリを作るときは、Sandbox を写して始められる。今はやらない（チームのリポジトリを作るときにこの章を見る）。

### C-1 リポジトリの形

```
Game/                         ← チームのリポジトリ
├─ .gitignore
├─ .gitmodules
├─ MyEngine_v1/               ← エンジン（サブモジュール）
└─ project/
   ├─ Game.slnx
   ├─ Game.vcxproj(.filters)  ← Sandbox.vcxproj を写して直す
   ├─ main.cpp                ← Sandbox の main.cpp を写して、タイトルと最初のシーンを直す
   ├─ Resources/scenes/SampleScene.scene.json
   └─ Parameters/PostEffect/ToneMap.json
```

### C-2 手順

1. 空のリポジトリにエンジンを足す。

```bash
git submodule add -b develop https://github.com/yamamoto-mitsuki/MyEngine_v1.git MyEngine_v1
```

2. `MyEngine_v1/project/Sandbox/` から `main.cpp`・`Sandbox.vcxproj`・`Sandbox.vcxproj.filters`・`Resources/scenes/SampleScene.scene.json`・`Parameters/PostEffect/ToneMap.json` を `project/` へ写す（`Source/Scripts/`・テストのシーン・`MyEngine/`・`Parameters/Editor/` は写さない）。vcxproj と `.filters` の `ClCompile` は `main.cpp` だけにする。
3. vcxproj の名前を `Game.vcxproj`（`.filters` も）に変えて、中を直す。

| 所 | 直し方 |
|---|---|
| `ProjectGuid` | 新しい番号（PowerShell で `[guid]::NewGuid()`） |
| `RootNamespace` | ゲームの名前 |
| `<Import Project="..\MyEngine.props" />` | `<Import Project="..\MyEngine_v1\project\MyEngine.props" />` |
| ビルドの後の `xcopy` の2行 | 消す（ゲームのソリューションでは、エンジンのビルドの後の写しが `project/MyEngine/` へ写す。1.1）。上の「3・4行目」のコメント2行も消す |
| 一番上のコメント | ゲームの説明に |

4. `project/Game.slnx` を作る（`Path` はサブモジュールの中を指す）。

```xml
<Solution>
  <Configurations>
    <BuildType Name="Debug" />
    <BuildType Name="Development" />
    <BuildType Name="Release" />
    <Platform Name="x64" />
  </Configurations>
  <Folder Name="/externals/">
    <Project Path="../MyEngine_v1/project/externals/DirectXTex/DirectXTex_Desktop_2022_Win10.vcxproj" Id="371b9fa9-4c90-4ac6-a123-aced756d6c77" />
    <Project Path="../MyEngine_v1/project/externals/imgui/imgui.vcxproj" Id="86a1cafe-73b6-4e0a-8c54-ef98278a3806">
      <Build Solution="Release|*" Project="false" />
    </Project>
  </Folder>
  <Project Path="../MyEngine_v1/project/MyEngine_v1.vcxproj" Id="1d0f63d3-16bb-43b8-985e-6bb516a1d662" />
  <Project Path="Game.vcxproj" Id="（3 で作った番号）" DefaultStartup="true" />
</Solution>
```

5. `.gitignore` を作る。エンジンの `.gitignore` の Visual Studio の部分（`## Ignore Visual Studio temporary files…` から `FodyWeavers.xsd` まで）を写して、最後に足す。

```gitignore
# ビルドの出力・中間ファイル（OutputDirs.props）
/generated/

# エンジンのビルドの後に写される、エンジンの Resources・.shader
/project/MyEngine/

# ImGui のウィンドウの位置・見た目、遊んだ記録（人ごとに違う）
/project/imgui.ini
/project/imgui_style.ini
/project/saves/

# モデルの .obj は入れる（上の Visual Studio の部分が *.obj を外すので）
!/project/Resources/**/*.obj

# エディタが書く、人ごとに違うファイル（git に入れると、保存するたびにぶつかる）
*.bak
*.broken
/project/Parameters/Editor/
```

> 最後の3行は PrefabInstance.md 2.1 の3行。**最初のコミットより前に** 足す。

6. `project/Game.slnx` を開いて、3構成で F5（初めてのビルドは、vcpkg が assimp などを用意するので数分かかる）。

## D 検証の結果（写しで確かめた）

### D-1 clone し直して、ビルドするだけで動くか（0.6）

1. このリポジトリ（a93c6b8。PrefabInstance.md 入り）を一時フォルダに clone して、パートA・B のとおりにファイルを置き、その写しの中でコミットした。
2. それを **もう一度 clone した**（git に入っている物だけになる。`vcpkg_installed`・`generated`・`project/Sandbox/MyEngine/` は無い）。
3. 何も写さずに、MSBuild で `MyEngine_v1.slnx` を Development → Debug → Release の順にビルドした。vcpkg のキャッシュも使わない（`VCPKG_BINARY_SOURCES=clear`。新しい PC と同じ）。

| 確かめたこと | 結果 |
|---|---|
| Development（最初のビルド） | 成功・419 秒。vcpkg が assimp と依存の 13 個を一から作った。エラー 0、警告は DirectXTex の C4865 だけ |
| Debug・Release | 成功（156 秒・142 秒）。エラー 0、DirectXTex 以外の警告 0 |
| 3構成の起動（`imgui.ini` も無い状態） | 全部起動して `SampleScene`（4個）を読み、閉じると終了コード 0。Release はゲームのカメラから地面と箱。Debug・Development はエディタが開く（ウィンドウは重なって出る）。ログの警告は imgui_style.ini（初めての起動）だけ |
| 移したシーン 11 個と SampleScene を Development で開く（PrefabInstance 入りのエンジン） | 全部開けた。警告・エラー 0 |
| ビルドと起動の後の `git status` | 何も出ない（作られた物は全部 `.gitignore` で外れている） |

### D-2 最初の写しで確かめたこと

写し（このリポジトリを 44a36b3 で clone した物。PrefabInstance.md はまだ入っていない。vcpkg_installed だけは本体から写した）に、パートA・B のとおりにファイルを置いて、MSBuild で `MyEngine_v1.slnx` をビルドし、`project/Sandbox` を作業フォルダにして起動した。md のコード（A-1〜A-6・A-8・B-3）は、確かめたファイルと1文字ずつ同じ。

| 確かめたこと | 結果 |
|---|---|
| パートA だけで、Debug・Development・Release のビルド | エラー 0。警告は DirectXTex の C4865（28 個、前からある物）だけ。Release では imgui をビルドしない（`.slnx` の `Build Project="false"` が効く） |
| 2回目からのビルドの後の写し | `xcopy /D` が「0 個のファイルをコピーしました」と出て、ビルドは成功のまま |
| Development・Debug の起動 | エディタが開き、`SampleScene` が開く（Hierarchy に Main Camera・Directional Light・Ground・Cube）。ログの警告は「imgui_style.ini が無い（初めての起動）」だけ。閉じると終了コード 0 |
| Release の起動 | ゲームのカメラから、灰色の地面と、2つの面が見えるオレンジの箱が出る |
| `ToneMap.json` が無いとき | Scene ビューの空と地面が白く飛ぶ。置くと飛ばない（0.4 のトーンマップ） |
| パートB を入れて3構成のビルド | エラー 0・DirectXTex 以外の警告 0（Script 7 つがコンパイルされる） |
| 移したシーン 11 個と SampleScene を、Development で1つずつ開く（`LastScene.json` を書き換えて起動） | 全部開けた。読めない Component などの警告・エラーは 0。開いて閉じただけでは、どのファイルも書き換わらない |
| 起動した後の `git status` | 出るのは `.gitignore`・`.sln` の削除・`.slnx`・Sandbox の vcxproj／.filters／main.cpp／ToneMap.json と、パートB の 54 ファイルだけ。`MyEngine/`・`imgui.ini`・`logs/`・`Parameters/Editor/` は出ない |
| パートC（ひな形）を写しで作る（`Game/MyEngine_v1` にエンジン、`Game/project/` に C-2 の直しをした vcxproj・`.slnx`）。Development のビルドと起動 | エラー 0・DirectXTex 以外の警告 0。エンジンのビルドの後の写しが `Game/project/MyEngine/` へ 35 ファイル写し、`SampleScene` が開く（ログの警告は imgui_style.ini だけ）。ゲームのソリューションでは Sandbox はビルドされない（0.5） |

**確かめていない物：**

- Play したときの動き（BossTest の攻撃・カメラの切り替え・UI のアニメなど）。シーンを開いただけ。Visual Studio の画面（スタートアップの太字・構成のリスト）も見ていない（MSBuild だけ）。
- GitHub Actions（`.slnx` を `windows-2025-vs2026` でビルドする所）。手元の MSBuild（Visual Studio 18）は読めた。
- パートB-5（AL4_01 から消す・`GameScene` を置き直す・サブモジュールを新しくする）。AL4_01 は触っていない。
- パートC の Debug・Release（Development だけ確かめた。エンジンは本物のサブモジュールではなく、写しへのジャンクション）と、C-2 の `.gitignore`。
- D-2 では `TimelineTriggerTest`・`TimelineWindowTest` だけ、閉じるのに 15 秒より長くかかった（終了コードは 0）。D-1（PrefabInstance 入り）ではすぐ閉じた。調べていない。
- A-9（README.md）は文章だけなので、写しには入れていない。

### D-3 本体に直接入れた後（2026-11-04）

| 確かめたこと | 結果 |
|---|---|
| 本体で `MyEngine_v1.slnx` を Development・Debug・Release でビルド | エラー 0・DirectXTex 以外の警告 0 |
| 本体の3構成の Sandbox を `project/Sandbox` で起動して閉じる | 全部終了コード 0。`SampleScene`（4個）を読んだ |
| 本体の変更を入れた写しを clone し直し、Development でビルド・起動（vcpkg のキャッシュはあり） | ビルド成功・起動して `SampleScene` を読み、終了コード 0。ログの警告は imgui_style.ini（初めての起動）だけ。起動の後の `git status` は何も出ない |

## この先

- clone したばかりだと `imgui.ini` が無いので、エディタのウィンドウが重なって出る（D-1）。`imgui.ini` は人ごとの物で、CheckUnwantedFiles でも git に入れられない。エンジンに「`imgui.ini` が無いときの並び」（ドッキングの最初の形）を持たせれば、clone してすぐ見やすくなる。エンジンの直しなので、別の md でやる。

- `Docs/Tasks/` の見本のフォルダ（`TimelineTest/`・`UIAnimationTest/`・`PolylineTest/` など）は、AL4_01 へ写して使う物だった。これからの見本は Sandbox に直接置けば、md から写す手間が無くなる（同じ物は B で Sandbox に入る）。
- `FolderSplit.md`・`Vcpkg.md` に `project/MyEngine_v1.sln` と書いてある所がある（終わった作業の記録なので、そのままでもよい）。
