"""Targeted, idempotent M21 DungeonDemo stair and enemy-pack migration.

Keeps the user's floor, wall, door, chest, and prop layout. Only the second
stair's visual/ramp transforms and encounter population data are changed.
Run in the Unreal editor commandlet without -nullrhi after building AuraEditor.
"""

import math
import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
NEW_TAG = unreal.Name("AuraPopulationM21")
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError("M21: DungeonDemo could not be loaded")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors_by_label = {}
for actor in editor.get_all_level_actors():
    actors_by_label.setdefault(actor.get_actor_label(), []).append(actor)


def unique(label, expected_class=None):
    matches = actors_by_label.get(label, [])
    if len(matches) != 1:
        raise RuntimeError(f"M21: expected one {label}, found {len(matches)}")
    actor = matches[0]
    if expected_class and actor.get_class().get_name() != expected_class:
        raise RuntimeError(f"M21: unexpected class for {label}")
    return actor


def approx(a, b, tolerance=2.0):
    return abs(float(a) - float(b)) <= tolerance


def location_matches(actor, x, y, z, tolerance=2.0):
    actual = actor.get_actor_location()
    return (approx(actual.x, x, tolerance)
            and approx(actual.y, y, tolerance)
            and approx(actual.z, z, tolerance))


def asset_class(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"M21: missing enemy asset {path}")
    return asset.generated_class()


goblin = asset_class("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear")
shroom = asset_class("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom")
shaman = asset_class("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman")
encounter_class = unreal.load_class(None, "/Script/Aura.AuraEncounterVolume")
if not encounter_class:
    raise RuntimeError("M21: build AuraEditor before running this migration")

# Fail closed if the authored staircase has moved since the read-only audit.
lower_floor = unique("DungeonFloor_Upper_02", "StaticMeshActor")
upper_floor = unique("DungeonFloor_M20_DeepHall_00", "StaticMeshActor")
stair = unique("DungeonStairs_M20_DeepAscent", "StaticMeshActor")
ramp = unique("DungeonStairRamp_M20_DeepNavigation", "StaticMeshActor")
if not (location_matches(lower_floor, 6600, -900, 420)
        and location_matches(upper_floor, 8400, -900, 850)
        and location_matches(stair, 7800, -300, 420)
        and (location_matches(ramp, 7948.5, 0, 853.2)
             or location_matches(ramp, 8108, 0, 622.4))):
    raise RuntimeError("M21: staircase/floor geometry differs from audited map")
stair_mesh = stair.get_editor_property("static_mesh_component").get_editor_property("static_mesh")
ramp_mesh = ramp.get_editor_property("static_mesh_component").get_editor_property("static_mesh")
if (not stair_mesh or "SM_Stairs" not in stair_mesh.get_path_name()
        or not ramp_mesh or "/Engine/BasicShapes/Cube" not in ramp_mesh.get_path_name()):
    raise RuntimeError("M21: staircase meshes are not the expected authored assets")

original_rooms = (
    "DungeonEncounter_OuterGuard", "DungeonEncounter_EliteShaman",
    "DungeonRewardEncounter_LowerNorth", "DungeonRewardEncounter_UpperSouth",
    "DungeonRewardEncounter_LowerSouthAmbush",
    "DungeonRewardEncounter_UpperNorthRisk",
)
for label in original_rooms:
    unique(label, "AuraEncounterVolume")

new_rooms = (
    "DungeonEncounter_M21_LowerSouthNest",
    "DungeonEncounter_M21_UpperNorthPatrol",
    "DungeonEncounter_M21_OldWingWatch",
)
for label in new_rooms:
    matches = actors_by_label.get(label, [])
    if len(matches) > 1:
        raise RuntimeError(f"M21: duplicate new room {label}")
    if matches and (matches[0].get_class().get_name() != "AuraEncounterVolume"
                    or NEW_TAG not in matches[0].get_editor_property("tags")):
        raise RuntimeError(f"M21: {label} exists but is not an M21 room")

