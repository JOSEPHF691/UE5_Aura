// Copyright yosep


#include "Character/AuraCharacter.h"

#include "Animation/AuraAnimInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Abilities/AuraArcaneShardsAbility.h"
#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"
#include "AbilitySystem/Abilities/AuraShockAbility.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "Camera/CameraComponent.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Coop/AuraReviveComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/OverlapResult.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "Kismet/GameplayStatics.h"
#include "Game/AuraGameMode.h"
#include "Game/AuraRunSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "UI/AuraHUD.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AAuraCharacter::AAuraCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	ReviveComponent = CreateDefaultSubobject<UAuraReviveComponent>(TEXT("ReviveComponent"));
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 720.f, 0.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = 500.f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 10.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	TopDownCamera.TargetArmLength = 1300.f;
	TopDownCamera.SocketOffset = FVector(0.f, 0.f, 120.f);
	TopDownCamera.RelativeRotation = FRotator(-62.f, 0.f, 0.f);
	TopDownCamera.bUsePawnControlRotation = true;
	TopDownCamera.bDoCollisionTest = false;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> EmberEffectAsset(
		TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
	if (EmberEffectAsset.Succeeded()) EmberBurstEffect = EmberEffectAsset.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LevelUpEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_LevelUp.NS_Free_Spells_LevelUp"));
	if (LevelUpEffectAsset.Succeeded()) LevelUpEffect = LevelUpEffectAsset.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> HealthRestoreEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Healing.NS_Free_Spells_Buff_Healing"));
	if (HealthRestoreEffectAsset.Succeeded()) HealthRestoreEffect = HealthRestoreEffectAsset.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ManaRestoreEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Mana.NS_Free_Spells_Buff_Mana"));
	if (ManaRestoreEffectAsset.Succeeded()) ManaRestoreEffect = ManaRestoreEffectAsset.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> EmberSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/FireBolt/sfx_FireBolt_Impact.sfx_FireBolt_Impact"));
	if (EmberSoundAsset.Succeeded()) EmberBurstSound = EmberSoundAsset.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> ChainSoundAsset(
		TEXT("/Game/Assets/Sounds/Abilities/Shock/sfx_Shock.sfx_Shock"));
	if (ChainSoundAsset.Succeeded()) FractureChainSound = ChainSoundAsset.Object;
}

void AAuraCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraCharacter, ProgressionState);
	DOREPLIFETIME(AAuraCharacter, RunUpgradeState);
	DOREPLIFETIME(AAuraCharacter, bInCombat);
	DOREPLIFETIME(AAuraCharacter, bDead);
	DOREPLIFETIME_CONDITION(AAuraCharacter, ComboMarks, COND_OwnerOnly);
	DOREPLIFETIME(AAuraCharacter, SkillRanks);
	DOREPLIFETIME(AAuraCharacter, OverloadBuffUntil);
	DOREPLIFETIME_CONDITION(AAuraCharacter, SpectatorViewRotation, COND_SkipOwner);
}

void AAuraCharacter::BeginPlay()
{
	Super::BeginPlay();
	BaseWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	InitialMeshRelativeTransform = GetMesh()->GetRelativeTransform();
	if (HasAuthority())
	{
		ApplyPlayerCombatBalance();
		FTransform CheckpointTransform;
		int32 SavedLevel = 1;
		int32 SavedExperience = 0;
		FAuraRunUpgradeState SavedRunUpgrades;
		if (GetNetMode() == NM_Standalone)
		{
			if (const UAuraRunSubsystem* RunSubsystem =
				GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
				RunSubsystem && RunSubsystem->TryGetCheckpointForWorld(
				GetWorld(), CheckpointTransform, &SavedLevel, &SavedExperience,
				&SavedRunUpgrades))
			{
				RestoreRunUpgrades(SavedRunUpgrades);
				const bool bTeleported = TeleportTo(
					CheckpointTransform.GetLocation(), CheckpointTransform.Rotator(), false, true);
				const bool bAtCheckpoint = bTeleported && FVector::DistSquared(
					GetActorLocation(), CheckpointTransform.GetLocation()) < FMath::Square(5.f);
				UE_LOG(LogTemp, Display,
					TEXT("Checkpoint reload restore completed. Passed=%s Id=%s Location=%s PlayerLevel=%d XP=%d Boons=%d"),
					bAtCheckpoint ? TEXT("true") : TEXT("false"),
					*RunSubsystem->GetActiveCheckpointId().ToString(),
					*CheckpointTransform.GetLocation().ToCompactString(),
					ProgressionState.Level, ProgressionState.Experience,
					RunUpgradeState.GetTotalRanks());
			}
		}
	}
	if (MovementBlendSpace)
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		GetMesh()->SetAnimInstanceClass(UAuraAnimInstance::StaticClass());
		if (UAuraAnimInstance* AuraAnimInstance = Cast<UAuraAnimInstance>(GetMesh()->GetAnimInstance()))
		{
			AuraAnimInstance->SetMovementBlendSpace(MovementBlendSpace);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Aura character has no MovementBlendSpace assigned."));
	}
	if (HasAuthority() && AbilitySystemComponent)
	{
		RefreshGrantedSkillAbility(EAuraPlayerSkill::FireBolt);
		RefreshGrantedSkillAbility(EAuraPlayerSkill::ArcaneShards);
		RefreshGrantedSkillAbility(EAuraPlayerSkill::Shock);
		SetCombatState(false, true);
	}
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UAuraAttributeSet::GetHealthAttribute())
			.AddUObject(this, &AAuraCharacter::OnHealthChanged);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UAuraAttributeSet::GetMoveSpeedAttribute())
			.AddUObject(this, &AAuraCharacter::OnMoveSpeedChanged);
		AbilitySystemComponent->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(TEXT("State.SporeExposed")),
			EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &AAuraCharacter::OnSporeExposureChanged);
		ApplyMovementSpeedBonus(AbilitySystemComponent->GetNumericAttribute(
			UAuraAttributeSet::GetMoveSpeedAttribute()));
	}
	if (HasAuthority())
	{
		if (const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>())
		{
			if (UAuraEquipmentComponent* Equipment = AuraPlayerState->GetEquipmentComponent())
			{
				Equipment->RefreshEquipmentEffects();
			}
		}
	}
	SetCameraMode(EAuraCameraMode::TopDown, true);
	UE_LOG(LogTemp, Display, TEXT("Aura character ready. Class=%s CameraMode=%s WeaponSocket=%s SocketExists=%s"),
		*GetClass()->GetName(),
		TEXT("GodView"),
		*Weapon->GetAttachSocketName().ToString(),
		GetMesh()->DoesSocketExist(Weapon->GetAttachSocketName()) ? TEXT("true") : TEXT("false"));
}

float AAuraCharacter::GetCombatExitDelay() const
{
	return UAuraCombatBalanceData::Get().Player.CombatExitDelay;
}

void AAuraCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && GetController()) SpectatorViewRotation = GetController()->GetControlRotation();
	if (bFireBoltTurning)
	{
		AAuraEnemy* Target = FireBoltTurnTarget.Get();
		if (bDead || !IsValid(Target) || Target->IsDead())
		{
			EndFireBoltTurn();
		}
		else
		{
			const FVector Direction = (Target->GetActorLocation()
				- GetActorLocation()).GetSafeNormal2D();
			if (!Direction.IsNearlyZero())
			{
				const FRotator Desired(0.f, Direction.Rotation().Yaw, 0.f);
				SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired,
					DeltaSeconds, FireBoltTurnRateDegreesPerSecond));
			}
		}
	}

	const FAuraCameraModeSettings& Settings = TopDownCamera;
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, Settings.TargetArmLength, DeltaSeconds, CameraTransitionSpeed);
	CameraBoom->SocketOffset = FMath::VInterpTo(CameraBoom->SocketOffset, Settings.SocketOffset, DeltaSeconds, CameraTransitionSpeed);
	CameraBoom->TargetOffset = FMath::VInterpTo(CameraBoom->TargetOffset,
		GetDesiredCameraTargetOffset(Settings), DeltaSeconds, CameraTransitionSpeed);
	FollowCamera->SetFieldOfView(FMath::FInterpTo(
		FollowCamera->FieldOfView, Settings.FieldOfView, DeltaSeconds, CameraTransitionSpeed));

	if (!CameraBoom->bUsePawnControlRotation)
	{
		CameraBoom->SetRelativeRotation(FMath::RInterpTo(CameraBoom->GetRelativeRotation(), Settings.RelativeRotation, DeltaSeconds, CameraTransitionSpeed));
	}
}

