// Copyright yosep

#include "Game/AuraRunSubsystem.h"

#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Engine/World.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Game/AuraGameState.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraRun, Log, All);

namespace
{
	bool IsKnownItemStat(const FGameplayTag& Tag)
	{
		const FString Name = Tag.ToString();
		return Name == TEXT("Item.Stat.MaxHealth") || Name == TEXT("Item.Stat.MaxMana")
			|| Name == TEXT("Item.Stat.Strength") || Name == TEXT("Item.Stat.Intelligence")
			|| Name == TEXT("Item.Stat.Armor") || Name == TEXT("Item.Stat.MoveSpeed");
	}
}

void UAuraRunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString RequestedSlot;
	const bool bExplicitSlot = FParse::Value(
		FCommandLine::Get(), TEXT("AuraSaveSlot="), RequestedSlot);
	if (bExplicitSlot)
	{
		bool bValidSlot = RequestedSlot.Len() > 0 && RequestedSlot.Len() <= 64;
		for (const TCHAR Character : RequestedSlot)
		{
			bValidSlot &= FChar::IsAlnum(Character)
				|| Character == TEXT('_') || Character == TEXT('-');
		}
		if (bValidSlot)
		{
			SaveSlotName = RequestedSlot;
		}
		else
		{
			bDiskEnabled = false;
			UE_LOG(LogAuraRun, Error, TEXT("Invalid AuraSaveSlot override; disk persistence disabled."));
		}
	}
	else if (GIsEditor)
	{
		SaveSlotName = TEXT("AuraProfile_Editor");
	}
	FString TestExitMarker;
	if (FParse::Param(FCommandLine::Get(), TEXT("server"))
		|| FParse::Param(FCommandLine::Get(), TEXT("AuraNoPersistentSave"))
		|| (!bExplicitSlot && FParse::Value(
			FCommandLine::Get(), TEXT("TestExit="), TestExitMarker)))
	{
		bDiskEnabled = false;
	}
	if (bDiskEnabled) LoadPersistentSave();
}

void UAuraRunSubsystem::Deinitialize()
{
	if (bSavePending) FlushPersistentSave();
	if (AAuraPlayerState* Player = ObservedPlayer.Get())
	{
		if (UAuraInventoryComponent* Inventory = Player->GetInventoryComponent())
			Inventory->OnInventoryChanged.RemoveAll(this);
		if (UAuraEquipmentComponent* Equipment = Player->GetEquipmentComponent())
			Equipment->OnEquipmentChanged.RemoveAll(this);
	}
	Super::Deinitialize();
}

FAuraSavedItem UAuraRunSubsystem::SaveItem(const FAuraItemInstance& Item)
{
	FAuraSavedItem Saved;
	Saved.InstanceId = Item.InstanceId;
	Saved.DefinitionId = Item.Definition ? Item.Definition->ItemId : NAME_None;
	Saved.Quantity = Item.Quantity;
	Saved.ItemLevel = Item.ItemLevel;
	Saved.EnhancementLevel = Item.EnhancementLevel;
	Saved.Rarity = Item.Rarity;
	Saved.RollSeed = Item.RollSeed;
	Saved.RolledModifiers = Item.RolledModifiers;
	Saved.SourceType = Item.SourceType;
	Saved.SourceId = Item.SourceId;
	return Saved;
}

FAuraItemInstance UAuraRunSubsystem::RestoreItem(const FAuraSavedItem& Item)
{
	FAuraItemInstance Restored;
	Restored.InstanceId = Item.InstanceId;
	Restored.Definition = UAuraItemCatalog::Get().FindItem(Item.DefinitionId);
	Restored.Quantity = Item.Quantity;
	Restored.ItemLevel = Item.ItemLevel;
	Restored.EnhancementLevel = Item.EnhancementLevel;
	Restored.Rarity = Item.Rarity;
	Restored.RollSeed = Item.RollSeed;
	Restored.RolledModifiers = Item.RolledModifiers;
	Restored.SourceType = Item.SourceType;
	Restored.SourceId = Item.SourceId;
	return Restored;
}

