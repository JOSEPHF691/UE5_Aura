"""Check the M20 *target* room budget for every three-of-four seal route.

These are authored M21/M22 reward quotas, not rewards already present in the
map or current DA_CombatBalance. Never report this check as a playable economy
test; use check_m22_live_route_budget.py to compare them with the saved map.
"""

from itertools import combinations


# Each entry is (space name, guaranteed personal XP, guaranteed personal souls).
# Neither random loot nor Boss-kill rewards count toward the pre-Boss guarantee.
SPINE = (
    ("OuterGuard", 90, 65),
    ("OuterPractice", 150, 110),
    ("UpperShaman", 180, 140),
    ("UpperCrossroads", 120, 100),
    ("MiddleInterlock", 120, 115),
    ("DeepHallPincer", 210, 165),
    ("CorePrep", 170, 140),
    ("CorePractice", 180, 145),
)
SEALS = {
    "LowerNorth": (170, 110),
    "UpperSouth": (180, 125),
    "LowerSouth": (190, 135),
    "UpperNorth": (240, 175),
}

# Recheck the specific safe-node purchasing promises in plan §21.4.
LEVELS = {
    3: (160, 30 + 40),
    5: (480, 30 + 40 + 75 + 100),
    6: (690, 30 + 40 + 75 + 100 + 120),
    10: (1730, 1095),
}
XP_TARGET = 1730
SOUL_TARGET = 1350


def total(entries):
    return tuple(sum(entry[index] for entry in entries) for index in (1, 2))


def require_budget(name, actual, minimum):
    if actual[0] < minimum[0] or actual[1] < minimum[1]:
        raise AssertionError(f"{name}: {actual} falls below {minimum}")


if __name__ == "__main__":
    require_budget("outer safe node", total(SPINE[:2]), LEVELS[3])
    require_budget("middle safe node", total(SPINE[:5]), LEVELS[5])
    require_budget("deep safe node", total(SPINE[:6]), LEVELS[6])
    spine_xp, spine_souls = total(SPINE)
    assert len(SEALS) == 4
    for chosen in combinations(sorted(SEALS), 3):
        xp = spine_xp + sum(SEALS[seal][0] for seal in chosen)
        souls = spine_souls + sum(SEALS[seal][1] for seal in chosen)
        omitted = (set(SEALS) - set(chosen)).pop()
        require_budget(f"omit {omitted}", (xp, souls), (XP_TARGET, SOUL_TARGET))
        assert souls - LEVELS[10][1] >= 255
        print(f"omit={omitted:10s} pre_boss_xp={xp:4d} "
              f"pre_boss_souls={souls:4d} post_upgrade_souls={souls - 1095:3d}")
    print("AURA_M20_BUDGET_PLAN_SUCCESS: target quotas only; not a playable route test")