# The north branch tile uses a corner pivot: x=6000 is its outer edge, where a
# wall top wins the floor trace. Move only this newly generated room inward.
north_patrol = actors_by_label.get("DungeonEncounter_M21_UpperNorthPatrol", [])
if north_patrol:
    actor = north_patrol[0]
    if location_matches(actor, 6000, 1450, 600):
        actor.set_actor_location(unreal.Vector(5600, 1450, 600), False, False)
    elif not location_matches(actor, 5600, 1450, 600):
        raise RuntimeError("M21: north patrol was moved by the user")

# The floor-to-floor rise is 430uu, while SM_Stairs was authored for ~600uu.
# Scale only its height. A short hidden ramp begins inside the visible stair
# footprint and overlaps both floors by ~38uu, eliminating the invisible bump
# before the stairs and the gap at the top.
rise = upper_floor.get_actor_location().z - lower_floor.get_actor_location().z
stair.set_actor_scale3d(unreal.Vector(1.0, 1.0, rise / 600.0))
stair.set_actor_enable_collision(False)
stair_component = stair.get_editor_property("static_mesh_component")
stair_component.set_collision_profile_name(unreal.Name("NoCollision"))
stair_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

horizontal_run = 675.0
pitch = math.degrees(math.atan2(rise, horizontal_run))
pitch_radians = math.radians(pitch)
ramp_half_thickness = 15.0
ramp_x = 8100.0 + ramp_half_thickness * math.sin(pitch_radians)
ramp_z = ((lower_floor.get_actor_location().z + upper_floor.get_actor_location().z)
          / 2.0 - ramp_half_thickness * math.cos(pitch_radians))
rotation = unreal.Rotator()
rotation.set_editor_property("pitch", pitch)
ramp.set_actor_location(unreal.Vector(ramp_x, 0.0, ramp_z), False, False)
ramp.set_actor_rotation(rotation, False)
ramp.set_actor_scale3d(
    unreal.Vector(math.hypot(horizontal_run, rise) / 100.0, 6.0, 0.3))
ramp_component = ramp.get_editor_property("static_mesh_component")
ramp_component.set_editor_property("visible", False)
ramp_component.set_editor_property("hidden_in_game", True)
ramp_component.set_editor_property("cast_shadow", False)
ramp_component.set_collision_profile_name(unreal.Name("BlockAll"))
ramp_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
ramp.set_actor_enable_collision(True)


def offset(x, y):
    return unreal.Vector(x, y, 96.0)


def configure(label, title, order, kinds, offsets, standing, cue=True,
              new_location=None, yaw=0.0, encounter_type=None):
    if len(kinds) != len(offsets):
        raise RuntimeError(f"M21: {label} class/offset count mismatch")
    if new_location is not None:
        existing = actors_by_label.get(label, [])
        if existing:
            actor = existing[0]
            if not location_matches(actor, *new_location):
                raise RuntimeError(f"M21: new room {label} was moved by the user")
        else:
            rotation = unreal.Rotator()
            rotation.set_editor_property("yaw", yaw)
            actor = editor.spawn_actor_from_class(
                encounter_class, unreal.Vector(*new_location), rotation)
            if not actor:
                raise RuntimeError(f"M21: cannot spawn {label}")
            actor.set_actor_label(label)
            actor.set_editor_property("tags", [NEW_TAG])
            actors_by_label[label] = [actor]
        rotation = unreal.Rotator()
        rotation.set_editor_property("yaw", yaw)
        actor.set_actor_rotation(rotation, False)
        actor.set_editor_property("encounter_id", unreal.Name(label))
        actor.set_editor_property("trigger_extent", unreal.Vector(360, 650, 180))
        actor.set_editor_property("arming_duration", 1.0)
        actor.set_editor_property("arming_room_entry_depth", 40.0)
        actor.set_editor_property("reward_encounter", False)
        actor.set_editor_property("controlled_gates", [])
        actor.set_editor_property("balance_profile", unreal.AuraEncounterBalanceProfile.CUSTOM)
        actor.set_editor_property("encounter_type", encounter_type or unreal.AuraEncounterType.NORMAL)
    else:
        actor = unique(label, "AuraEncounterVolume")
    actor.set_editor_property("encounter_title", title)
    actor.set_editor_property("encounter_order", order)
    actor.set_editor_property("enemy_class", kinds[0])
    actor.set_editor_property("spawn_enemy_classes", kinds)
    actor.set_editor_property("spawn_offsets", offsets)
    actor.set_editor_property("spawn_count_override", len(offsets))
    actor.set_editor_property("standing_guard_count", standing)
    actor.set_editor_property("cue_reinforcements_on_lock", cue)
    if hasattr(actor, "rerun_construction_scripts"):
        actor.rerun_construction_scripts()
    unreal.log_warning(
        f"AURA_M21_ROOM {label} standing={standing} ground_on_lock="
        f"{len(offsets) - standing if cue else 0} "
        f"classes={[kind.get_name() for kind in kinds]}")
    return actor


