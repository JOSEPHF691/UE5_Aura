// Copyright yosep

#include "Data/AuraLootProfileData.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraLootData, Log, All);

namespace
{
	TArray<FName> EquipmentIds()
	{
		return { TEXT("Equipment.ArcaneStaff"), TEXT("Equipment.WardenArmor"),
			TEXT("Equipment.SeerHeadpiece"), TEXT("Equipment.WindShoes") };
	}

	TArray<FAuraLootRarityWeight> RarityWeights(
		const float Common, const float Quality, const float Fine,
		const float Epic, const float Legendary)
	{
		return {
			{ EAuraItemRarity::Common, Common },
			{ EAuraItemRarity::Quality, Quality },
			{ EAuraItemRarity::Fine, Fine },
			{ EAuraItemRarity::Epic, Epic },
			{ EAuraItemRarity::Legendary, Legendary }
		};
	}

	FAuraLootProfile MakeProfile(const FName Id, const int32 GuaranteedRolls,
		const int32 BonusRolls, const float BonusChance, const float EquipmentChance,
		const TArray<FAuraLootRarityWeight>& Weights, const int32 MinLevelOffset,
		const int32 MaxLevelOffset)
	{
		FAuraLootProfile Result;
		Result.ProfileId = Id;
		Result.GuaranteedRolls = GuaranteedRolls;
		Result.BonusRolls = BonusRolls;
		Result.BonusRollChance = BonusChance;
		Result.EquipmentChance = EquipmentChance;
		Result.HealthPotionWeight = 0.5f;
		Result.EquipmentItemIds = EquipmentIds();
		Result.RarityWeights = Weights;
		Result.MinItemLevelOffset = MinLevelOffset;
		Result.MaxItemLevelOffset = MaxLevelOffset;
		Result.ScatterRadius = 115.f;
		Result.LifetimeSeconds = 120.f;
		return Result;
	}
}

UAuraLootProfileData::UAuraLootProfileData()
{
	CommonEnemy = MakeProfile(TEXT("CommonEnemy"), 0, 1, 0.38f, 0.25f,
		RarityWeights(70.f, 23.f, 6.f, 1.f, 0.f), -1, 0);
	EliteEnemy = MakeProfile(TEXT("EliteEnemy"), 1, 1, 0.25f, 0.55f,
		RarityWeights(35.f, 40.f, 20.f, 5.f, 0.f), 0, 1);
	SealGuardian = MakeProfile(TEXT("SealGuardian"), 1, 1, 0.7f, 0.65f,
		RarityWeights(15.f, 35.f, 35.f, 13.f, 2.f), 0, 1);
	Boss = MakeProfile(TEXT("Boss"), 2, 1, 0.75f, 0.8f,
		RarityWeights(0.f, 15.f, 40.f, 35.f, 10.f), 1, 2);
	Treasure = MakeProfile(TEXT("Treasure"), 1, 1, 0.8f, 0.75f,
		RarityWeights(10.f, 35.f, 35.f, 17.f, 3.f), 0, 1);
	CommonEnemy.BonusArcaneOreChance = 0.14f;
	EliteEnemy.GuaranteedArcaneOre = 1;
	SealGuardian.BonusArcaneOreChance = 0.35f;
	Boss.GuaranteedArcaneOre = 3;
}

const FSoftObjectPath& UAuraLootProfileData::GetDefaultAssetPath()
{
	static const FSoftObjectPath AssetPath(
		TEXT("/Game/Aura/Data/Loot/DA_LootProfiles.DA_LootProfiles"));
	return AssetPath;
}

const UAuraLootProfileData& UAuraLootProfileData::Get()
{
	static TSoftObjectPtr<UAuraLootProfileData> LootAsset(GetDefaultAssetPath());
	if (const UAuraLootProfileData* Loaded = LootAsset.LoadSynchronous()) return *Loaded;

	static bool bLoggedMissing = false;
	if (!bLoggedMissing)
	{
		bLoggedMissing = true;
		UE_LOG(LogAuraLootData, Warning,
			TEXT("Loot profile asset missing at %s; using native defaults."),
			*GetDefaultAssetPath().ToString());
	}
	return *GetDefault<UAuraLootProfileData>();
}

