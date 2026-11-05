import unreal


MAP_PATH = "/Game/Map/DungeonDemo"
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Could not load {MAP_PATH}")

world_settings = world.get_world_settings()
nav_config = world_settings.get_editor_property("navigation_system_config")
unreal.log_warning(
    "AURA_WORLD_NAV_CONFIG: "
    f"config={nav_config} "
    f"class_path={nav_config.get_editor_property('navigation_system_class')} "
    f"default_agent={nav_config.get_editor_property('default_agent_name')} "
    f"ai_enabled={world_settings.get_editor_property('enable_ai_system')}"
)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_actors = actor_subsystem.get_all_level_actors()
for actor in level_actors:
    if "Nav" in actor.get_class().get_name() or "Nav" in actor.get_actor_label():
        unreal.log_warning(
            "AURA_NAV_ACTOR: "
            f"label={actor.get_actor_label()} class={actor.get_class().get_name()}"
        )
        if actor.get_class().get_name() == "RecastNavMesh":
            unreal.log_warning(
                "AURA_RECAST_API: "
                + ",".join(
                    name
                    for name in dir(actor)
                    if any(token in name.lower() for token in ("tile", "build", "runtime"))
                )
            )
            try:
                unreal.log_warning(
                    "AURA_RECAST_RUNTIME_GENERATION: "
                    f"{actor.get_editor_property('runtime_generation')}"
                )
                unreal.log_warning(
                    "AURA_RECAST_FLAGS: "
                    f"can_be_main={actor.get_editor_property('can_be_main_nav_data')} "
                    f"net_load_on_client={actor.get_editor_property('net_load_on_client')} "
                    f"is_editor_only={actor.get_editor_property('is_editor_only_actor')}"
                )
            except Exception as error:
                unreal.log_warning(f"AURA_RECAST_PROPERTY_ERROR: {error}")

unreal.log_warning(
    "AURA_RECAST_PYTHON_CLASS: "
    f"available={hasattr(unreal, 'RecastNavMesh')}"
)

for actor in level_actors:
    location = actor.get_actor_location()
    if not (2800.0 <= location.x <= 5000.0):
        continue

    origin, extent = actor.get_actor_bounds(
        only_colliding_components=False,
        include_from_child_actors=False,
    )
    min_x = origin.x - extent.x
    max_x = origin.x + extent.x
    min_y = origin.y - extent.y
    max_y = origin.y + extent.y
    min_z = origin.z - extent.z
    max_z = origin.z + extent.z
    blocks_centre = (
        max_x >= 3200.0
        and min_x <= 4400.0
        and min_y <= 0.0 <= max_y
        and min_z <= 96.0 <= max_z
    )
    if (
        blocks_centre
        or "Stair" in actor.get_actor_label()
        or actor.get_actor_label() in {"DungeonFloor_Lower_02", "DungeonFloor_Upper_00"}
    ):
        component_details = ""
        try:
            component = actor.get_component_by_class(unreal.StaticMeshComponent)
            component_details = (
                f" collision={component.get_collision_enabled()}"
                f" visible={component.get_editor_property('visible')}"
            )
        except Exception:
            pass
        unreal.log_warning(
            "AURA_STAIR_ACTOR: "
            f"label={actor.get_actor_label()} "
            f"class={actor.get_class().get_name()} "
            f"location={location} rotation={actor.get_actor_rotation()} "
            f"bounds_min=({min_x:.1f},{min_y:.1f},{min_z:.1f}) "
            f"bounds_max=({max_x:.1f},{max_y:.1f},{max_z:.1f}) "
            f"blocks_centre={blocks_centre}{component_details}"
        )

for sample_x in (3400.0, 3500.0, 3700.0, 3900.0, 4100.0, 4300.0):
    hit = unreal.SystemLibrary.line_trace_single(
        world,
        unreal.Vector(sample_x, 0.0, 1100.0),
        unreal.Vector(sample_x, 0.0, -200.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
        True,
        [],
        unreal.DrawDebugTrace.NONE,
        True,
    )
    if sample_x == 3400.0:
        unreal.log_warning("AURA_HIT_RESULT_RAW: " + str(hit))
        unreal.log_warning("AURA_HIT_RESULT_API: " + ",".join(dir(hit)))
    hit_values = hit.to_tuple()
    did_hit = hit_values[0]
    hit_actor = hit_values[9] if did_hit else None
    hit_location = hit_values[4] if did_hit else unreal.Vector()
    unreal.log_warning(
        "AURA_STAIR_TRACE: "
        f"x={sample_x:.0f} hit={did_hit} "
        f"actor={hit_actor.get_actor_label() if hit_actor else 'None'} "
        f"location={hit_location}"
    )

unreal.log_warning("AURA_STAIR_INSPECTION_SUCCESS")
