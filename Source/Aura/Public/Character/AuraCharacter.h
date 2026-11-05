// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Character/AuraCharacterBase.h"
#include "Game/AuraRunUpgrade.h"
#include "GameplayTagContainer.h"
#include "Player/AuraCameraMode.h"
#include "AuraCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UBlendSpace;
class UAnimSequence;
class UGameplayEffect;
class UNiagaraSystem;
class USoundBase;
class AAuraEnemy;
class APlayerState;
class UAuraReviveComponent;
struct FOnAttributeChangeData;

UENUM(BlueprintType)
enum class EAuraComboMarkType : uint8
{
	Ember,
	Fracture,
	Overload
};

UENUM(BlueprintType)
enum class EAuraPlayerSkill : uint8
{
	FireBolt,
	ArcaneShards,
	Shock
};

/** Mutually exclusive third-rank FireBolt specialization. */
UENUM(BlueprintType)
enum class EAuraFireBoltPath : uint8
{
	None,
	Pierce,
	TripleVolley
};

UENUM(BlueprintType)
enum class EAuraArcanePath : uint8 { None, CrystalPrison, CrystalEcho };
UENUM(BlueprintType)
enum class EAuraShockPath : uint8 { None, StormField, ThunderJudgment };

/** Runtime mirror of the authoritative, per-player skill allocation. */
USTRUCT(BlueprintType)
struct FAuraSkillRankState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 FireBolt = 1;

	UPROPERTY(BlueprintReadOnly)
	EAuraFireBoltPath FireBoltPath = EAuraFireBoltPath::None;

	UPROPERTY(BlueprintReadOnly)
	int32 ArcaneShards = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Shock = 0;
	UPROPERTY(BlueprintReadOnly) EAuraArcanePath ArcanePath = EAuraArcanePath::None;
	UPROPERTY(BlueprintReadOnly) EAuraShockPath ShockPath = EAuraShockPath::None;
	UPROPERTY(BlueprintReadOnly) bool bArcaneEvolutionPending = false;
	UPROPERTY(BlueprintReadOnly) bool bShockEvolutionPending = false;
};

USTRUCT(BlueprintType)
struct FAuraComboMark
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AAuraEnemy> Target;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> SourcePlayerState;

	UPROPERTY(BlueprintReadOnly)
	EAuraComboMarkType Type = EAuraComboMarkType::Ember;

	UPROPERTY(BlueprintReadOnly)
	int32 SourceCastId = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 StackCount = 1;

	UPROPERTY(BlueprintReadOnly)
	float ExpireServerTime = 0.f;

	/** Server-only cast history; older duplicate projectile hits must not refresh/stack a mark. */
	TSet<int32> AppliedCastIds;
};

USTRUCT(BlueprintType)
struct FAuraPlayerProgressionState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Aura|Progression")
	int32 Level = 1;

	/** Total cumulative experience earned during this dungeon run. */
	UPROPERTY(BlueprintReadOnly, Category = "Aura|Progression")
	int32 Experience = 0;
};

UCLASS()
class AURA_API AAuraCharacter : public AAuraCharacterBase
{
	GENERATED_BODY()

public:
	AAuraCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	virtual FRotator GetViewRotation() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Aura|Camera")
	void SetCameraMode(EAuraCameraMode NewMode, bool bInstant = false);

	UFUNCTION(BlueprintPure, Category = "Aura|Camera")
	EAuraCameraMode GetCameraMode() const { return EAuraCameraMode::TopDown; }

