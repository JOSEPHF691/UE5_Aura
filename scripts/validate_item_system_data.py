import unreal


CATALOG_PATH = "/Game/Aura/Data/Items/DA_ItemCatalog"
EXPECTED = {
    "Consumable.HealthPotion": {
        "type": unreal.AuraItemType.CONSUMABLE,
        "stack": 5,
        "effect": unreal.AuraConsumableEffect.HEALTH,
        "restore": 35.0,
    },
    "Consumable.ManaPotion": {
        "type": unreal.AuraItemType.CONSUMABLE,
        "stack": 5,
        "effect": unreal.AuraConsumableEffect.MANA,
        "restore": 20.0,
    },
    "Consumable.RespecStone": {
        "type": unreal.AuraItemType.CONSUMABLE,
        "stack": 3,
        "effect": unreal.AuraConsumableEffect.RESPEC,
        "restore": 0.0,
    },
    "Equipment.ArcaneStaff": {
        "type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.WEAPON,
        "modifier_count": 1,
    },
    "Equipment.WardenArmor": {
        "type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.ARMOR,
        "modifier_count": 2,
    },
    "Equipment.SeerHeadpiece": {
        "type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.HEAD,
        "modifier_count": 2,
    },
    "Equipment.WindShoes": {
        "type": unreal.AuraItemType.EQUIPMENT,
        "slot": unreal.AuraEquipmentSlot.SHOES,
        "modifier_count": 2,
    },
    "Equipment.EmberweaveStaff": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.WEAPON, "modifier_count": 2},
    "Equipment.StormcallRod": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.WEAPON, "modifier_count": 2},
    "Equipment.BastionRobe": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.ARMOR, "modifier_count": 2},
    "Equipment.SpellguardMantle": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.ARMOR, "modifier_count": 2},
    "Equipment.AshenCrown": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.HEAD, "modifier_count": 2},
    "Equipment.BulwarkCowl": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.HEAD, "modifier_count": 2},
    "Equipment.FleetstepBoots": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.SHOES, "modifier_count": 1},
    "Equipment.GroundingBoots": {"type": unreal.AuraItemType.EQUIPMENT, "slot": unreal.AuraEquipmentSlot.SHOES, "modifier_count": 2},
    "Material.ArcaneOreShard": {
        "type": unreal.AuraItemType.MATERIAL,
        "stack": 20,
    },
}


def fail(message):
    raise RuntimeError(f"AURA_ITEM_VALIDATION_FAILED: {message}")


catalog = unreal.EditorAssetLibrary.load_asset(CATALOG_PATH)
if not catalog:
    fail(f"missing {CATALOG_PATH}")
if catalog.get_class() != unreal.AuraItemCatalog.static_class():
    fail(f"unexpected catalog class {catalog.get_class().get_name()}")

references = catalog.get_editor_property("items")
if len(references) != len(EXPECTED):
    fail(f"expected {len(EXPECTED)} definitions, found {len(references)}")

seen = set()
for definition in references:
    if not definition:
        fail("catalog contains a null definition")
    if definition.get_class() != unreal.AuraItemDefinition.static_class():
        fail(f"unexpected definition class {definition.get_class().get_name()}")
    item_id = str(definition.get_editor_property("item_id"))
    if item_id in seen:
        fail(f"duplicate item id {item_id}")
    seen.add(item_id)
    if item_id not in EXPECTED:
        fail(f"unexpected item id {item_id}")

    expected = EXPECTED[item_id]
    item_type = definition.get_editor_property("item_type")
    if item_type != expected["type"]:
        fail(f"{item_id} has incorrect item type")
    if not str(definition.get_editor_property("display_name")):
        fail(f"{item_id} has no display name")
    if not definition.get_editor_property("world_mesh"):
        fail(f"{item_id} has no world mesh")
    if not definition.get_editor_property("icon"):
        fail(f"{item_id} has no icon")

    if item_type == unreal.AuraItemType.CONSUMABLE:
        if definition.get_editor_property("max_stack_size") != expected["stack"]:
            fail(f"{item_id} has incorrect stack size")
        if definition.get_editor_property("consumable_effect") != expected["effect"]:
            fail(f"{item_id} has incorrect consumable effect")
        if abs(definition.get_editor_property("restore_amount") - expected["restore"]) > 0.001:
            fail(f"{item_id} has incorrect restore amount")
    elif item_type == unreal.AuraItemType.MATERIAL:
        if definition.get_editor_property("max_stack_size") != expected["stack"]:
            fail(f"{item_id} has incorrect stack size")
        if definition.get_editor_property("equipment_slot") != unreal.AuraEquipmentSlot.NONE:
            fail(f"{item_id} must not have an equipment slot")
    else:
        if definition.get_editor_property("max_stack_size") != 1:
            fail(f"{item_id} equipment must not stack")
        if definition.get_editor_property("equipment_slot") != expected["slot"]:
            fail(f"{item_id} has incorrect equipment slot")
        modifiers = definition.get_editor_property("base_stat_modifiers")
        if len(modifiers) != expected["modifier_count"]:
            fail(f"{item_id} has incorrect modifier count")
        for modifier in modifiers:
            tag_name = modifier.get_editor_property("stat_tag").export_text()
            if 'TagName="Item.Stat.' not in tag_name:
                fail(f"{item_id} has invalid stat tag {tag_name}")
            if modifier.get_editor_property("magnitude") <= 0.0:
                fail(f"{item_id} has a non-positive modifier")
        legendary_tag = {
            "Equipment.EmberweaveStaff": "Item.Power.EmberEcho",
            "Equipment.StormcallRod": "Item.Power.FractureSurge",
        }.get(item_id)
        if legendary_tag and legendary_tag not in definition.get_editor_property("item_tags").export_text():
            fail(f"{item_id} is missing its legendary Combo tag")

if seen != set(EXPECTED.keys()):
    fail(f"catalog ids differ: {seen}")

unreal.log_warning(
    "AURA_ITEM_VALIDATION_SUCCESS: sixteen definitions including twelve equipment bases passed"
)