FRotator AAuraCharacter::GetViewRotation() const
{
	return GetNetMode() == NM_Client && !IsLocallyControlled()
		? SpectatorViewRotation : Super::GetViewRotation();
}

float AAuraCharacter::BeginFireBoltTurnTo(AAuraEnemy* Target)
{
	if (bDead || !IsValid(Target)) return 0.f;
	const FVector Direction = (Target->GetActorLocation()
		- GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero()) return 0.f;
	EndFireBoltTurn();
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	bFireBoltPreviousOrientRotationToMovement = Movement->bOrientRotationToMovement;
	Movement->bOrientRotationToMovement = false;
	bFireBoltTurning = true;
	FireBoltTurnTarget = Target;
	const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
		GetActorRotation().Yaw, Direction.Rotation().Yaw));
	return YawDelta / FMath::Max(90.f, FireBoltTurnRateDegreesPerSecond);
}

void AAuraCharacter::AlignFireBoltTurnToTarget(AAuraEnemy* Target)
{
	if (bFireBoltTurning && IsValid(Target) && !Target->IsDead())
	{
		const FVector Direction = (Target->GetActorLocation()
			- GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		}
	}
	EndFireBoltTurn();
}

void AAuraCharacter::EndFireBoltTurn()
{
	if (!bFireBoltTurning) return;
	GetCharacterMovement()->bOrientRotationToMovement =
		bFireBoltPreviousOrientRotationToMovement;
	bFireBoltTurning = false;
	FireBoltTurnTarget.Reset();
}

void AAuraCharacter::SetCameraMode(EAuraCameraMode, bool bInstant)
{
	// Legacy callers and old serialized values are deliberately clamped. God View
	// is now the only gameplay camera.
	const FAuraCameraModeSettings& Settings = TopDownCamera;

	CameraBoom->bUsePawnControlRotation = Settings.bUsePawnControlRotation;
	CameraBoom->bEnableCameraLag = Settings.bEnableCameraLag;
	CameraBoom->bDoCollisionTest = Settings.bDoCollisionTest;
	// God-view movement owns character facing. The control rotation is
	// reserved for the camera, so A/S/D turn Aura toward the requested direction
	// instead of strafing or backpedalling while her body keeps the camera yaw.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = !bFireBoltTurning;

	if (Controller)
	{
		const float CameraYaw = Controller->GetControlRotation().Yaw;
		Controller->SetControlRotation(FRotator(Settings.RelativeRotation.Pitch, CameraYaw, 0.f));
	}

	if (bInstant)
	{
		CameraBoom->TargetArmLength = Settings.TargetArmLength;
		CameraBoom->SocketOffset = Settings.SocketOffset;
		CameraBoom->TargetOffset = GetDesiredCameraTargetOffset(Settings);
		CameraBoom->SetRelativeRotation(Settings.RelativeRotation);
		FollowCamera->SetFieldOfView(Settings.FieldOfView);
	}
}

FVector AAuraCharacter::GetDesiredCameraTargetOffset(
	const FAuraCameraModeSettings& Settings) const
{
	const float CameraYaw = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
	return FRotator(0.f, CameraYaw, 0.f).Vector() * Settings.LookAheadDistance;
}

void AAuraCharacter::NotifyCombatActivity()
{
	if (!HasAuthority() || bDead || !GetWorld() || !AbilitySystemComponent) return;

	SetCombatState(true);
	GetWorldTimerManager().SetTimer(
		CombatExitTimer, this, &AAuraCharacter::ExitCombatState, GetCombatExitDelay(), false);
}

void AAuraCharacter::RestoreVitalsAtCheckpoint()
{
	if (!HasAuthority() || bDead || !AbilitySystemComponent) return;

	const float OldHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	const float OldMana = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetManaAttribute());
	const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float MaxMana = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxManaAttribute());
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(CombatExitTimer);
	}
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetHealthAttribute(), MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetManaAttribute(), MaxMana);
	SetCombatState(false, true);
	if (OldHealth < MaxHealth - KINDA_SMALL_NUMBER) MulticastHealthRestoreEffect();
	if (OldMana < MaxMana - KINDA_SMALL_NUMBER) MulticastManaRestoreEffect();

	UE_LOG(LogTemp, Display, TEXT("Checkpoint restored player health and mana. Health=%.1f Mana=%.1f"),
		AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()),
		AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()));
}

void AAuraCharacter::ReviveAtTransform(const FTransform& RespawnTransform,
	const float VitalFraction, const float ProtectionDuration)
{
	if (!HasAuthority() || !bDead || !AbilitySystemComponent) return;

	bDead = false;
	bInCombat = false;
	bRunUpgradeSelectionProtected = false;
	ClearComboMarks();
	GetWorldTimerManager().ClearTimer(CombatExitTimer);
	AbilitySystemComponent->CancelAllAbilities();
	AbilitySystemComponent->RemoveLooseGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
	if (AbilitySystemComponent->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.InCombat"))))
		AbilitySystemComponent->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.InCombat")));

	ApplyRevivePresentation();
	TeleportTo(RespawnTransform.GetLocation(), RespawnTransform.Rotator(), false, true);
	ReviveComponent->ResetAfterRevive(ProtectionDuration);
	// Rescue restores a fraction of the currently equipped maxima. It does not
	// run checkpoint recovery, which would overwrite these values with full vitals.
	const float Fraction = FMath::Clamp(VitalFraction, 0.01f, 1.f);
	AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(),
		AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()) * Fraction);
	AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(),
		AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()) * Fraction);
	MulticastHealthRestoreEffect();
	MulticastManaRestoreEffect();
	ForceNetUpdate();

	UE_LOG(LogTemp, Display, TEXT("Player revived for co-op. Location=%s"),
		*RespawnTransform.GetLocation().ToCompactString());
}

void AAuraCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	if (GetNetMode() != NM_Standalone)
	{
		if (HasAuthority() && AbilitySystemComponent && !bDead)
		{
			const FTransform Safe = ReviveComponent->GetLastSafeTransform();
			TeleportTo(Safe.GetLocation(), Safe.Rotator(), false, true);
			AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 0.f);
		}
		return;
	}
	Super::FellOutOfWorld(DamageType);
}

void AAuraCharacter::MulticastArcaneShardsCast_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer || IsLocallyControlled()) return;
	GetDefault<UAuraArcaneShardsAbility>()->PlayCastPresentation(this);
	UE_LOG(LogTemp, Display, TEXT("Remote Arcane Shards cast presentation received. Character=%s"),
		*GetName());
}

void AAuraCharacter::MulticastFireBoltCast_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer || IsLocallyControlled()) return;
	GetDefault<UAuraFireBoltAbility>()->PlayCastPresentation(this);
	UE_LOG(LogTemp, Display, TEXT("Remote Fire Bolt cast presentation received. Character=%s"),
		*GetName());
}

