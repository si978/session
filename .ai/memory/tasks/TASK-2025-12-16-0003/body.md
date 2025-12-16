# TASK-2025-12-16-0003

依据 `docs/requirements.md` 的模块接口定义（6.5 Overlay），补齐 Overlay 基础控制 API，并做热键防抖：

- `Overlay::SetVisible(bool)`
- `Overlay::SetPosition(float x, float y)`
- `Overlay::SetAlpha(float alpha)`

并确保 `F1` 的切换不会因按键自动重复触发导致闪烁。

