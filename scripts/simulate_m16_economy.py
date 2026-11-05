"""Deterministic 100-seed budget check using the authored UE balance/loot assets.

This models rewards and purchases, not combat success or run duration. It
deliberately prints only aggregate results; keep the source seeds reproducible.
"""

import random
import statistics
import unreal


assets = unreal.EditorAssetLibrary
balance = assets.load_asset("/Game/Aura/Data/DA_CombatBalance")
loot = assets.load_asset("/Game/Aura/Data/Loot/DA_LootProfiles")
if not balance or not loot:
    raise RuntimeError("M16 economy assets are missing")
economy = balance.get_editor_property("growth_economy")
camp = balance.get_editor_property("camp_economy")
progression = balance.get_editor_property("progression")


def eligible_level(xp):
    threshold = 0
    level = 1
    while level < progression.max_level:
        threshold += progression.base_experience_to_next_level + (level - 1) * progression.experience_growth_per_level
        if xp < threshold:
            break
        level += 1
    return level


def roll(profile_name, rng):
    profile = loot.get_editor_property(profile_name)
    ore = profile.guaranteed_arcane_ore + int(rng.random() < profile.bonus_arcane_ore_chance)
    rolls = profile.guaranteed_rolls + sum(
        rng.random() < profile.bonus_roll_chance for _ in range(profile.bonus_rolls)
    )
    equipment = 0
    legendary = 0
    for _ in range(rolls):
        if rng.random() >= profile.equipment_chance:
            continue
        equipment += 1
        weights = [weight.weight for weight in profile.rarity_weights]
        index = rng.choices(range(len(weights)), weights=weights, k=1)[0]
        legendary += int(index == 4)
    return ore, equipment, legendary


branches = {
    "LowerNorth": ([("common_enemy", 2)], "treasure"),
    "LowerSouth": ([("common_enemy", 2)], "treasure"),
    "UpperSouth": ([("elite_enemy", 1)], "treasure"),
    "UpperNorth": ([("elite_enemy", 1), ("common_enemy", 2)], "boss"),
}


def simulate(seed, plan, players):
    rng = random.Random(seed)
    chosen = rng.sample(sorted(branches), 3)
    results = []
    for player in range(players):
        person_rng = random.Random(seed * 1009 + player * 9176)
        xp = souls = ore = equipment = legendary = 0

        def kill(profile, count):
            nonlocal xp, souls, ore, equipment, legendary
            is_elite = profile == "elite_enemy"
            for _ in range(count):
                xp += balance.get_editor_property("shaman").kill_experience_reward if is_elite else balance.get_editor_property("melee_enemy").kill_experience_reward
                souls += economy.elite_kill_souls if is_elite else economy.common_kill_souls
                new_ore, new_equipment, new_legendary = roll(profile, person_rng)
                ore += new_ore
                equipment += new_equipment
                legendary += new_legendary

        kill("common_enemy", 3)
        kill("elite_enemy", 1)
        xp += economy.elite_room_catch_up_experience
        # Levels are automatic. Reaching L3 provides the two skill points needed
        # for the Q/E unlock route; souls remain reserved for the camp economy.
        can_unlock_qe = eligible_level(xp) >= 3
        for branch in chosen:
            enemies, chest = branches[branch]
            for profile, count in enemies:
                kill(profile, count)
            souls += economy.seal_room_clear_souls
            new_ore, new_equipment, new_legendary = roll(chest, person_rng)
            ore += new_ore
            equipment += new_equipment
            legendary += new_legendary

        can_buy_l4 = eligible_level(xp) >= 4

        # Two distinct budget choices, both after mandatory skill unlocks.
        if plan == "spell":
            desired = 2 * camp.potion_soul_price
        else:
            desired = camp.potion_soul_price + camp.common_equipment_soul_price
        bought_plan = souls >= desired
        if bought_plan:
            souls -= desired
        forged = ore >= camp.forge_one_material_cost and souls >= camp.forge_one_soul_cost
        if forged:
            ore -= camp.forge_one_material_cost
            souls -= camp.forge_one_soul_cost
        results.append((can_unlock_qe, can_buy_l4, bought_plan, forged,
                        xp, souls, ore, equipment, legendary, tuple(chosen)))
    return results


for players in (1, 2):
    for plan in ("spell", "survivor"):
        records = [record for seed in range(1, 101)
                   for record in simulate(seed, plan, players)]
        count = len(records)
        q_e = sum(row[0] for row in records)
        l4 = sum(row[1] for row in records)
        buys = sum(row[2] for row in records)
        forges = sum(row[3] for row in records)
        med_souls = statistics.median(row[5] for row in records)
        med_ore = statistics.median(row[6] for row in records)
        med_gear = statistics.median(row[7] for row in records)
        legendary = sum(row[8] for row in records)
        unreal.log_warning(
            f"AURA_M16_ECONOMY Players={players} Plan={plan} Seeds=100 Records={count} "
            f"QE={q_e}/{count} L4={l4}/{count} PlannedBuy={buys}/{count} "
            f"Forge1={forges}/{count} MedianSouls={med_souls} "
            f"MedianOre={med_ore} MedianGear={med_gear} Legendary={legendary}"
        )
        if q_e != count or l4 != count:
            raise RuntimeError("Mandatory L2/L3 or reachable L4 gated by authored rewards")

unreal.log_warning("AURA_M16_ECONOMY_SUCCESS")
