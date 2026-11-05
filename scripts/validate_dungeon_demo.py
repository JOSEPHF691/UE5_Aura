import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
GENERATED_TAGS = {"AuraGeneratedDungeon", "AuraGeneratedM16", "AuraGeneratedM20"}


def fail(message):
    raise RuntimeError(f"AURA_DUNGEON_VALIDATION_FAILED: {message}")


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    fail("could not load DungeonDemo")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()
archmages = [actor for actor in actors
             if actor.get_actor_label() == "DungeonArchmage_SafeCamp"]
if len(archmages) != 1:
    fail(f"expected one safe-camp Archmage, found {len(archmages)}")
for label, service in (
    ("DungeonMerchant_SafeCamp", unreal.AuraCampService.MERCHANT),
    ("DungeonBlacksmith_SafeCamp", unreal.AuraCampService.BLACKSMITH),
):
    found = [actor for actor in actors if actor.get_actor_label() == label]
    if len(found) != 1 or found[0].get_class().get_name() != "AuraCampVendor":
        fail(f"expected one {label}, found {len(found)}")
    if found[0].get_editor_property("service_type") != service:
        fail(f"{label} has incorrect service type")
generated = [
    actor
    for actor in actors
    if GENERATED_TAGS.intersection(str(tag) for tag in actor.get_editor_property("tags"))
]

floors = [actor for actor in generated if actor.get_actor_label().startswith("DungeonFloor_")]
walls = [actor for actor in generated if actor.get_actor_label().startswith("DungeonWall_")]
stairs = [actor for actor in generated
          if actor.get_actor_label() in ("DungeonStairs_MainAscent",
                                         "DungeonStairs_M20_DeepAscent")]
stair_ramps = [
    actor for actor in generated
    if actor.get_actor_label() in ("DungeonStairRamp_Navigation",
                                   "DungeonStairRamp_M20_DeepNavigation")
]
stair_supports = [
    actor for actor in generated if actor.get_actor_label().startswith("DungeonStairSupport_")
]
gates = [actor for actor in generated if actor.get_class().get_name() == "AuraDungeonGate"]
encounters = [
    actor for actor in generated if actor.get_class().get_name() == "AuraEncounterVolume"
]
reward_encounters = [
    actor for actor in encounters if actor.get_editor_property("reward_encounter")
]
boon_reward_encounters = [
    actor for actor in reward_encounters if actor.get_editor_property("reward_chest")
]
loot_reward_encounters = [
    actor for actor in reward_encounters if actor.get_editor_property("loot_chest")
]
mandatory_encounters = [
    actor for actor in encounters if not actor.get_editor_property("reward_encounter")
]
checkpoints = [
    actor for actor in generated if actor.get_class().get_name() == "AuraDungeonCheckpoint"
]
pickups = [actor for actor in generated if actor.get_class().get_name() == "AuraPickup"]
lore_records = [actor for actor in generated
                if actor.get_class().get_name() == "AuraLoreRecord"]
treasures = [
    actor for actor in generated if actor.get_class().get_name() == "AuraTreasureChest"
]
loot_chests = [
    actor for actor in generated if actor.get_class().get_name() == "AuraLootChest"
]
if any(str(treasure.get_editor_property("treasure_id")) != treasure.get_actor_label()
       for treasure in treasures):
    fail("treasure caches do not have stable run-upgrade ids")
optional_enemies = [
    actor
    for actor in generated
    if actor.get_actor_label().startswith("DungeonOptionalEnemy_")
]
player_starts = [actor for actor in generated if actor.get_class().get_name() == "PlayerStart"]
nav_bounds = [
    actor for actor in generated if actor.get_class().get_name() == "NavMeshBoundsVolume"
]