void AAuraCharacter::MulticastArcaneShardsImpact_Implementation(
	const FVector ImpactLocation)
{
	if (GetNetMode() == NM_DedicatedServer || IsLocallyControlled()) return;
	GetDefault<UAuraArcaneShardsAbility>()->PlayImpactPresentation(this, ImpactLocation);
	UE_LOG(LogTemp, Display,
		TEXT("Remote Arcane Shards impact presentation received. Character=%s Impact=%s"),
		*GetName(), *ImpactLocation.ToCompactString());
}

void AAuraCharacter::MulticastShockCast_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer || IsLocallyControlled()) return;
	GetDefault<UAuraShockAbility>()->PlayCastPresentation(this);
	UE_LOG(LogTemp, Display, TEXT("Remote Shock cast presentation received. Character=%s"),
		*GetName());
}

void AAuraCharacter::MulticastShockImpact_Implementation(
	const FVector BeamStart, const FVector BeamEnd,
	const bool bHasImpact, const FVector ImpactLocation)
{
	if (GetNetMode() == NM_DedicatedServer || IsLocallyControlled()) return;
	GetDefault<UAuraShockAbility>()->PlayImpactPresentation(
		this, BeamStart, BeamEnd, bHasImpact, ImpactLocation);
	UE_LOG(LogTemp, Display,
		TEXT("Remote Shock impact presentation received. Character=%s Start=%s End=%s Hit=%s"),
		*GetName(), *BeamStart.ToCompactString(), *BeamEnd.ToCompactString(),
		bHasImpact ? TEXT("true") : TEXT("false"));
}

float AAuraCharacter::GetComboServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.f);
}

int32 AAuraCharacter::GetSkillRank(const EAuraPlayerSkill Skill) const
{
	switch (Skill)
	{
	case EAuraPlayerSkill::FireBolt: return SkillRanks.FireBolt;
	case EAuraPlayerSkill::ArcaneShards: return SkillRanks.ArcaneShards;
	case EAuraPlayerSkill::Shock: return SkillRanks.Shock;
	default: return 0;
	}
}

int32 AAuraCharacter::GetComboRank(const EAuraComboMarkType Type) const
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	return State ? State->GetGrowthState().ComboRanks.GetRank(Type) : 0;
}

bool AAuraCharacter::SetSkillRank(const EAuraPlayerSkill Skill, const int32 NewRank)
{
	if (!HasAuthority() || !AbilitySystemComponent || NewRank < 0
		|| NewRank > UAuraCombatBalanceData::Get().SkillRanks.MaxRank) return false;
	int32* Rank = nullptr;
	switch (Skill)
	{
	case EAuraPlayerSkill::FireBolt: Rank = &SkillRanks.FireBolt; break;
	case EAuraPlayerSkill::ArcaneShards: Rank = &SkillRanks.ArcaneShards; break;
	case EAuraPlayerSkill::Shock: Rank = &SkillRanks.Shock; break;
	default: return false;
	}
	if (*Rank == NewRank) return true;
	*Rank = NewRank;
	RefreshGrantedSkillAbility(Skill);
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("SKILL_RANK_CHANGED Player=%s Skill=%d Rank=%d"),
		*GetName(), static_cast<int32>(Skill), NewRank);
	return true;
}

void AAuraCharacter::SyncGrowthFromPlayerState(const bool bRestoreVitals)
{
	if (!HasAuthority()) return;
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	if (!State) return;
	const FAuraGrowthState& Growth = State->GetGrowthState();
	SetPaidProgression(Growth.PaidLevel, Growth.TotalExperience, bRestoreVitals);
	SetSkillRank(EAuraPlayerSkill::FireBolt, Growth.SkillRanks.FireBolt);
	if (SkillRanks.FireBoltPath != Growth.SkillRanks.FireBoltPath)
	{
		SkillRanks.FireBoltPath = Growth.SkillRanks.FireBoltPath;
		ForceNetUpdate();
	}
	SetSkillRank(EAuraPlayerSkill::ArcaneShards, Growth.SkillRanks.ArcaneShards);
	SetSkillRank(EAuraPlayerSkill::Shock, Growth.SkillRanks.Shock);
	SkillRanks = Growth.SkillRanks;
	ForceNetUpdate();
}

void AAuraCharacter::SetPaidProgression(const int32 PaidLevel,
	const int32 TotalExperience, const bool bRestoreVitals)
{
	if (!HasAuthority()) return;
	const FAuraPlayerProgressionState Old = ProgressionState;
	ProgressionState.Level = FMath::Clamp(PaidLevel, 1,
		UAuraCombatBalanceData::Get().Progression.MaxLevel);
	ProgressionState.Experience = FMath::Max(0, TotalExperience);
	if (Old.Level != ProgressionState.Level || bRestoreVitals)
	{
		ApplyProgressionStats(bRestoreVitals);
	}
	if (Old.Level != ProgressionState.Level || Old.Experience != ProgressionState.Experience)
	{
		ShowProgressionFeedback(Old,
			FMath::Max(0, ProgressionState.Experience - Old.Experience));
		ForceNetUpdate();
	}
}

void AAuraCharacter::RefreshGrantedSkillAbility(const EAuraPlayerSkill Skill)
{
	if (!HasAuthority() || !AbilitySystemComponent) return;
	TSubclassOf<UGameplayAbility> AbilityClass;
	switch (Skill)
	{
	case EAuraPlayerSkill::FireBolt: AbilityClass = UAuraFireBoltAbility::StaticClass(); break;
	case EAuraPlayerSkill::ArcaneShards: AbilityClass = UAuraArcaneShardsAbility::StaticClass(); break;
	case EAuraPlayerSkill::Shock: AbilityClass = UAuraShockAbility::StaticClass(); break;
	default: return;
	}
	FGameplayAbilitySpec* Existing = AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass);
	const int32 Rank = GetSkillRank(Skill);
	if (Rank == 0)
	{
		if (Existing)
		{
			AbilitySystemComponent->CancelAbilityHandle(Existing->Handle);
			AbilitySystemComponent->ClearAbility(Existing->Handle);
		}
		return;
	}
	if (Existing)
	{
		Existing->Level = Rank;
		AbilitySystemComponent->MarkAbilitySpecDirty(*Existing);
	}
	else
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, Rank));
	}
}

int32 AAuraCharacter::AllocateComboCastId()
{
	if (!HasAuthority()) return 0;
	if (NextComboCastId == MAX_int32)
	{
		NextComboCastId = 1;
		ConsumedComboCastIds.Reset();
	}
	return NextComboCastId++;
}

void AAuraCharacter::ApplyComboMark(AAuraEnemy* Target, const EAuraComboMarkType Type,
	const int32 SourceCastId)
{
	APlayerState* SourceState = GetPlayerState();
	if (!HasAuthority() || bDead || GetComboRank(Type) < 1
		|| !SourceState || !Target || Target->IsDead()
		|| Target->IsDamageImmune() || SourceCastId <= 0) return;

	PruneComboMarks();
	const float BonusWindow = Type == EAuraComboMarkType::Ember
		&& HasLegendaryComboEffect(TEXT("Item.Power.EmberEcho")) ? 1.5f : 0.f;
	const float Expiry = GetComboServerTime()
		+ UAuraCombatBalanceData::Get().Combo.MarkDuration + BonusWindow;
	for (FAuraComboMark& Mark : ComboMarks)
	{
		if (Mark.Target == Target && Mark.Type == Type)
		{
			if (Mark.AppliedCastIds.Contains(SourceCastId)) return;
			if (Type == EAuraComboMarkType::Ember && GetComboRank(Type) >= 2
				&& Mark.SourceCastId != SourceCastId)
			{
				Mark.StackCount = FMath::Min(
					UAuraCombatBalanceData::Get().Combo.EmberMaxStacks, Mark.StackCount + 1);
			}
			Mark.AppliedCastIds.Add(SourceCastId);
			Mark.SourcePlayerState = SourceState;
			Mark.SourceCastId = SourceCastId;
			Mark.ExpireServerTime = Expiry;
			ForceNetUpdate();
			return;
		}
	}
	FAuraComboMark& Mark = ComboMarks.AddDefaulted_GetRef();
	Mark.Target = Target;
	Mark.SourcePlayerState = SourceState;
	Mark.Type = Type;
	Mark.SourceCastId = SourceCastId;
	Mark.AppliedCastIds.Add(SourceCastId);
	Mark.ExpireServerTime = Expiry;
	if (!GetWorldTimerManager().IsTimerActive(ComboCleanupTimer))
	{
		GetWorldTimerManager().SetTimer(ComboCleanupTimer, this,
			&AAuraCharacter::PruneComboMarks, 0.2f, true);
	}
	ForceNetUpdate();
}

