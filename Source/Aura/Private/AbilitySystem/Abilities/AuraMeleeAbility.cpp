// Copyright yosep

#include "AbilitySystem/Abilities/AuraMeleeAbility.h"

#include "AI/AuraAIController.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraMelee, Log, All);

UAuraMeleeAbility::UAuraMeleeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActiveAction = EAuraMeleeAction::None;

	static ConstructorHelpers::FObjectFinder<USoundBase> AttackSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Ghoul/Attack/SFX_Enemies_Ghoul_Attack_ClawSwipe_01.SFX_Enemies_Ghoul_Attack_ClawSwipe_01"));
	if (AttackSoundAsset.Succeeded()) AttackSound = AttackSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> SpearSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Goblin/Swoosh/SFX_Sword_Swoosh_04.SFX_Sword_Swoosh_04"));
	if (SpearSoundAsset.Succeeded()) SpearSound = SpearSoundAsset.Object;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Melee")));
	SetAssetTags(Tags);
}

float UAuraMeleeAbility::GetDamage() const
{
	return UAuraCombatBalanceData::Get().MeleeEnemy.Damage;
}

float UAuraMeleeAbility::GetImpactTime() const
{
	return UAuraCombatBalanceData::Get().MeleeEnemy.ImpactTime;
}

float UAuraMeleeAbility::GetHitRange() const
{
	return UAuraCombatBalanceData::Get().MeleeEnemy.HitRange;
}

float UAuraMeleeAbility::GetAttackInterval() const
{
	return UAuraCombatBalanceData::Get().MeleeEnemy.AttackInterval;
}

bool UAuraMeleeAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const AAuraEnemy* Enemy = ActorInfo ? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	const AAuraAIController* AIController = Enemy
		? Cast<AAuraAIController>(Enemy->GetController()) : nullptr;
	const AAuraCharacter* Target = AIController ? AIController->GetCombatTarget() : nullptr;
	const UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;
	return Enemy && !Enemy->IsRangedCombatant() && !Enemy->IsBossEnemy()
		&& !Enemy->IsDead() && !Enemy->IsStunned() && !Enemy->IsActionLocked()
		&& Target && !Target->IsDead() && World
		&& World->GetTimeSeconds() >= NextAllowedActivationTime
		&& FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
			<= FMath::Square(Enemy->GetMeleeEngagementRange());
}

void UAuraMeleeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	AAuraAIController* AIController = Enemy
		? Cast<AAuraAIController>(Enemy->GetController()) : nullptr;
	AAuraCharacter* Target = AIController ? AIController->GetCombatTarget() : nullptr;
	if (!Enemy || !Target || !Enemy->HasAuthority() || !GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	LockedTarget = Target;
	LockedAimLocation = Target->GetActorLocation();
	ActiveImpactOrigin = FindGroundLocation(Enemy, Enemy->GetActorLocation());
	ActiveSeedLocation = FVector::ZeroVector;
	ActiveSweepDirection = FVector::ZeroVector;
	ActiveHitIndex = 0;
	++AttackSequence;
	SelectAttack(*Enemy, *Target);

	const FVector Direction = (LockedAimLocation - Enemy->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero()) Enemy->SetActorRotation(Direction.Rotation());

	FVector TelegraphLocation = FVector::ZeroVector;
	float TelegraphRadius = 0.f;
	const FAuraMeleeEnemyBalance& Balance = UAuraCombatBalanceData::Get().MeleeEnemy;
	switch (ActiveAction)
	{
	case EAuraMeleeAction::GhoulFrenzy:
		TelegraphLocation = ActiveImpactOrigin;
		TelegraphRadius = GetHitRange();
		break;
	case EAuraMeleeAction::GoblinLunge:
	{
		FVector WarningDirection = (LockedAimLocation - Enemy->GetActorLocation()).GetSafeNormal2D();
		if (WarningDirection.IsNearlyZero())
		{
			WarningDirection = Enemy->GetActorForwardVector().GetSafeNormal2D();
		}
		TelegraphLocation = FindGroundLocation(Enemy,
			Enemy->GetActorLocation() + WarningDirection * Balance.GoblinLungeWarningLength);
		TelegraphRadius = Balance.GoblinLungeWarningHalfWidth;
		break;
	}
	case EAuraMeleeAction::GoblinJab:
		TelegraphLocation = ActiveImpactOrigin;
		TelegraphRadius = Balance.GoblinJabRange + Balance.GoblinJabStepDistance;
		break;
	case EAuraMeleeAction::ShroomSporeBurst:
		TelegraphLocation = ActiveImpactOrigin;
		TelegraphRadius = Balance.ShroomSporeRadius;
		break;
	case EAuraMeleeAction::EliteShroomSeed:
		ActiveSeedLocation = FindGroundLocation(Enemy, LockedAimLocation);
		TelegraphLocation = ActiveSeedLocation;
		TelegraphRadius = UAuraCombatBalanceData::Get().EliteEnemy.ShroomSeedRadius;
		break;
	default:
		break;
	}

	Enemy->BeginMeleeAction(ActiveAction, TelegraphLocation, TelegraphRadius,
		ActiveImpactTime);
	NextAllowedActivationTime = GetWorld()->GetTimeSeconds() + ActiveAttackInterval;
	USoundBase* SelectedSound = Enemy->GetEnemyArchetype() == EAuraEnemyArchetype::GoblinSpearman
		? SpearSound.Get() : AttackSound.Get();
	if (SelectedSound)
	{
		UGameplayStatics::PlaySoundAtLocation(Enemy, SelectedSound, Enemy->GetActorLocation(), 0.78f);
	}

	UE_LOG(LogAuraMelee, Display,
		TEXT("Enemy attack started. Enemy=%s Archetype=%d Action=%s Impact=%.2f Damage=%.1f Range=%.1f Sequence=%d SpecialRoll=%.3f NormalSinceSpecial=%d"),
		*Enemy->GetName(), static_cast<int32>(Enemy->GetEnemyArchetype()), GetActiveActionName(),
		ActiveImpactTime, ActiveDamage, ActiveHitRange, AttackSequence,
		LastSpecialRoll, NormalAttacksSinceSpecial);
	UAbilityTask_WaitDelay* WaitForHit = UAbilityTask_WaitDelay::WaitDelay(this, ActiveImpactTime);
	WaitForHit->OnFinish.AddDynamic(this, &UAuraMeleeAbility::ResolveMeleeImpact);
	WaitForHit->ReadyForActivation();
}

