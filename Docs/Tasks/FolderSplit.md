参考：
- `git mv`（ファイルを動かす。履歴が続く）：https://git-scm.com/docs/git-mv
- .vcxproj.filters（Visual Studio のフォルダ表示）：https://learn.microsoft.com/ja-jp/cpp/build/reference/vcxproj-filters-files
- .NET の正規表現の「前後を見る」書き方（`(?<!...)`・`(?!...)`）：https://learn.microsoft.com/ja-jp/dotnet/standard/base-types/grouping-constructs-in-regular-expressions
- Windows PowerShell 5.1 とスクリプトの文字コード（BOM 無しの UTF-8 は読み違える）：https://learn.microsoft.com/ja-jp/powershell/module/microsoft.powershell.core/about/about_character_encoding

# フォルダ分け（Components と Systems をカテゴリのフォルダに）

## このファイルについて
- 課題の後の整理の **手順2**（手順0・1 は済み。手順3 のリファクタリングはこの後）。
- **ファイルを動かすだけで、コードの中身は変えない**。変わるのは `#include` のパス・vcxproj・filters・Docs の md に書いたパスだけ。
- 動かすファイルが 66 個、書き換える include が約 300 か所あるので、手で写すと間違えやすい。**スクリプトを1つ写して、実行する**形にした。
- **2026-10-30：本体で実行済み**（ユーザーの頼みで Claude が実行。出た数は3章と同じ・ARCHITECTURE.md も直した・3構成のビルドが通った）。このファイルは「何をどこへ動かしたか」の記録として残す。
- 確かめた環境：本体の develop（37e57d3）を一時フォルダに写して、このスクリプトを動かし、エンジンを Debug・Development・Release でビルドした（5章）。

## 作業する場所
- **HTTEngine の `project/HTTEngine.sln`**（この本体のリポジトリ）。AL4_01 の中のサブモジュールではない。
- AL4_01 のサブモジュールはコミット `58dd708` に止めてあるので、こちらでいくら動かしても **AL4_01 は壊れない**（6章）。
- エンジンは静的ライブラリなので、ここで確かめられるのは「ビルドが通るか」まで。ファイルを動かすだけなので、それで足りる。

---

## 1. 分け方

### 決まり：カテゴリ名＝Add Component のカテゴリ名
- フォルダの名前は、Inspector の **Add Component に出るカテゴリ**と同じにする（`ComponentEditor.cpp` の `kTopLevelOrder`・各 System の `ComponentEditorRegistry::Register` に書いてある名前）。
- System は、**その System が主に扱う Component と同じカテゴリ**に入れる。
- `EngineComponents.cpp`（エンジンの Component 全部の保存の項目）は、どれか1つのカテゴリの物ではないので **Components の直下に残す**。
- 前に決めた「名前の終わりで置き場所を決める」（〜Component は Components、〜System は Systems）はそのまま。その中を1段分けるだけ。

| カテゴリ | Components | Systems |
|---|---|---|
| Core | TransformComponent・FolderComponent | — |
| Camera | CameraComponent・CameraFollowComponent | CameraSystem |
| Rendering3D | ModelRendererComponent・PrimitiveRendererComponent・PolygonShapeComponent・PolygonRendererComponent・ArrowStripComponent・FogComponent・DepthOfFieldComponent | ModelRenderSystem・PrimitiveRenderSystem・PolygonShapeSystem・ArrowStripSystem・FogSystem・DepthOfFieldSystem |
| Rendering2D | SpriteRendererComponent | SpriteRenderSystem |
| UI | AlphaGroupComponent・ImageComponent・TextComponent・UIScreenComponent | TextRenderSystem |
| Lighting | LightComponent | LightSystem |
| Effects | ParticleSystemComponent・TrailComponent | ParticleSystem・TrailSystem |
| Physics | ColliderComponent・ElasticSurfaceComponent・RigidbodyComponent・WindZoneComponent | CollisionSystem・ElasticSurfaceSystem・PhysicsSystem・WindZoneSystem |
| Audio | AudioSourceComponent | AudioSystem |
| Animation | AnimatorComponent | AnimationSystem |
| Gameplay | TriggerActivatorComponent | TriggerActivatorSystem・ScriptSystem |

- 分けた後の一番大きいフォルダは、Systems の Rendering3D（6組＝12ファイル）。15 を下回る。
- Transform・Folder・Model Renderer は Add Component に出ない（Entity を作ると付く・Hierarchy で作る）が、Transform と Folder は `kTopLevelOrder` の先頭の **Core**、Model Renderer は形を描く物なので **Rendering3D** にした。
- SpriteRenderSystem は Image（UI）・Alpha Group（UI）も描くが、名前の通り Sprite Renderer が主なので **Rendering2D**。

