#include <Windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <atomic>
#include <mutex>
#include <deque>
#include <stdio.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "MinHook.h"
#include "fps_config.h"

// Heartbeat detection
#define HEARTBEAT_SHARED_NAME L"FpsOverlayHeartbeat"
static HANDLE g_hHeartbeatThread = NULL;
static bool g_shouldExit = false;
static bool g_renderDisabled = false;

// Forward declarations
static void RemoveHook();
static void Log(const char* fmt, ...);

// Shared config
static HANDLE g_hConfigMap = NULL;
static FpsConfig* g_pConfig = NULL;

static bool OpenSharedConfig() {
    g_hConfigMap = OpenFileMappingW(FILE_MAP_READ, FALSE, CONFIG_SHARED_NAME);
    if (!g_hConfigMap) return false;
    
    g_pConfig = (FpsConfig*)MapViewOfFile(g_hConfigMap, FILE_MAP_READ, 0, 0, sizeof(FpsConfig));
    return g_pConfig != nullptr;
}

static void CloseSharedConfig() {
    if (g_pConfig) { UnmapViewOfFile(g_pConfig); g_pConfig = nullptr; }
    if (g_hConfigMap) { CloseHandle(g_hConfigMap); g_hConfigMap = nullptr; }
}

// Heartbeat thread - monitors if main process is still running
static DWORD WINAPI HeartbeatThread(LPVOID) {
    while (!g_shouldExit) {
        Sleep(3000);
        
        HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, HEARTBEAT_SHARED_NAME);
        if (!hEvent) {
            // Main process exited - just disable rendering, don't unload hooks
            Log("Heartbeat: Monitor exited, disabling render");
            g_shouldExit = true;
            g_renderDisabled = true;
            CloseSharedConfig();
            break;
        }
        CloseHandle(hEvent);
    }
    return 0;
}

// Debug log (definition)
static void Log(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buf, fmt, args);
    va_end(args);
    
    FILE* f = nullptr;
    fopen_s(&f, "C:\\fps_hook_log.txt", "a");
    if (f) {
        fprintf(f, "[%d] %s\n", GetCurrentProcessId(), buf);
        fclose(f);
    }
}

#pragma data_seg(".shared")
HHOOK g_hHook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.shared,RWS")

static HMODULE g_hModule = NULL;
static bool g_initialized = false;
static bool g_hooked = false;

// FPS calculation - GPU FPS (Present calls)
static std::mutex g_mutex;
static std::deque<LARGE_INTEGER> g_frameTimes;
static LARGE_INTEGER g_frequency;
static std::atomic<int> g_gpuFps{0};  // Renamed from g_displayFps
static LARGE_INTEGER g_lastDisplayUpdate;
static bool g_visible = true;

// Display FPS calculation
static std::atomic<int> g_dispFps{0};          // Actual display FPS
static std::atomic<bool> g_dispFpsActual{false}; // true = measured, false = inferred
static int g_monitorRefreshRate = 60;
static int g_lastSyncInterval = 0;  // VSync state from Present call

// Drag to move support
static bool g_dragMode = false;      // Ctrl+Shift held
static bool g_dragging = false;      // Currently dragging
static int g_dragStartMouseX = 0;
static int g_dragStartMouseY = 0;
static int g_dragStartPosX = 0;
static int g_dragStartPosY = 0;
static int g_currentPosX = 100;      // Current absolute position
static int g_currentPosY = 10;
static int g_fpsBoxWidth = 0;        // FPS display box dimensions
static int g_fpsBoxHeight = 20;

// Frame Statistics for Display FPS
static UINT g_lastPresentCount = 0;
static LARGE_INTEGER g_lastStatsTime = {0};
static bool g_statsInitialized = false;

// D3D11 resources (cached)
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static ID3D11RenderTargetView* g_pRTV = nullptr;
static ID3D11VertexShader* g_pVS = nullptr;
static ID3D11PixelShader* g_pPS = nullptr;
static ID3D11InputLayout* g_pInputLayout = nullptr;
static ID3D11Buffer* g_pVertexBuffer = nullptr;
static ID3D11BlendState* g_pBlendState = nullptr;
static ID3D11Texture2D* g_pFontTexture = nullptr;
static ID3D11ShaderResourceView* g_pFontSRV = nullptr;
static ID3D11SamplerState* g_pSampler = nullptr;
static IDXGISwapChain* g_pCurrentSwapChain = nullptr;
static UINT g_width = 0, g_height = 0;
static bool g_resourcesCreated = false;

// Hook - DX11
typedef HRESULT(WINAPI* PFN_Present)(IDXGISwapChain*, UINT, UINT);
static PFN_Present g_originalPresent = nullptr;

// Hook - DX9
typedef HRESULT(WINAPI* PFN_EndScene9)(IDirect3DDevice9*);
static PFN_EndScene9 g_originalEndScene9 = nullptr;
static bool g_d3d9Hooked = false;

