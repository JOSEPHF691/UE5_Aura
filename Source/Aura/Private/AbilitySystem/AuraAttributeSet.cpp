// Copyright yosep

#include "AbilitySystem/AuraAttributeSet.h"

#include "GameplayEffectExtension.h"
#include "AbilitySystem/AuraElementalStatusComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Coop/AuraReviveComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Net/UnrealNetwork.h"

UAuraAttributeSet::UAuraAttributeSet()
{
	// Constructors may run before content packages are available. Use the native
	// fallback here; AAuraCharacter applies the authored Data Asset on BeginPlay.
	const FAuraPlayerCombatBalance& PlayerBalance =
		GetDefault<UAuraCombatBalanceData>()->Player;
	InitMaxHealth(PlayerBalance.MaxHealth);
	InitHealth(PlayerBalance.MaxHealth);
	InitMaxMana(PlayerBalance.MaxMana);
	InitMana(PlayerBalance.MaxMana);
	InitStrength(PlayerBalance.Strength);
	InitIntelligence(PlayerBalance.Intelligence);
	InitArmor(PlayerBalance.Armor);
	InitMoveSpeed(0.f);
}

void UAuraAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Strength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Intelligence, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Armor, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
}

void UAuraAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	if (Attribute == GetManaAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxManaAttribute()) NewValue = FMath::Max(0.f, NewValue);
	if (Attribute == GetStrengthAttribute() || Attribute == GetIntelligenceAttribute()
		|| Attribute == GetArmorAttribute()) NewValue = FMath::Max(0.f, NewValue);
	if (Attribute == GetMoveSpeedAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, 250.f);
}

float UAuraAttributeSet::CalculateMitigatedDamage(
	const float RawDamage, const float ArmorValue)
{
	const float SafeDamage = FMath::Max(0.f, RawDamage);
	const float SafeArmor = FMath::Clamp(ArmorValue, 0.f, 1000.f);
	return SafeDamage * 100.f / (100.f + SafeArmor);
}

bool UAuraAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data)) return false;
	if (Data.EvaluatedData.Attribute == GetHealthAttribute()
		&& Data.EvaluatedData.Magnitude < 0.f)
	{
		const AAuraCharacter* TargetCharacter = Cast<AAuraCharacter>(GetOwningActor());
		if (TargetCharacter && (TargetCharacter->IsDead()
			|| TargetCharacter->IsRunUpgradeSelectionProtected()
			|| TargetCharacter->GetReviveComponent()->IsProtected()))
		{
			return false;
		}
		AAuraEnemy* TargetEnemy = Cast<AAuraEnemy>(GetOwningActor());
		if (TargetEnemy && TargetEnemy->IsDamageImmune())
		{
			return false;
		}

		FGameplayTagContainer EffectTags;
		Data.EffectSpec.GetAllAssetTags(EffectTags);
		const FGameplayTag BypassArmorTag =
			FGameplayTag::RequestGameplayTag(TEXT("Damage.BypassArmor"));
		if (!EffectTags.HasTagExact(BypassArmorTag))
		{
			Data.EvaluatedData.Magnitude = -CalculateMitigatedDamage(
				-Data.EvaluatedData.Magnitude, GetArmor());
		}
		if (TargetEnemy && TargetEnemy->HasBossWard())
		{
			const float HealthDamage = TargetEnemy->AbsorbBossWardDamage(
				-Data.EvaluatedData.Magnitude);
			if (HealthDamage <= KINDA_SMALL_NUMBER) return false;
			Data.EvaluatedData.Magnitude = -HealthDamage;
		}
		if (TargetEnemy && !EffectTags.HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("Damage.ElementalSecondary")))
			&& Cast<AAuraCharacter>(Data.EffectSpec.GetContext().GetOriginalInstigator()))
			TargetEnemy->GetElementalStatus()->OnDirectDamage(-Data.EvaluatedData.Magnitude);
	}
	return true;
}

void UAuraAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		const float ClampedHealth = FMath::Clamp(GetHealth(), 0.f, GetMaxHealth());
		if (!FMath::IsNearlyEqual(GetHealth(), ClampedHealth)) SetHealth(ClampedHealth);
		if (ClampedHealth > 0.f && Data.EvaluatedData.Magnitude < 0.f)
		{
			if (AAuraEnemy* HitEnemy = Cast<AAuraEnemy>(GetOwningActor()))
			{
				const float PoiseDamage = Data.EffectSpec.GetSetByCallerMagnitude(
					FGameplayTag::RequestGameplayTag(TEXT("Data.PoiseDamage")), false, 0.f);
				if (PoiseDamage > 0.f) HitEnemy->ApplyPoiseDamage(PoiseDamage);
			}
		}
		if (ClampedHealth <= 0.f && Data.EvaluatedData.Magnitude < 0.f)
		{
			if (AAuraEnemy* DefeatedEnemy = Cast<AAuraEnemy>(GetOwningActor()))
			{
				// A lethal phase-one hit is clamped to the Demon Lord's phase break by
				// AAuraEnemy. Do not grant its kill reward until phase two can truly die.
				if (DefeatedEnemy->IsBossEnemy()
					&& DefeatedEnemy->GetBossPhase() != EAuraBossPhase::PhaseTwo)
				{
					return;
				}
				AActor* DamageInstigator = Data.EffectSpec.GetContext().GetOriginalInstigator();
				if (!DamageInstigator)
				{
					DamageInstigator = Data.EffectSpec.GetContext().GetEffectCauser();
				}
				DefeatedEnemy->TryAwardKillRewards(DamageInstigator);
			}
		}
	}
	if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		const float ClampedMana = FMath::Clamp(GetMana(), 0.f, GetMaxMana());
		if (!FMath::IsNearlyEqual(GetMana(), ClampedMana)) SetMana(ClampedMana);
	}
}

void UAuraAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Health, OldHealth); }
void UAuraAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MaxHealth, OldMaxHealth); }
void UAuraAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldMana) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Mana, OldMana); }
void UAuraAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MaxMana, OldMaxMana); }
void UAuraAttributeSet::OnRep_Strength(const FGameplayAttributeData& OldStrength) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Strength, OldStrength); }
void UAuraAttributeSet::OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Intelligence, OldIntelligence); }
void UAuraAttributeSet::OnRep_Armor(const FGameplayAttributeData& OldArmor) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Armor, OldArmor); }
void UAuraAttributeSet::OnRep_MoveSpeed(const FGameplayAttributeData& OldMoveSpeed) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MoveSpeed, OldMoveSpeed); }
