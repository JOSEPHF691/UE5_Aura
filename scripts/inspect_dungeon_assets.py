import unreal


ASSET_PATHS = [
    "/Game/Assets/Dungeon/SM_Tile_6x9",
    "/Game/Assets/Dungeon/SM_Tile_6x6",
    "/Game/Assets/Dungeon/SM_Tile_4x4",
    "/Game/Assets/Dungeon/SM_Wall_4x2",
    "/Game/Assets/Dungeon/SM_Wall_2x2",
    "/Game/Assets/Dungeon/SM_Block4x4x1",
    "/Game/Assets/Dungeon/SM_Block3x3x1",
    "/Game/Assets/Dungeon/SM_Stairs",
    "/Game/Assets/Dungeon/SM_Stairs_L",
    "/Game/Assets/Dungeon/SM_Stairs_R",
    "/Game/Assets/Dungeon/SM_Corner",
    "/Game/Assets/Dungeon/Checkpoint/SM_Checkpoint",
    "/Game/Assets/Dungeon/Beacon/SM_Beacon",
    "/Game/Assets/Dungeon/Beacon/SM_Pillar",
    "/Game/Assets/Enemies/Shaman/SKM_Shaman",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_Idle",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_Walk",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_Attack",
    "/Game/Assets/Enemies/Shaman/Animations/Shaman_HitReact",
    "/Game/Assets/Enemies/Shaman/Animations/Death",
    "/Game/Assets/Enemies/Demon/SKM_Demon",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Red_Idle",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Red_Run",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Attack_R",
    "/Game/Assets/Enemies/Demon/Animations/Demon_HitReact",
]


for asset_path in ASSET_PATHS:
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not asset:
        unreal.log_error(f"AURA_DUNGEON_ASSET missing={asset_path}")
        continue

    details = [f"path={asset_path}", f"class={asset.get_class().get_name()}"]
    try:
        bounds = asset.get_bounds()
        details.append(f"origin={bounds.origin}")
        details.append(f"box_extent={bounds.box_extent}")
        details.append(f"sphere_radius={bounds.sphere_radius}")
    except Exception as error:
        details.append(f"bounds_error={error}")
    try:
        details.append(f"skeleton={asset.get_editor_property('skeleton')}")
    except Exception:
        pass
    try:
        details.append(f"play_length={asset.get_play_length()}")
    except Exception:
        pass
    unreal.log_warning("AURA_DUNGEON_ASSET " + " ".join(details))

unreal.log_warning("AURA_DUNGEON_ASSET_INSPECTION_SUCCESS")
