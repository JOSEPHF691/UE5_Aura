import unreal

for legacy_asset in [
    "/Game/Aura/Input/IA_SwitchCamera",
    "/Game/Aura/Input/IMC_ThirdPerson",
]:
    if unreal.EditorAssetLibrary.does_asset_exist(legacy_asset):
        raise RuntimeError(f"Legacy camera asset still exists: {legacy_asset}")

expected_assets = [
    "/Game/Aura/Input/IA_Move",
    "/Game/Aura/Input/IA_Look",
    "/Game/Aura/Input/IA_PrimaryAttack",
    "/Game/Aura/Input/IA_Ability1",
    "/Game/Aura/Input/IA_Ability2",
    "/Game/Aura/Input/IA_Restart",
    "/Game/Aura/Input/IMC_Common",
    "/Game/Aura/Input/IMC_TopDown",
    "/Game/Aura/Blueprints/BP_AuraPlayerController",
    "/Game/Aura/Blueprints/BP_AuraCharacter",
    "/Game/Aura/Blueprints/BP_AuraEnemy",
    "/Game/Aura/Blueprints/BP_AuraGameMode",
]

loaded = {}
for path in expected_assets:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing expected asset: {path}")
    loaded[path] = asset

for path in [
    "/Game/Aura/Blueprints/BP_AuraPlayerController",
    "/Game/Aura/Blueprints/BP_AuraCharacter",
    "/Game/Aura/Blueprints/BP_AuraEnemy",
    "/Game/Aura/Blueprints/BP_AuraGameMode",
]:
    unreal.BlueprintEditorLibrary.compile_blueprint(loaded[path])

move = loaded["/Game/Aura/Input/IA_Move"]
look = loaded["/Game/Aura/Input/IA_Look"]
primary_attack = loaded["/Game/Aura/Input/IA_PrimaryAttack"]
ability1 = loaded["/Game/Aura/Input/IA_Ability1"]
ability2 = loaded["/Game/Aura/Input/IA_Ability2"]
restart = loaded["/Game/Aura/Input/IA_Restart"]
if move.get_editor_property("value_type") != unreal.InputActionValueType.AXIS2D:
    raise RuntimeError("IA_Move is not Axis2D")
if look.get_editor_property("value_type") != unreal.InputActionValueType.AXIS2D:
    raise RuntimeError("IA_Look is not Axis2D")
if primary_attack.get_editor_property("value_type") != unreal.InputActionValueType.BOOLEAN:
    raise RuntimeError("IA_PrimaryAttack is not Boolean")
if ability1.get_editor_property("value_type") != unreal.InputActionValueType.BOOLEAN:
    raise RuntimeError("IA_Ability1 is not Boolean")
if ability2.get_editor_property("value_type") != unreal.InputActionValueType.BOOLEAN:
    raise RuntimeError("IA_Ability2 is not Boolean")
if restart.get_editor_property("value_type") != unreal.InputActionValueType.BOOLEAN:
    raise RuntimeError("IA_Restart is not Boolean")

mapping_expectations = {
    "/Game/Aura/Input/IMC_Common": 4,
    "/Game/Aura/Input/IMC_TopDown": 5,
}
for path, count in mapping_expectations.items():
    mapping_data = loaded[path].get_editor_property("default_key_mappings")
    mappings = mapping_data.get_editor_property("mappings")
    if len(mappings) != count:
        raise RuntimeError(f"{path} expected {count} mappings, found {len(mappings)}")
    for mapping in mappings:
        key = mapping.get_editor_property("key")
        if not str(key.get_editor_property("key_name")):
            raise RuntimeError(f"{path} contains an invalid key")

for path in ["/Game/Aura/Input/IMC_TopDown"]:
    mappings = loaded[path].get_editor_property("default_key_mappings").get_editor_property("mappings")
    if not any(
        mapping.get_editor_property("action") == look
        and str(mapping.get_editor_property("key").get_editor_property("key_name")) == "Mouse2D"
        for mapping in mappings
    ):
        raise RuntimeError(f"{path} does not map Mouse2D to IA_Look")

common_mappings = loaded["/Game/Aura/Input/IMC_Common"].get_editor_property(
    "default_key_mappings"
).get_editor_property("mappings")
if any(
    str(mapping.get_editor_property("key").get_editor_property("key_name")) == "V"
    or (
        mapping.get_editor_property("action")
        and mapping.get_editor_property("action").get_name() == "IA_SwitchCamera"
    )
    for mapping in common_mappings
):
    raise RuntimeError("IMC_Common still contains a camera-switch mapping")
