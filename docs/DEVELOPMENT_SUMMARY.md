# FPS Overlay 开发总结

> 版本：1.0  
> 更新日期：2024年12月  
> 支持平台：Windows 10/11 64位  
> 图形API：DirectX 11

---

## 一、项目概述

### 1.1 项目目标

开发一款轻量级的游戏 FPS（帧率）实时显示工具，能够在任意 DirectX 11 游戏中叠加显示当前帧率，帮助玩家监控游戏性能。

### 1.2 核心特性

- **非侵入式**：通过 DLL 注入实现，无需修改游戏文件
- **实时显示**：Hook DirectX Present 函数，精确计算每帧时间
- **自动化**：后台监控进程，自动检测游戏启动并注入
- **轻量级**：极低的性能开销，不影响游戏体验

---

## 二、已实现功能

### 2.1 核心功能

| 功能 | 描述 | 实现方式 |
|------|------|----------|
| FPS 实时显示 | 右上角显示当前帧率 | 滑动窗口算法计算平均帧率 |
| 颜色指示 | 绿色(≥60)/黄色(30-60)/红色(<30) | ImGui 动态颜色渲染 |
| 热键切换 | F1 键显示/隐藏 | GetAsyncKeyState 检测 |
| DirectX 11 Hook | 拦截 Present/ResizeBuffers | MinHook + VTable Hook |

### 2.2 启动器功能

| 功能 | 描述 | 实现方式 |
|------|------|----------|
| 系统托盘运行 | 最小化到托盘，后台持续运行 | Shell_NotifyIconW API |
| 自动进程监控 | 检测配置的游戏进程启动 | CreateToolhelp32Snapshot 遍历 |
| 自动注入 | 游戏启动后自动注入 DLL | CreateRemoteThread + LoadLibraryW |
| 配置热重载 | 修改 games.txt 自动生效 | 定时检测文件修改时间 |
| 托盘通知 | 注入成功时弹出通知 | Shell_NotifyIconW NIF_INFO |
| 右键菜单 | 编辑配置/重载/退出 | TrackPopupMenu |
| 单实例检测 | 防止重复运行 | CreateMutexW |
| 自定义图标 | 绿色 FPS 图标 | 资源文件嵌入 ICO |

### 2.3 显示效果

```
┌─────────────────────────────────────────────────────┐
│                                        ┌──────────┐ │
│                                        │ FPS: 60  │ │
│                                        └──────────┘ │
│                                                     │
│                   （游戏画面）                       │
│                                                     │
└─────────────────────────────────────────────────────┘

显示规格：
- 位置：右上角 (8, 8) 像素偏移
- 背景：25% 透明度深灰色
- 圆角：4px
- 字体：默认 ImGui 字体
- 格式："FPS: XX"
- 刷新：每 300ms 更新显示值
```

---

## 三、技术架构

### 3.1 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        用户层                                │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐    注入     ┌─────────────────────────┐   │
│  │ launcher.exe│ ─────────→ │     目标游戏进程         │   │
│  │  (托盘监控) │            │  ┌───────────────────┐  │   │
│  └─────────────┘            │  │  fps_overlay.dll  │  │   │
│        ↑                    │  │  ┌─────────────┐  │  │   │
│        │ 读取               │  │  │   MinHook   │  │  │   │
│  ┌─────────────┐            │  │  └──────┬──────┘  │  │   │
│  │  games.txt  │            │  │         ↓         │  │   │
│  └─────────────┘            │  │  ┌─────────────┐  │  │   │
│                             │  │  │ DX11 Present│  │  │   │
│                             │  │  │    Hook     │  │  │   │
│                             │  │  └──────┬──────┘  │  │   │
│                             │  │         ↓         │  │   │
│                             │  │  ┌─────────────┐  │  │   │
│                             │  │  │    ImGui    │  │  │   │
│                             │  │  │   渲染FPS   │  │  │   │
│                             │  │  └─────────────┘  │  │   │
│                             │  └───────────────────┘  │   │
│                             └─────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                      DirectX 11 API                         │
├─────────────────────────────────────────────────────────────┤
│                        GPU 驱动                             │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| DLL 入口 | dllmain.cpp | DLL 加载/卸载，初始化 Hook |
| Hook 模块 | hooks.cpp/h | DirectX 11 Present/ResizeBuffers Hook |
| FPS 计算 | fps_counter.cpp/h | 滑动窗口算法计算帧率 |
| 叠加层渲染 | overlay.cpp/h | ImGui 初始化和 FPS 显示渲染 |
| 日志模块 | logger.h | 调试日志输出 |
| 启动器 | launcher/main.cpp | 托盘监控和自动注入 |

### 3.3 关键算法

#### FPS 计算 - 滑动窗口算法

```cpp
// 保留最近 100 帧的时间戳
std::deque<double> frameTimes;  // 最大 100 个样本

void Update() {
    double currentTime = GetCurrentTimeMs();
    frameTimes.push_back(currentTime);
    
    // 移除超过 1 秒的旧样本
    while (!frameTimes.empty() && 
           currentTime - frameTimes.front() > 1000.0) {
        frameTimes.pop_front();
    }
    
    // FPS = 样本数量（即过去 1 秒内的帧数）
    fps = frameTimes.size();
}
```

