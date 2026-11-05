// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Character/AuraCharacterBase.h"
#include "Data/AuraLootProfileData.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "AuraEnemy.generated.h"

class UAnimSequence;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
class USkeletalMesh;
class UStaticMeshComponent;
class AAuraEnemy;
class AAuraCharacter;
class UAuraElementalStatusComponent;
class AAuraEncounterVolume;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAuraEnemyDefeated, AAuraEnemy*, DefeatedEnemy);

UENUM(BlueprintType)
enum class EAuraEnemyCombatStyle : uint8
{
	Melee,
	Ranged
};

/** Gameplay identity used to select an attack kit, independent of the mesh. */
UENUM(BlueprintType)
enum class EAuraEnemyArchetype : uint8
{
	Ghoul,
	GoblinSpearman,
	CaveShroom,
	ShamanElite,
	DemonLord
};

UENUM(BlueprintType)
enum class EAuraMeleeAction : uint8
{
	None,
	GhoulSwipe,
	GhoulFrenzy,
	GoblinJab,
	GoblinLunge,
	ShroomBash,
	ShroomSporeBurst,
	EliteGoblinSweep,
	EliteShroomSeed
};

USTRUCT(BlueprintType)
struct FAuraMeleeActionState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EAuraMeleeAction Action = EAuraMeleeAction::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector TelegraphLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float TelegraphRadius = 0.f;

	/** Synced server clock values keep cosmetic windup fades aligned with the real impact. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float TelegraphStartServerTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float TelegraphDuration = 0.f;
};

/** The elite shroom's one persistent, server-authoritative ground hazard. */
USTRUCT(BlueprintType)
struct FAuraEliteSporeFieldState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Radius = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float ArmedServerTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float ExpireServerTime = 0.f;
};

UENUM(BlueprintType)
enum class EAuraBossPhase : uint8
{
	PhaseOne,
	Transitioning,
	PhaseTwo
};

UENUM(BlueprintType)
enum class EAuraBossAction : uint8
{
	None,
	ClawLeft,
	ClawRight,
	TailSpin,
	HellfireThrow,
	SummonMinions,
	DemonicCharge,
	DemonicWard,
	InfernalBarrage
};

USTRUCT(BlueprintType)
struct FAuraBossActionState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EAuraBossAction Action = EAuraBossAction::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector TelegraphLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float TelegraphRadius = 0.f;
};

