"""Idempotent M23 data-only migration. Does not load or modify DungeonDemo.umap."""

import unreal


assets = unreal.EditorAssetLibrary
balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
catalog = assets.load_asset("/Game/Aura/Data/Items/DA_ItemCatalog")
if not balance or not catalog:
    raise RuntimeError("M23 requires the existing balance and item catalog assets")

metal_path = "/Game/Aura/Data/Items/DA_Item_SmithMetal"
metal = assets.load_asset(metal_path) if assets.does_asset_exist(metal_path) else None
if not metal:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.AuraItemDefinition)
    metal = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DA_Item_SmithMetal", "/Game/Aura/Data/Items", unreal.AuraItemDefinition, factory)
if not metal or metal.get_class() != unreal.AuraItemDefinition.static_class():
    raise RuntimeError("Could not create Smith Metal definition")

metal.set_editor_property("item_id", unreal.Name("Material.SmithMetal"))
metal.set_editor_property("display_name", "锻造金属")
metal.set_editor_property("description", "交给锻造大师打造装备。宝箱、商人或熔炼均可获得。")
metal.set_editor_property("item_type", unreal.AuraItemType.MATERIAL)
metal.set_editor_property("equipment_slot", unreal.AuraEquipmentSlot.NONE)
metal.set_editor_property("default_rarity", unreal.AuraItemRarity.COMMON)
metal.set_editor_property("max_stack_size", 99)
metal.set_editor_property("droppable", False)
metal.set_editor_property("consumable_effect", unreal.AuraConsumableEffect.NONE)
metal.set_editor_property("restore_amount", 0.0)
metal.set_editor_property("world_mesh", assets.load_asset(
    "/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal"))
metal.set_editor_property("world_mesh_scale", 0.25)
metal.set_editor_property("icon", assets.load_asset(
    "/Game/Assets/UI/Pickups/T_ManaCrystal"))
if not assets.save_loaded_asset(metal, only_if_is_dirty=False):
    raise RuntimeError("Could not save Smith Metal definition")

items = list(catalog.get_editor_property("items"))
matches = [entry for entry in items if entry and
           str(entry.get_editor_property("item_id")) == "Material.SmithMetal"]
if len(matches) > 1:
    raise RuntimeError("Duplicate Smith Metal catalog entries")
if not matches:
    items.append(metal)
    catalog.set_editor_property("items", items)
    if not assets.save_loaded_asset(catalog, only_if_is_dirty=False):
        raise RuntimeError("Could not save item catalog")

ranks = balance.get_editor_property("skill_ranks")
ranks.set_editor_property("max_rank", 4)
balance.set_editor_property("skill_ranks", ranks)
economy = balance.get_editor_property("camp_economy")
for field, value in (
    ("fine_equipment_soul_price", 130),
    ("smith_metal_soul_price", 12),
    ("smith_metal_purchase_limit", 12),
    ("armor_base_stat_bonus_per_level", 0.04),
    ("craft_metal_cost", 6),
    ("craft_soul_cost", 20),
):
    economy.set_editor_property(field, value)
balance.set_editor_property("camp_economy", economy)
balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v4_M23"))
if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("Could not save combat balance")

unreal.log_warning("AURA_M23_ECONOMY_ASSETS_SUCCESS map_unchanged=true")
