// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "AI/AuraAIController.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Game/AuraRunSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Items/AuraWorldItem.h"
#include "Loot/AuraLootSubsystem.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraM25Smoke, Log, All);

void AAuraPlayerController::AuraTestInventoryMoveM25()
{
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	if (!HasAuthority() || !IsLocalController() || GetNetMode() != NM_Standalone
		|| !Inventory || !Run || Run->IsPersistentSaveEnabled())
	{
		UE_LOG(LogAuraM25Smoke, Error, TEXT("AURA_M25_INVENTORY Passed=false Setup=false"));
		return;
	}

	UAuraItemDefinition* Stackable = NewObject<UAuraItemDefinition>(this);
	Stackable->ItemId = TEXT("M25.Stackable");
	Stackable->DisplayName = FText::FromString(TEXT("测试药剂"));
	Stackable->ItemType = EAuraItemType::Consumable;
	Stackable->MaxStackSize = 10;
	UAuraItemDefinition* Other = NewObject<UAuraItemDefinition>(this);
	Other->ItemId = TEXT("M25.Other");
	Other->DisplayName = FText::FromString(TEXT("测试材料"));
	Other->ItemType = EAuraItemType::Material;
	Other->MaxStackSize = 10;
	UAuraItemDefinition* Equipment = NewObject<UAuraItemDefinition>(this);
	Equipment->ItemId = TEXT("M25.Equipment");
	Equipment->DisplayName = FText::FromString(TEXT("测试护甲"));
	Equipment->ItemType = EAuraItemType::Equipment;
	Equipment->EquipmentSlot = EAuraEquipmentSlot::Armor;
	// Even a bad authored stack-size value must not merge equipment instances.
	Equipment->MaxStackSize = 10;
	const auto MakeEntry = [](const int32 Slot, UAuraItemDefinition* Definition,
		const int32 Quantity)
	{
		FAuraInventoryEntry Entry;
		Entry.SlotIndex = Slot;
		Entry.Item.Definition = Definition;
		Entry.Item.Quantity = Quantity;
		Entry.Item.EnsureIdentity();
		return Entry;
	};
	FString Failure;
	TArray<FAuraInventoryEntry> Setup;
	const FAuraInventoryEntry First = MakeEntry(0, Stackable, 6);
	const FAuraInventoryEntry Different = MakeEntry(1, Other, 1);
	Setup.Add(First);
	Setup.Add(Different);
	Inventory->RestoreSnapshot(Setup);
	Inventory->RequestMoveItem(0, 5, First.Item.InstanceId);
	const bool bEmptyMove = !Inventory->FindEntryAtSlot(0)
		&& Inventory->FindEntryAtSlot(5)
		&& Inventory->FindEntryAtSlot(5)->Item.InstanceId == First.Item.InstanceId;
	Inventory->RequestMoveItem(5, 1, First.Item.InstanceId);
	const bool bSwap = Inventory->FindEntryAtSlot(1)
		&& Inventory->FindEntryAtSlot(1)->Item.InstanceId == First.Item.InstanceId
		&& Inventory->FindEntryAtSlot(5)
		&& Inventory->FindEntryAtSlot(5)->Item.InstanceId == Different.Item.InstanceId;
	Inventory->RequestMoveItem(1, 2, FGuid::NewGuid());
	const bool bStaleRequestRejected = Inventory->FindEntryAtSlot(1)
		&& Inventory->FindEntryAtSlot(1)->Item.InstanceId == First.Item.InstanceId
		&& !Inventory->FindEntryAtSlot(2);

	Setup.Reset();
	Setup.Add(MakeEntry(2, Stackable, 7));
	Setup.Add(MakeEntry(3, Stackable, 6));
	Inventory->RestoreSnapshot(Setup);
	const bool bPartialMerge = Inventory->TryMoveItem(2, 3, Failure)
		&& Inventory->FindEntryAtSlot(2)
		&& Inventory->FindEntryAtSlot(2)->Item.Quantity == 3
		&& Inventory->FindEntryAtSlot(3)
		&& Inventory->FindEntryAtSlot(3)->Item.Quantity == 10;
	const bool bFullStackRejected = !Inventory->TryMoveItem(2, 3, Failure)
		&& Inventory->FindEntryAtSlot(2)
		&& Inventory->FindEntryAtSlot(2)->Item.Quantity == 3;
	Setup.Reset();
	Setup.Add(MakeEntry(2, Stackable, 4));
	Setup.Add(MakeEntry(3, Stackable, 6));
	Inventory->RestoreSnapshot(Setup);
	const bool bCompleteMerge = Inventory->TryMoveItem(2, 3, Failure)
		&& !Inventory->FindEntryAtSlot(2)
		&& Inventory->FindEntryAtSlot(3)
		&& Inventory->FindEntryAtSlot(3)->Item.Quantity == 10;

	const FAuraInventoryEntry ArmorA = MakeEntry(6, Equipment, 1);
	const FAuraInventoryEntry ArmorB = MakeEntry(7, Equipment, 1);
	Setup.Reset();
	Setup.Add(ArmorA);
	Setup.Add(ArmorB);
	Inventory->RestoreSnapshot(Setup);
	const bool bEquipmentSwaps = !ArmorA.Item.CanStackWith(ArmorB.Item)
		&& Inventory->TryMoveItem(6, 7, Failure)
		&& Inventory->FindEntryAtSlot(6)
		&& Inventory->FindEntryAtSlot(6)->Item.InstanceId == ArmorB.Item.InstanceId
		&& Inventory->FindEntryAtSlot(7)
		&& Inventory->FindEntryAtSlot(7)->Item.InstanceId == ArmorA.Item.InstanceId;
	Inventory->DebugClearInventory();
	FAuraItemInstance MultipleArmor = ArmorA.Item;
	MultipleArmor.Quantity = 2;
	const bool bEquipmentAddSeparate = Inventory->TryAddItem(MultipleArmor, Failure, false) == 2
		&& Inventory->GetOccupiedSlotCount() == 2
		&& Inventory->GetEntries()[0].Item.Quantity == 1
		&& Inventory->GetEntries()[1].Item.Quantity == 1;
	// Real definitions and restored legacy instances must use the same stack rule
	// for pickup, capacity checks and the drag/drop request path.
	bool bConsumableStacks = true;
	const FName StackableIds[] = {TEXT("Consumable.HealthPotion"), TEXT("Consumable.ManaPotion"),
		TEXT("Material.ArcaneOreShard"), TEXT("Material.SmithMetal")};
	for (const FName ItemId : StackableIds)
	{
		UAuraItemDefinition* Definition = UAuraItemCatalog::Get().FindItem(ItemId);
		if (!Definition || Definition->GetMaxStackSize() <= 1)
		{
			bConsumableStacks = false;
			UE_LOG(LogAuraM25Smoke, Error, TEXT("AURA_STACK_REGRESSION Item=%s MissingStackableDefinition"), *ItemId.ToString());
			continue;
		}
		const int32 Limit = Definition->GetMaxStackSize();
		FAuraInventoryEntry Existing = MakeEntry(0, Definition, 1);
		Existing.Item.Rarity = Definition->DefaultRarity;
		Existing.Item.SourceType = EAuraItemSourceType::Merchant;
		FAuraItemInstance Incoming = Existing.Item;
		Incoming.InstanceId = FGuid::NewGuid();
		Incoming.SourceType = EAuraItemSourceType::EnemyDrop;
		Incoming.SourceId = TEXT("StackRegression");
		const bool bDifferentGuid = Existing.Item.CanStackWith(Incoming);
		Incoming.ItemLevel = 7;
		const bool bLegacyLevel = Existing.Item.CanStackWith(Incoming);
		Incoming.ItemLevel = 1;
		Incoming.Rarity = Existing.Item.Rarity == EAuraItemRarity::Common
			? EAuraItemRarity::Quality : EAuraItemRarity::Common;
		const bool bLegacyRarity = Existing.Item.CanStackWith(Incoming);
		Incoming.Rarity = Existing.Item.Rarity;
		Incoming.RollSeed = 12345;
		const bool bLegacySeed = Existing.Item.CanStackWith(Incoming);
		Incoming.ItemLevel = 7;
		Incoming.Rarity = Existing.Item.Rarity == EAuraItemRarity::Common
			? EAuraItemRarity::Quality : EAuraItemRarity::Common;
		Inventory->RestoreSnapshot({Existing});
		const bool bPickupMerge = Inventory->TryAddItem(Incoming, Failure, false) == 1
			&& Inventory->GetOccupiedSlotCount() == 1
			&& Inventory->FindEntryAtSlot(0)->Item.Quantity == 2
			&& Inventory->FindEntryAtSlot(0)->Item.InstanceId == Existing.Item.InstanceId;
		Inventory->RestoreSnapshot({Existing});
		AAuraCharacter* TestPawn = Cast<AAuraCharacter>(GetPawn());
		UAuraLootSubsystem* PickupLoot = GetWorld()->GetSubsystem<UAuraLootSubsystem>();
		AAuraWorldItem* WorldItem = TestPawn && PickupLoot
			? PickupLoot->SpawnSpecificPersonalItem(*GetPlayerState<AAuraPlayerState>(), Incoming,
				TestPawn->GetActorLocation() + FVector(1500.f, 0.f, 500.f)) : nullptr;
		bool bWorldPickup = false;
		if (WorldItem)
		{
			WorldItem->SetActorEnableCollision(false);
			WorldItem->SetActorLocation(TestPawn->GetActorLocation() + FVector(100.f, 0.f, 0.f));
			bWorldPickup = WorldItem->TryPickupBy(TestPawn) == 1
				&& Inventory->GetOccupiedSlotCount() == 1
				&& Inventory->FindEntryAtSlot(0)->Item.Quantity == 2;
			if (!WorldItem->IsActorBeingDestroyed()) WorldItem->Destroy();
		}
		FAuraInventoryEntry Source;
		Source.SlotIndex = 1;
		Source.Item = Incoming;
		Source.Item.Quantity = 3;
		Existing.Item.Quantity = Limit - 1;
		Inventory->RestoreSnapshot({Existing, Source});
		Inventory->RequestMoveItem(1, 0, Incoming.InstanceId);
		const bool bDragMerge = Inventory->FindEntryAtSlot(0)->Item.Quantity == Limit
			&& Inventory->FindEntryAtSlot(1) && Inventory->FindEntryAtSlot(1)->Item.Quantity == 2
			&& Inventory->FindEntryAtSlot(0)->Item.InstanceId == Existing.Item.InstanceId;
		Setup.Reset();
		Existing.Item.Quantity = Limit - 2;
		Setup.Add(Existing);
		for (int32 Slot = 1; Slot < Inventory->GetCapacity(); ++Slot)
			Setup.Add(MakeEntry(Slot, Equipment, 1));
		Inventory->RestoreSnapshot(Setup);
		Incoming.Quantity = 2;
		const bool bCapacityAccepts = Inventory->CanAcceptItem(Incoming);
		Incoming.Quantity = 3;
		const bool bCapacityAndPartial = bCapacityAccepts && !Inventory->CanAcceptItem(Incoming)
			&& Inventory->TryAddItem(Incoming, Failure, false) == 2
			&& Inventory->GetOccupiedSlotCount() == Inventory->GetCapacity()
			&& Inventory->FindEntryAtSlot(0)->Item.Quantity == Limit;
		const bool bItemPassed = bDifferentGuid && bLegacyLevel && bLegacyRarity && bLegacySeed
			&& bPickupMerge && bWorldPickup && bDragMerge && bCapacityAndPartial;
		bConsumableStacks &= bItemPassed;
		UE_LOG(LogAuraM25Smoke, Display,
			TEXT("AURA_STACK_REGRESSION Item=%s Passed=%s Max=%d DifferentGuid=%d LegacyLevel=%d LegacyRarity=%d LegacySeed=%d Pickup=%d WorldPickup=%d Drag=%d FullBagPartial=%d"),
			*ItemId.ToString(), bItemPassed ? TEXT("true") : TEXT("false"), Limit,
			bDifferentGuid, bLegacyLevel, bLegacyRarity, bLegacySeed, bPickupMerge, bWorldPickup, bDragMerge, bCapacityAndPartial);
	}
	UAuraItemDefinition* Ore = UAuraItemCatalog::Get().FindItem(TEXT("Material.ArcaneOreShard"));
	UAuraLootSubsystem* Loot = GetWorld()->GetSubsystem<UAuraLootSubsystem>();
	bool bRealLootOre = false;
	if (Loot && Ore)
	{
		const TArray<FAuraItemInstance> Drops = Loot->RollLoot(EAuraLootProfileType::Boss, 5, 7021);
		FAuraItemInstance Reference;
		Reference.Definition = Ore;
		Reference.Rarity = Ore->DefaultRarity;
		Reference.EnsureIdentity();
		for (const FAuraItemInstance& Drop : Drops)
		{
			if (Drop.Definition != Ore) continue;
			bRealLootOre = Reference.CanStackWith(Drop) && Drop.Rarity == Ore->DefaultRarity;
			UE_LOG(LogAuraM25Smoke, Display, TEXT("AURA_STACK_LOOT_ORE Passed=%s DefinitionRarity=%d DropRarity=%d"),
				bRealLootOre ? TEXT("true") : TEXT("false"), static_cast<int32>(Ore->DefaultRarity), static_cast<int32>(Drop.Rarity));
		}
	}
	Inventory->DebugClearInventory();
	const bool bPassed = bEmptyMove && bSwap && bStaleRequestRejected
		&& bPartialMerge && bFullStackRejected && bCompleteMerge
		&& bEquipmentSwaps && bEquipmentAddSeparate && bConsumableStacks && bRealLootOre;
	UE_LOG(LogAuraM25Smoke, Display,
		TEXT("AURA_M25_INVENTORY Passed=%s Empty=%d Swap=%d Stale=%d Partial=%d FullReject=%d FullMerge=%d EquipmentSwap=%d EquipmentSeparate=%d"),
		bPassed ? TEXT("true") : TEXT("false"), bEmptyMove, bSwap,
		bStaleRequestRejected, bPartialMerge, bFullStackRejected, bCompleteMerge,
		bEquipmentSwaps, bEquipmentAddSeparate);
}

