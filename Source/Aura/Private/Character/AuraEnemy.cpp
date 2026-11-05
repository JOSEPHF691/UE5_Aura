// Copyright yosep


#include "Character/AuraEnemy.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"

#include "AI/AuraAIController.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Abilities/AuraBossAbility.h"
#include "AbilitySystem/Abilities/AuraMeleeAbility.h"
#include "AbilitySystem/Abilities/AuraShamanBoltAbility.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Character/AuraCharacter.h"
#include "Data/AuraCombatBalanceData.h"
#include "Game/AuraGameState.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Level/AuraEncounterVolume.h"
#include "Loot/AuraLootSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Player/AuraPlayerState.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraEnemy, Log, All);

AAuraEnemy::AAuraEnemy()
{
	ElementalStatus = CreateDefaultSubobject<UAuraElementalStatusComponent>(TEXT("ElementalStatus"));
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.08f;
	AIControllerClass = AAuraAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 250.f;
	Weapon->SetVisibility(false);
	BossWardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BossWardShell"));
	BossWardMesh->SetupAttachment(GetRootComponent());
	BossWardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BossWardMesh->SetGenerateOverlapEvents(false);
	BossWardMesh->SetCastShadow(false);
	BossWardMesh->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WardSphereAsset(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (WardSphereAsset.Succeeded()) BossWardMesh->SetStaticMesh(WardSphereAsset.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WardMaterialAsset(
		TEXT("/Game/Aura/Materials/M_BossWardShell.M_BossWardShell"));
	if (WardMaterialAsset.Succeeded()) BossWardMesh->SetMaterial(0, WardMaterialAsset.Object);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> CastWarningEffectAsset(
		TEXT("/Game/Assets/Effects/Combat/NS_GroundSummon.NS_GroundSummon"));
	if (CastWarningEffectAsset.Succeeded())
	{
		MeleeTelegraphEffect = CastWarningEffectAsset.Object;
		RangedCastWarningEffect = CastWarningEffectAsset.Object;
		BossTelegraphEffect = CastWarningEffectAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> MeleeImpactEffectAsset(
		TEXT("/Game/Assets/Effects/Combat/NS_CombatImpact.NS_CombatImpact"));
	if (MeleeImpactEffectAsset.Succeeded()) MeleeImpactEffect = MeleeImpactEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> BossPhaseEffectAsset(
		TEXT("/Game/Assets/Effects/Fire/NS_Fire.NS_Fire"));
	if (BossPhaseEffectAsset.Succeeded())
	{
		BossPhaseEffect = BossPhaseEffectAsset.Object;
		BossDeathEffect = BossPhaseEffectAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> BossImpactEffectAsset(
		TEXT("/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion.NS_FireExplosion"));
	if (BossImpactEffectAsset.Succeeded()) BossImpactEffect = BossImpactEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> WindCastEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Air.NS_Free_Spells_Aura_Air"));
	if (WindCastEffectAsset.Succeeded()) WindCastEffect = WindCastEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SporeCastEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Soul.NS_Free_Spells_Aura_Soul"));
	if (SporeCastEffectAsset.Succeeded()) SporeCastEffect = SporeCastEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LightningCastEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Lightning.NS_Free_Spells_Aura_Lightning"));
	if (LightningCastEffectAsset.Succeeded()) LightningCastEffect = LightningCastEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> FireCastEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Fire.NS_Free_Spells_Aura_Fire"));
	if (FireCastEffectAsset.Succeeded())
	{
		FireCastEffect = FireCastEffectAsset.Object;
		// The phase transition is a boss skill beat, so use the authored aura while
		// retaining the legacy fire system for the longer death presentation.
		BossPhaseEffect = FireCastEffectAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> WindImpactEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Shockwave.NS_Free_Spells_Shockwave"));
	if (WindImpactEffectAsset.Succeeded()) WindImpactEffect = WindImpactEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> MagicBurstEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Explosion.NS_Free_Spells_Explosion"));
	if (MagicBurstEffectAsset.Succeeded()) MagicBurstEffect = MagicBurstEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SporeFieldEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Notes.NS_Free_Spells_Aura_Notes"));
	if (SporeFieldEffectAsset.Succeeded()) SporeFieldEffect = SporeFieldEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ShamanGroundBurstEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Air.NS_Free_Spells_Buff_Air"));
	if (ShamanGroundBurstEffectAsset.Succeeded())
		ShamanGroundBurstEffect = ShamanGroundBurstEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> LightningReleaseEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Lightning.NS_Free_Spells_Buff_Lightning"));
	if (LightningReleaseEffectAsset.Succeeded())
		LightningReleaseEffect = LightningReleaseEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> FireReleaseEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Fire.NS_Free_Spells_Buff_Fire"));
	if (FireReleaseEffectAsset.Succeeded()) FireReleaseEffect = FireReleaseEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SoulReleaseEffectAsset(
		TEXT("/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Buff_Soul.NS_Free_Spells_Buff_Soul"));
	if (SoulReleaseEffectAsset.Succeeded()) SoulReleaseEffect = SoulReleaseEffectAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> BossPhaseSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Demon/Hurt/SFX_Demon_Hurt_03.SFX_Demon_Hurt_03"));
	if (BossPhaseSoundAsset.Succeeded()) BossPhaseTransitionSound = BossPhaseSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> BossDeathSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Demon/Death/SFX_Demon_Death_01.SFX_Demon_Death_01"));
	if (BossDeathSoundAsset.Succeeded()) BossDeathSound = BossDeathSoundAsset.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> BossDeathImpactSoundAsset(
		TEXT("/Game/Assets/Sounds/Enemies/Goblin/RockHit/SFX_RockHit_Body_03.SFX_RockHit_Body_03"));
	if (BossDeathImpactSoundAsset.Succeeded()) BossDeathImpactSound = BossDeathImpactSoundAsset.Object;
}

void AAuraEnemy::ConfigurePreplacedGuard()
{
	bUseSpawnRisePresentation = false;
	bEncounterDormant = true;
}

void AAuraEnemy::ConfigureEliteVariant(const FVector& WorldScale)
{
	if (!HasAuthority() || bBossEnemy) return;
	bEliteVariant = true;
	ElitePresentationScale = WorldScale;
	SetActorScale3D(ElitePresentationScale);
	LootProfile = EAuraLootProfileType::EliteEnemy;
}

void AAuraEnemy::OnRep_ElitePresentationScale()
{
	SetActorScale3D(ElitePresentationScale);
}

bool AAuraEnemy::IsEliteEnraged() const
{
	if (!bEliteVariant || bDead || !AbilitySystemComponent) return false;
	const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	return MaxHealth > 0.f
		&& AbilitySystemComponent->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute())
			<= MaxHealth * UAuraCombatBalanceData::Get().EliteEnemy.EnrageHealthFraction;
}

float AAuraEnemy::GetEliteAttackIntervalMultiplier() const
{
	if (!bEliteVariant) return 1.f;
	const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
	return Elite.AttackIntervalMultiplier
		* (IsEliteEnraged() ? Elite.EnragedAttackIntervalMultiplier : 1.f);
}

float AAuraEnemy::GetEliteSpecialChanceBonus() const
{
	if (!bEliteVariant) return 0.f;
	const FAuraEliteEnemyBalance& Elite = UAuraCombatBalanceData::Get().EliteEnemy;
	return Elite.SpecialChanceBonus
		+ (IsEliteEnraged() ? Elite.EnragedSpecialChanceBonus : 0.f);
}

void AAuraEnemy::BeginPlay()
{
	Super::BeginPlay();
	// The direct-click FireBolt trace must hit this enemy, not the floor behind
	// it. Keep the existing capsule size and projectile collision unchanged.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	if (bEliteVariant) SetActorScale3D(ElitePresentationScale);
	if (EnemyWeaponMesh && Weapon && GetMesh())
	{
		Weapon->SetSkeletalMeshAsset(EnemyWeaponMesh);
		Weapon->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			EnemyWeaponSocket);
		Weapon->SetRelativeTransform(EnemyWeaponRelativeTransform);
		Weapon->SetVisibility(true, true);
	}
	else if (Weapon)
	{
		Weapon->SetVisibility(false, true);
	}
	ApplyCombatBalance();
	if (HasAuthority()) CurrentPoise = GetPoiseMax();
	PlayAnimation(IdleAnimation, true);
	if (AbilitySystemComponent)
	{
		if (HasAuthority())
		{
			AbilitySystemComponent->SetNumericAttributeBase(
				UAuraAttributeSet::GetMaxHealthAttribute(), StartingHealth);
			AbilitySystemComponent->SetNumericAttributeBase(
				UAuraAttributeSet::GetHealthAttribute(), StartingHealth);
		}
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UAuraAttributeSet::GetHealthAttribute())
			.AddUObject(this, &AAuraEnemy::OnHealthChanged);
		AbilitySystemComponent->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")), EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &AAuraEnemy::OnStunTagChanged);
		AbilitySystemComponent->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(TEXT("State.Slowed")), EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &AAuraEnemy::OnSlowTagChanged);
		if (HasAuthority())
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(
				IsBossEnemy() ? UAuraBossAbility::StaticClass()
					: (IsRangedCombatant() ? UAuraShamanBoltAbility::StaticClass()
						: UAuraMeleeAbility::StaticClass()), 1));
		}
	}
	if (IsBossEnemy())
	{
		GetCharacterMovement()->MaxWalkSpeed = BossPhaseOneWalkSpeed;
		RefreshBossPhasePresentation();
		RefreshBossWardPresentation();
	}
	BeginSpawnPresentation();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Enemy ready. Name=%s DisplayName=%s Archetype=%d Health=%.1f CombatStyle=%s Boss=%s Elite=%s AIController=%s Balance=%s"),
		*GetName(), *EnemyDisplayName, static_cast<int32>(EnemyArchetype), AttributeSet->GetHealth(),
		IsRangedCombatant() ? TEXT("Ranged") : TEXT("Melee"),
		IsBossEnemy() ? TEXT("true") : TEXT("false"),
		bEliteVariant ? TEXT("true") : TEXT("false"), *GetNameSafe(GetController()),
		*UAuraCombatBalanceData::Get().BalanceVersion.ToString());
}

