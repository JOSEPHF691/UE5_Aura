// Copyright yosep

#include "Player/AuraPlayerState.h"

#include "Equipment/AuraEquipmentComponent.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Game/AuraRunSubsystem.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Player/AuraPlayerController.h"
#include "Net/UnrealNetwork.h"

AAuraPlayerState::AAuraPlayerState()
{
	bReplicates = true;
	InventoryComponent = CreateDefaultSubobject<UAuraInventoryComponent>(TEXT("InventoryComponent"));
	EquipmentComponent = CreateDefaultSubobject<UAuraEquipmentComponent>(TEXT("EquipmentComponent"));
}

void AAuraPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && bPersistentItemsInitialized && GetWorld()
		&& GetWorld()->GetNetMode() == NM_Standalone && GetGameInstance())
	{
		if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			Run->FlushPersistentSave();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AAuraPlayerState::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAuraPlayerState, bPlayerDead);
	DOREPLIFETIME_CONDITION(AAuraPlayerState, ClaimedTreasureIds, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AAuraPlayerState, GrowthState, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AAuraPlayerState, FireBoltVolleyCharge, COND_OwnerOnly);
}

namespace
{
	int32 GetV3EarnedSkillPoints(const int32 Level)
	{
		return (Level >= 4) + (Level >= 5) + (Level >= 7)
			+ (Level >= 8) + (Level >= 10);
	}

	int32 GetV3SpentSkillPoints(const FAuraGrowthState& State)
	{
		return FMath::Max(0, State.SkillRanks.FireBolt - 1)
			+ FMath::Max(0, State.SkillRanks.ArcaneShards - 1)
			+ FMath::Max(0, State.SkillRanks.Shock - 1);
	}
}

int32 AAuraPlayerState::GetEligibleLevel() const
{
	const FAuraPlayerProgressionBalance& Curve = UAuraCombatBalanceData::Get().Progression;
	int32 Required = 0;
	int32 Level = 1;
	while (Level < Curve.MaxLevel)
	{
		Required += Curve.GetExperienceToNextLevel(Level);
		if (GrowthState.TotalExperience < Required) break;
		++Level;
	}
	return Level;
}

bool AAuraPlayerState::IsValidGrowthSnapshot(const FAuraGrowthState& Candidate)
{
	if (Candidate.UnspentComboPoints != 0 || Candidate.ComboRanks.GetSpentPoints() != 0) return false;
	const FAuraSkillRankState& Ranks = Candidate.SkillRanks;
	const auto ValidPath = [](int32 Rank, int32 Path, bool bPending)
	{
		return Rank < 3 ? Path == 0 && !bPending
			: ((Path == 1 || Path == 2) && !bPending) || (Path == 0 && bPending);
	};
	if (!ValidPath(Ranks.ArcaneShards, static_cast<int32>(Ranks.ArcanePath), Ranks.bArcaneEvolutionPending)
		|| !ValidPath(Ranks.Shock, static_cast<int32>(Ranks.ShockPath), Ranks.bShockEvolutionPending)) return false;
	FAuraGrowthState V5 = Candidate;
	V5.UnspentComboPoints = (V5.PaidLevel >= 3) + (V5.PaidLevel >= 6) + (V5.PaidLevel >= 9);
	return IsValidV5GrowthSnapshot(V5);
}

FAuraGrowthState AAuraPlayerState::MigrateV5GrowthSnapshot(const FAuraGrowthState& Legacy)
{
	FAuraGrowthState Result = Legacy;
	Result.ComboRanks = FAuraComboRankState();
	Result.UnspentComboPoints = 0;
	Result.SkillRanks.ArcanePath = EAuraArcanePath::None;
	Result.SkillRanks.ShockPath = EAuraShockPath::None;
	Result.SkillRanks.bArcaneEvolutionPending = Result.SkillRanks.ArcaneShards >= 3;
	Result.SkillRanks.bShockEvolutionPending = Result.SkillRanks.Shock >= 3;
	return Result;
}

