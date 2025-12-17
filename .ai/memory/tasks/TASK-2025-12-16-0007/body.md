# TASK-2025-12-16-0007

本任务为仓库增加 Windows CI 编译工作流，作为 AI 驱动开发的“可合并前最低保障”（至少能编译）。

产物：
- `.github/workflows/build_windows.yml`：windows-latest +（先下载 third_party 依赖）+ CMake Release x64
- `scripts/download_deps.ps1`：支持 CI 非交互运行，固定 MinHook/ImGui 版本并安全写入 third_party/