bool UAuraRunSubsystem::ValidateSave(
	const UAuraProfileSaveGame& Save, FString& OutError) const
{
	if (Save.SchemaVersion < 1 || Save.SchemaVersion > UAuraProfileSaveGame::CurrentVersion)
	{
		OutError = FString::Printf(TEXT("Unsupported schema version %d"), Save.SchemaVersion);
		return false;
	}
	if (Save.SchemaVersion >= 2 && Save.bAbyssTrialActive && !Save.bAbyssTrialUnlocked)
	{
		OutError = TEXT("Abyss trial active without first-clear unlock");
		return false;
	}
	if (!Save.bHasProfile && (!Save.Backpack.IsEmpty() || !Save.Equipment.IsEmpty()))
	{
		OutError = TEXT("Item records without a profile");
		return false;
	}
	if (Save.bHasProfile && !(Save.SchemaVersion < 3
		? AAuraPlayerState::IsValidLegacyGrowthSnapshot(Save.Growth)
		: (Save.SchemaVersion == 3
			? AAuraPlayerState::IsValidV3GrowthSnapshot(Save.Growth)
			: (Save.SchemaVersion == 4
				? AAuraPlayerState::IsValidV4GrowthSnapshot(Save.Growth)
				: (Save.SchemaVersion == 5 ? AAuraPlayerState::IsValidV5GrowthSnapshot(Save.Growth)
					: AAuraPlayerState::IsValidGrowthSnapshot(Save.Growth))))))
	{
		OutError = TEXT("Invalid growth/skill-point invariant");
		return false;
	}
	if (Save.Backpack.Num() > GetDefault<UAuraInventoryComponent>()->GetCapacity()
		|| Save.Equipment.Num() > 4)
	{
		OutError = TEXT("Item capacity exceeded");
		return false;
	}
	TSet<FGuid> InstanceIds;
	TSet<int32> BackpackSlots;
	TSet<EAuraEquipmentSlot> EquipmentSlots;
	auto ValidateItem = [&InstanceIds, &OutError, &Save](const FAuraSavedItem& Item,
		const bool bEquipped) -> const UAuraItemDefinition*
	{
		const UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(Item.DefinitionId);
		if (!Definition || !Item.InstanceId.IsValid()
			|| InstanceIds.Contains(Item.InstanceId)
			|| Item.Quantity < 1 || Item.Quantity > Definition->GetMaxStackSize()
			|| Item.ItemLevel < 1 || Item.ItemLevel > 100
			|| Item.EnhancementLevel < 0 || Item.EnhancementLevel > 3
			|| (Item.EnhancementLevel > 0
				&& (Save.SchemaVersion < 4
					? Definition->EquipmentSlot != EAuraEquipmentSlot::Weapon
					: Definition->ItemType != EAuraItemType::Equipment))
			|| static_cast<uint8>(Item.Rarity) > static_cast<uint8>(EAuraItemRarity::Legendary)
			|| static_cast<uint8>(Item.SourceType) > static_cast<uint8>(
				Save.SchemaVersion < 4 ? EAuraItemSourceType::Merchant
					: EAuraItemSourceType::Crafted)
			|| Item.RolledModifiers.Num() > 5
			|| (Definition->ItemType != EAuraItemType::Equipment
				&& !Item.RolledModifiers.IsEmpty())
			|| (bEquipped && (Definition->ItemType != EAuraItemType::Equipment
				|| Item.Quantity != 1)))
		{
			OutError = FString::Printf(TEXT("Invalid or duplicate item: %s"),
				*Item.DefinitionId.ToString());
			return nullptr;
		}
		for (const FAuraItemStatModifier& Modifier : Item.RolledModifiers)
		{
			if (!IsKnownItemStat(Modifier.StatTag) || !FMath::IsFinite(Modifier.Magnitude)
				|| Modifier.Magnitude < 0.f || Modifier.Magnitude > 1000.f)
			{
				OutError = TEXT("Invalid rolled modifier");
				return nullptr;
			}
		}
		InstanceIds.Add(Item.InstanceId);
		return Definition;
	};
	for (const FAuraSavedBackpackEntry& Entry : Save.Backpack)
	{
		if (Entry.SlotIndex < 0
			|| Entry.SlotIndex >= GetDefault<UAuraInventoryComponent>()->GetCapacity()
			|| BackpackSlots.Contains(Entry.SlotIndex)
			|| !ValidateItem(Entry.Item, false))
		{
			if (OutError.IsEmpty()) OutError = TEXT("Invalid backpack slot");
			return false;
		}
		BackpackSlots.Add(Entry.SlotIndex);
	}
	for (const FAuraSavedEquipmentEntry& Entry : Save.Equipment)
	{
		const UAuraItemDefinition* Definition = ValidateItem(Entry.Item, true);
		if (!Definition || Entry.Slot == EAuraEquipmentSlot::None
			|| Entry.Slot != Definition->EquipmentSlot || EquipmentSlots.Contains(Entry.Slot))
		{
			if (OutError.IsEmpty()) OutError = TEXT("Invalid equipment slot");
			return false;
		}
		EquipmentSlots.Add(Entry.Slot);
	}
	if (Save.bHasCheckpoint
		&& (Save.CheckpointLevelName.IsNone() || Save.ActiveCheckpointId.IsNone()
			|| Save.RespawnTransform.GetLocation().ContainsNaN()
			|| Save.RespawnTransform.GetRotation().ContainsNaN()
			|| Save.CheckpointPlayerLevel < 1 || Save.CheckpointPlayerExperience < 0))
	{
		OutError = TEXT("Invalid checkpoint");
		return false;
	}
	const int32 MaxRunRank = UAuraCombatBalanceData::Get().RunUpgrades.MaxRankPerUpgrade;
	for (const EAuraRunUpgradeType Type : AuraRunUpgrade::GetAllTypes())
	{
		if (Save.RunUpgrades.GetRank(Type) < 0
			|| Save.RunUpgrades.GetRank(Type) > MaxRunRank)
		{
			OutError = TEXT("Invalid run upgrade rank");
			return false;
		}
	}
	for (const FName Seal : Save.ActivatedSealIds)
	{
		if (!AAuraGameState::RequiredSealIds().Contains(Seal))
		{
			OutError = TEXT("Unknown seal ID");
			return false;
		}
	}
	return true;
}

