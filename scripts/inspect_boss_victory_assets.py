import unreal


def describe(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        unreal.log_error(f"AURA_BOSS_VICTORY_ASSET missing={path}")
        return

    fields = [f"path={path}", f"class={asset.get_class().get_name()}"]
    for property_name in ("skeleton", "rate_scale", "sequence_length", "duration"):
        try:
            fields.append(f"{property_name}={asset.get_editor_property(property_name)}")
        except Exception:
            pass
    try:
        fields.append(f"play_length={asset.get_play_length():.3f}")
    except Exception:
        pass
    unreal.log_warning("AURA_BOSS_VICTORY_ASSET " + " ".join(fields))


for asset_path in (
    "/Game/Assets/Enemies/Demon/Animations/Demon_Stun",
    "/Game/Assets/Enemies/Demon/Animations/Demon_HitReact",
    "/Game/Assets/Enemies/Demon/Animations/Demon_Throw",
    "/Game/Assets/Enemies/Ghoul/Animations/Death",
    "/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_01",
    "/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_02",
    "/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_03",
    "/Game/Assets/Sounds/LevelUp/SFX_LevelUp",
    "/Game/Assets/Sounds/UI/Unlock_Skills/SFX_UI_Unlock_NewSkill",
):
    describe(asset_path)

unreal.log_warning("AURA_BOSS_VICTORY_ASSET COMPLETE")
