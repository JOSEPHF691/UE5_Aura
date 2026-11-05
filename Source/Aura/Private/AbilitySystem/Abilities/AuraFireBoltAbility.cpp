// Copyright yosep

#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Character/AuraCharacterBase.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraAbility, Log, All);

namespace
{
	constexpr float FireBoltLockRange = 1500.f;
	constexpr float TripleOrbDamageScale = 0.6f;
	constexpr float TripleOrbPoiseScale = 0.35f;
}

bool UAuraFireBoltAbility::CanLockTarget(
	const AAuraCharacterBase* Character, const AAuraEnemy* Target)
{
	if (!Character || !Character->GetWorld() || !IsValid(Target)
		|| Target->IsDead() || Target->IsDamageImmune()) return false;
	const FVector Source = Character->GetActorLocation() + FVector(0.f, 0.f, 70.f);
	const FVector Destination = Target->GetActorLocation();
	if (FVector::DistSquared2D(Source, Destination) > FMath::Square(FireBoltLockRange))
		return false;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(AuraFireBoltLock), false, Character);
	Query.AddIgnoredActor(Target);
	FHitResult WallHit;
	return !Character->GetWorld()->LineTraceSingleByObjectType(
		WallHit, Source, Destination, Objects, Query);
}

UAuraFireBoltAbility::UAuraFireBoltAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ProjectileClass = AAuraFireBoltProjectile::StaticClass();
	CostGameplayEffectClass = UAuraManaCostEffect::StaticClass();
	CooldownGameplayEffectClass = UAuraFireBoltCooldownEffect::StaticClass();

	static ConstructorHelpers::FObjectFinder<UAnimSequence> CastAnimationAsset(
		TEXT("/Game/Assets/Characters/Aura/Animations/Abilities/Cast_FireBolt.Cast_FireBolt"));
	if (CastAnimationAsset.Succeeded()) CastAnimation = CastAnimationAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> CastSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireBolt/sfx_FireBolt.sfx_FireBolt"));
	if (CastSoundAsset.Succeeded()) CastSound = CastSoundAsset.Object;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.FireBolt")));
	SetAssetTags(Tags);
}

float UAuraFireBoltAbility::GetCooldownRemaining() const
{
	return GetCooldownTimeRemaining();
}

float UAuraFireBoltAbility::GetCooldownDuration() const
{
	return UAuraCombatBalanceData::Get().FireBolt.Cooldown;
}

float UAuraFireBoltAbility::GetManaCost() const
{
	return UAuraCombatBalanceData::Get().FireBolt.ManaCost;
}

float UAuraFireBoltAbility::GetDamage() const
{
	return UAuraCombatBalanceData::Get().FireBolt.Damage;
}

float UAuraFireBoltAbility::GetProjectileDelay() const
{
	return UAuraCombatBalanceData::Get().FireBolt.ReleaseDelay;
}

void UAuraFireBoltAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AAuraCharacterBase* Character = Cast<AAuraCharacterBase>(GetAvatarActorFromActorInfo());
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	if (!Character || !AbilitySystem || !GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		FGameplayAbilityTargetData_ActorArray* ActorData =
			new FGameplayAbilityTargetData_ActorArray();
		if (AAuraEnemy* Target = FindClientTarget(Character))
			ActorData->TargetActorArray.Add(Target);
		FGameplayAbilityTargetDataHandle TargetData(ActorData);
		FScopedPredictionWindow PredictionWindow(AbilitySystem, IsPredictingClient());
		if (IsPredictingClient())
		{
			AbilitySystem->CallServerSetReplicatedTargetData(
				Handle, ActivationInfo.GetActivationPredictionKey(), TargetData,
				FGameplayTag(), AbilitySystem->ScopedPredictionKey);
		}
		HandleTargetData(TargetData, FGameplayTag());
		return;
	}
	AbilitySystem->AbilityTargetDataSetDelegate(
		Handle, ActivationInfo.GetActivationPredictionKey()).AddUObject(
			this, &UAuraFireBoltAbility::HandleTargetData);
	AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
		Handle, ActivationInfo.GetActivationPredictionKey());
}