expected_counts = {
    "floors": (len(floors), 28),
    "walls": (len(walls), 113),
    "stairs": (len(stairs), 2),
    "stair_ramps": (len(stair_ramps), 2),
    "stair_supports": (len(stair_supports), 12),
    "gates": (len(gates), 5),
    "encounters": (len(encounters), 7),
    "reward_encounters": (len(reward_encounters), 4),
    "boon_reward_encounters": (len(boon_reward_encounters), 2),
    "loot_reward_encounters": (len(loot_reward_encounters), 2),
    "checkpoints": (len(checkpoints), 2),
    "pickups": (len(pickups), 9),
    "lore_records": (len(lore_records), 5),
    "treasures": (len(treasures), 2),
    "loot_chests": (len(loot_chests), 2),
    "optional_enemies": (len(optional_enemies), 0),
    "player_starts": (len(player_starts), 1),
    "nav_bounds": (len(nav_bounds), 1),
}
for label, (actual, expected) in expected_counts.items():
    if actual != expected:
        fail(f"{label} expected={expected} actual={actual}")

floor_mesh_path = str(
    floors[0]
    .get_editor_property("static_mesh_component")
    .get_editor_property("static_mesh")
    .get_path_name()
)
if "/Game/Assets/Dungeon/SM_Tile_6x9" not in floor_mesh_path:
    fail(f"unexpected floor mesh {floor_mesh_path}")

stair_mesh_path = str(
    stairs[0]
    .get_editor_property("static_mesh_component")
    .get_editor_property("static_mesh")
    .get_path_name()
)
if "/Game/Assets/Dungeon/SM_Stairs" not in stair_mesh_path:
    fail(f"unexpected stair mesh {stair_mesh_path}")

floor_heights = sorted({round(actor.get_actor_location().z) for actor in floors})
if floor_heights != [0, 600, 1200]:
    fail(f"expected three floor elevations [0, 600, 1200], actual={floor_heights}")
main_stair = next(actor for actor in stairs
                  if actor.get_actor_label() == "DungeonStairs_MainAscent")
deep_stair = next(actor for actor in stairs
                  if actor.get_actor_label() == "DungeonStairs_M20_DeepAscent")
stair_location = main_stair.get_actor_location()
stair_yaw = main_stair.get_actor_rotation().yaw
if (
    abs(stair_location.x - 3600.0) > 1.0
    or abs(stair_location.y + 300.0) > 1.0
    or abs(stair_yaw) > 1.0
):
    fail(f"unexpected stair position {stairs[0].get_actor_location()}")
for stair, expected_x, expected_z in ((main_stair, 3600, 0),
                                      (deep_stair, 7800, 600)):
    position = stair.get_actor_location()
    if abs(position.x - expected_x) > 1 or abs(position.z - expected_z) > 1:
        fail(f"unexpected staircase placement: {stair.get_actor_label()} {position}")
    if stair.get_actor_enable_collision():
        fail("visual stair collision must be disabled so the hidden ramp owns traversal")
    component = stair.get_editor_property("static_mesh_component")
    if component.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION:
        fail("visual stair component collision must be disabled")

for ramp in stair_ramps:
    ramp_component = ramp.get_editor_property("static_mesh_component")
    ramp_mesh_path = str(ramp_component.get_editor_property("static_mesh").get_path_name())
    if "/Engine/BasicShapes/Cube" not in ramp_mesh_path:
        fail(f"unexpected navigation ramp mesh {ramp_mesh_path}")
    if ramp_component.get_editor_property("visible"):
        fail("navigation ramp must be visually hidden")
    if ramp_component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
        fail("navigation ramp must provide character collision")

branch_floor_counts = {
    "lower_north": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_LowerNorth_")]
    ),
    "lower_south": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_LowerSouth_")]
    ),
    "upper_south": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_UpperSouth_")]
    ),
    "upper_north": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_UpperNorth_")]
    ),
    "deep_north": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_M20_DeepNorth_")]
    ),
    "deep_south": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_M20_DeepSouth_")]
    ),
    "deep_core": len(
        [actor for actor in floors if actor.get_actor_label().startswith("DungeonFloor_M20_DeepCore_")]
    ),
}
if branch_floor_counts != {"lower_north": 2, "lower_south": 2,
                          "upper_south": 2, "upper_north": 2,
                          "deep_north": 2, "deep_south": 2, "deep_core": 3}:
    fail(f"unexpected branch floor layout {branch_floor_counts}")

