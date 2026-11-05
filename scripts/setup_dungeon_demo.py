import unreal


SOURCE_MAP_PATH = "/Game/Map/StartupMap"
DUNGEON_MAP_PATH = "/Game/Map/DungeonDemo"
BLUEPRINT_PATH = "/Game/Aura/Blueprints/Dungeon"
BOSS_ANIMATION_PATH = "/Game/Aura/Animations/Boss"
BOSS_DEATH_ANIMATION = f"{BOSS_ANIMATION_PATH}/Demon_Death_Cinematic"
GENERATED_TAG = unreal.Name("AuraGeneratedDungeon")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    rotation = unreal.Rotator()
    rotation.set_editor_property("pitch", pitch)
    rotation.set_editor_property("yaw", yaw)
    rotation.set_editor_property("roll", roll)
    return rotation


def ensure_directory(path):
    if not editor_assets.does_directory_exist(path):
        editor_assets.make_directory(path)


def load_required(path):
    asset = editor_assets.load_asset(path)
    if not asset:
        raise RuntimeError(f"Required dungeon asset is missing: {path}")
    return asset


def create_blueprint(name, path, parent_class):
    full_path = f"{path}/{name}"
    if editor_assets.does_asset_exist(full_path):
        return editor_assets.load_asset(full_path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = asset_tools.create_asset(name, path, unreal.Blueprint, factory)
    if not blueprint:
        raise RuntimeError(f"Failed to create blueprint: {full_path}")
    return blueprint


def ensure_boss_death_animation():
    ensure_directory(BOSS_ANIMATION_PATH)
    if not editor_assets.does_asset_exist(BOSS_DEATH_ANIMATION):
        duplicated = editor_assets.duplicate_asset(
            "/Game/Assets/Enemies/Demon/Animations/Demon_Stun",
            BOSS_DEATH_ANIMATION,
        )
        if not duplicated:
            raise RuntimeError("Failed to create the Demon cinematic death sequence")
    animation = load_required(BOSS_DEATH_ANIMATION)
    animation.set_editor_property("rate_scale", 0.9)
    editor_assets.save_loaded_asset(animation, only_if_is_dirty=False)
    return animation


def configure_enemy_blueprint(
    name,
    mesh_path,
    idle_path,
    walk_path,
    attack_path,
    hit_react_path,
    death_path,
    stunned_path,
    display_name,
    starting_health,
    capsule_radius=None,
    capsule_half_height=None,
    mesh_scale=1.0,
    weapon_mesh_path=None,
    weapon_socket="None",
):
    enemy_parent = unreal.load_class(None, "/Script/Aura.AuraEnemy")
    if not enemy_parent:
        raise RuntimeError("Could not load native AuraEnemy class")
    blueprint = create_blueprint(name, BLUEPRINT_PATH, enemy_parent)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

    mesh = load_required(mesh_path)
    idle = load_required(idle_path)
    walk = load_required(walk_path)
    attack = load_required(attack_path)
    hit_react = load_required(hit_react_path)
    death = load_required(death_path)
    stunned = load_required(stunned_path)
    stun_effect = load_required("/Game/Assets/Effects/Stun/NS_Stars")

    cdo = unreal.get_default_object(blueprint.generated_class())
    cdo.set_editor_property("idle_animation", idle)
    cdo.set_editor_property("walk_animation", walk)
    cdo.set_editor_property("attack_animation", attack)
    cdo.set_editor_property("hit_react_animation", hit_react)
    cdo.set_editor_property("death_animation", death)
    cdo.set_editor_property("stunned_animation", stunned)
    cdo.set_editor_property("stun_effect", stun_effect)
    cdo.set_editor_property("enemy_display_name", display_name)
    cdo.set_editor_property("starting_health", starting_health)

    mesh_component = cdo.get_editor_property("mesh")
    mesh_component.set_skeletal_mesh_asset(mesh)
    mesh_floor_offset = capsule_half_height if capsule_half_height is not None else 96.0
    mesh_component.set_editor_property(
        "relative_location", unreal.Vector(0.0, 0.0, -mesh_floor_offset)
    )
    mesh_component.set_editor_property("relative_rotation", make_rotator(yaw=-90.0))
    mesh_component.set_editor_property(
        "relative_scale3d", unreal.Vector(mesh_scale, mesh_scale, mesh_scale)
    )
    mesh_component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    animation_data = mesh_component.get_editor_property("animation_data")
    animation_data.set_editor_property("anim_to_play", idle)
    animation_data.set_editor_property("saved_looping", True)
    animation_data.set_editor_property("saved_playing", True)
    mesh_component.set_editor_property("animation_data", animation_data)

    if capsule_radius is not None and capsule_half_height is not None:
        cdo.get_editor_property("capsule_component").set_capsule_size(
            capsule_radius, capsule_half_height
        )
    if weapon_mesh_path:
        cdo.set_editor_property("enemy_weapon_mesh", load_required(weapon_mesh_path))
        cdo.set_editor_property("enemy_weapon_socket", unreal.Name(weapon_socket))

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    editor_assets.save_loaded_asset(blueprint, only_if_is_dirty=False)
    return blueprint


def tag_generated(actor, label):
    actor.set_actor_label(label)
    actor.set_editor_property("tags", [GENERATED_TAG])
    return actor


def spawn_actor(actor_class, location, rotation=None, label="GeneratedActor"):
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class, location, rotation or make_rotator()
    )
    if not actor:
        raise RuntimeError(f"Failed to spawn actor: {label}")
    return tag_generated(actor, label)


