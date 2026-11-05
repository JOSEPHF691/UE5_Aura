// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "Items/AuraItemTypes.h"
#include "AuraEquipmentComponent.generated.h"

class AAuraCharacter;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FAuraEquippedItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Equipment")
	EAuraEquipmentSlot Slot = EAuraEquipmentSlot::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Equipment")
	FAuraItemInstance Item;
};

DECLARE_MULTICAST_DELEGATE(FOnAuraEquipmentChanged);

/**
 * Server-authoritative four-slot equipment state stored on PlayerState.
 * Each equipped GUID owns exactly one infinite GAS effect handle.
 */
UCLASS(ClassGroup = (Aura), meta = (BlueprintSpawnableComponent))
class AURA_API UAuraEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAuraEquipmentComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	const TArray<FAuraEquippedItem>& GetEquippedItems() const { return EquippedItems; }
	const FAuraItemInstance* GetEquippedItem(EAuraEquipmentSlot Slot) const;
	int32 GetEquippedCount() const { return EquippedItems.Num(); }
	float GetEquippedStatMagnitude(const FGameplayTag& StatTag) const;

	/** Authority-only mutation API used by UI requests and automated tests. */
	bool TryEquipItem(const FGuid& InventoryInstanceId, FString& OutFailureReason);
	bool TryUnequipSlot(EAuraEquipmentSlot Slot, FString& OutFailureReason);
	bool TryEnhanceEquippedWeapon(const FGuid& InstanceId, int32 NewLevel,
		FString& OutFailureReason);
	void RefreshEquipmentEffects();
	void DebugClearEquipment();
	/** Replace, never append, an already validated authority snapshot. */
	void RestoreSnapshot(const TArray<FAuraEquippedItem>& Entries);
	int32 GetActiveEffectCountForDebug() const { return ActiveEffectHandles.Num(); }

	/** Safe wrappers that execute locally on authority or route to the owning server. */
	void RequestEquipItem(const FGuid& InventoryInstanceId);
	void RequestUnequipSlot(EAuraEquipmentSlot Slot);

	FOnAuraEquipmentChanged OnEquipmentChanged;

private:
	UFUNCTION()
	void OnRep_EquippedItems();

	UFUNCTION(Server, Reliable)
	void ServerEquipItem(FGuid InventoryInstanceId);

	UFUNCTION(Server, Reliable)
	void ServerUnequipSlot(EAuraEquipmentSlot Slot);

	UFUNCTION(Client, Reliable)
	void ClientEquipmentMessage(const FString& Message, FLinearColor Color);

	AAuraCharacter* ResolveCharacter() const;
	UAbilitySystemComponent* ResolveAbilitySystem() const;
	FActiveGameplayEffectHandle ApplyItemEffect(
		const FAuraItemInstance& Item, UAbilitySystemComponent& AbilitySystem,
		FString& OutFailureReason) const;
	void RemoveItemEffect(const FGuid& InstanceId, UAbilitySystemComponent* AbilitySystem);
	void AdjustVitalsAfterMaxChange(UAbilitySystemComponent& AbilitySystem,
		float OldHealth, float OldMaxHealth, float OldMana, float OldMaxMana) const;
	void MarkEquipmentChanged();
	void NotifyOwner(const FString& Message, const FLinearColor& Color);
	int32 FindEquippedIndex(EAuraEquipmentSlot Slot) const;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedItems)
	TArray<FAuraEquippedItem> EquippedItems;

	TMap<FGuid, FActiveGameplayEffectHandle> ActiveEffectHandles;
	TWeakObjectPtr<UAbilitySystemComponent> AppliedAbilitySystem;
};

