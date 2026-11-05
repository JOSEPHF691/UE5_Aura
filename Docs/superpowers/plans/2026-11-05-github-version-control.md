# Aura GitHub Version Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有 Aura 工程纳入 Git 与 Git LFS，并安全推送到用户指定的 `JOSEPHF691/UE5_Aura`。

**Architecture:** 普通 Git 管理文本，LFS 管理 UE 资产和图标原图。当前目录尚无仓库，因此直接创建真实初始快照；不创建空历史的 worktree，不改动游戏代码或资产。

**Tech Stack:** Git、Git LFS、GitHub、本机 Git Credential Manager、PowerShell。

**Spec:** 用户已确认的仓库方案；文件职责与日常流程见 `README.md`、`Docs/Version_Control.md`。

## Global Constraints

- 目标为 `https://github.com/JOSEPHF691/UE5_Aura`；远端已有内容时先检查，不强制覆盖。
- 保留完整地图及外部 Actor 数据；忽略生成目录、RiderLink 和个人简历。
- 只记录真实当前快照与后续修改；不伪造历史，不声明未经验证的游戏测试结果。
- 远端当前公开；完整工程推送前先获得用户关于可见性的明确选择。
- 不输出或提交 GitHub 登录凭据，不改动全局 Git 配置。

## Review Focus

- 忽略规则不能遗漏地图依赖，也不能把生成缓存纳入初始提交。
- UE 资产必须成为 LFS 指针，真实对象必须上传成功。
- 暂存快照不能包含简历、存档或凭据。
- 首次导入不能覆盖已有远端分支。
- 仓库完整性验证与游戏功能回归结果应分别报告。

## Task 1: Repository configuration

**Files:** `.gitignore`、`.gitattributes`、`README.md`、`Docs/Version_Control.md`。

- [x] 创建上述文件，初始化 `main` 并仅在本仓库启用 LFS。
- [x] 用 `git check-ignore` 检查排除项与地图文件，用 `git check-attr` 检查 LFS 属性。

## Task 2: Honest baseline snapshot

- [x] 核对待纳入路径、文件数量/体积、密钥特征与暂存差异。
- [x] 验证暂存的 1083 个 LFS 指针及本地对象 SHA-256；新建仓库文件通过空白检查。
- [x] 创建初始提交后运行 `git lfs fsck`（首次提交前不存在 HEAD，无法直接运行此命令）。
- [x] 记录提交 ID、实际纳入的文件数与 LFS 对象数。

## Task 3: Publish and verify

- [ ] 按用户已选择的公开可见性推送完整工程，不使用 force。
- [ ] 核对远端提交 ID；从 GitHub 独立克隆并验证 LFS 对象和关键工程文件。
- [ ] 报告完成结果与真实存在的验证限制。

## 导入检查记录

暂存 1322 个文件，含 1083 个 LFS 对象（约 1321.86 MiB）、49 个外部 Actor 包和 7 个外部 Object 包。15 项忽略检查与 10 项保留检查通过；常见私钥及访问令牌特征扫描未发现匹配。

原有部分源码、配置和文档含行尾空格或文件末尾空行。首次导入保留其原始内容，未为消除格式提示而改动游戏代码；新增仓库文件通过默认 `git diff --cached --check`，全量快照在排除上述两类旧格式提示后通过检查。

本地初始提交：`66cb7225aed4784f57c6bbf097ccb4fc335de018`。提交后 `git lfs fsck` 通过。

发布决定：用户在说明完整工程含第三方资源、并讨论私有完整工程与公开展示方案后，明确选择“保持公开并上传完整工程”。仓库保持现有公开可见性。
