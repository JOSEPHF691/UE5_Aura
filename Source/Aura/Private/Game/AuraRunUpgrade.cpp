// Copyright yosep

#include "Game/AuraRunUpgrade.h"

#include "Data/AuraCombatBalanceData.h"

int32 FAuraRunUpgradeState::GetRank(const EAuraRunUpgradeType Type) const
{
	switch (Type)
	{
	case EAuraRunUpgradeType::FireBoltPower: return FireBoltPowerRank;
	case EAuraRunUpgradeType::ArcaneShardsMastery: return ArcaneShardsMasteryRank;
	case EAuraRunUpgradeType::ShockMastery: return ShockMasteryRank;
	case EAuraRunUpgradeType::Vitality: return VitalityRank;
	case EAuraRunUpgradeType::ManaSpring: return ManaSpringRank;
	case EAuraRunUpgradeType::Spellweaver: return SpellweaverRank;
	default: return 0;
	}
}

bool FAuraRunUpgradeState::AddRank(const EAuraRunUpgradeType Type, const int32 MaxRank)
{
	int32* Rank = nullptr;
	switch (Type)
	{
	case EAuraRunUpgradeType::FireBoltPower: Rank = &FireBoltPowerRank; break;
	case EAuraRunUpgradeType::ArcaneShardsMastery: Rank = &ArcaneShardsMasteryRank; break;
	case EAuraRunUpgradeType::ShockMastery: Rank = &ShockMasteryRank; break;
	case EAuraRunUpgradeType::Vitality: Rank = &VitalityRank; break;
	case EAuraRunUpgradeType::ManaSpring: Rank = &ManaSpringRank; break;
	case EAuraRunUpgradeType::Spellweaver: Rank = &SpellweaverRank; break;
	default: return false;
	}
	if (!Rank || *Rank >= MaxRank) return false;
	*Rank = FMath::Clamp(*Rank + 1, 0, MaxRank);
	return true;
}

int32 FAuraRunUpgradeState::GetTotalRanks() const
{
	return FireBoltPowerRank + ArcaneShardsMasteryRank + ShockMasteryRank
		+ VitalityRank + ManaSpringRank + SpellweaverRank;
}

bool FAuraRunUpgradeState::operator==(const FAuraRunUpgradeState& Other) const
{
	return FireBoltPowerRank == Other.FireBoltPowerRank
		&& ArcaneShardsMasteryRank == Other.ArcaneShardsMasteryRank
		&& ShockMasteryRank == Other.ShockMasteryRank
		&& VitalityRank == Other.VitalityRank
		&& ManaSpringRank == Other.ManaSpringRank
		&& SpellweaverRank == Other.SpellweaverRank;
}

const TArray<EAuraRunUpgradeType>& AuraRunUpgrade::GetAllTypes()
{
	static const TArray<EAuraRunUpgradeType> Types = {
		EAuraRunUpgradeType::FireBoltPower,
		EAuraRunUpgradeType::ArcaneShardsMastery,
		EAuraRunUpgradeType::ShockMastery,
		EAuraRunUpgradeType::Vitality,
		EAuraRunUpgradeType::ManaSpring,
		EAuraRunUpgradeType::Spellweaver
	};
	return Types;
}

FString AuraRunUpgrade::GetTitle(const EAuraRunUpgradeType Type)
{
	switch (Type)
	{
	case EAuraRunUpgradeType::FireBoltPower: return TEXT("余烬之核");
	case EAuraRunUpgradeType::ArcaneShardsMastery: return TEXT("奥术专注");
	case EAuraRunUpgradeType::ShockMastery: return TEXT("风暴纹章");
	case EAuraRunUpgradeType::Vitality: return TEXT("旺盛生命");
	case EAuraRunUpgradeType::ManaSpring: return TEXT("深蓝容器");
	case EAuraRunUpgradeType::Spellweaver: return TEXT("织法者");
	default: return TEXT("未知增益");
	}
}

