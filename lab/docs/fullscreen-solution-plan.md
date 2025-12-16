# 全屏游戏 FPS 覆盖显示解决方案

## 问题分析

### 现象
- 游戏窗口化/无边框模式：FPS 正常显示
- 游戏全屏独占模式：FPS 短暂出现后消失

### 根本原因

Windows 有三种全屏模式：

| 模式 | DWM 状态 | 覆盖窗口 | 说明 |
|------|----------|----------|------|
| 窗口化 | 正常运行 | ✅ 可显示 | 普通窗口 |
| 无边框全屏 | 正常运行 | ✅ 可显示 | 最大化无边框窗口 |
| **全屏独占** | **被绑定** | ❌ **无法显示** | 游戏直接控制显示输出 |

**全屏独占模式下：**
- 游戏绑定 DWM（Desktop Window Manager）
- 直接控制显卡输出
- 普通窗口无法渲染在游戏画面之上
- 这是硬件/驱动层面的限制

---

## 解决方案

### 方案 A：强制无边框全屏（推荐）

**原理：** Windows 10+ 提供"禁用全屏优化"选项，使游戏的全屏模式变为无边框全屏。

**实现：**
```
游戏启动
    ↓
检测是否全屏独占
    ↓ 是
自动修改游戏兼容性设置
或提示用户手动设置
    ↓
游戏变为无边框全屏
    ↓
覆盖窗口正常显示
```

**技术要点：**
1. 检测全屏独占：`IDXGIOutput::GetDesc()` 或 `DwmGetCompositionTimingInfo()`
2. 修改兼容性：写入注册表 `HKCU\SOFTWARE\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers`
3. 值：`~ DISABLEDXMAXIMIZEDWINDOWEDMODE`

**优点：**
- 不需要驱动级操作
- 一次设置永久生效
- 对游戏性能影响很小

**缺点：**
- 需要重启游戏生效
- 某些游戏可能有兼容性问题

---

### 方案 B：硬件覆盖层

**原理：** 使用显卡驱动提供的硬件覆盖功能。

**技术选项：**

| 技术 | 支持 | 复杂度 |
|------|------|--------|
| NVIDIA NVAPI Overlay | N卡 | 高 |
| AMD AGS Overlay | A卡 | 高 |
| Intel DX12 Overlay | Intel | 高 |
| MPO (Multiplane Overlay) | Win10+ | 很高 |

**优点：**
- 真正的硬件级覆盖
- 可在全屏独占模式显示

**缺点：**
- 需要针对不同显卡开发
- 需要显卡厂商 SDK
- 实现复杂度很高

---

### 方案 C：第二显示器/小窗口

**原理：** 不在游戏上覆盖，而是在独立位置显示。

**实现：**
```
┌─────────────────────┐  ┌──────────┐
│                     │  │ FPS: 60  │ ← 小窗口
│    游戏（全屏）      │  │          │   始终置顶
│                     │  └──────────┘
└─────────────────────┘
         主显示器              副显示器/角落
```

**优点：**
- 完全不受全屏独占影响
- 实现简单

**缺点：**
- 需要第二显示器或分散注意力
- 用户体验较差

---

### 方案 D：利用系统级覆盖

**原理：** 使用 Windows 已有的系统级覆盖机制。

**选项：**
1. **Xbox Game Bar API** - 但需要成为认证开发者
2. **Windows.Gaming.UI** - UWP API，限制较多
3. **TaskView/Alt+Tab 层级** - 使用未公开 API

**优点：**
- 系统级支持
- 可能被反作弊白名单

**缺点：**
- API 限制多
- 可能需要特殊权限

---

## 推荐实施计划

### 第一阶段：检测与提示

1. **检测全屏模式**
   ```cpp
   bool IsFullscreenExclusive(HWND gameWindow) {
       // 方法1: 检查窗口样式
       RECT windowRect, screenRect;
       GetWindowRect(gameWindow, &windowRect);
       GetWindowRect(GetDesktopWindow(), &screenRect);
       
       LONG style = GetWindowLong(gameWindow, GWL_STYLE);
       bool noBorder = !(style & WS_CAPTION);
       bool fullSize = EqualRect(&windowRect, &screenRect);
       
       // 方法2: 检查 DWM 状态
       BOOL isCompositing = FALSE;
       DwmIsCompositionEnabled(&isCompositing);
       
       return noBorder && fullSize && !isCompositing;
   }
   ```

2. **提示用户**
   - 检测到全屏独占时，弹出通知
   - 建议用户：
     - 切换到无边框全屏模式
     - 或右键游戏 exe → 属性 → 兼容性 → 禁用全屏优化

### 第二阶段：自动优化

1. **自动修改兼容性设置**
   ```cpp
   void DisableFullscreenOptimization(const wchar_t* exePath) {
       HKEY hKey;
       RegCreateKeyExW(HKEY_CURRENT_USER,
           L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
           0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
       
       RegSetValueExW(hKey, exePath, 0, REG_SZ,
           (BYTE*)L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE",
           sizeof(L"~ DISABLEDXMAXIMIZEDWINDOWEDMODE"));
       
       RegCloseKey(hKey);
   }
   ```

2. **提示重启游戏**

### 第三阶段：备选显示

1. **独立小窗口模式**
   - 当检测到全屏独占且用户不想修改设置时
   - 在屏幕角落显示独立的 FPS 小窗口
   - 使用 `WS_EX_TOPMOST` 但不覆盖全屏

---

## 实施优先级

| 优先级 | 任务 | 工作量 |
|--------|------|--------|
| P0 | 检测全屏独占模式 | 2小时 |
| P0 | 添加用户提示 | 1小时 |
| P1 | 自动禁用全屏优化 | 3小时 |
| P2 | 独立小窗口模式 | 4小时 |
| P3 | 硬件覆盖层研究 | 需评估 |

---

## 下一步行动

1. **立即可做**：添加全屏独占检测和用户提示
2. **短期目标**：实现自动禁用全屏优化
3. **中期研究**：评估硬件覆盖层可行性

---

## 用户操作指南（临时方案）

在我们实现自动化之前，用户可以手动操作：

### 方法1：游戏内设置
- 进入游戏设置 → 图形/视频
- 将"全屏"改为"无边框"或"窗口化全屏"

### 方法2：Windows 兼容性设置
1. 右键游戏 exe 文件
2. 属性 → 兼容性
3. 勾选"禁用全屏优化"
4. 应用并重启游戏

### 方法3：LOL 特定
- 游戏设置 → 视频 → 窗口模式 → 选择"无边框"
