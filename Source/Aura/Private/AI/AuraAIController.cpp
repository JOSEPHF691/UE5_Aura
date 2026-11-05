// Copyright yosep

#include "AI/AuraAIController.h"

#include "AbilitySystem/Abilities/AuraBossAbility.h"
#include "AbilitySystem/Abilities/AuraMeleeAbility.h"
#include "AbilitySystem/Abilities/AuraShamanBoltAbility.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraAI, Log, All);

namespace
{
	const TCHAR* GetAIStateName(EAuraAIState State)
	{
		switch (State)
		{
		case EAuraAIState::Idle: return TEXT("Idle");
		case EAuraAIState::Chasing: return TEXT("Chasing");
		case EAuraAIState::Attacking: return TEXT("Attacking");
		case EAuraAIState::Retreating: return TEXT("Retreating");
		case EAuraAIState::Returning: return TEXT("Returning");
		case EAuraAIState::Stunned: return TEXT("Stunned");
		case EAuraAIState::Dead: return TEXT("Dead");
		default: return TEXT("Unknown");
		}
	}
}

AAuraAIController::AAuraAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	AuraPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AuraPerception"));
	SetPerceptionComponent(*AuraPerception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = DetectionRadius;
	SightConfig->LoseSightRadius = FMath::Max(LoseTargetRadius, LeashRadius);
	SightConfig->PeripheralVisionAngleDegrees = 180.f;
	SightConfig->SetMaxAge(2.f);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	AuraPerception->ConfigureSense(*SightConfig);
	AuraPerception->SetDominantSense(SightConfig->GetSenseImplementation());
}

void AAuraAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ApplyCombatBalance();
	if (SightConfig)
	{
		SightConfig->SightRadius = DetectionRadius;
		SightConfig->LoseSightRadius = FMath::Max(LoseTargetRadius, LeashRadius);
		AuraPerception->ConfigureSense(*SightConfig);
	}
	HomeLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	AuraPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AAuraAIController::OnTargetPerceptionUpdated);
	SetAIState(EAuraAIState::Idle);
	UE_LOG(LogAuraAI, Display, TEXT("AI possessed %s. Home=%s"), *GetNameSafe(InPawn), *HomeLocation.ToCompactString());
}

void AAuraAIController::ApplyCombatBalance()
{
	const FAuraAICombatBalance& AI = UAuraCombatBalanceData::Get().AI;
	DetectionRadius = AI.DetectionRadius;
	LoseTargetRadius = AI.LoseTargetRadius;
	LeashRadius = AI.LeashRadius;
	AttackRange = AI.MeleeAttackRange;
	HomeAcceptanceRadius = AI.HomeAcceptanceRadius;
	MoveRequestInterval = AI.MoveRequestInterval;
}

void AAuraAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetPawn());
	if (!Enemy) return;
	if (Enemy->IsDead())
	{
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		SetCombatTarget(nullptr);
		SetAIState(EAuraAIState::Dead);
		return;
	}
	if (Enemy->IsEncounterDormant())
	{
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		SetCombatTarget(nullptr);
		SetAIState(EAuraAIState::Idle);
		return;
	}
	if (Enemy->IsStunned())
	{
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		SetAIState(EAuraAIState::Stunned);
		return;
	}

	if (CombatTarget.IsValid() && CombatTarget->IsDead()) SetCombatTarget(nullptr);
	if (!CombatTarget.IsValid()) AcquirePlayerTarget();

	AAuraCharacter* Target = CombatTarget.Get();
	if (!Target)
	{
		ReturnHome();
		return;
	}
	// A warned barrage owns the boss for its full channel. Keep its facing and
	// lane fixed even when the target runs out of the ordinary chase radius.
	if (Enemy->GetBossAction() == EAuraBossAction::InfernalBarrage)
	{
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		SetAIState(EAuraAIState::Attacking);
		return;
	}

	const float DistanceToTarget = FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation());
	// Pursuit is bounded by the player's approximate God View footprint, not by
	// distance from the spawn point. A hard home leash made enemies pace at its edge.
	const float PlayerViewRadius = FMath::Max(LoseTargetRadius, LeashRadius);
	if (DistanceToTarget > PlayerViewRadius)
	{
		StopMovement();
		SetCombatTarget(nullptr);
		NextMoveRequestTime = 0.f;
		ReturnHome();
		return;
	}

	SetFocus(Target, EAIFocusPriority::Gameplay);
	if (Enemy->IsActionLocked())
	{
		StopMovement();
		return;
	}
	if (Enemy->IsBossEnemy())
	{
		HandleBossCombat(Enemy, Target, DistanceToTarget, DeltaSeconds);
		return;
	}
	if (Enemy->IsRangedCombatant())
	{
		HandleRangedCombat(Enemy, Target, DistanceToTarget, DeltaSeconds);
		return;
	}

	const float MeleeEngagementRange = Enemy->GetMeleeEngagementRange();
	if (DistanceToTarget <= MeleeEngagementRange)
	{
		StopMovement();
		SetAIState(EAuraAIState::Attacking);
		const FVector Direction = (Target->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			const FRotator DesiredRotation(0.f, Direction.Rotation().Yaw, 0.f);
			Enemy->SetActorRotation(FMath::RInterpTo(Enemy->GetActorRotation(), DesiredRotation, DeltaSeconds, 12.f));
		}
		if (UAbilitySystemComponent* AbilitySystem = Enemy->GetAbilitySystemComponent())
		{
			AbilitySystem->TryActivateAbilityByClass(UAuraMeleeAbility::StaticClass());
		}
		return;
	}

	SetAIState(EAuraAIState::Chasing);
	if (GetWorld() && GetWorld()->GetTimeSeconds() >= NextMoveRequestTime)
	{
		// The attack threshold is handled above. A tiny navigation acceptance radius
		// prevents path following from adding both character capsule radii and
		// declaring success while the Ghoul is still outside melee range.
		MoveToActor(Target, 5.f, false, true, true, nullptr, true);
		NextMoveRequestTime = GetWorld()->GetTimeSeconds() + MoveRequestInterval;
	}
}

void AAuraAIController::HandleBossCombat(
	AAuraEnemy* Enemy, AAuraCharacter* Target, const float DistanceToTarget,
	const float DeltaSeconds)
{
	if (!Enemy || !Target || !GetWorld()) return;
	const FVector Direction =
		(Target->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		const FRotator DesiredRotation(0.f, Direction.Rotation().Yaw, 0.f);
		Enemy->SetActorRotation(FMath::RInterpTo(
			Enemy->GetActorRotation(), DesiredRotation, DeltaSeconds, 11.f));
	}

	const bool bPhaseTwo = Enemy->GetBossPhase() == EAuraBossPhase::PhaseTwo;
	const float AbilityRange = bPhaseTwo
		? Enemy->GetBossThrowRange() : Enemy->GetBossMeleeRange();
	const bool bNeedsClearShot = bPhaseTwo
		&& DistanceToTarget > Enemy->GetBossTailSpinRadius();
	if (DistanceToTarget <= AbilityRange
		&& (!bNeedsClearShot || HasClearRangedShot(Enemy, Target)))
	{
		StopMovement();
		SetAIState(EAuraAIState::Attacking);
		if (UAbilitySystemComponent* AbilitySystem = Enemy->GetAbilitySystemComponent())
		{
			AbilitySystem->TryActivateAbilityByClass(UAuraBossAbility::StaticClass());
		}
		return;
	}

	SetAIState(EAuraAIState::Chasing);
	if (GetWorld()->GetTimeSeconds() >= NextMoveRequestTime)
	{
		MoveToActor(Target, 5.f, false, true, true, nullptr, true);
		NextMoveRequestTime = GetWorld()->GetTimeSeconds() + MoveRequestInterval;
	}
}