	UFUNCTION(BlueprintPure, Category = "Aura|Camera")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "Aura|Combat")
	bool IsDead() const { return bDead; }
	UAuraReviveComponent* GetReviveComponent() const { return ReviveComponent; }

	UFUNCTION(BlueprintPure, Category = "Aura|Combat")
	bool IsInCombat() const { return bInCombat; }

	UFUNCTION(BlueprintPure, Category = "Aura|Combat")
	float GetCombatExitDelay() const;

	/** Temporarily owns facing during a locked FireBolt windup; returns the turn time. */
	float BeginFireBoltTurnTo(AAuraEnemy* Target);
	void AlignFireBoltTurnToTarget(AAuraEnemy* Target);
	void EndFireBoltTurn();
	bool IsFireBoltTurning() const { return bFireBoltTurning; }

	/** Fully restores health and mana at a checkpoint, then clears combat state. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Checkpoint")
	void RestoreVitalsAtCheckpoint();

	/** Reuses the same pawn so GAS state and per-run progression survive a co-op death. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Coop")
	void ReviveAtTransform(const FTransform& RespawnTransform, float VitalFraction = 1.f,
		float ProtectionDuration = 0.f);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastArcaneShardsCast();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireBoltCast();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastArcaneShardsImpact(FVector ImpactLocation);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShockCast();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShockImpact(FVector BeamStart, FVector BeamEnd,
		bool bHasImpact, FVector ImpactLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastEmberBurst(FVector Center);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastFractureChain(FVector Start, FVector End);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOverload(FVector Center);

	/** Authority-confirmed cosmetic cues. Reliable because each one represents a sparse gameplay event. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLevelUpEffect();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastHealthRestoreEffect();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastManaRestoreEffect();

	bool AreProgressionFeedbackEffectsConfigured() const
	{
		return LevelUpEffect && HealthRestoreEffect && ManaRestoreEffect;
	}

	/** Called only after an authoritative ability has confirmed a damageable target. */
	int32 AllocateComboCastId();
	void ApplyComboMark(AAuraEnemy* Target, EAuraComboMarkType Type, int32 SourceCastId);
	bool HasComboMark(const AAuraEnemy* Target, EAuraComboMarkType Type) const;
	bool TryConsumeComboMark(AAuraEnemy* Target, EAuraComboMarkType Type,
		int32 FinisherCastId, int32* OutStackCount = nullptr);
	/** Clears transient marks whenever the persistent combo allocation changes. */
	void ClearComboMarks();
	void ExecuteEmberBurst(const FVector& Center, float BaseQDamage, int32 StackCount = 1);
	void ExecuteFractureChain(AAuraEnemy* PrimaryTarget, float BaseEDamage);
	void ExecuteOverload(const FVector& Center, float BaseFireDamage);
	bool HasSporeExposure() const;
	bool ApplySporeExposure(AAuraEnemy* Source);
	/** Called by a successful enemy special on the authoritative player pawn. */
	bool ConsumeSporeExposure();
	bool HasLegendaryComboEffect(FName EffectTag) const;
	const TArray<FAuraComboMark>& GetComboMarks() const { return ComboMarks; }
	float GetComboServerTime() const;
	int32 GetComboRank(EAuraComboMarkType Type) const;

	int32 GetSkillRank(EAuraPlayerSkill Skill) const;
	/** Server-side application point for skill ranks learned at the Archmage. */
	bool SetSkillRank(EAuraPlayerSkill Skill, int32 NewRank);
	/** Copies automatically earned level, XP and learned ranks from PlayerState. */
	void SyncGrowthFromPlayerState(bool bRestoreVitals = false);
	void SetPaidProgression(int32 PaidLevel, int32 TotalExperience, bool bRestoreVitals = false);
	int32 GetEligibleLevel() const;
	const FAuraSkillRankState& GetSkillRankState() const { return SkillRanks; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Progression")
	void AddExperience(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Aura|Progression")
	int32 GetCharacterLevel() const { return ProgressionState.Level; }

	UFUNCTION(BlueprintPure, Category = "Aura|Progression")
	int32 GetTotalExperience() const { return ProgressionState.Experience; }

	UFUNCTION(BlueprintPure, Category = "Aura|Progression")
	int32 GetExperienceIntoCurrentLevel() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Progression")
	int32 GetExperienceRequiredForNextLevel() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Progression")
	bool IsAtMaxLevel() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Progression")
	float GetAbilityDamageMultiplier() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Run Upgrades")
	bool ApplyRunUpgrade(EAuraRunUpgradeType Type);

	UFUNCTION(BlueprintPure, Category = "Aura|Run Upgrades")
	int32 GetRunUpgradeRank(EAuraRunUpgradeType Type) const { return RunUpgradeState.GetRank(Type); }

	const FAuraRunUpgradeState& GetRunUpgradeState() const { return RunUpgradeState; }
	float GetFireBoltDamageMultiplier() const;
	float GetArcaneShardsDamageMultiplier() const;
	float GetArcaneShardsRadiusMultiplier() const;
	float GetShockDamageMultiplier() const;
	float GetShockStunDurationBonus() const;
	bool IsRunUpgradeSelectionProtected() const { return bRunUpgradeSelectionProtected; }
	void SetRunUpgradeSelectionProtected(bool bProtected);

	virtual void NotifyCombatActivity() override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Camera")
	FAuraCameraModeSettings TopDownCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Camera", meta = (ClampMin = "0.1"))
	float CameraTransitionSpeed = 6.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Animation")
	TObjectPtr<UBlendSpace> MovementBlendSpace;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Animation")
	TObjectPtr<UAnimSequence> HitReactAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Combat|FireBolt",
		meta = (ClampMin = "90.0", ClampMax = "1800.0"))
	float FireBoltTurnRateDegreesPerSecond = 900.f;

private:
	UFUNCTION()
	void OnRep_ProgressionState(FAuraPlayerProgressionState OldState);

	UFUNCTION()
	void OnRep_RunUpgradeState(FAuraRunUpgradeState OldState);

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_OverloadBuffUntil();

	FVector GetDesiredCameraTargetOffset(const FAuraCameraModeSettings& Settings) const;
	int32 GetCumulativeExperienceForLevel(int32 Level) const;
	int32 ResolveLevelForExperience(int32 TotalExperience) const;
	void RestoreProgression(int32 SavedLevel, int32 SavedExperience);
	void RestoreRunUpgrades(const FAuraRunUpgradeState& SavedRunUpgrades);
	void ApplyProgressionStats(bool bRestoreVitals);
	void ShowProgressionFeedback(const FAuraPlayerProgressionState& OldState, int32 ExperienceGained);
	void OnHealthChanged(const FOnAttributeChangeData& ChangeData);
	void OnMoveSpeedChanged(const FOnAttributeChangeData& ChangeData);
	void OnSporeExposureChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void ApplyMovementSpeedBonus(float BonusSpeed);
	/** Tracks combat gating only. Mana never regenerates from combat state. */
	void SetCombatState(bool bNewInCombat, bool bForce = false);
	void ExitCombatState();
	void ApplyPlayerCombatBalance();
	void HandleDeath();
	void ApplyDeathPresentation();
	void ApplyRevivePresentation();
	void PruneComboMarks();
	void ExpireOverloadBuff();
	bool ApplyComboDamage(AAuraEnemy* Target, float Damage);
	void RefreshGrantedSkillAbility(EAuraPlayerSkill Skill);
	void SpawnProgressionFeedbackEffect(UNiagaraSystem* Effect, float MaximumLifetime,
		const TCHAR* CueName);

	UPROPERTY(VisibleAnywhere, Category = "Aura|Coop")
	TObjectPtr<UAuraReviveComponent> ReviveComponent;
	/** Camera yaw is independent of character facing in God View. */
	UPROPERTY(Replicated)
	FRotator SpectatorViewRotation = FRotator(-62.f, 0.f, 0.f);

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;
	UPROPERTY(ReplicatedUsing = OnRep_ProgressionState)
	FAuraPlayerProgressionState ProgressionState;
	UPROPERTY(ReplicatedUsing = OnRep_RunUpgradeState)
	FAuraRunUpgradeState RunUpgradeState;
	UPROPERTY(Replicated)
	bool bInCombat = false;
	UPROPERTY(Replicated)
	TArray<FAuraComboMark> ComboMarks;
	UPROPERTY(Replicated)
	FAuraSkillRankState SkillRanks;
	UPROPERTY(ReplicatedUsing = OnRep_OverloadBuffUntil)
	float OverloadBuffUntil = 0.f;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Combo")
	TObjectPtr<UNiagaraSystem> EmberBurstEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Combo")
	TObjectPtr<USoundBase> EmberBurstSound;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Combo")
	TObjectPtr<USoundBase> FractureChainSound;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Feedback")
	TObjectPtr<UNiagaraSystem> LevelUpEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Feedback")
	TObjectPtr<UNiagaraSystem> HealthRestoreEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Aura|Feedback")
	TObjectPtr<UNiagaraSystem> ManaRestoreEffect;
	int32 NextComboCastId = 1;
	TSet<int32> ConsumedComboCastIds;
	FTimerHandle ComboCleanupTimer;
	FTimerHandle OverloadBuffTimer;
	float OverloadBuffReadyAt = 0.f;
	bool bRunUpgradeSelectionProtected = false;
	bool bDeathPresentationApplied = false;
	bool bFireBoltTurning = false;
	bool bFireBoltPreviousOrientRotationToMovement = true;
	TWeakObjectPtr<AAuraEnemy> FireBoltTurnTarget;
	float BaseWalkSpeed = 600.f;
	FTransform InitialMeshRelativeTransform = FTransform::Identity;
	FTimerHandle CombatExitTimer;
};
