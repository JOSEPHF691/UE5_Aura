import unreal


STARTUP_MAP_PATH = "/Game/Map/StartupMap"
PICKUP_CLASS_PATH = "/Script/Aura.AuraPickup"

pickup_specs = {
    "AuraHealthPickup": {
        "type": unreal.AuraPickupType.HEALTH,
        "location": unreal.Vector(220.0, -280.0, 5.0),
        "tag": unreal.Name("AuraHealthPickup"),
    },
    "AuraManaPickup": {
        "type": unreal.AuraPickupType.MANA,
        "location": unreal.Vector(220.0, 280.0, 5.0),
        "tag": unreal.Name("AuraManaPickup"),
    },
}


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    rotation = unreal.Rotator()
    rotation.set_editor_property("pitch", pitch)
    rotation.set_editor_property("yaw", yaw)
    rotation.set_editor_property("roll", roll)
    return rotation


world = unreal.EditorLoadingAndSavingUtils.load_map(STARTUP_MAP_PATH)
if not world:
    raise RuntimeError("Could not load StartupMap")

pickup_class = unreal.load_class(None, PICKUP_CLASS_PATH)
if not pickup_class:
    raise RuntimeError(f"Could not load native pickup class: {PICKUP_CLASS_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors_by_label = {}
for actor in actor_subsystem.get_all_level_actors():
    actors_by_label.setdefault(actor.get_actor_label(), []).append(actor)

for label, spec in pickup_specs.items():
    matches = actors_by_label.get(label, [])
    if len(matches) > 1:
        raise RuntimeError(f"Expected at most one actor labeled {label}, found {len(matches)}")

    pickup = matches[0] if matches else None
    if pickup and pickup.get_class() != pickup_class:
        raise RuntimeError(
            f"Actor labeled {label} has unexpected class {pickup.get_class().get_name()}"
        )

    if not pickup:
        pickup = actor_subsystem.spawn_actor_from_class(
            pickup_class, spec["location"], make_rotator()
        )
        if not pickup:
            raise RuntimeError(f"Failed to place {label}")
        pickup.set_actor_label(label)
    else:
        pickup.set_actor_location(spec["location"], False, False)
        pickup.set_actor_rotation(make_rotator(), False)
	
    pickup.set_editor_property("pickup_type", spec["type"])
    pickup.set_editor_property("tags", [spec["tag"]])
    if hasattr(pickup, "rerun_construction_scripts"):
        pickup.rerun_construction_scripts()

if not unreal.EditorLoadingAndSavingUtils.save_map(world, STARTUP_MAP_PATH):
    raise RuntimeError("Failed to save StartupMap after placing pickups")

unreal.log_warning(
    "AURA_PICKUP_SETUP_SUCCESS: one health pickup and one mana pickup placed with 5s respawn"
)