void UAuraMeleeAbility::SelectAttack(AAuraEnemy& Enemy, const AAuraCharacter& Target)
{
	const FAuraMeleeEnemyBalance& Balance = UAuraCombatBalanceData::Get().MeleeEnemy;
	ActiveSecondHitDelay = 0.f;
	ActiveMinimumForwardDot = -0.15f;

	switch (Enemy.GetEnemyArchetype())
	{
	case EAuraEnemyArchetype::GoblinSpearman:
	{
		// The AI engages at lunge range. A jab from that distance would either
		// miss or require an invisible long step, so choose the warned lunge.
		const bool bOutsideJabReach = FVector::Dist2D(
			Enemy.GetActorLocation(), Target.GetActorLocation())
			> Balance.GoblinJabRange + Balance.GoblinJabStepDistance;
		const bool bUseLunge = bOutsideJabReach
			|| RollSpecialAttack(Balance.GoblinLungeChance
				+ Enemy.GetEliteSpecialChanceBonus(),
				Balance.MaxNormalAttacksBeforeSpecial);
		if (bOutsideJabReach)
		{
			NormalAttacksSinceSpecial = 0;
			bLastAttackWasSpecial = true;
			LastSpecialRoll = -1.f; // Range-forced rather than a probability roll.
		}
		if (bUseLunge)
		{
			ActiveAction = EAuraMeleeAction::GoblinLunge;
			ActiveDamage = Balance.GoblinLungeDamage;
			ActiveImpactTime = Balance.GoblinLungeWindup;
			ActiveAttackInterval = Balance.GoblinLungeAttackInterval;
			ActiveHitRange = Balance.GoblinLungeHitRange;
			ActiveMinimumForwardDot = 0.45f;
		}
		else
		{
			ActiveAction = EAuraMeleeAction::GoblinJab;
			ActiveDamage = Balance.GoblinJabDamage;
			ActiveImpactTime = Balance.GoblinJabImpactTime;
			ActiveAttackInterval = Balance.GoblinJabAttackInterval;
			ActiveHitRange = Balance.GoblinJabRange;
			ActiveMinimumForwardDot = FMath::Cos(
				FMath::DegreesToRadians(Balance.GoblinJabHalfAngleDegrees));
		}
		break;
	}

	case EAuraEnemyArchetype::CaveShroom:
		if (Enemy.IsEliteVariant() && !Enemy.GetEliteSporeField().bActive
			&& !bLastAttackWasSpecial && GetWorld()
			&& GetWorld()->GetTimeSeconds() >= NextEliteShroomSeedReadyTime)
		{
			const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
			ActiveAction = EAuraMeleeAction::EliteShroomSeed;
			ActiveDamage = 0.f;
			ActiveImpactTime = Elite.ShroomSeedWindup;
			ActiveAttackInterval = FMath::Max(
				Balance.ShroomBashAttackInterval, Elite.ShroomSeedWindup + 0.2f);
			ActiveHitRange = Elite.ShroomSeedRadius;
			NextEliteShroomSeedReadyTime = GetWorld()->GetTimeSeconds()
				+ Elite.ShroomSeedCooldown;
			NormalAttacksSinceSpecial = 0;
			bLastAttackWasSpecial = true;
			LastSpecialRoll = -2.f;
			break;
		}
		if (RollSpecialAttack(Balance.ShroomSporeChance
			+ Enemy.GetEliteSpecialChanceBonus(),
			Balance.MaxNormalAttacksBeforeSpecial))
		{
			ActiveAction = EAuraMeleeAction::ShroomSporeBurst;
			ActiveDamage = Balance.ShroomSporeDamage;
			ActiveImpactTime = Balance.ShroomSporeWindup;
			ActiveAttackInterval = Balance.ShroomSporeAttackInterval;
			ActiveHitRange = Balance.ShroomSporeRadius;
			ActiveMinimumForwardDot = -1.f;
		}
		else
		{
			ActiveAction = EAuraMeleeAction::ShroomBash;
			ActiveDamage = Balance.ShroomBashDamage;
			ActiveImpactTime = Balance.ShroomBashImpactTime;
			ActiveAttackInterval = Balance.ShroomBashAttackInterval;
			ActiveHitRange = Balance.ShroomBashRange;
			ActiveMinimumForwardDot = -0.1f;
		}
		break;

	case EAuraEnemyArchetype::Ghoul:
	default:
		if (RollSpecialAttack(Balance.GhoulFrenzyChance
			+ Enemy.GetEliteSpecialChanceBonus(),
			Balance.MaxNormalAttacksBeforeSpecial))
		{
			ActiveAction = EAuraMeleeAction::GhoulFrenzy;
			ActiveDamage = Balance.GhoulFrenzyHitDamage;
			ActiveImpactTime = Balance.GhoulFrenzyFirstImpactTime;
			ActiveSecondHitDelay = Balance.GhoulFrenzySecondImpactDelay;
			ActiveAttackInterval = Balance.GhoulFrenzyAttackInterval;
			ActiveHitRange = Balance.HitRange;
		}
		else
		{
			ActiveAction = EAuraMeleeAction::GhoulSwipe;
			ActiveDamage = Balance.Damage;
			ActiveImpactTime = Balance.ImpactTime;
			ActiveAttackInterval = Balance.AttackInterval;
			ActiveHitRange = Balance.HitRange;
		}
		break;
	}
	ActiveAttackInterval = FMath::Max(
		ActiveImpactTime + ActiveSecondHitDelay + 0.05f,
		ActiveAttackInterval * Enemy.GetEliteAttackIntervalMultiplier());
}

bool UAuraMeleeAbility::RollSpecialAttack(const float Probability,
	const int32 MaxNormalAttacks)
{
	LastSpecialRoll = FMath::FRand();
	const bool bPityTriggered = NormalAttacksSinceSpecial >= FMath::Max(1, MaxNormalAttacks);
	const bool bRolledSpecial = LastSpecialRoll < FMath::Clamp(Probability, 0.f, 1.f);
	const bool bUseSpecial = !bLastAttackWasSpecial
		&& (bPityTriggered || bRolledSpecial);
	if (bUseSpecial)
	{
		NormalAttacksSinceSpecial = 0;
	}
	else
	{
		++NormalAttacksSinceSpecial;
	}
	bLastAttackWasSpecial = bUseSpecial;
	return bUseSpecial;
}