// DX12 detection
static bool g_isDX12 = false;

// Shaders
static const char* g_shaderCode = R"(
struct VS_INPUT {
    float2 pos : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

Texture2D fontTex : register(t0);
SamplerState samp : register(s0);

PS_INPUT VS(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target {
    float alpha = fontTex.Sample(samp, input.uv).r;
    return float4(input.color.rgb, input.color.a * alpha);
}
)";

// 8x8 bitmap font data (ASCII 32-127)
static const unsigned char g_fontData[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, // 33 !
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // 34 "
    {0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x00,0x00}, // 35 #
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}, // 36 $
    {0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00,0x00}, // 37 %
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // 38 &
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // 39 '
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // 40 (
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // 41 )
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 *
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // 43 +
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ,
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // 45 -
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 .
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x00,0x00}, // 47 /
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, // 48 0
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 49 1
    {0x7C,0xC6,0x06,0x1C,0x70,0xC6,0xFE,0x00}, // 50 2
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // 51 3
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, // 52 4
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // 53 5
    {0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // 54 6
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, // 55 7
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // 56 8
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // 57 9
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // 58 :
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // 59 ;
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // 60 <
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // 61 =
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // 62 >
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, // 63 ?
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x7C,0x00}, // 64 @
    {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00}, // 65 A
    {0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00}, // 66 B
    {0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00}, // 67 C
    {0xF8,0xCC,0xC6,0xC6,0xC6,0xCC,0xF8,0x00}, // 68 D
    {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xFE,0x00}, // 69 E
    {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0x00}, // 70 F
    {0x7C,0xC6,0xC0,0xCE,0xC6,0xC6,0x7E,0x00}, // 71 G
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 72 H
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, // 73 I
    {0x1E,0x06,0x06,0x06,0xC6,0xC6,0x7C,0x00}, // 74 J
    {0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00}, // 75 K
    {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0x00}, // 76 L
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, // 77 M
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 78 N
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 79 O
    {0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0,0x00}, // 80 P
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06}, // 81 Q
    {0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0x00}, // 82 R
    {0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00}, // 83 S
    {0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 84 T
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xFE,0x00}, // 85 U
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 86 V
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // 87 W
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // 88 X
    {0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x00}, // 89 Y
    {0xFE,0x06,0x0C,0x18,0x30,0x60,0xFE,0x00}, // 90 Z
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // 91 [
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x00,0x00}, // 92 backslash
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // 93 ]
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // 94 ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 _
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // 96 `
    {0x00,0x00,0x7C,0x06,0x7E,0xC6,0x7E,0x00}, // 97 a
    {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xFC,0x00}, // 98 b
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, // 99 c
    {0x06,0x06,0x7E,0xC6,0xC6,0xC6,0x7E,0x00}, // 100 d
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, // 101 e
    {0x1C,0x36,0x30,0x7C,0x30,0x30,0x30,0x00}, // 102 f
    {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x7C}, // 103 g
    {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x00}, // 104 h
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 105 i
    {0x06,0x00,0x0E,0x06,0x06,0x06,0xC6,0x7C}, // 106 j
    {0xC0,0xC0,0xCC,0xD8,0xF0,0xD8,0xCC,0x00}, // 107 k
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 108 l
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00}, // 109 m
    {0x00,0x00,0xFC,0xC6,0xC6,0xC6,0xC6,0x00}, // 110 n
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, // 111 o
    {0x00,0x00,0xFC,0xC6,0xC6,0xFC,0xC0,0xC0}, // 112 p
    {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x06}, // 113 q
    {0x00,0x00,0xDC,0xE6,0xC0,0xC0,0xC0,0x00}, // 114 r
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00}, // 115 s
    {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00}, // 116 t
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x7E,0x00}, // 117 u
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 118 v
    {0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00}, // 119 w
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 120 x
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0x7C}, // 121 y
    {0x00,0x00,0xFE,0x0C,0x38,0x60,0xFE,0x00}, // 122 z
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // 123 {
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 |
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // 125 }
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, // 126 ~
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 127
};

// Vertex structure
struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

