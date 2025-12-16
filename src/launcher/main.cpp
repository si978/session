#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <vector>
#include <set>
#include "resource.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_CONFIG 1002
#define ID_TRAY_OVERLAY_CONFIG 1004
#define ID_TRAY_RELOAD 1003

NOTIFYICONDATAW g_nid = {0};
HWND g_hWnd = nullptr;
bool g_running = true;
std::wstring g_exeDir;
std::wstring g_dllPath;
std::wstring g_configPath;
std::wstring g_overlayConfigPath;
std::vector<std::wstring> g_games;
std::set<DWORD> g_injectedPids;
FILETIME g_lastConfigTime = {0};
CRITICAL_SECTION g_cs;

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : L"";
}

FILETIME GetFileModTime(const std::wstring& path) {
    FILETIME ft = {0};
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        GetFileTime(hFile, nullptr, nullptr, &ft);
        CloseHandle(hFile);
    }
    return ft;
}

std::vector<std::wstring> LoadGameList(const std::wstring& configPath) {
    std::vector<std::wstring> games;
    std::wifstream file(configPath);
    std::wstring line;
    
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(L" \t\r\n");
        size_t end = line.find_last_not_of(L" \t\r\n");
        if (start != std::wstring::npos && end != std::wstring::npos) {
            line = line.substr(start, end - start + 1);
        }
        if (!line.empty() && line[0] != L'#') {
            games.push_back(line);
        }
    }
    return games;
}

void ReloadConfig() {
    EnterCriticalSection(&g_cs);
    g_games = LoadGameList(g_configPath);
    g_lastConfigTime = GetFileModTime(g_configPath);
    LeaveCriticalSection(&g_cs);
}

DWORD FindProcessByName(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName) == 0) {
                    pid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

bool InjectToProcess(DWORD pid, const wchar_t* dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;
    
    size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, dllPathSize,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        CloseHandle(hProcess);
        return false;
    }
    
    WriteProcessMemory(hProcess, pRemoteMem, dllPath, dllPathSize, nullptr);
    
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID pLoadLibrary = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryW");
    
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                         (LPTHREAD_START_ROUTINE)pLoadLibrary,
                                         pRemoteMem, 0, nullptr);
    
    bool success = false;
    if (hThread) {
        WaitForSingleObject(hThread, 5000);
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        success = (exitCode != 0);
        CloseHandle(hThread);
    }
    
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return success;
}

void CreateDefaultConfig(const std::wstring& configPath) {
    std::wofstream file(configPath);
    file << L"# FPS Overlay - Game List\n";
    file << L"# Add one game process name per line\n";
    file << L"# Example:\n";
    file << L"Brawlhalla.exe\n";
    file << L"# HollowKnight.exe\n";
    file << L"# Cuphead.exe\n";
    file.close();
}

void CreateDefaultOverlayConfig(const std::wstring& configPath) {
    std::wofstream file(configPath);
    file << L"; FPS Overlay - Overlay Settings\n";
    file << L"; This file is read by fps_overlay.dll (auto-reloads on change).\n";
    file << L"\n";
    file << L"[Overlay]\n";
    file << L"; 0..1 (window background alpha)\n";
    file << L"Alpha=0.25\n";
    file << L"\n";
    file << L"; 0/1\n";
    file << L"ShowFps=1\n";
    file << L"ShowFrameTime=1\n";
    file << L"\n";
    file << L"; Color thresholds\n";
    file << L"GreenThreshold=60\n";
    file << L"YellowThreshold=30\n";
    file << L"\n";
    file << L"; Font scale (default 1.0)\n";
    file << L"FontScale=1.0\n";
    file << L"\n";
    file << L"; Corner: TopLeft, TopRight, BottomLeft, BottomRight, Custom\n";
    file << L"Corner=TopRight\n";
    file << L"MarginX=8\n";
    file << L"MarginY=8\n";
    file << L"\n";
    file << L"; Used when Corner=Custom\n";
    file << L"X=10\n";
    file << L"Y=10\n";
    file << L"\n";
    file << L"; ToggleKey: F1-F12 (or a VK code number)\n";
    file << L"ToggleKey=F1\n";
    file << L"\n";
    file << L"; 0/1\n";
    file << L"Visible=1\n";
    file.close();
}

