// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Game/AuraProfileSaveGame.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Level/AuraCampVendor.h"
#include "Level/AuraTreasureChest.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AuraPlayerState.h"
#include "UI/AuraServiceMenuWidget.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraM23Smoke, Log, All);

void AAuraPlayerController::AuraTestEconomyM23()
{
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
	UAuraEquipmentComponent* Gear = State ? State->GetEquipmentComponent() : nullptr;
	UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	if (!HasAuthority() || !IsLocalController() || !State || !Bag || !Gear || !Run
		|| Run->IsPersistentSaveEnabled())
	{
		UE_LOG(LogAuraM23Smoke, Error, TEXT("AURA_M23_ECONOMY Passed=false Setup=false"));
		return;
	}
	FString Failure;
	bool bGrowth = true;
	bool bShop = true;
	bool bCraft = true;
	bool bSmelt = true;
	bool bEnhance = true;
	bool bUi = true;
	bool bChest = true;
	FAuraGrowthState V3;
	V3.PaidLevel = 5;
	V3.TotalExperience = 480;
	V3.SkillRanks.FireBolt = 3;
	V3.SkillRanks.ArcaneShards = 1;
	V3.SkillRanks.Shock = 1;
	V3.UnspentComboPoints = 1;
	bGrowth &= AAuraPlayerState::IsValidV3GrowthSnapshot(V3)
		&& AAuraPlayerState::IsValidGrowthSnapshot(
			AAuraPlayerState::MigrateV3GrowthSnapshot(V3));
	V3.PaidLevel = 10;
	V3.TotalExperience = 1730;
	V3.UnspentComboPoints = 3;
	V3.UnspentSkillPoints = 3;
	bGrowth &= AAuraPlayerState::IsValidV3GrowthSnapshot(V3)
		&& AAuraPlayerState::MigrateV3GrowthSnapshot(V3).UnspentSkillPoints == 5;
	const FAuraCampEconomyBalance& Economy = UAuraCombatBalanceData::Get().CampEconomy;
	FName ItemId;
	EAuraItemRarity Rarity;
	int32 Price = 0;
	int32 Limit = 0;
	bShop &= !Economy.GetOffer(9, ItemId, Rarity, Price, Limit, 1);
	bShop &= !Economy.GetOffer(12, ItemId, Rarity, Price, Limit, 4);
	bShop &= Economy.GetOffer(13, ItemId, Rarity, Price, Limit, 1)
		&& ItemId == FName(TEXT("Material.SmithMetal")) && Price == 12 && Limit == 12;
	State->AwardExperienceAndSouls(1730, 10000);
	bGrowth &= State->GetGrowthState().PaidLevel == 10
		&& State->GetGrowthState().GetSpentSkillPoints()
			+ State->GetGrowthState().UnspentSkillPoints == 9
		&& State->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards, Failure)
		&& State->GetGrowthState().SkillRanks.Shock == 0
		&& !State->TryAllocateCombo(EAuraComboMarkType::Fracture, Failure)
		&& State->TryAllocateCombo(EAuraComboMarkType::Ember, Failure)
		&& State->TryAllocateSkill(EAuraPlayerSkill::Shock, Failure)
		&& State->GetGrowthState().SkillRanks.Shock == 1
		&& AAuraPlayerState::IsValidGrowthSnapshot(State->GetGrowthState());
	bShop &= Economy.GetOffer(9, ItemId, Rarity, Price, Limit, 4)
		&& Rarity == EAuraItemRarity::Fine && Price == 130
		&& Economy.GetOffer(10, ItemId, Rarity, Price, Limit, 4)
		&& !Economy.GetOffer(11, ItemId, Rarity, Price, Limit, 4);
	bShop &= Economy.GetOffer(11, ItemId, Rarity, Price, Limit, 7)
		&& Economy.GetOffer(12, ItemId, Rarity, Price, Limit, 7)
		&& Rarity == EAuraItemRarity::Fine;
	for (int32 Index = 0; Index < Economy.SmithMetalPurchaseLimit; ++Index)
		bShop &= State->TryPurchaseMerchantOffer(13, Failure);
	const int32 BeforeSoldOutSouls = State->GetGrowthState().Souls;
	bShop &= Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == 12
		&& State->GetGrowthState().SmithMetalPurchased == 12
		&& !State->TryPurchaseMerchantOffer(13, Failure)
		&& State->GetGrowthState().Souls == BeforeSoldOutSouls;
	bShop &= State->TryPurchaseMerchantOffer(9, Failure)
		&& State->TryPurchaseMerchantOffer(10, Failure)
		&& State->TryPurchaseMerchantOffer(11, Failure)
		&& State->TryPurchaseMerchantOffer(12, Failure);
	const FName EquipmentIds[4] = { TEXT("Equipment.ArcaneStaff"),
		TEXT("Equipment.WardenArmor"), TEXT("Equipment.SeerHeadpiece"),
		TEXT("Equipment.WindShoes") };
	FGuid BlueIds[4];
	for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
	{
		if (Entry.Item.Rarity != EAuraItemRarity::Fine
			|| Entry.Item.SourceType != EAuraItemSourceType::Merchant
			|| !Entry.Item.Definition) continue;
		for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
			if (Entry.Item.Definition->ItemId == EquipmentIds[SlotIndex])
				BlueIds[SlotIndex] = Entry.Item.InstanceId;
	}
	for (const FGuid& Id : BlueIds) bShop &= Id.IsValid();
	const int32 BeforeCraftSouls = State->GetGrowthState().Souls;
	bCraft &= State->TryCraftEquipment(EAuraEquipmentSlot::Weapon, Failure)
		&& State->GetGrowthState().Souls == BeforeCraftSouls - 20
		&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == 6;
	FGuid CraftedId;
	EAuraItemRarity CraftedRarity = EAuraItemRarity::Common;
	for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
	{
		if (Entry.Item.SourceType != EAuraItemSourceType::Crafted
			|| !Entry.Item.Definition
			|| Entry.Item.Definition->ItemType != EAuraItemType::Equipment) continue;
		CraftedId = Entry.Item.InstanceId;
		CraftedRarity = Entry.Item.Rarity;
		bCraft &= Entry.Item.ItemLevel == 7
			&& (CraftedRarity == EAuraItemRarity::Common || Entry.Item.RollSeed != 0)
			&& Entry.Item.RolledModifiers.Num() == AuraItems::GetRarityAffixCount(CraftedRarity);
	}
	bCraft &= CraftedId.IsValid() && State->GetGrowthState().CraftPityMisses
		== (CraftedRarity >= EAuraItemRarity::Fine ? 0 : 1);
	const int32 BeforeSmeltMetal = Bag->GetTotalQuantity(TEXT("Material.SmithMetal"));
	bSmelt &= State->TrySmeltEquipment(CraftedId, Failure)
		&& !Bag->FindEntryByInstanceId(CraftedId)
		&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal"))
			== BeforeSmeltMetal + FMath::Clamp(static_cast<int32>(CraftedRarity) + 1, 1, 5)
		&& !State->TrySmeltEquipment(CraftedId, Failure);
	FGuid MetalGuid;
	for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
		if (Entry.Item.Definition && Entry.Item.Definition->ItemId
			== FName(TEXT("Material.SmithMetal")))
		{
			MetalGuid = Entry.Item.InstanceId;
			break;
		}
	const int32 BeforeMetalSale = Bag->GetTotalQuantity(TEXT("Material.SmithMetal"));
	const int32 BeforeMetalSaleSouls = State->GetGrowthState().Souls;
	bShop &= MetalGuid.IsValid() && State->TrySellMerchantItem(MetalGuid, Failure)
		&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == BeforeMetalSale - 1
		&& State->GetGrowthState().Souls == BeforeMetalSaleSouls + 3;
	const FName ChestId(TEXT("DungeonTreasure_LowerNorthDeadEnd"));
	const int32 BeforeChestMetal = Bag->GetTotalQuantity(TEXT("Material.SmithMetal"));
	bChest &= AAuraPlayerState::GetChestMetalReward(ChestId) == 2
		&& AAuraPlayerState::GetChestMetalReward(TEXT("DungeonTreasure_UpperSouthDeadEnd")) == 2
		&& AAuraPlayerState::GetChestMetalReward(TEXT("LowerSouthAmbushLoot")) == 2
		&& AAuraPlayerState::GetChestMetalReward(TEXT("UpperNorthRiskLoot")) == 2
		&& AAuraPlayerState::GetChestMetalReward(TEXT("UnrelatedChest")) == 0
		&& State->CanAcceptChestMetal(2) && State->GrantChestMetal(2, ChestId)
		&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == BeforeChestMetal + 2;
	State->MarkTreasureClaimed(ChestId);
	bChest &= !State->GrantChestMetal(2, ChestId);
	FAuraItemInstance Ore;
	Ore.Definition = UAuraItemCatalog::Get().FindItem(TEXT("Material.ArcaneOreShard"));
	Ore.Quantity = 12;
	Ore.EnsureIdentity();
	bEnhance &= Ore.IsValid() && Bag->TryAddItem(Ore, Failure, false) == 12;
	for (const FGuid& Id : BlueIds)
	{
		const FAuraInventoryEntry* Entry = Bag->FindEntryByInstanceId(Id);
		bEnhance &= Entry != nullptr;
		if (!Entry) continue;
		const TArray<FAuraItemStatModifier> OldStats = AuraItems::BuildFinalStatModifiers(Entry->Item);
		bEnhance &= State->TryForgeWeapon(Id, Failure);
		Entry = Bag->FindEntryByInstanceId(Id);
		bEnhance &= Entry && Entry->Item.EnhancementLevel == 1;
		if (Entry && !OldStats.IsEmpty())
		{
			const TArray<FAuraItemStatModifier> NewStats = AuraItems::BuildFinalStatModifiers(Entry->Item);
			bEnhance &= AuraItems::GetStatMagnitude(NewStats, OldStats[0].StatTag)
				> OldStats[0].Magnitude;
		}
	}
	bEnhance &= Gear->TryEquipItem(BlueIds[1], Failure)
		&& State->TryForgeWeapon(BlueIds[1], Failure)
		&& Gear->GetEquippedItem(EAuraEquipmentSlot::Armor)
		&& Gear->GetEquippedItem(EAuraEquipmentSlot::Armor)->EnhancementLevel == 2;
	bSmelt &= !State->TrySmeltEquipment(BlueIds[1], Failure);
	bCraft &= !State->TryCraftEquipment(EAuraEquipmentSlot::None, Failure);
	AAuraCampVendor* Smith = nullptr;
	for (TActorIterator<AAuraCampVendor> It(GetWorld()); It; ++It)
		if (It->GetServiceType() == EAuraCampService::Blacksmith) { Smith = *It; break; }
	if (Smith)
	{
		ClientOpenCampVendor_Implementation(Smith);
		bUi &= ActiveServiceMenuWidget != nullptr && SmithMode == 0;
		bUi &= ActiveServiceMenuWidget && ActiveServiceMenuWidget->ClickActionForSmoke(12);
		bUi &= SmithMode == 1 && BuildCampServiceRows().Num() == 4;
		bUi &= ActiveServiceMenuWidget && ActiveServiceMenuWidget->ClickActionForSmoke(1)
			&& bCampConfirming && CampPendingRow.OfferId
				== static_cast<int32>(EAuraEquipmentSlot::Weapon);
		bUi &= ActiveServiceMenuWidget && ActiveServiceMenuWidget->ClickActionForSmoke(16);
		bUi &= SmithMode == 2 && !bCampConfirming && !BuildCampServiceRows().IsEmpty();
		CloseCampMenu();
	}
	else bUi = false;
	AAuraTreasureChest* RewardChest = nullptr;
	for (TActorIterator<AAuraTreasureChest> It(GetWorld()); It; ++It)
		if (It->GetTreasureId() == FName(TEXT("DungeonTreasure_UpperSouthDeadEnd")))
		{
			RewardChest = *It;
			break;
		}
	AAuraCharacter* TestCharacter = Cast<AAuraCharacter>(GetPawn());
	if (RewardChest && TestCharacter)
	{
		RewardChest->SetGuardLocked(false, 0);
		TestCharacter->TeleportTo(RewardChest->GetActorLocation()
			+ RewardChest->GetActorForwardVector() * 130.f + FVector(0.f, 0.f, 96.f),
			FRotator::ZeroRotator, false, true);
		const int32 BeforeOpenMetal = Bag->GetTotalQuantity(TEXT("Material.SmithMetal"));
		bChest &= RewardChest->ClaimForCharacter(TestCharacter)
			&& State->HasClaimedTreasure(RewardChest->GetTreasureId())
			&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == BeforeOpenMetal + 2
			&& !RewardChest->ClaimForCharacter(TestCharacter);
	}
	else bChest = false;
	UE_LOG(LogAuraM23Smoke, Display,
		TEXT("AURA_M23_ECONOMY Passed=%s Growth=%d Shop=%d Craft=%d Smelt=%d Enhance=%d UI=%d Chest=%d Level=%d Metal=%d Pity=%d Failure=%s"),
		bGrowth && bShop && bCraft && bSmelt && bEnhance && bUi && bChest
			? TEXT("true") : TEXT("false"),
		bGrowth, bShop, bCraft, bSmelt, bEnhance, bUi, bChest,
		State->GetGrowthState().PaidLevel,
		Bag->GetTotalQuantity(TEXT("Material.SmithMetal")),
		State->GetGrowthState().CraftPityMisses, *Failure);
}

