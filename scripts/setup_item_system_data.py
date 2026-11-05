import unreal


ITEM_DIR = "/Game/Aura/Data/Items"
CATALOG_PATH = f"{ITEM_DIR}/DA_ItemCatalog"

POTION_MESH = "/Game/Assets/Pickups/Potions/SM_PotionBottle"
STAFF_MESH = "/Game/Assets/Characters/Aura/Staff/SM_Staff"
FALLBACK_MESH = "/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal"
HEALTH_ICON = "/Game/Assets/UI/Pickups/T_Potion_Red"
MANA_ICON = "/Game/Assets/UI/Pickups/T_Potion_Blue"
STAFF_ICON = "/Game/Aura/UI/Items/T_Equipment_ArcaneStaff"
ARMOR_ICON = "/Game/Aura/UI/Items/T_Equipment_WardenArmor"
HEAD_ICON = "/Game/Aura/UI/Items/T_Equipment_SeerHeadpiece"
SHOES_ICON = "/Game/Aura/UI/Items/T_Equipment_WindShoes"
USE_SOUND = "/Game/Assets/Sounds/Potions/Consume/sfx_Potion_Consume"

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def ensure_directory(path):
    if not assets.does_directory_exist(path):
        assets.make_directory(path)


def ensure_data_asset(asset_name, asset_class):
    asset_path = f"{ITEM_DIR}/{asset_name}"
    existing = assets.load_asset(asset_path) if assets.does_asset_exist(asset_path) else None
    if existing:
        if existing.get_class() != asset_class.static_class():
            raise RuntimeError(
                f"{asset_path} has class {existing.get_class().get_name()}, "
                f"expected {asset_class.static_class().get_name()}"
            )
        return existing

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    created = tools.create_asset(asset_name, ITEM_DIR, asset_class, factory)
    if not created:
        raise RuntimeError(f"Could not create {asset_path}")
    return created


def gameplay_tag(tag_name):
    # UE 5.7 exposes TagName as read-only through reflected properties. Import
    # text still goes through FGameplayTag's native serializer and validates it.
    tag = unreal.GameplayTag()
    if not tag.import_text(tag_name):
        raise RuntimeError(f"Could not import gameplay tag {tag_name}")
    return tag


def stat_modifier(tag_name, magnitude):
    modifier = unreal.AuraItemStatModifier()
    modifier.set_editor_property("stat_tag", gameplay_tag(tag_name))
    modifier.set_editor_property("magnitude", float(magnitude))
    return modifier


def load_optional(path):
    loaded = assets.load_asset(path)
    if not loaded:
        unreal.log_warning(f"Optional item presentation asset missing: {path}")
    return loaded


ensure_directory(ITEM_DIR)

