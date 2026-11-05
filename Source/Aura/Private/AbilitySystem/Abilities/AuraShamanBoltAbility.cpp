// Copyright yosep

#include "AbilitySystem/Abilities/AuraShamanBoltAbility.h"

#include "AI/AuraAIController.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraShamanAbility, Log, All);

UAuraShamanBoltAbility::UAuraShamanBoltAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ProjectileClass = AAuraFireBoltProjectile::StaticClass();
	CooldownGameplayEffectClass = UAuraShamanBoltCooldownEffect::StaticClass();

	static ConstructorHelpers::FObjectFinder<USoundBase> CastSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireBolt/sfx_FireBoltLow.sfx_FireBoltLow"));
	if (CastSoundAsset.Succeeded()) CastSound = CastSoundAsset.Object;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Enemy.ShamanBolt")));
	SetAssetTags(Tags);
}

float UAuraShamanBoltAbility::GetCooldownDuration() const
{
	const FGameplayAbilityActorInfo* ActorInfo = HasAnyFlags(RF_ClassDefaultObject)
		? nullptr : GetCurrentActorInfo();
	const AAuraEnemy* Enemy = ActorInfo
		? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	return UAuraCombatBalanceData::Get().Shaman.Cooldown
		* (Enemy ? Enemy->GetEliteAttackIntervalMultiplier() : 1.f);
}

float UAuraShamanBoltAbility::GetDamage() const
{
	return UAuraCombatBalanceData::Get().Shaman.Damage;
}

float UAuraShamanBoltAbility::GetWindupTime() const
{
	return UAuraCombatBalanceData::Get().Shaman.WindupTime;
}

float UAuraShamanBoltAbility::GetCastRange() const
{
	return UAuraCombatBalanceData::Get().Shaman.CastRange;
}

float UAuraShamanBoltAbility::GetVolleyChance() const
{
	return UAuraCombatBalanceData::Get().Shaman.VolleyChance;
}

int32 UAuraShamanBoltAbility::GetMaxNormalCastsBeforeVolley() const
{
	return UAuraCombatBalanceData::Get().Shaman.MaxNormalCastsBeforeVolley;
}

int32 UAuraShamanBoltAbility::GetVolleyProjectileCount() const
{
	return UAuraCombatBalanceData::Get().Shaman.VolleyProjectileCount;
}

float UAuraShamanBoltAbility::GetVolleySpreadDegrees() const
{
	return UAuraCombatBalanceData::Get().Shaman.VolleySpreadDegrees;
}

float UAuraShamanBoltAbility::GetVolleyDamageMultiplier() const
{
	return UAuraCombatBalanceData::Get().Shaman.VolleyDamageMultiplier;
}

float UAuraShamanBoltAbility::GetVolleyWindupBonus() const
{
	return UAuraCombatBalanceData::Get().Shaman.VolleyWindupBonus;
}

float UAuraShamanBoltAbility::GetGroundBurstChance() const
{
	return UAuraCombatBalanceData::Get().Shaman.GroundBurstChance;
}

int32 UAuraShamanBoltAbility::GetMaxNormalCastsBeforeGroundBurst() const
{
	return UAuraCombatBalanceData::Get().Shaman.MaxNormalCastsBeforeGroundBurst;
}

float UAuraShamanBoltAbility::GetGroundBurstDamage() const
{
	return UAuraCombatBalanceData::Get().Shaman.GroundBurstDamage;
}

float UAuraShamanBoltAbility::GetGroundBurstRadius() const
{
	return UAuraCombatBalanceData::Get().Shaman.GroundBurstRadius;
}

float UAuraShamanBoltAbility::GetGroundBurstWindup() const
{
	return UAuraCombatBalanceData::Get().Shaman.GroundBurstWindup;
}

float UAuraShamanBoltAbility::GetGroundBurstCooldown() const
{
	return UAuraCombatBalanceData::Get().Shaman.GroundBurstCooldown;
}

bool UAuraShamanBoltAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(
		Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
	const AAuraEnemy* Enemy = ActorInfo
		? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr;
	const AAuraAIController* AIController = Enemy
		? Cast<AAuraAIController>(Enemy->GetController()) : nullptr;
	const AAuraCharacter* Target = AIController ? AIController->GetCombatTarget() : nullptr;
	return Enemy && Enemy->IsRangedCombatant() && !Enemy->IsDead()
		&& !Enemy->IsStunned() && !Enemy->IsActionLocked()
		&& Target && !Target->IsDead()
		&& FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation())
			<= FMath::Square(FMath::Min(GetCastRange(), Enemy->GetRangedAttackRange()))
		&& AIController->HasClearRangedShot(Enemy, Target);
}

void UAuraShamanBoltAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
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

	LockedAimLocation = Target->GetActorLocation() + FVector(0.f, 0.f, 45.f);
	++CastSequence;
	const float Now = GetWorld()->GetTimeSeconds();
	LastGroundBurstRoll = FMath::FRand();
	const bool bGroundBurstPity = NormalCastsSinceGroundBurst
		>= FMath::Max(1, GetMaxNormalCastsBeforeGroundBurst());
	bActiveCastIsGroundBurst = bForceNextGroundBurstForTest
		|| (!bLastCastWasSpecial && Now >= NextGroundBurstReadyTime
			&& (bGroundBurstPity || LastGroundBurstRoll < FMath::Clamp(
				GetGroundBurstChance() + Enemy->GetEliteSpecialChanceBonus() * 0.5f,
				0.f, 1.f)));
	bForceNextGroundBurstForTest = false;

	LastVolleyRoll = FMath::FRand();
	const bool bVolleyPity = NormalCastsSinceVolley
		>= FMath::Max(1, GetMaxNormalCastsBeforeVolley());
	bActiveCastIsVolley = !bActiveCastIsGroundBurst && !bLastCastWasSpecial
		&& (bVolleyPity || LastVolleyRoll < FMath::Clamp(
			GetVolleyChance() + Enemy->GetEliteSpecialChanceBonus(), 0.f, 1.f));

	if (bActiveCastIsGroundBurst)
	{
		NormalCastsSinceGroundBurst = 0;
		NextGroundBurstReadyTime = Now + GetGroundBurstCooldown();
	}
	else if (bActiveCastIsVolley)
	{
		NormalCastsSinceVolley = 0;
	}
	else
	{
		++NormalCastsSinceVolley;
		++NormalCastsSinceGroundBurst;
	}
	bLastCastWasSpecial = bActiveCastIsVolley || bActiveCastIsGroundBurst;
	FVector TelegraphLocation = Target->GetActorLocation();
	TelegraphLocation.Z -= Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FHitResult GroundHit;
	FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(AuraShamanTelegraphGround), false);
	GroundQuery.AddIgnoredActor(Enemy);
	GroundQuery.AddIgnoredActor(Target);
	const FVector TraceStart = Target->GetActorLocation() + FVector(0.f, 0.f, 180.f);
	const FVector TraceEnd = Target->GetActorLocation() - FVector(0.f, 0.f, 500.f);
	if (GetWorld()->LineTraceSingleByChannel(
		GroundHit, TraceStart, TraceEnd, ECC_Visibility, GroundQuery))
	{
		TelegraphLocation = GroundHit.ImpactPoint + FVector(0.f, 0.f, 3.f);
	}
	LockedGroundBurstLocation = TelegraphLocation;

	const FVector Direction = (LockedAimLocation - Enemy->GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero()) Enemy->SetActorRotation(Direction.Rotation());
	Enemy->BeginRangedCast(
		TelegraphLocation, bActiveCastIsVolley, bActiveCastIsGroundBurst);
	if (CastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(Enemy, CastSound, Enemy->GetActorLocation(), 0.7f);
	}

	const float ActiveWindup = bActiveCastIsGroundBurst
		? GetGroundBurstWindup()
		: GetWindupTime() + (bActiveCastIsVolley ? GetVolleyWindupBonus() : 0.f);
	UE_LOG(LogAuraShamanAbility, Display,
		TEXT("Shaman cast started. Enemy=%s Pattern=%s Sequence=%d VolleyRoll=%.3f GroundRoll=%.3f NormalVolley=%d NormalGround=%d Windup=%.2f Target=%s Cooldown=%.2f"),
		*Enemy->GetName(), bActiveCastIsGroundBurst ? TEXT("GroundBurst")
			: (bActiveCastIsVolley ? TEXT("FanVolley") : TEXT("AimedBolt")),
		CastSequence, LastVolleyRoll, LastGroundBurstRoll,
		NormalCastsSinceVolley, NormalCastsSinceGroundBurst, ActiveWindup,
		*LockedAimLocation.ToCompactString(), GetCooldownDuration());
	UAbilityTask_WaitDelay* WaitForRelease = UAbilityTask_WaitDelay::WaitDelay(
		this, ActiveWindup);
	WaitForRelease->OnFinish.AddDynamic(this, &UAuraShamanBoltAbility::ReleaseProjectile);
	WaitForRelease->ReadyForActivation();
}

