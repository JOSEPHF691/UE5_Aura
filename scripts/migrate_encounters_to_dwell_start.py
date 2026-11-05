import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
DWELL_SECONDS = 1.0
ROOM_ENTRY_DEPTH = 40.0

if not unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH):
    raise RuntimeError(f"Could not load {MAP_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
updated = 0
for actor in actor_subsystem.get_all_level_actors():
    if actor.get_class().get_name() != "AuraEncounterVolume":
        continue
    actor.set_editor_property("arming_duration", DWELL_SECONDS)
    actor.set_editor_property("arming_room_entry_depth", ROOM_ENTRY_DEPTH)
    actor.set_editor_property("gather_team_on_lock", False)
    if hasattr(actor, "rerun_construction_scripts"):
        actor.rerun_construction_scripts()
    updated += 1

if updated == 0:
    raise RuntimeError("No AuraEncounterVolume actors found")
if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True):
    raise RuntimeError("Failed to save encounter dwell migration")

unreal.log_warning(
    f"ENCOUNTER_DWELL_MIGRATION_OK actors={updated} dwell={DWELL_SECONDS:.1f} "
    f"entry_depth={ROOM_ENTRY_DEPTH:.1f} gather=False"
)
