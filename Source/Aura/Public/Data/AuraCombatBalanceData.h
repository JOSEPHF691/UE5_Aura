// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/AuraItemTypes.h"
#include "AuraCombatBalanceData.generated.h"

UENUM(BlueprintType)
enum class EAuraEncounterBalanceProfile : uint8
{
	Custom UMETA(DisplayName = "Custom / Use All Spawn Points"),
	OuterGuard UMETA(DisplayName = "Outer Guard"),
	EliteShaman UMETA(DisplayName = "Elite Shaman"),
	DemonLord UMETA(DisplayName = "Demon Lord")
};

USTRUCT(BlueprintType)
struct FAuraPlayerCombatBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitals", meta = (ClampMin = "1.0"))
	float MaxMana = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float Strength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float Intelligence = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float Armor = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float CombatExitDelay = 3.f;
};

USTRUCT(BlueprintType)
struct FAuraPlayerProgressionBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Leveling", meta = (ClampMin = "2"))
	int32 MaxLevel = 10;

	/** Legacy five-level curve fields retained so existing data assets can migrate safely. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Leveling", meta = (ClampMin = "1"))
	int32 BaseExperienceToNextLevel = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Leveling", meta = (ClampMin = "0"))
	int32 ExperienceGrowthPerLevel = 40;

	/** Entry 0 is L1->L2; nine entries define the complete L1->L10 route. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Leveling")
	TArray<int32> ExperienceToNextLevel = {60, 100, 140, 180, 210, 230, 250, 270, 290};

	int32 GetExperienceToNextLevel(int32 CurrentLevel) const
	{
		return ExperienceToNextLevel.IsValidIndex(CurrentLevel - 1)
			? ExperienceToNextLevel[CurrentLevel - 1] : MAX_int32;
	}

	int32 GetCumulativeExperienceForLevel(int32 Level) const
	{
		int32 Total = 0;
		for (int32 Current = 1; Current < FMath::Min(Level, MaxLevel); ++Current)
		{
			Total += GetExperienceToNextLevel(Current);
		}
		return Total;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.0"))
	float MaxHealthPerLevel = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.0"))
	float MaxManaPerLevel = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.0"))
	float StrengthPerLevel = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.0"))
	float IntelligencePerLevel = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.0"))
	float ArmorPerLevel = 1.f;

	/** 0.04 means each level above one adds four percent to all player ability damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Growth", meta = (ClampMin = "0.0"))
	float AbilityDamageBonusPerLevel = 0.04f;
};

/** Personal XP/soul rewards. Levels are earned automatically from XP. */
USTRUCT(BlueprintType)
struct FAuraGrowthEconomyBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int32 CommonKillSouls = 12;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int32 EliteKillSouls = 38;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int32 BossKillSouls = 125;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int32 EliteRoomCatchUpExperience = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int32 SealRoomClearSouls = 20;
};

/** Level-gated camp offers, four-slot enhancement, and metal crafting. Offer IDs are server-owned. */
USTRUCT(BlueprintType)
struct FAuraCampEconomyBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "1"))
	int32 PotionSoulPrice = 18;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "1"))
	int32 CommonEquipmentSoulPrice = 45;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "1"))
	int32 QualityEquipmentSoulPrice = 80;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "1"))
	int32 FineEquipmentSoulPrice = 130;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "1"))
	int32 SmithMetalSoulPrice = 12;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "0"))
	int32 SmithMetalPurchaseLimit = 12;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "1"))
	int32 RespecStoneSoulPrice = 100;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "0"))
	int32 RespecStonePurchaseLimit = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float SellFraction = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 ForgeOneSoulCost = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 ForgeTwoSoulCost = 40;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 ForgeThreeSoulCost = 70;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 ForgeOneMaterialCost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 ForgeTwoMaterialCost = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 ForgeThreeMaterialCost = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "0.01", ClampMax = "0.25"))
	float WeaponBaseStatBonusPerLevel = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "0.01", ClampMax = "0.25"))
	float ArmorBaseStatBonusPerLevel = 0.04f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 CraftMetalCost = 6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forge", meta = (ClampMin = "1"))
	int32 CraftSoulCost = 20;

	bool GetOffer(int32 OfferId, FName& OutItemId, EAuraItemRarity& OutRarity,
		int32& OutCost, int32& OutLimit, int32 PaidLevel = 1) const;
	int32 GetForgeSoulCost(int32 NextLevel) const;
	int32 GetForgeSoulCost(int32 NextLevel, EAuraEquipmentSlot Slot) const;
	int32 GetForgeMaterialCost(int32 NextLevel) const;
	int32 GetSellPrice(const FAuraItemInstance& Item) const;
};

