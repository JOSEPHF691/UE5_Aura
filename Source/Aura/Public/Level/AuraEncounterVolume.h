// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Data/AuraCombatBalanceData.h"
#include "GameFramework/Actor.h"
#include "AuraEncounterVolume.generated.h"

class AAuraCharacter;
class AAuraDungeonGate;
class AAuraEnemy;
class AAuraEncounterVolume;
class AAuraLootChest;
class AAuraPlayerController;
class AAuraTreasureChest;
class APlayerState;
class AAuraPlayerState;
class UBoxComponent;
class USceneComponent;
class UTextRenderComponent;
class UPrimitiveComponent;
class UNiagaraSystem;
struct FHitResult;

UENUM(BlueprintType)
enum class EAuraEncounterState : uint8
{
	Dormant,
	Arming,
	LockedCombat,
	Cleared,
	RewardAvailable,
	Completed
};

UENUM(BlueprintType)
enum class EAuraEncounterType : uint8
{
	Normal UMETA(DisplayName = "Normal Combat"),
	Ambush UMETA(DisplayName = "Wave Ambush"),
	Elite UMETA(DisplayName = "Elite Combat"),
	GuardedTreasure UMETA(DisplayName = "Guarded Treasure"),
	Recovery UMETA(DisplayName = "Recovery Room"),
	Boss UMETA(DisplayName = "Boss Combat")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnAuraEncounterCompleted, AAuraEncounterVolume*, CompletedEncounter);

