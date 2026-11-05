import unreal


ASSET_PATH = "/Game/Aura/Data/Loot/DA_LootProfiles"
EXPECTED_EQUIPMENT_IDS = {
    "Equipment.ArcaneStaff",
    "Equipment.WardenArmor",
    "Equipment.SeerHeadpiece",
    "Equipment.WindShoes",
    "Equipment.EmberweaveStaff", "Equipment.StormcallRod",
    "Equipment.BastionRobe", "Equipment.SpellguardMantle",
    "Equipment.AshenCrown", "Equipment.BulwarkCowl",
    "Equipment.FleetstepBoots", "Equipment.GroundingBoots",
}
EXPECTED_RARITIES = [
    unreal.AuraItemRarity.COMMON,
    unreal.AuraItemRarity.QUALITY,
    unreal.AuraItemRarity.FINE,
    unreal.AuraItemRarity.EPIC,
    unreal.AuraItemRarity.LEGENDARY,
]


def fail(message):
    raise RuntimeError(f"AURA_LOOT_VALIDATION_FAILED: {message}")


def close(actual, expected, label, tolerance=0.001):
    if abs(float(actual) - float(expected)) > tolerance:
        fail(f"{label} expected {expected}, got {actual}")


loot_data = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if not loot_data:
    fail(f"missing {ASSET_PATH}")
if loot_data.get_class() != unreal.AuraLootProfileData.static_class():
    fail(f"{ASSET_PATH} has class {loot_data.get_class().get_name()}")
if str(loot_data.get_editor_property("balance_version")) != "DemoLoot_v1_2_M16TwelveBases":
    fail("unexpected balance version")

specs = [
    ("common_enemy", "CommonEnemy", 0, 1, 0.38, 0.25, -1, 0,
     [70.0, 23.0, 6.0, 1.0, 0.0]),
    ("elite_enemy", "EliteEnemy", 1, 1, 0.25, 0.55, 0, 1,
     [35.0, 40.0, 20.0, 5.0, 0.0]),
    ("seal_guardian", "SealGuardian", 1, 1, 0.70, 0.65, 0, 1,
     [15.0, 35.0, 35.0, 13.0, 2.0]),
    ("boss", "Boss", 2, 1, 0.75, 0.80, 1, 2,
     [0.0, 15.0, 40.0, 35.0, 10.0]),
    ("treasure", "Treasure", 1, 1, 0.80, 0.75, 0, 1,
     [10.0, 35.0, 35.0, 17.0, 3.0]),
]

for property_name, profile_id, guaranteed, bonus, bonus_chance, equipment_chance, \
        min_level, max_level, expected_weights in specs:
    profile = loot_data.get_editor_property(property_name)
    label = property_name
    if str(profile.profile_id) != profile_id:
        fail(f"{label}.profile_id expected {profile_id}, got {profile.profile_id}")
    if profile.guaranteed_rolls != guaranteed or profile.bonus_rolls != bonus:
        fail(f"{label} roll counts are incorrect")
    close(profile.bonus_roll_chance, bonus_chance, f"{label}.bonus_roll_chance")
    close(profile.equipment_chance, equipment_chance, f"{label}.equipment_chance")
    close(profile.health_potion_weight, 0.5, f"{label}.health_potion_weight")
    ore_expected = {
        "common_enemy": (0, 0.14),
        "elite_enemy": (1, 0.0),
        "seal_guardian": (0, 0.35),
        "boss": (3, 0.0),
        "treasure": (0, 0.0),
    }[property_name]
    if profile.guaranteed_arcane_ore != ore_expected[0]:
        fail(f"{label} guaranteed ore count is incorrect")
    close(profile.bonus_arcane_ore_chance, ore_expected[1], f"{label}.ore_chance")
    close(profile.scatter_radius, 115.0, f"{label}.scatter_radius")
    close(profile.lifetime_seconds, 120.0, f"{label}.lifetime_seconds")
    if profile.min_item_level_offset != min_level or profile.max_item_level_offset != max_level:
        fail(f"{label} level offsets are incorrect")
    item_ids = {str(item_id) for item_id in profile.equipment_item_ids}
    if item_ids != EXPECTED_EQUIPMENT_IDS:
        fail(f"{label} equipment pool is incorrect: {sorted(item_ids)}")
    if len(profile.rarity_weights) != 5:
        fail(f"{label} must contain five rarity weights")
    for index, expected_weight in enumerate(expected_weights):
        weight = profile.rarity_weights[index]
        close(weight.weight, expected_weight, f"{label}.rarity_weights[{index}]")
        if weight.rarity != EXPECTED_RARITIES[index]:
            fail(f"{label}.rarity_weights[{index}] rarity ordering is incorrect")

close(loot_data.get_editor_property("eligibility_radius"), 6000.0, "eligibility_radius")
if loot_data.get_editor_property("max_active_personal_drops_per_player") != 24:
    fail("max_active_personal_drops_per_player must be 24")

unreal.log_warning(
    "AURA_LOOT_VALIDATION_SUCCESS: DemoLoot_v1_1_Forge profiles and ore budgets passed"
)
