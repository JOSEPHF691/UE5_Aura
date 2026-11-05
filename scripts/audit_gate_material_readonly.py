"""Read-only report of the dungeon gate mesh and its assigned materials."""

import unreal

mesh = unreal.EditorAssetLibrary.load_asset("/Game/Assets/Dungeon/SM_Block4x4x1")
if not mesh:
    raise RuntimeError("Gate mesh asset not found")
unreal.log_warning(f"AURA_GATE_MESH slots={mesh.get_num_sections(0)}")
for index in range(len(mesh.get_editor_property("static_materials"))):
    material = mesh.get_material(index)
    unreal.log_warning(
        f"AURA_GATE_MATERIAL index={index} path={material.get_path_name() if material else 'none'} "
        f"class={material.get_class().get_name() if material else 'none'}")
    if material and material.get_class().get_name() == "Material":
        unreal.log_warning(
            f"AURA_GATE_BLEND index={index} blend={material.get_editor_property('blend_mode')}")
unreal.log_warning("AURA_GATE_AUDIT_SUCCESS read_only=true")

function = unreal.EditorAssetLibrary.load_asset(
    "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA")
unreal.log_warning(
    f"AURA_GATE_DITHER function={function.get_path_name() if function else 'none'}")
if function:
    try:
        for expression in function.get_editor_property("function_expressions"):
            if expression.get_class().get_name() in (
                "MaterialExpressionFunctionInput", "MaterialExpressionFunctionOutput"
            ):
                unreal.log_warning(
                    f"AURA_GATE_DITHER_IO class={expression.get_class().get_name()} "
                    f"name={expression.get_editor_property('input_name') if expression.get_class().get_name().endswith('Input') else expression.get_editor_property('output_name')}")
    except Exception as exc:
        unreal.log_warning(f"AURA_GATE_DITHER_IO_UNAVAILABLE {exc}")
