# FPS Overlay 用户指南

## 简介

FPS Overlay 是一款轻量级的游戏帧率显示工具，支持在任意 DirectX 11 游戏中实时显示 FPS。

---

## 功能特性

| 功能 | 说明 |
|------|------|
| 实时 FPS 显示 | 右上角显示当前帧率 |
| 颜色指示 | 绿色(≥60) / 黄色(30-60) / 红色(<30) |
| 热键切换 | F1 键显示/隐藏 |
| 后台监控 | 自动检测游戏启动并注入 |
| 托盘运行 | 最小化到系统托盘，不影响使用 |

---

## 文件说明

```
FPSOverlay/
├── launcher.exe       # 启动器（托盘后台监控）
├── fps_overlay.dll    # FPS 显示模块
└── games.txt          # 游戏配置文件（自动生成）
```

---

## 快速开始

### 第一步：配置游戏列表

1. 运行 `launcher.exe`
2. 首次运行会自动创建 `games.txt` 并打开记事本
3. 添加你的游戏进程名（每行一个）：

```
# 示例配置
Brawlhalla.exe
HollowKnight.exe
Cuphead.exe
```

4. 保存并关闭记事本

### 第二步：启动监控

1. 再次运行 `launcher.exe`
2. 程序最小化到系统托盘（右下角）
3. 看到通知 "FPS Overlay Started - Monitoring X game(s)"

### 第三步：启动游戏

1. 正常启动你的游戏（通过 Steam 或其他方式）
2. 程序自动检测并注入
3. 托盘弹出通知 "xxx.exe - Injected!"
4. 游戏右上角显示 FPS

---

## 使用说明

### 托盘图标操作

| 操作 | 说明 |
|------|------|
| 左键单击 | 显示菜单 |
| 右键单击 | 显示菜单 |

### 托盘菜单

| 选项 | 说明 |
|------|------|
| Edit games.txt | 编辑游戏配置文件 |
| Edit overlay.ini | 编辑叠加层配置 |
| Reload config | 重新加载 games.txt |
| Exit | 退出程序 |

### 游戏内操作

| 按键 | 说明 |
|------|------|
| F1 | 显示/隐藏 FPS 显示 |

---

## 配置文件说明

### games.txt 格式

```
# 这是注释行（以 # 开头）
# 每行填写一个游戏的进程名

Brawlhalla.exe
HollowKnight.exe
Cuphead.exe
Terraria.exe
```

### overlay.ini（叠加层设置）

`overlay.ini` 位于 `fps_overlay.dll` 同目录，修改后会在游戏内自动热更新（≤ 1s）。

示例：

```ini
[Overlay]
Alpha=0.25
ShowFps=1
ShowFrameTime=1
GreenThreshold=60
YellowThreshold=30
FontScale=1.0
SampleCount=60
DisplayUpdateMs=80
Corner=TopRight
MarginX=8
MarginY=8
ToggleKey=F1
Visible=1

; 自定义坐标（左上为原点）
; Corner=Custom
; X=10
; Y=10
```

参数说明：
- `Alpha`：0..1（窗口背景透明度）
- `ShowFps`：0/1（是否显示 FPS）
- `ShowFrameTime`：0/1（是否显示帧时间）
- `GreenThreshold`：绿色阈值（≥ 此值显示为绿色）
- `YellowThreshold`：黄色阈值（≥ 此值显示为黄色，否则红色）
- `FontScale`：字体缩放（默认 1.0）
- `SampleCount`：FPS 平滑采样长度（帧数）
- `DisplayUpdateMs`：显示值刷新周期（毫秒）
- `Corner`：`TopLeft` / `TopRight` / `BottomLeft` / `BottomRight` / `Custom`
- `MarginX` / `MarginY`：四角模式的边距
- `X` / `Y`：自定义坐标（仅 `Corner=Custom` 生效）
- `ToggleKey`：`F1`~`F12`（或直接填 VK 数字）
- `Visible`：0/1

### 如何查找游戏进程名

1. 启动游戏
2. 打开任务管理器（Ctrl + Shift + Esc）
3. 在"进程"或"详细信息"标签页找到游戏
4. 记录进程名称（如 `Game.exe`）

---

## 显示效果

```
┌─────────────────────────────────────────┐
│                              ┌────────┐ │
│                              │ FPS:60 │ │  ← 右上角
│                              └────────┘ │     绿色 = 流畅
│                                         │     黄色 = 一般
│           （游戏画面）                   │     红色 = 卡顿
│                                         │
│                                         │
└─────────────────────────────────────────┘
```

---

## 常见问题

### Q: 游戏启动后没有显示 FPS？

**可能原因：**
1. 游戏进程名配置错误 → 检查 games.txt
2. 游戏不是 DirectX 11 → 本工具仅支持 DX11
3. 游戏有反作弊保护 → 无法注入

**解决方法：**
1. 打开任务管理器确认进程名
2. 确保进程名完全一致（包括大小写）

### Q: 托盘图标消失了？

程序可能已退出。重新运行 `launcher.exe` 即可。

### Q: 如何开机自启动？

1. 按 Win + R，输入 `shell:startup`
2. 将 `launcher.exe` 的快捷方式放入此文件夹

### Q: 支持哪些游戏？

支持所有使用 DirectX 11 渲染的游戏，包括但不限于：
- Steam 游戏
- Epic 游戏
- 独立游戏
- 大部分 2015 年后的 3A 游戏

**不支持：**
- DirectX 9 游戏
- DirectX 12 游戏
- Vulkan / OpenGL 游戏
- 有反作弊的游戏（如 LOL、Valorant、原神等）

---

## 系统要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10 / 11 64位 |
| 权限 | 需要管理员权限运行 |
| 图形 API | DirectX 11 |

---

## 注意事项

1. **请勿在有反作弊的游戏中使用**，可能导致封号
2. 首次运行需要以管理员身份运行
3. 杀毒软件可能误报，请添加信任

---

## 版本信息

- 版本：1.0
- 更新日期：2024年
- 支持：DirectX 11
