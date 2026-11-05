"""Idempotent M14 layout migration: three guarded branch seals and one boss altar."""

import unreal


assets = unreal.EditorAssetLibrary
world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/DungeonDemo")
if not world:
    raise RuntimeError("Could not load DungeonDemo")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
site_class = unreal.load_class(None, "/Script/Aura.AuraRitualSite")
if not site_class:
    raise RuntimeError("Missing native AuraRitualSite class; build the C++ target first")
by_label = {}
for actor in actors.get_all_level_actors():
    by_label.setdefault(actor.get_actor_label(), []).append(actor)


def single(label):
    matches = by_label.get(label, [])
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one {label}, found {len(matches)}")
    return matches[0]


rooms = {
    "LowerNorth": single("DungeonRewardEncounter_LowerNorth"),
    "LowerSouth": single("DungeonRewardEncounter_LowerSouthAmbush"),
    "UpperSouth": single("DungeonRewardEncounter_UpperSouth"),
}
boss_room = single("DungeonEncounter_DemonLord")
boss_gate = single("DungeonGate_BossSeal")
changed = False

if boss_gate.get_editor_property("starts_open"):
    boss_gate.set_editor_property("starts_open", False)
    changed = True
if not boss_room.get_editor_property("requires_ritual"):
    boss_room.set_editor_property("requires_ritual", True)
    changed = True

specs = (
    ("DungeonRitual_LowerNorth", unreal.Vector(2650.0, 2780.0, 0.0),
     unreal.AuraRitualSiteType.BRANCH_SEAL, "LowerNorth", rooms["LowerNorth"]),
    ("DungeonRitual_LowerSouth", unreal.Vector(2450.0, -2760.0, 0.0),
     unreal.AuraRitualSiteType.BRANCH_SEAL, "LowerSouth", rooms["LowerSouth"]),
    ("DungeonRitual_UpperSouth", unreal.Vector(5350.0, -2760.0, 600.0),
     unreal.AuraRitualSiteType.BRANCH_SEAL, "UpperSouth", rooms["UpperSouth"]),
    ("DungeonRitual_BossAltar", unreal.Vector(7500.0, 3200.0, 600.0),
     unreal.AuraRitualSiteType.BOSS_ALTAR, None, None),
)
for label, location, site_type, seal_id, room in specs:
    matches = by_label.get(label, [])
    if len(matches) > 1:
        raise RuntimeError(f"Duplicate ritual site {label}")
    if matches:
        site = matches[0]
    else:
        site = actors.spawn_actor_from_class(site_class, location, unreal.Rotator())
        if not site:
            raise RuntimeError(f"Could not spawn {label}")
        site.set_actor_label(label)
        site.set_editor_property("tags", [unreal.Name("AuraGeneratedM14")])
        changed = True
    if site.get_class() != site_class:
        raise RuntimeError(f"{label} has unexpected class")
    if site.get_editor_property("site_type") != site_type:
        site.set_editor_property("site_type", site_type)
        changed = True
    desired_id = unreal.Name(seal_id) if seal_id else unreal.Name("None")
    if site.get_editor_property("seal_id") != desired_id:
        site.set_editor_property("seal_id", desired_id)
        changed = True
    desired = {
        "prerequisite_encounter": room,
        "boss_encounter": boss_room if seal_id is None else None,
        "boss_gate": boss_gate if seal_id is None else None,
    }
    for property_name, expected in desired.items():
        if site.get_editor_property(property_name) != expected:
            site.set_editor_property(property_name, expected)
            changed = True
    if hasattr(site, "rerun_construction_scripts"):
        site.rerun_construction_scripts()

if changed:
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, "/Game/Map/DungeonDemo"):
        raise RuntimeError("Could not save M14 ritual sites")
unreal.log_warning("AURA_M14_RITUAL_MIGRATION_SUCCESS")
