// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Game/AuraRunUpgrade.h"
#include "Game/AuraProfileSaveGame.h"
#include "Player/AuraPlayerState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "AuraRunSubsystem.generated.h"

/** Keeps the active dungeon checkpoint alive while the current map is reloaded after death. */
UCLASS()
class AURA_API UAuraRunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	/** Standalone only. Restore validated inventory/equipment once on a newly created PlayerState. */
	void RestoreStandalonePlayer(AAuraPlayerState* Player);
	void ObserveStandalonePlayer(AAuraPlayerState* Player);
	bool FlushPersistentSave();
	bool IsPersistentSaveEnabled() const { return bDiskEnabled && !bDiskWriteBlocked; }
	const TSet<FName>& GetClearedRewardEncounterIds() const { return ClearedRewardEncounterIds; }
	void SaveCheckpoint(const UWorld* World, FName CheckpointId, const FTransform& RespawnTransform,
		int32 PlayerLevel = 1, int32 PlayerExperience = 0,
		const FAuraRunUpgradeState& RunUpgrades = FAuraRunUpgradeState());
	void UpdateSavedProgression(const UWorld* World, int32 PlayerLevel, int32 PlayerExperience);
	void UpdateSavedRunUpgrades(const UWorld* World, const FAuraRunUpgradeState& RunUpgrades);
	void UpdateSavedGrowth(const FAuraGrowthState& Growth);
	bool TryGetSavedGrowth(FAuraGrowthState& OutGrowth) const;
	bool TryGetCheckpointForWorld(const UWorld* World, FTransform& OutRespawnTransform,
		int32* OutPlayerLevel = nullptr, int32* OutPlayerExperience = nullptr,
		FAuraRunUpgradeState* OutRunUpgrades = nullptr) const;
	void MarkTreasureClaimed(FName TreasureId);
	bool IsTreasureClaimed(FName TreasureId) const;
	void MarkRewardEncounterCleared(FName EncounterId);
	bool IsRewardEncounterCleared(FName EncounterId) const;
	void MarkSealActivated(FName SealId);
	bool HasActivatedSeal(FName SealId) const;
	int32 GetActivatedSealCount() const { return ActivatedSealIds.Num(); }
	int32 GetClaimedTreasureCount() const { return ClaimedTreasureIds.Num(); }
	void MarkRunCompleted();
	void RequestPostBossCamp();
	bool ConsumePostBossCampRequest();
	bool IsAtPostBossCamp() const { return bAtPostBossCamp; }
	bool IsAbyssTrialUnlocked() const { return bAbyssTrialUnlocked; }
	bool IsAbyssTrialActive() const { return bAbyssTrialActive; }
	bool StartAbyssTrial();
	void ResetRun(bool bPreserveDifficulty = false);

	bool HasCheckpoint() const { return bHasCheckpoint; }
	bool IsRunCompleted() const { return bRunCompleted; }
	FName GetActiveCheckpointId() const { return ActiveCheckpointId; }
	int32 GetSavedPlayerLevel() const { return SavedPlayerLevel; }
	int32 GetSavedPlayerExperience() const { return SavedPlayerExperience; }
	const FAuraRunUpgradeState& GetSavedRunUpgrades() const { return SavedRunUpgrades; }

private:
	static FName GetStableLevelName(const UWorld* World);
	void LoadPersistentSave();
	void RequestPersistentSave();
	void FlushPendingSave();
	void HandlePlayerItemsChanged();
	void CaptureObservedPlayer();
	bool ValidateSave(const UAuraProfileSaveGame& Save, FString& OutError) const;
	static FAuraSavedItem SaveItem(const FAuraItemInstance& Item);
	static FAuraItemInstance RestoreItem(const FAuraSavedItem& Item);

	FName CheckpointLevelName = NAME_None;
	FName ActiveCheckpointId = NAME_None;
	FTransform SavedRespawnTransform = FTransform::Identity;
	int32 SavedPlayerLevel = 1;
	int32 SavedPlayerExperience = 0;
	FAuraRunUpgradeState SavedRunUpgrades;
	FAuraGrowthState SavedGrowth;
	bool bHasGrowthSnapshot = false;
	TSet<FName> ClaimedTreasureIds;
	TSet<FName> ClearedRewardEncounterIds;
	TSet<FName> ActivatedSealIds;
	bool bHasCheckpoint = false;
	bool bRunCompleted = false;
	bool bReturnToPostBossCamp = false;
	bool bAtPostBossCamp = false;
	bool bAbyssTrialUnlocked = false;
	bool bAbyssTrialActive = false;
	TArray<FAuraSavedBackpackEntry> SavedBackpack;
	TArray<FAuraSavedEquipmentEntry> SavedEquipment;
	TWeakObjectPtr<AAuraPlayerState> ObservedPlayer;
	FTimerHandle SaveTimer;
	FString SaveSlotName = TEXT("AuraProfile");
	bool bDiskEnabled = true;
	bool bDiskWriteBlocked = false;
	bool bSavePending = false;
	bool bHasItemSnapshot = false;
};
