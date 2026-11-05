// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AuraMeleeAbility.generated.h"

class USoundBase;
class AAuraCharacter;
class AAuraEnemy;
enum class EAuraMeleeAction : uint8;

UCLASS()
class AURA_API UAuraMeleeAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraMeleeAbility();
	float GetDamage() const;
	float GetImpactTime() const;
	float GetHitRange() const;
	float GetAttackInterval() const;
	/** Shared by the attack resolver and deterministic M19 regression. */
	static float ResolveSporeSynergyDamage(AAuraCharacter& Target, EAuraMeleeAction Action,
		int32 HitIndex, float BaseDamage, bool& bOutDetonated);

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UFUNCTION()
	void ResolveMeleeImpact();

	UFUNCTION()
	void FinishMeleeAttack();

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Melee")
	TObjectPtr<USoundBase> AttackSound;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Melee")
	TObjectPtr<USoundBase> SpearSound;

private:
	void SelectAttack(AAuraEnemy& Enemy, const AAuraCharacter& Target);
	bool RollSpecialAttack(float Probability, int32 MaxNormalAttacks);
	void ApplyDamageToTarget(AAuraEnemy& Enemy, AAuraCharacter& Target, float Damage) const;
	bool TryHitTarget(AAuraEnemy& Enemy, AAuraCharacter& Target, float Damage,
		float HitRange, float MinimumForwardDot, const FVector& ImpactOrigin,
		const FVector& LockedDirection = FVector::ZeroVector);
	void ResolveShroomSporeBurst(AAuraEnemy& Enemy);
	void BeginEliteGoblinSweep(AAuraEnemy& Enemy, AAuraCharacter& Target);
	void PerformGoblinJabStep(AAuraEnemy& Enemy);
	void PerformGoblinLunge(AAuraEnemy& Enemy);
	FVector FindGroundLocation(const AActor* IgnoreActor, const FVector& AroundLocation) const;
	const TCHAR* GetActiveActionName() const;

	TWeakObjectPtr<AAuraCharacter> LockedTarget;
	FVector LockedAimLocation = FVector::ZeroVector;
	FVector ActiveImpactOrigin = FVector::ZeroVector;
	FVector ActiveSeedLocation = FVector::ZeroVector;
	FVector ActiveSweepDirection = FVector::ZeroVector;
	EAuraMeleeAction ActiveAction;
	float ActiveDamage = 0.f;
	float ActiveImpactTime = 0.f;
	float ActiveAttackInterval = 0.f;
	float ActiveHitRange = 0.f;
	float ActiveMinimumForwardDot = -1.f;
	float ActiveSecondHitDelay = 0.f;
	int32 AttackSequence = 0;
	int32 ActiveHitIndex = 0;
	int32 NormalAttacksSinceSpecial = 0;
	bool bLastAttackWasSpecial = false;
	float LastSpecialRoll = 1.f;
	float NextAllowedActivationTime = 0.f;
	float NextEliteShroomSeedReadyTime = 0.f;
	float NextEliteGoblinSweepReadyTime = 0.f;
};
