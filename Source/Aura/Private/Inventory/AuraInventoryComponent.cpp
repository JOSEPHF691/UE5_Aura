// Copyright yosep

#include "Inventory/AuraInventoryComponent.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Data/AuraItemDefinition.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "Items/AuraWorldItem.h"
#include "Net/UnrealNetwork.h"
#include "Player/AuraPlayerController.h"
#include "UI/AuraHUD.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraInventory, Log, All);

void FAuraInventoryList::Initialize(UAuraInventoryComponent* InOwner)
{
	OwnerComponent = InOwner;
}

void FAuraInventoryList::PostReplicatedAdd(
	const TArrayView<int32>& AddedIndices, const int32 FinalSize)
{
	if (OwnerComponent) OwnerComponent->HandleReplicatedInventoryChanged();
}

void FAuraInventoryList::PostReplicatedChange(
	const TArrayView<int32>& ChangedIndices, const int32 FinalSize)
{
	if (OwnerComponent) OwnerComponent->HandleReplicatedInventoryChanged();
}

void FAuraInventoryList::PreReplicatedRemove(
	const TArrayView<int32>& RemovedIndices, const int32 FinalSize)
{
	if (OwnerComponent) OwnerComponent->HandleReplicatedInventoryChanged();
}

UAuraInventoryComponent::UAuraInventoryComponent()
{
	SetIsReplicatedByDefault(true);
	Inventory.Initialize(this);
	WorldItemClass = AAuraWorldItem::StaticClass();
}

void UAuraInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	Inventory.Initialize(this);
	UE_LOG(LogAuraInventory, Display,
		TEXT("Inventory ready. Owner=%s Authority=%s Capacity=%d"),
		*GetNameSafe(GetOwner()), GetOwner() && GetOwner()->HasAuthority()
			? TEXT("true") : TEXT("false"), GetCapacity());
}

void UAuraInventoryComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UAuraInventoryComponent, Inventory, COND_OwnerOnly);
}

const FAuraInventoryEntry* UAuraInventoryComponent::FindEntryAtSlot(const int32 SlotIndex) const
{
	return Inventory.Entries.FindByPredicate([SlotIndex](const FAuraInventoryEntry& Entry)
	{
		return Entry.SlotIndex == SlotIndex;
	});
}

const FAuraInventoryEntry* UAuraInventoryComponent::FindEntryByInstanceId(
	const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid()) return nullptr;
	return Inventory.Entries.FindByPredicate([&InstanceId](const FAuraInventoryEntry& Entry)
	{
		return Entry.Item.InstanceId == InstanceId;
	});
}

FAuraInventoryEntry* UAuraInventoryComponent::FindMutableEntryByInstanceId(
	const FGuid& InstanceId)
{
	if (!InstanceId.IsValid()) return nullptr;
	return Inventory.Entries.FindByPredicate([&InstanceId](const FAuraInventoryEntry& Entry)
	{
		return Entry.Item.InstanceId == InstanceId;
	});
}

FAuraInventoryEntry* UAuraInventoryComponent::FindMutableEntryAtSlot(const int32 SlotIndex)
{
	return Inventory.Entries.FindByPredicate([SlotIndex](const FAuraInventoryEntry& Entry)
	{
		return Entry.SlotIndex == SlotIndex;
	});
}

FGuid UAuraInventoryComponent::FindFirstConsumable(const EAuraConsumableEffect Effect) const
{
	const FAuraInventoryEntry* BestEntry = nullptr;
	for (const FAuraInventoryEntry& Entry : Inventory.Entries)
	{
		const UAuraItemDefinition* Definition = Entry.Item.Definition;
		if (!Definition || Definition->ItemType != EAuraItemType::Consumable
			|| Definition->ConsumableEffect != Effect)
		{
			continue;
		}
		if (!BestEntry || Entry.SlotIndex < BestEntry->SlotIndex) BestEntry = &Entry;
	}
	return BestEntry ? BestEntry->Item.InstanceId : FGuid();
}

int32 UAuraInventoryComponent::GetTotalQuantity(const FName ItemId) const
{
	int32 Total = 0;
	for (const FAuraInventoryEntry& Entry : Inventory.Entries)
	{
		if (Entry.Item.Definition && Entry.Item.Definition->ItemId == ItemId)
		{
			Total += Entry.Item.Quantity;
		}
	}
	return Total;
}