void UAuraMeleeAbility::ResolveMeleeImpact()
{
	AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	AAuraCharacter* Target = LockedTarget.Get();
	if (!Enemy || !Enemy->HasAuthority() || Enemy->IsDead() || Enemy->IsStunned())
	{
		FinishMeleeAttack();
		return;
	}

	FVector CueDirection = ActiveAction == EAuraMeleeAction::EliteGoblinSweep
		? ActiveSweepDirection
		: (LockedAimLocation - Enemy->GetActorLocation()).GetSafeNormal2D();
	if (CueDirection.IsNearlyZero())
		CueDirection = Enemy->GetActorForwardVector().GetSafeNormal2D();
	if (ActiveAction == EAuraMeleeAction::GoblinJab
		|| ActiveAction == EAuraMeleeAction::GoblinLunge
		|| ActiveAction == EAuraMeleeAction::EliteGoblinSweep)
	{
		Enemy->PlayMeleeTextureCue(ActiveAction,
			Enemy->GetActorLocation() + FVector(0.f, 0.f, 42.f),
			CueDirection, 0.28f);
	}
	else if (ActiveAction == EAuraMeleeAction::ShroomBash)
	{
		Enemy->PlayMeleeTextureCue(ActiveAction,
			FindGroundLocation(Enemy,
				Enemy->GetActorLocation() + CueDirection * ActiveHitRange * 0.45f),
			CueDirection, 0.38f);
	}
	else if (ActiveAction == EAuraMeleeAction::ShroomSporeBurst)
	{
		Enemy->PlayMeleeTextureCue(ActiveAction,
			ActiveImpactOrigin, CueDirection, 0.26f);
	}

	Enemy->ClearMeleeTelegraph();
	if (ActiveAction == EAuraMeleeAction::GoblinLunge)
	{
		PerformGoblinLunge(*Enemy);
	}
	else if (ActiveAction == EAuraMeleeAction::GoblinJab)
	{
		PerformGoblinJabStep(*Enemy);
	}

	if (ActiveAction == EAuraMeleeAction::EliteShroomSeed)
	{
		const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
		Enemy->ActivateEliteSporeField(ActiveSeedLocation, Elite.ShroomSeedRadius,
			Elite.ShroomSeedArmDelay, Elite.ShroomSeedDuration);
	}
	else if (ActiveAction == EAuraMeleeAction::EliteGoblinSweep)
	{
		for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
		{
			AAuraCharacter* Candidate = *It;
			if (Candidate && !Candidate->IsDead()
				&& FMath::Abs(Candidate->GetActorLocation().Z
					- Enemy->GetActorLocation().Z) <= 180.f)
			{
				TryHitTarget(*Enemy, *Candidate, ActiveDamage, ActiveHitRange,
					ActiveMinimumForwardDot, ActiveImpactOrigin, ActiveSweepDirection);
			}
		}
	}
	else if (ActiveAction == EAuraMeleeAction::ShroomSporeBurst)
	{
		ResolveShroomSporeBurst(*Enemy);
	}
	else if (Target && !Target->IsDead())
	{
		TryHitTarget(*Enemy, *Target, ActiveDamage, ActiveHitRange,
			ActiveMinimumForwardDot, Enemy->GetActorLocation());
	}

	if (ActiveAction == EAuraMeleeAction::GhoulFrenzy && ActiveHitIndex == 0
		&& ActiveSecondHitDelay > KINDA_SMALL_NUMBER)
	{
		ActiveHitIndex = 1;
		Enemy->PlayMeleeAttackAnimation();
		UAbilityTask_WaitDelay* WaitForFollowUp = UAbilityTask_WaitDelay::WaitDelay(
			this, ActiveSecondHitDelay);
		WaitForFollowUp->OnFinish.AddDynamic(this, &UAuraMeleeAbility::ResolveMeleeImpact);
		WaitForFollowUp->ReadyForActivation();
		return;
	}
	if (ActiveAction == EAuraMeleeAction::GoblinLunge
		&& Enemy->IsEliteVariant() && Target && !Target->IsDead() && GetWorld()
		&& GetWorld()->GetTimeSeconds() >= NextEliteGoblinSweepReadyTime
		&& FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
			<= FMath::Square(UAuraCombatBalanceData::Get().EliteEnemy.GoblinSweepRange + 50.f))
	{
		BeginEliteGoblinSweep(*Enemy, *Target);
		return;
	}

	FinishMeleeAttack();
}