bool IsGraphicsProcess() {
    char exeName[MAX_PATH];
    GetModuleFileNameA(NULL, exeName, MAX_PATH);
    char* fileName = strrchr(exeName, '\\');
    fileName = fileName ? fileName + 1 : exeName;
    
    // Exclude system processes
    const char* excludeList[] = {
        "fps_monitor.exe",
        "conhost.exe",
        "explorer.exe",
        "dwm.exe",
        "csrss.exe",
        "svchost.exe",
        "SearchHost.exe",
        "ShellExperienceHost.exe",
        "StartMenuExperienceHost.exe",
        "RuntimeBroker.exe",
        "TextInputHost.exe",
        "taskhostw.exe",
        "ctfmon.exe",
        "cleanmgr.exe",
        "taskmgr.exe",
        "cmd.exe",
        "powershell.exe",
        "WindowsTerminal.exe",
        "OneDrive.exe",
        "OneDriveStandaloneUpdater.exe",
        "browser.exe",
        "BackgroundDownload.exe",
        "ApplicationFrameHost.exe",
        "SystemSettings.exe",
        "SettingsHelper.exe",
        "sihost.exe",
        "fontdrvhost.exe",
        "WmiPrvSE.exe",
        "dllhost.exe",
        "CompPkgSrv.exe",
        "SearchIndexer.exe",
        "SecurityHealthService.exe",
        "MsMpEng.exe",
        "NisSrv.exe",
        "smartscreen.exe",
        "spoolsv.exe",
        "services.exe",
        "lsass.exe",
        "wininit.exe",
        "winlogon.exe",
        NULL
    };
    
    for (int i = 0; excludeList[i] != NULL; i++) {
        if (_stricmp(fileName, excludeList[i]) == 0) return false;
    }
    
    // Convert path to lowercase for case-insensitive comparison
    char lowerPath[MAX_PATH];
    strcpy_s(lowerPath, exeName);
    _strlwr_s(lowerPath);
    
    // Exclude paths containing certain keywords (case-insensitive)
    if (strstr(lowerPath, "\\windows\\") != NULL) return false;
    if (strstr(lowerPath, "\\microsoft\\") != NULL) return false;
    if (strstr(lowerPath, "\\onedrive\\") != NULL) return false;
    if (strstr(lowerPath, "\\system32\\") != NULL) return false;
    if (strstr(lowerPath, "\\syswow64\\") != NULL) return false;
    if (strstr(lowerPath, "program files") != NULL) return false;
    if (strstr(lowerPath, "visual studio") != NULL) return false;
    if (strstr(lowerPath, "\\appdata\\") != NULL) return false;
    
    // Must have D3D loaded
    return GetModuleHandleW(L"d3d11.dll") != NULL || 
           GetModuleHandleW(L"dxgi.dll") != NULL ||
           GetModuleHandleW(L"d3d9.dll") != NULL;
}

// Get monitor refresh rate
void InitMonitorRefreshRate() {
    DEVMODEW devMode = {0};
    devMode.dmSize = sizeof(DEVMODEW);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &devMode)) {
        g_monitorRefreshRate = devMode.dmDisplayFrequency;
        if (g_monitorRefreshRate == 0 || g_monitorRefreshRate == 1) {
            g_monitorRefreshRate = 60;
        }
    }
    Log("Monitor refresh rate: %d Hz", g_monitorRefreshRate);
}

// Update GPU FPS (from Present calls)
void UpdateGpuFps() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    std::lock_guard<std::mutex> lock(g_mutex);
    g_frameTimes.push_back(now);
    
    double twoSecondsAgo = (double)(now.QuadPart - 2 * g_frequency.QuadPart);
    while (!g_frameTimes.empty() && (double)g_frameTimes.front().QuadPart < twoSecondsAgo) {
        g_frameTimes.pop_front();
    }
    
    double oneSecondAgo = (double)(now.QuadPart - g_frequency.QuadPart);
    int frameCount = 0;
    for (auto it = g_frameTimes.rbegin(); it != g_frameTimes.rend(); ++it) {
        if ((double)it->QuadPart >= oneSecondAgo) {
            frameCount++;
        } else {
            break;
        }
    }
    
    double elapsed = (double)(now.QuadPart - g_lastDisplayUpdate.QuadPart) / g_frequency.QuadPart;
    if (elapsed >= 0.2) {
        g_gpuFps = frameCount;
        g_lastDisplayUpdate = now;
    }
}

// Try to get Display FPS from Frame Statistics
bool TryGetDisplayFpsFromStats(IDXGISwapChain* pSwapChain) {
    DXGI_FRAME_STATISTICS stats;
    HRESULT hr = pSwapChain->GetFrameStatistics(&stats);
    
    if (FAILED(hr)) {
        return false;
    }
    
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    if (!g_statsInitialized) {
        g_lastPresentCount = stats.PresentCount;
        g_lastStatsTime = now;
        g_statsInitialized = true;
        return false;
    }
    
    double elapsed = (double)(now.QuadPart - g_lastStatsTime.QuadPart) / g_frequency.QuadPart;
    
    if (elapsed >= 1.0) {
        UINT framesDelta = stats.PresentCount - g_lastPresentCount;
        g_dispFps = (int)(framesDelta / elapsed);
        
        g_lastPresentCount = stats.PresentCount;
        g_lastStatsTime = now;
    }
    
    return true;
}

// Infer Display FPS based on VSync and refresh rate
void InferDisplayFps() {
    int gpuFps = g_gpuFps.load();
    
    if (g_lastSyncInterval > 0) {
        // VSync ON: display FPS = min(GPU FPS, refresh rate)
        g_dispFps = (gpuFps < g_monitorRefreshRate) ? gpuFps : g_monitorRefreshRate;
    } else {
        // VSync OFF: display at refresh rate (with possible tearing)
        g_dispFps = g_monitorRefreshRate;
    }
}

