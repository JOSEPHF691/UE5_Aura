// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Character/AuraCharacter.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Equipment/AuraEquipmentComponent.h"
#include "Game/AuraGameState.h"
#include "Game/AuraLanSessionSubsystem.h"
#include "Game/AuraRunSubsystem.h"
#include "Inventory/AuraInventoryComponent.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Level/AuraCampVendor.h"
#include "Player/AuraPlayerState.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraSaveSmoke, Log, All);

namespace
{
	FAuraItemInstance MakeSaveTestItem(const FName DefinitionId, const int32 Quantity)
	{
		FAuraItemInstance Item;
		Item.Definition = UAuraItemCatalog::Get().FindItem(DefinitionId);
		Item.Quantity = Quantity;
		Item.EnsureIdentity();
		return Item;
	}

	AAuraCampVendor* FindM15Merchant(UWorld* World)
	{
		for (TActorIterator<AAuraCampVendor> It(World); It; ++It)
		{
			if (It->GetServiceType() == EAuraCampService::Merchant) return *It;
		}
		return nullptr;
	}
}

void AAuraPlayerController::AuraTestSeedSaveM15()
{
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
	UAuraEquipmentComponent* Gear = State ? State->GetEquipmentComponent() : nullptr;
	if (!HasAuthority() || GetNetMode() != NM_Standalone || !State || !Run || !Bag || !Gear
		|| !Run->IsPersistentSaveEnabled() || !GetPawn())
	{
		UE_LOG(LogAuraSaveSmoke, Error, TEXT("AURA_M15_SEED_DONE Passed=false Setup=false"));
		return;
	}
	FString Failure;
	State->AwardExperienceAndSouls(480, 500);
	const bool bGrowth = State->GetGrowthState().PaidLevel == 5
		&& State->TryAllocateSkill(EAuraPlayerSkill::ArcaneShards, Failure);
	const FAuraItemInstance Staff = MakeSaveTestItem(TEXT("Equipment.ArcaneStaff"), 1);
	const FAuraItemInstance Ore = MakeSaveTestItem(TEXT("Material.ArcaneOreShard"), 4);
	const FAuraItemInstance Potion = MakeSaveTestItem(TEXT("Consumable.ManaPotion"), 2);
	FAuraItemInstance RareArmor = MakeSaveTestItem(TEXT("Equipment.WardenArmor"), 1);
	RareArmor.ItemLevel = 5;
	RareArmor.Rarity = EAuraItemRarity::Legendary;
	RareArmor.SourceType = EAuraItemSourceType::Debug;
	RareArmor.SourceId = TEXT("M15SaveSmoke");
	AuraItems::RollEquipmentModifiers(RareArmor, 12345);
	const bool bAdded = Staff.IsValid() && Ore.IsValid() && Potion.IsValid()
		&& RareArmor.IsValid() && RareArmor.RolledModifiers.Num() == 4
		&& Bag->TryAddItem(Staff, Failure, false) == 1
		&& Bag->TryAddItem(Ore, Failure, false) == 4
		&& Bag->TryAddItem(Potion, Failure, false) == 2
		&& Bag->TryAddItem(RareArmor, Failure, false) == 1;
	const bool bEquipped = bAdded && Gear->TryEquipItem(Staff.InstanceId, Failure)
		&& Gear->TryEnhanceEquippedWeapon(Staff.InstanceId, 1, Failure)
		&& Gear->TryEnhanceEquippedWeapon(Staff.InstanceId, 2, Failure);
	Run->SaveCheckpoint(GetWorld(), TEXT("M15SaveSmoke"), GetPawn()->GetActorTransform(),
		State->GetGrowthState().PaidLevel, State->GetGrowthState().TotalExperience);
	Run->MarkTreasureClaimed(TEXT("M15ClaimedChest"));
	Run->MarkRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"));
	AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	const bool bSeal = Quest && Quest->TryActivateSeal(TEXT("LowerNorth"));
	const bool bWritten = Run->FlushPersistentSave();
	const bool bPassed = bGrowth && bAdded && bEquipped && bSeal && bWritten
		&& Bag->GetOccupiedSlotCount() == 3 && Gear->GetEquippedCount() == 1;
	UE_LOG(LogAuraSaveSmoke, Display,
		TEXT("AURA_M15_SEED_DONE Passed=%s Growth=%s Items=%s Gear=%s Seal=%s Write=%s Soul=%d Bag=%d Effects=%d"),
		bPassed ? TEXT("true") : TEXT("false"), bGrowth ? TEXT("true") : TEXT("false"),
		bAdded ? TEXT("true") : TEXT("false"), bEquipped ? TEXT("true") : TEXT("false"),
		bSeal ? TEXT("true") : TEXT("false"), bWritten ? TEXT("true") : TEXT("false"),
		State->GetGrowthState().Souls, Bag->GetOccupiedSlotCount(),
		Gear->GetActiveEffectCountForDebug());
}