bool UAuraInventoryComponent::CanAcceptItem(const FAuraItemInstance& Item) const
{
	if (!Item.IsValid()) return false;
	int32 Remaining = Item.Quantity;
	const int32 MaxStack = Item.Definition->ItemType == EAuraItemType::Equipment
		? 1 : Item.Definition->GetMaxStackSize();
	if (MaxStack > 1)
	{
		for (const FAuraInventoryEntry& Entry : Inventory.Entries)
		{
			if (Entry.Item.CanStackWith(Item))
			{
				Remaining -= FMath::Max(0, MaxStack - Entry.Item.Quantity);
				if (Remaining <= 0) return true;
			}
		}
	}
	const int32 FreeSlots = GetCapacity() - Inventory.Entries.Num();
	return Remaining <= FreeSlots * MaxStack;
}

bool UAuraInventoryComponent::TryRemoveItemByGuid(const FGuid& InstanceId,
	const int32 Quantity, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	const FAuraInventoryEntry* Entry = FindEntryByInstanceId(InstanceId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Entry || Quantity <= 0
		|| Entry->Item.Quantity < Quantity)
	{
		OutFailureReason = TEXT("物品或数量不可用");
		return false;
	}
	if (!RemoveQuantity(InstanceId, Quantity))
	{
		OutFailureReason = TEXT("无法扣除物品数量");
		return false;
	}
	return true;
}

bool UAuraInventoryComponent::TryConsumeItemId(const FName ItemId,
	const int32 Quantity, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority() || ItemId.IsNone()
		|| Quantity <= 0 || GetTotalQuantity(ItemId) < Quantity)
	{
		OutFailureReason = TEXT("制作材料不足");
		return false;
	}
	int32 Remaining = Quantity;
	TArray<TPair<FGuid, int32>> ToConsume;
	for (const FAuraInventoryEntry& Entry : Inventory.Entries)
	{
		if (!Entry.Item.Definition || Entry.Item.Definition->ItemId != ItemId) continue;
		const int32 Taken = FMath::Min(Remaining, Entry.Item.Quantity);
		ToConsume.Emplace(Entry.Item.InstanceId, Taken);
		Remaining -= Taken;
		if (Remaining == 0) break;
	}
	if (Remaining != 0) return false;
	for (const TPair<FGuid, int32>& Part : ToConsume)
	{
		if (!RemoveQuantity(Part.Key, Part.Value))
		{
			UE_LOG(LogAuraInventory, Error,
				TEXT("Material consumption invariant failed. Owner=%s Item=%s"),
				*GetNameSafe(GetOwner()), *ItemId.ToString());
			return false;
		}
	}
	return true;
}

bool UAuraInventoryComponent::TrySetWeaponEnhancement(const FGuid& InstanceId,
	const int32 NewLevel, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	FAuraInventoryEntry* Entry = FindMutableEntryByInstanceId(InstanceId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Entry || !Entry->Item.Definition
		|| Entry->Item.Definition->EquipmentSlot == EAuraEquipmentSlot::None
		|| Entry->Item.Definition->ItemType != EAuraItemType::Equipment
		|| NewLevel != Entry->Item.EnhancementLevel + 1 || NewLevel > 3)
	{
		OutFailureReason = TEXT("装备不可用或已达强化上限");
		return false;
	}
	Entry->Item.EnhancementLevel = NewLevel;
	Inventory.MarkItemDirty(*Entry);
	GetOwner()->ForceNetUpdate();
	HandleReplicatedInventoryChanged();
	return true;
}

