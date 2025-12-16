#include "overlay_window.h"
#include <dwmapi.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

static OverlayWindow* g_overlay = nullptr;

OverlayWindow::OverlayWindow() {
    g_overlay = this;
    QueryPerformanceFrequency(&m_frequency);
    m_lastPresentTime.QuadPart = 0;
}

OverlayWindow::~OverlayWindow() {
    Destroy();
    g_overlay = nullptr;
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool OverlayWindow::Create(HWND targetWindow) {
    m_targetWindow = targetWindow;
    
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FPSOverlayWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    
    // Get target window position
    RECT targetRect;
    GetWindowRect(targetWindow, &targetRect);
    
    // Position at top-right corner of target
    int x = targetRect.right - m_width - 20;
    int y = targetRect.top + 50;
    
    m_hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"FPSOverlayWindowClass",
        L"",
        WS_POPUP,
        x, y, m_width, m_height,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );
    
    if (!m_hWnd) return false;
    
    // Set transparency
    SetLayeredWindowAttributes(m_hWnd, RGB(0, 0, 0), 255, LWA_COLORKEY);
    
    if (!InitD2D()) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
        return false;
    }
    
    ShowWindow(m_hWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(m_hWnd);
    
    return true;
}

bool OverlayWindow::InitD2D() {
    HRESULT hr;
    
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
    if (FAILED(hr)) return false;
    
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory),
                              (IUnknown**)&m_pDWriteFactory);
    if (FAILED(hr)) return false;
    
    hr = m_pDWriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        16.0f,
        L"en-us",
        &m_pTextFormat
    );
    if (FAILED(hr)) return false;
    
    m_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    
    hr = m_pD2DFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(m_width, m_height)),
        &m_pRenderTarget
    );
    if (FAILED(hr)) return false;
    
    m_pRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.75f),
        &m_pBrushBg
    );
    
    m_pRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 1.0f, 0.4f, 1.0f),
        &m_pBrushText
    );
    
    return true;
}

void OverlayWindow::Destroy() {
    m_running = false;
    
    if (m_renderThread) {
        WaitForSingleObject(m_renderThread, 2000);
        CloseHandle(m_renderThread);
        m_renderThread = nullptr;
    }
    
    if (m_pBrushText) { m_pBrushText->Release(); m_pBrushText = nullptr; }
    if (m_pBrushBg) { m_pBrushBg->Release(); m_pBrushBg = nullptr; }
    if (m_pTextFormat) { m_pTextFormat->Release(); m_pTextFormat = nullptr; }
    if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
    if (m_pDWriteFactory) { m_pDWriteFactory->Release(); m_pDWriteFactory = nullptr; }
    if (m_pD2DFactory) { m_pD2DFactory->Release(); m_pD2DFactory = nullptr; }
    
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

void OverlayWindow::SetVisible(bool visible) {
    m_visible = visible;
    if (m_hWnd) {
        ShowWindow(m_hWnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
}

void OverlayWindow::UpdatePosition() {
    if (!m_hWnd || !m_targetWindow) return;
    if (!IsWindow(m_targetWindow)) return;
    
    RECT targetRect;
    GetWindowRect(m_targetWindow, &targetRect);
    
    int x = targetRect.right - m_width - 20;
    int y = targetRect.top + 50;
    
    SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, 0, 0, 
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OverlayWindow::StartFpsCounter(DWORD targetPid) {
    m_targetPid = targetPid;
    m_running = true;
    m_renderThread = CreateThread(nullptr, 0, RenderThread, this, 0, nullptr);
}

DWORD WINAPI OverlayWindow::RenderThread(LPVOID param) {
    OverlayWindow* self = (OverlayWindow*)param;
    
    // Use DWM timing for FPS calculation
    LARGE_INTEGER lastTime, currentTime;
    QueryPerformanceCounter(&lastTime);
    
    int frameCount = 0;
    double fpsAccum = 0;
    int fpsCount = 0;
    
    while (self->m_running) {
        QueryPerformanceCounter(&currentTime);
        
        // Get DWM timing info (refresh-based)
        DWM_TIMING_INFO timingInfo = {0};
        timingInfo.cbSize = sizeof(DWM_TIMING_INFO);
        
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timingInfo))) {
            // Calculate FPS based on DWM refresh
            static UINT64 lastRefreshCount = 0;
            static LARGE_INTEGER lastRefreshTime = {0};
            
            if (lastRefreshCount != 0) {
                UINT64 refreshDiff = timingInfo.cRefresh - lastRefreshCount;
                double timeDiff = (double)(currentTime.QuadPart - lastRefreshTime.QuadPart) / self->m_frequency.QuadPart;
                
                if (timeDiff > 0.1) { // Update every 100ms
                    double currentFps = (double)refreshDiff / timeDiff;
                    
                    // Smooth FPS
                    fpsAccum += currentFps;
                    fpsCount++;
                    
                    if (fpsCount >= 5) {
                        self->m_fps = fpsAccum / fpsCount;
                        fpsAccum = 0;
                        fpsCount = 0;
                    }
                    
                    lastRefreshCount = timingInfo.cRefresh;
                    lastRefreshTime = currentTime;
                }
            } else {
                lastRefreshCount = timingInfo.cRefresh;
                lastRefreshTime = currentTime;
            }
        }
        
        // Render
        self->Render();
        
        Sleep(16); // ~60fps render loop
    }
    
    return 0;
}

void OverlayWindow::Render() {
    if (!m_pRenderTarget || !m_visible) return;
    
    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    
    double fps = m_fps;
    
    // Background rounded rect
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
        D2D1::RectF(0, 0, (float)m_width, (float)m_height),
        4.0f, 4.0f
    );
    m_pRenderTarget->FillRoundedRectangle(roundedRect, m_pBrushBg);
    
    // FPS color based on value
    D2D1_COLOR_F textColor;
    if (fps >= 55) {
        textColor = D2D1::ColorF(0.0f, 1.0f, 0.4f);
    } else if (fps >= 30) {
        textColor = D2D1::ColorF(1.0f, 0.8f, 0.0f);
    } else {
        textColor = D2D1::ColorF(1.0f, 0.2f, 0.2f);
    }
    m_pBrushText->SetColor(textColor);
    
    // Draw FPS text
    wchar_t text[32];
    if (fps > 0) {
        swprintf_s(text, L"FPS: %.0f", fps);
    } else {
        swprintf_s(text, L"FPS: --");
    }
    
    m_pRenderTarget->DrawText(
        text, (UINT32)wcslen(text),
        m_pTextFormat,
        D2D1::RectF(0, 0, (float)m_width, (float)m_height),
        m_pBrushText
    );
    
    m_pRenderTarget->EndDraw();
}
