"""Update only the new phase-two boss barrage tuning; do not save a map."""

import unreal


assets = unreal.EditorAssetLibrary
balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
if not balance:
    raise RuntimeError("Missing /Game/Aura/Data/DA_CombatBalance")

boss = balance.get_editor_property("boss")
values = {
    "barrage_shot_damage": 5.0,
    "barrage_windup": 0.95,
    "barrage_shot_interval": 0.20,
    "barrage_shot_count": 12,
    "barrage_half_angle_degrees": 27.0,
    "barrage_aim_turn_rate_degrees_per_second": 45.0,
    "barrage_range": 1150.0,
    "barrage_cooldown": 12.0,
    "barrage_chance": 0.32,
    "barrage_recovery": 0.55,
}
for field, value in values.items():
    boss.set_editor_property(field, value)
balance.set_editor_property("boss", boss)
balance.set_editor_property("balance_version", unreal.Name("DemoBalanced_v2_4_BossBarrageAim"))
if not assets.save_loaded_asset(balance, only_if_is_dirty=False):
    raise RuntimeError("Could not save boss balance")

for field, expected in values.items():
    actual = balance.get_editor_property("boss").get_editor_property(field)
    if abs(actual - expected) > 0.001:
        raise RuntimeError(f"Barrage balance mismatch: {field}={actual}, expected {expected}")
unreal.log_warning("AURA_BOSS_BARRAGE_BALANCE_UPDATED Shots=12 Damage=5 AimTurn=45")