int32 UAuraInventoryComponent::TryAddItem(
	const FAuraItemInstance& IncomingItem, FString& OutFailureReason,
	const bool bNotifyOwner)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return 0;
	}
	if (!IncomingItem.IsValid())
	{
		OutFailureReason = TEXT("无效物品");
		return 0;
	}

	FAuraItemInstance WorkingItem = IncomingItem;
	WorkingItem.EnsureIdentity();
	const int32 RequestedQuantity = WorkingItem.Quantity;
	const int32 MaxStack = WorkingItem.Definition->ItemType == EAuraItemType::Equipment
		? 1 : WorkingItem.Definition->GetMaxStackSize();
	int32 Remaining = RequestedQuantity;

	if (MaxStack > 1)
	{
		for (FAuraInventoryEntry& Entry : Inventory.Entries)
		{
			if (!Entry.Item.CanStackWith(WorkingItem) || Entry.Item.Quantity >= MaxStack) continue;
			const int32 Transfer = FMath::Min(Remaining, MaxStack - Entry.Item.Quantity);
			Entry.Item.Quantity += Transfer;
			Remaining -= Transfer;
			Inventory.MarkItemDirty(Entry);
			if (Remaining <= 0) break;
		}
	}

	while (Remaining > 0 && Inventory.Entries.Num() < GetCapacity())
	{
		const int32 FreeSlot = FindFirstFreeSlot();
		if (FreeSlot == INDEX_NONE) break;

		FAuraInventoryEntry& NewEntry = Inventory.Entries.AddDefaulted_GetRef();
		NewEntry.SlotIndex = FreeSlot;
		NewEntry.Item = WorkingItem;
		NewEntry.Item.Quantity = FMath::Min(Remaining, MaxStack);
		if (FindEntryByInstanceId(NewEntry.Item.InstanceId)
			&& &NewEntry != FindEntryByInstanceId(NewEntry.Item.InstanceId))
		{
			NewEntry.Item.InstanceId = FGuid::NewGuid();
		}
		NewEntry.Item.EnsureIdentity();
		Remaining -= NewEntry.Item.Quantity;
		Inventory.MarkItemDirty(NewEntry);
		WorkingItem.InstanceId = FGuid::NewGuid();
	}

	const int32 AddedQuantity = RequestedQuantity - Remaining;
	if (AddedQuantity > 0)
	{
		GetOwner()->ForceNetUpdate();
		HandleReplicatedInventoryChanged();
		if (bNotifyOwner)
		{
			NotifyOwner(FString::Printf(TEXT("拾取 %s%s"),
				*WorkingItem.Definition->DisplayName.ToString(),
				AddedQuantity > 1 ? *FString::Printf(TEXT(" x%d"), AddedQuantity) : TEXT("")),
				AuraItems::GetRarityColor(WorkingItem.Rarity));
		}
	}
	if (Remaining > 0)
	{
		OutFailureReason = AddedQuantity > 0
			? FString::Printf(TEXT("Backpack full; %d remained"), Remaining)
			: TEXT("Backpack full");
	}

	UE_LOG(LogAuraInventory, Display,
		TEXT("Inventory add. Owner=%s Item=%s Requested=%d Added=%d Remaining=%d Slots=%d/%d"),
		*GetNameSafe(GetOwner()), *WorkingItem.Definition->ItemId.ToString(), RequestedQuantity,
		AddedQuantity, Remaining, Inventory.Entries.Num(), GetCapacity());
	return AddedQuantity;
}

bool UAuraInventoryComponent::TryExchangeItemForEquipment(
	const FGuid& InstanceId, const FAuraItemInstance* ReplacedItem,
	FAuraItemInstance& OutEquippedItem, int32& OutSourceSlot,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	OutEquippedItem = FAuraItemInstance();
	OutSourceSlot = INDEX_NONE;
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	const int32 EntryIndex = Inventory.Entries.IndexOfByPredicate(
		[&InstanceId](const FAuraInventoryEntry& Entry)
		{
			return Entry.Item.InstanceId == InstanceId;
		});
	if (EntryIndex == INDEX_NONE)
	{
		OutFailureReason = TEXT("找不到物品");
		return false;
	}
	FAuraInventoryEntry& Entry = Inventory.Entries[EntryIndex];
	if (!Entry.Item.Definition || Entry.Item.Definition->ItemType != EAuraItemType::Equipment
		|| Entry.Item.Definition->EquipmentSlot == EAuraEquipmentSlot::None
		|| Entry.Item.Quantity != 1)
	{
		OutFailureReason = TEXT("该物品无法装备");
		return false;
	}
	if (ReplacedItem && (!ReplacedItem->IsValid() || !ReplacedItem->Definition
		|| ReplacedItem->Definition->ItemType != EAuraItemType::Equipment
		|| ReplacedItem->Quantity != 1))
	{
		OutFailureReason = TEXT("当前装备无效");
		return false;
	}

	OutEquippedItem = Entry.Item;
	OutSourceSlot = Entry.SlotIndex;
	if (ReplacedItem)
	{
		Entry.Item = *ReplacedItem;
		Inventory.MarkItemDirty(Entry);
	}
	else
	{
		Inventory.Entries.RemoveAt(EntryIndex);
		Inventory.MarkArrayDirty();
	}
	GetOwner()->ForceNetUpdate();
	HandleReplicatedInventoryChanged();
	UE_LOG(LogAuraInventory, Display,
		TEXT("Inventory equipment exchange. Owner=%s Equipped=%s Replaced=%s SourceSlot=%d"),
		*GetNameSafe(GetOwner()),
		OutEquippedItem.Definition ? *OutEquippedItem.Definition->ItemId.ToString() : TEXT("None"),
		ReplacedItem && ReplacedItem->Definition
			? *ReplacedItem->Definition->ItemId.ToString() : TEXT("None"), OutSourceSlot);
	return true;
}

