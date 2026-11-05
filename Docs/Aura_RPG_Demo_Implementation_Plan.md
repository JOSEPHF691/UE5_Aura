# Aura RPG Demo 实施计划（历史版本）

> 2026-10-26：Q/E 三级进化、取消独立连携与 v6 迁移的实施和验收统一记录在 [Skill_Elemental_Rework.md](Skill_Elemental_Rework.md)。

> 本文件保留早期实施记录。后续统一以 [Aura_RPG_Complete_Development_Plan.md](Aura_RPG_Complete_Development_Plan.md) 为主计划。

更新日期：2026-09-18

## 1. Demo 目标

利用项目内现有人物、敌人、场景、动画、音效和 Niagara 资产，完成一个可持续扩展的 GAS RPG Demo。核心体验包括：

- 固定上帝视角，支持 WASD 朝向移动与按住鼠标右键旋转镜头。
- WASD 移动、鼠标视角控制和统一战斗输入。
- 基于 Gameplay Ability System 的属性、消耗、技能、伤害和状态处理。
- 玩家施法、投射物命中、敌人受击/死亡、HUD 反馈与基础敌人 AI。
- 一段可开始、战斗、结束并重新游玩的短关卡流程。

## 2. 当前框架

核心调用链：

`Enhanced Input → AuraPlayerController → AbilitySystemComponent → GameplayAbility → 瞄准/投射物 → GameplayEffect → AttributeSet → 敌人状态与表现`

模块职责：

- `AAuraCharacter`：上帝视角相机、移动表现、玩家 ASC 初始化和初始技能授予。
- `AAuraPlayerController`：上帝视角输入映射与鼠标瞄准、Arcane Shards/Shock 选择状态机、宝箱三选一输入状态，以及带遮挡检测和尸体跟踪的 Boss 胜利镜头。
- `UAuraAnimInstance`：Idle/Walk/Run 基础姿态与 `DefaultSlot` 施法动画叠加。
- `UAuraAttributeSet`：Health、Mana、主属性和次级属性的复制与边界约束。
- `UAuraFireBoltAbility`：免费基础施法、冷却、声音和延迟生成火球。
- `UAuraArcaneShardsAbility`：消费鼠标地面目标、服务器侧限制施法距离、延迟爆发、范围查询和多目标伤害。
- `UAuraShockAbility`：消费鼠标确认方向，以窄幅 Sweep 选择 950uu 内首个敌人，造成 40 点伤害并施加 1.75 秒眩晕；电束与火花 Niagara 定时显式销毁。
- `UAuraShamanBoltAbility`：服务器权威的精英远程攻击，锁定起手时的玩家位置，经过 0.8 秒预警后生成 20 点伤害火球，并使用 3 秒 GAS 冷却。
- `UAuraBossAbility`：服务器权威的 Demon Boss 技能状态机；一阶段交替左右爪击，二阶段根据距离选择爪击、范围甩尾或 Hellfire 远程投掷，并公开可复制的预警状态。
- `UAuraManaCostEffect`、三项技能的 Cooldown Effect 与 `UAuraShockStunEffect`：Q/E 的标准 GAS 消耗、全部技能的冷却及持续状态标签；免费 FireBolt 跳过资源扣除。
- `AAuraFireBoltProjectile`：飞行、碰撞、阵营过滤、伤害 GameplayEffect、命中音效与 Niagara 表现。
- `AAuraAIController`：视野感知、仇恨目标、NavMesh 追击、近战/远程距离决策与返回出生点；远程单位会在 450uu 内后撤并优先保持约 700uu 距离。
- `UAuraMeleeAbility`：Ghoul 攻击动画、攻击音效、命中延迟、距离复核、伤害和攻击冷却。
- `AAuraPickup`：服务器权威碰撞拾取、悬浮旋转表现、拾取隐藏与 5 秒原地刷新。
- `UAuraHealthPickupEffect`、`UAuraManaPickupEffect`：通过 Instant GameplayEffect 分别恢复 35 点生命和 20 点法力。
- `AAuraEnemy`：敌人 ASC、可配置近战/远程类型、Idle/Walk/Attack、受击响应、远程施法预警复制、`State.Stunned` 监听、Boss 阶段切换/免伤，以及 Boss 动画转布娃娃死亡演出。
- `AAuraCharacter`：玩家生命监听、受击动画、死亡标签、输入阻断和布娃娃死亡，以及复制的等级/累计经验、六类局内强化、成长属性重建和各技能伤害倍率。
- `AAuraHUD`：生命/法力/等级/经验/冷却/准星/敌人血条、局内强化列表与三选一卡片、Boss 顶部血条与范围预警、电影黑边、升级提示、延迟胜利结算、玩家受击闪烁和失败重开提示。
- `AAuraEncounterVolume`、`AAuraDungeonGate`：按顺序触发主线遭遇、生成敌人、封门、统计击杀并推进目标；奖励支路模式会绑定宝箱、更新剩余守卫数并在清场后解除封印。
- `AAuraDungeonCheckpoint`、`UAuraRunSubsystem`：恢复全部状态、重置补给，并在关卡重载期间保存检查点位置、等级、累计经验、局内强化、已领取宝箱与已清理奖励房。
- `AAuraTreasureChest`：使用 Dungeon 石块与 Beacon 组合成一次性宝箱；守卫存活时显示红色封印与剩余数量，清场后开启碰撞奖励，恢复 35 Health / 20 Mana，并生成三个不重复局内强化供玩家选择。
- `UAuraCombatBalanceData` / `DA_CombatBalance`：玩家、成长曲线、局内强化、技能、普通敌人、精英、Boss、AI、恢复物与强制遭遇数量的统一数值源；消耗、冷却和持续效果通过 SetByCaller 读取配置。

