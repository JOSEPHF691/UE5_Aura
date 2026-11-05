# Aura 版本管理约定

## 文件范围

普通 Git 管理 `.uproject`、`.gitignore`、`.gitattributes`、`Source`、`Config`、`Docs` 和 `scripts`。Git LFS 管理 `.uasset`、`.umap` 和 `SourceArt` 下的 PNG 原图。

`Content/__ExternalActors__` 与 `Content/__ExternalObjects__` 属于关卡数据，必须与地图一起提交。`Build`、`Plugins` 不整体忽略；本机 RiderLink 除外。

不提交缓存、编译产物、日志、个人存档、IDE 本地状态、临时备份和个人简历。不把 UE 资源包或 Windows 试玩包直接作为普通 Git 文件反复提交。

## 分支与提交

- `main`：默认主分支。首次提交保存当前工程快照；之后将验证通过的改动合入。
- `feat/<主题>`：新功能，例如 `feat/elite-skills`。
- `fix/<主题>`：缺陷修复，例如 `fix/inventory-stacking`。
- 一次提交描述一个完整目的。提交信息说明改变了什么；复杂改动在正文补充原因、验证方式与未验证项。
- Git 从本次导入开始记录历史，不补造更早的提交或修改提交日期。

日常操作示例：

```powershell
git switch -c fix/inventory-stacking
git status
git diff
# 修改并验证后，按实际范围暂存文件
git add Source/Aura/Private/Items/AuraItemTypes.cpp
git diff --cached
git commit -m "fix: correct consumable stack compatibility"
git push -u origin fix/inventory-stacking
```

上述命令是后续工作的示例，不表示首次导入已单独创建该修复提交。

## 资产修改

保存 UE 编辑器中的修改后，检查 `git status`，连同相关蓝图、地图和外部 Actor 文件一起提交。LFS 只负责二进制资产存储，不会自动解决两人修改同一蓝图或地图的冲突；协作时先约定资产归属，必要时单独启用 LFS 锁。

## 验证与发布

- 提交前分别检查 `git diff --check`、`git diff --cached --check` 和暂存清单，覆盖未暂存与已暂存修改，避免把生成目录、个人文件带入。
- 用 `git lfs ls-files` 检查资产确实受 LFS 管理，用 `git lfs fsck` 校验本地对象。
- 推送后核对本地与远端提交 ID，并从 GitHub 在独立目录克隆，验证实际资产下载与工程完整性。
- 只有对应版本通过实际编译和游玩/回归后，才将其标记为可运行里程碑；仓库上传成功不等于游戏验证通过。
- 需要对外分发试玩包时，单独整理发行内容与第三方资源授权，不把个人存档或本地环境配置打入发布包。

## 定位回归

先保存当前未提交的工作，再在独立工作目录比较最近正常版本和异常版本。通过 `git log`、`git show` 查看修改；有稳定复现步骤时，可使用 `git bisect` 逐步找到首次引入故障的提交。避免直接用破坏性命令丢弃现有工作。

## 本机网络配置

浏览器能够访问 GitHub，而 Git LFS 直连超时时，检查 Git 是否使用了本机现有代理。代理按机器配置在 `.git/config`，不会随仓库分发；首次上传所用开发机已在本仓库配置其现有系统代理。

```powershell
git config --local --get http.proxy
# 不再需要该代理时，移除本仓库设置
git config --local --unset http.proxy
```

不要把访问令牌、带账号密码的远端地址或本机代理凭据写入项目文档和提交内容。
