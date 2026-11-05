// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraConfigurableGameplayAbility.h"
#include "AuraShamanBoltAbility.generated.h"

class AAuraFireBoltProjectile;
class USoundBase;

/**
 * Server-authoritative, dodgeable ranged attack used by Shaman enemies.
 * The target point is locked before the warning appears, so moving during the
 * windup can avoid the projectile. State.Stunned cancels the active cast.
 */
UCLASS()
class AURA_API UAuraShamanBoltAbility : public UAuraConfigurableGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraShamanBoltAbility();

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

	float GetDamage() const;
	float GetWindupTime() const;
	float GetCastRange() const;
	float GetCooldownDuration() const;
	float GetVolleyChance() const;
	int32 GetMaxNormalCastsBeforeVolley() const;
	int32 GetVolleyProjectileCount() const;
	float GetVolleySpreadDegrees() const;
	float GetVolleyDamageMultiplier() const;
	float GetVolleyWindupBonus() const;
	float GetGroundBurstChance() const;
	int32 GetMaxNormalCastsBeforeGroundBurst() const;
	float GetGroundBurstDamage() const;
	float GetGroundBurstRadius() const;
	float GetGroundBurstWindup() const;
	float GetGroundBurstCooldown() const;
	/** Deterministic smoke-test hook; never called by runtime AI. */
	void ForceNextGroundBurstForTest() { bForceNextGroundBurstForTest = true; }

protected:
	UFUNCTION()
	void ReleaseProjectile();

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Shaman")
	TSubclassOf<AAuraFireBoltProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Shaman")
	TObjectPtr<USoundBase> CastSound;

	virtual float GetConfiguredCooldownDuration() const override { return GetCooldownDuration(); }

private:
	void SpawnProjectile(class AAuraEnemy& Enemy, const FVector& SpawnLocation,
		const FVector& LaunchDirection, float Damage) const;
	void ResolveGroundBurst(class AAuraEnemy& Enemy) const;

	FVector LockedAimLocation = FVector::ZeroVector;
	FVector LockedGroundBurstLocation = FVector::ZeroVector;
	int32 CastSequence = 0;
	int32 NormalCastsSinceVolley = 0;
	int32 NormalCastsSinceGroundBurst = 0;
	bool bActiveCastIsVolley = false;
	bool bActiveCastIsGroundBurst = false;
	bool bLastCastWasSpecial = false;
	float LastVolleyRoll = 1.f;
	float LastGroundBurstRoll = 1.f;
	float NextGroundBurstReadyTime = 0.f;
	bool bForceNextGroundBurstForTest = false;
};