def spawn_static_mesh(mesh, location, rotation=None, scale=None, label="DungeonMesh"):
    actor = spawn_actor(unreal.StaticMeshActor, location, rotation, label)
    component = actor.get_editor_property("static_mesh_component")
    component.set_static_mesh(mesh)
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    if scale:
        actor.set_actor_scale3d(scale)
    return actor


def spawn_point_light(location, color, intensity, radius, label):
    light = spawn_actor(unreal.PointLight, location, make_rotator(), label)
    component = light.get_editor_property("point_light_component")
    component.set_editor_property("intensity", intensity)
    component.set_editor_property("attenuation_radius", radius)
    component.set_editor_property("light_color", color)
    component.set_editor_property("cast_shadows", True)
    return light


ensure_directory(BLUEPRINT_PATH)
boss_death_animation = ensure_boss_death_animation()

regular_enemy_blueprint = load_required("/Game/Aura/Blueprints/BP_AuraEnemy")
regular_enemy_cdo = unreal.get_default_object(regular_enemy_blueprint.generated_class())
regular_enemy_cdo.set_editor_property("enemy_display_name", "GHOUL")
regular_enemy_cdo.set_editor_property("starting_health", 100.0)
regular_enemy_cdo.set_editor_property("combat_style", unreal.AuraEnemyCombatStyle.MELEE)
regular_enemy_cdo.set_editor_property("enemy_archetype", unreal.AuraEnemyArchetype.GHOUL)
regular_enemy_cdo.set_editor_property("boss_enemy", False)
unreal.BlueprintEditorLibrary.compile_blueprint(regular_enemy_blueprint)
editor_assets.save_loaded_asset(regular_enemy_blueprint, only_if_is_dirty=False)

goblin_blueprint = configure_enemy_blueprint(
    "BP_AuraGoblinSpear",
    "/Game/Assets/Enemies/Goblin/SKM_Goblin",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Idle_Spear2",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Run_Spear_fix",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Attack_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/HitReact_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Death_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Stun_Loop",
    "GOBLIN SPEARMAN",
    100.0,
    capsule_radius=42.0,
    capsule_half_height=76.0,
    mesh_scale=1.00,
    weapon_mesh_path="/Game/Assets/Enemies/Goblin/Spear/SKM_Spear",
    weapon_socket="RSocket",
)
goblin_cdo = unreal.get_default_object(goblin_blueprint.generated_class())
goblin_cdo.set_editor_property("combat_style", unreal.AuraEnemyCombatStyle.MELEE)
goblin_cdo.set_editor_property("enemy_archetype", unreal.AuraEnemyArchetype.GOBLIN_SPEARMAN)
goblin_cdo.set_editor_property("boss_enemy", False)
unreal.BlueprintEditorLibrary.compile_blueprint(goblin_blueprint)
editor_assets.save_loaded_asset(goblin_blueprint, only_if_is_dirty=False)

shroom_blueprint = configure_enemy_blueprint(
    "BP_AuraShroom",
    "/Game/Assets/Enemies/Shroom/Mesh/SKM_Shroom",
    "/Game/Assets/Enemies/Shroom/Animations/Idle",
    "/Game/Assets/Enemies/Shroom/Animations/Bounce",
    "/Game/Assets/Enemies/Shroom/Animations/Attack",
    "/Game/Assets/Enemies/Shroom/Animations/Bounce",
    "/Game/Assets/Enemies/Shroom/Animations/Attack",
    "/Game/Assets/Enemies/Shroom/Animations/Idle",
    "CAVE SHROOM",
    100.0,
    capsule_radius=60.0,
    capsule_half_height=90.0,
    mesh_scale=1.00,
)
shroom_cdo = unreal.get_default_object(shroom_blueprint.generated_class())
shroom_cdo.set_editor_property("combat_style", unreal.AuraEnemyCombatStyle.MELEE)
shroom_cdo.set_editor_property("enemy_archetype", unreal.AuraEnemyArchetype.CAVE_SHROOM)
shroom_cdo.set_editor_property("boss_enemy", False)
unreal.BlueprintEditorLibrary.compile_blueprint(shroom_blueprint)
editor_assets.save_loaded_asset(shroom_blueprint, only_if_is_dirty=False)

elite_blueprint = configure_enemy_blueprint(
    "BP_AuraEliteShaman",
    "/Game/Assets/Enemies/Shaman/SKM_Shaman",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_Idle",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_Walk",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_Attack",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_HitReact",
    "/Game/Assets/Enemies/Shaman/Animations/Death",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_ShockLoop",
    "SHAMAN ELITE",
    200.0,
)
elite_cdo = unreal.get_default_object(elite_blueprint.generated_class())
elite_cdo.set_editor_property("combat_style", unreal.AuraEnemyCombatStyle.RANGED)
elite_cdo.set_editor_property("enemy_archetype", unreal.AuraEnemyArchetype.SHAMAN_ELITE)
elite_cdo.set_editor_property("boss_enemy", False)
elite_cdo.set_editor_property(
    "ranged_cast_animation",
    load_required("/Game/Assets/Enemies/Shaman/Animations/Shaman_Summon"),
)
unreal.BlueprintEditorLibrary.compile_blueprint(elite_blueprint)
editor_assets.save_loaded_asset(elite_blueprint, only_if_is_dirty=False)

