"""Create the translucent blue Demon Lord ward shell without touching the map."""

import unreal


root = "/Game/Aura/Materials"
name = "M_BossWardShell"
path = f"{root}/{name}"
assets = unreal.EditorAssetLibrary

if assets.does_asset_exist(path):
    unreal.log_warning(f"AURA_BOSS_WARD_MATERIAL_EXISTS path={path}")
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, root, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not material:
        raise RuntimeError("Could not create boss ward material")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    editor = unreal.MaterialEditingLibrary
    fresnel = editor.create_material_expression(
        material, unreal.MaterialExpressionFresnel, -640, 50
    )
    fresnel.set_editor_property("exponent", 2.2)
    fresnel.set_editor_property("base_reflect_fraction", 0.015)

    color = editor.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -640, -310
    )
    color.set_editor_property("parameter_name", "WardColor")
    color.set_editor_property(
        "default_value", unreal.LinearColor(0.04, 0.48, 1.0, 1.0)
    )
    intensity = editor.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -640, -150
    )
    intensity.set_editor_property("parameter_name", "GlowIntensity")
    intensity.set_editor_property("default_value", 3.5)
    opacity = editor.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -640, 260
    )
    opacity.set_editor_property("parameter_name", "EdgeOpacity")
    opacity.set_editor_property("default_value", 0.32)

    glow = editor.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -360, -270
    )
    edge_glow = editor.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -70, -150
    )
    edge_opacity = editor.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -70, 210
    )
    for source, target, pin in (
        (color, glow, "A"),
        (intensity, glow, "B"),
        (glow, edge_glow, "A"),
        (fresnel, edge_glow, "B"),
        (opacity, edge_opacity, "A"),
        (fresnel, edge_opacity, "B"),
    ):
        if not editor.connect_material_expressions(source, "", target, pin):
            raise RuntimeError(f"Could not connect {source.get_name()} to {pin}")
    if not editor.connect_material_property(
        edge_glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Could not connect ward emissive")
    if not editor.connect_material_property(
        edge_opacity, "", unreal.MaterialProperty.MP_OPACITY
    ):
        raise RuntimeError("Could not connect ward opacity")
    editor.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Could not save boss ward material")
    unreal.log_warning(f"AURA_BOSS_WARD_MATERIAL_CREATED path={path}")
