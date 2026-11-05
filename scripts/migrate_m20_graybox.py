"""Idempotent M20 three-tier graybox migration for DungeonDemo.

Run with UnrealEditor-Cmd -ExecutePythonScript after an AuraEditor build.
Do not pass -nullrhi to this editor-side map migration on the current UE 5.7
installation: it crashes during map load before Python reports an error.
This deliberately keeps the four existing seal IDs, encounter/chest links and
boss-altar rules. LowerSouth and UpperNorth become deep-layer branches; the
old rooms remain traversable optional recovery/dead-end spaces.
"""

import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
TAG = unreal.Name("AuraGeneratedM20")
assets = unreal.EditorAssetLibrary
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError("M20: cannot load DungeonDemo")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
by_label = {}
for entry in editor.get_all_level_actors():
    by_label.setdefault(entry.get_actor_label(), []).append(entry)


def required_asset(path):
    asset = assets.load_asset(path)
    if not asset:
        raise RuntimeError(f"M20: missing asset {path}")
    return asset


def single(label):
    matches = by_label.get(label, [])
    if len(matches) != 1:
        raise RuntimeError(f"M20: expected one {label}, found {len(matches)}")
    return matches[0]


def native_class(name):
    klass = unreal.load_class(None, f"/Script/Aura.{name}")
    if not klass:
        raise RuntimeError(f"M20: missing native class {name}; build AuraEditor first")
    return klass


def rotation(pitch=0.0, yaw=0.0, roll=0.0):
    # Unreal's Python Rotator positional constructor is not pitch/yaw/roll.
    # Set named properties so floor tiles stay horizontal and ramps slope in X.
    result = unreal.Rotator()
    result.set_editor_property("pitch", pitch)
    result.set_editor_property("yaw", yaw)
    result.set_editor_property("roll", roll)
    return result


floor_mesh = required_asset("/Game/Assets/Dungeon/SM_Tile_6x9")
wall_mesh = required_asset("/Game/Assets/Dungeon/SM_Wall_4x2")
stair_mesh = required_asset("/Game/Assets/Dungeon/SM_Stairs")
support_mesh = required_asset("/Game/Assets/Dungeon/SM_Block3x3x1")
beacon_mesh = required_asset("/Game/Assets/Dungeon/Beacon/SM_Beacon")
ramp_mesh = required_asset("/Engine/BasicShapes/Cube")
checkpoint_class = native_class("AuraDungeonCheckpoint")
pickup_class = native_class("AuraPickup")
lore_class = native_class("AuraLoreRecord")

# Strict preflight prevents accidentally editing a different/custom map layout.
for label in (
    "DungeonWall_UpperEast_00", "DungeonWall_UpperEast_01",
    "DungeonWall_UpperEast_02",
):
    if len(by_label.get(label, [])) > 1:
        raise RuntimeError(f"M20: duplicate source wall {label}")
old_walls_present = [label for label in by_label if label.startswith("DungeonWall_UpperEast_")]
if len(old_walls_present) not in (0, 3):
    raise RuntimeError(f"M20: unexpected upper-east wall modules {old_walls_present}")

move_specs = {
    "DungeonRewardEncounter_LowerSouthAmbush": ((2100, -1550, 0), (11100, -1550, 1200)),
    "DungeonLootChest_LowerSouth": ((2100, -2980, 0), (11100, -2980, 1200)),
    "DungeonRitual_LowerSouth": ((2450, -2760, 0), (11450, -2760, 1200)),
    "DungeonGate_LowerSouthAmbush": ((2100, -1250, 225), (11100, -1250, 1425)),
    "DungeonRewardEncounter_UpperNorthRisk": ((5100, 1450, 600), (9900, 1450, 1200)),
    "DungeonLootChest_UpperNorthRisk": ((5100, 2980, 600), (9900, 2980, 1200)),
    "DungeonRitual_UpperNorth": ((5450, 2780, 600), (10250, 2780, 1200)),
    "DungeonGate_UpperNorthRisk": ((5100, 1250, 825), (9900, 1250, 1425)),
    "DungeonEncounter_DemonLord": ((7500, 3150, 600), (12300, 3150, 1200)),
    "DungeonRitual_BossAltar": ((7500, 3200, 600), (12300, 3200, 1200)),
    "DungeonGate_InnerSeal": ((7500, 1100, 825), (12300, 1100, 1425)),
    "DungeonGate_BossSeal": ((7500, 2700, 825), (12300, 2700, 1425)),
    "DungeonPickup_BossPrepHealth": ((7200, 1650, 605), (12000, 1650, 1205)),
    "DungeonPickup_BossPrepMana": ((7800, 1650, 605), (12600, 1650, 1205)),
}