bool AAuraPlayerState::IsValidV5GrowthSnapshot(const FAuraGrowthState& Candidate)
{
	const int32 MaxLevel = UAuraCombatBalanceData::Get().Progression.MaxLevel;
	const int32 MaxRank = UAuraCombatBalanceData::Get().SkillRanks.MaxRank;
	const FAuraPlayerProgressionBalance& Curve = UAuraCombatBalanceData::Get().Progression;
	const int32 RequiredExperience = Curve.GetCumulativeExperienceForLevel(Candidate.PaidLevel);
	const int32 MaximumExperience = Curve.GetCumulativeExperienceForLevel(MaxLevel);
	const int32 Q = Candidate.SkillRanks.ArcaneShards;
	const int32 E = Candidate.SkillRanks.Shock;
	const bool bValidBaseSkills = Candidate.PaidLevel > 1 || (Q == 0 && E == 0);
	const bool bValidCombos = (Candidate.ComboRanks.Ember == 0 || Q > 0)
		&& (Candidate.ComboRanks.Fracture == 0 || (Q > 0 && E > 0))
		&& (Candidate.ComboRanks.Overload == 0 || E > 0);
	const bool bValidFireBoltPath = Candidate.SkillRanks.FireBolt < 3
		? Candidate.SkillRanks.FireBoltPath == EAuraFireBoltPath::None
		: (Candidate.SkillRanks.FireBoltPath == EAuraFireBoltPath::Pierce
			|| Candidate.SkillRanks.FireBoltPath == EAuraFireBoltPath::TripleVolley);
	if (Candidate.PaidLevel < 1 || Candidate.PaidLevel > MaxLevel
		|| Candidate.TotalExperience < RequiredExperience
		|| Candidate.TotalExperience > MaximumExperience || Candidate.Souls < 0
		|| Candidate.RespecStonesPurchased < 0
		|| Candidate.SmithMetalPurchased < 0
		|| Candidate.SmithMetalPurchased > UAuraCombatBalanceData::Get().CampEconomy.SmithMetalPurchaseLimit
		|| Candidate.CraftPityMisses < 0 || Candidate.CraftPityMisses > 2
		|| Candidate.UnspentSkillPoints < 0 || Candidate.UnspentComboPoints < 0
		|| (Candidate.bFreeRespecClaimed && Candidate.PaidLevel < 6)
		|| !bValidBaseSkills || !bValidCombos || !bValidFireBoltPath
		|| Candidate.SkillRanks.FireBolt < 1
		|| Candidate.SkillRanks.FireBolt > MaxRank
		|| Candidate.SkillRanks.ArcaneShards < 0 || Candidate.SkillRanks.ArcaneShards > MaxRank
		|| Candidate.SkillRanks.Shock < 0 || Candidate.SkillRanks.Shock > MaxRank
		|| Candidate.ComboRanks.Ember < 0 || Candidate.ComboRanks.Ember > 2
		|| Candidate.ComboRanks.Fracture < 0 || Candidate.ComboRanks.Fracture > 2
		|| Candidate.ComboRanks.Overload < 0 || Candidate.ComboRanks.Overload > 2
		|| Candidate.GetSpentSkillPoints() + Candidate.UnspentSkillPoints
			!= FAuraGrowthState::GetEarnedSkillPoints(Candidate.PaidLevel)
		|| Candidate.ComboRanks.GetSpentPoints() + Candidate.UnspentComboPoints
			!= ((Candidate.PaidLevel >= 3) + (Candidate.PaidLevel >= 6) + (Candidate.PaidLevel >= 9)))
	{
		return false;
	}
	return true;
}

bool AAuraPlayerState::IsValidV4GrowthSnapshot(const FAuraGrowthState& Candidate)
{
	FAuraGrowthState V5 = Candidate;
	V5.SkillRanks.FireBoltPath = V5.SkillRanks.FireBolt >= 3 ? EAuraFireBoltPath::Pierce : EAuraFireBoltPath::None;
	return IsValidV5GrowthSnapshot(V5);
}

FAuraGrowthState AAuraPlayerState::MigrateV4GrowthSnapshot(const FAuraGrowthState& Legacy)
{
	FAuraGrowthState Migrated = MigrateV5GrowthSnapshot(Legacy);
	Migrated.SkillRanks.FireBoltPath = Migrated.SkillRanks.FireBolt >= 3
		? EAuraFireBoltPath::Pierce : EAuraFireBoltPath::None;
	return MigrateV5GrowthSnapshot(Migrated);
}

bool AAuraPlayerState::IsValidV3GrowthSnapshot(const FAuraGrowthState& Candidate)
{
	const int32 Q = Candidate.SkillRanks.ArcaneShards;
	const int32 E = Candidate.SkillRanks.Shock;
	if (Candidate.SkillRanks.FireBolt > 3 || Q > 3 || E > 3
		|| (Candidate.PaidLevel == 2 && Q + E > 1)
		|| (Candidate.PaidLevel >= 3 && (Q < 1 || E < 1))
		|| GetV3SpentSkillPoints(Candidate) + Candidate.UnspentSkillPoints
			!= GetV3EarnedSkillPoints(Candidate.PaidLevel))
	{
		return false;
	}
	FAuraGrowthState V5 = Candidate;
	V5.SkillRanks.FireBoltPath = V5.SkillRanks.FireBolt >= 3 ? EAuraFireBoltPath::Pierce : EAuraFireBoltPath::None;
	V5.UnspentSkillPoints = FAuraGrowthState::GetEarnedSkillPoints(V5.PaidLevel) - V5.GetSpentSkillPoints();
	return IsValidV5GrowthSnapshot(V5);
}

FAuraGrowthState AAuraPlayerState::MigrateV3GrowthSnapshot(const FAuraGrowthState& Legacy)
{
	FAuraGrowthState Migrated = MigrateV4GrowthSnapshot(Legacy);
	Migrated.UnspentSkillPoints = FAuraGrowthState::GetEarnedSkillPoints(Legacy.PaidLevel)
		- Migrated.GetSpentSkillPoints();
	return Migrated;
}

bool AAuraPlayerState::IsValidLegacyGrowthSnapshot(const FAuraGrowthState& Candidate)
{
	const int32 OldMaximumExperience = 480;
	const int32 RequiredExperience = Candidate.PaidLevel <= 1 ? 0
		: (Candidate.PaidLevel == 2 ? 60 : (Candidate.PaidLevel == 3 ? 160
			: (Candidate.PaidLevel == 4 ? 300 : 480)));
	const int32 SpentOldPoints = FMath::Max(0, Candidate.SkillRanks.FireBolt - 1)
		+ Candidate.SkillRanks.ArcaneShards + Candidate.SkillRanks.Shock;
	return Candidate.PaidLevel >= 1 && Candidate.PaidLevel <= 5
		&& Candidate.TotalExperience >= RequiredExperience
		&& Candidate.TotalExperience <= OldMaximumExperience && Candidate.Souls >= 0
		&& Candidate.RespecStonesPurchased >= 0 && Candidate.UnspentSkillPoints >= 0
		&& Candidate.SkillRanks.FireBolt >= 1 && Candidate.SkillRanks.FireBolt <= 3
		&& Candidate.SkillRanks.ArcaneShards >= 0 && Candidate.SkillRanks.ArcaneShards <= 3
		&& Candidate.SkillRanks.Shock >= 0 && Candidate.SkillRanks.Shock <= 3
		&& SpentOldPoints + Candidate.UnspentSkillPoints == Candidate.PaidLevel - 1;
}