#### 显示更新 - 防抖动

```cpp
// 每 300ms 更新一次显示值，避免数字跳动
if (currentTime - lastDisplayUpdate > 300.0) {
    displayFps = currentFps;
    lastDisplayUpdate = currentTime;
}
```

### 3.4 Hook 流程

```
游戏调用 IDXGISwapChain::Present()
              ↓
        MinHook 拦截
              ↓
      HookedPresent() 执行
              ↓
    ┌─────────────────────┐
    │ 1. 更新帧计数       │
    │ 2. 计算 FPS         │
    │ 3. 渲染 ImGui 叠加层│
    └─────────────────────┘
              ↓
      调用原始 Present()
              ↓
        画面呈现到屏幕
```

---

## 四、文件结构

```
fps-fresh/
├── build/                          # 构建输出
│   └── bin/Release/
│       ├── launcher.exe            # 启动器 (66KB)
│       ├── fps_overlay.dll         # FPS 显示模块 (372KB)
│       └── test_app.exe            # 测试程序
│
├── docs/                           # 文档
│   ├── requirements.md             # 需求文档 (含演进记录)
│   ├── fps-basics.md               # 技术原理文档
│   ├── USER_GUIDE.md               # 用户使用指南
│   └── DEVELOPMENT_SUMMARY.md      # 开发总结 (本文档)
│
├── src/                            # 源代码
│   ├── dllmain.cpp                 # DLL 入口
│   ├── hooks.cpp/h                 # DirectX Hook
│   ├── fps_counter.cpp/h           # FPS 计算
│   ├── overlay.cpp/h               # ImGui 渲染
│   ├── logger.h                    # 日志工具
│   ├── launcher/                   # 启动器
│   │   ├── main.cpp
│   │   ├── resource.h
│   │   ├── launcher.rc
│   │   └── app.ico
│   └── test_app/                   # 测试程序
│       └── main.cpp
│
├── third_party/                    # 第三方库
│   ├── minhook/                    # Hook 库
│   │   ├── include/MinHook.h
│   │   └── src/...
│   └── imgui/                      # UI 库
│       ├── imgui.h
│       ├── imgui.cpp
│       └── backends/
│           ├── imgui_impl_dx11.cpp/h
│           └── imgui_impl_win32.cpp/h
│
├── scripts/                        # 脚本
│   ├── build.bat                   # 构建脚本
│   ├── download_deps.ps1           # 依赖下载
│   └── create_icon.ps1             # 图标生成
│
├── CMakeLists.txt                  # 构建配置
├── README.md                       # 项目说明
└── .gitignore                      # Git 忽略配置
```

---

## 五、使用方法

### 5.1 快速开始

```
1. 运行 launcher.exe
2. 首次运行自动创建 games.txt，添加游戏进程名
3. 重新运行 launcher.exe，最小化到托盘
4. 启动游戏，自动注入显示 FPS
5. 游戏内按 F1 切换显示/隐藏
```

### 5.2 配置文件

```
# games.txt 示例
# 每行一个游戏进程名，# 开头为注释

Brawlhalla.exe
HollowKnight.exe
Cuphead.exe
Terraria.exe
```

### 5.3 托盘菜单

| 菜单项 | 功能 |
|--------|------|
| Edit games.txt | 打开记事本编辑配置 |
| Reload config | 立即重新加载配置 |
| Exit | 退出程序 |

---

## 六、开发历程

### 6.1 启动器演进

| 版本 | 模式 | 问题 | 解决方案 |
|------|------|------|----------|
| V1 | 命令行注入 | 权限错误 Error 5 | - |
| V2 | 拖拽启动 | Steam 游戏无法直接启动 | - |
| V3 | 同目录启动 | 需要复制文件到每个游戏目录 | - |
| V4 | 控制台监控 | 关闭窗口程序终止 | - |
| V5 | **托盘后台** | ✅ 完美运行 | 当前版本 |

### 6.2 关键问题解决

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| 注入失败 Error 5 | 权限不足 | 使用 UAC 管理员权限 |
| Steam 游戏无法启动 | 需要 Steam 验证 | 改为等待进程启动后注入 |
| 控制台关闭程序终止 | 控制台子系统特性 | 改用 WIN32 子系统 + 托盘 |
| 托盘图标不显示 | 使用了大图标 | LoadImageW 指定 16x16 尺寸 |
| 配置修改不生效 | 只在启动时读取 | 定时检测文件修改时间 |
| 中文编码错误 | 源文件编码问题 | 移除中文注释 |

---

## 七、可优化功能

### 7.1 高优先级

