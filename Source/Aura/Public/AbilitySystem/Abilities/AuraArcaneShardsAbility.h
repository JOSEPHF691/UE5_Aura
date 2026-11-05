// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraConfigurableGameplayAbility.h"
#include "AuraArcaneShardsAbility.generated.h"

class UAnimSequence;
class UNiagaraSystem;
class USoundBase;
class AAuraCharacterBase;
struct FGameplayAbilityTargetDataHandle;

/** Ground-targeted area spell: press Q to preview and click LMB to erupt at the selected point. */
UCLASS()
class AURA_API UAuraArcaneShardsAbility : public UAuraConfigurableGameplayAbility
{
	GENERATED_BODY()

public:
	UAuraArcaneShardsAbility();

	float GetCooldownRemaining() const;
	float GetCooldownDuration() const;
	float GetManaCost() const;
	float GetDamage() const;
	float GetEffectRadius() const;
	float GetCastRange() const;
	float GetCastDelay() const;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	void PlayCastPresentation(AAuraCharacterBase* Character) const;
	void PlayImpactPresentation(UObject* WorldContextObject, const FVector& ImpactLocation) const;

protected:
	UFUNCTION()
	void EruptShardsAndFinish();

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Arcane Shards")
	TObjectPtr<UAnimSequence> CastAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Arcane Shards")
	TObjectPtr<UNiagaraSystem> ShardsEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Arcane Shards")
	TObjectPtr<USoundBase> CastSound;

	virtual float GetConfiguredManaCost() const override { return GetManaCost(); }
	virtual float GetConfiguredCooldownDuration() const override { return GetCooldownDuration(); }

private:
	void HandleTargetData(const FGameplayAbilityTargetDataHandle& TargetData, FGameplayTag ActivationTag);
	FVector LockedImpactLocation = FVector::ZeroVector;
};
