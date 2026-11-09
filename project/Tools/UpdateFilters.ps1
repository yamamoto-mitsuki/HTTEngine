<#
=============================================================================
UpdateFilters.ps1
vcxproj に入っているファイルのフォルダ構成に合わせて、フィルタを作り直す。

使い方（project フォルダで）：
powershell -ExecutionPolicy Bypass -File Tools\UpdateFilters.ps1 HTTEngine.vcxproj
=============================================================================
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath
)

# ===== 読む =====
$projectFile = (Resolve-Path $ProjectPath).Path
$filtersFile = "$projectFile.filters"
[xml]$project = [System.IO.File]::ReadAllText($projectFile)

# 今のフィルタのGUIDを覚えておく
$oldGuids = @{}
if (Test-Path $filtersFile) {
    [xml]$oldFilters = [System.IO.File]::ReadAllText($filtersFile)
    foreach ($node in $oldFilters.GetElementsByTagName("Filter")) {
        $name = $node.GetAttribute("Include")
        if ($name) {
            $oldGuids[$name] = $node.UniqueIdentifier
        }
    }
}

# ===== ファイルとフォルダを集める =====
$skipKinds = @("ProjectConfiguration", "ProjectReference")
$items = New-Object System.Collections.Generic.List[object]
$folders = @{}

foreach ($group in $project.Project.ItemGroup) {
    foreach ($node in $group.ChildNodes) {
        if ($node.NodeType -ne "Element" -or $skipKinds -contains $node.LocalName) {
            continue
        }

        $path = $node.GetAttribute("Include")
        if (-not $path) {
            continue
        }

        $folder = Split-Path $path -Parent
        $items.Add([pscustomobject]@{
            Kind   = $node.LocalName
            Path   = $path
            Folder = $folder
        })

        while ($folder) {
            $folders[$folder] = $true
            $folder = Split-Path $folder -Parent
        }
    }
}

# ===== 書く =====
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('<?xml version="1.0" encoding="utf-8"?>')
$lines.Add('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')

# フィルタ（フォルダと同じ名前）
$lines.Add('  <ItemGroup>')
foreach ($folder in ($folders.Keys | Sort-Object)) {
    $guid = if ($oldGuids.ContainsKey($folder)) {
        $oldGuids[$folder]
    } else {
        "{$([guid]::NewGuid())}"
    }

    $lines.Add("    <Filter Include=""$folder"">")
    $lines.Add("      <UniqueIdentifier>$guid</UniqueIdentifier>")
    $lines.Add('    </Filter>')
}
$lines.Add('  </ItemGroup>')

# ファイル（種類ごとにまとめる）
foreach ($kindGroup in ($items | Group-Object Kind | Sort-Object Name)) {
    $lines.Add('  <ItemGroup>')

    foreach ($item in ($kindGroup.Group | Sort-Object Path)) {
        if ($item.Folder) {
            $lines.Add("    <$($item.Kind) Include=""$($item.Path)"">")
            $lines.Add("      <Filter>$($item.Folder)</Filter>")
            $lines.Add("    </$($item.Kind)>")
        } else {
            $lines.Add("    <$($item.Kind) Include=""$($item.Path)"" />")
        }
    }

    $lines.Add('  </ItemGroup>')
}

$lines.Add('</Project>')

# UTF-8 BOM付き、改行はCRLFで保存
$utf8WithBom = New-Object System.Text.UTF8Encoding($true)
[System.IO.File]::WriteAllText(
    $filtersFile,
    ($lines -join "`r`n") + "`r`n",
    $utf8WithBom
)

Write-Host "$($items.Count) files, $($folders.Count) filters -> $filtersFile"