boss_blueprint = configure_enemy_blueprint(
    "BP_AuraBossDemon",
    "/Game/Assets/Enemies/Demon/SKM_Demon",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Red_Idle",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Red_Run",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Attack_R",
    "/Game/Assets/Enemies/Demon/Animations/Demon_HitReact",
    BOSS_DEATH_ANIMATION,
    "/Game/Assets/Enemies/Demon/Animations/DemonShockLoop",
    "DEMON LORD",
    450.0,
)
boss_cdo = unreal.get_default_object(boss_blueprint.generated_class())
boss_cdo.set_editor_property("combat_style", unreal.AuraEnemyCombatStyle.MELEE)
boss_cdo.set_editor_property("enemy_archetype", unreal.AuraEnemyArchetype.DEMON_LORD)
boss_cdo.set_editor_property("boss_enemy", True)
boss_cdo.set_editor_property(
    "boss_attack_left_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Attack_L"),
)
boss_cdo.set_editor_property(
    "boss_attack_right_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Attack_R"),
)
boss_cdo.set_editor_property(
    "boss_tail_spin_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Red_Tailspin"),
)
boss_cdo.set_editor_property(
    "boss_throw_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Throw"),
)
boss_cdo.set_editor_property(
    "boss_summon_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Stun"),
)
boss_cdo.set_editor_property(
    "boss_charge_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Attack_R"),
)
boss_cdo.set_editor_property(
    "boss_summon_classes",
    [
        regular_enemy_blueprint.generated_class(),
        goblin_blueprint.generated_class(),
        shroom_blueprint.generated_class(),
    ],
)
boss_cdo.set_editor_property(
    "boss_phase_transition_animation",
    load_required("/Game/Assets/Enemies/Demon/Animations/Demon_Stun"),
)
boss_cdo.set_editor_property("boss_death_animation", boss_death_animation)
boss_cdo.set_editor_property(
    "boss_death_effect",
    load_required("/Game/Assets/Effects/Fire/NS_Fire"),
)
boss_cdo.set_editor_property(
    "boss_death_sound",
    load_required("/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_01"),
)
boss_cdo.set_editor_property(
    "boss_death_impact_sound",
    load_required("/Game/Assets/Sounds/Enemies/Goblin/RockHit/SFX_RockHit_Body_03"),
)
boss_cdo.set_editor_property("boss_death_ragdoll_delay", 1.45)
boss_cdo.set_editor_property("boss_death_effect_duration", 2.8)
boss_cdo.set_editor_property("boss_death_impulse_strength", 90.0)
boss_cdo.set_editor_property("boss_corpse_lifetime", 10.0)
unreal.BlueprintEditorLibrary.compile_blueprint(boss_blueprint)
editor_assets.save_loaded_asset(boss_blueprint, only_if_is_dirty=False)

controller_blueprint = load_required("/Game/Aura/Blueprints/BP_AuraPlayerController")
unreal.BlueprintEditorLibrary.compile_blueprint(controller_blueprint)
controller_cdo = unreal.get_default_object(controller_blueprint.generated_class())
controller_cdo.set_editor_property(
    "victory_sting", load_required("/Game/Assets/Sounds/LevelUp/SFX_LevelUp")
)
controller_cdo.set_editor_property(
    "victory_accent_sound",
    load_required("/Game/Assets/Sounds/UI/Unlock_Skills/SFX_UI_Unlock_NewSkill"),
)
controller_cdo.set_editor_property("victory_camera_blend_time", 0.55)
controller_cdo.set_editor_property("victory_camera_distance", 650.0)
controller_cdo.set_editor_property("victory_camera_height", 180.0)
controller_cdo.set_editor_property("victory_camera_motion_duration", 3.0)
controller_cdo.set_editor_property("victory_camera_orbit_degrees", 18.0)
controller_cdo.set_editor_property("victory_camera_fov", 48.0)
controller_cdo.set_editor_property("victory_sting_delay", 1.25)
controller_cdo.set_editor_property("victory_overlay_delay", 3.2)
unreal.BlueprintEditorLibrary.compile_blueprint(controller_blueprint)
editor_assets.save_loaded_asset(controller_blueprint, only_if_is_dirty=False)

if not editor_assets.does_asset_exist(DUNGEON_MAP_PATH):
    duplicated_map = editor_assets.duplicate_asset(SOURCE_MAP_PATH, DUNGEON_MAP_PATH)
    if not duplicated_map:
        raise RuntimeError("Failed to duplicate StartupMap into DungeonDemo")

world = unreal.EditorLoadingAndSavingUtils.load_map(DUNGEON_MAP_PATH)
if not world:
    raise RuntimeError("Could not load DungeonDemo")

source_labels_to_remove = {
    "Floor",
    "PlayerStart",
    "AuraDemoEnemy",
    "AuraHealthPickup",
    "AuraManaPickup",
    "AuraNavBounds",
}
for actor in list(actor_subsystem.get_all_level_actors()):
    tags = [str(tag) for tag in actor.get_editor_property("tags")]
    if str(GENERATED_TAG) in tags or actor.get_actor_label() in source_labels_to_remove:
        actor_subsystem.destroy_actor(actor)

floor_mesh = load_required("/Game/Assets/Dungeon/SM_Tile_6x9")
wall_mesh = load_required("/Game/Assets/Dungeon/SM_Wall_4x2")
stair_mesh = load_required("/Game/Assets/Dungeon/SM_Stairs")
support_mesh = load_required("/Game/Assets/Dungeon/SM_Block3x3x1")
ramp_mesh = load_required("/Engine/BasicShapes/Cube")
beacon_mesh = load_required("/Game/Assets/Dungeon/Beacon/SM_Beacon")
pillar_mesh = load_required("/Game/Assets/Dungeon/Beacon/SM_Pillar")
corner_mesh = load_required("/Game/Assets/Dungeon/SM_Corner")