bool AAuraCharacter::HasLegendaryComboEffect(const FName EffectTag) const
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const UAuraEquipmentComponent* Equipment = State ? State->GetEquipmentComponent() : nullptr;
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(EffectTag, false);
	if (!Equipment || !Tag.IsValid()) return false;
	for (const FAuraEquippedItem& Entry : Equipment->GetEquippedItems())
	{
		if (Entry.Item.Rarity == EAuraItemRarity::Legendary && Entry.Item.Definition
			&& Entry.Item.Definition->ItemTags.HasTagExact(Tag)) return true;
	}
	return false;
}

bool AAuraCharacter::HasComboMark(const AAuraEnemy* Target, const EAuraComboMarkType Type) const
{
	if (!HasAuthority() || bDead || GetComboRank(Type) < 1
		|| !Target || Target->IsDead()
		|| Target->IsDamageImmune()) return false;
	const APlayerState* SourceState = GetPlayerState();
	const float Now = GetComboServerTime();
	return ComboMarks.ContainsByPredicate([Target, Type, SourceState, Now](const FAuraComboMark& Mark)
	{
		return Mark.Target == Target && Mark.Type == Type
			&& Mark.SourcePlayerState == SourceState && Mark.ExpireServerTime > Now;
	});
}

bool AAuraCharacter::TryConsumeComboMark(AAuraEnemy* Target, const EAuraComboMarkType Type,
	const int32 FinisherCastId, int32* OutStackCount)
{
	if (!HasAuthority() || bDead || FinisherCastId <= 0
		|| ConsumedComboCastIds.Contains(FinisherCastId)
		|| !HasComboMark(Target, Type)) return false;
	const APlayerState* SourceState = GetPlayerState();
	const float Now = GetComboServerTime();
	int32 ConsumedStacks = 1;
	const int32 Removed = ComboMarks.RemoveAll([Target, Type, SourceState, Now,
		&ConsumedStacks](const FAuraComboMark& Mark)
	{
		const bool bMatch = Mark.Target == Target && Mark.Type == Type
			&& Mark.SourcePlayerState == SourceState && Mark.ExpireServerTime > Now;
		if (bMatch) ConsumedStacks = Mark.StackCount;
		return bMatch;
	});
	if (Removed == 0) return false;
	if (OutStackCount) *OutStackCount = ConsumedStacks;
	ConsumedComboCastIds.Add(FinisherCastId);
	if (ConsumedComboCastIds.Num() > 64) ConsumedComboCastIds.Reset();
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("COMBO_CONSUMED Type=%s Caster=%s Target=%s CastId=%d"),
		Type == EAuraComboMarkType::Ember ? TEXT("Ember")
			: (Type == EAuraComboMarkType::Fracture ? TEXT("Fracture") : TEXT("Overload")),
		*GetNameSafe(SourceState), *GetNameSafe(Target), FinisherCastId);
	return true;
}

void AAuraCharacter::PruneComboMarks()
{
	if (!HasAuthority()) return;
	const APlayerState* SourceState = GetPlayerState();
	const float Now = GetComboServerTime();
	const int32 Removed = ComboMarks.RemoveAll([SourceState, Now](const FAuraComboMark& Mark)
	{
		return !IsValid(Mark.Target) || Mark.Target->IsDead()
			|| Mark.ExpireServerTime <= Now || Mark.SourcePlayerState != SourceState;
	});
	if (Removed > 0) ForceNetUpdate();
	if (ComboMarks.IsEmpty()) GetWorldTimerManager().ClearTimer(ComboCleanupTimer);
}

void AAuraCharacter::ClearComboMarks()
{
	if (!HasAuthority()) return;
	ComboMarks.Reset();
	ConsumedComboCastIds.Reset();
	GetWorldTimerManager().ClearTimer(ComboCleanupTimer);
	if (OverloadBuffUntil > 0.f)
	{
		GetWorldTimerManager().ClearTimer(OverloadBuffTimer);
		OverloadBuffUntil = 0.f;
		OnRep_OverloadBuffUntil();
		SetCombatState(bInCombat, true);
	}
	ForceNetUpdate();
}

bool AAuraCharacter::ApplyComboDamage(AAuraEnemy* Target, const float Damage)
{
	if (!HasAuthority() || bDead || !Target || Target->IsDead()
		|| Target->IsDamageImmune() || Damage <= 0.f || !AbilitySystemComponent) return false;
	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	if (!TargetASC) return false;
	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(
		UAuraDamageGameplayEffect::StaticClass(), GetCharacterLevel(), Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -Damage);
	Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Data.PoiseDamage")),
		UAuraCombatBalanceData::Get().Poise.ComboDamage);
	TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (AAuraHUD* HUD = Cast<AAuraHUD>(PlayerController->GetHUD())) HUD->ShowDamageNumber(Target, Damage);
	}
	return true;
}

void AAuraCharacter::ExecuteEmberBurst(const FVector& Center, const float BaseQDamage,
	const int32 StackCount)
{
	if (!HasAuthority() || bDead || GetComboRank(EAuraComboMarkType::Ember) < 1) return;
	const FAuraComboBalance& Balance = UAuraCombatBalanceData::Get().Combo;
	const float DamageMultiplier = Balance.EmberBurstDamageMultiplier
		+ (GetComboRank(EAuraComboMarkType::Ember) >= 2
		? Balance.EmberExtraStackDamageMultiplier
			* FMath::Clamp(StackCount - 1, 0, Balance.EmberMaxStacks - 1) : 0.f);
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AuraEmberBurst), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(Balance.EmberBurstRadius), Params);
	TSet<AAuraEnemy*> HitEnemies;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAuraEnemy* Enemy = Cast<AAuraEnemy>(Overlap.GetActor());
		if (Enemy && !HitEnemies.Contains(Enemy)
			&& ApplyComboDamage(Enemy, BaseQDamage * DamageMultiplier))
		{
			HitEnemies.Add(Enemy);
		}
	}
	MulticastEmberBurst(Center);
	UE_LOG(LogTemp, Display, TEXT("COMBO_EMBER_BURST Caster=%s Targets=%d Damage=%.1f"),
		*GetName(), HitEnemies.Num(), BaseQDamage * DamageMultiplier);
}

