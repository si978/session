# FPS Overlay v1.4 使用教程

## 功能特点

- 实时显示游戏帧率 (FPS)
- 显示 VSync 状态 (`[V]` 表示开启)
- 支持鼠标拖拽移动位置
- 支持 DX9/DX11/DX12 游戏
- 支持 32/64 位游戏
- 低性能开销 (< 0.5%)

## 文件说明

```
v1.4/
├── fps_monitor.exe    # 主程序（托盘运行）
├── fps_hook64.dll     # 64位游戏注入模块
├── fps_hook32.dll     # 32位游戏注入模块
└── fps_config.ini     # 配置文件（首次运行自动生成）
```

## 快速开始

### 1. 启动程序

双击运行 `fps_monitor.exe`，程序会最小化到系统托盘。

### 2. 启动游戏

正常启动游戏，FPS 显示会自动出现在游戏画面中。

### 3. 显示格式

```
120 FPS [V]   ← VSync 开启，帧率被锁定
120 FPS       ← VSync 关闭，无帧率限制
```

## 操作说明

### 显示/隐藏 FPS

按 `F1` 键切换显示/隐藏

### 移动 FPS 位置

使用热键 `Ctrl + Alt + 方向键` 微调位置：

| 热键 | 操作 |
|------|------|
| `Ctrl+Alt+↑` | 向上移动 10 像素 |
| `Ctrl+Alt+↓` | 向下移动 10 像素 |
| `Ctrl+Alt+←` | 向左移动 10 像素 |
| `Ctrl+Alt+→` | 向右移动 10 像素 |

位置会自动保存，下次启动游戏时保持。

### 托盘菜单

右键点击托盘图标：

| 选项 | 说明 |
|------|------|
| Toggle | 显示/隐藏 FPS |
| Position | 选择预设位置（左上/右上/左下/右下）|
| Size | 调整字体大小 |
| Edit Config | 打开配置文件 |
| Reload | 重新加载配置 |
| Exit | 退出程序 |

## 配置文件

配置文件 `fps_config.ini` 位于程序目录，可手动编辑：

```ini
[Display]
Position=TopRight          ; TopLeft, TopRight, BottomLeft, BottomRight, Custom
OffsetX=10                 ; 距离边缘的像素
OffsetY=10
FontSize=14                ; 字体大小: 12, 14, 18
CustomX=100                ; 自定义位置 X 坐标（拖拽后自动保存）
CustomY=10                 ; 自定义位置 Y 坐标

[Colors]
ColorHigh=FF00E070         ; >= 60 FPS 颜色（绿色）
ColorMedium=FFFFCC00       ; 30-59 FPS 颜色（黄色）
ColorLow=FFFF4040          ; < 30 FPS 颜色（红色）

[Hotkey]
ToggleKey=F1               ; 切换显示的热键
UseCtrl=false              ; 是否需要 Ctrl
UseAlt=false               ; 是否需要 Alt
UseShift=false             ; 是否需要 Shift
```

## 常见问题

### Q: 游戏中不显示 FPS？

**A:** 请确保：
1. `fps_monitor.exe` 在游戏启动**之前**运行
2. 如果游戏已经在运行，需要重启游戏
3. 按 `F1` 确认没有隐藏显示

### Q: 显示位置被游戏 UI 遮挡？

**A:** 按住 `Ctrl+Shift` 拖动 FPS 到其他位置。

### Q: 如何退出？

**A:** 右键托盘图标 → Exit，或直接在任务管理器结束进程。

### Q: 会被反作弊检测吗？

**A:** 本工具使用 Windows 标准 API，不修改游戏内存，但不保证所有游戏兼容。建议在非竞技模式下使用。

### Q: VSync [V] 是什么意思？

**A:** 
- `[V]` 表示游戏开启了垂直同步 (VSync)
- VSync 开启时，帧率会被限制在显示器刷新率（如 60Hz/144Hz）
- VSync 关闭时，帧率不受限制，但可能出现画面撕裂

## 系统要求

- Windows 10/11
- DirectX 9/11/12 游戏
- 约 10MB 内存占用

## 版本历史

### v1.4
- 新增：鼠标拖拽移动位置
- 新增：VSync 状态显示 `[V]`
- 优化：简化显示格式
- 优化：增强底色和边框

### v1.3
- 支持 32/64 位游戏
- 配置系统和托盘菜单
- 稳定性改进

---

**提示**: 遇到问题时，可以删除 `fps_config.ini` 恢复默认设置。
