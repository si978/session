# GPU FPS vs 显示器 FPS：深度分析

## 核心问题

**用户实际看到的是显示器的帧，而非 GPU 渲染的帧。**

我们的 FPS Overlay 工具通过 Hook `Present` 函数来计算帧率，但这测量的是 **GPU 提交帧的速率**，而非 **显示器实际显示帧的速率**。这两者之间存在根本性差异。

---

## 一、基础概念

### 1.1 Frame Rate（帧率）vs Refresh Rate（刷新率）

| 概念 | 定义 | 单位 | 控制方 |
|------|------|------|--------|
| **Frame Rate** | GPU 每秒渲染的帧数 | FPS | GPU + 游戏引擎 |
| **Refresh Rate** | 显示器每秒刷新的次数 | Hz | 显示器硬件 |

**关键理解**：
- GPU 可以渲染 300 FPS，但 60Hz 显示器每秒只能显示 60 帧
- GPU 可能只渲染 30 FPS，但 144Hz 显示器仍会刷新 144 次（重复帧）

### 1.2 Present 调用的真正含义

```
游戏循环:
┌─────────────────────────────────────────────────────────┐
│  1. 处理输入                                             │
│  2. 更新游戏逻辑                                         │
│  3. 渲染场景到 Back Buffer                               │
│  4. 调用 Present() ← 我们 Hook 的位置                    │
│     └─ "GPU 完成了一帧，请求显示"                         │
│  5. 等待或继续下一帧（取决于 VSync）                       │
└─────────────────────────────────────────────────────────┘
```

**Present() 不等于显示**：
- Present() 是 GPU 向显示系统提交帧的请求
- 实际显示取决于 VSync、帧队列、显示器刷新周期

---

## 二、帧缓冲与交换链

### 2.1 双缓冲机制

```
┌─────────────────────────────────────────────────────────┐
│                      GPU                                 │
│  ┌─────────────┐    Present()    ┌─────────────┐        │
│  │ Back Buffer │ ──────────────→ │ Front Buffer│        │
│  │  (渲染中)    │      交换       │  (显示中)   │        │
│  └─────────────┘                 └──────┬──────┘        │
└─────────────────────────────────────────┼───────────────┘
                                          │
                                          ▼
                                   ┌──────────────┐
                                   │    显示器     │
                                   │   60/144 Hz  │
                                   └──────────────┘
```

### 2.2 VSync 的影响

| 模式 | 行为 | 优点 | 缺点 |
|------|------|------|------|
| **VSync Off** | Present 后立即交换 | 低延迟 | 撕裂（Tearing） |
| **VSync On** | 等待显示器刷新后交换 | 无撕裂 | 延迟增加 1-2 帧 |
| **Triple Buffer** | 使用 3 个缓冲区 | 减少卡顿 | 延迟更高 |

### 2.3 Flip Model vs Blit Model

**Blit Model（传统）**：
```
Back Buffer → 复制 → DWM Surface → 复制 → 显示器
              └──────── 额外开销 ────────┘
```

**Flip Model（现代，DX12 强制）**：
```
Back Buffer → 直接共享 → DWM → 显示器
              └─── 零拷贝，低延迟 ───┘
```

---

## 三、帧队列（Frame Queue）

### 3.1 预渲染帧

GPU 可以提前渲染多帧排队等待显示：

```
时间线:
─────────────────────────────────────────────────────────→
帧 N     帧 N+1   帧 N+2   帧 N+3
[渲染中]  [队列中]  [队列中]  [显示中]
          ↑
       预渲染帧（Pre-rendered Frames）
```

### 3.2 SetMaximumFrameLatency

```cpp
// 控制帧队列深度
IDXGIDevice1* pDevice;
pDevice->SetMaximumFrameLatency(1);  // 最低延迟
pDevice->SetMaximumFrameLatency(3);  // 默认值
```

| 设置 | 效果 |
|------|------|
| MaxLatency = 1 | 最低延迟，但可能卡顿 |
| MaxLatency = 2 | 平衡 |
| MaxLatency = 3 | 默认，高吞吐量 |
| MaxLatency = 4+ | 高吞吐量，高延迟 |

### 3.3 帧队列的影响

```
场景：GPU 渲染 100 FPS，显示器 60 Hz，帧队列深度 3

GPU 侧:     帧1 → 帧2 → 帧3 → 帧4 → 帧5 → ...
            │     │     │
            └─────┴─────┴──→ 帧队列（3帧）
                              │
显示器侧:   ─────────────────→ 帧1 显示 ─→ 帧2 显示 ─→ ...

延迟 = 帧队列深度 × 帧时间 = 3 × 10ms = 30ms
```

---

## 四、两种 FPS 指标

### 4.1 PresentMon 的关键发现