void UAuraRunSubsystem::LoadPersistentSave()
{
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0)) return;
	const UAuraProfileSaveGame* Save = Cast<UAuraProfileSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	FString Error;
	if (!Save || !ValidateSave(*Save, Error))
	{
		bDiskWriteBlocked = true;
		UE_LOG(LogAuraRun, Error,
			TEXT("M15 save rejected; original slot preserved. Slot=%s Reason=%s"),
			*SaveSlotName, Save ? *Error : TEXT("Could not deserialize SaveGame"));
		return;
	}
	const FAuraGrowthState CandidateGrowth = Save->bHasProfile
		? (Save->SchemaVersion < 3
			? AAuraPlayerState::MigrateLegacyGrowthSnapshot(Save->Growth)
			: (Save->SchemaVersion == 3
				? AAuraPlayerState::MigrateV3GrowthSnapshot(Save->Growth)
				: (Save->SchemaVersion == 4
					? AAuraPlayerState::MigrateV4GrowthSnapshot(Save->Growth)
					: (Save->SchemaVersion == 5 ? AAuraPlayerState::MigrateV5GrowthSnapshot(Save->Growth) : Save->Growth))))
		: Save->Growth;
	if (Save->bHasProfile && !AAuraPlayerState::IsValidGrowthSnapshot(CandidateGrowth))
	{
		bDiskWriteBlocked = true;
		UE_LOG(LogAuraRun, Error,
			TEXT("M21 legacy growth migration rejected; original slot preserved. Slot=%s"),
			*SaveSlotName);
		return;
	}
	bHasGrowthSnapshot = Save->bHasProfile;
	SavedGrowth = CandidateGrowth;
	SavedBackpack = Save->Backpack;
	SavedEquipment = Save->Equipment;
	bHasItemSnapshot = Save->bHasProfile;
	bHasCheckpoint = Save->bHasCheckpoint;
	CheckpointLevelName = Save->CheckpointLevelName;
	ActiveCheckpointId = Save->ActiveCheckpointId;
	SavedRespawnTransform = Save->RespawnTransform;
	SavedPlayerLevel = Save->CheckpointPlayerLevel;
	SavedPlayerExperience = Save->CheckpointPlayerExperience;
	SavedRunUpgrades = Save->RunUpgrades;
	for (const FName Id : Save->ClaimedTreasureIds) ClaimedTreasureIds.Add(Id);
	for (const FName Id : Save->ClearedRewardEncounterIds) ClearedRewardEncounterIds.Add(Id);
	for (const FName Id : Save->ActivatedSealIds) ActivatedSealIds.Add(Id);
	bRunCompleted = Save->bRunCompleted;
	bAtPostBossCamp = Save->bAtPostBossCamp;
	bAbyssTrialUnlocked = Save->bRunCompleted
		|| (Save->SchemaVersion >= 2 && Save->bAbyssTrialUnlocked);
	bAbyssTrialActive = Save->SchemaVersion >= 2 && Save->bAbyssTrialActive;
	// Older snapshots are migrated in memory, then written as v5 on the next state change.
	UE_LOG(LogAuraRun, Display,
		TEXT("M15 save loaded. Slot=%s Version=%d Level=%d Backpack=%d Equipment=%d Checkpoint=%s Seals=%d"),
		*SaveSlotName, Save->SchemaVersion, SavedGrowth.PaidLevel,
		SavedBackpack.Num(), SavedEquipment.Num(),
		*ActiveCheckpointId.ToString(), ActivatedSealIds.Num());
}

