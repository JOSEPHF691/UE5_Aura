"""Inspect saved DungeonDemo stair collision and encounter composition without edits."""

import unreal


world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/DungeonDemo")
if not world:
    raise RuntimeError("Cannot load DungeonDemo")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def xyz(value):
    return f"({value.x:.1f},{value.y:.1f},{value.z:.1f})"


def mesh_path(actor):
    component = actor.get_editor_property("static_mesh_component")
    mesh = component.get_editor_property("static_mesh")
    return mesh.get_path_name() if mesh else "none"


for actor in sorted(actors, key=lambda entry: entry.get_actor_label()):
    if actor.get_class().get_name() != "StaticMeshActor":
        continue
    origin, extent = actor.get_actor_bounds(only_colliding_components=False)
    if not (origin.x + extent.x >= 6900 and origin.x - extent.x <= 8700
            and origin.y + extent.y >= -550 and origin.y - extent.y <= 550
            and origin.z + extent.z >= 350 and origin.z - extent.z <= 1350):
        continue
    component = actor.get_editor_property("static_mesh_component")
    unreal.log_warning(
        f"AURA_STAIRS_NEAR {actor.get_actor_label()} "
        f"location={xyz(actor.get_actor_location())} "
        f"rotation={actor.get_actor_rotation()} "
        f"scale={xyz(actor.get_actor_scale3d())} "
        f"bounds_center={xyz(origin)} bounds_half={xyz(extent)} "
        f"actor_collision={actor.get_actor_enable_collision()} "
        f"component_collision={component.get_collision_enabled()} "
        f"mesh={mesh_path(actor)}")

for actor in sorted(actors, key=lambda entry: entry.get_actor_label()):
    if actor.get_class().get_name() != "AuraEncounterVolume":
        continue
    classes = actor.get_editor_property("spawn_enemy_classes")
    enemy_class = actor.get_editor_property("enemy_class")
    unreal.log_warning(
        f"AURA_ENCOUNTER_COMPOSITION {actor.get_actor_label()} "
        f"location={xyz(actor.get_actor_location())} "
        f"type={actor.get_editor_property('encounter_type')} "
        f"profile={actor.get_editor_property('balance_profile')} "
        f"base_class={enemy_class.get_name() if enemy_class else 'none'} "
        f"classes={[item.get_name() if item else 'none' for item in classes]} "
        f"offsets={[xyz(item) for item in actor.get_editor_property('spawn_offsets')]} "
        f"wavesize={actor.get_editor_property('enemies_per_wave')}")

unreal.log_warning("AURA_STAIRS_AND_ENCOUNTERS_AUDIT_SUCCESS read_only=true")
