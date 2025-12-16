# 根源解决方案：驱动级 FPS 显示

## 问题本质

**全屏独占模式下，普通窗口无法显示是硬件/操作系统层面的限制。**

要突破这个限制，必须在更底层工作：

```
应用层（用户模式）     ← 当前方案，受限于 DWM
      ↓
驱动层（内核模式）     ← 需要的方案，可绑定任何渲染
      ↓
硬件层（GPU）
```

---

## 现有工具如何实现

| 工具 | 技术 | 全屏支持 |
|------|------|----------|
| **RTSS (RivaTuner)** | 内核驱动 Hook | ✅ 全支持 |
| **MSI Afterburner** | 依赖 RTSS | ✅ 全支持 |
| **NVIDIA ShadowPlay** | 驱动级集成 | ✅ 全支持 |
| **Discord/Steam** | 注入 + 白名单 | ✅ 被信任 |
| **我们当前方案** | 用户态窗口 | ❌ 全屏失效 |

**结论：要真正解决，必须走驱动级路线。**

---

## 驱动级方案设计

### 架构

```
┌─────────────────────────────────────────────────────────┐
│                     用户态组件                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │ 托盘程序    │  │ 配置管理    │  │ 与驱动通信      │ │
│  │ games.txt   │  │ 位置/颜色   │  │ DeviceIoControl │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└───────────────────────────┬─────────────────────────────┘
                            │ IOCTL
┌───────────────────────────┴─────────────────────────────┐
│                     内核态驱动                          │
│  ┌─────────────────────────────────────────────────┐   │
│  │              fps_overlay.sys                     │   │
│  │  ┌───────────┐  ┌───────────┐  ┌─────────────┐  │   │
│  │  │ DXGI Hook │  │ FPS 计算  │  │ 文本渲染    │  │   │
│  │  │ Present   │  │ 帧时间    │  │ 直接写显存  │  │   │
│  │  └───────────┘  └───────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                            │
┌───────────────────────────┴─────────────────────────────┐
│                     GPU 驱动 / 硬件                     │
└─────────────────────────────────────────────────────────┘
```

### 核心技术

1. **DXGI Hook (内核级)**
   - Hook `dxgi.dll` 的 Present 函数
   - 在驱动层面拦截，绑定任何模式

2. **直接渲染**
   - 获取 SwapChain 的 BackBuffer
   - 直接在 GPU 内存上绘制 FPS 文本
   - 在 Present 之前完成

3. **字体渲染**
   - 预渲染字符纹理
   - 使用简单的位图拷贝

---

## 实施路径

### 阶段 1：开发环境准备

```
1. 安装 WDK (Windows Driver Kit)
2. 配置驱动开发环境
3. 启用测试签名模式（开发阶段）
   bcdedit /set testsigning on
```

### 阶段 2：基础驱动框架

```c
// fps_overlay.sys 框架
#include <ntddk.h>
#include <wdm.h>

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    // 初始化驱动
    // 创建设备对象
    // 注册 IOCTL 处理
    return STATUS_SUCCESS;
}

// IOCTL 接口
// - 启动/停止监控
// - 设置目标进程
// - 配置显示参数
```

### 阶段 3：DXGI Hook 实现

```c
// Hook IDXGISwapChain::Present
typedef HRESULT(WINAPI* PFN_Present)(IDXGISwapChain*, UINT, UINT);

HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // 1. 计算帧时间
    UpdateFrameTime();
    
    // 2. 获取 BackBuffer
    ID3D11Texture2D* pBackBuffer;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    
    // 3. 渲染 FPS 文本
    RenderFpsOverlay(pBackBuffer);
    
    // 4. 调用原始 Present
    return OriginalPresent(pSwapChain, SyncInterval, Flags);
}
```

### 阶段 4：文本渲染

```c
// 预渲染的字符位图
static BYTE g_fontBitmap[16][10][8]; // 0-9, 16x8 像素

void RenderFpsOverlay(ID3D11Texture2D* pBackBuffer) {
    // 获取纹理描述
    D3D11_TEXTURE2D_DESC desc;
    pBackBuffer->GetDesc(&desc);
    
    // Map 纹理内存
    D3D11_MAPPED_SUBRESOURCE mapped;
    pContext->Map(pBackBuffer, 0, D3D11_MAP_WRITE, 0, &mapped);
    
    // 直接写入像素
    DrawFpsText((BYTE*)mapped.pData, mapped.RowPitch, desc.Width, desc.Height, g_currentFps);
    
    pContext->Unmap(pBackBuffer, 0);
}
```

---

## 驱动签名问题

### 开发阶段
```batch
# 启用测试签名
bcdedit /set testsigning on
# 重启

# 使用测试证书签名驱动
signtool sign /v /s TestCertStore /n "Test Cert" fps_overlay.sys
```

### 发布阶段

| 选项 | 成本 | 说明 |
|------|------|------|
| EV 代码签名证书 | ~$400/年 | 可正常签名驱动 |
| WHQL 认证 | ~$2000+ | 微软官方认证 |
| 开源 + 用户自签 | 免费 | 用户需启用测试模式 |

---

## 开发计划

| 阶段 | 内容 | 时间 |
|------|------|------|
| 1 | 环境搭建 + 基础驱动框架 | 1周 |
| 2 | DXGI Hook 实现 | 2周 |
| 3 | FPS 计算 + 渲染 | 1周 |
| 4 | 用户态通信 + 托盘程序 | 1周 |
| 5 | 测试 + 优化 | 2周 |
| **总计** | | **7周** |

---

## 风险评估

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 反作弊检测驱动 | 高 | 仅用于非竞技游戏 |
| 系统稳定性 | 高 | 充分测试，异常处理 |
| 驱动签名成本 | 中 | 开源 + 测试模式 |
| 兼容性问题 | 中 | 多版本 Windows 测试 |

---

## 替代方案对比

| 方案 | 全屏支持 | 反作弊 | 复杂度 | 推荐 |
|------|----------|--------|--------|------|
| 用户态窗口 | ❌ | ✅ 安全 | 低 | 仅窗口化 |
| DLL 注入 | ✅ | ❌ 被检测 | 中 | 无反作弊游戏 |
| **内核驱动** | ✅ | ⚠️ 可能 | 高 | **全场景** |

---

## 结论

**要实现"不改游戏设置、兼容全部模式"的 FPS 显示，驱动级方案是唯一可行路径。**

是否开始驱动开发？
