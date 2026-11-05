// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "AIController.h"
#include "Camera/CameraComponent.h"
#include "Character/AuraEnemy.h"
#include "Coop/AuraReviveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Game/AuraGameMode.h"
#include "Game/AuraGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Level/AuraEncounterVolume.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	struct FRescueSmoke
	{
		TWeakObjectPtr<AAuraPlayerController> Controllers[2];
		TWeakObjectPtr<AAuraCharacter> Pawns[2];
		TWeakObjectPtr<AAuraEncounterVolume> BossRoom;
		TWeakObjectPtr<AAuraEnemy> OldBoss;
		FGuid BagIds[2];
		FGuid ArmorIds[2];
		FIntPoint InitialGrowth[2];
		FVector Origin;
		bool bPassed = true;
		int32 Checks = 0;
	};

	void CheckRescue(const TSharedRef<FRescueSmoke>& Test, const TCHAR* Name, const bool bResult)
	{
		Test->bPassed &= bResult;
		++Test->Checks;
		UE_LOG(LogTemp, Display, TEXT("AURA_RESCUE_CHECK Name=%s Passed=%s"),
			Name, bResult ? TEXT("true") : TEXT("false"));
	}

	void Later(UWorld* World, const float Delay, TFunction<void()> Action)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, [Action = MoveTemp(Action)]() { Action(); }, Delay, false);
	}

	void SetHealth(AAuraCharacter* Pawn, const float Value)
	{
		if (Pawn) Pawn->GetAbilitySystemComponent()->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), Value);
	}

	float Health(const AAuraCharacter* Pawn)
	{
		return Pawn->GetAbilitySystemComponent()->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	}

	bool VitalsEqual(const AAuraCharacter* Pawn, const float Fraction)
	{
		if (!Pawn || Pawn->IsDead()) return false;
		const UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent();
		return FMath::IsNearlyEqual(Health(Pawn), Fraction * ASC->GetNumericAttribute(
			UAuraAttributeSet::GetMaxHealthAttribute()), 0.1f)
			&& FMath::IsNearlyEqual(ASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()),
				Fraction * ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()), 0.1f);
	}

	void Damage(AAuraCharacter* Pawn)
	{
		UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent();
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAuraDamageGameplayEffect::StaticClass(),
			1.f, ASC->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -1.f);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}

	void StopEnemyAI(UWorld* World)
	{
		for (TActorIterator<AAuraEnemy> It(World); It; ++It)
		{
			if (AAIController* AI = Cast<AAIController>(It->GetController()))
			{
				AI->StopMovement();
				AI->SetActorTickEnabled(false);
			}
			if (It->GetAbilitySystemComponent()) It->GetAbilitySystemComponent()->CancelAllAbilities();
		}
	}
}

void AAuraPlayerController::ClientDriveCoopRescueSmoke_Implementation(const int32 Command)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("AuraRescueCoopSmoke"))) return;
	if (Command == 3) { ConsoleCommand(TEXT("quit")); return; }
	if (Command == 5) { PrimaryAttack(); return; }
	if (Command == 6)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("AuraRescueCapture")))
		{
			const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
			const TCHAR* Stage = AuraPawn->IsDead() ? TEXT("Spectating")
				: AuraPawn->GetReviveComponent()->IsRescuing() ? TEXT("Channel") : TEXT("Recovered");
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("AuraCoopRescue_%s.png"), Stage), false, false);
		}
		return;
	}
	const FKey Key = Command == 2 || Command == 4 ? EKeys::W : EKeys::F;
	const bool bPressed = Command == 1 || Command == 2;
	if (Command == 1) SetControlRotation(FRotator(-62.f, 37.f, 0.f));
	InputKey(FInputKeyEventArgs::CreateSimulated(Key, bPressed ? IE_Pressed : IE_Released, bPressed ? 1.f : 0.f));
	UE_LOG(LogTemp, Display, TEXT("AURA_RESCUE_CLIENT_INPUT Command=%d Held=%s Candidate=%s"),
		Command, bRescueInputHeld ? TEXT("true") : TEXT("false"), *GetNameSafe(FindRescueCandidate()));
}

