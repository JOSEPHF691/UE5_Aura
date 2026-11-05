// Copyright yosep

#include "AbilitySystem/Abilities/AuraShockAbility.h"
#include "AbilitySystem/AuraElementalSpell.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"

#include "AbilitySystem/AuraAttributeSet.h"
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
#include "Components/SkeletalMeshComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/AuraPlayerController.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UI/AuraHUD.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraShock, Log, All);

UAuraShockAbility::UAuraShockAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	CostGameplayEffectClass = UAuraManaCostEffect::StaticClass();
	CooldownGameplayEffectClass = UAuraShockCooldownEffect::StaticClass();

	static ConstructorHelpers::FObjectFinder<UAnimSequence> CastAnimationAsset(
		TEXT("/Game/Assets/Characters/Aura/Animations/Abilities/Cast_Shock.Cast_Shock"));
	if (CastAnimationAsset.Succeeded()) CastAnimation = CastAnimationAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> BeamEffectAsset(
		TEXT("/Game/Assets/Effects/Shock/NS_ElectricBeam.NS_ElectricBeam"));
	if (BeamEffectAsset.Succeeded()) BeamEffect = BeamEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ImpactEffectAsset(
		TEXT("/Game/Assets/Effects/Shock/NS_Sparks.NS_Sparks"));
	if (ImpactEffectAsset.Succeeded()) ImpactEffect = ImpactEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> CastSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/Shock/sfx_Shock.sfx_Shock"));
	if (CastSoundAsset.Succeeded()) CastSound = CastSoundAsset.Object;

	FGameplayTagContainer Tags;
	Tags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Ability.Shock")));
	SetAssetTags(Tags);
}

float UAuraShockAbility::GetCooldownRemaining() const
{
	return GetCooldownTimeRemaining();
}

float UAuraShockAbility::GetCooldownDuration() const
{
	return UAuraCombatBalanceData::Get().Shock.Cooldown;
}

float UAuraShockAbility::GetManaCost() const
{
	return UAuraCombatBalanceData::Get().Shock.ManaCost;
}

float UAuraShockAbility::GetDamage() const
{
	return UAuraCombatBalanceData::Get().Shock.Damage;
}

float UAuraShockAbility::GetStunDuration() const
{
	const AAuraCharacter* PlayerCharacter = HasAnyFlags(RF_ClassDefaultObject)
		? nullptr : Cast<AAuraCharacter>(GetAvatarActorFromActorInfo());
	return UAuraCombatBalanceData::Get().Elemental.ParalysisDuration + (PlayerCharacter
		? PlayerCharacter->GetShockStunDurationBonus() : 0.f);
}

float UAuraShockAbility::GetCastRange() const
{
	return UAuraCombatBalanceData::Get().Shock.CastRange;
}

float UAuraShockAbility::GetAimAssistRadius() const
{
	return UAuraCombatBalanceData::Get().Shock.AimAssistRadius;
}

float UAuraShockAbility::GetImpactDelay() const
{
	return UAuraCombatBalanceData::Get().Shock.ImpactDelay;
}

float UAuraShockAbility::GetEffectLifetime() const
{
	return UAuraCombatBalanceData::Get().Shock.EffectLifetime;
}

void UAuraShockAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
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
		FVector ConfirmedDirection = FVector::ZeroVector;
		AAuraPlayerController* AuraController =
			Cast<AAuraPlayerController>(Character->GetController());
		const bool bHasConfirmedDirection = AuraController
			&& AuraController->ConsumeShockCastDirection(ConfirmedDirection);
		const AAuraCharacter* PlayerCaster = Cast<AAuraCharacter>(Character);
		const bool bGround = PlayerCaster && PlayerCaster->GetSkillRankState().ShockPath == EAuraShockPath::ThunderJudgment;
		FVector Direction = bHasConfirmedDirection
			? ConfirmedDirection.GetSafeNormal2D()
			: Character->GetActorForwardVector().GetSafeNormal2D();
		if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector;

		const FVector TargetEndpoint = bGround && bHasConfirmedDirection ? ConfirmedDirection
			: Character->GetActorLocation() + Direction * GetCastRange();
		FHitResult TargetHit;
		TargetHit.Location = TargetEndpoint;
		TargetHit.ImpactPoint = TargetEndpoint;
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
			this, &UAuraShockAbility::HandleTargetData);
	AbilitySystem->CallReplicatedTargetDataDelegatesIfSet(
		Handle, ActivationInfo.GetActivationPredictionKey());
}

