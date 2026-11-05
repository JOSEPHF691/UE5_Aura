import unreal


BLUEPRINT_PATH = "/Game/Aura/Blueprints"
ENEMY_BLUEPRINT_PATH = f"{BLUEPRINT_PATH}/BP_AuraEnemy"
STARTUP_MAP_PATH = "/Game/Map/StartupMap"
DEMO_ENEMY_LABEL = "AuraDemoEnemy"
NAV_BOUNDS_LABEL = "AuraNavBounds"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    rotation = unreal.Rotator()
    rotation.set_editor_property("pitch", pitch)
    rotation.set_editor_property("yaw", yaw)
    rotation.set_editor_property("roll", roll)
    return rotation


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


if not editor_assets.does_directory_exist(BLUEPRINT_PATH):
    editor_assets.make_directory(BLUEPRINT_PATH)

enemy_parent = unreal.load_class(None, "/Script/Aura.AuraEnemy")
if not enemy_parent:
    raise RuntimeError("Could not load native AuraEnemy class")

enemy_blueprint = create_blueprint("BP_AuraEnemy", BLUEPRINT_PATH, enemy_parent)
unreal.BlueprintEditorLibrary.compile_blueprint(enemy_blueprint)

ghoul_mesh = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/SKM_Ghoul")
idle_animation = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/Animations/Idle")
walk_animation = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/Animations/Walk")
attack_animation = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/Animations/Attack_L")
hit_react_animation = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/Animations/HitReact")
death_animation = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/Animations/Death")
stunned_animation = editor_assets.load_asset("/Game/Assets/Enemies/Ghoul/Animations/ShockLoop")
stun_effect = editor_assets.load_asset("/Game/Assets/Effects/Stun/NS_Stars")
if not all((ghoul_mesh, idle_animation, walk_animation, attack_animation, hit_react_animation,
            death_animation, stunned_animation, stun_effect)):
    raise RuntimeError("One or more Ghoul demo assets are missing")

enemy_cdo = unreal.get_default_object(enemy_blueprint.generated_class())
enemy_cdo.set_editor_property("idle_animation", idle_animation)
enemy_cdo.set_editor_property("walk_animation", walk_animation)
enemy_cdo.set_editor_property("attack_animation", attack_animation)
enemy_cdo.set_editor_property("hit_react_animation", hit_react_animation)
enemy_cdo.set_editor_property("death_animation", death_animation)
enemy_cdo.set_editor_property("stunned_animation", stunned_animation)
enemy_cdo.set_editor_property("stun_effect", stun_effect)

mesh_component = enemy_cdo.get_editor_property("mesh")
mesh_component.set_skeletal_mesh_asset(ghoul_mesh)
mesh_component.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -96.0))
mesh_component.set_editor_property("relative_rotation", make_rotator(yaw=-90.0))
mesh_component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
animation_data = mesh_component.get_editor_property("animation_data")
animation_data.set_editor_property("anim_to_play", idle_animation)
animation_data.set_editor_property("saved_looping", True)
animation_data.set_editor_property("saved_playing", True)
mesh_component.set_editor_property("animation_data", animation_data)

unreal.BlueprintEditorLibrary.compile_blueprint(enemy_blueprint)
editor_assets.save_loaded_asset(enemy_blueprint, only_if_is_dirty=False)

world = unreal.EditorLoadingAndSavingUtils.load_map(STARTUP_MAP_PATH)
if not world:
    raise RuntimeError("Could not load StartupMap")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
demo_enemy = None
nav_bounds = None
for actor in actor_subsystem.get_all_level_actors():
    if actor.get_actor_label() == DEMO_ENEMY_LABEL:
        demo_enemy = actor
    if actor.get_actor_label() == NAV_BOUNDS_LABEL:
        nav_bounds = actor

enemy_class = enemy_blueprint.generated_class()
if demo_enemy and demo_enemy.get_class() != enemy_class:
    raise RuntimeError(
        f"Actor labeled {DEMO_ENEMY_LABEL} already exists with unexpected class "
        f"{demo_enemy.get_class().get_name()}"
    )

spawn_location = unreal.Vector(700.0, 0.0, 96.0)
spawn_rotation = make_rotator(yaw=180.0)
if not demo_enemy:
    demo_enemy = actor_subsystem.spawn_actor_from_class(enemy_class, spawn_location, spawn_rotation)
    if not demo_enemy:
        raise RuntimeError("Failed to place the combat demo enemy")
    demo_enemy.set_actor_label(DEMO_ENEMY_LABEL)
else:
    demo_enemy.set_actor_location(spawn_location, False, False)
    demo_enemy.set_actor_rotation(spawn_rotation, False)

demo_enemy.set_editor_property("tags", [unreal.Name("AuraDemoEnemy")])

if nav_bounds and nav_bounds.get_class().get_name() != "NavMeshBoundsVolume":
    raise RuntimeError(
        f"Actor labeled {NAV_BOUNDS_LABEL} already exists with unexpected class "
        f"{nav_bounds.get_class().get_name()}"
    )
if not nav_bounds:
    nav_bounds = actor_subsystem.spawn_actor_from_class(
        unreal.NavMeshBoundsVolume,
        unreal.Vector(0.0, 0.0, 150.0),
        make_rotator(),
    )
    if not nav_bounds:
        raise RuntimeError("Failed to place the demo NavMeshBoundsVolume")
    nav_bounds.set_actor_label(NAV_BOUNDS_LABEL)
else:
    nav_bounds.set_actor_location(unreal.Vector(0.0, 0.0, 150.0), False, False)
    nav_bounds.set_actor_rotation(make_rotator(), False)
nav_bounds.set_actor_scale3d(unreal.Vector(20.0, 20.0, 5.0))
nav_bounds.set_editor_property("tags", [unreal.Name("AuraNavBounds")])

if not unreal.EditorLoadingAndSavingUtils.save_map(world, STARTUP_MAP_PATH):
    raise RuntimeError("Failed to save StartupMap after placing the demo enemy")

unreal.log_warning(
    "AURA_COMBAT_SETUP_SUCCESS: Ghoul AI/stun presentation, AuraDemoEnemy, and AuraNavBounds configured"
)
