import unreal


def log(label, value):
    unreal.log_warning(f"AURA_COMBAT_DIAG {label}: {value}")


world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Map/StartupMap")
if not world:
    raise RuntimeError("Could not load StartupMap")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actor_subsystem.get_all_level_actors():
    log(
        "ACTOR",
        f"name={actor.get_name()} label={actor.get_actor_label()} "
        f"class={actor.get_class().get_name()} location={actor.get_actor_location()} "
        f"rotation={actor.get_actor_rotation()} scale={actor.get_actor_scale3d()}",
    )

asset_paths = [
    "/Game/Assets/Enemies/Ghoul/SKM_Ghoul",
    "/Game/Assets/Enemies/Ghoul/Animations/Idle",
    "/Game/Assets/Enemies/Ghoul/Animations/Walk",
    "/Game/Assets/Enemies/Ghoul/Animations/Attack_L",
    "/Game/Assets/Enemies/Ghoul/Animations/HitReact",
    "/Game/Assets/Enemies/Ghoul/Animations/Death",
]
for path in asset_paths:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing combat demo asset: {path}")
    details = [f"path={path}", f"class={asset.get_class().get_name()}"]
    for property_name in ("skeleton", "play_length"):
        try:
            details.append(f"{property_name}={asset.get_editor_property(property_name)}")
        except Exception:
            pass
    try:
        details.append(f"bounds={asset.get_bounds()}")
    except Exception:
        pass
    try:
        details.append(f"play_length_method={asset.get_play_length()}")
    except Exception:
        pass
    try:
        details.append(f"notifies={asset.get_editor_property('notifies')}")
    except Exception:
        pass
    log("ASSET", " ".join(details))

log("COMPLETE", True)