void AAuraEnemy::ApplyAbyssHealthBonus(const float Multiplier)
{
	if (!HasAuthority() || !AbilitySystemComponent || bDead) return;
	const float NewMax = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute()) * FMath::Clamp(Multiplier, 1.f, 2.f);
	AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), NewMax);
	AbilitySystemComponent->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), NewMax);
	StartingHealth = NewMax;
}

float AAuraEnemy::GetDifficultyDamageMultiplier() const
{
	const AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	const float Difficulty = Quest && Quest->IsAbyssTrialActive()
		? UAuraCombatBalanceData::Get().AbyssTrial.DamageMultiplier : 1.f;
	return Difficulty * (bEliteVariant
		? UAuraCombatBalanceData::Get().EliteEnemy.DamageMultiplier : 1.f)
		* (GetWorld() && GetWorld()->GetTimeSeconds() < ComboWeakenUntil
		? 1.f - UAuraCombatBalanceData::Get().Combo.FractureBossDamageReduction : 1.f);
}

bool AAuraEnemy::AreEnemySkillEffectsConfigured() const
{
	return WindCastEffect && SporeCastEffect && LightningCastEffect && FireCastEffect
		&& WindImpactEffect && MagicBurstEffect && SporeFieldEffect
		&& ShamanGroundBurstEffect && LightningReleaseEffect
		&& FireReleaseEffect && SoulReleaseEffect;
}

bool AAuraEnemy::TryApplyComboStagger(UAbilitySystemComponent* SourceASC,
	const float Duration, const float ImmunitySeconds)
{
	if (!HasAuthority() || bDead || IsBossEnemy() || bStunned || !SourceASC
		|| !AbilitySystemComponent || !GetWorld()
		|| GetWorld()->GetTimeSeconds() < ComboStaggerImmuneUntil) return false;
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		UAuraShockStunEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")), FMath::Max(0.05f, Duration));
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	ComboStaggerImmuneUntil = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, ImmunitySeconds);
	return true;
}

float AAuraEnemy::GetPoiseMax() const
{
	const FAuraEnemyPoiseBalance& Poise = UAuraCombatBalanceData::Get().Poise;
	return FMath::Max(1.f, IsBossEnemy() ? Poise.BossMax
		: (bEliteVariant ? Poise.EliteMax
			: (IsRangedCombatant() ? Poise.ShamanMax : Poise.NormalMax)));
}

bool AAuraEnemy::ApplyPoiseDamage(const float Amount)
{
	if (!ElementalStatus->CanAcceptHardControl()) return false;
	if (!HasAuthority() || !GetWorld() || !AbilitySystemComponent || bDead
		|| bStunned || bPoiseBroken || IsDamageImmune() || Amount <= 0.f) return false;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < PoiseImmuneUntil) return false;
	LastPoiseDamageTime = Now;
	CurrentPoise = FMath::Max(0.f, CurrentPoise - Amount);
	if (CurrentPoise > KINDA_SMALL_NUMBER)
	{
		ForceNetUpdate();
		return false;
	}

	const FAuraEnemyPoiseBalance& Poise = UAuraCombatBalanceData::Get().Poise;
	const float Duration = IsBossEnemy() ? Poise.BossStaggerDuration
		: (bEliteVariant ? Poise.EliteStaggerDuration : Poise.NormalStaggerDuration);
	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(
		UAuraShockStunEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		CurrentPoise = GetPoiseMax();
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Duration")), Duration);
	bPoiseBroken = true;
	PoiseImmuneUntil = Now + Duration + Poise.BreakImmunity;
	ElementalStatus->NotifyHardControl(Duration);
	GetWorldTimerManager().SetTimer(PoiseBreakTimer, this,
		&AAuraEnemy::FinishPoiseBreak, Duration, false);
	AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Enemy poise broken. Name=%s Max=%.1f Stagger=%.2f Immunity=%.2f"),
		*GetName(), GetPoiseMax(), Duration, Poise.BreakImmunity);
	return true;
}

float AAuraEnemy::GetPoiseBreakRemaining() const
{
	return FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(PoiseBreakTimer));
}

void AAuraEnemy::FinishPoiseBreak()
{
	if (!HasAuthority() || bDead) return;
	bPoiseBroken = false;
	CurrentPoise = GetPoiseMax();
	LastPoiseDamageTime = GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
}

void AAuraEnemy::UpdatePoise(const float DeltaSeconds)
{
	if (!HasAuthority() || bDead || bPoiseBroken || !GetWorld()) return;
	const FAuraEnemyPoiseBalance& Poise = UAuraCombatBalanceData::Get().Poise;
	if (GetWorld()->GetTimeSeconds() < LastPoiseDamageTime + Poise.RegenDelay) return;
	CurrentPoise = FMath::Min(GetPoiseMax(),
		CurrentPoise + Poise.RegenPerSecond * DeltaSeconds);
}

bool AAuraEnemy::TryApplyComboWeaken(const float Duration, const float CooldownSeconds)
{
	if (!HasAuthority() || bDead || !IsBossEnemy() || !GetWorld()
		|| GetWorld()->GetTimeSeconds() < ComboWeakenReadyAt) return false;
	ComboWeakenUntil = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, Duration);
	ComboWeakenReadyAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, CooldownSeconds);
	return true;
}

float AAuraEnemy::GetMeleeEngagementRange() const
{
	const FAuraMeleeEnemyBalance& Melee = UAuraCombatBalanceData::Get().MeleeEnemy;
	switch (EnemyArchetype)
	{
	case EAuraEnemyArchetype::GoblinSpearman:
		return Melee.GoblinLungeEngageRange;
	case EAuraEnemyArchetype::CaveShroom:
		return FMath::Max(Melee.ShroomBashRange, 190.f);
	case EAuraEnemyArchetype::Ghoul:
	default:
		return Melee.HitRange;
	}
}

void AAuraEnemy::ApplyCombatBalance()
{
	const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
	if (IsBossEnemy())
	{
		StartingHealth = Balance.Boss.MaxHealth;
		KillExperienceReward = Balance.Boss.KillExperienceReward;
		BossPhaseTwoHealthThreshold = Balance.Boss.PhaseTwoHealthThreshold;
		BossPhaseTransitionDuration = Balance.Boss.PhaseTransitionDuration;
		BossPhaseOneWalkSpeed = Balance.Boss.PhaseOneWalkSpeed;
		BossPhaseTwoWalkSpeed = Balance.Boss.PhaseTwoWalkSpeed;
		BossMeleeRange = Balance.Boss.MeleeRange;
		BossTailSpinRadius = Balance.Boss.TailSpinRadius;
		BossThrowRange = Balance.Boss.ThrowRange;
		BossThrowWarningRadius = Balance.Boss.ThrowWarningRadius;
		GetCharacterMovement()->MaxWalkSpeed = BossPhaseOneWalkSpeed;
		if (HasAuthority() && AbilitySystemComponent)
		{
			AbilitySystemComponent->SetNumericAttributeBase(
				UAuraAttributeSet::GetArmorAttribute(), Balance.Boss.Armor);
		}
		return;
	}

	if (IsRangedCombatant())
	{
		StartingHealth = Balance.Shaman.MaxHealth;
		KillExperienceReward = Balance.Shaman.KillExperienceReward;
		RangedRetreatDistance = Balance.Shaman.RetreatDistance;
		RangedPreferredDistance = Balance.Shaman.PreferredDistance;
		RangedAttackRange = Balance.Shaman.AttackRange;
		GetCharacterMovement()->MaxWalkSpeed = Balance.Shaman.WalkSpeed;
	}
	else
	{
		StartingHealth = Balance.MeleeEnemy.MaxHealth;
		KillExperienceReward = Balance.MeleeEnemy.KillExperienceReward;
		GetCharacterMovement()->MaxWalkSpeed = Balance.MeleeEnemy.WalkSpeed;
	}
	if (bEliteVariant)
	{
		StartingHealth *= Balance.EliteEnemy.MaxHealthMultiplier;
		KillExperienceReward = FMath::Max(KillExperienceReward,
			Balance.EliteEnemy.KillExperienceReward);
		GetCharacterMovement()->MaxWalkSpeed *= Balance.EliteEnemy.WalkSpeedMultiplier;
	}
}

void AAuraEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraEnemy, bUseSpawnRisePresentation);
	DOREPLIFETIME(AAuraEnemy, bEliteVariant);
	DOREPLIFETIME(AAuraEnemy, CurrentPoise);
	DOREPLIFETIME(AAuraEnemy, bPoiseBroken);
	DOREPLIFETIME(AAuraEnemy, ElitePresentationScale);
	DOREPLIFETIME(AAuraEnemy, MeleeActionState);
	DOREPLIFETIME(AAuraEnemy, EliteSporeField);
	DOREPLIFETIME(AAuraEnemy, RangedCastTargetLocation);
	DOREPLIFETIME(AAuraEnemy, bRangedCasting);
	DOREPLIFETIME(AAuraEnemy, bRangedVolleyCasting);
	DOREPLIFETIME(AAuraEnemy, bRangedGroundBurstCasting);
	DOREPLIFETIME(AAuraEnemy, BossPhase);
	DOREPLIFETIME(AAuraEnemy, BossActionState);
	DOREPLIFETIME(AAuraEnemy, BossWardHealth);
	DOREPLIFETIME(AAuraEnemy, BossWardCapacity);
}

void AAuraEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bSpawnPresentationActive) UpdateSpawnPresentation(DeltaSeconds);
	if (HasAuthority()) UpdateEliteSporeField();
	if (HasAuthority()) UpdatePoise(DeltaSeconds);
	if (bDead || IsActionLocked()) return;
	if (GetWorld() && GetWorld()->GetTimeSeconds() < HitReactVisualUntil) return;

	const bool bMoving = GetVelocity().SizeSquared2D() > FMath::Square(5.f);
	PlayAnimation(bMoving && WalkAnimation ? WalkAnimation : IdleAnimation, true);
}