const FAuraLootProfile& UAuraLootProfileData::GetProfile(
	const EAuraLootProfileType Type) const
{
	switch (Type)
	{
	case EAuraLootProfileType::EliteEnemy: return EliteEnemy;
	case EAuraLootProfileType::SealGuardian: return SealGuardian;
	case EAuraLootProfileType::Boss: return Boss;
	case EAuraLootProfileType::Treasure: return Treasure;
	case EAuraLootProfileType::CommonEnemy: return CommonEnemy;
	default:
		static const FAuraLootProfile Disabled;
		return Disabled;
	}
}

bool UAuraLootProfileData::Validate(FString& OutError) const
{
	TArray<FString> Errors;
	const auto ValidateProfile = [&Errors](const FAuraLootProfile& Profile)
	{
		if (Profile.ProfileId.IsNone()) Errors.Add(TEXT("Loot profile has no id"));
		if (Profile.GuaranteedRolls < 0 || Profile.BonusRolls < 0)
		{
			Errors.Add(FString::Printf(TEXT("%s has negative roll counts"),
				*Profile.ProfileId.ToString()));
		}
		if (Profile.BonusRollChance < 0.f || Profile.BonusRollChance > 1.f
			|| Profile.EquipmentChance < 0.f || Profile.EquipmentChance > 1.f
			|| Profile.HealthPotionWeight < 0.f || Profile.HealthPotionWeight > 1.f
			|| Profile.BonusArcaneOreChance < 0.f || Profile.BonusArcaneOreChance > 1.f
			|| Profile.GuaranteedArcaneOre < 0 || Profile.GuaranteedArcaneOre > 5)
		{
			Errors.Add(FString::Printf(TEXT("%s has an invalid probability"),
				*Profile.ProfileId.ToString()));
		}
		if (Profile.MinItemLevelOffset > Profile.MaxItemLevelOffset)
		{
			Errors.Add(FString::Printf(TEXT("%s has reversed level offsets"),
				*Profile.ProfileId.ToString()));
		}
		if (Profile.LifetimeSeconds < 5.f)
		{
			Errors.Add(FString::Printf(TEXT("%s lifetime is too short"),
				*Profile.ProfileId.ToString()));
		}
		float TotalRarityWeight = 0.f;
		TSet<EAuraItemRarity> SeenRarities;
		for (const FAuraLootRarityWeight& Weight : Profile.RarityWeights)
		{
			if (Weight.Weight < 0.f) Errors.Add(FString::Printf(
				TEXT("%s has a negative rarity weight"), *Profile.ProfileId.ToString()));
			if (SeenRarities.Contains(Weight.Rarity)) Errors.Add(FString::Printf(
				TEXT("%s repeats a rarity"), *Profile.ProfileId.ToString()));
			SeenRarities.Add(Weight.Rarity);
			TotalRarityWeight += FMath::Max(0.f, Weight.Weight);
		}
		if (Profile.EquipmentChance > 0.f
			&& (Profile.EquipmentItemIds.IsEmpty() || TotalRarityWeight <= 0.f))
		{
			Errors.Add(FString::Printf(TEXT("%s cannot generate equipment"),
				*Profile.ProfileId.ToString()));
		}
	};

	ValidateProfile(CommonEnemy);
	ValidateProfile(EliteEnemy);
	ValidateProfile(SealGuardian);
	ValidateProfile(Boss);
	ValidateProfile(Treasure);
	if (EligibilityRadius < 500.f) Errors.Add(TEXT("Eligibility radius is too small"));
	if (MaxActivePersonalDropsPerPlayer <= 0) Errors.Add(TEXT("Personal drop cap is invalid"));
	OutError = FString::Join(Errors, TEXT("; "));
	return Errors.IsEmpty();
}
