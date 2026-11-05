import unreal


assets = unreal.EditorAssetLibrary


def require(path):
    asset = assets.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing Boss victory presentation asset: {path}")
    return asset


death_folder = "/Game/Aura/Animations/Boss"
death_path = f"{death_folder}/Demon_Death_Cinematic"
if not assets.does_directory_exist(death_folder):
    assets.make_directory(death_folder)
if not assets.does_asset_exist(death_path):
    if not assets.duplicate_asset(
        "/Game/Assets/Enemies/Demon/Animations/Demon_Stun", death_path
    ):
        raise RuntimeError("Could not create Demon_Death_Cinematic")

death_animation = require(death_path)
death_animation.set_editor_property("rate_scale", 0.9)
assets.save_loaded_asset(death_animation, only_if_is_dirty=False)

boss_blueprint = require("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon")
unreal.BlueprintEditorLibrary.compile_blueprint(boss_blueprint)
boss_cdo = unreal.get_default_object(boss_blueprint.generated_class())
boss_cdo.set_editor_property("death_animation", death_animation)
boss_cdo.set_editor_property("boss_death_animation", death_animation)
boss_cdo.set_editor_property(
    "boss_death_effect", require("/Game/Assets/Effects/Fire/NS_Fire")
)
boss_cdo.set_editor_property(
    "boss_death_sound",
    require("/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_01"),
)
boss_cdo.set_editor_property(
    "boss_death_impact_sound",
    require("/Game/Assets/Sounds/Enemies/Goblin/RockHit/SFX_RockHit_Body_03"),
)
boss_cdo.set_editor_property("boss_death_ragdoll_delay", 1.45)
boss_cdo.set_editor_property("boss_death_effect_duration", 2.8)
boss_cdo.set_editor_property("boss_death_impulse_strength", 90.0)
boss_cdo.set_editor_property("boss_corpse_lifetime", 10.0)
unreal.BlueprintEditorLibrary.compile_blueprint(boss_blueprint)
assets.save_loaded_asset(boss_blueprint, only_if_is_dirty=False)

controller_blueprint = require("/Game/Aura/Blueprints/BP_AuraPlayerController")
unreal.BlueprintEditorLibrary.compile_blueprint(controller_blueprint)
controller_cdo = unreal.get_default_object(controller_blueprint.generated_class())
controller_cdo.set_editor_property(
    "victory_sting", require("/Game/Assets/Sounds/LevelUp/SFX_LevelUp")
)
controller_cdo.set_editor_property(
    "victory_accent_sound",
    require("/Game/Assets/Sounds/UI/Unlock_Skills/SFX_UI_Unlock_NewSkill"),
)
controller_cdo.set_editor_property("victory_camera_blend_time", 0.55)
controller_cdo.set_editor_property("victory_camera_distance", 650.0)
controller_cdo.set_editor_property("victory_camera_height", 180.0)
controller_cdo.set_editor_property("victory_camera_motion_duration", 3.0)
controller_cdo.set_editor_property("victory_camera_orbit_degrees", 18.0)
controller_cdo.set_editor_property("victory_camera_fov", 48.0)
controller_cdo.set_editor_property("victory_sting_delay", 1.25)
controller_cdo.set_editor_property("victory_overlay_delay", 3.2)
unreal.BlueprintEditorLibrary.compile_blueprint(controller_blueprint)
assets.save_loaded_asset(controller_blueprint, only_if_is_dirty=False)

unreal.log_warning(
    "AURA_BOSS_VICTORY_SETUP_SUCCESS: "
    f"death={death_animation.get_path_name()} "
    f"sting={controller_cdo.get_editor_property('victory_sting').get_path_name()} "
    f"overlay_delay={controller_cdo.get_editor_property('victory_overlay_delay')}"
)
