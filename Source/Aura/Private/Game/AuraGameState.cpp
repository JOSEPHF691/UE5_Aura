// Copyright yosep

#include "Game/AuraGameState.h"

#include "Game/AuraRunSubsystem.h"
#include "Level/AuraRitualSite.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraGameState, Log, All);

const TArray<FName>& AAuraGameState::RequiredSealIds()
{
	static const TArray<FName> Ids = {
		FName(TEXT("LowerNorth")), FName(TEXT("LowerSouth")),
		FName(TEXT("UpperSouth")), FName(TEXT("UpperNorth"))
	};
	return Ids;
}

void AAuraGameState::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || GetNetMode() != NM_Standalone || !GetGameInstance()) return;
	if (const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
	{
		bRunCompleted = Run->IsRunCompleted();
		bAbyssTrialActive = Run->IsAbyssTrialActive();
		FTransform Checkpoint;
		if (Run->TryGetCheckpointForWorld(GetWorld(), Checkpoint))
		{
			bHasActiveCheckpoint = true;
			ActiveCheckpointId = Run->GetActiveCheckpointId();
			ActiveCheckpointTransform = Checkpoint;
		}
		for (const FName Id : Run->GetClearedRewardEncounterIds())
		{
			ClearedRewardEncounterIds.Add(Id);
		}
		for (const FName Id : RequiredSealIds())
		{
			if (Run->HasActivatedSeal(Id)) ActivatedSealIds.Add(Id);
		}
	}
}

void AAuraGameState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraGameState, bHasActiveCheckpoint);
	DOREPLIFETIME(AAuraGameState, ActiveCheckpointId);
	DOREPLIFETIME(AAuraGameState, ActiveCheckpointTransform);
	DOREPLIFETIME(AAuraGameState, ClearedRewardEncounterIds);
	DOREPLIFETIME(AAuraGameState, bRunCompleted);
	DOREPLIFETIME(AAuraGameState, ActivatedSealIds);
	DOREPLIFETIME(AAuraGameState, bBossSummoned);
	DOREPLIFETIME(AAuraGameState, bAbyssTrialActive);
	DOREPLIFETIME(AAuraGameState, TeamWipeEndServerTime);
}

bool AAuraGameState::TryActivateSeal(const FName SealId)
{
	if (!HasAuthority() || !RequiredSealIds().Contains(SealId)
		|| ActivatedSealIds.Contains(SealId) || bRunCompleted) return false;
	ActivatedSealIds.Add(SealId);
	if (GetNetMode() == NM_Standalone && GetGameInstance())
	{
		if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			Run->MarkSealActivated(SealId);
		}
	}
	ForceNetUpdate();
	if (AreAllSealsActivated())
	{
		for (TActorIterator<AAuraRitualSite> It(GetWorld()); It; ++It)
		{
			if (It->IsBossAltar()) It->RefreshBossGate();
		}
	}
	UE_LOG(LogAuraGameState, Display, TEXT("Team seal activated. Id=%s Count=%d/%d Sources=%d"),
		*SealId.ToString(), ActivatedSealIds.Num(), SealsNeededForBoss, RequiredSealIds().Num());
	return true;
}

bool AAuraGameState::HasActivatedSeal(const FName SealId) const
{
	return ActivatedSealIds.Contains(SealId);
}

bool AAuraGameState::TryMarkBossSummoned()
{
	if (!HasAuthority() || !AreAllSealsActivated() || bBossSummoned || bRunCompleted)
	{
		return false;
	}
	bBossSummoned = true;
	ForceNetUpdate();
	return true;
}

void AAuraGameState::SetTeamWipeEndTime(const float EndServerTime)
{
	if (!HasAuthority()) return;
	TeamWipeEndServerTime = EndServerTime;
	ForceNetUpdate();
}

void AAuraGameState::ResetBossSummonAfterWipe()
{
	if (!HasAuthority() || bRunCompleted) return;
	bBossSummoned = false;
	ForceNetUpdate();
	for (TActorIterator<AAuraRitualSite> It(GetWorld()); It; ++It)
		if (It->IsBossAltar()) It->RefreshBossGate();
}

void AAuraGameState::SetActiveCheckpoint(
	const FName CheckpointId, const FTransform& RespawnTransform)
{
	if (!HasAuthority() || CheckpointId.IsNone()) return;

	bHasActiveCheckpoint = true;
	ActiveCheckpointId = CheckpointId;
	ActiveCheckpointTransform = RespawnTransform;
	bRunCompleted = false;
	ForceNetUpdate();

	UE_LOG(LogAuraGameState, Display,
		TEXT("Shared checkpoint activated. Id=%s Location=%s"),
		*ActiveCheckpointId.ToString(),
		*ActiveCheckpointTransform.GetLocation().ToCompactString());
}

bool AAuraGameState::TryGetActiveCheckpoint(
	FTransform& OutRespawnTransform, FName* OutCheckpointId) const
{
	if (!bHasActiveCheckpoint || bRunCompleted) return false;
	OutRespawnTransform = ActiveCheckpointTransform;
	if (OutCheckpointId) *OutCheckpointId = ActiveCheckpointId;
	return true;
}

void AAuraGameState::MarkRewardEncounterCleared(const FName EncounterId)
{
	if (!HasAuthority() || EncounterId.IsNone()
		|| ClearedRewardEncounterIds.Contains(EncounterId))
	{
		return;
	}

	ClearedRewardEncounterIds.Add(EncounterId);
	ForceNetUpdate();
	UE_LOG(LogAuraGameState, Display,
		TEXT("Shared reward encounter cleared. Id=%s"), *EncounterId.ToString());
}

bool AAuraGameState::IsRewardEncounterCleared(const FName EncounterId) const
{
	return !EncounterId.IsNone() && ClearedRewardEncounterIds.Contains(EncounterId);
}

void AAuraGameState::MarkRunCompleted()
{
	if (!HasAuthority()) return;
	bRunCompleted = true;
	ForceNetUpdate();
	UE_LOG(LogAuraGameState, Display, TEXT("Shared dungeon run completed."));
}
