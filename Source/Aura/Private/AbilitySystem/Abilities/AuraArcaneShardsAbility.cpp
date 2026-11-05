// Copyright yosep

#include "AbilitySystem/Abilities/AuraArcaneShardsAbility.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/AuraArcaneSlowField.h"
#include "AbilitySystem/AuraElementalSpell.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Character/AuraCharacterBase.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/AuraPlayerController.h"
#include "Sound/SoundBase.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraArcaneShards, Log, All);

UAuraArcaneShardsAbility::UAuraArcaneShardsAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	CostGameplayEffectClass = UAuraManaCostEffect::StaticClass();
	CooldownGameplayEffectClass = UAuraArcaneShardsCooldownEffect::StaticClass();

	static ConstructorHelpers::FObjectFinder<UAnimSequence> CastAnimationAsset(
		TEXT("/Game/Assets/Characters/Aura/Animations/Abilities/Cast_ArcaneShards_Short.Cast_ArcaneShards_Short"));
	if (CastAnimationAsset.Succeeded()) CastAnimation = CastAnimationAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ShardsEffectAsset(
		TEXT("/Game/Assets/Effects/ArcaneShards/NS_ArcaneShards.NS_ArcaneShards"));
	if (ShardsEffectAsset.Succeeded()) ShardsEffect = ShardsEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> CastSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/ArcaneShards/sfx_ArcaneShards.sfx_ArcaneShards"));
	if (CastSoundAsset.Succeeded()) CastSound = CastSoundAsset.Object;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.ArcaneShards")));
	SetAssetTags(Tags);
}

float UAuraArcaneShardsAbility::GetCooldownRemaining() const
{
	return GetCooldownTimeRemaining();
}

float UAuraArcaneShardsAbility::GetCooldownDuration() const
{
	return UAuraCombatBalanceData::Get().ArcaneShards.Cooldown;
}

float UAuraArcaneShardsAbility::GetManaCost() const
{
	return UAuraCombatBalanceData::Get().ArcaneShards.ManaCost;
}

float UAuraArcaneShardsAbility::GetDamage() const
{
	return UAuraCombatBalanceData::Get().ArcaneShards.Damage;
}

float UAuraArcaneShardsAbility::GetEffectRadius() const
{
	const AAuraCharacter* PlayerCharacter = HasAnyFlags(RF_ClassDefaultObject)
		? nullptr : Cast<AAuraCharacter>(GetAvatarActorFromActorInfo());
	return UAuraCombatBalanceData::Get().ArcaneShards.EffectRadius * (PlayerCharacter
		? PlayerCharacter->GetArcaneShardsRadiusMultiplier() : 1.f);
}

float UAuraArcaneShardsAbility::GetCastRange() const
{
	return UAuraCombatBalanceData::Get().ArcaneShards.CastRange;
}

float UAuraArcaneShardsAbility::GetCastDelay() const
{
	return UAuraCombatBalanceData::Get().ArcaneShards.CastDelay;
}

void UAuraArcaneShardsAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
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
		FVector RequestedLocation = FVector::ZeroVector;
		AAuraPlayerController* AuraController =
			Cast<AAuraPlayerController>(Character->GetController());
		const bool bHasGroundTarget = AuraController
			&& AuraController->ConsumeArcaneShardsCastTarget(RequestedLocation);
		if (!bHasGroundTarget)
		{
			FVector CastDirection = Character->GetActorForwardVector().GetSafeNormal2D();
			if (CastDirection.IsNearlyZero()) CastDirection = FVector::ForwardVector;
			RequestedLocation = Character->GetActorLocation() + CastDirection
				* FMath::Min(450.f, GetCastRange());
		}

		FHitResult TargetHit;
		TargetHit.Location = RequestedLocation;
		TargetHit.ImpactPoint = RequestedLocation;
		TargetHit.bBlockingHit = true;
		FGameplayAbilityTargetDataHandle TargetData(
			new FGameplayAbilityTargetData_SingleTargetHit(TargetHit));

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
			this, &UAuraArcaneShardsAbility::HandleTargetData);
	AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
		Handle, ActivationInfo.GetActivationPredictionKey());
}