deep_checkpoint = [actor for actor in checkpoints
                   if actor.get_actor_label() == "DungeonCheckpoint_M20_Deep"]
if (len(deep_checkpoint) != 1
        or str(deep_checkpoint[0].get_editor_property("checkpoint_id")) != "DeepSanctum"
        or abs(deep_checkpoint[0].get_actor_location().z - 1200) > 1):
    fail("M20 deep checkpoint missing or misplaced")
if any(actor.get_actor_label().startswith("DungeonWall_UpperEast_") for actor in actors):
    fail("M20 staircase entry is still blocked by the old upper-east wall")
for label in ("DungeonFloor_Upper_02", "DungeonFloor_M20_DeepHall_00",
              "DungeonFloor_M20_DeepNorth_01", "DungeonFloor_M20_DeepCore_02",
              "DungeonFloor_UpperNorth_00",
              "DungeonStairRamp_M20_DeepNavigation"):
    sample = next((actor for actor in actors if actor.get_actor_label() == label), None)
    if not sample:
        fail(f"M20 navigation geometry missing: {label}")
    origin, extent = sample.get_actor_bounds(
        only_colliding_components=False, include_from_child_actors=False)
    component = sample.get_editor_property("static_mesh_component")
    unreal.log_warning(
        f"AURA_M20_NAV_GEOMETRY Label={label} Location={sample.get_actor_location()} "
        f"Origin={origin} Extent={extent} Mobility={component.get_editor_property('mobility')} "
        f"Collision={component.get_collision_enabled()}"
    )
    if "Floor_" in label and extent.z > 2:
        fail(f"M20 floor tile is not horizontal: {label} extent={extent}")
    if label == "DungeonStairRamp_M20_DeepNavigation":
        pitch = sample.get_actor_rotation().pitch
        if abs(pitch - 36) > 1 or extent.z < 300:
            fail(f"M20 ramp slopes around the wrong axis: pitch={pitch} extent={extent}")

pickup_types = [actor.get_editor_property("pickup_type") for actor in pickups]
if (
    pickup_types.count(unreal.AuraPickupType.HEALTH) != 4
    or pickup_types.count(unreal.AuraPickupType.MANA) != 5
):
    fail(f"expected four health and five mana pickups, actual={pickup_types}")

treasure_locations = sorted(
    (round(actor.get_actor_location().y), round(actor.get_actor_location().z))
    for actor in treasures
)
if treasure_locations != [(-2980, 600), (2980, 0)]:
    fail(f"unexpected treasure dead-end locations {treasure_locations}")

boon_reward_encounters.sort(key=lambda actor: actor.get_editor_property("encounter_order"))
reward_ids = [str(actor.get_editor_property("encounter_id")) for actor in boon_reward_encounters]
reward_spawn_counts = [
    len(actor.get_editor_property("spawn_offsets")) for actor in boon_reward_encounters
]
reward_chest_labels = [
    actor.get_editor_property("reward_chest").get_actor_label()
    if actor.get_editor_property("reward_chest") else None
    for actor in boon_reward_encounters
]
if reward_ids != ["LowerNorthBoonGuardians", "UpperSouthBoonGuardian"]:
    fail(f"unexpected reward encounter ids {reward_ids}")
if reward_spawn_counts != [2, 1]:
    fail(f"unexpected reward guardian counts {reward_spawn_counts}")
if sorted(reward_chest_labels) != sorted(
    ["DungeonTreasure_LowerNorthDeadEnd", "DungeonTreasure_UpperSouthDeadEnd"]
):
    fail(f"reward encounters are not linked one-to-one with treasure caches {reward_chest_labels}")
