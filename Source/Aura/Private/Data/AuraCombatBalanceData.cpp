// Copyright yosep

#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraCombatBalance, Log, All);

bool FAuraCampEconomyBalance::GetOffer(const int32 OfferId, FName& OutItemId,
	EAuraItemRarity& OutRarity, int32& OutCost, int32& OutLimit,
	const int32 PaidLevel) const
{
	OutRarity = EAuraItemRarity::Common;
	OutLimit = 0;
	switch (OfferId)
	{
	case 1: OutItemId = TEXT("Consumable.HealthPotion"); OutCost = PotionSoulPrice; break;
	case 2: OutItemId = TEXT("Consumable.ManaPotion"); OutCost = PotionSoulPrice; break;
	case 3: OutItemId = TEXT("Equipment.ArcaneStaff"); OutCost = CommonEquipmentSoulPrice; break;
	case 4: OutItemId = TEXT("Equipment.WardenArmor"); OutCost = CommonEquipmentSoulPrice; break;
	case 5: OutItemId = TEXT("Equipment.SeerHeadpiece"); OutCost = CommonEquipmentSoulPrice; break;
	case 6: OutItemId = TEXT("Equipment.WindShoes"); OutCost = CommonEquipmentSoulPrice; break;
	case 7:
		OutItemId = TEXT("Equipment.ArcaneStaff");
		OutRarity = EAuraItemRarity::Quality;
		OutCost = QualityEquipmentSoulPrice;
		break;
	case 8:
		OutItemId = TEXT("Consumable.RespecStone");
		OutRarity = EAuraItemRarity::Fine;
		OutCost = RespecStoneSoulPrice;
		OutLimit = RespecStonePurchaseLimit;
		break;
	case 9: case 10: case 11: case 12:
		if (PaidLevel < (OfferId <= 10 ? 4 : 7)) return false;
		OutItemId = OfferId == 9 ? TEXT("Equipment.ArcaneStaff")
			: (OfferId == 10 ? TEXT("Equipment.WardenArmor")
				: (OfferId == 11 ? TEXT("Equipment.SeerHeadpiece")
					: TEXT("Equipment.WindShoes")));
		OutRarity = EAuraItemRarity::Fine;
		OutCost = FineEquipmentSoulPrice;
		break;
	case 13:
		OutItemId = TEXT("Material.SmithMetal");
		OutCost = SmithMetalSoulPrice;
		OutLimit = SmithMetalPurchaseLimit;
		break;
	default: return false;
	}
	return OutCost > 0;
}

int32 FAuraCampEconomyBalance::GetForgeSoulCost(const int32 NextLevel) const
{
	switch (NextLevel)
	{
	case 1: return ForgeOneSoulCost;
	case 2: return ForgeTwoSoulCost;
	case 3: return ForgeThreeSoulCost;
	default: return MAX_int32;
	}
}

int32 FAuraCampEconomyBalance::GetForgeMaterialCost(const int32 NextLevel) const
{
	switch (NextLevel)
	{
	case 1: return ForgeOneMaterialCost;
	case 2: return ForgeTwoMaterialCost;
	case 3: return ForgeThreeMaterialCost;
	default: return MAX_int32;
	}
}

int32 FAuraCampEconomyBalance::GetForgeSoulCost(
	const int32 NextLevel, const EAuraEquipmentSlot Slot) const
{
	if (Slot == EAuraEquipmentSlot::Weapon) return GetForgeSoulCost(NextLevel);
	if (Slot == EAuraEquipmentSlot::None || NextLevel < 1 || NextLevel > 3)
		return MAX_int32;
	return NextLevel == 1 ? 15 : (NextLevel == 2 ? 30 : 55);
}

