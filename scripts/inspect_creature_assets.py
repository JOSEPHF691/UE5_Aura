import unreal


ASSET_PATHS = [
    "/Game/Assets/Enemies/Goblin/SKM_Goblin",
    "/Game/Assets/Enemies/Goblin/Spear/SKM_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Idle_Spear2",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Run_Spear_fix",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Attack_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/HitReact_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Death_Spear",
    "/Game/Assets/Enemies/Goblin/Animations/Spear/Stun_Loop",
    "/Game/Assets/Enemies/Shroom/Mesh/SKM_Shroom",
    "/Game/Assets/Enemies/Shroom/Animations/Idle",
    "/Game/Assets/Enemies/Shroom/Animations/Bounce",
    "/Game/Assets/Enemies/Shroom/Animations/Attack",
]


for asset_path in ASSET_PATHS:
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not asset:
        unreal.log_error(f"AURA_CREATURE_ASSET missing={asset_path}")
        continue

    details = [f"path={asset_path}", f"class={asset.get_class().get_name()}"]
    try:
        bounds = asset.get_bounds()
        details.append(f"origin={bounds.origin}")
        details.append(f"extent={bounds.box_extent}")
    except Exception as error:
        details.append(f"bounds_error={error}")
    try:
        details.append(f"skeleton={asset.get_editor_property('skeleton').get_path_name()}")
    except Exception:
        pass
    try:
        details.append(f"length={asset.get_play_length():.3f}")
    except Exception:
        pass
    unreal.log_warning("AURA_CREATURE_ASSET " + " ".join(details))


for skeleton_path in [
    "/Game/Assets/Enemies/Goblin/SK_Goblin",
    "/Game/Assets/Enemies/Shroom/Mesh/SK_Shroom",
]:
    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    if not skeleton:
        continue
    try:
        sockets = skeleton.get_editor_property("sockets")
        socket_names = [str(socket.get_editor_property("socket_name")) for socket in sockets]
        unreal.log_warning(
            f"AURA_CREATURE_SOCKETS path={skeleton_path} sockets={socket_names}"
        )
    except Exception as error:
        unreal.log_warning(
            f"AURA_CREATURE_SOCKETS path={skeleton_path} unavailable={error}"
        )
    unreal.log_warning(
        f"AURA_CREATURE_SOCKET_API path={skeleton_path} "
        f"members={[name for name in dir(skeleton) if 'socket' in name.lower()]}"
    )


unreal.log_warning("AURA_CREATURE_ASSET_INSPECTION_SUCCESS")

for subsystem_name in ("SkeletalMeshEditorSubsystem", "AnimationBlueprintEditorSubsystem"):
    subsystem_type = getattr(unreal, subsystem_name, None)
    if subsystem_type:
        try:
            subsystem = unreal.get_editor_subsystem(subsystem_type)
            unreal.log_warning(
                f"AURA_CREATURE_SUBSYSTEM_API type={subsystem_name} "
                f"members={[name for name in dir(subsystem) if 'bone' in name.lower() or 'socket' in name.lower()]}"
            )
        except Exception as error:
            unreal.log_warning(
                f"AURA_CREATURE_SUBSYSTEM_API type={subsystem_name} error={error}"
            )