if any(
    actor.get_editor_property("balance_profile")
    != unreal.AuraEncounterBalanceProfile.CUSTOM
    for actor in boon_reward_encounters
):
    fail("reward encounters must use their authored custom guardian counts")
if any(
    actor.get_editor_property("encounter_type")
    != unreal.AuraEncounterType.GUARDED_TREASURE
    for actor in boon_reward_encounters
):
    fail("run-boon guardian rooms must use GuardedTreasure room type")

ambush = next(actor for actor in loot_reward_encounters
              if actor.get_actor_label() == "DungeonRewardEncounter_LowerSouthAmbush")
ambush_chest = next(actor for actor in loot_chests
                    if actor.get_actor_label() == "DungeonLootChest_LowerSouth")
if (
    ambush.get_editor_property("encounter_type") != unreal.AuraEncounterType.AMBUSH
    or str(ambush.get_editor_property("encounter_id")) != "LowerSouthLootAmbush"
    or len(ambush.get_editor_property("spawn_offsets")) != 2
    or ambush.get_editor_property("enemies_per_wave") != 1
    or abs(ambush.get_editor_property("inter_wave_delay") - 1.0) > 0.01
    or len(ambush.get_editor_property("controlled_gates")) != 1
    or ambush.get_editor_property("loot_chest") != ambush_chest
    or str(ambush_chest.get_editor_property("loot_chest_id")) != "LowerSouthAmbushLoot"
):
    fail("lower-south wave ambush or its personal loot chest is misconfigured")
risk = next(actor for actor in loot_reward_encounters
            if actor.get_actor_label() == "DungeonRewardEncounter_UpperNorthRisk")
risk_chest = next(actor for actor in loot_chests
                  if actor.get_actor_label() == "DungeonLootChest_UpperNorthRisk")
if (str(risk.get_editor_property("encounter_id")) != "UpperNorthRiskGuardians"
        or len(risk.get_editor_property("spawn_offsets")) != 3
        or len(risk.get_editor_property("spawn_enemy_classes")) != 3
        or risk.get_editor_property("loot_chest") != risk_chest
        or str(risk_chest.get_editor_property("loot_chest_id")) != "UpperNorthRiskLoot"
        or risk_chest.get_editor_property("loot_profile") != unreal.AuraLootProfileType.BOSS
        or len(risk.get_editor_property("controlled_gates")) != 1):
    fail("M16 risk wing is not configured as a distinct elite pack and treasure")
if any(abs(actor.get_editor_property("arming_duration") - 1.0) > 0.01 for actor in encounters):
    fail("all combat rooms must require a one-second inside-room dwell")
if any(abs(actor.get_editor_property("arming_room_entry_depth") - 40.0) > 0.01 for actor in encounters):
    fail("all combat rooms must use the doorway-crossing room entry depth")
if any(actor.get_editor_property("gather_team_on_lock") for actor in encounters):
    fail("combat-room entry must not force-teleport teammates")

mandatory_encounters.sort(key=lambda actor: actor.get_editor_property("encounter_order"))
orders = [actor.get_editor_property("encounter_order") for actor in mandatory_encounters]
spawn_counts = [len(actor.get_editor_property("spawn_offsets")) for actor in mandatory_encounters]
balance_profiles = [actor.get_editor_property("balance_profile") for actor in mandatory_encounters]
final_flags = [actor.get_editor_property("final_encounter") for actor in mandatory_encounters]
controlled_gate_counts = [
    len(actor.get_editor_property("controlled_gates")) for actor in mandatory_encounters
]
spawn_scales = [actor.get_editor_property("spawned_enemy_scale") for actor in mandatory_encounters]
if orders != [1, 2, 3]:
    fail(f"unexpected encounter order {orders}")
if spawn_counts != [3, 1, 1]:
    fail(f"unexpected spawn counts {spawn_counts}")
