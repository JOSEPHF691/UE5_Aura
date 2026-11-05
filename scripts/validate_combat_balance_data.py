import unreal

ASSET_PATH = "/Game/Aura/Data/DA_CombatBalance"


def fail(message):
    raise RuntimeError(f"AURA_COMBAT_BALANCE_VALIDATION_FAILED: {message}")


value_errors = []


def close(actual, expected, label, tolerance=0.001):
    if abs(actual - expected) > tolerance:
        value_errors.append(f"{label} expected {expected}, got {actual}")


balance = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if not balance:
    fail(f"missing {ASSET_PATH}")
if balance.get_class() != unreal.AuraCombatBalanceData.static_class():
    fail(f"{ASSET_PATH} has class {balance.get_class().get_name()}")
if str(balance.get_editor_property("balance_version")) != "DemoBalanced_v2_4_BossBarrageAim":
    fail("unexpected balance version")

player = balance.get_editor_property("player")
progression = balance.get_editor_property("progression")
economy = balance.get_editor_property("growth_economy")
camp = balance.get_editor_property("camp_economy")
abyss = balance.get_editor_property("abyss_trial")
for field, expected in (("max_health_multiplier", 1.30),
                        ("damage_multiplier", 1.15),
                        ("soul_multiplier", 1.25),
                        ("ore_multiplier", 1.25)):
    close(abyss.get_editor_property(field), expected, f"abyss.{field}")
run_upgrades = balance.get_editor_property("run_upgrades")
fire_bolt = balance.get_editor_property("fire_bolt")
shards = balance.get_editor_property("arcane_shards")
shock = balance.get_editor_property("shock")
combo = balance.get_editor_property("combo")
skill_ranks = balance.get_editor_property("skill_ranks")
melee = balance.get_editor_property("melee_enemy")
shaman = balance.get_editor_property("shaman")
boss = balance.get_editor_property("boss")
recovery = balance.get_editor_property("recovery")
ai = balance.get_editor_property("ai")
encounters = balance.get_editor_property("encounters")