float AAuraEnemy::GetSpawnPresentationProgress() const
{
	return bSpawnPresentationActive
		? FMath::Clamp(SpawnPresentationElapsed
			/ FMath::Max(0.01f, SpawnPresentationDuration), 0.f, 1.f)
		: 1.f;
}

void AAuraEnemy::BeginSpawnPresentation()
{
	if (!bUseSpawnRisePresentation || !GetMesh()
		|| SpawnPresentationDuration <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	SpawnPresentationTargetMeshLocation = GetMesh()->GetRelativeLocation();
	const float MeshWorldHeight = FMath::Max(1.f, GetMesh()->Bounds.BoxExtent.Z * 2.f);
	const float MinimumDepth = FMath::Max(0.f, SpawnRiseMinimumDepth);
	const float MaximumDepth = FMath::Max(MinimumDepth, SpawnRiseMaximumDepth);
	SpawnPresentationWorldDepth = FMath::Clamp(
		MeshWorldHeight * 0.9f, MinimumDepth, MaximumDepth);
	const float ActorScaleZ = FMath::Max(0.01f, FMath::Abs(GetActorScale3D().Z));
	const float LocalRiseDepth = SpawnPresentationWorldDepth / ActorScaleZ;
	SpawnPresentationStartMeshLocation = SpawnPresentationTargetMeshLocation
		- FVector(0.f, 0.f, LocalRiseDepth);
	SpawnPresentationElapsed = 0.f;
	bSpawnPresentationActive = true;
	SpawnPresentationPreviousTickInterval = PrimaryActorTick.TickInterval;
	SetActorTickInterval(0.f);
	GetMesh()->SetRelativeLocation(SpawnPresentationStartMeshLocation);

	UE_LOG(LogAuraEnemy, Display,
		TEXT("Enemy spawn rise started. Name=%s Duration=%.2f Depth=%.1f StartZ=%.1f TargetZ=%.1f"),
		*GetName(), SpawnPresentationDuration, SpawnPresentationWorldDepth,
		SpawnPresentationStartMeshLocation.Z, SpawnPresentationTargetMeshLocation.Z);
}

void AAuraEnemy::UpdateSpawnPresentation(const float DeltaSeconds)
{
	if (!bSpawnPresentationActive || !GetMesh()) return;
	SpawnPresentationElapsed += FMath::Max(0.f, DeltaSeconds);
	const float Alpha = FMath::Clamp(SpawnPresentationElapsed
		/ FMath::Max(0.01f, SpawnPresentationDuration), 0.f, 1.f);
	const float EasedAlpha = FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.25f);
	GetMesh()->SetRelativeLocation(FMath::Lerp(
		SpawnPresentationStartMeshLocation,
		SpawnPresentationTargetMeshLocation,
		EasedAlpha));
	if (Alpha >= 1.f) FinishSpawnPresentation(false);
}

void AAuraEnemy::FinishSpawnPresentation(const bool bInterrupted)
{
	if (!bSpawnPresentationActive) return;
	if (GetMesh()) GetMesh()->SetRelativeLocation(SpawnPresentationTargetMeshLocation);
	SpawnPresentationElapsed = SpawnPresentationDuration;
	bSpawnPresentationActive = false;
	SetActorTickInterval(SpawnPresentationPreviousTickInterval);
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Enemy spawn rise completed. Name=%s Interrupted=%s FinalZ=%.1f"),
		*GetName(), bInterrupted ? TEXT("true") : TEXT("false"),
		GetMesh() ? GetMesh()->GetRelativeLocation().Z : 0.f);
}

void AAuraEnemy::PlayMeleeAttackAnimation()
{
	if (!bDead && !bStunned)
	{
		PlayActionAnimation(AttackAnimation);
	}
}

FString AAuraEnemy::GetMeleeActionLabel() const
{
	switch (MeleeActionState.Action)
	{
	case EAuraMeleeAction::GhoulFrenzy:
		return TEXT("狂暴连击");
	case EAuraMeleeAction::GoblinJab:
		return TEXT("长矛戳刺");
	case EAuraMeleeAction::GoblinLunge:
		return TEXT("长矛突进");
	case EAuraMeleeAction::EliteGoblinSweep:
		return TEXT("破阵横扫");
	case EAuraMeleeAction::ShroomSporeBurst:
		return TEXT("孢子爆发");
	case EAuraMeleeAction::EliteShroomSeed:
		return TEXT("播撒菌毯");
	default:
		return FString();
	}
}

void AAuraEnemy::BeginMeleeAction(const EAuraMeleeAction NewAction,
	const FVector& TelegraphLocation, const float TelegraphRadius,
	const float TelegraphDuration)
{
	if (!HasAuthority() || IsRangedCombatant() || IsBossEnemy() || bDead || bStunned) return;

	MeleeActionState.Action = NewAction;
	MeleeActionState.TelegraphLocation = TelegraphLocation;
	MeleeActionState.TelegraphRadius = FMath::Max(0.f, TelegraphRadius);
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	MeleeActionState.TelegraphStartServerTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
	MeleeActionState.TelegraphDuration = FMath::Max(0.05f,
		TelegraphDuration > KINDA_SMALL_NUMBER ? TelegraphDuration : 0.8f);
	PlayMeleeAttackAnimation();
	RefreshMeleeTelegraphPresentation();
	RefreshSkillCastPresentation();
	ForceNetUpdate();
}

float AAuraEnemy::GetMeleeTelegraphProgress() const
{
	if (!GetWorld() || MeleeActionState.Action == EAuraMeleeAction::None
		|| MeleeActionState.TelegraphDuration <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return FMath::Clamp((ServerTime - MeleeActionState.TelegraphStartServerTime)
		/ MeleeActionState.TelegraphDuration, 0.f, 1.f);
}

void AAuraEnemy::ClearMeleeTelegraph()
{
	if (MeleeActionState.Action == EAuraMeleeAction::None) return;
	MeleeActionState.TelegraphRadius = 0.f;
	RefreshMeleeTelegraphPresentation();
	ClearSkillCastPresentation();
	if (HasAuthority()) ForceNetUpdate();
}

void AAuraEnemy::EndMeleeAction()
{
	if (MeleeActionState.Action == EAuraMeleeAction::None
		&& !ActiveMeleeTelegraph && !ActiveSkillCastEffect) return;
	MeleeActionState = FAuraMeleeActionState();
	RefreshMeleeTelegraphPresentation();
	RefreshSkillCastPresentation();
	if (HasAuthority()) ForceNetUpdate();
}

void AAuraEnemy::OnRep_MeleeActionState(const FAuraMeleeActionState PreviousState)
{
	const bool bActionChanged = MeleeActionState.Action != PreviousState.Action;
	if (MeleeActionState.Action != EAuraMeleeAction::None
		&& bActionChanged && !bDead && !bStunned)
	{
		PlayMeleeAttackAnimation();
	}
	RefreshMeleeTelegraphPresentation();
	if (!bActionChanged && PreviousState.TelegraphRadius > 0.f
		&& MeleeActionState.TelegraphRadius <= 0.f)
	{
		ClearSkillCastPresentation();
	}
	if (bActionChanged) RefreshSkillCastPresentation();
}

void AAuraEnemy::RefreshMeleeTelegraphPresentation()
{
	if (ActiveMeleeTelegraph)
	{
		ActiveMeleeTelegraph->DeactivateImmediate();
		ActiveMeleeTelegraph->DestroyComponent();
		ActiveMeleeTelegraph = nullptr;
	}
	if (MeleeActionState.Action == EAuraMeleeAction::None
		|| MeleeActionState.TelegraphRadius <= 0.f || !MeleeTelegraphEffect || !GetWorld())
	{
		return;
	}
	if (MeleeActionState.Action == EAuraMeleeAction::GoblinLunge
		|| MeleeActionState.Action == EAuraMeleeAction::GoblinJab
		|| MeleeActionState.Action == EAuraMeleeAction::EliteGoblinSweep
		|| MeleeActionState.Action == EAuraMeleeAction::ShroomSporeBurst
		|| MeleeActionState.Action == EAuraMeleeAction::EliteShroomSeed)
	{
		// The HUD owns the exact directional/texture warning for these actions.
		return;
	}

	const float VisualScale = FMath::Clamp(MeleeActionState.TelegraphRadius / 125.f, 0.55f, 2.8f);
	ActiveMeleeTelegraph = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, MeleeTelegraphEffect, MeleeActionState.TelegraphLocation,
		FRotator::ZeroRotator, FVector(VisualScale), false, true);
}

void AAuraEnemy::PlayMeleeImpactEffect(const FVector& ImpactLocation,
	const float EffectRadius, const EAuraMeleeAction ActionOverride)
{
	if (!HasAuthority()) return;
	const EAuraMeleeAction ResolvedAction = ActionOverride != EAuraMeleeAction::None
		? ActionOverride : MeleeActionState.Action;
	MulticastPlayMeleeImpactEffect(ImpactLocation, EffectRadius, ResolvedAction);
}

void AAuraEnemy::MulticastPlayMeleeImpactEffect_Implementation(
	const FVector_NetQuantize ImpactLocation, const float EffectRadius,
	const EAuraMeleeAction Action)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld()) return;

	UNiagaraSystem* SelectedEffect = MeleeImpactEffect;
	float ReferenceRadius = 180.f;
	float MaximumScale = 1.8f;
	switch (Action)
	{
	case EAuraMeleeAction::GhoulFrenzy:
		SelectedEffect = WindImpactEffect ? WindImpactEffect.Get() : MeleeImpactEffect.Get();
		ReferenceRadius = 620.f;
		MaximumScale = 0.78f;
		break;
	case EAuraMeleeAction::GoblinJab:
	case EAuraMeleeAction::GoblinLunge:
	case EAuraMeleeAction::EliteGoblinSweep:
	case EAuraMeleeAction::ShroomBash:
	case EAuraMeleeAction::ShroomSporeBurst:
	case EAuraMeleeAction::EliteShroomSeed:
		// These actions use the authored Texture2D combat cues or the persistent
		// elite Notes field. Do not layer the old generic impact over them.
		SelectedEffect = nullptr;
		break;
	default:
		break;
	}
	if (!SelectedEffect) return;
	const bool bUsesAuthoredSkillEffect = SelectedEffect != MeleeImpactEffect;
	float VisualScale = FMath::Clamp(EffectRadius / ReferenceRadius,
		bUsesAuthoredSkillEffect ? 0.24f : 0.38f, MaximumScale);
	if (bEliteVariant) VisualScale *= 1.05f;
	SpawnTransientSkillEffect(SelectedEffect, ImpactLocation,
		FRotator(0.f, GetActorRotation().Yaw, 0.f), FVector(VisualScale), 0.55f);
}