if balance_profiles != [
    unreal.AuraEncounterBalanceProfile.OUTER_GUARD,
    unreal.AuraEncounterBalanceProfile.ELITE_SHAMAN,
    unreal.AuraEncounterBalanceProfile.DEMON_LORD,
]:
    fail(f"unexpected combat balance profiles {balance_profiles}")
if final_flags != [False, False, True]:
    fail(f"unexpected final flags {final_flags}")
boss_room = mandatory_encounters[2]
boss_gate_matches = [actor for actor in gates
                     if actor.get_actor_label() == "DungeonGate_BossSeal"]
if len(boss_gate_matches) != 1:
    fail("M14 boss seal gate is missing or duplicated")
boss_gate = boss_gate_matches[0]
if boss_gate.get_editor_property("starts_open"):
    fail("M14 boss seal gate must begin closed")
if not boss_room.get_editor_property("requires_ritual"):
    fail("boss room must require the three-seal altar ritual")
ritual_specs = (
    ("DungeonRitual_LowerNorth", "LowerNorth", "DungeonRewardEncounter_LowerNorth"),
    ("DungeonRitual_LowerSouth", "LowerSouth", "DungeonRewardEncounter_LowerSouthAmbush"),
    ("DungeonRitual_UpperSouth", "UpperSouth", "DungeonRewardEncounter_UpperSouth"),
    ("DungeonRitual_UpperNorth", "UpperNorth", "DungeonRewardEncounter_UpperNorthRisk"),
)
for label, seal_id, room_label in ritual_specs:
    found = [actor for actor in actors if actor.get_actor_label() == label]
    if len(found) != 1 or found[0].get_class().get_name() != "AuraRitualSite":
        fail(f"expected one M14 ritual site {label}")
    site = found[0]
    prerequisite = site.get_editor_property("prerequisite_encounter")
    if (site.get_editor_property("site_type") != unreal.AuraRitualSiteType.BRANCH_SEAL
            or str(site.get_editor_property("seal_id")) != seal_id
            or not prerequisite or prerequisite.get_actor_label() != room_label):
        fail(f"M14 branch seal {label} is mislinked")
for label, expected in (
    ("DungeonRewardEncounter_LowerSouthAmbush", (11100, -1550, 1200)),
    ("DungeonLootChest_LowerSouth", (11100, -2980, 1200)),
    ("DungeonRitual_LowerSouth", (11450, -2760, 1200)),
    ("DungeonGate_LowerSouthAmbush", (11100, -1250, 1425)),
    ("DungeonRewardEncounter_UpperNorthRisk", (9900, 1450, 1200)),
    ("DungeonLootChest_UpperNorthRisk", (9900, 2980, 1200)),
    ("DungeonRitual_UpperNorth", (10250, 2780, 1200)),
    ("DungeonGate_UpperNorthRisk", (9900, 1250, 1425)),
    ("DungeonEncounter_DemonLord", (12300, 3150, 1200)),
    ("DungeonRitual_BossAltar", (12300, 3200, 1200)),
    ("DungeonGate_InnerSeal", (12300, 1100, 1425)),
    ("DungeonGate_BossSeal", (12300, 2700, 1425)),
    ("DungeonPickup_BossPrepHealth", (12000, 1650, 1205)),
    ("DungeonPickup_BossPrepMana", (12600, 1650, 1205)),
):
    matches = [actor for actor in actors if actor.get_actor_label() == label]
    if len(matches) != 1:
        fail(f"M20 moved actor missing or duplicated: {label}")
    position = matches[0].get_actor_location()
    if any(abs(actual - target) > 2 for actual, target in
           zip((position.x, position.y, position.z), expected)):
        fail(f"M20 moved actor misplaced: {label} at {position}")
