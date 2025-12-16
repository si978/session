# FPS Overlay - 下载依赖脚本
# 使用方法: 右键 -> 使用 PowerShell 运行

$ErrorActionPreference = "Stop"

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
    $minhookZip = Join-Path $thirdPartyDir "minhook.zip"
    $minhookUrl = "https://github.com/TsudaKageyu/minhook/archive/refs/heads/master.zip"
    
    try {
        Invoke-WebRequest -Uri $minhookUrl -OutFile $minhookZip
        Expand-Archive -Path $minhookZip -DestinationPath $thirdPartyDir -Force
        
        # 重命名目录
        $extractedDir = Join-Path $thirdPartyDir "minhook-master"
        if (Test-Path $extractedDir) {
            if (Test-Path $minhookDir) {
                Remove-Item -Recurse -Force $minhookDir
            }
            Rename-Item -Path $extractedDir -NewName "minhook"
        }
        
        Remove-Item $minhookZip
        Write-Host "      MinHook downloaded successfully!" -ForegroundColor Green
    } catch {
        Write-Host "      Failed to download MinHook: $_" -ForegroundColor Red
        Write-Host "      Please download manually from: https://github.com/TsudaKageyu/minhook" -ForegroundColor Yellow
    }
}

# 下载 ImGui
$imguiDir = Join-Path $thirdPartyDir "imgui"
Write-Host "[2/2] Downloading ImGui..." -ForegroundColor Yellow

if (Test-Path (Join-Path $imguiDir "imgui.cpp")) {
    Write-Host "      ImGui already exists, skipping." -ForegroundColor Green
} else {
    $imguiZip = Join-Path $thirdPartyDir "imgui.zip"
    $imguiUrl = "https://github.com/ocornut/imgui/archive/refs/heads/master.zip"
    
    try {
        Invoke-WebRequest -Uri $imguiUrl -OutFile $imguiZip
        Expand-Archive -Path $imguiZip -DestinationPath $thirdPartyDir -Force
        
        # 重命名目录
        $extractedDir = Join-Path $thirdPartyDir "imgui-master"
        if (Test-Path $extractedDir) {
            if (Test-Path $imguiDir) {
                Remove-Item -Recurse -Force $imguiDir
            }
            Rename-Item -Path $extractedDir -NewName "imgui"
        }
        
        Remove-Item $imguiZip
        Write-Host "      ImGui downloaded successfully!" -ForegroundColor Green
    } catch {
        Write-Host "      Failed to download ImGui: $_" -ForegroundColor Red
        Write-Host "      Please download manually from: https://github.com/ocornut/imgui" -ForegroundColor Yellow
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

Read-Host "Press Enter to exit"