# A two-level branching dungeon assembled on the kit's native 200uu grid.
# The lower junction has north/south dead ends; the upper junction has another
# south dead end. Only the east-then-north route reaches the boss.
floor_specs = [
    ("Lower_00", unreal.Vector(0.0, -900.0, 0.0), make_rotator()),
    ("Lower_01", unreal.Vector(1200.0, -900.0, 0.0), make_rotator()),
    ("Lower_02", unreal.Vector(2400.0, -900.0, 0.0), make_rotator()),
    ("LowerNorth_00", unreal.Vector(3000.0, 900.0, 0.0), make_rotator(yaw=90.0)),
    ("LowerNorth_01", unreal.Vector(3000.0, 2100.0, 0.0), make_rotator(yaw=90.0)),
    ("LowerSouth_00", unreal.Vector(1200.0, -900.0, 0.0), make_rotator(yaw=-90.0)),
    ("LowerSouth_01", unreal.Vector(1200.0, -2100.0, 0.0), make_rotator(yaw=-90.0)),
    ("Upper_00", unreal.Vector(4200.0, -900.0, 600.0), make_rotator()),
    ("Upper_01", unreal.Vector(5400.0, -900.0, 600.0), make_rotator()),
    ("Upper_02", unreal.Vector(6600.0, -900.0, 600.0), make_rotator()),
    ("UpperSouth_00", unreal.Vector(4800.0, -900.0, 600.0), make_rotator(yaw=-90.0)),
    ("UpperSouth_01", unreal.Vector(4800.0, -2100.0, 600.0), make_rotator(yaw=-90.0)),
    ("BossWing_00", unreal.Vector(8400.0, 900.0, 600.0), make_rotator(yaw=90.0)),
    ("BossWing_01", unreal.Vector(8400.0, 2100.0, 600.0), make_rotator(yaw=90.0)),
    ("BossWing_02", unreal.Vector(8400.0, 3300.0, 600.0), make_rotator(yaw=90.0)),
]
for floor_name, location, rotation in floor_specs:
    spawn_static_mesh(
        floor_mesh,
        location,
        rotation,
        label=f"DungeonFloor_{floor_name}",
    )

# The supplied stair is visual; a hidden 36-degree cube ramp supplies smooth
# character collision and a continuous Recast surface beneath the steps. The
# ramp deliberately continues inside both floor tiles so Recast can weld the
# sloped polygon to the lower floor. Its upper face reaches 600uu at the exact
# front edge of the upper floor, avoiding a low-ceiling pocket under that tile.
stair_actor = spawn_static_mesh(
    stair_mesh,
    unreal.Vector(3600.0, -300.0, 0.0),
    make_rotator(),
    label="DungeonStairs_MainAscent",
)
stair_component = stair_actor.get_editor_property("static_mesh_component")
stair_actor.set_actor_enable_collision(False)
stair_component.set_collision_profile_name(unreal.Name("NoCollision"))
stair_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

ramp_actor = spawn_static_mesh(
    ramp_mesh,
    unreal.Vector(3748.5, 0.0, 253.2),
    make_rotator(pitch=36.0),
    unreal.Vector(11.75, 6.0, 0.3),
    "DungeonStairRamp_Navigation",
)
ramp_component = ramp_actor.get_editor_property("static_mesh_component")
ramp_component.set_editor_property("visible", False)
ramp_component.set_editor_property("hidden_in_game", True)
ramp_component.set_editor_property("cast_shadow", False)
ramp_component.set_collision_profile_name(unreal.Name("BlockAll"))
ramp_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)

for side_index, y in enumerate((-900.0, 300.0)):
    for layer_index, z in enumerate((0.0, 200.0, 400.0)):
        spawn_static_mesh(
            support_mesh,
            unreal.Vector(3600.0, y, z),
            label=f"DungeonStairSupport_{side_index}_{layer_index}",
        )

wall_piece_count = 0


def spawn_wall_run(group, orientation, boundary, run_start, run_end, floor_z):
    """Tile one exact boundary run with the supplied 800uu wall module."""
    global wall_piece_count
    cursor = run_start
    piece_index = 0
    while cursor < run_end - 0.01:
        piece_length = min(800.0, run_end - cursor)
        piece_scale = unreal.Vector(piece_length / 800.0, 1.0, 1.0)
        if orientation == "north":
            location = unreal.Vector(cursor, boundary - 550.0, floor_z + 130.0)
            rotation = make_rotator()
        elif orientation == "south":
            location = unreal.Vector(cursor + piece_length, boundary + 550.0, floor_z + 130.0)
            rotation = make_rotator(yaw=180.0)
        elif orientation == "east":
            location = unreal.Vector(boundary + 550.0, cursor, floor_z + 130.0)
            rotation = make_rotator(yaw=90.0)
        elif orientation == "west":
            location = unreal.Vector(boundary - 550.0, cursor + piece_length, floor_z + 130.0)
            rotation = make_rotator(yaw=-90.0)
        else:
            raise RuntimeError(f"Unsupported wall orientation: {orientation}")
        spawn_static_mesh(
            wall_mesh,
            location,
            rotation,
            piece_scale,
            f"DungeonWall_{group}_{piece_index:02d}",
        )
        cursor += piece_length
        piece_index += 1
        wall_piece_count += 1


