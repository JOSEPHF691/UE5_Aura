import unreal

for class_name in [
    "InputAction",
    "InputMappingContext",
    "EnhancedActionKeyMapping",
    "BlueprintFactory",
    "InputModifierNegate",
    "InputModifierSwizzleAxis",
    "InputAxisSwizzle",
    "DataAssetFactory",
]:
    cls = getattr(unreal, class_name, None)
    unreal.log_warning(f"AURA_API {class_name}: {cls}")
    if cls:
        unreal.log_warning(f"AURA_API_DIR {class_name}: {dir(cls)}")

unreal.log_warning(f"AURA_VALUE_TYPES: {list(unreal.InputActionValueType)}")
unreal.log_warning(f"AURA_SWIZZLES: {list(unreal.InputAxisSwizzle)}")