if not any(
    mapping.get_editor_property("action") == ability1
    and str(mapping.get_editor_property("key").get_editor_property("key_name")) == "Q"
    for mapping in common_mappings
):
    raise RuntimeError("IMC_Common does not map Q to IA_Ability1")

if not any(
    mapping.get_editor_property("action") == ability2
    and str(mapping.get_editor_property("key").get_editor_property("key_name")) == "E"
    for mapping in common_mappings
):
    raise RuntimeError("IMC_Common does not map E to IA_Ability2")

controller_bp = loaded["/Game/Aura/Blueprints/BP_AuraPlayerController"]
controller_cdo = unreal.get_default_object(controller_bp.generated_class())
for property_name in [
    "common_mapping_context",
    "top_down_mapping_context",
    "move_action",
    "look_action",
    "primary_attack_action",
    "ability1_action",
    "ability2_action",
    "restart_action",
    "victory_sting",
    "victory_accent_sound",
]:
    if not controller_cdo.get_editor_property(property_name):
        raise RuntimeError(f"Controller property is not assigned: {property_name}")

game_mode_bp = loaded["/Game/Aura/Blueprints/BP_AuraGameMode"]
game_mode_cdo = unreal.get_default_object(game_mode_bp.generated_class())
if not game_mode_cdo.get_editor_property("player_controller_class"):
    raise RuntimeError("GameMode PlayerControllerClass is not assigned")
if not game_mode_cdo.get_editor_property("default_pawn_class"):
    raise RuntimeError("GameMode DefaultPawnClass is not assigned")
hud_class = game_mode_cdo.get_editor_property("hud_class")
if not hud_class or hud_class.get_name() != "AuraHUD":
    raise RuntimeError(f"GameMode is not using AuraHUD: {hud_class}")

character_mesh = unreal.EditorAssetLibrary.load_asset("/Game/Assets/Characters/Aura/SKM_Aura")
weapon_socket = character_mesh.find_socket(unreal.Name("WeaponHandSocket")) if character_mesh else None
if not weapon_socket:
    raise RuntimeError("WeaponHandSocket is missing from SKM_Aura")
if str(weapon_socket.get_editor_property("bone_name")) != "hand_l":
    raise RuntimeError("WeaponHandSocket is not parented to hand_l")
socket_rotation = weapon_socket.get_editor_property("relative_rotation")
expected_socket_rotation = (-88.10, -140.31, 141.51)
actual_socket_rotation = (
    socket_rotation.get_editor_property("pitch"),
    socket_rotation.get_editor_property("yaw"),
    socket_rotation.get_editor_property("roll"),
)
if any(abs(actual - expected) > 0.25 for actual, expected in zip(actual_socket_rotation, expected_socket_rotation)):
    raise RuntimeError(f"WeaponHandSocket rotation is invalid: {socket_rotation}")

character_bp = loaded["/Game/Aura/Blueprints/BP_AuraCharacter"]
character_cdo = unreal.get_default_object(character_bp.generated_class())
camera_expectations = {
    "top_down_camera": (1300.0, -62.0, 90.0, 0.0, True, False),
}
for property_name, values in camera_expectations.items():
    expected_arm, expected_pitch, expected_fov, expected_look_ahead, expected_control, expected_collision = values
    settings = character_cdo.get_editor_property(property_name)
    rotation = settings.get_editor_property("relative_rotation")
    if abs(settings.get_editor_property("target_arm_length") - expected_arm) > 0.1:
        raise RuntimeError(f"{property_name} arm length is invalid: {settings}")
    if abs(rotation.get_editor_property("pitch") - expected_pitch) > 0.1:
        raise RuntimeError(f"{property_name} pitch is invalid: {rotation}")
    if abs(settings.get_editor_property("field_of_view") - expected_fov) > 0.1:
        raise RuntimeError(f"{property_name} field of view is invalid: {settings}")
    if abs(settings.get_editor_property("look_ahead_distance") - expected_look_ahead) > 0.1:
        raise RuntimeError(f"{property_name} look-ahead distance is invalid: {settings}")
    if settings.get_editor_property("use_pawn_control_rotation") != expected_control:
        raise RuntimeError(f"{property_name} control rotation is invalid")
    if settings.get_editor_property("do_collision_test") != expected_collision:
        raise RuntimeError(f"{property_name} collision setting is invalid")
movement_blend_space = character_cdo.get_editor_property("movement_blend_space")
if not movement_blend_space or movement_blend_space.get_class().get_name() != "BlendSpace1D":
    raise RuntimeError("Character MovementBlendSpace is not assigned to IdleWalkRun")
