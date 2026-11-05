"""Create an inverted-hull gold outline as a mesh overlay, without map edits."""

import unreal


ROOT = "/Game/Aura/Materials"
NAME = "M_InteractableGoldOutlineV2"
PATH = f"{ROOT}/{NAME}"


def connect(source, target, pin):
    if unreal.MaterialEditingLibrary.connect_material_expressions(source, "", target, pin):
        return
    inputs = unreal.MaterialEditingLibrary.get_material_expression_input_names(target)
    if len(inputs) == 1 and unreal.MaterialEditingLibrary.connect_material_expressions(
        source, "", target, ""
    ):
        return
    raise RuntimeError(f"Could not connect {source.get_name()} to {pin}: {inputs}")


def multiply(material, a, b, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, x, y
    )
    connect(a, node, "A")
    connect(b, node, "B")
    return node


def scalar(material, name, value, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y
    )
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    unreal.log_warning(f"AURA_INTERACTABLE_OUTLINE_EXISTS path={PATH}")
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        NAME, ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not material:
        raise RuntimeError("Could not create outline material")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_skeletal_mesh", True)

    normal = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexNormalWS, -850, -450
    )
    thickness = scalar(material, "OutlineThicknessCm", 3.0, -850, -310)
    offset = multiply(material, normal, thickness, -560, -390)
    if not unreal.MaterialEditingLibrary.connect_material_property(
        offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
    ):
        raise RuntimeError("Could not connect vertex offset")

    side = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTwoSidedSign, -850, -20
    )
    negative_side = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionOneMinus, -620, -20
    )
    connect(side, negative_side, "Input")
    half = scalar(material, "Half", 0.5, -850, 130)
    back_face = multiply(material, negative_side, half, -380, -10)

    time = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTime, -850, 310
    )
    speed = scalar(material, "PulseSpeed", 0.4, -850, 450)
    phase = multiply(material, time, speed, -620, 330)
    sine = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionSine, -390, 330
    )
    connect(phase, sine, "Input")
    pulse_amount = scalar(material, "PulseAmount", 0.16, -620, 510)
    varying = multiply(material, sine, pulse_amount, -170, 350)
    pulse_floor = scalar(material, "PulseFloor", 0.82, -390, 530)
    pulse = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAdd, 40, 350
    )
    connect(varying, pulse, "A")
    connect(pulse_floor, pulse, "B")
    outline_mask = multiply(material, back_face, pulse, 260, 140)

    color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -150, -480
    )
    color.set_editor_property("parameter_name", "OutlineColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.72, 0.08, 1.0))
    intensity = scalar(material, "GlowIntensity", 4.0, -150, -330)
    bright = multiply(material, color, intensity, 60, -350)
    emissive = multiply(material, bright, outline_mask, 480, -170)
    alpha = scalar(material, "OutlineOpacity", 0.83, 60, 560)
    opacity = multiply(material, outline_mask, alpha, 480, 240)
    if not unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Could not connect emissive color")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    ):
        raise RuntimeError("Could not connect opacity")

    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError("Could not save outline material")
    unreal.log_warning(f"AURA_INTERACTABLE_OUTLINE_CREATED path={PATH}")
