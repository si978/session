# 双 FPS 显示实现方案

## 目标

同时显示两个 FPS 指标：
- **GPU FPS**：GPU 提交帧的速率（现有实现）
- **Display FPS**：显示器实际显示帧的速率（需要实现）

---

## 技术方案对比

| 方案 | 复杂度 | 准确度 | 权限要求 | 兼容性 |
|------|--------|--------|----------|--------|
| A. DXGI Frame Statistics | 低 | 中 | 无 | Flip Model 模式 |
| B. ETW 追踪 | 高 | 高 | 管理员 | 全部 |
| C. 刷新率推断 | 低 | 低 | 无 | 全部 |
| D. 混合方案（推荐） | 中 | 中-高 | 无 | 全部 |

---

## 方案 A：DXGI Frame Statistics

### 原理

使用 `IDXGISwapChain::GetFrameStatistics()` 获取实际显示统计：

```cpp
DXGI_FRAME_STATISTICS stats;
HRESULT hr = pSwapChain->GetFrameStatistics(&stats);
if (SUCCEEDED(hr)) {
    // stats.PresentCount     - 实际显示的帧数
    // stats.SyncRefreshCount - 显示器刷新周期计数
    // stats.SyncQPCTime      - 最后显示的时间戳
}
```

### 计算 Display FPS

```cpp
static UINT lastPresentCount = 0;
static LARGE_INTEGER lastTime = {0};

void CalculateDisplayFps(IDXGISwapChain* pSwapChain) {
    DXGI_FRAME_STATISTICS stats;
    if (FAILED(pSwapChain->GetFrameStatistics(&stats))) {
        g_displayFpsAvailable = false;
        return;
    }
    
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    if (lastPresentCount > 0) {
        UINT framesDelta = stats.PresentCount - lastPresentCount;
        double timeDelta = (now.QuadPart - lastTime.QuadPart) / (double)g_frequency.QuadPart;
        
        if (timeDelta >= 1.0) {
            g_displayFps = (int)(framesDelta / timeDelta);
            lastPresentCount = stats.PresentCount;
            lastTime = now;
        }
    } else {
        lastPresentCount = stats.PresentCount;
        lastTime = now;
    }
    
    g_displayFpsAvailable = true;
}
```

### 限制

| 场景 | 支持 | 说明 |
|------|------|------|
| 全屏独占 | ✅ | 完全支持 |
| Flip Model 窗口 | ✅ | Windows 10+ |
| Blit Model 窗口 | ❌ | 返回错误 |
| DX9 | ❌ | 需要单独处理 |

---

## 方案 B：ETW 追踪（PresentMon 方式）

### 原理

捕获 Windows 内核级事件：

```
┌─────────────────────────────────────────────────────────────┐
│  ETW Provider: Microsoft-Windows-DxgKrnl                     │
│  ├─ Event: Present                                           │
│  ├─ Event: PresentHistory                                    │
│  └─ Event: VSyncDPC (显示器刷新)                             │
│                                                              │
│  ETW Provider: Microsoft-Windows-Dwm-Core                    │
│  └─ Event: FlipComplete (DWM 完成帧显示)                     │
└─────────────────────────────────────────────────────────────┘
```

### 实现复杂度

需要：
1. 启动 ETW Session
2. 注册事件回调
3. 解析事件数据
4. 匹配 Present 与 Display 事件

### 代码示例（简化）

```cpp
#include <evntrace.h>
#include <evntcons.h>

// 启动 ETW 追踪
EVENT_TRACE_PROPERTIES* pSessionProperties;
StartTrace(&hSession, L"FpsOverlaySession", pSessionProperties);

// 启用 DxgKrnl Provider
EnableTraceEx2(hSession, &DXGKRNL_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER, ...);

// 事件回调
void WINAPI EventCallback(PEVENT_RECORD pEvent) {
    if (pEvent->EventHeader.ProviderId == DXGKRNL_GUID) {
        // 解析 Present 事件
        if (IsFlipCompleteEvent(pEvent)) {
            RecordDisplayFrame();
        }
    }
}
```

### 优缺点

| 优点 | 缺点 |
|------|------|
| 最准确的数据 | 需要管理员权限 |
| 支持所有模式 | 实现复杂 |
| 与 PresentMon 一致 | 性能开销较大 |

---

## 方案 C：刷新率推断

### 原理

