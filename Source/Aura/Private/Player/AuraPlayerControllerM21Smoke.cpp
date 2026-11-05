// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Character/AuraEnemy.h"
#include "AI/AuraAIController.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Abilities/AuraFireBoltAbility.h"
#include "AbilitySystem/AuraFireBoltProjectile.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Game/AuraProfileSaveGame.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/Parse.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraM21Smoke, Log, All);

namespace
{
	struct FFractureRankSmokeState
	{
		bool bInitial = false;
		bool bImmunity = false;
		bool bCooldown = false;
	};

	void FreezeFractureTarget(AAuraEnemy* Enemy)
	{
		if (!Enemy) return;
		Enemy->GetCharacterMovement()->DisableMovement();
		if (AAuraAIController* AI = Cast<AAuraAIController>(Enemy->GetController()))
		{
			AI->StopMovement();
			AI->SetActorTickEnabled(false);
		}
		if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
		{
			ASC->SetNumericAttributeBase(UAuraAttributeSet::GetMaxHealthAttribute(), 1000.f);
			ASC->SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), 1000.f);
		}
	}

	FString SavedItemIdentity(const FAuraSavedItem& Item)
	{
		return FString::Printf(TEXT("%s:%s:%d:%d:%d:%d:%d:%d"),
			*Item.InstanceId.ToString(), *Item.DefinitionId.ToString(),
			Item.Quantity, Item.ItemLevel, Item.EnhancementLevel,
			static_cast<int32>(Item.Rarity), Item.RollSeed, Item.RolledModifiers.Num());
	}

	FString RuntimeItemIdentity(const FAuraItemInstance& Item)
	{
		return FString::Printf(TEXT("%s:%s:%d:%d:%d:%d:%d:%d"),
			*Item.InstanceId.ToString(),
			Item.Definition ? *Item.Definition->ItemId.ToString() : TEXT("None"),
			Item.Quantity, Item.ItemLevel, Item.EnhancementLevel,
			static_cast<int32>(Item.Rarity), Item.RollSeed, Item.RolledModifiers.Num());
	}

	uint32 IdentityHash(TArray<FString>& Identities)
	{
		Identities.Sort();
		return FCrc::StrCrc32(*FString::Join(Identities, TEXT("|")));
	}
}

