// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/AuraItemTypes.h"
#include "AuraLootProfileData.generated.h"

UENUM(BlueprintType)
enum class EAuraLootProfileType : uint8
{
	Auto UMETA(DisplayName = "Auto From Enemy"),
	None UMETA(DisplayName = "No Loot"),
	CommonEnemy UMETA(DisplayName = "Common Enemy"),
	EliteEnemy UMETA(DisplayName = "Elite Enemy"),
	SealGuardian UMETA(DisplayName = "Seal / Boon Guardian"),
	Boss UMETA(DisplayName = "Boss"),
	Treasure UMETA(DisplayName = "Treasure")
};

USTRUCT(BlueprintType)
struct FAuraLootRarityWeight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	EAuraItemRarity Rarity = EAuraItemRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0.0"))
	float Weight = 0.f;
};

USTRUCT(BlueprintType)
struct FAuraLootProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FName ProfileId = NAME_None;

	/** Always executed for each eligible player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0", ClampMax = "10"))
	int32 GuaranteedRolls = 0;

	/** Each bonus roll independently uses BonusRollChance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0", ClampMax = "10"))
	int32 BonusRolls = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BonusRollChance = 0.f;

	/** Chance that a successful roll becomes equipment instead of a potion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EquipmentChance = 0.f;

	/** Health share among consumables; the remainder becomes mana potions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealthPotionWeight = 0.5f;

	/** Independent personal arcane-ore budget; boss summons use profile None. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot|Crafting", meta = (ClampMin = "0", ClampMax = "5"))
	int32 GuaranteedArcaneOre = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot|Crafting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BonusArcaneOreChance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	TArray<FName> EquipmentItemIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	TArray<FAuraLootRarityWeight> RarityWeights;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "-20", ClampMax = "20"))
	int32 MinItemLevelOffset = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "-20", ClampMax = "20"))
	int32 MaxItemLevelOffset = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "0.0"))
	float ScatterRadius = 115.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "5.0"))
	float LifetimeSeconds = 120.f;
};

/** Data-driven personal loot budgets used by enemies and reward chests. */
UCLASS(BlueprintType)
class AURA_API UAuraLootProfileData : public UDataAsset
{
	GENERATED_BODY()

public:
	UAuraLootProfileData();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FName BalanceVersion = TEXT("NativeLoot_v1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FAuraLootProfile CommonEnemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FAuraLootProfile EliteEnemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FAuraLootProfile SealGuardian;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FAuraLootProfile Boss;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot")
	FAuraLootProfile Treasure;

	/** Nearby connected players receive independent personal rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "500.0"))
	float EligibilityRadius = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Loot", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxActivePersonalDropsPerPlayer = 24;

	const FAuraLootProfile& GetProfile(EAuraLootProfileType Type) const;
	bool Validate(FString& OutError) const;

	static const UAuraLootProfileData& Get();
	static const FSoftObjectPath& GetDefaultAssetPath();
};
