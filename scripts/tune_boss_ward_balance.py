"""Tune only the Demon Lord balance and preview defaults; never save a map."""

import unreal


assets = unreal.EditorAssetLibrary
balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    raise RuntimeError("Missing /Game/Aura/Data/DA_CombatBalance")

boss = balance.get_editor_property("boss")
values = {
    "max_health": 575.0,
    "armor": 18.0,
    "phase_one_claw_damage": 21.0,
    "phase_two_claw_damage": 26.0,
    "tail_spin_damage": 32.0,
    "throw_damage": 28.0,
    "charge_damage": 28.0,
    "recovery_time": 0.24,
    "phase_one_attack_interval": 1.30,
    "phase_two_attack_interval": 0.97,
    "ward_health_fraction": 0.15,
    "ward_duration": 8.0,
    "ward_cooldown": 18.0,
    "ward_windup": 1.05,
    "ward_chance": 0.22,
    "ward_max_casts": 2,
}
for field, value in values.items():
    boss.set_editor_property(field, value)
balance.set_editor_property("boss", boss)
balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v2_2_BossWard"))
if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("Could not save boss balance")

blueprint = assets.load_asset("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon")
if not blueprint:
    raise RuntimeError("Missing Demon Lord Blueprint")
cdo = unreal.get_default_object(blueprint.generated_class())
cdo.set_editor_property("starting_health", 575.0)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
if not assets.save_loaded_asset(blueprint, only_if_is_dirty=False):
    raise RuntimeError("Could not save boss Blueprint preview defaults")

unreal.log_warning("AURA_BOSS_WARD_BALANCE_UPDATED Health=575 Armor=18")