void AAuraEnemy::ActivateEliteSporeField(const FVector& Location, const float Radius,
	const float ArmDelay, const float Duration)
{
	if (!HasAuthority() || bDead || !bEliteVariant
		|| EnemyArchetype != EAuraEnemyArchetype::CaveShroom || !GetWorld()) return;
	EliteSporeFieldVictims.Reset();
	EliteSporeField.bActive = true;
	EliteSporeField.Location = Location;
	EliteSporeField.Radius = FMath::Max(0.f, Radius);
	EliteSporeField.ArmedServerTime = GetWorld()->GetTimeSeconds()
		+ FMath::Max(0.f, ArmDelay);
	EliteSporeField.ExpireServerTime = EliteSporeField.ArmedServerTime
		+ FMath::Max(0.f, Duration);
	RefreshEliteSporeFieldPresentation();
	PlayMeleeImpactEffect(Location, Radius, EAuraMeleeAction::EliteShroomSeed);
	ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("ELITE_SHROOM_FIELD_SEEDED Enemy=%s Location=%s Radius=%.1f ArmTime=%.2f ExpireTime=%.2f"),
		*GetName(), *Location.ToCompactString(), Radius,
		EliteSporeField.ArmedServerTime, EliteSporeField.ExpireServerTime);
}

void AAuraEnemy::ClearEliteSporeField()
{
	if (!EliteSporeField.bActive) return;
	EliteSporeField = FAuraEliteSporeFieldState();
	EliteSporeFieldVictims.Reset();
	RefreshEliteSporeFieldPresentation();
	if (HasAuthority()) ForceNetUpdate();
}

void AAuraEnemy::OnRep_EliteSporeField()
{
	RefreshEliteSporeFieldPresentation();
}

void AAuraEnemy::UpdateEliteSporeField()
{
	if (!EliteSporeField.bActive || bDead || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= EliteSporeField.ExpireServerTime)
	{
		ClearEliteSporeField();
		return;
	}
	if (Now < EliteSporeField.ArmedServerTime || !AbilitySystemComponent) return;
	const FAuraEliteEnemyBalance& Balance = UAuraCombatBalanceData::Get().EliteEnemy;
	for (TActorIterator<AAuraCharacter> It(GetWorld()); It; ++It)
	{
		AAuraCharacter* Candidate = *It;
		if (!Candidate || Candidate->IsDead()
			|| EliteSporeFieldVictims.Contains(TWeakObjectPtr<AAuraCharacter>(Candidate))
			|| FVector::DistSquared2D(EliteSporeField.Location, Candidate->GetActorLocation())
				> FMath::Square(EliteSporeField.Radius)
			|| FMath::Abs(Candidate->GetActorLocation().Z - EliteSporeField.Location.Z) > 190.f)
		{
			continue;
		}
		UAbilitySystemComponent* TargetASC = Candidate->GetAbilitySystemComponent();
		if (!TargetASC) continue;
		FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
		Context.AddSourceObject(this);
		FGameplayEffectSpecHandle DamageSpec = AbilitySystemComponent->MakeOutgoingSpec(
			UAuraDamageGameplayEffect::StaticClass(), 1.f, Context);
		if (!DamageSpec.IsValid()) continue;
		DamageSpec.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")),
			-Balance.ShroomSeedDamage * GetDifficultyDamageMultiplier());
		TargetASC->ApplyGameplayEffectSpecToSelf(*DamageSpec.Data.Get());
		if (!Candidate->IsDead()) Candidate->ApplySporeExposure(this);
		EliteSporeFieldVictims.Add(TWeakObjectPtr<AAuraCharacter>(Candidate));
		PlayMeleeImpactEffect(EliteSporeField.Location, EliteSporeField.Radius,
			EAuraMeleeAction::EliteShroomSeed);
		UE_LOG(LogAuraEnemy, Display,
			TEXT("ELITE_SHROOM_FIELD_HIT Enemy=%s Target=%s Damage=%.1f"),
			*GetName(), *Candidate->GetName(), Balance.ShroomSeedDamage);
	}
}

void AAuraEnemy::BeginRangedCast(const FVector& TelegraphLocation, const bool bIsVolley,
	const bool bIsGroundBurst)
{
	if (bDead || bStunned || !IsRangedCombatant()) return;
	RangedCastTargetLocation = TelegraphLocation;
	bRangedGroundBurstCasting = bIsGroundBurst;
	bRangedVolleyCasting = bIsVolley && !bIsGroundBurst;
	bRangedCasting = true;
	PlayActionAnimation(RangedCastAnimation ? RangedCastAnimation : AttackAnimation);
	RefreshRangedCastPresentation();
	RefreshSkillCastPresentation();
	ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Enemy ranged cast telegraph started. Name=%s Target=%s Volley=%s GroundBurst=%s"),
		*GetName(), *RangedCastTargetLocation.ToCompactString(),
		bRangedVolleyCasting ? TEXT("true") : TEXT("false"),
		bRangedGroundBurstCasting ? TEXT("true") : TEXT("false"));
}

void AAuraEnemy::EndRangedCast()
{
	if (!bRangedCasting && !ActiveRangedCastWarning && !ActiveSkillCastEffect) return;
	bRangedCasting = false;
	bRangedVolleyCasting = false;
	bRangedGroundBurstCasting = false;
	RefreshRangedCastPresentation();
	RefreshSkillCastPresentation();
	ForceNetUpdate();
}

void AAuraEnemy::PlayRangedReleaseEffect(const FVector& ReleaseLocation,
	const FVector& ReleaseDirection, const bool bIsVolley)
{
	if (!HasAuthority()) return;
	MulticastPlayRangedReleaseEffect(ReleaseLocation,
		ReleaseDirection.GetSafeNormal(), bIsVolley);
}

void AAuraEnemy::MulticastPlayRangedReleaseEffect_Implementation(
	const FVector_NetQuantize ReleaseLocation,
	const FVector_NetQuantizeNormal ReleaseDirection, const bool bIsVolley)
{
	if (GetNetMode() == NM_DedicatedServer || !LightningReleaseEffect) return;
	const FVector SafeDirection = FVector(ReleaseDirection).IsNearlyZero()
		? GetActorForwardVector() : FVector(ReleaseDirection);
	SpawnTransientSkillEffect(LightningReleaseEffect, ReleaseLocation,
		SafeDirection.Rotation(), FVector(bIsVolley ? 0.42f : 0.32f), 0.3f);
}

void AAuraEnemy::PlayShamanGroundBurstEffect(
	const FVector& ImpactLocation, const float EffectRadius)
{
	if (!HasAuthority()) return;
	MulticastPlayShamanGroundBurstEffect(ImpactLocation, EffectRadius);
}

void AAuraEnemy::MulticastPlayShamanGroundBurstEffect_Implementation(
	const FVector_NetQuantize ImpactLocation, const float EffectRadius)
{
	if (GetNetMode() == NM_DedicatedServer || !ShamanGroundBurstEffect) return;
	const float VisualScale = FMath::Clamp(EffectRadius / 440.f, 0.38f, 0.82f);
	SpawnTransientSkillEffect(ShamanGroundBurstEffect,
		ImpactLocation + FVector(0.f, 0.f, 4.f), FRotator::ZeroRotator,
		FVector(VisualScale), 0.5f, 0.45f);
}

void AAuraEnemy::PlayMeleeTextureCue(const EAuraMeleeAction Action,
	const FVector& CueLocation, const FVector& AttackDirection, const float Duration)
{
	if (!HasAuthority() || Action == EAuraMeleeAction::None) return;
	MulticastPlayMeleeTextureCue(Action, CueLocation,
		AttackDirection.GetSafeNormal2D(), FMath::Clamp(Duration, 0.12f, 1.f));
}

void AAuraEnemy::MulticastPlayMeleeTextureCue_Implementation(
	const EAuraMeleeAction Action, const FVector_NetQuantize CueLocation,
	const FVector_NetQuantizeNormal AttackDirection, const float Duration)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld()) return;
	LocalMeleeTextureCueAction = Action;
	LocalMeleeTextureCueLocation = CueLocation;
	LocalMeleeTextureCueDirection = FVector(AttackDirection).GetSafeNormal2D();
	if (LocalMeleeTextureCueDirection.IsNearlyZero())
		LocalMeleeTextureCueDirection = GetActorForwardVector().GetSafeNormal2D();
	LocalMeleeTextureCueStartTime = GetWorld()->GetTimeSeconds();
	LocalMeleeTextureCueEndTime = LocalMeleeTextureCueStartTime
		+ FMath::Clamp(Duration, 0.12f, 1.f);
	++LocalSkillVFXSpawnCount;
}

bool AAuraEnemy::GetLocalMeleeTextureCue(EAuraMeleeAction& OutAction,
	FVector& OutLocation, FVector& OutDirection, float& OutProgress) const
{
	if (!GetWorld() || LocalMeleeTextureCueAction == EAuraMeleeAction::None
		|| LocalMeleeTextureCueEndTime <= LocalMeleeTextureCueStartTime)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < LocalMeleeTextureCueStartTime || Now >= LocalMeleeTextureCueEndTime)
	{
		return false;
	}
	OutAction = LocalMeleeTextureCueAction;
	OutLocation = LocalMeleeTextureCueLocation;
	OutDirection = LocalMeleeTextureCueDirection;
	OutProgress = FMath::Clamp((Now - LocalMeleeTextureCueStartTime)
		/ (LocalMeleeTextureCueEndTime - LocalMeleeTextureCueStartTime), 0.f, 1.f);
	return true;
}