void AAuraPlayerController::AuraTestGrowthM21()
{
	AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	if (!HasAuthority() || !IsLocalController() || !Caster || !State || !GetWorld())
	{
		UE_LOG(LogAuraM21Smoke, Error, TEXT("AURA_M21_GROWTH_SMOKE Passed=false Setup=false"));
		return;
	}
	const FAuraPlayerProgressionBalance& Curve = UAuraCombatBalanceData::Get().Progression;
	bool bPassed = Curve.MaxLevel == 10
		&& Curve.GetCumulativeExperienceForLevel(10) == 1730
		&& State->GetGrowthState().PaidLevel == 1;
	FString Failure;
	bPassed &= !State->TryAllocateCombo(EAuraComboMarkType::Ember, Failure);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* Target = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
		Caster->GetActorLocation() + FVector(600.f, 0.f, 2200.f),
		FRotator::ZeroRotator, Spawn);
	if (!Target)
	{
		UE_LOG(LogAuraM21Smoke, Error, TEXT("AURA_M21_GROWTH_SMOKE Passed=false Spawn=false"));
		return;
	}
	Target->GetCharacterMovement()->DisableMovement();
	Caster->ApplyComboMark(Target, EAuraComboMarkType::Ember, Caster->AllocateComboCastId());
	bPassed &= !Caster->HasComboMark(Target, EAuraComboMarkType::Ember);
	State->AwardExperienceAndSouls(1730, 1095);
	bPassed &= State->GetEligibleLevel() == 10 && Caster->GetCharacterLevel() == 10
		&& State->GetGrowthState().PaidLevel == 10
		&& State->GetGrowthState().UnspentSkillPoints == 9
		&& State->GetGrowthState().UnspentComboPoints == 3;
	bPassed &= State->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards, Failure)
		&& !State->TryAllocateCombo(EAuraComboMarkType::Fracture, Failure)
		&& State->TryAllocateSkill(EAuraPlayerSkill::Shock, Failure)
		&& State->TryAllocateCombo(EAuraComboMarkType::Ember, Failure)
		&& Caster->GetComboRank(EAuraComboMarkType::Ember) == 1
		&& State->TryAllocateCombo(EAuraComboMarkType::Ember, Failure)
		&& Caster->GetComboRank(EAuraComboMarkType::Ember) == 2;
	const int32 FirstCast = Caster->AllocateComboCastId();
	Caster->ApplyComboMark(Target, EAuraComboMarkType::Ember, FirstCast);
	Caster->ApplyComboMark(Target, EAuraComboMarkType::Ember, FirstCast);
	Caster->ApplyComboMark(Target, EAuraComboMarkType::Ember,
		Caster->AllocateComboCastId());
	Caster->ApplyComboMark(Target, EAuraComboMarkType::Ember, FirstCast);
	bPassed &= Caster->GetComboMarks().Num() == 1
		&& Caster->GetComboMarks()[0].StackCount == 2;
	Caster->ApplyComboMark(Target, EAuraComboMarkType::Ember,
		Caster->AllocateComboCastId());
	int32 Stacks = 0;
	bPassed &= Caster->TryConsumeComboMark(Target, EAuraComboMarkType::Ember,
		Caster->AllocateComboCastId(), &Stacks) && Stacks == 3;
	bPassed &= State->TryAllocateCombo(EAuraComboMarkType::Fracture, Failure)
		&& State->GetGrowthState().ComboRanks.GetSpentPoints() == 3
		&& AAuraPlayerState::IsValidGrowthSnapshot(State->GetGrowthState());
	bPassed &= State->GetGrowthState().Souls == 1095
		&& State->GetGrowthState().UnspentSkillPoints == 7;
	FAuraGrowthState Generalist = State->GetGrowthState();
	Generalist.ComboRanks.Ember = 1;
	Generalist.ComboRanks.Fracture = 1;
	Generalist.ComboRanks.Overload = 1;
	Generalist.UnspentComboPoints = 0;
	bPassed &= AAuraPlayerState::IsValidGrowthSnapshot(Generalist);
	FAuraGrowthState Legacy;
	Legacy.PaidLevel = 5;
	Legacy.TotalExperience = 480;
	Legacy.Souls = 123;
	Legacy.SkillRanks.FireBolt = 3;
	Legacy.SkillRanks.ArcaneShards = 1;
	Legacy.SkillRanks.Shock = 1;
	bPassed &= AAuraPlayerState::IsValidLegacyGrowthSnapshot(Legacy);
	const FAuraGrowthState Migrated = AAuraPlayerState::MigrateLegacyGrowthSnapshot(Legacy);
	bPassed &= AAuraPlayerState::IsValidGrowthSnapshot(Migrated)
		&& Migrated.Souls == 123 && Migrated.UnspentComboPoints == 1;
	Legacy.PaidLevel = 3;
	Legacy.TotalExperience = 160;
	Legacy.SkillRanks.FireBolt = 3;
	Legacy.SkillRanks.ArcaneShards = 0;
	Legacy.SkillRanks.Shock = 0;
	bPassed &= AAuraPlayerState::IsValidLegacyGrowthSnapshot(Legacy)
		&& AAuraPlayerState::IsValidGrowthSnapshot(
			AAuraPlayerState::MigrateLegacyGrowthSnapshot(Legacy));
	FAuraSkillRankState BaseSkills;
	BaseSkills.FireBolt = 1;
	BaseSkills.ArcaneShards = 1;
	BaseSkills.Shock = 1;
	bPassed &= State->TryRespecSkills(BaseSkills, FGuid(), Failure);
	bPassed &= State->GetGrowthState().bFreeRespecClaimed
		&& State->GetGrowthState().ComboRanks.GetSpentPoints() == 0
		&& State->GetGrowthState().UnspentComboPoints == 3;
	// Rank 3 is atomic: no point is spent until one valid specialization is chosen.
	bPassed &= State->TryAllocateSkill(EAuraPlayerSkill::FireBolt, Failure);
	const int32 PointsBeforeChoice = State->GetGrowthState().UnspentSkillPoints;
	bPassed &= !State->TryAllocateSkill(EAuraPlayerSkill::FireBolt, Failure)
		&& State->GetGrowthState().UnspentSkillPoints == PointsBeforeChoice
		&& State->GetGrowthState().SkillRanks.FireBolt == 2;
	bPassed &= State->TryAllocateSkill(EAuraPlayerSkill::FireBolt, Failure,
		EAuraFireBoltPath::TripleVolley)
		&& State->GetGrowthState().SkillRanks.FireBoltPath == EAuraFireBoltPath::TripleVolley;
	for (int32 Shot = 1; Shot <= 4; ++Shot)
	{
		bPassed &= !State->AdvanceFireBoltCast();
		bPassed &= State->GetFireBoltVolleyCharge() == Shot;
	}
	FAuraGrowthState PierceAlternative = State->GetGrowthState();
	PierceAlternative.SkillRanks.FireBoltPath = EAuraFireBoltPath::Pierce;
	bPassed &= AAuraPlayerState::IsValidGrowthSnapshot(PierceAlternative)
		&& AAuraPlayerState::IsValidGrowthSnapshot(State->GetGrowthState());
	FAuraGrowthState V4Alternative = PierceAlternative;
	V4Alternative.SkillRanks.FireBoltPath = EAuraFireBoltPath::None;
	bPassed &= AAuraPlayerState::IsValidV4GrowthSnapshot(V4Alternative)
		&& AAuraPlayerState::MigrateV4GrowthSnapshot(V4Alternative)
			.SkillRanks.FireBoltPath == EAuraFireBoltPath::Pierce;
	bPassed &= State->TryAllocateCombo(EAuraComboMarkType::Overload, Failure)
		&& State->TryAllocateCombo(EAuraComboMarkType::Overload, Failure);
	const float BaseSpeed = Caster->GetCharacterMovement()->MaxWalkSpeed;
	UAbilitySystemComponent* CasterASC = Caster->GetAbilitySystemComponent();
	if (CasterASC) CasterASC->SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), 17.f);
	const float BaseMana = CasterASC
		? CasterASC->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : -1.f;
	Caster->ExecuteOverload(Target->GetActorLocation() + FVector(0.f, 0.f, 500.f), 0.f);
	bPassed &= Caster->GetCharacterMovement()->MaxWalkSpeed > BaseSpeed
		&& CasterASC && FMath::IsNearlyEqual(CasterASC->GetNumericAttribute(
			UAuraAttributeSet::GetManaAttribute()), BaseMana, 0.01f);
	Target->Destroy();
	const bool bVolleyActivated = Caster->GetAbilitySystemComponent()
		&& Caster->GetAbilitySystemComponent()->TryActivateAbilityByClass(
			UAuraFireBoltAbility::StaticClass());
	bPassed &= bVolleyActivated;
	const TWeakObjectPtr<AAuraCharacter> WeakVolleyCaster(Caster);
	const TWeakObjectPtr<AAuraPlayerState> WeakVolleyState(State);
	FTimerHandle VolleyCheckTimer;
	GetWorldTimerManager().SetTimer(VolleyCheckTimer,
		[WeakVolleyCaster, WeakVolleyState, bVolleyActivated]()
	{
		int32 HoldingOrbs = 0;
		if (AAuraCharacter* Character = WeakVolleyCaster.Get())
		{
			for (TActorIterator<AAuraFireBoltProjectile> It(Character->GetWorld()); It; ++It)
				if (It->GetOwner() == Character && It->IsOrbitHolding()) ++HoldingOrbs;
		}
		UE_LOG(LogAuraM21Smoke, Display,
			TEXT("AURA_FIREBOLT_VOLLEY_SMOKE Passed=%s Activated=%s HoldingOrbs=%d Charge=%d"),
			bVolleyActivated && HoldingOrbs == 3 && WeakVolleyState.IsValid()
				&& WeakVolleyState->GetFireBoltVolleyCharge() == 0
					? TEXT("true") : TEXT("false"),
			bVolleyActivated ? TEXT("true") : TEXT("false"), HoldingOrbs,
			WeakVolleyState.IsValid() ? WeakVolleyState->GetFireBoltVolleyCharge() : -1);
	}, 0.37f, false);
	const TWeakObjectPtr<AAuraCharacter> WeakCaster(Caster);
	FTimerHandle VerifyTimer;
	GetWorldTimerManager().SetTimer(VerifyTimer,
		[WeakCaster, bPassed, BaseSpeed, BaseMana]()
	{
		const UAbilitySystemComponent* CurrentASC = WeakCaster.IsValid()
			? WeakCaster->GetAbilitySystemComponent() : nullptr;
		const bool bExpired = WeakCaster.IsValid()
			&& FMath::IsNearlyEqual(WeakCaster->GetCharacterMovement()->MaxWalkSpeed,
				BaseSpeed, 0.1f)
			&& CurrentASC && FMath::IsNearlyEqual(CurrentASC->GetNumericAttribute(
				UAuraAttributeSet::GetManaAttribute()), BaseMana, 0.01f);
		UE_LOG(LogAuraM21Smoke, Display,
			TEXT("AURA_M21_GROWTH_SMOKE Passed=%s Progression=%s BuffExpired=%s"),
			bPassed && bExpired ? TEXT("true") : TEXT("false"),
			bPassed ? TEXT("true") : TEXT("false"),
			bExpired ? TEXT("true") : TEXT("false"));
	}, 4.3f, false);
}

