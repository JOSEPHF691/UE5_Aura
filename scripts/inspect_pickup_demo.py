import unreal


def log(label, value):
    unreal.log_warning(f"AURA_PICKUP_DIAG {label}: {value}")


for path in [
    "/Game/Assets/Pickups/HealthCrystal/SM_HealthCrystal",
    "/Game/Assets/Pickups/Potions/SM_PotionBottle",
]:
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not mesh:
        raise RuntimeError(f"Missing pickup mesh: {path}")
    log("MESH", f"path={path} bounds={mesh.get_bounds()}")
    materials = mesh.get_editor_property("static_materials")
    for index, material in enumerate(materials):
        log(
            "MATERIAL",
            f"path={path} index={index} slot={material.get_editor_property('material_slot_name')} "
            f"material={material.get_editor_property('material_interface')}",
        )

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/StartupMap")
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actor_subsystem.get_all_level_actors():
    if actor.get_class().get_name() == "AuraPickup":
        visual_mesh = actor.get_editor_property("visual_mesh")
        log(
            "ACTOR",
            f"label={actor.get_actor_label()} type={actor.get_editor_property('pickup_type')} "
            f"location={actor.get_actor_location()} mesh={visual_mesh.get_editor_property('static_mesh')} "
            f"mesh_rotation={visual_mesh.get_editor_property('relative_rotation')} "
            f"mesh_scale={visual_mesh.get_editor_property('relative_scale3d')}",
        )

log("COMPLETE", True)

