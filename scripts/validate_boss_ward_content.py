"""Check only the current Demon Lord balance and shield presentation assets."""

import unreal


def require(condition, message):
    if not condition:
        raise RuntimeError(f"AURA_BOSS_WARD_CONTENT_FAILED: {message}")


def near(actual, expected, label):
    require(abs(actual - expected) < 0.002, f"{label}: {actual} != {expected}")


assets = unreal.EditorAssetLibrary
balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
require(balance is not None, "missing combat balance asset")
require(
    str(balance.get_editor_property("balance_version")) == "DemoBalanced_v2_2_BossWard",
    "unexpected balance version",
)
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
for field, expected in values.items():
    near(boss.get_editor_property(field), expected, field)
require(
    boss.phase_two_attack_interval >= boss.claw_impact_time + boss.recovery_time,
    "phase-two claw timing would be cooldown-clamped",
)
require(boss.ward_cooldown > boss.ward_duration, "ward can chain without a gap")

blueprint = assets.load_asset("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon")
require(blueprint is not None, "missing boss Blueprint")
cdo = unreal.get_default_object(blueprint.generated_class())
near(cdo.get_editor_property("starting_health"), boss.max_health, "preview health")
require(cdo.get_editor_property("boss_summon_animation") is not None,
        "ward cast has no animation")

material = assets.load_asset("/Game/Aura/Materials/M_BossWardShell")
require(material is not None, "missing ward shell material")
require(material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT,
        "ward shell is not translucent")
unreal.log_warning("AURA_BOSS_WARD_CONTENT_PASSED")
