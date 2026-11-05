// Copyright yosep

#include "AbilitySystem/Effects/AuraDamageGameplayEffect.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "GameplayTagsManager.h"

UAuraDamageGameplayEffect::UAuraDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat DamageMagnitude;
	DamageMagnitude.DataTag = FGameplayTag::RequestGameplayTag(TEXT("Data.Damage"));

	FGameplayModifierInfo HealthModifier;
	HealthModifier.Attribute = UAuraAttributeSet::GetHealthAttribute();
	HealthModifier.ModifierOp = EGameplayModOp::Additive;
	HealthModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(DamageMagnitude);
	Modifiers.Add(HealthModifier);
}
