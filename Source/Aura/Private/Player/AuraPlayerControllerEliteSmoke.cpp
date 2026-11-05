// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraLootProfileData.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Level/AuraEncounterVolume.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraEliteSmoke, Log, All);

void AAuraPlayerController::AuraTestEliteVariants()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	AAuraEncounterVolume* Intro = nullptr;
	AAuraEncounterVolume* Nest = nullptr;
	AAuraEncounterVolume* Patrol = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		switch (It->GetEncounterOrder())
		{
		case 1: Intro = *It; break;
		case 105: Nest = *It; break;
		case 106: Patrol = *It; break;
		default: break;
		}
	}
	const FAuraEliteEnemyBalance& Balance = UAuraCombatBalanceData::Get().EliteEnemy;
	FString ValidationError;
	const bool bBalance = UAuraCombatBalanceData::Get().Validate(ValidationError)
		&& UAuraLootProfileData::Get().EliteEnemy.GuaranteedArcaneOre >= 1;
	const bool bIntro = Intro && Intro->GetConfiguredSpawnCount() == 2
		&& Intro->GetEliteSpawnIndices().IsEmpty()
		&& Intro->GetActiveEnemies().Num() == 2
		&& !Intro->GetActiveEnemies()[0]->IsEliteVariant()
		&& !Intro->GetActiveEnemies()[1]->IsEliteVariant();
	const bool bSlots = Nest && Patrol
		&& Nest->GetEliteSpawnIndices().Num() == 1
		&& Nest->GetEliteSpawnIndices()[0] == 2
		&& Patrol->GetEliteSpawnIndices().Num() == 1
		&& Patrol->GetEliteSpawnIndices()[0] == 0;

	AAuraEnemy* StandingElite = nullptr;
	if (Patrol)
	{
		for (AAuraEnemy* Enemy : Patrol->GetActiveEnemies())
		{
			if (Enemy && Enemy->IsEliteVariant()) StandingElite = Enemy;
		}
	}
	const UAbilitySystemComponent* ASC = StandingElite
		? StandingElite->GetAbilitySystemComponent() : nullptr;
	const bool bStanding = StandingElite && ASC
		&& StandingElite->GetEnemyArchetype() == EAuraEnemyArchetype::GoblinSpearman
		&& FMath::IsNearlyEqual(ASC->GetNumericAttribute(
			UAuraAttributeSet::GetMaxHealthAttribute()),
			UAuraCombatBalanceData::Get().MeleeEnemy.MaxHealth * Balance.MaxHealthMultiplier)
		&& FMath::IsNearlyEqual(StandingElite->GetActorScale3D().X, Balance.VisualScale)
		&& FMath::IsNearlyEqual(StandingElite->GetCharacterMovement()->MaxWalkSpeed,
			UAuraCombatBalanceData::Get().MeleeEnemy.WalkSpeed * Balance.WalkSpeedMultiplier)
		&& FMath::IsNearlyEqual(StandingElite->GetDifficultyDamageMultiplier(),
			Balance.DamageMultiplier)
		&& FMath::IsNearlyEqual(StandingElite->GetEliteAttackIntervalMultiplier(),
			Balance.AttackIntervalMultiplier)
		&& FMath::IsNearlyEqual(StandingElite->GetEliteSpecialChanceBonus(),
			Balance.SpecialChanceBonus)
		&& StandingElite->GetKillExperienceReward() == Balance.KillExperienceReward
		&& StandingElite->GetLootProfile() == EAuraLootProfileType::EliteEnemy;
	bool bDamageDoesNotLock = StandingElite && ASC && !StandingElite->IsActionLocked();
	if (bDamageDoesNotLock)
	{
		UAbilitySystemComponent* SourceASC = AuraPawn
			? AuraPawn->GetAbilitySystemComponent() : nullptr;
		const float InitialHealth = ASC->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute());
		for (int32 Hit = 1; Hit <= 3; ++Hit)
		{
			if (!SourceASC) { bDamageDoesNotLock = false; break; }
			FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
			Context.AddSourceObject(AuraPawn);
			FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
				UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
			if (!Spec.IsValid()) { bDamageDoesNotLock = false; break; }
			Spec.Data->SetSetByCallerMagnitude(
				FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -5.f);
			StandingElite->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(
				*Spec.Data.Get());
			bDamageDoesNotLock &= !StandingElite->IsActionLocked()
				&& !StandingElite->IsStunned();
		}
		bDamageDoesNotLock &= ASC->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute()) < InitialHealth;
	}

	if (Nest && AuraPawn) Nest->StartEncounter(AuraPawn, true);
	AAuraEnemy* SpawnedElite = nullptr;
	if (Nest)
	{
		for (AAuraEnemy* Enemy : Nest->GetActiveEnemies())
		{
			if (Enemy && Enemy->IsEliteVariant()) SpawnedElite = Enemy;
		}
	}
	const bool bGroundSpawn = Nest && SpawnedElite
		&& Nest->GetActiveEnemyCount() == 3
		&& SpawnedElite->GetEnemyArchetype() == EAuraEnemyArchetype::CaveShroom
		&& SpawnedElite->GetLootProfile() == EAuraLootProfileType::EliteEnemy
		&& FMath::IsNearlyEqual(SpawnedElite->GetActorScale3D().X, Balance.VisualScale);

	bool bEnrage = false;
	if (StandingElite && ASC)
	{
		const float MaxHealth = ASC->GetNumericAttribute(
			UAuraAttributeSet::GetMaxHealthAttribute());
		StandingElite->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetHealthAttribute(),
			MaxHealth * Balance.EnrageHealthFraction * 0.9f);
		bEnrage = StandingElite->IsEliteEnraged()
			&& FMath::IsNearlyEqual(StandingElite->GetEliteAttackIntervalMultiplier(),
				Balance.AttackIntervalMultiplier * Balance.EnragedAttackIntervalMultiplier)
			&& FMath::IsNearlyEqual(StandingElite->GetEliteSpecialChanceBonus(),
				Balance.SpecialChanceBonus + Balance.EnragedSpecialChanceBonus);
	}
	const bool bPassed = HasAuthority() && IsLocalController() && AuraPawn
		&& bBalance && bIntro && bSlots && bStanding && bGroundSpawn
		&& bEnrage && bDamageDoesNotLock;
	UE_LOG(LogAuraEliteSmoke, Display,
		TEXT("AURA_ELITE_VARIANT Passed=%s Balance=%s Intro=%s Slots=%s Standing=%s Ground=%s Enrage=%s DamageNoLock=%s Error=%s"),
		bPassed ? TEXT("true") : TEXT("false"),
		bBalance ? TEXT("true") : TEXT("false"),
		bIntro ? TEXT("true") : TEXT("false"),
		bSlots ? TEXT("true") : TEXT("false"),
		bStanding ? TEXT("true") : TEXT("false"),
		bGroundSpawn ? TEXT("true") : TEXT("false"),
		bEnrage ? TEXT("true") : TEXT("false"),
		bDamageDoesNotLock ? TEXT("true") : TEXT("false"),
		ValidationError.IsEmpty() ? TEXT("None") : *ValidationError);
}