### 今回は分けない所
| フォルダ | ファイルの数 | 分けない理由 |
|---|---|---|
| Math | 17 ＋ Geometry フォルダ | ベクトル・行列・クォータニオンなど、どれも「数学の型」で1つの話。ゲームからの include も一番多い（Vector3 だけで 34 か所）。分けても探しやすくならない |
| Editor/Windows | 16（8組） | ウィンドウ1つ＝1組で、組の数は 8。CLAUDE.md の「15 以上で視野に入れ始める」の境目。ウィンドウが増えたら分ける |

---

## 2. スクリプトを写す

`project/Tools/FolderSplit.ps1` を作って、下を全部写す。

- **保存は「UTF-8（BOM 付き）」にする**。Windows PowerShell 5.1 は BOM の無い UTF-8 を Shift-JIS として読むので、日本語のエラーの文が化けて、文字列の `"` がずれて動かなくなる。
  - Visual Studio で作った場合：ファイル → 名前を付けて保存 → 保存ボタンの横の ▼ →「エンコード付きで保存」→「Unicode (UTF-8 シグネチャ付き) - コードページ 65001」。
  - 同じフォルダの `UpdateFilters.ps1` は BOM 無しだが、日本語がコメントにしか無いので化けても動く（今回のは文字列に日本語がある）。
- 字下げは `UpdateFilters.ps1` と同じ **空白4つ**。

```powershell
<#
=============================================================================
FolderSplit.ps1
HTTEngine/Components と HTTEngine/Systems の中を、カテゴリのフォルダに分ける。
ファイルの中身（コード）は変えない。変えるのは「パス」だけ。
  1. ファイルを git mv で動かす（履歴が続く）
  2. include・vcxproj・Docs の md に書いてあるパスを、新しい場所に書き換える
  3. Tools\UpdateFilters.ps1 で、Visual Studio のフィルタを作り直す

使い方（project フォルダで。Visual Studio は閉じておく）：
powershell -ExecutionPolicy Bypass -File Tools\FolderSplit.ps1
=============================================================================
#>

$ErrorActionPreference = "Stop"

# ===== 場所 =====
$projectDir = Split-Path -Parent $PSScriptRoot   # Tools の1つ上＝project フォルダ
$rootDir = Split-Path -Parent $projectDir        # Git のルート（Docs がある所）
$engineDir = Join-Path $projectDir "HTTEngine"

# ===== どのカテゴリに入れるか =====
# カテゴリ名は Inspector の Add Component のカテゴリと同じ（ComponentEditor.cpp の kTopLevelOrder）
# Components は「名前.h」、Systems は「名前.h と 名前.cpp」を動かす
$categories = [ordered]@{
    "Core"        = @{ Components = @("TransformComponent", "FolderComponent"); Systems = @() }
    "Camera"      = @{ Components = @("CameraComponent", "CameraFollowComponent"); Systems = @("CameraSystem") }
    "Rendering3D" = @{
        Components = @("ModelRendererComponent", "PrimitiveRendererComponent", "PolygonShapeComponent", "PolygonRendererComponent", "ArrowStripComponent", "FogComponent", "DepthOfFieldComponent")
        Systems    = @("ModelRenderSystem", "PrimitiveRenderSystem", "PolygonShapeSystem", "ArrowStripSystem", "FogSystem", "DepthOfFieldSystem")
    }
    "Rendering2D" = @{ Components = @("SpriteRendererComponent"); Systems = @("SpriteRenderSystem") }
    "UI"          = @{ Components = @("AlphaGroupComponent", "ImageComponent", "TextComponent", "UIScreenComponent"); Systems = @("TextRenderSystem") }
    "Lighting"    = @{ Components = @("LightComponent"); Systems = @("LightSystem") }
    "Effects"     = @{ Components = @("ParticleSystemComponent", "TrailComponent"); Systems = @("ParticleSystem", "TrailSystem") }
    "Physics"     = @{
        Components = @("ColliderComponent", "ElasticSurfaceComponent", "RigidbodyComponent", "WindZoneComponent")
        Systems    = @("CollisionSystem", "ElasticSurfaceSystem", "PhysicsSystem", "WindZoneSystem")
    }
    "Audio"       = @{ Components = @("AudioSourceComponent"); Systems = @("AudioSystem") }
    "Animation"   = @{ Components = @("AnimatorComponent"); Systems = @("AnimationSystem") }
    "Gameplay"    = @{ Components = @("TriggerActivatorComponent"); Systems = @("TriggerActivatorSystem", "ScriptSystem") }
}


#=============================================================================
# 動かす一覧を作る
#=============================================================================

# Folder（Components か Systems）・Category・File（拡張子つきの名前）を1件にする
$moves = New-Object System.Collections.Generic.List[object]
foreach ($category in $categories.Keys) {
    foreach ($name in $categories[$category].Components) {
        $moves.Add([pscustomobject]@{ Folder = "Components"; Category = $category; File = "$name.h" })
    }
    foreach ($name in $categories[$category].Systems) {
        foreach ($extension in @("h", "cpp")) {
            $moves.Add([pscustomobject]@{ Folder = "Systems"; Category = $category; File = "$name.$extension" })
        }
    }
}

# --- 動かす前に全部そろっているか確かめる（途中で止まると半端に動いてしまうため） ---
foreach ($move in $moves) {
    $from = Join-Path $engineDir "$($move.Folder)\$($move.File)"
    $to = Join-Path $engineDir "$($move.Folder)\$($move.Category)\$($move.File)"
    if (-not (Test-Path $from)) {
        throw "見つからない：$from"
    }
    if (Test-Path $to) {
        throw "もうある：$to"
    }
}

# --- 一覧に無いファイルが残っていないか（新しく足した物の入れ忘れ） ---
# Components に残してよいのは EngineComponents.cpp（全部の Component の保存の項目）だけ
$listed = $moves | ForEach-Object { "$($_.Folder)\$($_.File)" }
foreach ($folder in @("Components", "Systems")) {
    foreach ($file in (Get-ChildItem (Join-Path $engineDir $folder) -File)) {
        $key = "$folder\$($file.Name)"
        if ($key -ne "Components\EngineComponents.cpp" -and $listed -notcontains $key) {
            throw "カテゴリが決まっていない：$key（上の `$categories に足す）"
        }
    }
}


