# FPS Overlay 改进计划

## 版本规划

| 版本 | 目标 | 预计工作量 |
|------|------|-----------|
| v1.1 | 性能优化 + 多 API 支持 | 3-4 天 |
| v1.2 | 配置系统 + 用户界面 | 2-3 天 |
| v1.3 | 稳定性 + 32位支持 | 2 天 |

---

## v1.1 - 性能与兼容性

### 1.1.1 渲染性能优化

**当前问题：** 每帧复制整个 BackBuffer，开销大

**改进方案：**
```
当前：BackBuffer → Staging → CPU修改 → Staging → BackBuffer
改进：使用 GPU 直接渲染（RenderTargetView + SpriteBatch）
```

**完成标准：**
- [ ] 使用 ID3D11RenderTargetView 直接在 GPU 绘制
- [ ] 预创建字体纹理，避免每帧创建资源
- [ ] 性能测试：Hook 前后帧率差异 < 1%

### 1.1.2 DirectX 9 支持

**目标游戏：** 老游戏（如 CS 1.6、魔兽争霸3、老单机游戏）

**实现方案：**
```cpp
// 检测 D3D9
if (GetModuleHandle("d3d9.dll")) {
    // Hook IDirect3DDevice9::Present 或 EndScene
    void** vtable = GetD3D9DeviceVTable();
    HookFunction(vtable[17], HookedPresent9);  // Present
    HookFunction(vtable[42], HookedEndScene);  // EndScene
}
```

**完成标准：**
- [ ] 支持 IDirect3DDevice9::Present Hook
- [ ] 支持 IDirect3DDevice9::EndScene Hook（某些游戏只调用这个）
- [ ] 在至少 3 款 DX9 游戏中测试通过

### 1.1.3 DirectX 12 支持

**目标游戏：** 新游戏（如 Cyberpunk 2077、艾尔登法环）

**实现方案：**
```cpp
// DX12 仍使用 DXGI，Present 地址相同
// 但渲染方式需要调整
if (GetModuleHandle("d3d12.dll")) {
    // 使用 ID3D12CommandQueue::ExecuteCommandLists 时机绘制
    // 或继续用 DXGI Present，但用 D3D11on12 渲染
}
```

**完成标准：**
- [ ] Hook IDXGISwapChain::Present（DX12 共用）
- [ ] 使用 D3D11on12 或纯 DX12 渲染 FPS
- [ ] 在至少 2 款 DX12 游戏中测试通过

### 1.1.4 监控线程优化

**当前问题：** 每个进程启动 30 秒监控线程

**改进方案：**
```cpp
// 改为事件驱动
// 只在检测到 D3D DLL 加载时才启动监控
DWORD MonitorThread() {
    for (int i = 0; i < 10; i++) {  // 缩短到 10 秒
        Sleep(1000);
        if (IsGraphicsProcess()) {
            InstallHook();
            break;
        }
    }
    // 线程结束，释放资源
}
```

**完成标准：**
- [ ] 监控时间缩短到 10 秒
- [ ] 非游戏进程资源占用最小化
- [ ] 添加提前退出机制

---

## v1.2 - 配置系统与用户界面

### 1.2.1 配置文件

**配置文件：** `fps_config.ini`

```ini
[Display]
Position=TopRight      # TopLeft, TopRight, BottomLeft, BottomRight
OffsetX=10
OffsetY=10
FontSize=14            # 小=12, 中=14, 大=18
ShowBackground=true

[Colors]
HighFPS=00FF00         # >= 60 FPS
MediumFPS=FFCC00       # 30-59 FPS
LowFPS=FF0000          # < 30 FPS
Background=80000000    # ARGB

[Hotkey]
Toggle=F1              # 切换显示
# 支持: F1-F12, Ctrl+F1, Alt+F1 等

[Filter]
Mode=Whitelist         # Whitelist 或 Blacklist
Games=                  # 留空=所有游戏
# Games=League of Legends.exe,Valorant.exe

[General]
AutoStart=false        # 开机自启
```

**完成标准：**
- [ ] 支持 INI 格式配置文件
- [ ] 程序启动时读取配置
- [ ] 配置热重载（修改后自动生效）
- [ ] 配置文件不存在时自动创建默认配置

### 1.2.2 托盘菜单增强

**当前：** 只有 Exit 选项

**改进为：**
```
右键托盘图标：
├── FPS Overlay v1.2
├── ──────────────
├── ✓ 启用 FPS 显示
├── 位置 →
│   ├── 左上角
│   ├── ✓ 右上角
│   ├── 左下角
│   └── 右下角
├── 字体大小 →
│   ├── 小
│   ├── ✓ 中
│   └── 大
├── ──────────────
├── 打开配置文件
├── 重新加载配置
├── ──────────────
├── ✓ 开机自启
├── 关于
└── 退出
```

**完成标准：**
- [ ] 托盘菜单支持上述所有选项
- [ ] 菜单操作实时生效
- [ ] 菜单状态与配置文件同步

### 1.2.3 热键自定义