/** Spawns one room encounter, controls its gates, and finishes the demo after the final boss dies. */
UCLASS()
class AURA_API AAuraEncounterVolume : public AActor
{
	GENERATED_BODY()

public:
	AAuraEncounterVolume();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	void StartEncounter(AAuraCharacter* TriggeringCharacter, bool bSkipArming = false);
	/** Boss-only entry: requires the shared seals and gathers living teammates before lockdown. */
	bool ActivateFromRitual(AAuraCharacter* TriggeringCharacter,
		bool bImmediateSpawnForSmokeTest = false);

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	EAuraEncounterState GetEncounterState() const { return EncounterState; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	EAuraEncounterType GetEncounterType() const { return EncounterType; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsCombatActive() const
	{
		return EncounterState == EAuraEncounterState::Arming
			|| EncounterState == EAuraEncounterState::LockedCombat;
	}

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	int32 GetActiveEnemyCount() const { return ActiveEnemies.Num(); }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	int32 GetRemainingEnemyCount() const { return RemainingEnemyCount; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	TArray<AAuraEnemy*> GetActiveEnemies() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	int32 GetEncounterOrder() const { return EncounterOrder; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsFinalEncounter() const { return bFinalEncounter; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsRewardEncounter() const { return bRewardEncounter; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	AAuraTreasureChest* GetRewardChest() const { return RewardChest; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	AAuraLootChest* GetLootChest() const { return LootChest; }

	FName GetEncounterId() const { return EncounterId.IsNone() ? GetFName() : EncounterId; }
	/** Stable M22 reward identity; NAME_None means this fight is not a route-budget node. */
	FName GetM22BudgetId() const;
	/** Called by an enemy after its authoritative kill reward is actually granted. */
	void RecordM22KillReward(AAuraPlayerState* Player, int32 Experience, int32 Souls);
	TArray<AAuraPlayerState*> GetEligibleRewardPlayers() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool AreControlledGatesOpen() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	int32 GetConfiguredSpawnCount() const;
	const TArray<int32>& GetEliteSpawnIndices() const { return EliteSpawnIndices; }
	/** Ground detected at runtime; the authored map actor is never moved or saved. */
	float GetRuntimeFloorZ() const { return RuntimeFloorZ; }
	int32 GetPreplacedGuardCount() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	float GetArmingDuration() const { return ArmingDuration; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	float GetArmingTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	float GetArmingEntryPlaneLocalX() const;

	FTransform GetRoomReentryTransform(int32 PlayerIndex = 0, int32 PlayerCount = 1) const;
	void SyncStateToController(AAuraPlayerController* Controller) const;
	/** Reset an unfinished fight without touching authored geometry or claimed rewards. */
	bool ResetAfterTeamWipe();

	UPROPERTY(BlueprintAssignable, Category = "Aura|Dungeon")
	FOnAuraEncounterCompleted OnEncounterCompleted;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex);

	UFUNCTION()
	void HandleEnemyDefeated(AAuraEnemy* DefeatedEnemy);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UTextRenderComponent> EncounterLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FString EncounterTitle = TEXT("清理房间");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	int32 EncounterOrder = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	EAuraEncounterType EncounterType = EAuraEncounterType::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	EAuraEncounterBalanceProfile BalanceProfile = EAuraEncounterBalanceProfile::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FVector TriggerExtent = FVector(260.f, 760.f, 180.f);

	/** Time a player must remain on the room side of the doorway before the doors seal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Room",
		meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float ArmingDuration = 1.f;

	/** Distance from the trigger's approach-side edge required to count the player as inside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Room",
		meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float ArmingRoomEntryDepth = 40.f;

	/** Legacy switch retained for serialized maps. Entry never force-teleports teammates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Room")
	bool bGatherTeamOnLock = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Room")
	FVector RoomReentryOffset = FVector(220.f, 0.f, 96.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Room",
		meta = (ClampMin = "50.0", ClampMax = "400.0"))
	float ReentryPlayerSpacing = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TSubclassOf<AAuraEnemy> EnemyClass;

	/** Optional class per spawn offset. Missing entries fall back to EnemyClass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TArray<TSubclassOf<AAuraEnemy>> SpawnEnemyClasses;

	/** Optional class overrides in Abyss Trial, aligned to SpawnOffsets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Abyss")
	TArray<TSubclassOf<AAuraEnemy>> AbyssSpawnEnemyClasses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TArray<FVector> SpawnOffsets;

	/** Spawn slots promoted to elites; works for both standing guards and later waves. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Population")
	TArray<int32> EliteSpawnIndices;

	/** -1 keeps the balance profile count; otherwise use this room's authored pack size. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Population",
		meta = (ClampMin = "-1", ClampMax = "20"))
	int32 SpawnCountOverride = -1;

	/** -1 preserves legacy standing guards; 0 or more explicitly authors this room. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Population",
		meta = (ClampMin = "-1", ClampMax = "20"))
	int32 StandingGuardCount = -1;

	/** Cue the remaining pack as soon as combat locks, even if standing guards live. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Population")
	bool bCueReinforcementsOnLock = false;

	/** Ambush rooms spawn this many enemies per wave. Other room types ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Waves",
		meta = (ClampMin = "1", ClampMax = "10"))
	int32 EnemiesPerWave = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Waves",
		meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float InterWaveDelay = 1.f;

	/** Ground warning played before a wave becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Waves")
	TObjectPtr<UNiagaraSystem> EnemySpawnTelegraphEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Waves",
		meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float EnemySpawnTelegraphLeadTime = 0.65f;

	/** Escaped living enemies are returned to their authored spawn transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Room",
		meta = (ClampMin = "500.0"))
	float EnemyLeashRadius = 3200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FVector SpawnedEnemyScale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	bool bFinalEncounter = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Ritual")
	bool bRequiresRitual = false;

	/** Optional branch fight that seals its linked run-boon cache until cleared. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Reward")
	bool bRewardEncounter = false;

	/** Stable identity used to preserve a cleared-but-unclaimed room across checkpoint reloads. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Reward")
	FName EncounterId = NAME_None;

	/** Optional authored mainline prerequisites; prevents adjacent packs from locking together. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Reward")
	TArray<FName> RequiredClearedEncounterIds;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Reward")
	TObjectPtr<AAuraTreasureChest> RewardChest;

	/** Optional item-only chest; separate from the run-boon cache above. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Reward")
	TObjectPtr<AAuraLootChest> LootChest;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon")
	TArray<TObjectPtr<AAuraDungeonGate>> ControlledGates;

private:
	void UpdateArmingDwell();
	void TryStartAfterPrerequisiteClear();
	void CancelArming();
	bool IsEligibleArmingCharacter(const AAuraCharacter* Character) const;
	bool IsInsideRoomSide(const AAuraCharacter* Character) const;
	AAuraCharacter* FindArmingCandidate(bool bRequireRoomSide) const;
	void BeginLockedCombat(bool bSkipSpawnTelegraph = false);
	void CalibrateRuntimeGround();
	FVector GetGroundedSpawnLocation(int32 SpawnIndex) const;
	bool IsChestAmbushRoom() const;
	void SpawnPreplacedGuards();
	void CueNextWave();
	void SpawnNextWave();
	AAuraEnemy* SpawnEnemyAtIndex(int32 SpawnIndex, bool bPreplaced = false);
	void ResolveEmptyWave();
	void HandleRoomCleared(AAuraEnemy* DefeatedEnemy = nullptr);
	void GrantM22FirstClearReward();
	void FinalizeEncounter(AAuraEnemy* DefeatedEnemy = nullptr);
	void GatherLivingPlayers();
	void ValidateActiveEnemies();
	void BroadcastArmingStatus();
	void SetRewardGuardLocked(bool bLocked, int32 RemainingGuards);
	bool HasRoomReward() const { return RewardChest != nullptr || LootChest != nullptr; }

	UFUNCTION()
	void HandleTreasureClaimed(AAuraTreasureChest* Chest, APlayerState* ClaimingPlayer);

	UFUNCTION()
	void HandleLootChestClaimed(AAuraLootChest* Chest, APlayerState* ClaimingPlayer);

	void UpdateObjective() const;
	void RefreshEditorVisuals();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayEnemySpawnTelegraph(FVector SpawnLocation, FVector EffectScale);

	UPROPERTY(ReplicatedUsing = OnRep_EncounterState)
	EAuraEncounterState EncounterState = EAuraEncounterState::Dormant;

	UFUNCTION()
	void OnRep_EncounterState();

	UPROPERTY(Replicated)
	int32 RemainingEnemyCount = 0;

	UPROPERTY(Replicated)
	float ArmingEndServerTime = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<AAuraEnemy>> ActiveEnemies;
	UPROPERTY()
	TArray<TObjectPtr<AAuraEnemy>> PreplacedGuards;

	TWeakObjectPtr<AAuraCharacter> ActivePlayer;
	TArray<TWeakObjectPtr<AAuraPlayerState>> RewardParticipants;
	TMap<TWeakObjectPtr<AAuraPlayerState>, FIntPoint> M22KillRewardsByPlayer;
	TMap<TWeakObjectPtr<AAuraEnemy>, FTransform> EnemyHomeTransforms;
	int32 NextSpawnIndex = 0;
	int32 PlannedSpawnCount = 0;
	bool bRoomClearHandled = false;
	bool bChestAmbushActive = false;
	bool bResumeChestAmbush = false;
	float RuntimeFloorZ = 0.f;
	bool bRitualActivated = false;
	bool bArmingRequiresOccupancy = false;
	FTimerHandle ArmingTimer;
	FTimerHandle PrerequisiteProbeTimer;
	FTimerHandle ArmingPulseTimer;
	FTimerHandle WaveTimer;
	FTimerHandle SpawnCueTimer;
	FTimerHandle EnemyValidationTimer;
};
