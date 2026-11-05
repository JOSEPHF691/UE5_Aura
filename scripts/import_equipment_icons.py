import os

import unreal


SOURCE_DIR = os.path.join(unreal.Paths.project_dir(), "SourceArt", "EquipmentIcons")
DESTINATION = "/Game/Aura/UI/Items"
ICON_NAMES = [
    "T_Equipment_ArcaneStaff",
    "T_Equipment_WardenArmor",
    "T_Equipment_SeerHeadpiece",
    "T_Equipment_WindShoes",
]


assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

if not assets.does_directory_exist(DESTINATION):
    assets.make_directory(DESTINATION)

tasks = []
for icon_name in ICON_NAMES:
    source_file = os.path.join(SOURCE_DIR, f"{icon_name}.png")
    if not os.path.isfile(source_file):
        raise RuntimeError(f"AURA_EQUIPMENT_ICON_IMPORT_FAILED: missing {source_file}")

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source_file)
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("destination_name", icon_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tasks.append(task)

asset_tools.import_asset_tasks(tasks)

imported = []
for icon_name in ICON_NAMES:
    asset_path = f"{DESTINATION}/{icon_name}"
    texture = assets.load_asset(asset_path)
    if not texture or not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(
            f"AURA_EQUIPMENT_ICON_IMPORT_FAILED: {asset_path} is not a Texture2D"
        )

    # These are transparent UI cut-outs. Keep their alpha crisp and avoid
    # world-texture streaming behaviour. Property availability varies slightly
    # across engine builds, so presentation tuning is best-effort while import
    # itself remains strict.
    try:
        texture.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON
        )
        texture.set_editor_property(
            "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
        )
        texture.set_editor_property("never_stream", True)
        texture.set_editor_property("srgb", True)
    except Exception as error:
        unreal.log_warning(f"Equipment icon UI tuning skipped for {asset_path}: {error}")

    assets.save_loaded_asset(texture, only_if_is_dirty=False)
    imported.append(asset_path)

unreal.log_warning(
    "AURA_EQUIPMENT_ICON_IMPORT_SUCCESS: " + ", ".join(imported)
)