FAuraGrowthState AAuraPlayerState::MigrateLegacyGrowthSnapshot(const FAuraGrowthState& Legacy)
{
	FAuraGrowthState Migrated = Legacy;
	// Preserve learned skills. Under v4, unlocking Q/E costs the same point
	// earned on any level; never auto-teach a missing skill during migration.
	while (Migrated.GetSpentSkillPoints()
		> FAuraGrowthState::GetEarnedSkillPoints(Migrated.PaidLevel))
	{
		if (Migrated.SkillRanks.FireBolt > 1) --Migrated.SkillRanks.FireBolt;
		else if (Migrated.SkillRanks.ArcaneShards > 1) --Migrated.SkillRanks.ArcaneShards;
		else if (Migrated.SkillRanks.Shock > 1) --Migrated.SkillRanks.Shock;
		else if (Migrated.SkillRanks.Shock > 0) --Migrated.SkillRanks.Shock;
		else if (Migrated.SkillRanks.ArcaneShards > 0) --Migrated.SkillRanks.ArcaneShards;
		else break;
	}
	Migrated.ComboRanks = FAuraComboRankState();
	Migrated.UnspentComboPoints = FAuraGrowthState::GetEarnedComboPoints(Legacy.PaidLevel);
	Migrated.UnspentSkillPoints = FAuraGrowthState::GetEarnedSkillPoints(Legacy.PaidLevel)
		- Migrated.GetSpentSkillPoints();
	Migrated.bFreeRespecClaimed = false;
	Migrated.SkillRanks.FireBoltPath = Migrated.SkillRanks.FireBolt >= 3
		? EAuraFireBoltPath::Pierce : EAuraFireBoltPath::None;
	return MigrateV5GrowthSnapshot(Migrated);
}

void AAuraPlayerState::EnsureGrowthInitialized()
{
	if (!HasAuthority() || bGrowthInitialized) return;
	UAuraRunSubsystem* Run = nullptr;
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone)
	{
		Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
		if (Run)
		{
			FAuraGrowthState Snapshot;
			if (Run->TryGetSavedGrowth(Snapshot))
			{
				RestoreGrowth(Snapshot);
			}
			else
			{
				FTransform Checkpoint;
				int32 OldLevel = 1;
				int32 OldExperience = 0;
				if (Run->TryGetCheckpointForWorld(GetWorld(), Checkpoint,
					&OldLevel, &OldExperience))
				{
					MigrateLegacyGrowth(OldLevel, OldExperience);
				}
			}
		}
	}
	if (!bGrowthInitialized)
	{
		bGrowthInitialized = true;
		CommitGrowth();
	}
	if (Run && !bPersistentItemsInitialized)
	{
		Run->RestoreStandalonePlayer(this);
		bPersistentItemsInitialized = true;
		Run->ObserveStandalonePlayer(this);
	}
}

void AAuraPlayerState::RestoreGrowth(const FAuraGrowthState& SavedGrowth)
{
	if (!HasAuthority() || bGrowthInitialized) return;
	if (!IsValidGrowthSnapshot(SavedGrowth))
	{
		UE_LOG(LogTemp, Error, TEXT("M12 rejected invalid growth snapshot for %s"), *GetName());
		bGrowthInitialized = true;
		CommitGrowth();
		return;
	}
	GrowthState = SavedGrowth;
	bGrowthInitialized = true;
	// v5 profiles may have banked enough XP for levels because the old rule
	// required a separate Archmage purchase. Promote them in memory once so
	// old saves enter the automatic-leveling rules without losing points.
	ApplyAutomaticLevelUps();
	CommitGrowth(true);
	UE_LOG(LogTemp, Display, TEXT("M12 growth restored. Level=%d XP=%d Souls=%d Points=%d"),
		GrowthState.PaidLevel, GrowthState.TotalExperience, GrowthState.Souls,
		GrowthState.UnspentSkillPoints);
}

void AAuraPlayerState::MigrateLegacyGrowth(const int32 LegacyLevel,
	const int32 LegacyExperience)
{
	if (!HasAuthority() || bGrowthInitialized) return;
	GrowthState = FAuraGrowthState();
	const FAuraPlayerProgressionBalance& Curve = UAuraCombatBalanceData::Get().Progression;
	const int32 MaxExperience = Curve.GetCumulativeExperienceForLevel(Curve.MaxLevel);
	GrowthState.TotalExperience = FMath::Clamp(LegacyExperience, 0, MaxExperience);
	const int32 Eligible = GetEligibleLevel();
	GrowthState.PaidLevel = FMath::Clamp(LegacyLevel, 1, Eligible);
	if (GrowthState.PaidLevel >= 2) GrowthState.SkillRanks.ArcaneShards = 1;
	if (GrowthState.PaidLevel >= 3) GrowthState.SkillRanks.Shock = 1;
	GrowthState.UnspentSkillPoints = FAuraGrowthState::GetEarnedSkillPoints(GrowthState.PaidLevel)
		- GrowthState.GetSpentSkillPoints();
	GrowthState.UnspentComboPoints = FAuraGrowthState::GetEarnedComboPoints(GrowthState.PaidLevel);
	bGrowthInitialized = true;
	ApplyAutomaticLevelUps();
	CommitGrowth(true);
	UE_LOG(LogTemp, Display,
		TEXT("M12 legacy progression migrated once. LegacyLevel=%d PaidLevel=%d XP=%d Points=%d"),
		LegacyLevel, GrowthState.PaidLevel, GrowthState.TotalExperience,
		GrowthState.UnspentSkillPoints);
}