void AAuraPlayerController::AuraTestVerifySaveM15()
{
	AAuraPlayerState* State = GetPlayerState<AAuraPlayerState>();
	UAuraRunSubsystem* Run = GetGameInstance()->GetSubsystem<UAuraRunSubsystem>();
	UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
	UAuraEquipmentComponent* Gear = State ? State->GetEquipmentComponent() : nullptr;
	AAuraGameState* Quest = GetWorld()->GetGameState<AAuraGameState>();
	if (!HasAuthority() || GetNetMode() != NM_Standalone || !State || !Run || !Bag || !Gear
		|| !Quest || !Run->IsPersistentSaveEnabled())
	{
		UE_LOG(LogAuraSaveSmoke, Error, TEXT("AURA_M15_VERIFY_DONE Passed=false Setup=false"));
		return;
	}
	const FAuraGrowthState& Growth = State->GetGrowthState();
	const int32 ExpectedSouls = 500;
	const FAuraItemInstance* Weapon = Gear->GetEquippedItem(EAuraEquipmentSlot::Weapon);
	const FAuraItemInstance* Armor = nullptr;
	for (const FAuraInventoryEntry& Entry : Bag->GetEntries())
	{
		if (Entry.Item.Definition
			&& Entry.Item.Definition->ItemId == FName(TEXT("Equipment.WardenArmor")))
		{
			Armor = &Entry.Item;
			break;
		}
	}
	FTransform Checkpoint;
	const bool bGrowth = Growth.PaidLevel == 5 && Growth.TotalExperience == 480
		&& Growth.Souls == ExpectedSouls && Growth.UnspentSkillPoints == 3
		&& Growth.SkillRanks.ArcaneShards == 1;
	const bool bItems = Bag->GetOccupiedSlotCount() == 3
		&& Bag->GetTotalQuantity(TEXT("Material.ArcaneOreShard")) == 4
		&& Bag->GetTotalQuantity(TEXT("Consumable.ManaPotion")) == 2
		&& Armor && Armor->ItemLevel == 5
		&& Armor->Rarity == EAuraItemRarity::Legendary
		&& Armor->RollSeed == 12345 && Armor->RolledModifiers.Num() == 4
		&& Armor->SourceId == FName(TEXT("M15SaveSmoke"))
		&& Weapon && Weapon->Definition
		&& Weapon->Definition->ItemId == FName(TEXT("Equipment.ArcaneStaff"))
		&& Weapon->EnhancementLevel == 2;
	const bool bWorld = Run->TryGetCheckpointForWorld(GetWorld(), Checkpoint)
		&& Run->GetActiveCheckpointId() == FName(TEXT("M15SaveSmoke"))
		&& Run->IsTreasureClaimed(TEXT("M15ClaimedChest"))
		&& Run->IsRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"))
		&& Quest->IsRewardEncounterCleared(TEXT("LowerNorthBoonGuardians"))
		&& Quest->HasActivatedSeal(TEXT("LowerNorth"));
	TSet<FGuid> BeforeIds;
	for (const FAuraInventoryEntry& Entry : Bag->GetEntries()) BeforeIds.Add(Entry.Item.InstanceId);
	if (Weapon) BeforeIds.Add(Weapon->InstanceId);
	Run->RestoreStandalonePlayer(State);
	Run->RestoreStandalonePlayer(State);
	Gear->RefreshEquipmentEffects();
	Gear->RefreshEquipmentEffects();
	TSet<FGuid> AfterIds;
	for (const FAuraInventoryEntry& Entry : Bag->GetEntries()) AfterIds.Add(Entry.Item.InstanceId);
	const FAuraItemInstance* AfterWeapon = Gear->GetEquippedItem(EAuraEquipmentSlot::Weapon);
	if (AfterWeapon) AfterIds.Add(AfterWeapon->InstanceId);
	bool bSameIds = BeforeIds.Num() == AfterIds.Num();
	for (const FGuid& Id : BeforeIds) bSameIds &= AfterIds.Contains(Id);
	const bool bIdempotent = BeforeIds.Num() == 4 && bSameIds
		&& AfterIds.Num() == 4 && Bag->GetOccupiedSlotCount() == 3
		&& Gear->GetEquippedCount() == 1 && Gear->GetActiveEffectCountForDebug() == 1
		&& State->GetGrowthState().Souls == ExpectedSouls
		&& State->GetGrowthState().UnspentSkillPoints == 0;
	if (!bInventoryOpen) ToggleInventory();
	const bool bEquipmentPage = bInventoryOpen && !bInventoryQuestPage && !bInventorySkillPage;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Pressed, 1.f));
	const bool bQuestPage = bInventoryQuestPage && !bInventorySkillPage;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Pressed, 1.f));
	const bool bSkillPage = bInventorySkillPage && !bInventoryQuestPage;
	InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Pressed, 1.f));
	const bool bReturned = !bInventoryQuestPage && !bInventorySkillPage;
	ToggleInventory();
	const bool bPages = bEquipmentPage && bQuestPage && bSkillPage && bReturned;
	const bool bPassed = bGrowth && bItems && bWorld && bIdempotent && bPages;
	UE_LOG(LogAuraSaveSmoke, Display,
		TEXT("AURA_M15_VERIFY_DONE Passed=%s Growth=%s Items=%s World=%s Idempotent=%s Pages=%s Soul=%d Bag=%d Effects=%d"),
		bPassed ? TEXT("true") : TEXT("false"), bGrowth ? TEXT("true") : TEXT("false"),
		bItems ? TEXT("true") : TEXT("false"), bWorld ? TEXT("true") : TEXT("false"),
		bIdempotent ? TEXT("true") : TEXT("false"),
		bPages ? TEXT("true") : TEXT("false"), Growth.Souls,
		Bag->GetOccupiedSlotCount(), Gear->GetActiveEffectCountForDebug());
}