## 3. 已完成基线

- [x] 删除第三人称、全景俯瞰和 `V` 键切换，只保留上帝视角。
- [x] 上帝视角 WASD 会让角色转向移动方向后前进，不使用横移/倒退表现。
- [x] 上帝视角按住鼠标右键旋转镜头，松开后鼠标继续用于战斗交互。
- [x] 角色朝向、Idle/Walk/Run 动画和施法 Slot 动画。
- [x] 法杖绑定到 `hand_l` 的 `WeaponHandSocket`。
- [x] 左键触发 FireBolt，免费造成 15 点伤害，冷却 0.75 秒，零蓝时仍可使用。
- [x] 按 Q 进入 Arcane Shards 鼠标选点，左键确认施法、再次按 Q 取消；最大距离 900uu，超距自动夹取，造成 300uu 半径、每目标 50 点范围伤害，消耗 18 Mana。
- [x] FireBolt、Arcane Shards 与 Shock 使用统一的可配置 GAS Cost/Cooldown 链路；零消耗技能安全跳过扣蓝。
- [x] 按 E 进入 Shock 方向选择，鼠标调整、左键确认、再次按 E 取消；角色转向确认方向并命中 950uu 内第一个敌人，造成 40 点伤害、1.75 秒眩晕，消耗 14 Mana、冷却 6 秒。
- [x] Shock 使用电束/火花 Niagara、施法音效、Ghoul ShockLoop 动画和 `STUNNED` HUD 状态；眩晕期间停止移动、追击和近战，结束后恢复。
- [x] 火球 Niagara、飞行碰撞、15 点伤害、命中特效与命中音效。
- [x] Ghoul 训练敌人蓝图，包含 Idle、HitReact 和 Death 动画。
- [x] 精英萨满独立远程闭环：450/700/1000uu 距离策略、`Shaman_Summon` 施法动画、0.8 秒地面预警、20 点火球、3 秒冷却与 Shock 中断。
- [x] 萨满施法期间显示 `CASTING` 和红色落点圈；投射物会忽略友军，玩家移动可以躲开已锁定的射击点。
- [x] StartupMap 中放置可攻击训练敌人。
- [x] 最大法力 50；战斗恢复 1/s，脱战延迟 3 秒后恢复 6/s；FireBolt 免费，Q 消耗 18，E 消耗 14，普通/精英击杀返还 2/5 Mana。
- [x] 等级 1–5 成长闭环：敌人击杀奖励 25/75/250 XP，升级提升生命、法力、主属性、护甲和三项玩家技能伤害；HUD 显示等级/经验，检查点保留成长进度。
- [x] 两座支路宝箱升级为局内三选一：六类强化、每类最多三层、三个不重复选项、鼠标或 `1/2/3` 选择、选择期伤害保护、HUD 汇总和检查点恢复。
- [x] 生命水晶恢复 35 Health，法力药瓶恢复 20 Mana；训练图支持 5 秒刷新，地牢补给单次使用并由检查点重置。
- [x] `DungeonDemo` 已改为双层分支地牢：三处死胡同包含五只可选敌人、两座宝箱和恢复补给；其中两条奖励支路进入后生成 2 只 Ghoul / 1 只精英萨满，清场前封印宝箱；唯一主路经楼梯、精英战与检查点通向 Boss。
- [x] Demon Boss 两阶段闭环：一阶段 18 点左右爪击交替；50% 生命触发 1.35 秒锁血、免伤转阶段；二阶段移速提升至 310，并加入 28 点范围甩尾、24 点 Hellfire 投掷和 22 点爪击。
- [x] Boss 顶部血条显示阶段、当前招式与 `INVULNERABLE`；甩尾/投掷具有范围预警，普通招式可被 E 技能眩晕打断，转阶段不可打断。
- [x] Boss 使用独立 `Demon_Death_Cinematic` 序列起手，随后衔接稳定布娃娃、火焰爆发、死亡吼叫与落地重音；尸体保留 10 秒。
- [x] 胜利镜头会从当前视角附近选择无遮挡机位，跟随倒地 Boss 缓慢推进环绕，并在 3.2 秒后播放 UI 收尾音和结算面板。
- [x] 自动化资产校验和运行时完整战斗烟测。

