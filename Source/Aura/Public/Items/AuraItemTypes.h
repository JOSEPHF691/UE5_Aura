// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AuraItemTypes.generated.h"

class UAuraItemDefinition;

UENUM(BlueprintType)
enum class EAuraItemType : uint8
{
	Consumable UMETA(DisplayName = "Consumable"),
	Equipment UMETA(DisplayName = "Equipment"),
	Quest UMETA(DisplayName = "Quest"),
	Material UMETA(DisplayName = "Material")
};

UENUM(BlueprintType)
enum class EAuraEquipmentSlot : uint8
{
	None UMETA(DisplayName = "None"),
	Weapon UMETA(DisplayName = "Weapon"),
	Armor UMETA(DisplayName = "Armor"),
	Head UMETA(DisplayName = "Head"),
	Shoes UMETA(DisplayName = "Shoes")
};

UENUM(BlueprintType)
enum class EAuraItemRarity : uint8
{
	Common UMETA(DisplayName = "Common"),
	Quality UMETA(DisplayName = "Quality"),
	Fine UMETA(DisplayName = "Fine"),
	Epic UMETA(DisplayName = "Epic"),
	Legendary UMETA(DisplayName = "Legendary")
};

UENUM(BlueprintType)
enum class EAuraConsumableEffect : uint8
{
	None UMETA(DisplayName = "None"),
	Health UMETA(DisplayName = "Restore Health"),
	Mana UMETA(DisplayName = "Restore Mana"),
	Respec UMETA(DisplayName = "Respec Skills at Archmage")
};

UENUM(BlueprintType)
enum class EAuraItemSourceType : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	EnemyDrop UMETA(DisplayName = "Enemy Drop"),
	Treasure UMETA(DisplayName = "Treasure"),
	PlayerDrop UMETA(DisplayName = "Player Drop"),
	Debug UMETA(DisplayName = "Debug"),
	Merchant UMETA(DisplayName = "Merchant"),
	Crafted UMETA(DisplayName = "Crafted")
};

/** One data-driven numeric modifier authored on an equipment definition or rolled item. */
USTRUCT(BlueprintType)
struct FAuraItemStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item")
	FGameplayTag StatTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item")
	float Magnitude = 0.f;

	bool operator==(const FAuraItemStatModifier& Other) const
	{
		return StatTag == Other.StatTag && FMath::IsNearlyEqual(Magnitude, Other.Magnitude);
	}
};

/** Lightweight runtime state. The definition owns static text/assets; this struct owns rolled data. */
USTRUCT(BlueprintType)
struct FAuraItemInstance
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Item")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item")
	TObjectPtr<UAuraItemDefinition> Definition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item", meta = (ClampMin = "1"))
	int32 ItemLevel = 1;

	/** Four-slot smith level. Independent of item level, rarity and rolled affixes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Item")
	int32 EnhancementLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item")
	EAuraItemRarity Rarity = EAuraItemRarity::Common;

	/** Persisted seed used to reproduce this equipment instance's affixes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Item")
	int32 RollSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aura|Item")
	TArray<FAuraItemStatModifier> RolledModifiers;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Item")
	EAuraItemSourceType SourceType = EAuraItemSourceType::Unknown;

	/** Stable profile/chest/enemy identity used by tooltips, logs, and saves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Item")
	FName SourceId = NAME_None;

	bool IsValid() const;
	bool CanStackWith(const FAuraItemInstance& Other) const;
	void EnsureIdentity();
};

namespace AuraItems
{
	AURA_API const TCHAR* GetRarityName(EAuraItemRarity Rarity);
	AURA_API FLinearColor GetRarityColor(EAuraItemRarity Rarity);
	AURA_API const TCHAR* GetEquipmentSlotName(EAuraEquipmentSlot Slot);
	AURA_API const TCHAR* GetStatName(const FGameplayTag& StatTag);
	AURA_API const TCHAR* GetSourceName(EAuraItemSourceType SourceType);
	AURA_API float GetRarityMultiplier(EAuraItemRarity Rarity);
	AURA_API int32 GetRarityAffixCount(EAuraItemRarity Rarity);
	AURA_API float GetItemLevelMultiplier(int32 ItemLevel);
	AURA_API void RollEquipmentModifiers(FAuraItemInstance& Item, int32 Seed = 0);
	AURA_API TArray<FAuraItemStatModifier> BuildFinalStatModifiers(const FAuraItemInstance& Item);
	AURA_API float GetStatMagnitude(
		const TArray<FAuraItemStatModifier>& Modifiers, const FGameplayTag& StatTag);
}
