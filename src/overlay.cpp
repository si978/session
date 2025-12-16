#include "overlay.h"
#include "fps_counter.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <cstdio>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Overlay {
    static HWND s_hWnd = nullptr;
    static WNDPROC s_originalWndProc = nullptr;
    static bool s_showOverlay = true;

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        if (msg == WM_KEYDOWN && wParam == VK_F1) {
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

        return true;
    }

    void Render() {
        if (!s_showOverlay)
            return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8, 8), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.25f);

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
            if (fps >= 60.0f) {
                textColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
            } else if (fps >= 30.0f) {
                textColor = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
            } else {
                textColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            }

            ImGui::TextColored(textColor, "FPS: %.1f", fps);
            ImGui::TextColored(textColor, "Frame: %.1f ms", frameTimeMs);
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

    void Shutdown() {
        if (s_originalWndProc && s_hWnd) {
            SetWindowLongPtr(s_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_originalWndProc));
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}