| 功能 | 描述 | 预计工作量 | 实现思路 |
|------|------|------------|----------|
| **开机自启** | 托盘菜单添加开机启动开关 | 2小时 | 写入注册表 `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run` |
| **显示位置配置** | 支持四角选择 | 3小时 | 配置文件增加 position 字段，overlay.cpp 读取并应用 |
| **帧时间显示** | 显示 ms 值（如 16.7ms） | 1小时 | fps_counter 已计算，overlay 增加显示 |
| **日志记录** | 记录注入成功/失败 | 2小时 | 写入 log.txt 文件 |

### 7.2 中优先级

| 功能 | 描述 | 预计工作量 | 实现思路 |
|------|------|------------|----------|
| **热键配置** | 自定义显示/隐藏热键 | 3小时 | 配置文件增加 hotkey 字段 |
| **透明度配置** | 自定义背景透明度 | 2小时 | 配置文件增加 opacity 字段 |
| **字体大小配置** | 自定义显示字体大小 | 2小时 | ImGui 字体缩放 |
| **托盘悬停信息** | 显示已注入的游戏列表 | 2小时 | 更新 szTip 内容 |
| **GUI 配置界面** | 替代手动编辑 txt | 8小时 | 简单 Win32 对话框或单独配置程序 |

### 7.3 低优先级（扩展功能）

| 功能 | 描述 | 预计工作量 | 实现思路 |
|------|------|------------|----------|
| **DirectX 12 支持** | 覆盖更多新游戏 | 16小时 | 类似 DX11，Hook ID3D12CommandQueue::ExecuteCommandLists |
| **DirectX 9 支持** | 覆盖老游戏 | 8小时 | Hook IDirect3DDevice9::EndScene |
| **Vulkan 支持** | 覆盖 Vulkan 游戏 | 16小时 | Hook vkQueuePresentKHR |
| **GPU 使用率显示** | 更全面的性能监控 | 8小时 | NVAPI / ADL SDK |
| **CPU 使用率显示** | 更全面的性能监控 | 4小时 | PDH 或 GetSystemTimes |
| **内存使用显示** | 更全面的性能监控 | 2小时 | GetProcessMemoryInfo |
| **游戏内配置菜单** | 游戏内按键打开设置 | 12小时 | ImGui 多窗口 |
| **性能统计图表** | 显示 FPS 变化曲线 | 8小时 | ImGui Plot |
| **截图/录制** | 性能数据导出 | 16小时 | 帧数据记录 + CSV 导出 |

### 7.4 架构优化

| 优化项 | 描述 | 预计工作量 |
|--------|------|------------|
| **配置文件格式** | 改用 JSON/INI 格式支持更多配置 | 4小时 |
| **错误处理增强** | 更详细的错误提示和日志 | 4小时 |
| **代码重构** | 模块化、接口抽象 | 8小时 |
| **单元测试** | FPS 计算等核心逻辑测试 | 4小时 |
| **安装包** | NSIS/Inno Setup 安装程序 | 4小时 |

---

## 八、兼容性

### 8.1 已测试游戏

| 游戏 | 图形API | 窗口模式 | 状态 |
|------|---------|----------|------|
| Brawlhalla | DX11 | 窗口化 | ✅ 正常 |

### 8.2 已知限制

| 限制 | 说明 |
|------|------|
| 仅支持 DX11 | DX9/DX12/Vulkan/OpenGL 暂不支持 |
| 需要管理员权限 | 注入需要提升权限 |
| 反作弊游戏不可用 | EAC/BattlEye 等会阻止注入 |
| 64位游戏 | 当前仅支持 64位游戏 |

### 8.3 系统要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10/11 64位 |
| 运行时 | Visual C++ Runtime 2019+ |
| 权限 | 管理员权限 |

---

## 九、第三方依赖

| 库 | 版本 | 用途 | 许可证 |
|----|------|------|--------|
| MinHook | 1.3.3 | 函数 Hook | BSD 2-Clause |
| Dear ImGui | 1.91.5 | UI 渲染 | MIT |

---

## 十、构建说明

### 10.1 环境要求

- Visual Studio 2022
- CMake 3.15+
- Windows SDK 10.0+

### 10.2 构建步骤

```batch
# 配置
cmake -B build -A x64

# 构建
cmake --build build --config Release

# 输出位置
build/bin/Release/launcher.exe
build/bin/Release/fps_overlay.dll
```

---

## 十一、总结

### 11.1 项目成果

- ✅ 完整的 FPS 显示功能
- ✅ 自动化后台注入系统
- ✅ 用户友好的托盘界面
- ✅ 热重载配置支持
- ✅ 自定义应用图标
- ✅ 完善的文档体系

### 11.2 技术亮点

1. **滑动窗口 FPS 算法** - 平滑稳定的帧率计算
2. **VTable Hook** - 可靠的 DirectX 拦截方案
3. **托盘后台模式** - 真正的无感自动化
4. **配置热重载** - 无需重启即时生效

### 11.3 后续规划

1. **短期**：开机自启、显示位置配置
2. **中期**：GUI 配置工具、更多显示指标
3. **长期**：多图形 API 支持、性能分析工具

---

*文档结束*
