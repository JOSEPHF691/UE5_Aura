// Copyright yosep

#include "Game/AuraGameMode.h"

#include "AbilitySystem/AuraArcaneSlowField.h"
#include "AbilitySystem/AuraElementalSpell.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "Character/AuraCharacter.h"
#include "Coop/AuraReviveComponent.h"
#include "EngineUtils.h"
#include "Game/AuraGameState.h"
#include "Level/AuraEncounterVolume.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"

bool AAuraGameMode::IsTeamWipePending() const
{
	const AAuraGameState* State = GetGameState<AAuraGameState>();
	return State && State->IsTeamWipePending();
}

void AAuraGameMode::CheckForTeamWipe()
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone || bResettingTeam || IsTeamWipePending()) return;
	AAuraGameState* State = GetGameState<AAuraGameState>();
	if (!State || State->IsRunCompleted()) return;
	bool bHasDownedPlayer = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AAuraCharacter* Character = It->Get() ? Cast<AAuraCharacter>(It->Get()->GetPawn()) : nullptr;
		if (!Character) continue;
		if (!Character->IsDead()) return;
		bHasDownedPlayer = true;
	}
	if (!bHasDownedPlayer) return;
	State->SetTeamWipeEndTime(State->GetServerWorldTimeSeconds() + 3.f);
	GetWorldTimerManager().SetTimer(TeamWipeTimer, this, &ThisClass::FinishTeamWipe, 3.f, false);
	UE_LOG(LogTemp, Display, TEXT("AURA_COOP_TEAM_WIPE Started=true Delay=3"));
}

void AAuraGameMode::FinishTeamWipe()
{
	if (!HasAuthority() || !IsTeamWipePending()) return;
	bResettingTeam = true;
	AAuraGameState* State = GetGameState<AAuraGameState>();
	// Remove delayed attacks before resetting packs or restoring any player's collision.
	for (TActorIterator<AAuraFireBoltProjectile> It(GetWorld()); It; ++It) It->Destroy();
	for (TActorIterator<AAuraArcaneSlowField> It(GetWorld()); It; ++It) It->Destroy();
	for (TActorIterator<AAuraElementalSpell> It(GetWorld()); It; ++It) It->Destroy();
	int32 ResetRooms = 0;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
		if (It->ResetAfterTeamWipe()) ++ResetRooms;
	State->ResetBossSummonAfterWipe();

	int32 Restored = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		AAuraCharacter* Character = Controller ? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
		if (!Character) continue;
		// Resolve only after active rooms were reset: never put the party back
		// inside the failed locked room through the legacy reentry fallback.
		const FTransform Destination = ResolveRespawnTransform(Controller);
		if (Character->IsDead()) Character->ReviveAtTransform(Destination, 1.f, 2.f);
		else
		{
			Character->TeleportTo(Destination.GetLocation(), Destination.Rotator(), false, true);
			Character->RestoreVitalsAtCheckpoint();
		}
		if (AAuraPlayerState* Player = Controller->GetPlayerState<AAuraPlayerState>())
			Player->SetPlayerDead(false);
		if (AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(Controller))
			AuraController->ShowCoopMessage(TEXT("已返回检查点"), FLinearColor(0.5f, 0.8f, 1.f), 2.f);
		++Restored;
	}
	State->SetTeamWipeEndTime(0.f);
	bResettingTeam = false;
	UE_LOG(LogTemp, Display, TEXT("AURA_COOP_TEAM_WIPE Finished=true Players=%d ResetRooms=%d"),
		Restored, ResetRooms);
}
