"""One-shot, fail-closed M22 map addition: six encounters and one seal site.

Never moves or edits an existing actor. Run with AuraEditor built and the exact
audited DungeonDemo backed up as Backup/DungeonDemo_PreM22RewardNodes_20261010.umap.
"""

import hashlib
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[1]
MAP_FILE = ROOT / "Content/Map/DungeonDemo.umap"
BACKUP = ROOT / "Backup/DungeonDemo_PreM22RewardNodes_20261010.umap"
ORIGINAL_SHA256 = "9D83FD548A4FC5452D8C492B840F339F970E28CE13F20106528C16A608DB8D1F"
MAP_PATH = "/Game/Map/DungeonDemo"
TAG = unreal.Name("AuraM22RewardNodes")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


if not MAP_FILE.is_file() or not BACKUP.is_file():
    raise RuntimeError("M22: audited map or exact pre-migration backup missing")
if digest(MAP_FILE) != ORIGINAL_SHA256 or digest(BACKUP) != ORIGINAL_SHA256:
    raise RuntimeError("M22: map/backup SHA256 differs from read-only audit; refusing write")

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError("M22: DungeonDemo did not load")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
before_actors = editor.get_all_level_actors()
by_label = {}
for old_actor in before_actors:
    by_label.setdefault(old_actor.get_actor_label(), []).append(old_actor)


def unique(label, class_name):
    found = by_label.get(label, [])
    if len(found) != 1 or found[0].get_class().get_name() != class_name:
        raise RuntimeError(f"M22: expected one {label} ({class_name}), found {len(found)}")
    return found[0]


def vector_tuple(value):
    return (round(value.x, 3), round(value.y, 3), round(value.z, 3))


def actor_signature(actor):
    rotation = actor.get_actor_rotation()
    return (actor.get_actor_label(), actor.get_class().get_name(),
            vector_tuple(actor.get_actor_location()),
            (round(rotation.pitch, 3), round(rotation.yaw, 3),
             round(rotation.roll, 3)),
            vector_tuple(actor.get_actor_scale3d()))


old_signatures = {actor.get_path_name(): actor_signature(actor)
                  for actor in before_actors}
if len(old_signatures) != len(before_actors):
    raise RuntimeError("M22: duplicate existing actor paths")

for floor_label, expected in (
    ("DungeonFloor_Lower_02", (2400, -900, 0)),
    ("DungeonFloor_Upper_02", (6600, -900, 420)),
    ("DungeonFloor_M20_DeepHall_00", (8400, -900, 850)),
    ("DungeonFloor_M20_DeepHall_03", (12000, -900, 850)),
    ("DungeonFloor_UpperSouth_01", (4800, -2100, 420)),
):
    floor = unique(floor_label, "StaticMeshActor")
    actual = vector_tuple(floor.get_actor_location())
    if any(abs(actual[i] - expected[i]) > 2 for i in range(3)):
        raise RuntimeError(f"M22: floor moved since audit: {floor_label} {actual}")

upper_south = unique("DungeonRewardEncounter_UpperSouth", "AuraEncounterVolume")
unique("DungeonTreasure_UpperSouthDeadEnd", "AuraTreasureChest")
for label in ("DungeonRitual_LowerNorth", "DungeonRitual_LowerSouth",
              "DungeonRitual_UpperNorth", "DungeonRitual_BossAltar"):
    unique(label, "AuraRitualSite")

specs = (
    # id, order, Chinese title, origin, pack classes, local offsets, prerequisite
    ("M22.OuterPractice", 11, "外层试炼", (2850, 0, 0),
     ("goblin", "shroom", "goblin"),
     ((20, -330), (95, 320), (135, 0)), ("M22.OuterGuard",)),
    ("M22.UpperCrossroads", 12, "中层岔路伏击", (6850, 0, 420),
     ("goblin", "goblin", "shaman"),
     ((80, -310), (160, 300), (250, 0)), ("M22.UpperShaman",)),
    ("M22.MiddleInterlock", 13, "升阶口阻击", (8700, 0, 850),
     ("shroom", "shroom", "goblin"),
     ((100, -320), (230, 300), (350, 0)), ("M22.UpperCrossroads",)),
    ("M22.DeepHallPincer", 14, "深厅夹击", (9800, 0, 850),
     ("goblin", "goblin", "shroom", "shaman"),
     ((100, -360), (180, 340), (300, -80), (360, 220)),
     ("M22.MiddleInterlock",)),
    ("M22.CorePrep", 15, "核心前哨", (10900, 0, 850),
     ("shaman", "goblin", "goblin"),
     ((120, 0), (240, -350), (330, 320)), ("M22.DeepHallPincer",)),
    ("M22.CorePractice", 16, "封印前试炼", (12000, 0, 850),
     ("shaman", "shroom", "goblin"),
     ((110, 0), (250, -340), (380, 330)), ("M22.CorePrep",)),
)
new_labels = [f"DungeonEncounter_M22_{item[0].split('.')[1]}" for item in specs]
new_labels.append("DungeonRitual_UpperSouth")
if any(by_label.get(label) for label in new_labels):
    raise RuntimeError("M22: one or more new actors already exist; refusing partial migration")