def near(actual, expected, tolerance=3.0):
    return all(abs(float(a) - float(b)) <= tolerance
               for a, b in zip((actual.x, actual.y, actual.z), expected))


for label, (original, desired) in move_specs.items():
    actor = single(label)
    if not near(actor.get_actor_location(), original) and not near(actor.get_actor_location(), desired):
        raise RuntimeError(f"M20: {label} moved by user; review before migration")

nav = single("DungeonNavBounds")
if nav.get_class().get_name() != "NavMeshBoundsVolume":
    raise RuntimeError("M20: unexpected navigation bounds actor")


def ensure(label, klass, location, rotation=unreal.Rotator()):
    matches = by_label.get(label, [])
    if len(matches) > 1:
        raise RuntimeError(f"M20: duplicate actor {label}")
    if matches:
        actor = matches[0]
        if actor.get_class() != klass:
            raise RuntimeError(f"M20: unexpected class for {label}")
    else:
        actor = editor.spawn_actor_from_class(klass, location, rotation)
        if not actor:
            raise RuntimeError(f"M20: failed to spawn {label}")
        actor.set_actor_label(label)
        actor.set_editor_property("tags", [TAG])
        by_label[label] = [actor]
    actor.set_actor_location(location, False, False)
    actor.set_actor_rotation(rotation, False)
    return actor


def static(label, mesh, location, rotation=unreal.Rotator(),
           scale=unreal.Vector(1, 1, 1)):
    actor = ensure(label, unreal.StaticMeshActor.static_class(), location, rotation)
    actor.set_actor_scale3d(scale)
    actor.get_editor_property("static_mesh_component").set_static_mesh(mesh)
    return actor


def wall_run(name, orientation, boundary, start, end, z):
    cursor = float(start)
    index = 0
    while cursor < end - 0.01:
        length = min(800.0, end - cursor)
        scale = unreal.Vector(length / 800.0, 1, 1)
        if orientation == "north":
            loc, rot = unreal.Vector(cursor, boundary - 550, z + 130), unreal.Rotator()
        elif orientation == "south":
            loc, rot = unreal.Vector(cursor + length, boundary + 550, z + 130), rotation(yaw=180)
        elif orientation == "east":
            loc, rot = unreal.Vector(boundary + 550, cursor, z + 130), rotation(yaw=90)
        elif orientation == "west":
            loc, rot = unreal.Vector(boundary - 550, cursor + length, z + 130), rotation(yaw=-90)
        else:
            raise RuntimeError(f"M20: invalid wall orientation {orientation}")
        static(f"DungeonWall_M20_{name}_{index:02d}", wall_mesh, loc, rot, scale)
        cursor += length
        index += 1


# Only remove the exact three source wall modules to open the next staircase.
for label in old_walls_present:
    editor.destroy_actor(single(label))
    del by_label[label]
# The first M20 graybox capped the hall at x=12000. The core extension opens
# its east end and uses a shorter north run so the boss wing has a proper door.
retired_north_wall = "DungeonWall_M20_DeepHallNorthE_01"
if retired_north_wall in by_label:
    editor.destroy_actor(single(retired_north_wall))
    del by_label[retired_north_wall]

# Reuse the supplied kit and the proven visual-stair/hidden-ramp arrangement.
static("DungeonStairs_M20_DeepAscent", stair_mesh,
       unreal.Vector(7800, -300, 600)).set_actor_enable_collision(False)
stair_component = single("DungeonStairs_M20_DeepAscent").get_editor_property(
    "static_mesh_component")
stair_component.set_collision_profile_name(unreal.Name("NoCollision"))
stair_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
ramp = static("DungeonStairRamp_M20_DeepNavigation", ramp_mesh,
              unreal.Vector(7948.5, 0, 853.2), rotation(pitch=36),
              unreal.Vector(11.75, 6.0, 0.3))
ramp_component = ramp.get_editor_property("static_mesh_component")
ramp_component.set_editor_property("visible", False)
ramp_component.set_editor_property("hidden_in_game", True)
ramp_component.set_editor_property("cast_shadow", False)
ramp_component.set_collision_profile_name(unreal.Name("BlockAll"))
ramp_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
for side_index, y in enumerate((-900, 300)):
    for layer_index, dz in enumerate((0, 200, 400)):
        static(f"DungeonStairSupport_M20_{side_index}_{layer_index}", support_mesh,
               unreal.Vector(7800, y, 600 + dz))