void AAuraPlayerController::AuraTestGrowthCoopM21()
{
	if (!HasAuthority() || !GetWorld()) return;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	bool bPassed = GameState && GameState->PlayerArray.Num() == 2;
	if (bPassed)
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			AAuraPlayerState* TestState = Cast<AAuraPlayerState>(GameState->PlayerArray[Index]);
			FString Failure;
			bPassed &= TestState && TestState->GetPawn();
			if (!TestState) continue;
			TestState->AwardExperienceAndSouls(160, 70);
			bPassed &= TestState->TryAllocateSkill(Index == 0
				? EAuraPlayerSkill::ArcaneShards : EAuraPlayerSkill::Shock, Failure);
			bPassed &= TestState->TryAllocateSkill(Index == 0
				? EAuraPlayerSkill::Shock : EAuraPlayerSkill::ArcaneShards, Failure);
			bPassed &= TestState->TryAllocateCombo(Index == 0
				? EAuraComboMarkType::Ember : EAuraComboMarkType::Fracture, Failure);
			const FAuraGrowthState& Growth = TestState->GetGrowthState();
			bPassed &= Growth.PaidLevel == 3 && Growth.TotalExperience == 160
				&& Growth.Souls == 70 && Growth.UnspentComboPoints == 0
				&& Growth.ComboRanks.GetSpentPoints() == 1
				&& AAuraPlayerState::IsValidGrowthSnapshot(Growth);
		}
	}
	UE_LOG(LogAuraM21Smoke, Display,
		TEXT("AURA_M21_COOP_SERVER Passed=%s Players=%d"),
		bPassed ? TEXT("true") : TEXT("false"),
		GameState ? GameState->PlayerArray.Num() : 0);
	FTimerHandle DoneTimer;
	GetWorldTimerManager().SetTimer(DoneTimer, []()
	{
		UE_LOG(LogAuraM21Smoke, Display, TEXT("AURA_M21_COOP_SERVER_DONE"));
	}, 8.f, false);
}