void AAuraPlayerController::AuraTestEconomyCoopM23()
{
	if (!HasAuthority() || !GetWorld()) return;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	AAuraCampVendor* Smith = nullptr;
	for (TActorIterator<AAuraCampVendor> It(GetWorld()); It; ++It)
		if (It->GetServiceType() == EAuraCampService::Blacksmith) { Smith = *It; break; }
	bool bPassed = GameState && GameState->PlayerArray.Num() == 2 && Smith;
	TSet<FGuid> CraftedIds;
	if (bPassed)
	{
		for (APlayerState* PlayerEntry : GameState->PlayerArray)
		{
			AAuraPlayerState* State = Cast<AAuraPlayerState>(PlayerEntry);
			AAuraPlayerController* TestController = State
				? Cast<AAuraPlayerController>(State->GetPlayerController()) : nullptr;
			AAuraCharacter* TestPawn = State ? Cast<AAuraCharacter>(State->GetPawn()) : nullptr;
			UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
			if (!State || !TestController || !TestPawn || !Bag)
			{
				bPassed = false;
				continue;
			}
			FString Failure;
			State->AwardExperienceAndSouls(1730, 10000);
			bPassed &= State->GetGrowthState().PaidLevel == 10;
			for (int32 Index = 0; Index < 12; ++Index)
				bPassed &= State->TryPurchaseMerchantOffer(13, Failure);
			bPassed &= !State->TryPurchaseMerchantOffer(13, Failure);
			bPassed &= State->TryPurchaseMerchantOffer(9, Failure)
				&& State->TryPurchaseMerchantOffer(4, Failure);
			FGuid CommonArmorId;
			for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
				if (Entry.Item.Definition && Entry.Item.Definition->ItemId
					== FName(TEXT("Equipment.WardenArmor"))
					&& Entry.Item.Rarity == EAuraItemRarity::Common)
					CommonArmorId = Entry.Item.InstanceId;
			bPassed &= CommonArmorId.IsValid();
			TestPawn->TeleportTo(Smith->GetActorLocation() + FVector(-95.f, 0.f, 96.f),
				FRotator::ZeroRotator, false, true);
			TestPawn->RestoreVitalsAtCheckpoint();
			const FGuid CraftRequest = FGuid::NewGuid();
			TestController->ServerCraftCampEquipment_Implementation(
				Smith, EAuraEquipmentSlot::Armor, CraftRequest);
			TestController->ServerCraftCampEquipment_Implementation(
				Smith, EAuraEquipmentSlot::Armor, CraftRequest);
			FGuid CraftedId;
			int32 CraftedCount = 0;
			for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
				if (Entry.Item.SourceType == EAuraItemSourceType::Crafted
					&& Entry.Item.Definition && Entry.Item.Definition->ItemType
						== EAuraItemType::Equipment)
				{
					CraftedId = Entry.Item.InstanceId;
					++CraftedCount;
				}
			bPassed &= CraftedCount == 1 && CraftedId.IsValid()
				&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == 6;
			CraftedIds.Add(CraftedId);
			const FGuid SmeltRequest = FGuid::NewGuid();
			TestController->ServerSmeltCampEquipment_Implementation(
				Smith, CommonArmorId, SmeltRequest);
			TestController->ServerSmeltCampEquipment_Implementation(
				Smith, CommonArmorId, SmeltRequest);
			bPassed &= !Bag->FindEntryByInstanceId(CommonArmorId)
				&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == 7;
			FAuraItemInstance Ore;
			Ore.Definition = UAuraItemCatalog::Get().FindItem(
				TEXT("Material.ArcaneOreShard"));
			Ore.EnsureIdentity();
			bPassed &= Ore.IsValid() && Bag->TryAddItem(Ore, Failure, false) == 1
				&& State->TryForgeWeapon(CraftedId, Failure);
			const FAuraInventoryEntry* Enhanced = Bag->FindEntryByInstanceId(CraftedId);
			bPassed &= Enhanced && Enhanced->Item.EnhancementLevel == 1
				&& AAuraPlayerState::IsValidGrowthSnapshot(State->GetGrowthState());
		}
	}
	bPassed &= CraftedIds.Num() == 2;
	UE_LOG(LogAuraM23Smoke, Display,
		TEXT("AURA_M23_COOP_SERVER Passed=%s Players=%d CraftedIds=%d"),
		bPassed ? TEXT("true") : TEXT("false"),
		GameState ? GameState->PlayerArray.Num() : 0, CraftedIds.Num());
	FTimerHandle DoneTimer;
	GetWorldTimerManager().SetTimer(DoneTimer, []()
	{
		UE_LOG(LogAuraM23Smoke, Display, TEXT("AURA_M23_COOP_SERVER_DONE"));
	}, 14.f, false);
}

