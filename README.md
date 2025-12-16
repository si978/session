# FPS Overlay

一个专业的游戏 FPS 叠加显示工具，支持 DirectX 11 游戏。

## 功能

- 实时显示 FPS 和帧时间
- 根据 FPS 数值自动变色（绿/黄/红）
- F1 热键切换显示/隐藏
- overlay.ini 配置透明度/位置/热键（运行时热更新）
- 低性能开销（< 1% CPU）

## 快速开始

### 1. 下载依赖

运行 PowerShell 脚本自动下载：

```powershell
.\scripts\download_deps.ps1
```

或手动下载：
- [MinHook](https://github.com/TsudaKageyu/minhook) → `third_party/minhook/`
- [ImGui](https://github.com/ocornut/imgui) → `third_party/imgui/`

### 2. 编译

```bash
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
```

### 3. 使用

```bash
# 以管理员身份运行
injector.exe Game.exe
```

可选：在 `fps_overlay.dll` 同目录创建/编辑 `overlay.ini` 来调整显示效果。

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
- Visual Studio 2019 或更高版本
- CMake 3.20 或更高版本

## 许可证

MIT License