void AAuraPlayerController::AuraTestNetSetupM15()
{
	if (!HasAuthority() || GetNetMode() == NM_Standalone) return;
	TArray<AAuraPlayerController*> Controllers;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get()))
			Controllers.Add(Controller);
	}
	if (Controllers.Num() < 2 && GetWorld()->GetTimeSeconds() < 30.f)
	{
		FTimerHandle RetryTimer;
		GetWorldTimerManager().SetTimer(RetryTimer, this,
			&AAuraPlayerController::AuraTestNetSetupM15, 1.f, false);
		return;
	}
	AAuraCampVendor* Merchant = FindM15Merchant(GetWorld());
	if (Controllers.Num() != 2 || !Merchant)
	{
		UE_LOG(LogAuraSaveSmoke, Error,
			TEXT("AURA_M15_NET_SERVER_DONE Passed=false Setup=false Players=%d"), Controllers.Num());
		return;
	}
	for (int32 Index = 0; Index < Controllers.Num(); ++Index)
	{
		AAuraPlayerController* Controller = Controllers[Index];
		AAuraPlayerState* State = Controller->GetPlayerState<AAuraPlayerState>();
		AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(Controller->GetPawn());
		if (!State || !AuraPawn)
		{
			UE_LOG(LogAuraSaveSmoke, Error, TEXT("AURA_M15_NET_SERVER_DONE Passed=false Pawn=false"));
			return;
		}
		State->GetEquipmentComponent()->DebugClearEquipment();
		State->GetInventoryComponent()->DebugClearInventory();
		State->AwardExperienceAndSouls(0, 100);
		AuraPawn->TeleportTo(Merchant->GetActorLocation()
			+ FVector(-95.f, Index == 0 ? -70.f : 70.f, 96.f),
			FRotator::ZeroRotator, false, true);
		AuraPawn->RestoreVitalsAtCheckpoint();
	}
	UE_LOG(LogAuraSaveSmoke, Display,
		TEXT("AURA_M15_NET_SETUP Players=2 Merchant=%s"), *GetNameSafe(Merchant));
	const TWeakObjectPtr<AAuraPlayerController> WeakSelf(this);
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [WeakSelf]()
	{
		if (!WeakSelf.IsValid()) return;
		int32 PassedPlayers = 0;
		int32 Count = 0;
		for (FConstPlayerControllerIterator It = WeakSelf->GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AAuraPlayerController* Controller = Cast<AAuraPlayerController>(It->Get());
			const AAuraPlayerState* State = Controller
				? Controller->GetPlayerState<AAuraPlayerState>() : nullptr;
			if (!State) continue;
			++Count;
			const UAuraInventoryComponent* Bag = State->GetInventoryComponent();
			const bool bCorrect = State->GetGrowthState().Souls == 82
				&& Bag->GetTotalQuantity(TEXT("Consumable.HealthPotion")) == 1
				&& Bag->GetOccupiedSlotCount() == 1;
			PassedPlayers += bCorrect ? 1 : 0;
		}
		UE_LOG(LogAuraSaveSmoke, Display,
			TEXT("AURA_M15_NET_SERVER_DONE Passed=%s Players=%d Correct=%d"),
			Count == 2 && PassedPlayers == 2 ? TEXT("true") : TEXT("false"),
			Count, PassedPlayers);
	}, 12.f, false);
}