lore_expected = {
    "DungeonLore_M20_Entrance": ("EntranceDispatch", (500, -580, 0)),
    "DungeonLore_M20_BrokenSeal": ("BrokenSealLedger", (2100, -2600, 0)),
    "DungeonLore_M20_ReverseRitual": ("ReverseRitualNotes", (7500, 3650, 600)),
    "DungeonLore_M20_DeepChoice": ("DeepChoiceRecord", (8900, 650, 1200)),
    "DungeonLore_M20_CorePrep": ("CoreRitualWarning", (11700, 2050, 1200)),
}
for record in lore_records:
    label = record.get_actor_label()
    if label not in lore_expected:
        fail(f"unexpected M20 lore actor {label}")
    record_id, expected = lore_expected[label]
    position = record.get_actor_location()
    if (str(record.get_editor_property("record_id")) != record_id
            or any(abs(actual - target) > 2 for actual, target in
                   zip((position.x, position.y, position.z), expected))
            or any(not record.get_editor_property(field)
                   for field in ("record_title", "line_one", "line_two", "line_three"))):
        fail(f"M20 lore record misconfigured: {label} at {position}")
if (ambush.get_editor_property("controlled_gates")[0].get_actor_label()
        != "DungeonGate_LowerSouthAmbush"
        or risk.get_editor_property("controlled_gates")[0].get_actor_label()
        != "DungeonGate_UpperNorthRisk"):
    fail("M20 deep encounter gate references were lost")
nav_position = nav_bounds[0].get_actor_location()
nav_scale = nav_bounds[0].get_actor_scale3d()
nav_origin, nav_extent = nav_bounds[0].get_actor_bounds(
    only_colliding_components=False, include_from_child_actors=False)
unreal.log_warning(
    f"AURA_M20_NAV_BOUNDS Origin={nav_origin} Extent={nav_extent} "
    f"MaxX={nav_origin.x + nav_extent.x:.0f} MaxZ={nav_origin.z + nav_extent.z:.0f}"
)
if (abs(nav_position.x - 6200) > 2 or abs(nav_position.z - 600) > 2
        or nav_scale.x < 80 or nav_scale.y < 60 or nav_scale.z < 15):
    fail("M20 navigation bounds do not cover the third tier")
altar_matches = [actor for actor in actors
                 if actor.get_actor_label() == "DungeonRitual_BossAltar"]
if len(altar_matches) != 1 or altar_matches[0].get_class().get_name() != "AuraRitualSite":
    fail("expected one M14 boss altar")
altar = altar_matches[0]
if (altar.get_editor_property("site_type") != unreal.AuraRitualSiteType.BOSS_ALTAR
        or altar.get_editor_property("boss_encounter") != boss_room
        or altar.get_editor_property("boss_gate") != boss_gate):
    fail("M14 boss altar is mislinked")
if controlled_gate_counts != [1, 1, 1]:
    fail(f"unexpected gate links {controlled_gate_counts}")
boss_location = mandatory_encounters[2].get_actor_location()
boss_yaw = mandatory_encounters[2].get_actor_rotation().yaw
if boss_location.y < 3000.0 or abs(boss_location.z - 1200.0) > 1.0 or abs(boss_yaw - 90.0) > 1.0:
    fail(f"boss core is not on the deep 90-degree turn: location={boss_location} yaw={boss_yaw}")
expected_uniform_scales = [1.0, 1.75, 2.5]
for scale, expected in zip(spawn_scales, expected_uniform_scales):
    if any(abs(component - expected) > 0.01 for component in (scale.x, scale.y, scale.z)):
        fail(f"unexpected enemy scale {scale}; expected uniform scale {expected}")

