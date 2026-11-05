// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Character/AuraCharacter.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Items/AuraWorldItem.h"
#include "Level/AuraCampVendor.h"
#include "Loot/AuraLootSubsystem.h"
#include "Player/AuraPlayerState.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraCampSmoke, Log, All);

namespace
{
	AAuraCampVendor* FindService(UWorld* World, const EAuraCampService Type)
	{
		for (TActorIterator<AAuraCampVendor> It(World); It; ++It)
		{
			if (It->GetServiceType() == Type) return *It;
		}
		return nullptr;
	}

	FAuraItemInstance MakeItem(const FName Id, const int32 Quantity = 1)
	{
		FAuraItemInstance Item;
		Item.Definition = UAuraItemCatalog::Get().FindItem(Id);
		Item.Quantity = Quantity;
		Item.EnsureIdentity();
		return Item;
	}

	void PlaceNearService(AAuraCharacter* Character, AAuraCampVendor* Vendor,
		const float Side = 0.f)
	{
		Character->TeleportTo(Vendor->GetActorLocation()
			+ FVector(-95.f, Side, 96.f), FRotator::ZeroRotator, false, true);
		Character->RestoreVitalsAtCheckpoint();
	}
}

void AAuraPlayerController::AuraTestCampEconomyM13()
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone) return;
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	UAuraEquipmentComponent* Equipment = GetAuraEquipment();
	AAuraCampVendor* Merchant = FindService(GetWorld(), EAuraCampService::Merchant);
	AAuraCampVendor* Smith = FindService(GetWorld(), EAuraCampService::Blacksmith);
	if (!State || !AuraPawn || !Inventory || !Equipment || !Merchant || !Smith)
	{
		UE_LOG(LogAuraCampSmoke, Error, TEXT("AURA_M13_STANDALONE setup failed"));
		return;
	}
	Equipment->DebugClearEquipment();
	Inventory->DebugClearInventory();
	PlaceNearService(AuraPawn, Merchant);
	FString Failure;
	const int32 InitialSouls = State->GetGrowthState().Souls;
	const bool bNoMoney = !State->TryPurchaseMerchantOffer(1, Failure)
		&& State->GetGrowthState().Souls == InitialSouls;
	State->AwardExperienceAndSouls(0, 1000);
	ServerOpenCampVendor_Implementation(Merchant);
	const bool bMenu = bCampMenuOpen && BuildCampServiceRows().Num() == 9;
	CloseCampMenu();

	const int32 BeforeFirstBuy = State->GetGrowthState().Souls;
	ServerBuyCampOffer_Implementation(Merchant, 1, FGuid::NewGuid());
	const FGuid PotionGuid = Inventory->FindFirstConsumable(EAuraConsumableEffect::Health);
	const bool bBuy = PotionGuid.IsValid()
		&& State->GetGrowthState().Souls == BeforeFirstBuy - 18;
	const int32 BeforeSale = State->GetGrowthState().Souls;
	ServerSellCampItem_Implementation(Merchant, PotionGuid, FGuid::NewGuid());
	const bool bNoArbitrage = Inventory->GetTotalQuantity(TEXT("Consumable.HealthPotion")) == 0
		&& State->GetGrowthState().Souls == BeforeSale + 4
		&& State->GetGrowthState().Souls < BeforeFirstBuy;

	Inventory->DebugClearInventory();
	const FAuraItemInstance StaffTemplate = MakeItem(TEXT("Equipment.ArcaneStaff"));
	for (int32 Index = 0; Index < Inventory->GetCapacity(); ++Index)
	{
		FAuraItemInstance Filler = StaffTemplate;
		Filler.InstanceId = FGuid::NewGuid();
		Inventory->TryAddItem(Filler, Failure, false);
	}
	const int32 BeforeFullBuy = State->GetGrowthState().Souls;
	const bool bFullRejected = Inventory->GetOccupiedSlotCount() == Inventory->GetCapacity()
		&& !State->TryPurchaseMerchantOffer(1, Failure)
		&& State->GetGrowthState().Souls == BeforeFullBuy;
	Inventory->DebugClearInventory();

	const int32 BeforeStone = State->GetGrowthState().Souls;
	const bool bStoneBought = State->TryPurchaseMerchantOffer(8, Failure);
	const bool bStoneLimit = bStoneBought
		&& !State->TryPurchaseMerchantOffer(8, Failure)
		&& State->GetGrowthState().Souls == BeforeStone - 100
		&& Inventory->GetTotalQuantity(TEXT("Consumable.RespecStone")) == 1;

	const FGuid ReplayId = FGuid::NewGuid();
	const int32 BeforeReplay = State->GetGrowthState().Souls;
	ServerBuyCampOffer_Implementation(Merchant, 2, ReplayId);
	ServerBuyCampOffer_Implementation(Merchant, 2, ReplayId);
	const bool bReplay = State->GetGrowthState().Souls == BeforeReplay - 18
		&& Inventory->GetTotalQuantity(TEXT("Consumable.ManaPotion")) == 1;
	const int32 BeforeWrongId = State->GetGrowthState().Souls;
	const bool bWrongOwner = !State->TrySellMerchantItem(FGuid::NewGuid(), Failure)
		&& State->GetGrowthState().Souls == BeforeWrongId;

	PlaceNearService(AuraPawn, Smith);
	const int32 BeforeDistance = State->GetGrowthState().Souls;
	AuraPawn->TeleportTo(Smith->GetActorLocation() + FVector(1000.f, 0.f, 96.f),
		FRotator::ZeroRotator, false, true);
	ServerBuyCampOffer_Implementation(Merchant, 1, FGuid::NewGuid());
	const bool bDistanceRejected = State->GetGrowthState().Souls == BeforeDistance;
	PlaceNearService(AuraPawn, Merchant);
	AuraPawn->NotifyCombatActivity();
	ServerBuyCampOffer_Implementation(Merchant, 1, FGuid::NewGuid());
	const bool bCombatRejected = State->GetGrowthState().Souls == BeforeDistance;
	AuraPawn->RestoreVitalsAtCheckpoint();

	Inventory->DebugClearInventory();
	FAuraItemInstance Staff = MakeItem(TEXT("Equipment.ArcaneStaff"));
	const FGuid WeaponId = Staff.InstanceId;
	Inventory->TryAddItem(Staff, Failure, false);
	const bool bEquipped = Equipment->TryEquipItem(WeaponId, Failure);
	const int32 BeforeNoOre = State->GetGrowthState().Souls;
	const bool bNoOre = !State->TryForgeWeapon(WeaponId, Failure)
		&& State->GetGrowthState().Souls == BeforeNoOre
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)->EnhancementLevel == 0;
	FAuraItemInstance Ore = MakeItem(TEXT("Material.ArcaneOreShard"), 6);
	Inventory->TryAddItem(Ore, Failure, false);
	PlaceNearService(AuraPawn, Smith);
	const FGameplayTag IntTag = FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Intelligence"));
	const float BeforeInt = Equipment->GetEquippedStatMagnitude(IntTag);
	const int32 BeforeForge = State->GetGrowthState().Souls;
	ServerForgeCampWeapon_Implementation(Smith, WeaponId, FGuid::NewGuid());
	const bool bForgeOne = Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)->EnhancementLevel == 1
		&& Equipment->GetEquippedStatMagnitude(IntTag) > BeforeInt
		&& Equipment->GetActiveEffectCountForDebug() == 1;
	const bool bForgeTwo = State->TryForgeWeapon(WeaponId, Failure);
	const bool bForgeThree = State->TryForgeWeapon(WeaponId, Failure);
	const int32 BeforeMaxAttempt = State->GetGrowthState().Souls;
	const bool bMaxRejected = !State->TryForgeWeapon(WeaponId, Failure)
		&& State->GetGrowthState().Souls == BeforeMaxAttempt;
	const bool bForgeEconomy = bForgeOne && bForgeTwo && bForgeThree && bMaxRejected
		&& State->GetGrowthState().Souls == BeforeForge - 130
		&& Inventory->GetTotalQuantity(TEXT("Material.ArcaneOreShard")) == 0
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)->EnhancementLevel == 3;
	const bool bUnequipped = Equipment->TryUnequipSlot(EAuraEquipmentSlot::Weapon, Failure);
	const FAuraInventoryEntry* BagWeapon = Inventory->FindEntryByInstanceId(WeaponId);
	const bool bBagKeepsLevel = bUnequipped && BagWeapon
		&& BagWeapon->Item.EnhancementLevel == 3;
	const bool bReequipped = Equipment->TryEquipItem(WeaponId, Failure)
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)
		&& Equipment->GetEquippedItem(EAuraEquipmentSlot::Weapon)->EnhancementLevel == 3
		&& Equipment->GetActiveEffectCountForDebug() == 1;
	Equipment->TryUnequipSlot(EAuraEquipmentSlot::Weapon, Failure);
	BagWeapon = Inventory->FindEntryByInstanceId(WeaponId);
	AAuraWorldItem* Dropped = nullptr;
	const bool bDropped = BagWeapon && Inventory->TryDropSlot(
		BagWeapon->SlotIndex, 1, Failure, &Dropped);
	const bool bGroundKeepsLevel = bDropped && Dropped
		&& Dropped->GetItemInstance().EnhancementLevel == 3;
	const bool bPicked = Dropped && Dropped->TryPickupBy(AuraPawn) == 1;
	const FAuraInventoryEntry* Recovered = Inventory->FindEntryByInstanceId(WeaponId);
	const bool bRoundTrip = bBagKeepsLevel && bReequipped && bGroundKeepsLevel
		&& bPicked && Recovered && Recovered->Item.EnhancementLevel == 3;
	bool bOreLoot = false;
	if (const UAuraLootSubsystem* Loot = GetWorld()->GetSubsystem<UAuraLootSubsystem>())
	{
		const auto CountOre = [](const TArray<FAuraItemInstance>& Items)
		{
			int32 Total = 0;
			for (const FAuraItemInstance& Item : Items)
			{
				if (Item.Definition && Item.Definition->ItemId
					== FName(TEXT("Material.ArcaneOreShard"))) Total += Item.Quantity;
			}
			return Total;
		};
		const int32 EliteOre = CountOre(Loot->RollLoot(EAuraLootProfileType::EliteEnemy, 2, 1301));
		const int32 BossOre = CountOre(Loot->RollLoot(EAuraLootProfileType::Boss, 5, 1302));
		int32 CommonOre = 0;
		for (int32 Seed = 1; Seed <= 100; ++Seed)
		{
			CommonOre += CountOre(Loot->RollLoot(EAuraLootProfileType::CommonEnemy, 1,
				13000 + Seed));
		}
		bOreLoot = EliteOre >= 1 && BossOre >= 3 && CommonOre >= 5 && CommonOre <= 25;
	}

	const bool bPassed = bNoMoney && bMenu && bBuy && bNoArbitrage
		&& bFullRejected && bStoneLimit && bReplay && bWrongOwner
		&& bDistanceRejected && bCombatRejected && bEquipped && bNoOre
		&& bForgeEconomy && bRoundTrip && bOreLoot;
	UE_LOG(LogAuraCampSmoke, Display,
		TEXT("AURA_M13_STANDALONE Passed=%s NoMoney=%s Menu=%s BuySale=%s Full=%s Limit=%s Replay=%s WrongOwner=%s Distance=%s Combat=%s NoOre=%s Forge=%s RoundTrip=%s OreLoot=%s"),
		bPassed ? TEXT("true") : TEXT("false"), bNoMoney ? TEXT("true") : TEXT("false"),
		bMenu ? TEXT("true") : TEXT("false"),
		bBuy && bNoArbitrage ? TEXT("true") : TEXT("false"),
		bFullRejected ? TEXT("true") : TEXT("false"),
		bStoneLimit ? TEXT("true") : TEXT("false"),
		bReplay ? TEXT("true") : TEXT("false"),
		bWrongOwner ? TEXT("true") : TEXT("false"),
		bDistanceRejected ? TEXT("true") : TEXT("false"),
		bCombatRejected ? TEXT("true") : TEXT("false"),
		bNoOre ? TEXT("true") : TEXT("false"),
		bForgeEconomy ? TEXT("true") : TEXT("false"),
		bRoundTrip ? TEXT("true") : TEXT("false"),
		bOreLoot ? TEXT("true") : TEXT("false"));
}

