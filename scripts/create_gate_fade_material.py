"""Create a masked, temporally dithered copy of the existing dungeon gate material.

Only creates /Game/Aura/Materials/M_DungeonGateFade. Never loads or saves a map.
"""

import unreal


SOURCE = "/Game/Assets/Dungeon/Materials/Tileset1x1/Tilese1x1_2/M_Tilese1x1_2"
DESTINATION = "/Game/Aura/Materials/M_DungeonGateFade"
DITHER_FUNCTION = "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"

if unreal.EditorAssetLibrary.does_asset_exist(DESTINATION):
    raise RuntimeError(f"Destination already exists; will not overwrite: {DESTINATION}")

source = unreal.EditorAssetLibrary.load_asset(SOURCE)
dither = unreal.EditorAssetLibrary.load_asset(DITHER_FUNCTION)
if not source or not dither:
    raise RuntimeError("Missing source gate material or engine dither function")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
material = asset_tools.duplicate_asset(
    "M_DungeonGateFade", "/Game/Aura/Materials", source)
if not material:
    raise RuntimeError("Gate material duplication failed")

material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
parameter = unreal.MaterialEditingLibrary.create_material_expression(
    material, unreal.MaterialExpressionScalarParameter, -620, 720)
parameter.set_editor_property("parameter_name", "GateOpacity")
parameter.set_editor_property("default_value", 1.0)

call = unreal.MaterialEditingLibrary.create_material_expression(
    material, unreal.MaterialExpressionMaterialFunctionCall, -360, 720)
call.set_editor_property("material_function", dither)

connected_input = None
for name in ("Alpha Threshold", "AlphaThreshold", "Alpha", "Opacity"):
    if unreal.MaterialEditingLibrary.connect_material_expressions(
            parameter, "", call, name):
        connected_input = name
        break
if not connected_input:
    raise RuntimeError("Could not connect GateOpacity to DitherTemporalAA")
if not unreal.MaterialEditingLibrary.connect_material_property(
        call, "", unreal.MaterialProperty.MP_OPACITY_MASK):
    raise RuntimeError("Could not connect dither output to Opacity Mask")

unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError("Could not save gate fade material")

unreal.log_warning(
    f"AURA_GATE_FADE_CREATED path={DESTINATION} input={connected_input} "
    f"blend={material.get_editor_property('blend_mode')}")