void AAuraCharacter::ExecuteFractureChain(AAuraEnemy* PrimaryTarget, const float BaseEDamage)
{
	if (!HasAuthority() || bDead || !PrimaryTarget
		|| GetComboRank(EAuraComboMarkType::Fracture) < 1) return;
	const FAuraComboBalance& Balance = UAuraCombatBalanceData::Get().Combo;
	const FVector Origin = PrimaryTarget->GetActorLocation();
	const bool bControlRank = GetComboRank(EAuraComboMarkType::Fracture) >= 2;
	if (bControlRank && !PrimaryTarget->IsDead())
	{
		if (PrimaryTarget->IsBossEnemy())
		{
			PrimaryTarget->TryApplyComboWeaken(Balance.FractureBossWeakenDuration,
				Balance.FractureBossWeakenCooldown);
		}
		else
		{
			PrimaryTarget->TryApplyComboStagger(AbilitySystemComponent,
				Balance.FractureStaggerDuration, Balance.FractureStaggerImmunity);
		}
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AuraFractureChain), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(Balance.FractureChainRadius), Params);
	TArray<AAuraEnemy*> Candidates;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAuraEnemy* Enemy = Cast<AAuraEnemy>(Overlap.GetActor());
		if (Enemy && Enemy != PrimaryTarget && !Enemy->IsDead() && !Enemy->IsDamageImmune())
		{
			Candidates.AddUnique(Enemy);
		}
	}
	Candidates.Sort([Origin](const AAuraEnemy& A, const AAuraEnemy& B)
	{
		return FVector::DistSquared(Origin, A.GetActorLocation())
			< FVector::DistSquared(Origin, B.GetActorLocation());
	});
	int32 HitCount = 0;
	for (AAuraEnemy* Enemy : Candidates)
	{
		const int32 TargetLimit = Balance.FractureChainMaxTargets
			+ (HasLegendaryComboEffect(TEXT("Item.Power.FractureSurge")) ? 1 : 0);
		if (HitCount >= TargetLimit) break;
		if (!ApplyComboDamage(Enemy, BaseEDamage * Balance.FractureChainDamageMultiplier)) continue;
		if (bControlRank && !Enemy->IsDead())
		{
			if (Enemy->IsBossEnemy())
			{
				Enemy->TryApplyComboWeaken(Balance.FractureBossWeakenDuration,
					Balance.FractureBossWeakenCooldown);
			}
			else
			{
				Enemy->TryApplyComboStagger(AbilitySystemComponent,
					Balance.FractureStaggerDuration, Balance.FractureStaggerImmunity);
			}
		}
		if (GetSkillRank(EAuraPlayerSkill::Shock) >= 3 && !Enemy->IsDead())
		{
			Enemy->InterruptTelegraphedAttack();
		}
		MulticastFractureChain(Origin + FVector(0.f, 0.f, 70.f),
			Enemy->GetActorLocation() + FVector(0.f, 0.f, 70.f));
		++HitCount;
	}
	UE_LOG(LogTemp, Display, TEXT("COMBO_FRACTURE_CHAIN Caster=%s Targets=%d Damage=%.1f"),
		*GetName(), HitCount, BaseEDamage * Balance.FractureChainDamageMultiplier);
}

void AAuraCharacter::ExecuteOverload(const FVector& Center, const float BaseFireDamage)
{
	if (!HasAuthority() || bDead || GetComboRank(EAuraComboMarkType::Overload) < 1) return;
	const FAuraComboBalance& Balance = UAuraCombatBalanceData::Get().Combo;
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AuraOverloadBurst), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(Balance.OverloadRadius), Params);
	TSet<AAuraEnemy*> HitEnemies;
	int32 Interrupted = 0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAuraEnemy* Enemy = Cast<AAuraEnemy>(Overlap.GetActor());
		if (!Enemy || HitEnemies.Contains(Enemy)
			|| !ApplyComboDamage(Enemy, BaseFireDamage * Balance.OverloadDamageMultiplier)) continue;
		HitEnemies.Add(Enemy);
		if (!Enemy->IsBossEnemy() && !Enemy->IsDead()
			&& Enemy->InterruptTelegraphedAttack()) ++Interrupted;
	}
	MulticastOverload(Center);
	if (GetComboRank(EAuraComboMarkType::Overload) >= 2
		&& GetComboServerTime() >= OverloadBuffReadyAt)
	{
		OverloadBuffReadyAt = GetComboServerTime() + Balance.OverloadBuffCooldown;
		OverloadBuffUntil = GetComboServerTime() + Balance.OverloadBuffDuration;
		GetWorldTimerManager().SetTimer(OverloadBuffTimer, this,
			&AAuraCharacter::ExpireOverloadBuff, Balance.OverloadBuffDuration, false);
		OnRep_OverloadBuffUntil();
		ForceNetUpdate();
	}
	UE_LOG(LogTemp, Display,
		TEXT("COMBO_OVERLOAD Caster=%s Targets=%d Interrupted=%d Damage=%.1f"),
		*GetName(), HitEnemies.Num(), Interrupted,
		BaseFireDamage * Balance.OverloadDamageMultiplier);
}

void AAuraCharacter::MulticastEmberBurst_Implementation(const FVector Center)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UE_LOG(LogTemp, Display, TEXT("AURA_COMBO_EFFECT EmberBurst Character=%s LocalOwner=%s"),
		*GetName(), IsLocallyControlled() ? TEXT("true") : TEXT("false"));
	const FVector EffectLocation = Center + FVector(0.f, 0.f, 45.f);
	if (EmberBurstEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, EmberBurstEffect, EffectLocation,
			FRotator::ZeroRotator, FVector(0.7f));
	}
	if (EmberBurstSound) UGameplayStatics::PlaySoundAtLocation(this, EmberBurstSound, EffectLocation);
	if (IsLocallyControlled())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (AAuraHUD* HUD = Cast<AAuraHUD>(PlayerController->GetHUD()))
			{
				HUD->ShowStatusMessage(TEXT("余烬爆发"), FLinearColor(1.f, 0.45f, 0.08f));
			}
		}
	}
}

void AAuraCharacter::MulticastFractureChain_Implementation(const FVector Start, const FVector End)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UE_LOG(LogTemp, Display, TEXT("AURA_COMBO_EFFECT FractureChain Character=%s LocalOwner=%s"),
		*GetName(), IsLocallyControlled() ? TEXT("true") : TEXT("false"));
	GetDefault<UAuraShockAbility>()->PlayImpactPresentation(this, Start, End, true, End);
	if (FractureChainSound) UGameplayStatics::PlaySoundAtLocation(this, FractureChainSound, End);
	if (IsLocallyControlled())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (AAuraHUD* HUD = Cast<AAuraHUD>(PlayerController->GetHUD()))
			{
				HUD->ShowStatusMessage(TEXT("裂隙连锁"), FLinearColor(0.3f, 0.75f, 1.f));
			}
		}
	}
}

void AAuraCharacter::MulticastOverload_Implementation(const FVector Center)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	const FVector EffectLocation = Center + FVector(0.f, 0.f, 45.f);
	if (EmberBurstEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, EmberBurstEffect,
			EffectLocation, FRotator::ZeroRotator, FVector(0.48f));
	}
	if (FractureChainSound)
		UGameplayStatics::PlaySoundAtLocation(this, FractureChainSound, EffectLocation);
	if (IsLocallyControlled())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (AAuraHUD* HUD = Cast<AAuraHUD>(PlayerController->GetHUD()))
				HUD->ShowStatusMessage(TEXT("雷火过载"), FLinearColor(0.58f, 0.76f, 1.f));
		}
	}
}

void AAuraCharacter::MulticastLevelUpEffect_Implementation()
{
	SpawnProgressionFeedbackEffect(LevelUpEffect, 4.f, TEXT("LevelUp"));
}

void AAuraCharacter::MulticastHealthRestoreEffect_Implementation()
{
	SpawnProgressionFeedbackEffect(HealthRestoreEffect, 3.f, TEXT("HealthRestore"));
}

void AAuraCharacter::MulticastManaRestoreEffect_Implementation()
{
	SpawnProgressionFeedbackEffect(ManaRestoreEffect, 3.f, TEXT("ManaRestore"));
}

