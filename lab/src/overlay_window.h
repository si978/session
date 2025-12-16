#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <atomic>
#include <deque>

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();
    
    bool Create(HWND targetWindow);
    void Destroy();
    void SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }
    void UpdatePosition();
    void StartFpsCounter(DWORD targetPid);
    
private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI RenderThread(LPVOID param);
    bool InitD2D();
    void Render();
    void CalculateFps();
    
    HWND m_hWnd = nullptr;
    HWND m_targetWindow = nullptr;
    bool m_visible = true;
    std::atomic<bool> m_running{false};
    HANDLE m_renderThread = nullptr;
    
    ID2D1Factory* m_pD2DFactory = nullptr;
    ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
    ID2D1SolidColorBrush* m_pBrushBg = nullptr;
    ID2D1SolidColorBrush* m_pBrushText = nullptr;
    IDWriteFactory* m_pDWriteFactory = nullptr;
    IDWriteTextFormat* m_pTextFormat = nullptr;
    
    DWORD m_targetPid = 0;
    std::atomic<double> m_fps{0.0};
    
    // For DWM-based FPS calculation
    LARGE_INTEGER m_frequency;
    std::deque<LARGE_INTEGER> m_frameTimes;
    LARGE_INTEGER m_lastPresentTime;
    
    int m_width = 110;
    int m_height = 32;
};
