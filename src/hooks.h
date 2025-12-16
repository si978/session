#pragma once
#include <Windows.h>
#include <d3d11.h>

namespace Hooks {
    bool Initialize(HMODULE hModule);
    void Shutdown();
    
    extern HMODULE g_hModule;
    extern ID3D11Device* g_pDevice;
    extern ID3D11DeviceContext* g_pContext;
    extern IDXGISwapChain* g_pSwapChain;
}
