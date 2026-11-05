// Copyright yosep

#include "Data/AuraItemDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraItems, Log, All);

FPrimaryAssetId UAuraItemDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("AuraItem"), ItemId.IsNone() ? GetFName() : ItemId);
}

const FSoftObjectPath& UAuraItemCatalog::GetDefaultAssetPath()
{
	static const FSoftObjectPath AssetPath(TEXT("/Game/Aura/Data/Items/DA_ItemCatalog.DA_ItemCatalog"));
	return AssetPath;
}

const UAuraItemCatalog& UAuraItemCatalog::Get()
{
	static TSoftObjectPtr<UAuraItemCatalog> CatalogAsset(GetDefaultAssetPath());
	if (const UAuraItemCatalog* LoadedCatalog = CatalogAsset.LoadSynchronous())
	{
		return *LoadedCatalog;
	}

	static bool bLoggedMissingCatalog = false;
	if (!bLoggedMissingCatalog)
	{
		bLoggedMissingCatalog = true;
		UE_LOG(LogAuraItems, Warning,
			TEXT("Item catalog is missing at %s; item lookup will be empty."),
			*GetDefaultAssetPath().ToString());
	}
	return *GetDefault<UAuraItemCatalog>();
}

UAuraItemDefinition* UAuraItemCatalog::FindItem(const FName ItemId) const
{
	if (ItemId.IsNone()) return nullptr;
	for (const TSoftObjectPtr<UAuraItemDefinition>& ItemReference : Items)
	{
		UAuraItemDefinition* Definition = ItemReference.LoadSynchronous();
		if (Definition && Definition->ItemId == ItemId) return Definition;
	}
	return nullptr;
}

bool UAuraItemCatalog::Validate(FString& OutError) const
{
	TArray<FString> Errors;
	TSet<FName> SeenIds;
	for (const TSoftObjectPtr<UAuraItemDefinition>& ItemReference : Items)
	{
		const UAuraItemDefinition* Definition = ItemReference.LoadSynchronous();
		if (!Definition)
		{
			Errors.Add(FString::Printf(TEXT("Missing definition: %s"), *ItemReference.ToString()));
			continue;
		}
		if (Definition->ItemId.IsNone())
		{
			Errors.Add(FString::Printf(TEXT("%s has no ItemId"), *Definition->GetName()));
		}
		else if (SeenIds.Contains(Definition->ItemId))
		{
			Errors.Add(FString::Printf(TEXT("Duplicate ItemId: %s"), *Definition->ItemId.ToString()));
		}
		SeenIds.Add(Definition->ItemId);
		if (Definition->DisplayName.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("%s has no display name"), *Definition->GetName()));
		}
		if (Definition->ItemType == EAuraItemType::Consumable
			&& (Definition->ConsumableEffect == EAuraConsumableEffect::None
				|| (Definition->ConsumableEffect != EAuraConsumableEffect::Respec
					&& Definition->RestoreAmount <= 0.f)))
		{
			Errors.Add(FString::Printf(TEXT("Consumable %s has no valid use effect"),
				*Definition->ItemId.ToString()));
		}
		if (Definition->ItemType == EAuraItemType::Equipment
			&& Definition->EquipmentSlot == EAuraEquipmentSlot::None)
		{
			Errors.Add(FString::Printf(TEXT("Equipment %s has no equipment slot"),
				*Definition->ItemId.ToString()));
		}
		if (Definition->ItemType != EAuraItemType::Consumable
			&& Definition->ItemType != EAuraItemType::Material
			&& Definition->GetMaxStackSize() != 1)
		{
			Errors.Add(FString::Printf(TEXT("Non-consumable %s must not stack"),
				*Definition->ItemId.ToString()));
		}
		if (Definition->ItemType == EAuraItemType::Material
			&& (Definition->EquipmentSlot != EAuraEquipmentSlot::None
				|| Definition->ConsumableEffect != EAuraConsumableEffect::None
				|| Definition->GetMaxStackSize() <= 1))
		{
			Errors.Add(FString::Printf(TEXT("Material %s has invalid rules"),
				*Definition->ItemId.ToString()));
		}
	}
	OutError = FString::Join(Errors, TEXT("; "));
	return Errors.IsEmpty();
}