for label, x, y, yaw in (
    ("DeepHall_00", 8400, -900, 0),
    ("DeepHall_01", 9600, -900, 0),
    ("DeepHall_02", 10800, -900, 0),
    ("DeepHall_03", 12000, -900, 0),
    ("DeepNorth_00", 10800, 900, 90),
    ("DeepNorth_01", 10800, 2100, 90),
    ("DeepSouth_00", 10200, -900, -90),
    ("DeepSouth_01", 10200, -2100, -90),
    ("DeepCore_00", 13200, 900, 90),
    ("DeepCore_01", 13200, 2100, 90),
    ("DeepCore_02", 13200, 3300, 90),
):
    static(f"DungeonFloor_M20_{label}", floor_mesh,
           unreal.Vector(x, y, 1200), rotation(yaw=yaw))

for spec in (
    ("StairMiddleS", "east", 7800, -900, -350, 600),
    ("StairMiddleN", "east", 7800, 350, 900, 600),
    ("StairDeepS", "west", 8400, -900, -350, 1200),
    ("StairDeepN", "west", 8400, 350, 900, 1200),
    ("DeepHallNorthW", "north", 900, 8400, 9000, 1200),
    ("DeepHallNorthE", "north", 900, 10800, 11400, 1200),
    ("DeepHallSouth", "south", -900, 8400, 10200, 1200),
    ("DeepHallSouthTail", "south", -900, 12000, 13200, 1200),
    ("DeepHallEast", "east", 13200, -900, 900, 1200),
    ("DeepNorthW", "west", 9000, 900, 3300, 1200),
    ("DeepNorthE", "east", 10800, 900, 3300, 1200),
    ("DeepNorthEnd", "north", 3300, 9000, 10800, 1200),
    ("DeepSouthW", "west", 10200, -3300, -900, 1200),
    ("DeepSouthE", "east", 12000, -3300, -900, 1200),
    ("DeepSouthEnd", "south", -3300, 10200, 12000, 1200),
    ("DeepCoreSouthTail", "north", 900, 12600, 13200, 1200),
    ("DeepCoreW", "west", 11400, 900, 4500, 1200),
    ("DeepCoreE", "east", 13200, 900, 4500, 1200),
    ("DeepCoreEnd", "north", 4500, 11400, 13200, 1200),
):
    wall_run(*spec)

# The old lower-south and upper-north rooms remain as optional dead ends.
# Preserve encounter IDs and all cross-actor references while relocating the
# gameplay cluster into the new two-branch deep tier.
for label, (_, desired) in move_specs.items():
    actor = single(label)
    actor.set_actor_location(unreal.Vector(*desired), False, False)
    if hasattr(actor, "rerun_construction_scripts"):
        actor.rerun_construction_scripts()

# M16's Python constructor passed 90 as a positional Rotator argument; in
# Unreal Python that tilts these meshes instead of yawing them. Keep its old
# north wing as a usable optional dead end while fixing the moved cluster.
for index in range(2):
    single(f"DungeonFloor_UpperNorth_{index:02d}").set_actor_rotation(
        rotation(yaw=90), False)
for side, yaw in (("West", -90), ("East", 90)):
    for index in range(3):
        single(f"DungeonWall_M16_UpperNorth{side}_{index:02d}").set_actor_rotation(
            rotation(yaw=yaw), False)
for label in ("DungeonGate_UpperNorthRisk",
              "DungeonLootChest_UpperNorthRisk",
              "DungeonRewardEncounter_UpperNorthRisk"):
    single(label).set_actor_rotation(rotation(yaw=90), False)

checkpoint = ensure("DungeonCheckpoint_M20_Deep", checkpoint_class,
                    unreal.Vector(8900, 0, 1200))
checkpoint.set_editor_property("checkpoint_id", unreal.Name("DeepSanctum"))
checkpoint.set_editor_property("respawn_offset", unreal.Vector(200, 0, 96))
if hasattr(checkpoint, "rerun_construction_scripts"):
    checkpoint.rerun_construction_scripts()
for label, kind, location in (
    ("DungeonPickup_M20_DeepHealth", unreal.AuraPickupType.HEALTH,
     unreal.Vector(9200, -550, 1205)),
    ("DungeonPickup_M20_DeepMana", unreal.AuraPickupType.MANA,
     unreal.Vector(9200, 550, 1205)),
    ("DungeonPickup_M20_OldNorthMana", unreal.AuraPickupType.MANA,
     unreal.Vector(5100, 2950, 605)),
):
    pickup = ensure(label, pickup_class, location)
    pickup.set_editor_property("pickup_type", kind)
    pickup.set_editor_property("respawns_automatically", False)
    if hasattr(pickup, "rerun_construction_scripts"):
        pickup.rerun_construction_scripts()

