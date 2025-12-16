# TASK-2025-12-16-0006

在现有 overlay.ini 热更新机制上，增加 FPS 统计相关配置：

- `SampleCount`：平滑窗口长度（帧数）
- `DisplayUpdateMs`：显示值刷新周期（毫秒）

实现方式：为 `FpsCounter` 增加可运行时调整的参数，并在 overlay.ini reload 时应用。

