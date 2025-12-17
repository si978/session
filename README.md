# FPS Overlay

一个专业的游戏 FPS 叠加显示工具，支持 DirectX 11 游戏。

## 功能

- 实时显示 FPS 和帧时间
- 根据 FPS 数值自动变色（绿/黄/红）
- F1 热键切换显示/隐藏
- overlay.ini 配置透明度/位置/热键（运行时热更新）
- 低性能开销（< 1% CPU）

## 快速开始

### 方式 A：直接下载预编译（推荐）

1. 打开 GitHub → Actions → 选择 `Windows Build` 工作流 → 下载 artifact：`fps-overlay-windows-x64`
2. 解压后，以管理员身份运行 `launcher.exe`
3. 首次运行会生成并打开 `games.txt`：每行填一个游戏进程名（例如 `Game.exe`），保存后重启 `launcher.exe`
4. 正常启动游戏，叠加层会自动注入并显示（默认 F1 切换显示/隐藏）

运行时配置：同目录 `overlay.ini`（`fps_overlay.dll` 运行时会自动热加载）。

### 方式 B：从源码编译

#### 1. 下载依赖

运行 PowerShell 脚本自动下载：

```powershell
.\scripts\download_deps.ps1
```

或手动下载：
- [MinHook](https://github.com/TsudaKageyu/minhook) → `third_party/minhook/`
- [ImGui](https://github.com/ocornut/imgui) → `third_party/imgui/`

#### 2. 编译

```bash
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
```

#### 3. 使用

推荐：以管理员身份运行 `launcher.exe`（托盘后台监控 `games.txt`，自动注入）。

或使用注入器：

```bash
injector.exe Game.exe
injector.exe 12345
```

## 项目结构

```
fps-fresh/
├── src/
│   ├── dllmain.cpp          # DLL 入口
│   ├── hooks.cpp/.h         # DirectX Hook 实现
│   ├── fps_counter.cpp/.h   # FPS 计算
│   ├── overlay.cpp/.h       # ImGui 叠加层渲染
│   ├── logger.h             # 日志模块
│   └── injector/
│       └── main.cpp         # DLL 注入器
│   └── launcher/
│       ├── main.cpp         # 托盘后台监控 + 自动注入
│       └── launcher.rc      # 图标/资源
├── third_party/
│   ├── minhook/             # MinHook 库
│   └── imgui/               # ImGui 库
├── scripts/
│   └── download_deps.ps1    # 依赖下载脚本
├── docs/
│   ├── requirements.md      # 需求文档
│   └── fps-basics.md        # 基础知识文档
├── CMakeLists.txt           # 构建配置
└── README.md
```

## 文档

- [怎么使用（本地文档）](docs/USAGE.md) - 从“直接运行”到“用 Codex CLI 持续迭代”的最短路径
- [需求文档](docs/requirements.md) - 完整的项目需求和技术方案
- [基础知识](docs/fps-basics.md) - FPS 原理、Hook 技术、ImGui 集成等

## 技术栈

| 组件 | 技术 |
|------|------|
| Hook 库 | MinHook |
| UI 渲染 | ImGui |
| 图形 API | DirectX 11 |
| 构建系统 | CMake |

## 系统要求

- Windows 10/11 64 位
- 仅运行：无需额外依赖（下载预编译产物即可）
- 从源码编译：Visual Studio 2019 或更高版本、CMake 3.20 或更高版本

## 许可证

MIT License