AAuraEnemy* UAuraFireBoltAbility::FindClientTarget(AAuraCharacterBase* Character) const
{
	if (!Character) return nullptr;
	AAuraPlayerController* Controller =
		Cast<AAuraPlayerController>(Character->GetController());
	// A miss outside the half-body margin stays a straight, forward shot.
	return Controller ? Controller->ConsumeFireBoltClickTarget() : nullptr;
}

void UAuraFireBoltAbility::HandleTargetData(
	const FGameplayAbilityTargetDataHandle& TargetData, FGameplayTag ActivationTag)
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponentFromActorInfo();
	AAuraCharacterBase* Character = Cast<AAuraCharacterBase>(GetAvatarActorFromActorInfo());
	if (!AbilitySystem || !Character || !GetWorld())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	AbilitySystem->AbilityTargetDataSetDelegate(
		CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey()).RemoveAll(this);
	if (CurrentActorInfo && !CurrentActorInfo->IsLocallyControlled())
	{
		AbilitySystem->ConsumeClientReplicatedTargetData(
			CurrentSpecHandle, CurrentActivationInfo.GetActivationPredictionKey());
	}
	LockedTarget.Reset();
	bHadLockedTarget = false;
	if (const FGameplayAbilityTargetData* Entry = TargetData.Get(0))
	{
		const TArray<TWeakObjectPtr<AActor>> Actors = Entry->GetActors();
		if (Actors.Num() == 1)
		{
			AAuraEnemy* Requested = Cast<AAuraEnemy>(Actors[0].Get());
			if (CanLockTarget(Character, Requested)) LockedTarget = Requested;
		}
	}
	LockedCastDirection = Character->GetActorForwardVector().GetSafeNormal2D();
	if (LockedCastDirection.IsNearlyZero()) LockedCastDirection = FVector::ForwardVector;
	bHadLockedTarget = LockedTarget.IsValid();
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	Character->NotifyCombatActivity();
	if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
		PlayCastPresentation(Character);
	if (Character->HasAuthority())
	{
		if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(Character))
			AuraCharacter->MulticastFireBoltCast();
	}
	float TurnSeconds = 0.f;
	if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(Character);
		AuraCharacter && LockedTarget.IsValid())
	{
		TurnSeconds = AuraCharacter->BeginFireBoltTurnTo(LockedTarget.Get());
	}

	UAbilityTask_WaitDelay* WaitForRelease = UAbilityTask_WaitDelay::WaitDelay(
		this, FMath::Max(GetProjectileDelay(), TurnSeconds + 0.04f));
	WaitForRelease->OnFinish.AddDynamic(this, &UAuraFireBoltAbility::SpawnProjectileAndFinish);
	WaitForRelease->ReadyForActivation();
}

