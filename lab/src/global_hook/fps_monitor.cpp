#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <shlobj.h>
#include "fps_config.h"

#pragma comment(lib, "shell32.lib")

typedef HHOOK (*PFN_InstallGlobalHook)();
typedef void (*PFN_RemoveGlobalHook)();

// Heartbeat for hook cleanup
#define HEARTBEAT_SHARED_NAME L"FpsOverlayHeartbeat"
static HANDLE g_hHeartbeat = NULL;

// Menu IDs
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_TOGGLE 1001
#define ID_TRAY_POS_TL 1010
#define ID_TRAY_POS_TR 1011
#define ID_TRAY_POS_BL 1012
#define ID_TRAY_POS_BR 1013
#define ID_TRAY_SIZE_S 1020
#define ID_TRAY_SIZE_M 1021
#define ID_TRAY_SIZE_L 1022
#define ID_TRAY_EDIT_CONFIG 1030
#define ID_TRAY_RELOAD 1031
#define ID_TRAY_AUTOSTART 1040
#define ID_TRAY_ABOUT 1050
#define ID_TRAY_EXIT 1099

static HMODULE g_hHookDll64 = NULL;
static HMODULE g_hHookDll32 = NULL;
static HHOOK g_hHook64 = NULL;
static HHOOK g_hHook32 = NULL;
static HWND g_hWnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static bool g_running = true;

// Shared memory for config
static HANDLE g_hConfigMap = NULL;
static FpsConfig* g_pConfig = NULL;

// Config file path
static std::wstring g_configPath;

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : L"";
}

bool InitSharedConfig() {
    g_hConfigMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                       0, sizeof(FpsConfig), CONFIG_SHARED_NAME);
    if (!g_hConfigMap) return false;
    
    bool isNew = (GetLastError() != ERROR_ALREADY_EXISTS);
    
    g_pConfig = (FpsConfig*)MapViewOfFile(g_hConfigMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FpsConfig));
    if (!g_pConfig) {
        CloseHandle(g_hConfigMap);
        return false;
    }
    
    if (isNew) {
        InitDefaultConfig(g_pConfig);
    }
    
    return true;
}

void CleanupSharedConfig() {
    if (g_pConfig) UnmapViewOfFile(g_pConfig);
    if (g_hConfigMap) CloseHandle(g_hConfigMap);
}

int GetPrivateProfileIntDefault(const wchar_t* section, const wchar_t* key, int def, const wchar_t* file) {
    return GetPrivateProfileIntW(section, key, def, file);
}

DWORD ParseColor(const wchar_t* str) {
    if (!str || !*str) return 0;
    return wcstoul(str, NULL, 16);
}

