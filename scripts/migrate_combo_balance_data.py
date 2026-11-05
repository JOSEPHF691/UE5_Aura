"""Add M11 combo and skill-rank tuning without overwriting existing balance fields."""

import unreal


asset_path = "/Game/Aura/Data/DA_CombatBalance"
balance = unreal.EditorAssetLibrary.load_asset(asset_path)
if not balance:
    raise RuntimeError(f"Missing combat balance data asset: {asset_path}")

combo = unreal.AuraComboBalance()
for name, value in (
    ("mark_duration", 3.0),
    ("ember_burst_radius", 220.0),
    ("ember_burst_damage_multiplier", 0.40),
    ("fracture_chain_radius", 300.0),
    ("fracture_chain_damage_multiplier", 0.35),
    ("fracture_chain_max_targets", 2),
):
    combo.set_editor_property(name, value)

balance.set_editor_property("combo", combo)
skill_ranks = unreal.AuraSkillRankBalance()
for name, value in (
    ("max_rank", 3),
    ("fire_bolt_damage_bonus", 0.10),
    ("arcane_shards_radius_bonus", 0.10),
    ("shock_damage_bonus", 0.10),
    ("fire_bolt_extra_pierces", 1),
    ("arcane_shards_field_duration", 2.0),
    ("arcane_shards_field_radius", 260.0),
    ("arcane_shards_slow_fraction", 0.30),
):
    skill_ranks.set_editor_property(name, value)
balance.set_editor_property("skill_ranks", skill_ranks)
balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v1_8_CombatRanks"))
if not unreal.EditorAssetLibrary.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError(f"Could not save {asset_path}")
unreal.log_warning("AURA_M11_BALANCE_MIGRATION_SUCCESS")