void AAuraPlayerController::AuraTestCampCoopM13()
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone) return;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	AAuraCampVendor* Merchant = FindService(GetWorld(), EAuraCampService::Merchant);
	AAuraCampVendor* Smith = FindService(GetWorld(), EAuraCampService::Blacksmith);
	if (!GameState || GameState->PlayerArray.Num() != 2 || !Merchant || !Smith)
	{
		UE_LOG(LogAuraCampSmoke, Error, TEXT("AURA_M13_COOP_SERVER setup failed"));
		return;
	}
	TArray<AAuraPlayerState*> States;
	TArray<AAuraPlayerController*> Controllers;
	TArray<FGuid> WeaponIds;
	FString Failure;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AAuraPlayerState* State = Cast<AAuraPlayerState>(GameState->PlayerArray[Index]);
		AAuraPlayerController* Controller = State
			? Cast<AAuraPlayerController>(State->GetPlayerController()) : nullptr;
		AAuraCharacter* AuraPawn = Controller
			? Cast<AAuraCharacter>(Controller->GetPawn()) : nullptr;
		if (!State || !Controller || !AuraPawn)
		{
			UE_LOG(LogAuraCampSmoke, Error, TEXT("AURA_M13_COOP_SERVER player setup failed"));
			return;
		}
		States.Add(State);
		Controllers.Add(Controller);
		PlaceNearService(AuraPawn, Merchant, Index == 0 ? -65.f : 65.f);
		State->AwardExperienceAndSouls(0, 300);
		Controller->ServerBuyCampOffer_Implementation(Merchant, 3, FGuid::NewGuid());
		const UAuraInventoryComponent* Inventory = State->GetInventoryComponent();
		FGuid StaffId;
		for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
		{
			if (Entry.Item.Definition
				&& Entry.Item.Definition->ItemId == FName(TEXT("Equipment.ArcaneStaff")))
			{
				StaffId = Entry.Item.InstanceId;
				break;
			}
		}
		WeaponIds.Add(StaffId);
	}
	const int32 WrongOwnerSouls = States[0]->GetGrowthState().Souls;
	const bool bOwnership = !States[0]->TrySellMerchantItem(WeaponIds[1], Failure)
		&& States[0]->GetGrowthState().Souls == WrongOwnerSouls;
	const FGuid RequestId = FGuid::NewGuid();
	Controllers[0]->ServerBuyCampOffer_Implementation(Merchant, 2, RequestId);
	Controllers[0]->ServerBuyCampOffer_Implementation(Merchant, 2, RequestId);
	bool bForged = true;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AAuraPlayerState* State = States[Index];
		UAuraInventoryComponent* Inventory = State->GetInventoryComponent();
		Inventory->TryAddItem(MakeItem(TEXT("Material.ArcaneOreShard"), 2), Failure, false);
		PlaceNearService(Cast<AAuraCharacter>(Controllers[Index]->GetPawn()), Smith,
			Index == 0 ? -65.f : 65.f);
		Controllers[Index]->ServerForgeCampWeapon_Implementation(
			Smith, WeaponIds[Index], FGuid::NewGuid());
		const FAuraInventoryEntry* Entry = Inventory->FindEntryByInstanceId(WeaponIds[Index]);
		bForged &= Entry && Entry->Item.EnhancementLevel == 1
			&& Inventory->GetTotalQuantity(TEXT("Material.ArcaneOreShard")) == 1;
	}
	const bool bEconomy = States[0]->GetGrowthState().Souls == 217
		&& States[1]->GetGrowthState().Souls == 235
		&& States[0]->GetInventoryComponent()->GetTotalQuantity(
			TEXT("Consumable.ManaPotion")) == 1;
	UE_LOG(LogAuraCampSmoke, Display,
		TEXT("AURA_M13_COOP_SERVER Passed=%s Ownership=%s Forge=%s Economy=%s Souls=%d/%d"),
		bOwnership && bForged && bEconomy ? TEXT("true") : TEXT("false"),
		bOwnership ? TEXT("true") : TEXT("false"),
		bForged ? TEXT("true") : TEXT("false"),
		bEconomy ? TEXT("true") : TEXT("false"),
		States[0]->GetGrowthState().Souls, States[1]->GetGrowthState().Souls);
	FTimerHandle DoneTimer;
	GetWorldTimerManager().SetTimer(DoneTimer, []()
	{
		UE_LOG(LogAuraCampSmoke, Display, TEXT("AURA_M13_COOP_SERVER_DONE"));
	}, 9.f, false);
}

void AAuraPlayerController::AuraObserveCampM13()
{
	const AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	const UAuraInventoryComponent* Inventory = GetAuraInventory();
	int32 EnhancedStaffs = 0;
	if (Inventory)
	{
		for (const FAuraInventoryEntry& Entry : Inventory->GetEntries())
		{
			if (Entry.Item.Definition
				&& Entry.Item.Definition->ItemId == FName(TEXT("Equipment.ArcaneStaff"))
				&& Entry.Item.EnhancementLevel == 1)
			{
				++EnhancedStaffs;
			}
		}
	}
	const int32 Souls = State ? State->GetGrowthState().Souls : -1;
	const bool bPassed = Inventory && EnhancedStaffs == 1
		&& Inventory->GetTotalQuantity(TEXT("Material.ArcaneOreShard")) == 1
		&& (Souls == 217 || Souls == 235);
	UE_LOG(LogAuraCampSmoke, Display,
		TEXT("AURA_M13_COOP_CLIENT Passed=%s Souls=%d ForgedStaffs=%d Ore=%d"),
		bPassed ? TEXT("true") : TEXT("false"), Souls, EnhancedStaffs,
		Inventory ? Inventory->GetTotalQuantity(TEXT("Material.ArcaneOreShard")) : -1);
}
