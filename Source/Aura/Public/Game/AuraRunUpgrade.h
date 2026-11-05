// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AuraRunUpgrade.generated.h"

/** A run-only boon offered by dungeon treasure caches. */
UENUM(BlueprintType)
enum class EAuraRunUpgradeType : uint8
{
	FireBoltPower UMETA(DisplayName = "Ember Core"),
	ArcaneShardsMastery UMETA(DisplayName = "Arcane Focus"),
	ShockMastery UMETA(DisplayName = "Storm Sigil"),
	Vitality UMETA(DisplayName = "Vitality"),
	/** Legacy enum name retained for checkpoint compatibility; now grants max mana only. */
	ManaSpring UMETA(DisplayName = "Mana Vessel"),
	Spellweaver UMETA(DisplayName = "Spellweaver"),
	MAX UMETA(Hidden)
};

/** Replicated and checkpointed ranks for the current dungeon run. */
USTRUCT(BlueprintType)
struct FAuraRunUpgradeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Run Upgrades")
	int32 FireBoltPowerRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Run Upgrades")
	int32 ArcaneShardsMasteryRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Run Upgrades")
	int32 ShockMasteryRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Run Upgrades")
	int32 VitalityRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Run Upgrades")
	int32 ManaSpringRank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Run Upgrades")
	int32 SpellweaverRank = 0;

	int32 GetRank(EAuraRunUpgradeType Type) const;
	bool AddRank(EAuraRunUpgradeType Type, int32 MaxRank);
	int32 GetTotalRanks() const;
	bool IsEmpty() const { return GetTotalRanks() == 0; }
	bool operator==(const FAuraRunUpgradeState& Other) const;
	bool operator!=(const FAuraRunUpgradeState& Other) const { return !(*this == Other); }
};

namespace AuraRunUpgrade
{
	AURA_API const TArray<EAuraRunUpgradeType>& GetAllTypes();
	AURA_API FString GetTitle(EAuraRunUpgradeType Type);
	AURA_API void GetDescriptionLines(EAuraRunUpgradeType Type, int32 NextRank,
		FString& OutPrimary, FString& OutSecondary);
	AURA_API FLinearColor GetColor(EAuraRunUpgradeType Type);
	AURA_API FBox2D GetSelectionCardRect(const FVector2D& ViewportSize, int32 OptionIndex,
		int32 OptionCount);
}