void ShowNotification(const wchar_t* title, const wchar_t* msg) {
    g_nid.uFlags = NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title);
    wcscpy_s(g_nid.szInfo, msg);
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void UpdateTrayTip() {
    EnterCriticalSection(&g_cs);
    size_t count = g_games.size();
    LeaveCriticalSection(&g_cs);
    
    swprintf_s(g_nid.szTip, L"FPS Overlay - Monitoring %zu game(s)", count);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

DWORD WINAPI MonitorThread(LPVOID param) {
    int checkCounter = 0;
    
    while (g_running) {
        // Check config file every 5 seconds
        if (++checkCounter >= 5) {
            checkCounter = 0;
            FILETIME currentTime = GetFileModTime(g_configPath);
            if (CompareFileTime(&currentTime, &g_lastConfigTime) != 0) {
                ReloadConfig();
                UpdateTrayTip();
                
                EnterCriticalSection(&g_cs);
                size_t count = g_games.size();
                LeaveCriticalSection(&g_cs);
                
                std::wstring msg = L"Reloaded: " + std::to_wstring(count) + L" game(s)";
                ShowNotification(L"FPS Overlay", msg.c_str());
            }
        }
        
        // Monitor games
        EnterCriticalSection(&g_cs);
        std::vector<std::wstring> games = g_games;
        LeaveCriticalSection(&g_cs);
        
        for (const auto& game : games) {
            DWORD pid = FindProcessByName(game.c_str());
            
            if (pid != 0 && g_injectedPids.find(pid) == g_injectedPids.end()) {
                Sleep(2000);
                
                if (FindProcessByName(game.c_str()) == pid) {
                    if (InjectToProcess(pid, g_dllPath.c_str())) {
                        g_injectedPids.insert(pid);
                        ShowNotification(L"FPS Overlay", (game + L" - Injected! Press F1 to toggle.").c_str());
                    }
                }
            }
        }
        
        // Clean up closed processes
        std::set<DWORD> activePids;
        for (DWORD pid : g_injectedPids) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h) {
                DWORD exitCode;
                if (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE) {
                    activePids.insert(pid);
                }
                CloseHandle(h);
            }
        }
        g_injectedPids = activePids;
        
        Sleep(1000);
    }
    return 0;
}

void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_CONFIG, L"Edit games.txt");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OVERLAY_CONFIG, L"Edit overlay.ini");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RELOAD, L"Reload config");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
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
        case ID_TRAY_EXIT:
            g_running = false;
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        case ID_TRAY_CONFIG:
            ShellExecuteW(nullptr, L"open", L"notepad.exe", g_configPath.c_str(), nullptr, SW_SHOW);
            break;
        case ID_TRAY_OVERLAY_CONFIG:
            ShellExecuteW(nullptr, L"open", L"notepad.exe", g_overlayConfigPath.c_str(), nullptr, SW_SHOW);
            break;
        case ID_TRAY_RELOAD:
            ReloadConfig();
            UpdateTrayTip();
            {
                EnterCriticalSection(&g_cs);
                size_t count = g_games.size();
                LeaveCriticalSection(&g_cs);
                std::wstring msg = L"Reloaded: " + std::to_wstring(count) + L" game(s)";
                ShowNotification(L"FPS Overlay", msg.c_str());
            }
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
    // Single instance check
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"FPSOverlayLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"FPS Overlay is already running!", L"FPS Overlay", MB_ICONINFORMATION);
        return 0;
    }
    
    InitializeCriticalSection(&g_cs);
    
    g_exeDir = GetExeDir();
    g_dllPath = g_exeDir + L"fps_overlay.dll";
    g_configPath = g_exeDir + L"games.txt";
    g_overlayConfigPath = g_exeDir + L"overlay.ini";
    
    // Check DLL
    if (GetFileAttributesW(g_dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"fps_overlay.dll not found!", L"Error", MB_ICONERROR);
        return 1;
    }
    
    // Create config if not exists
    if (GetFileAttributesW(g_configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDefaultConfig(g_configPath);
        MessageBoxW(nullptr, 
            L"games.txt created!\n\nPlease add your game process names (one per line), then restart.", 
            L"FPS Overlay", MB_ICONINFORMATION);
        ShellExecuteW(nullptr, L"open", L"notepad.exe", g_configPath.c_str(), nullptr, SW_SHOW);
        return 0;
    }

    // Create overlay config if not exists
    if (GetFileAttributesW(g_overlayConfigPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDefaultOverlayConfig(g_overlayConfigPath);
    }
    
    // Load games
    g_games = LoadGameList(g_configPath);
    g_lastConfigTime = GetFileModTime(g_configPath);
    
    if (g_games.empty()) {
        MessageBoxW(nullptr, 
            L"No games configured!\n\nPlease add game process names to games.txt", 
            L"FPS Overlay", MB_ICONWARNING);
        ShellExecuteW(nullptr, L"open", L"notepad.exe", g_configPath.c_str(), nullptr, SW_SHOW);
        return 0;
    }
    
    // Load custom icons
    HICON hIconLarge = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), 
                                          IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), 
                                          IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!hIconLarge) hIconLarge = LoadIconW(nullptr, IDI_APPLICATION);
    if (!hIconSmall) hIconSmall = LoadIconW(nullptr, IDI_APPLICATION);
    
    // Create hidden window
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = hIconLarge;
    wc.hIconSm = hIconSmall;
    wc.lpszClassName = L"FPSOverlayClass";
    RegisterClassExW(&wc);
    
    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"FPS Overlay", 0,
                              0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    
    // Create tray icon
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hIconSmall;
    swprintf_s(g_nid.szTip, L"FPS Overlay - Monitoring %zu game(s)", g_games.size());
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    
    // Show startup notification
    std::wstring tipMsg = L"Monitoring " + std::to_wstring(g_games.size()) + L" game(s)";
    ShowNotification(L"FPS Overlay Started", tipMsg.c_str());
    
    // Start monitor thread
    HANDLE hThread = CreateThread(nullptr, 0, MonitorThread, nullptr, 0, nullptr);
    
    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    g_running = false;
    WaitForSingleObject(hThread, 2000);
    CloseHandle(hThread);
    CloseHandle(hMutex);
    DeleteCriticalSection(&g_cs);
    
    return 0;
}