bool UAuraInventoryComponent::TryReturnEquipmentItem(
	const FAuraItemInstance& Item, FString& OutFailureReason)
{
	if (!Item.IsValid() || !Item.Definition
		|| Item.Definition->ItemType != EAuraItemType::Equipment || Item.Quantity != 1)
	{
		OutFailureReason = TEXT("已装备物品无效");
		return false;
	}
	return TryAddItem(Item, OutFailureReason, false) == 1;
}

bool UAuraInventoryComponent::TryUseItem(
	const FGuid& InstanceId, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	FAuraInventoryEntry* Entry = FindMutableEntryByInstanceId(InstanceId);
	if (!Entry || !Entry->Item.Definition)
	{
		OutFailureReason = TEXT("找不到物品");
		return false;
	}
	UAuraItemDefinition* Definition = Entry->Item.Definition;
	if (Definition->ItemType != EAuraItemType::Consumable
		|| Definition->ConsumableEffect == EAuraConsumableEffect::None)
	{
		OutFailureReason = TEXT("该物品无法使用");
		return false;
	}
	if (Definition->ConsumableEffect == EAuraConsumableEffect::Respec)
	{
		OutFailureReason = TEXT("请在大魔法师处使用洗点石");
		return false;
	}

	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	AAuraCharacter* Character = PlayerState ? Cast<AAuraCharacter>(PlayerState->GetPawn()) : nullptr;
	UAbilitySystemComponent* AbilitySystem = Character
		? Character->GetAbilitySystemComponent() : nullptr;
	if (!Character || Character->IsDead() || !AbilitySystem)
	{
		OutFailureReason = TEXT("角色必须存活");
		return false;
	}

	const bool bHealth = Definition->ConsumableEffect == EAuraConsumableEffect::Health;
	const FGameplayAttribute Attribute = bHealth
		? UAuraAttributeSet::GetHealthAttribute() : UAuraAttributeSet::GetManaAttribute();
	const FGameplayAttribute MaxAttribute = bHealth
		? UAuraAttributeSet::GetMaxHealthAttribute() : UAuraAttributeSet::GetMaxManaAttribute();
	const float OldValue = AbilitySystem->GetNumericAttribute(Attribute);
	const float MaxValue = AbilitySystem->GetNumericAttribute(MaxAttribute);
	if (OldValue >= MaxValue - KINDA_SMALL_NUMBER)
	{
		OutFailureReason = bHealth ? TEXT("生命已满") : TEXT("法力已满");
		return false;
	}

	const TSubclassOf<UGameplayEffect> RestoreEffectClass = bHealth
		? UAuraHealthPickupEffect::StaticClass() : UAuraManaPickupEffect::StaticClass();
	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	Context.AddSourceObject(Definition);
	FGameplayEffectSpecHandle Spec = AbilitySystem->MakeOutgoingSpec(RestoreEffectClass, 1.f, Context);
	if (!Spec.IsValid())
	{
		OutFailureReason = TEXT("恢复效果创建失败");
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Restore")), Definition->RestoreAmount);
	AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	const float NewValue = AbilitySystem->GetNumericAttribute(Attribute);
	if (NewValue <= OldValue + KINDA_SMALL_NUMBER)
	{
		OutFailureReason = TEXT("物品没有产生效果");
		return false;
	}

	const FString UsedName = Definition->DisplayName.ToString();
	if (!RemoveQuantity(InstanceId, 1))
	{
		OutFailureReason = TEXT("物品消耗失败");
		return false;
	}
	if (bHealth) Character->MulticastHealthRestoreEffect();
	else Character->MulticastManaRestoreEffect();
	NotifyOwner(FString::Printf(TEXT("使用 %s  +%.0f %s"), *UsedName,
		NewValue - OldValue, bHealth ? TEXT("生命") : TEXT("法力")),
		bHealth ? FLinearColor(0.24f, 1.f, 0.3f) : FLinearColor(0.22f, 0.58f, 1.f));
	UE_LOG(LogAuraInventory, Display,
		TEXT("Inventory item used. Owner=%s Item=%s Old=%.1f New=%.1f Remaining=%d"),
		*GetNameSafe(GetOwner()), *Definition->ItemId.ToString(), OldValue, NewValue,
		GetTotalQuantity(Definition->ItemId));
	return true;
}

bool UAuraInventoryComponent::TryUseFirstConsumable(
	const EAuraConsumableEffect Effect, FString& OutFailureReason)
{
	const FGuid InstanceId = FindFirstConsumable(Effect);
	if (!InstanceId.IsValid())
	{
		OutFailureReason = Effect == EAuraConsumableEffect::Health
			? TEXT("没有生命药水")
			: (Effect == EAuraConsumableEffect::Mana
				? TEXT("没有法力药水") : TEXT("没有洗点石"));
		return false;
	}
	return TryUseItem(InstanceId, OutFailureReason);
}

bool UAuraInventoryComponent::TryConsumeRespecStone(
	const FGuid& InstanceId, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	const FAuraInventoryEntry* Entry = FindEntryByInstanceId(InstanceId);
	if (!Entry || !Entry->Item.Definition
		|| Entry->Item.Definition->ConsumableEffect != EAuraConsumableEffect::Respec)
	{
		OutFailureReason = TEXT("背包里没有洗点石");
		return false;
	}
	if (!RemoveQuantity(InstanceId, 1))
	{
		OutFailureReason = TEXT("洗点石消耗失败");
		return false;
	}
	return true;
}

bool UAuraInventoryComponent::TryMoveItem(
	const int32 FromSlot, const int32 ToSlot, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	if (!IsSlotValid(FromSlot) || !IsSlotValid(ToSlot) || FromSlot == ToSlot)
	{
		OutFailureReason = TEXT("无效的背包移动操作");
		return false;
	}
	FAuraInventoryEntry* FromEntry = FindMutableEntryAtSlot(FromSlot);
	if (!FromEntry)
	{
		OutFailureReason = TEXT("来源格子为空");
		return false;
	}
	FAuraInventoryEntry* ToEntry = FindMutableEntryAtSlot(ToSlot);
	// Keep the actual pair that reached the server; a rule regression test cannot
	// identify which data blocked a player's earlier drag operation.
	const bool bCanStack = ToEntry && FromEntry->Item.CanStackWith(ToEntry->Item);
	const TCHAR* MoveReason = !ToEntry ? TEXT("EmptyTarget")
		: (FromEntry->Item.Definition != ToEntry->Item.Definition ? TEXT("DifferentDefinition")
			: (FromEntry->Item.Definition
				&& FromEntry->Item.Definition->ItemType == EAuraItemType::Equipment
				? TEXT("EquipmentCannotStack")
				: (bCanStack ? (ToEntry->Item.Quantity >= ToEntry->Item.Definition->GetMaxStackSize()
					? TEXT("TargetFull") : TEXT("Merge")) : TEXT("StackingDisabled"))));
	UE_LOG(LogAuraInventory, Display,
		TEXT("Inventory move decision. Owner=%s From=%d To=%d Reason=%s"),
		*GetNameSafe(GetOwner()), FromSlot, ToSlot, MoveReason);
	const auto LogMoveItem = [](const TCHAR* Side, const FAuraItemInstance& Item)
	{
		UE_LOG(LogAuraInventory, Display,
			TEXT("Inventory move item. Side=%s Definition=%s ItemId=%s GUID=%s Quantity=%d Max=%d Rarity=%d Level=%d Enhancement=%d Seed=%d Modifiers=%d Source=%d"),
			Side, *GetPathNameSafe(Item.Definition.Get()),
			Item.Definition ? *Item.Definition->ItemId.ToString() : TEXT("None"),
			*Item.InstanceId.ToString(), Item.Quantity,
			Item.Definition ? Item.Definition->GetMaxStackSize() : 0,
			static_cast<int32>(Item.Rarity), Item.ItemLevel, Item.EnhancementLevel,
			Item.RollSeed, Item.RolledModifiers.Num(), static_cast<int32>(Item.SourceType));
	};
	LogMoveItem(TEXT("From"), FromEntry->Item);
	if (ToEntry) LogMoveItem(TEXT("To"), ToEntry->Item);
	if (bCanStack)
	{
		const int32 Space = ToEntry->Item.Definition->GetMaxStackSize()
			- ToEntry->Item.Quantity;
		const int32 Transfer = FMath::Min(FromEntry->Item.Quantity, Space);
		if (Transfer <= 0)
		{
			OutFailureReason = TEXT("目标堆叠已满");
			return false;
		}
		ToEntry->Item.Quantity += Transfer;
		Inventory.MarkItemDirty(*ToEntry);
		FromEntry->Item.Quantity -= Transfer;
		if (FromEntry->Item.Quantity == 0)
		{
			const int32 SourceIndex = Inventory.Entries.IndexOfByPredicate(
				[FromSlot](const FAuraInventoryEntry& Entry)
				{
					return Entry.SlotIndex == FromSlot;
				});
			Inventory.Entries.RemoveAt(SourceIndex);
			Inventory.MarkArrayDirty();
		}
		else Inventory.MarkItemDirty(*FromEntry);
		GetOwner()->ForceNetUpdate();
		HandleReplicatedInventoryChanged();
		UE_LOG(LogAuraInventory, Display,
			TEXT("Inventory stack. Owner=%s From=%d To=%d Transferred=%d"),
			*GetNameSafe(GetOwner()), FromSlot, ToSlot, Transfer);
		return true;
	}
	if (ToEntry)
	{
		ToEntry->SlotIndex = FromSlot;
		Inventory.MarkItemDirty(*ToEntry);
	}
	FromEntry->SlotIndex = ToSlot;
	Inventory.MarkItemDirty(*FromEntry);
	GetOwner()->ForceNetUpdate();
	HandleReplicatedInventoryChanged();
	UE_LOG(LogAuraInventory, Display, TEXT("Inventory move. Owner=%s From=%d To=%d Swap=%s"),
		*GetNameSafe(GetOwner()), FromSlot, ToSlot, ToEntry ? TEXT("true") : TEXT("false"));
	return true;
}

bool UAuraInventoryComponent::TryDropSlot(
	const int32 SlotIndex, const int32 Quantity, FString& OutFailureReason,
	AAuraWorldItem** OutSpawnedItem)
{
	OutFailureReason.Reset();
	if (OutSpawnedItem) *OutSpawnedItem = nullptr;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	FAuraInventoryEntry* Entry = FindMutableEntryAtSlot(SlotIndex);
	if (!Entry || !Entry->Item.Definition)
	{
		OutFailureReason = TEXT("格子为空");
		return false;
	}
	if (!Entry->Item.Definition->bDroppable || Entry->Item.Definition->ItemType == EAuraItemType::Quest)
	{
		OutFailureReason = TEXT("该物品不能丢弃");
		return false;
	}

	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	AAuraCharacter* Character = PlayerState ? Cast<AAuraCharacter>(PlayerState->GetPawn()) : nullptr;
	if (!Character || Character->IsDead())
	{
		OutFailureReason = TEXT("角色必须存活");
		return false;
	}

	const int32 DropQuantity = FMath::Clamp(Quantity, 1, Entry->Item.Quantity);
	FAuraItemInstance DroppedItem = Entry->Item;
	DroppedItem.Quantity = DropQuantity;
	DroppedItem.SourceType = EAuraItemSourceType::PlayerDrop;
	DroppedItem.SourceId = PlayerState ? PlayerState->GetFName() : NAME_None;
	if (DropQuantity < Entry->Item.Quantity) DroppedItem.InstanceId = FGuid::NewGuid();
	const FVector DesiredSpawnLocation = Character->GetActorLocation()
		+ Character->GetActorForwardVector().GetSafeNormal2D() * 180.f
		+ FVector(0.f, 0.f, 35.f);
	const FVector SpawnLocation = AAuraWorldItem::FindGroundedSpawnLocation(
		GetWorld(), DesiredSpawnLocation, Character);
	const FTransform SpawnTransform(Character->GetActorRotation(), SpawnLocation);
	UClass* SpawnClass = WorldItemClass.Get() ? WorldItemClass.Get() : AAuraWorldItem::StaticClass();
	AAuraWorldItem* WorldItem = GetWorld()->SpawnActorDeferred<AAuraWorldItem>(
		SpawnClass, SpawnTransform,
		Character, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!WorldItem)
	{
		OutFailureReason = TEXT("地面物品生成失败");
		return false;
	}
	// Player-dropped equipment is shared immediately. A hidden reservation window
	// would make the nearby list advertise an item that another player cannot yet take.
	WorldItem->InitializeItem(DroppedItem, PlayerState, 0.f, false, 120.f);
	WorldItem->FinishSpawning(SpawnTransform);
	if (!RemoveQuantity(Entry->Item.InstanceId, DropQuantity))
	{
		WorldItem->Destroy();
		OutFailureReason = TEXT("丢弃物品扣除失败");
		return false;
	}
	if (OutSpawnedItem) *OutSpawnedItem = WorldItem;
	NotifyOwner(FString::Printf(TEXT("丢弃 %s%s"),
		*DroppedItem.Definition->DisplayName.ToString(),
		DropQuantity > 1 ? *FString::Printf(TEXT(" x%d"), DropQuantity) : TEXT("")),
		AuraItems::GetRarityColor(DroppedItem.Rarity));
	UE_LOG(LogAuraInventory, Display,
		TEXT("Inventory drop. Owner=%s Item=%s Quantity=%d Slot=%d Actor=%s"),
		*GetNameSafe(GetOwner()), *DroppedItem.Definition->ItemId.ToString(), DropQuantity,
		SlotIndex, *GetNameSafe(WorldItem));
	return true;
}

void UAuraInventoryComponent::DebugClearInventory()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Inventory.Entries.Reset();
	Inventory.MarkArrayDirty();
	GetOwner()->ForceNetUpdate();
	HandleReplicatedInventoryChanged();
	UE_LOG(LogAuraInventory, Display, TEXT("Inventory cleared for validation. Owner=%s"),
		*GetNameSafe(GetOwner()));
}

