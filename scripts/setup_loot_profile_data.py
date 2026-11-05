import unreal


ASSET_DIR = "/Game/Aura/Data/Loot"
ASSET_NAME = "DA_LootProfiles"
ASSET_PATH = f"{ASSET_DIR}/{ASSET_NAME}"
EQUIPMENT_IDS = [
    unreal.Name("Equipment.ArcaneStaff"),
    unreal.Name("Equipment.WardenArmor"),
    unreal.Name("Equipment.SeerHeadpiece"),
    unreal.Name("Equipment.WindShoes"),
]

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def make_struct(struct_type, **values):
    result = struct_type()
    for property_name, value in values.items():
        result.set_editor_property(property_name, value)
    return result


def rarity_weights(common, quality, fine, epic, legendary):
    values = [
        (unreal.AuraItemRarity.COMMON, common),
        (unreal.AuraItemRarity.QUALITY, quality),
        (unreal.AuraItemRarity.FINE, fine),
        (unreal.AuraItemRarity.EPIC, epic),
        (unreal.AuraItemRarity.LEGENDARY, legendary),
    ]
    return [
        make_struct(unreal.AuraLootRarityWeight, rarity=rarity, weight=float(weight))
        for rarity, weight in values
    ]


def profile(profile_id, guaranteed, bonus, bonus_chance, equipment_chance,
            weights, min_level_offset, max_level_offset):
    ore_budget = {
        "CommonEnemy": (0, 0.14),
        "EliteEnemy": (1, 0.0),
        "SealGuardian": (0, 0.35),
        "Boss": (3, 0.0),
        "Treasure": (0, 0.0),
    }[profile_id]
    return make_struct(
        unreal.AuraLootProfile,
        profile_id=unreal.Name(profile_id),
        guaranteed_rolls=guaranteed,
        bonus_rolls=bonus,
        bonus_roll_chance=bonus_chance,
        equipment_chance=equipment_chance,
        health_potion_weight=0.5,
        guaranteed_arcane_ore=ore_budget[0],
        bonus_arcane_ore_chance=ore_budget[1],
        equipment_item_ids=EQUIPMENT_IDS,
        rarity_weights=weights,
        min_item_level_offset=min_level_offset,
        max_item_level_offset=max_level_offset,
        scatter_radius=115.0,
        lifetime_seconds=120.0,
    )


if not assets.does_directory_exist(ASSET_DIR):
    assets.make_directory(ASSET_DIR)

loot_data = assets.load_asset(ASSET_PATH) if assets.does_asset_exist(ASSET_PATH) else None
if loot_data and loot_data.get_class() != unreal.AuraLootProfileData.static_class():
    raise RuntimeError(
        f"{ASSET_PATH} has class {loot_data.get_class().get_name()}, expected AuraLootProfileData"
    )
if not loot_data:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.AuraLootProfileData)
    loot_data = asset_tools.create_asset(
        ASSET_NAME, ASSET_DIR, unreal.AuraLootProfileData, factory
    )
if not loot_data:
    raise RuntimeError(f"Failed to create {ASSET_PATH}")

loot_data.set_editor_property("balance_version", unreal.Name("DemoLoot_v1_1_Forge"))
loot_data.set_editor_property(
    "common_enemy",
    profile(
        "CommonEnemy", 0, 1, 0.38, 0.25,
        rarity_weights(70.0, 23.0, 6.0, 1.0, 0.0), -1, 0,
    ),
)
loot_data.set_editor_property(
    "elite_enemy",
    profile(
        "EliteEnemy", 1, 1, 0.25, 0.55,
        rarity_weights(35.0, 40.0, 20.0, 5.0, 0.0), 0, 1,
    ),
)
loot_data.set_editor_property(
    "seal_guardian",
    profile(
        "SealGuardian", 1, 1, 0.70, 0.65,
        rarity_weights(15.0, 35.0, 35.0, 13.0, 2.0), 0, 1,
    ),
)
loot_data.set_editor_property(
    "boss",
    profile(
        "Boss", 2, 1, 0.75, 0.80,
        rarity_weights(0.0, 15.0, 40.0, 35.0, 10.0), 1, 2,
    ),
)
loot_data.set_editor_property(
    "treasure",
    profile(
        "Treasure", 1, 1, 0.80, 0.75,
        rarity_weights(10.0, 35.0, 35.0, 17.0, 3.0), 0, 1,
    ),
)
loot_data.set_editor_property("eligibility_radius", 6000.0)
loot_data.set_editor_property("max_active_personal_drops_per_player", 24)

if not assets.save_loaded_asset(loot_data, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save {ASSET_PATH}")

unreal.log_warning(
    "AURA_LOOT_SETUP_SUCCESS: DemoLoot_v1_1_Forge personal-loot profiles saved"
)
