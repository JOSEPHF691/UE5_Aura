"""Idempotent M16 migration for pre-M20 maps only.

Run after M13/M14 and a native AuraEditor build. Do not reapply to the M20
three-tier map: this script authors the old upper-north geometry positions.
"""

import unreal


assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
item_dir = "/Game/Aura/Data/Items"
map_path = "/Game/Map/DungeonDemo"
world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
if not world:
    raise RuntimeError("Could not load DungeonDemo")
if any(actor.get_actor_label() == "DungeonStairs_M20_DeepAscent"
       for actor in actors.get_all_level_actors()):
    raise RuntimeError("M20 map detected; do not rerun the pre-M20 M16 migration")


def required(path):
    value = assets.load_asset(path)
    if not value:
        raise RuntimeError(f"Missing asset: {path}")
    return value


def item_tag(name):
    tag = unreal.GameplayTag()
    if not tag.import_text(name):
        raise RuntimeError(f"Invalid item tag {name}")
    container = unreal.GameplayTagContainer()
    if not container.import_text(f'(GameplayTags=((TagName="{name}")))'):
        raise RuntimeError(f"Could not import item tag container {name}")
    return container


def modifier(name, amount):
    tag = unreal.GameplayTag()
    if not tag.import_text(name):
        raise RuntimeError(f"Invalid stat tag {name}")
    result = unreal.AuraItemStatModifier()
    result.set_editor_property("stat_tag", tag)
    result.set_editor_property("magnitude", float(amount))
    return result


staff_mesh = required("/Game/Assets/Characters/Aura/Staff/SM_Staff")
floor_mesh = required("/Game/Assets/Dungeon/SM_Tile_6x9")
wall_mesh = required("/Game/Assets/Dungeon/SM_Wall_4x2")
fallback_mesh = required("/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal")
icons = {
    "weapon": required("/Game/Aura/UI/Items/T_Equipment_ArcaneStaff"),
    "armor": required("/Game/Aura/UI/Items/T_Equipment_WardenArmor"),
    "head": required("/Game/Aura/UI/Items/T_Equipment_SeerHeadpiece"),
    "shoes": required("/Game/Aura/UI/Items/T_Equipment_WindShoes"),
}
slots = {
    "weapon": unreal.AuraEquipmentSlot.WEAPON,
    "armor": unreal.AuraEquipmentSlot.ARMOR,
    "head": unreal.AuraEquipmentSlot.HEAD,
    "shoes": unreal.AuraEquipmentSlot.SHOES,
}
# Each slot has three genuinely comparable bases including the M11 original.
# Combo tags only function when the rolled instance is Legendary.
equipment = (
    ("EmberweaveStaff", "weapon", "Emberweave Staff",
     "Intelligence and mana. Legendary: Ember lasts 1.5s longer for Firebolt > Q.",
     (("Intelligence", 3), ("MaxMana", 5)), "Item.Power.EmberEcho"),
    ("StormcallRod", "weapon", "Stormcall Rod",
     "Intelligence and movement. Legendary: Q > E chains to one extra enemy.",
     (("Intelligence", 3), ("MoveSpeed", 15)), "Item.Power.FractureSurge"),
    ("BastionRobe", "armor", "Bastion Robe",
     "High health, lighter armor; a forgiving close-range route.",
     (("MaxHealth", 30), ("Armor", 1)), None),
    ("SpellguardMantle", "armor", "Spellguard Mantle",
     "Mana and armor for a spell-heavy route.",
     (("MaxMana", 12), ("Armor", 2)), None),
    ("AshenCrown", "head", "Ashen Crown",
     "More intelligence with a smaller mana reserve.",
     (("Intelligence", 4), ("MaxMana", 5)), None),
    ("BulwarkCowl", "head", "Bulwark Cowl",
     "Health and armor for a defensive route.",
     (("MaxHealth", 16), ("Armor", 2)), None),
    ("FleetstepBoots", "shoes", "Fleetstep Boots",
     "Maximum mobility for dodging enemy warnings.",
     (("MoveSpeed", 50),), None),
    ("GroundingBoots", "shoes", "Grounding Boots",
     "Less mobility, better armor and health.",
     (("Armor", 4), ("MaxHealth", 10)), None),
)