checks = [
    (player.max_health, 100.0, "player.max_health"),
    (player.max_mana, 50.0, "player.max_mana"),
    (player.combat_exit_delay, 3.0, "player.combat_exit_delay"),
    (progression.max_level, 10, "progression.max_level"),
    (progression.base_experience_to_next_level, 60, "progression.base_xp"),
    (progression.experience_growth_per_level, 40, "progression.xp_growth"),
    (progression.max_health_per_level, 10.0, "progression.health_growth"),
    (progression.max_mana_per_level, 3.0, "progression.mana_growth"),
    (progression.strength_per_level, 1.0, "progression.strength_growth"),
    (progression.intelligence_per_level, 1.0, "progression.intelligence_growth"),
    (progression.armor_per_level, 1.0, "progression.armor_growth"),
    (progression.ability_damage_bonus_per_level, 0.04, "progression.damage_growth"),
    (economy.common_kill_souls, 12, "economy.common_kill_souls"),
    (economy.elite_kill_souls, 38, "economy.elite_kill_souls"),
    (economy.boss_kill_souls, 125, "economy.boss_kill_souls"),
    (economy.elite_room_catch_up_experience, 10, "economy.elite_room_catch_up_experience"),
    (economy.seal_room_clear_souls, 20, "economy.seal_room_clear_souls"),
    (camp.potion_soul_price, 18, "camp.potion_soul_price"),
    (camp.common_equipment_soul_price, 45, "camp.common_equipment_soul_price"),
    (camp.quality_equipment_soul_price, 80, "camp.quality_equipment_soul_price"),
    (camp.respec_stone_soul_price, 100, "camp.respec_stone_soul_price"),
    (camp.respec_stone_purchase_limit, 1, "camp.respec_stone_purchase_limit"),
    (camp.sell_fraction, 0.25, "camp.sell_fraction"),
    (camp.forge_one_soul_cost, 20, "camp.forge_one_soul_cost"),
    (camp.forge_two_soul_cost, 40, "camp.forge_two_soul_cost"),
    (camp.forge_three_soul_cost, 70, "camp.forge_three_soul_cost"),
    (camp.forge_one_material_cost, 1, "camp.forge_one_material_cost"),
    (camp.forge_two_material_cost, 2, "camp.forge_two_material_cost"),
    (camp.forge_three_material_cost, 3, "camp.forge_three_material_cost"),
    (camp.weapon_base_stat_bonus_per_level, 0.05, "camp.weapon_base_stat_bonus_per_level"),
    (run_upgrades.max_rank_per_upgrade, 3, "run_upgrades.max_rank"),
    (run_upgrades.fire_bolt_damage_bonus_per_rank, 0.15, "run_upgrades.fire_bolt_damage"),
    (run_upgrades.arcane_shards_damage_bonus_per_rank, 0.20, "run_upgrades.shards_damage"),
    (run_upgrades.arcane_shards_radius_bonus_per_rank, 0.10, "run_upgrades.shards_radius"),
    (run_upgrades.shock_damage_bonus_per_rank, 0.20, "run_upgrades.shock_damage"),
    (run_upgrades.shock_stun_bonus_per_rank, 0.25, "run_upgrades.shock_stun"),
    (run_upgrades.max_health_per_rank, 20.0, "run_upgrades.max_health"),
    (run_upgrades.max_mana_per_rank, 10.0, "run_upgrades.max_mana"),
    (run_upgrades.universal_damage_bonus_per_rank, 0.08, "run_upgrades.universal_damage"),
    (fire_bolt.damage, 15.0, "fire_bolt.damage"),
    (fire_bolt.mana_cost, 0.0, "fire_bolt.mana_cost"),
    (fire_bolt.cooldown, 0.75, "fire_bolt.cooldown"),
    (shards.damage, 50.0, "arcane_shards.damage"),
    (shards.mana_cost, 18.0, "arcane_shards.mana_cost"),
    (shards.cooldown, 5.0, "arcane_shards.cooldown"),
    (shock.damage, 40.0, "shock.damage"),
    (shock.mana_cost, 14.0, "shock.mana_cost"),
    (shock.cooldown, 6.0, "shock.cooldown"),
    (shock.stun_duration, 1.75, "shock.stun_duration"),
    (combo.mark_duration, 3.0, "combo.mark_duration"),
    (combo.ember_burst_radius, 220.0, "combo.ember_burst_radius"),
    (combo.ember_burst_damage_multiplier, 0.40, "combo.ember_burst_damage_multiplier"),
    (combo.fracture_chain_radius, 300.0, "combo.fracture_chain_radius"),
    (combo.fracture_chain_damage_multiplier, 0.35, "combo.fracture_chain_damage_multiplier"),
    (combo.fracture_chain_max_targets, 2, "combo.fracture_chain_max_targets"),
    (combo.overload_radius, 180.0, "combo.overload_radius"),
    (combo.overload_damage_multiplier, 0.65, "combo.overload_damage_multiplier"),
    (skill_ranks.max_rank, 4, "skill_ranks.max_rank"),
    (skill_ranks.fire_bolt_damage_bonus, 0.10, "skill_ranks.fire_bolt_damage_bonus"),
    (skill_ranks.arcane_shards_radius_bonus, 0.10, "skill_ranks.arcane_shards_radius_bonus"),
    (skill_ranks.shock_damage_bonus, 0.10, "skill_ranks.shock_damage_bonus"),
    (skill_ranks.fire_bolt_extra_pierces, 1, "skill_ranks.fire_bolt_extra_pierces"),
    (skill_ranks.arcane_shards_field_duration, 2.0, "skill_ranks.arcane_shards_field_duration"),
    (skill_ranks.arcane_shards_field_radius, 260.0, "skill_ranks.arcane_shards_field_radius"),
    (skill_ranks.arcane_shards_slow_fraction, 0.30, "skill_ranks.arcane_shards_slow_fraction"),
    (melee.max_health, 100.0, "melee.max_health"),
    (melee.damage, 15.0, "melee.damage"),
    (melee.kill_experience_reward, 25, "melee.kill_experience_reward"),
    (melee.ghoul_frenzy_chance, 0.30, "melee.ghoul_frenzy_chance"),
    (melee.ghoul_frenzy_hit_damage, 9.0, "melee.ghoul_frenzy_hit_damage"),
    (melee.goblin_lunge_damage, 20.0, "melee.goblin_lunge_damage"),
    (melee.goblin_lunge_chance, 0.35, "melee.goblin_lunge_chance"),
    (melee.goblin_jab_step_distance, 50.0, "melee.goblin_jab_step_distance"),
    (melee.goblin_lunge_engage_range, 350.0, "melee.goblin_lunge_engage_range"),
    (melee.goblin_lunge_warning_length, 375.0, "melee.goblin_lunge_warning_length"),
    (melee.shroom_spore_damage, 22.0, "melee.shroom_spore_damage"),
    (melee.shroom_spore_radius, 285.0, "melee.shroom_spore_radius"),
    (melee.spore_exposure_duration, 2.0, "melee.spore_exposure_duration"),
    (melee.spore_exposure_slow_fraction, 0.20, "melee.spore_exposure_slow_fraction"),
    (melee.spore_detonation_damage_multiplier, 1.25, "melee.spore_detonation_damage_multiplier"),
    (shaman.max_health, 200.0, "shaman.max_health"),
    (shaman.damage, 20.0, "shaman.damage"),
    (shaman.kill_experience_reward, 75, "shaman.kill_experience_reward"),
    (shaman.volley_chance, 0.30, "shaman.volley_chance"),
    (shaman.max_normal_casts_before_volley, 4, "shaman.volley_pity"),
    (shaman.volley_projectile_count, 3, "shaman.volley_projectile_count"),
    (shaman.volley_spread_degrees, 18.0, "shaman.volley_spread_degrees"),
    (boss.max_health, 575.0, "boss.max_health"),
    (boss.phase_two_health_threshold, 0.5, "boss.phase_threshold"),
    (boss.phase_one_claw_damage, 21.0, "boss.phase_one_claw_damage"),
    (boss.tail_spin_damage, 32.0, "boss.tail_spin_damage"),
    (boss.charge_damage, 28.0, "boss.charge_damage"),
    (boss.charge_distance, 900.0, "boss.charge_distance"),
    (boss.summon_count, 2, "boss.summon_count"),
    (boss.max_active_summons, 4, "boss.max_active_summons"),
    (boss.kill_experience_reward, 250, "boss.kill_experience_reward"),
    (recovery.health_pickup_restore, 35.0, "recovery.health_pickup"),
    (recovery.mana_pickup_restore, 20.0, "recovery.mana_pickup"),
    (recovery.training_pickup_respawn_delay, 5.0, "recovery.respawn"),
    (ai.detection_radius, 1200.0, "ai.detection_radius"),
]
for actual, expected, label in checks:
    close(actual, expected, label)