specs = [
    {
        "asset_name": "DA_Item_HealthPotion",
        "item_id": "Consumable.HealthPotion",
        "display_name": "Health Potion",
        "description": "Restores 35 health. Cannot be consumed at full health.",
        "item_type": unreal.AuraItemType.CONSUMABLE,
        "slot": unreal.AuraEquipmentSlot.NONE,
        "rarity": unreal.AuraItemRarity.COMMON,
        "max_stack": 5,
        "effect": unreal.AuraConsumableEffect.HEALTH,
        "restore": 35.0,
        "modifiers": [],
        "mesh": POTION_MESH,
        "icon": HEALTH_ICON,
    },
    {
        "asset_name": "DA_Item_ManaPotion",
        "item_id": "Consumable.ManaPotion",
        "display_name": "Mana Potion",
        "description": "Restores 20 mana. Cannot be consumed at full mana.",
        "item_type": unreal.AuraItemType.CONSUMABLE,
        "slot": unreal.AuraEquipmentSlot.NONE,
        "rarity": unreal.AuraItemRarity.COMMON,
        "max_stack": 5,
        "effect": unreal.AuraConsumableEffect.MANA,
        "restore": 20.0,
        "modifiers": [],
        "mesh": POTION_MESH,
        "icon": MANA_ICON,
    },
    {
        "asset_name": "DA_Item_ArcaneStaff",
        "item_id": "Equipment.ArcaneStaff",
        "display_name": "Arcane Staff",
        "description": "A stat-only weapon. The current left-hand staff model is preserved.",
        "item_type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.WEAPON,
        "rarity": unreal.AuraItemRarity.COMMON,
        "max_stack": 1,
        "effect": unreal.AuraConsumableEffect.NONE,
        "restore": 0.0,
        "modifiers": [stat_modifier("Item.Stat.Intelligence", 4.0)],
        "mesh": STAFF_MESH,
        "icon": STAFF_ICON,
    },
    {
        "asset_name": "DA_Item_WardenArmor",
        "item_id": "Equipment.WardenArmor",
        "display_name": "Warden Armor",
        "description": "Stat-only armor: maximum health and armor.",
        "item_type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.ARMOR,
        "rarity": unreal.AuraItemRarity.COMMON,
        "max_stack": 1,
        "effect": unreal.AuraConsumableEffect.NONE,
        "restore": 0.0,
        "modifiers": [
            stat_modifier("Item.Stat.MaxHealth", 20.0),
            stat_modifier("Item.Stat.Armor", 3.0),
        ],
        "mesh": FALLBACK_MESH,
        "icon": ARMOR_ICON,
    },
    {
        "asset_name": "DA_Item_SeerHeadpiece",
        "item_id": "Equipment.SeerHeadpiece",
        "display_name": "Seer Headpiece",
        "description": "Stat-only headpiece: maximum mana and intelligence.",
        "item_type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.HEAD,
        "rarity": unreal.AuraItemRarity.COMMON,
        "max_stack": 1,
        "effect": unreal.AuraConsumableEffect.NONE,
        "restore": 0.0,
        "modifiers": [
            stat_modifier("Item.Stat.MaxMana", 10.0),
            stat_modifier("Item.Stat.Intelligence", 3.0),
        ],
        "mesh": FALLBACK_MESH,
        "icon": HEAD_ICON,
    },
    {
        "asset_name": "DA_Item_WindShoes",
        "item_id": "Equipment.WindShoes",
        "display_name": "Wind Shoes",
        "description": "Stat-only shoes: movement speed and armor.",
        "item_type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.SHOES,
        "rarity": unreal.AuraItemRarity.COMMON,
        "max_stack": 1,
        "effect": unreal.AuraConsumableEffect.NONE,
        "restore": 0.0,
        "modifiers": [
            stat_modifier("Item.Stat.MoveSpeed", 35.0),
            stat_modifier("Item.Stat.Armor", 2.0),
        ],
        "mesh": FALLBACK_MESH,
        "icon": SHOES_ICON,
    },
]

definitions = []
for spec in specs:
    definition = ensure_data_asset(spec["asset_name"], unreal.AuraItemDefinition)
    definition.set_editor_property("item_id", unreal.Name(spec["item_id"]))
    definition.set_editor_property("display_name", spec["display_name"])
    definition.set_editor_property("description", spec["description"])
    definition.set_editor_property("item_type", spec["item_type"])
    definition.set_editor_property("equipment_slot", spec["slot"])
    definition.set_editor_property("default_rarity", spec["rarity"])
    definition.set_editor_property("max_stack_size", spec["max_stack"])
    definition.set_editor_property("droppable", True)
    definition.set_editor_property("consumable_effect", spec["effect"])
    definition.set_editor_property("restore_amount", spec["restore"])
    definition.set_editor_property("base_stat_modifiers", spec["modifiers"])
    definition.set_editor_property("world_mesh", load_optional(spec["mesh"]))
    definition.set_editor_property("world_mesh_scale", 1.0)
    definition.set_editor_property("icon", load_optional(spec["icon"]))
    definition.set_editor_property("use_sound", load_optional(USE_SOUND))
    assets.save_loaded_asset(definition, only_if_is_dirty=False)
    definitions.append(definition)

catalog = ensure_data_asset("DA_ItemCatalog", unreal.AuraItemCatalog)
existing_items = list(catalog.get_editor_property("items"))
known_ids = {str(definition.get_editor_property("item_id")) for definition in definitions}
preserved_items = [item for item in existing_items
                   if item and str(item.get_editor_property("item_id")) not in known_ids]
catalog.set_editor_property("items", definitions + preserved_items)
assets.save_loaded_asset(catalog, only_if_is_dirty=False)

unreal.log_warning(
    "AURA_ITEM_SETUP_SUCCESS: six item definitions and DA_ItemCatalog saved"
)