void AAuraAIController::HandleRangedCombat(
	AAuraEnemy* Enemy, AAuraCharacter* Target, const float DistanceToTarget,
	const float DeltaSeconds)
{
	if (!Enemy || !Target || !GetWorld()) return;

	const FVector Direction =
		(Target->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		const FRotator DesiredRotation(0.f, Direction.Rotation().Yaw, 0.f);
		Enemy->SetActorRotation(FMath::RInterpTo(
			Enemy->GetActorRotation(), DesiredRotation, DeltaSeconds, 12.f));
	}

	if (DistanceToTarget < Enemy->GetRangedRetreatDistance())
	{
		SetAIState(EAuraAIState::Retreating);
		if (GetWorld()->GetTimeSeconds() >= NextMoveRequestTime)
		{
			RequestRangedRetreat(Enemy, Target);
			NextMoveRequestTime = GetWorld()->GetTimeSeconds() + MoveRequestInterval;
		}
		return;
	}

	if (DistanceToTarget <= Enemy->GetRangedAttackRange()
		&& HasClearRangedShot(Enemy, Target))
	{
		StopMovement();
		SetAIState(EAuraAIState::Attacking);
		if (UAbilitySystemComponent* AbilitySystem = Enemy->GetAbilitySystemComponent())
		{
			AbilitySystem->TryActivateAbilityByClass(UAuraShamanBoltAbility::StaticClass());
		}
		return;
	}

	SetAIState(EAuraAIState::Chasing);
	if (GetWorld()->GetTimeSeconds() >= NextMoveRequestTime)
	{
		// Close only to the preferred casting distance. If line of sight is blocked
		// inside attack range, use a smaller acceptance radius to round the obstacle.
		const float AcceptanceRadius = DistanceToTarget > Enemy->GetRangedAttackRange()
			? Enemy->GetRangedPreferredDistance() : 80.f;
		MoveToActor(Target, AcceptanceRadius, false, true, true, nullptr, true);
		NextMoveRequestTime = GetWorld()->GetTimeSeconds() + MoveRequestInterval;
	}
}

bool AAuraAIController::HasClearRangedShot(
	const AAuraEnemy* Enemy, const AAuraCharacter* Target) const
{
	if (!Enemy || !Target || !GetWorld()) return false;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraShamanLineOfFire), false);
	// Fire bolts pass through friendly enemies, so they must not make the caster
	// think an otherwise clear shot is blocked.
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		QueryParams.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	const FVector TraceStart = Enemy->GetActorLocation() + FVector(0.f, 0.f, 90.f);
	const FVector TraceEnd = Target->GetActorLocation() + FVector(0.f, 0.f, 45.f);
	if (!GetWorld()->LineTraceSingleByChannel(
		Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return true;
	}
	return Hit.GetActor() == Target;
}

void AAuraAIController::RequestRangedRetreat(
	AAuraEnemy* Enemy, const AAuraCharacter* Target)
{
	if (!Enemy || !Target || !GetWorld()) return;
	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavigationSystem) return;

	FVector AwayDirection =
		(Enemy->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	if (AwayDirection.IsNearlyZero()) AwayDirection = -Enemy->GetActorForwardVector();
	AwayDirection.Z = 0.f;
	AwayDirection.Normalize();

	const float CurrentDistance = FVector::Dist2D(
		Enemy->GetActorLocation(), Target->GetActorLocation());
	const float StepDistance = FMath::Clamp(
		Enemy->GetRangedPreferredDistance() - CurrentDistance, 260.f, 460.f);
	const float CandidateAngles[] = { 0.f, 42.f, -42.f, 78.f, -78.f };

	FNavLocation BestLocation;
	float BestTargetDistanceSquared = -1.f;
	for (const float Angle : CandidateAngles)
	{
		const FVector Candidate = Enemy->GetActorLocation()
			+ AwayDirection.RotateAngleAxis(Angle, FVector::UpVector) * StepDistance;

		FNavLocation ProjectedLocation;
		if (!NavigationSystem->ProjectPointToNavigation(
			Candidate, ProjectedLocation, FVector(120.f, 120.f, 260.f)))
		{
			continue;
		}
		const float TargetDistanceSquared = FVector::DistSquared2D(
			ProjectedLocation.Location, Target->GetActorLocation());
		// Projection near a ledge can snap behind the caster. Never accept a
		// retreat point unless it materially increases separation from the player.
		if (TargetDistanceSquared > FMath::Square(CurrentDistance + 35.f)
			&& TargetDistanceSquared > BestTargetDistanceSquared)
		{
			BestTargetDistanceSquared = TargetDistanceSquared;
			BestLocation = ProjectedLocation;
		}
	}

	if (BestTargetDistanceSquared >= 0.f)
	{
		MoveToLocation(BestLocation.Location, 35.f, true, true, true, false, nullptr, true);
	}
}

void AAuraAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (const AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetPawn());
		Enemy && Enemy->IsEncounterDormant()) return;
	AAuraCharacter* SensedCharacter = Cast<AAuraCharacter>(Actor);
	if (!SensedCharacter || SensedCharacter->IsDead()) return;
	const AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetPawn());
	if (Enemy && Stimulus.WasSuccessfullySensed() && !CombatTarget.IsValid()
		&& FVector::DistSquared2D(Enemy->GetActorLocation(),
			SensedCharacter->GetActorLocation()) <= FMath::Square(DetectionRadius)
		&& (Enemy->IsRangedCombatant()
			? HasClearRangedShot(Enemy, SensedCharacter) : LineOfSightTo(SensedCharacter)))
	{
		SetCombatTarget(SensedCharacter);
	}
}

void AAuraAIController::AcquirePlayerTarget()
{
	const AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetPawn());
	if (!Enemy || !GetWorld()) return;

	AAuraCharacter* BestTarget = nullptr;
	float BestDistanceSquared = FMath::Square(DetectionRadius);
	for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
	{
		AAuraCharacter* Candidate = *It;
		if (!Candidate || Candidate->IsDead()) continue;

		const float DistanceSquared = FVector::DistSquared2D(
			Enemy->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared > BestDistanceSquared) continue;

		const bool bHasLineOfSight = Enemy->IsRangedCombatant()
			? HasClearRangedShot(Enemy, Candidate) : LineOfSightTo(Candidate);
		if (!bHasLineOfSight) continue;

		BestTarget = Candidate;
		BestDistanceSquared = DistanceSquared;
	}

	if (BestTarget) SetCombatTarget(BestTarget);
}

void AAuraAIController::SetCombatTarget(AAuraCharacter* NewTarget)
{
	if (CombatTarget.Get() == NewTarget) return;
	CombatTarget = NewTarget;
	UE_LOG(LogAuraAI, Display, TEXT("AI target changed. Controller=%s Target=%s"),
		*GetName(), *GetNameSafe(NewTarget));
}

void AAuraAIController::SetAIState(EAuraAIState NewState)
{
	if (AIState == NewState) return;
	UE_LOG(LogAuraAI, Display, TEXT("AI state changed. Pawn=%s %s -> %s"),
		*GetNameSafe(GetPawn()), GetAIStateName(AIState), GetAIStateName(NewState));
	AIState = NewState;
}

void AAuraAIController::ReturnHome()
{
	APawn* Enemy = GetPawn();
	if (!Enemy) return;
	ClearFocus(EAIFocusPriority::Gameplay);
	if (FVector::DistSquared2D(Enemy->GetActorLocation(), HomeLocation) <= FMath::Square(HomeAcceptanceRadius))
	{
		StopMovement();
		SetAIState(EAuraAIState::Idle);
		return;
	}

	SetAIState(EAuraAIState::Returning);
	if (GetWorld() && GetWorld()->GetTimeSeconds() >= NextMoveRequestTime)
	{
		MoveToLocation(HomeLocation, HomeAcceptanceRadius, true, true, true, false, nullptr, true);
		NextMoveRequestTime = GetWorld()->GetTimeSeconds() + MoveRequestInterval;
	}
}