PresentMon 工具揭示了两个不同的帧率指标：

| 指标 | 定义 | 测量对象 |
|------|------|----------|
| **MsBetweenPresents** | Present 调用之间的时间 | GPU 提交帧的速率 |
| **MsBetweenDisplayChange** | 显示器实际更新之间的时间 | 用户看到的帧率 |

### 4.2 差异示例

```
场景：游戏报告 120 FPS，60Hz 显示器

MsBetweenPresents:      8.3ms（1000/120 = 120 FPS）
MsBetweenDisplayChange: 16.7ms（1000/60 = 60 FPS）

用户实际看到: 60 FPS（受显示器限制）
GPU 工作量:   120 FPS（浪费了一半）
```

### 4.3 FrameView 1.6 的重大更新

NVIDIA 在 FrameView 1.6 中改变了 FPS 计算方式：

```
旧方法: FPS = 1000 / MsBetweenPresents
        └─ 测量 GPU 渲染速率

新方法: FPS = 1000 / MsBetweenDisplayChange
        └─ 测量用户实际看到的帧率
```

**原因**：新方法更准确反映用户体验。

---

## 五、我们的工具测量的是什么？

### 5.1 当前实现

```cpp
HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, ...) {
    RecordFrameTime();  // 记录 Present 调用时间
    CalculateFPS();     // 统计每秒 Present 次数
    return g_originalPresent(...);
}
```

**我们测量的是**：`MsBetweenPresents`（GPU 提交帧的速率）

### 5.2 与显示帧率的差异

| 场景 | 我们显示 | 用户看到 | 差异原因 |
|------|----------|----------|----------|
| VSync Off, GPU 快 | 200 FPS | 60 FPS（撕裂） | 显示器刷新率限制 |
| VSync On | 60 FPS | 60 FPS | 同步到刷新率 |
| GPU 慢 | 45 FPS | 45 FPS（卡顿） | GPU 瓶颈 |
| 帧生成技术 | 120 FPS | 60 FPS | 插帧可能不显示 |

### 5.3 我们的数据仍然有价值

虽然不是"显示帧率"，但 Present 帧率仍然有意义：
1. **反映 GPU 工作负载** - CPU/GPU 瓶颈
2. **游戏性能指标** - 游戏引擎的实际输出
3. **行业标准** - FRAPS、Steam Overlay 等都这样测量
4. **相对比较** - 可以对比不同设置的性能差异

---

## 六、完整的帧生命周期

```
┌─────────────────────────────────────────────────────────────────────┐
│                         帧的完整旅程                                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. CPU 准备数据                                                     │
│     ├─ 游戏逻辑                                                      │
│     ├─ 物理计算                                                      │
│     └─ Draw Call 提交                                                │
│           │                                                          │
│           ▼                                                          │
│  2. GPU 渲染                                                         │
│     ├─ 顶点处理                                                      │
│     ├─ 光栅化                                                        │
│     └─ 像素着色 → 写入 Back Buffer                                   │
│           │                                                          │
│           ▼                                                          │
│  3. Present() 调用  ← 我们测量的点                                   │
│     ├─ 请求显示当前帧                                                │
│     └─ 进入帧队列                                                    │
│           │                                                          │
│           ▼                                                          │
│  4. 帧队列等待（0-3 帧延迟）                                          │
│           │                                                          │
│           ▼                                                          │
│  5. VSync 同步点                                                     │
│     ├─ VSync On: 等待显示器刷新                                      │
│     └─ VSync Off: 立即交换                                           │
│           │                                                          │
│           ▼                                                          │
│  6. Buffer 交换                                                      │
│     ├─ Flip: 指针交换                                                │
│     └─ Blit: 内存复制                                                │
│           │                                                          │
│           ▼                                                          │
│  7. 显示器扫描输出  ← 用户实际看到的点                                │
│     └─ 从上到下扫描显示                                              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘

延迟组成:
├─ CPU 时间: ~5-20ms
├─ GPU 时间: ~5-15ms
├─ 帧队列: 0-50ms（取决于队列深度）
├─ VSync 等待: 0-16.7ms（@ 60Hz）
└─ 扫描输出: ~8-16ms
总延迟: 20-100+ms
```

---

## 七、实际影响场景分析

### 7.1 场景一：高端 GPU + 低刷新率显示器

```
配置: RTX 4090 + 60Hz 显示器
GPU 能力: 300 FPS
VSync: Off

结果:
├─ 我们显示: 300 FPS
├─ 用户看到: 60 FPS（但有撕裂）
├─ GPU 利用率: ~20%（大量空闲）
└─ 体验: 撕裂明显，感觉不流畅
```

### 7.2 场景二：VSync 开启