int32 FAuraCampEconomyBalance::GetSellPrice(const FAuraItemInstance& Item) const
{
	if (!Item.Definition || Item.Definition->ItemType == EAuraItemType::Quest) return 0;
	if (Item.Definition->ItemType == EAuraItemType::Material) return 3;
	int32 ReferencePrice = PotionSoulPrice;
	if (Item.Definition->ItemId == FName(TEXT("Consumable.RespecStone")))
	{
		ReferencePrice = RespecStoneSoulPrice;
	}
	else if (Item.Definition->ItemType == EAuraItemType::Equipment)
	{
		ReferencePrice = Item.Rarity == EAuraItemRarity::Common
			? CommonEquipmentSoulPrice : (Item.Rarity == EAuraItemRarity::Quality
				? QualityEquipmentSoulPrice : QualityEquipmentSoulPrice + 25
					* (static_cast<int32>(Item.Rarity) - static_cast<int32>(EAuraItemRarity::Quality)));
		ReferencePrice += FMath::Max(0, Item.ItemLevel - 1) * 4;
		ReferencePrice += FMath::Clamp(Item.EnhancementLevel, 0, 3) * 6;
	}
	return FMath::Max(1, FMath::FloorToInt(ReferencePrice
		* FMath::Clamp(SellFraction, 0.01f, 0.5f)));
}

const FSoftObjectPath& UAuraCombatBalanceData::GetDefaultAssetPath()
{
	static const FSoftObjectPath AssetPath(
		TEXT("/Game/Aura/Data/DA_CombatBalance.DA_CombatBalance"));
	return AssetPath;
}

const UAuraCombatBalanceData& UAuraCombatBalanceData::Get()
{
	static TSoftObjectPtr<UAuraCombatBalanceData> BalanceAsset(GetDefaultAssetPath());
	if (const UAuraCombatBalanceData* LoadedBalance = BalanceAsset.LoadSynchronous())
	{
		return *LoadedBalance;
	}

	static bool bLoggedMissingAsset = false;
	if (!bLoggedMissingAsset)
	{
		bLoggedMissingAsset = true;
		UE_LOG(LogAuraCombatBalance, Warning,
			TEXT("Combat balance asset is missing at %s; native fallback values are active."),
			*GetDefaultAssetPath().ToString());
	}
	return *GetDefault<UAuraCombatBalanceData>();
}

int32 UAuraCombatBalanceData::GetEncounterSpawnCount(
	const EAuraEncounterBalanceProfile Profile, const int32 CustomCount) const
{
	switch (Profile)
	{
	case EAuraEncounterBalanceProfile::OuterGuard:
		return Encounters.OuterGuardEnemyCount;
	case EAuraEncounterBalanceProfile::EliteShaman:
		return Encounters.EliteShamanEnemyCount;
	case EAuraEncounterBalanceProfile::DemonLord:
		return Encounters.DemonLordEnemyCount;
	default:
		return CustomCount;
	}
}