void AAuraPlayerState::CommitGrowth(const bool bRestoreVitals)
{
	if (!HasAuthority()) return;
	if (AAuraCharacter* Character = Cast<AAuraCharacter>(GetPawn()))
	{
		Character->SyncGrowthFromPlayerState(bRestoreVitals);
	}
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Standalone)
	{
		if (UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>())
		{
			Run->UpdateSavedGrowth(GrowthState);
		}
	}
	ForceNetUpdate();
}

void AAuraPlayerState::AwardExperienceAndSouls(const int32 Experience,
	const int32 Souls)
{
	if (!HasAuthority() || Experience < 0 || Souls < 0 || !bGrowthInitialized) return;
	const FAuraPlayerProgressionBalance& Curve = UAuraCombatBalanceData::Get().Progression;
	const int32 MaximumExperience = Curve.GetCumulativeExperienceForLevel(Curve.MaxLevel);
	GrowthState.TotalExperience = FMath::Clamp(
		GrowthState.TotalExperience + Experience, 0, MaximumExperience);
	GrowthState.Souls = FMath::Max(0, GrowthState.Souls + Souls);
	const int32 LevelsGained = ApplyAutomaticLevelUps();
	CommitGrowth();
	if (LevelsGained > 0)
	{
		if (AAuraCharacter* Character = Cast<AAuraCharacter>(GetPawn()))
		{
			Character->MulticastLevelUpEffect();
		}
	}
	if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(GetOwner()))
	{
		const FString RewardMessage = LevelsGained > 0
			? FString::Printf(TEXT("经验 +%d  灵魂 +%d  ·  自动升至 %d 级"),
				Experience, Souls, GrowthState.PaidLevel)
			: FString::Printf(TEXT("经验 +%d  灵魂 +%d"), Experience, Souls);
		Controller->ShowCoopMessage(RewardMessage,
			LevelsGained > 0 ? FLinearColor(1.f, 0.78f, 0.16f)
				: FLinearColor(0.72f, 0.5f, 1.f), LevelsGained > 0 ? 2.2f : 1.5f);
	}
}

int32 AAuraPlayerState::ApplyAutomaticLevelUps()
{
	if (!HasAuthority()) return 0;
	const int32 OldLevel = GrowthState.PaidLevel;
	const int32 TargetLevel = GetEligibleLevel();
	if (TargetLevel <= OldLevel) return 0;

	const int32 SkillPointsGained = FAuraGrowthState::GetEarnedSkillPoints(TargetLevel)
		- FAuraGrowthState::GetEarnedSkillPoints(OldLevel);
	const int32 ComboPointsGained = FAuraGrowthState::GetEarnedComboPoints(TargetLevel)
		- FAuraGrowthState::GetEarnedComboPoints(OldLevel);
	GrowthState.PaidLevel = TargetLevel;
	GrowthState.UnspentSkillPoints += SkillPointsGained;
	GrowthState.UnspentComboPoints += ComboPointsGained;

	UE_LOG(LogTemp, Display,
		TEXT("Automatic level-up. Player=%s OldLevel=%d NewLevel=%d SkillPoints=+%d ComboPoints=+%d XP=%d"),
		*GetName(), OldLevel, TargetLevel, SkillPointsGained, ComboPointsGained,
		GrowthState.TotalExperience);
	return TargetLevel - OldLevel;
}

bool AAuraPlayerState::TryAllocateSkill(const EAuraPlayerSkill Skill,
	FString& OutFailureReason, const EAuraFireBoltPath FireBoltPath, const int32 EvolutionChoice)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized)
	{
		OutFailureReason = TEXT("成长数据尚未准备好");
		return false;
	}
	int32* Rank = nullptr;
	switch (Skill)
	{
	case EAuraPlayerSkill::FireBolt: Rank = &GrowthState.SkillRanks.FireBolt; break;
	case EAuraPlayerSkill::ArcaneShards: Rank = &GrowthState.SkillRanks.ArcaneShards; break;
	case EAuraPlayerSkill::Shock: Rank = &GrowthState.SkillRanks.Shock; break;
	default: break;
	}
	if (!Rank || *Rank >= UAuraCombatBalanceData::Get().SkillRanks.MaxRank)
	{
		OutFailureReason = TEXT("技能已达最高等级");
		return false;
	}
	if (*Rank == 0)
	{
		if (GrowthState.PaidLevel < 2 || Skill == EAuraPlayerSkill::FireBolt)
		{
			OutFailureReason = TEXT("当前等级不能解锁该技能");
			return false;
		}
	}
	if (GrowthState.UnspentSkillPoints <= 0)
	{
		OutFailureReason = TEXT("没有可用技艺点");
		return false;
	}
	const bool bChoosingFirePath = Skill == EAuraPlayerSkill::FireBolt && *Rank == 2;
	const bool bChoosingElementalPath = Skill != EAuraPlayerSkill::FireBolt && *Rank == 2;
	if ((bChoosingElementalPath && EvolutionChoice != 1 && EvolutionChoice != 2)
		|| (!bChoosingElementalPath && EvolutionChoice != 0))
	{ OutFailureReason = TEXT("技能第三级必须选择一种进化"); return false; }
	if ((Skill == EAuraPlayerSkill::ArcaneShards && GrowthState.SkillRanks.bArcaneEvolutionPending)
		|| (Skill == EAuraPlayerSkill::Shock && GrowthState.SkillRanks.bShockEvolutionPending))
	{ OutFailureReason = TEXT("请先免费补选技能进化"); return false; }
	if ((bChoosingFirePath && FireBoltPath != EAuraFireBoltPath::Pierce
			&& FireBoltPath != EAuraFireBoltPath::TripleVolley)
		|| (!bChoosingFirePath && FireBoltPath != EAuraFireBoltPath::None))
	{
		OutFailureReason = TEXT("火球第三级必须选择穿透或三火球");
		return false;
	}
	++*Rank;
	if (bChoosingFirePath) GrowthState.SkillRanks.FireBoltPath = FireBoltPath;
	if (bChoosingElementalPath && Skill == EAuraPlayerSkill::ArcaneShards)
		GrowthState.SkillRanks.ArcanePath = static_cast<EAuraArcanePath>(EvolutionChoice);
	if (bChoosingElementalPath && Skill == EAuraPlayerSkill::Shock)
		GrowthState.SkillRanks.ShockPath = static_cast<EAuraShockPath>(EvolutionChoice);
	--GrowthState.UnspentSkillPoints;
	CommitGrowth();
	UE_LOG(LogTemp, Display, TEXT("M12 skill point allocated. Player=%s Skill=%d Rank=%d Points=%d"),
		*GetName(), static_cast<int32>(Skill), *Rank, GrowthState.UnspentSkillPoints);
	return true;
}