USTRUCT(BlueprintType)
struct FAuraRunUpgradeBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "1"))
	int32 MaxRankPerUpgrade = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float FireBoltDamageBonusPerRank = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float ArcaneShardsDamageBonusPerRank = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float ArcaneShardsRadiusBonusPerRank = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float ShockDamageBonusPerRank = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float ShockStunBonusPerRank = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float MaxHealthPerRank = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float MaxManaPerRank = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Upgrades", meta = (ClampMin = "0.0"))
	float UniversalDamageBonusPerRank = 0.08f;
};

USTRUCT(BlueprintType)
struct FAuraFireBoltBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolt", meta = (ClampMin = "0.0"))
	float Damage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolt", meta = (ClampMin = "0.0"))
	float ManaCost = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolt", meta = (ClampMin = "0.0"))
	float Cooldown = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FireBolt", meta = (ClampMin = "0.0"))
	float ReleaseDelay = 0.28f;
};

USTRUCT(BlueprintType)
struct FAuraArcaneShardsBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arcane Shards", meta = (ClampMin = "0.0"))
	float Damage = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arcane Shards", meta = (ClampMin = "0.0"))
	float ManaCost = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arcane Shards", meta = (ClampMin = "0.0"))
	float Cooldown = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arcane Shards", meta = (ClampMin = "0.0"))
	float CastDelay = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arcane Shards", meta = (ClampMin = "100.0"))
	float CastRange = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arcane Shards", meta = (ClampMin = "1.0"))
	float EffectRadius = 300.f;
};

USTRUCT(BlueprintType)
struct FAuraShockBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "0.0"))
	float Damage = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "0.0"))
	float ManaCost = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "0.0"))
	float Cooldown = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "0.0"))
	float ImpactDelay = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "100.0"))
	float CastRange = 950.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "1.0"))
	float AimAssistRadius = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "0.0"))
	float StunDuration = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shock", meta = (ClampMin = "0.05"))
	float EffectLifetime = 0.45f;
};

/** Damage to the enemy's break gauge is authored per attack, not inferred from HP damage. */
USTRUCT(BlueprintType)
struct FAuraEnemyPoiseBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Enemy", meta = (ClampMin = "1.0"))
	float NormalMax = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Enemy", meta = (ClampMin = "1.0"))
	float EliteMax = 150.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Enemy", meta = (ClampMin = "1.0"))
	float ShamanMax = 135.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Enemy", meta = (ClampMin = "1.0"))
	float BossMax = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Player Hits", meta = (ClampMin = "0.0"))
	float FireBoltDamage = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Player Hits", meta = (ClampMin = "0.0"))
	float ArcaneShardsDamage = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Player Hits", meta = (ClampMin = "0.0"))
	float ShockDamage = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Player Hits", meta = (ClampMin = "0.0"))
	float ComboDamage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Recovery", meta = (ClampMin = "0.0"))
	float RegenDelay = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Recovery", meta = (ClampMin = "0.0"))
	float RegenPerSecond = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Recovery", meta = (ClampMin = "0.0"))
	float BreakImmunity = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Break", meta = (ClampMin = "0.05"))
	float NormalStaggerDuration = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Break", meta = (ClampMin = "0.05"))
	float EliteStaggerDuration = 0.42f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise|Break", meta = (ClampMin = "0.05"))
	float BossStaggerDuration = 0.32f;
};