balance = unreal.EditorAssetLibrary.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    fail("missing DA_CombatBalance")
melee_balance = balance.get_editor_property("melee_enemy")
shaman_balance = balance.get_editor_property("shaman")
boss_balance = balance.get_editor_property("boss")
encounter_balance = balance.get_editor_property("encounters")
ambush_spawn_count = len(ambush.get_editor_property("spawn_offsets"))
if len(optional_enemies) + sum(reward_spawn_counts) + ambush_spawn_count \
        + len(risk.get_editor_property("spawn_offsets")) \
        != encounter_balance.optional_branch_enemy_count:
    fail(
        "authored optional enemies do not match combat balance: "
        f"authored={len(optional_enemies) + sum(reward_spawn_counts) + ambush_spawn_count + len(risk.get_editor_property('spawn_offsets'))} "
        f"balance={encounter_balance.optional_branch_enemy_count}"
    )
enemy_expectations = [
    ("/Game/Aura/Blueprints/BP_AuraEnemy", "食尸鬼", melee_balance.max_health, unreal.AuraEnemyCombatStyle.MELEE, unreal.AuraEnemyArchetype.GHOUL, False),
    ("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman", "精英萨满", shaman_balance.max_health, unreal.AuraEnemyCombatStyle.RANGED, unreal.AuraEnemyArchetype.SHAMAN_ELITE, False),
    ("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon", "恶魔领主", boss_balance.max_health, unreal.AuraEnemyCombatStyle.MELEE, unreal.AuraEnemyArchetype.DEMON_LORD, True),
]
for path, expected_name, expected_health, expected_style, expected_archetype, expected_boss in enemy_expectations:
    blueprint = unreal.EditorAssetLibrary.load_asset(path)
    if not blueprint:
        fail(f"missing enemy blueprint {path}")
    cdo = unreal.get_default_object(blueprint.generated_class())
    actual_name = cdo.get_editor_property("enemy_display_name")
    actual_health = cdo.get_editor_property("starting_health")
    actual_style = cdo.get_editor_property("combat_style")
    actual_archetype = cdo.get_editor_property("enemy_archetype")
    actual_boss = cdo.get_editor_property("boss_enemy")
    mesh = cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
    if (
        actual_name != expected_name
        or abs(actual_health - expected_health) > 0.01
        or actual_style != expected_style
        or actual_archetype != expected_archetype
        or actual_boss != expected_boss
        or not mesh
    ):
        fail(
            f"enemy mismatch path={path} name={actual_name} health={actual_health} "
            f"style={actual_style} archetype={actual_archetype} "
            f"boss={actual_boss} mesh={mesh}"
        )

shaman_blueprint = unreal.EditorAssetLibrary.load_asset(
    "/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman"
)
shaman_cdo = unreal.get_default_object(shaman_blueprint.generated_class())
shaman_cast_animation = shaman_cdo.get_editor_property("ranged_cast_animation")
if (
    not shaman_cast_animation
    or "/Game/Assets/Enemies/Shaman/Animations/Shaman_Summon"
    not in str(shaman_cast_animation.get_path_name())
):
    fail(f"unexpected Shaman cast animation {shaman_cast_animation}")

boss_blueprint = unreal.EditorAssetLibrary.load_asset(
    "/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon"
)
boss_cdo = unreal.get_default_object(boss_blueprint.generated_class())
boss_animation_expectations = {
    "boss_attack_left_animation": "/Game/Assets/Enemies/Demon/Animations/Demon_Attack_L",
    "boss_attack_right_animation": "/Game/Assets/Enemies/Demon/Animations/Demon_Attack_R",
    "boss_tail_spin_animation": "/Game/Assets/Enemies/Demon/Animations/Demon_Red_Tailspin",
    "boss_throw_animation": "/Game/Assets/Enemies/Demon/Animations/Demon_Throw",
    "boss_phase_transition_animation": "/Game/Assets/Enemies/Demon/Animations/Demon_Stun",
    "boss_death_animation": "/Game/Aura/Animations/Boss/Demon_Death_Cinematic",
}
for property_name, expected_path in boss_animation_expectations.items():
    animation = boss_cdo.get_editor_property(property_name)
    if not animation or expected_path not in str(animation.get_path_name()):
        fail(f"unexpected Boss animation {property_name}={animation}")