bool AAuraPlayerState::TryChooseEvolution(const EAuraPlayerSkill Skill, const int32 Choice, FString& Failure)
{
	if (!HasAuthority() || !bGrowthInitialized || Choice < 1 || Choice > 2) return false;
	const bool bQ = Skill == EAuraPlayerSkill::ArcaneShards;
	if (!bQ && Skill != EAuraPlayerSkill::Shock) return false;
	const int32 Rank = bQ ? GrowthState.SkillRanks.ArcaneShards : GrowthState.SkillRanks.Shock;
	if (Rank == 2) return TryAllocateSkill(Skill, Failure, EAuraFireBoltPath::None, Choice);
	if (Rank < 3 || !(bQ ? GrowthState.SkillRanks.bArcaneEvolutionPending : GrowthState.SkillRanks.bShockEvolutionPending))
	{ Failure = TEXT("进化已选择，重新选择需要洗点"); return false; }
	if (bQ) { GrowthState.SkillRanks.ArcanePath = static_cast<EAuraArcanePath>(Choice); GrowthState.SkillRanks.bArcaneEvolutionPending = false; }
	else { GrowthState.SkillRanks.ShockPath = static_cast<EAuraShockPath>(Choice); GrowthState.SkillRanks.bShockEvolutionPending = false; }
	CommitGrowth();
	return true;
}

bool AAuraPlayerState::AdvanceFireBoltCast()
{
	if (!HasAuthority() || !bGrowthInitialized
		|| GrowthState.SkillRanks.FireBolt < 3
		|| GrowthState.SkillRanks.FireBoltPath != EAuraFireBoltPath::TripleVolley)
	{
		return false;
	}
	FireBoltVolleyCharge = (FireBoltVolleyCharge + 1) % 5;
	ForceNetUpdate();
	return FireBoltVolleyCharge == 0;
}

bool AAuraPlayerState::TryAllocateCombo(const EAuraComboMarkType Type, FString& OutFailureReason)
{
	OutFailureReason = TEXT("独立连携学习已取消，火球余烬可直接由雷电引爆");
	return false;
}

bool AAuraPlayerState::TryRespecSkills(const FAuraSkillRankState& NewRanks,
	const FGuid& StoneId, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized || !InventoryComponent)
	{
		OutFailureReason = TEXT("成长或背包数据尚未准备好");
		return false;
	}
	FAuraGrowthState Candidate = GrowthState;
	Candidate.SkillRanks = NewRanks;
	Candidate.UnspentSkillPoints = FAuraGrowthState::GetEarnedSkillPoints(Candidate.PaidLevel)
		- Candidate.GetSpentSkillPoints();
	Candidate.ComboRanks = FAuraComboRankState();
	Candidate.UnspentComboPoints = FAuraGrowthState::GetEarnedComboPoints(Candidate.PaidLevel);
	const bool bUseFreeRespec = Candidate.PaidLevel >= 6 && !Candidate.bFreeRespecClaimed;
	if (bUseFreeRespec) Candidate.bFreeRespecClaimed = true;
	if (!IsValidGrowthSnapshot(Candidate))
	{
		OutFailureReason = TEXT("当前等级不允许这样分配技能点");
		return false;
	}
	if (NewRanks.FireBolt == GrowthState.SkillRanks.FireBolt
		&& NewRanks.FireBoltPath == GrowthState.SkillRanks.FireBoltPath
		&& NewRanks.ArcaneShards == GrowthState.SkillRanks.ArcaneShards
		&& NewRanks.Shock == GrowthState.SkillRanks.Shock
		&& NewRanks.ArcanePath == GrowthState.SkillRanks.ArcanePath
		&& NewRanks.ShockPath == GrowthState.SkillRanks.ShockPath
		&& GrowthState.ComboRanks.GetSpentPoints() == 0)
	{
		OutFailureReason = TEXT("请选择与当前不同的技能分配");
		return false;
	}
	if (!bUseFreeRespec
		&& !InventoryComponent->TryConsumeRespecStone(StoneId, OutFailureReason)) return false;
	GrowthState = Candidate;
	FireBoltVolleyCharge = 0;
	if (AAuraCharacter* Character = Cast<AAuraCharacter>(GetPawn())) Character->ClearComboMarks();
	CommitGrowth();
	UE_LOG(LogTemp, Display, TEXT("M12 skills respecced. Player=%s Points=%d"),
		*GetName(), GrowthState.UnspentSkillPoints);
	return true;
}