void AAuraPlayerController::AuraObserveGrowthM21()
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const AAuraCharacter* TestPawn = Cast<AAuraCharacter>(GetPawn());
	const FAuraGrowthState* Growth = State ? &State->GetGrowthState() : nullptr;
	const bool bPassed = Growth && TestPawn && Growth->PaidLevel == 3
		&& Growth->TotalExperience == 160 && Growth->Souls == 70
		&& Growth->SkillRanks.ArcaneShards == 1 && Growth->SkillRanks.Shock == 1
		&& Growth->ComboRanks.GetSpentPoints() == 1
		&& Growth->ComboRanks.Overload == 0
		&& TestPawn->GetCharacterLevel() == 3
		&& TestPawn->GetComboRank(EAuraComboMarkType::Ember) == Growth->ComboRanks.Ember
		&& TestPawn->GetComboRank(EAuraComboMarkType::Fracture) == Growth->ComboRanks.Fracture;
	UE_LOG(LogAuraM21Smoke, Display,
		TEXT("AURA_M21_COOP_CLIENT Passed=%s Level=%d Ember=%d Fracture=%d"),
		bPassed ? TEXT("true") : TEXT("false"),
		Growth ? Growth->PaidLevel : -1,
		Growth ? Growth->ComboRanks.Ember : -1,
		Growth ? Growth->ComboRanks.Fracture : -1);
}

