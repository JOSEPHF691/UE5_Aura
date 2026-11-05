// Copyright yosep

#include "AbilitySystem/Abilities/AuraBossAbility.h"

#include "AI/AuraAIController.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Character/AuraCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "EngineUtils.h"
#include "Game/AuraGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraBossAbility, Log, All);

UAuraBossAbility::UAuraBossAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ProjectileClass = AAuraFireBoltProjectile::StaticClass();

	static ConstructorHelpers::FObjectFinder<USoundBase> ClawSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Demon/Hurt/SFX_Demon_Hurt_01.SFX_Demon_Hurt_01"));
	if (ClawSoundAsset.Succeeded()) ClawSound = ClawSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> TailSpinSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireNado/SFX_Fire-Nado_01.SFX_Fire-Nado_01"));
	if (TailSpinSoundAsset.Succeeded()) TailSpinSound = TailSpinSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> ThrowSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireBolt/sfx_FireBoltLow.sfx_FireBoltLow"));
	if (ThrowSoundAsset.Succeeded()) ThrowSound = ThrowSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> SummonSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Demon/Hurt/SFX_Demon_Hurt_03.SFX_Demon_Hurt_03"));
	if (SummonSoundAsset.Succeeded()) SummonSound = SummonSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> ChargeSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireNado/SFX_Fire-Nado_02.SFX_Fire-Nado_02"));
	if (ChargeSoundAsset.Succeeded()) ChargeSound = ChargeSoundAsset.Object;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Enemy.Boss")));
	SetAssetTags(Tags);
}

float UAuraBossAbility::GetPhaseOneClawDamage() const
{
	return UAuraCombatBalanceData::Get().Boss.PhaseOneClawDamage;
}

float UAuraBossAbility::GetPhaseTwoClawDamage() const
{
	return UAuraCombatBalanceData::Get().Boss.PhaseTwoClawDamage;
}

float UAuraBossAbility::GetTailSpinDamage() const
{
	return UAuraCombatBalanceData::Get().Boss.TailSpinDamage;
}

float UAuraBossAbility::GetThrowDamage() const
{
	return UAuraCombatBalanceData::Get().Boss.ThrowDamage;
}

float UAuraBossAbility::GetChargeDamage() const
{
	return UAuraCombatBalanceData::Get().Boss.ChargeDamage;
}

float UAuraBossAbility::GetClawImpactTime() const
{
	return UAuraCombatBalanceData::Get().Boss.ClawImpactTime;
}

float UAuraBossAbility::GetTailSpinWindup() const
{
	return UAuraCombatBalanceData::Get().Boss.TailSpinWindup;
}

float UAuraBossAbility::GetThrowWindup() const
{
	return UAuraCombatBalanceData::Get().Boss.ThrowWindup;
}

float UAuraBossAbility::GetChargeWindup() const
{
	return UAuraCombatBalanceData::Get().Boss.ChargeWindup;
}

float UAuraBossAbility::GetSummonWindup() const
{
	return UAuraCombatBalanceData::Get().Boss.SummonWindup;
}

float UAuraBossAbility::GetRecoveryTime() const
{
	return UAuraCombatBalanceData::Get().Boss.RecoveryTime;
}

float UAuraBossAbility::GetPhaseOneAttackInterval() const
{
	return UAuraCombatBalanceData::Get().Boss.PhaseOneAttackInterval;
}

float UAuraBossAbility::GetPhaseTwoAttackInterval() const
{
	return UAuraCombatBalanceData::Get().Boss.PhaseTwoAttackInterval;
}

bool UAuraBossAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;

	const AAuraEnemy* Boss = ActorInfo
		? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	const AAuraAIController* AIController = Boss
		? Cast<AAuraAIController>(Boss->GetController()) : nullptr;
	const AAuraCharacter* Target = AIController ? AIController->GetCombatTarget() : nullptr;
	const UWorld* World = Boss ? Boss->GetWorld() : nullptr;
	if (!Boss || !Boss->IsBossEnemy() || Boss->IsDead() || Boss->IsStunned()
		|| Boss->IsActionLocked() || Boss->IsBossTransitioning()
		|| !Target || Target->IsDead() || !World
		|| World->GetTimeSeconds() < NextAllowedActivationTime)
	{
		return false;
	}

	const float Distance = FVector::Dist2D(Boss->GetActorLocation(), Target->GetActorLocation());
	return Boss->GetBossPhase() == EAuraBossPhase::PhaseOne
		? Distance <= Boss->GetBossMeleeRange()
		: Distance <= Boss->GetBossThrowRange();
}

void UAuraBossAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AAuraEnemy* Boss = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	AAuraAIController* AIController = Boss
		? Cast<AAuraAIController>(Boss->GetController()) : nullptr;
	AAuraCharacter* Target = AIController ? AIController->GetCombatTarget() : nullptr;
	if (!Boss || !Target || !Boss->HasAuthority() || Boss->IsBossTransitioning())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	LockedTarget = Target;
	LockedAimLocation = Target->GetActorLocation() + FVector(0.f, 0.f, 45.f);
	const float Distance = FVector::Dist2D(Boss->GetActorLocation(), Target->GetActorLocation());
	const bool bPhaseTwo = Boss->GetBossPhase() == EAuraBossPhase::PhaseTwo;
	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	float WindupTime = GetClawImpactTime();
	FVector TelegraphLocation = Boss->GetActorLocation();
	float TelegraphRadius = 0.f;
	const FVector Direction = (Target->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero()) Boss->SetActorRotation(Direction.Rotation());
	ActiveAction = SelectAction(*Boss, Distance);
	switch (ActiveAction)
	{
	case EAuraBossAction::ClawLeft:
	case EAuraBossAction::ClawRight:
		TelegraphLocation = FindGroundLocation(Boss,
			Boss->GetActorLocation() + Direction * Boss->GetBossMeleeRange());
		TelegraphRadius = Boss->GetBossMeleeRange();
		break;
	case EAuraBossAction::TailSpin:
		WindupTime = GetTailSpinWindup();
		TelegraphLocation = FindGroundLocation(Boss, Boss->GetActorLocation());
		TelegraphRadius = Boss->GetBossTailSpinRadius();
		break;
	case EAuraBossAction::HellfireThrow:
		WindupTime = GetThrowWindup();
		TelegraphLocation = FindGroundLocation(Boss, Target->GetActorLocation());
		TelegraphRadius = Boss->GetBossThrowWarningRadius();
		break;
	case EAuraBossAction::InfernalBarrage:
	{
		WindupTime = Balance.BarrageWindup;
		BarrageForward = Direction.IsNearlyZero()
			? Boss->GetActorForwardVector().GetSafeNormal2D() : Direction;
		if (BarrageForward.IsNearlyZero()) BarrageForward = FVector::ForwardVector;
		BarrageAimYawOffset = 0.f;
		TelegraphLocation = FindGroundLocation(Boss,
			Boss->GetActorLocation() + BarrageForward * Balance.BarrageRange);
		TelegraphRadius = Balance.BarrageRange;
		NextAllowedBarrageTime = GetWorld()->GetTimeSeconds() + Balance.BarrageCooldown;
		break;
	}
	case EAuraBossAction::SummonMinions:
		WindupTime = GetSummonWindup();
		TelegraphLocation = FindGroundLocation(Boss, Boss->GetActorLocation());
		TelegraphRadius = Balance.SummonRadius;
		NextAllowedSummonTime = GetWorld()->GetTimeSeconds() + Balance.SummonCooldown;
		break;
	case EAuraBossAction::DemonicCharge:
	{
		WindupTime = GetChargeWindup();
		ChargeStartLocation = Boss->GetActorLocation();
		const float ChargeTravel = FMath::Clamp(
			Distance - Boss->GetBossMeleeRange() * 0.72f, 120.f, Balance.ChargeDistance);
		TelegraphLocation = FindGroundLocation(Boss,
			Boss->GetActorLocation() + Direction * ChargeTravel);
		TelegraphRadius = Balance.ChargeHalfWidth;
		break;
	}
	case EAuraBossAction::DemonicWard:
		WindupTime = Balance.WardWindup;
		TelegraphLocation = FindGroundLocation(Boss, Boss->GetActorLocation());
		TelegraphRadius = Boss->GetBossMeleeRange() * 0.85f;
		NextAllowedWardTime = GetWorld()->GetTimeSeconds() + Balance.WardCooldown;
		++WardCastsUsed;
		break;
	default:
		break;
	}

	Boss->BeginBossAction(ActiveAction, TelegraphLocation, TelegraphRadius);
	NextAllowedActivationTime = GetWorld()->GetTimeSeconds()
		+ (bPhaseTwo ? GetPhaseTwoAttackInterval() : GetPhaseOneAttackInterval());

	USoundBase* ActionSound = ClawSound.Get();
	if (ActiveAction == EAuraBossAction::TailSpin) ActionSound = TailSpinSound.Get();
	else if (ActiveAction == EAuraBossAction::HellfireThrow
		|| ActiveAction == EAuraBossAction::InfernalBarrage) ActionSound = ThrowSound.Get();
	else if (ActiveAction == EAuraBossAction::SummonMinions) ActionSound = SummonSound.Get();
	else if (ActiveAction == EAuraBossAction::DemonicCharge) ActionSound = ChargeSound.Get();
	else if (ActiveAction == EAuraBossAction::DemonicWard) ActionSound = SummonSound.Get();
	if (ActionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(Boss, ActionSound, Boss->GetActorLocation(), 0.82f);
	}

	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss action started. Boss=%s Phase=%d Action=%d Windup=%.2f Distance=%.1f TelegraphRadius=%.1f"),
		*Boss->GetName(), static_cast<int32>(Boss->GetBossPhase()),
		static_cast<int32>(ActiveAction), WindupTime, Distance, TelegraphRadius);
	UAbilityTask_WaitDelay* WaitForImpact = UAbilityTask_WaitDelay::WaitDelay(this, WindupTime);
	WaitForImpact->OnFinish.AddDynamic(this, &UAuraBossAbility::ResolveActiveAction);
	WaitForImpact->ReadyForActivation();
}

