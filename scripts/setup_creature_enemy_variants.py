import unreal


BLUEPRINT_DIR = "/Game/Aura/Blueprints/Dungeon"
DUNGEON_MAP = "/Game/Map/DungeonDemo"

assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    rotation = unreal.Rotator()
    rotation.pitch = pitch
    rotation.yaw = yaw
    rotation.roll = roll
    return rotation


def load_required(path):
    asset = assets.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def ensure_blueprint(name):
    path = f"{BLUEPRINT_DIR}/{name}"
    existing = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if existing:
        return existing

    parent = unreal.load_class(None, "/Script/Aura.AuraEnemy")
    if not parent:
        raise RuntimeError("Could not load AuraEnemy native class")
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    blueprint = asset_tools.create_asset(name, BLUEPRINT_DIR, unreal.Blueprint, factory)
    if not blueprint:
        raise RuntimeError(f"Could not create {path}")
    return blueprint


def configure_enemy(
    name,
    mesh_path,
    idle_path,
    walk_path,
    attack_path,
    hit_path,
    death_path,
    stunned_path,
    display_name,
    capsule_radius,
    capsule_half_height,
    attack_archetype,
    mesh_scale=1.0,
    weapon_mesh_path=None,
    weapon_socket=None,
):
    blueprint = ensure_blueprint(name)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    cdo = unreal.get_default_object(blueprint.generated_class())

    cdo.set_editor_property("idle_animation", load_required(idle_path))
    cdo.set_editor_property("walk_animation", load_required(walk_path))
    cdo.set_editor_property("attack_animation", load_required(attack_path))
    cdo.set_editor_property("hit_react_animation", load_required(hit_path))
    cdo.set_editor_property("death_animation", load_required(death_path))
    cdo.set_editor_property("stunned_animation", load_required(stunned_path))
    cdo.set_editor_property("stun_effect", load_required("/Game/Assets/Effects/Stun/NS_Stars"))
    cdo.set_editor_property("enemy_display_name", display_name)
    cdo.set_editor_property("combat_style", unreal.AuraEnemyCombatStyle.MELEE)
    cdo.set_editor_property("enemy_archetype", attack_archetype)
    cdo.set_editor_property("boss_enemy", False)
    cdo.set_editor_property("starting_health", 100.0)

    mesh = cdo.get_editor_property("mesh")
    mesh.set_skeletal_mesh_asset(load_required(mesh_path))
    mesh.set_editor_property(
        "relative_location", unreal.Vector(0.0, 0.0, -capsule_half_height)
    )
    mesh.set_editor_property("relative_rotation", make_rotator(yaw=-90.0))
    mesh.set_editor_property(
        "relative_scale3d", unreal.Vector(mesh_scale, mesh_scale, mesh_scale)
    )
    mesh.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    animation_data = mesh.get_editor_property("animation_data")
    animation_data.set_editor_property("anim_to_play", load_required(idle_path))
    animation_data.set_editor_property("saved_looping", True)
    animation_data.set_editor_property("saved_playing", True)
    mesh.set_editor_property("animation_data", animation_data)

    capsule = cdo.get_editor_property("capsule_component")
    capsule.set_capsule_size(capsule_radius, capsule_half_height)

    if weapon_mesh_path:
        cdo.set_editor_property("enemy_weapon_mesh", load_required(weapon_mesh_path))
        cdo.set_editor_property("enemy_weapon_socket", unreal.Name(weapon_socket))
    else:
        cdo.set_editor_property("enemy_weapon_mesh", None)
        cdo.set_editor_property("enemy_weapon_socket", unreal.Name("None"))

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assets.save_loaded_asset(blueprint, only_if_is_dirty=False)
    return blueprint


goblin = configure_enemy(
    "BP_AuraGoblinSpear",
    "/Game/Assets/Enemies/Goblin/SKM_Goblin",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Idle_Spear2",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Run_Spear_fix",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Attack_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/HitReact_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Death_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Stun_Loop",
    "GOBLIN SPEARMAN",
    42.0,
    76.0,
    unreal.AuraEnemyArchetype.GOBLIN_SPEARMAN,
    mesh_scale=1.00,
    weapon_mesh_path="/Game/Assets/Enemies/Goblin/Spear/SKM_Spear",
    weapon_socket="RSocket",
)

shroom = configure_enemy(
    "BP_AuraShroom",
    "/Game/Assets/Enemies/Shroom/Mesh/SKM_Shroom",
    "/Game/Assets/Enemies/Shroom/Animations/Idle",
    "/Game/Assets/Enemies/Shroom/Animations/Bounce",
    "/Game/Assets/Enemies/Shroom/Animations/Attack",
    "/Game/Assets/Enemies/Shroom/Animations/Bounce",
    "/Game/Assets/Enemies/Shroom/Animations/Attack",
    "/Game/Assets/Enemies/Shroom/Animations/Idle",
    "CAVE SHROOM",
    60.0,
    90.0,
    unreal.AuraEnemyArchetype.CAVE_SHROOM,
    mesh_scale=1.00,
)

regular = load_required("/Game/Aura/Blueprints/BP_AuraEnemy")
goblin_class = goblin.generated_class()
shroom_class = shroom.generated_class()
regular_class = regular.generated_class()

world = unreal.EditorLoadingAndSavingUtils.load_map(DUNGEON_MAP)
if not world:
    raise RuntimeError(f"Could not load map: {DUNGEON_MAP}")

encounter_classes = {
    "DungeonEncounter_OuterGuard": [goblin_class, shroom_class, regular_class],
    "DungeonRewardEncounter_LowerNorth": [shroom_class, goblin_class],
    "DungeonRewardEncounter_LowerSouthAmbush": [shroom_class, goblin_class],
}

updated = []
for actor in actor_subsystem.get_all_level_actors():
    label = actor.get_actor_label()
    classes = encounter_classes.get(label)
    if classes is None:
        continue
    actor.set_editor_property("spawn_enemy_classes", classes)
    if hasattr(actor, "rerun_construction_scripts"):
        actor.rerun_construction_scripts()
    updated.append(label)

missing = sorted(set(encounter_classes) - set(updated))
if missing:
    raise RuntimeError(f"Missing encounter actors: {missing}")

if not unreal.EditorLoadingAndSavingUtils.save_map(world, DUNGEON_MAP):
    raise RuntimeError("Failed to save DungeonDemo")

unreal.log_warning(
    "AURA_CREATURE_VARIANTS_SETUP_SUCCESS "
    f"goblin={goblin.get_path_name()} shroom={shroom.get_path_name()} "
    f"encounters={updated}"
)