void AAuraPlayerController::AuraTestEliteSkillKits()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	UAbilitySystemComponent* PlayerASC = AuraPawn
		? AuraPawn->GetAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !IsLocalController() || !AuraPawn || !PlayerASC || !GetWorld())
	{
		UE_LOG(LogAuraEliteSmoke, Error,
			TEXT("AURA_ELITE_SKILLS Passed=false Setup=invalid player or world"));
		return;
	}
	PlayerASC->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
	PlayerASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 1000.f);
	AuraPawn->GetCharacterMovement()->StopMovementImmediately();
	const FAuraEliteEnemyBalance& Balance = UAuraCombatBalanceData::Get().EliteEnemy;
	const FVector PlayerLocation = AuraPawn->GetActorLocation();
	const auto SpawnElite = [this, AuraPawn, PlayerLocation, &Balance](
		const TCHAR* ClassPath, const FVector& Offset) -> AAuraEnemy*
	{
		UClass* Class = LoadClass<AAuraEnemy>(nullptr, ClassPath);
		if (!Class) return nullptr;
		const FVector SpawnLocation = PlayerLocation + Offset;
		const FTransform Transform((PlayerLocation - SpawnLocation).Rotation(),
			SpawnLocation, FVector(Balance.VisualScale));
		AAuraEnemy* Enemy = GetWorld()->SpawnActorDeferred<AAuraEnemy>(
			Class, Transform, this, AuraPawn,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Enemy) return nullptr;
		Enemy->ConfigureEliteVariant(FVector(Balance.VisualScale));
		UGameplayStatics::FinishSpawningActor(Enemy, Transform);
		return Enemy;
	};
	AAuraEnemy* Shroom = SpawnElite(
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom.BP_AuraShroom_C"),
		FVector(0.f, 170.f, 0.f));
	AAuraEnemy* Goblin = SpawnElite(
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraGoblinSpear.BP_AuraGoblinSpear_C"),
		FVector(-325.f, 0.f, 0.f));
	if (!Shroom || !Goblin)
	{
		UE_LOG(LogAuraEliteSmoke, Error,
			TEXT("AURA_ELITE_SKILLS Passed=false Spawn=Shroom:%s Goblin:%s"),
			*GetNameSafe(Shroom), *GetNameSafe(Goblin));
		return;
	}
	struct FEliteSkillObservations
	{
		bool bShroomWarning = false;
		bool bFieldCreated = false;
		bool bFieldArmed = false;
		bool bFieldGeometry = false;
		bool bSporeExposure = false;
		bool bGoblinLunge = false;
		bool bSweepWarning = false;
		bool bSweepGeometry = false;
		bool bFieldClearedOnDeath = false;
	};
	const TSharedRef<FEliteSkillObservations> Seen = MakeShared<FEliteSkillObservations>();
	const TSharedRef<FTimerHandle> ObserveHandle = MakeShared<FTimerHandle>();
	const TWeakObjectPtr<AAuraCharacter> WeakPlayer(AuraPawn);
	const TWeakObjectPtr<AAuraEnemy> WeakShroom(Shroom);
	const TWeakObjectPtr<AAuraEnemy> WeakGoblin(Goblin);
	FTimerDelegate Observe;
	Observe.BindLambda([this, Seen, WeakPlayer, WeakShroom, WeakGoblin,
		PlayerLocation]()
	{
		const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
		if (const AAuraCharacter* CurrentPlayer = WeakPlayer.Get())
		{
			Seen->bSporeExposure |= CurrentPlayer->HasSporeExposure();
		}
		if (const AAuraEnemy* CurrentShroom = WeakShroom.Get())
		{
			Seen->bShroomWarning |= CurrentShroom->GetMeleeAction()
				== EAuraMeleeAction::EliteShroomSeed;
			const FAuraEliteSporeFieldState& Field = CurrentShroom->GetEliteSporeField();
			Seen->bFieldCreated |= Field.bActive;
			Seen->bFieldGeometry |= Field.bActive
				&& FMath::IsNearlyEqual(Elite.ShroomSeedRadius, 240.f)
				&& FMath::IsNearlyEqual(Field.Radius, Elite.ShroomSeedRadius)
				&& FVector::DistSquared2D(Field.Location, PlayerLocation)
					< FMath::Square(100.f);
			Seen->bFieldArmed |= Field.bActive && GetWorld()
				&& GetWorld()->GetTimeSeconds() >= Field.ArmedServerTime;
		}
		if (const AAuraEnemy* CurrentGoblin = WeakGoblin.Get())
		{
			Seen->bGoblinLunge |= CurrentGoblin->GetMeleeAction()
				== EAuraMeleeAction::GoblinLunge;
			if (CurrentGoblin->GetMeleeAction() == EAuraMeleeAction::EliteGoblinSweep)
			{
				Seen->bSweepWarning = true;
				const FVector WarnDirection = (CurrentGoblin->GetMeleeTelegraphLocation()
					- CurrentGoblin->GetActorLocation()).GetSafeNormal2D();
				Seen->bSweepGeometry |= FMath::IsNearlyEqual(
					CurrentGoblin->GetMeleeTelegraphRadius(), Elite.GoblinSweepRange)
					&& FVector::DotProduct(WarnDirection,
						CurrentGoblin->GetActorForwardVector().GetSafeNormal2D()) > 0.99f;
			}
		}
	});
	GetWorldTimerManager().SetTimer(*ObserveHandle, Observe, 0.04f, true, 0.04f);
	FTimerHandle DeathTimer;
	GetWorldTimerManager().SetTimer(DeathTimer,
		[Seen, WeakShroom]()
	{
		AAuraEnemy* CurrentShroom = WeakShroom.Get();
		if (!CurrentShroom || !CurrentShroom->GetEliteSporeField().bActive) return;
		CurrentShroom->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetHealthAttribute(), 0.f);
		Seen->bFieldClearedOnDeath = CurrentShroom->IsDead()
			&& !CurrentShroom->GetEliteSporeField().bActive;
	}, 4.2f, false);
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, Seen, ObserveHandle, WeakShroom, WeakGoblin]()
	{
		GetWorldTimerManager().ClearTimer(*ObserveHandle);
		const bool bPassed = WeakShroom.IsValid() && WeakGoblin.IsValid()
			&& Seen->bShroomWarning && Seen->bFieldCreated
			&& Seen->bFieldArmed && Seen->bFieldGeometry
			&& Seen->bSporeExposure && Seen->bGoblinLunge
			&& Seen->bSweepWarning && Seen->bSweepGeometry
			&& Seen->bFieldClearedOnDeath;
		UE_LOG(LogAuraEliteSmoke, Display,
			TEXT("AURA_ELITE_SKILLS Passed=%s Seed=%s Field=%s Armed=%s Geometry=%s Exposure=%s Lunge=%s Sweep=%s SweepGeometry=%s DeathCleanup=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			Seen->bShroomWarning ? TEXT("true") : TEXT("false"),
			Seen->bFieldCreated ? TEXT("true") : TEXT("false"),
			Seen->bFieldArmed ? TEXT("true") : TEXT("false"),
			Seen->bFieldGeometry ? TEXT("true") : TEXT("false"),
			Seen->bSporeExposure ? TEXT("true") : TEXT("false"),
			Seen->bGoblinLunge ? TEXT("true") : TEXT("false"),
			Seen->bSweepWarning ? TEXT("true") : TEXT("false"),
			Seen->bSweepGeometry ? TEXT("true") : TEXT("false"),
			Seen->bFieldClearedOnDeath ? TEXT("true") : TEXT("false"));
	}, 5.f, false);
}

