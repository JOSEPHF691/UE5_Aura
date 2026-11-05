// Copyright yosep

#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

namespace
{
	FGameplayEffectModifierMagnitude MakeSetByCallerMagnitude(const TCHAR* TagName)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = FGameplayTag::RequestGameplayTag(FName(TagName));
		return FGameplayEffectModifierMagnitude(SetByCaller);
	}

	void AddManaCost(UGameplayEffect& Effect)
	{
		Effect.DurationPolicy = EGameplayEffectDurationType::Instant;

		FGameplayModifierInfo ManaModifier;
		ManaModifier.Attribute = UAuraAttributeSet::GetManaAttribute();
		ManaModifier.ModifierOp = EGameplayModOp::Additive;
		ManaModifier.ModifierMagnitude = MakeSetByCallerMagnitude(TEXT("Data.ManaCost"));
		Effect.Modifiers.Add(ManaModifier);
	}

	void AddInstantRestore(UGameplayEffect& Effect, const FGameplayAttribute& Attribute)
	{
		Effect.DurationPolicy = EGameplayEffectDurationType::Instant;

		FGameplayModifierInfo RestoreModifier;
		RestoreModifier.Attribute = Attribute;
		RestoreModifier.ModifierOp = EGameplayModOp::Additive;
		RestoreModifier.ModifierMagnitude = MakeSetByCallerMagnitude(TEXT("Data.Restore"));
		Effect.Modifiers.Add(RestoreModifier);
	}

	void AddEquipmentModifier(UGameplayEffect& Effect,
		const FGameplayAttribute& Attribute, const TCHAR* StatTag)
	{
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = MakeSetByCallerMagnitude(StatTag);
		Effect.Modifiers.Add(Modifier);
	}

	void ConfigureDurationTag(UGameplayEffect& Effect, UTargetTagsGameplayEffectComponent& TargetTagsComponent,
		const TCHAR* DurationDataTag, const TCHAR* GrantedTagName)
	{
		Effect.DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Effect.DurationMagnitude = MakeSetByCallerMagnitude(DurationDataTag);

		FInheritedTagContainer GrantedTags;
		GrantedTags.Added.AddTag(FGameplayTag::RequestGameplayTag(FName(GrantedTagName)));
		TargetTagsComponent.SetAndApplyTargetTagChanges(GrantedTags);
	}
}

UAuraManaCostEffect::UAuraManaCostEffect()
{
	AddManaCost(*this);
}


UAuraFireBoltCooldownEffect::UAuraFireBoltCooldownEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("CooldownTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent,
		TEXT("Data.CooldownDuration"), TEXT("Cooldown.FireBolt"));
}

UAuraArcaneShardsCooldownEffect::UAuraArcaneShardsCooldownEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("CooldownTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent,
		TEXT("Data.CooldownDuration"), TEXT("Cooldown.ArcaneShards"));
}

UAuraShockCooldownEffect::UAuraShockCooldownEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("CooldownTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent,
		TEXT("Data.CooldownDuration"), TEXT("Cooldown.Shock"));
}

UAuraShockStunEffect::UAuraShockStunEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("StateTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent,
		TEXT("Data.Duration"), TEXT("State.Stunned"));
}

UAuraArcaneSlowEffect::UAuraArcaneSlowEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("StateTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent,
		TEXT("Data.Duration"), TEXT("State.Slowed"));
}

UAuraSporeExposureEffect::UAuraSporeExposureEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("StateTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent,
		TEXT("Data.Duration"), TEXT("State.SporeExposed"));
}

UAuraShamanBoltCooldownEffect::UAuraShamanBoltCooldownEffect(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this, TEXT("CooldownTags"));
	GEComponents.Add(TargetTagsComponent);
	ConfigureDurationTag(*this, *TargetTagsComponent, TEXT("Data.CooldownDuration"),
		TEXT("Cooldown.Enemy.ShamanBolt"));
}

UAuraHealthPickupEffect::UAuraHealthPickupEffect()
{
	AddInstantRestore(*this, UAuraAttributeSet::GetHealthAttribute());
}

UAuraManaPickupEffect::UAuraManaPickupEffect()
{
	AddInstantRestore(*this, UAuraAttributeSet::GetManaAttribute());
}

UAuraEquipmentAttributeEffect::UAuraEquipmentAttributeEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	AddEquipmentModifier(*this, UAuraAttributeSet::GetMaxHealthAttribute(),
		TEXT("Item.Stat.MaxHealth"));
	AddEquipmentModifier(*this, UAuraAttributeSet::GetMaxManaAttribute(),
		TEXT("Item.Stat.MaxMana"));
	AddEquipmentModifier(*this, UAuraAttributeSet::GetStrengthAttribute(),
		TEXT("Item.Stat.Strength"));
	AddEquipmentModifier(*this, UAuraAttributeSet::GetIntelligenceAttribute(),
		TEXT("Item.Stat.Intelligence"));
	AddEquipmentModifier(*this, UAuraAttributeSet::GetArmorAttribute(),
		TEXT("Item.Stat.Armor"));
	AddEquipmentModifier(*this, UAuraAttributeSet::GetMoveSpeedAttribute(),
		TEXT("Item.Stat.MoveSpeed"));
}