EAuraBossAction UAuraBossAbility::SelectAlternatingClaw()
{
	const EAuraBossAction Claw = bNextClawIsLeft
		? EAuraBossAction::ClawLeft : EAuraBossAction::ClawRight;
	bNextClawIsLeft = !bNextClawIsLeft;
	return Claw;
}

EAuraBossAction UAuraBossAbility::SelectAction(
	AAuraEnemy& Boss, const float DistanceToTarget)
{
	if (Boss.GetBossPhase() != EAuraBossPhase::PhaseTwo)
	{
		ActionsWithoutClaw = 0;
		CloseActionsWithoutTail = 0;
		ActionsWithoutSummon = 0;
		FarActionsWithoutThrow = 0;
		PreviousAction = SelectAlternatingClaw();
		return PreviousAction;
	}

	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	const bool bInClawRange = DistanceToTarget <= Boss.GetBossMeleeRange();
	const bool bInSweepRange = DistanceToTarget <= Boss.GetBossTailSpinRadius();
	const bool bSummonAvailable = CanSummon(Boss)
		&& PreviousAction != EAuraBossAction::SummonMinions;
	const bool bWardAvailable = !Boss.HasBossWard()
		&& WardCastsUsed < Balance.WardMaxCasts
		&& GetWorld() && GetWorld()->GetTimeSeconds() >= NextAllowedWardTime
		&& PreviousAction != EAuraBossAction::DemonicWard;
	const bool bBarrageAvailable = DistanceToTarget > Boss.GetBossTailSpinRadius()
		&& GetWorld() && GetWorld()->GetTimeSeconds() >= NextAllowedBarrageTime
		&& PreviousAction != EAuraBossAction::InfernalBarrage;
	const float SummonRoll = FMath::FRand();
	const float WardRoll = FMath::FRand();
	const float BarrageRoll = FMath::FRand();
	const float PrimaryRoll = FMath::FRand();
	EAuraBossAction Selected = EAuraBossAction::None;

	// A charge promises a claw follow-up. This also fixes the old phase-two loop
	// where a ranged player could make the boss cast Hellfire forever.
	if (bForceClawAfterCharge)
	{
		Selected = bInClawRange
			? SelectAlternatingClaw() : EAuraBossAction::DemonicCharge;
	}
	else if ((FParse::Param(FCommandLine::Get(), TEXT("AuraBossBarrageSmoke"))
		|| FParse::Param(FCommandLine::Get(), TEXT("AuraBossBarrageCancelSmoke")))
		&& bBarrageAvailable && NextAllowedBarrageTime <= 0.f)
	{
		// Deterministic first cast for the isolated barrage regression smoke.
		Selected = EAuraBossAction::InfernalBarrage;
	}
	else if (bInClawRange && ActionsWithoutClaw >= Balance.MaxActionsWithoutClaw)
	{
		Selected = SelectAlternatingClaw();
	}
	else if (bWardAvailable && WardRoll < Balance.WardChance)
	{
		Selected = EAuraBossAction::DemonicWard;
	}
	else if (bSummonAvailable
		&& (ActionsWithoutSummon >= Balance.MaxActionsWithoutSummon
			|| SummonRoll < Balance.SummonChance))
	{
		Selected = EAuraBossAction::SummonMinions;
	}
	else if (bBarrageAvailable && FarActionsWithoutThrow < Balance.MaxFarActionsWithoutThrow
		&& BarrageRoll < Balance.BarrageChance)
	{
		Selected = EAuraBossAction::InfernalBarrage;
	}
	else if (bInClawRange)
	{
		const bool bTailPity = CloseActionsWithoutTail >= Balance.MaxCloseActionsWithoutTail;
		Selected = PreviousAction != EAuraBossAction::TailSpin
			&& (bTailPity || PrimaryRoll < Balance.TailSpinChance)
			? EAuraBossAction::TailSpin : SelectAlternatingClaw();
	}
	else if (bInSweepRange)
	{
		const bool bTailPity = CloseActionsWithoutTail >= Balance.MaxCloseActionsWithoutTail;
		Selected = PreviousAction != EAuraBossAction::TailSpin
			&& (bTailPity || PrimaryRoll < Balance.TailSpinChance)
			? EAuraBossAction::TailSpin : EAuraBossAction::DemonicCharge;
	}
	else
	{
		const bool bMustCloseForClaw = ActionsWithoutClaw >= Balance.MaxActionsWithoutClaw;
		if (FarActionsWithoutThrow >= Balance.MaxFarActionsWithoutThrow)
		{
			Selected = EAuraBossAction::HellfireThrow;
		}
		else
		{
			Selected = (bMustCloseForClaw || PreviousAction == EAuraBossAction::HellfireThrow
				|| PrimaryRoll < Balance.ChargeChance)
				? EAuraBossAction::DemonicCharge : EAuraBossAction::HellfireThrow;
		}
	}

	if (Selected == EAuraBossAction::ClawLeft || Selected == EAuraBossAction::ClawRight)
	{
		ActionsWithoutClaw = 0;
		bForceClawAfterCharge = false;
		++CloseActionsWithoutTail;
	}
	else
	{
		++ActionsWithoutClaw;
		if (Selected == EAuraBossAction::TailSpin) CloseActionsWithoutTail = 0;
		else if (bInSweepRange) ++CloseActionsWithoutTail;
		if (Selected == EAuraBossAction::DemonicCharge) bForceClawAfterCharge = true;
	}

	if (Selected == EAuraBossAction::SummonMinions) ActionsWithoutSummon = 0;
	else ++ActionsWithoutSummon;
	if (!bInSweepRange)
	{
		if (Selected == EAuraBossAction::HellfireThrow) FarActionsWithoutThrow = 0;
		else ++FarActionsWithoutThrow;
	}
	PreviousAction = Selected;
	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss action roll. Boss=%s Selected=%d Distance=%.1f PrimaryRoll=%.3f SummonRoll=%.3f WardRoll=%.3f BarrageRoll=%.3f WardCasts=%d NoClaw=%d NoTail=%d NoSummon=%d NoThrow=%d"),
		*Boss.GetName(), static_cast<int32>(Selected), DistanceToTarget,
		PrimaryRoll, SummonRoll, WardRoll, BarrageRoll, WardCastsUsed, ActionsWithoutClaw,
		CloseActionsWithoutTail, ActionsWithoutSummon, FarActionsWithoutThrow);
	return Selected;
}