if value_errors:
    fail("; ".join(value_errors))

if not (
    ai.detection_radius < ai.lose_target_radius <= ai.leash_radius
    and shaman.retreat_distance < shaman.preferred_distance <= shaman.attack_range
    and melee.goblin_lunge_travel_distance
        >= melee.goblin_lunge_engage_range - melee.goblin_lunge_hit_range
    and 0.0 <= melee.ghoul_frenzy_chance <= 1.0
    and 0.0 <= melee.goblin_lunge_chance <= 1.0
    and 0.0 <= melee.shroom_spore_chance <= 1.0
    and 0.0 <= shaman.volley_chance <= 1.0
    and 0.0 <= boss.tail_spin_chance <= 1.0
    and 0.0 <= boss.charge_chance <= 1.0
    and 0.0 <= boss.summon_chance <= 1.0
    and boss.max_active_summons >= boss.summon_count
    and boss.melee_range <= boss.tail_spin_radius <= boss.throw_range
    and fire_bolt.mana_cost == 0.0
    and shards.damage > fire_bolt.damage
    and shock.damage > fire_bolt.damage
    and shards.mana_cost * 2.0 + shock.mana_cost <= player.max_mana
):
    fail("distance or player ability role ordering is invalid")
encounter_counts = [
    encounters.outer_guard_enemy_count,
    encounters.elite_shaman_enemy_count,
    encounters.demon_lord_enemy_count,
    encounters.optional_branch_enemy_count,
]
if encounter_counts != [2, 1, 1, 8]:
    fail(f"encounter counts are not 2/1/1 mandatory plus 8 optional: got {encounter_counts}")

boss_blueprint = unreal.EditorAssetLibrary.load_asset(
    "/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon"
)
if not boss_blueprint:
    fail("missing Demon Lord Blueprint")
boss_cdo = unreal.get_default_object(boss_blueprint.generated_class())
summon_classes = boss_cdo.get_editor_property("boss_summon_classes")
if len(summon_classes) < 3 or any(enemy_class is None for enemy_class in summon_classes):
    fail("Demon Lord must have Ghoul, Goblin, and Shroom summon classes")
if not boss_cdo.get_editor_property("boss_summon_animation"):
    fail("Demon Lord summon animation is missing")
if not boss_cdo.get_editor_property("boss_charge_animation"):
    fail("Demon Lord charge animation is missing")

unreal.log_warning(
    "AURA_COMBAT_BALANCE_VALIDATION_SUCCESS: DemoBalanced_v2_4_BossBarrageAim values and invariants passed"
)