void AAuraPlayerController::ObserveCoopRescueSmoke()
{
	if (!IsLocalController() || !FParse::Param(FCommandLine::Get(), TEXT("AuraRescueCoopSmoke"))) return;
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!AuraPawn) return;
	uint8 Seen = RescueSmokeObservations;
	if (bInputContextsInitialized && AuraPawn->GetAbilitySystemComponent()) Seen |= 64;
	const AAuraCharacter* Watched = GetSpectatedTeammate();
	if (AuraPawn->IsDead() && IsCoopSpectating() && Watched && GetViewTarget() == Watched) Seen |= 1;
	if (AuraPawn->GetReviveComponent()->GetRescueProgress() > 0.05f) Seen |= 2;
	if ((Seen & 1) && !AuraPawn->IsDead() && !IsCoopSpectating() && GetViewTarget() == AuraPawn) Seen |= 4;
	if (FindRescueCandidate()) Seen |= 8;
	if (Watched && !Watched->IsDead() && GetViewTarget() == Watched
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(Watched->GetViewRotation().Yaw,
			Watched->GetFollowCamera()->GetComponentRotation().Yaw)) < 2.f) Seen |= 16;
	if (Seen == RescueSmokeObservations) return;
	RescueSmokeObservations = Seen;
	ServerReportCoopRescueSmoke(Seen);
	UE_LOG(LogTemp, Display, TEXT("AURA_RESCUE_CLIENT_OBSERVED Flags=%d"), Seen);
}

void AAuraPlayerController::ServerReportCoopRescueSmoke_Implementation(const uint8 Observations)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraRescueCoopSmoke")))
		RescueSmokeObservations |= Observations;
}