void LoadConfig() {
    if (!g_pConfig) return;
    
    g_configPath = GetExeDir() + CONFIG_FILE_NAME;
    
    // Check if file exists
    if (GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Create default config file
        WritePrivateProfileStringW(L"Display", L"Position", L"TopRight", g_configPath.c_str());
        WritePrivateProfileStringW(L"Display", L"OffsetX", L"10", g_configPath.c_str());
        WritePrivateProfileStringW(L"Display", L"OffsetY", L"10", g_configPath.c_str());
        WritePrivateProfileStringW(L"Display", L"FontSize", L"14", g_configPath.c_str());
        WritePrivateProfileStringW(L"Display", L"ShowBackground", L"true", g_configPath.c_str());
        
        WritePrivateProfileStringW(L"Colors", L"HighFPS", L"FF00E070", g_configPath.c_str());
        WritePrivateProfileStringW(L"Colors", L"MediumFPS", L"FFFFCC00", g_configPath.c_str());
        WritePrivateProfileStringW(L"Colors", L"LowFPS", L"FFFF4040", g_configPath.c_str());
        WritePrivateProfileStringW(L"Colors", L"Background", L"B0202020", g_configPath.c_str());
        
        WritePrivateProfileStringW(L"Hotkey", L"Toggle", L"F1", g_configPath.c_str());
        
        WritePrivateProfileStringW(L"Filter", L"Mode", L"All", g_configPath.c_str());
        WritePrivateProfileStringW(L"Filter", L"Games", L"", g_configPath.c_str());
        
        return;
    }
    
    // Load config
    wchar_t buf[256];
    
    // Position
    GetPrivateProfileStringW(L"Display", L"Position", L"TopRight", buf, 256, g_configPath.c_str());
    if (_wcsicmp(buf, L"TopLeft") == 0) g_pConfig->position = POS_TOP_LEFT;
    else if (_wcsicmp(buf, L"TopRight") == 0) g_pConfig->position = POS_TOP_RIGHT;
    else if (_wcsicmp(buf, L"BottomLeft") == 0) g_pConfig->position = POS_BOTTOM_LEFT;
    else if (_wcsicmp(buf, L"BottomRight") == 0) g_pConfig->position = POS_BOTTOM_RIGHT;
    
    g_pConfig->offsetX = GetPrivateProfileIntW(L"Display", L"OffsetX", 10, g_configPath.c_str());
    g_pConfig->offsetY = GetPrivateProfileIntW(L"Display", L"OffsetY", 10, g_configPath.c_str());
    g_pConfig->fontSize = GetPrivateProfileIntW(L"Display", L"FontSize", 14, g_configPath.c_str());
    
    GetPrivateProfileStringW(L"Display", L"ShowBackground", L"true", buf, 256, g_configPath.c_str());
    g_pConfig->showBackground = (_wcsicmp(buf, L"true") == 0 || _wcsicmp(buf, L"1") == 0);
    
    // Colors
    GetPrivateProfileStringW(L"Colors", L"HighFPS", L"FF00E070", buf, 256, g_configPath.c_str());
    g_pConfig->colorHigh = ParseColor(buf);
    GetPrivateProfileStringW(L"Colors", L"MediumFPS", L"FFFFCC00", buf, 256, g_configPath.c_str());
    g_pConfig->colorMedium = ParseColor(buf);
    GetPrivateProfileStringW(L"Colors", L"LowFPS", L"FFFF4040", buf, 256, g_configPath.c_str());
    g_pConfig->colorLow = ParseColor(buf);
    GetPrivateProfileStringW(L"Colors", L"Background", L"B0202020", buf, 256, g_configPath.c_str());
    g_pConfig->colorBackground = ParseColor(buf);
    
    // Hotkey
    GetPrivateProfileStringW(L"Hotkey", L"Toggle", L"F1", buf, 256, g_configPath.c_str());
    if (_wcsicmp(buf, L"F1") == 0) g_pConfig->toggleKey = VK_F1;
    else if (_wcsicmp(buf, L"F2") == 0) g_pConfig->toggleKey = VK_F2;
    else if (_wcsicmp(buf, L"F3") == 0) g_pConfig->toggleKey = VK_F3;
    else if (_wcsicmp(buf, L"F4") == 0) g_pConfig->toggleKey = VK_F4;
    else if (_wcsicmp(buf, L"F5") == 0) g_pConfig->toggleKey = VK_F5;
    else if (_wcsicmp(buf, L"F6") == 0) g_pConfig->toggleKey = VK_F6;
    else if (_wcsicmp(buf, L"F7") == 0) g_pConfig->toggleKey = VK_F7;
    else if (_wcsicmp(buf, L"F8") == 0) g_pConfig->toggleKey = VK_F8;
    else if (_wcsicmp(buf, L"F9") == 0) g_pConfig->toggleKey = VK_F9;
    else if (_wcsicmp(buf, L"F10") == 0) g_pConfig->toggleKey = VK_F10;
    else if (_wcsicmp(buf, L"F11") == 0) g_pConfig->toggleKey = VK_F11;
    else if (_wcsicmp(buf, L"F12") == 0) g_pConfig->toggleKey = VK_F12;
    else g_pConfig->toggleKey = VK_F1;
    
    // Filter
    GetPrivateProfileStringW(L"Filter", L"Mode", L"All", buf, 256, g_configPath.c_str());
    if (_wcsicmp(buf, L"Whitelist") == 0) g_pConfig->filterMode = FILTER_WHITELIST;
    else if (_wcsicmp(buf, L"Blacklist") == 0) g_pConfig->filterMode = FILTER_BLACKLIST;
    else g_pConfig->filterMode = FILTER_ALL;
    
    char gameListA[4096];
    char configPathA[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, g_configPath.c_str(), -1, configPathA, MAX_PATH, NULL, NULL);
    GetPrivateProfileStringA("Filter", "Games", "", gameListA, 4096, configPathA);
    strcpy_s(g_pConfig->gameList, gameListA);
}

