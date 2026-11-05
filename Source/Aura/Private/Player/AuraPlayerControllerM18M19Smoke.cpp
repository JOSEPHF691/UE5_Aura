// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AI/AuraAIController.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystem/Abilities/AuraMeleeAbility.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraM18M19Smoke, Log, All);

namespace
{
	void FreezeTestEnemy(AAuraEnemy* Enemy)
	{
		if (!Enemy) return;
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AI->StopMovement();
			AI->SetActorTickEnabled(false);
		}
		UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
		ASC->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
		ASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 1000.f);
	}

	float EnemyHealth(const AAuraEnemy* Enemy)
	{
		return Enemy && Enemy->GetAbilitySystemComponent()
			? Enemy->GetAbilitySystemComponent()->GetNumericAttribute(
				UAuraAttributeSet::GetHealthAttribute()) : -1.f;
	}
}

void AAuraPlayerController::AuraTestComboM18()
{
	AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	if (!HasAuthority() || !IsLocalController() || !Caster || !GetWorld()
		|| !PrepareComboSmokeRanks({EAuraComboMarkType::Overload}))
	{
		UE_LOG(LogAuraM18M19Smoke, Error, TEXT("AURA_M18_COMBO Passed=false Setup=false"));
		return;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Origin = Caster->GetActorLocation() + FVector(600.f, 0.f, 2200.f);
	AAuraEnemy* Primary = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), Origin, FRotator::ZeroRotator, Spawn);
	AAuraEnemy* Near = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), Origin + FVector(0.f, 110.f, 0.f), FRotator::ZeroRotator, Spawn);
	AAuraEnemy* Far = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), Origin + FVector(0.f, 420.f, 0.f), FRotator::ZeroRotator, Spawn);
	if (!Primary || !Near || !Far)
	{
		UE_LOG(LogAuraM18M19Smoke, Error, TEXT("AURA_M18_COMBO Passed=false Spawn=false"));
		if (Primary) Primary->Destroy();
		if (Near) Near->Destroy();
		if (Far) Far->Destroy();
		return;
	}
	for (AAuraEnemy* Enemy : { Primary, Near, Far }) FreezeTestEnemy(Enemy);

	Caster->ApplyComboMark(Primary, EAuraComboMarkType::Overload,
		Caster->AllocateComboCastId());
	const bool bMarked = Caster->HasComboMark(Primary, EAuraComboMarkType::Overload);
	const int32 FinisherCast = Caster->AllocateComboCastId();
	const bool bWrongTarget = !Caster->TryConsumeComboMark(
		Near, EAuraComboMarkType::Overload, FinisherCast);
	const bool bWrongOrder = !Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Ember, FinisherCast);
	const bool bConsumedOnce = Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Overload, FinisherCast)
		&& !Caster->HasComboMark(Primary, EAuraComboMarkType::Overload);
	Caster->ApplyComboMark(Primary, EAuraComboMarkType::Overload,
		Caster->AllocateComboCastId());
	const bool bDuplicateCast = !Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Overload, FinisherCast);
	const bool bLaterCast = Caster->TryConsumeComboMark(
		Primary, EAuraComboMarkType::Overload, Caster->AllocateComboCastId());

	Near->BeginMeleeAction(EAuraMeleeAction::GhoulFrenzy, Near->GetActorLocation(), 90.f);
	const bool bTelegraphed = Near->GetMeleeAction() != EAuraMeleeAction::None;
	Caster->ExecuteOverload(Origin, UAuraCombatBalanceData::Get().FireBolt.Damage);
	const bool bBurst = EnemyHealth(Primary) < 1000.f && EnemyHealth(Near) < 1000.f
		&& FMath::IsNearlyEqual(EnemyHealth(Far), 1000.f);
	const bool bInterrupt = bTelegraphed && Near->GetMeleeAction() == EAuraMeleeAction::None;
	for (AAuraEnemy* Enemy : { Primary, Near, Far })
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(
			UAuraAttributeSet::GetHealthAttribute(), 1000.f);
	Caster->ApplyComboMark(Primary, EAuraComboMarkType::Overload,
		Caster->AllocateComboCastId());
	FActorSpawnParameters ProjectileSpawn;
	ProjectileSpawn.Owner = Caster;
	ProjectileSpawn.Instigator = Caster;
	ProjectileSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraFireBoltProjectile* Projectile = GetWorld()->SpawnActor<AAuraFireBoltProjectile>(
		AAuraFireBoltProjectile::StaticClass(), Origin - FVector(170.f, 0.f, 0.f),
		FRotator::ZeroRotator, ProjectileSpawn);
	if (Projectile)
	{
		Projectile->SetDamage(UAuraCombatBalanceData::Get().FireBolt.Damage);
		Projectile->SetComboCastId(Caster->AllocateComboCastId());
	}
	const bool bInitial = bMarked && bWrongTarget && bWrongOrder && bConsumedOnce
		&& bDuplicateCast && bLaterCast && bBurst && bInterrupt && Projectile;
	const TWeakObjectPtr<AAuraCharacter> WeakCaster(Caster);
	const TWeakObjectPtr<AAuraEnemy> WeakPrimary(Primary);
	const TWeakObjectPtr<AAuraEnemy> WeakNear(Near);
	const TWeakObjectPtr<AAuraEnemy> WeakFar(Far);
	FTimerHandle Timer;
	GetWorldTimerManager().SetTimer(Timer,
		[WeakCaster, WeakPrimary, WeakNear, WeakFar, bInitial, bMarked,
			bWrongTarget, bWrongOrder, bConsumedOnce, bDuplicateCast,
			bLaterCast, bBurst, bInterrupt]()
	{
		const bool bProjectileCombo = WeakCaster.IsValid() && WeakPrimary.IsValid()
			&& WeakNear.IsValid() && WeakFar.IsValid()
			&& !WeakCaster->HasComboMark(WeakPrimary.Get(), EAuraComboMarkType::Overload)
			&& EnemyHealth(WeakPrimary.Get())
				< 1000.f - UAuraCombatBalanceData::Get().FireBolt.Damage
			&& EnemyHealth(WeakNear.Get()) < 1000.f
			&& FMath::IsNearlyEqual(EnemyHealth(WeakFar.Get()), 1000.f);
		UE_LOG(LogAuraM18M19Smoke, Display,
			TEXT("AURA_M18_COMBO Passed=%s Mark=%d WrongTarget=%d WrongOrder=%d Once=%d Duplicate=%d Later=%d Burst=%d Interrupt=%d Projectile=%d"),
			bInitial && bProjectileCombo ? TEXT("true") : TEXT("false"), bMarked,
			bWrongTarget, bWrongOrder, bConsumedOnce, bDuplicateCast,
			bLaterCast, bBurst, bInterrupt, bProjectileCombo);
		if (WeakPrimary.IsValid()) WeakPrimary->Destroy();
		if (WeakNear.IsValid()) WeakNear->Destroy();
		if (WeakFar.IsValid()) WeakFar->Destroy();
	}, 0.7f, false);
}