bool AAuraEnemy::InterruptTelegraphedAttack()
{
	if (!HasAuthority() || bDead || bStunned || IsBossTransitioning()) return false;
	const bool bTelegraphing = bRangedCasting
		|| MeleeActionState.Action != EAuraMeleeAction::None
		|| BossActionState.Action != EAuraBossAction::None;
	if (!bTelegraphing) return false;
	if (AbilitySystemComponent) AbilitySystemComponent->CancelAllAbilities();
	EndMeleeAction();
	EndRangedCast();
	EndBossAction();
	GetWorldTimerManager().ClearTimer(ActionAnimationTimer);
	bActionLocked = false;
	ResumeLocomotionAnimation();
	ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display, TEXT("Enemy telegraph interrupted without stun. Name=%s"), *GetName());
	return true;
}

void AAuraEnemy::OnRep_RangedCasting()
{
	if (bRangedCasting && !bDead && !bStunned)
	{
		PlayActionAnimation(RangedCastAnimation ? RangedCastAnimation : AttackAnimation);
	}
	RefreshRangedCastPresentation();
	// Pattern flags replicate independently from the active bit. Always rebuild so
	// a late GroundBurst/Volley update cannot leave the previous cast aura visible.
	RefreshSkillCastPresentation();
}

FString AAuraEnemy::GetBossPhaseLabel() const
{
	switch (BossPhase)
	{
	case EAuraBossPhase::PhaseOne: return TEXT("第一阶段");
	case EAuraBossPhase::Transitioning: return TEXT("觉醒中");
	case EAuraBossPhase::PhaseTwo: return TEXT("第二阶段");
	default: return TEXT("第一阶段");
	}
}

FString AAuraEnemy::GetBossActionLabel() const
{
	switch (BossActionState.Action)
	{
	case EAuraBossAction::ClawLeft:
		return TEXT("左爪攻击");
	case EAuraBossAction::ClawRight:
		return TEXT("右爪攻击");
	case EAuraBossAction::TailSpin:
		return TEXT("甩尾横扫");
	case EAuraBossAction::HellfireThrow:
		return TEXT("地狱火投掷");
	case EAuraBossAction::SummonMinions:
		return TEXT("深渊召唤");
	case EAuraBossAction::DemonicCharge:
		return TEXT("恶魔冲锋");
	case EAuraBossAction::DemonicWard:
		return TEXT("深渊护盾蓄能");
	case EAuraBossAction::InfernalBarrage:
		return TEXT("炼狱火弹幕");
	default:
		return FString();
	}
}

void AAuraEnemy::ActivateBossWard()
{
	if (!HasAuthority() || !IsBossEnemy() || bDead || IsBossTransitioning()
		|| BossPhase != EAuraBossPhase::PhaseTwo || HasBossWard()
		|| !AbilitySystemComponent || !GetWorld()) return;

	const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	BossWardCapacity = FMath::Max(1.f,
		MaxHealth * UAuraCombatBalanceData::Get().Boss.WardHealthFraction);
	BossWardHealth = BossWardCapacity;
	GetWorldTimerManager().SetTimer(BossWardExpiryTimer, this,
		&AAuraEnemy::ExpireBossWard,
		UAuraCombatBalanceData::Get().Boss.WardDuration, false);
	RefreshBossWardPresentation();
	ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Boss ward activated. Boss=%s Absorb=%.1f Duration=%.2f"),
		*GetName(), BossWardHealth, UAuraCombatBalanceData::Get().Boss.WardDuration);
}

float AAuraEnemy::AbsorbBossWardDamage(const float MitigatedDamage)
{
	const float SafeDamage = FMath::Max(0.f, MitigatedDamage);
	if (!HasAuthority() || !HasBossWard() || SafeDamage <= 0.f) return SafeDamage;
	const float Absorbed = FMath::Min(BossWardHealth, SafeDamage);
	BossWardHealth = FMath::Max(0.f, BossWardHealth - Absorbed);
	if (!HasBossWard()) ClearBossWard(true);
	else ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Boss ward absorbed damage. Boss=%s Absorbed=%.1f RemainingWard=%.1f HealthOverflow=%.1f"),
		*GetName(), Absorbed, BossWardHealth, SafeDamage - Absorbed);
	return SafeDamage - Absorbed;
}

void AAuraEnemy::ClearBossWard(const bool bBroken)
{
	if (!HasAuthority()) return;
	const bool bWasActive = BossWardCapacity > 0.f;
	GetWorldTimerManager().ClearTimer(BossWardExpiryTimer);
	BossWardHealth = 0.f;
	BossWardCapacity = 0.f;
	RefreshBossWardPresentation();
	ForceNetUpdate();
	if (bWasActive)
	{
		UE_LOG(LogAuraEnemy, Display, TEXT("Boss ward ended. Boss=%s Reason=%s"),
			*GetName(), bBroken ? TEXT("Broken") : TEXT("ExpiredOrCleared"));
	}
}

void AAuraEnemy::ExpireBossWard()
{
	ClearBossWard(false);
}

void AAuraEnemy::OnRep_BossWardHealth()
{
	RefreshBossWardPresentation();
}

void AAuraEnemy::RefreshBossWardPresentation()
{
	if (!BossWardMesh) return;
	const bool bVisible = IsBossEnemy() && HasBossWard() && !bDead;
	if (bVisible && GetCapsuleComponent())
	{
		const float Radius = FMath::Max(
			GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.65f,
			GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 1.2f);
		// The engine sphere has a 50 cm radius. World scale keeps the shell aligned
		// even when the boss Blueprint or encounter changes actor scale.
		BossWardMesh->SetWorldScale3D(FVector(Radius / 50.f));
		// Character meshes sit below the capsule origin; center the ward on the
		// visible Demon instead of leaving its feet outside a floating shell.
		const float MeshCenterZ = GetMesh()
			? GetMesh()->Bounds.Origin.Z : GetActorLocation().Z;
		BossWardMesh->SetWorldLocation(FVector(
			GetActorLocation().X, GetActorLocation().Y, MeshCenterZ));
	}
	BossWardMesh->SetVisibility(bVisible, true);
}

void AAuraEnemy::BeginBossAction(const EAuraBossAction NewAction,
	const FVector& TelegraphLocation, const float TelegraphRadius)
{
	if (!HasAuthority() || !IsBossEnemy() || bDead || bStunned || IsBossTransitioning()) return;

	BossActionState.Action = NewAction;
	BossActionState.TelegraphLocation = TelegraphLocation;
	BossActionState.TelegraphRadius = FMath::Max(0.f, TelegraphRadius);
	bActionLocked = true;
	PlayBossActionAnimation();
	RefreshBossTelegraphPresentation();
	RefreshSkillCastPresentation();
	ForceNetUpdate();
}

void AAuraEnemy::ClearBossTelegraph()
{
	if (!IsBossEnemy()) return;
	BossActionState.TelegraphRadius = 0.f;
	RefreshBossTelegraphPresentation();
	ClearSkillCastPresentation();
	if (HasAuthority()) ForceNetUpdate();
}

void AAuraEnemy::EndBossAction()
{
	if (!IsBossEnemy()) return;
	BossActionState = FAuraBossActionState();
	RefreshBossTelegraphPresentation();
	RefreshSkillCastPresentation();
	if (!bDead && !bStunned && !IsBossTransitioning())
	{
		bActionLocked = false;
		CurrentAnimation = nullptr;
		ResumeLocomotionAnimation();
	}
	if (HasAuthority()) ForceNetUpdate();
}

void AAuraEnemy::PlayBossActionAnimation()
{
	if (!IsBossEnemy() || BossActionState.Action == EAuraBossAction::None || bDead) return;

	UAnimSequence* ActionAnimation = AttackAnimation;
	switch (BossActionState.Action)
	{
	case EAuraBossAction::ClawLeft:
		ActionAnimation = BossAttackLeftAnimation ? BossAttackLeftAnimation.Get() : AttackAnimation.Get();
		break;
	case EAuraBossAction::ClawRight:
		ActionAnimation = BossAttackRightAnimation ? BossAttackRightAnimation.Get() : AttackAnimation.Get();
		break;
	case EAuraBossAction::TailSpin:
		ActionAnimation = BossTailSpinAnimation ? BossTailSpinAnimation.Get() : AttackAnimation.Get();
		break;
	case EAuraBossAction::HellfireThrow:
	case EAuraBossAction::InfernalBarrage:
		ActionAnimation = BossThrowAnimation ? BossThrowAnimation.Get() : AttackAnimation.Get();
		break;
	case EAuraBossAction::SummonMinions:
		ActionAnimation = BossSummonAnimation
			? BossSummonAnimation.Get()
			: (BossPhaseTransitionAnimation ? BossPhaseTransitionAnimation.Get() : AttackAnimation.Get());
		break;
	case EAuraBossAction::DemonicCharge:
		ActionAnimation = BossChargeAnimation
			? BossChargeAnimation.Get()
			: (BossAttackRightAnimation ? BossAttackRightAnimation.Get() : AttackAnimation.Get());
		break;
	case EAuraBossAction::DemonicWard:
		ActionAnimation = BossSummonAnimation
			? BossSummonAnimation.Get() : BossPhaseTransitionAnimation.Get();
		break;
	default:
		break;
	}

	GetWorldTimerManager().ClearTimer(ActionAnimationTimer);
	bActionLocked = true;
	CurrentAnimation = nullptr;
	PlayAnimation(ActionAnimation, BossActionState.Action == EAuraBossAction::InfernalBarrage);
}