UCLASS()
class AURA_API AAuraEnemy : public AAuraCharacterBase
{
	GENERATED_BODY()

public:
	AAuraEnemy();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsDead() const { return bDead; }
	bool IsActionLocked() const
	{
		return bActionLocked || bSpawnPresentationActive
			|| MeleeActionState.Action != EAuraMeleeAction::None;
	}
	bool IsSpawnPresentationActive() const { return bSpawnPresentationActive; }
	bool IsEncounterDormant() const { return bEncounterDormant; }
	/** Call before FinishSpawning for guards already visible when a room loads. */
	void ConfigurePreplacedGuard();
	void SetEncounterDormant(bool bDormant) { bEncounterDormant = bDormant; }
	float GetSpawnPresentationProgress() const;
	bool IsStunned() const { return bStunned; }
	float GetPoiseMax() const;
	float GetPoiseCurrent() const { return CurrentPoise; }
	bool IsPoiseBroken() const { return bPoiseBroken; }
	UAuraElementalStatusComponent* GetElementalStatus() const { return ElementalStatus; }
	void RefreshElementalMovement() { RefreshMovementSpeedFromStatus(); }
	float GetPoiseBreakRemaining() const;
	/** Server-only. Returns true only when this hit breaks the gauge and starts a stagger. */
	bool ApplyPoiseDamage(float Amount);
	bool IsRangedCombatant() const { return CombatStyle == EAuraEnemyCombatStyle::Ranged; }
	bool IsRangedCasting() const { return bRangedCasting; }
	bool IsRangedVolleyCasting() const { return bRangedCasting && bRangedVolleyCasting; }
	bool IsRangedGroundBurstCasting() const { return bRangedCasting && bRangedGroundBurstCasting; }
	bool IsBossEnemy() const { return bBossEnemy; }
	bool IsEliteVariant() const { return bEliteVariant; }
	bool IsEliteEnraged() const;
	/** Set on a deferred room spawn before BeginPlay initializes GAS attributes. */
	void ConfigureEliteVariant(const FVector& WorldScale);
	float GetEliteAttackIntervalMultiplier() const;
	float GetEliteSpecialChanceBonus() const;
	bool IsBossTransitioning() const { return bBossEnemy && BossPhase == EAuraBossPhase::Transitioning; }
	bool IsDamageImmune() const { return IsBossTransitioning(); }
	bool HasBossDeathPresentationStarted() const { return bBossDeathPresentationStarted; }
	bool IsBossDeathRagdollActive() const { return bBossDeathRagdollActive; }
	const FVector& GetRangedCastTargetLocation() const { return RangedCastTargetLocation; }
	float GetRangedRetreatDistance() const { return RangedRetreatDistance; }
	float GetRangedPreferredDistance() const { return RangedPreferredDistance; }
	float GetRangedAttackRange() const { return RangedAttackRange; }
	EAuraEnemyArchetype GetEnemyArchetype() const { return EnemyArchetype; }
	EAuraMeleeAction GetMeleeAction() const { return MeleeActionState.Action; }
	const FVector& GetMeleeTelegraphLocation() const { return MeleeActionState.TelegraphLocation; }
	float GetMeleeTelegraphRadius() const { return MeleeActionState.TelegraphRadius; }
	float GetMeleeTelegraphProgress() const;
	const FAuraEliteSporeFieldState& GetEliteSporeField() const { return EliteSporeField; }
	float GetMeleeEngagementRange() const;
	FString GetMeleeActionLabel() const;
	EAuraBossPhase GetBossPhase() const { return BossPhase; }
	EAuraBossAction GetBossAction() const { return BossActionState.Action; }
	const FVector& GetBossTelegraphLocation() const { return BossActionState.TelegraphLocation; }
	float GetBossTelegraphRadius() const { return BossActionState.TelegraphRadius; }
	float GetBossMeleeRange() const { return BossMeleeRange; }
	float GetBossTailSpinRadius() const { return BossTailSpinRadius; }
	float GetBossThrowRange() const { return BossThrowRange; }
	float GetBossThrowWarningRadius() const { return BossThrowWarningRadius; }
	const TArray<TSubclassOf<AAuraEnemy>>& GetBossSummonClasses() const { return BossSummonClasses; }
	AAuraEnemy* GetBossSummoner() const { return BossSummoner; }
	float GetBossPhaseThreshold() const { return BossPhaseTwoHealthThreshold; }
	float GetBossPhaseTransitionDuration() const { return BossPhaseTransitionDuration; }
	float GetBossWardHealth() const { return BossWardHealth; }
	float GetBossWardCapacity() const { return BossWardCapacity; }
	bool HasBossWard() const { return BossWardHealth > KINDA_SMALL_NUMBER; }
	/** Server-only. Starts the finite phase-two absorb pool after the warned cast. */
	void ActivateBossWard();
	/** Server-only, called after armor mitigation; returns damage that reaches health. */
	float AbsorbBossWardDamage(float MitigatedDamage);
	float GetBossDeathRagdollDelay() const { return BossDeathRagdollDelay; }
	FString GetBossPhaseLabel() const;
	FString GetBossActionLabel() const;
	int32 GetKillExperienceReward() const { return KillExperienceReward; }
	EAuraLootProfileType GetLootProfile() const { return LootProfile; }
	void SetLootProfile(EAuraLootProfileType NewProfile) { LootProfile = NewProfile; }
	float GetStartingHealth() const { return StartingHealth; }
	/** Server-only post-spawn scaling for the optional second-run challenge. */
	void ApplyAbyssHealthBonus(float Multiplier);
	float GetDifficultyDamageMultiplier() const;
	/** True when the authored enemy-skill Niagara set was loaded successfully. */
	bool AreEnemySkillEffectsConfigured() const;
	/** Local-only diagnostic count; useful for rendered and network smoke tests. */
	int32 GetLocalSkillVFXSpawnCount() const { return LocalSkillVFXSpawnCount; }
	/** Rank-two fracture: short normal-enemy stagger with per-target immunity. */
	bool TryApplyComboStagger(UAbilitySystemComponent* SourceASC,
		float Duration, float ImmunitySeconds);
	/** Boss substitute for hard control: temporary outgoing-damage reduction. */
	bool TryApplyComboWeaken(float Duration, float CooldownSeconds);
	const FString& GetEnemyDisplayName() const { return EnemyDisplayName; }
	void PlayMeleeAttackAnimation();
	void BeginMeleeAction(EAuraMeleeAction NewAction, const FVector& TelegraphLocation,
		float TelegraphRadius, float TelegraphDuration = 0.f);
	void ClearMeleeTelegraph();
	void EndMeleeAction();
	void PlayMeleeImpactEffect(const FVector& ImpactLocation, float EffectRadius,
		EAuraMeleeAction ActionOverride = EAuraMeleeAction::None);
	void ActivateEliteSporeField(const FVector& Location, float Radius,
		float ArmDelay, float Duration);
	void ClearEliteSporeField();
	void BeginRangedCast(const FVector& TelegraphLocation, bool bIsVolley = false,
		bool bIsGroundBurst = false);
	/** Server-only entry point for the staff release flash shared by bolt and volley casts. */
	void PlayRangedReleaseEffect(const FVector& ReleaseLocation,
		const FVector& ReleaseDirection, bool bIsVolley);
	/** Server-only ground impact for the Shaman's locked-position air burst. */
	void PlayShamanGroundBurstEffect(const FVector& ImpactLocation, float EffectRadius);
	/** Server-only trigger for short HUD texture cues used by Goblin and Shroom attacks. */
	void PlayMeleeTextureCue(EAuraMeleeAction Action, const FVector& CueLocation,
		const FVector& AttackDirection, float Duration = 0.34f);
	/** Local presentation queried by the owning client's HUD. */
	bool GetLocalMeleeTextureCue(EAuraMeleeAction& OutAction, FVector& OutLocation,
		FVector& OutDirection, float& OutProgress) const;
	void EndRangedCast();
	/** Cancels an active telegraphed action without applying the full stun state. */
	bool InterruptTelegraphedAttack();
	void BeginBossAction(EAuraBossAction NewAction, const FVector& TelegraphLocation,
		float TelegraphRadius);
	void ClearBossTelegraph();
	void EndBossAction();
	void PlayBossImpactEffect(const FVector& ImpactLocation, float EffectRadius,
		EAuraBossAction ActionOverride = EAuraBossAction::None);
	/** Server-only entry point for Hellfire and Infernal Barrage muzzle feedback. */
	void PlayBossProjectileReleaseEffect(const FVector& ReleaseLocation,
		const FVector& ReleaseDirection, EAuraBossAction Action);
	void ConfigureAsBossSummon(AAuraEnemy* InSummoner);
	void SetRewardEncounter(AAuraEncounterVolume* Encounter) { RewardEncounter = Encounter; }
	AAuraEncounterVolume* GetRewardEncounter() const { return RewardEncounter; }
	void TryAwardKillRewards(AActor* DamageInstigator);

