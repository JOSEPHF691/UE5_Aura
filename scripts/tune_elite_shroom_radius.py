"""Double the elite shroom's seed-field radius in the active balance asset."""

import unreal


assets = unreal.EditorAssetLibrary
balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    raise RuntimeError("Missing /Game/Aura/Data/DA_CombatBalance")

elite = balance.get_editor_property("elite_enemy")
current = elite.get_editor_property("shroom_seed_radius")
if abs(current - 120.0) < 0.01:
    elite.set_editor_property("shroom_seed_radius", 240.0)
    balance.set_editor_property("elite_enemy", elite)
    if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
        raise RuntimeError("Could not save combat balance asset")
elif abs(current - 240.0) >= 0.01:
    raise RuntimeError(f"Unexpected shroom radius {current}; refusing to overwrite user tuning")

actual = balance.get_editor_property("elite_enemy").get_editor_property(
    "shroom_seed_radius"
)
if abs(actual - 240.0) >= 0.01:
    raise RuntimeError(f"Elite shroom radius was not updated: {actual}")
unreal.log_warning(f"AURA_ELITE_SHROOM_RADIUS Old={current:.1f} New={actual:.1f}")
