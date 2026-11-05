import unreal


ASSETS = [
    "/Game/Assets/Characters/Aura/Animations/Abilities/Cast_Shock",
    "/Game/Assets/Characters/Aura/Animations/Abilities/Cast_Shock_Loop",
    "/Game/Assets/Enemies/Ghoul/Animations/Stun",
    "/Game/Assets/Enemies/Ghoul/Animations/ShockLoop",
    "/Game/Assets/Effects/Shock/NS_ElectricBeam",
    "/Game/Assets/Effects/Shock/NS_Sparks",
    "/Game/Assets/Effects/Stun/NS_Stars",
    "/Game/Assets/Sounds/Abilities/Shock/sfx_Shock",
]


for path in ASSETS:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        unreal.log_error(f"SHOCK_INSPECT missing={path}")
        continue

    details = [
        f"path={path}",
        f"class={asset.get_class().get_name()}",
    ]
    if isinstance(asset, unreal.AnimSequence):
        details.append(f"length={asset.get_play_length():.3f}")
        skeleton = asset.get_editor_property("skeleton")
        details.append(f"skeleton={skeleton.get_path_name() if skeleton else 'None'}")
    unreal.log_warning("SHOCK_INSPECT " + " ".join(details))

    if isinstance(asset, unreal.NiagaraSystem):
        names = [name for name in dir(asset) if "parameter" in name.lower() or "exposed" in name.lower()]
        unreal.log_warning(f"SHOCK_NIAGARA_API path={path} names={names}")
        try:
            store = asset.get_exposed_parameters()
            unreal.log_warning(f"SHOCK_NIAGARA_STORE path={path} store={store}")
            store_names = [name for name in dir(store) if "parameter" in name.lower() or "variable" in name.lower()]
            unreal.log_warning(f"SHOCK_NIAGARA_STORE_API path={path} names={store_names}")
        except Exception as exc:
            unreal.log_warning(f"SHOCK_NIAGARA_STORE_ERROR path={path} error={exc}")


unreal.log_warning("SHOCK_INSPECTION_SUCCESS")
