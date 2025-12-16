# 全局钩子 FPS 显示技术实现

## 概述

本文档记录了一种通用的游戏 FPS 显示方案，使用 Windows 全局钩子技术实现"一次部署，所有游戏自动生效"的效果。

**核心特点：**
- 无需针对每个游戏单独配置
- 自动检测并注入所有 DirectX 游戏
- 支持全屏独占模式
- 在 LOL 等带反作弊的游戏中也能工作

---

## 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                     fps_monitor.exe                          │
│                     (托盘程序)                                │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  1. 加载 fps_hook.dll                                    │ │
│  │  2. 调用 SetWindowsHookEx(WH_CBT, ...)                  │ │
│  │  3. 安装全局钩子                                         │ │
│  │  4. 保持运行，维持钩子生命周期                            │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ Windows 自动注入
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                    任意新进程                                 │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  fps_hook.dll 被自动加载                                  │ │
│  │                                                          │ │
│  │  DllMain()                                               │ │
│  │      ↓                                                   │ │
│  │  CBTProc() 被调用                                        │ │
│  │      ↓                                                   │ │
│  │  启动监控线程                                             │ │
│  │      ↓                                                   │ │
│  │  检测 D3D11.dll 是否加载                                  │ │
│  │      ↓                                                   │ │
│  │  [是游戏] → Hook Present() → 显示 FPS                    │ │
│  │  [非游戏] → 不做任何事                                    │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## 技术实现细节

### 1. 全局钩子安装

使用 `SetWindowsHookEx` 安装 `WH_CBT`（Computer-Based Training）钩子：

```cpp
// 安装全局钩子
HHOOK g_hHook = SetWindowsHookExW(
    WH_CBT,          // 钩子类型：窗口创建/激活等事件
    CBTProc,         // 回调函数
    g_hModule,       // DLL 模块句柄
    0                // 0 = 全局钩子，影响所有线程
);
```

**WH_CBT 钩子触发时机：**
- 窗口创建 (HCBT_CREATEWND)
- 窗口激活 (HCBT_ACTIVATE)
- 窗口最小化/最大化
- 等等...

**关键点：** 当钩子被触发时，Windows 会自动将 DLL 加载到目标进程中。

### 2. 进程内初始化

```cpp
LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && !g_initialized) {
        g_initialized = true;
        
        // 启动监控线程，持续检测 D3D
        CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}
```

### 3. 游戏进程识别

```cpp
bool IsGraphicsProcess() {
    // 排除系统进程
    char* fileName = GetCurrentProcessName();
    if (_stricmp(fileName, "fps_monitor.exe") == 0) return false;
    if (_stricmp(fileName, "conhost.exe") == 0) return false;
    if (_stricmp(fileName, "explorer.exe") == 0) return false;
    // ... 其他系统进程
    
    // 检测是否加载了 DirectX
    return GetModuleHandleW(L"d3d11.dll") != NULL || 
           GetModuleHandleW(L"dxgi.dll") != NULL;
}
```

**识别逻辑：**
1. 排除已知的系统进程（避免不必要的 Hook）
2. 检测进程是否加载了 DirectX DLL
3. 加载了 D3D = 可能是游戏 = 需要 Hook

### 4. 延迟检测机制

游戏启动时，D3D 可能还没加载，所以需要持续监控：

```cpp
DWORD WINAPI MonitorThread(LPVOID) {
    // 持续检测 30 秒
    for (int i = 0; i < 30; i++) {
        Sleep(1000);
        
        if (!g_hooked && IsGraphicsProcess()) {
            // 检测到 D3D 加载，安装 Hook
            InstallHook();
            break;
        }
    }
    return 0;
}
```

### 5. Present 函数 Hook

使用 MinHook 库拦截 `IDXGISwapChain::Present`：

```cpp
bool InstallHook() {
    // 初始化 MinHook
    MH_Initialize();
    
    // 获取 Present 函数地址
    void* presentAddr = GetPresentAddress();
    
    // 创建 Hook
    MH_CreateHook(
        presentAddr,           // 原函数地址
        &HookedPresent,        // 我们的函数
        (void**)&g_originalPresent  // 保存原函数指针
    );
    
    // 启用 Hook
    MH_EnableHook(presentAddr);
    
    return true;
}
```

### 6. 获取 Present 地址

通过创建临时 D3D 设备获取 VTable：

```cpp
void* GetPresentAddress() {
    // 创建临时窗口
    HWND hWnd = CreateWindowEx(...);
    
    // 创建 D3D 设备和交换链
    DXGI_SWAP_CHAIN_DESC sd = {...};
    IDXGISwapChain* pSwapChain;
    ID3D11Device* pDevice;
    
    D3D11CreateDeviceAndSwapChain(..., &pSwapChain, &pDevice, ...);
    
    // 从 VTable 获取 Present 地址
    void** vtable = *(void***)pSwapChain;
    void* presentAddr = vtable[8];  // Present 在索引 8
    
    // 清理
    pSwapChain->Release();
    pDevice->Release();
    DestroyWindow(hWnd);
    
    return presentAddr;
}
```