void AAuraEnemy::RefreshBossTelegraphPresentation()
{
	if (ActiveBossTelegraph)
	{
		ActiveBossTelegraph->DeactivateImmediate();
		ActiveBossTelegraph->DestroyComponent();
		ActiveBossTelegraph = nullptr;
	}
	if (!IsBossEnemy() || BossActionState.Action == EAuraBossAction::None
		|| BossActionState.TelegraphRadius <= 0.f || !BossTelegraphEffect || !GetWorld())
	{
		return;
	}
	if (BossActionState.Action == EAuraBossAction::ClawLeft
		|| BossActionState.Action == EAuraBossAction::ClawRight
		|| BossActionState.Action == EAuraBossAction::DemonicCharge
		|| BossActionState.Action == EAuraBossAction::DemonicWard
		|| BossActionState.Action == EAuraBossAction::InfernalBarrage)
	{
		// Directional warnings and the non-damaging ward are drawn by the HUD.
		return;
	}

	const float VisualScale = FMath::Clamp(BossActionState.TelegraphRadius / 125.f, 0.7f, 3.2f);
	ActiveBossTelegraph = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, BossTelegraphEffect, BossActionState.TelegraphLocation,
		FRotator::ZeroRotator, FVector(VisualScale), false, true);
}

void AAuraEnemy::OnRep_BossActionState(const FAuraBossActionState PreviousState)
{
	const bool bActionChanged = BossActionState.Action != PreviousState.Action;
	if (bActionChanged)
	{
		if (BossActionState.Action == EAuraBossAction::None)
		{
			if (!bDead && !bStunned && !IsBossTransitioning())
			{
				bActionLocked = false;
				CurrentAnimation = nullptr;
				ResumeLocomotionAnimation();
			}
		}
		else
		{
			PlayBossActionAnimation();
		}
		RefreshSkillCastPresentation();
	}
	RefreshBossTelegraphPresentation();
	if (!bActionChanged && PreviousState.TelegraphRadius > 0.f
		&& BossActionState.TelegraphRadius <= 0.f)
	{
		ClearSkillCastPresentation();
	}
}

void AAuraEnemy::PlayBossImpactEffect(const FVector& ImpactLocation,
	const float EffectRadius, const EAuraBossAction ActionOverride)
{
	if (!HasAuthority()) return;
	const EAuraBossAction ResolvedAction = ActionOverride != EAuraBossAction::None
		? ActionOverride : BossActionState.Action;
	MulticastPlayBossImpactEffect(ImpactLocation, EffectRadius, ResolvedAction);
}

void AAuraEnemy::MulticastPlayBossImpactEffect_Implementation(
	const FVector_NetQuantize ImpactLocation, const float EffectRadius,
	const EAuraBossAction Action)
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld()) return;
	UNiagaraSystem* SelectedEffect = BossImpactEffect;
	float ReferenceRadius = 240.f;
	float MinimumScale = 0.7f;
	float MaximumScale = 2.2f;
	switch (Action)
	{
	case EAuraBossAction::ClawLeft:
	case EAuraBossAction::ClawRight:
	case EAuraBossAction::TailSpin:
	case EAuraBossAction::DemonicCharge:
		SelectedEffect = WindImpactEffect ? WindImpactEffect.Get() : BossImpactEffect.Get();
		ReferenceRadius = 650.f;
		MinimumScale = 0.3f;
		MaximumScale = 0.9f;
		break;
	case EAuraBossAction::SummonMinions:
		SelectedEffect = SoulReleaseEffect ? SoulReleaseEffect.Get() : BossImpactEffect.Get();
		ReferenceRadius = 500.f;
		MinimumScale = 0.3f;
		MaximumScale = 0.75f;
		break;
	case EAuraBossAction::DemonicWard:
		SelectedEffect = MagicBurstEffect ? MagicBurstEffect.Get() : BossImpactEffect.Get();
		ReferenceRadius = 700.f;
		MinimumScale = 0.35f;
		MaximumScale = 0.8f;
		break;
	default:
		break;
	}
	if (!SelectedEffect) return;
	const float VisualScale = FMath::Clamp(
		EffectRadius / ReferenceRadius, MinimumScale, MaximumScale);
	SpawnTransientSkillEffect(SelectedEffect, ImpactLocation,
		FRotator(0.f, GetActorRotation().Yaw, 0.f), FVector(VisualScale), 0.65f);
}

void AAuraEnemy::PlayBossProjectileReleaseEffect(const FVector& ReleaseLocation,
	const FVector& ReleaseDirection, const EAuraBossAction Action)
{
	if (!HasAuthority()) return;
	MulticastPlayBossProjectileReleaseEffect(ReleaseLocation,
		ReleaseDirection.GetSafeNormal(), Action);
}

void AAuraEnemy::MulticastPlayBossProjectileReleaseEffect_Implementation(
	const FVector_NetQuantize ReleaseLocation,
	const FVector_NetQuantizeNormal ReleaseDirection, const EAuraBossAction Action)
{
	if (GetNetMode() == NM_DedicatedServer || !FireReleaseEffect) return;
	const FVector SafeDirection = FVector(ReleaseDirection).IsNearlyZero()
		? GetActorForwardVector() : FVector(ReleaseDirection);
	const bool bBarrage = Action == EAuraBossAction::InfernalBarrage;
	const float Scale = bBarrage ? 0.22f : 0.36f;
	SpawnTransientSkillEffect(FireReleaseEffect, ReleaseLocation,
		SafeDirection.Rotation(), FVector(Scale), bBarrage ? 0.12f : 0.32f,
		bBarrage ? 0.08f : 0.4f);
}

void AAuraEnemy::RefreshRangedCastPresentation()
{
	if (ActiveRangedCastWarning)
	{
		ActiveRangedCastWarning->DeactivateImmediate();
		ActiveRangedCastWarning->DestroyComponent();
		ActiveRangedCastWarning = nullptr;
	}
	if (!bRangedCasting || !RangedCastWarningEffect || !GetWorld()) return;
	if (bRangedVolleyCasting || bRangedGroundBurstCasting)
	{
		// The HUD renders volley direction and ground-burst radius accurately.
		return;
	}

	ActiveRangedCastWarning = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, RangedCastWarningEffect, RangedCastTargetLocation,
		FRotator::ZeroRotator, FVector(bRangedVolleyCasting ? 1.05f : 0.75f), false, true);
}

void AAuraEnemy::ClearSkillCastPresentation()
{
	if (!ActiveSkillCastEffect) return;
	ActiveSkillCastEffect->DeactivateImmediate();
	ActiveSkillCastEffect->DestroyComponent();
	ActiveSkillCastEffect = nullptr;
}

void AAuraEnemy::RefreshSkillCastPresentation()
{
	ClearSkillCastPresentation();
	if (GetNetMode() == NM_DedicatedServer || !GetWorld() || bDead || bStunned
		|| !GetRootComponent())
	{
		return;
	}

	UNiagaraSystem* SelectedEffect = nullptr;
	float VisualScale = 0.3f;
	if (IsBossEnemy() && BossActionState.Action != EAuraBossAction::None)
	{
		VisualScale = 0.48f;
		switch (BossActionState.Action)
		{
		case EAuraBossAction::ClawLeft:
		case EAuraBossAction::ClawRight:
			SelectedEffect = FireReleaseEffect;
			VisualScale = 0.34f;
			break;
		case EAuraBossAction::TailSpin:
		case EAuraBossAction::HellfireThrow:
		case EAuraBossAction::InfernalBarrage:
			SelectedEffect = FireCastEffect;
			break;
		case EAuraBossAction::SummonMinions:
			SelectedEffect = SporeCastEffect;
			break;
		case EAuraBossAction::DemonicCharge:
			SelectedEffect = WindCastEffect;
			break;
		case EAuraBossAction::DemonicWard:
			SelectedEffect = SporeFieldEffect;
			VisualScale = 0.62f;
			break;
		default:
			break;
		}
	}
	else if (bRangedCasting)
	{
		SelectedEffect = bRangedGroundBurstCasting
			? ShamanGroundBurstEffect.Get() : LightningCastEffect.Get();
		VisualScale = bRangedGroundBurstCasting
			? 0.38f : (bRangedVolleyCasting ? 0.42f : 0.34f);
	}
	else
	{
		switch (MeleeActionState.Action)
		{
		case EAuraMeleeAction::GhoulFrenzy:
			SelectedEffect = WindCastEffect;
			break;
		case EAuraMeleeAction::GoblinJab:
		case EAuraMeleeAction::GoblinLunge:
		case EAuraMeleeAction::EliteGoblinSweep:
		case EAuraMeleeAction::ShroomBash:
		case EAuraMeleeAction::ShroomSporeBurst:
		case EAuraMeleeAction::EliteShroomSeed:
			// Their bespoke texture/Notes presentation is driven by the HUD or
			// replicated hazard state, so the previous feet aura is intentionally absent.
			break;
		default:
			break;
		}
		if (bEliteVariant) VisualScale *= 1.08f;
	}
	if (!SelectedEffect) return;

	const float FeetOffset = GetCapsuleComponent()
		? -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 4.f : 0.f;
	ActiveSkillCastEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
		SelectedEffect, GetRootComponent(), NAME_None, FVector(0.f, 0.f, FeetOffset),
		FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
	if (ActiveSkillCastEffect)
	{
		ActiveSkillCastEffect->SetRelativeScale3D(FVector(VisualScale));
		ActiveSkillCastEffect->SetAutoDestroy(false);
		++LocalSkillVFXSpawnCount;
	}
}

void AAuraEnemy::RefreshEliteSporeFieldPresentation()
{
	if (ActiveEliteSporeFieldEffect)
	{
		ActiveEliteSporeFieldEffect->DeactivateImmediate();
		ActiveEliteSporeFieldEffect->DestroyComponent();
		ActiveEliteSporeFieldEffect = nullptr;
	}
	if (GetNetMode() == NM_DedicatedServer || !GetWorld() || bDead
		|| !EliteSporeField.bActive || !SporeFieldEffect) return;

	const float VisualScale = FMath::Clamp(
		EliteSporeField.Radius / 420.f, 0.62f, 1.55f);
	ActiveEliteSporeFieldEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, SporeFieldEffect, EliteSporeField.Location + FVector(0.f, 0.f, 3.f),
		FRotator::ZeroRotator, FVector(VisualScale), false, true);
	if (ActiveEliteSporeFieldEffect)
	{
		ActiveEliteSporeFieldEffect->SetAutoDestroy(false);
		++LocalSkillVFXSpawnCount;
	}
}

