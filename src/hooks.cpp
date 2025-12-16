#include "hooks.h"
#include "fps_counter.h"
#include "overlay.h"
#include "logger.h"
#include <dxgi.h>
#include <d3d11.h>
#include <MinHook.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace Hooks {
    HMODULE g_hModule = nullptr;
    ID3D11Device* g_pDevice = nullptr;
    ID3D11DeviceContext* g_pContext = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    PresentFn oPresent = nullptr;
    ResizeBuffersFn oResizeBuffers = nullptr;

    bool g_initialized = false;

    void CreateRenderTarget() {
        ID3D11Texture2D* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer) {
            g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
            pBackBuffer->Release();
        }
    }

    void CleanupRenderTarget() {
        if (g_pRenderTargetView) {
            g_pRenderTargetView->Release();
            g_pRenderTargetView = nullptr;
        }
    }

    HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!g_initialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&g_pDevice)))) {
                g_pDevice->GetImmediateContext(&g_pContext);
                g_pSwapChain = pSwapChain;

                DXGI_SWAP_CHAIN_DESC desc;
                pSwapChain->GetDesc(&desc);

                Overlay::Initialize(desc.OutputWindow, g_pDevice, g_pContext);
                CreateRenderTarget();
                g_initialized = true;
            }
        }

        if (g_initialized) {
            FpsCounter::Update();
            
            g_pContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
            Overlay::Render();
        }

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, 
                                       UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT Flags) {
        CleanupRenderTarget();
        Overlay::InvalidateDeviceObjects();
        
        HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, Flags);
        
        CreateRenderTarget();
        Overlay::CreateDeviceObjects();
        
        return hr;
    }

    void* GetVTableFunction(void* pInterface, UINT index) {
        return (*static_cast<void***>(pInterface))[index];
    }

    bool CreateDummySwapChain(IDXGISwapChain** ppSwapChain) {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0, 0, 
                          GetModuleHandle(nullptr), nullptr, nullptr, nullptr, 
                          nullptr, L"DummyClass", nullptr };
        RegisterClassEx(&wc);
        
        HWND hWnd = CreateWindow(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 
                                  0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        D3D_FEATURE_LEVEL featureLevel;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, ppSwapChain, &pDevice, &featureLevel, &pContext
        );

        if (pDevice) pDevice->Release();
        if (pContext) pContext->Release();
        
        DestroyWindow(hWnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);

        return SUCCEEDED(hr);
    }

    bool Initialize(HMODULE hModule) {
        g_hModule = hModule;
        Logger::Initialize();
        LOG("Hooks::Initialize started");

        IDXGISwapChain* pDummySwapChain = nullptr;
        if (!CreateDummySwapChain(&pDummySwapChain)) {
            LOG_ERROR("Failed to create dummy swap chain");
            return false;
        }
        LOG("Dummy swap chain created");

        void* pPresent = GetVTableFunction(pDummySwapChain, 8);       // Present is index 8
        void* pResizeBuffers = GetVTableFunction(pDummySwapChain, 13); // ResizeBuffers is index 13

        pDummySwapChain->Release();

        if (MH_Initialize() != MH_OK) {
            return false;
        }

        if (MH_CreateHook(pPresent, &hkPresent, reinterpret_cast<void**>(&oPresent)) != MH_OK) {
            return false;
        }

        if (MH_CreateHook(pResizeBuffers, &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers)) != MH_OK) {
            return false;
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            LOG_ERROR("Failed to enable hooks");
            return false;
        }

        LOG("All hooks enabled successfully");
        return true;
    }

    void Shutdown() {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        Overlay::Shutdown();
        CleanupRenderTarget();

        if (g_pContext) g_pContext->Release();
        if (g_pDevice) g_pDevice->Release();
    }
}