void UAuraRunSubsystem::RestoreStandalonePlayer(AAuraPlayerState* Player)
{
	if (!Player || !Player->HasAuthority() || !Player->GetWorld()
		|| Player->GetWorld()->GetNetMode() != NM_Standalone || !bHasItemSnapshot) return;
	TArray<FAuraInventoryEntry> BackpackEntries;
	for (const FAuraSavedBackpackEntry& Saved : SavedBackpack)
	{
		FAuraInventoryEntry& Entry = BackpackEntries.AddDefaulted_GetRef();
		Entry.SlotIndex = Saved.SlotIndex;
		Entry.Item = RestoreItem(Saved.Item);
	}
	TArray<FAuraEquippedItem> EquipmentEntries;
	for (const FAuraSavedEquipmentEntry& Saved : SavedEquipment)
	{
		FAuraEquippedItem& Entry = EquipmentEntries.AddDefaulted_GetRef();
		Entry.Slot = Saved.Slot;
		Entry.Item = RestoreItem(Saved.Item);
	}
	Player->GetEquipmentComponent()->RestoreSnapshot(EquipmentEntries);
	Player->GetInventoryComponent()->RestoreSnapshot(BackpackEntries);
	UE_LOG(LogAuraRun, Display,
		TEXT("M15 player items restored. Backpack=%d Equipment=%d"),
		BackpackEntries.Num(), EquipmentEntries.Num());
}

void UAuraRunSubsystem::ObserveStandalonePlayer(AAuraPlayerState* Player)
{
	if (!Player || !Player->HasAuthority() || !Player->GetWorld()
		|| Player->GetWorld()->GetNetMode() != NM_Standalone) return;
	if (ObservedPlayer.Get() != Player)
	{
		if (AAuraPlayerState* Old = ObservedPlayer.Get())
		{
			Old->GetInventoryComponent()->OnInventoryChanged.RemoveAll(this);
			Old->GetEquipmentComponent()->OnEquipmentChanged.RemoveAll(this);
		}
		ObservedPlayer = Player;
		Player->GetInventoryComponent()->OnInventoryChanged.AddUObject(
			this, &UAuraRunSubsystem::HandlePlayerItemsChanged);
		Player->GetEquipmentComponent()->OnEquipmentChanged.AddUObject(
			this, &UAuraRunSubsystem::HandlePlayerItemsChanged);
	}
	CaptureObservedPlayer();
	if (bSavePending) RequestPersistentSave();
}

