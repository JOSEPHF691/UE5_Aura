// Copyright yosep

#include "Game/AuraGameMode.h"

#include "Character/AuraCharacter.h"
#include "Coop/AuraReviveComponent.h"
#include "Game/AuraGameState.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Level/AuraEncounterVolume.h"
#include "Level/AuraArchmage.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraGameMode, Log, All);

AAuraGameMode::AAuraGameMode()
{
	GameStateClass = AAuraGameState::StaticClass();
	PlayerStateClass = AAuraPlayerState::StaticClass();
	HUDClass = AAuraHUD::StaticClass();
	bUseSeamlessTravel = true;

	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClass(
		TEXT("/Game/Aura/Blueprints/BP_AuraCharacter"));
	if (PlayerPawnClass.Succeeded()) DefaultPawnClass = PlayerPawnClass.Class;

	static ConstructorHelpers::FClassFinder<APlayerController> PlayerControllerBlueprint(
		TEXT("/Game/Aura/Blueprints/BP_AuraPlayerController"));
	if (PlayerControllerBlueprint.Succeeded())
	{
		PlayerControllerClass = PlayerControllerBlueprint.Class;
	}
}

void AAuraGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	if (!NewPlayer) return;

	if (AAuraPlayerState* AuraPlayerState = NewPlayer->GetPlayerState<AAuraPlayerState>())
	{
		AuraPlayerState->SetPlayerDead(false);
	}

	AAuraCharacter* Character = Cast<AAuraCharacter>(NewPlayer->GetPawn());
	AAuraGameState* AuraGameState = GetGameState<AAuraGameState>();
	FTransform CheckpointTransform;
	FTransform ActiveRoomTransform;
	const bool bHasActiveRoom = Character
		&& TryResolveActiveRoomTransform(NewPlayer, ActiveRoomTransform);
	const bool bHasCheckpoint = Character && AuraGameState
		&& AuraGameState->TryGetActiveCheckpoint(CheckpointTransform);
	if (Character && (bHasActiveRoom || bHasCheckpoint))
	{
		const FTransform Destination = bHasActiveRoom
			? ActiveRoomTransform : ResolveRespawnTransform(NewPlayer);
		Character->TeleportTo(
			Destination.GetLocation(), Destination.Rotator(), false, true);
		Character->RestoreVitalsAtCheckpoint();
	}
	if (Character && GetNetMode() == NM_Standalone)
	{
		UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
		if (Run && Run->IsRunCompleted() && !Run->IsAtPostBossCamp())
		{
			// A process can stop after the boss dies but before the camp return input.
			Run->RequestPostBossCamp();
		}
		if (Run && (Run->ConsumePostBossCampRequest() || Run->IsAtPostBossCamp()))
		{
			for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
			{
				const FVector CampLocation = It->GetActorLocation()
					+ FVector(-160.f, 0.f, 96.f);
				Character->TeleportTo(CampLocation, FRotator::ZeroRotator, false, true);
				Character->RestoreVitalsAtCheckpoint();
				if (AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(NewPlayer))
				{
					AuraController->ShowCoopMessage(
						TEXT("已返回营地  |  可拜访大魔法师"),
						FLinearColor(0.8f, 0.57f, 1.f), 3.f,
						Run->IsAbyssTrialUnlocked()
							? TEXT("大魔法师 [F]  |  [N] 普通模式  |  [T] 深渊试炼")
							: TEXT("大魔法师 [F] 学习技能与进化  |  [N] 开始下一局"));
				}
				UE_LOG(LogAuraGameMode, Display,
					TEXT("M12 postboss camp arrival. Player=%s Location=%s PaidLevel=%d XP=%d"),
					*GetNameSafe(NewPlayer->PlayerState),
					*Character->GetActorLocation().ToCompactString(),
					Character->GetCharacterLevel(), Character->GetTotalExperience());
				break;
			}
		}
	}
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		It->SyncStateToController(Cast<AAuraPlayerController>(NewPlayer));
	}

	UE_LOG(LogAuraGameMode, Display,
		TEXT("Co-op player joined. Controller=%s Players=%d Pawn=%s ActiveRoom=%s Location=%s"),
		*GetNameSafe(NewPlayer), GameState ? GameState->PlayerArray.Num() : 0,
		*GetNameSafe(NewPlayer->GetPawn()), bHasActiveRoom ? TEXT("true") : TEXT("false"),
		Character ? *Character->GetActorLocation().ToCompactString() : TEXT("None"));
}

void AAuraGameMode::Logout(AController* Exiting)
{
	if (AAuraCharacter* Character = Exiting ? Cast<AAuraCharacter>(Exiting->GetPawn()) : nullptr)
		Character->GetReviveComponent()->CancelAllRescueLinks();
	if (FTimerHandle* PendingTimer = PendingRespawns.Find(Exiting))
	{
		GetWorldTimerManager().ClearTimer(*PendingTimer);
		PendingRespawns.Remove(Exiting);
	}
	Super::Logout(Exiting);
	if (GetNetMode() != NM_Standalone)
		GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::CheckForTeamWipe);
}