void UAuraInventoryComponent::RestoreSnapshot(const TArray<FAuraInventoryEntry>& Entries)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Inventory.Entries.Reset();
	Inventory.MarkArrayDirty();
	for (const FAuraInventoryEntry& Source : Entries)
	{
		FAuraInventoryEntry& Added = Inventory.Entries.AddDefaulted_GetRef();
		Added.SlotIndex = Source.SlotIndex;
		Added.Item = Source.Item;
		Inventory.MarkItemDirty(Added);
	}
	GetOwner()->ForceNetUpdate();
	HandleReplicatedInventoryChanged();
}

void UAuraInventoryComponent::RequestUseFirstConsumable(const EAuraConsumableEffect Effect)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FString Failure;
		if (!TryUseFirstConsumable(Effect, Failure)) NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
	}
	else ServerUseFirstConsumable(Effect);
}

void UAuraInventoryComponent::RequestUseItem(const FGuid& InstanceId)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FString Failure;
		if (!TryUseItem(InstanceId, Failure))
		{
			NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
		}
	}
	else ServerUseItem(InstanceId);
}

void UAuraInventoryComponent::RequestMoveItem(
	const int32 FromSlot, const int32 ToSlot, const FGuid ExpectedInstanceId)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const FAuraInventoryEntry* Source = FindEntryAtSlot(FromSlot);
		if (!Source || Source->Item.InstanceId != ExpectedInstanceId)
		{
			NotifyOwner(TEXT("物品位置已变化，请重试"), FLinearColor(1.f, 0.35f, 0.16f));
			return;
		}
		FString Failure;
		if (!TryMoveItem(FromSlot, ToSlot, Failure)) NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
	}
	else ServerMoveItem(FromSlot, ToSlot, ExpectedInstanceId);
}

