"""Read-only audit of the currently saved DungeonDemo map.

This intentionally never calls save_map, changes actor properties, or runs a
migration. Use without -nullrhi on UE 5.7 when running editor Python.
"""

import collections
import math
import unreal


world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/DungeonDemo")
if not world:
    raise RuntimeError("AUDIT: cannot load DungeonDemo")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
groups = collections.defaultdict(list)
for actor in actors:
    groups[actor.get_class().get_name()].append(actor)


def pos(actor):
    if not actor:
        return "none"
    p = actor.get_actor_location()
    return f"({p.x:.0f},{p.y:.0f},{p.z:.0f})"


def label(actor):
    return actor.get_actor_label() if actor else "none"


def locdistance(a, b):
    first, second = a.get_actor_location(), b.get_actor_location()
    return math.sqrt((first.x - second.x) ** 2
                     + (first.y - second.y) ** 2
                     + (first.z - second.z) ** 2)


floors = [actor for actor in groups["StaticMeshActor"]
          if "Floor" in actor.get_actor_label()
          or "Tile" in actor.get_actor_label()]
unreal.log_warning(
    f"AURA_LAYOUT_AUDIT map_actor_count={len(actors)} "
    f"encounters={len(groups['AuraEncounterVolume'])} "
    f"floors={len(floors)} gates={len(groups['AuraDungeonGate'])} "
    f"treasures={len(groups['AuraTreasureChest'])} "
    f"loot_chests={len(groups['AuraLootChest'])}")
for actor in sorted(floors, key=lambda item: item.get_actor_label()):
    origin, extent = actor.get_actor_bounds(only_colliding_components=True)
    unreal.log_warning(
        f"AURA_LAYOUT_FLOOR {label(actor)} at={pos(actor)} "
        f"bounds=({origin.x:.0f},{origin.y:.0f},{origin.z:.0f}) "
        f"half=({extent.x:.0f},{extent.y:.0f},{extent.z:.0f})")
for group in ("AuraDungeonGate", "AuraTreasureChest", "AuraLootChest",
              "AuraRitualSite", "PlayerStart"):
    for actor in sorted(groups[group], key=lambda item: item.get_actor_label()):
        unreal.log_warning(f"AURA_LAYOUT_{group} {label(actor)} at={pos(actor)}")

for room in sorted(groups["AuraEncounterVolume"],
                   key=lambda actor: actor.get_editor_property("encounter_order")):
    rotation = room.get_actor_rotation()
    extent = room.get_editor_property("trigger_extent")
    trigger = room.get_editor_property("trigger_box")
    trigger_pos = trigger.get_world_location()
    gates = room.get_editor_property("controlled_gates")
    reward = room.get_editor_property("reward_chest")
    loot = room.get_editor_property("loot_chest")
    nearest = sorted(floors, key=lambda item: locdistance(room, item))[:4]
    unreal.log_warning(
        f"AURA_LAYOUT_ROOM {label(room)} id={room.get_editor_property('encounter_id')} "
        f"order={room.get_editor_property('encounter_order')} "
        f"type={room.get_editor_property('encounter_type')} "
        f"profile={room.get_editor_property('balance_profile')} "
        f"wavesize={room.get_editor_property('enemies_per_wave')} "
        f"at={pos(room)} yaw={rotation.yaw:.0f} "
        f"trigger=({trigger_pos.x:.0f},{trigger_pos.y:.0f},{trigger_pos.z:.0f}) "
        f"extent=({extent.x:.0f},{extent.y:.0f},{extent.z:.0f}) "
        f"entry_depth={room.get_editor_property('arming_room_entry_depth'):.0f} "
        f"gates={[f'{label(g)}@{pos(g)}' for g in gates]} "
        f"reward={label(reward)}@{pos(reward)} "
        f"loot={label(loot)}@{pos(loot)} "
        f"nearest_floors={[f'{label(f)}@{pos(f)}' for f in nearest]}")
    transform = room.get_actor_transform()
    for index, offset in enumerate(room.get_editor_property("spawn_offsets")):
        point = transform.transform_location(offset)
        unreal.log_warning(
            f"AURA_LAYOUT_SPAWN {label(room)}[{index}] "
            f"at=({point.x:.0f},{point.y:.0f},{point.z:.0f})")

unreal.log_warning("AURA_LAYOUT_AUDIT_SUCCESS read_only=true")