bool UAuraMeleeAbility::TryHitTarget(AAuraEnemy& Enemy, AAuraCharacter& Target,
	const float Damage, const float HitRange, const float MinimumForwardDot,
	const FVector& ImpactOrigin, const FVector& LockedDirection)
{
	FVector ToTarget = Target.GetActorLocation() - ImpactOrigin;
	ToTarget.Z = 0.f;
	if (ToTarget.SizeSquared() > FMath::Square(HitRange))
	{
		UE_LOG(LogAuraMelee, Display, TEXT("Enemy attack missed by range. Enemy=%s Action=%s"),
			*Enemy.GetName(), GetActiveActionName());
		return false;
	}
	if (MinimumForwardDot > -1.f && !ToTarget.IsNearlyZero())
	{
		const FVector Facing = LockedDirection.IsNearlyZero()
			? Enemy.GetActorForwardVector().GetSafeNormal2D()
			: LockedDirection.GetSafeNormal2D();
		const float ForwardDot = FVector::DotProduct(Facing, ToTarget.GetSafeNormal());
		if (ForwardDot < MinimumForwardDot)
		{
			UE_LOG(LogAuraMelee, Display,
				TEXT("Enemy attack dodged outside arc. Enemy=%s Action=%s Dot=%.2f Required=%.2f"),
				*Enemy.GetName(), GetActiveActionName(), ForwardDot, MinimumForwardDot);
			return false;
		}
	}

	bool bSporeDetonated = false;
	const float FinalDamage = ResolveSporeSynergyDamage(
		Target, ActiveAction, ActiveHitIndex, Damage, bSporeDetonated);
	ApplyDamageToTarget(Enemy, Target, FinalDamage);
	Enemy.PlayMeleeImpactEffect(FindGroundLocation(&Enemy, Target.GetActorLocation()),
		bSporeDetonated ? 120.f : 75.f);
	if (bSporeDetonated)
	{
		UE_LOG(LogAuraMelee, Display,
			TEXT("ENEMY_SPORE_DETONATION Enemy=%s Target=%s Action=%s Damage=%.1f"),
			*Enemy.GetName(), *Target.GetName(), GetActiveActionName(), FinalDamage);
	}
	return true;
}

float UAuraMeleeAbility::ResolveSporeSynergyDamage(AAuraCharacter& Target,
	const EAuraMeleeAction Action, const int32 HitIndex,
	const float BaseDamage, bool& bOutDetonated)
{
	const bool bEligible = Action == EAuraMeleeAction::GoblinLunge
		|| (Action == EAuraMeleeAction::GhoulFrenzy && HitIndex == 1);
	bOutDetonated = bEligible && Target.ConsumeSporeExposure();
	return BaseDamage * (bOutDetonated
		? UAuraCombatBalanceData::Get().MeleeEnemy.SporeDetonationDamageMultiplier : 1.f);
}

void UAuraMeleeAbility::ApplyDamageToTarget(
	AAuraEnemy& Enemy, AAuraCharacter& Target, const float Damage) const
{
	UAbilitySystemComponent* SourceAbilitySystem = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetAbilitySystem = Target.GetAbilitySystemComponent();
	if (Target.IsDead() || !SourceAbilitySystem || !TargetAbilitySystem) return;

	FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(&Enemy);
	FGameplayEffectSpecHandle DamageSpec = SourceAbilitySystem->MakeOutgoingSpec(
		UAuraDamageGameplayEffect::StaticClass(), 1.f, EffectContext);
	if (!DamageSpec.IsValid()) return;

	DamageSpec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")),
		-Damage * Enemy.GetDifficultyDamageMultiplier());
	TargetAbilitySystem->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
	UE_LOG(LogAuraMelee, Display,
		TEXT("Enemy attack hit. Enemy=%s Target=%s Action=%s Damage=%.1f Health=%.1f Hit=%d"),
		*Enemy.GetName(), *Target.GetName(), GetActiveActionName(), Damage,
		TargetAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()),
		ActiveHitIndex + 1);
}