void UAuraInventoryComponent::RequestDropSlot(const int32 SlotIndex, const int32 Quantity)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FString Failure;
		if (!TryDropSlot(SlotIndex, Quantity, Failure)) NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
	}
	else ServerDropSlot(SlotIndex, Quantity);
}

void UAuraInventoryComponent::ServerUseFirstConsumable_Implementation(
	const EAuraConsumableEffect Effect)
{
	FString Failure;
	if (!TryUseFirstConsumable(Effect, Failure)) NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
}

void UAuraInventoryComponent::ServerUseItem_Implementation(const FGuid InstanceId)
{
	FString Failure;
	if (!TryUseItem(InstanceId, Failure))
	{
		NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
	}
}

void UAuraInventoryComponent::ServerMoveItem_Implementation(
	const int32 FromSlot, const int32 ToSlot, const FGuid ExpectedInstanceId)
{
	const FAuraInventoryEntry* Source = FindEntryAtSlot(FromSlot);
	if (!Source || Source->Item.InstanceId != ExpectedInstanceId)
	{
		NotifyOwner(TEXT("物品位置已变化，请重试"), FLinearColor(1.f, 0.35f, 0.16f));
		return;
	}
	FString Failure;
	if (!TryMoveItem(FromSlot, ToSlot, Failure)) NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
}

