"""Promote two authored pack slots without touching the user's geometry.

The caller must first back up DungeonDemo.umap and DA_CombatBalance.uasset.
Run in UnrealEditor-Cmd (without -nullrhi) after building AuraEditor.
"""

import math
import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
BALANCE_PATH = "/Game/Aura/Data/DA_CombatBalance"
ROOM_SPECS = (
    # The optional lower mushroom nest teaches an elite's low-health enrage.
    ("DungeonEncounter_M21_LowerSouthNest", 105, 3, 2, 2,
     "/Game/Aura/Blueprints/Dungeon/BP_AuraShroom"),
    # A visible mid-floor goblin promotes a standing guard, not the first room.
    ("DungeonEncounter_M21_UpperNorthPatrol", 106, 3, 2, 0,
     "/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear"),
)
ELITE_VALUES = {
    "visual_scale": 1.18,
    "max_health_multiplier": 1.6,
    "damage_multiplier": 1.15,
    "walk_speed_multiplier": 1.07,
    "attack_interval_multiplier": 0.92,
    "special_chance_bonus": 0.12,
    "enrage_health_fraction": 0.5,
    "enraged_attack_interval_multiplier": 0.82,
    "enraged_special_chance_bonus": 0.08,
    "kill_experience_reward": 50,
}


def actor_transform(actor):
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    return (location.x, location.y, location.z,
            rotation.pitch, rotation.yaw, rotation.roll,
            scale.x, scale.y, scale.z)


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError("EliteVariant: DungeonDemo did not load")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
initial_transforms = {actor.get_path_name(): actor_transform(actor) for actor in actors}
by_label = {}
for actor in actors:
    by_label.setdefault(actor.get_actor_label(), []).append(actor)

intro = by_label.get("DungeonEncounter_OuterGuard", [])
if len(intro) != 1 or int(intro[0].get_editor_property("encounter_order")) != 1:
    raise RuntimeError("EliteVariant: first-room identity changed")
if list(intro[0].get_editor_property("elite_spawn_indices")):
    raise RuntimeError("EliteVariant: first room must stay non-elite")

promotions = []
for label, order, count, standing, elite_index, expected_blueprint in ROOM_SPECS:
    matches = by_label.get(label, [])
    if len(matches) != 1 or matches[0].get_class().get_name() != "AuraEncounterVolume":
        raise RuntimeError(f"EliteVariant: expected exactly one encounter {label}")
    room = matches[0]
    if int(room.get_editor_property("encounter_order")) != order:
        raise RuntimeError(f"EliteVariant: encounter order changed for {label}")
    classes = list(room.get_editor_property("spawn_enemy_classes"))
    offsets = list(room.get_editor_property("spawn_offsets"))
    if (len(classes) != count or len(offsets) != count
            or int(room.get_editor_property("spawn_count_override")) != count
            or int(room.get_editor_property("standing_guard_count")) != standing):
        raise RuntimeError(f"EliteVariant: authored pack changed for {label}")
    asset = unreal.EditorAssetLibrary.load_asset(expected_blueprint)
    expected_class = asset.generated_class() if asset else None
    if (not expected_class or not classes[elite_index]
            or classes[elite_index].get_path_name() != expected_class.get_path_name()):
        raise RuntimeError(f"EliteVariant: expected monster missing at {label}[{elite_index}]")
    current = list(room.get_editor_property("elite_spawn_indices"))
    if current not in ([], [elite_index]):
        raise RuntimeError(f"EliteVariant: user-authored elites found in {label}: {current}")
    promotions.append((room, elite_index))

balance = unreal.EditorAssetLibrary.load_asset(BALANCE_PATH)
if not balance:
    raise RuntimeError("EliteVariant: DA_CombatBalance is missing")
elite = balance.get_editor_property("elite_enemy")
for field, expected in ELITE_VALUES.items():
    actual = elite.get_editor_property(field)
    if not math.isclose(float(actual), float(expected), abs_tol=0.001):
        raise RuntimeError(f"EliteVariant: custom balance {field}={actual}; refusing overwrite")

for room, index in promotions:
    room.set_editor_property("elite_spawn_indices", [index])
for field, value in ELITE_VALUES.items():
    elite.set_editor_property(field, value)
balance.set_editor_property("elite_enemy", elite)

final_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
final_transforms = {actor.get_path_name(): actor_transform(actor) for actor in final_actors}
if final_transforms != initial_transforms:
    raise RuntimeError("EliteVariant: an actor transform changed; refusing to save")
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError("EliteVariant: failed to save DungeonDemo")
if not unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("EliteVariant: failed to save DA_CombatBalance")
unreal.log_warning(
    "AURA_ELITE_MIGRATION_SUCCESS "
    + " ".join(f"{room.get_actor_label()}={index}" for room, index in promotions)
    + f" actors_preserved={len(final_transforms)}"
)