void AAuraCharacter::SpawnProgressionFeedbackEffect(UNiagaraSystem* Effect,
	const float MaximumLifetime, const TCHAR* CueName)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (!Effect || !GetRootComponent() || !GetCapsuleComponent())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AURA_FEEDBACK_EFFECT Cue=%s Spawned=false Character=%s Asset=%s"),
			CueName, *GetName(), *GetNameSafe(Effect));
		return;
	}

	const FVector FeetOffset(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
		Effect, GetRootComponent(), NAME_None, FeetOffset, FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset, true);
	if (!Component)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AURA_FEEDBACK_EFFECT Cue=%s Spawned=false Character=%s Asset=%s"),
			CueName, *GetName(), *GetNameSafe(Effect));
		return;
	}

	// Keep the component on the moving pawn. The mana asset also uses local-space
	// emitters so particles which have already spawned move with this transform.
	Component->SetAbsolute(false, false, false);
	Component->AttachToComponent(GetRootComponent(),
		FAttachmentTransformRules::KeepRelativeTransform);
	Component->SetRelativeLocation(FeetOffset);
	Component->SetAutoDestroy(true);
	const TWeakObjectPtr<UNiagaraComponent> WeakComponent(Component);
	FTimerHandle LifetimeTimer;
	GetWorldTimerManager().SetTimer(LifetimeTimer, [WeakComponent]()
	{
		if (WeakComponent.IsValid()) WeakComponent->DestroyComponent();
	}, FMath::Max(0.1f, MaximumLifetime), false);
	UE_LOG(LogTemp, Display,
		TEXT("AURA_FEEDBACK_EFFECT Cue=%s Spawned=true Character=%s Asset=%s Lifetime=%.1f"),
		CueName, *GetName(), *GetNameSafe(Effect), MaximumLifetime);
}

void AAuraCharacter::AddExperience(const int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return;
	if (AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>())
	{
		State->EnsureGrowthInitialized();
		State->AwardExperienceAndSouls(Amount, 0);
	}
}

int32 AAuraCharacter::GetEligibleLevel() const
{
	return ResolveLevelForExperience(ProgressionState.Experience);
}

int32 AAuraCharacter::GetExperienceIntoCurrentLevel() const
{
	if (GetEligibleLevel() >= UAuraCombatBalanceData::Get().Progression.MaxLevel) return 0;
	return FMath::Max(0, ProgressionState.Experience
		- GetCumulativeExperienceForLevel(GetEligibleLevel()));
}

int32 AAuraCharacter::GetExperienceRequiredForNextLevel() const
{
	if (GetEligibleLevel() >= UAuraCombatBalanceData::Get().Progression.MaxLevel) return 0;
	const FAuraPlayerProgressionBalance& Progression = UAuraCombatBalanceData::Get().Progression;
	return Progression.GetExperienceToNextLevel(GetEligibleLevel());
}

bool AAuraCharacter::IsAtMaxLevel() const
{
	return ProgressionState.Level >= UAuraCombatBalanceData::Get().Progression.MaxLevel;
}

float AAuraCharacter::GetAbilityDamageMultiplier() const
{
	const FAuraPlayerProgressionBalance& Progression = UAuraCombatBalanceData::Get().Progression;
	const FAuraPlayerCombatBalance& Player = UAuraCombatBalanceData::Get().Player;
	const float ProgressionIntelligence = Player.Intelligence
		+ FMath::Max(0, ProgressionState.Level - 1) * Progression.IntelligencePerLevel;
	const float CurrentIntelligence = AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetIntelligenceAttribute())
		: ProgressionIntelligence;
	const float EquipmentIntelligenceBonus = FMath::Max(
		0.f, CurrentIntelligence - ProgressionIntelligence);
	return 1.f + FMath::Max(0, ProgressionState.Level - 1)
		* Progression.AbilityDamageBonusPerLevel
		+ RunUpgradeState.SpellweaverRank
			* UAuraCombatBalanceData::Get().RunUpgrades.UniversalDamageBonusPerRank
		+ FMath::Min(0.5f, EquipmentIntelligenceBonus * 0.01f);
}

float AAuraCharacter::GetFireBoltDamageMultiplier() const
{
	return GetAbilityDamageMultiplier() * (1.f + RunUpgradeState.FireBoltPowerRank
		* UAuraCombatBalanceData::Get().RunUpgrades.FireBoltDamageBonusPerRank)
		* (GetSkillRank(EAuraPlayerSkill::FireBolt) >= 2
			? 1.f + UAuraCombatBalanceData::Get().SkillRanks.FireBoltDamageBonus : 1.f)
		* (GetSkillRank(EAuraPlayerSkill::FireBolt) >= 4 ? 1.15f : 1.f);
}

float AAuraCharacter::GetArcaneShardsDamageMultiplier() const
{
	return GetAbilityDamageMultiplier() * (1.f + RunUpgradeState.ArcaneShardsMasteryRank
		* UAuraCombatBalanceData::Get().RunUpgrades.ArcaneShardsDamageBonusPerRank)
		* (GetSkillRank(EAuraPlayerSkill::ArcaneShards) >= 4 ? 1.15f : 1.f);
}

float AAuraCharacter::GetArcaneShardsRadiusMultiplier() const
{
	return 1.f + RunUpgradeState.ArcaneShardsMasteryRank
		* UAuraCombatBalanceData::Get().RunUpgrades.ArcaneShardsRadiusBonusPerRank
		+ (GetSkillRank(EAuraPlayerSkill::ArcaneShards) >= 2
			? UAuraCombatBalanceData::Get().SkillRanks.ArcaneShardsRadiusBonus : 0.f);
}

float AAuraCharacter::GetShockDamageMultiplier() const
{
	return GetAbilityDamageMultiplier() * (1.f + RunUpgradeState.ShockMasteryRank
		* UAuraCombatBalanceData::Get().RunUpgrades.ShockDamageBonusPerRank)
		* (GetSkillRank(EAuraPlayerSkill::Shock) >= 2
			? 1.f + UAuraCombatBalanceData::Get().SkillRanks.ShockDamageBonus : 1.f)
		* (GetSkillRank(EAuraPlayerSkill::Shock) >= 4 ? 1.15f : 1.f);
}

float AAuraCharacter::GetShockStunDurationBonus() const
{
	return RunUpgradeState.ShockMasteryRank
		* UAuraCombatBalanceData::Get().RunUpgrades.ShockStunBonusPerRank;
}

bool AAuraCharacter::ApplyRunUpgrade(const EAuraRunUpgradeType Type)
{
	if (!HasAuthority() || bDead) return false;

	const int32 MaxRank = UAuraCombatBalanceData::Get().RunUpgrades.MaxRankPerUpgrade;
	const FAuraRunUpgradeState OldState = RunUpgradeState;
	if (!RunUpgradeState.AddRank(Type, MaxRank)) return false;

	ApplyProgressionStats(false);
	if (GetNetMode() == NM_Standalone)
	{
		if (UAuraRunSubsystem* RunSubsystem =
			GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			RunSubsystem->UpdateSavedRunUpgrades(GetWorld(), RunUpgradeState);
		}
	}
	ForceNetUpdate();
	UE_LOG(LogTemp, Display,
		TEXT("Run upgrade applied. Type=%s Rank=%d TotalRanks=%d Damage=%.2f Health=%.1f Mana=%.1f"),
		*AuraRunUpgrade::GetTitle(Type), RunUpgradeState.GetRank(Type),
		RunUpgradeState.GetTotalRanks(), GetAbilityDamageMultiplier(),
		AbilitySystemComponent ? AbilitySystemComponent->GetNumericAttribute(
			UAuraAttributeSet::GetMaxHealthAttribute()) : 0.f,
		AbilitySystemComponent ? AbilitySystemComponent->GetNumericAttribute(
			UAuraAttributeSet::GetMaxManaAttribute()) : 0.f);
	return RunUpgradeState != OldState;
}