factory = unreal.DataAssetFactory()
factory.set_editor_property("data_asset_class", unreal.AuraItemDefinition)
new_items = []
for asset_name, kind, display_name, description, modifiers, power_tag in equipment:
    path = f"{item_dir}/DA_Item_{asset_name}"
    definition = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if not definition:
        definition = tools.create_asset(f"DA_Item_{asset_name}", item_dir,
                                        unreal.AuraItemDefinition, factory)
    if not definition or definition.get_class() != unreal.AuraItemDefinition.static_class():
        raise RuntimeError(f"Invalid equipment asset {path}")
    definition.set_editor_property("item_id", unreal.Name(f"Equipment.{asset_name}"))
    definition.set_editor_property("display_name", display_name)
    definition.set_editor_property("description", description)
    definition.set_editor_property("item_type", unreal.AuraItemType.EQUIPMENT)
    definition.set_editor_property("equipment_slot", slots[kind])
    definition.set_editor_property("default_rarity", unreal.AuraItemRarity.COMMON)
    definition.set_editor_property("max_stack_size", 1)
    definition.set_editor_property("droppable", True)
    definition.set_editor_property("base_stat_modifiers", [
        modifier(f"Item.Stat.{stat}", amount) for stat, amount in modifiers
    ])
    definition.set_editor_property("world_mesh", staff_mesh if kind == "weapon" else fallback_mesh)
    definition.set_editor_property("world_mesh_scale", 1.0)
    definition.set_editor_property("icon", icons[kind])
    if power_tag:
        definition.set_editor_property("item_tags", item_tag(power_tag))
    if not assets.save_loaded_asset(definition, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {path}")
    new_items.append(definition)

catalog = required(f"{item_dir}/DA_ItemCatalog")
catalog_items = list(catalog.get_editor_property("items"))
ids = {str(item.get_editor_property("item_id")) for item in catalog_items if item}
for item in new_items:
    if str(item.get_editor_property("item_id")) not in ids:
        catalog_items.append(item)
catalog.set_editor_property("items", catalog_items)
if not assets.save_loaded_asset(catalog, only_if_is_dirty=False):
    raise RuntimeError("Could not save expanded catalog")

loot = required("/Game/Aura/Data/Loot/DA_LootProfiles")
all_equipment_ids = [unreal.Name(f"Equipment.{name}") for name in (
    "ArcaneStaff", "WardenArmor", "SeerHeadpiece", "WindShoes",
) + tuple(spec[0] for spec in equipment)]
for field in ("common_enemy", "elite_enemy", "seal_guardian", "boss", "treasure"):
    profile = loot.get_editor_property(field)
    profile.set_editor_property("equipment_item_ids", all_equipment_ids)
    loot.set_editor_property(field, profile)
loot.set_editor_property("balance_version", unreal.Name("DemoLoot_v1_2_M16TwelveBases"))
if not assets.save_loaded_asset(loot, only_if_is_dirty=False):
    raise RuntimeError("Could not save expanded loot profiles")

balance = required("/Game/Aura/Data/DA_CombatBalance")
encounter_balance = balance.get_editor_property("encounters")
encounter_balance.set_editor_property("optional_branch_enemy_count", 8)
balance.set_editor_property("encounters", encounter_balance)
abyss = unreal.AuraAbyssTrialBalance()
for field, value in (("max_health_multiplier", 1.30),
                     ("damage_multiplier", 1.15),
                     ("soul_multiplier", 1.25),
                     ("ore_multiplier", 1.25)):
    abyss.set_editor_property(field, value)
balance.set_editor_property("abyss_trial", abyss)
balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v2_1_AbyssTrial"))
if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("Could not save M16 encounter budget")

by_label = {}
for actor in actors.get_all_level_actors():
    by_label.setdefault(actor.get_actor_label(), []).append(actor)


def singleton(label):
    found = by_label.get(label, [])
    if len(found) != 1:
        raise RuntimeError(f"Expected exactly one {label}, got {len(found)}")
    return found[0]


def ensure_actor(label, actor_class, location, rotation=unreal.Rotator()):
    found = by_label.get(label, [])
    if len(found) > 1:
        raise RuntimeError(f"Duplicate actor: {label}")
    if found:
        actor = found[0]
        if actor.get_class() != actor_class:
            raise RuntimeError(f"Unexpected class for {label}")
        return actor
    actor = actors.spawn_actor_from_class(actor_class, location, rotation)
    if not actor:
        raise RuntimeError(f"Could not spawn {label}")
    actor.set_actor_label(label)
    actor.set_editor_property("tags", [unreal.Name("AuraGeneratedM16")])
    by_label[label] = [actor]
    return actor


def static_actor(label, mesh, location, rotation, scale=unreal.Vector(1, 1, 1)):
    actor = ensure_actor(label, unreal.StaticMeshActor.static_class(), location, rotation)
    actor.set_actor_location(location, False, False)
    actor.set_actor_rotation(rotation, False)
    actor.set_actor_scale3d(scale)
    actor.get_editor_property("static_mesh_component").set_static_mesh(mesh)
    return actor


# Carve an upper-north opening from the existing upper hall, then extend the
# supplied tile kit into a fourth dead-end chamber. Keep the original M14 map.
old_wall_labels = sorted(label for label in by_label if label.startswith("DungeonWall_UpperNorth_"))
if len(old_wall_labels) not in (0, 3):
    raise RuntimeError(f"Unexpected upper-north wall count: {old_wall_labels}")
for label in old_wall_labels:
    for actor in by_label[label]:
        actors.destroy_actor(actor)
    del by_label[label]

for index, y in enumerate((900.0, 2100.0)):
    static_actor(f"DungeonFloor_UpperNorth_{index:02d}", floor_mesh,
                 unreal.Vector(6000.0, y, 600.0), unreal.Rotator(0, 90, 0))

# The source wall has an off-centre pivot; match setup_dungeon_demo.py exactly.
static_actor("DungeonWall_M16_UpperNorthDoorEast", wall_mesh,
             unreal.Vector(6000.0, 350.0, 730.0), unreal.Rotator(),
             unreal.Vector(.75, 1, 1))
for side, boundary in (("West", 4200.0), ("East", 6000.0)):
    for index, y in enumerate((900.0, 1700.0, 2500.0)):
        west = side == "West"
        static_actor(f"DungeonWall_M16_UpperNorth{side}_{index:02d}", wall_mesh,
                     unreal.Vector(boundary - 550.0 if west else boundary + 550.0,
                                   y + 800.0 if west else y, 730.0),
                     unreal.Rotator(0, -90 if west else 90, 0))
for index, x in enumerate((4200.0, 5000.0, 5800.0)):
    static_actor(f"DungeonWall_M16_UpperNorthEnd_{index:02d}", wall_mesh,
                 unreal.Vector(x, 2750.0, 730.0), unreal.Rotator())

gate_class = unreal.load_class(None, "/Script/Aura.AuraDungeonGate")
room_class = unreal.load_class(None, "/Script/Aura.AuraEncounterVolume")
chest_class = unreal.load_class(None, "/Script/Aura.AuraLootChest")
ritual_class = unreal.load_class(None, "/Script/Aura.AuraRitualSite")
if not all((gate_class, room_class, chest_class, ritual_class)):
    raise RuntimeError("Native M16 actors unavailable; build AuraEditor first")
gate = ensure_actor("DungeonGate_UpperNorthRisk", gate_class,
                    unreal.Vector(5100.0, 1250.0, 825.0), unreal.Rotator(0, 90, 0))
gate.set_editor_property("starts_open", True)
chest = ensure_actor("DungeonLootChest_UpperNorthRisk", chest_class,
                     unreal.Vector(5100.0, 2980.0, 600.0), unreal.Rotator(0, 90, 0))
chest.set_editor_property("loot_chest_id", unreal.Name("UpperNorthRiskLoot"))
chest.set_editor_property("loot_profile", unreal.AuraLootProfileType.BOSS)
room = ensure_actor("DungeonRewardEncounter_UpperNorthRisk", room_class,
                    unreal.Vector(5100.0, 1450.0, 600.0), unreal.Rotator(0, 90, 0))
room.set_editor_property("encounter_title", "SURVIVE THE RISK WING")
room.set_editor_property("encounter_order", 104)
room.set_editor_property("encounter_type", unreal.AuraEncounterType.GUARDED_TREASURE)
room.set_editor_property("balance_profile", unreal.AuraEncounterBalanceProfile.CUSTOM)
room.set_editor_property("enemy_class", required("/Game/Aura/Blueprints/BP_AuraEnemy").generated_class())
shaman = required("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman").generated_class()
goblin = required("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear").generated_class()
shroom = required("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom").generated_class()
room.set_editor_property("spawn_enemy_classes", [shaman, goblin, shroom])
room.set_editor_property("spawn_offsets", [unreal.Vector(650, 0, 96),
                                            unreal.Vector(950, -300, 96),
                                            unreal.Vector(950, 300, 96)])
room.set_editor_property("trigger_extent", unreal.Vector(360, 650, 180))
room.set_editor_property("arming_duration", 1.0)
room.set_editor_property("arming_room_entry_depth", 40.0)
room.set_editor_property("gather_team_on_lock", False)
room.set_editor_property("reward_encounter", True)
room.set_editor_property("encounter_id", unreal.Name("UpperNorthRiskGuardians"))
room.set_editor_property("loot_chest", chest)
room.set_editor_property("controlled_gates", [gate])
site = ensure_actor("DungeonRitual_UpperNorth", ritual_class,
                    unreal.Vector(5450.0, 2780.0, 600.0))
site.set_editor_property("site_type", unreal.AuraRitualSiteType.BRANCH_SEAL)
site.set_editor_property("seal_id", unreal.Name("UpperNorth"))
site.set_editor_property("prerequisite_encounter", room)

# A clear encounter-composition change, not just a health multiplier.
for label, abyss_classes in (
    ("DungeonEncounter_OuterGuard", [goblin, shaman, shroom]),
    ("DungeonRewardEncounter_LowerNorth", [shaman, goblin]),
    ("DungeonRewardEncounter_UpperNorthRisk", [shaman, shaman, goblin]),
):
    target = singleton(label)
    target.set_editor_property("abyss_spawn_enemy_classes", abyss_classes)

for actor in (gate, chest, room, site):
    if hasattr(actor, "rerun_construction_scripts"):
        actor.rerun_construction_scripts()
if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
    raise RuntimeError("Could not save M16 map")
unreal.log_warning("AURA_M16_CONTENT_SUCCESS: 12 bases, 4 seals, upper-north risk wing, Abyss classes")