for old_label in ("DungeonEncounter_OuterGuard", "DungeonEncounter_EliteShaman",
                  "DungeonEncounter_DemonLord"):
    unique(old_label, "AuraEncounterVolume")

asset_paths = {
    "goblin": "/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear",
    "shroom": "/Game/Aura/Blueprints/Dungeon/BP_AuraShroom",
    "shaman": "/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman",
}
enemy_classes = {}
for key, path in asset_paths.items():
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset or not asset.generated_class():
        raise RuntimeError(f"M22: missing enemy Blueprint {path}")
    enemy_classes[key] = asset.generated_class()
encounter_class = unreal.load_class(None, "/Script/Aura.AuraEncounterVolume")
site_class = unreal.load_class(None, "/Script/Aura.AuraRitualSite")
if not encounter_class or not site_class:
    raise RuntimeError("M22: native classes missing; rebuild AuraEditor")

created = []
for encounter_id, order, title, origin, pack, offsets, prerequisites in specs:
    label = f"DungeonEncounter_M22_{encounter_id.split('.')[1]}"
    room = editor.spawn_actor_from_class(encounter_class, unreal.Vector(*origin),
                                         unreal.Rotator())
    if not room:
        raise RuntimeError(f"M22: could not spawn {label}")
    created.append(room)
    room.set_actor_label(label)
    room.set_editor_property("tags", [TAG])
    room.set_editor_property("encounter_id", unreal.Name(encounter_id))
    room.set_editor_property("encounter_title", title)
    room.set_editor_property("encounter_order", order)
    room.set_editor_property("encounter_type", unreal.AuraEncounterType.NORMAL)
    room.set_editor_property("balance_profile", unreal.AuraEncounterBalanceProfile.CUSTOM)
    room.set_editor_property("reward_encounter", False)
    room.set_editor_property("controlled_gates", [])
    room.set_editor_property("required_cleared_encounter_ids",
                             [unreal.Name(value) for value in prerequisites])
    room.set_editor_property("trigger_extent", unreal.Vector(220, 860, 180))
    room.set_editor_property("arming_duration", 0.6)
    room.set_editor_property("arming_room_entry_depth", 40.0)
    room.set_editor_property("enemy_class", enemy_classes[pack[0]])
    room.set_editor_property("spawn_enemy_classes",
                             [enemy_classes[kind] for kind in pack])
    room.set_editor_property("spawn_offsets",
                             [unreal.Vector(x, y, 96) for x, y in offsets])
    room.set_editor_property("spawn_count_override", len(pack))
    room.set_editor_property("standing_guard_count", 0)
    room.set_editor_property("cue_reinforcements_on_lock", True)
    if hasattr(room, "rerun_construction_scripts"):
        room.rerun_construction_scripts()
    unreal.log_warning(f"AURA_M22_NEW_ROOM {label} id={encounter_id} "
                       f"order={order} at={origin} enemies={pack}")

site = editor.spawn_actor_from_class(site_class,
                                     unreal.Vector(5400, -2980, 420),
                                     unreal.Rotator())
if not site:
    raise RuntimeError("M22: could not spawn UpperSouth seal site")
created.append(site)
site.set_actor_label("DungeonRitual_UpperSouth")
site.set_editor_property("tags", [TAG])
site.set_editor_property("site_type", unreal.AuraRitualSiteType.BRANCH_SEAL)
site.set_editor_property("seal_id", unreal.Name("UpperSouth"))
site.set_editor_property("prerequisite_encounter", upper_south)
if hasattr(site, "rerun_construction_scripts"):
    site.rerun_construction_scripts()

after_actors = editor.get_all_level_actors()
after_by_path = {actor.get_path_name(): actor for actor in after_actors}
if len(after_actors) != len(before_actors) + len(created):
    raise RuntimeError("M22: unexpected actor count before save")
for path, signature in old_signatures.items():
    if path not in after_by_path or actor_signature(after_by_path[path]) != signature:
        raise RuntimeError(f"M22: existing actor changed before save: {path}")

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    raise RuntimeError("M22: map save failed")
unreal.log_warning(
    f"AURA_M22_NODE_MIGRATION_SUCCESS old_actors={len(before_actors)} "
    f"new_actors={len(created)} existing_transforms_unchanged=true "
    f"map_sha256={digest(MAP_FILE)}")