void UAuraBossAbility::ResolveActiveAction()
{
	AAuraEnemy* Boss = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	AAuraCharacter* Target = LockedTarget.Get();
	if (!Boss || !Boss->HasAuthority() || Boss->IsDead() || Boss->IsStunned()
		|| Boss->IsBossTransitioning())
	{
		FinishActiveAction();
		return;
	}

	Boss->ClearBossTelegraph();
	switch (ActiveAction)
	{
	case EAuraBossAction::ClawLeft:
	case EAuraBossAction::ClawRight:
		ResolveClawCleave(*Boss);
		break;

	case EAuraBossAction::TailSpin:
	{
		const FVector ImpactLocation = FindGroundLocation(Boss, Boss->GetActorLocation());
		Boss->PlayBossImpactEffect(ImpactLocation, Boss->GetBossTailSpinRadius());
		int32 HitCount = 0;
		for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
		{
			AAuraCharacter* Candidate = *It;
			if (!Candidate || Candidate->IsDead()
				|| FVector::DistSquared2D(Boss->GetActorLocation(), Candidate->GetActorLocation())
					> FMath::Square(Boss->GetBossTailSpinRadius()))
			{
				continue;
			}
			ApplyDamageToTarget(Candidate, GetTailSpinDamage());
			++HitCount;
		}
		UE_LOG(LogAuraBossAbility, Display,
			TEXT("Boss tail sweep affected %d player(s). Boss=%s"), HitCount, *Boss->GetName());
		break;
	}

	case EAuraBossAction::HellfireThrow:
		if (ProjectileClass && GetWorld())
		{
			const FVector SpawnLocation = Boss->GetActorLocation()
				+ FVector(0.f, 0.f, 145.f) + Boss->GetActorForwardVector() * 115.f;
			FVector LaunchDirection = (LockedAimLocation - SpawnLocation).GetSafeNormal();
			if (LaunchDirection.IsNearlyZero()) LaunchDirection = Boss->GetActorForwardVector();

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = Boss;
			SpawnParameters.Instigator = Boss;
			SpawnParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AAuraFireBoltProjectile* Projectile = GetWorld()->SpawnActor<AAuraFireBoltProjectile>(
				ProjectileClass, SpawnLocation, LaunchDirection.Rotation(), SpawnParameters))
			{
				Projectile->SetDamage(GetThrowDamage());
				Boss->PlayBossProjectileReleaseEffect(
					SpawnLocation, LaunchDirection, EAuraBossAction::HellfireThrow);
			}
		}
		break;

	case EAuraBossAction::InfernalBarrage:
		BarrageShotsFired = 0;
		bBarrageProjectileSpawnFailed = false;
		FireNextBarrageShot();
		if (ActiveAction == EAuraBossAction::InfernalBarrage
			&& BarrageShotsFired < UAuraCombatBalanceData::Get().Boss.BarrageShotCount)
		{
			GetWorld()->GetTimerManager().SetTimer(BarrageShotTimer, this,
				&UAuraBossAbility::FireNextBarrageShot,
				UAuraCombatBalanceData::Get().Boss.BarrageShotInterval, true);
		}
		return;

	case EAuraBossAction::SummonMinions:
		ResolveSummonMinions(*Boss);
		break;

	case EAuraBossAction::DemonicCharge:
		ResolveDemonicCharge(*Boss);
		break;

	case EAuraBossAction::DemonicWard:
		Boss->ActivateBossWard();
		Boss->PlayBossImpactEffect(FindGroundLocation(Boss, Boss->GetActorLocation()),
			Boss->GetBossMeleeRange() * 0.85f, EAuraBossAction::DemonicWard);
		break;

	default:
		break;
	}

	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss action resolved. Boss=%s Action=%d Target=%s"),
		*Boss->GetName(), static_cast<int32>(ActiveAction), *GetNameSafe(Target));
	BeginRecovery();
}

