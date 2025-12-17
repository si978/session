# FPS Overlay - 下载依赖脚本
# 使用方法: 右键 -> 使用 PowerShell 运行

[CmdletBinding()]
param(
    [switch]$NonInteractive,
    [string]$MinHookRef = "v1.3.3",
    [string]$ImGuiRef = "v1.91.5"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Test-IsCi {
    return $NonInteractive -or ($env:GITHUB_ACTIONS -eq "true") -or ($env:CI -eq "true")
}

function Copy-Directory {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$DestinationParentDir
    )

    if (-not (Test-Path $SourceDir)) {
        throw "Missing source directory: $SourceDir"
    }

    Copy-Item -Recurse -Force -Path $SourceDir -Destination $DestinationParentDir
}

function Remove-IfExists {
    param([Parameter(Mandatory = $true)][string]$PathToRemove)

    if (Test-Path $PathToRemove) {
        Remove-Item -Recurse -Force -Path $PathToRemove
    }
}

function Copy-FileIfExists {
    param(
        [Parameter(Mandatory = $true)][string]$SourceFile,
        [Parameter(Mandatory = $true)][string]$DestinationDir
    )

    if (Test-Path $SourceFile) {
        Copy-Item -Force -Path $SourceFile -Destination $DestinationDir
    }
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$thirdPartyDir = Join-Path $projectRoot "third_party"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "       FPS Overlay - Download Dependencies" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 创建目录
if (-not (Test-Path $thirdPartyDir)) {
    New-Item -ItemType Directory -Path $thirdPartyDir | Out-Null
}

# 下载 MinHook
$minhookDir = Join-Path $thirdPartyDir "minhook"
Write-Host "[1/2] Downloading MinHook..." -ForegroundColor Yellow

if (Test-Path (Join-Path $minhookDir "src\hook.c")) {
    Write-Host "      MinHook already exists, skipping." -ForegroundColor Green
} else {
    $tmpRoot = Join-Path $env:TEMP ("fps-overlay-deps-" + [Guid]::NewGuid().ToString("N"))
    $minhookTmp = Join-Path $tmpRoot "minhook"
    $minhookZip = Join-Path $tmpRoot "minhook.zip"
    $minhookUrl = "https://github.com/TsudaKageyu/minhook/archive/refs/tags/$MinHookRef.zip"
    
    try {
        New-Item -ItemType Directory -Path $minhookTmp -Force | Out-Null

        Invoke-WebRequest -Uri $minhookUrl -OutFile $minhookZip
        Expand-Archive -Path $minhookZip -DestinationPath $minhookTmp -Force

        $extractedDir = Get-ChildItem -Path $minhookTmp -Directory | Where-Object { $_.Name -like "minhook-*" } | Select-Object -First 1
        if (-not $extractedDir) {
            throw "Failed to locate extracted MinHook directory under: $minhookTmp"
        }

        if (-not (Test-Path $minhookDir)) {
            New-Item -ItemType Directory -Path $minhookDir -Force | Out-Null
        }

        Remove-IfExists (Join-Path $minhookDir "include")
        Remove-IfExists (Join-Path $minhookDir "src")

        Copy-Directory -SourceDir (Join-Path $extractedDir.FullName "include") -DestinationParentDir $minhookDir
        Copy-Directory -SourceDir (Join-Path $extractedDir.FullName "src") -DestinationParentDir $minhookDir

        Copy-FileIfExists -SourceFile (Join-Path $extractedDir.FullName "LICENSE") -DestinationDir $minhookDir
        Copy-FileIfExists -SourceFile (Join-Path $extractedDir.FullName "LICENSE.txt") -DestinationDir $minhookDir
        Copy-FileIfExists -SourceFile (Join-Path $extractedDir.FullName "LICENSE.md") -DestinationDir $minhookDir

        Write-Host "      MinHook downloaded successfully!" -ForegroundColor Green
    } catch {
        Write-Host "      Failed to download MinHook: $_" -ForegroundColor Red
        Write-Host "      Please download manually from: https://github.com/TsudaKageyu/minhook" -ForegroundColor Yellow
        if (Test-IsCi) { throw }
    } finally {
        if (Test-Path $tmpRoot) { Remove-Item -Recurse -Force $tmpRoot }
    }
}

# 下载 ImGui
$imguiDir = Join-Path $thirdPartyDir "imgui"
Write-Host "[2/2] Downloading ImGui..." -ForegroundColor Yellow

if (Test-Path (Join-Path $imguiDir "imgui.cpp")) {
    Write-Host "      ImGui already exists, skipping." -ForegroundColor Green
} else {
    $tmpRoot = Join-Path $env:TEMP ("fps-overlay-deps-" + [Guid]::NewGuid().ToString("N"))
    $imguiTmp = Join-Path $tmpRoot "imgui"
    $imguiZip = Join-Path $tmpRoot "imgui.zip"
    $imguiUrl = "https://github.com/ocornut/imgui/archive/refs/tags/$ImGuiRef.zip"
    
    try {
        New-Item -ItemType Directory -Path $imguiTmp -Force | Out-Null

        Invoke-WebRequest -Uri $imguiUrl -OutFile $imguiZip
        Expand-Archive -Path $imguiZip -DestinationPath $imguiTmp -Force

        $extractedDir = Get-ChildItem -Path $imguiTmp -Directory | Where-Object { $_.Name -like "imgui-*" } | Select-Object -First 1
        if (-not $extractedDir) {
            throw "Failed to locate extracted ImGui directory under: $imguiTmp"
        }

        if (-not (Test-Path $imguiDir)) {
            New-Item -ItemType Directory -Path $imguiDir -Force | Out-Null
        }

        $backendsDst = Join-Path $imguiDir "backends"
        Remove-IfExists $backendsDst

        Get-ChildItem -Path $imguiDir -File -Include "imgui*.cpp","imgui*.h","imconfig.h","imstb_*.h","LICENSE*","COPYING*" -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue

        $coreFiles = @(
            "imgui.cpp",
            "imgui.h",
            "imgui_demo.cpp",
            "imgui_draw.cpp",
            "imgui_internal.h",
            "imgui_tables.cpp",
            "imgui_widgets.cpp",
            "imconfig.h",
            "imstb_rectpack.h",
            "imstb_textedit.h",
            "imstb_truetype.h"
        )
        foreach ($f in $coreFiles) {
            Copy-Item -Force -Path (Join-Path $extractedDir.FullName $f) -Destination $imguiDir
        }

        Copy-Directory -SourceDir (Join-Path $extractedDir.FullName "backends") -DestinationParentDir $imguiDir

        Copy-FileIfExists -SourceFile (Join-Path $extractedDir.FullName "LICENSE") -DestinationDir $imguiDir
        Copy-FileIfExists -SourceFile (Join-Path $extractedDir.FullName "LICENSE.txt") -DestinationDir $imguiDir
        Copy-FileIfExists -SourceFile (Join-Path $extractedDir.FullName "LICENSE.md") -DestinationDir $imguiDir

        Write-Host "      ImGui downloaded successfully!" -ForegroundColor Green
    } catch {
        Write-Host "      Failed to download ImGui: $_" -ForegroundColor Red
        Write-Host "      Please download manually from: https://github.com/ocornut/imgui" -ForegroundColor Yellow
        if (Test-IsCi) { throw }
    } finally {
        if (Test-Path $tmpRoot) { Remove-Item -Recurse -Force $tmpRoot }
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Download complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. mkdir build && cd build" -ForegroundColor White
Write-Host "  2. cmake .." -ForegroundColor White
Write-Host "  3. cmake --build . --config Release" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Cyan

if (-not (Test-IsCi)) {
    Read-Host "Press Enter to exit"
}
