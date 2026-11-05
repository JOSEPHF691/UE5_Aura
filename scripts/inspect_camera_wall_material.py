"""Read-only inspection of the mesh/material used by runtime camera wall occlusion."""

import unreal


wall = unreal.EditorAssetLibrary.load_asset("/Game/Assets/Dungeon/SM_Wall_4x2")
if not wall:
    raise RuntimeError("AURA_CAMERA_WALL missing wall mesh")

for index in range(8):
    material = wall.get_material(index)
    if not material:
        break
    unreal.log_warning(
        f"AURA_CAMERA_WALL slot={index} material={material.get_path_name()} class={material.get_class().get_name()}"
    )

unreal.log_warning("AURA_CAMERA_WALL_INSPECTION_SUCCESS")