void AAuraPlayerController::AuraTestEliteReplicationServer()
{
	AAuraEncounterVolume* Patrol = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 106) { Patrol = *It; break; }
	}
	int32 PlayerCount = 0;
	AAuraCharacter* FirstPlayerPawn = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		AAuraCharacter* TargetPawn = Controller
			? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
		if (!Patrol || !TargetPawn) continue;
		if (!FirstPlayerPawn) FirstPlayerPawn = TargetPawn;
		TargetPawn->TeleportTo(Patrol->GetActorLocation()
			+ FVector(0.f, PlayerCount * 90.f, 100.f),
			Patrol->GetActorRotation(), false, true);
		if (UAbilitySystemComponent* PlayerASC = TargetPawn->GetAbilitySystemComponent())
		{
			PlayerASC->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 500.f);
			PlayerASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 500.f);
		}
		++PlayerCount;
	}
	AAuraEnemy* ReplicatedShroom = nullptr;
	AAuraEnemy* ReplicatedShaman = nullptr;
	if (FirstPlayerPawn)
	{
		UClass* ShroomClass = LoadClass<AAuraEnemy>(nullptr,
			TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraShroom.BP_AuraShroom_C"));
		if (ShroomClass)
		{
			const FVector ShroomSpawnLocation = FirstPlayerPawn->GetActorLocation()
				+ FVector(0.f, 170.f, 0.f);
			const float Scale = UAuraCombatBalanceData::Get().EliteEnemy.VisualScale;
			const FTransform Transform((FirstPlayerPawn->GetActorLocation()
				- ShroomSpawnLocation).Rotation(), ShroomSpawnLocation, FVector(Scale));
			ReplicatedShroom = GetWorld()->SpawnActorDeferred<AAuraEnemy>(
				ShroomClass, Transform, this, FirstPlayerPawn,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (ReplicatedShroom)
			{
				ReplicatedShroom->ConfigureEliteVariant(FVector(Scale));
				UGameplayStatics::FinishSpawningActor(ReplicatedShroom, Transform);
			}
		}
		UClass* ShamanClass = LoadClass<AAuraEnemy>(nullptr,
			TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraEliteShaman.BP_AuraEliteShaman_C"));
		if (ShamanClass)
		{
			const FVector ShamanSpawnLocation = FirstPlayerPawn->GetActorLocation()
				+ FVector(0.f, -320.f, 0.f);
			const FTransform Transform((FirstPlayerPawn->GetActorLocation()
				- ShamanSpawnLocation).Rotation(), ShamanSpawnLocation);
			ReplicatedShaman = GetWorld()->SpawnActorDeferred<AAuraEnemy>(
				ShamanClass, Transform, this, FirstPlayerPawn,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
			if (ReplicatedShaman)
			{
				ReplicatedShaman->ConfigurePreplacedGuard();
				UGameplayStatics::FinishSpawningActor(ReplicatedShaman, Transform);
				ReplicatedShaman->SetEncounterDormant(true);
				ReplicatedShaman->BeginRangedCast(
					FirstPlayerPawn->GetActorLocation(), false, true);
				ReplicatedShaman->PlayShamanGroundBurstEffect(
					FirstPlayerPawn->GetActorLocation(),
					UAuraCombatBalanceData::Get().Shaman.GroundBurstRadius);
			}
		}
	}
	AAuraEnemy* Elite = nullptr;
	if (Patrol)
	{
		for (AAuraEnemy* Enemy : Patrol->GetActiveEnemies())
		{
			if (Enemy && Enemy->IsEliteVariant()) Elite = Enemy;
		}
	}
	if (Elite && FirstPlayerPawn)
	{
		// Keep the replication smoke focused on the warning itself. The
		// preplaced guard may be across a room divider from the test pawns,
		// making its lunge repeatedly hit geometry before sweep range.
		const FVector StagingLocation = FirstPlayerPawn->GetActorLocation()
			+ FVector(-300.f, 0.f, 0.f);
		Elite->TeleportTo(StagingLocation,
			(FirstPlayerPawn->GetActorLocation() - StagingLocation).Rotation(),
			false, true);
		Elite->ForceNetUpdate();
	}
	if (Elite)
	{
		const TWeakObjectPtr<AAuraEnemy> WeakElite(Elite);
		FTimerHandle PoiseReplicationTimer;
		GetWorldTimerManager().SetTimer(PoiseReplicationTimer,
			[WeakElite]()
			{
				if (AAuraEnemy* CurrentElite = WeakElite.Get())
				{
					CurrentElite->ApplyPoiseDamage(
						UAuraCombatBalanceData::Get().Poise.FireBoltDamage);
					CurrentElite->ForceNetUpdate();
				}
			}, 1.5f, false);
	}
	const bool bSetup = GetNetMode() == NM_DedicatedServer
		&& PlayerCount == 2 && Elite != nullptr && ReplicatedShroom != nullptr
		&& ReplicatedShaman != nullptr;
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [bSetup, PlayerCount]()
	{
		UE_LOG(LogAuraEliteSmoke, Display,
			TEXT("AURA_ELITE_REPLICATION_SERVER Passed=%s Players=%d"),
			bSetup ? TEXT("true") : TEXT("false"), PlayerCount);
	}, 6.f, false);
}

void AAuraPlayerController::AuraObserveEliteSkillReplication()
{
	if (GetNetMode() != NM_Client || !IsLocalController() || !GetWorld()) return;
	AAuraEncounterVolume* Patrol = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 106) { Patrol = *It; break; }
	}
	struct FReplicatedEliteSkills
	{
		bool bSweep = false;
		bool bSweepGeometry = false;
		bool bField = false;
		bool bFieldGeometry = false;
		bool bShamanGroundBurst = false;
		bool bShamanGroundBurstGeometry = false;
		bool bShamanGroundBurstAssets = false;
	};
	const TSharedRef<FReplicatedEliteSkills> Seen = MakeShared<FReplicatedEliteSkills>();
	const TSharedRef<FTimerHandle> ObserveHandle = MakeShared<FTimerHandle>();
	const TWeakObjectPtr<AAuraEncounterVolume> WeakPatrol(Patrol);
	FTimerDelegate Observe;
	Observe.BindLambda([this, Seen, WeakPatrol]()
	{
		const AAuraEncounterVolume* CurrentPatrol = WeakPatrol.Get();
		if (!CurrentPatrol || !GetWorld()) return;
		const FAuraEliteEnemyBalance& Balance = UAuraCombatBalanceData::Get().EliteEnemy;
		for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
		{
			const AAuraEnemy* Enemy = *It;
			if (!Enemy || Enemy->IsDead()
				|| FVector::DistSquared2D(Enemy->GetActorLocation(),
					CurrentPatrol->GetActorLocation()) > FMath::Square(1400.f)) continue;
			if (Enemy->IsRangedCombatant() && Enemy->IsRangedGroundBurstCasting())
			{
				Seen->bShamanGroundBurst = true;
				Seen->bShamanGroundBurstGeometry |= FMath::IsNearlyEqual(
					UAuraCombatBalanceData::Get().Shaman.GroundBurstRadius, 220.f)
					&& !Enemy->GetRangedCastTargetLocation().ContainsNaN();
				Seen->bShamanGroundBurstAssets |= Enemy->AreEnemySkillEffectsConfigured();
				continue;
			}
			if (!Enemy->IsEliteVariant()) continue;
			if (Enemy->GetEnemyArchetype() == EAuraEnemyArchetype::GoblinSpearman
				&& Enemy->GetMeleeAction() == EAuraMeleeAction::EliteGoblinSweep)
			{
				Seen->bSweep = true;
				Seen->bSweepGeometry |= FMath::IsNearlyEqual(
					Enemy->GetMeleeTelegraphRadius(), Balance.GoblinSweepRange)
					&& FVector::DotProduct(
						(Enemy->GetMeleeTelegraphLocation()
							- Enemy->GetActorLocation()).GetSafeNormal2D(),
						Enemy->GetActorForwardVector().GetSafeNormal2D()) > 0.99f;
			}
			if (Enemy->GetEnemyArchetype() == EAuraEnemyArchetype::CaveShroom)
			{
				const FAuraEliteSporeFieldState& Field = Enemy->GetEliteSporeField();
				Seen->bField |= Field.bActive;
				Seen->bFieldGeometry |= Field.bActive
					&& FMath::IsNearlyEqual(Balance.ShroomSeedRadius, 240.f)
					&& FMath::IsNearlyEqual(Field.Radius, Balance.ShroomSeedRadius)
					&& Field.ExpireServerTime > Field.ArmedServerTime;
			}
		}
	});
	GetWorldTimerManager().SetTimer(*ObserveHandle, Observe, 0.05f, true, 0.05f);
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer,
		[this, Seen, ObserveHandle]()
	{
		GetWorldTimerManager().ClearTimer(*ObserveHandle);
		const bool bPassed = Seen->bSweep && Seen->bSweepGeometry
			&& Seen->bField && Seen->bFieldGeometry
			&& Seen->bShamanGroundBurst && Seen->bShamanGroundBurstGeometry
			&& Seen->bShamanGroundBurstAssets;
		UE_LOG(LogAuraEliteSmoke, Display,
			TEXT("AURA_ELITE_SKILL_REPLICATION_CLIENT Passed=%s Sweep=%s SweepGeometry=%s Field=%s FieldGeometry=%s ShamanGround=%s ShamanGeometry=%s ShamanAssets=%s"),
			bPassed ? TEXT("true") : TEXT("false"),
			Seen->bSweep ? TEXT("true") : TEXT("false"),
			Seen->bSweepGeometry ? TEXT("true") : TEXT("false"),
			Seen->bField ? TEXT("true") : TEXT("false"),
			Seen->bFieldGeometry ? TEXT("true") : TEXT("false"),
			Seen->bShamanGroundBurst ? TEXT("true") : TEXT("false"),
			Seen->bShamanGroundBurstGeometry ? TEXT("true") : TEXT("false"),
			Seen->bShamanGroundBurstAssets ? TEXT("true") : TEXT("false"));
	}, 7.f, false);
}