void AAuraPlayerController::AuraTestEnemySynergyM19()
{
	AAuraCharacter* Target = Cast<AAuraCharacter>(GetPawn());
	if (!HasAuthority() || !IsLocalController() || !Target || !GetWorld())
	{
		UE_LOG(LogAuraM18M19Smoke, Error, TEXT("AURA_M19_SYNERGY Passed=false Setup=false"));
		return;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* Shroom = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), Target->GetActorLocation() + FVector(0.f, 0.f, 2200.f),
		FRotator::ZeroRotator, Spawn);
	if (!Shroom)
	{
		UE_LOG(LogAuraM18M19Smoke, Error, TEXT("AURA_M19_SYNERGY Passed=false Spawn=false"));
		return;
	}
	FreezeTestEnemy(Shroom);
	const float BaseSpeed = Target->GetCharacterMovement()->MaxWalkSpeed;
	const bool bApplied = Target->ApplySporeExposure(Shroom)
		&& Target->HasSporeExposure()
		&& Target->GetCharacterMovement()->MaxWalkSpeed < BaseSpeed;
	bool bDetonated = false;
	const float NormalDamage = UAuraMeleeAbility::ResolveSporeSynergyDamage(
		*Target, EAuraMeleeAction::GhoulSwipe, 0, 15.f, bDetonated);
	const bool bNormalSafe = FMath::IsNearlyEqual(NormalDamage, 15.f)
		&& !bDetonated && Target->HasSporeExposure();
	const float FirstFrenzy = UAuraMeleeAbility::ResolveSporeSynergyDamage(
		*Target, EAuraMeleeAction::GhoulFrenzy, 0, 9.f, bDetonated);
	const bool bFirstSafe = FMath::IsNearlyEqual(FirstFrenzy, 9.f)
		&& !bDetonated && Target->HasSporeExposure();
	const float SecondFrenzy = UAuraMeleeAbility::ResolveSporeSynergyDamage(
		*Target, EAuraMeleeAction::GhoulFrenzy, 1, 9.f, bDetonated);
	const bool bGhoulDetonated = bDetonated && !Target->HasSporeExposure()
		&& FMath::IsNearlyEqual(SecondFrenzy,
			9.f * UAuraCombatBalanceData::Get().MeleeEnemy.SporeDetonationDamageMultiplier)
		&& FMath::IsNearlyEqual(Target->GetCharacterMovement()->MaxWalkSpeed, BaseSpeed);
	Target->ApplySporeExposure(Shroom);
	const float Lunge = UAuraMeleeAbility::ResolveSporeSynergyDamage(
		*Target, EAuraMeleeAction::GoblinLunge, 0, 20.f, bDetonated);
	const bool bGoblinDetonated = bDetonated && !Target->HasSporeExposure()
		&& FMath::IsNearlyEqual(Lunge,
			20.f * UAuraCombatBalanceData::Get().MeleeEnemy.SporeDetonationDamageMultiplier);
	const bool bReapplied = Target->ApplySporeExposure(Shroom)
		&& Target->HasSporeExposure();
	const TWeakObjectPtr<AAuraCharacter> WeakTarget(Target);
	const TWeakObjectPtr<AAuraEnemy> WeakShroom(Shroom);
	FTimerHandle Timer;
	GetWorldTimerManager().SetTimer(Timer,
		[WeakTarget, WeakShroom, BaseSpeed, bApplied, bNormalSafe,
			bFirstSafe, bGhoulDetonated, bGoblinDetonated, bReapplied]()
	{
		const bool bExpired = WeakTarget.IsValid() && !WeakTarget->HasSporeExposure()
			&& FMath::IsNearlyEqual(WeakTarget->GetCharacterMovement()->MaxWalkSpeed,
				BaseSpeed);
		const bool bPassed = bApplied && bNormalSafe && bFirstSafe
			&& bGhoulDetonated && bGoblinDetonated && bReapplied && bExpired;
		UE_LOG(LogAuraM18M19Smoke, Display,
			TEXT("AURA_M19_SYNERGY Passed=%s Applied=%d NormalSafe=%d FirstSafe=%d Ghoul=%d Goblin=%d Reapplied=%d Expired=%d"),
			bPassed ? TEXT("true") : TEXT("false"), bApplied, bNormalSafe,
			bFirstSafe, bGhoulDetonated, bGoblinDetonated, bReapplied, bExpired);
		if (WeakShroom.IsValid()) WeakShroom->Destroy();
	}, UAuraCombatBalanceData::Get().MeleeEnemy.SporeExposureDuration + 0.35f, false);
}
