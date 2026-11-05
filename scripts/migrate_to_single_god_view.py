import unreal


COMMON_CONTEXT_PATH = "/Game/Aura/Input/IMC_Common"
CONTROLLER_PATH = "/Game/Aura/Blueprints/BP_AuraPlayerController"
CHARACTER_PATH = "/Game/Aura/Blueprints/BP_AuraCharacter"
LEGACY_CAMERA_ASSETS = [
    "/Game/Aura/Input/IA_SwitchCamera",
    "/Game/Aura/Input/IMC_ThirdPerson",
]


editor_assets = unreal.EditorAssetLibrary


def set_legacy_property_if_present(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception:
        return False


common_context = editor_assets.load_asset(COMMON_CONTEXT_PATH)
controller_bp = editor_assets.load_asset(CONTROLLER_PATH)
character_bp = editor_assets.load_asset(CHARACTER_PATH)
if not common_context or not controller_bp or not character_bp:
    raise RuntimeError("Could not load camera migration assets")

mapping_data = common_context.get_editor_property("default_key_mappings")
old_mappings = list(mapping_data.get_editor_property("mappings"))
kept_mappings = []
removed_mappings = []
for mapping in old_mappings:
    action = mapping.get_editor_property("action")
    key = mapping.get_editor_property("key")
    key_name = str(key.get_editor_property("key_name"))
    if (action and action.get_name() == "IA_SwitchCamera") or key_name == "V":
        removed_mappings.append(mapping)
    else:
        kept_mappings.append(mapping)

mapping_data.set_editor_property("mappings", kept_mappings)
common_context.set_editor_property("default_key_mappings", mapping_data)
common_context.set_editor_property("mappings", [])

unreal.BlueprintEditorLibrary.compile_blueprint(controller_bp)
controller_cdo = unreal.get_default_object(controller_bp.generated_class())
set_legacy_property_if_present(controller_cdo, "third_person_mapping_context", None)
set_legacy_property_if_present(controller_cdo, "switch_camera_action", None)

unreal.BlueprintEditorLibrary.compile_blueprint(character_bp)
character_cdo = unreal.get_default_object(character_bp.generated_class())
top_down = character_cdo.get_editor_property("top_down_camera")
# Before the native legacy properties are removed, make any stale serialized
# mode request visually identical to God View as an additional migration guard.
set_legacy_property_if_present(character_cdo, "initial_camera_mode", unreal.AuraCameraMode.TOP_DOWN)
set_legacy_property_if_present(character_cdo, "third_person_camera", top_down)
set_legacy_property_if_present(character_cdo, "overview_camera", top_down)

for asset in [common_context, controller_bp, character_bp]:
    editor_assets.save_loaded_asset(asset, only_if_is_dirty=False)

deleted_legacy_assets = []
for asset_path in LEGACY_CAMERA_ASSETS:
    if not editor_assets.does_asset_exist(asset_path):
        continue
    referencers = editor_assets.find_package_referencers_for_asset(
        asset_path, load_assets_to_confirm=True
    )
    if referencers:
        raise RuntimeError(
            f"Cannot delete legacy camera asset {asset_path}; referencers={referencers}"
        )
    if not editor_assets.delete_asset(asset_path):
        raise RuntimeError(f"Failed to delete legacy camera asset: {asset_path}")
    deleted_legacy_assets.append(asset_path)

unreal.log_warning(
    "AURA_SINGLE_GOD_VIEW_MIGRATION_SUCCESS: "
    f"common_mappings={len(kept_mappings)} removed_camera_mappings={len(removed_mappings)} "
    f"deleted_legacy_assets={deleted_legacy_assets}"
)