void AAuraGameMode::HandlePlayerDeath(AAuraCharacter* DefeatedCharacter)
{
	if (!DefeatedCharacter || !DefeatedCharacter->HasAuthority()) return;
	AController* Controller = DefeatedCharacter->GetController();
	if (!Controller) return;

	if (AAuraPlayerState* AuraPlayerState = Controller->GetPlayerState<AAuraPlayerState>())
	{
		AuraPlayerState->SetPlayerDead(true);
	}
	if (GetNetMode() != NM_Standalone)
	{
		CheckForTeamWipe();
		return;
	}

	FTimerHandle& RespawnTimer = PendingRespawns.FindOrAdd(Controller);
	FTimerDelegate RespawnDelegate;
	RespawnDelegate.BindUObject(
		this, &AAuraGameMode::RespawnPlayerAtCheckpoint, TWeakObjectPtr<AController>(Controller));
	GetWorldTimerManager().SetTimer(
		RespawnTimer, RespawnDelegate, FMath::Max(0.1f, RespawnDelay), false);

	UE_LOG(LogAuraGameMode, Display,
		TEXT("Player defeated; checkpoint respawn scheduled. Player=%s Delay=%.1f"),
		*GetNameSafe(Controller->PlayerState), RespawnDelay);
}

void AAuraGameMode::RequestPlayerRespawn(AController* Controller)
{
	if (!Controller || !HasAuthority()) return;
	// Every co-op respawn goes through rescue or the team-wipe transaction.
	if (GetNetMode() != NM_Standalone) return;
	const AAuraCharacter* Character = Cast<AAuraCharacter>(Controller->GetPawn());
	if (!Character || !Character->IsDead()) return;

	if (FTimerHandle* PendingTimer = PendingRespawns.Find(Controller))
	{
		GetWorldTimerManager().ClearTimer(*PendingTimer);
	}
	RespawnPlayerAtCheckpoint(Controller);
}

FTransform AAuraGameMode::ResolveRespawnTransform(AController* Controller)
{
	FTransform RespawnTransform = FTransform::Identity;
	if (TryResolveActiveRoomTransform(Controller, RespawnTransform))
	{
		return RespawnTransform;
	}
	if (const AAuraGameState* AuraGameState = GetGameState<AAuraGameState>();
		AuraGameState && AuraGameState->TryGetActiveCheckpoint(RespawnTransform))
	{
		const int32 PlayerIndex = GameState && Controller && Controller->PlayerState
			? GameState->PlayerArray.IndexOfByKey(Controller->PlayerState) : 0;
		const int32 PlayerCount = GameState ? FMath::Max(1, GameState->PlayerArray.Num()) : 1;
		const float LateralOffset = (PlayerIndex - (PlayerCount - 1) * 0.5f) * 140.f;
		RespawnTransform.AddToTranslation(
			RespawnTransform.GetRotation().RotateVector(FVector(0.f, LateralOffset, 0.f)));
		return RespawnTransform;
	}

	if (AActor* StartSpot = FindPlayerStart(Controller))
	{
		return StartSpot->GetActorTransform();
	}
	if (Controller && Controller->GetPawn()) return Controller->GetPawn()->GetActorTransform();
	return RespawnTransform;
}

bool AAuraGameMode::TryResolveActiveRoomTransform(
	AController* Controller, FTransform& OutTransform) const
{
	if (!GetWorld()) return false;
	const FVector ReferenceLocation = Controller && Controller->GetPawn()
		? Controller->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	AAuraEncounterVolume* BestEncounter = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		AAuraEncounterVolume* Encounter = *It;
		if (!Encounter || !Encounter->IsCombatActive()) continue;
		const float DistanceSquared = FVector::DistSquared(
			ReferenceLocation, Encounter->GetActorLocation());
		if (!BestEncounter || DistanceSquared < BestDistanceSquared)
		{
			BestEncounter = Encounter;
			BestDistanceSquared = DistanceSquared;
		}
	}
	if (!BestEncounter) return false;

	const int32 PlayerIndex = GameState && Controller && Controller->PlayerState
		? GameState->PlayerArray.IndexOfByKey(Controller->PlayerState) : 0;
	const int32 PlayerCount = GameState ? FMath::Max(1, GameState->PlayerArray.Num()) : 1;
	OutTransform = BestEncounter->GetRoomReentryTransform(
		FMath::Max(0, PlayerIndex), PlayerCount);
	return true;
}

void AAuraGameMode::RespawnPlayerAtCheckpoint(TWeakObjectPtr<AController> Controller)
{
	AController* ResolvedController = Controller.Get();
	PendingRespawns.Remove(Controller);
	if (!ResolvedController) return;

	AAuraCharacter* Character = Cast<AAuraCharacter>(ResolvedController->GetPawn());
	if (!Character || !Character->IsDead()) return;

	const FTransform RespawnTransform = ResolveRespawnTransform(ResolvedController);
	Character->ReviveAtTransform(RespawnTransform);
	if (AAuraPlayerState* AuraPlayerState =
		ResolvedController->GetPlayerState<AAuraPlayerState>())
	{
		AuraPlayerState->SetPlayerDead(false);
	}

	UE_LOG(LogAuraGameMode, Display,
		TEXT("Player respawned. Player=%s Location=%s"),
		*GetNameSafe(ResolvedController->PlayerState),
		*RespawnTransform.GetLocation().ToCompactString());
}