void UAuraBossAbility::FireNextBarrageShot()
{
	AAuraEnemy* Boss = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	UWorld* World = GetWorld();
	if (!Boss || !World || !Boss->HasAuthority() || Boss->IsDead() || Boss->IsStunned()
		|| Boss->IsBossTransitioning() || Boss->GetBossPhase() != EAuraBossPhase::PhaseTwo
		|| Boss->GetBossAction() != EAuraBossAction::InfernalBarrage
		|| ActiveAction != EAuraBossAction::InfernalBarrage)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	if (BarrageShotsFired >= Balance.BarrageShotCount) return;
	const AAuraCharacter* Target = LockedTarget.Get();
	const FVector AimLocation = Target && !Target->IsDead()
		? Target->GetActorLocation() + FVector(0.f, 0.f, 45.f) : LockedAimLocation;
	const FVector ToTarget = (AimLocation - Boss->GetActorLocation()).GetSafeNormal2D();
	const float BaseYaw = BarrageForward.Rotation().Yaw;
	const float DesiredOffset = ToTarget.IsNearlyZero() ? BarrageAimYawOffset
		: FMath::Clamp(FMath::FindDeltaAngleDegrees(BaseYaw, ToTarget.Rotation().Yaw),
			-Balance.BarrageHalfAngleDegrees, Balance.BarrageHalfAngleDegrees);
	const float AimTime = BarrageShotsFired == 0
		? Balance.BarrageWindup : Balance.BarrageShotInterval;
	BarrageAimYawOffset = FMath::FixedTurn(BarrageAimYawOffset, DesiredOffset,
		Balance.BarrageAimTurnRateDegreesPerSecond * AimTime);
	// Re-aim the boss between shots, but never outside the warned fan. A spawned
	// projectile keeps this launch rotation and does not home in flight.
	const float LaunchYaw = BaseYaw + BarrageAimYawOffset;
	Boss->SetActorRotation(FRotator(0.f, LaunchYaw, 0.f));
	const FVector SpawnLocation = Boss->GetActorLocation()
		+ FVector(0.f, 0.f, 145.f) + Boss->GetActorForwardVector() * 115.f;
	FRotator LaunchRotation = (AimLocation - SpawnLocation).Rotation();
	LaunchRotation.Yaw = LaunchYaw;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Boss;
	SpawnParameters.Instigator = Boss;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ProjectileClass)
	{
		if (AAuraFireBoltProjectile* Projectile = World->SpawnActor<AAuraFireBoltProjectile>(
			ProjectileClass, SpawnLocation, LaunchRotation, SpawnParameters))
		{
			Projectile->SetDamage(Balance.BarrageShotDamage);
			Boss->PlayBossProjectileReleaseEffect(
				SpawnLocation, LaunchRotation.Vector(), EAuraBossAction::InfernalBarrage);
		}
		else bBarrageProjectileSpawnFailed = true;
	}
	else bBarrageProjectileSpawnFailed = true;
	++BarrageShotsFired;
	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss barrage shot. Boss=%s Shot=%d/%d AimYawOffset=%.1f Target=%s"),
		*Boss->GetName(), BarrageShotsFired, Balance.BarrageShotCount,
		BarrageAimYawOffset, *GetNameSafe(Target));
	if (BarrageShotsFired >= Balance.BarrageShotCount)
	{
		World->GetTimerManager().ClearTimer(BarrageShotTimer);
		UE_LOG(LogAuraBossAbility, Display,
			TEXT("Boss barrage completed. Boss=%s Shots=%d"),
			*Boss->GetName(), BarrageShotsFired);
		BeginRecovery();
	}
}