// Update Display FPS (try Frame Statistics, fallback to inference)
void UpdateDisplayFps(IDXGISwapChain* pSwapChain) {
    if (TryGetDisplayFpsFromStats(pSwapChain)) {
        g_dispFpsActual = true;
    } else {
        InferDisplayFps();
        g_dispFpsActual = false;
    }
}

void CleanupResources() {
    if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }
    if (g_pVS) { g_pVS->Release(); g_pVS = nullptr; }
    if (g_pPS) { g_pPS->Release(); g_pPS = nullptr; }
    if (g_pInputLayout) { g_pInputLayout->Release(); g_pInputLayout = nullptr; }
    if (g_pVertexBuffer) { g_pVertexBuffer->Release(); g_pVertexBuffer = nullptr; }
    if (g_pBlendState) { g_pBlendState->Release(); g_pBlendState = nullptr; }
    if (g_pFontTexture) { g_pFontTexture->Release(); g_pFontTexture = nullptr; }
    if (g_pFontSRV) { g_pFontSRV->Release(); g_pFontSRV = nullptr; }
    if (g_pSampler) { g_pSampler->Release(); g_pSampler = nullptr; }
    g_resourcesCreated = false;
}

bool CreateResources(IDXGISwapChain* pSwapChain) {
    if (g_resourcesCreated && g_pCurrentSwapChain == pSwapChain) return true;
    
    static bool loggedStart = false;
    if (!loggedStart) {
        Log("CreateResources: starting");
        loggedStart = true;
    }
    
    CleanupResources();
    g_pCurrentSwapChain = pSwapChain;
    
    // Check if DX12
    ID3D12Device* pD3D12Device = nullptr;
    if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pD3D12Device))) {
        g_isDX12 = true;
        pD3D12Device->Release();
        Log("CreateResources: DX12 detected, using fallback rendering");
        return false; // Use fallback rendering
    }
    
    // Get DX11 device
    if (!g_pd3dDevice) {
        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice);
        if (FAILED(hr)) {
            Log("CreateResources: GetDevice failed hr=0x%08X", hr);
            return false;
        }
        if (g_pd3dDevice) g_pd3dDevice->GetImmediateContext(&g_pd3dContext);
    }
    if (!g_pd3dDevice || !g_pd3dContext) {
        Log("CreateResources: No device/context");
        return false;
    }
    
    // Get backbuffer size
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) return false;
    
    D3D11_TEXTURE2D_DESC bbDesc;
    pBackBuffer->GetDesc(&bbDesc);
    g_width = bbDesc.Width;
    g_height = bbDesc.Height;
    
    // Create RTV
    if (FAILED(g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRTV))) {
        pBackBuffer->Release();
        return false;
    }
    pBackBuffer->Release();
    
    // Compile shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    if (FAILED(D3DCompile(g_shaderCode, strlen(g_shaderCode), nullptr, nullptr, nullptr, 
                          "VS", "vs_4_0", 0, 0, &vsBlob, &errorBlob))) {
        if (errorBlob) errorBlob->Release();
        return false;
    }
    if (FAILED(D3DCompile(g_shaderCode, strlen(g_shaderCode), nullptr, nullptr, nullptr,
                          "PS", "ps_4_0", 0, 0, &psBlob, &errorBlob))) {
        vsBlob->Release();
        if (errorBlob) errorBlob->Release();
        return false;
    }
    
    g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_pVS);
    g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pPS);
    
    // Input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    g_pd3dDevice->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_pInputLayout);
    
    vsBlob->Release();
    psBlob->Release();
    
    // Vertex buffer (enough for ~100 characters)
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(Vertex) * 6 * 100;
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pd3dDevice->CreateBuffer(&vbDesc, nullptr, &g_pVertexBuffer);
    
    // Blend state
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_pd3dDevice->CreateBlendState(&blendDesc, &g_pBlendState);
    
    // Font texture (16x6 characters, 8x8 each = 128x48)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 128;
    texDesc.Height = 48;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    
    unsigned char texData[128 * 48] = {0};
    for (int i = 0; i < 96; i++) {
        int tx = (i % 16) * 8;
        int ty = (i / 16) * 8;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (g_fontData[i][y] & (0x80 >> x)) {
                    texData[(ty + y) * 128 + tx + x] = 255;
                }
            }
        }
    }
    
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = texData;
    initData.SysMemPitch = 128;
    g_pd3dDevice->CreateTexture2D(&texDesc, &initData, &g_pFontTexture);
    g_pd3dDevice->CreateShaderResourceView(g_pFontTexture, nullptr, &g_pFontSRV);
    
    // Sampler
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_pd3dDevice->CreateSamplerState(&sampDesc, &g_pSampler);
    
    g_resourcesCreated = true;
    return true;
}

