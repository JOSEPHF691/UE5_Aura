// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraItemDefinition.h"
#include "Game/AuraGameState.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Level/AuraDungeonGate.h"
#include "Level/AuraEncounterVolume.h"
#include "Level/AuraRitualSite.h"
#include "Player/AuraPlayerState.h"
#include "EngineUtils.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraSealSmoke, Log, All);

namespace
{
	AAuraRitualSite* FindRitual(UWorld* World, const FName SealId, const bool bBoss = false)
	{
		for (TActorIterator<AAuraRitualSite> It(World); It; ++It)
		{
			if (It->IsBossAltar() == bBoss && (bBoss || It->GetSealId() == SealId))
			{
				return *It;
			}
		}
		return nullptr;
	}

	AAuraEncounterVolume* FindRoom(UWorld* World, const FName Id)
	{
		for (TActorIterator<AAuraEncounterVolume> It(World); It; ++It)
		{
			if ((Id == FName(TEXT("DungeonEncounter_DemonLord")) && It->IsFinalEncounter())
				|| It->GetEncounterId() == Id) return *It;
		}
		return nullptr;
	}

	AAuraDungeonGate* FindBossGate(UWorld* World)
	{
		const AAuraRitualSite* Altar = FindRitual(World, NAME_None, true);
		return Altar ? Altar->GetBossGate() : nullptr;
	}

	void PlaceAt(AAuraCharacter* Pawn, const AActor* Target)
	{
		Pawn->TeleportTo(Target->GetActorLocation() + FVector(-95.f, 0.f, 96.f),
			FRotator::ZeroRotator, false, true);
		Pawn->RestoreVitalsAtCheckpoint();
	}

	int32 DefeatCurrentWave(AAuraCharacter* Pawn, AAuraEncounterVolume* Room)
	{
		UAbilitySystemComponent* Source = Pawn ? Pawn->GetAbilitySystemComponent() : nullptr;
		if (!Source || !Room) return 0;
		int32 Damaged = 0;
		for (AAuraEnemy* Enemy : Room->GetActiveEnemies())
		{
			if (!Enemy || Enemy->IsDead() || !Enemy->GetAbilitySystemComponent()) continue;
			FGameplayEffectContextHandle Context = Source->MakeEffectContext();
			Context.AddSourceObject(Pawn);
			FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(
				UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
			if (!Spec.IsValid()) continue;
			Spec.Data->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -10000.f);
			Enemy->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			++Damaged;
		}
		return Damaged;
	}

	struct FM14StandaloneState
	{
		bool bInitialGate = false;
		bool bEarlyBossRejected = false;
		bool bEarlySealRejected = false;
		bool bFirstTwo = false;
		bool bDistance = false;
		bool bDuplicate = false;
		bool bThreeSeals = false;
		bool bFullBag = false;
		bool bQuestUI = false;
		bool bRunSnapshot = false;
		bool bRitual = false;
		bool bBossSpawned = false;
	};
}