**IDXGISwapChain VTable 布局：**
| 索引 | 函数 |
|------|------|
| 0 | QueryInterface |
| 1 | AddRef |
| 2 | Release |
| ... | ... |
| 8 | **Present** |
| 9 | GetBuffer |
| ... | ... |

### 7. FPS 计算

使用滑动窗口算法：

```cpp
static std::deque<LARGE_INTEGER> g_frameTimes;

void UpdateFps() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    // 记录当前帧时间
    g_frameTimes.push_back(now);
    
    // 移除超过 2 秒的旧记录
    double twoSecondsAgo = now.QuadPart - 2 * g_frequency.QuadPart;
    while (!g_frameTimes.empty() && 
           g_frameTimes.front().QuadPart < twoSecondsAgo) {
        g_frameTimes.pop_front();
    }
    
    // 计算过去 1 秒内的帧数 = FPS
    double oneSecondAgo = now.QuadPart - g_frequency.QuadPart;
    int frameCount = 0;
    for (auto& t : g_frameTimes) {
        if (t.QuadPart >= oneSecondAgo) {
            frameCount++;
        }
    }
    
    g_currentFps = frameCount;
}
```

### 8. FPS 渲染

在 Hook 的 Present 中绘制 FPS：

```cpp
HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, 
                              UINT SyncInterval, UINT Flags) {
    // 更新 FPS
    UpdateFps();
    
    // 获取 BackBuffer
    ID3D11Texture2D* pBackBuffer;
    pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    
    // 创建 Staging 纹理用于 CPU 写入
    ID3D11Texture2D* pStaging;
    // ... 创建并复制
    
    // Map 纹理，直接写入像素
    D3D11_MAPPED_SUBRESOURCE mapped;
    pContext->Map(pStaging, 0, D3D11_MAP_READ_WRITE, 0, &mapped);
    
    // 绘制 FPS 文字（位图字体）
    DrawString(mapped.pData, "FPS:60", color);
    
    // Unmap 并复制回 BackBuffer
    pContext->Unmap(pStaging, 0);
    pContext->CopyResource(pBackBuffer, pStaging);
    
    // 调用原始 Present
    return g_originalPresent(pSwapChain, SyncInterval, Flags);
}
```

---

## 关键技术点

### 共享数据段

钩子句柄需要在所有进程间共享：

```cpp
#pragma data_seg(".shared")
HHOOK g_hHook = NULL;
#pragma data_seg()
#pragma comment(linker, "/SECTION:.shared,RWS")
```

### 位图字体渲染

使用 5x7 像素的位图字体，避免依赖 GDI/DirectWrite：

```cpp
// 数字 "0" 的 5x7 位图
static const unsigned char font_0[] = {
    0x0E,  // .###.
    0x11,  // #...#
    0x13,  // #..##
    0x15,  // #.#.#
    0x19,  // ##..#
    0x11,  // #...#
    0x0E   // .###.
};

void DrawChar(BYTE* pixels, int x, int y, char c, DWORD color) {
    const unsigned char* glyph = GetGlyph(c);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (glyph[row] & (0x10 >> col)) {
                SetPixel(pixels, x + col, y + row, color);
            }
        }
    }
}
```

---

## 文件结构

```
global_hook/
├── fps_hook.cpp      # Hook DLL 源码
├── fps_monitor.cpp   # 托盘程序源码
├── CMakeLists.txt    # 构建配置
└── output/
    ├── fps_hook.dll      # Hook DLL (30KB)
    └── fps_monitor.exe   # 托盘程序 (30KB)
```

---

## 为什么能在 LOL 等游戏中工作？

1. **合法的 Windows 机制**
   - SetWindowsHookEx 是 Windows 官方 API
   - 不是恶意注入，是标准的钩子机制

2. **被动加载**
   - 我们不主动注入进程
   - Windows 自己把 DLL 加载进去

3. **行为无害**
   - 只 Hook 渲染函数
   - 不修改游戏逻辑
   - 不读取游戏数据
   - 只添加 UI 覆盖

4. **全局钩子的特殊性**
   - 反作弊难以完全禁用全局钩子
   - 禁用会影响很多正常软件（如输入法、屏幕阅读器）

---

## 局限性

1. **仅支持 DirectX 11**
   - 需要额外开发支持 DX9/DX12/OpenGL/Vulkan

2. **进程排除列表需维护**
   - 新的系统进程可能需要手动添加到排除列表

3. **32/64 位兼容**
   - 当前只编译了 64 位版本
   - 32 位游戏需要单独的 32 位 DLL

4. **性能开销**
   - 每帧都要复制和修改 BackBuffer
   - 对低端机器可能有轻微影响

---

## 总结

这个方案通过巧妙利用 Windows 全局钩子机制，实现了：

| 特性 | 实现方式 |
|------|----------|
| 自动注入所有进程 | SetWindowsHookEx WH_CBT |
| 识别游戏进程 | 检测 D3D11.dll 加载 |
| 拦截渲染 | MinHook + VTable Hook |
| 显示 FPS | 直接修改 BackBuffer 像素 |
| 兼容反作弊 | 使用合法 Windows API |

**核心思想：让 Windows 帮我们把代码送进游戏进程，然后检测是否需要工作。**

---

*文档版本：1.0*  
*创建日期：2024年12月*  
*状态：已验证可用*