#=============================================================================
# 1. git mv で動かす
#=============================================================================

foreach ($move in $moves) {
    $toDir = Join-Path $engineDir "$($move.Folder)\$($move.Category)"
    if (-not (Test-Path $toDir)) {
        New-Item -ItemType Directory $toDir | Out-Null
    }
    git -C $projectDir mv "HTTEngine/$($move.Folder)/$($move.File)" "HTTEngine/$($move.Folder)/$($move.Category)/$($move.File)"
    if ($LASTEXITCODE -ne 0) {
        throw "git mv に失敗：$($move.Folder)/$($move.File)"
    }
}
Write-Host "$($moves.Count) files moved"


#=============================================================================
# 2. パスを書き換える
#=============================================================================

# ===== 置き換えの表を作る =====
# 「Systems/〇〇System.h」→「Systems/カテゴリ/〇〇System.h」の形。/ と \ の両方
# (?<![A-Za-z]) は「前が英字ではない」。OldSystems/... のような別の名前の一部を書き換えないため
$replacements = New-Object System.Collections.Generic.List[object]
foreach ($move in $moves) {
    foreach ($separator in @("/", "\")) {
        $old = "$($move.Folder)$separator$($move.File)"
        $new = "$($move.Folder)$separator$($move.Category)$separator$($move.File)"
        $replacements.Add([pscustomobject]@{
            Pattern     = "(?<![A-Za-z])" + [regex]::Escape($old) + "(?![A-Za-z0-9_])"
            Replacement = $new
        })
    }
}

# ===== 書き換えるファイル =====
# エンジンのコード・プロジェクトファイル・Docs の md（externals と generated は触らない）
$targets = @()
$targets += Get-ChildItem $engineDir -Recurse -File -Include *.h, *.cpp
$targets += Get-Item (Join-Path $projectDir "HTTEngine.vcxproj")
$targets += Get-ChildItem (Join-Path $rootDir "Docs") -Recurse -File -Include *.md

# ===== 書き換える（文字コードと BOM はそのまま残す） =====
$utf8 = New-Object System.Text.UTF8Encoding($false)
$changedCount = 0
foreach ($target in $targets) {
    $bytes = [System.IO.File]::ReadAllBytes($target.FullName)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $offset = if ($hasBom) { 3 } else { 0 }
    $text = $utf8.GetString($bytes, $offset, $bytes.Length - $offset)

    $newText = $text
    foreach ($replacement in $replacements) {
        $newText = [regex]::Replace($newText, $replacement.Pattern, $replacement.Replacement)
    }

    # --- 変わったファイルだけ書く（更新日時を無駄に変えないため） ---
    if ($newText -cne $text) {
        $encoding = New-Object System.Text.UTF8Encoding($hasBom)
        [System.IO.File]::WriteAllText($target.FullName, $newText, $encoding)
        $changedCount++
    }
}
Write-Host "$changedCount files rewritten"


#=============================================================================
# 3. フィルタを作り直す
#=============================================================================

& (Join-Path $PSScriptRoot "UpdateFilters.ps1") (Join-Path $projectDir "HTTEngine.vcxproj")
```

### 解説：スクリプトの分かりにくい所

**`[ordered]@{ ... }`**
- PowerShell の `@{ }`（ハッシュテーブル）は、書いた順番を覚えていない。`[ordered]` を付けると、書いた順番のまま取り出せる（中身は .NET の `OrderedDictionary`）。
- 今回は順番が変わっても結果は同じだが、`Write-Host` やエラーの出る順番が表と同じになるので付けた。

**動かす前に2回確かめている理由**
- `git mv` を途中で失敗すると、半分だけ動いた状態になって、直すのが面倒。なので、**全部のファイルがあるか・行き先が空いているか**を、1つも動かさないうちに確かめる。
- もう1つは「一覧に無いファイルが Components・Systems に残っていないか」。新しい Component を足したのに表に入れ忘れると、それだけ直下に残ってしまうので、止めて知らせる。
- この確かめがあるので、**2回目を動かすと「もうある」で止まる**（同じ移動を2回しない）。

**`(?<![A-Za-z])` と `(?![A-Za-z0-9_])`（正規表現の「前後を見る」）**
- `(?<!X)` は「すぐ前が X ではない」、`(?!X)` は「すぐ後が X ではない」。どちらも文字は食べずに、条件だけを見る。
- 置き換える文字は「Systems/」＋ファイル名。前が英字なら、`OldSystems/` のような別の名前の一部なので書き換えない。後ろが英数字なら、`CameraSystem.hpp` のような別のファイルなので書き換えない。
- include の `"HTTEngine/` の後ろ・vcxproj の `HTTEngine\` の後ろ・md の `` ` `` の後ろは、どれも英字ではないので書き換わる。

**BOM をそのまま残す**
- エンジンの .h・.cpp は BOM 無し、vcxproj は BOM 付き、と混ざっている。`Get-Content`・`Set-Content` で書くと、PowerShell 5.1 は文字コードを勝手に変えてしまう。
- なので、バイトで読んで、先頭の3バイト（`EF BB BF`）があるかを見て、**同じ形で書き戻す**。改行（CRLF）は文字列の中にそのまま入っているので変わらない。

**filters は自分で書き換えない**
- vcxproj のパスを新しくした後で `UpdateFilters.ps1` を呼ぶと、vcxproj のパスの通りにフォルダを作り直してくれる。前からあるフォルダの GUID はそのまま使う。
- 今の filters には `Graphics/Animation`・`Graphics/Compute`・`Graphics/Text` など、前から足りなかったフォルダがある（Visual Studio で足したファイルが、親のフォルダに入っている）。作り直すとそれも直るので、filters の差分は大きくなる（フォルダの数 52 → 80）。

---

## 3. 動かす

1. **Visual Studio を閉じる**（開いたままだと、Visual Studio が古い vcxproj を書き戻すことがある）。
2. 今の変更が全部コミットしてあることを確かめる（`git status` が空）。こうしておくと、失敗しても `git reset --hard` と `git clean -fd project/HTTEngine` で戻せる。
3. project フォルダで動かす。

```bash
powershell -ExecutionPolicy Bypass -File Tools\FolderSplit.ps1
```

出る物（数が同じなら OK）：
```
66 files moved
70 files rewritten
395 files, 80 filters -> ...\project\HTTEngine.vcxproj.filters
```

---

## 4. ARCHITECTURE.md のフォルダの決まりを直す

スクリプトはパスだけを書き換えるので、決まりの文は手で直す。`Docs/ARCHITECTURE.md` の「# フォルダの決まり」の最初の2行を、下に書き換える。

```
・〜Component → HTTEngine/Components/カテゴリ/（データだけ。カテゴリは Add Component のカテゴリと同じ名前。エンジンの Component の保存の項目 EngineComponents.cpp は Components の直下）
・〜System → HTTEngine/Systems/カテゴリ/（Component を読んで処理する係。カテゴリは、主に扱う Component と同じ。鳴っている音・当たりの一覧など、実行中だけの状態もここが持つ）
```

その下の「機能ごと（Light/ にライトの全部）にしないのは〜」の行の後ろに、1行足す。

```
カテゴリで1段分けるのは、Components・Systems のファイルが 15 を超えたため（2026-10-29。Docs/Tasks/FolderSplit.md）。分けても「名前の終わりで Components か Systems かが決まる」はそのまま。
```

---

## 5. 確かめること

1. Visual Studio で `project/HTTEngine.sln` を開き、ソリューション エクスプローラーで HTTEngine → Components・Systems の下にカテゴリのフォルダが出ている。
2. **ビルドが通る**（エラー 0）。sln の構成は Debug と Release の2つで、**sln の Debug はエンジンの Development をビルドする**（sln の構成の対応がそうなっている）。なので Visual Studio では「Debug」と「Release」の2回でよい。
   - エンジンの Debug 構成も確かめたいときは、vcxproj を直接ビルドする（Claude はこれで確かめた）。
3. `git status` で、動かしたファイルが「renamed」になっている（「deleted」と「new file」に分かれていない＝履歴が続く）。
   - 中身を変えていないので、git は名前の変更として扱う。
4. コミットして、`Tools/FolderSplit.ps1` は消す（1回しか使わない。中身はこの md にある）。

### 注意：`project/HTTEngine/Resources` ができたとき（2026-10-30 に直した）
- ビルド後の処理（vcxproj の PostBuildEvent）は、`Resources` を `$(SolutionDir)HTTEngine\Resources\` に xcopy する。ゲームの sln からビルドしたときにゲームの側へ写すための物。
- 最初の行の `if /I "$(SolutionDir)"=="$(ProjectDir)" exit /b 0` で、**エンジンの sln からのビルドでは写さない**ようになっている（2026-10-30 に sln でビルドして、できないことを確かめた）。
- 2026-10-29 にできたのは、Claude が vcxproj を直接ビルドしたとき `SolutionDir` を `project\\`（終わりの `\` が2つ）で渡し、上の比べが合わなかったため。エンジンの作りの問題ではなかった。
- もしできたら、git に入れずにフォルダごと消してよい（中身は `project/Resources` の写し）。

Claude が確かめたこと（2026-10-29、本体の develop 37e57d3 の写しで）：
- 出る物は上の3行のとおり。Components の直下に残ったのは `EngineComponents.cpp` だけ。
- 古いパス（カテゴリの無い形）は、エンジンのコード・vcxproj・Docs のどこにも残っていない。
- エンジンを Debug・Development・Release でビルドして、3つとも通った（sln の Debug・Release と、vcxproj を直接 Debug で）。

---

## 6. ゲーム（AL4_01）への影響

- **今は何も起きない**。AL4_01 のサブモジュールはコミット `58dd708` に止まっていて、こちらの変更を読まない。
- 起きるのは、AL4_01 でサブモジュールを進めたとき（`git submodule update --remote` など）。ゲームの include（**23 ファイル・36 か所**。`#include <HTTEngine/Components/...>` のように山かっこで書いてある）が古いパスのままなので、**ビルドが通らなくなる**（2026-10-30 に写しで確かめた）。
  - そのときは、このスクリプトの「2. パスを書き換える」と同じ置き換えを、AL4_01 の `project/Source` に掛ければ直る（ゲームの CLAUDE.md は「Engine 層を変えない」だが、これはゲーム側の include の直し）。include のほかに直す所は無かった。
- シーンファイル（.scene・.prefab）は Component を**名前**（"Transform" など）で保存しているので、フォルダを動かしても読める。

---

## 決めたこと

| 決めたこと | 理由 |
|---|---|
| 分けるのは Components と Systems だけ | 15 を超えているのはこの2つ（Systems 40・Components 27）。Math と Editor/Windows は 1章の表の理由で今は分けない |
| カテゴリ名は Add Component のカテゴリと同じ | 覚える物が増えない。エディタで見えている分け方のまま、ファイルも探せる |
| System は主に扱う Component と同じカテゴリ | Component と System を行き来するとき、同じ名前のフォルダを開けばよい |
| 手で写さずにスクリプトにする | 66 ファイル・約 300 か所の include。1つでも写し違えるとビルドが通らない |
| filters は UpdateFilters.ps1 に任せる | 前からある道具で、vcxproj の通りに作り直せる。足りなかったフォルダも一緒に直る |
| 作業は本体の sln で、AL4_01 には入れない | ゲームはもう触らない。サブモジュールは止めてあるので壊れない |