void AAuraPlayerController::AuraTestCoopRescue()
{
	if (!HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("AuraRescueCoopSmoke"))
		|| GetWorld()->GetFirstPlayerController() != this) return;
	TArray<AAuraPlayerController*> Members;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (AAuraPlayerController* PC = Cast<AAuraPlayerController>(It->Get()); PC && PC->GetPawn()) Members.Add(PC);
	const bool bClientsReady = Members.Num() == 2 && (Members[0]->RescueSmokeObservations & 64)
		&& (Members[1]->RescueSmokeObservations & 64);
	if (!bClientsReady)
	{
		if (++RescueSmokeStartAttempts < 45)
		{
			FTimerHandle Retry;
			GetWorldTimerManager().SetTimer(Retry, this, &ThisClass::AuraTestCoopRescue, 0.5f, false);
		}
		else UE_LOG(LogTemp, Error, TEXT("AURA_RESCUE_COOP_RESULT Passed=false Players=%d AURA_RESCUE_COOP_DONE"), Members.Num());
		return;
	}
	const TSharedRef<FRescueSmoke> Test = MakeShared<FRescueSmoke>();
	UWorld* World = GetWorld();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Test->Controllers[Index] = Members[Index];
		Test->Pawns[Index] = Cast<AAuraCharacter>(Members[Index]->GetPawn());
		AAuraPlayerState* State = Members[Index]->GetPlayerState<AAuraPlayerState>();
		FString Failure;
		FAuraItemInstance Potion;
		Potion.Definition = UAuraItemCatalog::Get().FindItem(TEXT("Consumable.HealthPotion"));
		Potion.Quantity = 2;
		Potion.EnsureIdentity();
		Test->BagIds[Index] = Potion.InstanceId;
		FAuraItemInstance Armor;
		Armor.Definition = UAuraItemCatalog::Get().FindItem(TEXT("Equipment.WardenArmor"));
		Armor.Quantity = 1;
		Armor.EnsureIdentity();
		Test->ArmorIds[Index] = Armor.InstanceId;
		const bool bSeeded = State->GetInventoryComponent()->TryAddItem(Potion, Failure, false) == 2
			&& State->GetInventoryComponent()->TryAddItem(Armor, Failure, false) == 1
			&& State->GetEquipmentComponent()->TryEquipItem(Armor.InstanceId, Failure);
		CheckRescue(Test, Index == 0 ? TEXT("SeedPlayer0Inventory") : TEXT("SeedPlayer1Inventory"), bSeeded);
		State->AwardExperienceAndSouls(10, 15);
		Test->InitialGrowth[Index] = FIntPoint(State->GetGrowthState().TotalExperience, State->GetGrowthState().Souls);
		Test->Pawns[Index]->RestoreVitalsAtCheckpoint();
	}
	Test->Origin = Test->Pawns[0]->GetActorLocation();
	for (TActorIterator<AAuraEncounterVolume> It(World); It; ++It)
	{
		It->FindComponentByClass<UBoxComponent>()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (It->IsFinalEncounter()) Test->BossRoom = *It;
	}
	StopEnemyAI(World);
	AAuraGameState* Quest = World->GetGameState<AAuraGameState>();
	Quest->SetActiveCheckpoint(TEXT("RescueSmokeCheckpoint"), FTransform(FRotator::ZeroRotator, Test->Origin));
	Quest->MarkRewardEncounterCleared(TEXT("RescueSmokeCompletedRoom"));
	Test->Pawns[1]->TeleportTo(Test->Origin + FVector(110.f, 0.f, 0.f), FRotator::ZeroRotator, false, true);
	SetHealth(Test->Pawns[0].Get(), 0.f);
	CheckRescue(Test, TEXT("Downed"), Test->Pawns[0]->IsDead()
		&& Test->Pawns[0]->GetReviveComponent()->IsWaitingForRescue());
	World->GetAuthGameMode<AAuraGameMode>()->RequestPlayerRespawn(Members[0]);
	CheckRescue(Test, TEXT("ManualRespawnRejected"), Test->Pawns[0]->IsDead());
	Later(World, 0.8f, [Test]()
	{
		AAuraCharacter* Rescuer = Test->Pawns[1].Get();
		UAuraReviveComponent* Component = Rescuer->GetReviveComponent();
		Rescuer->TeleportTo(Test->Origin + FVector(600.f, 0.f, 0.f), FRotator::ZeroRotator, false, true);
		CheckRescue(Test, TEXT("DistanceRejected"), !Component->CanRescue(Test->Pawns[0].Get()));
		Rescuer->TeleportTo(Test->Origin + FVector(110.f, 0.f, 400.f), FRotator::ZeroRotator, false, true);
		CheckRescue(Test, TEXT("OtherFloorRejected"), !Component->CanRescue(Test->Pawns[0].Get()));
		Rescuer->TeleportTo(Test->Origin + FVector(110.f, 0.f, 0.f), FRotator::ZeroRotator, false, true);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Blocker = Rescuer->GetWorld()->SpawnActor<AStaticMeshActor>(
			Test->Origin + FVector(55.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
		Blocker->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Blocker->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Blocker->SetActorScale3D(FVector(0.1f, 2.f, 2.f));
		Blocker->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		CheckRescue(Test, TEXT("WallRejected"), !Component->CanRescue(Test->Pawns[0].Get()));
		Blocker->Destroy();
		CheckRescue(Test, TEXT("NearbyAllowed"), Component->CanRescue(Test->Pawns[0].Get()));
	});
	Later(World, 1.4f, [Test]() { Test->Controllers[1]->ClientDriveCoopRescueSmoke(1); });
	Later(World, 2.5f, [Test]()
	{
		AAuraCharacter* Rescuer = Test->Pawns[1].Get();
		CheckRescue(Test, TEXT("RealClientStarted"), Rescuer->GetReviveComponent()->IsRescuing()
			&& Test->Pawns[0]->GetReviveComponent()->GetRescueProgress() > 0.05f);
		UAbilitySystemComponent* ASC = Rescuer->GetAbilitySystemComponent();
		const FGameplayAbilitySpec* Fire = ASC->FindAbilitySpecFromClass(UAuraFireBoltAbility::StaticClass());
		// TryActivate on a server may return true merely because it routed a
		// LocalPredicted request to its owning client. Check authority permission.
		CheckRescue(Test, TEXT("CastBlockedDuringRescue"), Fire && !Fire->Ability->CanActivateAbility(
			Fire->Handle, ASC->AbilityActorInfo.Get()));
		const float Before = Health(Rescuer);
		Damage(Rescuer);
		CheckRescue(Test, TEXT("OrdinaryDamageContinues"), Health(Rescuer) < Before
			&& Rescuer->GetReviveComponent()->IsRescuing());
		Test->Controllers[1]->ClientDriveCoopRescueSmoke(0);
	});
	Later(World, 3.1f, [Test]()
	{
		CheckRescue(Test, TEXT("ReleaseCancels"), !Test->Pawns[1]->GetReviveComponent()->IsRescuing()
			&& Test->Pawns[0]->GetReviveComponent()->GetRescueProgress() == 0.f);
		Test->Controllers[1]->ClientDriveCoopRescueSmoke(1);
	});
	Later(World, 4.1f, [Test]() { Test->Controllers[1]->ClientDriveCoopRescueSmoke(2); });
	Later(World, 4.2f, [Test]() { Test->Controllers[1]->ClientDriveCoopRescueSmoke(4); Test->Controllers[1]->ClientDriveCoopRescueSmoke(0); });
	Later(World, 4.8f, [Test]()
	{
		CheckRescue(Test, TEXT("MovementCancels"), !Test->Pawns[1]->GetReviveComponent()->IsRescuing());
		CheckRescue(Test, TEXT("NoAutomaticRespawn"), Test->Pawns[0]->IsDead());
		Test->Pawns[1]->TeleportTo(Test->Origin + FVector(110.f, 0.f, 0.f), FRotator::ZeroRotator, false, true);
		Test->Controllers[1]->ClientDriveCoopRescueSmoke(1);
	});
	Later(World, 5.7f, [Test]()
	{
		Test->Pawns[1]->GetAbilitySystemComponent()->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")));
	});
	Later(World, 6.2f, [Test]()
	{
		CheckRescue(Test, TEXT("StunCancels"), !Test->Pawns[1]->GetReviveComponent()->IsRescuing());
		Test->Pawns[1]->GetAbilitySystemComponent()->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")));
		Test->Controllers[1]->ClientDriveCoopRescueSmoke(0);
	});
	Later(World, 6.6f, [Test]() { Test->Controllers[1]->ClientDriveCoopRescueSmoke(1); });
	Later(World, 7.8f, [Test]()
	{
		UAuraReviveComponent* Component = Test->Pawns[1]->GetReviveComponent();
		CheckRescue(Test, TEXT("DuplicateRejected"), !Component->TryBeginRescue(Test->Pawns[0].Get(), 1));
		Component->CancelRescue(1);
		CheckRescue(Test, TEXT("OldCancelIgnored"), Component->IsRescuing());
	});
	Later(World, 8.8f, [Test]()
	{
		for (int32 Index = 0; Index < 2; ++Index) Test->Controllers[Index]->ClientDriveCoopRescueSmoke(6);
	});
	Later(World, 11.3f, [Test]() { CheckRescue(Test, TEXT("FiveSecondsRequired"), Test->Pawns[0]->IsDead()); });
	Later(World, 12.4f, [Test]()
	{
		AAuraCharacter* Revived = Test->Pawns[0].Get();
		CheckRescue(Test, TEXT("TwentyPercentVitals"), VitalsEqual(Revived, 0.2f));
		const float Before = Health(Revived);
		Damage(Revived);
		CheckRescue(Test, TEXT("GetUpProtected"), Revived->GetReviveComponent()->IsProtected()
			&& FMath::IsNearlyEqual(Before, Health(Revived)));
		Test->Controllers[0]->ClientDriveCoopRescueSmoke(5);
		Test->Controllers[1]->ClientDriveCoopRescueSmoke(0);
	});
	Later(World, 12.1f, [Test]() { Test->Controllers[0]->ClientDriveCoopRescueSmoke(6); });
	Later(World, 13.f, [Test]()
	{
		AAuraCharacter* Revived = Test->Pawns[0].Get();
		const float Before = Health(Revived);
		Damage(Revived);
		CheckRescue(Test, TEXT("AttackEndsProtection"), !Revived->GetReviveComponent()->IsProtected()
			&& Health(Revived) < Before);
	});
	Later(World, 13.4f, [Test]()
	{
		SetHealth(Test->Pawns[1].Get(), 0.f);
	});
	Later(World, 14.1f, [Test]() { Test->Controllers[0]->ClientDriveCoopRescueSmoke(1); });
	Later(World, 19.9f, [Test]()
	{
		CheckRescue(Test, TEXT("SymmetricRescue"), VitalsEqual(Test->Pawns[1].Get(), 0.2f));
		Test->Controllers[0]->ClientDriveCoopRescueSmoke(0);
		for (int32 Index = 0; Index < 2; ++Index)
			CheckRescue(Test, Index == 0 ? TEXT("Client0CameraAndUI") : TEXT("Client1CameraAndUI"),
				(Test->Controllers[Index]->RescueSmokeObservations & 31) == 31);
	});
	Later(World, 20.4f, [Test, World]()
	{
		AAuraGameState* State = World->GetGameState<AAuraGameState>();
		for (int32 Index = 0; Index < 3; ++Index) State->TryActivateSeal(AAuraGameState::RequiredSealIds()[Index]);
		const bool bBossStarted = Test->BossRoom.IsValid()
			&& Test->BossRoom->ActivateFromRitual(Test->Pawns[0].Get(), true) && State->TryMarkBossSummoned();
		CheckRescue(Test, TEXT("BossSetup"), bBossStarted);
		StopEnemyAI(World);
		if (bBossStarted)
		{
			for (AAuraEnemy* Enemy : Test->BossRoom->GetActiveEnemies())
				if (Enemy->IsBossEnemy()) Test->OldBoss = Enemy;
			if (Test->OldBoss.IsValid())
			{
				UAbilitySystemComponent* ASC = Test->OldBoss->GetAbilitySystemComponent();
				ASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(),
					ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()) * 0.75f);
			}
		}
		SetHealth(Test->Pawns[0].Get(), 0.f);
		SetHealth(Test->Pawns[1].Get(), 0.f);
		CheckRescue(Test, TEXT("TeamWipePending"), State->IsTeamWipePending());
	});
	Later(World, 24.2f, [Test, World]()
	{
		AAuraGameState* State = World->GetGameState<AAuraGameState>();
		CheckRescue(Test, TEXT("CheckpointRecovery"), VitalsEqual(Test->Pawns[0].Get(), 1.f)
			&& VitalsEqual(Test->Pawns[1].Get(), 1.f) && !State->IsTeamWipePending()
			&& FVector::Dist(Test->Pawns[0]->GetActorLocation(), Test->Origin) < 220.f
			&& FVector::Dist(Test->Pawns[1]->GetActorLocation(), Test->Origin) < 220.f);
		CheckRescue(Test, TEXT("BossAndRoomReset"), (!Test->OldBoss.IsValid() || Test->OldBoss->IsActorBeingDestroyed())
			&& !State->IsBossSummoned()
			&& Test->BossRoom.IsValid() && Test->BossRoom->GetEncounterState() == EAuraEncounterState::Dormant
			&& Test->BossRoom->AreControlledGatesOpen());
		CheckRescue(Test, TEXT("CompletedProgressPreserved"), State->GetActivatedSealCount() == 3
			&& State->IsRewardEncounterCleared(TEXT("RescueSmokeCompletedRoom")));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const AAuraPlayerState* PlayerState = Test->Controllers[Index]->GetPlayerState<AAuraPlayerState>();
			const FAuraInventoryEntry* Bag = PlayerState->GetInventoryComponent()->FindEntryByInstanceId(Test->BagIds[Index]);
			const FAuraItemInstance* Armor = PlayerState->GetEquipmentComponent()->GetEquippedItem(EAuraEquipmentSlot::Armor);
			CheckRescue(Test, Index == 0 ? TEXT("Player0Persistence") : TEXT("Player1Persistence"),
				Bag && Bag->Item.Quantity == 2 && Armor && Armor->InstanceId == Test->ArmorIds[Index]
				&& Test->Controllers[Index]->GetPawn() == Test->Pawns[Index].Get()
				&& PlayerState->GetGrowthState().TotalExperience == Test->InitialGrowth[Index].X
				&& PlayerState->GetGrowthState().Souls == Test->InitialGrowth[Index].Y);
		}
		const bool bRestarted = Test->BossRoom.IsValid()
			&& Test->BossRoom->ActivateFromRitual(Test->Pawns[0].Get(), true) && State->TryMarkBossSummoned();
		StopEnemyAI(World);
		bool bFullBoss = false;
		if (bRestarted) for (AAuraEnemy* Enemy : Test->BossRoom->GetActiveEnemies())
		{
			UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
			bFullBoss |= Enemy->IsBossEnemy() && Enemy->GetBossPhase() == EAuraBossPhase::PhaseOne
				&& FMath::IsNearlyEqual(ASC->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()),
					ASC->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()));
		}
		CheckRescue(Test, TEXT("BossRetryFullHealth"), bRestarted && bFullBoss);
	});
	Later(World, 25.f, [Test]()
	{
		SetHealth(Test->Pawns[0].Get(), 0.f);
		Test->Controllers[1]->ClientDriveCoopRescueSmoke(3);
	});
	Later(World, 31.f, [Test, World]()
	{
		CheckRescue(Test, TEXT("DisconnectFallback"), Test->Pawns[0].IsValid()
			&& !Test->Pawns[0]->IsDead() && !World->GetGameState<AAuraGameState>()->IsTeamWipePending());
		UE_LOG(LogTemp, Display, TEXT("AURA_RESCUE_COOP_RESULT Passed=%s Checks=%d AURA_RESCUE_COOP_DONE"),
			Test->bPassed ? TEXT("true") : TEXT("false"), Test->Checks);
	});
}
