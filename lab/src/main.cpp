#include <Windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <Psapi.h>
#include <string>
#include <fstream>
#include <vector>
#include <set>
#include "overlay_window.h"

#pragma comment(lib, "psapi.lib")

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_CONFIG 1002
#define ID_TRAY_TOGGLE 1003

// Log file for debugging
static std::wstring g_logPath;

void Log(const wchar_t* fmt, ...) {
    if (g_logPath.empty()) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        g_logPath = path;
        g_logPath = g_logPath.substr(0, g_logPath.rfind(L'\\') + 1) + L"fps_monitor.log";
        DeleteFileW(g_logPath.c_str());
    }
    
    FILE* f = nullptr;
    _wfopen_s(&f, g_logPath.c_str(), L"a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfwprintf(f, fmt, args);
        fwprintf(f, L"\n");
        fclose(f);
        va_end(args);
    }
}

struct AppState {
    HWND hWnd = nullptr;
    NOTIFYICONDATAW nid = {0};
    bool running = true;
    
    std::wstring exeDir;
    std::wstring configPath;
    std::vector<std::wstring> games;
    
    OverlayWindow* overlay = nullptr;
    HWND targetGameWindow = nullptr;
    DWORD targetPid = 0;
    bool overlayVisible = true;
    std::wstring currentGame;
};

static AppState g_app;

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : L"";
}

std::vector<std::wstring> LoadGameList(const std::wstring& configPath) {
    std::vector<std::wstring> games;
    Log(L"Loading config from: %s", configPath.c_str());
    
    FILE* f = nullptr;
    _wfopen_s(&f, configPath.c_str(), L"r, ccs=UTF-8");
    if (!f) {
        Log(L"Failed to open config file!");
        return games;
    }
    
    wchar_t line[512];
    while (fgetws(line, 512, f)) {
        std::wstring wline(line);
        
        // Trim
        size_t start = wline.find_first_not_of(L" \t\r\n");
        size_t end = wline.find_last_not_of(L" \t\r\n");
        if (start != std::wstring::npos && end != std::wstring::npos) {
            wline = wline.substr(start, end - start + 1);
        } else {
            continue;
        }
        
        if (!wline.empty() && wline[0] != L'#') {
            games.push_back(wline);
            Log(L"Loaded game: %s", wline.c_str());
        }
    }
    fclose(f);
    
    Log(L"Total games loaded: %zu", games.size());
    return games;
}

void CreateDefaultConfig(const std::wstring& configPath) {
    std::wofstream file(configPath);
    file << L"# FPS Monitor - External Mode\n";
    file << L"# Add game process names (one per line)\n";
    file << L"\n";
    file << L"Brawlhalla.exe\n";
    file << L"LeagueofLegends.exe\n";
    file.close();
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

struct EnumWindowData {
    DWORD pid;
    HWND hWnd;
    wchar_t title[256];
};

BOOL CALLBACK EnumWindowProc(HWND hWnd, LPARAM lParam) {
    EnumWindowData* data = (EnumWindowData*)lParam;
    DWORD windowPid;
    GetWindowThreadProcessId(hWnd, &windowPid);
    
    if (windowPid == data->pid && IsWindowVisible(hWnd)) {
        LONG style = GetWindowLong(hWnd, GWL_STYLE);
        LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        
        // Skip tool windows and child windows
        if ((exStyle & WS_EX_TOOLWINDOW) || (style & WS_CHILD)) {
            return TRUE;
        }
        
        // Check if it has a title
        wchar_t title[256] = {0};
        GetWindowTextW(hWnd, title, 256);
        
        if (wcslen(title) > 0) {
            RECT rect;
            GetWindowRect(hWnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            
            // Only consider windows larger than 400x300
            if (width > 400 && height > 300) {
                data->hWnd = hWnd;
                wcscpy_s(data->title, title);
                Log(L"Found window: %s (%dx%d)", title, width, height);
                return FALSE;
            }
        }
    }
    return TRUE;
}

HWND FindMainWindow(DWORD pid) {
    EnumWindowData data = { pid, nullptr, {0} };
    EnumWindows(EnumWindowProc, (LPARAM)&data);
    return data.hWnd;
}

void ShowNotification(const wchar_t* title, const wchar_t* msg) {
    g_app.nid.uFlags = NIF_INFO;
    wcscpy_s(g_app.nid.szInfoTitle, title);
    wcscpy_s(g_app.nid.szInfo, msg);
    g_app.nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
    Log(L"Notification: %s - %s", title, msg);
}

void StartMonitoring(DWORD pid, HWND gameWindow, const std::wstring& gameName) {
    Log(L"StartMonitoring: PID=%d, HWND=%p, Game=%s", pid, gameWindow, gameName.c_str());
    
    if (g_app.targetPid == pid) {
        Log(L"Already monitoring this PID");
        return;
    }
    
    // Stop previous monitoring
    if (g_app.overlay) {
        delete g_app.overlay;
        g_app.overlay = nullptr;
    }
    
    g_app.targetPid = pid;
    g_app.targetGameWindow = gameWindow;
    g_app.currentGame = gameName;
    
    // Create overlay
    g_app.overlay = new OverlayWindow();
    if (!g_app.overlay->Create(gameWindow)) {
        Log(L"Failed to create overlay window!");
        delete g_app.overlay;
        g_app.overlay = nullptr;
        return;
    }
    
    Log(L"Overlay created successfully");
    
    // For now, use a simple estimation (will improve later)
    g_app.overlay->StartFpsCounter(pid);
}

void StopMonitoring() {
    Log(L"StopMonitoring");
    if (g_app.overlay) {
        delete g_app.overlay;
        g_app.overlay = nullptr;
    }
    g_app.targetPid = 0;
    g_app.targetGameWindow = nullptr;
    g_app.currentGame.clear();
}

DWORD WINAPI MonitorThread(LPVOID param) {
    Log(L"MonitorThread started");
    std::set<DWORD> monitoredPids;
    
    while (g_app.running) {
        // Check for games
        for (const auto& game : g_app.games) {
            DWORD pid = FindProcessByName(game.c_str());
            
            if (pid != 0 && monitoredPids.find(pid) == monitoredPids.end()) {
                Log(L"Found process: %s (PID: %d)", game.c_str(), pid);
                Sleep(3000); // Wait for game to initialize
                
                pid = FindProcessByName(game.c_str());
                if (pid != 0) {
                    HWND gameWindow = FindMainWindow(pid);
                    Log(L"Game window search result: %p", gameWindow);
                    
                    if (gameWindow) {
                        StartMonitoring(pid, gameWindow, game);
                        monitoredPids.insert(pid);
                        ShowNotification(L"FPS Monitor", 
                            (game + L" - Monitoring started").c_str());
                    } else {
                        Log(L"Could not find game window for PID %d", pid);
                    }
                }
            }
        }
        
        // Update overlay position
        if (g_app.overlay && g_app.targetGameWindow) {
            if (!IsWindow(g_app.targetGameWindow)) {
                Log(L"Game window closed");
                StopMonitoring();
                monitoredPids.clear();
            } else {
                g_app.overlay->UpdatePosition();
            }
        }
        
        // Clean up closed processes
        std::set<DWORD> activePids;
        for (DWORD pid : monitoredPids) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h) {
                DWORD exitCode;
                if (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE) {
                    activePids.insert(pid);
                }
                CloseHandle(h);
            }
        }
        
        if (monitoredPids.size() != activePids.size()) {
            Log(L"Some processes closed");
        }
        monitoredPids = activePids;
        
        if (g_app.targetPid != 0 && activePids.find(g_app.targetPid) == activePids.end()) {
            StopMonitoring();
        }
        
        Sleep(500);
    }
    
    StopMonitoring();
    Log(L"MonitorThread ended");
    return 0;
}