void AAuraPlayerController::AuraTestSaveReadOnlyM21()
{
	FString Slot = TEXT("AuraProfile");
	FParse::Value(FCommandLine::Get(), TEXT("AuraM21AuditSlot="), Slot);
	bool bValidSlot = !Slot.IsEmpty() && Slot.Len() <= 64;
	for (const TCHAR SlotCharacter : Slot)
	{
		bValidSlot &= FChar::IsAlnum(SlotCharacter)
			|| SlotCharacter == TEXT('_') || SlotCharacter == TEXT('-');
	}
	const UAuraProfileSaveGame* Save = bValidSlot
		? Cast<UAuraProfileSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0)) : nullptr;
	if (!Save)
	{
		UE_LOG(LogAuraM21Smoke, Error,
			TEXT("AURA_M21_SAVE_READONLY Passed=false Slot=%s Load=false"), *Slot);
		return;
	}
	const bool bOldSchema = Save->SchemaVersion < UAuraProfileSaveGame::CurrentVersion;
	const bool bValidOld = Save->SchemaVersion < 3
		? AAuraPlayerState::IsValidLegacyGrowthSnapshot(Save->Growth)
		: (Save->SchemaVersion == 3
			? AAuraPlayerState::IsValidV3GrowthSnapshot(Save->Growth)
			: (Save->SchemaVersion == 4
				? AAuraPlayerState::IsValidV4GrowthSnapshot(Save->Growth)
				: AAuraPlayerState::IsValidGrowthSnapshot(Save->Growth)));
	const FAuraGrowthState Candidate = Save->SchemaVersion < 3
		? AAuraPlayerState::MigrateLegacyGrowthSnapshot(Save->Growth)
		: (Save->SchemaVersion == 3
			? AAuraPlayerState::MigrateV3GrowthSnapshot(Save->Growth)
			: (Save->SchemaVersion == 4
				? AAuraPlayerState::MigrateV4GrowthSnapshot(Save->Growth) : Save->Growth));
	TArray<FString> Identities;
	for (const FAuraSavedBackpackEntry& Entry : Save->Backpack)
	{
		Identities.Add(SavedItemIdentity(Entry.Item));
	}
	for (const FAuraSavedEquipmentEntry& Entry : Save->Equipment)
	{
		Identities.Add(SavedItemIdentity(Entry.Item));
	}
	const uint32 ItemsHash = IdentityHash(Identities);
	const bool bPassed = Save->bHasProfile && bValidOld
		&& AAuraPlayerState::IsValidGrowthSnapshot(Candidate);
	UE_LOG(LogAuraM21Smoke, Display,
		TEXT("AURA_M21_SAVE_READONLY Passed=%s Slot=%s Schema=%d Level=%d XP=%d Souls=%d SkillPoints=%d ComboPoints=%d Bag=%d Gear=%d ItemsHash=%u Checkpoint=%s Seals=%d Claimed=%d"),
		bPassed ? TEXT("true") : TEXT("false"), *Slot, Save->SchemaVersion,
		Candidate.PaidLevel, Candidate.TotalExperience, Candidate.Souls,
		Candidate.UnspentSkillPoints, Candidate.UnspentComboPoints,
		Save->Backpack.Num(), Save->Equipment.Num(), ItemsHash,
		*Save->ActiveCheckpointId.ToString(), Save->ActivatedSealIds.Num(),
		Save->ClaimedTreasureIds.Num());
}

