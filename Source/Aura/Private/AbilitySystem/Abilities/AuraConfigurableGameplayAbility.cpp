// Copyright yosep

#include "AbilitySystem/Abilities/AuraConfigurableGameplayAbility.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Coop/AuraReviveComponent.h"
#include "GameplayEffect.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraAbilityCost, Log, All);

namespace
{
	const FGameplayTag& GetManaCostTag()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(TEXT("Data.ManaCost"));
		return Tag;
	}

	const FGameplayTag& GetCooldownDurationTag()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(TEXT("Data.CooldownDuration"));
		return Tag;
	}
}

bool UAuraConfigurableGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const AAuraCharacter* Character = ActorInfo ? Cast<AAuraCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (Character && (Character->IsDead() || Character->GetReviveComponent()->IsRescuing())) return false;
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

bool UAuraConfigurableGameplayAbility::CheckCost(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	const float ManaCost = FMath::Max(0.f, GetConfiguredManaCost());
	if (ManaCost <= KINDA_SMALL_NUMBER) return true;
	if (!CostGameplayEffectClass)
	{
		UE_LOG(LogAuraAbilityCost, Error,
			TEXT("Ability %s has a %.1f mana cost but no Cost GameplayEffect; activation rejected."),
			*GetName(), ManaCost);
		return false;
	}

	const UAbilitySystemComponent* AbilitySystem = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return AbilitySystem
		&& AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute())
			+ KINDA_SMALL_NUMBER >= ManaCost;
}

void UAuraConfigurableGameplayAbility::ApplyCost(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const float ManaCost = FMath::Max(0.f, GetConfiguredManaCost());
	if (ManaCost <= KINDA_SMALL_NUMBER) return;
	if (!CostGameplayEffectClass)
	{
		UE_LOG(LogAuraAbilityCost, Error,
			TEXT("Ability %s could not apply its %.1f mana cost: Cost GameplayEffect is missing."),
			*GetName(), ManaCost);
		return;
	}

	UAbilitySystemComponent* AbilitySystem = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const float ManaBefore = AbilitySystem
		? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;

	FGameplayEffectSpecHandle CostSpec = MakeOutgoingGameplayEffectSpec(
		Handle, ActorInfo, ActivationInfo, CostGameplayEffectClass,
		GetAbilityLevel(Handle, ActorInfo));
	if (!CostSpec.IsValid())
	{
		UE_LOG(LogAuraAbilityCost, Error,
			TEXT("Ability %s could not create its mana cost spec."), *GetName());
		return;
	}

	CostSpec.Data->SetSetByCallerMagnitude(GetManaCostTag(), -ManaCost);
	const float ResolvedMagnitude = CostSpec.Data->GetSetByCallerMagnitude(
		GetManaCostTag(), false, BIG_NUMBER);
	const int32 ModifierCount = CostSpec.Data->Def
		? CostSpec.Data->Def->Modifiers.Num() : 0;
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, CostSpec);

	float ManaAfter = AbilitySystem
		? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
	bool bApplied = AbilitySystem
		&& FMath::IsNearlyEqual(ManaBefore - ManaAfter, ManaCost, 0.05f);
	const bool bHasAuthority = ActorInfo && ActorInfo->AvatarActor.IsValid()
		&& ActorInfo->AvatarActor->HasAuthority();
	if (!bHasAuthority)
	{
		// A predicting client can be reconciled by an attribute replication update in
		// the same frame, so its immediate delta is not an authoritative cost audit.
		UE_LOG(LogAuraAbilityCost, Verbose,
			TEXT("Predicted mana cost submitted. Ability=%s Cost=%.1f LocalManaBefore=%.1f LocalManaAfter=%.1f"),
			*GetName(), ManaCost, ManaBefore, ManaAfter);
		return;
	}
	if (!bApplied && AbilitySystem && bHasAuthority)
	{
		// Instant SetByCaller effects can be ignored during the ASC's first-frame
		// initialization window even though the spec is valid. Never allow a
		// successful authoritative cast to escape its configured resource cost.
		AbilitySystem->SetNumericAttributeBase(
			UAuraAttributeSet::GetManaAttribute(), ManaBefore - ManaCost);
		ManaAfter = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
		bApplied = FMath::IsNearlyEqual(ManaBefore - ManaAfter, ManaCost, 0.05f);
		UE_LOG(LogAuraAbilityCost, Warning,
			TEXT("GameplayEffect cost required authoritative fallback. Ability=%s Cost=%.1f ManaBefore=%.1f ManaAfter=%.1f"),
			*GetName(), ManaCost, ManaBefore, ManaAfter);
	}
	UE_CLOG(!bApplied, LogAuraAbilityCost, Error,
		TEXT("Ability mana cost audit failed. Ability=%s Effect=%s Cost=%.1f SetByCaller=%.1f Modifiers=%d ManaBefore=%.1f ManaAfter=%.1f Authority=%s"),
		*GetName(), *GetNameSafe(CostSpec.Data->Def), ManaCost, ResolvedMagnitude,
		ModifierCount, ManaBefore, ManaAfter,
		bHasAuthority ? TEXT("true") : TEXT("false"));
	UE_CLOG(bApplied, LogAuraAbilityCost, Display,
		TEXT("Ability mana cost applied. Ability=%s Effect=%s Cost=%.1f SetByCaller=%.1f Modifiers=%d ManaBefore=%.1f ManaAfter=%.1f"),
		*GetName(), *GetNameSafe(CostSpec.Data->Def), ManaCost, ResolvedMagnitude,
		ModifierCount, ManaBefore, ManaAfter);
}

void UAuraConfigurableGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const float CooldownDuration = FMath::Max(0.f, GetConfiguredCooldownDuration());
	if (CooldownDuration <= KINDA_SMALL_NUMBER || !CooldownGameplayEffectClass) return;

	FGameplayEffectSpecHandle CooldownSpec = MakeOutgoingGameplayEffectSpec(
		Handle, ActorInfo, ActivationInfo, CooldownGameplayEffectClass,
		GetAbilityLevel(Handle, ActorInfo));
	if (!CooldownSpec.IsValid()) return;

	CooldownSpec.Data->SetSetByCallerMagnitude(
		GetCooldownDurationTag(), CooldownDuration);
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, CooldownSpec);
}
