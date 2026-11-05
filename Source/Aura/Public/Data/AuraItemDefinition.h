// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/AuraItemTypes.h"
#include "AuraItemDefinition.generated.h"

class USoundBase;
class UStaticMesh;
class UTexture2D;

/** Static, authored data shared by every runtime instance of an item. */
UCLASS(BlueprintType)
class AURA_API UAuraItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UFUNCTION(BlueprintPure, Category = "Aura|Item")
	int32 GetMaxStackSize() const { return FMath::Max(1, MaxStackSize); }

	UFUNCTION(BlueprintPure, Category = "Aura|Item")
	bool IsStackable() const { return GetMaxStackSize() > 1; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FName ItemId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	EAuraItemType ItemType = EAuraItemType::Consumable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	EAuraEquipmentSlot EquipmentSlot = EAuraEquipmentSlot::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	EAuraItemRarity DefaultRarity = EAuraItemRarity::Common;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "1"))
	int32 MaxStackSize = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	bool bDroppable = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	EAuraConsumableEffect ConsumableEffect = EAuraConsumableEffect::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable", meta = (ClampMin = "0.0"))
	float RestoreAmount = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TArray<FAuraItemStatModifier> BaseStatModifiers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (ClampMin = "0.05"))
	float WorldMeshScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<USoundBase> UseSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer ItemTags;
};

/** Single registry used by runtime code and validation; definitions remain individual Data Assets. */
UCLASS(BlueprintType)
class AURA_API UAuraItemCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	static const FSoftObjectPath& GetDefaultAssetPath();
	static const UAuraItemCatalog& Get();

	UFUNCTION(BlueprintCallable, Category = "Aura|Item")
	UAuraItemDefinition* FindItem(FName ItemId) const;

	bool Validate(FString& OutError) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Item")
	TArray<TSoftObjectPtr<UAuraItemDefinition>> Items;
};