void AAuraEnemy::SpawnTransientSkillEffect(UNiagaraSystem* Effect,
	const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const float MaxLifetime, const float FadeOutLifetime)
{
	if (GetNetMode() == NM_DedicatedServer || !Effect || !GetWorld()) return;
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this, Effect, Location, Rotation, Scale, false, true);
	if (Component) ++LocalSkillVFXSpawnCount;
	ScheduleTransientSkillEffectCleanup(Component, MaxLifetime, FadeOutLifetime);
}

void AAuraEnemy::ScheduleTransientSkillEffectCleanup(
	UNiagaraComponent* Component, const float MaxLifetime,
	const float FadeOutLifetime)
{
	if (!Component || !GetWorld() || MaxLifetime <= 0.f) return;
	const TWeakObjectPtr<UNiagaraComponent> WeakComponent(Component);
	FTimerDelegate DeactivateDelegate = FTimerDelegate::CreateLambda([WeakComponent]()
	{
		UNiagaraComponent* ValidComponent = WeakComponent.Get();
		if (!IsValid(ValidComponent)) return;
		ValidComponent->Deactivate();
	});
	FTimerHandle DeactivateTimer;
	GetWorldTimerManager().SetTimer(
		DeactivateTimer, MoveTemp(DeactivateDelegate), MaxLifetime, false);

	FTimerDelegate DestroyDelegate = FTimerDelegate::CreateLambda([WeakComponent]()
	{
		UNiagaraComponent* ValidComponent = WeakComponent.Get();
		if (!IsValid(ValidComponent)) return;
		ValidComponent->DestroyComponent();
	});
	FTimerHandle DestroyTimer;
	GetWorldTimerManager().SetTimer(
		DestroyTimer, MoveTemp(DestroyDelegate),
		MaxLifetime + FMath::Max(0.f, FadeOutLifetime), false);
}

void AAuraEnemy::ConfigureAsBossSummon(AAuraEnemy* InSummoner)
{
	if (!HasAuthority() || IsBossEnemy() || !InSummoner || !InSummoner->IsBossEnemy()) return;
	BossSummoner = InSummoner;
	LootProfile = EAuraLootProfileType::None;
	KillExperienceReward = 0;
	ForceNetUpdate();
}

void AAuraEnemy::DismissOwnedBossSummons()
{
	if (!HasAuthority() || !IsBossEnemy() || !GetWorld()) return;

	int32 DismissedCount = 0;
	for (TActorIterator<AAuraEnemy> It(GetWorld()); It; ++It)
	{
		AAuraEnemy* Summon = *It;
		if (!Summon || Summon == this || Summon->GetBossSummoner() != this) continue;
		Summon->Destroy();
		++DismissedCount;
	}
	if (DismissedCount > 0)
	{
		UE_LOG(LogAuraEnemy, Display,
			TEXT("Boss summons dismissed. Boss=%s Count=%d"), *GetName(), DismissedCount);
	}
}

void AAuraEnemy::TryAwardKillRewards(AActor* DamageInstigator)
{
	if (!HasAuthority() || bKillRewardsAwarded) return;
	bKillRewardsAwarded = true;
	if (UAuraLootSubsystem* LootSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UAuraLootSubsystem>() : nullptr)
	{
		LootSubsystem->SpawnPersonalLootForEnemy(*this);
	}
	const FAuraGrowthEconomyBalance& Economy = UAuraCombatBalanceData::Get().GrowthEconomy;
	const int32 BaseSoulReward = BossSummoner ? 0
		: (IsBossEnemy() ? Economy.BossKillSouls
			: (LootProfile == EAuraLootProfileType::EliteEnemy
				|| LootProfile == EAuraLootProfileType::SealGuardian
					? Economy.EliteKillSouls : Economy.CommonKillSouls));
	const AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	const int32 SoulReward = Quest && Quest->IsAbyssTrialActive()
		? FMath::RoundToInt(BaseSoulReward
			* UAuraCombatBalanceData::Get().AbyssTrial.SoulMultiplier)
		: BaseSoulReward;
	TArray<AAuraPlayerState*> RewardPlayers;
	if (const AAuraEncounterVolume* Room = RewardEncounter.Get())
	{
		RewardPlayers = Room->GetEligibleRewardPlayers();
	}
	AAuraCharacter* PlayerCharacter = Cast<AAuraCharacter>(DamageInstigator);
	if (RewardPlayers.IsEmpty() && PlayerCharacter)
	{
		if (AAuraPlayerState* State = PlayerCharacter->GetPlayerState<AAuraPlayerState>())
		{
			RewardPlayers.Add(State);
		}
	}
	for (AAuraPlayerState* State : RewardPlayers)
	{
		if (!State || (KillExperienceReward <= 0 && SoulReward <= 0)) continue;
		State->EnsureGrowthInitialized();
		State->AwardExperienceAndSouls(KillExperienceReward, SoulReward);
		if (AAuraEncounterVolume* Room = RewardEncounter.Get())
		{
			Room->RecordM22KillReward(State, KillExperienceReward, SoulReward);
		}
	}

	UE_LOG(LogAuraEnemy, Display,
		TEXT("Enemy kill rewards awarded. Enemy=%s XPReward=%d SoulReward=%d Recipients=%d PlayerXP=%d PlayerLevel=%d ManaReward=Disabled"),
		*GetName(), KillExperienceReward, SoulReward, RewardPlayers.Num(),
		PlayerCharacter ? PlayerCharacter->GetTotalExperience() : 0,
		PlayerCharacter ? PlayerCharacter->GetCharacterLevel() : 0);
}

void AAuraEnemy::OnHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	UE_LOG(LogAuraEnemy, Display, TEXT("Enemy health changed. Name=%s Old=%.1f New=%.1f"),
		*GetName(), ChangeData.OldValue, ChangeData.NewValue);

	if (IsBossEnemy() && !bDead && ChangeData.NewValue < ChangeData.OldValue)
	{
		const float MaxHealth = AbilitySystemComponent
			? AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute())
			: StartingHealth;
		const float PhaseThresholdHealth = MaxHealth * BossPhaseTwoHealthThreshold;
		if (BossPhase == EAuraBossPhase::Transitioning)
		{
			// The phase break is a short, readable combat beat. Damage and stun cannot
			// erase it before the second move set becomes active.
			AbilitySystemComponent->SetNumericAttributeBase(
				UAuraAttributeSet::GetHealthAttribute(),
				FMath::Max(ChangeData.OldValue, PhaseThresholdHealth));
			UE_LOG(LogAuraEnemy, Display,
				TEXT("Boss ignored damage during phase transition. Name=%s Health=%.1f"),
				*GetName(), PhaseThresholdHealth);
			return;
		}
		if (BossPhase == EAuraBossPhase::PhaseOne && ChangeData.NewValue <= PhaseThresholdHealth)
		{
			AbilitySystemComponent->SetNumericAttributeBase(
				UAuraAttributeSet::GetHealthAttribute(), PhaseThresholdHealth);
			StartBossPhaseTransition();
			return;
		}
	}

	if (ChangeData.NewValue <= 0.f)
	{
		if (bDead) return;
		FinishSpawnPresentation(true);
		bDead = true;
		ElementalStatus->ClearAll();
		bStunned = false;
		bActionLocked = true;
		EndMeleeAction();
		ClearEliteSporeField();
		EndRangedCast();
		EndBossAction();
		ClearBossWard(false);
		DismissOwnedBossSummons();
		GetWorldTimerManager().ClearTimer(ActionAnimationTimer);
		GetWorldTimerManager().ClearTimer(BossPhaseTransitionTimer);
		GetWorldTimerManager().ClearTimer(PoiseBreakTimer);
		if (AbilitySystemComponent) AbilitySystemComponent->CancelAllAbilities();
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		if (ActiveStunEffect)
		{
			ActiveStunEffect->DestroyComponent();
			ActiveStunEffect = nullptr;
		}
		if (ActiveBossPhaseEffect)
		{
			ActiveBossPhaseEffect->DestroyComponent();
			ActiveBossPhaseEffect = nullptr;
		}
		PlayDeathPresentation();
		OnEnemyDefeated.Broadcast(this);
		SetLifeSpan(IsBossEnemy() ? BossCorpseLifetime : 6.f);
		UE_LOG(LogAuraEnemy, Display, TEXT("Enemy defeated. Name=%s"), *GetName());
		return;
	}

	if (ChangeData.NewValue < ChangeData.OldValue) PlayDamageReaction();
}

void AAuraEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (AbilitySystemComponent) AbilitySystemComponent->CancelAllAbilities();
		DismissOwnedBossSummons();
	}
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AAuraEnemy::PlayDeathPresentation()
{
	CurrentAnimation = nullptr;
	if (!IsBossEnemy())
	{
		PlayAnimation(DeathAnimation, false);
		return;
	}

	bBossDeathPresentationStarted = true;
	PlayAnimation(BossDeathAnimation ? BossDeathAnimation.Get() : DeathAnimation.Get(), false);

	const float BossScale = FMath::Clamp(GetActorScale3D().GetMax(), 1.f, 3.f);
	if (BossImpactEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, BossImpactEffect,
			GetActorLocation() + FVector(0.f, 0.f, 55.f * BossScale),
			FRotator::ZeroRotator, FVector(1.1f * BossScale), true, true);
	}
	if (BossDeathEffect)
	{
		ActiveBossDeathEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, BossDeathEffect,
			GetActorLocation() + FVector(0.f, 0.f, 80.f * BossScale),
			FRotator::ZeroRotator, FVector(0.85f * BossScale), false, true);
	}
	if (BossDeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this, BossDeathSound, GetActorLocation(), 1.15f, 0.92f);
	}

	GetWorldTimerManager().SetTimer(
		BossDeathRagdollTimer, this, &AAuraEnemy::BeginBossDeathRagdoll,
		BossDeathRagdollDelay, false);
	GetWorldTimerManager().SetTimer(
		BossDeathEffectTimer, this, &AAuraEnemy::ClearBossDeathEffect,
		BossDeathEffectDuration, false);
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Boss death presentation started. Name=%s Animation=%s RagdollDelay=%.2f EffectDuration=%.2f"),
		*GetName(), *GetNameSafe(BossDeathAnimation ? BossDeathAnimation.Get() : DeathAnimation.Get()),
		BossDeathRagdollDelay, BossDeathEffectDuration);
}

