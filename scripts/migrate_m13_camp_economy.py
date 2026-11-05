"""Idempotent M13 migration. Preserve existing map actors and catalog entries."""

import unreal


assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    raise RuntimeError("Missing DA_CombatBalance")
economy = unreal.AuraCampEconomyBalance()
for key, value in (
    ("potion_soul_price", 18),
    ("common_equipment_soul_price", 45),
    ("quality_equipment_soul_price", 80),
    ("respec_stone_soul_price", 100),
    ("respec_stone_purchase_limit", 1),
    ("sell_fraction", 0.25),
    ("forge_one_soul_cost", 20),
    ("forge_two_soul_cost", 40),
    ("forge_three_soul_cost", 70),
    ("forge_one_material_cost", 1),
    ("forge_two_material_cost", 2),
    ("forge_three_material_cost", 3),
    ("weapon_base_stat_bonus_per_level", 0.05),
):
    economy.set_editor_property(key, value)
balance.set_editor_property("camp_economy", economy)
balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v2_0_CampEconomy"))
if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("Could not save camp economy")

loot = assets.load_asset("/Game/Aura/Data/Loot/DA_LootProfiles")
if not loot:
    raise RuntimeError("Missing DA_LootProfiles")
for field, guaranteed, chance in (
    ("common_enemy", 0, 0.14),
    ("elite_enemy", 1, 0.0),
    ("seal_guardian", 0, 0.35),
    ("boss", 3, 0.0),
    ("treasure", 0, 0.0),
):
    profile = loot.get_editor_property(field)
    profile.set_editor_property("guaranteed_arcane_ore", guaranteed)
    profile.set_editor_property("bonus_arcane_ore_chance", chance)
    loot.set_editor_property(field, profile)
loot.set_editor_property("balance_version", unreal.Name("DemoLoot_v1_1_Forge"))
if not assets.save_loaded_asset(loot, only_if_is_dirty=False):
    raise RuntimeError("Could not save ore loot budgets")

item_dir = "/Game/Aura/Data/Items"
material_path = f"{item_dir}/DA_Item_ArcaneOreShard"
material = assets.load_asset(material_path) if assets.does_asset_exist(material_path) else None
if not material:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.AuraItemDefinition)
    material = tools.create_asset("DA_Item_ArcaneOreShard", item_dir,
                                  unreal.AuraItemDefinition, factory)
if not material or material.get_class() != unreal.AuraItemDefinition.static_class():
    raise RuntimeError("Could not create Arcane Ore Shard definition")
material.set_editor_property("item_id", unreal.Name("Material.ArcaneOreShard"))
material.set_editor_property("display_name", "Arcane Ore Shard")
material.set_editor_property("description", "Personal forge material. Used for weapon +1 to +3.")
material.set_editor_property("item_type", unreal.AuraItemType.MATERIAL)
material.set_editor_property("equipment_slot", unreal.AuraEquipmentSlot.NONE)
material.set_editor_property("default_rarity", unreal.AuraItemRarity.QUALITY)
material.set_editor_property("max_stack_size", 20)
material.set_editor_property("droppable", True)
material.set_editor_property("consumable_effect", unreal.AuraConsumableEffect.NONE)
material.set_editor_property("restore_amount", 0.0)
material.set_editor_property("world_mesh", assets.load_asset(
    "/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal"))
material.set_editor_property("world_mesh_scale", 0.4)
material.set_editor_property("icon", assets.load_asset(
    "/Game/Assets/UI/Pickups/T_ManaCrystal"))
if not assets.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError("Could not save Arcane Ore Shard")

catalog = assets.load_asset(f"{item_dir}/DA_ItemCatalog")
if not catalog:
    raise RuntimeError("Missing DA_ItemCatalog")
items = list(catalog.get_editor_property("items"))
if not any(item and str(item.get_editor_property("item_id")) == "Material.ArcaneOreShard"
           for item in items):
    items.append(material)
    catalog.set_editor_property("items", items)
    if not assets.save_loaded_asset(catalog, only_if_is_dirty=False):
        raise RuntimeError("Could not add Arcane Ore Shard to catalog")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/DungeonDemo")
if not world:
    raise RuntimeError("Could not load DungeonDemo")
actor_system = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
vendor_class = unreal.load_class(None, "/Script/Aura.AuraCampVendor")
if not vendor_class:
    raise RuntimeError("Missing AuraCampVendor class")
all_actors = actor_system.get_all_level_actors()
changed = False
for label, position, service in (
    ("DungeonMerchant_SafeCamp", unreal.Vector(4100.0, 450.0, 600.0),
     unreal.AuraCampService.MERCHANT),
    ("DungeonBlacksmith_SafeCamp", unreal.Vector(4600.0, 450.0, 600.0),
     unreal.AuraCampService.BLACKSMITH),
):
    found = [actor for actor in all_actors if actor.get_actor_label() == label]
    if len(found) > 1:
        raise RuntimeError(f"Duplicate {label} actors; resolve manually")
    if found:
        vendor = found[0]
    else:
        vendor = actor_system.spawn_actor_from_class(vendor_class, position, unreal.Rotator())
        if not vendor:
            raise RuntimeError(f"Could not spawn {label}")
        vendor.set_actor_label(label)
        vendor.set_editor_property("tags", [unreal.Name("AuraGeneratedM13")])
        changed = True
    if vendor.get_class() != vendor_class:
        raise RuntimeError(f"{label} has unexpected class")
    if vendor.get_editor_property("service_type") != service:
        vendor.set_editor_property("service_type", service)
        changed = True
    if changed and hasattr(vendor, "rerun_construction_scripts"):
        vendor.rerun_construction_scripts()
if changed and not unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Map/DungeonDemo"):
    raise RuntimeError("Could not save DungeonDemo vendors")

unreal.log_warning("AURA_M13_CAMP_DATA_SUCCESS")