void UAuraShockAbility::HandleTargetData(
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

	LockedCastDirection =
		(TargetHit->ImpactPoint - Character->GetActorLocation()).GetSafeNormal2D();
	const FVector Delta = TargetHit->ImpactPoint - Character->GetActorLocation();
	if (Delta.SizeSquared2D() > FMath::Square(GetCastRange() + 10.f) || FMath::Abs(Delta.Z) > 230.f)
	{ EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	LockedGroundLocation = TargetHit->ImpactPoint;
	const AAuraCharacter* PlayerCaster = Cast<AAuraCharacter>(Character);
	const bool bGround = PlayerCaster && PlayerCaster->GetSkillRankState().ShockPath == EAuraShockPath::ThunderJudgment;
	if (bGround)
	{
		FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(JudgmentTarget), false, Character);
		FHitResult Floor, Wall;
		const FVector Point = TargetHit->ImpactPoint;
		if (!GetWorld()->LineTraceSingleByObjectType(Floor, Point + FVector(0,0,180), Point - FVector(0,0,180), Objects, Query)
			|| Floor.ImpactNormal.Z < 0.5f || FMath::Abs(Floor.ImpactPoint.Z - Point.Z) > 60.f
			|| GetWorld()->LineTraceSingleByObjectType(Wall, Character->GetActorLocation(), Floor.ImpactPoint + FVector(0,0,45), Objects, Query))
		{ EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
		LockedGroundLocation = Floor.ImpactPoint + FVector(0,0,5);
		if (LockedCastDirection.IsNearlyZero()) LockedCastDirection = Character->GetActorForwardVector();
	}
	if (LockedCastDirection.IsNearlyZero())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	Character->NotifyCombatActivity();
	if (Character->HasAuthority()) Character->SetActorRotation(LockedCastDirection.Rotation());

	if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled())
	{
		PlayCastPresentation(Character);
	}
	if (Character->HasAuthority())
	{
		if (AAuraCharacter* AuraCharacter = Cast<AAuraCharacter>(Character))
		{
			AuraCharacter->MulticastShockCast();
		}
	}

	UE_LOG(LogAuraShock, Display,
		TEXT("Shock cast started. CharacterYaw=%.1f Direction=%s Range=%.1f ReplicatedTarget=true"),
		Character->GetActorRotation().Yaw, *LockedCastDirection.ToCompactString(), GetCastRange());

	UAbilityTask_WaitDelay* WaitForImpact = UAbilityTask_WaitDelay::WaitDelay(
		this, GetImpactDelay());
	WaitForImpact->OnFinish.AddDynamic(this, &UAuraShockAbility::ImpactShockAndFinish);
	WaitForImpact->ReadyForActivation();
}