if (
    abs(boss_cdo.get_editor_property("boss_phase_two_health_threshold") - 0.5) > 0.001
    or abs(boss_cdo.get_editor_property("boss_melee_range") - 285.0) > 0.01
    or abs(boss_cdo.get_editor_property("boss_tail_spin_radius") - 380.0) > 0.01
    or abs(boss_cdo.get_editor_property("boss_throw_range") - 1150.0) > 0.01
    or abs(boss_cdo.get_editor_property("boss_death_ragdoll_delay") - 1.45) > 0.01
    or abs(boss_cdo.get_editor_property("boss_death_effect_duration") - 2.8) > 0.01
    or abs(boss_cdo.get_editor_property("boss_death_impulse_strength") - 90.0) > 0.01
    or abs(boss_cdo.get_editor_property("boss_corpse_lifetime") - 10.0) > 0.01
):
    fail("unexpected Boss phase/range/death tuning")

boss_death_effect = boss_cdo.get_editor_property("boss_death_effect")
boss_death_sound = boss_cdo.get_editor_property("boss_death_sound")
boss_death_impact_sound = boss_cdo.get_editor_property("boss_death_impact_sound")
if (
    not boss_death_effect
    or "/Game/Assets/Effects/Fire/NS_Fire" not in str(boss_death_effect.get_path_name())
    or not boss_death_sound
    or "/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_01"
    not in str(boss_death_sound.get_path_name())
    or not boss_death_impact_sound
    or "/Game/Assets/Sounds/Enemies/Goblin/RockHit/SFX_RockHit_Body_03"
    not in str(boss_death_impact_sound.get_path_name())
):
    fail("unexpected Boss death effect/audio configuration")

boss_death_sequence = unreal.EditorAssetLibrary.load_asset(
    "/Game/Aura/Animations/Boss/Demon_Death_Cinematic"
)
if (
    not boss_death_sequence
    or abs(boss_death_sequence.get_editor_property("rate_scale") - 0.9) > 0.001
):
    fail(f"unexpected dedicated Boss death sequence {boss_death_sequence}")

controller_blueprint = unreal.EditorAssetLibrary.load_asset(
    "/Game/Aura/Blueprints/BP_AuraPlayerController"
)
if not controller_blueprint:
    fail("missing Aura player controller blueprint")
controller_cdo = unreal.get_default_object(controller_blueprint.generated_class())
victory_sting = controller_cdo.get_editor_property("victory_sting")
victory_accent = controller_cdo.get_editor_property("victory_accent_sound")
if (
    not victory_sting
    or "/Game/Assets/Sounds/LevelUp/SFX_LevelUp" not in str(victory_sting.get_path_name())
    or not victory_accent
    or "/Game/Assets/Sounds/UI/Unlock_Skills/SFX_UI_Unlock_NewSkill"
    not in str(victory_accent.get_path_name())
    or abs(controller_cdo.get_editor_property("victory_camera_motion_duration") - 3.0) > 0.01
    or abs(controller_cdo.get_editor_property("victory_overlay_delay") - 3.2) > 0.01
):
    fail("unexpected victory camera/audio configuration")

unreal.log_warning(
    "AURA_DUNGEON_VALIDATION_SUCCESS: "
    f"generated={len(generated)} floors={len(floors)} walls={len(walls)} "
    f"branches={branch_floor_counts} stairs={len(stairs)} ramps={len(stair_ramps)} "
    f"supports={len(stair_supports)} elevations={floor_heights} "
    f"gates={len(gates)} mandatory_encounters={len(mandatory_encounters)} "
    f"reward_encounters={len(reward_encounters)} checkpoint={len(checkpoints)} "
    f"optional_enemies={len(optional_enemies)} reward_guardians={reward_spawn_counts} "
    f"ambush_waves={ambush_spawn_count} treasures={len(treasures)} "
    f"loot_chests={len(loot_chests)} pickups={len(pickups)} spawn_counts={spawn_counts}"
)
