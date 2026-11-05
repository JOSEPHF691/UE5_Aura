"""Create a dither-fade copy of the dungeon wall material; do not load or save a map."""

import unreal


SOURCE = "/Game/Assets/Dungeon/Materials/TilesetEdge/M_Edge_wall"
DESTINATION = "/Game/Aura/Materials/M_DungeonWallCameraFade"
DITHER_FUNCTION = "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"

if unreal.EditorAssetLibrary.does_asset_exist(DESTINATION):
    unreal.log_warning(f"AURA_CAMERA_WALL_FADE_EXISTS path={DESTINATION}")
else:
    source = unreal.EditorAssetLibrary.load_asset(SOURCE)
    dither = unreal.EditorAssetLibrary.load_asset(DITHER_FUNCTION)
    if not source or not dither:
        raise RuntimeError("Missing source wall material or DitherTemporalAA function")

    material = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
        "M_DungeonWallCameraFade", "/Game/Aura/Materials", source
    )
    if not material:
        raise RuntimeError("Could not duplicate dungeon wall material")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -620, 760
    )
    opacity.set_editor_property("parameter_name", "OcclusionOpacity")
    opacity.set_editor_property("default_value", 1.0)
    dither_call = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMaterialFunctionCall, -360, 760
    )
    dither_call.set_editor_property("material_function", dither)

    connected = False
    for input_name in ("Alpha Threshold", "AlphaThreshold", "Alpha", "Opacity"):
        if unreal.MaterialEditingLibrary.connect_material_expressions(
            opacity, "", dither_call, input_name
        ):
            connected = True
            break
    if not connected or not unreal.MaterialEditingLibrary.connect_material_property(
        dither_call, "", unreal.MaterialProperty.MP_OPACITY_MASK
    ):
        raise RuntimeError("Could not connect dithered wall opacity mask")

    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError("Could not save camera wall fade material")
    unreal.log_warning(f"AURA_CAMERA_WALL_FADE_CREATED path={DESTINATION}")