bool UAuraCombatBalanceData::Validate(FString& OutError) const
{
	TArray<FString> Errors;
	const auto RequirePositive = [&Errors](const TCHAR* Label, const float Value)
	{
		if (Value <= 0.f) Errors.Add(FString::Printf(TEXT("%s must be greater than zero"), Label));
	};
	const auto RequireNonNegative = [&Errors](const TCHAR* Label, const float Value)
	{
		if (Value < 0.f) Errors.Add(FString::Printf(TEXT("%s cannot be negative"), Label));
	};

	RequirePositive(TEXT("Player.MaxHealth"), Player.MaxHealth);
	RequirePositive(TEXT("Player.MaxMana"), Player.MaxMana);
	bool bValidExperienceTable = Progression.MaxLevel == 10
		&& Progression.ExperienceToNextLevel.Num() == Progression.MaxLevel - 1;
	for (const int32 Threshold : Progression.ExperienceToNextLevel)
	{
		bValidExperienceTable &= Threshold > 0;
	}
	if (!bValidExperienceTable)
	{
		Errors.Add(TEXT("M21 requires a positive nine-step experience table and level 10 cap"));
	}
	RequireNonNegative(TEXT("Progression.MaxHealthPerLevel"), Progression.MaxHealthPerLevel);
	RequireNonNegative(TEXT("Progression.MaxManaPerLevel"), Progression.MaxManaPerLevel);
	RequireNonNegative(TEXT("Progression.StrengthPerLevel"), Progression.StrengthPerLevel);
	RequireNonNegative(TEXT("Progression.IntelligencePerLevel"), Progression.IntelligencePerLevel);
	RequireNonNegative(TEXT("Progression.ArmorPerLevel"), Progression.ArmorPerLevel);
	RequireNonNegative(TEXT("Progression.AbilityDamageBonusPerLevel"), Progression.AbilityDamageBonusPerLevel);
	if (GrowthEconomy.CommonKillSouls < 0
		|| GrowthEconomy.EliteKillSouls < 0 || GrowthEconomy.BossKillSouls < 0
		|| GrowthEconomy.EliteRoomCatchUpExperience < 0
		|| GrowthEconomy.SealRoomClearSouls < 0)
	{
		Errors.Add(TEXT("Growth economy rewards cannot be negative"));
	}
	if (CampEconomy.PotionSoulPrice <= 0 || CampEconomy.CommonEquipmentSoulPrice <= 0
		|| CampEconomy.QualityEquipmentSoulPrice <= CampEconomy.CommonEquipmentSoulPrice
		|| CampEconomy.RespecStoneSoulPrice <= 0
		|| CampEconomy.RespecStonePurchaseLimit < 1
		|| CampEconomy.SellFraction <= 0.f || CampEconomy.SellFraction > 0.5f
		|| CampEconomy.GetForgeSoulCost(1) <= 0 || CampEconomy.GetForgeSoulCost(2) <= 0
		|| CampEconomy.GetForgeSoulCost(3) <= 0
		|| CampEconomy.GetForgeMaterialCost(1) <= 0
		|| CampEconomy.GetForgeMaterialCost(2) <= 0
		|| CampEconomy.GetForgeMaterialCost(3) <= 0
		|| CampEconomy.WeaponBaseStatBonusPerLevel <= 0.f)
	{
		Errors.Add(TEXT("M13 camp merchant/forge economy is invalid"));
	}
	if (RunUpgrades.MaxRankPerUpgrade <= 0)
	{
		Errors.Add(TEXT("Run upgrade max rank must be positive"));
	}
	RequireNonNegative(TEXT("RunUpgrades.FireBoltDamageBonusPerRank"), RunUpgrades.FireBoltDamageBonusPerRank);
	RequireNonNegative(TEXT("RunUpgrades.ArcaneShardsDamageBonusPerRank"), RunUpgrades.ArcaneShardsDamageBonusPerRank);
	RequireNonNegative(TEXT("RunUpgrades.ArcaneShardsRadiusBonusPerRank"), RunUpgrades.ArcaneShardsRadiusBonusPerRank);
	RequireNonNegative(TEXT("RunUpgrades.ShockDamageBonusPerRank"), RunUpgrades.ShockDamageBonusPerRank);
	RequireNonNegative(TEXT("RunUpgrades.ShockStunBonusPerRank"), RunUpgrades.ShockStunBonusPerRank);
	RequireNonNegative(TEXT("RunUpgrades.MaxHealthPerRank"), RunUpgrades.MaxHealthPerRank);
	RequireNonNegative(TEXT("RunUpgrades.MaxManaPerRank"), RunUpgrades.MaxManaPerRank);
	RequireNonNegative(TEXT("RunUpgrades.UniversalDamageBonusPerRank"), RunUpgrades.UniversalDamageBonusPerRank);
	if (MeleeEnemy.KillExperienceReward <= 0 || Shaman.KillExperienceReward <= 0
		|| Boss.KillExperienceReward <= 0)
	{
		Errors.Add(TEXT("Every enemy tier must grant positive experience"));
	}
	RequirePositive(TEXT("FireBolt.Damage"), FireBolt.Damage);
	RequirePositive(TEXT("ArcaneShards.Damage"), ArcaneShards.Damage);
	RequirePositive(TEXT("Shock.Damage"), Shock.Damage);
	RequirePositive(TEXT("Poise.NormalMax"), Poise.NormalMax);
	RequirePositive(TEXT("Poise.EliteMax"), Poise.EliteMax);
	RequirePositive(TEXT("Poise.ShamanMax"), Poise.ShamanMax);
	RequirePositive(TEXT("Poise.BossMax"), Poise.BossMax);
	RequireNonNegative(TEXT("Poise.FireBoltDamage"), Poise.FireBoltDamage);
	RequireNonNegative(TEXT("Poise.ArcaneShardsDamage"), Poise.ArcaneShardsDamage);
	RequireNonNegative(TEXT("Poise.ShockDamage"), Poise.ShockDamage);
	RequireNonNegative(TEXT("Poise.ComboDamage"), Poise.ComboDamage);
	RequireNonNegative(TEXT("Poise.RegenDelay"), Poise.RegenDelay);
	RequireNonNegative(TEXT("Poise.RegenPerSecond"), Poise.RegenPerSecond);
	RequireNonNegative(TEXT("Poise.BreakImmunity"), Poise.BreakImmunity);
	RequirePositive(TEXT("Poise.NormalStaggerDuration"), Poise.NormalStaggerDuration);
	RequirePositive(TEXT("Poise.EliteStaggerDuration"), Poise.EliteStaggerDuration);
	RequirePositive(TEXT("Poise.BossStaggerDuration"), Poise.BossStaggerDuration);
	RequirePositive(TEXT("Combo.MarkDuration"), Combo.MarkDuration);
	RequirePositive(TEXT("Combo.EmberBurstRadius"), Combo.EmberBurstRadius);
	RequireNonNegative(TEXT("Combo.EmberBurstDamageMultiplier"), Combo.EmberBurstDamageMultiplier);
	RequireNonNegative(TEXT("Combo.EmberExtraStackDamageMultiplier"), Combo.EmberExtraStackDamageMultiplier);
	if (Combo.EmberMaxStacks < 1 || Combo.EmberMaxStacks > 8)
	{
		Errors.Add(TEXT("Combo.EmberMaxStacks must be between one and eight"));
	}
	RequirePositive(TEXT("Combo.FractureChainRadius"), Combo.FractureChainRadius);
	RequireNonNegative(TEXT("Combo.FractureChainDamageMultiplier"), Combo.FractureChainDamageMultiplier);
	RequireNonNegative(TEXT("Combo.FractureStaggerDuration"), Combo.FractureStaggerDuration);
	RequireNonNegative(TEXT("Combo.FractureStaggerImmunity"), Combo.FractureStaggerImmunity);
	if (Combo.FractureBossDamageReduction < 0.f || Combo.FractureBossDamageReduction > 0.5f)
	{
		Errors.Add(TEXT("Combo.FractureBossDamageReduction must be between zero and 0.5"));
	}
	RequireNonNegative(TEXT("Combo.FractureBossWeakenDuration"), Combo.FractureBossWeakenDuration);
	RequireNonNegative(TEXT("Combo.FractureBossWeakenCooldown"), Combo.FractureBossWeakenCooldown);
	RequirePositive(TEXT("Combo.OverloadRadius"), Combo.OverloadRadius);
	RequireNonNegative(TEXT("Combo.OverloadDamageMultiplier"), Combo.OverloadDamageMultiplier);
	RequireNonNegative(TEXT("Combo.OverloadBuffDuration"), Combo.OverloadBuffDuration);
	RequireNonNegative(TEXT("Combo.OverloadMoveSpeedBonusFraction"), Combo.OverloadMoveSpeedBonusFraction);
	RequireNonNegative(TEXT("Combo.OverloadBuffCooldown"), Combo.OverloadBuffCooldown);
	if (Combo.FractureChainMaxTargets < 0 || Combo.FractureChainMaxTargets > 8)
	{
		Errors.Add(TEXT("Combo.FractureChainMaxTargets must be between zero and eight"));
	}
	if (SkillRanks.MaxRank != 4 || SkillRanks.FireBoltExtraPierces < 0
		|| SkillRanks.FireBoltExtraPierces > 2)
	{
		Errors.Add(TEXT("Skill rank limits are invalid"));
	}
	RequireNonNegative(TEXT("SkillRanks.FireBoltDamageBonus"), SkillRanks.FireBoltDamageBonus);
	RequireNonNegative(TEXT("SkillRanks.ArcaneShardsRadiusBonus"), SkillRanks.ArcaneShardsRadiusBonus);
	RequireNonNegative(TEXT("SkillRanks.ShockDamageBonus"), SkillRanks.ShockDamageBonus);
	RequirePositive(TEXT("SkillRanks.ArcaneShardsFieldDuration"), SkillRanks.ArcaneShardsFieldDuration);
	RequirePositive(TEXT("SkillRanks.ArcaneShardsFieldRadius"), SkillRanks.ArcaneShardsFieldRadius);
	if (SkillRanks.ArcaneShardsSlowFraction < 0.f || SkillRanks.ArcaneShardsSlowFraction > 0.8f)
	{
		Errors.Add(TEXT("SkillRanks.ArcaneShardsSlowFraction must be between zero and 0.8"));
	}
	RequireNonNegative(TEXT("FireBolt.ManaCost"), FireBolt.ManaCost);
	RequirePositive(TEXT("ArcaneShards.ManaCost"), ArcaneShards.ManaCost);
	RequirePositive(TEXT("Shock.ManaCost"), Shock.ManaCost);
	if (!FMath::IsNearlyZero(FireBolt.ManaCost))
	{
		Errors.Add(TEXT("FireBolt is the free fallback attack and must not consume mana"));
	}
	if (ArcaneShards.Damage <= FireBolt.Damage || Shock.Damage <= FireBolt.Damage)
	{
		Errors.Add(TEXT("Q and E must each deal more damage than the free FireBolt fallback"));
	}
	if (FireBolt.ManaCost > Player.MaxMana || ArcaneShards.ManaCost > Player.MaxMana
		|| Shock.ManaCost > Player.MaxMana)
	{
		Errors.Add(TEXT("Every player ability mana cost must fit inside the maximum mana pool"));
	}
	if (ArcaneShards.ManaCost * 2.f + Shock.ManaCost > Player.MaxMana + KINDA_SMALL_NUMBER)
	{
		Errors.Add(TEXT("A full mana pool must support the intended Q + E + Q combat cycle"));
	}
	if (ArcaneShards.EffectRadius > ArcaneShards.CastRange)
	{
		Errors.Add(TEXT("Arcane Shards effect radius cannot exceed its cast range"));
	}
	if (Shock.AimAssistRadius > Shock.CastRange)
	{
		Errors.Add(TEXT("Shock aim assist radius cannot exceed its cast range"));
	}
	if (!(AI.DetectionRadius < AI.LoseTargetRadius && AI.LoseTargetRadius <= AI.LeashRadius))
	{
		Errors.Add(TEXT("AI radii must satisfy Detection < LoseTarget <= PlayerView"));
	}
	if (!(Shaman.RetreatDistance < Shaman.PreferredDistance
		&& Shaman.PreferredDistance <= Shaman.AttackRange
		&& Shaman.AttackRange <= AI.LeashRadius))
	{
		Errors.Add(TEXT("Shaman distances must satisfy Retreat < Preferred <= Attack <= AI PlayerView"));
	}
	const auto IsProbability = [](const float Value)
	{
		return Value >= 0.f && Value <= 1.f;
	};
	if (!IsProbability(MeleeEnemy.GhoulFrenzyChance)
		|| !IsProbability(MeleeEnemy.GoblinLungeChance)
		|| !IsProbability(MeleeEnemy.ShroomSporeChance)
		|| MeleeEnemy.MaxNormalAttacksBeforeSpecial < 2)
	{
		Errors.Add(TEXT("Melee enemy special probabilities or pity limit are invalid"));
	}
	if (EliteEnemy.VisualScale < 1.f || EliteEnemy.VisualScale > 1.5f
		|| EliteEnemy.MaxHealthMultiplier < 1.f || EliteEnemy.MaxHealthMultiplier > 3.f
		|| EliteEnemy.DamageMultiplier < 1.f || EliteEnemy.DamageMultiplier > 2.f
		|| EliteEnemy.WalkSpeedMultiplier < 1.f || EliteEnemy.WalkSpeedMultiplier > 1.5f
		|| EliteEnemy.AttackIntervalMultiplier < 0.7f || EliteEnemy.AttackIntervalMultiplier > 1.f
		|| EliteEnemy.EnragedAttackIntervalMultiplier < 0.7f
		|| EliteEnemy.EnragedAttackIntervalMultiplier > 1.f
		|| EliteEnemy.SpecialChanceBonus < 0.f || EliteEnemy.SpecialChanceBonus > 0.3f
		|| EliteEnemy.EnragedSpecialChanceBonus < 0.f
		|| EliteEnemy.EnragedSpecialChanceBonus > 0.2f
		|| EliteEnemy.EnrageHealthFraction <= 0.f || EliteEnemy.EnrageHealthFraction >= 1.f
		|| EliteEnemy.ShroomSeedWindup < 0.5f || EliteEnemy.ShroomSeedArmDelay < 0.5f
		|| EliteEnemy.ShroomSeedCooldown <= EliteEnemy.ShroomSeedWindup
			+ EliteEnemy.ShroomSeedArmDelay + EliteEnemy.ShroomSeedDuration
		|| EliteEnemy.ShroomSeedRadius < 50.f || EliteEnemy.ShroomSeedDamage < 0.f
		|| EliteEnemy.GoblinSweepWindup < 0.5f
		|| EliteEnemy.GoblinSweepCooldown <= EliteEnemy.GoblinSweepWindup
		|| EliteEnemy.GoblinSweepRange < 50.f
		|| EliteEnemy.GoblinSweepHalfAngleDegrees <= 0.f
		|| EliteEnemy.GoblinSweepHalfAngleDegrees >= 90.f
		|| EliteEnemy.GoblinSweepDamage < 0.f
		|| EliteEnemy.KillExperienceReward < MeleeEnemy.KillExperienceReward)
	{
		Errors.Add(TEXT("Elite enemy scaling, enrage, or rewards are invalid"));
	}
	RequirePositive(TEXT("MeleeEnemy.GhoulFrenzyHitDamage"), MeleeEnemy.GhoulFrenzyHitDamage);
	RequirePositive(TEXT("MeleeEnemy.GoblinJabDamage"), MeleeEnemy.GoblinJabDamage);
	RequirePositive(TEXT("MeleeEnemy.GoblinLungeDamage"), MeleeEnemy.GoblinLungeDamage);
	RequirePositive(TEXT("MeleeEnemy.ShroomBashDamage"), MeleeEnemy.ShroomBashDamage);
	RequirePositive(TEXT("MeleeEnemy.ShroomSporeDamage"), MeleeEnemy.ShroomSporeDamage);
	RequirePositive(TEXT("MeleeEnemy.SporeExposureDuration"), MeleeEnemy.SporeExposureDuration);
	if (MeleeEnemy.SporeExposureSlowFraction < 0.f
		|| MeleeEnemy.SporeExposureSlowFraction > 0.5f
		|| MeleeEnemy.SporeDetonationDamageMultiplier < 1.f
		|| MeleeEnemy.SporeDetonationDamageMultiplier > 2.f)
	{
		Errors.Add(TEXT("Melee enemy spore synergy values are outside their safe ranges"));
	}
	if (MeleeEnemy.GoblinLungeEngageRange < MeleeEnemy.GoblinLungeHitRange
		|| MeleeEnemy.GoblinLungeTravelDistance
			< MeleeEnemy.GoblinLungeEngageRange - MeleeEnemy.GoblinLungeHitRange
		|| MeleeEnemy.GoblinJabRange <= 0.f
		|| MeleeEnemy.GoblinJabStepDistance < 0.f
		|| MeleeEnemy.GoblinJabRange + MeleeEnemy.GoblinJabStepDistance
			>= MeleeEnemy.GoblinLungeEngageRange
		|| MeleeEnemy.GoblinJabImpactTime < 0.55f
		|| MeleeEnemy.GoblinJabAttackInterval <= MeleeEnemy.GoblinJabImpactTime
		|| MeleeEnemy.GoblinJabHalfAngleDegrees <= 0.f
		|| MeleeEnemy.GoblinJabHalfAngleDegrees >= 90.f)
	{
		Errors.Add(TEXT("Goblin jab must remain a short warned sector; lunge bridges the outer engage range"));
	}
	if (!IsProbability(Shaman.VolleyChance) || Shaman.MaxNormalCastsBeforeVolley < 2
		|| Shaman.VolleyProjectileCount < 2 || Shaman.VolleyDamageMultiplier <= 0.f)
	{
		Errors.Add(TEXT("Shaman volley configuration is invalid"));
	}
	if (!IsProbability(Shaman.GroundBurstChance)
		|| Shaman.MaxNormalCastsBeforeGroundBurst < 2
		|| Shaman.GroundBurstDamage <= 0.f || Shaman.GroundBurstRadius < 50.f
		|| Shaman.GroundBurstWindup < 0.1f
		|| Shaman.GroundBurstCooldown <= Shaman.GroundBurstWindup)
	{
		Errors.Add(TEXT("Shaman ground-burst configuration is invalid"));
	}
	if (!(Boss.PhaseTwoHealthThreshold > 0.f && Boss.PhaseTwoHealthThreshold < 1.f))
	{
		Errors.Add(TEXT("Boss phase-two threshold must be between zero and one"));
	}
	RequirePositive(TEXT("Boss.MaxHealth"), Boss.MaxHealth);
	RequireNonNegative(TEXT("Boss.Armor"), Boss.Armor);
	RequirePositive(TEXT("Boss.PhaseOneClawDamage"), Boss.PhaseOneClawDamage);
	RequirePositive(TEXT("Boss.PhaseTwoClawDamage"), Boss.PhaseTwoClawDamage);
	RequirePositive(TEXT("Boss.TailSpinDamage"), Boss.TailSpinDamage);
	RequirePositive(TEXT("Boss.ThrowDamage"), Boss.ThrowDamage);
	if (Boss.BarrageShotDamage <= 0.f || Boss.BarrageShotDamage >= Boss.ThrowDamage
		|| Boss.BarrageWindup < 0.3f || Boss.BarrageShotInterval < 0.1f
		|| Boss.BarrageShotCount < 2 || Boss.BarrageShotCount > 24
		|| Boss.BarrageHalfAngleDegrees < 10.f || Boss.BarrageHalfAngleDegrees > 55.f
		|| Boss.BarrageAimTurnRateDegreesPerSecond < 1.f
		|| Boss.BarrageAimTurnRateDegreesPerSecond > 180.f
		|| Boss.BarrageRange < Boss.TailSpinRadius || Boss.BarrageRange > Boss.ThrowRange
		|| Boss.BarrageRecovery < 0.f || !IsProbability(Boss.BarrageChance)
		|| Boss.BarrageCooldown <= Boss.BarrageWindup
			+ (Boss.BarrageShotCount - 1) * Boss.BarrageShotInterval + Boss.BarrageRecovery)
	{
		Errors.Add(TEXT("Boss barrage damage, spread, range, timing, or chance is invalid"));
	}
	RequirePositive(TEXT("Boss.ChargeDamage"), Boss.ChargeDamage);
	if (Boss.WardHealthFraction <= 0.f || Boss.WardHealthFraction > 0.5f
		|| Boss.WardDuration <= 0.f || Boss.WardCooldown <= Boss.WardDuration
		|| Boss.WardWindup < 0.3f || !IsProbability(Boss.WardChance)
		|| Boss.WardMaxCasts < 1 || Boss.WardMaxCasts > 3)
	{
		Errors.Add(TEXT("Boss ward pool, timing, chance, or cast limit is invalid"));
	}
	if (Boss.PhaseOneAttackInterval < Boss.ClawImpactTime + Boss.RecoveryTime
		|| Boss.PhaseTwoAttackInterval < Boss.ClawImpactTime + Boss.RecoveryTime)
	{
		Errors.Add(TEXT("Boss attack intervals must allow a claw to finish its windup and recovery"));
	}
	if (Boss.PhaseTwoWalkSpeed < Boss.PhaseOneWalkSpeed)
	{
		Errors.Add(TEXT("Boss phase-two speed must not be lower than phase-one speed"));
	}
	if (!(Boss.MeleeRange <= Boss.TailSpinRadius && Boss.TailSpinRadius <= Boss.ThrowRange))
	{
		Errors.Add(TEXT("Boss ranges must satisfy Melee <= TailSpin <= Throw"));
	}
	if (!IsProbability(Boss.TailSpinChance) || !IsProbability(Boss.ChargeChance)
		|| !IsProbability(Boss.SummonChance) || Boss.MaxActionsWithoutClaw < 1
		|| Boss.MaxCloseActionsWithoutTail < 1 || Boss.MaxActionsWithoutSummon < 2
		|| Boss.MaxFarActionsWithoutThrow < 1)
	{
		Errors.Add(TEXT("Boss action probabilities or drought protection are invalid"));
	}
	if (Boss.ChargeDistance < Boss.MeleeRange || Boss.ChargeHalfWidth <= 0.f
		|| Boss.SummonCount <= 0 || Boss.MaxActiveSummons < Boss.SummonCount
		|| Boss.SummonRadius <= Boss.MeleeRange)
	{
		Errors.Add(TEXT("Boss charge or summon geometry is invalid"));
	}
	if (Encounters.OuterGuardEnemyCount <= 0 || Encounters.EliteShamanEnemyCount <= 0
		|| Encounters.DemonLordEnemyCount <= 0 || Encounters.OptionalBranchEnemyCount < 0)
	{
		Errors.Add(TEXT("Encounter enemy counts are invalid"));
	}

	if (Elemental.NormalSlow < Elemental.EliteSlow || Elemental.EliteSlow < Elemental.BossSlow
		|| Elemental.BossSlow < 0.f || Elemental.NormalSlow >= 1.f
		|| Elemental.SlowDuration <= 0.f || Elemental.CrystalDuration < Elemental.EliteCrystalDuration
		|| Elemental.EliteCrystalDuration <= 0.f || Elemental.CrystalHealthInFirebolts <= 0.f
		|| Elemental.ParalysisInterval <= Elemental.NormalPulseStun + Elemental.ControlProtection
		|| Elemental.ElitePulseStun <= 0.f || Elemental.NormalPulseStun < Elemental.ElitePulseStun
		|| !IsProbability(Elemental.IgniteBaseChance) || Elemental.IgniteChancePerRank < 0.f
		|| !IsProbability(Elemental.IgniteBaseChance + (SkillRanks.MaxRank - 1) * Elemental.IgniteChancePerRank)
		|| Elemental.EchoInterval <= 0.f || Elemental.EchoRadiusScale <= 0.f
		|| Elemental.StormInterval <= 0.f || Elemental.StormDuration < Elemental.StormInterval
		|| Elemental.StormHalfAngle <= 0.f || Elemental.StormHalfAngle >= 90.f
		|| Elemental.JudgmentWindup < 0.1f || Elemental.JudgmentInterval <= Elemental.JudgmentWindup
		|| Elemental.JudgmentRadius <= 0.f || Elemental.JudgmentImpactRadius <= 0.f)
		Errors.Add(TEXT("Elemental skill control, evolution, or probability configuration is invalid"));
	OutError = FString::Join(Errors, TEXT("; "));
	return Errors.IsEmpty();
}