void AAuraEnemy::BeginBossDeathRagdoll()
{
	if (!bDead || !IsBossEnemy() || bBossDeathRagdollActive || !GetMesh()) return;

	bBossDeathRagdollActive = true;
	USkeletalMeshComponent* BossMesh = GetMesh();
	BossMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	BossMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BossMesh->SetAllBodiesSimulatePhysics(true);
	BossMesh->SetSimulatePhysics(true);
	BossMesh->SetLinearDamping(2.5f);
	BossMesh->SetAngularDamping(4.f);
	BossMesh->WakeAllRigidBodies();
	const FVector DeathImpulse = -GetActorForwardVector() * BossDeathImpulseStrength
		+ FVector::UpVector * BossDeathImpulseStrength * 0.18f;
	BossMesh->AddImpulse(DeathImpulse, NAME_None, true);
	if (BossDeathImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this, BossDeathImpactSound, GetActorLocation(), 1.2f, 0.72f);
	}
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Boss death ragdoll activated. Name=%s Impulse=%s"),
		*GetName(), *DeathImpulse.ToCompactString());
}

void AAuraEnemy::ClearBossDeathEffect()
{
	if (!ActiveBossDeathEffect) return;
	ActiveBossDeathEffect->DeactivateImmediate();
	ActiveBossDeathEffect->DestroyComponent();
	ActiveBossDeathEffect = nullptr;
}

void AAuraEnemy::StartBossPhaseTransition()
{
	if (!HasAuthority() || !IsBossEnemy() || bDead
		|| BossPhase != EAuraBossPhase::PhaseOne || !AbilitySystemComponent) return;

	BossPhase = EAuraBossPhase::Transitioning;
	BossActionState = FAuraBossActionState();
	ClearBossWard(false);
	bActionLocked = true;
	GetWorldTimerManager().ClearTimer(ActionAnimationTimer);
	AbilitySystemComponent->CancelAllAbilities();
	FGameplayTagContainer StunTags;
	StunTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")));
	AbilitySystemComponent->RemoveActiveEffectsWithGrantedTags(StunTags);
	AbilitySystemComponent->AddLooseGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.Boss.PhaseTransition")));
	bStunned = false;
	if (ActiveStunEffect)
	{
		ActiveStunEffect->DestroyComponent();
		ActiveStunEffect = nullptr;
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	RefreshBossTelegraphPresentation();
	RefreshBossPhasePresentation();
	ForceNetUpdate();

	GetWorldTimerManager().SetTimer(BossPhaseTransitionTimer, this,
		&AAuraEnemy::FinishBossPhaseTransition, BossPhaseTransitionDuration, false);
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Boss phase transition started. Name=%s Duration=%.2f Health=%.1f"),
		*GetName(), BossPhaseTransitionDuration,
		AbilitySystemComponent->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()));
}

void AAuraEnemy::FinishBossPhaseTransition()
{
	if (!HasAuthority() || bDead || !IsBossEnemy()
		|| BossPhase != EAuraBossPhase::Transitioning) return;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.Boss.PhaseTransition")));
		AbilitySystemComponent->AddLooseGameplayTag(
			FGameplayTag::RequestGameplayTag(TEXT("State.Boss.PhaseTwo")));
	}
	BossPhase = EAuraBossPhase::PhaseTwo;
	bActionLocked = false;
	RefreshMovementSpeedFromStatus();
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	CurrentAnimation = nullptr;
	RefreshBossPhasePresentation();
	ResumeLocomotionAnimation();
	ForceNetUpdate();
	UE_LOG(LogAuraEnemy, Display,
		TEXT("Boss phase two active. Name=%s WalkSpeed=%.1f"),
		*GetName(), GetCharacterMovement()->MaxWalkSpeed);
}

void AAuraEnemy::OnRep_BossPhase()
{
	RefreshBossPhasePresentation();
	if (BossPhase == EAuraBossPhase::PhaseTwo && !bDead)
	{
		bActionLocked = false;
		RefreshMovementSpeedFromStatus();
		CurrentAnimation = nullptr;
		ResumeLocomotionAnimation();
	}
}

void AAuraEnemy::RefreshBossPhasePresentation()
{
	if (!IsBossEnemy()) return;
	if (ActiveBossPhaseEffect)
	{
		ActiveBossPhaseEffect->DeactivateImmediate();
		ActiveBossPhaseEffect->DestroyComponent();
		ActiveBossPhaseEffect = nullptr;
	}
	if (BossPhase != EAuraBossPhase::Transitioning || bDead) return;

	bActionLocked = true;
	CurrentAnimation = nullptr;
	PlayAnimation(BossPhaseTransitionAnimation
		? BossPhaseTransitionAnimation.Get() : StunnedAnimation.Get(), false);
	if (BossPhaseEffect && GetMesh())
	{
		ActiveBossPhaseEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
			BossPhaseEffect, GetMesh(), NAME_None, FVector(0.f, 0.f, 80.f),
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
	}
	if (BossPhaseTransitionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this, BossPhaseTransitionSound, GetActorLocation(), 1.05f);
	}
}

void AAuraEnemy::PlayAnimation(UAnimSequence* Animation, bool bLooping)
{
	if (!Animation || !GetMesh() || CurrentAnimation == Animation) return;
	CurrentAnimation = Animation;
	GetMesh()->PlayAnimation(Animation, bLooping);
}

void AAuraEnemy::PlayActionAnimation(UAnimSequence* Animation)
{
	if (!Animation || bDead || bStunned) return;
	bActionLocked = true;
	GetWorldTimerManager().ClearTimer(ActionAnimationTimer);
	CurrentAnimation = nullptr;
	PlayAnimation(Animation, false);
	GetWorldTimerManager().SetTimer(ActionAnimationTimer, this,
		&AAuraEnemy::ResumeLocomotionAnimation,
		FMath::Max(0.1f, Animation->GetPlayLength()), false);
}

void AAuraEnemy::PlayDamageReaction()
{
	// Damage is not control. The old path locked movement for the entire clip and
	// restarted that lock on every free FireBolt hit, creating an infinite stagger.
	if (!HitReactAnimation || bDead || bStunned || IsActionLocked() || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextHitReactAllowedTime) return;
	NextHitReactAllowedTime = Now + 0.95f;
	HitReactVisualUntil = Now + FMath::Min(HitReactAnimation->GetPlayLength(), 0.2f);
	CurrentAnimation = nullptr;
	PlayAnimation(HitReactAnimation, false);
}

void AAuraEnemy::ResumeLocomotionAnimation()
{
	if (bDead || bStunned || IsBossTransitioning()) return;
	bActionLocked = false;
	CurrentAnimation = nullptr;
	const bool bMoving = GetVelocity().SizeSquared2D() > FMath::Square(5.f);
	PlayAnimation(bMoving && WalkAnimation ? WalkAnimation : IdleAnimation, true);
}

void AAuraEnemy::OnStunTagChanged(const FGameplayTag CallbackTag, const int32 NewCount)
{
	if (NewCount > 0)
	{
		BeginStun();
	}
	else
	{
		EndStun();
	}
}

void AAuraEnemy::OnSlowTagChanged(const FGameplayTag CallbackTag, const int32 NewCount)
{
	bSlowed = NewCount > 0;
	RefreshMovementSpeedFromStatus();
}

void AAuraEnemy::RefreshMovementSpeedFromStatus()
{
	if (!GetCharacterMovement()) return;
	const UAuraCombatBalanceData& Balance = UAuraCombatBalanceData::Get();
	const float BaseSpeed = IsBossEnemy()
		? (BossPhase == EAuraBossPhase::PhaseTwo ? BossPhaseTwoWalkSpeed : BossPhaseOneWalkSpeed)
		: (IsRangedCombatant() ? Balance.Shaman.WalkSpeed : Balance.MeleeEnemy.WalkSpeed);
	GetCharacterMovement()->MaxWalkSpeed = BaseSpeed
		* (bEliteVariant ? Balance.EliteEnemy.WalkSpeedMultiplier : 1.f)
		* (bSlowed
		? 1.f - FMath::Max(Balance.SkillRanks.ArcaneShardsSlowFraction, ElementalStatus->GetSlowFraction())
		: 1.f - ElementalStatus->GetSlowFraction());
}

void AAuraEnemy::BeginStun()
{
	if (bDead || bStunned || IsBossTransitioning())
	{
		if (IsBossTransitioning())
		{
			UE_LOG(LogAuraEnemy, Display,
				TEXT("Boss rejected stun during phase transition. Name=%s"), *GetName());
		}
		return;
	}
	bStunned = true;
	bActionLocked = true;
	EndMeleeAction();
	EndRangedCast();
	GetWorldTimerManager().ClearTimer(ActionAnimationTimer);
	if (AbilitySystemComponent) AbilitySystemComponent->CancelAllAbilities();
	if (AAuraAIController* AuraAIController = Cast<AAuraAIController>(GetController()))
	{
		AuraAIController->StopMovement();
		AuraAIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	CurrentAnimation = nullptr;
	PlayAnimation(StunnedAnimation, true);

	if (StunEffect && GetMesh())
	{
		ActiveStunEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
			StunEffect, GetMesh(), NAME_None, FVector(0.f, 0.f, 115.f), FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false);
	}
	UE_LOG(LogAuraEnemy, Display, TEXT("Enemy stunned. Name=%s"), *GetName());
}

void AAuraEnemy::EndStun()
{
	if (!bStunned && !ActiveStunEffect) return;
	bStunned = false;
	if (ActiveStunEffect)
	{
		ActiveStunEffect->DestroyComponent();
		ActiveStunEffect = nullptr;
	}
	if (bDead || IsBossTransitioning()) return;

	bActionLocked = false;
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	CurrentAnimation = nullptr;
	ResumeLocomotionAnimation();
	UE_LOG(LogAuraEnemy, Display, TEXT("Enemy stun ended. Name=%s"), *GetName());
}