void AAuraPlayerController::AuraTestPursuitM25()
{
	AAuraCharacter* TestCharacter = Cast<AAuraCharacter>(GetPawn());
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	if (!HasAuthority() || !IsLocalController() || GetNetMode() != NM_Standalone
		|| !TestCharacter || !GetWorld() || !Run || Run->IsPersistentSaveEnabled())
	{
		UE_LOG(LogAuraM25Smoke, Error, TEXT("AURA_M25_PURSUIT Passed=false Setup=false"));
		return;
	}
	const FTransform Original = TestCharacter->GetActorTransform();
	const float OriginalGravity = TestCharacter->GetCharacterMovement()->GravityScale;
	const EMovementMode OriginalMode = TestCharacter->GetCharacterMovement()->MovementMode;
	const FVector TestHome = Original.GetLocation() + FVector(0.f, 0.f, 2200.f);
	TestCharacter->TeleportTo(TestHome + FVector(500.f, 0.f, 0.f),
		FRotator::ZeroRotator, false, true);
	TestCharacter->GetCharacterMovement()->GravityScale = 0.f;
	TestCharacter->GetCharacterMovement()->DisableMovement();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraEnemy* Enemy = GetWorld()->SpawnActor<AAuraEnemy>(AAuraEnemy::StaticClass(),
		TestHome, FRotator::ZeroRotator, Params);
	AAuraAIController* AI = Enemy ? Cast<AAuraAIController>(Enemy->GetController()) : nullptr;
	if (!Enemy || !AI)
	{
		UE_LOG(LogAuraM25Smoke, Error, TEXT("AURA_M25_PURSUIT Passed=false Spawn=false"));
		if (Enemy) Enemy->Destroy();
		TestCharacter->TeleportTo(Original.GetLocation(), Original.Rotator(), false, true);
		TestCharacter->GetCharacterMovement()->GravityScale = OriginalGravity;
		TestCharacter->GetCharacterMovement()->SetMovementMode(OriginalMode);
		return;
	}
	Enemy->GetCharacterMovement()->DisableMovement();
	const FAuraAICombatBalance& Balance = UAuraCombatBalanceData::Get().AI;
	const float BeyondHome = Balance.LeashRadius + 180.f;
	const float PlayerViewRadius = FMath::Max(Balance.LoseTargetRadius, Balance.LeashRadius);
	const TWeakObjectPtr<AAuraCharacter> WeakPlayer(TestCharacter);
	const TWeakObjectPtr<AAuraEnemy> WeakEnemy(Enemy);
	const TWeakObjectPtr<AAuraAIController> WeakAI(AI);
	FTimerHandle ChaseTimer;
	GetWorldTimerManager().SetTimer(ChaseTimer,
		[this, WeakPlayer, WeakEnemy, WeakAI, TestHome, BeyondHome,
			PlayerViewRadius, Original, OriginalGravity, OriginalMode]()
	{
		AAuraCharacter* CurrentPlayer = WeakPlayer.Get();
		AAuraEnemy* CurrentEnemy = WeakEnemy.Get();
		AAuraAIController* CurrentAI = WeakAI.Get();
		if (!CurrentPlayer || !CurrentEnemy || !CurrentAI) return;
		const bool bInitialChase = CurrentAI->GetCombatTarget() == CurrentPlayer;
		CurrentEnemy->SetActorLocation(TestHome + FVector(BeyondHome, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		CurrentAI->StopMovement();
		CurrentPlayer->SetActorLocation(TestHome + FVector(BeyondHome - 500.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		FTimerHandle BeyondHomeTimer;
		GetWorldTimerManager().SetTimer(BeyondHomeTimer,
			[this, WeakPlayer, WeakEnemy, WeakAI, TestHome, BeyondHome,
				PlayerViewRadius, Original, OriginalGravity, OriginalMode, bInitialChase]()
		{
			AAuraCharacter* CurrentPlayer = WeakPlayer.Get();
			AAuraEnemy* CurrentEnemy = WeakEnemy.Get();
			AAuraAIController* CurrentAI = WeakAI.Get();
			if (!CurrentPlayer || !CurrentEnemy || !CurrentAI) return;
			const bool bPursuesBeyondHome = CurrentAI->GetCombatTarget() == CurrentPlayer
				&& CurrentAI->GetAIState() != EAuraAIState::Returning;
			CurrentPlayer->SetActorLocation(TestHome
				+ FVector(BeyondHome + PlayerViewRadius + 400.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			FTimerHandle LoseTimer;
			GetWorldTimerManager().SetTimer(LoseTimer,
				[this, WeakPlayer, WeakEnemy, WeakAI, Original, OriginalGravity,
					OriginalMode, bInitialChase, bPursuesBeyondHome]()
			{
				AAuraCharacter* CurrentPlayer = WeakPlayer.Get();
				AAuraEnemy* CurrentEnemy = WeakEnemy.Get();
				AAuraAIController* CurrentAI = WeakAI.Get();
				if (!CurrentPlayer || !CurrentEnemy || !CurrentAI) return;
				const bool bReturned = CurrentAI->GetCombatTarget() == nullptr
					&& CurrentAI->GetAIState() == EAuraAIState::Returning;
				CurrentPlayer->SetActorLocation(CurrentEnemy->GetActorLocation()
					+ FVector(500.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
				FTimerHandle ReacquireTimer;
				GetWorldTimerManager().SetTimer(ReacquireTimer,
					[WeakPlayer, WeakEnemy, WeakAI, Original, OriginalGravity,
						OriginalMode, bInitialChase, bPursuesBeyondHome, bReturned]()
				{
					AAuraCharacter* FinalPlayer = WeakPlayer.Get();
					AAuraEnemy* FinalEnemy = WeakEnemy.Get();
					AAuraAIController* FinalAI = WeakAI.Get();
					if (!FinalPlayer || !FinalEnemy || !FinalAI) return;
					const bool bReacquired = FinalAI->GetCombatTarget() == FinalPlayer;
					const bool bPassed = bInitialChase && bPursuesBeyondHome
						&& bReturned && bReacquired;
					UE_LOG(LogAuraM25Smoke, Display,
						TEXT("AURA_M25_PURSUIT Passed=%s Initial=%d BeyondHome=%d Returned=%d Reacquired=%d"),
						bPassed ? TEXT("true") : TEXT("false"), bInitialChase,
						bPursuesBeyondHome, bReturned, bReacquired);
					FinalEnemy->Destroy();
					FinalPlayer->TeleportTo(Original.GetLocation(), Original.Rotator(), false, true);
					FinalPlayer->GetCharacterMovement()->GravityScale = OriginalGravity;
					FinalPlayer->GetCharacterMovement()->SetMovementMode(OriginalMode);
				}, 0.35f, false);
			}, 0.35f, false);
		}, 0.35f, false);
	}, 0.55f, false);
}

void AAuraPlayerController::AuraTestInventoryMoveCoopServerM25()
{
	const UAuraRunSubsystem* Run = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAuraRunSubsystem>() : nullptr;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	UAuraItemDefinition* Potion = UAuraItemCatalog::Get().FindItem(
		TEXT("Consumable.HealthPotion"));
	if (!HasAuthority() || GetNetMode() == NM_Standalone || !Run
		|| Run->IsPersistentSaveEnabled() || !GameState
		|| GameState->PlayerArray.Num() != 2 || !Potion)
	{
		UE_LOG(LogAuraM25Smoke, Error,
			TEXT("AURA_M25_INVENTORY_COOP_SERVER Passed=false Setup=false"));
		return;
	}
	TArray<TWeakObjectPtr<AAuraPlayerState>> States;
	bool bSeeded = true;
	for (APlayerState* RawState : GameState->PlayerArray)
	{
		AAuraPlayerState* State = Cast<AAuraPlayerState>(RawState);
		UAuraInventoryComponent* Inventory = State
			? State->GetInventoryComponent() : nullptr;
		if (!Inventory)
		{
			bSeeded = false;
			break;
		}
		Inventory->DebugClearInventory();
		FAuraItemInstance Item;
		Item.Definition = Potion;
		Item.Quantity = 2;
		Item.EnsureIdentity();
		FString Failure;
		bSeeded &= Inventory->TryAddItem(Item, Failure, false) == 2
			&& Inventory->FindEntryAtSlot(0)
			&& Inventory->FindEntryAtSlot(0)->Item.InstanceId == Item.InstanceId;
		FAuraInventoryEntry Target;
		Target.SlotIndex = 1;
		Target.Item = Item;
		Target.Item.InstanceId = FGuid::NewGuid();
		Target.Item.Quantity = Potion->GetMaxStackSize() - 2;
		Target.Item.ItemLevel = 7;
		Target.Item.Rarity = EAuraItemRarity::Quality;
		Target.Item.RollSeed = 7021;
		FAuraInventoryEntry Source;
		Source.SlotIndex = 0;
		Source.Item = Item;
		Inventory->RestoreSnapshot({Source, Target});
		States.Add(State);
	}
	UE_LOG(LogAuraM25Smoke, Display,
		TEXT("AURA_M25_INVENTORY_COOP_SEEDED Passed=%s Players=%d"),
		bSeeded ? TEXT("true") : TEXT("false"), States.Num());
	FTimerHandle VerifyTimer;
	GetWorldTimerManager().SetTimer(VerifyTimer, [States, bSeeded]()
	{
		bool bMoved = bSeeded && States.Num() == 2;
		for (const TWeakObjectPtr<AAuraPlayerState>& State : States)
		{
			const UAuraInventoryComponent* Inventory = State.IsValid()
				? State->GetInventoryComponent() : nullptr;
			const FAuraInventoryEntry* Moved = Inventory
				? Inventory->FindEntryAtSlot(1) : nullptr;
			bMoved &= Inventory && !Inventory->FindEntryAtSlot(0) && !Inventory->FindEntryAtSlot(5)
				&& Moved && Moved->Item.Definition
				&& Moved->Item.Definition->ItemId == FName(TEXT("Consumable.HealthPotion"))
				&& Moved->Item.Quantity == Moved->Item.Definition->GetMaxStackSize();
		}
		UE_LOG(LogAuraM25Smoke, Display,
			TEXT("AURA_M25_INVENTORY_COOP_SERVER Passed=%s Players=%d"),
			bMoved ? TEXT("true") : TEXT("false"), States.Num());
		UE_LOG(LogAuraM25Smoke, Display, TEXT("AURA_M25_INVENTORY_COOP_DONE"));
	}, 9.f, false);
}

void AAuraPlayerController::AuraTestInventoryMoveCoopClientM25()
{
	UAuraInventoryComponent* Inventory = GetAuraInventory();
	const FAuraInventoryEntry* Source = Inventory
		? Inventory->FindEntryAtSlot(0) : nullptr;
	const FAuraInventoryEntry* Target = Inventory
		? Inventory->FindEntryAtSlot(1) : nullptr;
	if (GetNetMode() != NM_Client || !IsLocalController()
		|| !Source || !Source->Item.InstanceId.IsValid() || !Target)
	{
		UE_LOG(LogAuraM25Smoke, Error,
			TEXT("AURA_M25_INVENTORY_COOP_CLIENT Passed=false Setup=false"));
		return;
	}
	const FGuid ExpectedInstanceId = Source->Item.InstanceId;
	const FGuid ExpectedTargetId = Target->Item.InstanceId;
	Inventory->RequestMoveItem(0, 5, ExpectedInstanceId);
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	FTimerHandle MergeTimer;
	GetWorldTimerManager().SetTimer(MergeTimer, [WeakController, ExpectedInstanceId]()
	{
		if (WeakController.IsValid())
			if (UAuraInventoryComponent* CurrentInventory = WeakController->GetAuraInventory())
				CurrentInventory->RequestMoveItem(5, 1, ExpectedInstanceId);
	}, 0.4f, false);
	FTimerHandle VerifyTimer;
	GetWorldTimerManager().SetTimer(VerifyTimer, [WeakController, ExpectedTargetId]()
	{
		AAuraPlayerController* Controller = WeakController.Get();
		const UAuraInventoryComponent* CurrentInventory = Controller
			? Controller->GetAuraInventory() : nullptr;
		const FAuraInventoryEntry* Moved = CurrentInventory
			? CurrentInventory->FindEntryAtSlot(1) : nullptr;
		bool bForeignInventoryHidden = true;
		const AGameStateBase* GameState = Controller && Controller->GetWorld()
			? Controller->GetWorld()->GetGameState() : nullptr;
		const APlayerState* LocalState = Controller
			? Controller->GetPlayerState<APlayerState>() : nullptr;
		if (GameState)
		{
			for (APlayerState* RawState : GameState->PlayerArray)
			{
				if (RawState == LocalState) continue;
				const AAuraPlayerState* Other = Cast<AAuraPlayerState>(RawState);
				const UAuraInventoryComponent* OtherInventory = Other
					? Other->GetInventoryComponent() : nullptr;
				bForeignInventoryHidden &= !OtherInventory
					|| OtherInventory->GetEntries().IsEmpty();
			}
		}
		const bool bMoved = CurrentInventory && !CurrentInventory->FindEntryAtSlot(0)
			&& !CurrentInventory->FindEntryAtSlot(5)
			&& Moved && Moved->Item.InstanceId == ExpectedTargetId
			&& Moved->Item.Quantity == Moved->Item.Definition->GetMaxStackSize();
		UE_LOG(LogAuraM25Smoke, Display,
			TEXT("AURA_M25_INVENTORY_COOP_CLIENT Passed=%s Moved=%d Private=%d"),
			bMoved && bForeignInventoryHidden ? TEXT("true") : TEXT("false"),
			bMoved, bForeignInventoryHidden);
	}, 1.f, false);
}