wall_runs = [
    # Lower main hall, with 1800uu-wide north/south junction openings.
    ("LowerStart", "west", 0.0, -900.0, 900.0, 0.0),
    ("LowerNorthWest", "north", 900.0, 0.0, 1200.0, 0.0),
    ("LowerNorthEast", "north", 900.0, 3000.0, 3600.0, 0.0),
    ("LowerSouthWest", "south", -900.0, 0.0, 1200.0, 0.0),
    ("LowerSouthEast", "south", -900.0, 3000.0, 3600.0, 0.0),
    # Lower north dead end.
    ("LowerNorthBranchW", "west", 1200.0, 900.0, 3300.0, 0.0),
    ("LowerNorthBranchE", "east", 3000.0, 900.0, 3300.0, 0.0),
    ("LowerNorthDeadEnd", "north", 3300.0, 1200.0, 3000.0, 0.0),
    # Lower south dead end.
    ("LowerSouthBranchW", "west", 1200.0, -3300.0, -900.0, 0.0),
    ("LowerSouthBranchE", "east", 3000.0, -3300.0, -900.0, 0.0),
    ("LowerSouthDeadEnd", "south", -3300.0, 1200.0, 3000.0, 0.0),
    # The wall mesh has an off-centre pivot. These compensated runs create a
    # centred 700uu opening around the 660uu-wide visual staircase.
    ("StairLowerS", "east", 3600.0, -550.0, 0.0, 0.0),
    ("StairLowerN", "east", 3600.0, 700.0, 1250.0, 0.0),
    ("StairUpperS", "west", 4200.0, -1250.0, -700.0, 600.0),
    ("StairUpperN", "west", 4200.0, 0.0, 550.0, 600.0),
    # Upper main hall: south branch opening and north boss-route opening.
    ("UpperNorth", "north", 900.0, 4200.0, 6600.0, 600.0),
    ("UpperSouthWest", "south", -900.0, 4200.0, 4800.0, 600.0),
    ("UpperSouthEast", "south", -900.0, 6600.0, 7800.0, 600.0),
    ("UpperEast", "east", 7800.0, -900.0, 900.0, 600.0),
    # Upper south dead end.
    ("UpperSouthBranchW", "west", 4800.0, -3300.0, -900.0, 600.0),
    ("UpperSouthBranchE", "east", 6600.0, -3300.0, -900.0, 600.0),
    ("UpperSouthDeadEnd", "south", -3300.0, 4800.0, 6600.0, 600.0),
    # Unique main route: turn north into the boss wing.
    ("BossWingSouthTail", "north", 900.0, 7800.0, 8400.0, 600.0),
    ("BossWingW", "west", 6600.0, 900.0, 4500.0, 600.0),
    ("BossWingE", "east", 8400.0, 900.0, 4500.0, 600.0),
    ("BossDeadEnd", "north", 4500.0, 6600.0, 8400.0, 600.0),
]
for wall_run in wall_runs:
    spawn_wall_run(*wall_run)

beacon_specs = [
    (900.0, -710.0, 0.0), (900.0, 710.0, 0.0),
    (2600.0, -710.0, 0.0), (2600.0, 710.0, 0.0),
    (1380.0, 1700.0, 0.0), (2820.0, 1700.0, 0.0),
    (1380.0, 3000.0, 0.0), (2820.0, 3000.0, 0.0),
    (1380.0, -1700.0, 0.0), (2820.0, -1700.0, 0.0),
    (1380.0, -3000.0, 0.0), (2820.0, -3000.0, 0.0),
    (4700.0, -710.0, 600.0), (4700.0, 710.0, 600.0),
    (6300.0, -710.0, 600.0), (6300.0, 710.0, 600.0),
    (4980.0, -1800.0, 600.0), (6420.0, -1800.0, 600.0),
    (4980.0, -3000.0, 600.0), (6420.0, -3000.0, 600.0),
    (6750.0, 1500.0, 600.0), (8250.0, 1500.0, 600.0),
    (6750.0, 2600.0, 600.0), (8250.0, 2600.0, 600.0),
    (6750.0, 4000.0, 600.0), (8250.0, 4000.0, 600.0),
]
for beacon_index, (x, y, z) in enumerate(beacon_specs):
    spawn_static_mesh(
        beacon_mesh,
        unreal.Vector(x, y, z),
        label=f"DungeonBeacon_{beacon_index:02d}",
    )

for pillar_index, (x, y, z) in enumerate(
    ((3450.0, -520.0, 0.0), (3450.0, 520.0, 0.0),
     (4350.0, -520.0, 600.0), (4350.0, 520.0, 600.0))
):
    spawn_static_mesh(
        pillar_mesh,
        unreal.Vector(x, y, z),
        label=f"DungeonPillar_Stair_{pillar_index}",
    )

for corner_index, (x, y, z, yaw) in enumerate([
    (150.0, -750.0, 0.0, 0.0),
    (150.0, 750.0, 0.0, 90.0),
    (1350.0, 3150.0, 0.0, 0.0),
    (2850.0, 3150.0, 0.0, 90.0),
    (1350.0, -3150.0, 0.0, -90.0),
    (2850.0, -3150.0, 0.0, 180.0),
    (4950.0, -3150.0, 600.0, -90.0),
    (6450.0, -3150.0, 600.0, 180.0),
    (6750.0, 4350.0, 600.0, -90.0),
    (8250.0, 4350.0, 600.0, 180.0),
]):
    spawn_static_mesh(
        corner_mesh,
        unreal.Vector(x, y, z),
        make_rotator(yaw=yaw),
        unreal.Vector(0.75, 0.75, 0.75),
        f"DungeonCorner_{corner_index}",
    )

