# FRAPS 技术分析

## FRAPS 概述

FRAPS (Frame Rate Per Second) 是一款经典的游戏帧率显示和录制软件，已存在20+年。

**官网**: https://fraps.com/

---

## 核心技术实现

### 1. 工作原理

```
FRAPS 进程
    ↓
DLL 注入到游戏进程
    ↓
Hook DirectX/OpenGL API
    ↓
在渲染帧上绘制 FPS
    ↓
调用原始函数完成渲染
```

### 2. Hook 目标函数

| 图形 API | Hook 函数 | 作用 |
|----------|-----------|------|
| DirectX 9 | `IDirect3DDevice9::Present` | 显示帧 |
| DirectX 9 | `IDirect3DDevice9::EndScene` | 结束渲染 |
| DirectX 10/11 | `IDXGISwapChain::Present` | 显示帧 |
| DirectX 12 | `IDXGISwapChain::Present` | 显示帧 |
| OpenGL | `SwapBuffers()` | 交换缓冲区 |

### 3. FPS 计算

```cpp
// 伪代码
void HookedPresent() {
    static LARGE_INTEGER lastTime;
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    
    // 计算帧时间
    double frameTime = (currentTime - lastTime) / frequency;
    
    // 计算 FPS
    double fps = 1.0 / frameTime;
    
    // 可能取平均值以平滑显示
    averageFps = (averageFps * 0.9) + (fps * 0.1);
    
    // 绘制 FPS 覆盖
    DrawFpsOverlay(averageFps);
    
    // 调用原始 Present
    OriginalPresent();
    
    lastTime = currentTime;
}
```

### 4. 覆盖层绘制

```cpp
void DrawFpsOverlay(double fps) {
    // 获取 BackBuffer
    IDirect3DSurface9* backBuffer;
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    
    // 使用 D3DX 或 GDI 绘制文本
    // FRAPS 使用预渲染的字体纹理
    DrawText(backBuffer, fps);
}
```

---

## 为什么 FRAPS 能在 LOL 中工作？

### 可能原因分析

| 原因 | 可能性 | 说明 |
|------|--------|------|
| **白名单机制** | 高 | FRAPS 是知名软件，可能被反作弊白名单 |
| **历史兼容** | 高 | FRAPS 存在20+年，反作弊需要兼容它 |
| **注入方式** | 中 | 可能使用更隐蔽的注入技术 |
| **签名验证** | 中 | FRAPS 有合法的代码签名 |
| **行为检测** | 中 | 只读取不修改游戏数据，不触发警报 |

### 反作弊系统的检测逻辑

反作弊主要检测：
1. ❌ 修改游戏内存（作弊）
2. ❌ 修改游戏代码（破解）
3. ❌ 读取敏感游戏数据（透视）
4. ❌ 模拟输入（自动瞄准）

FRAPS 的行为：
1. ✅ 只 Hook 渲染函数
2. ✅ 不修改游戏逻辑
3. ✅ 不读取游戏数据
4. ✅ 只添加 UI 覆盖

**结论：FRAPS 的行为模式不触发反作弊的核心检测逻辑**

---

## 我们的方案 vs FRAPS

### 对比

| 方面 | 我们的注入版 | FRAPS |
|------|-------------|-------|
| 注入方式 | CreateRemoteThread | 可能更隐蔽 |
| 代码签名 | 无 | 有合法签名 |
| 软件历史 | 新软件 | 20+年历史 |
| 白名单 | 无 | 可能有 |
| 功能 | 只显示 FPS | FPS + 录制 + 截图 |

### 可能的改进方向

1. **代码签名**
   - 获取 EV 代码签名证书
   - 签名我们的 DLL

2. **更隐蔽的注入**
   - 使用 Manual Map 而不是 LoadLibrary
   - 使用 APC 注入而不是 CreateRemoteThread
   - Hook 系统 DLL 而不是直接注入

3. **模仿 FRAPS 行为**
   - 确保只 Hook 渲染函数
   - 不触碰任何游戏逻辑
   - 最小化内存占用

---

## 技术实现方案

### 方案 1：改进现有注入方式

```cpp
// 使用 Manual Map 注入
bool ManualMapInject(HANDLE hProcess, const BYTE* dllData) {
    // 1. 在目标进程分配内存
    // 2. 复制 DLL 到目标进程
    // 3. 手动处理导入表
    // 4. 手动处理重定位
    // 5. 调用 DllMain
    // 不使用 LoadLibrary，更难被检测
}
```

### 方案 2：使用 D3D Wrapper DLL

```
游戏目录:
├── game.exe
├── d3d11.dll      ← 我们的包装器
└── d3d11_orig.dll ← 原始系统 DLL（重命名）

工作流程:
game.exe 加载 d3d11.dll (我们的)
    ↓
我们的 DLL 加载 d3d11_orig.dll
    ↓
转发所有调用，同时 Hook Present
    ↓
绘制 FPS 覆盖
```

**优点**：不需要注入，游戏主动加载我们的 DLL

### 方案 3：全局钩子

```cpp
// 使用 SetWindowsHookEx 安装全局钩子
HHOOK hook = SetWindowsHookEx(WH_CBT, HookProc, hDll, 0);

// 当任何进程加载时，我们的 DLL 也会被加载
// 然后在 DLL_PROCESS_ATTACH 中检查是否是目标游戏
```

---

## 下一步建议

### 优先级 1：D3D Wrapper 方案
- 不需要运行时注入
- 被检测风险最低
- 实现相对简单

### 优先级 2：改进注入方式
- 使用 Manual Map
- 添加代码签名
- 模仿 FRAPS 行为

### 优先级 3：研究 FRAPS 具体实现
- 逆向分析 FRAPS
- 了解其具体注入机制
- 复制其成功模式

---

## 结论

FRAPS 能在 LOL 中工作，主要因为：
1. 它是一个有20+年历史的合法软件
2. 可能被反作弊系统白名单
3. 只 Hook 渲染函数，不触碰游戏逻辑
4. 有合法的代码签名

我们可以通过 **D3D Wrapper** 方案来实现类似效果，避免运行时注入带来的检测风险。
