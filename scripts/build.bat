@echo off
REM FPS Overlay - 构建脚本

echo ========================================
echo       FPS Overlay - Build
echo ========================================
echo.

cd /d "%~dp0.."

REM 检查 build 目录
if not exist build (
    echo [INFO] Creating build directory...
    mkdir build
)

cd build

REM 运行 CMake
echo [INFO] Running CMake...
cmake .. -A x64
if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    pause
    exit /b 1
)

REM 编译
echo.
echo [INFO] Building Release...
cmake --build . --config Release
if errorlevel 1 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo [SUCCESS] Build completed!
echo.
echo Output files:
echo   build\bin\Release\fps_overlay.dll
echo   build\bin\Release\injector.exe
echo ========================================
echo.

pause
