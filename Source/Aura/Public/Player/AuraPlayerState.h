// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Character/AuraCharacter.h"
#include "GameFramework/PlayerState.h"
#include "AuraPlayerState.generated.h"

class UAuraInventoryComponent;
class UAuraEquipmentComponent;
enum class EAuraEquipmentSlot : uint8;

/** Three independently learned hit combos. Rank 0 never creates or consumes a mark. */
USTRUCT(BlueprintType)
struct FAuraComboRankState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Ember = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 Fracture = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 Overload = 0;

	int32 GetRank(const EAuraComboMarkType Type) const
	{
		switch (Type)
		{
		case EAuraComboMarkType::Ember: return Ember;
		case EAuraComboMarkType::Fracture: return Fracture;
		case EAuraComboMarkType::Overload: return Overload;
		default: return 0;
		}
	}

	int32 GetSpentPoints() const { return Ember + Fracture + Overload; }
};

/** Durable-in-session growth; skills and combos obey separate point budgets. */
USTRUCT(BlueprintType)
struct FAuraGrowthState
{
	GENERATED_BODY()

	/** Serialized name retained for v1-v5 save compatibility; this is now the automatic XP level. */
	UPROPERTY(BlueprintReadOnly)
	int32 PaidLevel = 1;
	UPROPERTY(BlueprintReadOnly)
	int32 TotalExperience = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 Souls = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 UnspentSkillPoints = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 UnspentComboPoints = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 RespecStonesPurchased = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 SmithMetalPurchased = 0;
	/** Consecutive crafts below Fine, capped at two for the next-craft pity. */
	UPROPERTY(BlueprintReadOnly)
	int32 CraftPityMisses = 0;
	UPROPERTY(BlueprintReadOnly)
	bool bFreeRespecClaimed = false;
	UPROPERTY(BlueprintReadOnly)
	FAuraSkillRankState SkillRanks;
	UPROPERTY(BlueprintReadOnly)
	FAuraComboRankState ComboRanks;

	static int32 GetEarnedSkillPoints(const int32 Level)
	{
		return FMath::Max(0, Level - 1);
	}

	static int32 GetEarnedComboPoints(const int32 Level)
	{
		return 0; // Deprecated fields exist only to deserialize old profiles.
	}

	int32 GetSpentSkillPoints() const
	{
		return FMath::Max(0, SkillRanks.FireBolt - 1)
			+ SkillRanks.ArcaneShards + SkillRanks.Shock;
	}
};

/** Per-player replicated state that must survive pawn death during a co-op run. */
UCLASS()
class AURA_API AAuraPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AAuraPlayerState();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetPlayerDead(bool bNewDead);
	void MarkTreasureClaimed(FName TreasureId);
	void EnsureGrowthInitialized();
	void AwardExperienceAndSouls(int32 Experience, int32 Souls);
	bool TryAllocateSkill(EAuraPlayerSkill Skill, FString& OutFailureReason,
		EAuraFireBoltPath FireBoltPath = EAuraFireBoltPath::None, int32 EvolutionChoice = 0);
	bool TryChooseEvolution(EAuraPlayerSkill Skill, int32 Choice, FString& OutFailureReason);
	/** Called once on the server for each successfully released basic attack. */
	bool AdvanceFireBoltCast();
	int32 GetFireBoltVolleyCharge() const { return FireBoltVolleyCharge; }
	bool TryAllocateCombo(EAuraComboMarkType Type, FString& OutFailureReason);
	bool TryRespecSkills(const FAuraSkillRankState& NewRanks, const FGuid& StoneId,
		FString& OutFailureReason);
	bool TryPurchaseMerchantOffer(int32 OfferId, FString& OutFailureReason);
	bool TrySellMerchantItem(const FGuid& InstanceId, FString& OutFailureReason);
	bool TryForgeWeapon(const FGuid& InstanceId, FString& OutFailureReason);
	bool TryCraftEquipment(EAuraEquipmentSlot Slot, FString& OutFailureReason);
	bool TrySmeltEquipment(const FGuid& InstanceId, FString& OutFailureReason);
	bool CanAcceptChestMetal(int32 Quantity) const;
	bool GrantChestMetal(int32 Quantity, FName ChestId);
	static int32 GetChestMetalReward(FName ChestId);
	void RestoreGrowth(const FAuraGrowthState& SavedGrowth);
	void MigrateLegacyGrowth(int32 LegacyLevel, int32 LegacyExperience);
	static bool IsValidGrowthSnapshot(const FAuraGrowthState& Candidate);
	static bool IsValidV5GrowthSnapshot(const FAuraGrowthState& Candidate);
	static FAuraGrowthState MigrateV5GrowthSnapshot(const FAuraGrowthState& Legacy);
	static bool IsValidV3GrowthSnapshot(const FAuraGrowthState& Candidate);
	static bool IsValidV4GrowthSnapshot(const FAuraGrowthState& Candidate);
	static bool IsValidLegacyGrowthSnapshot(const FAuraGrowthState& Candidate);
	static FAuraGrowthState MigrateLegacyGrowthSnapshot(const FAuraGrowthState& Legacy);
	static FAuraGrowthState MigrateV3GrowthSnapshot(const FAuraGrowthState& Legacy);
	static FAuraGrowthState MigrateV4GrowthSnapshot(const FAuraGrowthState& Legacy);
	int32 GetEligibleLevel() const;
	const FAuraGrowthState& GetGrowthState() const { return GrowthState; }

	UFUNCTION(BlueprintPure, Category = "Aura|Coop")
	bool IsPlayerDead() const { return bPlayerDead; }

	UFUNCTION(BlueprintPure, Category = "Aura|Coop")
	bool HasClaimedTreasure(FName TreasureId) const;

	UFUNCTION(BlueprintPure, Category = "Aura|Inventory")
	UAuraInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	UFUNCTION(BlueprintPure, Category = "Aura|Equipment")
	UAuraEquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAuraInventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAuraEquipmentComponent> EquipmentComponent;

	UPROPERTY(Replicated)
	bool bPlayerDead = false;

	/** Owner-only: another client does not need to know which personal boons this player claimed. */
	UPROPERTY(Replicated)
	TArray<FName> ClaimedTreasureIds;

	UPROPERTY(Replicated)
	FAuraGrowthState GrowthState;

	/** Session-only fifth-cast charge; changing specialization resets it. */
	UPROPERTY(Replicated)
	int32 FireBoltVolleyCharge = 0;

	bool bGrowthInitialized = false;
	bool bPersistentItemsInitialized = false;
	/** Promotes to every level already earned by XP and grants the exact point deltas. */
	int32 ApplyAutomaticLevelUps();
	void CommitGrowth(bool bRestoreVitals = false);
};
