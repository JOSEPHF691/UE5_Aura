import unreal

INPUT_PATH = "/Game/Aura/Input"
BLUEPRINT_PATH = "/Game/Aura/Blueprints"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def ensure_directory(path):
    if not editor_assets.does_directory_exist(path):
        editor_assets.make_directory(path)


def create_data_asset(name, path, asset_class):
    full_path = f"{path}/{name}"
    if editor_assets.does_asset_exist(full_path):
        return editor_assets.load_asset(full_path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = asset_tools.create_asset(name, path, asset_class, factory)
    if not asset:
        raise RuntimeError(f"Failed to create {full_path}")
    return asset


def create_blueprint(name, path, parent_class):
    full_path = f"{path}/{name}"
    if editor_assets.does_asset_exist(full_path):
        return editor_assets.load_asset(full_path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = asset_tools.create_asset(name, path, unreal.Blueprint, factory)
    if not blueprint:
        raise RuntimeError(f"Failed to create {full_path}")
    return blueprint


def make_modifier(modifier_class, outer, **properties):
    modifier = unreal.new_object(modifier_class, outer=outer)
    for key, value in properties.items():
        modifier.set_editor_property(key, value)
    return modifier


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    rotation = unreal.Rotator()
    rotation.set_editor_property("pitch", pitch)
    rotation.set_editor_property("yaw", yaw)
    rotation.set_editor_property("roll", roll)
    return rotation


def make_camera_settings(
    arm_length,
    socket_offset,
    pitch,
    field_of_view=90.0,
    look_ahead_distance=0.0,
    use_control_rotation=True,
    enable_lag=True,
    collision_test=False,
):
    settings = unreal.AuraCameraModeSettings()
    settings.set_editor_property("target_arm_length", arm_length)
    settings.set_editor_property("field_of_view", field_of_view)
    settings.set_editor_property("look_ahead_distance", look_ahead_distance)
    settings.set_editor_property("socket_offset", socket_offset)
    settings.set_editor_property("relative_rotation", make_rotator(pitch=pitch))
    settings.set_editor_property("use_pawn_control_rotation", use_control_rotation)
    settings.set_editor_property("enable_camera_lag", enable_lag)
    settings.set_editor_property("do_collision_test", collision_test)
    return settings


def mapping(action, key_name, modifiers=None):
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(key_name))
    return unreal.EnhancedActionKeyMapping(
        action=action,
        key=key,
        modifiers=modifiers or [],
        triggers=[],
    )


def movement_mappings(context, move_action):
    swizzle_w = make_modifier(unreal.InputModifierSwizzleAxis, context, order=unreal.InputAxisSwizzle.YXZ)
    swizzle_s = make_modifier(unreal.InputModifierSwizzleAxis, context, order=unreal.InputAxisSwizzle.YXZ)
    negate_s = make_modifier(unreal.InputModifierNegate, context)
    negate_a = make_modifier(unreal.InputModifierNegate, context)
    return [
        mapping(move_action, "W", [swizzle_w]),
        mapping(move_action, "S", [swizzle_s, negate_s]),
        mapping(move_action, "A", [negate_a]),
        mapping(move_action, "D"),
    ]


def set_context_mappings(context, mappings):
    mapping_data = unreal.InputMappingContextMappingData()
    mapping_data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", mapping_data)
    # UE 5.7 runtime reads DefaultKeyMappings. Keep the deprecated array empty so
    # future loads cannot migrate stale data back over the new representation.
    context.set_editor_property("mappings", [])


ensure_directory(INPUT_PATH)
ensure_directory(BLUEPRINT_PATH)

ia_move = create_data_asset("IA_Move", INPUT_PATH, unreal.InputAction)
ia_move.set_editor_property("value_type", unreal.InputActionValueType.AXIS2D)

ia_look = create_data_asset("IA_Look", INPUT_PATH, unreal.InputAction)
ia_look.set_editor_property("value_type", unreal.InputActionValueType.AXIS2D)

ia_primary_attack = create_data_asset("IA_PrimaryAttack", INPUT_PATH, unreal.InputAction)
ia_primary_attack.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)

ia_ability1 = create_data_asset("IA_Ability1", INPUT_PATH, unreal.InputAction)
ia_ability1.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)

ia_ability2 = create_data_asset("IA_Ability2", INPUT_PATH, unreal.InputAction)
ia_ability2.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)

ia_restart = create_data_asset("IA_Restart", INPUT_PATH, unreal.InputAction)
ia_restart.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)

imc_common = create_data_asset("IMC_Common", INPUT_PATH, unreal.InputMappingContext)
set_context_mappings(
    imc_common,
    [
        mapping(ia_primary_attack, "LeftMouseButton"),
        mapping(ia_ability1, "Q"),
        mapping(ia_ability2, "E"),
        mapping(ia_restart, "R"),
    ],
)

imc_top_down = create_data_asset("IMC_TopDown", INPUT_PATH, unreal.InputMappingContext)
top_down_mappings = movement_mappings(imc_top_down, ia_move)
top_down_mappings.append(mapping(ia_look, "Mouse2D"))
set_context_mappings(imc_top_down, top_down_mappings)

controller_parent = unreal.load_class(None, "/Script/Aura.AuraPlayerController")
character_parent = unreal.load_class(None, "/Script/Aura.AuraCharacter")
game_mode_parent = unreal.load_class(None, "/Script/Engine.GameModeBase")
hud_class = unreal.load_class(None, "/Script/Aura.AuraHUD")
if not controller_parent or not character_parent or not game_mode_parent or not hud_class:
    raise RuntimeError("Could not load one or more native parent classes")

