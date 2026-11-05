"""Compare authored route quotas with the read-only audit of the saved map.

This reports missing playable encounters; it never treats target-card arithmetic
as proof that a player can earn the rewards in the current level.
"""

import argparse
from itertools import combinations
from pathlib import Path
import re

from check_m20_route_budget import SEALS, SPINE


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Source/Aura/Private/Level/AuraEncounterVolume.cpp"
DEFAULT_AUDIT = ROOT / "Saved/Logs/M22MapReadonlyAudit.log"
SPINE_IDS = {
    "OuterGuard": "M22.OuterGuard",
    "OuterPractice": "M22.OuterPractice",
    "UpperShaman": "M22.UpperShaman",
    "UpperCrossroads": "M22.UpperCrossroads",
    "MiddleInterlock": "M22.MiddleInterlock",
    "DeepHallPincer": "M22.DeepHallPincer",
    "CorePrep": "M22.CorePrep",
    "CorePractice": "M22.CorePractice",
}
SEAL_IDS = {
    "LowerNorth": "LowerNorthBoonGuardians",
    "UpperSouth": "UpperSouthBoonGuardian",
    "LowerSouth": "LowerSouthLootAmbush",
    "UpperNorth": "UpperNorthRiskGuardians",
}
SEAL_SITE_LABELS = {
    "LowerNorth": "DungeonRitual_LowerNorth",
    "UpperSouth": "DungeonRitual_UpperSouth",
    "LowerSouth": "DungeonRitual_LowerSouth",
    "UpperNorth": "DungeonRitual_UpperNorth",
}


def read_budget_cards():
    source = SOURCE.read_text(encoding="utf-8")
    matches = re.findall(r'\{TEXT\("([^"]+)"\), (\d+), (\d+)\}', source)
    result = {name: (int(xp), int(souls)) for name, xp, souls in matches}
    if len(result) != 12 or len(matches) != 12:
        raise AssertionError("Expected exactly 12 unique M22 C++ reward cards")
    for name, xp, souls in SPINE:
        if result.get(SPINE_IDS[name]) != (xp, souls):
            raise AssertionError(f"C++ reward card drift: {name}")
    for name, budget in SEALS.items():
        if result.get(SEAL_IDS[name]) != budget:
            raise AssertionError(f"C++ seal card drift: {name}")
    return result


def read_map_ids(audit_file):
    content = audit_file.read_text(encoding="utf-8", errors="replace")
    if "AURA_LAYOUT_AUDIT_SUCCESS read_only=true" not in content:
        raise AssertionError("Map audit is missing its success marker")
    room_ids = []
    for line in content.splitlines():
        if "AURA_LAYOUT_ROOM " not in line:
            continue
        found = re.search(r"AURA_LAYOUT_ROOM (\S+) id=(\S+) order=(\d+)", line)
        if not found:
            raise AssertionError(f"Could not parse room: {line}")
        _, authored_id, order = found.groups()
        if authored_id != "None":
            room_ids.append(authored_id)
        elif order == "1":
            room_ids.append("M22.OuterGuard")
        elif order == "2":
            room_ids.append("M22.UpperShaman")
    if len(room_ids) != len(set(room_ids)):
        raise AssertionError("Duplicate route reward identity in the map")
    ritual_labels = set(re.findall(r"AURA_LAYOUT_AuraRitualSite (\S+) ", content))
    return set(room_ids), ritual_labels


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--map-audit", type=Path, default=DEFAULT_AUDIT)
    parser.add_argument("--strict", action="store_true",
                        help="fail until every planned room is actually present")
    args = parser.parse_args()
    cards = read_budget_cards()
    on_map, ritual_labels = read_map_ids(args.map_audit)
    planned = set(SPINE_IDS.values()) | set(SEAL_IDS.values())
    present = planned & on_map
    missing = planned - on_map
    print("present=" + ",".join(sorted(present)))
    print("missing=" + ",".join(sorted(missing)))
    missing_sites = [name for name, label in SEAL_SITE_LABELS.items()
                     if label not in ritual_labels]
    print("missing_seal_sites=" + ",".join(sorted(missing_sites)))
    for selected in combinations(sorted(SEALS), 3):
        route = [SPINE_IDS[name] for name, _, _ in SPINE]
        route += [SEAL_IDS[name] for name in selected]
        available = [cards[name] for name in route if name in on_map]
        target = [cards[name] for name in route]
        available_xp = sum(xp for xp, _ in available)
        available_souls = sum(souls for _, souls in available)
        target_xp = sum(xp for xp, _ in target)
        target_souls = sum(souls for _, souls in target)
        omitted = (set(SEALS) - set(selected)).pop()
        print(f"omit={omitted:10s} mapped={available_xp}/{available_souls} "
              f"target={target_xp}/{target_souls} "
              f"missing={target_xp-available_xp}/{target_souls-available_souls} "
              f"three_seals_available={all(name not in missing_sites for name in selected)}")
    if missing or missing_sites:
        print("AURA_M22_ROUTE_AUDIT_PARTIAL: mainline budget or seal site is absent")
        if args.strict:
            raise SystemExit(1)
    else:
        print("AURA_M22_ROUTE_AUDIT_COMPLETE: map contains all quota nodes; "
              "runtime playthrough remains required")


if __name__ == "__main__":
    main()