void UAuraMeleeAbility::ResolveShroomSporeBurst(AAuraEnemy& Enemy)
{
	const FVector BurstLocation = ActiveImpactOrigin;
	Enemy.PlayMeleeImpactEffect(BurstLocation, ActiveHitRange);
	int32 HitCount = 0;
	for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
	{
		AAuraCharacter* Candidate = *It;
		if (!Candidate || Candidate->IsDead()
			|| FVector::DistSquared2D(BurstLocation, Candidate->GetActorLocation())
				> FMath::Square(ActiveHitRange))
		{
			continue;
		}
		ApplyDamageToTarget(Enemy, *Candidate, ActiveDamage);
		if (!Candidate->IsDead()) Candidate->ApplySporeExposure(&Enemy);
		++HitCount;
	}
	UE_LOG(LogAuraMelee, Display,
		TEXT("Shroom spore burst resolved. Enemy=%s Radius=%.1f Targets=%d"),
		*Enemy.GetName(), ActiveHitRange, HitCount);
}

void UAuraMeleeAbility::PerformGoblinLunge(AAuraEnemy& Enemy)
{
	const FAuraMeleeEnemyBalance& Balance = UAuraCombatBalanceData::Get().MeleeEnemy;
	FVector Direction = (LockedAimLocation - Enemy.GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero()) Direction = Enemy.GetActorForwardVector().GetSafeNormal2D();
	const float DistanceToLockedPoint = FVector::Dist2D(
		Enemy.GetActorLocation(), LockedAimLocation);
	const float TravelDistance = FMath::Clamp(
		DistanceToLockedPoint - ActiveHitRange * 0.58f,
		0.f, Balance.GoblinLungeTravelDistance);
	FHitResult MoveHit;
	Enemy.AddActorWorldOffset(Direction * TravelDistance, true, &MoveHit, ETeleportType::None);
	UE_LOG(LogAuraMelee, Display,
		TEXT("Goblin lunge moved. Enemy=%s Distance=%.1f Blocked=%s"),
		*Enemy.GetName(), TravelDistance, MoveHit.bBlockingHit ? TEXT("true") : TEXT("false"));
}

void UAuraMeleeAbility::BeginEliteGoblinSweep(
	AAuraEnemy& Enemy, AAuraCharacter& Target)
{
	const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
	ActiveSweepDirection = (Target.GetActorLocation()
		- Enemy.GetActorLocation()).GetSafeNormal2D();
	if (ActiveSweepDirection.IsNearlyZero())
	{
		ActiveSweepDirection = Enemy.GetActorForwardVector().GetSafeNormal2D();
	}
	if (ActiveSweepDirection.IsNearlyZero()) ActiveSweepDirection = FVector::ForwardVector;
	Enemy.SetActorRotation(ActiveSweepDirection.Rotation());
	ActiveImpactOrigin = Enemy.GetActorLocation();
	ActiveAction = EAuraMeleeAction::EliteGoblinSweep;
	ActiveHitIndex = 1;
	ActiveDamage = Elite.GoblinSweepDamage;
	ActiveHitRange = Elite.GoblinSweepRange;
	ActiveMinimumForwardDot = FMath::Cos(
		FMath::DegreesToRadians(Elite.GoblinSweepHalfAngleDegrees));
	const FVector TelegraphEnd = FindGroundLocation(&Enemy,
		ActiveImpactOrigin + ActiveSweepDirection * Elite.GoblinSweepRange);
	Enemy.BeginMeleeAction(ActiveAction, TelegraphEnd, Elite.GoblinSweepRange,
		Elite.GoblinSweepWindup);
	const float Now = GetWorld()->GetTimeSeconds();
	NextEliteGoblinSweepReadyTime = Now + Elite.GoblinSweepCooldown;
	NextAllowedActivationTime = FMath::Max(
		NextAllowedActivationTime, Now + Elite.GoblinSweepWindup + 0.25f);
	UE_LOG(LogAuraMelee, Display,
		TEXT("ELITE_GOBLIN_SWEEP_STARTED Enemy=%s Direction=%s Range=%.1f HalfAngle=%.1f Windup=%.2f"),
		*Enemy.GetName(), *ActiveSweepDirection.ToCompactString(),
		Elite.GoblinSweepRange, Elite.GoblinSweepHalfAngleDegrees, Elite.GoblinSweepWindup);
	UAbilityTask_WaitDelay* WaitForSweep = UAbilityTask_WaitDelay::WaitDelay(
		this, Elite.GoblinSweepWindup);
	WaitForSweep->OnFinish.AddDynamic(this, &UAuraMeleeAbility::ResolveMeleeImpact);
	WaitForSweep->ReadyForActivation();
}