light_specs = [
    (unreal.Vector(900.0, 0.0, 430.0), unreal.Color(255, 150, 70, 255), 4200.0),
    (unreal.Vector(2600.0, 0.0, 430.0), unreal.Color(255, 98, 42, 255), 4200.0),
    (unreal.Vector(2100.0, 2500.0, 430.0), unreal.Color(70, 130, 255, 255), 4700.0),
    (unreal.Vector(2100.0, -2500.0, 430.0), unreal.Color(90, 255, 130, 255), 4300.0),
    (unreal.Vector(3900.0, 0.0, 620.0), unreal.Color(75, 145, 255, 255), 5000.0),
    (unreal.Vector(5000.0, 0.0, 1030.0), unreal.Color(85, 150, 255, 255), 4400.0),
    (unreal.Vector(6500.0, 0.0, 1030.0), unreal.Color(175, 75, 255, 255), 4800.0),
    (unreal.Vector(5700.0, -2500.0, 1030.0), unreal.Color(235, 85, 255, 255), 5200.0),
    (unreal.Vector(7500.0, 1600.0, 1030.0), unreal.Color(55, 155, 255, 255), 4600.0),
    (unreal.Vector(7500.0, 3400.0, 1100.0), unreal.Color(255, 45, 25, 255), 6800.0),
    (unreal.Vector(7500.0, 4300.0, 1050.0), unreal.Color(255, 25, 15, 255), 6200.0),
]
for light_index, (location, color, intensity) in enumerate(light_specs):
    spawn_point_light(
        location,
        color,
        intensity,
        1450.0,
        f"DungeonLight_{light_index:02d}",
    )

player_start = spawn_actor(
    unreal.PlayerStart,
    unreal.Vector(250.0, 0.0, 96.0),
    make_rotator(yaw=0.0),
    "DungeonPlayerStart",
)

nav_bounds = spawn_actor(
    unreal.NavMeshBoundsVolume,
    unreal.Vector(4200.0, 600.0, 450.0),
    make_rotator(),
    "DungeonNavBounds",
)
nav_bounds.set_actor_scale3d(unreal.Vector(50.0, 50.0, 10.0))

# Commandlet-driven level generation can save an empty Recast actor. Ensure the
# dynamic nav mesh performs a complete first build when the playable map loads.
for nav_actor in actor_subsystem.get_all_level_actors():
    if nav_actor.get_class().get_name() == "RecastNavMesh":
        nav_actor.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_actor.set_editor_property("force_rebuild_on_load", True)

gate_class = unreal.load_class(None, "/Script/Aura.AuraDungeonGate")
checkpoint_class = unreal.load_class(None, "/Script/Aura.AuraDungeonCheckpoint")
encounter_class = unreal.load_class(None, "/Script/Aura.AuraEncounterVolume")
pickup_class = unreal.load_class(None, "/Script/Aura.AuraPickup")
treasure_class = unreal.load_class(None, "/Script/Aura.AuraTreasureChest")
loot_chest_class = unreal.load_class(None, "/Script/Aura.AuraLootChest")
if not all((gate_class, checkpoint_class, encounter_class, pickup_class,
            treasure_class, loot_chest_class)):
    raise RuntimeError("Could not load one or more native dungeon classes")

gate_specs = [
    ("OuterSeal", unreal.Vector(3200.0, 0.0, 225.0), make_rotator(), False),
    ("InnerSeal", unreal.Vector(7500.0, 1100.0, 825.0), make_rotator(yaw=90.0), False),
    ("BossSeal", unreal.Vector(7500.0, 2700.0, 825.0), make_rotator(yaw=90.0), True),
    ("LowerSouthAmbush", unreal.Vector(2100.0, -1250.0, 225.0), make_rotator(yaw=90.0), True),
]
gates = []
for gate_name, location, rotation, starts_open in gate_specs:
    gate = spawn_actor(
        gate_class,
        location,
        rotation,
        f"DungeonGate_{gate_name}",
    )
    gate.set_editor_property("starts_open", starts_open)
    if hasattr(gate, "rerun_construction_scripts"):
        gate.rerun_construction_scripts()
    gates.append(gate)

checkpoint = spawn_actor(
    checkpoint_class,
    unreal.Vector(4450.0, 0.0, 600.0),
    make_rotator(),
    "DungeonCheckpoint_Midpoint",
)
checkpoint.set_editor_property("checkpoint_id", unreal.Name("InnerSanctum"))
checkpoint.set_editor_property("respawn_offset", unreal.Vector(200.0, 0.0, 96.0))
if hasattr(checkpoint, "rerun_construction_scripts"):
    checkpoint.rerun_construction_scripts()

encounter_specs = [
    {
        "label": "DungeonEncounter_OuterGuard",
        "location": unreal.Vector(900.0, 0.0, 0.0),
        "rotation": make_rotator(),
        "title": "BREAK THE OUTER GUARD",
        "order": 1,
        "encounter_type": unreal.AuraEncounterType.NORMAL,
        "balance_profile": unreal.AuraEncounterBalanceProfile.OUTER_GUARD,
        "enemy_class": regular_enemy_blueprint.generated_class(),
        "spawn_classes": [
            goblin_blueprint.generated_class(),
            shroom_blueprint.generated_class(),
            regular_enemy_blueprint.generated_class(),
        ],
        "spawns": [
            unreal.Vector(650.0, -360.0, 96.0),
            unreal.Vector(950.0, 360.0, 96.0),
            unreal.Vector(1300.0, 0.0, 96.0),
        ],
        "scale": unreal.Vector(1.0, 1.0, 1.0),
        "final": False,
        "gates": [gates[0]],
    },
    {
        "label": "DungeonEncounter_EliteShaman",
        "location": unreal.Vector(5200.0, 0.0, 600.0),
        "rotation": make_rotator(),
        "title": "DEFEAT THE SHAMAN ELITE",
        "order": 2,
        "encounter_type": unreal.AuraEncounterType.ELITE,
        "balance_profile": unreal.AuraEncounterBalanceProfile.ELITE_SHAMAN,
        "enemy_class": elite_blueprint.generated_class(),
        "spawns": [unreal.Vector(1000.0, 0.0, 96.0)],
        # Keep the elite clearly readable against the regular enemies in both
        # camera modes without making its collision fill the whole corridor.
        "scale": unreal.Vector(1.75, 1.75, 1.75),
        "final": False,
        "gates": [gates[1]],
    },
    {
        "label": "DungeonEncounter_DemonLord",
        "location": unreal.Vector(7500.0, 3150.0, 600.0),
        "rotation": make_rotator(yaw=90.0),
        "title": "DEFEAT THE DEMON LORD",
        "order": 3,
        "encounter_type": unreal.AuraEncounterType.BOSS,
        "balance_profile": unreal.AuraEncounterBalanceProfile.DEMON_LORD,
        "enemy_class": boss_blueprint.generated_class(),
        "spawns": [unreal.Vector(950.0, 0.0, 96.0)],
        # The source Demon is roughly human-sized, so promote it to a true
        # room-scale boss silhouette here.
        "scale": unreal.Vector(2.5, 2.5, 2.5),
        "final": True,
        "gates": [gates[2]],
    },
]