void AAuraPlayerController::AuraObserveEconomyM23()
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
	int32 CraftedCount = 0;
	int32 FineCount = 0;
	if (Bag)
	{
		for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
		{
			CraftedCount += Entry.Item.SourceType == EAuraItemSourceType::Crafted
				&& Entry.Item.Definition
				&& Entry.Item.Definition->ItemType == EAuraItemType::Equipment
				&& Entry.Item.EnhancementLevel == 1;
			FineCount += Entry.Item.SourceType == EAuraItemSourceType::Merchant
				&& Entry.Item.Rarity == EAuraItemRarity::Fine;
		}
	}
	const bool bPassed = State && Bag && State->GetGrowthState().PaidLevel == 7
		&& State->GetGrowthState().SmithMetalPurchased == 12
		&& Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) == 7
		&& CraftedCount == 1 && FineCount == 1;
	UE_LOG(LogAuraM23Smoke, Display,
		TEXT("AURA_M23_COOP_CLIENT Passed=%s Level=%d Purchased=%d Metal=%d Crafted=%d Fine=%d"),
		bPassed ? TEXT("true") : TEXT("false"),
		State ? State->GetGrowthState().PaidLevel : -1,
		State ? State->GetGrowthState().SmithMetalPurchased : -1,
		Bag ? Bag->GetTotalQuantity(TEXT("Material.SmithMetal")) : -1,
		CraftedCount, FineCount);
}