当前操作：

- `W/A/S/D`：移动。
- `按住鼠标右键 + 移动鼠标`：上帝视角旋转镜头。
- `鼠标左键`：释放 FireBolt。
- `Q`：进入 Arcane Shards 选点；移动鼠标调整落点，`鼠标左键`确认施法，再按 `Q` 取消。
- `E`：进入 Shock 方向选择；移动鼠标调整方向，`鼠标左键`确认施法，再按 `E` 取消。
- `碰触生命水晶/法力药瓶`：分别恢复 35 Health / 20 Mana；属性已满时不消耗道具。
- `进入奖励支路`：触发守卫战，HUD 显示剩余数量；守卫全部击败后宝箱由红色封印切换为可领取状态。
- `碰触已解锁宝箱`：恢复 35 Health / 20 Mana 并进入三选一；点击卡片或按 `1/2/3` 领取强化。已清场和已开启状态在检查点重载后都会保留。
- `R`：玩家死亡后从已激活检查点重载；胜利后重置本轮进度并重新开始。

## 4. 后续里程碑

### M2：战斗 HUD 与反馈

- [x] 玩家 Health/Mana 条、FireBolt 冷却反馈。
- [x] 上帝视角鼠标落点与技能范围/方向预览。
- [x] 敌人头顶血条、伤害数字和准星命中标记。
- [x] 无法施法时的缺蓝/冷却反馈。
- [ ] 敌人材质命中闪白（美术增强项）。

验收：玩家无需看日志即可理解当前生命、法力、瞄准点、技能是否可用和敌人剩余生命。

### M3：敌人 AI 与近战闭环

- [x] NavMesh、感知/仇恨范围、追击与返回出生点。
- [x] Ghoul Idle/Walk/Attack 动画状态与攻击音效。
- [x] Ghoul 近战攻击 Gameplay Ability、0.88 秒爪击接触帧、距离复核、15 点伤害与攻击冷却。
- [x] 玩家受击动画、HUD 红色反馈、生命归零和 `State.Dead` 状态。
- [x] 玩家布娃娃死亡、移动/视角/攻击阻断、失败界面与 `R` 重开。
- [x] 自动烟测覆盖追击、近战、死亡、返回以及真实 `R` 键重载关卡。
- [x] 自动烟测覆盖萨满后撤、预警、眩晕中断、冷却阻断、再次施法和 20 点远程伤害；Ghoul 近战回归保持通过。