bp_controller = create_blueprint("BP_AuraPlayerController", BLUEPRINT_PATH, controller_parent)
bp_character = create_blueprint("BP_AuraCharacter", BLUEPRINT_PATH, character_parent)
bp_game_mode = create_blueprint("BP_AuraGameMode", BLUEPRINT_PATH, game_mode_parent)

unreal.BlueprintEditorLibrary.compile_blueprint(bp_controller)
unreal.BlueprintEditorLibrary.compile_blueprint(bp_character)
unreal.BlueprintEditorLibrary.compile_blueprint(bp_game_mode)

controller_cdo = unreal.get_default_object(bp_controller.generated_class())
controller_cdo.set_editor_property("common_mapping_context", imc_common)
controller_cdo.set_editor_property("top_down_mapping_context", imc_top_down)
controller_cdo.set_editor_property("move_action", ia_move)
controller_cdo.set_editor_property("look_action", ia_look)
controller_cdo.set_editor_property("primary_attack_action", ia_primary_attack)
controller_cdo.set_editor_property("ability1_action", ia_ability1)
controller_cdo.set_editor_property("ability2_action", ia_ability2)
controller_cdo.set_editor_property("restart_action", ia_restart)
controller_cdo.set_editor_property(
    "victory_sting",
    editor_assets.load_asset("/Game/Assets/Sounds/LevelUp/SFX_LevelUp"),
)
controller_cdo.set_editor_property(
    "victory_accent_sound",
    editor_assets.load_asset(
        "/Game/Assets/Sounds/UI/Unlock_Skills/SFX_UI_Unlock_NewSkill"
    ),
)
controller_cdo.set_editor_property("victory_camera_blend_time", 0.55)
controller_cdo.set_editor_property("victory_camera_distance", 650.0)
controller_cdo.set_editor_property("victory_camera_height", 180.0)
controller_cdo.set_editor_property("victory_camera_motion_duration", 3.0)
controller_cdo.set_editor_property("victory_camera_orbit_degrees", 18.0)
controller_cdo.set_editor_property("victory_camera_fov", 48.0)
controller_cdo.set_editor_property("victory_sting_delay", 1.25)
controller_cdo.set_editor_property("victory_overlay_delay", 3.2)

character_cdo = unreal.get_default_object(bp_character.generated_class())
character_cdo.set_editor_property(
    "top_down_camera",
    make_camera_settings(
        1300.0, unreal.Vector(0.0, 0.0, 120.0), -62.0
    ),
)
character_mesh = editor_assets.load_asset("/Game/Assets/Characters/Aura/SKM_Aura")
staff_mesh = editor_assets.load_asset("/Game/Assets/Characters/Aura/Staff/SKM_Staff")
idle_animation = editor_assets.load_asset("/Game/Assets/Characters/Aura/Animations/Idle")
movement_blend_space = editor_assets.load_asset("/Game/Assets/Characters/Aura/Animations/IdleWalkRun")
hit_react_animation = editor_assets.load_asset("/Game/Assets/Characters/Aura/Animations/Abilities/HitReact")
character_cdo.set_editor_property("movement_blend_space", movement_blend_space)
character_cdo.set_editor_property("hit_react_animation", hit_react_animation)
aura_anim_class = unreal.load_class(None, "/Script/Aura.AuraAnimInstance")
if character_mesh:
    mesh_component = character_cdo.get_editor_property("mesh")
    mesh_component.set_skeletal_mesh_asset(character_mesh)
    mesh_component.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
    mesh_component.set_editor_property("relative_rotation", make_rotator(yaw=-90.0))
    if aura_anim_class:
        mesh_component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
        mesh_component.set_editor_property("anim_class", aura_anim_class)
    elif idle_animation:
        mesh_component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        animation_data = mesh_component.get_editor_property("animation_data")
        animation_data.set_editor_property("anim_to_play", idle_animation)
        animation_data.set_editor_property("saved_looping", True)
        animation_data.set_editor_property("saved_playing", True)
        mesh_component.set_editor_property("animation_data", animation_data)

    socket_created = unreal.AuraEditorAssetLibrary.ensure_skeletal_mesh_socket(
        character_mesh,
        unreal.Name("WeaponHandSocket"),
        unreal.Name("hand_l"),
        unreal.Vector(0.0, 0.0, 0.0),
        make_rotator(pitch=-88.10, yaw=-140.31, roll=141.51),
        unreal.Vector(1.0, 1.0, 1.0),
    )
    if not socket_created:
        raise RuntimeError("Failed to create WeaponHandSocket on hand_l")
    editor_assets.save_loaded_asset(character_mesh, only_if_is_dirty=False)
if staff_mesh:
    weapon_component = character_cdo.get_editor_property("weapon")
    weapon_component.set_skeletal_mesh_asset(staff_mesh)
    weapon_component.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, 0.0))
    weapon_component.set_editor_property("relative_rotation", make_rotator())

game_mode_cdo = unreal.get_default_object(bp_game_mode.generated_class())
game_mode_cdo.set_editor_property("player_controller_class", bp_controller.generated_class())
game_mode_cdo.set_editor_property("default_pawn_class", bp_character.generated_class())
game_mode_cdo.set_editor_property("hud_class", hud_class)

for asset in [
    ia_move,
    ia_look,
    ia_primary_attack,
    ia_ability1,
    ia_ability2,
    ia_restart,
    imc_common,
    imc_top_down,
    bp_controller,
    bp_character,
    bp_game_mode,
]:
    editor_assets.save_loaded_asset(asset, only_if_is_dirty=False)

unreal.log_warning("AURA_SETUP_SUCCESS: single God-view camera, input, Q Arcane Shards, E Shock, Blueprint assets, and native HUD configured")
