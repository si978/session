# Build script for 32-bit and 64-bit versions
$ErrorActionPreference = "Stop"

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$srcDir = $PSScriptRoot
$outputDir = "$srcDir\v1.3"

# Clean output
Remove-Item -Path $outputDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -Path $outputDir -ItemType Directory -Force | Out-Null

Write-Host "Building 64-bit version..." -ForegroundColor Cyan

# Build 64-bit
$build64 = "$srcDir\build64"
Remove-Item -Path $build64 -Recurse -Force -ErrorAction SilentlyContinue
& $cmake $srcDir -B $build64 -A x64
& $cmake --build $build64 --config Release

# Copy 64-bit files
Copy-Item "$build64\bin\fps_hook.dll" "$outputDir\fps_hook64.dll"
Copy-Item "$build64\bin\fps_monitor.exe" "$outputDir\fps_monitor.exe"

Write-Host "Building 32-bit version..." -ForegroundColor Cyan

# Build 32-bit
$build32 = "$srcDir\build32"
Remove-Item -Path $build32 -Recurse -Force -ErrorAction SilentlyContinue
& $cmake $srcDir -B $build32 -A Win32
& $cmake --build $build32 --config Release

# Copy 32-bit files
Copy-Item "$build32\bin\fps_hook.dll" "$outputDir\fps_hook32.dll"

Write-Host "Build complete!" -ForegroundColor Green
Get-ChildItem $outputDir | Format-Table Name, Length
