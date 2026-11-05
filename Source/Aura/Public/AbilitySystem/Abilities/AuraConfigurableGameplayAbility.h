// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AuraConfigurableGameplayAbility.generated.h"

/** Shared SetByCaller cost/cooldown bridge for values read from DA_CombatBalance. */
UCLASS(Abstract)
class AURA_API UAuraConfigurableGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

protected:
	virtual float GetConfiguredManaCost() const { return 0.f; }
	virtual float GetConfiguredCooldownDuration() const { return 0.f; }
};