// GDI fallback for DX12
void RenderFpsGDI(IDXGISwapChain* pSwapChain) {
    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(pSwapChain->GetDesc(&desc))) return;
    
    HWND hWnd = desc.OutputWindow;
    if (!hWnd) return;
    
    HDC hdc = GetDC(hWnd);
    if (!hdc) return;
    
    // Show estimated actual display FPS (what user actually sees)
    int gpuFps = g_gpuFps.load();
    bool vsyncOn = (g_lastSyncInterval > 0);
    int refreshRate = g_monitorRefreshRate;
    int displayFps = vsyncOn ? (gpuFps < refreshRate ? gpuFps : refreshRate) : gpuFps;
    
    char text[64];
    if (vsyncOn) {
        sprintf_s(text, "%d FPS [V]", displayFps);
    } else {
        sprintf_s(text, "%d FPS", displayFps);
    }
    int textLen = (int)strlen(text);
    
    // FPS color based on value
    COLORREF textColor;
    if (displayFps >= 60) textColor = RGB(0, 230, 100);
    else if (displayFps >= 30) textColor = RGB(255, 200, 0);
    else textColor = RGB(255, 64, 64);
    
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    
    // Use dragged position
    int textWidth = textLen * 10;
    int x = g_currentPosX;
    int y = g_currentPosY;
    
    // Clamp to screen
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + textWidth > clientRect.right) x = clientRect.right - textWidth;
    if (y + 18 > clientRect.bottom) y = clientRect.bottom - 18;
    
    // Background with border
    RECT bgRect = {x - 6, y - 3, x + textLen * 10 + 6, y + 18};
    HBRUSH borderBrush = CreateSolidBrush(RGB(77, 77, 89));
    FrameRect(hdc, &bgRect, borderBrush);
    DeleteObject(borderBrush);
    
    RECT innerRect = {x - 5, y - 2, x + textLen * 10 + 5, y + 17};
    HBRUSH bgBrush = CreateSolidBrush(RGB(13, 13, 20));
    FillRect(hdc, &innerRect, bgBrush);
    DeleteObject(bgBrush);
    
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    HFONT hFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    TextOutA(hdc, x, y, text, textLen);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    
    ReleaseDC(hWnd, hdc);
}