void UAuraRunSubsystem::CaptureObservedPlayer()
{
	AAuraPlayerState* Player = ObservedPlayer.Get();
	if (!Player || !Player->HasAuthority()) return;
	SavedGrowth = Player->GetGrowthState();
	bHasGrowthSnapshot = true;
	SavedBackpack.Reset();
	for (const FAuraInventoryEntry& Entry : Player->GetInventoryComponent()->GetEntries())
	{
		FAuraSavedBackpackEntry& Saved = SavedBackpack.AddDefaulted_GetRef();
		Saved.SlotIndex = Entry.SlotIndex;
		Saved.Item = SaveItem(Entry.Item);
	}
	SavedEquipment.Reset();
	for (const FAuraEquippedItem& Entry : Player->GetEquipmentComponent()->GetEquippedItems())
	{
		FAuraSavedEquipmentEntry& Saved = SavedEquipment.AddDefaulted_GetRef();
		Saved.Slot = Entry.Slot;
		Saved.Item = SaveItem(Entry.Item);
	}
	bHasItemSnapshot = true;
}

void UAuraRunSubsystem::HandlePlayerItemsChanged()
{
	CaptureObservedPlayer();
	RequestPersistentSave();
}

void UAuraRunSubsystem::RequestPersistentSave()
{
	if (!bDiskEnabled || bDiskWriteBlocked) return;
	bSavePending = true;
	AAuraPlayerState* Player = ObservedPlayer.Get();
	if (!Player || !Player->GetWorld()
		|| Player->GetWorld()->GetNetMode() != NM_Standalone) return;
	FTimerManager& Timers = Player->GetWorld()->GetTimerManager();
	if (!Timers.IsTimerActive(SaveTimer))
	{
		Timers.SetTimer(SaveTimer, this,
			&UAuraRunSubsystem::FlushPendingSave, 0.35f, false);
	}
}

void UAuraRunSubsystem::FlushPendingSave()
{
	FlushPersistentSave();
}

bool UAuraRunSubsystem::FlushPersistentSave()
{
	if (!bDiskEnabled || bDiskWriteBlocked) return false;
	AAuraPlayerState* Player = ObservedPlayer.Get();
	if (!Player || !Player->GetWorld()
		|| Player->GetWorld()->GetNetMode() != NM_Standalone) return false;
	CaptureObservedPlayer();
	UAuraProfileSaveGame* Save = Cast<UAuraProfileSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAuraProfileSaveGame::StaticClass()));
	if (!Save) return false;
	Save->bHasProfile = bHasGrowthSnapshot;
	Save->Growth = SavedGrowth;
	Save->Backpack = SavedBackpack;
	Save->Equipment = SavedEquipment;
	Save->bHasCheckpoint = bHasCheckpoint;
	Save->CheckpointLevelName = CheckpointLevelName;
	Save->ActiveCheckpointId = ActiveCheckpointId;
	Save->RespawnTransform = SavedRespawnTransform;
	Save->CheckpointPlayerLevel = SavedPlayerLevel;
	Save->CheckpointPlayerExperience = SavedPlayerExperience;
	Save->RunUpgrades = SavedRunUpgrades;
	Save->ClaimedTreasureIds = ClaimedTreasureIds.Array();
	Save->ClearedRewardEncounterIds = ClearedRewardEncounterIds.Array();
	Save->ActivatedSealIds = ActivatedSealIds.Array();
	Save->bRunCompleted = bRunCompleted;
	Save->bAtPostBossCamp = bAtPostBossCamp;
	Save->bAbyssTrialUnlocked = bAbyssTrialUnlocked;
	Save->bAbyssTrialActive = bAbyssTrialActive;
	FString Error;
	if (!ValidateSave(*Save, Error))
	{
		UE_LOG(LogAuraRun, Error, TEXT("M15 live snapshot rejected. Reason=%s"), *Error);
		return false;
	}
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0);
	if (bSaved) bSavePending = false;
	if (bSaved)
	{
		UE_LOG(LogAuraRun, Display,
			TEXT("M15 save written. Slot=%s Level=%d Backpack=%d Equipment=%d Seals=%d"),
			*SaveSlotName, SavedGrowth.PaidLevel, SavedBackpack.Num(),
			SavedEquipment.Num(), ActivatedSealIds.Num());
	}
	else UE_LOG(LogAuraRun, Error, TEXT("M15 save write failed. Slot=%s"), *SaveSlotName);
	return bSaved;
}