**支持格式：**
- 单键：`F1`, `F2`, ... `F12`
- 组合键：`Ctrl+F1`, `Alt+F1`, `Ctrl+Shift+F1`

**完成标准：**
- [ ] 配置文件中可设置热键
- [ ] 支持 Ctrl/Alt/Shift 修饰键
- [ ] 热键冲突检测和提示

### 1.2.4 游戏过滤

**白名单模式：** 只在指定游戏中显示
**黑名单模式：** 在指定游戏中不显示

```ini
[Filter]
Mode=Whitelist
Games=LeagueClient.exe,League of Legends.exe
```

**完成标准：**
- [ ] 支持按进程名过滤
- [ ] 支持白名单和黑名单两种模式
- [ ] 过滤规则实时生效（无需重启）

---

## v1.3 - 稳定性与完整性

### 1.3.1 32 位支持

**需要：** 编译 32 位版本的 fps_hook.dll

```
output/
├── fps_monitor.exe    # 64位主程序
├── fps_hook64.dll     # 64位 Hook
└── fps_hook32.dll     # 32位 Hook
```

**实现方案：**
```cpp
// fps_monitor.exe 同时安装两个钩子
g_hHook64 = SetWindowsHookExW(WH_CBT, CBTProc, hDll64, 0);
g_hHook32 = SetWindowsHookExW(WH_CBT, CBTProc, hDll32, 0);
```

**完成标准：**
- [ ] 编译 32 位 fps_hook32.dll
- [ ] 主程序同时加载 32/64 位 DLL
- [ ] 在至少 2 款 32 位游戏中测试通过

### 1.3.2 钩子卸载优化

**当前问题：** 关闭程序后钩子可能残留

**改进方案：**
```cpp
// 添加心跳机制
// DLL 定期检查主程序是否还在运行
DWORD HeartbeatThread() {
    while (true) {
        Sleep(5000);
        // 检查主程序进程是否存在
        if (!IsMonitorRunning()) {
            // 主程序已退出，自行卸载
            RemoveHook();
            FreeLibraryAndExitThread(g_hModule, 0);
        }
    }
}
```

**完成标准：**
- [ ] 主程序退出后，DLL 在 10 秒内自动卸载
- [ ] 无需重启电脑
- [ ] 不影响正在运行的游戏

### 1.3.3 冲突检测

**潜在冲突：** Discord Overlay、Steam Overlay、录屏软件

**方案：**
```cpp
// 检测其他 Hook
bool HasConflict() {
    // 检查是否有其他软件 Hook 了 Present
    void* currentPresent = GetCurrentPresentAddress();
    void* originalPresent = GetOriginalPresentAddress();
    
    if (currentPresent != originalPresent) {
        // 已被其他软件 Hook
        Log("Conflict detected: Present already hooked");
        return true;
    }
    return false;
}
```

**完成标准：**
- [ ] 检测 Present 是否已被 Hook
- [ ] 日志记录冲突信息
- [ ] 冲突时优雅降级（不 Hook，避免崩溃）

### 1.3.4 异常处理

**添加崩溃保护：**
```cpp
HRESULT WINAPI HookedPresent(...) {
    __try {
        UpdateFps();
        RenderOverlay();
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // 发生异常，禁用渲染
        g_renderEnabled = false;
        Log("Exception in render, disabled");
    }
    
    return g_originalPresent(...);
}
```

**完成标准：**
- [ ] 所有关键函数添加异常处理
- [ ] 异常时自动禁用功能而不是崩溃
- [ ] 异常信息记录到日志

---

## 测试矩阵

### 游戏兼容性测试

| 游戏 | API | 位数 | v1.1 | v1.2 | v1.3 |
|------|-----|------|------|------|------|
| League of Legends | DX11 | 64 | ✓ | ✓ | ✓ |
| Valorant | DX11 | 64 | ? | ? | ? |
| CS2 | DX11 | 64 | ? | ? | ? |
| Hollow Knight | DX11 | 64 | ? | ? | ? |
| 魔兽争霸3 | DX9 | 32 | - | - | ✓ |
| Cyberpunk 2077 | DX12 | 64 | ✓ | ✓ | ✓ |

### 性能测试

| 场景 | 目标 |
|------|------|
| Hook 前后帧率差异 | < 1% |
| 内存占用增加 | < 10MB |
| CPU 占用增加 | < 1% |

---

## 里程碑

| 日期 | 版本 | 交付物 |
|------|------|--------|
| +4天 | v1.1 | DX9/11/12 支持，性能优化 |
| +7天 | v1.2 | 配置系统，托盘菜单，热键自定义 |
| +9天 | v1.3 | 32位支持，稳定性改进 |

---

## 风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| DX12 渲染复杂 | v1.1 延期 | 先用 D3D11on12 降级方案 |
| 反作弊更新 | LOL 不可用 | 记录文档，建议用户使用官方 FPS |
| 杀软误报 | 用户流失 | 考虑购买代码签名证书 |

---

*计划版本：1.0*  
*创建日期：2024年12月*  
*状态：待执行*
