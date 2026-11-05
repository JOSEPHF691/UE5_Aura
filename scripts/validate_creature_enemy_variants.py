import unreal


BLUEPRINT_DIR = "/Game/Aura/Blueprints/Dungeon"
DUNGEON_MAP = "/Game/Map/DungeonDemo"

assets = unreal.EditorAssetLibrary
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def path_name(value):
    return value.get_path_name() if value else "None"


def validate_enemy(
    blueprint_name,
    mesh_path,
    idle_path,
    attack_path,
    expected_display_name,
    expected_archetype,
    expected_radius,
    expected_half_height,
    expected_mesh_scale,
    weapon_mesh_path=None,
    weapon_socket="None",
):
    blueprint_path = f"{BLUEPRINT_DIR}/{blueprint_name}"
    blueprint = assets.load_asset(blueprint_path)
    require(blueprint is not None, f"Missing blueprint: {blueprint_path}")

    generated_class = blueprint.generated_class()
    require(generated_class is not None, f"Missing generated class: {blueprint_path}")
    cdo = unreal.get_default_object(generated_class)

    mesh = cdo.get_editor_property("mesh")
    configured_mesh = mesh.get_skeletal_mesh_asset()
    require(
        path_name(configured_mesh) == mesh_path,
        f"{blueprint_name} mesh mismatch: {path_name(configured_mesh)}",
    )
    require(
        path_name(cdo.get_editor_property("idle_animation")) == idle_path,
        f"{blueprint_name} idle animation mismatch",
    )
    require(
        path_name(cdo.get_editor_property("attack_animation")) == attack_path,
        f"{blueprint_name} attack animation mismatch",
    )
    require(
        str(cdo.get_editor_property("enemy_display_name")) == expected_display_name,
        f"{blueprint_name} display name mismatch",
    )
    require(
        cdo.get_editor_property("enemy_archetype") == expected_archetype,
        f"{blueprint_name} attack archetype mismatch",
    )
    require(
        path_name(cdo.get_editor_property("enemy_weapon_mesh"))
        == (weapon_mesh_path or "None"),
        f"{blueprint_name} weapon mesh mismatch",
    )
    require(
        str(cdo.get_editor_property("enemy_weapon_socket")) == weapon_socket,
        f"{blueprint_name} weapon socket mismatch",
    )

    capsule = cdo.get_editor_property("capsule_component")
    radius = capsule.get_unscaled_capsule_radius()
    half_height = capsule.get_unscaled_capsule_half_height()
    mesh_scale = mesh.get_editor_property("relative_scale3d")
    require(
        abs(radius - expected_radius) < 0.01,
        f"{blueprint_name} capsule radius mismatch: {radius}",
    )
    require(
        abs(half_height - expected_half_height) < 0.01,
        f"{blueprint_name} capsule half height mismatch: {half_height}",
    )
    require(
        abs(mesh_scale.x - expected_mesh_scale) < 0.01
        and abs(mesh_scale.y - expected_mesh_scale) < 0.01
        and abs(mesh_scale.z - expected_mesh_scale) < 0.01,
        f"{blueprint_name} mesh scale mismatch: {mesh_scale}",
    )
    # The player FireBolt travels at roughly 99uu with an 18uu collision sphere.
    # Keep the top of every small-enemy capsule comfortably above its lower edge.
    require(
        half_height * 2.0 >= 120.0,
        f"{blueprint_name} hit capsule is too short for horizontal FireBolt shots",
    )
    return {
        "class": generated_class,
        "mesh": path_name(configured_mesh),
        "radius": radius,
        "half_height": half_height,
        "mesh_scale": mesh_scale,
        "weapon": path_name(cdo.get_editor_property("enemy_weapon_mesh")),
        "socket": str(cdo.get_editor_property("enemy_weapon_socket")),
    }


goblin = validate_enemy(
    "BP_AuraGoblinSpear",
    "/Game/Assets/Enemies/Goblin/SKM_Goblin.SKM_Goblin",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Idle_Spear2.Idle_Spear2",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Attack_Spear.Attack_Spear",
    "GOBLIN SPEARMAN",
    unreal.AuraEnemyArchetype.GOBLIN_SPEARMAN,
    42.0,
    76.0,
    1.00,
    "/Game/Assets/Enemies/Goblin/Spear/SKM_Spear.SKM_Spear",
    "RSocket",
)

shroom = validate_enemy(
    "BP_AuraShroom",
    "/Game/Assets/Enemies/Shroom/Mesh/SKM_Shroom.SKM_Shroom",
    "/Game/Assets/Enemies/Shroom/Animations/Idle.Idle",
    "/Game/Assets/Enemies/Shroom/Animations/Attack.Attack",
    "CAVE SHROOM",
    unreal.AuraEnemyArchetype.CAVE_SHROOM,
    60.0,
    90.0,
    1.00,
)

regular = assets.load_asset("/Game/Aura/Blueprints/BP_AuraEnemy")
require(regular is not None, "Missing regular enemy blueprint")

expected_encounters = {
    "DungeonEncounter_OuterGuard": [
        goblin["class"].get_path_name(),
        shroom["class"].get_path_name(),
        regular.generated_class().get_path_name(),
    ],
    "DungeonRewardEncounter_LowerNorth": [
        shroom["class"].get_path_name(),
        goblin["class"].get_path_name(),
    ],
    "DungeonRewardEncounter_LowerSouthAmbush": [
        shroom["class"].get_path_name(),
        goblin["class"].get_path_name(),
    ],
}

world = unreal.EditorLoadingAndSavingUtils.load_map(DUNGEON_MAP)
require(world is not None, f"Could not load map: {DUNGEON_MAP}")

found = {}
for actor in actor_subsystem.get_all_level_actors():
    label = actor.get_actor_label()
    if label not in expected_encounters:
        continue
    actual = [path_name(enemy_class) for enemy_class in actor.get_editor_property("spawn_enemy_classes")]
    require(
        actual == expected_encounters[label],
        f"{label} class mix mismatch: expected={expected_encounters[label]} actual={actual}",
    )
    found[label] = actual

require(set(found) == set(expected_encounters), f"Missing encounter actors: {sorted(set(expected_encounters) - set(found))}")

unreal.log_warning(
    "AURA_CREATURE_VARIANTS_VALIDATION_SUCCESS "
    f"goblin={goblin} shroom={shroom} encounters={found}"
)