FName UAuraRunSubsystem::GetStableLevelName(const UWorld* World)
{
	return World ? FName(*UWorld::RemovePIEPrefix(World->GetMapName())) : NAME_None;
}

void UAuraRunSubsystem::SaveCheckpoint(
	const UWorld* World, const FName CheckpointId, const FTransform& RespawnTransform,
	const int32 PlayerLevel, const int32 PlayerExperience,
	const FAuraRunUpgradeState& RunUpgrades)
{
	if (!World || CheckpointId.IsNone()) return;

	CheckpointLevelName = GetStableLevelName(World);
	ActiveCheckpointId = CheckpointId;
	SavedRespawnTransform = RespawnTransform;
	SavedPlayerLevel = FMath::Max(1, PlayerLevel);
	SavedPlayerExperience = FMath::Max(0, PlayerExperience);
	SavedRunUpgrades = RunUpgrades;
	bHasCheckpoint = true;
	bRunCompleted = false;
	RequestPersistentSave();

	UE_LOG(LogAuraRun, Display,
		TEXT("Checkpoint saved. Map=%s Id=%s Location=%s PlayerLevel=%d XP=%d Boons=%d"),
		*CheckpointLevelName.ToString(), *ActiveCheckpointId.ToString(),
		*SavedRespawnTransform.GetLocation().ToCompactString(),
		SavedPlayerLevel, SavedPlayerExperience, SavedRunUpgrades.GetTotalRanks());
}

void UAuraRunSubsystem::UpdateSavedRunUpgrades(
	const UWorld* World, const FAuraRunUpgradeState& RunUpgrades)
{
	if (!bHasCheckpoint || bRunCompleted || !World
		|| GetStableLevelName(World) != CheckpointLevelName)
	{
		return;
	}

	SavedRunUpgrades = RunUpgrades;
	RequestPersistentSave();
	UE_LOG(LogAuraRun, Display,
		TEXT("Checkpoint run upgrades updated. Id=%s TotalRanks=%d"),
		*ActiveCheckpointId.ToString(), SavedRunUpgrades.GetTotalRanks());
}

void UAuraRunSubsystem::UpdateSavedGrowth(const FAuraGrowthState& Growth)
{
	SavedGrowth = Growth;
	bHasGrowthSnapshot = true;
	RequestPersistentSave();
	UE_LOG(LogAuraRun, Display,
		TEXT("M12 growth snapshot updated. Level=%d XP=%d Souls=%d Points=%d"),
		Growth.PaidLevel, Growth.TotalExperience, Growth.Souls,
		Growth.UnspentSkillPoints);
}

bool UAuraRunSubsystem::TryGetSavedGrowth(FAuraGrowthState& OutGrowth) const
{
	if (!bHasGrowthSnapshot) return false;
	OutGrowth = SavedGrowth;
	return true;
}

void UAuraRunSubsystem::UpdateSavedProgression(
	const UWorld* World, const int32 PlayerLevel, const int32 PlayerExperience)
{
	if (!bHasCheckpoint || bRunCompleted || !World
		|| GetStableLevelName(World) != CheckpointLevelName)
	{
		return;
	}

	SavedPlayerLevel = FMath::Max(1, PlayerLevel);
	SavedPlayerExperience = FMath::Max(0, PlayerExperience);
	RequestPersistentSave();
	UE_LOG(LogAuraRun, Display,
		TEXT("Checkpoint progression updated. Id=%s PlayerLevel=%d XP=%d"),
		*ActiveCheckpointId.ToString(), SavedPlayerLevel, SavedPlayerExperience);
}