void AAuraPlayerController::AuraTestNetPurchaseM15()
{
	if (!IsLocalController() || HasAuthority()) return;
	AAuraCampVendor* Merchant = FindM15Merchant(GetWorld());
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if ((!Merchant || !AuraPawn || FVector::DistSquared2D(
		Merchant->GetActorLocation(), AuraPawn->GetActorLocation()) > FMath::Square(230.f))
		&& GetWorld()->GetTimeSeconds() < 30.f)
	{
		FTimerHandle RetryTimer;
		GetWorldTimerManager().SetTimer(RetryTimer, this,
			&AAuraPlayerController::AuraTestNetPurchaseM15, 1.f, false);
		return;
	}
	if (!Merchant || !AuraPawn)
	{
		UE_LOG(LogAuraSaveSmoke, Error, TEXT("AURA_M15_NET_CLIENT Passed=false Setup=false"));
		return;
	}
	const FGuid RequestId = FGuid::NewGuid();
	ServerBuyCampOffer(Merchant, 1, RequestId);
	ServerBuyCampOffer(Merchant, 1, RequestId);
	const TWeakObjectPtr<AAuraPlayerController> WeakSelf(this);
	FTimerHandle ResultTimer;
	GetWorldTimerManager().SetTimer(ResultTimer, [WeakSelf]()
	{
		if (!WeakSelf.IsValid()) return;
		const AAuraPlayerState* State = WeakSelf->GetPlayerState<AAuraPlayerState>();
		const UAuraInventoryComponent* Bag = State ? State->GetInventoryComponent() : nullptr;
		const bool bPassed = State && Bag && State->GetGrowthState().Souls == 82
			&& Bag->GetTotalQuantity(TEXT("Consumable.HealthPotion")) == 1
			&& Bag->GetOccupiedSlotCount() == 1;
		UE_LOG(LogAuraSaveSmoke, Display,
			TEXT("AURA_M15_NET_CLIENT Passed=%s Soul=%d Potion=%d Slots=%d"),
			bPassed ? TEXT("true") : TEXT("false"),
			State ? State->GetGrowthState().Souls : -1,
			Bag ? Bag->GetTotalQuantity(TEXT("Consumable.HealthPotion")) : -1,
			Bag ? Bag->GetOccupiedSlotCount() : -1);
	}, 3.f, false);
}

