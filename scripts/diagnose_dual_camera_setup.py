import unreal


def log(label, value):
    unreal.log_warning(f"AURA_DIAG {label}: {value}")


def safe_property(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception as exc:
        return f"<ERROR {exc}>"


world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/NewMap")
log("WORLD", world)
if world:
    world_settings = world.get_world_settings()
    log("WORLD_SETTINGS_CLASS", world_settings.get_class().get_name())
    log("WORLD_DEFAULT_GAME_MODE", safe_property(world_settings, "default_game_mode"))

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    log("ACTOR_COUNT", len(actors))
    relevant = []
    for actor in actors:
        class_name = actor.get_class().get_name()
        if "PlayerStart" in class_name or "Aura" in class_name or "GameMode" in class_name:
            relevant.append(f"{actor.get_name()}<{class_name}>")
    log("RELEVANT_ACTORS", relevant)

paths = {
    "CONTROLLER_BP": "/Game/Aura/Blueprints/BP_AuraPlayerController",
    "CHARACTER_BP": "/Game/Aura/Blueprints/BP_AuraCharacter",
    "GAME_MODE_BP": "/Game/Aura/Blueprints/BP_AuraGameMode",
}
assets = {name: unreal.EditorAssetLibrary.load_asset(path) for name, path in paths.items()}
for name, asset in assets.items():
    log(name, asset)
    if asset:
        log(f"{name}_PARENT", safe_property(asset, "parent_class"))

controller_bp = assets["CONTROLLER_BP"]
if controller_bp:
    cdo = unreal.get_default_object(controller_bp.generated_class())
    for property_name in [
        "common_mapping_context",
        "top_down_mapping_context",
        "move_action",
        "look_action",
    ]:
        log(f"CONTROLLER_{property_name}", safe_property(cdo, property_name))

character_bp = assets["CHARACTER_BP"]
if character_bp:
    cdo = unreal.get_default_object(character_bp.generated_class())
    log("CHARACTER_CLASS", cdo.get_class().get_name())
    mesh_component = safe_property(cdo, "mesh")
    weapon_component = safe_property(cdo, "weapon")
    log("CHARACTER_MESH_COMPONENT", mesh_component)
    log("CHARACTER_MESH_ASSET", safe_property(mesh_component, "skeletal_mesh_asset") if not isinstance(mesh_component, str) else mesh_component)
    log("CHARACTER_MESH_LOCATION", safe_property(mesh_component, "relative_location") if not isinstance(mesh_component, str) else mesh_component)
    log("CHARACTER_MESH_ROTATION", safe_property(mesh_component, "relative_rotation") if not isinstance(mesh_component, str) else mesh_component)
    log("CHARACTER_ANIMATION_MODE", safe_property(mesh_component, "animation_mode") if not isinstance(mesh_component, str) else mesh_component)
    log("CHARACTER_ANIMATION_DATA", safe_property(mesh_component, "animation_data") if not isinstance(mesh_component, str) else mesh_component)
    if not isinstance(mesh_component, str):
        all_attachment_names = [str(name) for name in mesh_component.get_all_socket_names()]
        log("CHARACTER_HAND_BONES", [name for name in all_attachment_names if "hand" in name.lower() or "weapon" in name.lower() or "staff" in name.lower()])
    log("WEAPON_MESH_ASSET", safe_property(weapon_component, "skeletal_mesh_asset") if not isinstance(weapon_component, str) else weapon_component)
    log("WEAPON_PARENT_SOCKET", safe_property(weapon_component, "attach_socket_name") if not isinstance(weapon_component, str) else weapon_component)
    camera_boom = safe_property(cdo, "camera_boom")
    log("CAMERA_BOOM_CONTROL_ROTATION", safe_property(camera_boom, "use_pawn_control_rotation") if not isinstance(camera_boom, str) else camera_boom)
    log("CAMERA_BOOM_RELATIVE_ROTATION", safe_property(camera_boom, "relative_rotation") if not isinstance(camera_boom, str) else camera_boom)
    log("CAMERA_BOOM_ARM_LENGTH", safe_property(camera_boom, "target_arm_length") if not isinstance(camera_boom, str) else camera_boom)
    log("TOP_DOWN_SETTINGS", safe_property(cdo, "top_down_camera"))
    log("USE_CONTROLLER_ROTATION_YAW", safe_property(cdo, "use_controller_rotation_yaw"))

game_mode_bp = assets["GAME_MODE_BP"]
if game_mode_bp:
    cdo = unreal.get_default_object(game_mode_bp.generated_class())
    log("GAME_MODE_CONTROLLER_CLASS", safe_property(cdo, "player_controller_class"))
    log("GAME_MODE_PAWN_CLASS", safe_property(cdo, "default_pawn_class"))

for context_name in ["IMC_Common", "IMC_TopDown"]:
    context = unreal.EditorAssetLibrary.load_asset(f"/Game/Aura/Input/{context_name}")
    default_key_mappings = safe_property(context, "default_key_mappings")
    log(f"{context_name}_DEFAULT_KEY_MAPPINGS", default_key_mappings)
    if not isinstance(default_key_mappings, str):
        log(f"{context_name}_DEFAULT_KEY_MAPPINGS_DIR", dir(default_key_mappings))
    mappings = safe_property(default_key_mappings, "mappings") if not isinstance(default_key_mappings, str) else default_key_mappings
    log(f"{context_name}_COUNT", len(mappings) if not isinstance(mappings, str) else mappings)
    if not isinstance(mappings, str):
        for index, item in enumerate(mappings):
            action = safe_property(item, "action")
            key = safe_property(item, "key")
            modifiers = safe_property(item, "modifiers")
            log(f"{context_name}_{index}", f"action={action}, key={key}, modifiers={modifiers}")
            if not isinstance(key, str):
                log(f"{context_name}_{index}_KEY_NAME", safe_property(key, "key_name"))
                try:
                    log(f"{context_name}_{index}_KEY_EXPORT", key.export_text())
                except Exception as exc:
                    log(f"{context_name}_{index}_KEY_EXPORT", f"<ERROR {exc}>")

character_mesh = unreal.EditorAssetLibrary.load_asset("/Game/Assets/Characters/Aura/SKM_Aura")
staff_mesh = unreal.EditorAssetLibrary.load_asset("/Game/Assets/Characters/Aura/Staff/SKM_Staff")
idle_animation = unreal.EditorAssetLibrary.load_asset("/Game/Assets/Characters/Aura/Animations/Idle")
log("CHARACTER_SKELETAL_MESH", character_mesh)
log("STAFF_SKELETAL_MESH", staff_mesh)
log("IDLE_ANIMATION", idle_animation)
if idle_animation:
    log("IDLE_CLASS", idle_animation.get_class().get_name())
    log("IDLE_SKELETON", safe_property(idle_animation, "skeleton"))
    log("IDLE_ADDITIVE_TYPE", safe_property(idle_animation, "additive_anim_type"))
if character_mesh:
    log("CHARACTER_MESH_DIR_SOCKET_METHODS", [name for name in dir(character_mesh) if "socket" in name.lower()])
    skeleton = safe_property(character_mesh, "skeleton")
    log("CHARACTER_SKELETON", skeleton)
    if not isinstance(skeleton, str):
        log("SKELETON_DIR_SOCKET_METHODS", [name for name in dir(skeleton) if "socket" in name.lower()])
        log("SKELETON_SOCKETS_PROPERTY", safe_property(skeleton, "sockets"))
    log("CHARACTER_MESH_SOCKET_COUNT", character_mesh.num_sockets())
    for index in range(character_mesh.num_sockets()):
        socket = character_mesh.get_socket_by_index(index)
        log(f"CHARACTER_SOCKET_{index}", socket)

if character_bp:
    cdo = unreal.get_default_object(character_bp.generated_class())
    weapon_component = safe_property(cdo, "weapon")
    if not isinstance(weapon_component, str):
        log("WEAPON_SOCKET_METHODS", [name for name in dir(weapon_component) if "socket" in name.lower() or "attach" in name.lower()])
        try:
            log("WEAPON_GET_ATTACH_SOCKET", weapon_component.get_attach_socket_name())
        except Exception as exc:
            log("WEAPON_GET_ATTACH_SOCKET", f"<ERROR {exc}>")

log("DIAGNOSIS_COMPLETE", True)