void UAuraMeleeAbility::PerformGoblinJabStep(AAuraEnemy& Enemy)
{
	const FAuraMeleeEnemyBalance& Balance = UAuraCombatBalanceData::Get().MeleeEnemy;
	const float DistanceToLockedPoint = FVector::Dist2D(
		Enemy.GetActorLocation(), LockedAimLocation);
	if (DistanceToLockedPoint <= Balance.GoblinJabRange) return;
	FVector Direction = (LockedAimLocation - Enemy.GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero()) return;
	const float StepDistance = FMath::Clamp(
		DistanceToLockedPoint - Balance.GoblinJabRange * 0.85f,
		0.f, Balance.GoblinJabStepDistance);
	FHitResult MoveHit;
	Enemy.AddActorWorldOffset(Direction * StepDistance, true, &MoveHit, ETeleportType::None);
	UE_LOG(LogAuraMelee, Display,
		TEXT("Goblin jab step. Enemy=%s Distance=%.1f Blocked=%s"),
		*Enemy.GetName(), StepDistance, MoveHit.bBlockingHit ? TEXT("true") : TEXT("false"));
}

FVector UAuraMeleeAbility::FindGroundLocation(
	const AActor* IgnoreActor, const FVector& AroundLocation) const
{
	if (!GetWorld()) return AroundLocation;

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraEnemyAttackGround), false);
	if (IgnoreActor) QueryParams.AddIgnoredActor(IgnoreActor);
	if (LockedTarget.IsValid() && LockedTarget.Get() != IgnoreActor)
	{
		QueryParams.AddIgnoredActor(LockedTarget.Get());
	}
	const FVector TraceStart = AroundLocation + FVector(0.f, 0.f, 260.f);
	const FVector TraceEnd = AroundLocation - FVector(0.f, 0.f, 520.f);
	if (GetWorld()->LineTraceSingleByChannel(
		GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return GroundHit.ImpactPoint + FVector(0.f, 0.f, 4.f);
	}
	return AroundLocation;
}

const TCHAR* UAuraMeleeAbility::GetActiveActionName() const
{
	switch (ActiveAction)
	{
	case EAuraMeleeAction::GhoulSwipe: return TEXT("GhoulSwipe");
	case EAuraMeleeAction::GhoulFrenzy: return TEXT("GhoulFrenzy");
	case EAuraMeleeAction::GoblinJab: return TEXT("GoblinJab");
	case EAuraMeleeAction::GoblinLunge: return TEXT("GoblinLunge");
	case EAuraMeleeAction::EliteGoblinSweep: return TEXT("EliteGoblinSweep");
	case EAuraMeleeAction::ShroomBash: return TEXT("ShroomBash");
	case EAuraMeleeAction::ShroomSporeBurst: return TEXT("ShroomSporeBurst");
	case EAuraMeleeAction::EliteShroomSeed: return TEXT("EliteShroomSeed");
	default: return TEXT("None");
	}
}

void UAuraMeleeAbility::FinishMeleeAttack()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAuraMeleeAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (AAuraEnemy* Enemy = ActorInfo
		? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr)
	{
		Enemy->EndMeleeAction();
		if (bWasCancelled && ActiveAction != EAuraMeleeAction::None)
		{
			UE_LOG(LogAuraMelee, Display,
				TEXT("Enemy attack interrupted. Enemy=%s Action=%s"),
				*Enemy->GetName(), GetActiveActionName());
		}
	}
	ActiveAction = EAuraMeleeAction::None;
	LockedTarget.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