void UAuraShockAbility::ImpactShockAndFinish()
{
 AAuraCharacter* Caster = Cast<AAuraCharacter>(GetAvatarActorFromActorInfo());
 if (!Caster || !GetWorld() || Caster->IsDead()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
 const auto Path = Caster->GetSkillRankState().ShockPath;
 const bool bEvolved = Caster->GetSkillRank(EAuraPlayerSkill::Shock) >= 3;
 const auto& B = UAuraCombatBalanceData::Get().Elemental;
 if (Caster->HasAuthority())
 {
  const float Damage = GetDamage() * Caster->GetShockDamageMultiplier();
  const int32 CastId = Caster->AllocateComboCastId();
  if (bEvolved && Path == EAuraShockPath::ThunderJudgment)
  {
   const FTransform Transform(FRotator::ZeroRotator, LockedGroundLocation);
   AAuraElementalSpell* Spell = GetWorld()->SpawnActorDeferred<AAuraElementalSpell>(
    AAuraElementalSpell::StaticClass(), Transform, Caster, Caster, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if (Spell) { Spell->Configure(Caster, EAuraElementalZone::Judgment, Damage, B.JudgmentRadius, CastId); UGameplayStatics::FinishSpawningActor(Spell, Transform); }
  }
  else if (bEvolved && Path == EAuraShockPath::StormField)
  {
   for (AAuraEnemy* Target : UAuraElementalStatusComponent::GatherTargets(GetWorld(), Caster->GetActorLocation(), GetCastRange()))
   {
    if (FVector::DotProduct(LockedCastDirection, (Target->GetActorLocation() - Caster->GetActorLocation()).GetSafeNormal2D())
     >= FMath::Cos(FMath::DegreesToRadians(B.StormHalfAngle)))
     AAuraElementalSpell::ApplyLightning(Caster, Target, Damage * 0.75f, CastId);
   }
   const FTransform Transform(LockedCastDirection.Rotation(), Caster->GetActorLocation());
   AAuraElementalSpell* Spell = GetWorld()->SpawnActorDeferred<AAuraElementalSpell>(
    AAuraElementalSpell::StaticClass(), Transform, Caster, Caster, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if (Spell) { Spell->Configure(Caster, EAuraElementalZone::Storm, Damage, GetCastRange(), CastId); UGameplayStatics::FinishSpawningActor(Spell, Transform); }
  }
  else
  {
   USkeletalMeshComponent* WeaponMesh = Caster->GetWeaponMesh();
   const auto Bounds = WeaponMesh->GetLocalBounds();
   const FVector Start = WeaponMesh->GetComponentTransform().TransformPosition(Bounds.Origin + FVector(0.f,0.f,Bounds.BoxExtent.Z));
   const FVector End = Start + LockedCastDirection * GetCastRange();
   AAuraEnemy* Target = FindFirstEnemyInFacingDirection(Start, End);
   if (Target) AAuraElementalSpell::ApplyLightning(Caster, Target, Damage, CastId);
   Caster->MulticastShockImpact(Start, End, Target != nullptr, Target ? Target->GetActorLocation() : FVector::ZeroVector);
   if (Caster->IsLocallyControlled()) PlayImpactPresentation(Caster, Start, End, Target != nullptr, Target ? Target->GetActorLocation() : FVector::ZeroVector);
  }
 }
 EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, Caster->HasAuthority(), false);
}

AAuraEnemy* UAuraShockAbility::FindFirstEnemyInFacingDirection(
	const FVector& TraceStart, const FVector& TraceEnd) const
{
	UWorld* World = GetWorld();
	const AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!World || !AvatarActor) return nullptr;

	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraShockSweep), false, AvatarActor);
	World->SweepMultiByObjectType(Hits, TraceStart, TraceEnd, FQuat::Identity, ObjectQuery,
		FCollisionShape::MakeSphere(GetAimAssistRadius()), QueryParams);

	AAuraEnemy* NearestEnemy = nullptr;
	float NearestForwardDistance = TNumericLimits<float>::Max();
	for (const FHitResult& Hit : Hits)
	{
		AAuraEnemy* Enemy = Cast<AAuraEnemy>(Hit.GetActor());
		if (!Enemy || Enemy->IsDead()) continue;

		const float ForwardDistance = FVector::DotProduct(
			Enemy->GetActorLocation() - TraceStart, LockedCastDirection);
		if (ForwardDistance >= 0.f && ForwardDistance <= GetCastRange()
			&& ForwardDistance < NearestForwardDistance)
		{
			NearestEnemy = Enemy;
			NearestForwardDistance = ForwardDistance;
		}
	}
	return NearestEnemy;
}