void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_CONFIG, L"Edit games.txt");
    AppendMenuW(hMenu, MF_STRING | (g_app.overlayVisible ? MF_CHECKED : 0), 
                ID_TRAY_TOGGLE, L"Show Overlay");
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
            g_app.running = false;
            Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
            PostQuitMessage(0);
            break;
        case ID_TRAY_CONFIG:
            ShellExecuteW(nullptr, L"open", L"notepad.exe", 
                          g_app.configPath.c_str(), nullptr, SW_SHOW);
            break;
        case ID_TRAY_TOGGLE:
            g_app.overlayVisible = !g_app.overlayVisible;
            if (g_app.overlay) {
                g_app.overlay->SetVisible(g_app.overlayVisible);
            }
            break;
        }
        return 0;
        
    case WM_DESTROY:
        g_app.running = false;
        Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    Log(L"=== FPS Monitor Starting ===");
    
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"FPSMonitorExternal");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"FPS Monitor is already running!", L"FPS Monitor", MB_ICONINFORMATION);
        return 0;
    }
    
    g_app.exeDir = GetExeDir();
    g_app.configPath = g_app.exeDir + L"games.txt";
    Log(L"Config path: %s", g_app.configPath.c_str());
    
    if (GetFileAttributesW(g_app.configPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CreateDefaultConfig(g_app.configPath);
        MessageBoxW(nullptr, 
            L"games.txt created!\n\nAdd your game process names, then restart.",
            L"FPS Monitor", MB_ICONINFORMATION);
        ShellExecuteW(nullptr, L"open", L"notepad.exe", 
                      g_app.configPath.c_str(), nullptr, SW_SHOW);
        return 0;
    }
    
    g_app.games = LoadGameList(g_app.configPath);
    Log(L"Loaded %zu games", g_app.games.size());
    
    if (g_app.games.empty()) {
        MessageBoxW(nullptr, L"No games configured!", L"FPS Monitor", MB_ICONWARNING);
        ShellExecuteW(nullptr, L"open", L"notepad.exe", 
                      g_app.configPath.c_str(), nullptr, SW_SHOW);
        return 0;
    }
    
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FPSMonitorMainClass";
    RegisterClassExW(&wc);
    
    g_app.hWnd = CreateWindowExW(0, wc.lpszClassName, L"FPS Monitor", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    
    g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_app.nid.hWnd = g_app.hWnd;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    swprintf_s(g_app.nid.szTip, L"FPS Monitor (External) - %zu game(s)", g_app.games.size());
    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
    
    ShowNotification(L"FPS Monitor Started", L"External mode - Works with anti-cheat!");
    
    HANDLE hThread = CreateThread(nullptr, 0, MonitorThread, nullptr, 0, nullptr);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    g_app.running = false;
    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);
    CloseHandle(hMutex);
    
    return 0;
}