验收：已通过。运行时烟测中 Ghoul 从 700uu 外追击并连续造成 15 点伤害，玩家 100→0 后进入布娃娃死亡，Ghoul 返回出生点；注入 `R` 后 StartupMap 成功重载。

### M4：技能与角色构筑

- [x] 增加范围技能 Arcane Shards：Q 键进入地面选点、左键确认、900uu 施法范围圈、300uu 命中圈、超距夹取、动画、Niagara、音效、群体伤害与 HUD 冷却槽。
- [x] 增加控制技能 Shock，并实现眩晕状态、AI/近战阻断、敌人受控动画和 Niagara 表现。
- [ ] Gameplay Tags 完整管理施法、受击、眩晕、死亡和输入阻断状态。
- [x] 三项玩家攻击接入数据驱动 GAS Cost/Cooldown；FireBolt 零消耗，Q/E 使用共享 Mana Cost GameplayEffect。
- [x] 增加生命与法力碰撞拾取物，恢复走 GAS Instant GameplayEffect，支持联网复制和 5 秒刷新。
- [x] 建立战斗/脱战双速法力恢复、击杀返蓝和检查点全恢复机制。
- [x] 建立击杀 XP、连续升级、成长属性、全技能伤害倍率、等级/经验 HUD 和检查点持久化。
- [x] 建立宝箱三选一局内构筑：FireBolt/Q/E 专精、生命、法力循环、通用伤害、最大层数、HUD 和检查点持久化。
- [ ] 伤害计算与基础护甲结算。
- [x] 首版战斗数值迁移到 `DA_CombatBalance`，技能消耗/冷却、资源恢复、敌人/Boss 与强制遭遇数量均可配置。
- [ ] 技能栏与技能等级。

验收：至少三种技能具有不同战术用途，属性和效果都可数据驱动调整。

### M5：关卡与 Demo 完整流程

- [x] 使用现有 Dungeon 资产生成双层分支地牢，并保留可重复执行的搭建与结构校验脚本。
- [x] 下层南北分支和二层南支路构成三处死胡同；支路提供可选战斗、宝箱、回血或回蓝，东进再北转的唯一主路通向 Boss。
- [x] 楼梯视觉模型与隐藏平滑碰撞分离，上下层门洞居中，运行时动态 NavMesh 完整连通至 Boss 区。
- [x] 外层三只 Ghoul、精英萨满、Demon Boss 的顺序触发、封门和目标 HUD。
- [x] 中段检查点、战前补给、跨 OpenLevel 复活位置保存。
- [x] Boss 击杀后胜利界面、输入锁定和 `R` 重玩。
- [x] 单一上帝视角的资产校验、`V` 无效校验与全流程运行时烟测。
- [x] 精英萨满远程技能、预警、眩晕中断与冷却行为。
- [x] Boss 两阶段机制、范围预警、E 打断窗口、转阶段保护与顶部 Boss HUD。
- [x] Boss 专属死亡序列、胜利音频分层、动态镜头、电影黑边和延迟结算面板。
- [x] 数值配置化与首轮自动平衡回归。
- [x] 两座支路宝箱接入三选一强化、选择期输入/伤害保护及死亡重载防重复领取。
- [x] 两座宝箱接入奖励守卫战：进入支路触发刷怪、逐个更新封印计数、清场解锁，并分别持久化清场与领取状态。
- [ ] 5–10 分钟盲测、联网边界测试和最终打包检查。

验收：从启动、教学、战斗到胜利/失败均无需编辑器操作，可独立打包运行。

## 5. 实施顺序与质量门槛

当前推荐顺序：`场景盲测与第二轮调平衡 → 边界测试 → 打包`。

每个里程碑均要求：

- C++ Development Editor 编译通过。
- Blueprint 与输入资产自动校验通过。
- 上帝视角移动、右键旋转、鼠标选点和 `V` 无效各做一次操作验收。
- 关键战斗数值通过运行时日志或自动烟测验证。
- 不覆盖项目内无关资产；生成脚本保持可重复执行。
