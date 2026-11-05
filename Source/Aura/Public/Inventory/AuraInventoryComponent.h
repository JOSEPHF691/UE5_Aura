// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/AuraItemTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "AuraInventoryComponent.generated.h"

class AAuraWorldItem;
class UAuraInventoryComponent;

USTRUCT(BlueprintType)
struct FAuraInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Inventory")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Inventory")
	FAuraItemInstance Item;
};

USTRUCT()
struct FAuraInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
	{
		return FastArrayDeltaSerialize<FAuraInventoryEntry, FAuraInventoryList>(
			Entries, DeltaParameters, *this);
	}

	void Initialize(UAuraInventoryComponent* InOwner);
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);

	UPROPERTY()
	TArray<FAuraInventoryEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UAuraInventoryComponent> OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FAuraInventoryList> : public TStructOpsTypeTraitsBase2<FAuraInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

DECLARE_MULTICAST_DELEGATE(FOnAuraInventoryChanged);

/** Server-authoritative, owner-only replicated backpack that survives pawn death on PlayerState. */
UCLASS(ClassGroup = (Aura), meta = (BlueprintSpawnableComponent))
class AURA_API UAuraInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAuraInventoryComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const TArray<FAuraInventoryEntry>& GetEntries() const { return Inventory.Entries; }
	int32 GetCapacity() const { return FMath::Max(1, Capacity); }
	int32 GetOccupiedSlotCount() const { return Inventory.Entries.Num(); }
	const FAuraInventoryEntry* FindEntryAtSlot(int32 SlotIndex) const;
	const FAuraInventoryEntry* FindEntryByInstanceId(const FGuid& InstanceId) const;
	FGuid FindFirstConsumable(EAuraConsumableEffect Effect) const;
	int32 GetTotalQuantity(FName ItemId) const;
	bool CanAcceptItem(const FAuraItemInstance& Item) const;
	bool TryRemoveItemByGuid(const FGuid& InstanceId, int32 Quantity,
		FString& OutFailureReason);
	bool TryConsumeItemId(FName ItemId, int32 Quantity, FString& OutFailureReason);
	bool TrySetWeaponEnhancement(const FGuid& InstanceId, int32 NewLevel,
		FString& OutFailureReason);

	/** Authority-only mutation API used by world drops, rewards, and automated validation. */
	int32 TryAddItem(const FAuraItemInstance& IncomingItem, FString& OutFailureReason,
		bool bNotifyOwner = true);
	bool TryUseItem(const FGuid& InstanceId, FString& OutFailureReason);
	bool TryUseFirstConsumable(EAuraConsumableEffect Effect, FString& OutFailureReason);
	/** Archmage-only consumption after a fully validated skill redistribution. */
	bool TryConsumeRespecStone(const FGuid& InstanceId, FString& OutFailureReason);
	bool TryMoveItem(int32 FromSlot, int32 ToSlot, FString& OutFailureReason);
	bool TryDropSlot(int32 SlotIndex, int32 Quantity, FString& OutFailureReason,
		AAuraWorldItem** OutSpawnedItem = nullptr);
	/** Atomic backpack side of an equip/replace operation. */
	bool TryExchangeItemForEquipment(const FGuid& InstanceId,
		const FAuraItemInstance* ReplacedItem, FAuraItemInstance& OutEquippedItem,
		int32& OutSourceSlot, FString& OutFailureReason);
	bool TryReturnEquipmentItem(const FAuraItemInstance& Item, FString& OutFailureReason);
	void DebugClearInventory();
	/** Replace, never append, an already validated authority snapshot. */
	void RestoreSnapshot(const TArray<FAuraInventoryEntry>& Entries);

	/** Safe wrappers: execute directly on authority or route an owning client request to the server. */
	void RequestUseFirstConsumable(EAuraConsumableEffect Effect);
	void RequestUseItem(const FGuid& InstanceId);
	void RequestMoveItem(int32 FromSlot, int32 ToSlot, FGuid ExpectedInstanceId);
	void RequestDropSlot(int32 SlotIndex, int32 Quantity = 1);

	FOnAuraInventoryChanged OnInventoryChanged;

private:
	friend struct FAuraInventoryList;

	void HandleReplicatedInventoryChanged();
	void NotifyOwner(const FString& Message, const FLinearColor& Color);
	FAuraInventoryEntry* FindMutableEntryByInstanceId(const FGuid& InstanceId);
	FAuraInventoryEntry* FindMutableEntryAtSlot(int32 SlotIndex);
	int32 FindFirstFreeSlot() const;
	bool IsSlotValid(int32 SlotIndex) const;
	bool RemoveQuantity(const FGuid& InstanceId, int32 Quantity);

	UFUNCTION(Server, Reliable)
	void ServerUseFirstConsumable(EAuraConsumableEffect Effect);

	UFUNCTION(Server, Reliable)
	void ServerUseItem(FGuid InstanceId);

	UFUNCTION(Server, Reliable)
	void ServerMoveItem(int32 FromSlot, int32 ToSlot, FGuid ExpectedInstanceId);

	UFUNCTION(Server, Reliable)
	void ServerDropSlot(int32 SlotIndex, int32 Quantity);

	UFUNCTION(Client, Reliable)
	void ClientInventoryMessage(const FString& Message, FLinearColor Color);

	UPROPERTY(Replicated)
	FAuraInventoryList Inventory;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Inventory", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Capacity = 24;

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Inventory")
	TSubclassOf<AAuraWorldItem> WorldItemClass;
};