bool AAuraPlayerState::TryPurchaseMerchantOffer(const int32 OfferId,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized || !InventoryComponent)
	{
		OutFailureReason = TEXT("成长或背包数据尚未准备好");
		return false;
	}
	const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
	FName ItemId;
	EAuraItemRarity Rarity;
	int32 Cost = 0;
	int32 Limit = 0;
	if (!Economy.GetOffer(OfferId, ItemId, Rarity, Cost, Limit,
		GrowthState.PaidLevel))
	{
		OutFailureReason = TEXT("商品不存在");
		return false;
	}
	if (Limit > 0 && (OfferId == 13 ? GrowthState.SmithMetalPurchased
		: GrowthState.RespecStonesPurchased) >= Limit)
	{
		OutFailureReason = TEXT("该商品已达到购买上限");
		return false;
	}
	if (GrowthState.Souls < Cost)
	{
		OutFailureReason = TEXT("灵魂不足");
		return false;
	}
	UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(ItemId);
	if (!Definition || Definition->ItemType == EAuraItemType::Quest
		|| (Definition->ItemType == EAuraItemType::Material && OfferId != 13))
	{
		OutFailureReason = TEXT("商品数据不可用");
		return false;
	}
	FAuraItemInstance Item;
	Item.Definition = Definition;
	Item.Rarity = Rarity;
	Item.ItemLevel = Definition->ItemType == EAuraItemType::Equipment
		? GrowthState.PaidLevel : 1;
	Item.SourceType = EAuraItemSourceType::Merchant;
	Item.SourceId = TEXT("CampMerchant");
	Item.EnsureIdentity();
	if (Definition->ItemType == EAuraItemType::Equipment)
	{
		AuraItems::RollEquipmentModifiers(Item);
	}
	if (!InventoryComponent->CanAcceptItem(Item))
	{
		OutFailureReason = TEXT("背包已满");
		return false;
	}
	if (InventoryComponent->TryAddItem(Item, OutFailureReason, false) != 1)
	{
		if (OutFailureReason.IsEmpty()) OutFailureReason = TEXT("购买的物品无法放入背包");
		return false;
	}
	GrowthState.Souls -= Cost;
	if (OfferId == 13) ++GrowthState.SmithMetalPurchased;
	else if (Limit > 0) ++GrowthState.RespecStonesPurchased;
	CommitGrowth();
	UE_LOG(LogTemp, Display, TEXT("M13 merchant purchase. Player=%s Offer=%d Item=%s Cost=%d Souls=%d"),
		*GetName(), OfferId, *ItemId.ToString(), Cost, GrowthState.Souls);
	return true;
}

bool AAuraPlayerState::TrySellMerchantItem(const FGuid& InstanceId,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized || !InventoryComponent)
	{
		OutFailureReason = TEXT("成长或背包数据尚未准备好");
		return false;
	}
	const FAuraInventoryEntry* Entry = InventoryComponent->FindEntryByInstanceId(InstanceId);
	if (!Entry || !Entry->Item.Definition || Entry->Item.Quantity < 1)
	{
		OutFailureReason = TEXT("背包中没有该物品");
		return false;
	}
	const bool bSellableMetal = Entry->Item.Definition->ItemId
		== FName(TEXT("Material.SmithMetal"));
	if (Entry->Item.Definition->ItemType == EAuraItemType::Quest
		|| (!Entry->Item.Definition->bDroppable && !bSellableMetal))
	{
		OutFailureReason = TEXT("任务物品或绑定物品不能出售");
		return false;
	}
	const FName SoldId = Entry->Item.Definition->ItemId;
	const int32 Price = UAuraCombatBalanceData::Get().CampEconomy.GetSellPrice(Entry->Item);
	if (Price <= 0)
	{
		OutFailureReason = TEXT("该物品无法出售");
		return false;
	}
	if (!InventoryComponent->TryRemoveItemByGuid(InstanceId, 1, OutFailureReason)) return false;
	GrowthState.Souls = FMath::Min(MAX_int32 - Price, GrowthState.Souls) + Price;
	CommitGrowth();
	UE_LOG(LogTemp, Display, TEXT("M13 merchant sale. Player=%s Item=%s Price=%d Souls=%d"),
		*GetName(), *SoldId.ToString(), Price, GrowthState.Souls);
	return true;
}

