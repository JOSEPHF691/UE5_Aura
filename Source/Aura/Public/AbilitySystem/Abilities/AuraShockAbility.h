// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraConfigurableGameplayAbility.h"
#include "AuraShockAbility.generated.h"

class AAuraEnemy;
class AAuraCharacterBase;
class UAnimSequence;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundBase;
struct FGameplayAbilityTargetDataHandle;

/** Mouse-directed control spell: shocks the first enemy in a narrow confirmed sweep. */
UCLASS()
class AURA_API UAuraShockAbility : public UAuraConfigurableGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraShockAbility();

	float GetCooldownRemaining() const;
	float GetCooldownDuration() const;
	float GetManaCost() const;
	float GetDamage() const;
	float GetStunDuration() const;
	float GetCastRange() const;
	float GetAimAssistRadius() const;
	float GetImpactDelay() const;
	float GetEffectLifetime() const;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	void PlayCastPresentation(AAuraCharacterBase* Character) const;
	void PlayImpactPresentation(UObject* WorldContextObject,
		const FVector& BeamStart, const FVector& BeamEnd,
		bool bHasImpact, const FVector& ImpactLocation) const;

protected:
	UFUNCTION()
	void ImpactShockAndFinish();

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Shock")
	TObjectPtr<UAnimSequence> CastAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Shock")
	TObjectPtr<UNiagaraSystem> BeamEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Shock")
	TObjectPtr<UNiagaraSystem> ImpactEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Shock")
	TObjectPtr<USoundBase> CastSound;

	virtual float GetConfiguredManaCost() const override { return GetManaCost(); }
	virtual float GetConfiguredCooldownDuration() const override { return GetCooldownDuration(); }

private:
	void HandleTargetData(const FGameplayAbilityTargetDataHandle& TargetData, FGameplayTag ActivationTag);
	AAuraEnemy* FindFirstEnemyInFacingDirection(const FVector& TraceStart, const FVector& TraceEnd) const;
	void ScheduleEffectCleanup(UNiagaraComponent* EffectComponent) const;

	FVector LockedCastDirection = FVector::ForwardVector;
	FVector LockedGroundLocation = FVector::ZeroVector;
};