void AAuraCharacter::SetRunUpgradeSelectionProtected(const bool bProtected)
{
	if (HasAuthority()) bRunUpgradeSelectionProtected = bProtected;
}

int32 AAuraCharacter::GetCumulativeExperienceForLevel(const int32 Level) const
{
	const FAuraPlayerProgressionBalance& Progression = UAuraCombatBalanceData::Get().Progression;
	return Progression.GetCumulativeExperienceForLevel(Level);
}

int32 AAuraCharacter::ResolveLevelForExperience(const int32 TotalExperience) const
{
	const int32 MaxLevel = UAuraCombatBalanceData::Get().Progression.MaxLevel;
	int32 ResolvedLevel = 1;
	while (ResolvedLevel < MaxLevel
		&& TotalExperience >= GetCumulativeExperienceForLevel(ResolvedLevel + 1))
	{
		++ResolvedLevel;
	}
	return ResolvedLevel;
}

void AAuraCharacter::RestoreProgression(const int32 SavedLevel, const int32 SavedExperience)
{
	if (AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>())
	{
		State->MigrateLegacyGrowth(SavedLevel, SavedExperience);
	}
}

void AAuraCharacter::RestoreRunUpgrades(const FAuraRunUpgradeState& SavedRunUpgrades)
{
	const int32 MaxRank = UAuraCombatBalanceData::Get().RunUpgrades.MaxRankPerUpgrade;
	RunUpgradeState = SavedRunUpgrades;
	RunUpgradeState.FireBoltPowerRank = FMath::Clamp(RunUpgradeState.FireBoltPowerRank, 0, MaxRank);
	RunUpgradeState.ArcaneShardsMasteryRank = FMath::Clamp(RunUpgradeState.ArcaneShardsMasteryRank, 0, MaxRank);
	RunUpgradeState.ShockMasteryRank = FMath::Clamp(RunUpgradeState.ShockMasteryRank, 0, MaxRank);
	RunUpgradeState.VitalityRank = FMath::Clamp(RunUpgradeState.VitalityRank, 0, MaxRank);
	RunUpgradeState.ManaSpringRank = FMath::Clamp(RunUpgradeState.ManaSpringRank, 0, MaxRank);
	RunUpgradeState.SpellweaverRank = FMath::Clamp(RunUpgradeState.SpellweaverRank, 0, MaxRank);
	UE_LOG(LogTemp, Display, TEXT("Run upgrades restored. TotalRanks=%d"),
		RunUpgradeState.GetTotalRanks());
}

void AAuraCharacter::ApplyProgressionStats(const bool bRestoreVitals)
{
	if (!AbilitySystemComponent) return;

	const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
	const int32 LevelOffset = FMath::Max(0, ProgressionState.Level - 1);
	const float NewBaseMaxHealth = Balance.Player.MaxHealth
		+ LevelOffset * Balance.Progression.MaxHealthPerLevel
		+ RunUpgradeState.VitalityRank * Balance.RunUpgrades.MaxHealthPerRank;
	const float NewBaseMaxMana = Balance.Player.MaxMana
		+ LevelOffset * Balance.Progression.MaxManaPerLevel
		+ RunUpgradeState.ManaSpringRank * Balance.RunUpgrades.MaxManaPerRank;
	const float OldMaxHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float OldHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetHealthAttribute());
	const float OldMana = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetManaAttribute());

	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetMaxHealthAttribute(), NewBaseMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetMaxManaAttribute(), NewBaseMaxMana);
	const float NewMaxHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float NewMaxMana = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxManaAttribute());
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetHealthAttribute(), bRestoreVitals ? NewMaxHealth
			: FMath::Min(NewMaxHealth, OldHealth + FMath::Max(0.f, NewMaxHealth - OldMaxHealth)));
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetManaAttribute(), bRestoreVitals ? NewMaxMana
			: FMath::Min(NewMaxMana, OldMana));
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetStrengthAttribute(),
		Balance.Player.Strength + LevelOffset * Balance.Progression.StrengthPerLevel);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetIntelligenceAttribute(),
		Balance.Player.Intelligence + LevelOffset * Balance.Progression.IntelligencePerLevel);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetArmorAttribute(),
		Balance.Player.Armor + LevelOffset * Balance.Progression.ArmorPerLevel);

	UE_LOG(LogTemp, Display,
		TEXT("Player level stats applied. Level=%d MaxHealth=%.1f MaxMana=%.1f Strength=%.1f Intelligence=%.1f Armor=%.1f DamageMultiplier=%.2f RestoreVitals=%s"),
		ProgressionState.Level, NewMaxHealth, NewMaxMana,
		Balance.Player.Strength + LevelOffset * Balance.Progression.StrengthPerLevel,
		Balance.Player.Intelligence + LevelOffset * Balance.Progression.IntelligencePerLevel,
		Balance.Player.Armor + LevelOffset * Balance.Progression.ArmorPerLevel,
		GetAbilityDamageMultiplier(), bRestoreVitals ? TEXT("true") : TEXT("false"));
}

void AAuraCharacter::ShowProgressionFeedback(
	const FAuraPlayerProgressionState& OldState, const int32 ExperienceGained)
{
	AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(GetController());
	AAuraHUD* AuraHUD = AuraController ? Cast<AAuraHUD>(AuraController->GetHUD()) : nullptr;
	if (!AuraHUD || ExperienceGained <= 0) return;

	if (ProgressionState.Level > OldState.Level)
	{
		const int32 SkillPointsGained = FAuraGrowthState::GetEarnedSkillPoints(
			ProgressionState.Level) - FAuraGrowthState::GetEarnedSkillPoints(OldState.Level);
		const FString PointMessage = FString::Printf(TEXT("技艺点 +%d"), SkillPointsGained);
		AuraHUD->ShowStatusMessage(
			FString::Printf(TEXT("自动升至 %d 级  |  %s  |  伤害 x%.2f"),
				ProgressionState.Level, *PointMessage, GetAbilityDamageMultiplier()),
			FLinearColor(1.f, 0.78f, 0.16f), 2.5f);
	}
	else
	{
		AuraHUD->ShowStatusMessage(
			FString::Printf(TEXT("经验 +%d"), ExperienceGained),
			FLinearColor(0.68f, 0.48f, 1.f));
	}
}

void AAuraCharacter::OnRep_ProgressionState(const FAuraPlayerProgressionState OldState)
{
	ShowProgressionFeedback(OldState,
		FMath::Max(0, ProgressionState.Experience - OldState.Experience));
}

void AAuraCharacter::OnRep_RunUpgradeState(const FAuraRunUpgradeState OldState)
{
	UE_LOG(LogTemp, Display,
		TEXT("Run upgrades replicated. OldRanks=%d NewRanks=%d"),
		OldState.GetTotalRanks(), RunUpgradeState.GetTotalRanks());
}

void AAuraCharacter::OnRep_OverloadBuffUntil()
{
	if (AbilitySystemComponent)
	{
		ApplyMovementSpeedBonus(AbilitySystemComponent->GetNumericAttribute(
			UAuraAttributeSet::GetMoveSpeedAttribute()));
	}
}

void AAuraCharacter::ExpireOverloadBuff()
{
	if (!HasAuthority()) return;
	OverloadBuffUntil = 0.f;
	OnRep_OverloadBuffUntil();
	ForceNetUpdate();
}

void AAuraCharacter::SetCombatState(const bool bNewInCombat, const bool bForce)
{
	if (!HasAuthority() || !AbilitySystemComponent || (!bForce && bInCombat == bNewInCombat)) return;

	const FGameplayTag InCombatTag = FGameplayTag::RequestGameplayTag(TEXT("State.InCombat"));
	if (bNewInCombat && !bInCombat)
	{
		AbilitySystemComponent->AddLooseGameplayTag(InCombatTag);
	}
	else if (!bNewInCombat && bInCombat)
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(InCombatTag);
	}
	bInCombat = bNewInCombat;

	UE_LOG(LogTemp, Display, TEXT("Player combat state. State=%s ExitDelay=%.1fs ManaRegen=Disabled"),
		bInCombat ? TEXT("Combat") : TEXT("OutOfCombat"),
		GetCombatExitDelay());
}

