import unreal


def describe_asset(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        unreal.log_warning(f"AURA_CHARACTER_DIAG MISSING {path}")
        return None
    unreal.log_warning(
        f"AURA_CHARACTER_DIAG ASSET path={path} class={asset.get_class().get_name()} object={asset}"
    )
    return asset


idle = describe_asset("/Game/Assets/Characters/Aura/Animations/Idle")
walk = describe_asset("/Game/Assets/Characters/Aura/Animations/Walk")
run = describe_asset("/Game/Assets/Characters/Aura/Animations/Run")
movement = describe_asset("/Game/Assets/Characters/Aura/Animations/IdleWalkRun")

if movement:
    for property_name in [
        "skeleton",
        "blend_parameters",
        "sample_data",
        "target_weight_interpolation_speed_per_sec",
    ]:
        try:
            value = movement.get_editor_property(property_name)
            unreal.log_warning(f"AURA_CHARACTER_DIAG MOVEMENT_{property_name.upper()}={value}")
        except Exception as error:
            unreal.log_warning(f"AURA_CHARACTER_DIAG MOVEMENT_{property_name.upper()}_ERROR={error}")
    try:
        for index, parameter in enumerate(movement.get_editor_property("blend_parameters")):
            unreal.log_warning(f"AURA_CHARACTER_DIAG BLEND_PARAMETER_{index}={parameter.export_text()}")
        for index, sample in enumerate(movement.get_editor_property("sample_data")):
            unreal.log_warning(f"AURA_CHARACTER_DIAG BLEND_SAMPLE_{index}={sample.export_text()}")
    except Exception as error:
        unreal.log_warning(f"AURA_CHARACTER_DIAG BLEND_DETAILS_ERROR={error}")

character_mesh = describe_asset("/Game/Assets/Characters/Aura/SKM_Aura")
staff_mesh = describe_asset("/Game/Assets/Characters/Aura/Staff/SKM_Staff")

if character_mesh:
    socket = character_mesh.find_socket(unreal.Name("WeaponHandSocket"))
    unreal.log_warning(f"AURA_CHARACTER_DIAG SOCKET={socket}")
    if socket:
        for property_name in ["bone_name", "relative_location", "relative_rotation", "relative_scale"]:
            unreal.log_warning(
                f"AURA_CHARACTER_DIAG SOCKET_{property_name.upper()}={socket.get_editor_property(property_name)}"
            )

if staff_mesh:
    try:
        unreal.log_warning(f"AURA_CHARACTER_DIAG STAFF_BOUNDS={staff_mesh.get_bounds()}")
    except Exception as error:
        unreal.log_warning(f"AURA_CHARACTER_DIAG STAFF_BOUNDS_ERROR={error}")

character_class = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Aura/Blueprints/BP_AuraCharacter")
if character_class:
    character_cdo = unreal.get_default_object(character_class)
    mesh_component = character_cdo.get_editor_property("mesh")
    weapon_component = character_cdo.get_editor_property("weapon")
    unreal.log_warning(f"AURA_CHARACTER_DIAG CHARACTER_ANIMATION_MODE={mesh_component.get_editor_property('animation_mode')}")
    unreal.log_warning(f"AURA_CHARACTER_DIAG CHARACTER_ANIMATION_DATA={mesh_component.get_editor_property('animation_data')}")
    unreal.log_warning(f"AURA_CHARACTER_DIAG WEAPON_SOCKET_NAME={weapon_component.get_attach_socket_name()}")
    unreal.log_warning(f"AURA_CHARACTER_DIAG WEAPON_RELATIVE_LOCATION={weapon_component.get_editor_property('relative_location')}")
    unreal.log_warning(f"AURA_CHARACTER_DIAG WEAPON_RELATIVE_ROTATION={weapon_component.get_editor_property('relative_rotation')}")

unreal.log_warning("AURA_CHARACTER_DIAG_COMPLETE")