void SaveConfig() {
    if (!g_pConfig) return;
    
    // Position
    const wchar_t* posStr[] = {L"TopLeft", L"TopRight", L"BottomLeft", L"BottomRight"};
    WritePrivateProfileStringW(L"Display", L"Position", posStr[g_pConfig->position], g_configPath.c_str());
    
    wchar_t buf[64];
    swprintf_s(buf, L"%d", g_pConfig->offsetX);
    WritePrivateProfileStringW(L"Display", L"OffsetX", buf, g_configPath.c_str());
    swprintf_s(buf, L"%d", g_pConfig->offsetY);
    WritePrivateProfileStringW(L"Display", L"OffsetY", buf, g_configPath.c_str());
    swprintf_s(buf, L"%d", g_pConfig->fontSize);
    WritePrivateProfileStringW(L"Display", L"FontSize", buf, g_configPath.c_str());
}

bool LoadHookDll() {
    std::wstring exeDir = GetExeDir();
    bool anyLoaded = false;
    
    // Create heartbeat event
    g_hHeartbeat = CreateEventW(NULL, TRUE, TRUE, HEARTBEAT_SHARED_NAME);
    
    // Try to load 64-bit DLL
    std::wstring dll64Path = exeDir + L"fps_hook64.dll";
    if (GetFileAttributesW(dll64Path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_hHookDll64 = LoadLibraryW(dll64Path.c_str());
        if (g_hHookDll64) {
            PFN_InstallGlobalHook pfnInstall = (PFN_InstallGlobalHook)GetProcAddress(g_hHookDll64, "InstallGlobalHook");
            if (pfnInstall) {
                g_hHook64 = pfnInstall();
                if (g_hHook64) anyLoaded = true;
            }
        }
    }
    
    // Try to load 32-bit DLL (for 32-bit games)
    std::wstring dll32Path = exeDir + L"fps_hook32.dll";
    if (GetFileAttributesW(dll32Path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_hHookDll32 = LoadLibraryW(dll32Path.c_str());
        if (g_hHookDll32) {
            PFN_InstallGlobalHook pfnInstall = (PFN_InstallGlobalHook)GetProcAddress(g_hHookDll32, "InstallGlobalHook");
            if (pfnInstall) {
                g_hHook32 = pfnInstall();
                if (g_hHook32) anyLoaded = true;
            }
        }
    }
    
    // Fallback: try fps_hook.dll (single file mode)
    if (!anyLoaded) {
        std::wstring dllPath = exeDir + L"fps_hook.dll";
        g_hHookDll64 = LoadLibraryW(dllPath.c_str());
        if (g_hHookDll64) {
            PFN_InstallGlobalHook pfnInstall = (PFN_InstallGlobalHook)GetProcAddress(g_hHookDll64, "InstallGlobalHook");
            if (pfnInstall) {
                g_hHook64 = pfnInstall();
                if (g_hHook64) anyLoaded = true;
            }
        }
    }
    
    if (!anyLoaded) {
        MessageBoxW(NULL, L"Failed to load hook DLL", L"Error", MB_ICONERROR);
        return false;
    }
    
    return true;
}

void UnloadHookDll() {
    // Just close heartbeat - DLLs will detect this and stop rendering
    // DO NOT call UnhookWindowsHookEx - it causes DLL unload from all processes = crash!
    // DO NOT call FreeLibrary - same reason
    // The hooks and DLLs will remain active but rendering stops
    // Everything cleans up naturally when game exits
    
    if (g_hHeartbeat) {
        CloseHandle(g_hHeartbeat);
        g_hHeartbeat = NULL;
    }
    
    // Keep hooks installed - don't touch them!
    // g_hHook64 and g_hHook32 stay as-is
}

bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
                       0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    
    wchar_t path[MAX_PATH];
    DWORD size = sizeof(path);
    bool exists = (RegQueryValueExW(hKey, L"FPSOverlay", NULL, NULL, (BYTE*)path, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists;
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                       0, KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    
    if (enable) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        RegSetValueExW(hKey, L"FPSOverlay", 0, REG_SZ, (BYTE*)path, (DWORD)(wcslen(path) + 1) * 2);
    } else {
        RegDeleteValueW(hKey, L"FPSOverlay");
    }
    RegCloseKey(hKey);
}

void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    HMENU hPosMenu = CreatePopupMenu();
    HMENU hSizeMenu = CreatePopupMenu();
    
    // Toggle
    AppendMenuW(hMenu, g_pConfig->visible ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_TOGGLE, L"Show FPS");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    // Position submenu
    AppendMenuW(hPosMenu, g_pConfig->position == 0 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_POS_TL, L"Top Left");
    AppendMenuW(hPosMenu, g_pConfig->position == 1 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_POS_TR, L"Top Right");
    AppendMenuW(hPosMenu, g_pConfig->position == 2 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_POS_BL, L"Bottom Left");
    AppendMenuW(hPosMenu, g_pConfig->position == 3 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_POS_BR, L"Bottom Right");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPosMenu, L"Position");
    
    // Size submenu
    AppendMenuW(hSizeMenu, g_pConfig->fontSize == 12 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_SIZE_S, L"Small");
    AppendMenuW(hSizeMenu, g_pConfig->fontSize == 14 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_SIZE_M, L"Medium");
    AppendMenuW(hSizeMenu, g_pConfig->fontSize == 18 ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_SIZE_L, L"Large");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSizeMenu, L"Size");
    
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EDIT_CONFIG, L"Edit Config");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RELOAD, L"Reload Config");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED, ID_TRAY_AUTOSTART, L"Start with Windows");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ABOUT, L"About");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            ShowContextMenu(hWnd);
        }
        return 0;
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_TOGGLE:
            g_pConfig->visible = !g_pConfig->visible;
            break;
        case ID_TRAY_POS_TL: g_pConfig->position = 0; SaveConfig(); break;
        case ID_TRAY_POS_TR: g_pConfig->position = 1; SaveConfig(); break;
        case ID_TRAY_POS_BL: g_pConfig->position = 2; SaveConfig(); break;
        case ID_TRAY_POS_BR: g_pConfig->position = 3; SaveConfig(); break;
        case ID_TRAY_SIZE_S: g_pConfig->fontSize = 12; SaveConfig(); break;
        case ID_TRAY_SIZE_M: g_pConfig->fontSize = 14; SaveConfig(); break;
        case ID_TRAY_SIZE_L: g_pConfig->fontSize = 18; SaveConfig(); break;
        case ID_TRAY_EDIT_CONFIG:
            ShellExecuteW(NULL, L"open", g_configPath.c_str(), NULL, NULL, SW_SHOW);
            break;
        case ID_TRAY_RELOAD:
            LoadConfig();
            break;
        case ID_TRAY_AUTOSTART:
            SetAutoStart(!IsAutoStartEnabled());
            break;
        case ID_TRAY_ABOUT:
            MessageBoxW(NULL, L"FPS Overlay v1.2\n\nGlobal FPS display for all games.\n\nPress F1 to toggle.", 
                        L"About", MB_ICONINFORMATION);
            break;
        case ID_TRAY_EXIT:
            g_running = false;
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        }
        return 0;
        
    case WM_DESTROY:
        g_running = false;
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"FPSOverlayGlobalHook");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"FPS Overlay is already running!", L"FPS Overlay", MB_ICONINFORMATION);
        return 0;
    }
    
    // Init shared config
    if (!InitSharedConfig()) {
        MessageBoxW(NULL, L"Failed to init shared config", L"FPS Overlay Error", MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }
    
    // Load config
    LoadConfig();
    
    // Load hook
    if (!LoadHookDll()) {
        CleanupSharedConfig();
        CloseHandle(hMutex);
        return 1;
    }
    
    // Create window
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FPSOverlayClass";
    RegisterClassExW(&wc);
    
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"FPS Overlay", 0,
                              0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    
    // Tray icon
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"FPS Overlay - Right click for options");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // Cleanup
    UnloadHookDll();
    CleanupSharedConfig();
    CloseHandle(hMutex);
    
    return 0;
}