void UAuraBossAbility::BeginRecovery()
{
	const float Recovery = ActiveAction == EAuraBossAction::InfernalBarrage
		? UAuraCombatBalanceData::Get().Boss.BarrageRecovery : GetRecoveryTime();
	UAbilityTask_WaitDelay* WaitForRecovery = UAbilityTask_WaitDelay::WaitDelay(
		this, Recovery);
	WaitForRecovery->OnFinish.AddDynamic(this, &UAuraBossAbility::FinishActiveAction);
	WaitForRecovery->ReadyForActivation();
}

void UAuraBossAbility::FinishActiveAction()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAuraBossAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(BarrageShotTimer);
	BarrageShotsFired = 0;
	if (AAuraEnemy* Boss = ActorInfo
		? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr)
	{
		Boss->EndBossAction();
		// A cancelled charge never earned its promised claw follow-up. Clearing the
		// latch prevents an interrupt from forcing the boss straight back into it.
		if (bWasCancelled && ActiveAction == EAuraBossAction::DemonicCharge)
		{
			bForceClawAfterCharge = false;
		}
		if (bWasCancelled && ActiveAction != EAuraBossAction::None)
		{
			UE_LOG(LogAuraBossAbility, Display,
				TEXT("Boss action interrupted. Boss=%s Action=%d"),
				*Boss->GetName(), static_cast<int32>(ActiveAction));
		}
	}
	ActiveAction = EAuraBossAction::None;
	LockedTarget.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAuraBossAbility::ResolveClawCleave(AAuraEnemy& Boss)
{
	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	const float Damage = Boss.GetBossPhase() == EAuraBossPhase::PhaseTwo
		? GetPhaseTwoClawDamage() : GetPhaseOneClawDamage();
	const FVector Forward = Boss.GetActorForwardVector().GetSafeNormal2D();
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(Balance.ClawHalfAngleDegrees));
	int32 HitCount = 0;
	for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
	{
		AAuraCharacter* Candidate = *It;
		if (!Candidate || Candidate->IsDead()) continue;
		FVector ToCandidate = Candidate->GetActorLocation() - Boss.GetActorLocation();
		ToCandidate.Z = 0.f;
		if (ToCandidate.SizeSquared() > FMath::Square(Boss.GetBossMeleeRange())) continue;
		if (!ToCandidate.IsNearlyZero()
			&& FVector::DotProduct(Forward, ToCandidate.GetSafeNormal()) < MinimumDot)
		{
			continue;
		}
		ApplyDamageToTarget(Candidate, Damage);
		++HitCount;
	}
	Boss.PlayBossImpactEffect(FindGroundLocation(&Boss,
		Boss.GetActorLocation() + Forward * Boss.GetBossMeleeRange() * 0.65f), 120.f);
	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss claw cleave resolved. Boss=%s Side=%s Targets=%d Arc=%.1f"),
		*Boss.GetName(), ActiveAction == EAuraBossAction::ClawLeft ? TEXT("Left") : TEXT("Right"),
		HitCount, Balance.ClawHalfAngleDegrees * 2.f);
}