void AAuraCharacter::ApplyPlayerCombatBalance()
{
	if (!AbilitySystemComponent) return;

	const FAuraPlayerCombatBalance& PlayerBalance = UAuraCombatBalanceData::Get().Player;
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetMaxHealthAttribute(), PlayerBalance.MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetHealthAttribute(), PlayerBalance.MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetMaxManaAttribute(), PlayerBalance.MaxMana);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetManaAttribute(), PlayerBalance.MaxMana);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetStrengthAttribute(), PlayerBalance.Strength);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetIntelligenceAttribute(), PlayerBalance.Intelligence);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetArmorAttribute(), PlayerBalance.Armor);
	AbilitySystemComponent->SetNumericAttributeBase(
		UAuraAttributeSet::GetMoveSpeedAttribute(), 0.f);

	UE_LOG(LogTemp, Display,
		TEXT("Player combat balance applied. Version=%s Health=%.1f Mana=%.1f ManaRegen=Disabled"),
		*UAuraCombatBalanceData::Get().BalanceVersion.ToString(),
		PlayerBalance.MaxHealth, PlayerBalance.MaxMana);
}

void AAuraCharacter::ExitCombatState()
{
	if (bDead) return;
	SetCombatState(false);
}

void AAuraCharacter::OnHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	UE_LOG(LogTemp, Display, TEXT("Player health changed. Old=%.1f New=%.1f"),
		ChangeData.OldValue, ChangeData.NewValue);
	if (ChangeData.NewValue >= ChangeData.OldValue || bDead) return;
	const float CurrentMaxHealth = AbilitySystemComponent
		? AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute())
		: ChangeData.NewValue;
	// Removing a MaxHealth modifier may clamp current health to the new maximum.
	// This is a loadout adjustment, not incoming damage: do not show hit feedback
	// or enter combat merely because armor was unequipped.
	if (ChangeData.OldValue > CurrentMaxHealth + KINDA_SMALL_NUMBER
		&& FMath::IsNearlyEqual(ChangeData.NewValue, CurrentMaxHealth, 0.1f))
	{
		return;
	}
	NotifyCombatActivity();

	if (AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(GetController()))
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(AuraController->GetHUD()))
		{
			AuraHUD->ShowPlayerHit(ChangeData.OldValue - ChangeData.NewValue);
		}
	}

	if (ChangeData.NewValue <= 0.f)
	{
		if (HasAuthority()) HandleDeath();
		return;
	}

	if (HitReactAnimation)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			AnimInstance->PlaySlotAnimationAsDynamicMontage(
				HitReactAnimation, TEXT("DefaultSlot"), 0.05f, 0.12f, 1.f, 1, -1.f, 0.f);
		}
	}
}

void AAuraCharacter::OnMoveSpeedChanged(const FOnAttributeChangeData& ChangeData)
{
	ApplyMovementSpeedBonus(ChangeData.NewValue);
}

void AAuraCharacter::ApplyMovementSpeedBonus(const float BonusSpeed)
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		const float BaseSpeed = FMath::Clamp(
			BaseWalkSpeed + BonusSpeed, BaseWalkSpeed * 0.6f, BaseWalkSpeed + 250.f);
		Movement->MaxWalkSpeed = BaseSpeed * (HasSporeExposure()
			? 1.f - UAuraCombatBalanceData::Get().MeleeEnemy.SporeExposureSlowFraction : 1.f)
			* (!bDead && GetComboServerTime() < OverloadBuffUntil
				? 1.f + UAuraCombatBalanceData::Get().Combo.OverloadMoveSpeedBonusFraction : 1.f);
	}
}

void AAuraCharacter::OnSporeExposureChanged(const FGameplayTag, const int32)
{
	if (AbilitySystemComponent)
	{
		ApplyMovementSpeedBonus(AbilitySystemComponent->GetNumericAttribute(
			UAuraAttributeSet::GetMoveSpeedAttribute()));
	}
}

bool AAuraCharacter::HasSporeExposure() const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.SporeExposed")));
}

bool AAuraCharacter::ApplySporeExposure(AAuraEnemy* Source)
{
	if (!HasAuthority() || bDead || !Source || Source->IsDead()
		|| !AbilitySystemComponent || !Source->GetAbilitySystemComponent()) return false;
	UAbilitySystemComponent* SourceASC = Source->GetAbilitySystemComponent();
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddSourceObject(Source);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		UAuraSporeExposureEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")),
		UAuraCombatBalanceData::Get().MeleeEnemy.SporeExposureDuration);
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	return HasSporeExposure();
}

bool AAuraCharacter::ConsumeSporeExposure()
{
	if (!HasAuthority() || bDead || !HasSporeExposure()) return false;
	FGameplayTagContainer ExposureTags;
	ExposureTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.SporeExposed")));
	return AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(ExposureTags) > 0;
}

void AAuraCharacter::HandleDeath()
{
	if (!HasAuthority() || bDead) return;
	bDead = true;
	ReviveComponent->EnterDownedState();
	ClearComboMarks();
	GetWorldTimerManager().ClearTimer(OverloadBuffTimer);
	OverloadBuffUntil = 0.f;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
		FGameplayTagContainer ExposureTags;
		ExposureTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.SporeExposed")));
		ExposureTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")));
		ExposureTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Slowed")));
		AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(ExposureTags);
		if (bInCombat)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(
				FGameplayTag::RequestGameplayTag(TEXT("State.InCombat")));
		}
		AbilitySystemComponent->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
	}
	bInCombat = false;
	bRunUpgradeSelectionProtected = false;
	GetWorldTimerManager().ClearTimer(CombatExitTimer);
	ApplyDeathPresentation();
	ForceNetUpdate();
	if (AAuraGameMode* AuraGameMode = GetWorld()->GetAuthGameMode<AAuraGameMode>())
	{
		AuraGameMode->HandlePlayerDeath(this);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Player defeated. Ragdoll enabled; waiting for rescue or team checkpoint recovery."));
}

void AAuraCharacter::OnRep_Dead()
{
	if (bDead) ApplyDeathPresentation();
	else ApplyRevivePresentation();
}

void AAuraCharacter::ApplyDeathPresentation()
{
	if (bDeathPresentationApplied) return;
	bDeathPresentationApplied = true;
	EndFireBoltTurn();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(GetController());
		AuraController && AuraController->IsLocalController())
	{
		AuraController->SetIgnoreMoveInput(true);
	}

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		if (UAnimInstance* AnimInstance = CharacterMesh->GetAnimInstance())
		{
			AnimInstance->StopAllMontages(0.1f);
		}
		CharacterMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		// Keep the capsule's rescue anchor fixed and prevent teammates pushing the body.
		CharacterMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CharacterMesh->SetAllBodiesSimulatePhysics(true);
		CharacterMesh->SetSimulatePhysics(true);
		CharacterMesh->WakeAllRigidBodies();
	}
}

void AAuraCharacter::ApplyRevivePresentation()
{
	bDeathPresentationApplied = false;
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetSimulatePhysics(false);
		CharacterMesh->SetAllBodiesSimulatePhysics(false);
		CharacterMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CharacterMesh->SetRelativeTransform(InitialMeshRelativeTransform);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	if (AAuraPlayerController* AuraController = Cast<AAuraPlayerController>(GetController());
		AuraController && AuraController->IsLocalController())
	{
		AuraController->SetIgnoreMoveInput(false);
	}
}