	UPROPERTY(BlueprintAssignable, Category = "Aura|Combat")
	FOnAuraEnemyDefeated OnEnemyDefeated;

protected:
	UPROPERTY(VisibleAnywhere, Category="Aura|Elements")
	TObjectPtr<UAuraElementalStatusComponent> ElementalStatus;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void OnHealthChanged(const FOnAttributeChangeData& ChangeData);
	void PlayAnimation(UAnimSequence* Animation, bool bLooping);
	void PlayActionAnimation(UAnimSequence* Animation);
	void PlayDamageReaction();
	void ResumeLocomotionAnimation();
	void OnStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void OnSlowTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void RefreshMovementSpeedFromStatus();
	void BeginStun();
	void EndStun();
	void RefreshMeleeTelegraphPresentation();
	void RefreshRangedCastPresentation();
	void RefreshSkillCastPresentation();
	void ClearSkillCastPresentation();
	void RefreshEliteSporeFieldPresentation();
	void SpawnTransientSkillEffect(UNiagaraSystem* Effect, const FVector& Location,
		const FRotator& Rotation, const FVector& Scale, float MaxLifetime,
		float FadeOutLifetime = 0.55f);
	void ScheduleTransientSkillEffectCleanup(UNiagaraComponent* Component,
		float MaxLifetime, float FadeOutLifetime);
	void StartBossPhaseTransition();
	void FinishBossPhaseTransition();
	void RefreshBossPhasePresentation();
	void PlayBossActionAnimation();
	void RefreshBossTelegraphPresentation();
	void PlayDeathPresentation();
	void BeginBossDeathRagdoll();
	void ClearBossDeathEffect();
	void DismissOwnedBossSummons();
	void ApplyCombatBalance();
	void BeginSpawnPresentation();
	void UpdateSpawnPresentation(float DeltaSeconds);
	void FinishSpawnPresentation(bool bInterrupted = false);
	void UpdateEliteSporeField();
	void UpdatePoise(float DeltaSeconds);
	void FinishPoiseBreak();