void UAuraShockAbility::PlayCastPresentation(AAuraCharacterBase* Character) const
{
	if (!Character || Character->GetNetMode() == NM_DedicatedServer) return;
	if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance();
		AnimInstance && CastAnimation)
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(
			CastAnimation, TEXT("DefaultSlot"), 0.04f, 0.12f, 0.78f, 1, -1.f, 0.f);
	}
	if (CastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			Character, CastSound, Character->GetActorLocation());
	}
}

void UAuraShockAbility::PlayImpactPresentation(
	UObject* WorldContextObject, const FVector& BeamStart, const FVector& BeamEnd,
	const bool bHasImpact, const FVector& ImpactLocation) const
{
	const AActor* ContextActor = Cast<AActor>(WorldContextObject);
	if (!WorldContextObject
		|| (ContextActor && ContextActor->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}
	if (BeamEffect)
	{
		if (UNiagaraComponent* BeamComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			WorldContextObject, BeamEffect, BeamStart,
			FRotator::ZeroRotator, FVector(1.f), false, false))
		{
			// NS_ElectricBeam has a mixed legacy coordinate contract: Beam Start is
			// authored as Local Space and converted to Simulation Space by the Beam
			// Emitter Setup module, while Beam End is consumed directly in Simulation
			// (world) space. Passing both values in either space double-transforms one
			// endpoint. Keep the component at the muzzle, provide a local start and an
			// absolute world end.
			const FTransform& BeamTransform = BeamComponent->GetComponentTransform();
			const FVector LocalBeamStart = BeamTransform.InverseTransformPosition(BeamStart);
			BeamComponent->SetVariableVec3(TEXT("User.Beam Start"), LocalBeamStart);
			BeamComponent->SetVariableVec3(TEXT("User.Beam End"), BeamEnd);
			BeamComponent->Activate(true);
			const FVector ResolvedStart = BeamTransform.TransformPosition(LocalBeamStart);
			const FVector ResolvedDirection = (BeamEnd - ResolvedStart).GetSafeNormal2D();
			UE_LOG(LogAuraShock, Display,
				TEXT("Shock beam component spawned. ComponentLocation=%s LocalStart=%s WorldEnd=%s ResolvedDirection=%s ResolvedDirectionDot=%.4f"),
				*BeamComponent->GetComponentLocation().ToCompactString(),
				*LocalBeamStart.ToCompactString(), *BeamEnd.ToCompactString(),
				*ResolvedDirection.ToCompactString(),
				FVector::DotProduct(
					(BeamEnd - BeamStart).GetSafeNormal2D(), ResolvedDirection));
			ScheduleEffectCleanup(BeamComponent);
		}
	}
	if (bHasImpact && ImpactEffect)
	{
		UNiagaraComponent* ImpactComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			WorldContextObject, ImpactEffect, ImpactLocation,
			FRotator::ZeroRotator, FVector(1.f), false, true);
		ScheduleEffectCleanup(ImpactComponent);
	}
}

void UAuraShockAbility::ScheduleEffectCleanup(UNiagaraComponent* EffectComponent) const
{
	if (!EffectComponent || !EffectComponent->GetWorld()) return;

	const TWeakObjectPtr<UNiagaraComponent> WeakEffect(EffectComponent);
	FTimerHandle CleanupTimer;
	EffectComponent->GetWorld()->GetTimerManager().SetTimer(CleanupTimer, [WeakEffect]()
	{
		if (!WeakEffect.IsValid()) return;
		const FString EffectName = GetNameSafe(WeakEffect.Get());
		WeakEffect->DeactivateImmediate();
		WeakEffect->DestroyComponent();
		UE_LOG(LogAuraShock, Display, TEXT("Shock Niagara cleanup completed. Component=%s"), *EffectName);
	}, GetEffectLifetime(), false);
}