void AuraRunUpgrade::GetDescriptionLines(
	const EAuraRunUpgradeType Type, const int32 NextRank,
	FString& OutPrimary, FString& OutSecondary)
{
	const FAuraRunUpgradeBalance& Balance = UAuraCombatBalanceData::Get().RunUpgrades;
	switch (Type)
	{
	case EAuraRunUpgradeType::FireBoltPower:
		OutPrimary = FString::Printf(TEXT("火球术伤害 +%.0f%%"),
			Balance.FireBoltDamageBonusPerRank * 100.f);
		OutSecondary = TEXT("强化不消耗法力的基础攻击");
		break;
	case EAuraRunUpgradeType::ArcaneShardsMastery:
		OutPrimary = FString::Printf(TEXT("Q 伤害 +%.0f%%  |  范围 +%.0f%%"),
			Balance.ArcaneShardsDamageBonusPerRank * 100.f,
			Balance.ArcaneShardsRadiusBonusPerRank * 100.f);
		OutSecondary = TEXT("更擅长清理成群敌人");
		break;
	case EAuraRunUpgradeType::ShockMastery:
		OutPrimary = FString::Printf(TEXT("E 伤害 +%.0f%%  |  麻痹持续 +%.2f 秒"),
			Balance.ShockDamageBonusPerRank * 100.f,
			Balance.ShockStunBonusPerRank);
		OutSecondary = TEXT("增强爆发与控制");
		break;
	case EAuraRunUpgradeType::Vitality:
		OutPrimary = FString::Printf(TEXT("生命上限 +%.0f"), Balance.MaxHealthPerRank);
		OutSecondary = TEXT("增加的生命值同时会被回复");
		break;
	case EAuraRunUpgradeType::ManaSpring:
		OutPrimary = FString::Printf(TEXT("法力上限 +%.0f"), Balance.MaxManaPerRank);
		OutSecondary = TEXT("提升法力上限，但不立即回复当前法力");
		break;
	case EAuraRunUpgradeType::Spellweaver:
		OutPrimary = FString::Printf(TEXT("所有技能伤害 +%.0f%%"),
			Balance.UniversalDamageBonusPerRank * 100.f);
		OutSecondary = TEXT("火球术、Q 和 E 均受益");
		break;
	default:
		OutPrimary = TEXT("未知效果");
		OutSecondary.Reset();
		break;
	}
	OutSecondary += FString::Printf(TEXT("  |  等级 %d/%d"),
		NextRank, Balance.MaxRankPerUpgrade);
}

FLinearColor AuraRunUpgrade::GetColor(const EAuraRunUpgradeType Type)
{
	switch (Type)
	{
	case EAuraRunUpgradeType::FireBoltPower: return FLinearColor(1.f, 0.28f, 0.04f, 1.f);
	case EAuraRunUpgradeType::ArcaneShardsMastery: return FLinearColor(0.68f, 0.32f, 1.f, 1.f);
	case EAuraRunUpgradeType::ShockMastery: return FLinearColor(0.12f, 0.78f, 1.f, 1.f);
	case EAuraRunUpgradeType::Vitality: return FLinearColor(0.2f, 0.92f, 0.38f, 1.f);
	case EAuraRunUpgradeType::ManaSpring: return FLinearColor(0.18f, 0.42f, 1.f, 1.f);
	case EAuraRunUpgradeType::Spellweaver: return FLinearColor(1.f, 0.72f, 0.16f, 1.f);
	default: return FLinearColor::White;
	}
}

FBox2D AuraRunUpgrade::GetSelectionCardRect(
	const FVector2D& ViewportSize, const int32 OptionIndex, const int32 OptionCount)
{
	const float UIScale = FMath::Clamp(
		FMath::Min(ViewportSize.X / 1280.f, ViewportSize.Y / 720.f), 0.75f, 2.f);
	const float Width = 286.f * UIScale;
	const float Height = 216.f * UIScale;
	const float Gap = 20.f * UIScale;
	const float TotalWidth = Width * OptionCount + Gap * FMath::Max(0, OptionCount - 1);
	const float X = ViewportSize.X * 0.5f - TotalWidth * 0.5f
		+ OptionIndex * (Width + Gap);
	const float Y = ViewportSize.Y * 0.5f - Height * 0.42f;
	return FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + Height));
}