void RenderFpsOverlay(IDXGISwapChain* pSwapChain) {
    static bool loggedOnce = false;
    if (!loggedOnce) {
        Log("RenderFpsOverlay: called, visible=%d", g_visible ? 1 : 0);
        loggedOnce = true;
    }
    
    if (!g_visible) return;
    
    // Try DX11 rendering, fall back to GDI for DX12
    if (!CreateResources(pSwapChain)) {
        static bool loggedFail = false;
        if (!loggedFail) {
            Log("RenderFpsOverlay: CreateResources failed, isDX12=%d", g_isDX12 ? 1 : 0);
            loggedFail = true;
        }
        if (g_isDX12) {
            RenderFpsGDI(pSwapChain);
        }
        return;
    }
    
    // Show estimated actual display FPS
    int gpuFps = g_gpuFps.load();
    bool vsyncOn = (g_lastSyncInterval > 0);
    int refreshRate = g_monitorRefreshRate;
    int displayFps = vsyncOn ? (gpuFps < refreshRate ? gpuFps : refreshRate) : gpuFps;
    
    char text[64];
    if (vsyncOn) {
        sprintf_s(text, "%d FPS [V]", displayFps);
    } else {
        sprintf_s(text, "%d FPS", displayFps);
    }
    int textLen = (int)strlen(text);
    
    // FPS color based on value
    float fr, fg, fb;
    if (displayFps >= 60) { fr = 0.0f; fg = 0.9f; fb = 0.4f; }
    else if (displayFps >= 30) { fr = 1.0f; fg = 0.8f; fb = 0.0f; }
    else { fr = 1.0f; fg = 0.25f; fb = 0.25f; }
    
    // Build vertices
    float charW = 10.0f;
    float charH = 12.0f;
    
    // Calculate position - use dragged position or config
    float startX, startY;
    float textWidth = textLen * charW;
    
    // Use current dragged position (stored in g_currentPosX/Y)
    startX = (float)g_currentPosX;
    startY = (float)g_currentPosY;
    
    // Clamp to screen bounds
    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;
    if (startX + textWidth > g_width) startX = g_width - textWidth;
    if (startY + charH > g_height) startY = g_height - charH;
    
    // Update box dimensions for hit testing
    g_fpsBoxWidth = (int)(textWidth + 12);  // with padding
    g_fpsBoxHeight = (int)(charH + 6);
    
    Vertex vertices[600];
    int vertCount = 0;
    
    // Background quad with padding
    float pad = 6.0f;
    float bgL = (startX - pad) / g_width * 2.0f - 1.0f;
    float bgR = (startX + textLen * charW + pad) / g_width * 2.0f - 1.0f;
    float bgT = 1.0f - (startY - pad / 2) / g_height * 2.0f;
    float bgB = 1.0f - (startY + charH + pad / 2) / g_height * 2.0f;
    
    // Dark background with high opacity (90%)
    float bgColor[4] = {0.05f, 0.05f, 0.08f, 0.9f};
    vertices[vertCount++] = {bgL, bgT, 0, 0, bgColor[0], bgColor[1], bgColor[2], bgColor[3]};
    vertices[vertCount++] = {bgR, bgT, 0, 0, bgColor[0], bgColor[1], bgColor[2], bgColor[3]};
    vertices[vertCount++] = {bgL, bgB, 0, 0, bgColor[0], bgColor[1], bgColor[2], bgColor[3]};
    vertices[vertCount++] = {bgR, bgT, 0, 0, bgColor[0], bgColor[1], bgColor[2], bgColor[3]};
    vertices[vertCount++] = {bgR, bgB, 0, 0, bgColor[0], bgColor[1], bgColor[2], bgColor[3]};
    vertices[vertCount++] = {bgL, bgB, 0, 0, bgColor[0], bgColor[1], bgColor[2], bgColor[3]};
    
    // Border (1 pixel, lighter color)
    float borderW = 1.5f / g_width * 2.0f;
    float borderH = 1.5f / g_height * 2.0f;
    float borderColor[4] = {0.3f, 0.3f, 0.35f, 0.95f};
    // Top border
    vertices[vertCount++] = {bgL, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL, bgT - borderH, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgT - borderH, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL, bgT - borderH, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    // Bottom border
    vertices[vertCount++] = {bgL, bgB + borderH, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgB + borderH, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgB + borderH, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    // Left border
    vertices[vertCount++] = {bgL, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL + borderW, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL + borderW, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL + borderW, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgL, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    // Right border
    vertices[vertCount++] = {bgR - borderW, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR - borderW, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgT, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    vertices[vertCount++] = {bgR - borderW, bgB, 0, 0, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    
    // Text characters - format: "xxx FPS [V]" or "xxx FPS"
    // Find where "FPS" starts for color coding
    int fpsPos = -1;
    for (int i = 0; i < textLen - 2; i++) {
        if (text[i] == 'F' && text[i+1] == 'P' && text[i+2] == 'S') {
            fpsPos = i;
            break;
        }
    }
    
    for (int i = 0; i < textLen; i++) {
        char c = text[i];
        int idx = c - 32;
        if (idx < 0 || idx >= 96) idx = 0;
        
        float x = startX + i * charW;
        float y = startY;
        
        // Screen coords to NDC
        float l = x / g_width * 2.0f - 1.0f;
        float rr = (x + charW) / g_width * 2.0f - 1.0f;
        float t = 1.0f - y / g_height * 2.0f;
        float bb = 1.0f - (y + charH) / g_height * 2.0f;
        
        // UV coords
        float u0 = (idx % 16) * 8.0f / 128.0f;
        float v0 = (idx / 16) * 8.0f / 48.0f;
        float u1 = u0 + 8.0f / 128.0f;
        float v1 = v0 + 8.0f / 48.0f;
        
        // Choose color: FPS number colored, rest white
        float r, g, b;
        if (fpsPos > 0 && i < fpsPos) {
            // FPS number - colored based on value
            r = fr; g = fg; b = fb;
        } else {
            // "FPS" and "[V]" - white
            r = 1.0f; g = 1.0f; b = 1.0f;
        }
        
        vertices[vertCount++] = {l, t, u0, v0, r, g, b, 1.0f};
        vertices[vertCount++] = {rr, t, u1, v0, r, g, b, 1.0f};
        vertices[vertCount++] = {l, bb, u0, v1, r, g, b, 1.0f};
        vertices[vertCount++] = {rr, t, u1, v0, r, g, b, 1.0f};
        vertices[vertCount++] = {rr, bb, u1, v1, r, g, b, 1.0f};
        vertices[vertCount++] = {l, bb, u0, v1, r, g, b, 1.0f};
    }
    
    // Update vertex buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_pd3dContext->Map(g_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, vertices, vertCount * sizeof(Vertex));
        g_pd3dContext->Unmap(g_pVertexBuffer, 0);
    }
    
    // Save state
    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    g_pd3dContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);
    
    // Set state
    g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
    g_pd3dContext->OMSetBlendState(g_pBlendState, nullptr, 0xFFFFFFFF);
    
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_pd3dContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
    g_pd3dContext->IASetInputLayout(g_pInputLayout);
    g_pd3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    g_pd3dContext->VSSetShader(g_pVS, nullptr, 0);
    g_pd3dContext->PSSetShader(g_pPS, nullptr, 0);
    g_pd3dContext->PSSetShaderResources(0, 1, &g_pFontSRV);
    g_pd3dContext->PSSetSamplers(0, 1, &g_pSampler);
    
    D3D11_VIEWPORT vp = {0, 0, (float)g_width, (float)g_height, 0, 1};
    g_pd3dContext->RSSetViewports(1, &vp);
    
    // Draw
    g_pd3dContext->Draw(vertCount, 0);
    
    // Restore state
    g_pd3dContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();
}
HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // Record VSync state for Display FPS inference
    g_lastSyncInterval = SyncInterval;
    
    if (!g_renderDisabled) {
        __try {
            // Update GPU FPS only (Display FPS disabled for stability)
            UpdateGpuFps();
            
            // Improved Display FPS inference based on VSync and refresh rate
            int gpuFps = g_gpuFps.load();
            if (SyncInterval > 0) {
                // VSync ON: Display FPS = min(GPU FPS, monitor refresh rate)
                g_dispFps = (gpuFps < g_monitorRefreshRate) ? gpuFps : g_monitorRefreshRate;
            } else {
                // VSync OFF: Display can show up to GPU FPS (with tearing)
                g_dispFps = gpuFps;
            }
            g_dispFpsActual = false;  // Mark as inferred
            
            // Read visibility and position from shared config (controlled by monitor)
            // No hotkey processing in hook - safer and more stable
            if (g_pConfig) {
                g_visible = g_pConfig->visible;
                
                // Update position from config if changed
                if (g_pConfig->position == POS_CUSTOM) {
                    g_currentPosX = g_pConfig->customX;
                    g_currentPosY = g_pConfig->customY;
                } else {
                    g_currentPosX = g_pConfig->offsetX;
                    g_currentPosY = g_pConfig->offsetY;
                }
            }
            
            RenderFpsOverlay(pSwapChain);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("Exception in HookedPresent, disabling render");
            g_renderDisabled = true;
        }
    }
    
    return g_originalPresent(pSwapChain, SyncInterval, Flags);
}

// D3D9 rendering using GDI
void RenderFpsOverlay9(IDirect3DDevice9* pDevice) {
    if (!g_visible) return;
    
    IDirect3DSurface9* pBackBuffer = nullptr;
    if (FAILED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer))) return;
    
    HDC hdc = nullptr;
    if (SUCCEEDED(pBackBuffer->GetDC(&hdc))) {
        // Show estimated actual display FPS
        int gpuFps = g_gpuFps.load();
        bool vsyncOn = (g_lastSyncInterval > 0);
        int refreshRate = g_monitorRefreshRate;
        int displayFps = vsyncOn ? (gpuFps < refreshRate ? gpuFps : refreshRate) : gpuFps;
        
        char text[64];
        if (vsyncOn) {
            sprintf_s(text, "%d FPS [V]", displayFps);
        } else {
            sprintf_s(text, "%d FPS", displayFps);
        }
        
        // Get surface size
        D3DSURFACE_DESC desc;
        pBackBuffer->GetDesc(&desc);
        
        // Use dragged position
        int textLen = (int)strlen(text);
        int textWidth = textLen * 10;
        int x = g_currentPosX;
        int y = g_currentPosY;
        
        // Clamp to screen
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + textWidth > (int)desc.Width) x = desc.Width - textWidth;
        if (y + 18 > (int)desc.Height) y = desc.Height - 18;
        
        // Background with border
        RECT bgRect = {x - 6, y - 3, x + textWidth + 6, y + 18};
        HBRUSH borderBrush = CreateSolidBrush(RGB(77, 77, 89));
        FrameRect(hdc, &bgRect, borderBrush);
        DeleteObject(borderBrush);
        
        RECT innerRect = {x - 5, y - 2, x + textWidth + 5, y + 17};
        HBRUSH bgBrush = CreateSolidBrush(RGB(13, 13, 20));
        FillRect(hdc, &innerRect, bgBrush);
        DeleteObject(bgBrush);
        
        // Text - use GPU FPS color for simplicity in GDI
        COLORREF textColor;
        if (gpuFps >= 60) textColor = RGB(0, 230, 100);
        else if (gpuFps >= 30) textColor = RGB(255, 200, 0);
        else textColor = RGB(255, 64, 64);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        HFONT hFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
        TextOutA(hdc, x, y, text, textLen);
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        
        pBackBuffer->ReleaseDC(hdc);
    }
    pBackBuffer->Release();
}