void UAuraBossAbility::ResolveDemonicCharge(AAuraEnemy& Boss)
{
	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	FVector Direction = (LockedAimLocation - Boss.GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero()) Direction = Boss.GetActorForwardVector().GetSafeNormal2D();
	const float DistanceToAim = FVector::Dist2D(Boss.GetActorLocation(), LockedAimLocation);
	const float TravelDistance = FMath::Clamp(
		DistanceToAim - Boss.GetBossMeleeRange() * 0.72f, 0.f, Balance.ChargeDistance);
	const FVector Start = Boss.GetActorLocation();
	FHitResult MoveHit;
	Boss.AddActorWorldOffset(Direction * TravelDistance, true, &MoveHit, ETeleportType::None);
	const FVector End = Boss.GetActorLocation();
	bForceClawAfterCharge = LockedTarget.IsValid()
		&& FVector::Dist2D(Boss.GetActorLocation(), LockedTarget->GetActorLocation())
			<= Boss.GetBossMeleeRange();

	int32 HitCount = 0;
	for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
	{
		AAuraCharacter* Candidate = *It;
		if (!Candidate || Candidate->IsDead()) continue;
		FVector PlayerPoint = Candidate->GetActorLocation();
		FVector SegmentStart = Start;
		FVector SegmentEnd = End;
		PlayerPoint.Z = SegmentStart.Z = SegmentEnd.Z = 0.f;
		const FVector ClosestPoint = FMath::ClosestPointOnSegment(
			PlayerPoint, SegmentStart, SegmentEnd);
		const float PlayerRadius = Candidate->GetCapsuleComponent()
			? Candidate->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f;
		if (FVector::DistSquared2D(PlayerPoint, ClosestPoint)
			> FMath::Square(Balance.ChargeHalfWidth + PlayerRadius))
		{
			continue;
		}
		ApplyDamageToTarget(Candidate, GetChargeDamage());
		++HitCount;
	}
	Boss.PlayBossImpactEffect(FindGroundLocation(&Boss, End), Balance.ChargeHalfWidth);
	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss demonic charge resolved. Boss=%s Travel=%.1f Blocked=%s Targets=%d ClawFollowUp=%s"),
		*Boss.GetName(), FVector::Dist2D(Start, End),
		MoveHit.bBlockingHit ? TEXT("true") : TEXT("false"), HitCount,
		bForceClawAfterCharge ? TEXT("true") : TEXT("false"));
}

int32 UAuraBossAbility::CountActiveSummons(const AAuraEnemy& Boss) const
{
	if (!GetWorld()) return 0;
	int32 Count = 0;
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		const AAuraEnemy* Candidate = *It;
		if (Candidate && !Candidate->IsDead() && Candidate->GetBossSummoner() == &Boss) ++Count;
	}
	return Count;
}

bool UAuraBossAbility::CanSummon(const AAuraEnemy& Boss) const
{
	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	return GetWorld() && Boss.GetBossPhase() == EAuraBossPhase::PhaseTwo
		&& GetWorld()->GetTimeSeconds() >= NextAllowedSummonTime
		&& !Boss.GetBossSummonClasses().IsEmpty()
		&& CountActiveSummons(Boss) < Balance.MaxActiveSummons;
}