/** Server-authoritative, same-caster hit-combo tuning. */
USTRUCT(BlueprintType)
struct FAuraComboBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "0.1"))
	float MarkDuration = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Ember", meta = (ClampMin = "1.0"))
	float EmberBurstRadius = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Ember", meta = (ClampMin = "0.0"))
	float EmberBurstDamageMultiplier = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Ember", meta = (ClampMin = "0.0"))
	float EmberExtraStackDamageMultiplier = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Ember", meta = (ClampMin = "1", ClampMax = "8"))
	int32 EmberMaxStacks = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "1.0"))
	float FractureChainRadius = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0.0"))
	float FractureChainDamageMultiplier = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0", ClampMax = "8"))
	int32 FractureChainMaxTargets = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0.0"))
	float FractureStaggerDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0.0"))
	float FractureStaggerImmunity = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FractureBossDamageReduction = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0.0"))
	float FractureBossWeakenDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Fracture", meta = (ClampMin = "0.0"))
	float FractureBossWeakenCooldown = 6.f;

	/** E hit -> FireBolt on the same target; one burst per FireBolt cast. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Overload", meta = (ClampMin = "1.0"))
	float OverloadRadius = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Overload", meta = (ClampMin = "0.0"))
	float OverloadDamageMultiplier = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Overload", meta = (ClampMin = "0.0"))
	float OverloadBuffDuration = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Overload", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float OverloadMoveSpeedBonusFraction = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo|Overload", meta = (ClampMin = "0.0"))
	float OverloadBuffCooldown = 8.f;
};

USTRUCT(BlueprintType)
struct FAuraSkillRankBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skill Ranks", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MaxRank = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 2", meta = (ClampMin = "0.0"))
	float FireBoltDamageBonus = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 2", meta = (ClampMin = "0.0"))
	float ArcaneShardsRadiusBonus = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 2", meta = (ClampMin = "0.0"))
	float ShockDamageBonus = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 3", meta = (ClampMin = "0", ClampMax = "2"))
	int32 FireBoltExtraPierces = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 3", meta = (ClampMin = "0.1"))
	float ArcaneShardsFieldDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 3", meta = (ClampMin = "50.0"))
	float ArcaneShardsFieldRadius = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rank 3", meta = (ClampMin = "0.0", ClampMax = "0.8"))
	float ArcaneShardsSlowFraction = 0.30f;
};

USTRUCT(BlueprintType)
struct FAuraElementalBalance
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) float SlowDuration = 3.f;
	UPROPERTY(EditAnywhere) float NormalSlow = 0.30f;
	UPROPERTY(EditAnywhere) float EliteSlow = 0.15f;
	UPROPERTY(EditAnywhere) float BossSlow = 0.08f;
	UPROPERTY(EditAnywhere) float CrystalDuration = 3.f;
	UPROPERTY(EditAnywhere) float EliteCrystalDuration = 1.5f;
	UPROPERTY(EditAnywhere) float CrystalHealthInFirebolts = 2.5f;
	UPROPERTY(EditAnywhere) float CrystalBurstScale = 0.6f;
	UPROPERTY(EditAnywhere) float CrystalBurstRadius = 230.f;
	UPROPERTY(EditAnywhere) float ParalysisDuration = 4.f;
	UPROPERTY(EditAnywhere) float ParalysisInterval = 1.2f;
	UPROPERTY(EditAnywhere) float NormalPulseStun = 0.2f;
	UPROPERTY(EditAnywhere) float ElitePulseStun = 0.1f;
	UPROPERTY(EditAnywhere) float ControlProtection = 0.8f;
	UPROPERTY(EditAnywhere) float EliteControlProtection = 1.5f;
	UPROPERTY(EditAnywhere) float IgniteChancePerRank = 0.04f;
	UPROPERTY(EditAnywhere) float IgniteBaseChance = 0.08f;
	UPROPERTY(EditAnywhere) float IgniteDuration = 3.f;
	UPROPERTY(EditAnywhere) float IgniteDamageScalePerSecond = 0.18f;
	UPROPERTY(EditAnywhere) float EmberDuration = 4.f;
	UPROPERTY(EditAnywhere) float EmberBurstScale = 0.4f;
	UPROPERTY(EditAnywhere) float EchoInterval = 0.45f;
	UPROPERTY(EditAnywhere) float EchoRadiusScale = 0.55f;
	UPROPERTY(EditAnywhere) float EchoSpacingScale = 1.35f;
	UPROPERTY(EditAnywhere) float StormDuration = 3.f;
	UPROPERTY(EditAnywhere) float StormInterval = 0.6f;
	UPROPERTY(EditAnywhere) float StormHalfAngle = 45.f;
	UPROPERTY(EditAnywhere) float StormDamageScale = 0.15f;
	UPROPERTY(EditAnywhere) float JudgmentRadius = 300.f;
	UPROPERTY(EditAnywhere) float JudgmentImpactRadius = 170.f;
	UPROPERTY(EditAnywhere) float JudgmentInterval = 0.6f;
	UPROPERTY(EditAnywhere) float JudgmentWindup = 0.25f;
	UPROPERTY(EditAnywhere) float JudgmentDamageScale = 0.5f;
};

USTRUCT(BlueprintType)
struct FAuraMeleeEnemyBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "1.0"))
	float WalkSpeed = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "0"))
	int32 KillExperienceReward = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "0.0"))
	float Damage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "0.0"))
	float ImpactTime = 0.88f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "50.0"))
	float HitRange = 205.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy", meta = (ClampMin = "0.0"))
	float AttackInterval = 1.35f;

	/** Rolled independently on eligible attacks; anti-repeat and pity are handled by the ability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Ghoul",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GhoulFrenzyChance = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Ghoul", meta = (ClampMin = "0.0"))
	float GhoulFrenzyHitDamage = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Ghoul", meta = (ClampMin = "0.0"))
	float GhoulFrenzyFirstImpactTime = 0.58f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Ghoul", meta = (ClampMin = "0.0"))
	float GhoulFrenzySecondImpactDelay = 0.52f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Ghoul", meta = (ClampMin = "0.0"))
	float GhoulFrenzyAttackInterval = 1.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinJabDamage = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinJabImpactTime = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "50.0"))
	float GoblinJabRange = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinJabAttackInterval = 1.25f;

	/** Short step before impact. The warning covers Range + StepDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinJabStepDistance = 50.f;

	/** Matches the forward-facing hit check and the HUD sector warning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin",
		meta = (ClampMin = "1.0", ClampMax = "89.0"))
	float GoblinJabHalfAngleDegrees = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GoblinLungeChance = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinLungeDamage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinLungeWindup = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "50.0"))
	float GoblinLungeEngageRange = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "50.0"))
	float GoblinLungeHitRange = 225.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinLungeTravelDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "25.0"))
	float GoblinLungeWarningHalfWidth = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "100.0"))
	float GoblinLungeWarningLength = 375.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Goblin", meta = (ClampMin = "0.0"))
	float GoblinLungeAttackInterval = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "0.0"))
	float ShroomBashDamage = 11.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "0.0"))
	float ShroomBashImpactTime = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "50.0"))
	float ShroomBashRange = 185.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "0.0"))
	float ShroomBashAttackInterval = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShroomSporeChance = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "0.0"))
	float ShroomSporeDamage = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "0.0"))
	float ShroomSporeWindup = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "50.0"))
	float ShroomSporeRadius = 285.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Shroom", meta = (ClampMin = "0.0"))
	float ShroomSporeAttackInterval = 2.1f;

	/** A hit by the telegraphed spore burst exposes the player for a short counter window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Synergy", meta = (ClampMin = "0.1"))
	float SporeExposureDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Synergy", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SporeExposureSlowFraction = 0.2f;

	/** Consumed once by a goblin lunge or the second hit of a ghoul frenzy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Synergy", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float SporeDetonationDamageMultiplier = 1.25f;

	/** Guarantees that probability cannot create an excessively long basic-attack drought. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee Enemy|Specials", meta = (ClampMin = "2", ClampMax = "10"))
	int32 MaxNormalAttacksBeforeSpecial = 4;
};

/** A room may promote one ordinary spawn into a readable, better-rewarded elite. */
USTRUCT(BlueprintType)
struct FAuraEliteEnemyBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "1.0", ClampMax = "1.5"))
	float VisualScale = 1.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float MaxHealthMultiplier = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float DamageMultiplier = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "1.0", ClampMax = "1.5"))
	float WalkSpeedMultiplier = 1.07f;

	/** Shortens recovery between attacks without shortening the visible windup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "0.7", ClampMax = "1.0"))
	float AttackIntervalMultiplier = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float SpecialChanceBonus = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "0.2", ClampMax = "0.8"))
	float EnrageHealthFraction = 0.5f;

	/** Further recovery reduction at low health; attack warnings stay at full length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "0.7", ClampMax = "1.0"))
	float EnragedAttackIntervalMultiplier = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float EnragedSpecialChanceBonus = 0.08f;

	/** The elite shroom seeds a fixed location; it never follows the player after warning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Shroom", meta = (ClampMin = "3.0"))
	float ShroomSeedCooldown = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Shroom", meta = (ClampMin = "0.5"))
	float ShroomSeedWindup = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Shroom", meta = (ClampMin = "0.5"))
	float ShroomSeedArmDelay = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Shroom", meta = (ClampMin = "0.5"))
	float ShroomSeedDuration = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Shroom", meta = (ClampMin = "50.0"))
	float ShroomSeedRadius = 240.f;

	/** One hit per player per field, not a stacking damage-over-time effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Shroom", meta = (ClampMin = "0.0"))
	float ShroomSeedDamage = 7.f;

	/** At most one warned follow-up per cooldown; ordinary lunges remain unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Goblin", meta = (ClampMin = "3.0"))
	float GoblinSweepCooldown = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Goblin", meta = (ClampMin = "0.5"))
	float GoblinSweepWindup = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Goblin", meta = (ClampMin = "50.0"))
	float GoblinSweepRange = 210.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Goblin",
		meta = (ClampMin = "1.0", ClampMax = "89.0"))
	float GoblinSweepHalfAngleDegrees = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite|Goblin", meta = (ClampMin = "0.0"))
	float GoblinSweepDamage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elite", meta = (ClampMin = "0"))
	int32 KillExperienceReward = 50;

};

USTRUCT(BlueprintType)
struct FAuraShamanBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "1.0"))
	float MaxHealth = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "1.0"))
	float WalkSpeed = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "0"))
	int32 KillExperienceReward = 75;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "0.0"))
	float Damage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "0.0"))
	float WindupTime = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "100.0"))
	float CastRange = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "0.0"))
	float Cooldown = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "100.0"))
	float RetreatDistance = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "100.0"))
	float PreferredDistance = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman", meta = (ClampMin = "100.0"))
	float AttackRange = 1000.f;

	/** Rolled on every eligible cast; repeated volleys are suppressed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Volley",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VolleyChance = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Volley",
		meta = (ClampMin = "2", ClampMax = "10"))
	int32 MaxNormalCastsBeforeVolley = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Volley", meta = (ClampMin = "2", ClampMax = "7"))
	int32 VolleyProjectileCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Volley", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float VolleySpreadDegrees = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Volley", meta = (ClampMin = "0.0"))
	float VolleyDamageMultiplier = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Volley", meta = (ClampMin = "0.0"))
	float VolleyWindupBonus = 0.25f;

	/** Locked-position area strike: like the player's Q, but the target point is fixed at cast start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Ground Burst",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundBurstChance = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Ground Burst",
		meta = (ClampMin = "2", ClampMax = "12"))
	int32 MaxNormalCastsBeforeGroundBurst = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Ground Burst",
		meta = (ClampMin = "0.0"))
	float GroundBurstDamage = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Ground Burst",
		meta = (ClampMin = "50.0"))
	float GroundBurstRadius = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Ground Burst",
		meta = (ClampMin = "0.1"))
	float GroundBurstWindup = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shaman|Ground Burst",
		meta = (ClampMin = "0.1"))
	float GroundBurstCooldown = 8.f;
};

USTRUCT(BlueprintType)
struct FAuraBossBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "1.0"))
	float MaxHealth = 575.f;

	/** Permanent armor; the ward below is a separate, breakable health pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0.0"))
	float Armor = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0"))
	int32 KillExperienceReward = 250;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float PhaseTwoHealthThreshold = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0.1"))
	float PhaseTransitionDuration = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Movement", meta = (ClampMin = "1.0"))
	float PhaseOneWalkSpeed = 245.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Movement", meta = (ClampMin = "1.0"))
	float PhaseTwoWalkSpeed = 310.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Range", meta = (ClampMin = "100.0"))
	float MeleeRange = 285.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Range", meta = (ClampMin = "100.0"))
	float TailSpinRadius = 380.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Range", meta = (ClampMin = "100.0"))
	float ThrowRange = 1150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Range", meta = (ClampMin = "25.0"))
	float ThrowWarningRadius = 115.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Range",
		meta = (ClampMin = "10.0", ClampMax = "85.0"))
	float ClawHalfAngleDegrees = 52.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Damage", meta = (ClampMin = "0.0"))
	float PhaseOneClawDamage = 21.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Damage", meta = (ClampMin = "0.0"))
	float PhaseTwoClawDamage = 26.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Damage", meta = (ClampMin = "0.0"))
	float TailSpinDamage = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Damage", meta = (ClampMin = "0.0"))
	float ThrowDamage = 28.f;

	/** Phase-two targeted stream; each shot flies straight after launch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "1.0"))
	float BarrageShotDamage = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "0.3"))
	float BarrageWindup = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "0.1"))
	float BarrageShotInterval = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "2", ClampMax = "24"))
	int32 BarrageShotCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "10.0", ClampMax = "55.0"))
	float BarrageHalfAngleDegrees = 27.f;

	/** Maximum boss re-aim speed between shots, within the warned fan. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float BarrageAimTurnRateDegreesPerSecond = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "100.0"))
	float BarrageRange = 1150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "1.0"))
	float BarrageCooldown = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BarrageChance = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Barrage", meta = (ClampMin = "0.0"))
	float BarrageRecovery = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeDamage = 28.f;

	/** Phase-two ward: a visible cast, then a finite absorb pool (after armor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ward", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float WardHealthFraction = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ward", meta = (ClampMin = "0.5"))
	float WardDuration = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ward", meta = (ClampMin = "1.0"))
	float WardCooldown = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ward", meta = (ClampMin = "0.3"))
	float WardWindup = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ward", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WardChance = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Ward", meta = (ClampMin = "1", ClampMax = "3"))
	int32 WardMaxCasts = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Charge", meta = (ClampMin = "100.0"))
	float ChargeDistance = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Charge", meta = (ClampMin = "25.0"))
	float ChargeHalfWidth = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TailSpinChance = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeChance = 0.38f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SummonChance = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxActionsWithoutClaw = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxCloseActionsWithoutTail = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "2", ClampMax = "12"))
	int32 MaxActionsWithoutSummon = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Decision",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxFarActionsWithoutThrow = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float ClawImpactTime = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float TailSpinWindup = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float ThrowWindup = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float ChargeWindup = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0"))
	float SummonWindup = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.0"))
	float SummonCooldown = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "100.0"))
	float SummonRadius = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "1", ClampMax = "6"))
	int32 SummonCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxActiveSummons = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float RecoveryTime = 0.24f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float PhaseOneAttackInterval = 1.30f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Timing", meta = (ClampMin = "0.0"))
	float PhaseTwoAttackInterval = 0.97f;
};

USTRUCT(BlueprintType)
struct FAuraWorldRecoveryBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "0.0"))
	float HealthPickupRestore = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "0.0"))
	float ManaPickupRestore = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickups", meta = (ClampMin = "0.1"))
	float TrainingPickupRespawnDelay = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Treasure", meta = (ClampMin = "0.0"))
	float TreasureHealthRestore = 35.f;

};

USTRUCT(BlueprintType)
struct FAuraAICombatBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awareness", meta = (ClampMin = "100.0"))
	float DetectionRadius = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awareness", meta = (ClampMin = "100.0"))
	float LoseTargetRadius = 1550.f;

	/** Legacy asset field: now the approximate God View pursuit radius, not a home leash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Awareness", meta = (ClampMin = "100.0"))
	float LeashRadius = 1850.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee", meta = (ClampMin = "50.0"))
	float MeleeAttackRange = 190.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "1.0"))
	float HomeAcceptanceRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Navigation", meta = (ClampMin = "0.05"))
	float MoveRequestInterval = 0.22f;
};

USTRUCT(BlueprintType)
struct FAuraEncounterBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mandatory Encounters", meta = (ClampMin = "1"))
	int32 OuterGuardEnemyCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mandatory Encounters", meta = (ClampMin = "1"))
	int32 EliteShamanEnemyCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mandatory Encounters", meta = (ClampMin = "1"))
	int32 DemonLordEnemyCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optional Encounters", meta = (ClampMin = "0"))
	int32 OptionalBranchEnemyCount = 5;
};

/** Optional post-victory trial. Kept data-driven so blind-test changes need no code edits. */
USTRUCT(BlueprintType)
struct FAuraAbyssTrialBalance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abyss", meta = (ClampMin = "1.0"))
	float MaxHealthMultiplier = 1.30f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abyss", meta = (ClampMin = "1.0"))
	float DamageMultiplier = 1.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abyss", meta = (ClampMin = "1.0"))
	float SoulMultiplier = 1.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abyss", meta = (ClampMin = "1.0"))
	float OreMultiplier = 1.25f;
};