void UAuraShamanBoltAbility::ReleaseProjectile()
{
	AAuraEnemy* Enemy = Cast<AAuraEnemy>(GetAvatarActorFromActorInfo());
	if (Enemy) Enemy->EndRangedCast();

	if (Enemy && Enemy->HasAuthority() && !Enemy->IsDead() && !Enemy->IsStunned()
		&& GetWorld())
	{
		if (bActiveCastIsGroundBurst)
		{
			ResolveGroundBurst(*Enemy);
			EndAbility(CurrentSpecHandle, CurrentActorInfo,
				CurrentActivationInfo, true, false);
			return;
		}
		if (!ProjectileClass)
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo,
				CurrentActivationInfo, true, true);
			return;
		}
		const FVector SpawnLocation = Enemy->GetActorLocation()
			+ FVector(0.f, 0.f, 88.f) + Enemy->GetActorForwardVector() * 72.f;
		FVector CenterDirection = (LockedAimLocation - SpawnLocation).GetSafeNormal();
		if (CenterDirection.IsNearlyZero()) CenterDirection = Enemy->GetActorForwardVector();
		Enemy->PlayRangedReleaseEffect(
			SpawnLocation, CenterDirection, bActiveCastIsVolley);

		const int32 ProjectileCount = bActiveCastIsVolley
			? FMath::Max(2, GetVolleyProjectileCount()) : 1;
		const float ProjectileDamage = (bActiveCastIsVolley
			? GetDamage() * GetVolleyDamageMultiplier() : GetDamage())
			* Enemy->GetDifficultyDamageMultiplier();
		for (int32 Index = 0; Index < ProjectileCount; ++Index)
		{
			const float SpreadAlpha = ProjectileCount > 1
				? static_cast<float>(Index) / static_cast<float>(ProjectileCount - 1) - 0.5f
				: 0.f;
			const FVector LaunchDirection = CenterDirection.RotateAngleAxis(
				SpreadAlpha * GetVolleySpreadDegrees(), FVector::UpVector).GetSafeNormal();
			SpawnProjectile(*Enemy, SpawnLocation, LaunchDirection, ProjectileDamage);
		}
		UE_LOG(LogAuraShamanAbility, Display,
			TEXT("Shaman projectiles released. Enemy=%s Pattern=%s Count=%d DamageEach=%.1f Spread=%.1f"),
			*Enemy->GetName(), bActiveCastIsVolley ? TEXT("FanVolley") : TEXT("AimedBolt"),
			ProjectileCount, ProjectileDamage,
			bActiveCastIsVolley ? GetVolleySpreadDegrees() : 0.f);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAuraShamanBoltAbility::ResolveGroundBurst(AAuraEnemy& Enemy) const
{
	UWorld* World = GetWorld();
	UAbilitySystemComponent* SourceAbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (!World || !SourceAbilitySystem) return;

	const float Radius = GetGroundBurstRadius();
	const float Damage = GetGroundBurstDamage() * Enemy.GetDifficultyDamageMultiplier();
	Enemy.PlayShamanGroundBurstEffect(LockedGroundBurstLocation, Radius);
	int32 HitCount = 0;
	for (TActorIterator<AAuraCharacter> It(World); It; ++It)
	{
		AAuraCharacter* Candidate = *It;
		if (!Candidate || Candidate->IsDead()
			|| FMath::Abs(Candidate->GetActorLocation().Z
				- LockedGroundBurstLocation.Z) > 190.f
			|| FVector::DistSquared2D(Candidate->GetActorLocation(),
				LockedGroundBurstLocation) > FMath::Square(Radius))
		{
			continue;
		}
		UAbilitySystemComponent* TargetAbilitySystem = Candidate->GetAbilitySystemComponent();
		if (!TargetAbilitySystem) continue;
		FGameplayEffectContextHandle Context = SourceAbilitySystem->MakeEffectContext();
		Context.AddSourceObject(&Enemy);
		FGameplayEffectSpecHandle DamageSpec = SourceAbilitySystem->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
		if (!DamageSpec.IsValid()) continue;
		DamageSpec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -Damage);
		TargetAbilitySystem->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		++HitCount;
	}
	UE_LOG(LogAuraShamanAbility, Display,
		TEXT("SHAMAN_GROUND_BURST_RELEASED Enemy=%s Location=%s Radius=%.1f Damage=%.1f Targets=%d"),
		*Enemy.GetName(), *LockedGroundBurstLocation.ToCompactString(),
		Radius, Damage, HitCount);
}

void UAuraShamanBoltAbility::SpawnProjectile(AAuraEnemy& Enemy,
	const FVector& SpawnLocation, const FVector& LaunchDirection, const float Damage) const
{
	if (!ProjectileClass || !GetWorld()) return;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = &Enemy;
	SpawnParameters.Instigator = &Enemy;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AAuraFireBoltProjectile* Projectile = GetWorld()->SpawnActor<AAuraFireBoltProjectile>(
		ProjectileClass, SpawnLocation, LaunchDirection.Rotation(), SpawnParameters))
	{
		Projectile->SetDamage(Damage);
	}
}

void UAuraShamanBoltAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (AAuraEnemy* Enemy = ActorInfo
		? Cast<AAuraEnemy>(ActorInfo->AvatarActor.Get()) : nullptr)
	{
		Enemy->EndRangedCast();
		if (bWasCancelled)
		{
			UE_LOG(LogAuraShamanAbility, Display,
				TEXT("Shaman cast interrupted. Enemy=%s"), *Enemy->GetName());
		}
	}
	bActiveCastIsVolley = false;
	bActiveCastIsGroundBurst = false;
	Super::EndAbility(
		Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