void UAuraBossAbility::ResolveSummonMinions(AAuraEnemy& Boss)
{
	if (!GetWorld()) return;
	const FAuraBossBalance& Balance = UAuraCombatBalanceData::Get().Boss;
	const TArray<TSubclassOf<AAuraEnemy>>& SummonClasses = Boss.GetBossSummonClasses();
	const int32 AvailableSlots = FMath::Max(0,
		Balance.MaxActiveSummons - CountActiveSummons(Boss));
	const int32 SpawnCount = FMath::Min(Balance.SummonCount, AvailableSlots);
	if (SpawnCount <= 0 || SummonClasses.IsEmpty()) return;

	UNavigationSystemV1* NavigationSystem =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		const float Angle = 360.f * static_cast<float>(Index) / static_cast<float>(SpawnCount)
			+ 35.f + FMath::FRandRange(-12.f, 12.f);
		const FVector RadialDirection = FVector::ForwardVector.RotateAngleAxis(
			Angle, FVector::UpVector);
		FVector DesiredLocation = Boss.GetActorLocation()
			+ RadialDirection * Balance.SummonRadius * 0.72f;
		FNavLocation ProjectedLocation;
		if (NavigationSystem && NavigationSystem->ProjectPointToNavigation(
			DesiredLocation, ProjectedLocation, FVector(150.f, 150.f, 280.f)))
		{
			DesiredLocation = ProjectedLocation.Location;
		}
		const FVector GroundLocation = FindGroundLocation(&Boss, DesiredLocation);
		const TSubclassOf<AAuraEnemy> SummonClass =
			SummonClasses[SummonClassCursor++ % SummonClasses.Num()];
		const AAuraEnemy* DefaultEnemy = SummonClass
			? SummonClass->GetDefaultObject<AAuraEnemy>() : nullptr;
		const float CapsuleHalfHeight = DefaultEnemy && DefaultEnemy->GetCapsuleComponent()
			? DefaultEnemy->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 90.f;
		const FVector SpawnLocation = GroundLocation + FVector(0.f, 0.f, CapsuleHalfHeight);
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = &Boss;
		SpawnParameters.Instigator = &Boss;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const FVector FaceDirection = LockedTarget.IsValid()
			? (LockedTarget->GetActorLocation() - SpawnLocation).GetSafeNormal2D()
			: -RadialDirection;
		if (AAuraEnemy* Summon = GetWorld()->SpawnActor<AAuraEnemy>(
			SummonClass, SpawnLocation, FaceDirection.Rotation(), SpawnParameters))
		{
			const AAuraGameState* DungeonState = GetWorld()->GetGameState<AAuraGameState>();
			if (DungeonState && DungeonState->IsAbyssTrialActive())
			{
				Summon->ApplyAbyssHealthBonus(
					UAuraCombatBalanceData::Get().AbyssTrial.MaxHealthMultiplier);
			}
			Summon->ConfigureAsBossSummon(&Boss);
			Summon->SetActorScale3D(Summon->GetActorScale3D() * 0.88f);
			Boss.PlayBossImpactEffect(GroundLocation, 85.f);
			++SpawnedCount;
		}
	}
	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss summoned minions. Boss=%s Spawned=%d Active=%d Max=%d"),
		*Boss.GetName(), SpawnedCount, CountActiveSummons(Boss), Balance.MaxActiveSummons);
}

void UAuraBossAbility::ApplyDamageToTarget(AAuraCharacter* Target, const float Damage) const
{
	AAuraEnemy* Boss = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* SourceAbilitySystem = GetAbilitySystemComponentFromActorInfo();
	UAbilitySystemComponent* TargetAbilitySystem = Target ? Target->GetAbilitySystemComponent() : nullptr;
	if (!Boss || !Target || Target->IsDead() || !SourceAbilitySystem || !TargetAbilitySystem) return;

	FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(Boss);
	FGameplayEffectSpecHandle DamageSpec = SourceAbilitySystem->MakeOutgoingSpec(
		UAuraDamageGameplayEffect::StaticClass(), 1.f, EffectContext);
	if (!DamageSpec.IsValid()) return;

	DamageSpec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")),
		-Damage * Boss->GetDifficultyDamageMultiplier());
	TargetAbilitySystem->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
	UE_LOG(LogAuraBossAbility, Display,
		TEXT("Boss hit player. Boss=%s Action=%d Damage=%.1f PlayerHealth=%.1f"),
		*Boss->GetName(), static_cast<int32>(ActiveAction), Damage,
		TargetAbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()));
}

FVector UAuraBossAbility::FindGroundLocation(
	const AActor* IgnoreActor, const FVector& AroundLocation) const
{
	if (!GetWorld()) return AroundLocation;

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraBossGround), false);
	if (IgnoreActor) QueryParams.AddIgnoredActor(IgnoreActor);
	if (LockedTarget.IsValid() && LockedTarget.Get() != IgnoreActor)
	{
		QueryParams.AddIgnoredActor(LockedTarget.Get());
	}
	const FVector TraceStart = AroundLocation + FVector(0.f, 0.f, 300.f);
	const FVector TraceEnd = AroundLocation - FVector(0.f, 0.f, 650.f);
	if (GetWorld()->LineTraceSingleByChannel(
		GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return GroundHit.ImpactPoint + FVector(0.f, 0.f, 4.f);
	}
	return AroundLocation;
}
