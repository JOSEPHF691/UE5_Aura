"""Localize player-facing DataAssets and DungeonDemo instance titles in place.

Run with UnrealEditor-Cmd -ExecutePythonScript after the native editor build.
Does not touch item IDs, actor labels, gameplay tags, or save-game keys.
"""

import unreal


assets = unreal.EditorAssetLibrary
items = {
    "HealthPotion": ("生命药水", "恢复 35 点生命。生命已满时不可使用。"),
    "ManaPotion": ("法力药水", "恢复 20 点法力。法力已满时不可使用。"),
    "ArcaneStaff": ("奥术法杖", "提供属性加成的武器，不改变左手法杖外观。"),
    "WardenArmor": ("守卫护甲", "提高生命上限和护甲。"),
    "SeerHeadpiece": ("先知头饰", "提高法力上限和智力。"),
    "WindShoes": ("疾风之靴", "提高移动速度和护甲。"),
    "RespecStone": ("洗点石", "在大魔法师处重新分配技能点。确认洗点后消耗。"),
    "ArcaneOreShard": ("奥术矿石", "武器强化材料，可用于强化至 +1～+3。"),
    "EmberweaveStaff": ("织焰法杖", "提高智力和法力。传说品质：火球术接 Q 的余烬印记延长 1.5 秒。"),
    "StormcallRod": ("唤雷法杖", "提高智力和移速。传说品质：Q 接 E 可额外连锁一名敌人。"),
    "BastionRobe": ("堡垒法袍", "大幅提高生命上限，兼顾少量护甲。"),
    "SpellguardMantle": ("护法披肩", "提高法力上限和护甲，适合法术流派。"),
    "AshenCrown": ("灰烬冠冕", "提高智力，并提供少量法力上限。"),
    "BulwarkCowl": ("壁垒兜帽", "提高生命上限和护甲，适合防御流派。"),
    "FleetstepBoots": ("迅捷之靴", "大幅提高移速，帮助躲避敌人预警。"),
    "GroundingBoots": ("坚守之靴", "提高护甲和生命上限。"),
}

for asset_name, (display_name, description) in items.items():
    path = f"/Game/Aura/Data/Items/DA_Item_{asset_name}"
    asset = assets.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing item definition: {path}")
    asset.set_editor_property("display_name", display_name)
    asset.set_editor_property("description", description)
    if not assets.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save item definition: {path}")

enemies = {
    "BP_AuraEnemy": "食尸鬼",
    "BP_AuraGoblinSpear": "哥布林枪兵",
    "BP_AuraShroom": "洞穴蘑菇",
    "BP_AuraEliteShaman": "精英萨满",
    "BP_AuraBossDemon": "恶魔领主",
}
for blueprint_name, display_name in enemies.items():
    folder = "" if blueprint_name == "BP_AuraEnemy" else "Dungeon/"
    path = f"/Game/Aura/Blueprints/{folder}{blueprint_name}"
    blueprint = assets.load_asset(path)
    if not blueprint:
        raise RuntimeError(f"Missing enemy blueprint: {path}")
    cdo = unreal.get_default_object(blueprint.generated_class())
    cdo.set_editor_property("enemy_display_name", display_name)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not assets.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save enemy blueprint: {path}")

encounter_titles = {
    "BREAK THE OUTER GUARD": "突破外围守卫",
    "DEFEAT THE SHAMAN ELITE": "击败精英萨满",
    "DEFEAT THE DEMON LORD": "击败恶魔领主",
    "DEFEAT THE BOON GUARDIANS": "击败增益守卫",
    "DEFEAT THE ELITE BOON GUARDIAN": "击败精英增益守卫",
    "SURVIVE THE AMBUSH": "在伏击中生存",
    "SURVIVE THE RISK WING": "穿越险境侧翼",
    "CLEAR THE CHAMBER": "清理房间",
}
map_path = "/Game/Map/DungeonDemo"
world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
if not world:
    raise RuntimeError(f"Could not load map: {map_path}")
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
encounter_count = 0
for actor in actor_subsystem.get_all_level_actors():
    if not actor.get_class().get_name().startswith("AuraEncounterVolume"):
        continue
    current = str(actor.get_editor_property("encounter_title"))
    translated = encounter_titles.get(current)
    if translated:
        actor.set_editor_property("encounter_title", translated)
        encounter_count += 1
    elif any("A" <= char <= "Z" or "a" <= char <= "z" for char in current):
        raise RuntimeError(
            f"Untranslated encounter title: {actor.get_actor_label()} = {current}"
        )
if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
    raise RuntimeError(f"Could not save localized map: {map_path}")

unreal.log_warning(
    "AURA_CHINESE_LOCALIZATION_SUCCESS "
    f"items={len(items)} enemies={len(enemies)} encounters={encounter_count}"
)
