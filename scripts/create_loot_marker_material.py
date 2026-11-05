import unreal


MATERIAL_ROOT = "/Game/Aura/Materials"
GROUND_NAME = "M_LootMarkerGroundRingV3"
BEAM_NAME = "M_LootMarkerBeamGradientV1"


def connect(source, target, input_name):
    if unreal.MaterialEditingLibrary.connect_material_expressions(
        source, "", target, input_name
    ):
        return
    # Several single-input expression classes expose an unnamed pin to the
    # Python editor API even though the C++ property is called Input.
    inputs = unreal.MaterialEditingLibrary.get_material_expression_input_names(target)
    if len(inputs) == 1 and unreal.MaterialEditingLibrary.connect_material_expressions(
        source, "", target, ""
    ):
        return
    raise RuntimeError(
        f"Could not connect {source.get_name()} to {target.get_name()}:{input_name}; "
        f"available_inputs={inputs}"
    )


def create_asset(name):
    path = f"{MATERIAL_ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_warning(f"LOOT_GRADIENT_MATERIAL_ALREADY_READY path={path}")
        return None
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"Could not create {path}")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    return material


def vector_parameter(material, name, value, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, x, y
    )
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def scalar_parameter(material, name, value, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, x, y
    )
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def finish(material, asset_name):
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log_warning(
        f"LOOT_GRADIENT_MATERIAL_READY path={MATERIAL_ROOT}/{asset_name}"
    )


def create_ground_material():
    material = create_asset(GROUND_NAME)
    if material is None:
        return

    color = vector_parameter(
        material, "MarkerColor", unreal.LinearColor(1.0, 0.24, 0.04, 1.0), -1000, -260
    )
    edge_color = vector_parameter(
        material, "EdgeColor", unreal.LinearColor(1.0, 0.82, 0.55, 1.0), -1000, -150
    )
    strength = scalar_parameter(material, "EmissiveStrength", 5.0, -1000, -40)
    opacity = scalar_parameter(material, "Opacity", 0.58, -1000, 80)
    inner_radius = scalar_parameter(material, "InnerRadius", 0.24, -1000, 200)
    inner_feather = scalar_parameter(material, "InnerFeather", 0.06, -1000, 300)
    outer_scale = scalar_parameter(material, "OuterScale", 2.05, -1000, 400)

    uv = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureCoordinate, -1000, 400
    )
    center = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant2Vector, -1000, 520
    )
    center.set_editor_property("r", 0.5)
    center.set_editor_property("g", 0.5)
    centered_uv = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionSubtract, -760, 430
    )
    connect(uv, centered_uv, "A")
    connect(center, centered_uv, "B")
    distance = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLength, -560, 430
    )
    connect(centered_uv, distance, "Input")
    inner_delta = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionSubtract, -370, 400
    )
    connect(distance, inner_delta, "A")
    connect(inner_radius, inner_delta, "B")
    normalized_inner = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionDivide, -170, 400
    )
    connect(inner_delta, normalized_inner, "A")
    connect(inner_feather, normalized_inner, "B")
    inner_mask = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionSaturate, 20, 400
    )
    connect(normalized_inner, inner_mask, "Input")

    scaled_distance = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -170, 560
    )
    connect(distance, scaled_distance, "A")
    connect(outer_scale, scaled_distance, "B")
    outer_invert = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionOneMinus, 20, 560
    )
    connect(scaled_distance, outer_invert, "Input")
    outer_mask = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionSaturate, 210, 560
    )
    connect(outer_invert, outer_mask, "Input")
    falloff = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 400, 430
    )
    connect(inner_mask, falloff, "A")
    connect(outer_mask, falloff, "B")

    gradient_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, 210, -170
    )
    connect(color, gradient_color, "A")
    connect(edge_color, gradient_color, "B")
    connect(falloff, gradient_color, "Alpha")
    bright_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 450, -130
    )
    connect(gradient_color, bright_color, "A")
    connect(strength, bright_color, "B")
    emissive = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 700, -80
    )
    connect(bright_color, emissive, "A")
    connect(falloff, emissive, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    final_opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 700, 130
    )
    connect(opacity, final_opacity, "A")
    connect(falloff, final_opacity, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        final_opacity, "", unreal.MaterialProperty.MP_OPACITY
    )
    finish(material, GROUND_NAME)


def create_beam_material():
    material = create_asset(BEAM_NAME)
    if material is None:
        return

    color = vector_parameter(
        material, "MarkerColor", unreal.LinearColor(1.0, 0.24, 0.04, 1.0), -760, -240
    )
    edge_color = vector_parameter(
        material, "EdgeColor", unreal.LinearColor(1.0, 0.82, 0.55, 1.0), -760, -120
    )
    strength = scalar_parameter(material, "EmissiveStrength", 4.0, -760, 20)
    opacity = scalar_parameter(material, "Opacity", 0.48, -760, 140)

    fresnel = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionFresnel, -720, 340
    )
    fresnel.set_editor_property("exponent", 1.65)
    fresnel.set_editor_property("base_reflect_fraction", 0.0)
    facing = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionOneMinus, -470, 350
    )
    connect(fresnel, facing, "Input")
    soft_facing = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionPower, -250, 350
    )
    soft_facing.set_editor_property("const_exponent", 0.72)
    connect(facing, soft_facing, "Base")

    gradient_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, -430, -150
    )
    connect(color, gradient_color, "A")
    connect(edge_color, gradient_color, "B")
    connect(fresnel, gradient_color, "Alpha")
    bright_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -120, -110
    )
    connect(gradient_color, bright_color, "A")
    connect(strength, bright_color, "B")
    emissive = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 150, -70
    )
    connect(bright_color, emissive, "A")
    connect(soft_facing, emissive, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    final_opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 150, 130
    )
    connect(opacity, final_opacity, "A")
    connect(soft_facing, final_opacity, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        final_opacity, "", unreal.MaterialProperty.MP_OPACITY
    )
    finish(material, BEAM_NAME)


create_ground_material()
create_beam_material()