void AAuraPlayerController::AuraTestSeedV3SaveM23()
{
	FString Slot;
	FParse::Value(FCommandLine::Get(), TEXT("AuraM23AuditSlot="), Slot);
	bool bValidSlot = Slot.StartsWith(TEXT("AuraM23V3_")) && Slot.Len() <= 40;
	for (const TCHAR SlotChar : Slot)
		bValidSlot &= FChar::IsAlnum(SlotChar) || SlotChar == TEXT('_');
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	if (!HasAuthority() || !IsLocalController() || !Run
		|| Run->IsPersistentSaveEnabled() || !bValidSlot
		|| UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		UE_LOG(LogAuraM23Smoke, Error,
			TEXT("AURA_M23_V3_SEED Passed=false Guard=false Slot=%s"), *Slot);
		return;
	}
	UAuraProfileSaveGame* Save = Cast<UAuraProfileSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAuraProfileSaveGame::StaticClass()));
	if (!Save) return;
	Save->SchemaVersion = 3;
	Save->bHasProfile = true;
	Save->Growth.PaidLevel = 10;
	Save->Growth.TotalExperience = 1730;
	Save->Growth.Souls = 321;
	Save->Growth.SkillRanks.FireBolt = 3;
	Save->Growth.SkillRanks.ArcaneShards = 1;
	Save->Growth.SkillRanks.Shock = 1;
	Save->Growth.UnspentSkillPoints = 3;
	Save->Growth.ComboRanks.Ember = 1;
	Save->Growth.ComboRanks.Fracture = 1;
	Save->Growth.ComboRanks.Overload = 1;
	Save->Growth.UnspentComboPoints = 0;
	FAuraSavedBackpackEntry& Ore = Save->Backpack.AddDefaulted_GetRef();
	Ore.SlotIndex = 0;
	Ore.Item.InstanceId = FGuid::NewGuid();
	Ore.Item.DefinitionId = TEXT("Material.ArcaneOreShard");
	Ore.Item.Quantity = 3;
	Ore.Item.SourceType = EAuraItemSourceType::Treasure;
	Ore.Item.SourceId = TEXT("M23V3Seed");
	const bool bValid = AAuraPlayerState::IsValidV3GrowthSnapshot(Save->Growth)
		&& UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
	UE_LOG(LogAuraM23Smoke, Display,
		TEXT("AURA_M23_V3_SEED Passed=%s Slot=%s OriginalSkillPoints=%d MigratedSkillPoints=%d"),
		bValid ? TEXT("true") : TEXT("false"), *Slot,
		Save->Growth.UnspentSkillPoints,
		AAuraPlayerState::MigrateV3GrowthSnapshot(Save->Growth).UnspentSkillPoints);
}