获取显示器刷新率，结合 VSync 状态推断：

```cpp
// 获取显示器刷新率
DEVMODEW devMode = {0};
devMode.dmSize = sizeof(DEVMODEW);
EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &devMode);
int refreshRate = devMode.dmDisplayFrequency;  // 如 60, 144, 240

// VSync 状态推断
if (g_vsyncEnabled) {
    g_displayFps = min(g_gpuFps, refreshRate);
} else {
    g_displayFps = refreshRate;  // 理论上限
}
```

### 限制

- 不是实际测量，只是推断
- 无法检测丢帧
- 无法反映实际性能问题

---

## 方案 D：混合方案（推荐）

### 设计思路

```
┌─────────────────────────────────────────────────────────────┐
│                    Display FPS 获取策略                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. 尝试 DXGI Frame Statistics                              │
│     ├─ 成功 → 使用实际统计数据                               │
│     └─ 失败 → 进入步骤 2                                     │
│                                                              │
│  2. 获取显示器刷新率                                         │
│     ├─ 检测 VSync 状态（通过 SyncInterval 参数）             │
│     ├─ VSync On  → Display FPS = min(GPU FPS, 刷新率)       │
│     └─ VSync Off → Display FPS = 刷新率 (理论值)            │
│                                                              │
│  3. 显示时标注数据来源                                       │
│     ├─ "D: 60" = 实际测量                                    │
│     └─ "D: ~60" = 推断值                                     │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 实现代码

```cpp
// 全局变量
static int g_gpuFps = 0;           // GPU 帧率（现有）
static int g_displayFps = 0;       // 显示帧率
static bool g_displayFpsActual = false;  // 是否为实际测量
static int g_monitorRefreshRate = 60;
static int g_lastSyncInterval = 0;

// Frame Statistics 数据
static UINT g_lastPresentCount = 0;
static UINT g_lastSyncRefreshCount = 0;
static LARGE_INTEGER g_lastStatsTime = {0};
static int g_displayedFrames = 0;

// 初始化：获取显示器刷新率
void InitDisplayInfo() {
    DEVMODEW devMode = {0};
    devMode.dmSize = sizeof(DEVMODEW);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &devMode)) {
        g_monitorRefreshRate = devMode.dmDisplayFrequency;
        if (g_monitorRefreshRate == 0 || g_monitorRefreshRate == 1) {
            g_monitorRefreshRate = 60;  // 默认值
        }
    }
}

// 尝试使用 Frame Statistics
bool TryGetDisplayFpsFromStats(IDXGISwapChain* pSwapChain) {
    DXGI_FRAME_STATISTICS stats;
    HRESULT hr = pSwapChain->GetFrameStatistics(&stats);
    
    if (FAILED(hr)) {
        return false;
    }
    
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    
    if (g_lastPresentCount == 0) {
        // 首次调用，初始化
        g_lastPresentCount = stats.PresentCount;
        g_lastSyncRefreshCount = stats.SyncRefreshCount;
        g_lastStatsTime = now;
        return false;
    }
    
    double elapsed = (double)(now.QuadPart - g_lastStatsTime.QuadPart) / g_frequency.QuadPart;
    
    if (elapsed >= 1.0) {
        // 计算过去 1 秒实际显示的帧数
        UINT framesDelta = stats.PresentCount - g_lastPresentCount;
        g_displayFps = (int)(framesDelta / elapsed);
        
        g_lastPresentCount = stats.PresentCount;
        g_lastSyncRefreshCount = stats.SyncRefreshCount;
        g_lastStatsTime = now;
        
        return true;
    }
    
    return true;  // 使用上次的值
}

// 推断 Display FPS
void InferDisplayFps() {
    if (g_lastSyncInterval > 0) {
        // VSync 开启，锁定到刷新率
        g_displayFps = min(g_gpuFps, g_monitorRefreshRate);
    } else {
        // VSync 关闭，显示器以固定刷新率显示
        // 但实际帧可能重复或撕裂
        g_displayFps = g_monitorRefreshRate;
    }
}

// 主更新函数
void UpdateDisplayFps(IDXGISwapChain* pSwapChain) {
    // 优先使用 Frame Statistics
    if (TryGetDisplayFpsFromStats(pSwapChain)) {
        g_displayFpsActual = true;
    } else {
        // 回退到推断
        InferDisplayFps();
        g_displayFpsActual = false;
    }
}

