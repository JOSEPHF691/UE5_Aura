import unreal


SHAMAN_PATH = "/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman"
GHOUL_PATH = "/Game/Aura/Blueprints/BP_AuraEnemy"
BOSS_PATH = "/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon"
SHAMAN_CAST_ANIMATION = "/Game/Assets/Enemies/Shaman/Animations/Shaman_Summon"


def load_required(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing required asset: {path}")
    return asset


def configure_style(blueprint_path, combat_style, cast_animation=None):
    blueprint = load_required(blueprint_path)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    cdo = unreal.get_default_object(blueprint.generated_class())
    cdo.set_editor_property("combat_style", combat_style)
    if cast_animation:
        cdo.set_editor_property("ranged_cast_animation", cast_animation)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)


summon_animation = load_required(SHAMAN_CAST_ANIMATION)
configure_style(
    SHAMAN_PATH,
    unreal.AuraEnemyCombatStyle.RANGED,
    summon_animation,
)
configure_style(GHOUL_PATH, unreal.AuraEnemyCombatStyle.MELEE)
configure_style(BOSS_PATH, unreal.AuraEnemyCombatStyle.MELEE)

boss_blueprint = unreal.EditorAssetLibrary.load_asset(BOSS_PATH)
boss_cdo = unreal.get_default_object(boss_blueprint.generated_class())
boss_cdo.set_editor_property("boss_enemy", True)
unreal.BlueprintEditorLibrary.compile_blueprint(boss_blueprint)
unreal.EditorAssetLibrary.save_loaded_asset(boss_blueprint, only_if_is_dirty=False)

unreal.log_warning(
    "AURA_SHAMAN_RANGED_CONFIG_SUCCESS: Shaman=Ranged Ghoul=Melee Boss=Melee "
    f"CastAnimation={SHAMAN_CAST_ANIMATION}"
)