	UFUNCTION()
	void OnRep_MeleeActionState(FAuraMeleeActionState PreviousState);

	UFUNCTION()
	void OnRep_RangedCasting();

	UFUNCTION()
	void OnRep_EliteSporeField();

	UFUNCTION()
	void OnRep_ElitePresentationScale();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayMeleeImpactEffect(FVector_NetQuantize ImpactLocation,
		float EffectRadius, EAuraMeleeAction Action);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayRangedReleaseEffect(FVector_NetQuantize ReleaseLocation,
		FVector_NetQuantizeNormal ReleaseDirection, bool bIsVolley);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayShamanGroundBurstEffect(FVector_NetQuantize ImpactLocation,
		float EffectRadius);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayMeleeTextureCue(EAuraMeleeAction Action,
		FVector_NetQuantize CueLocation,
		FVector_NetQuantizeNormal AttackDirection, float Duration);

	UFUNCTION()
	void OnRep_BossPhase();

	UFUNCTION()
	void OnRep_BossActionState(FAuraBossActionState PreviousState);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayBossImpactEffect(FVector_NetQuantize ImpactLocation,
		float EffectRadius, EAuraBossAction Action);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayBossProjectileReleaseEffect(FVector_NetQuantize ReleaseLocation,
		FVector_NetQuantizeNormal ReleaseDirection, EAuraBossAction Action);

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> IdleAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> WalkAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> AttackAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> HitReactAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> DeathAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> StunnedAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> RangedCastAnimation;

	/** Optional weapon presentation for enemy archetypes such as goblin spearmen. */
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Equipment")
	TObjectPtr<USkeletalMesh> EnemyWeaponMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Equipment")
	FName EnemyWeaponSocket = NAME_None;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Equipment")
	FTransform EnemyWeaponRelativeTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects")
	TObjectPtr<UNiagaraSystem> StunEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects")
	TObjectPtr<UNiagaraSystem> RangedCastWarningEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects")
	TObjectPtr<UNiagaraSystem> MeleeTelegraphEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects")
	TObjectPtr<UNiagaraSystem> MeleeImpactEffect;

	/** Physical wind-up used by lunges, sweeps, and charges. */
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> WindCastEffect;

	/** Persistent soul/spore wind-up used by mushroom and summon skills. */
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> SporeCastEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> LightningCastEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> FireCastEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> WindImpactEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> MagicBurstEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> SporeFieldEffect;

	/** Locked-position Shaman area skill requested by the authored combat pass. */
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> ShamanGroundBurstEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> LightningReleaseEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> FireReleaseEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Effects|Enemy Skills")
	TObjectPtr<UNiagaraSystem> SoulReleaseEffect;