/**
 * Single source of truth for the first playable demo's combat economy.
 * The C++ defaults are safe fallbacks; designers tune DA_CombatBalance in the editor.
 */
UCLASS(BlueprintType)
class AURA_API UAuraCombatBalanceData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	FName BalanceVersion = TEXT("NativeFallback_v1_8_CombatRanks");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	FAuraPlayerCombatBalance Player;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Progression")
	FAuraPlayerProgressionBalance Progression;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Progression")
	FAuraGrowthEconomyBalance GrowthEconomy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Camp Economy")
	FAuraCampEconomyBalance CampEconomy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Run Upgrades")
	FAuraRunUpgradeBalance RunUpgrades;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Player Abilities")
	FAuraFireBoltBalance FireBolt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Player Abilities")
	FAuraArcaneShardsBalance ArcaneShards;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Player Abilities")
	FAuraShockBalance Shock;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Enemies")
	FAuraEnemyPoiseBalance Poise;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Player Abilities")
	FAuraComboBalance Combo;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Player Abilities")
	FAuraSkillRankBalance SkillRanks;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Player Abilities")
	FAuraElementalBalance Elemental;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Enemies")
	FAuraMeleeEnemyBalance MeleeEnemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Enemies")
	FAuraEliteEnemyBalance EliteEnemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Enemies")
	FAuraShamanBalance Shaman;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Boss")
	FAuraBossBalance Boss;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Abyss Trial")
	FAuraAbyssTrialBalance AbyssTrial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|World")
	FAuraWorldRecoveryBalance Recovery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|AI")
	FAuraAICombatBalance AI;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance|Encounters")
	FAuraEncounterBalance Encounters;

	/** Loads /Game/Aura/Data/DA_CombatBalance, falling back to these native defaults. */
	static const UAuraCombatBalanceData& Get();

	static const FSoftObjectPath& GetDefaultAssetPath();

	int32 GetEncounterSpawnCount(EAuraEncounterBalanceProfile Profile, int32 CustomCount) const;
	bool Validate(FString& OutError) const;
};
