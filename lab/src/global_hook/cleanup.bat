@echo off
echo ========================================
echo FPS Overlay Cleanup Tool
echo ========================================
echo.

echo Stopping fps_monitor.exe...
taskkill /F /IM fps_monitor.exe 2>nul

echo.
echo Checking processes with fps_hook DLL loaded:
echo.
echo --- 64-bit DLL ---
tasklist /m fps_hook64.dll 2>nul | findstr /v "INFO:"
echo.
echo --- 32-bit DLL ---
tasklist /m fps_hook32.dll 2>nul | findstr /v "INFO:"
echo.

echo ========================================
echo To fully unload the DLL, close the processes listed above.
echo Or restart your computer.
echo ========================================
echo.
pause
