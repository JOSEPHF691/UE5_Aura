"""Tune the first goblin encounter without moving any authored map actor.

Run once through UnrealEditor-Cmd after building AuraEditor. The map and balance
asset are backed up by the caller before this script is allowed to save either.
"""

import math
import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
BALANCE_PATH = "/Game/Aura/Data/DA_CombatBalance"
ROOM_LABEL = "DungeonEncounter_OuterGuard"
GOBLIN_PATH = "/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear"


def close(actual, expected):
    return math.isclose(float(actual), expected, abs_tol=0.001)


def transform_values(actor):
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    return (
        location.x, location.y, location.z,
        rotation.pitch, rotation.yaw, rotation.roll,
        scale.x, scale.y, scale.z,
    )


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError("IntroGoblin: DungeonDemo did not load")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = editor.get_all_level_actors()
rooms = [actor for actor in actors if actor.get_actor_label() == ROOM_LABEL]
if len(rooms) != 1 or rooms[0].get_class().get_name() != "AuraEncounterVolume":
    raise RuntimeError(f"IntroGoblin: expected one encounter {ROOM_LABEL}, found {len(rooms)}")
room = rooms[0]
if int(room.get_editor_property("encounter_order")) != 1:
    raise RuntimeError("IntroGoblin: first-room order changed; refusing to edit")
authored_id = str(room.get_editor_property("encounter_id"))
if authored_id not in ("None", "M22.OuterGuard"):
    raise RuntimeError(f"IntroGoblin: unexpected first-room reward id {authored_id}")

goblin_blueprint = unreal.EditorAssetLibrary.load_asset(GOBLIN_PATH)
if not goblin_blueprint:
    raise RuntimeError("IntroGoblin: goblin Blueprint is missing")
goblin_class_path = goblin_blueprint.generated_class().get_path_name()
enemy_class = room.get_editor_property("enemy_class")
classes = list(room.get_editor_property("spawn_enemy_classes"))
offsets = list(room.get_editor_property("spawn_offsets"))
if not enemy_class or enemy_class.get_path_name() != goblin_class_path:
    raise RuntimeError("IntroGoblin: the first-room fallback enemy is not a goblin")
if len(classes) not in (2, 4) or len(offsets) != len(classes):
    raise RuntimeError("IntroGoblin: the first-room pack was changed by the user")
if any(not enemy or enemy.get_path_name() != goblin_class_path for enemy in classes):
    raise RuntimeError("IntroGoblin: the first-room pack is no longer all goblins")
if int(room.get_editor_property("spawn_count_override")) != len(classes):
    raise RuntimeError("IntroGoblin: first-room spawn count and class array disagree")
if int(room.get_editor_property("standing_guard_count")) != 2:
    raise RuntimeError("IntroGoblin: first-room standing guard count changed")

balance = unreal.EditorAssetLibrary.load_asset(BALANCE_PATH)
if not balance:
    raise RuntimeError("IntroGoblin: DA_CombatBalance is missing")
melee = balance.get_editor_property("melee_enemy")
tuning = (
    ("goblin_jab_impact_time", 0.54, 0.65),
    ("goblin_jab_range", 215.0, 180.0),
    ("goblin_jab_attack_interval", 1.15, 1.25),
    ("goblin_jab_step_distance", 145.0, 50.0),
)
for field, old, new in tuning:
    current = melee.get_editor_property(field)
    if not (close(current, old) or close(current, new)):
        raise RuntimeError(f"IntroGoblin: {field} is custom tuned to {current}; refusing overwrite")
if not close(melee.get_editor_property("goblin_jab_half_angle_degrees"), 70.0):
    raise RuntimeError("IntroGoblin: jab half-angle changed; refusing overwrite")
encounters = balance.get_editor_property("encounters")
if int(encounters.get_editor_property("outer_guard_enemy_count")) not in (2, 3):
    raise RuntimeError("IntroGoblin: the outer-guard profile count was custom tuned")

original_transforms = {actor.get_path_name(): transform_values(actor) for actor in actors}
if len(classes) == 4:
    room.set_editor_property("spawn_enemy_classes", classes[:2])
    room.set_editor_property("spawn_offsets", offsets[:2])
room.set_editor_property("spawn_count_override", 2)
room.set_editor_property("standing_guard_count", 2)
room.set_editor_property("cue_reinforcements_on_lock", False)

for field, _, new in tuning:
    melee.set_editor_property(field, new)
balance.set_editor_property("melee_enemy", melee)
encounters.set_editor_property("outer_guard_enemy_count", 2)
balance.set_editor_property("encounters", encounters)

current_actors = editor.get_all_level_actors()
current_transforms = {actor.get_path_name(): transform_values(actor) for actor in current_actors}
if original_transforms != current_transforms:
    raise RuntimeError("IntroGoblin: an actor transform changed; refusing to save the map")
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError("IntroGoblin: failed to save DungeonDemo")
if not unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("IntroGoblin: failed to save DA_CombatBalance")

unreal.log_warning(
    "AURA_INTRO_GOBLIN_TUNED room=OuterGuard enemies=2 standing=2 "
    "jab_range=180 jab_step=50 jab_windup=0.65 jab_interval=1.25 "
    f"transforms_preserved={len(current_transforms)}"
)
