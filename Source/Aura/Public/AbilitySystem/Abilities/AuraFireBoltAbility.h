// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraConfigurableGameplayAbility.h"
#include "AuraFireBoltAbility.generated.h"

class AAuraFireBoltProjectile;
class AAuraCharacterBase;
class AAuraEnemy;
class UAnimSequence;
class USoundBase;
struct FGameplayAbilityTargetDataHandle;

UCLASS()
class AURA_API UAuraFireBoltAbility : public UAuraConfigurableGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraFireBoltAbility();

	float GetCooldownRemaining() const;
	float GetCooldownDuration() const;
	float GetManaCost() const;
	float GetDamage() const;
	float GetProjectileDelay() const;
	/** Shared by cursor feedback and both sides of the cast: only a visible, in-range enemy may be locked. */
	static bool CanLockTarget(const AAuraCharacterBase* Character, const AAuraEnemy* Target);

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	void PlayCastPresentation(AAuraCharacterBase* Character) const;

protected:
	UFUNCTION()
	void SpawnProjectileAndFinish();

	UPROPERTY(EditDefaultsOnly, Category = "Aura|FireBolt")
	TSubclassOf<AAuraFireBoltProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|FireBolt")
	TObjectPtr<UAnimSequence> CastAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|FireBolt")
	TObjectPtr<USoundBase> CastSound;

	virtual float GetConfiguredManaCost() const override { return GetManaCost(); }
	virtual float GetConfiguredCooldownDuration() const override { return GetCooldownDuration(); }

private:
	void HandleTargetData(const FGameplayAbilityTargetDataHandle& TargetData,
		FGameplayTag ActivationTag);
	AAuraEnemy* FindClientTarget(AAuraCharacterBase* Character) const;
	FVector LockedCastDirection = FVector::ForwardVector;
	TWeakObjectPtr<AAuraEnemy> LockedTarget;
	bool bHadLockedTarget = false;
};