void AAuraPlayerController::AuraTestSealRitualM14()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraRitualSite* North = FindRitual(GetWorld(), TEXT("LowerNorth"));
	AAuraRitualSite* South = FindRitual(GetWorld(), TEXT("LowerSouth"));
	AAuraRitualSite* Upper = FindRitual(GetWorld(), TEXT("UpperSouth"));
	AAuraRitualSite* Altar = FindRitual(GetWorld(), NAME_None, true);
	AAuraEncounterVolume* NorthRoom = FindRoom(GetWorld(), TEXT("LowerNorthBoonGuardians"));
	AAuraEncounterVolume* SouthRoom = FindRoom(GetWorld(), TEXT("LowerSouthLootAmbush"));
	AAuraEncounterVolume* UpperRoom = FindRoom(GetWorld(), TEXT("UpperSouthBoonGuardian"));
	AAuraEncounterVolume* BossRoom = FindRoom(GetWorld(), TEXT("DungeonEncounter_DemonLord"));
	AAuraDungeonGate* Gate = FindBossGate(GetWorld());
	if (!Quest || !AuraPawn || !North || !South || !Upper || !Altar || !NorthRoom
		|| !SouthRoom || !UpperRoom || !BossRoom || !Gate)
	{
		UE_LOG(LogAuraSealSmoke, Error, TEXT("AURA_M14_STANDALONE Passed=false Setup=false"));
		return;
	}
	TSharedRef<FM14StandaloneState> Test = MakeShared<FM14StandaloneState>();
	FString Failure;
	Test->bInitialGate = Quest->GetActivatedSealCount() == 0 && !Gate->IsOpen()
		&& BossRoom->GetEncounterState() == EAuraEncounterState::Dormant;
	PlaceAt(AuraPawn, Altar);
	Test->bEarlyBossRejected = !Altar->TryInteract(AuraPawn, Failure);
	BossRoom->StartEncounter(AuraPawn, true);
	Test->bEarlyBossRejected &= BossRoom->GetEncounterState() == EAuraEncounterState::Dormant;
	PlaceAt(AuraPawn, North);
	Test->bEarlySealRejected = !North->TryInteract(AuraPawn, Failure)
		&& Quest->GetActivatedSealCount() == 0;

	NorthRoom->StartEncounter(AuraPawn, true);
	DefeatCurrentWave(AuraPawn, NorthRoom);
	PlaceAt(AuraPawn, North);
	const FGuid ReplayId = FGuid::NewGuid();
	ServerInteractRitualSite_Implementation(North, ReplayId);
	ServerInteractRitualSite_Implementation(North, ReplayId);
	Test->bDuplicate = Quest->GetActivatedSealCount() == 1;
	PlaceAt(AuraPawn, Upper);
	UpperRoom->StartEncounter(AuraPawn, true);
	DefeatCurrentWave(AuraPawn, UpperRoom);
	PlaceAt(AuraPawn, Upper);
	const bool bUpper = Upper->TryInteract(AuraPawn, Failure);
	Test->bFirstTwo = bUpper && Quest->GetActivatedSealCount() == 2 && !Gate->IsOpen();
	AuraPawn->TeleportTo(Upper->GetActorLocation() + FVector(1000.f, 0.f, 96.f),
		FRotator::ZeroRotator, false, true);
	Test->bDistance = !Upper->TryInteract(AuraPawn, Failure)
		&& Failure.Contains(TEXT("靠近"));
	PlaceAt(AuraPawn, South);
	SouthRoom->StartEncounter(AuraPawn, true);
	DefeatCurrentWave(AuraPawn, SouthRoom);

	TWeakObjectPtr<AAuraCharacter> WeakPawn(AuraPawn);
	TWeakObjectPtr<AAuraGameState> WeakQuest(Quest);
	TWeakObjectPtr<AAuraEncounterVolume> WeakSouthRoom(SouthRoom);
	TWeakObjectPtr<AAuraEncounterVolume> WeakBossRoom(BossRoom);
	TWeakObjectPtr<AAuraRitualSite> WeakSouth(South);
	TWeakObjectPtr<AAuraRitualSite> WeakAltar(Altar);
	TWeakObjectPtr<AAuraDungeonGate> WeakGate(Gate);
	FTimerHandle SecondWaveTimer;
	GetWorldTimerManager().SetTimer(SecondWaveTimer,
		[this, Test, WeakPawn, WeakQuest, WeakSouthRoom, WeakBossRoom,
			WeakSouth, WeakAltar, WeakGate]()
	{
		AAuraCharacter* Pawn = WeakPawn.Get();
		AAuraGameState* State = WeakQuest.Get();
		AAuraEncounterVolume* Room = WeakSouthRoom.Get();
		AAuraEncounterVolume* Boss = WeakBossRoom.Get();
		AAuraRitualSite* SouthSite = WeakSouth.Get();
		AAuraRitualSite* BossAltar = WeakAltar.Get();
		AAuraDungeonGate* BossGate = WeakGate.Get();
		if (!Pawn || !State || !Room || !Boss || !SouthSite || !BossAltar || !BossGate)
		{
			UE_LOG(LogAuraSealSmoke, Error, TEXT("AURA_M14_STANDALONE Passed=false Phase2=false"));
			return;
		}
		DefeatCurrentWave(Pawn, Room);
		PlaceAt(Pawn, SouthSite);
		FString FailureReason;
		if (AAuraPlayerState* PlayerState = Pawn->GetPlayerState<AAuraPlayerState>())
		{
			if (UAuraInventoryComponent* Bag = PlayerState->GetInventoryComponent())
			{
				if (UAuraItemDefinition* Staff = UAuraItemCatalog::Get().FindItem(
					TEXT("Equipment.ArcaneStaff")))
				{
					while (Bag->GetOccupiedSlotCount() < Bag->GetCapacity())
					{
						FAuraItemInstance Filler;
						Filler.Definition = Staff;
						Filler.EnsureIdentity();
						if (Bag->TryAddItem(Filler, FailureReason, false) != 1) break;
					}
					Test->bFullBag = Bag->GetOccupiedSlotCount() == Bag->GetCapacity();
				}
			}
		}
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::B, IE_Pressed, 1.f));
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Pressed, 1.f));
		Test->bQuestUI = bInventoryOpen && bInventoryQuestPage;
		InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F, IE_Pressed, 1.f));
		const bool bSouth = !bInventoryOpen && State->HasActivatedSeal(TEXT("LowerSouth"));
		Test->bThreeSeals = bSouth && State->GetActivatedSealCount() == 3
			&& State->AreAllSealsActivated() && BossGate->IsOpen();
		if (const UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			Test->bRunSnapshot = Run->HasActivatedSeal(TEXT("LowerNorth"))
				&& Run->HasActivatedSeal(TEXT("LowerSouth"))
				&& Run->HasActivatedSeal(TEXT("UpperSouth"));
		}
		PlaceAt(Pawn, BossAltar);
		const bool bStarted = BossAltar->TryInteract(Pawn, FailureReason);
		Test->bRitual = bStarted && State->IsBossSummoned()
			&& Boss->GetEncounterState() == EAuraEncounterState::LockedCombat
			&& !BossGate->IsOpen() && !BossAltar->TryInteract(Pawn, FailureReason);
		FTimerHandle BossSpawnTimer;
		GetWorldTimerManager().SetTimer(BossSpawnTimer,
			[this, Test, WeakPawn, WeakQuest, WeakBossRoom, WeakGate]()
		{
			AAuraCharacter* Player = WeakPawn.Get();
			AAuraEncounterVolume* BossEncounter = WeakBossRoom.Get();
			if (!Player || !BossEncounter) return;
			Test->bBossSpawned = BossEncounter->GetActiveEnemies().Num() == 1;
			DefeatCurrentWave(Player, BossEncounter); // phase I -> invulnerable transition
			FTimerHandle BossFinishTimer;
			GetWorldTimerManager().SetTimer(BossFinishTimer,
				[Test, WeakPawn, WeakQuest, WeakBossRoom, WeakGate]()
			{
				AAuraCharacter* Attacker = WeakPawn.Get();
				AAuraGameState* State = WeakQuest.Get();
				AAuraEncounterVolume* BossRoomFinal = WeakBossRoom.Get();
				AAuraDungeonGate* GateFinal = WeakGate.Get();
				if (Attacker && BossRoomFinal) DefeatCurrentWave(Attacker, BossRoomFinal);
				const bool bVictory = State && State->IsRunCompleted()
					&& BossRoomFinal && BossRoomFinal->GetEncounterState()
						== EAuraEncounterState::Completed
					&& GateFinal && GateFinal->IsOpen();
				const bool bPassed = Test->bInitialGate && Test->bEarlyBossRejected
					&& Test->bEarlySealRejected && Test->bFirstTwo && Test->bDistance
					&& Test->bDuplicate && Test->bThreeSeals && Test->bFullBag
					&& Test->bQuestUI
					&& Test->bRunSnapshot && Test->bRitual
					&& Test->bBossSpawned && bVictory;
				UE_LOG(LogAuraSealSmoke, Display,
					TEXT("AURA_M14_STANDALONE Passed=%s Gate=%s EarlyBoss=%s EarlySeal=%s FirstTwo=%s Distance=%s Replay=%s Three=%s FullBag=%s QuestUI=%s Snapshot=%s Ritual=%s Spawn=%s Victory=%s"),
					bPassed ? TEXT("true") : TEXT("false"),
					Test->bInitialGate ? TEXT("true") : TEXT("false"),
					Test->bEarlyBossRejected ? TEXT("true") : TEXT("false"),
					Test->bEarlySealRejected ? TEXT("true") : TEXT("false"),
					Test->bFirstTwo ? TEXT("true") : TEXT("false"),
					Test->bDistance ? TEXT("true") : TEXT("false"),
					Test->bDuplicate ? TEXT("true") : TEXT("false"),
					Test->bThreeSeals ? TEXT("true") : TEXT("false"),
					Test->bFullBag ? TEXT("true") : TEXT("false"),
					Test->bQuestUI ? TEXT("true") : TEXT("false"),
					Test->bRunSnapshot ? TEXT("true") : TEXT("false"),
					Test->bRitual ? TEXT("true") : TEXT("false"),
					Test->bBossSpawned ? TEXT("true") : TEXT("false"),
					bVictory ? TEXT("true") : TEXT("false"));
			}, 3.5f, false);
		}, 1.5f, false);
	}, 2.8f, false);
}

