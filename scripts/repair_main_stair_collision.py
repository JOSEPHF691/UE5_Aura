"""Repair only the hidden first-floor stair collision ramp, never map art."""

import hashlib
import math
from pathlib import Path

import unreal


root = Path(__file__).resolve().parents[1]
map_file = root / "Content/Map/DungeonDemo.umap"
backup = root / "Backup/DungeonDemo_PreMainStairCollision_20261010.umap"
expected_hash = "C265905857E209121965C880329EB4061573635EFFDF63E62C96FCAFC56815F9"


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


if not map_file.is_file() or not backup.is_file():
    raise RuntimeError("Main stair: exact map and backup required")
if file_hash(map_file) != expected_hash or file_hash(backup) != expected_hash:
    raise RuntimeError("Main stair: map changed since collision audit; refusing edit")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/DungeonDemo")
if not world:
    raise RuntimeError("Main stair: DungeonDemo not loaded")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = editor.get_all_level_actors()


def find(label):
    found = [actor for actor in actors if actor.get_actor_label() == label]
    if len(found) != 1 or found[0].get_class().get_name() != "StaticMeshActor":
        raise RuntimeError(f"Main stair: expected one StaticMeshActor {label}")
    return found[0]


lower_floor = find("DungeonFloor_Lower_02")
upper_floor = find("DungeonFloor_Upper_00")
visible_stair = find("DungeonStairs_MainAscent")
ramp = find("DungeonStairRamp_Navigation")
if abs(lower_floor.get_actor_location().z) > 1.0 or abs(
        upper_floor.get_actor_location().z - 420.0) > 1.0:
    raise RuntimeError("Main stair: authored floor heights changed")
if abs(ramp.get_actor_location().x - 3748.5) > 1.0 or abs(
        ramp.get_actor_location().z - 253.2) > 1.0 or abs(
        ramp.get_actor_rotation().pitch - 36.0) > 1.0:
    raise RuntimeError("Main stair: hidden ramp no longer matches audited geometry")
stair_component = visible_stair.get_editor_property("static_mesh_component")
if stair_component.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION:
    raise RuntimeError("Main stair: visible mesh collision was edited; refusing write")


def signature(actor):
    loc = actor.get_actor_location()
    rot = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    return (actor.get_actor_label(), actor.get_class().get_name(),
            round(loc.x, 3), round(loc.y, 3), round(loc.z, 3),
            round(rot.pitch, 3), round(rot.yaw, 3), round(rot.roll, 3),
            round(scale.x, 3), round(scale.y, 3), round(scale.z, 3))


unchanged = {actor.get_path_name(): signature(actor) for actor in actors if actor != ramp}
if len(unchanged) != len(actors) - 1:
    raise RuntimeError("Main stair: duplicate actor paths")

# Fit the upper face of a hidden 30uu-thick cube to the actual 0 -> 420uu
# walking rise. It overlaps the existing lower approach and upper floor; at
# x=4240 the collision top is level with the upper tile, not 180uu above it.
start_x = 3260.0
end_x = 4240.0
rise = upper_floor.get_actor_location().z - lower_floor.get_actor_location().z
run = end_x - start_x
pitch = math.degrees(math.atan2(rise, run))
angle = math.radians(pitch)
half_thickness = 15.0
ramp_x = (start_x + end_x) / 2.0 + half_thickness * math.sin(angle)
ramp_z = (lower_floor.get_actor_location().z + upper_floor.get_actor_location().z) / 2.0
ramp_z -= half_thickness * math.cos(angle)
rotation = unreal.Rotator()
rotation.set_editor_property("pitch", pitch)
ramp.set_actor_location(unreal.Vector(ramp_x, 0.0, ramp_z), False, False)
ramp.set_actor_rotation(rotation, False)
ramp.set_actor_scale3d(unreal.Vector(math.hypot(run, rise) / 100.0, 6.0, 0.3))

for actor in editor.get_all_level_actors():
    if actor == ramp:
        continue
    path = actor.get_path_name()
    if path not in unchanged or signature(actor) != unchanged[path]:
        raise RuntimeError(f"Main stair: unrelated actor changed: {path}")
if len(editor.get_all_level_actors()) != len(actors):
    raise RuntimeError("Main stair: actor count changed")
if not unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Map/DungeonDemo"):
    raise RuntimeError("Main stair: failed to save map")
unreal.log_warning(
    f"AURA_MAIN_STAIR_REPAIR_SUCCESS unchanged_actors={len(unchanged)} "
    f"rise={rise:.1f} pitch={pitch:.3f} ramp=({ramp_x:.2f},0,{ramp_z:.2f}) "
    f"map_sha256={file_hash(map_file)}")