void UAuraInventoryComponent::ServerDropSlot_Implementation(const int32 SlotIndex, const int32 Quantity)
{
	FString Failure;
	if (!TryDropSlot(SlotIndex, Quantity, Failure)) NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
}

void UAuraInventoryComponent::ClientInventoryMessage_Implementation(
	const FString& Message, const FLinearColor Color)
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	APlayerController* Controller = PlayerState ? PlayerState->GetPlayerController() : nullptr;
	if (AAuraHUD* HUD = Controller ? Cast<AAuraHUD>(Controller->GetHUD()) : nullptr)
	{
		FString DisplayMessage = Message;
		if (DisplayMessage.StartsWith(TEXT("Backpack full")))
		{
			DisplayMessage = TEXT("背包已满");
		}
		HUD->ShowStatusMessage(DisplayMessage, Color, 1.6f);
	}
}

void UAuraInventoryComponent::HandleReplicatedInventoryChanged()
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		UE_LOG(LogAuraInventory, Display,
			TEXT("Owner inventory replication applied. Owner=%s Slots=%d"),
			*GetNameSafe(GetOwner()), GetOccupiedSlotCount());
	}
	OnInventoryChanged.Broadcast();
}

void UAuraInventoryComponent::NotifyOwner(const FString& Message, const FLinearColor& Color)
{
	if (!Message.IsEmpty()) ClientInventoryMessage(Message, Color);
}

int32 UAuraInventoryComponent::FindFirstFreeSlot() const
{
	for (int32 Slot = 0; Slot < GetCapacity(); ++Slot)
	{
		if (!FindEntryAtSlot(Slot)) return Slot;
	}
	return INDEX_NONE;
}

bool UAuraInventoryComponent::IsSlotValid(const int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < GetCapacity();
}

bool UAuraInventoryComponent::RemoveQuantity(const FGuid& InstanceId, const int32 Quantity)
{
	if (Quantity <= 0) return false;
	for (int32 Index = 0; Index < Inventory.Entries.Num(); ++Index)
	{
		FAuraInventoryEntry& Entry = Inventory.Entries[Index];
		if (Entry.Item.InstanceId != InstanceId || Entry.Item.Quantity < Quantity) continue;
		Entry.Item.Quantity -= Quantity;
		if (Entry.Item.Quantity <= 0)
		{
			Inventory.Entries.RemoveAt(Index);
			Inventory.MarkArrayDirty();
		}
		else
		{
			Inventory.MarkItemDirty(Entry);
		}
		if (GetOwner()) GetOwner()->ForceNetUpdate();
		HandleReplicatedInventoryChanged();
		return true;
	}
	return false;
}