	/** Generic material-independent entrance: the mesh rises from below the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Aura|Spawn Presentation")
	bool bUseSpawnRisePresentation = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Spawn Presentation",
		meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float SpawnPresentationDuration = 0.72f;

	/** Minimum world-space depth used even for very small enemies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Spawn Presentation",
		meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float SpawnRiseMinimumDepth = 90.f;

	/** Caps the rise distance of large bosses so their entrance remains readable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Spawn Presentation",
		meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float SpawnRiseMaximumDepth = 260.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossAttackLeftAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossAttackRightAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossTailSpinAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossThrowAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossSummonAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossChargeAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossPhaseTransitionAnimation;

	/** Dedicated sequence used before the boss falls into physics. */
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Animation")
	TObjectPtr<UAnimSequence> BossDeathAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Effects")
	TObjectPtr<UNiagaraSystem> BossTelegraphEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Effects")
	TObjectPtr<UNiagaraSystem> BossPhaseEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Effects")
	TObjectPtr<UNiagaraSystem> BossImpactEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Effects")
	TObjectPtr<UNiagaraSystem> BossDeathEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> BossPhaseTransitionSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> BossDeathSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> BossDeathImpactSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Death",
		meta = (ClampMin = "0.1"))
	float BossDeathRagdollDelay = 1.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Death",
		meta = (ClampMin = "0.1"))
	float BossDeathEffectDuration = 2.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Death",
		meta = (ClampMin = "0.0"))
	float BossDeathImpulseStrength = 90.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Death",
		meta = (ClampMin = "3.0"))
	float BossCorpseLifetime = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Combat")
	EAuraEnemyCombatStyle CombatStyle = EAuraEnemyCombatStyle::Melee;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Identity")
	EAuraEnemyArchetype EnemyArchetype = EAuraEnemyArchetype::Ghoul;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss")
	bool bBossEnemy = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss",
		meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float BossPhaseTwoHealthThreshold = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss",
		meta = (ClampMin = "0.1"))
	float BossPhaseTransitionDuration = 1.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Movement",
		meta = (ClampMin = "1.0"))
	float BossPhaseOneWalkSpeed = 245.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Movement",
		meta = (ClampMin = "1.0"))
	float BossPhaseTwoWalkSpeed = 310.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Combat",
		meta = (ClampMin = "100.0"))
	float BossMeleeRange = 285.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Combat",
		meta = (ClampMin = "100.0"))
	float BossTailSpinRadius = 380.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Combat",
		meta = (ClampMin = "100.0"))
	float BossThrowRange = 1150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Combat",
		meta = (ClampMin = "25.0"))
	float BossThrowWarningRadius = 115.f;

	/** Enemy Blueprint classes available to the Demon Lord's summon action. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Boss|Summon")
	TArray<TSubclassOf<AAuraEnemy>> BossSummonClasses;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Combat|Ranged",
		meta = (ClampMin = "100.0"))
	float RangedRetreatDistance = 450.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Combat|Ranged",
		meta = (ClampMin = "100.0"))
	float RangedPreferredDistance = 700.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Combat|Ranged",
		meta = (ClampMin = "100.0"))
	float RangedAttackRange = 1000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Identity")
	FString EnemyDisplayName = TEXT("食尸鬼");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Attributes", meta = (ClampMin = "1.0"))
	float StartingHealth = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Rewards", meta = (ClampMin = "0"))
	int32 KillExperienceReward = 25;

	/** Auto resolves melee/ranged/boss; encounters may override guardians explicitly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Rewards")
	EAuraLootProfileType LootProfile = EAuraLootProfileType::Auto;

private:
	UPROPERTY(Replicated)
	bool bEliteVariant = false;

	UPROPERTY(Replicated)
	float CurrentPoise = 0.f;

	UPROPERTY(Replicated)
	bool bPoiseBroken = false;

	/** Actor scale is sent explicitly so the enlarged capsule matches on clients. */
	UPROPERTY(ReplicatedUsing = OnRep_ElitePresentationScale)
	FVector ElitePresentationScale = FVector::OneVector;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveStunEffect;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveMeleeTelegraph;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveRangedCastWarning;

	/** One attached cast aura at a time; explicitly destroyed when the action ends. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveSkillCastEffect;

	/** Replicated elite mushroom hazard presentation. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveEliteSporeFieldEffect;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveBossTelegraph;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveBossPhaseEffect;

	/** Local presentation follows replicated ward health; this mesh never collides. */
	UPROPERTY(VisibleAnywhere, Category = "Aura|Boss|Ward")
	TObjectPtr<UStaticMeshComponent> BossWardMesh;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveBossDeathEffect;

	/** Separate from Actor Owner because AI possession replaces a Pawn's owner with its controller. */
	UPROPERTY(Transient)
	TObjectPtr<AAuraEnemy> BossSummoner;

	/** Stable room identity; APawn Owner is overwritten when AI takes possession. */
	UPROPERTY(Transient)
	TObjectPtr<AAuraEncounterVolume> RewardEncounter;

	UPROPERTY(Replicated)
	FVector RangedCastTargetLocation = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_MeleeActionState)
	FAuraMeleeActionState MeleeActionState;

	UPROPERTY(ReplicatedUsing = OnRep_EliteSporeField)
	FAuraEliteSporeFieldState EliteSporeField;

	UPROPERTY(ReplicatedUsing = OnRep_RangedCasting)
	bool bRangedCasting = false;

	UPROPERTY(ReplicatedUsing = OnRep_RangedCasting)
	bool bRangedVolleyCasting = false;

	UPROPERTY(ReplicatedUsing = OnRep_RangedCasting)
	bool bRangedGroundBurstCasting = false;

	UPROPERTY(ReplicatedUsing = OnRep_BossPhase)
	EAuraBossPhase BossPhase = EAuraBossPhase::PhaseOne;

	UPROPERTY(ReplicatedUsing = OnRep_BossActionState)
	FAuraBossActionState BossActionState;

	UPROPERTY(ReplicatedUsing = OnRep_BossWardHealth)
	float BossWardHealth = 0.f;

	UPROPERTY(Replicated)
	float BossWardCapacity = 0.f;

	FTimerHandle ActionAnimationTimer;
	FTimerHandle BossPhaseTransitionTimer;
	FTimerHandle BossWardExpiryTimer;
	FTimerHandle BossDeathRagdollTimer;
	FTimerHandle BossDeathEffectTimer;
	FTimerHandle PoiseBreakTimer;
	FVector SpawnPresentationStartMeshLocation = FVector::ZeroVector;
	FVector SpawnPresentationTargetMeshLocation = FVector::ZeroVector;
	float SpawnPresentationElapsed = 0.f;
	float SpawnPresentationPreviousTickInterval = 0.08f;
	float SpawnPresentationWorldDepth = 0.f;
	bool bDead = false;
	bool bKillRewardsAwarded = false;
	bool bActionLocked = false;
	bool bStunned = false;
	bool bSlowed = false;
	bool bBossDeathPresentationStarted = false;
	bool bBossDeathRagdollActive = false;
	bool bSpawnPresentationActive = false;
	bool bEncounterDormant = false;
	float ComboStaggerImmuneUntil = 0.f;
	float ComboWeakenUntil = 0.f;
	float ComboWeakenReadyAt = 0.f;
	EAuraMeleeAction LocalMeleeTextureCueAction = EAuraMeleeAction::None;
	FVector LocalMeleeTextureCueLocation = FVector::ZeroVector;
	FVector LocalMeleeTextureCueDirection = FVector::ForwardVector;
	float LocalMeleeTextureCueStartTime = 0.f;
	float LocalMeleeTextureCueEndTime = 0.f;
	float HitReactVisualUntil = 0.f;
	float NextHitReactAllowedTime = 0.f;
	float LastPoiseDamageTime = 0.f;
	float PoiseImmuneUntil = 0.f;
	TSet<TWeakObjectPtr<AAuraCharacter>> EliteSporeFieldVictims;
	int32 LocalSkillVFXSpawnCount = 0;

	void ClearBossWard(bool bBroken);
	void ExpireBossWard();
	void RefreshBossWardPresentation();
	UFUNCTION() void OnRep_BossWardHealth();
};
