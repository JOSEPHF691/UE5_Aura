// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "AuraGameState.generated.h"

/**
 * Server-owned state shared by every player in the current dungeon run.
 * Player-specific progression stays on the player/character; world progress
 * such as checkpoints and cleared rooms lives here so clients cannot diverge.
 */
UCLASS()
class AURA_API AAuraGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	static const TArray<FName>& RequiredSealIds();
	static constexpr int32 SealsNeededForBoss = 3;
	bool TryActivateSeal(FName SealId);
	bool HasActivatedSeal(FName SealId) const;
	bool TryMarkBossSummoned();
	int32 GetActivatedSealCount() const { return ActivatedSealIds.Num(); }
	bool AreAllSealsActivated() const { return ActivatedSealIds.Num() >= SealsNeededForBoss; }
	bool IsBossSummoned() const { return bBossSummoned; }
	bool IsAbyssTrialActive() const { return bAbyssTrialActive; }

	void SetActiveCheckpoint(FName CheckpointId, const FTransform& RespawnTransform);
	bool TryGetActiveCheckpoint(FTransform& OutRespawnTransform, FName* OutCheckpointId = nullptr) const;

	void MarkRewardEncounterCleared(FName EncounterId);
	bool IsRewardEncounterCleared(FName EncounterId) const;

	void MarkRunCompleted();
	void SetTeamWipeEndTime(float EndServerTime);
	bool IsTeamWipePending() const { return TeamWipeEndServerTime > 0.f; }
	float GetTeamWipeEndTime() const { return TeamWipeEndServerTime; }
	void ResetBossSummonAfterWipe();

	UFUNCTION(BlueprintPure, Category = "Aura|Coop")
	bool HasActiveCheckpoint() const { return bHasActiveCheckpoint; }

	UFUNCTION(BlueprintPure, Category = "Aura|Coop")
	FName GetActiveCheckpointId() const { return ActiveCheckpointId; }

	UFUNCTION(BlueprintPure, Category = "Aura|Coop")
	bool IsRunCompleted() const { return bRunCompleted; }

private:
	UPROPERTY(Replicated)
	float TeamWipeEndServerTime = 0.f;

	UPROPERTY(Replicated)
	bool bHasActiveCheckpoint = false;

	UPROPERTY(Replicated)
	FName ActiveCheckpointId = NAME_None;

	UPROPERTY(Replicated)
	FTransform ActiveCheckpointTransform = FTransform::Identity;

	UPROPERTY(Replicated)
	TArray<FName> ClearedRewardEncounterIds;

	UPROPERTY(Replicated)
	bool bRunCompleted = false;

	/** Team quest state, intentionally independent of personal backpack capacity/ownership. */
	UPROPERTY(Replicated)
	TArray<FName> ActivatedSealIds;

	UPROPERTY(Replicated)
	bool bBossSummoned = false;

	UPROPERTY(Replicated)
	bool bAbyssTrialActive = false;
};