bool AAuraPlayerState::TryForgeWeapon(const FGuid& InstanceId,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized || !InventoryComponent || !EquipmentComponent)
	{
		OutFailureReason = TEXT("锻造服务尚未准备好");
		return false;
	}
	const FAuraInventoryEntry* BagEntry = InventoryComponent->FindEntryByInstanceId(InstanceId);
	const FAuraItemInstance* Weapon = BagEntry ? &BagEntry->Item : nullptr;
	if (!Weapon)
	{
		for (const FAuraEquippedItem& Equipped : EquipmentComponent->GetEquippedItems())
		{
			if (Equipped.Item.InstanceId == InstanceId) { Weapon = &Equipped.Item; break; }
		}
	}
	if (!Weapon || Weapon->InstanceId != InstanceId || !Weapon->Definition
		|| Weapon->Definition->ItemType != EAuraItemType::Equipment
		|| Weapon->Definition->EquipmentSlot == EAuraEquipmentSlot::None)
	{
		OutFailureReason = TEXT("你没有这件装备");
		return false;
	}
	const int32 NextLevel = Weapon->EnhancementLevel + 1;
	const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
	const int32 SoulCost = Economy.GetForgeSoulCost(NextLevel,
		Weapon->Definition->EquipmentSlot);
	const int32 MaterialCost = Economy.GetForgeMaterialCost(NextLevel);
	if (SoulCost == MAX_int32 || MaterialCost == MAX_int32)
	{
		OutFailureReason = TEXT("装备已达 +3 强化上限");
		return false;
	}
	if (GrowthState.Souls < SoulCost)
	{
		OutFailureReason = TEXT("灵魂不足");
		return false;
	}
	const FName MaterialId(TEXT("Material.ArcaneOreShard"));
	if (InventoryComponent->GetTotalQuantity(MaterialId) < MaterialCost)
	{
		OutFailureReason = TEXT("奥术矿石不足");
		return false;
	}
	const bool bEnhanced = BagEntry
		? InventoryComponent->TrySetWeaponEnhancement(InstanceId, NextLevel, OutFailureReason)
		: EquipmentComponent->TryEnhanceEquippedWeapon(InstanceId, NextLevel, OutFailureReason);
	if (!bEnhanced) return false;
	FString ConsumeFailure;
	if (!InventoryComponent->TryConsumeItemId(MaterialId, MaterialCost, ConsumeFailure))
	{
		UE_LOG(LogTemp, Error, TEXT("M13 forge material invariant failed after enhancement: %s"),
			*ConsumeFailure);
		OutFailureReason = TEXT("锻造材料扣除失败");
		return false;
	}
	GrowthState.Souls -= SoulCost;
	CommitGrowth();
	UE_LOG(LogTemp, Display,
		TEXT("M23 enhanced equipment. Player=%s Instance=%s Level=%d Souls=%d Materials=%d"),
		*GetName(), *InstanceId.ToString(), NextLevel, GrowthState.Souls,
		InventoryComponent->GetTotalQuantity(MaterialId));
	return true;
}

int32 AAuraPlayerState::GetChestMetalReward(const FName ChestId)
{
	static const TSet<FName> RouteChests = {
		TEXT("DungeonTreasure_LowerNorthDeadEnd"),
		TEXT("DungeonTreasure_UpperSouthDeadEnd"),
		TEXT("LowerSouthAmbushLoot"), TEXT("UpperNorthRiskLoot")
	};
	return RouteChests.Contains(ChestId) ? 2 : 0;
}

bool AAuraPlayerState::CanAcceptChestMetal(const int32 Quantity) const
{
	if (Quantity <= 0) return true;
	UAuraItemDefinition* Metal = UAuraItemCatalog::Get().FindItem(
		TEXT("Material.SmithMetal"));
	FAuraItemInstance Item;
	Item.Definition = Metal;
	Item.Quantity = Quantity;
	Item.SourceType = EAuraItemSourceType::Treasure;
	Item.EnsureIdentity();
	return InventoryComponent && Metal && InventoryComponent->CanAcceptItem(Item);
}

bool AAuraPlayerState::GrantChestMetal(const int32 Quantity, const FName ChestId)
{
	if (Quantity <= 0) return true;
	if (!HasAuthority() || HasClaimedTreasure(ChestId)
		|| Quantity != GetChestMetalReward(ChestId) || !CanAcceptChestMetal(Quantity))
		return false;
	FAuraItemInstance Item;
	Item.Definition = UAuraItemCatalog::Get().FindItem(TEXT("Material.SmithMetal"));
	Item.Quantity = Quantity;
	Item.SourceType = EAuraItemSourceType::Treasure;
	Item.SourceId = ChestId;
	Item.EnsureIdentity();
	FString Failure;
	if (InventoryComponent->TryAddItem(Item, Failure, false) != Quantity)
	{
		UE_LOG(LogTemp, Error, TEXT("M23 chest metal award failed: %s"), *Failure);
		return false;
	}
	return true;
}

