# 怎么使用本仓库（本地文档）

这份文档回答两个问题：
1) 我怎么把 **FPS Overlay 跑起来**（不要求你会编译）  
2) 我怎么用 **Codex CLI + 本仓库的 AI 工作流** 持续迭代（让 AI 负责决策与执行，你只做验收）

---

## 0. 你现在该做什么（最短路径）

### A) 你只想“先用起来”FPS Overlay
1. 打开 GitHub 仓库 → Actions → 选择工作流 `Windows Build`
2. 下载 artifact：`fps-overlay-windows-x64`
3. 解压后 **以管理员身份** 运行 `launcher.exe`
4. 首次运行会生成并打开 `games.txt`：每行写一个游戏进程名（例如 `Game.exe`），保存后重启 `launcher.exe`
5. 正常启动游戏：出现 FPS/帧时间叠加即成功；默认 `F1` 切换显示/隐藏

更详细说明见：`docs/USER_GUIDE.md`

### B) 你想“让 AI 驱动开发/迭代”
只做两件事：
1) 把需求写进仓库（推荐 `docs/requirements.md`）  
2) 每次让 AI 以一个 `TASK` 为单位改动并合并 PR（下面给你可以复制粘贴的命令模板）

---

## 1. FPS Overlay：运行方式（推荐：launcher.exe）

### 1.1 你会拿到哪些文件？
从 Actions artifact 解压后，目录里至少应包含：
- `launcher.exe`：托盘后台监控 + 自动注入（推荐使用）
- `injector.exe`：命令行注入器（调试用）
- `fps_overlay.dll`：叠加层模块（被注入到游戏进程）

首次运行 `launcher.exe` 会在同目录生成：
- `games.txt`：要监控的游戏进程名列表
- `overlay.ini`：叠加层显示配置（运行时热加载，≤ 1s 生效）

### 1.2 launcher.exe 的最小使用步骤
1. 管理员运行 `launcher.exe`
2. 在自动弹出的 `games.txt` 里填你的游戏进程名（任务管理器里看到的 `xxx.exe`）
3. 保存后退出并重新运行 `launcher.exe`
4. 启动游戏 → 等托盘通知 “Injected” → 看叠加层是否出现

常见问题与参数说明见：`docs/USER_GUIDE.md`

---

## 2. 这个仓库的 AI 工作流：它是什么、为什么有用

### 2.1 你在解决什么问题？
AI 驱动的软件开发最大的问题不是“写代码”，而是 **长期一致性**：
- 不同 Agent/不同时间的上下文不一致 → 决策漂移
- 只靠聊天记录 → 记忆丢失/不可审计/不可复现
- 改动没有边界 → 容易误伤仓库其他部分

### 2.2 本仓库怎么解决？
核心机制是三件套：
1) **Repo-first Memory（真相在仓库里）**：`.ai/memory/**` 保存约束/决策/流程/任务  
2) **Deterministic Context Pack（唯一资料包）**：用 `memctl build-pack` 生成 `context_pack.json`，AI 开工只允许依据 pack 内文件  
3) **PR Gate（强制执行）**：PR 打 `ai` 标签后，CI 会校验 Task ID、memory 合法性、stale、pack 可复现

入口文档：
- `.ai/README.md`
- `AGENTS.md`
- `.ai/memory/runbooks/RUNBOOK-0001-codex-cli-workflow/body.md`

---

## 3. 你在本仓库里如何让 Codex CLI 开始干活（可复制粘贴）

### 3.1 每次迭代的标准步骤（你只要照做）

#### 第 1 步：确认需求文档
- 需求默认放：`docs/requirements.md`

#### 第 2 步：新建一个 TASK（一次任务一个 PR）
路径格式：
- `.ai/memory/tasks/TASK-YYYY-MM-DD-000X/meta.json`
- `.ai/memory/tasks/TASK-YYYY-MM-DD-000X/body.md`

`meta.json` 里最重要的是：
- `goal`：这次要达成什么
- `acceptance_criteria`：你怎么判断“做完了”
- `pack.include_paths`：这次允许 AI 阅读/修改的文件范围（越小越安全）

#### 第 3 步：生成 context pack（AI 开工前必须做）
在仓库根目录运行：
```bash
python .ai/tools/memctl.py validate --commit HEAD
python .ai/tools/memctl.py check-stale --commit HEAD
python .ai/tools/memctl.py build-pack --commit HEAD --task-id TASK-... --out context_pack.json
python .ai/tools/memctl.py validate-pack --pack context_pack.json --task-id TASK-...
```

#### 第 4 步：把这段“开场指令”粘贴进 Codex CLI
把 `TASK-...` 换成你的 Task ID：
```
Task ID: TASK-...
请读取并遵守 context pack: context_pack.json
规则：
1) 只依据 pack 内的文件做判断与修改；
2) 如果你需要 pack 外的文件，请先输出“需要加入 pack 的路径清单”，等待我更新 TASK 的 pack 后再继续；
3) 完成后给出：变更摘要 + 你运行过的验证命令 + 可用于 PR 的描述（包含 Task ID）。
```

#### 第 5 步：提 PR 并合并
PR 要求：
- PR 正文必须包含：`Task ID: TASK-...`
- 给 PR 打标签：`ai`（启用门禁）

---

## 4. 怎么迁移到你的其他 GitHub 项目（标准仓库）

最小迁移清单（建议直接复制）：
- `.ai/`
- `AGENTS.md`
- `.github/workflows/ai_pr_gate.yml`
- `.github/workflows/ai_memory_validate.yml`
- `.github/workflows/ai_context_pack.yml`
- `.github/PULL_REQUEST_TEMPLATE/ai.md`（如果你希望 PR 模板强制包含 Task ID）
- `CODEOWNERS`（可选）

迁移后你的日常动作基本不变：
- 你仍然在 CLI/IDE 里写需求、看 diff、合 PR  
- 只是每次让 AI 干活前，你先生成 `context_pack.json`，并要求 AI 只依据 pack 工作

