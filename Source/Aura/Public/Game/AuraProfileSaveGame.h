// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Game/AuraRunUpgrade.h"
#include "GameFramework/SaveGame.h"
#include "Items/AuraItemTypes.h"
#include "Player/AuraPlayerState.h"
#include "AuraProfileSaveGame.generated.h"

/** Save only stable definition IDs and instance data, never a live UObject reference. */
USTRUCT()
struct FAuraSavedItem
{
	GENERATED_BODY()

	UPROPERTY() FGuid InstanceId;
	UPROPERTY() FName DefinitionId = NAME_None;
	UPROPERTY() int32 Quantity = 1;
	UPROPERTY() int32 ItemLevel = 1;
	UPROPERTY() int32 EnhancementLevel = 0;
	UPROPERTY() EAuraItemRarity Rarity = EAuraItemRarity::Common;
	UPROPERTY() int32 RollSeed = 0;
	UPROPERTY() TArray<FAuraItemStatModifier> RolledModifiers;
	UPROPERTY() EAuraItemSourceType SourceType = EAuraItemSourceType::Unknown;
	UPROPERTY() FName SourceId = NAME_None;
};

USTRUCT()
struct FAuraSavedBackpackEntry
{
	GENERATED_BODY()

	UPROPERTY() int32 SlotIndex = INDEX_NONE;
	UPROPERTY() FAuraSavedItem Item;
};

USTRUCT()
struct FAuraSavedEquipmentEntry
{
	GENERATED_BODY()

	UPROPERTY() EAuraEquipmentSlot Slot = EAuraEquipmentSlot::None;
	UPROPERTY() FAuraSavedItem Item;
};

/** Versioned standalone profile plus current dungeon checkpoint. Remote players need a stable account ID first. */
UCLASS()
class AURA_API UAuraProfileSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 6;

	UPROPERTY() int32 SchemaVersion = CurrentVersion;
	UPROPERTY() bool bHasProfile = false;
	UPROPERTY() FAuraGrowthState Growth;
	UPROPERTY() TArray<FAuraSavedBackpackEntry> Backpack;
	UPROPERTY() TArray<FAuraSavedEquipmentEntry> Equipment;

	UPROPERTY() bool bHasCheckpoint = false;
	UPROPERTY() FName CheckpointLevelName = NAME_None;
	UPROPERTY() FName ActiveCheckpointId = NAME_None;
	UPROPERTY() FTransform RespawnTransform = FTransform::Identity;
	UPROPERTY() int32 CheckpointPlayerLevel = 1;
	UPROPERTY() int32 CheckpointPlayerExperience = 0;
	UPROPERTY() FAuraRunUpgradeState RunUpgrades;
	UPROPERTY() TArray<FName> ClaimedTreasureIds;
	UPROPERTY() TArray<FName> ClearedRewardEncounterIds;
	UPROPERTY() TArray<FName> ActivatedSealIds;
	UPROPERTY() bool bRunCompleted = false;
	UPROPERTY() bool bAtPostBossCamp = false;
	/** v2: first clear unlocks the optional next-run challenge. */
	UPROPERTY() bool bAbyssTrialUnlocked = false;
	UPROPERTY() bool bAbyssTrialActive = false;
};
