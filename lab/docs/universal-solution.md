# 通用 FPS 显示方案分析

## 需求
- 一次部署，所有游戏自动生效
- 不需要针对每个游戏单独配置
- 兼容全屏独占模式
- 兼容有反作弊的游戏

---

## 全局注入技术对比

| 方法 | 原理 | 优点 | 缺点 |
|------|------|------|------|
| **SetWindowsHookEx** | 全局钩子 | 简单实现 | 仅限 user32.dll 进程 |
| **AppInit_DLLs** | 注册表 | 系统级 | Win10 Secure Boot 禁用 |
| **驱动注入** | 内核级 | 最强大 | 需要签名驱动 |
| **IAT Hook** | 导入表 | 隐蔽 | 需要先注入 |

---

## RTSS 的实现方式

根据研究，RTSS 使用的是**驱动级全局注入**：

```
RTSSHooks64.sys (内核驱动)
        ↓
监控所有进程创建
        ↓
对图形进程注入 Hook DLL
        ↓
Hook Present 函数
        ↓
绘制 OSD 覆盖
```

**关键技术：**
1. 内核驱动监控进程创建 (PsSetCreateProcessNotifyRoutine)
2. 在进程初始化早期注入 DLL
3. 使用 APC 或 Manual Map 注入
4. Hook 所有图形 API (DX9/DX10/DX11/DX12/OpenGL/Vulkan)

---

## 可行方案

### 方案 1：全局钩子 (SetWindowsHookEx)

**原理：**
```cpp
// 安装全局钩子
HHOOK g_hook = SetWindowsHookEx(WH_CBT, HookProc, g_hDll, 0);

// 当任何窗口创建时，我们的 DLL 被加载到该进程
LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 检查是否是目标游戏进程
    if (IsTargetGame()) {
        // 初始化 DX Hook
        InitializeDxHook();
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}
```

**限制：**
- 仅对加载 user32.dll 的进程有效
- 大多数游戏都加载 user32.dll，所以基本可用
- 某些保护进程可能拒绝

**实现难度：** ⭐⭐ 中等

---

### 方案 2：驱动级注入

**架构：**
```
┌─────────────────────────────────────────┐
│            用户态服务                    │
│  - 托盘程序                              │
│  - 配置管理                              │
│  - 与驱动通信                            │
└─────────────────────┬───────────────────┘
                      │ IOCTL
┌─────────────────────┴───────────────────┐
│            fps_monitor.sys               │
│  - 监控进程创建                          │
│  - APC 注入 DLL                          │
│  - 管理注入列表                          │
└─────────────────────┬───────────────────┘
                      │ 注入
┌─────────────────────┴───────────────────┐
│            fps_overlay.dll               │
│  - Hook DX/OpenGL/Vulkan                 │
│  - 计算 FPS                              │
│  - 渲染覆盖                              │
└─────────────────────────────────────────┘
```

**核心代码：**
```c
// 驱动：监控进程创建
VOID ProcessNotifyCallback(HANDLE ParentId, HANDLE ProcessId, BOOLEAN Create) {
    if (Create) {
        // 检查是否需要注入
        if (ShouldInject(ProcessId)) {
            // 使用 APC 注入 DLL
            QueueApcInject(ProcessId, L"fps_overlay.dll");
        }
    }
}

// 注册回调
PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, FALSE);
```

**实现难度：** ⭐⭐⭐⭐⭐ 高

---

### 方案 3：混合方案（推荐）

**阶段 1：使用 SetWindowsHookEx（快速实现）**
- 覆盖大部分游戏
- 无需驱动开发
- 2-3 天可完成

**阶段 2：升级到驱动方案（完整支持）**
- 解决全屏独占问题
- 解决反作弊兼容
- 需要数周开发

---

## 推荐实施路线

### 立即可做：SetWindowsHookEx 方案

```
fps_monitor.exe (托盘程序)
        ↓
SetWindowsHookEx 安装全局钩子
        ↓
任何进程创建窗口时
        ↓
fps_hook.dll 被自动加载
        ↓
检测是否是游戏进程
        ↓ 是
Hook Present，显示 FPS
```

**优势：**
- 不需要驱动
- 不需要每个游戏配置
- 自动对所有游戏生效
- 开发周期短

**文件结构：**
```
FPSMonitor/
├── fps_monitor.exe    # 托盘程序，安装全局钩子
├── fps_hook32.dll     # 32位 Hook DLL
├── fps_hook64.dll     # 64位 Hook DLL
└── games.txt          # 可选：游戏进程白名单
```

---

## 下一步行动

1. **实现 SetWindowsHookEx 全局钩子方案**
   - 创建托盘程序
   - 创建 32/64 位 Hook DLL
   - 实现自动检测游戏进程
   - 实现 Present Hook 和 FPS 显示

2. **测试覆盖率**
   - 测试普通游戏
   - 测试全屏独占游戏
   - 测试反作弊游戏

3. **根据测试结果决定是否需要驱动方案**

---

## 是否开始实现 SetWindowsHookEx 方案？

这个方案可以在 2-3 天内完成，覆盖大部分游戏场景。