void UAuraArcaneShardsAbility::HandleTargetData(
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

	const FGameplayAbilityTargetData* TargetDataEntry = TargetData.Get(0);
	const FHitResult* TargetHit = TargetDataEntry ? TargetDataEntry->GetHitResult() : nullptr;
	if (!TargetHit || TargetHit->ImpactPoint.ContainsNaN())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	const FVector RequestedLocation = TargetHit->ImpactPoint;
	FVector PlanarOffset = RequestedLocation - Character->GetActorLocation();
	PlanarOffset.Z = 0.f;
	if (PlanarOffset.SizeSquared() > FMath::Square(GetCastRange()))
	{
		PlanarOffset = PlanarOffset.GetSafeNormal() * GetCastRange();
	}
	LockedImpactLocation = Character->GetActorLocation() + PlanarOffset;
	LockedImpactLocation.Z = RequestedLocation.Z;

	FHitResult GroundHit;
	FCollisionObjectQueryParams GroundObjectQuery;
	GroundObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(AuraArcaneShardsGround), false, Character);
	const FVector TraceStart = LockedImpactLocation + FVector(0.f, 0.f, 500.f);
	const FVector TraceEnd = LockedImpactLocation - FVector(0.f, 0.f, 1000.f);
	if (GetWorld()->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, GroundObjectQuery, GroundQuery))
	{
		LockedImpactLocation = GroundHit.ImpactPoint;
	}
	LockedImpactLocation.Z += 3.f;

	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	Character->NotifyCombatActivity();

	if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
	{
		PlayCastPresentation(Character);
	}
	if (Character->HasAuthority())
	{
		if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(Character))
		{
			AuraCharacter->MulticastArcaneShardsCast();
		}
	}

	UE_LOG(LogAuraArcaneShards, Display,
		TEXT("Arcane Shards cast started. CharacterYaw=%.1f ReplicatedTarget=true Impact=%s Radius=%.1f Range=%.1f"),
		Character->GetActorRotation().Yaw,
		*LockedImpactLocation.ToCompactString(), GetEffectRadius(), GetCastRange());

	UAbilityTask_WaitDelay* WaitForEruption = UAbilityTask_WaitDelay::WaitDelay(
		this, GetCastDelay());
	WaitForEruption->OnFinish.AddDynamic(this, &UAuraArcaneShardsAbility::EruptShardsAndFinish);
	WaitForEruption->ReadyForActivation();
}

void UAuraArcaneShardsAbility::EruptShardsAndFinish()
{
 AAuraCharacter* Caster = Cast<AAuraCharacter>(GetAvatarActorFromActorInfo());
 if (!Caster || !GetWorld() || Caster->IsDead()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
 if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled()) PlayImpactPresentation(Caster, LockedImpactLocation);
 if (Caster->HasAuthority())
 {
  Caster->MulticastArcaneShardsImpact(LockedImpactLocation);
  const float Damage = GetDamage() * Caster->GetArcaneShardsDamageMultiplier();
  const auto Path = Caster->GetSkillRankState().ArcanePath;
  const bool bEvolved = Caster->GetSkillRank(EAuraPlayerSkill::ArcaneShards) >= 3;
  AAuraElementalSpell::ApplyQ(Caster, LockedImpactLocation, Damage, GetEffectRadius(),
   bEvolved && Path == EAuraArcanePath::CrystalPrison);
  if (bEvolved && Path == EAuraArcanePath::CrystalEcho)
  {
   const FTransform Transform(FRotator::ZeroRotator, LockedImpactLocation);
   AAuraElementalSpell* Spell = GetWorld()->SpawnActorDeferred<AAuraElementalSpell>(
    AAuraElementalSpell::StaticClass(), Transform, Caster, Caster, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if (Spell) { Spell->Configure(Caster, EAuraElementalZone::Echo, Damage, GetEffectRadius(), Caster->AllocateComboCastId()); UGameplayStatics::FinishSpawningActor(Spell, Transform); }
  }
 }
 EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, Caster->HasAuthority(), false);
}

void UAuraArcaneShardsAbility::PlayCastPresentation(AAuraCharacterBase* Character) const
{
	if (!Character || Character->GetNetMode() == NM_DedicatedServer) return;
	if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
		AnimInstance && CastAnimation)
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(
			CastAnimation, TEXT("DefaultSlot"), 0.08f, 0.18f, 1.f, 1, -1.f, 0.f);
	}
}

void UAuraArcaneShardsAbility::PlayImpactPresentation(
	UObject* WorldContextObject, const FVector& ImpactLocation) const
{
	const AActor* ContextActor = Cast<AActor>(WorldContextObject);
	if (!WorldContextObject
		|| (ContextActor && ContextActor->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}
	if (ShardsEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			WorldContextObject, ShardsEffect, ImpactLocation);
	}
	if (APlayerController* LocalController = UGameplayStatics::GetPlayerController(
		WorldContextObject, 0))
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(LocalController->GetHUD()))
		{
			AuraHUD->ShowArcaneShardsTextureImpact(
				ImpactLocation, GetEffectRadius());
		}
	}
	if (CastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			WorldContextObject, CastSound, ImpactLocation);
	}
}