for spec in encounter_specs:
    encounter = spawn_actor(
        encounter_class, spec["location"], spec["rotation"], spec["label"]
    )
    encounter.set_editor_property("encounter_title", spec["title"])
    encounter.set_editor_property("encounter_order", spec["order"])
    encounter.set_editor_property("encounter_type", spec["encounter_type"])
    encounter.set_editor_property("arming_duration", 1.0)
    encounter.set_editor_property("arming_room_entry_depth", 40.0)
    encounter.set_editor_property("gather_team_on_lock", False)
    encounter.set_editor_property("balance_profile", spec["balance_profile"])
    encounter.set_editor_property("enemy_class", spec["enemy_class"])
    encounter.set_editor_property("spawn_enemy_classes", spec.get("spawn_classes", []))
    encounter.set_editor_property("spawn_offsets", spec["spawns"])
    encounter.set_editor_property("spawned_enemy_scale", spec["scale"])
    encounter.set_editor_property("final_encounter", spec["final"])
    encounter.set_editor_property("controlled_gates", spec["gates"])
    if hasattr(encounter, "rerun_construction_scripts"):
        encounter.rerun_construction_scripts()

# The former lower-south roaming enemies now belong to a repeatable room
# configuration below, so entry warning, locking, waves and reward all share
# the same authoritative state machine.
optional_enemy_specs = []
for label, enemy_class, location, scale in optional_enemy_specs:
    enemy = spawn_actor(enemy_class, location, make_rotator(yaw=90.0), label)
    enemy.set_actor_scale3d(scale)

treasure_specs = [
    (
        "DungeonTreasure_LowerNorthDeadEnd",
        unreal.Vector(2100.0, 2980.0, 0.0),
        make_rotator(yaw=180.0),
    ),
    (
        "DungeonTreasure_UpperSouthDeadEnd",
        unreal.Vector(5700.0, -2980.0, 600.0),
        make_rotator(),
    ),
]
treasures_by_label = {}
for label, location, rotation in treasure_specs:
    treasure = spawn_actor(treasure_class, location, rotation, label)
    treasure.set_editor_property("treasure_id", unreal.Name(label))
    treasures_by_label[label] = treasure
    if hasattr(treasure, "rerun_construction_scripts"):
        treasure.rerun_construction_scripts()

reward_encounter_specs = [
    {
        "label": "DungeonRewardEncounter_LowerNorth",
        "id": "LowerNorthBoonGuardians",
        "location": unreal.Vector(2100.0, 1450.0, 0.0),
        "rotation": make_rotator(yaw=90.0),
        "title": "DEFEAT THE BOON GUARDIANS",
        "order": 101,
        "encounter_type": unreal.AuraEncounterType.GUARDED_TREASURE,
        "enemy_class": regular_enemy_blueprint.generated_class(),
        "spawn_classes": [
            shroom_blueprint.generated_class(),
            goblin_blueprint.generated_class(),
        ],
        "spawns": [
            unreal.Vector(650.0, -360.0, 96.0),
            unreal.Vector(900.0, 360.0, 96.0),
        ],
        "scale": unreal.Vector(1.0, 1.0, 1.0),
        "trigger_extent": unreal.Vector(360.0, 650.0, 180.0),
        "chest": treasures_by_label["DungeonTreasure_LowerNorthDeadEnd"],
    },
    {
        "label": "DungeonRewardEncounter_UpperSouth",
        "id": "UpperSouthBoonGuardian",
        "location": unreal.Vector(5700.0, -1450.0, 600.0),
        "rotation": make_rotator(yaw=-90.0),
        "title": "DEFEAT THE ELITE BOON GUARDIAN",
        "order": 102,
        "encounter_type": unreal.AuraEncounterType.GUARDED_TREASURE,
        "enemy_class": elite_blueprint.generated_class(),
        "spawns": [unreal.Vector(820.0, 0.0, 96.0)],
        "scale": unreal.Vector(1.5, 1.5, 1.5),
        "trigger_extent": unreal.Vector(360.0, 650.0, 180.0),
        "chest": treasures_by_label["DungeonTreasure_UpperSouthDeadEnd"],
    },
]
for spec in reward_encounter_specs:
    encounter = spawn_actor(
        encounter_class, spec["location"], spec["rotation"], spec["label"]
    )
    encounter.set_editor_property("encounter_title", spec["title"])
    encounter.set_editor_property("encounter_order", spec["order"])
    encounter.set_editor_property("encounter_type", spec["encounter_type"])
    encounter.set_editor_property("arming_duration", 1.0)
    encounter.set_editor_property("arming_room_entry_depth", 40.0)
    encounter.set_editor_property("gather_team_on_lock", False)
    encounter.set_editor_property(
        "balance_profile", unreal.AuraEncounterBalanceProfile.CUSTOM
    )
    encounter.set_editor_property("enemy_class", spec["enemy_class"])
    encounter.set_editor_property("spawn_enemy_classes", spec.get("spawn_classes", []))
    encounter.set_editor_property("spawn_offsets", spec["spawns"])
    encounter.set_editor_property("spawned_enemy_scale", spec["scale"])
    encounter.set_editor_property("trigger_extent", spec["trigger_extent"])
    encounter.set_editor_property("reward_encounter", True)
    encounter.set_editor_property("encounter_id", unreal.Name(spec["id"]))
    encounter.set_editor_property("reward_chest", spec["chest"])
    if hasattr(encounter, "rerun_construction_scripts"):
        encounter.rerun_construction_scripts()