bool AAuraPlayerState::TryCraftEquipment(const EAuraEquipmentSlot Slot,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized || !InventoryComponent)
	{
		OutFailureReason = TEXT("锻造服务尚未准备好");
		return false;
	}
	const FName ItemId = Slot == EAuraEquipmentSlot::Weapon
		? FName(TEXT("Equipment.ArcaneStaff")) : Slot == EAuraEquipmentSlot::Armor
			? FName(TEXT("Equipment.WardenArmor")) : Slot == EAuraEquipmentSlot::Head
				? FName(TEXT("Equipment.SeerHeadpiece")) : Slot == EAuraEquipmentSlot::Shoes
					? FName(TEXT("Equipment.WindShoes")) : NAME_None;
	UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(ItemId);
	if (ItemId.IsNone() || !Definition || Definition->ItemType != EAuraItemType::Equipment
		|| Definition->EquipmentSlot != Slot)
	{
		OutFailureReason = TEXT("不能打造该部位");
		return false;
	}
	const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
	if (GrowthState.Souls < Economy.CraftSoulCost)
	{
		OutFailureReason = TEXT("灵魂不足");
		return false;
	}
	const FName MetalId(TEXT("Material.SmithMetal"));
	if (InventoryComponent->GetTotalQuantity(MetalId) < Economy.CraftMetalCost)
	{
		OutFailureReason = TEXT("金属不足");
		return false;
	}
	// Require a free output slot even when consuming the final metal stack would
	// free one. This makes the inventory mutation fail-closed and reversible.
	if (InventoryComponent->GetOccupiedSlotCount() >= InventoryComponent->GetCapacity())
	{
		OutFailureReason = TEXT("背包已满，请先腾出一个空格");
		return false;
	}
	const int32 Level = GrowthState.PaidLevel;
	const int32 Roll = FMath::RandRange(1, 100);
	EAuraItemRarity Rarity = EAuraItemRarity::Common;
	if (Level < 4)
	{
		Rarity = Roll <= 55 ? EAuraItemRarity::Common
			: (Roll <= 90 ? EAuraItemRarity::Quality : EAuraItemRarity::Fine);
	}
	else if (Level < 7)
	{
		Rarity = Roll <= 30 ? EAuraItemRarity::Common
			: (Roll <= 70 ? EAuraItemRarity::Quality
				: (Roll <= 95 ? EAuraItemRarity::Fine : EAuraItemRarity::Epic));
	}
	else
	{
		Rarity = Roll <= 10 ? EAuraItemRarity::Common
			: (Roll <= 45 ? EAuraItemRarity::Quality
				: (Roll <= 85 ? EAuraItemRarity::Fine
					: (Roll <= 98 ? EAuraItemRarity::Epic : EAuraItemRarity::Legendary)));
	}
	if (GrowthState.CraftPityMisses >= 2 && Rarity < EAuraItemRarity::Fine)
		Rarity = EAuraItemRarity::Fine;
	FAuraItemInstance Crafted;
	Crafted.Definition = Definition;
	Crafted.Rarity = Rarity;
	Crafted.ItemLevel = Level;
	Crafted.SourceType = EAuraItemSourceType::Crafted;
	Crafted.SourceId = TEXT("CampBlacksmith");
	Crafted.EnsureIdentity();
	AuraItems::RollEquipmentModifiers(Crafted);
	if (!InventoryComponent->CanAcceptItem(Crafted)
		|| InventoryComponent->TryAddItem(Crafted, OutFailureReason, false) != 1)
	{
		if (OutFailureReason.IsEmpty()) OutFailureReason = TEXT("背包无法接收打造装备");
		return false;
	}
	FString ConsumeFailure;
	if (!InventoryComponent->TryConsumeItemId(MetalId, Economy.CraftMetalCost,
		ConsumeFailure))
	{
		InventoryComponent->TryRemoveItemByGuid(Crafted.InstanceId, 1, ConsumeFailure);
		OutFailureReason = TEXT("材料扣除失败，打造已撤销");
		return false;
	}
	GrowthState.Souls -= Economy.CraftSoulCost;
	GrowthState.CraftPityMisses = Rarity >= EAuraItemRarity::Fine
		? 0 : FMath::Min(2, GrowthState.CraftPityMisses + 1);
	CommitGrowth();
	UE_LOG(LogTemp, Display, TEXT("M23 crafted. Player=%s Slot=%d Rarity=%d GUID=%s Pity=%d"),
		*GetName(), static_cast<int32>(Slot), static_cast<int32>(Rarity),
		*Crafted.InstanceId.ToString(), GrowthState.CraftPityMisses);
	return true;
}

bool AAuraPlayerState::TrySmeltEquipment(const FGuid& InstanceId,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!HasAuthority() || !bGrowthInitialized || !InventoryComponent)
	{
		OutFailureReason = TEXT("熔炼服务尚未准备好");
		return false;
	}
	const FAuraInventoryEntry* Entry = InventoryComponent->FindEntryByInstanceId(InstanceId);
	if (!Entry || !Entry->Item.Definition
		|| Entry->Item.Definition->ItemType != EAuraItemType::Equipment
		|| !Entry->Item.Definition->bDroppable || Entry->Item.Quantity != 1)
	{
		OutFailureReason = TEXT("只能熔炼背包中可丢弃的装备，请先卸下已装备物品");
		return false;
	}
	UAuraItemDefinition* Metal = UAuraItemCatalog::Get().FindItem(
		TEXT("Material.SmithMetal"));
	if (!Metal)
	{
		OutFailureReason = TEXT("金属物品数据不可用");
		return false;
	}
	const FAuraItemInstance Original = Entry->Item;
	FAuraItemInstance Yield;
	Yield.Definition = Metal;
	Yield.Quantity = FMath::Clamp(static_cast<int32>(Original.Rarity) + 1, 1, 5);
	Yield.SourceType = EAuraItemSourceType::Crafted;
	Yield.SourceId = TEXT("SmeltedEquipment");
	Yield.EnsureIdentity();
	// The consumed gear frees one slot; stacked metal may use even less space.
	if (!InventoryComponent->TryRemoveItemByGuid(InstanceId, 1, OutFailureReason))
		return false;
	const int32 Added = InventoryComponent->CanAcceptItem(Yield)
		? InventoryComponent->TryAddItem(Yield, OutFailureReason, false) : 0;
	if (Added != Yield.Quantity)
	{
		FString RestoreFailure;
		if (Added > 0)
			InventoryComponent->TryConsumeItemId(Metal->ItemId, Added, RestoreFailure);
		InventoryComponent->TryAddItem(Original, RestoreFailure, false);
		if (OutFailureReason.IsEmpty()) OutFailureReason = TEXT("金属无法放入背包，熔炼已撤销");
		return false;
	}
	CommitGrowth();
	UE_LOG(LogTemp, Display, TEXT("M23 smelted. Player=%s GUID=%s Metal=%d"),
		*GetName(), *InstanceId.ToString(), Yield.Quantity);
	return true;
}

void AAuraPlayerState::SetPlayerDead(const bool bNewDead)
{
	if (!HasAuthority()) return;
	bPlayerDead = bNewDead;
	ForceNetUpdate();
}

void AAuraPlayerState::MarkTreasureClaimed(const FName TreasureId)
{
	if (!HasAuthority() || TreasureId.IsNone()
		|| ClaimedTreasureIds.Contains(TreasureId))
	{
		return;
	}
	ClaimedTreasureIds.Add(TreasureId);
	ForceNetUpdate();
}

bool AAuraPlayerState::HasClaimedTreasure(const FName TreasureId) const
{
	return !TreasureId.IsNone() && ClaimedTreasureIds.Contains(TreasureId);
}