HRESULT WINAPI HookedEndScene9(IDirect3DDevice9* pDevice) {
    if (g_renderDisabled) return g_originalEndScene9(pDevice);
    
    UpdateGpuFps();
    g_dispFps = g_gpuFps.load();
    g_dispFpsActual = false;
    
    // Read visibility from shared config (no hotkey processing)
    if (g_pConfig) {
        g_visible = g_pConfig->visible;
    }
    
    RenderFpsOverlay9(pDevice);
    
    return g_originalEndScene9(pDevice);
}

void* GetEndScene9Address() {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0, 
                       GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"D3D9Dummy", NULL };
    RegisterClassExW(&wc);
    
    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    if (!hWnd) return nullptr;
    
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        DestroyWindow(hWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return nullptr;
    }
    
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    
    IDirect3DDevice9* pDevice = nullptr;
    void* endSceneAddr = nullptr;
    
    if (SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &pDevice))) {
        void** vtable = *(void***)pDevice;
        endSceneAddr = vtable[42]; // EndScene is at index 42
        pDevice->Release();
    }
    
    pD3D->Release();
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return endSceneAddr;
}

void* GetPresentAddress() {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0, 
                       GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"DX11Dummy", NULL };
    RegisterClassExW(&wc);
    
    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    if (!hWnd) return nullptr;
    
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    IDXGISwapChain* pSwapChain = nullptr;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    void* presentAddr = nullptr;
    
    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
            D3D11_SDK_VERSION, &sd, &pSwapChain, &pDevice, NULL, &pContext))) {
        void** vtable = *(void***)pSwapChain;
        presentAddr = vtable[8];
        pContext->Release();
        pDevice->Release();
        pSwapChain->Release();
    }
    
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return presentAddr;
}