void AAuraPlayerController::AuraTestSealCoopM14()
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone) return;
	AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	AAuraRitualSite* North = FindRitual(GetWorld(), TEXT("LowerNorth"));
	AAuraRitualSite* South = FindRitual(GetWorld(), TEXT("LowerSouth"));
	AAuraRitualSite* Upper = FindRitual(GetWorld(), TEXT("UpperSouth"));
	AAuraRitualSite* Altar = FindRitual(GetWorld(), NAME_None, true);
	AAuraEncounterVolume* Boss = FindRoom(GetWorld(), TEXT("DungeonEncounter_DemonLord"));
	AAuraDungeonGate* Gate = FindBossGate(GetWorld());
	if (!Quest || Quest->PlayerArray.Num() != 2 || !North || !South || !Upper
		|| !Altar || !Boss || !Gate)
	{
		UE_LOG(LogAuraSealSmoke, Error, TEXT("AURA_M14_COOP_SERVER Passed=false Setup=false"));
		return;
	}
	AAuraCharacter* Players[2] = {};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Players[Index] = Quest->PlayerArray[Index]
			? Cast<AAuraCharacter>(Quest->PlayerArray[Index]->GetPawn()) : nullptr;
	}
	if (!Players[0] || !Players[1])
	{
		UE_LOG(LogAuraSealSmoke, Error, TEXT("AURA_M14_COOP_SERVER Passed=false Players=false"));
		return;
	}
	FString Failure;
	Quest->MarkRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"));
	Quest->MarkRewardEncounterCleared(TEXT("LowerSouthLootAmbush"));
	Quest->MarkRewardEncounterCleared(TEXT("UpperSouthBoonGuardian"));
	PlaceAt(Players[0], North);
	const bool bNorth = North->TryInteract(Players[0], Failure);
	PlaceAt(Players[1], South);
	const bool bSouth = South->TryInteract(Players[1], Failure);
	PlaceAt(Players[1], Altar);
	const bool bEarlyRejected = !Altar->TryInteract(Players[1], Failure)
		&& Quest->GetActivatedSealCount() == 2 && !Gate->IsOpen();
	PlaceAt(Players[1], Upper);
	const bool bUpper = Upper->TryInteract(Players[1], Failure);
	const bool bGateOpen = Quest->AreAllSealsActivated() && Gate->IsOpen();
	// The other player remains outside the boss room until the ritual gathers them.
	PlaceAt(Players[0], North);
	PlaceAt(Players[1], Altar);
	const bool bSummoned = Altar->TryInteract(Players[1], Failure);
	const bool bGathered = FVector::DistSquared2D(
		Players[0]->GetActorLocation(), Boss->GetActorLocation()) < FMath::Square(900.f);
	const bool bPassed = bNorth && bSouth && bUpper && bEarlyRejected && bGateOpen
		&& bSummoned && bGathered && Quest->IsBossSummoned()
		&& Boss->GetEncounterState() == EAuraEncounterState::LockedCombat
		&& !Gate->IsOpen();
	UE_LOG(LogAuraSealSmoke, Display,
		TEXT("AURA_M14_COOP_SERVER Passed=%s Seals=%d Early=%s GateOpen=%s Gather=%s Summon=%s"),
		bPassed ? TEXT("true") : TEXT("false"), Quest->GetActivatedSealCount(),
		bEarlyRejected ? TEXT("true") : TEXT("false"),
		bGateOpen ? TEXT("true") : TEXT("false"),
		bGathered ? TEXT("true") : TEXT("false"),
		bSummoned ? TEXT("true") : TEXT("false"));
	FTimerHandle DoneTimer;
	GetWorldTimerManager().SetTimer(DoneTimer, []()
	{
		UE_LOG(LogAuraSealSmoke, Display, TEXT("AURA_M14_COOP_SERVER_DONE"));
	}, 17.f, false);
}

void AAuraPlayerController::AuraObserveSealM14()
{
	const AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	const AAuraEncounterVolume* Boss = FindRoom(GetWorld(), TEXT("DungeonEncounter_DemonLord"));
	const AAuraDungeonGate* Gate = FindBossGate(GetWorld());
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	const bool bPassed = Quest && Quest->GetActivatedSealCount() == 3
		&& Quest->IsBossSummoned() && Boss && Boss->IsCombatActive()
		&& Gate && !Gate->IsOpen() && AuraPawn
		&& FVector::DistSquared2D(AuraPawn->GetActorLocation(), Boss->GetActorLocation())
			< FMath::Square(1000.f);
	UE_LOG(LogAuraSealSmoke, Display,
		TEXT("AURA_M14_COOP_CLIENT Passed=%s Seals=%d Summoned=%s Pawn=%s"),
		bPassed ? TEXT("true") : TEXT("false"),
		Quest ? Quest->GetActivatedSealCount() : -1,
		Quest && Quest->IsBossSummoned() ? TEXT("true") : TEXT("false"),
		AuraPawn ? *AuraPawn->GetActorLocation().ToCompactString() : TEXT("None"));
}
