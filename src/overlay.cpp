#include "overlay.h"
#include "fps_counter.h"
#include "hooks.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <cstdio>
#include <cwctype>
#include <cwchar>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Overlay {
    enum class PositionMode {
        Corner,
        Custom,
    };

    static HWND s_hWnd = nullptr;
    static WNDPROC s_originalWndProc = nullptr;
    static bool s_showOverlay = true;
    static PositionMode s_positionMode = PositionMode::Corner;
    static int s_corner = 1;
    static float s_marginX = 8.0f;
    static float s_marginY = 8.0f;
    static float s_posX = 0.0f;
    static float s_posY = 0.0f;
    static float s_alpha = 0.25f;
    static int s_toggleKey = VK_F1;
    static bool s_showFps = true;
    static bool s_showFrameTime = true;
    static float s_greenThreshold = 60.0f;
    static float s_yellowThreshold = 30.0f;
    static float s_fontScale = 1.0f;

    static bool s_configPathReady = false;
    static wchar_t s_configPath[MAX_PATH] = {0};
    static FILETIME s_configWriteTime = {0};
    static ULONGLONG s_lastConfigCheckTick = 0;
    static bool s_configLoaded = false;

    static float Clamp01(float value) {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    static float ClampNonNegative(float value) {
        return value < 0.0f ? 0.0f : value;
    }

    static float ClampRange(float value, float minValue, float maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static bool TryGetFileWriteTime(const wchar_t* path, FILETIME* out) {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
            return false;
        }
        *out = data.ftLastWriteTime;
        return true;
    }

    static void InitConfigPath() {
        if (s_configPathReady) return;
        if (!Hooks::g_hModule) return;

        wchar_t modulePath[MAX_PATH] = {0};
        DWORD len = GetModuleFileNameW(Hooks::g_hModule, modulePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return;

        wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
        wchar_t* lastSlash2 = wcsrchr(modulePath, L'/');
        if (!lastSlash || (lastSlash2 && lastSlash2 > lastSlash)) lastSlash = lastSlash2;
        if (!lastSlash) return;

        *(lastSlash + 1) = L'\0';
        swprintf_s(s_configPath, L"%soverlay.ini", modulePath);
        s_configPathReady = true;
    }

    static bool TryParseFloat(const wchar_t* text, float* out) {
        if (!text || !out) return false;
        wchar_t* end = nullptr;
        float v = wcstof(text, &end);
        if (end == text) return false;
        *out = v;
        return true;
    }

    static float ReadIniFloat(const wchar_t* section, const wchar_t* key, float def) {
        wchar_t defBuf[32];
        swprintf_s(defBuf, L"%.3f", def);

        wchar_t buf[64];
        GetPrivateProfileStringW(section, key, defBuf, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), s_configPath);

        float v = def;
        if (TryParseFloat(buf, &v)) return v;
        return def;
    }

    static int ParseCorner(const wchar_t* text) {
        if (!text) return -1;
        while (*text && iswspace(*text)) text++;
        if (!*text) return -1;

        if (_wcsicmp(text, L"Custom") == 0) return 4;
        if (_wcsicmp(text, L"TopLeft") == 0) return 0;
        if (_wcsicmp(text, L"TopRight") == 0) return 1;
        if (_wcsicmp(text, L"BottomLeft") == 0) return 2;
        if (_wcsicmp(text, L"BottomRight") == 0) return 3;

        if (_wcsicmp(text, L"TL") == 0) return 0;
        if (_wcsicmp(text, L"TR") == 0) return 1;
        if (_wcsicmp(text, L"BL") == 0) return 2;
        if (_wcsicmp(text, L"BR") == 0) return 3;

        return -1;
    }

    static int ParseToggleKey(const wchar_t* text) {
        if (!text) return VK_F1;
        while (*text && iswspace(*text)) text++;
        if (!*text) return VK_F1;

        wchar_t* end = nullptr;
        long numeric = wcstol(text, &end, 10);
        if (end != text && numeric > 0 && numeric < 256) {
            return static_cast<int>(numeric);
        }

        if ((text[0] == L'F' || text[0] == L'f') && iswdigit(text[1])) {
            int n = _wtoi(text + 1);
            if (n >= 1 && n <= 12) return VK_F1 + (n - 1);
        }

        return VK_F1;
    }

    static void LoadConfigFromIni() {
        if (!s_configPathReady) return;

        constexpr const wchar_t* SECTION = L"Overlay";

        int visible = GetPrivateProfileIntW(SECTION, L"Visible", s_showOverlay ? 1 : 0, s_configPath);
        s_showOverlay = (visible != 0);

        float alpha = ReadIniFloat(SECTION, L"Alpha", s_alpha);
        s_alpha = Clamp01(alpha);

        int showFps = GetPrivateProfileIntW(SECTION, L"ShowFps", s_showFps ? 1 : 0, s_configPath);
        int showFrameTime = GetPrivateProfileIntW(SECTION, L"ShowFrameTime", s_showFrameTime ? 1 : 0, s_configPath);
        s_showFps = (showFps != 0);
        s_showFrameTime = (showFrameTime != 0);

        float greenThreshold = ReadIniFloat(SECTION, L"GreenThreshold", s_greenThreshold);
        float yellowThreshold = ReadIniFloat(SECTION, L"YellowThreshold", s_yellowThreshold);
        greenThreshold = ClampNonNegative(greenThreshold);
        yellowThreshold = ClampNonNegative(yellowThreshold);
        if (greenThreshold < yellowThreshold) {
            float tmp = greenThreshold;
            greenThreshold = yellowThreshold;
            yellowThreshold = tmp;
        }
        s_greenThreshold = greenThreshold;
        s_yellowThreshold = yellowThreshold;

        float fontScale = ReadIniFloat(SECTION, L"FontScale", s_fontScale);
        s_fontScale = ClampRange(fontScale, 0.5f, 5.0f);

        float marginX = ReadIniFloat(SECTION, L"MarginX", s_marginX);
        float marginY = ReadIniFloat(SECTION, L"MarginY", s_marginY);
        s_marginX = ClampNonNegative(marginX);
        s_marginY = ClampNonNegative(marginY);

        wchar_t cornerBuf[32] = {0};
        GetPrivateProfileStringW(SECTION, L"Corner", L"", cornerBuf, static_cast<DWORD>(sizeof(cornerBuf) / sizeof(cornerBuf[0])), s_configPath);
        int corner = ParseCorner(cornerBuf);
        if (corner >= 0 && corner <= 3) {
            s_corner = corner;
            s_positionMode = PositionMode::Corner;
        } else if (corner == 4) {
            float x = ReadIniFloat(SECTION, L"X", s_posX);
            float y = ReadIniFloat(SECTION, L"Y", s_posY);
            s_posX = x;
            s_posY = y;
            s_positionMode = PositionMode::Custom;
        }

        wchar_t keyBuf[32] = {0};
        GetPrivateProfileStringW(SECTION, L"ToggleKey", L"F1", keyBuf, static_cast<DWORD>(sizeof(keyBuf) / sizeof(keyBuf[0])), s_configPath);
        s_toggleKey = ParseToggleKey(keyBuf);
    }

    static void MaybeReloadConfig() {
        if (!s_configPathReady) return;

        ULONGLONG nowTick = GetTickCount64();
        if (nowTick - s_lastConfigCheckTick < 1000) return;
        s_lastConfigCheckTick = nowTick;

        FILETIME ft = {0};
        if (!TryGetFileWriteTime(s_configPath, &ft)) return;

        if (!s_configLoaded || CompareFileTime(&ft, &s_configWriteTime) != 0) {
            s_configWriteTime = ft;
            LoadConfigFromIni();
            s_configLoaded = true;
        }
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        if (msg == WM_KEYDOWN && wParam == static_cast<WPARAM>(s_toggleKey) && ((lParam & (1LL << 30)) == 0)) {
            s_showOverlay = !s_showOverlay;
        }

        return CallWindowProc(s_originalWndProc, hWnd, msg, wParam, lParam);
    }

    bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pContext) {
        s_hWnd = hWnd;

        s_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc))
        );

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.WindowPadding = ImVec2(6, 4);

        ImGui_ImplWin32_Init(hWnd);
        ImGui_ImplDX11_Init(pDevice, pContext);

        InitConfigPath();
        if (s_configPathReady) {
            FILETIME ft = {0};
            if (TryGetFileWriteTime(s_configPath, &ft)) {
                s_configWriteTime = ft;
                LoadConfigFromIni();
                s_configLoaded = true;
                s_lastConfigCheckTick = GetTickCount64();
            }
        }

        return true;
    }

    void Render() {
        MaybeReloadConfig();
        if (!s_showOverlay) return;
        if (!s_showFps && !s_showFrameTime) return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        io.FontGlobalScale = s_fontScale;
        if (s_positionMode == PositionMode::Custom) {
            ImGui::SetNextWindowPos(ImVec2(s_posX, s_posY), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        } else {
            float x = (s_corner == 0 || s_corner == 2) ? s_marginX : (io.DisplaySize.x - s_marginX);
            float y = (s_corner == 0 || s_corner == 1) ? s_marginY : (io.DisplaySize.y - s_marginY);
            float pivotX = (s_corner == 0 || s_corner == 2) ? 0.0f : 1.0f;
            float pivotY = (s_corner == 0 || s_corner == 1) ? 0.0f : 1.0f;
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(pivotX, pivotY));
        }
        ImGui::SetNextWindowBgAlpha(s_alpha);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoInputs |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoFocusOnAppearing;

        if (ImGui::Begin("##FPS", nullptr, flags)) {
            float fps = FpsCounter::GetDisplayFps();
            float frameTimeMs = FpsCounter::GetDisplayFrameTime();

            ImVec4 textColor;
            if (fps >= s_greenThreshold) {
                textColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
            } else if (fps >= s_yellowThreshold) {
                textColor = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
            } else {
                textColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            }

            if (s_showFps) {
                ImGui::TextColored(textColor, "FPS: %.1f", fps);
            }
            if (s_showFrameTime) {
                ImGui::TextColored(textColor, "Frame: %.1f ms", frameTimeMs);
            }
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void InvalidateDeviceObjects() {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }

    void CreateDeviceObjects() {
        ImGui_ImplDX11_CreateDeviceObjects();
    }

    void SetVisible(bool visible) {
        s_showOverlay = visible;
    }

    void SetPosition(float x, float y) {
        s_posX = x;
        s_posY = y;
        s_positionMode = PositionMode::Custom;
    }

    void SetAlpha(float alpha) {
        s_alpha = Clamp01(alpha);
    }

    void Shutdown() {
        if (s_originalWndProc && s_hWnd) {
            SetWindowLongPtr(s_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_originalWndProc));
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}