```
配置: 任意 GPU + 60Hz 显示器
VSync: On

结果:
├─ 我们显示: 60 FPS（锁定）
├─ 用户看到: 60 FPS
├─ 延迟: 增加 16-33ms（1-2 帧）
└─ 体验: 流畅无撕裂，但输入延迟大
```

### 7.3 场景三：GPU 瓶颈

```
配置: 低端 GPU + 144Hz 显示器
GPU 能力: 45 FPS

结果:
├─ 我们显示: 45 FPS
├─ 用户看到: 45 FPS（重复帧填充到 144Hz）
├─ 体验: 明显卡顿
└─ 问题: 帧时间不均匀导致顿挫感
```

### 7.4 场景四：帧生成技术（DLSS 3, FSR 3）

```
配置: 支持帧生成的游戏
原始帧: 60 FPS
生成帧: +60 FPS

结果:
├─ 传统工具显示: 120 FPS
├─ 实际渲染帧: 60 FPS
├─ 插入帧: 60 FPS（AI 生成）
├─ 用户看到: 120 FPS
└─ 注意: 插入帧有延迟，输入响应仍是 60 FPS 级别
```

---

## 八、如何测量真正的显示帧率

### 8.1 软件方法

| 工具 | 指标 | 说明 |
|------|------|------|
| **PresentMon 2.0** | MsBetweenDisplayChange | Intel 官方工具 |
| **FrameView 1.6+** | Display FPS | NVIDIA 工具 |
| **OCAT** | Displayed FPS | AMD 开源工具 |

### 8.2 硬件方法

```
高速摄像机 (240+ FPS) → 拍摄显示器 → 分析实际刷新
└─ 最准确，但成本高
```

### 8.3 为什么我们不测量显示帧率

1. **技术限制**：
   - 需要访问 DWM（Desktop Window Manager）内部
   - 需要 ETW（Event Tracing for Windows）权限
   - 复杂度大幅增加

2. **兼容性问题**：
   - 不同 Windows 版本行为不同
   - 全屏独占 vs 窗口模式差异

3. **行业惯例**：
   - FRAPS、Steam Overlay、游戏内置都测量 Present
   - 用户已习惯这种指标

---

## 九、对我们工具的建议

### 9.1 当前状态

```
当前: 测量 Present 调用频率（GPU 帧率）
优点: 简单、稳定、符合行业标准
缺点: 不反映用户实际看到的帧率
```

### 9.2 可能的改进

| 改进 | 复杂度 | 价值 |
|------|--------|------|
| 显示 VSync 状态 | 低 | 帮助用户理解 |
| 检测显示器刷新率 | 中 | 提供上下文 |
| 显示帧时间图 | 中 | 分析卡顿 |
| 集成 PresentMon | 高 | 真正的显示帧率 |

### 9.3 建议的 UI 改进

```
当前显示:
┌────────────┐
│ FPS: 120   │
└────────────┘

改进显示:
┌────────────────────┐
│ GPU: 120 FPS       │  ← 我们测量的
│ Display: 60 Hz     │  ← 显示器刷新率
│ VSync: On          │  ← 同步状态
└────────────────────┘
```

---

## 十、总结

### 关键理解

1. **Present ≠ Display**
   - Present 是 GPU 提交帧
   - Display 是显示器显示帧
   - 两者可能有很大差异

2. **用户体验由显示器决定**
   - 60Hz 显示器最多显示 60 FPS
   - 高 GPU FPS 不等于流畅体验

3. **我们测量的仍然有价值**
   - 反映游戏/GPU 性能
   - 行业标准指标
   - 便于性能对比

4. **差异的来源**
   - VSync 同步
   - 帧队列深度
   - Flip/Blit 模式
   - 显示器刷新率

### 最终结论

我们的 FPS Overlay 工具测量的是 **GPU 提交帧的速率**，这是业界标准做法（FRAPS、Steam、游戏内置 FPS 都这样做）。虽然这与用户实际看到的帧率可能存在差异，但仍然是有价值的性能指标。

理解这个差异有助于：
- 正确解读 FPS 数值
- 理解为什么"高 FPS"有时仍然卡顿
- 做出合理的图形设置调整

---

## 参考资料

1. [NVIDIA - Advanced API Performance: Swap Chains](https://developer.nvidia.com/blog/advanced-api-performance-swap-chains/)
2. [Microsoft - DXGI Flip Model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model)
3. [Microsoft - Reduce latency with DXGI 1.3](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains)
4. [PresentMon - Intel](https://github.com/GameTechDev/PresentMon)
5. [FrameView 1.6 Release Notes](https://www.nvidia.com/en-us/geforce/technologies/frameview/)
6. [GamersNexus - Animation Error Methodology](https://gamersnexus.net/gpus-gn-extras-cpus/problem-gpu-benchmarks-reality-vs-numbers-animation-error-methodology-white-paper)
