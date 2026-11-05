// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Character/AuraEnemy.h"
#include "AuraBossAbility.generated.h"

class AAuraCharacter;
class AAuraFireBoltProjectile;
class USoundBase;

/**
 * Server-authoritative Demon Lord attack brain. Phase one establishes the claw pattern;
 * phase two uses weighted, anti-repeat choices, including a timed fireball barrage.
 * State.Stunned cancels an ordinary windup through AAuraEnemy::BeginStun.
 */
UCLASS()
class AURA_API UAuraBossAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraBossAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	float GetPhaseOneClawDamage() const;
	float GetPhaseTwoClawDamage() const;
	float GetTailSpinDamage() const;
	float GetThrowDamage() const;
	float GetChargeDamage() const;
	float GetClawImpactTime() const;
	float GetTailSpinWindup() const;
	float GetThrowWindup() const;
	float GetChargeWindup() const;
	float GetSummonWindup() const;
	float GetRecoveryTime() const;
	float GetPhaseOneAttackInterval() const;
	float GetPhaseTwoAttackInterval() const;
	int32 GetBarrageShotsFired() const { return BarrageShotsFired; }
	float GetBarrageAimYawOffset() const { return BarrageAimYawOffset; }
	bool DidBarrageProjectileSpawnFail() const { return bBarrageProjectileSpawnFailed; }

protected:
	UFUNCTION()
	void ResolveActiveAction();

	UFUNCTION()
	void FinishActiveAction();

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Throw")
	TSubclassOf<AAuraFireBoltProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> ClawSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> TailSpinSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> ThrowSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> SummonSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Boss|Audio")
	TObjectPtr<USoundBase> ChargeSound;

private:
	EAuraBossAction SelectAction(AAuraEnemy& Boss, float DistanceToTarget);
	EAuraBossAction SelectAlternatingClaw();
	void ApplyDamageToTarget(AAuraCharacter* Target, float Damage) const;
	void ResolveClawCleave(AAuraEnemy& Boss);
	void ResolveDemonicCharge(AAuraEnemy& Boss);
	void ResolveSummonMinions(AAuraEnemy& Boss);
	void FireNextBarrageShot();
	void BeginRecovery();
	int32 CountActiveSummons(const AAuraEnemy& Boss) const;
	bool CanSummon(const AAuraEnemy& Boss) const;
	FVector FindGroundLocation(const AActor* IgnoreActor, const FVector& AroundLocation) const;

	TWeakObjectPtr<AAuraCharacter> LockedTarget;
	FVector LockedAimLocation = FVector::ZeroVector;
	EAuraBossAction ActiveAction = EAuraBossAction::None;
	float NextAllowedActivationTime = 0.f;
	float NextAllowedSummonTime = 0.f;
	bool bNextClawIsLeft = true;
	bool bForceClawAfterCharge = false;
	int32 ActionsWithoutClaw = 0;
	int32 CloseActionsWithoutTail = 0;
	int32 ActionsWithoutSummon = 0;
	int32 FarActionsWithoutThrow = 0;
	int32 SummonClassCursor = 0;
	int32 WardCastsUsed = 0;
	float NextAllowedWardTime = 0.f;
	float NextAllowedBarrageTime = 0.f;
	EAuraBossAction PreviousAction = EAuraBossAction::None;
	FVector ChargeStartLocation = FVector::ZeroVector;
	FVector BarrageForward = FVector::ForwardVector;
	float BarrageAimYawOffset = 0.f;
	int32 BarrageShotsFired = 0;
	bool bBarrageProjectileSpawnFailed = false;
	FTimerHandle BarrageShotTimer;
};