# The first corridor introduces one cohesive goblin pack.
configure("DungeonEncounter_OuterGuard", "外层哥布林巡队", 1,
          [goblin] * 4,
          [offset(650, -350), offset(850, 120), offset(1120, 400), offset(1350, -180)],
          standing=2)

# First-floor branches: a guarded mushroom cache and a formerly empty nest.
configure("DungeonRewardEncounter_LowerNorth", "蘑菇宝箱巢", 101,
          [shroom] * 3,
          [offset(620, -280), offset(830, 260), offset(1100, 0)],
          standing=2)
configure("DungeonEncounter_M21_LowerSouthNest", "下层孢子巢穴", 105,
          [shroom] * 3,
          [offset(620, -250), offset(850, 260), offset(1080, 0)],
          standing=2, new_location=(2100, -1450, 0), yaw=-90)

# The middle-floor shaman is present on arrival; three goblins emerge together
# when the combat boundary locks, not after the shaman dies.
configure("DungeonEncounter_EliteShaman", "萨满与哥布林援兵", 2,
          [shaman, goblin, goblin, goblin],
          [offset(1000, 0), offset(700, -260), offset(950, 310), offset(1250, -190)],
          standing=1)
configure("DungeonRewardEncounter_UpperSouth", "南翼萨满守卫", 102,
          [shaman, goblin, goblin],
          [offset(650, 0), offset(900, -300), offset(1100, 250)],
          standing=1)
configure("DungeonEncounter_M21_UpperNorthPatrol", "中层北翼巡逻队", 106,
          [goblin] * 3,
          [offset(620, -270), offset(850, 250), offset(1100, 0)],
          standing=2, new_location=(5600, 1450, 600), yaw=90)
configure("DungeonEncounter_M21_OldWingWatch", "旧守印厅哨队", 107,
          [goblin, goblin, shroom, shroom],
          [offset(620, -240), offset(850, 260), offset(1070, -280), offset(1250, 250)],
          standing=2, new_location=(7500, 1450, 600), yaw=90)

# Deep-floor packs retain the chest-triggered ambush and boss progression.
configure("DungeonRewardEncounter_UpperNorthRisk", "深层混合守卫", 104,
          [shaman, goblin, goblin, shroom],
          [offset(650, 0), offset(850, -310), offset(1000, 280), offset(1170, 0)],
          standing=2)
configure("DungeonRewardEncounter_LowerSouthAmbush", "深层蘑菇伏击", 103,
          [shroom, shroom], [offset(650, -280), offset(1050, 280)],
          standing=2, cue=False)

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError("M21: failed to save DungeonDemo")
unreal.log_warning(
    "AURA_M21_POPULATION_AND_STAIR_SUCCESS rooms=10 new_rooms=3 "
    f"stair_rise={rise:.1f} ramp_pitch={pitch:.2f}")
