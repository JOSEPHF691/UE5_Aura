import unreal

path = "/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Mana"
system = unreal.load_asset(path)
configured = unreal.AuraEditorAssetLibrary.set_niagara_emitters_local_space(system, True)
saved = unreal.EditorAssetLibrary.save_loaded_asset(system, only_if_is_dirty=False)
# A second call is an idempotence check; its native log must report Changed=0.
verified = unreal.AuraEditorAssetLibrary.set_niagara_emitters_local_space(system, True)
unreal.log_warning(
    f"AURA_NIAGARA_LOCAL_SPACE configured={configured} saved={saved} verified={verified} asset={path}"
)