void AAuraPlayerController::AuraTestLanHostM15()
{
	if (!IsLocalController()) return;
	if (GetNetMode() == NM_Standalone)
	{
		UAuraLanSessionSubsystem* Lan = GetGameInstance()->GetSubsystem<UAuraLanSessionSubsystem>();
		const bool bStarted = Lan && Lan->HostRoom();
		UE_LOG(LogAuraSaveSmoke, Display,
			TEXT("AURA_M15_LAN_HOST_REQUEST Started=%s Status=%s"),
			bStarted ? TEXT("true") : TEXT("false"), Lan ? *Lan->GetStatus() : TEXT("No subsystem"));
		return;
	}
	const AAuraGameState* SessionState = GetWorld()->GetGameState<AAuraGameState>();
	const int32 Players = SessionState ? SessionState->PlayerArray.Num() : 0;
	if (GetNetMode() == NM_ListenServer && Players < 2
		&& GetWorld()->GetTimeSeconds() < 50.f)
	{
		FTimerHandle Retry;
		GetWorldTimerManager().SetTimer(Retry, this,
			&AAuraPlayerController::AuraTestLanHostM15, 1.f, false);
		return;
	}
	UE_LOG(LogAuraSaveSmoke, Display,
		TEXT("AURA_M15_LAN_HOST_CONNECTED Passed=%s Players=%d"),
		GetNetMode() == NM_ListenServer && Players == 2 ? TEXT("true") : TEXT("false"),
		Players);
}

void AAuraPlayerController::AuraTestLanJoinM15()
{
	if (!IsLocalController()) return;
	if (GetNetMode() == NM_Client)
	{
		const AAuraGameState* SessionState = GetWorld()->GetGameState<AAuraGameState>();
		const bool bJoined = GetPawn() != nullptr && SessionState
			&& SessionState->PlayerArray.Num() == 2;
		if (!bJoined && GetWorld()->GetTimeSeconds() < 40.f)
		{
			FTimerHandle Retry;
			GetWorldTimerManager().SetTimer(Retry, this,
				&AAuraPlayerController::AuraTestLanJoinM15, 1.f, false);
			return;
		}
		UE_LOG(LogAuraSaveSmoke, Display,
			TEXT("AURA_M15_LAN_JOINED Passed=%s Players=%d Pawn=%s"),
			bJoined ? TEXT("true") : TEXT("false"),
			SessionState ? SessionState->PlayerArray.Num() : 0, *GetNameSafe(GetPawn()));
		return;
	}
	UAuraLanSessionSubsystem* Lan = GetGameInstance()->GetSubsystem<UAuraLanSessionSubsystem>();
	if (!Lan || GetWorld()->GetTimeSeconds() > 45.f)
	{
		UE_LOG(LogAuraSaveSmoke, Error, TEXT("AURA_M15_LAN_JOINED Passed=false SearchTimeout=true"));
		return;
	}
	if (!Lan->IsBusy())
	{
		if (!Lan->GetRoomLabels().IsEmpty())
		{
			if (Lan->JoinRoom(0)) return;
		}
		else Lan->SearchRooms();
	}
	FTimerHandle Retry;
	GetWorldTimerManager().SetTimer(Retry, this,
		&AAuraPlayerController::AuraTestLanJoinM15, 2.f, false);
}