void AAuraPlayerController::AuraTestLoadedSaveM21()
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	const UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
	const UAuraEquipmentComponent* Gear = State ? State->GetEquipmentComponent() : nullptr;
	FAuraGrowthState SavedGrowth;
	const bool bRestored = Run && Run->IsPersistentSaveEnabled()
		&& Run->TryGetSavedGrowth(SavedGrowth);
	TArray<FString> Identities;
	if (Bag)
	{
		for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
		{
			Identities.Add(RuntimeItemIdentity(Entry.Item));
		}
	}
	if (Gear)
	{
		for (const FAuraEquippedItem& Entry : Gear->GetEquippedItems())
		{
			Identities.Add(RuntimeItemIdentity(Entry.Item));
		}
	}
	const uint32 ItemsHash = IdentityHash(Identities);
	const bool bSameGrowth = State && bRestored
		&& State->GetGrowthState().PaidLevel == SavedGrowth.PaidLevel
		&& State->GetGrowthState().TotalExperience == SavedGrowth.TotalExperience
		&& State->GetGrowthState().Souls == SavedGrowth.Souls
		&& State->GetGrowthState().UnspentSkillPoints == SavedGrowth.UnspentSkillPoints
		&& State->GetGrowthState().UnspentComboPoints == SavedGrowth.UnspentComboPoints;
	const bool bWritten = bSameGrowth && Run->FlushPersistentSave();
	const bool bPassed = bWritten && Bag && Gear
		&& AAuraPlayerState::IsValidGrowthSnapshot(SavedGrowth);
	UE_LOG(LogAuraM21Smoke, Display,
		TEXT("AURA_M21_SAVE_LOADED Passed=%s Level=%d XP=%d Souls=%d SkillPoints=%d ComboPoints=%d Bag=%d Gear=%d ItemsHash=%u Checkpoint=%s Seals=%d Claimed=%d"),
		bPassed ? TEXT("true") : TEXT("false"), SavedGrowth.PaidLevel,
		SavedGrowth.TotalExperience, SavedGrowth.Souls,
		SavedGrowth.UnspentSkillPoints, SavedGrowth.UnspentComboPoints,
		Bag ? Bag->GetEntries().Num() : -1,
		Gear ? Gear->GetEquippedItems().Num() : -1, ItemsHash,
		Run ? *Run->GetActiveCheckpointId().ToString() : TEXT("None"),
		Run ? Run->GetActivatedSealCount() : 0,
		Run ? Run->GetClaimedTreasureCount() : 0);
}

void AAuraPlayerController::AuraTestSeedV2SaveM21()
{
	FString Slot;
	FParse::Value(FCommandLine::Get(), TEXT("AuraM21AuditSlot="), Slot);
	if (!Slot.StartsWith(TEXT("AuraM21V2_")) || Slot.Len() > 64
		|| UGameplayStatics::DoesSaveGameExist(Slot, 0) || !GetPawn())
	{
		UE_LOG(LogAuraM21Smoke, Error,
			TEXT("AURA_M21_V2_SEED Passed=false Slot=%s Guard=false"), *Slot);
		return;
	}
	UAuraProfileSaveGame* Save = Cast<UAuraProfileSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAuraProfileSaveGame::StaticClass()));
	if (!Save) return;
	Save->SchemaVersion = 2;
	Save->bHasProfile = true;
	Save->Growth.PaidLevel = 5;
	Save->Growth.TotalExperience = 480;
	Save->Growth.Souls = 777;
	Save->Growth.SkillRanks.FireBolt = 3;
	Save->Growth.SkillRanks.ArcaneShards = 1;
	Save->Growth.SkillRanks.Shock = 1;
	Save->Growth.UnspentSkillPoints = 0;
	FAuraSavedBackpackEntry& Ore = Save->Backpack.AddDefaulted_GetRef();
	Ore.SlotIndex = 0;
	Ore.Item.InstanceId = FGuid::NewGuid();
	Ore.Item.DefinitionId = TEXT("Material.ArcaneOreShard");
	Ore.Item.Quantity = 4;
	Ore.Item.SourceType = EAuraItemSourceType::Treasure;
	Ore.Item.SourceId = TEXT("M21V2SeedChest");
	FAuraSavedEquipmentEntry& Staff = Save->Equipment.AddDefaulted_GetRef();
	Staff.Slot = EAuraEquipmentSlot::Weapon;
	Staff.Item.InstanceId = FGuid::NewGuid();
	Staff.Item.DefinitionId = TEXT("Equipment.ArcaneStaff");
	Staff.Item.ItemLevel = 5;
	Staff.Item.EnhancementLevel = 2;
	Staff.Item.Rarity = EAuraItemRarity::Fine;
	Staff.Item.RollSeed = 12345;
	Staff.Item.SourceType = EAuraItemSourceType::EnemyDrop;
	Staff.Item.SourceId = TEXT("M21V2SeedGhoul");
	FAuraItemStatModifier& Modifier = Staff.Item.RolledModifiers.AddDefaulted_GetRef();
	Modifier.StatTag = FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Intelligence"));
	Modifier.Magnitude = 2.5f;
	Save->bHasCheckpoint = true;
	Save->CheckpointLevelName = TEXT("DungeonDemo");
	Save->ActiveCheckpointId = TEXT("M21V2SeedCheckpoint");
	Save->RespawnTransform = GetPawn()->GetActorTransform();
	Save->CheckpointPlayerLevel = 5;
	Save->CheckpointPlayerExperience = 480;
	Save->ActivatedSealIds.Add(TEXT("LowerNorth"));
	Save->ClaimedTreasureIds.Add(TEXT("M21V2SeedChest"));
	const bool bPassed = AAuraPlayerState::IsValidLegacyGrowthSnapshot(Save->Growth)
		&& UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
	UE_LOG(LogAuraM21Smoke, Display,
		TEXT("AURA_M21_V2_SEED Passed=%s Slot=%s Level=5 Bag=%d Gear=%d Seals=%d"),
		bPassed ? TEXT("true") : TEXT("false"), *Slot,
		Save->Backpack.Num(), Save->Equipment.Num(), Save->ActivatedSealIds.Num());
}

