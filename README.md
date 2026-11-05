# Aura — 地牢 RPG Demo

基于 **Unreal Engine 5.7 / C++ / Gameplay Ability System（GAS）** 的个人项目。玩家探索多层地牢，完成房间战斗与埋伏事件，通过角色成长、装备和技能进化构建战斗路线，最终挑战 Boss；支持单人及局域网双人 PvE。

## 主要系统

- **GAS 战斗与成长**：属性、技能消耗与冷却、伤害结算、火焰/冰霜/雷电效果、技能等级与进化分支。
- **怪物 AI**：感知、追击、攻击、脱战回巢，普通怪、精英怪及分阶段 Boss 技能。
- **物品与营地**：背包拖拽、消耗品堆叠、装备替换、交易、强化、随机锻造及熔炼。
- **关卡推进**：多层地图、封闭战斗房间、宝箱、埋伏、检查点与最终 Boss。
- **联机与恢复**：服务器权威结算、个人物品同步、队友救援，以及版本化单机成长/检查点存档。

本仓库记录当前工程及后续真实修改。首次提交是已有工程的导入快照，不代表过往开发过程已有 Git 历史，也不代表全部历史测试已在本次导入中重新运行。

## 开发环境

- Windows 64 位。
- Unreal Engine **5.7**，与 `Aura.uproject` 的引擎版本一致。
- Visual Studio 2022 的 C++ 游戏开发工具及 Windows SDK。
- Git 与 Git LFS。

项目通过 `.uproject` 声明所需引擎插件。RiderLink 属于本机 IDE 集成，不是游戏运行依赖，不包含在版本库中。

## 获取与运行

```powershell
git lfs install
git clone https://github.com/JOSEPHF691/UE5_Aura.git
Set-Location UE5_Aura
git lfs pull
```

私有仓库需要先获得访问权限。`.uasset`、`.umap` 和装备图标原图使用 LFS；只有指针文件而没有真实资产时，工程不能完整打开。

右键 `Aura.uproject` 生成 Visual Studio 工程文件，编译 `AuraEditor / Development / Win64`，然后打开项目。也可以从项目根目录编译：

```powershell
$ueRoot = 'D:\UE_5.7' # 修改为本机 UE 5.7 安装位置
$projectPath = Join-Path (Get-Location) 'Aura.uproject'
& "$ueRoot\Engine\Build\BatchFiles\Build.bat" AuraEditor Win64 Development $projectPath -WaitMutex -NoHotReloadFromIDE
```

主关卡为 `/Game/Map/DungeonDemo`。第一次打开需要生成缓存与着色器；缓存、编译产物、个人存档均不随仓库分发。

## 目录与文档

| 路径 | 内容 |
| --- | --- |
| `Source/Aura` | C++ 游戏逻辑与现有回归入口 |
| `Config` | 默认工程、输入与 Gameplay Tags 配置 |
| `Content` | 关卡、蓝图、模型、材质、动画、Niagara 等 UE 资产 |
| `SourceArt` | 装备图标原图 |
| `scripts` | 资产配置及回归脚本 |
| `Docs` | 设计、数值、开发计划与技术文档 |

- [元素技能当前设计与验证入口](Docs/Skill_Elemental_Rework.md)
- [完整开发计划与历史记录](Docs/Aura_RPG_Complete_Development_Plan.md)
- [版本管理约定](Docs/Version_Control.md)

部分脚本仍包含原开发机的项目和引擎绝对路径。换机器后，先检查脚本中的 `$root`、`$project`、`$editor` 等变量，再运行对应回归。历史阶段测试可能仍检查旧玩法；以当前设计文档和对应测试入口为准。

## 资源与分发

工程使用了第三方美术及特效资源，相关权利和授权条件归原作者；这些资源不会因为进入 Git 仓库而变更授权。面向求职的公开展示内容、可公开源码和可分发试玩包，应按各资源原有授权分别整理。本次仓库导入不额外授予第三方素材再分发许可。