if not character_cdo.get_editor_property("hit_react_animation"):
    raise RuntimeError("Character HitReact animation is not assigned")
mesh_component = character_cdo.get_editor_property("mesh")
mesh_rotation = mesh_component.get_editor_property("relative_rotation")
if abs(mesh_rotation.get_editor_property("pitch")) > 0.1 or abs(mesh_rotation.get_editor_property("yaw") + 90.0) > 0.1:
    raise RuntimeError(f"Character mesh rotation is invalid: {mesh_rotation}")
if mesh_component.get_editor_property("animation_mode") != unreal.AnimationMode.ANIMATION_BLUEPRINT:
    raise RuntimeError("Character mesh is not using Animation Blueprint mode")
anim_class = mesh_component.get_editor_property("anim_class")
if not anim_class or anim_class.get_name() != "AuraAnimInstance":
    raise RuntimeError(f"Character mesh is not using AuraAnimInstance: {anim_class}")

enemy_bp = loaded["/Game/Aura/Blueprints/BP_AuraEnemy"]
enemy_cdo = unreal.get_default_object(enemy_bp.generated_class())
for property_name in [
    "idle_animation",
    "walk_animation",
    "attack_animation",
    "hit_react_animation",
    "death_animation",
    "stunned_animation",
    "stun_effect",
]:
    if not enemy_cdo.get_editor_property(property_name):
        raise RuntimeError(f"Enemy animation is not assigned: {property_name}")
enemy_mesh = enemy_cdo.get_editor_property("mesh")
enemy_mesh_asset = enemy_mesh.get_editor_property("skeletal_mesh_asset")
if not enemy_mesh_asset or enemy_mesh_asset.get_name() != "SKM_Ghoul":
    raise RuntimeError(f"Enemy is not using SKM_Ghoul: {enemy_mesh_asset}")
ai_controller_class = enemy_cdo.get_editor_property("ai_controller_class")
if not ai_controller_class or ai_controller_class.get_name() != "AuraAIController":
    raise RuntimeError(f"Enemy is not using AuraAIController: {ai_controller_class}")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/StartupMap")
if not world:
    raise RuntimeError("Could not load StartupMap for combat validation")
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
demo_enemies = [
    actor
    for actor in actor_subsystem.get_all_level_actors()
    if actor.get_actor_label() == "AuraDemoEnemy"
]
if len(demo_enemies) != 1:
    raise RuntimeError(f"Expected exactly one AuraDemoEnemy, found {len(demo_enemies)}")
if demo_enemies[0].get_class() != enemy_bp.generated_class():
    raise RuntimeError("AuraDemoEnemy is not an instance of BP_AuraEnemy")
nav_bounds = [
    actor
    for actor in actor_subsystem.get_all_level_actors()
    if actor.get_actor_label() == "AuraNavBounds"
]
if len(nav_bounds) != 1 or nav_bounds[0].get_class().get_name() != "NavMeshBoundsVolume":
    raise RuntimeError(f"Expected exactly one AuraNavBounds NavMeshBoundsVolume, found {len(nav_bounds)}")

pickup_class = unreal.load_class(None, "/Script/Aura.AuraPickup")
if not pickup_class:
    raise RuntimeError("Could not load native AuraPickup class")
pickup_expectations = {
    "AuraHealthPickup": unreal.AuraPickupType.HEALTH,
    "AuraManaPickup": unreal.AuraPickupType.MANA,
}
for label, expected_type in pickup_expectations.items():
    matches = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if actor.get_actor_label() == label
    ]
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one {label}, found {len(matches)}")
    pickup = matches[0]
    if pickup.get_class() != pickup_class:
        raise RuntimeError(f"{label} is not an AuraPickup")
    if pickup.get_editor_property("pickup_type") != expected_type:
        raise RuntimeError(f"{label} has the wrong pickup type")

balance = unreal.EditorAssetLibrary.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    raise RuntimeError("Missing DA_CombatBalance")
if abs(balance.get_editor_property("recovery").training_pickup_respawn_delay - 5.0) > 0.01:
    raise RuntimeError("Combat balance does not have a 5 second training pickup respawn")

unreal.log_warning(
    "AURA_VALIDATION_SUCCESS: single God-view camera, input, animation, GameMode, hand_l weapon socket, "
    "FireBolt/ArcaneShards/Shock/restart input, AuraHUD, Ghoul AI/stun presentation, NavMesh bounds, "
    "and health/mana pickups validated"
)
