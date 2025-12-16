#pragma once
#include <Windows.h>
#include <d3d11.h>

namespace Overlay {
    bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
    void Render();
    void Shutdown();
    void InvalidateDeviceObjects();
    void CreateDeviceObjects();
    void SetVisible(bool visible);
    void SetPosition(float x, float y);
    void SetAlpha(float alpha);
}