# A pure item chest is deliberately separate from the run-boon reward chests.
# The lower-south room demonstrates a two-wave ambush and an explicit F-opened,
# per-player item reward without opening the three-choice upgrade UI.
loot_chest = spawn_actor(
    loot_chest_class,
    unreal.Vector(2100.0, -2980.0, 0.0),
    make_rotator(yaw=90.0),
    "DungeonLootChest_LowerSouth",
)
loot_chest.set_editor_property("loot_chest_id", unreal.Name("LowerSouthAmbushLoot"))
if hasattr(loot_chest, "rerun_construction_scripts"):
    loot_chest.rerun_construction_scripts()

ambush_encounter = spawn_actor(
    encounter_class,
    unreal.Vector(2100.0, -1550.0, 0.0),
    make_rotator(yaw=-90.0),
    "DungeonRewardEncounter_LowerSouthAmbush",
)
ambush_encounter.set_editor_property("encounter_title", "SURVIVE THE AMBUSH")
ambush_encounter.set_editor_property("encounter_order", 103)
ambush_encounter.set_editor_property("encounter_type", unreal.AuraEncounterType.AMBUSH)
ambush_encounter.set_editor_property(
    "balance_profile", unreal.AuraEncounterBalanceProfile.CUSTOM
)
ambush_encounter.set_editor_property("enemy_class", regular_enemy_blueprint.generated_class())
ambush_encounter.set_editor_property(
    "spawn_enemy_classes",
    [shroom_blueprint.generated_class(), goblin_blueprint.generated_class()],
)
ambush_encounter.set_editor_property(
    "spawn_offsets",
    [
        unreal.Vector(650.0, -300.0, 96.0),
        unreal.Vector(1050.0, 300.0, 96.0),
    ],
)
ambush_encounter.set_editor_property("spawned_enemy_scale", unreal.Vector(1.0, 1.0, 1.0))
ambush_encounter.set_editor_property("trigger_extent", unreal.Vector(360.0, 650.0, 180.0))
ambush_encounter.set_editor_property("arming_duration", 1.0)
ambush_encounter.set_editor_property("arming_room_entry_depth", 40.0)
ambush_encounter.set_editor_property("gather_team_on_lock", False)
ambush_encounter.set_editor_property("enemies_per_wave", 1)
ambush_encounter.set_editor_property("inter_wave_delay", 1.0)
ambush_encounter.set_editor_property("reward_encounter", True)
ambush_encounter.set_editor_property("encounter_id", unreal.Name("LowerSouthLootAmbush"))
ambush_encounter.set_editor_property("loot_chest", loot_chest)
ambush_encounter.set_editor_property("controlled_gates", [gates[3]])
if hasattr(ambush_encounter, "rerun_construction_scripts"):
    ambush_encounter.rerun_construction_scripts()

pickup_specs = [
    (
        "DungeonPickup_LowerNorthMana",
        unreal.AuraPickupType.MANA,
        unreal.Vector(1500.0, 2950.0, 5.0),
    ),
    (
        "DungeonPickup_LowerSouthHealth",
        unreal.AuraPickupType.HEALTH,
        unreal.Vector(1500.0, -2950.0, 5.0),
    ),
    (
        "DungeonPickup_UpperSouthHealth",
        unreal.AuraPickupType.HEALTH,
        unreal.Vector(5100.0, -2950.0, 605.0),
    ),
    (
        "DungeonPickup_UpperSouthMana",
        unreal.AuraPickupType.MANA,
        unreal.Vector(6300.0, -2950.0, 605.0),
    ),
    (
        "DungeonPickup_BossPrepHealth",
        unreal.AuraPickupType.HEALTH,
        unreal.Vector(7200.0, 1650.0, 605.0),
    ),
    (
        "DungeonPickup_BossPrepMana",
        unreal.AuraPickupType.MANA,
        unreal.Vector(7800.0, 1650.0, 605.0),
    ),
]
for label, pickup_type, location in pickup_specs:
    pickup = spawn_actor(pickup_class, location, make_rotator(), label)
    pickup.set_editor_property("pickup_type", pickup_type)
    pickup.set_editor_property("respawns_automatically", False)
    if hasattr(pickup, "rerun_construction_scripts"):
        pickup.rerun_construction_scripts()

if not unreal.EditorLoadingAndSavingUtils.save_map(world, DUNGEON_MAP_PATH):
    raise RuntimeError("Failed to save DungeonDemo")

unreal.log_warning(
    "AURA_DUNGEON_SETUP_SUCCESS: "
    f"DungeonDemo created as a two-level branching route with {len(floor_specs)} floors, "
    f"{wall_piece_count} wall modules, 3 dead-end branches, 1 staircase, "
    "6 stair supports, 3 mandatory encounters, 2 boon rooms, 1 wave ambush, 4 gates, "
    "1 checkpoint, 2 boon caches, 1 item loot chest, 5 triggered optional enemies, "
    "6 pickups, Shaman elites, and Demon boss"
)