void UAuraFireBoltAbility::SpawnProjectileAndFinish()
{
	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	AAuraCharacterBase* Character = Cast<AAuraCharacterBase>(AvatarActor);
	AAuraEnemy* Target = LockedTarget.Get();
	if (!CanLockTarget(Character, Target)) Target = nullptr;
	AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(Character);
	if (PlayerCharacter)
	{
		if (Target) PlayerCharacter->AlignFireBoltTurnToTarget(Target);
		else PlayerCharacter->EndFireBoltTurn();
	}

	if (Character && AvatarActor->HasAuthority() && ProjectileClass)
	{
		USkeletalMeshComponent* WeaponMesh = Character->GetWeaponMesh();
		const FVector SpawnLocation = WeaponMesh
			? WeaponMesh->GetComponentLocation() + WeaponMesh->GetUpVector() * 68.f
			: Character->GetActorLocation() + FVector(0.f, 0.f, 80.f);

		const FVector FallbackDirection = bHadLockedTarget
			? Character->GetActorForwardVector().GetSafeNormal2D()
			: LockedCastDirection;
		const FVector ShotDirection = Target
			? (Target->GetActorLocation() - SpawnLocation).GetSafeNormal()
			: (FallbackDirection.IsNearlyZero() ? LockedCastDirection : FallbackDirection);
		const float ScaledDamage = GetDamage() * (PlayerCharacter
			? PlayerCharacter->GetFireBoltDamageMultiplier() : 1.f);
		const FAuraSkillRankState* Ranks = PlayerCharacter
			? &PlayerCharacter->GetSkillRankState() : nullptr;
		const bool bPierce = Ranks && Ranks->FireBolt >= 3
			&& Ranks->FireBoltPath == EAuraFireBoltPath::Pierce;
		AAuraPlayerState* State = PlayerCharacter
			? PlayerCharacter->GetPlayerState<AAuraPlayerState>() : nullptr;
		const bool bVolley = State && State->AdvanceFireBoltCast();
		const int32 CastId = PlayerCharacter ? PlayerCharacter->AllocateComboCastId() : 0;
		const int32 ShotCount = bVolley ? 3 : 1;
		for (int32 Index = 0; Index < ShotCount; ++Index)
		{
			const FVector OrbOffset = bVolley
				? FVector(0.f, (Index - 1) * 85.f, 75.f)
				: FVector::ZeroVector;
			const FVector ShotOrigin = SpawnLocation
				+ Character->GetActorRotation().RotateVector(OrbOffset);
			const FTransform ShotTransform(ShotDirection.Rotation(), ShotOrigin);
			AAuraFireBoltProjectile* Projectile = GetWorld()->SpawnActorDeferred<AAuraFireBoltProjectile>(
				ProjectileClass, ShotTransform, Character, Character,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Projectile) continue;
			Projectile->SetDamage(ScaledDamage * (bVolley ? TripleOrbDamageScale : 1.f));
			Projectile->SetPoiseDamageScale(bVolley ? TripleOrbPoiseScale : 1.f);
			Projectile->SetComboCastId(CastId);
			if (bPierce)
			{
				Projectile->SetExtraPierces(
					UAuraCombatBalanceData::Get().SkillRanks.FireBoltExtraPierces);
			}
			if (Ranks && Ranks->FireBolt >= 2 && Target)
				Projectile->SetHomingTarget(Target);
			if (bVolley)
				Projectile->ConfigureOrbitalRelease(Index, 0.18f * (Index + 1),
					Target, ShotDirection);
			Projectile->FinishSpawning(ShotTransform);
		}
		UE_LOG(LogAuraAbility, Display,
			TEXT("FireBolt release. Target=%s Path=%d Volley=%s Shots=%d DamageEach=%.1f"),
			*GetNameSafe(Target), Ranks ? static_cast<int32>(Ranks->FireBoltPath) : 0,
			bVolley ? TEXT("true") : TEXT("false"), ShotCount,
			ScaledDamage * (bVolley ? TripleOrbDamageScale : 1.f));
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		AvatarActor && AvatarActor->HasAuthority(), false);
}

void UAuraFireBoltAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	if (AAuraCharacter* AuraCharacter = ActorInfo
		? Cast<AAuraCharacter>(ActorInfo->AvatarActor.Get()) : nullptr)
	{
		AuraCharacter->EndFireBoltTurn();
	}
	LockedTarget.Reset();
	bHadLockedTarget = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo,
		bReplicateEndAbility, bWasCancelled);
}

void UAuraFireBoltAbility::PlayCastPresentation(AAuraCharacterBase* Character) const
{
	if (!Character || Character->GetNetMode() == NM_DedicatedServer) return;
	if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
		AnimInstance && CastAnimation)
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(
			CastAnimation, TEXT("DefaultSlot"), 0.08f, 0.18f, 1.f, 1, -1.f, 0.f);
	}
	if (CastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			Character, CastSound, Character->GetActorLocation());
	}
}