for label, x, y in (
    ("StairS", 7700, -710), ("StairN", 7700, 710),
    ("DeepNorthW", 9200, 1200), ("DeepNorthE", 10600, 1200),
    ("DeepSouthW", 10400, -1200), ("DeepSouthE", 11800, -1200),
    ("DeepCoreW", 11600, 1500), ("DeepCoreE", 13000, 1500),
):
    static(f"DungeonBeacon_M20_{label}", beacon_mesh,
           unreal.Vector(x, y, 1200 if x >= 8400 else 600))

for label, location, color in (
    ("Hub", unreal.Vector(9000, 0, 1630), unreal.Color(80, 145, 255, 255)),
    ("North", unreal.Vector(9900, 2350, 1630), unreal.Color(185, 80, 255, 255)),
    ("South", unreal.Vector(11100, -2350, 1630), unreal.Color(255, 85, 55, 255)),
    ("CorePrep", unreal.Vector(12300, 1800, 1630), unreal.Color(255, 90, 45, 255)),
    ("CoreBoss", unreal.Vector(12300, 3650, 1700), unreal.Color(255, 40, 22, 255)),
):
    light = ensure(f"DungeonLight_M20_{label}", unreal.PointLight.static_class(), location)
    component = light.get_editor_property("point_light_component")
    component.set_editor_property("intensity", 4800.0)
    component.set_editor_property("attenuation_radius", 1450.0)
    component.set_editor_property("light_color", color)
    component.set_editor_property("cast_shadows", True)

# Optional, rereadable environmental records. They only show text on the
# interacting player's HUD; no seal, inventory or reward state is changed.
for label, record_id, location, title, lines, color in (
    (
        "DungeonLore_M20_Entrance", "EntranceDispatch",
        (500, -580, 0), "守印者委任",
        ("地底裂隙正在腐化旧守印设施。",
         "深入地牢，找出失联的守印者与封印。",
         "先查看外层支路，再向高处推进。"),
        unreal.LinearColor(0.4, 0.75, 1.0, 1.0),
    ),
    (
        "DungeonLore_M20_BrokenSeal", "BrokenSealLedger",
        (2100, -2600, 0), "损坏的封印簿",
        ("封印会拖慢腐化，却无法修复裂隙。",
         "这间旧守卫室已失去封印，只剩补给。",
         "另外三条路仍有守印可寻。"),
        unreal.LinearColor(0.4, 1.0, 0.7, 1.0),
    ),
    (
        "DungeonLore_M20_ReverseRitual", "ReverseRitualNotes",
        (7500, 3650, 600), "逆转仪式残页",
        ("重新封门已经无用；三枚守印可逆转法阵。",
         "仪式会让裂隙源头在核心显形。",
         "从中层大厅东端的楼梯继续深入。"),
        unreal.LinearColor(0.75, 0.5, 1.0, 1.0),
    ),
    (
        "DungeonLore_M20_DeepChoice", "DeepChoiceRecord",
        (8900, 650, 1200), "深层巡守记录",
        ("北侧是精英守卫；南侧是分波伏击。",
         "四处守印只需取三处，第四处可换取更多战利品。",
         "前方核心有祭坛与最终首领。"),
        unreal.LinearColor(0.5, 0.8, 1.0, 1.0),
    ),
    (
        "DungeonLore_M20_CorePrep", "CoreRitualWarning",
        (11700, 2050, 1200), "核心警告石",
        ("仪式并非召唤援军，而是逼出腐化源头。",
         "先在前厅整备，三枚守印齐备后再触碰祭坛。",
         "首领被击败，地牢远征才算结束。"),
        unreal.LinearColor(1.0, 0.46, 0.33, 1.0),
    ),
):
    record = ensure(label, lore_class, unreal.Vector(*location))
    record.set_editor_property("record_id", unreal.Name(record_id))
    record.set_editor_property("record_title", title)
    for property_name, line in zip(("line_one", "line_two", "line_three"), lines):
        record.set_editor_property(property_name, line)
    record.set_editor_property("accent_color", color)
    if hasattr(record, "rerun_construction_scripts"):
        record.rerun_construction_scripts()

nav.set_actor_location(unreal.Vector(6200, 0, 600), False, False)
nav.set_actor_scale3d(unreal.Vector(80, 60, 15))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError("M20: failed to save DungeonDemo")
unreal.log_warning(
    "AURA_M20_GRAYBOX_SUCCESS: three elevations, second staircase, "
    "two deep seal branches, deep boss core, deep checkpoint, five lore records"
)