bool InstallHook() {
    if (g_hooked) return true;
    
    Log("InstallHook: starting");
    
    // Try to open shared config
    if (OpenSharedConfig()) {
        Log("InstallHook: Shared config opened");
        
        // Initialize position from config
        if (g_pConfig) {
            if (g_pConfig->position == POS_CUSTOM) {
                g_currentPosX = g_pConfig->customX;
                g_currentPosY = g_pConfig->customY;
            } else {
                // Use offset from config for default position
                g_currentPosX = g_pConfig->offsetX;
                g_currentPosY = g_pConfig->offsetY;
            }
        }
    }
    
    if (MH_Initialize() != MH_OK) { Log("InstallHook: MH_Initialize failed"); return false; }
    
    bool hooked = false;
    
    // Try DX11 first
    if (GetModuleHandleW(L"d3d11.dll") || GetModuleHandleW(L"dxgi.dll")) {
        void* presentAddr = GetPresentAddress();
        if (presentAddr) {
            Log("InstallHook: DX11 Present at %p", presentAddr);
            if (MH_CreateHook(presentAddr, &HookedPresent, (void**)&g_originalPresent) == MH_OK) {
                if (MH_EnableHook(presentAddr) == MH_OK) {
                    Log("InstallHook: DX11 hook SUCCESS");
                    hooked = true;
                }
            }
        }
    }
    
    // Try DX9
    if (!hooked && GetModuleHandleW(L"d3d9.dll")) {
        void* endSceneAddr = GetEndScene9Address();
        if (endSceneAddr) {
            Log("InstallHook: DX9 EndScene at %p", endSceneAddr);
            if (MH_CreateHook(endSceneAddr, &HookedEndScene9, (void**)&g_originalEndScene9) == MH_OK) {
                if (MH_EnableHook(endSceneAddr) == MH_OK) {
                    Log("InstallHook: DX9 hook SUCCESS");
                    g_d3d9Hooked = true;
                    hooked = true;
                }
            }
        }
    }
    
    if (!hooked) {
        Log("InstallHook: No hooks installed");
        MH_Uninitialize();
        return false;
    }
    
    // Start heartbeat thread to detect when monitor exits
    g_hHeartbeatThread = CreateThread(NULL, 0, HeartbeatThread, NULL, 0, NULL);
    
    g_hooked = true;
    return true;
}

static void RemoveHook() {
    if (!g_hooked) return;
    g_shouldExit = true;
    
    // Just disable rendering - don't unload hooks to avoid crashes
    // The DLL will be unloaded naturally when the process exits
    g_renderDisabled = true;
    
    // Cleanup GPU resources safely
    CleanupResources();
    CloseSharedConfig();
    
    if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    
    // Don't call MH_DisableHook or MH_Uninitialize - causes crashes
    // g_hooked = false;  // Keep as true so we don't try to reinstall
}

LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && !g_initialized) {
        g_initialized = true;
        
        QueryPerformanceFrequency(&g_frequency);
        QueryPerformanceCounter(&g_lastDisplayUpdate);
        
        // Only start monitoring thread for potential game processes
        CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            for (int i = 0; i < 10; i++) {
                Sleep(1000);
                if (!g_hooked && IsGraphicsProcess()) {
                    char exeName[MAX_PATH];
                    GetModuleFileNameA(NULL, exeName, MAX_PATH);
                    Log("Game detected: %s", exeName);
                    InstallHook();
                    break;
                }
            }
            return 0;
        }, NULL, 0, NULL);
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

extern "C" __declspec(dllexport) HHOOK InstallGlobalHook() {
    if (g_hHook) return g_hHook;
    g_hHook = SetWindowsHookExW(WH_CBT, CBTProc, g_hModule, 0);
    return g_hHook;
}

extern "C" __declspec(dllexport) void RemoveGlobalHook() {
    if (g_hHook) { UnhookWindowsHookEx(g_hHook); g_hHook = NULL; }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        RemoveHook();
        break;
    }
    return TRUE;
}