bool UAuraRunSubsystem::TryGetCheckpointForWorld(
	const UWorld* World, FTransform& OutRespawnTransform,
	int32* OutPlayerLevel, int32* OutPlayerExperience,
	FAuraRunUpgradeState* OutRunUpgrades) const
{
	if (!bHasCheckpoint || bRunCompleted || GetStableLevelName(World) != CheckpointLevelName)
	{
		return false;
	}
	OutRespawnTransform = SavedRespawnTransform;
	if (OutPlayerLevel) *OutPlayerLevel = SavedPlayerLevel;
	if (OutPlayerExperience) *OutPlayerExperience = SavedPlayerExperience;
	if (OutRunUpgrades) *OutRunUpgrades = SavedRunUpgrades;
	return true;
}

void UAuraRunSubsystem::MarkTreasureClaimed(const FName TreasureId)
{
	if (!TreasureId.IsNone() && !ClaimedTreasureIds.Contains(TreasureId))
	{
		ClaimedTreasureIds.Add(TreasureId);
		RequestPersistentSave();
	}
}

bool UAuraRunSubsystem::IsTreasureClaimed(const FName TreasureId) const
{
	return !TreasureId.IsNone() && ClaimedTreasureIds.Contains(TreasureId);
}

void UAuraRunSubsystem::MarkRewardEncounterCleared(const FName EncounterId)
{
	if (EncounterId.IsNone()) return;
	ClearedRewardEncounterIds.Add(EncounterId);
	RequestPersistentSave();
	UE_LOG(LogAuraRun, Display, TEXT("Reward encounter cleared. Id=%s"),
		*EncounterId.ToString());
}

bool UAuraRunSubsystem::IsRewardEncounterCleared(const FName EncounterId) const
{
	return !EncounterId.IsNone() && ClearedRewardEncounterIds.Contains(EncounterId);
}

void UAuraRunSubsystem::MarkSealActivated(const FName SealId)
{
	if (!SealId.IsNone() && !ActivatedSealIds.Contains(SealId))
	{
		ActivatedSealIds.Add(SealId);
		RequestPersistentSave();
	}
}

bool UAuraRunSubsystem::HasActivatedSeal(const FName SealId) const
{
	return !SealId.IsNone() && ActivatedSealIds.Contains(SealId);
}

void UAuraRunSubsystem::MarkRunCompleted()
{
	bRunCompleted = true;
	bAbyssTrialUnlocked = true;
	RequestPersistentSave();
	UE_LOG(LogAuraRun, Display, TEXT("Dungeon run completed. Checkpoint=%s"),
		*ActiveCheckpointId.ToString());
}

bool UAuraRunSubsystem::StartAbyssTrial()
{
	if (!bAbyssTrialUnlocked || !bAtPostBossCamp) return false;
	ResetRun();
	bAbyssTrialActive = true;
	RequestPersistentSave();
	// The map travel can discard the old world's coalescing timer. Persist the
	// newly selected mode before travel when a standalone disk slot is active.
	if (bDiskEnabled && !bDiskWriteBlocked) FlushPersistentSave();
	return true;
}

void UAuraRunSubsystem::RequestPostBossCamp()
{
	bReturnToPostBossCamp = true;
	bAtPostBossCamp = false;
	RequestPersistentSave();
}

bool UAuraRunSubsystem::ConsumePostBossCampRequest()
{
	if (!bReturnToPostBossCamp) return false;
	bReturnToPostBossCamp = false;
	bAtPostBossCamp = true;
	RequestPersistentSave();
	return true;
}

void UAuraRunSubsystem::ResetRun(const bool bPreserveDifficulty)
{
	if (!bPreserveDifficulty) bAbyssTrialActive = false;
	bReturnToPostBossCamp = false;
	bAtPostBossCamp = false;
	CheckpointLevelName = NAME_None;
	ActiveCheckpointId = NAME_None;
	SavedRespawnTransform = FTransform::Identity;
	SavedPlayerLevel = 1;
	SavedPlayerExperience = 0;
	SavedRunUpgrades = FAuraRunUpgradeState();
	ClaimedTreasureIds.Reset();
	ClearedRewardEncounterIds.Reset();
	ActivatedSealIds.Reset();
	bHasCheckpoint = false;
	bRunCompleted = false;
	RequestPersistentSave();
	UE_LOG(LogAuraRun, Display, TEXT("Dungeon run state reset."));
}
