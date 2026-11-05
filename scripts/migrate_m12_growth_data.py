"""Narrow M12 migration: preserve all existing catalog entries and map actors."""

import unreal


assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    raise RuntimeError("Missing DA_CombatBalance")
economy = unreal.AuraGrowthEconomyBalance()
for field, value in (
    ("common_kill_souls", 12),
    ("elite_kill_souls", 38),
    ("boss_kill_souls", 125),
    ("elite_room_catch_up_experience", 10),
    ("seal_room_clear_souls", 20),
):
    economy.set_editor_property(field, value)
balance.set_editor_property("growth_economy", economy)
if str(balance.get_editor_property("balance_version")) != "DemoBalanced_v2_0_CampEconomy":
    balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v1_9_PaidGrowth"))
if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("Could not save combat balance")

item_dir = "/Game/Aura/Data/Items"
stone_path = f"{item_dir}/DA_Item_RespecStone"
stone = assets.load_asset(stone_path) if assets.does_asset_exist(stone_path) else None
if not stone:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.AuraItemDefinition)
    stone = asset_tools.create_asset("DA_Item_RespecStone", item_dir,
                                    unreal.AuraItemDefinition, factory)
if not stone or stone.get_class() != unreal.AuraItemDefinition.static_class():
    raise RuntimeError("Could not create the respec stone definition")
stone.set_editor_property("item_id", unreal.Name("Consumable.RespecStone"))
stone.set_editor_property("display_name", "Respec Stone")
stone.set_editor_property("description", "Redistribute skills at the Archmage. Consumed only on confirmation.")
stone.set_editor_property("item_type", unreal.AuraItemType.CONSUMABLE)
stone.set_editor_property("equipment_slot", unreal.AuraEquipmentSlot.NONE)
stone.set_editor_property("default_rarity", unreal.AuraItemRarity.FINE)
stone.set_editor_property("max_stack_size", 3)
stone.set_editor_property("droppable", True)
stone.set_editor_property("consumable_effect", unreal.AuraConsumableEffect.RESPEC)
stone.set_editor_property("restore_amount", 0.0)
stone.set_editor_property("world_mesh", assets.load_asset(
    "/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal"))
stone.set_editor_property("icon", assets.load_asset(
    "/Game/Assets/UI/Spells/ArcaneShards"))
if not assets.save_loaded_asset(stone, only_if_is_dirty=False):
    raise RuntimeError("Could not save respec stone")

catalog = assets.load_asset(f"{item_dir}/DA_ItemCatalog")
if not catalog:
    raise RuntimeError("Missing DA_ItemCatalog")
items = list(catalog.get_editor_property("items"))
if not any("DA_Item_RespecStone" in str(item) for item in items):
    items.append(stone)
    catalog.set_editor_property("items", items)
    if not assets.save_loaded_asset(catalog, only_if_is_dirty=False):
        raise RuntimeError("Could not update DA_ItemCatalog")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/DungeonDemo")
if not world:
    raise RuntimeError("Could not load DungeonDemo")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mage_class = unreal.load_class(None, "/Script/Aura.AuraArchmage")
if not mage_class:
    raise RuntimeError("Missing AuraArchmage class")
existing = [actor for actor in actors.get_all_level_actors()
            if actor.get_actor_label() == "DungeonArchmage_SafeCamp"]
if len(existing) > 1:
    raise RuntimeError("Duplicate DungeonArchmage_SafeCamp actors; resolve manually")
if not existing:
    mage = actors.spawn_actor_from_class(
        mage_class, unreal.Vector(4000.0, -500.0, 600.0), unreal.Rotator())
    if not mage:
        raise RuntimeError("Could not spawn Archmage")
    mage.set_actor_label("DungeonArchmage_SafeCamp")
    mage.set_editor_property("tags", [unreal.Name("AuraGeneratedM12")])
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Map/DungeonDemo"):
        raise RuntimeError("Could not save DungeonDemo with Archmage")

unreal.log_warning("AURA_M12_GROWTH_DATA_SUCCESS")
