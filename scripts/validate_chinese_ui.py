"""Read-only validation of the demo's player-facing localized assets."""

import unreal


assets = unreal.EditorAssetLibrary
font_path = "/Game/Aura/UI/Fonts/F_AuraWorldChinese"
font = assets.load_asset(font_path)
if not font or font.get_editor_property("font_cache_type") != unreal.FontCacheType.OFFLINE:
    raise RuntimeError("Chinese world font is missing or is not an offline atlas")
font_chars = set(font.get_editor_property("import_options").get_editor_property("chars"))

item_count = 0
for path in assets.list_assets("/Game/Aura/Data/Items", recursive=False):
    if "/DA_Item_" not in path:
        continue
    item = assets.load_asset(path)
    for property_name in ("display_name", "description"):
        value = str(item.get_editor_property(property_name))
        if not any("\u4e00" <= char <= "\u9fff" for char in value):
            raise RuntimeError(f"Untranslated {property_name}: {path} = {value}")
        if property_name == "display_name":
            missing = {char for char in value if "\u4e00" <= char <= "\u9fff"}
            missing -= font_chars
            if missing:
                raise RuntimeError(f"World font lacks item-name glyphs: {path} = {missing}")
    item_count += 1

for blueprint_name in (
    "BP_AuraEnemy", "Dungeon/BP_AuraGoblinSpear", "Dungeon/BP_AuraShroom",
    "Dungeon/BP_AuraEliteShaman", "Dungeon/BP_AuraBossDemon",
):
    path = f"/Game/Aura/Blueprints/{blueprint_name}"
    blueprint = assets.load_asset(path)
    enemy = unreal.get_default_object(blueprint.generated_class())
    name = str(enemy.get_editor_property("enemy_display_name"))
    if not any("\u4e00" <= char <= "\u9fff" for char in name):
        raise RuntimeError(f"Untranslated enemy name: {path} = {name}")

map_path = "/Game/Map/DungeonDemo"
world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
if not world:
    raise RuntimeError(f"Could not load map: {map_path}")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
encounters = []
world_labels = []
for actor in actors:
    if actor.get_class().get_name().startswith("AuraEncounterVolume"):
        title = str(actor.get_editor_property("encounter_title"))
        if any(char.isascii() and char.isalpha() for char in title):
            raise RuntimeError(f"Untranslated encounter: {actor.get_actor_label()} = {title}")
        encounters.append(title)
    for label in actor.get_components_by_class(unreal.TextRenderComponent):
        if label.get_name() not in (
            "InteractionLabel", "RewardLabel", "CheckpointLabel",
            "EncounterLabel", "PickupLabel", "ItemLabel",
        ):
            continue
        used_font = label.get_editor_property("font")
        if not used_font or used_font.get_path_name().split(".")[0] != font_path:
            raise RuntimeError(
                f"Wrong world label font: {actor.get_actor_label()} "
                f"{label.get_name()} = {used_font}"
            )
        visible_text = str(label.get_editor_property("text"))
        missing = {char for char in visible_text if "\u4e00" <= char <= "\u9fff"}
        missing -= font_chars
        if missing:
            raise RuntimeError(
                f"World font lacks label glyphs: {actor.get_actor_label()} = {missing}"
            )
        world_labels.append(label.get_name())

if len(encounters) < 7 or len(world_labels) < 7:
    raise RuntimeError(
        f"Too few encounters or world labels: {len(encounters)} / {len(world_labels)}"
    )

unreal.log_warning(
    "AURA_CHINESE_UI_VALIDATION_SUCCESS "
    f"items={item_count} encounters={len(encounters)} world_labels={len(world_labels)} "
    f"font={font_path}"
)
