"""Apply the M21 ten-level curve without replacing unrelated balance fields."""

import unreal


asset_path = "/Game/Aura/Data/DA_CombatBalance"
asset = unreal.EditorAssetLibrary.load_asset(asset_path)
if not asset:
    raise RuntimeError(f"Missing {asset_path}")

progression = asset.get_editor_property("progression")
for name, value in (
    ("max_level", 10),
    ("experience_to_next_level", [60, 100, 140, 180, 210, 230, 250, 270, 290]),
    ("max_health_per_level", 10.0),
    ("max_mana_per_level", 3.0),
    ("strength_per_level", 1.0),
    ("intelligence_per_level", 1.0),
    ("ability_damage_bonus_per_level", 0.04),
):
    progression.set_editor_property(name, value)
asset.set_editor_property("progression", progression)

combo = asset.get_editor_property("combo")
for name, value in (
    ("ember_extra_stack_damage_multiplier", 0.12),
    ("ember_max_stacks", 3),
    ("fracture_stagger_duration", 0.45),
    ("fracture_stagger_immunity", 4.0),
    ("fracture_boss_damage_reduction", 0.08),
    ("fracture_boss_weaken_duration", 2.0),
    ("fracture_boss_weaken_cooldown", 6.0),
    ("overload_buff_duration", 4.0),
    ("overload_move_speed_bonus_fraction", 0.12),
    ("overload_buff_cooldown", 8.0),
):
    combo.set_editor_property(name, value)
asset.set_editor_property("combo", combo)

asset.set_editor_property("balance_version", unreal.Name("DemoBalanced_v3_0_TenLevel"))

if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
    raise RuntimeError(f"Could not save {asset_path}")
unreal.log_warning("AURA_M21_GROWTH_ASSET_SUCCESS")