void AAuraPlayerController::AuraObserveEliteVariants()
{
	AAuraEncounterVolume* Patrol = nullptr;
	for (TActorIterator<AAuraEncounterVolume> It(GetWorld()); It; ++It)
	{
		if (It->GetEncounterOrder() == 106) { Patrol = *It; break; }
	}
	const FAuraEliteEnemyBalance& Balance = UAuraCombatBalanceData::Get().EliteEnemy;
	int32 NearbyElites = 0;
	int32 NearbyNormals = 0;
	bool bScale = false;
	bool bHealth = false;
	bool bPoise = false;
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		AAuraEnemy* Enemy = *It;
		if (!Patrol || !Enemy || Enemy->IsDead()
			|| FVector::DistSquared2D(Enemy->GetActorLocation(), Patrol->GetActorLocation())
				> FMath::Square(1400.f)
			|| Enemy->GetEnemyArchetype() != EAuraEnemyArchetype::GoblinSpearman)
		{
			continue;
		}
		if (!Enemy->IsEliteVariant()) { ++NearbyNormals; continue; }
		++NearbyElites;
		bScale = FMath::IsNearlyEqual(Enemy->GetActorScale3D().X, Balance.VisualScale);
		const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
		bHealth = ASC && FMath::IsNearlyEqual(ASC->GetNumericAttribute(
			UAuraAttributeSet::GetMaxHealthAttribute()),
			UAuraCombatBalanceData::Get().MeleeEnemy.MaxHealth
				* Balance.MaxHealthMultiplier);
		bPoise = FMath::IsNearlyEqual(Enemy->GetPoiseMax(),
			UAuraCombatBalanceData::Get().Poise.EliteMax)
			&& Enemy->GetPoiseCurrent() > 0.f
			&& Enemy->GetPoiseCurrent() < Enemy->GetPoiseMax() - 1.f;
	}
	const bool bPassed = GetNetMode() == NM_Client && IsLocalController()
		&& Patrol && NearbyElites == 1 && NearbyNormals >= 1
		&& bScale && bHealth && bPoise;
	UE_LOG(LogAuraEliteSmoke, Display,
		TEXT("AURA_ELITE_REPLICATION_CLIENT Passed=%s Elites=%d Normals=%d Scale=%s Health=%s Poise=%s"),
		bPassed ? TEXT("true") : TEXT("false"), NearbyElites, NearbyNormals,
		bScale ? TEXT("true") : TEXT("false"),
		bHealth ? TEXT("true") : TEXT("false"),
		bPoise ? TEXT("true") : TEXT("false"));
}