void AAuraPlayerController::AuraTestFractureRankM21()
{
	AAuraCharacter* Caster = Cast<AAuraCharacter>(GetPawn());
	UClass* BossClass = LoadClass<AAuraEnemy>(nullptr,
		TEXT("/Game/Aura/Blueprints/Dungeon/BP_AuraBossDemon.BP_AuraBossDemon_C"));
	if (!Caster || !GetWorld() || !BossClass
		|| !PrepareComboSmokeRanks({EAuraComboMarkType::Fracture,
			EAuraComboMarkType::Fracture}))
	{
		UE_LOG(LogAuraM21Smoke, Error, TEXT("AURA_M21_FRACTURE Passed=false Setup=false"));
		return;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Origin = Caster->GetActorLocation() + FVector(600.f, 0.f, 2200.f);
	AAuraEnemy* Primary = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), Origin, FRotator::ZeroRotator, Spawn);
	AAuraEnemy* Secondary = GetWorld()->SpawnActor<AAuraEnemy>(
		AAuraEnemy::StaticClass(), Origin + FVector(0.f, 120.f, 0.f),
		FRotator::ZeroRotator, Spawn);
	AAuraEnemy* Boss = GetWorld()->SpawnActor<AAuraEnemy>(
		BossClass, Origin + FVector(0.f, 260.f, 0.f), FRotator::ZeroRotator, Spawn);
	if (!Primary || !Secondary || !Boss)
	{
		UE_LOG(LogAuraM21Smoke, Error, TEXT("AURA_M21_FRACTURE Passed=false Spawn=false"));
		if (Primary) Primary->Destroy();
		if (Secondary) Secondary->Destroy();
		if (Boss) Boss->Destroy();
		return;
	}
	for (AAuraEnemy* Enemy : {Primary, Secondary, Boss}) FreezeFractureTarget(Enemy);
	const float BaseBossDamage = Boss->GetDifficultyDamageMultiplier();
	const float WeakenedBossDamage = BaseBossDamage
		* (1.f - UAuraCombatBalanceData::Get().Combo.FractureBossDamageReduction);
	const float BaseEDamage = UAuraCombatBalanceData::Get().Shock.Damage;
	Caster->ExecuteFractureChain(Primary, BaseEDamage);
	const TSharedRef<FFractureRankSmokeState> State = MakeShared<FFractureRankSmokeState>();
	State->bInitial = Primary->IsStunned() && Secondary->IsStunned()
		&& !Boss->IsStunned() && Boss->IsBossEnemy()
		&& Secondary->GetAbilitySystemComponent()->GetNumericAttribute(
			UAuraAttributeSet::GetHealthAttribute()) < 1000.f
		&& FMath::IsNearlyEqual(Boss->GetDifficultyDamageMultiplier(),
			WeakenedBossDamage, 0.001f);
	const TWeakObjectPtr<AAuraCharacter> WeakCaster(Caster);
	const TWeakObjectPtr<AAuraEnemy> WeakPrimary(Primary);
	const TWeakObjectPtr<AAuraEnemy> WeakSecondary(Secondary);
	const TWeakObjectPtr<AAuraEnemy> WeakBoss(Boss);
	FTimerHandle FirstTimer;
	GetWorldTimerManager().SetTimer(FirstTimer,
		[WeakCaster, WeakPrimary, WeakSecondary, WeakBoss, State,
			BaseEDamage, BaseBossDamage, WeakenedBossDamage]()
	{
		if (!WeakCaster.IsValid() || !WeakPrimary.IsValid()
			|| !WeakSecondary.IsValid() || !WeakBoss.IsValid())
		{
			UE_LOG(LogAuraM21Smoke, Error, TEXT("AURA_M21_FRACTURE Passed=false ActorLost=true"));
			return;
		}
		const bool bRecovered = !WeakSecondary->IsStunned();
		WeakBoss->SetActorLocation(WeakPrimary->GetActorLocation()
			+ FVector(0.f, 260.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		WeakCaster->ExecuteFractureChain(WeakPrimary.Get(), BaseEDamage);
		State->bImmunity = bRecovered && !WeakSecondary->IsStunned()
			&& FMath::IsNearlyEqual(WeakBoss->GetDifficultyDamageMultiplier(),
				WeakenedBossDamage, 0.001f);
		FTimerHandle SecondTimer;
		WeakCaster->GetWorldTimerManager().SetTimer(SecondTimer,
			[WeakCaster, WeakPrimary, WeakSecondary, WeakBoss, State,
				BaseEDamage, BaseBossDamage, WeakenedBossDamage]()
		{
			if (!WeakCaster.IsValid() || !WeakPrimary.IsValid()
				|| !WeakSecondary.IsValid() || !WeakBoss.IsValid())
			{
				UE_LOG(LogAuraM21Smoke, Error,
					TEXT("AURA_M21_FRACTURE Passed=false ActorLost=true"));
				return;
			}
			const bool bExpired = FMath::IsNearlyEqual(
				WeakBoss->GetDifficultyDamageMultiplier(), BaseBossDamage, 0.001f);
			WeakBoss->SetActorLocation(WeakPrimary->GetActorLocation()
				+ FVector(0.f, 260.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			WeakCaster->ExecuteFractureChain(WeakPrimary.Get(), BaseEDamage);
			State->bCooldown = bExpired && FMath::IsNearlyEqual(
				WeakBoss->GetDifficultyDamageMultiplier(), BaseBossDamage, 0.001f);
			FTimerHandle ThirdTimer;
			WeakCaster->GetWorldTimerManager().SetTimer(ThirdTimer,
				[WeakCaster, WeakPrimary, WeakSecondary, WeakBoss, State,
					BaseEDamage, WeakenedBossDamage]()
			{
				const bool bActorsValid = WeakCaster.IsValid() && WeakPrimary.IsValid()
					&& WeakSecondary.IsValid() && WeakBoss.IsValid();
				if (bActorsValid)
				{
					WeakBoss->SetActorLocation(WeakPrimary->GetActorLocation()
						+ FVector(0.f, 260.f, 0.f), false, nullptr,
						ETeleportType::TeleportPhysics);
					WeakCaster->ExecuteFractureChain(WeakPrimary.Get(), BaseEDamage);
				}
				const bool bReadyAgain = bActorsValid && FMath::IsNearlyEqual(
					WeakBoss->GetDifficultyDamageMultiplier(), WeakenedBossDamage, 0.001f);
				UE_LOG(LogAuraM21Smoke, Display,
					TEXT("AURA_M21_FRACTURE Passed=%s Initial=%d Immunity=%d Cooldown=%d ReadyAgain=%d"),
					State->bInitial && State->bImmunity && State->bCooldown && bReadyAgain
						? TEXT("true") : TEXT("false"),
					State->bInitial, State->bImmunity, State->bCooldown, bReadyAgain);
				if (WeakPrimary.IsValid()) WeakPrimary->Destroy();
				if (WeakSecondary.IsValid()) WeakSecondary->Destroy();
				if (WeakBoss.IsValid()) WeakBoss->Destroy();
			}, 4.1f, false);
		}, 1.6f, false);
	}, 0.75f, false);
}