// 在 HookedPresent 中调用
HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    g_lastSyncInterval = SyncInterval;  // 记录 VSync 状态
    
    UpdateGpuFps();           // 现有：计算 GPU FPS
    UpdateDisplayFps(pSwapChain);  // 新增：计算 Display FPS
    
    RenderDualFpsOverlay(pSwapChain);  // 渲染双 FPS
    
    return g_originalPresent(pSwapChain, SyncInterval, Flags);
}
```

### 渲染显示

```cpp
void RenderDualFpsOverlay(IDXGISwapChain* pSwapChain) {
    char text[64];
    
    if (g_displayFpsActual) {
        // 实际测量值
        sprintf(text, "G:%d D:%d", g_gpuFps, g_displayFps);
    } else {
        // 推断值，用 ~ 标记
        sprintf(text, "G:%d D:~%d", g_gpuFps, g_displayFps);
    }
    
    // 或者更详细的显示
    // sprintf(text, "GPU:%d\nDisplay:%d\n%dHz %s", 
    //         g_gpuFps, g_displayFps, g_monitorRefreshRate,
    //         g_lastSyncInterval > 0 ? "VSync" : "");
    
    RenderText(pSwapChain, text);
}
```

---

## 显示设计（已实现）

### 最终显示格式

```
┌─────────────────────┐
│ FPS G:120 D:60      │  ← 实际测量值
└─────────────────────┘

┌─────────────────────┐
│ FPS G:120 D:~60     │  ← 推断值（~ 标记）
└─────────────────────┘
```

### 颜色编码

| 部分 | 颜色规则 |
|------|----------|
| "FPS " | 白色 |
| "G:xxx" | 绿(≥60) / 黄(30-59) / 红(<30) |
| "D:xxx" 或 "D:~xxx" | 绿(≥60) / 黄(30-59) / 红(<30) |

### 说明

- **G:** = GPU FPS（Present 调用频率）
- **D:** = Display FPS（实际显示帧率）
- **D:~** = 推断的 Display FPS（Frame Statistics 不可用时）

---

## 实现步骤

### Phase 1：基础框架

1. 添加 Display FPS 相关全局变量
2. 实现 `InitDisplayInfo()` 获取刷新率
3. 修改 `HookedPresent` 记录 SyncInterval

### Phase 2：Frame Statistics

1. 实现 `TryGetDisplayFpsFromStats()`
2. 处理各种错误情况
3. 添加 1 秒采样窗口

### Phase 3：推断回退

1. 实现 `InferDisplayFps()`
2. VSync 状态检测
3. 标记数据来源

### Phase 4：渲染更新

1. 修改渲染代码支持双行显示
2. 调整布局和颜色
3. 添加配置选项

### Phase 5：DX9 支持

1. DX9 使用 `IDirect3DSwapChain9::GetPresentStats()`
2. 返回 `D3DPRESENTSTATS` 结构
3. 类似的计算逻辑

---

## 预期结果

### 场景一：全屏游戏 + VSync Off

```
G:200 D:144
        └─ 显示器 144Hz
```

### 场景二：窗口游戏 + VSync On

```
G:60 D:60
      └─ 锁定到刷新率
```

### 场景三：GPU 瓶颈

```
G:45 D:45
      └─ 低于刷新率，有卡顿
```

### 场景四：统计不可用

```
G:120 D:~60
        └─ 推断值（窗口 Blit 模式）
```

---

## 风险与限制

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| Frame Statistics 不可用 | 无法获取实际值 | 回退到推断 |
| 窗口模式限制 | Blit 模式不支持 | 提示用户 |
| 多显示器 | 刷新率可能不同 | 获取当前窗口所在显示器 |
| DX12 差异 | 统计 API 不同 | 需要额外适配 |

---

## 文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `fps_hook.cpp` | 添加 Display FPS 计算逻辑 |
| `fps_config.h` | 添加配置项（显示模式、详细/简洁） |
| `fps_monitor.cpp` | 托盘菜单添加显示模式切换 |

---

## 总结

**推荐方案 D（混合方案）**：

1. 首选 DXGI Frame Statistics（最准确）
2. 失败时回退到刷新率推断
3. 通过标记区分数据来源
4. 无需管理员权限，兼容性好

这个方案在准确性和实用性之间取得平衡，同时保持实现的简洁性。
