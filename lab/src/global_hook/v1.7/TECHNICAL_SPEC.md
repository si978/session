# FPS Overlay 技术方案文档

## 1. 项目概述

### 1.1 目标
开发一个轻量级的 FPS 显示工具，能够在任意 DirectX 游戏中显示实时帧率。

### 1.2 核心功能
- 实时显示游戏帧率
- 显示 VSync 状态
- 支持 DirectX 9/10/11 游戏
- 支持 32 位和 64 位游戏
- 托盘菜单控制

---

## 2. 需求规格

### 2.1 功能需求

| 需求 | 描述 | 优先级 |
|------|------|--------|
| FPS 显示 | 显示估算的实际显示帧率 | 高 |
| VSync 标记 | 显示 `[V]` 表示垂直同步开启 | 高 |
| 颜色提示 | 绿/黄/红 表示帧率健康度 | 中 |
| 托盘控制 | 通过托盘菜单显示/隐藏 | 高 |
| 稳定性 | 退出程序不影响游戏运行 | 高 |

### 2.2 非功能需求

| 需求 | 描述 |
|------|------|
| 性能 | 对游戏帧率影响 < 1% |
| 兼容性 | Windows 10/11, DX9/10/11 |
| 稳定性 | 无崩溃、无卡死 |

---

## 3. 技术架构

### 3.1 组件结构

```
┌─────────────────────────────────────────────────────┐
│                   fps_monitor.exe                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │
│  │ 托盘管理    │  │ 共享内存    │  │ 心跳事件    │  │
│  └─────────────┘  └─────────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────┘
                          │
                          │ SetWindowsHookEx (WH_CBT)
                          ▼
┌─────────────────────────────────────────────────────┐
│                 fps_hook.dll (游戏进程)              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  │
│  │ Present Hook│  │ FPS 计算    │  │ 渲染叠加层  │  │
│  └─────────────┘  └─────────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────┘
```

### 3.2 数据流

```
游戏调用 Present()
       │
       ▼
┌──────────────────┐
│ HookedPresent()  │
│  - 记录时间戳    │
│  - 计算 FPS      │
│  - 检测 VSync    │
│  - 渲染叠加层    │
└──────────────────┘
       │
       ▼
原始 Present() 执行
```

---

## 4. 核心实现

### 4.1 全局钩子注入

```cpp
// 使用 CBT 钩子实现 DLL 注入
HHOOK g_hHook = SetWindowsHookExW(WH_CBT, CBTProc, hModule, 0);

// CBT 回调 - DLL 被加载到目标进程
LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && !g_initialized) {
        g_initialized = true;
        // 检测是否为图形进程，安装 Present Hook
        if (IsGraphicsProcess()) {
            InstallHook();
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}
```

### 4.2 Present Hook (MinHook)

```cpp
// Hook DirectX Present 函数
MH_CreateHook(pPresent, HookedPresent, (LPVOID*)&g_originalPresent);
MH_EnableHook(pPresent);

// Hook 后的 Present 函数
HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // 1. 记录 VSync 状态
    g_lastSyncInterval = SyncInterval;
    
    // 2. 更新 FPS 计算
    UpdateGpuFps();
    
    // 3. 计算显示帧率
    int displayFps = vsyncOn ? min(gpuFps, refreshRate) : gpuFps;
    
    // 4. 渲染叠加层
    RenderFpsOverlay(pSwapChain);
    
    // 5. 调用原始函数
    return g_originalPresent(pSwapChain, SyncInterval, Flags);
}
```

### 4.3 FPS 计算算法

```cpp
static std::deque<LARGE_INTEGER> g_frameTimes;

void UpdateGpuFps() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    // 记录帧时间
    g_frameTimes.push_back(now);
    
    // 移除 1 秒前的记录
    double oneSecondAgo = now.QuadPart - g_frequency.QuadPart;
    while (!g_frameTimes.empty() && g_frameTimes.front().QuadPart < oneSecondAgo) {
        g_frameTimes.pop_front();
    }
    
    // FPS = 1秒内的帧数
    g_gpuFps = (int)g_frameTimes.size();
}
```

### 4.4 显示帧率计算

```cpp
// 估算用户实际看到的帧率
int gpuFps = g_gpuFps.load();
int refreshRate = g_monitorRefreshRate;
bool vsyncOn = (g_lastSyncInterval > 0);

// VSync ON: 被限制到刷新率
// VSync OFF: 等于 GPU 帧率
int displayFps = vsyncOn ? min(gpuFps, refreshRate) : gpuFps;
```

### 4.5 心跳机制

```cpp
// Monitor 创建心跳事件
g_hHeartbeat = CreateEventW(NULL, TRUE, TRUE, L"FpsOverlayHeartbeat");

// DLL 定期检测心跳
DWORD WINAPI HeartbeatThread(LPVOID) {
    while (!g_shouldExit) {
        Sleep(3000);
        HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, L"FpsOverlayHeartbeat");
        if (!hEvent) {
            // Monitor 已退出，停止渲染
            g_renderDisabled = true;
            break;
        }
        CloseHandle(hEvent);
    }
    return 0;
}
```

---

## 5. 关键问题与解决方案

### 5.1 退出导致游戏崩溃

**问题**：调用 `UnhookWindowsHookEx` 会触发 DLL 从所有进程卸载

**解决**：退出时只关闭心跳，不卸载钩子
```cpp
void UnloadHookDll() {
    // 只关闭心跳，不调用 UnhookWindowsHookEx
    if (g_hHeartbeat) {
        CloseHandle(g_hHeartbeat);
        g_hHeartbeat = NULL;
    }
    // DLL 保持加载，但停止渲染
}
```

### 5.2 游戏内热键导致卡死

**问题**：在 Present Hook 中调用 `GetAsyncKeyState` 可能导致死锁

**解决**：移除所有游戏内热键，改用托盘菜单控制
```cpp
// 不在 Hook 中处理热键
// 只读取共享配置
if (g_pConfig) {
    g_visible = g_pConfig->visible;
}
```

### 5.3 系统进程被注入

**问题**：全局钩子会注入到所有进程，包括系统进程

**解决**：进程过滤
```cpp
bool IsGraphicsProcess() {
    char exeName[MAX_PATH];
    GetModuleFileNameA(NULL, exeName, MAX_PATH);
    
    // 排除系统进程
    const char* excluded[] = {
        "csrss.exe", "smss.exe", "services.exe",
        "svchost.exe", "explorer.exe", ...
    };
    
    for (auto& name : excluded) {
        if (strstr(lowerName, name)) return false;
    }
    return true;
}
```

---

## 6. 文件结构

```
v1.7/
├── fps_monitor.exe     # 主程序（托盘）
├── fps_hook64.dll      # 64 位钩子 DLL
├── fps_hook32.dll      # 32 位钩子 DLL
├── fps_config.ini      # 配置文件（自动生成）
├── README.md           # 用户文档
├── TECHNICAL_SPEC.md   # 技术文档
└── preview.html        # 效果预览
```

---

## 7. 配置说明

### fps_config.ini

```ini
[Display]
Position=TopLeft        # TopLeft/TopRight/BottomLeft/BottomRight
OffsetX=10              # X 偏移
OffsetY=10              # Y 偏移
FontSize=14             # 字体大小

[Hotkey]
ToggleKey=112           # VK_F1 (仅托盘菜单生效)
```

---

## 8. 版本历史

| 版本 | 主要变更 |
|------|----------|
| v1.0 | 基础 FPS 显示 |
| v1.3 | 支持 32/64 位，系统进程过滤 |
| v1.5 | 添加 VSync 检测 |
| v1.7 | 稳定版：移除热键，被动卸载策略 |
