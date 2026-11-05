// Copyright yosep

#include "Equipment/AuraEquipmentComponent.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Effects/AuraAbilityGameplayEffects.h"
#include "AbilitySystemComponent.h"
#include "Character/AuraCharacter.h"
#include "Data/AuraItemDefinition.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/AuraInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/AuraPlayerState.h"
#include "UI/AuraHUD.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraEquipment, Log, All);

namespace
{
	const TArray<FGameplayTag>& EquipmentStatTags()
	{
		static const TArray<FGameplayTag> Tags = {
			FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.MaxHealth")),
			FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.MaxMana")),
			FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Strength")),
			FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Intelligence")),
			FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.Armor")),
			FGameplayTag::RequestGameplayTag(TEXT("Item.Stat.MoveSpeed"))
		};
		return Tags;
	}
}

UAuraEquipmentComponent::UAuraEquipmentComponent()
{
	SetIsReplicatedByDefault(true);
}

void UAuraEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority()) RefreshEquipmentEffects();
	UE_LOG(LogAuraEquipment, Display,
		TEXT("Equipment ready. Owner=%s Authority=%s Equipped=%d"),
		*GetNameSafe(GetOwner()), GetOwner() && GetOwner()->HasAuthority()
			? TEXT("true") : TEXT("false"), EquippedItems.Num());
}

void UAuraEquipmentComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UAuraEquipmentComponent, EquippedItems, COND_OwnerOnly);
}

int32 UAuraEquipmentComponent::FindEquippedIndex(const EAuraEquipmentSlot Slot) const
{
	return EquippedItems.IndexOfByPredicate([Slot](const FAuraEquippedItem& Entry)
	{
		return Entry.Slot == Slot;
	});
}

const FAuraItemInstance* UAuraEquipmentComponent::GetEquippedItem(
	const EAuraEquipmentSlot Slot) const
{
	const int32 Index = FindEquippedIndex(Slot);
	return EquippedItems.IsValidIndex(Index) ? &EquippedItems[Index].Item : nullptr;
}

float UAuraEquipmentComponent::GetEquippedStatMagnitude(
	const FGameplayTag& StatTag) const
{
	float Result = 0.f;
	for (const FAuraEquippedItem& Equipped : EquippedItems)
	{
		Result += AuraItems::GetStatMagnitude(
			AuraItems::BuildFinalStatModifiers(Equipped.Item), StatTag);
	}
	return Result;
}

bool UAuraEquipmentComponent::TryEquipItem(
	const FGuid& InventoryInstanceId, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	AAuraCharacter* Character = ResolveCharacter();
	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	AAuraPlayerState* PlayerState = Cast<AAuraPlayerState>(GetOwner());
	UAuraInventoryComponent* Inventory = PlayerState
		? PlayerState->GetInventoryComponent() : nullptr;
	if (!Character || Character->IsDead() || !AbilitySystem || !Inventory)
	{
		OutFailureReason = TEXT("角色必须存活");
		return false;
	}
	const FAuraInventoryEntry* InventoryEntry =
		Inventory->FindEntryByInstanceId(InventoryInstanceId);
	if (!InventoryEntry || !InventoryEntry->Item.Definition
		|| InventoryEntry->Item.Definition->ItemType != EAuraItemType::Equipment)
	{
		OutFailureReason = TEXT("找不到装备");
		return false;
	}
	const EAuraEquipmentSlot Slot = InventoryEntry->Item.Definition->EquipmentSlot;
	if (Slot == EAuraEquipmentSlot::None)
	{
		OutFailureReason = TEXT("装备栏位无效");
		return false;
	}

	const int32 ExistingIndex = FindEquippedIndex(Slot);
	const FAuraItemInstance OldItem = EquippedItems.IsValidIndex(ExistingIndex)
		? EquippedItems[ExistingIndex].Item : FAuraItemInstance();
	const float OldHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float OldMaxHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	const float OldMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float OldMaxMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute());

	FString EffectFailure;
	const FActiveGameplayEffectHandle NewHandle = ApplyItemEffect(
		InventoryEntry->Item, *AbilitySystem, EffectFailure);
	if (!NewHandle.IsValid())
	{
		OutFailureReason = EffectFailure.IsEmpty()
			? TEXT("装备属性应用失败") : EffectFailure;
		return false;
	}

	FAuraItemInstance NewItem;
	int32 SourceSlot = INDEX_NONE;
	if (!Inventory->TryExchangeItemForEquipment(InventoryInstanceId,
		OldItem.IsValid() ? &OldItem : nullptr, NewItem, SourceSlot, OutFailureReason))
	{
		AbilitySystem->RemoveActiveGameplayEffect(NewHandle);
		return false;
	}

	if (OldItem.IsValid()) RemoveItemEffect(OldItem.InstanceId, AbilitySystem);
	if (EquippedItems.IsValidIndex(ExistingIndex))
	{
		EquippedItems[ExistingIndex].Item = NewItem;
		EquippedItems[ExistingIndex].Slot = Slot;
	}
	else
	{
		FAuraEquippedItem& Added = EquippedItems.AddDefaulted_GetRef();
		Added.Slot = Slot;
		Added.Item = NewItem;
	}
	ActiveEffectHandles.Add(NewItem.InstanceId, NewHandle);
	AppliedAbilitySystem = AbilitySystem;
	AdjustVitalsAfterMaxChange(
		*AbilitySystem, OldHealth, OldMaxHealth, OldMana, OldMaxMana);
	MarkEquipmentChanged();
	NotifyOwner(FString::Printf(TEXT("已装备 %s  [%s]"),
		*NewItem.Definition->DisplayName.ToString(), AuraItems::GetEquipmentSlotName(Slot)),
		AuraItems::GetRarityColor(NewItem.Rarity));
	UE_LOG(LogAuraEquipment, Display,
		TEXT("Equipment changed. Owner=%s Slot=%s New=%s Old=%s SourceSlot=%d Handle=%s"),
		*GetNameSafe(GetOwner()), AuraItems::GetEquipmentSlotName(Slot),
		*NewItem.Definition->ItemId.ToString(), OldItem.Definition
			? *OldItem.Definition->ItemId.ToString() : TEXT("None"), SourceSlot,
		NewHandle.IsValid() ? TEXT("valid") : TEXT("invalid"));
	return true;
}

bool UAuraEquipmentComponent::TryUnequipSlot(
	const EAuraEquipmentSlot Slot, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	AAuraCharacter* Character = ResolveCharacter();
	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	AAuraPlayerState* PlayerState = Cast<AAuraPlayerState>(GetOwner());
	UAuraInventoryComponent* Inventory = PlayerState
		? PlayerState->GetInventoryComponent() : nullptr;
	if (!Character || Character->IsDead() || !AbilitySystem || !Inventory)
	{
		OutFailureReason = TEXT("角色必须存活");
		return false;
	}
	const int32 ExistingIndex = FindEquippedIndex(Slot);
	if (!EquippedItems.IsValidIndex(ExistingIndex))
	{
		OutFailureReason = TEXT("装备栏位为空");
		return false;
	}
	const FAuraItemInstance OldItem = EquippedItems[ExistingIndex].Item;
	if (!Inventory->TryReturnEquipmentItem(OldItem, OutFailureReason)) return false;

	const float OldHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float OldMaxHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	const float OldMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float OldMaxMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute());
	RemoveItemEffect(OldItem.InstanceId, AbilitySystem);
	EquippedItems.RemoveAt(ExistingIndex);
	AdjustVitalsAfterMaxChange(
		*AbilitySystem, OldHealth, OldMaxHealth, OldMana, OldMaxMana);
	MarkEquipmentChanged();
	NotifyOwner(FString::Printf(TEXT("已卸下 %s"),
		*OldItem.Definition->DisplayName.ToString()), FLinearColor(0.86f, 0.88f, 0.94f));
	UE_LOG(LogAuraEquipment, Display,
		TEXT("Equipment removed. Owner=%s Slot=%s Item=%s"),
		*GetNameSafe(GetOwner()), AuraItems::GetEquipmentSlotName(Slot),
		OldItem.Definition ? *OldItem.Definition->ItemId.ToString() : TEXT("None"));
	return true;
}

bool UAuraEquipmentComponent::TryEnhanceEquippedWeapon(const FGuid& InstanceId,
	const int32 NewLevel, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutFailureReason = TEXT("需要服务器授权");
		return false;
	}
	const int32 Index = EquippedItems.IndexOfByPredicate(
		[&InstanceId](const FAuraEquippedItem& Entry)
		{
			return Entry.Item.InstanceId == InstanceId;
		});
	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!EquippedItems.IsValidIndex(Index) || !AbilitySystem
		|| EquippedItems[Index].Item.InstanceId != InstanceId
		|| NewLevel != EquippedItems[Index].Item.EnhancementLevel + 1 || NewLevel > 3)
	{
		OutFailureReason = TEXT("已装备物品不可用或已达强化上限");
		return false;
	}
	const float OldHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float OldMaxHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	const float OldMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float OldMaxMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute());
	FAuraItemInstance Enhanced = EquippedItems[Index].Item;
	Enhanced.EnhancementLevel = NewLevel;
	const FActiveGameplayEffectHandle NewHandle = ApplyItemEffect(
		Enhanced, *AbilitySystem, OutFailureReason);
	if (!NewHandle.IsValid()) return false;
	RemoveItemEffect(InstanceId, AbilitySystem);
	EquippedItems[Index].Item = Enhanced;
	ActiveEffectHandles.Add(InstanceId, NewHandle);
	AppliedAbilitySystem = AbilitySystem;
	AdjustVitalsAfterMaxChange(*AbilitySystem,
		OldHealth, OldMaxHealth, OldMana, OldMaxMana);
	MarkEquipmentChanged();
	return true;
}

void UAuraEquipmentComponent::RefreshEquipmentEffects()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!AbilitySystem) return;

	const float OldHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute());
	const float OldMaxHealth = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute());
	const float OldMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute());
	const float OldMaxMana = AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute());
	bool bChanged = false;
	if (AppliedAbilitySystem.IsValid() && AppliedAbilitySystem.Get() != AbilitySystem)
	{
		for (const TPair<FGuid, FActiveGameplayEffectHandle>& Pair : ActiveEffectHandles)
		{
			AppliedAbilitySystem->RemoveActiveGameplayEffect(Pair.Value);
		}
		ActiveEffectHandles.Reset();
		bChanged = true;
	}
	AppliedAbilitySystem = AbilitySystem;

	TSet<FGuid> ValidInstances;
	for (const FAuraEquippedItem& Equipped : EquippedItems)
	{
		if (Equipped.Item.IsValid()) ValidInstances.Add(Equipped.Item.InstanceId);
	}
	for (auto It = ActiveEffectHandles.CreateIterator(); It; ++It)
	{
		if (!ValidInstances.Contains(It.Key())
			|| !AbilitySystem->GetActiveGameplayEffect(It.Value()))
		{
			if (AbilitySystem->GetActiveGameplayEffect(It.Value()))
			{
				AbilitySystem->RemoveActiveGameplayEffect(It.Value());
			}
			It.RemoveCurrent();
			bChanged = true;
		}
	}

	for (const FAuraEquippedItem& Equipped : EquippedItems)
	{
		if (!Equipped.Item.IsValid() || ActiveEffectHandles.Contains(Equipped.Item.InstanceId))
		{
			continue;
		}
		FString Failure;
		const FActiveGameplayEffectHandle Handle = ApplyItemEffect(
			Equipped.Item, *AbilitySystem, Failure);
		if (Handle.IsValid())
		{
			ActiveEffectHandles.Add(Equipped.Item.InstanceId, Handle);
			bChanged = true;
		}
		else
		{
			UE_LOG(LogAuraEquipment, Error,
				TEXT("Could not reconcile equipped item. Owner=%s Item=%s Reason=%s"),
				*GetNameSafe(GetOwner()),
				Equipped.Item.Definition
					? *Equipped.Item.Definition->ItemId.ToString() : TEXT("None"),
				Failure.IsEmpty() ? TEXT("Unknown") : *Failure);
		}
	}
	if (bChanged)
	{
		AdjustVitalsAfterMaxChange(
			*AbilitySystem, OldHealth, OldMaxHealth, OldMana, OldMaxMana);
	}
	UE_LOG(LogAuraEquipment, Display,
		TEXT("Equipment effects reconciled. Owner=%s Equipped=%d ActiveEffects=%d Changed=%s ASC=%s"),
		*GetNameSafe(GetOwner()), EquippedItems.Num(), ActiveEffectHandles.Num(),
		bChanged ? TEXT("true") : TEXT("false"), *GetNameSafe(AbilitySystem));
}

void UAuraEquipmentComponent::DebugClearEquipment()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	const float OldHealth = AbilitySystem
		? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetHealthAttribute()) : 0.f;
	const float OldMaxHealth = AbilitySystem
		? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxHealthAttribute()) : 0.f;
	const float OldMana = AbilitySystem
		? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetManaAttribute()) : 0.f;
	const float OldMaxMana = AbilitySystem
		? AbilitySystem->GetNumericAttribute(UAuraAttributeSet::GetMaxManaAttribute()) : 0.f;
	if (AbilitySystem)
	{
		for (const TPair<FGuid, FActiveGameplayEffectHandle>& Pair : ActiveEffectHandles)
		{
			AbilitySystem->RemoveActiveGameplayEffect(Pair.Value);
		}
	}
	ActiveEffectHandles.Reset();
	EquippedItems.Reset();
	if (AbilitySystem)
	{
		AdjustVitalsAfterMaxChange(
			*AbilitySystem, OldHealth, OldMaxHealth, OldMana, OldMaxMana);
	}
	MarkEquipmentChanged();
	UE_LOG(LogAuraEquipment, Display,
		TEXT("Equipment cleared for validation. Owner=%s"), *GetNameSafe(GetOwner()));
}

void UAuraEquipmentComponent::RestoreSnapshot(const TArray<FAuraEquippedItem>& Entries)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	DebugClearEquipment();
	EquippedItems = Entries;
	MarkEquipmentChanged();
	RefreshEquipmentEffects();
}

void UAuraEquipmentComponent::RequestEquipItem(const FGuid& InventoryInstanceId)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FString Failure;
		if (!TryEquipItem(InventoryInstanceId, Failure))
		{
			NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
		}
	}
	else ServerEquipItem(InventoryInstanceId);
}

void UAuraEquipmentComponent::RequestUnequipSlot(const EAuraEquipmentSlot Slot)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FString Failure;
		if (!TryUnequipSlot(Slot, Failure))
		{
			NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
		}
	}
	else ServerUnequipSlot(Slot);
}

void UAuraEquipmentComponent::OnRep_EquippedItems()
{
	UE_LOG(LogAuraEquipment, Display,
		TEXT("Owner equipment replication applied. Owner=%s Equipped=%d"),
		*GetNameSafe(GetOwner()), EquippedItems.Num());
	OnEquipmentChanged.Broadcast();
}

void UAuraEquipmentComponent::ServerEquipItem_Implementation(
	const FGuid InventoryInstanceId)
{
	FString Failure;
	if (!TryEquipItem(InventoryInstanceId, Failure))
	{
		NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
	}
}

void UAuraEquipmentComponent::ServerUnequipSlot_Implementation(
	const EAuraEquipmentSlot Slot)
{
	FString Failure;
	if (!TryUnequipSlot(Slot, Failure))
	{
		NotifyOwner(Failure, FLinearColor(1.f, 0.35f, 0.16f));
	}
}

void UAuraEquipmentComponent::ClientEquipmentMessage_Implementation(
	const FString& Message, const FLinearColor Color)
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	APlayerController* Controller = PlayerState ? PlayerState->GetPlayerController() : nullptr;
	if (AAuraHUD* HUD = Controller ? Cast<AAuraHUD>(Controller->GetHUD()) : nullptr)
	{
		HUD->ShowStatusMessage(Message, Color, 1.8f);
	}
}

AAuraCharacter* UAuraEquipmentComponent::ResolveCharacter() const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	return PlayerState ? Cast<AAuraCharacter>(PlayerState->GetPawn()) : nullptr;
}

UAbilitySystemComponent* UAuraEquipmentComponent::ResolveAbilitySystem() const
{
	const AAuraCharacter* Character = ResolveCharacter();
	return Character ? Character->GetAbilitySystemComponent() : nullptr;
}

FActiveGameplayEffectHandle UAuraEquipmentComponent::ApplyItemEffect(
	const FAuraItemInstance& Item, UAbilitySystemComponent& AbilitySystem,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!Item.IsValid() || !Item.Definition
		|| Item.Definition->ItemType != EAuraItemType::Equipment)
	{
		OutFailureReason = TEXT("无效装备");
		return FActiveGameplayEffectHandle();
	}
	FGameplayEffectContextHandle Context = AbilitySystem.MakeEffectContext();
	Context.AddSourceObject(Item.Definition);
	FGameplayEffectSpecHandle Spec = AbilitySystem.MakeOutgoingSpec(
		UAuraEquipmentAttributeEffect::StaticClass(), Item.ItemLevel, Context);
	if (!Spec.IsValid())
	{
		OutFailureReason = TEXT("装备效果创建失败");
		return FActiveGameplayEffectHandle();
	}
	const TArray<FAuraItemStatModifier> Modifiers =
		AuraItems::BuildFinalStatModifiers(Item);
	for (const FGameplayTag& StatTag : EquipmentStatTags())
	{
		Spec.Data->SetSetByCallerMagnitude(
			StatTag, AuraItems::GetStatMagnitude(Modifiers, StatTag));
	}
	return AbilitySystem.ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void UAuraEquipmentComponent::RemoveItemEffect(
	const FGuid& InstanceId, UAbilitySystemComponent* AbilitySystem)
{
	if (FActiveGameplayEffectHandle* Handle = ActiveEffectHandles.Find(InstanceId))
	{
		if (AbilitySystem) AbilitySystem->RemoveActiveGameplayEffect(*Handle);
		ActiveEffectHandles.Remove(InstanceId);
	}
}

void UAuraEquipmentComponent::AdjustVitalsAfterMaxChange(
	UAbilitySystemComponent& AbilitySystem, const float OldHealth,
	const float OldMaxHealth, const float OldMana, const float /*OldMaxMana*/) const
{
	const float NewMaxHealth = AbilitySystem.GetNumericAttribute(
		UAuraAttributeSet::GetMaxHealthAttribute());
	const float NewMaxMana = AbilitySystem.GetNumericAttribute(
		UAuraAttributeSet::GetMaxManaAttribute());
	const float NewHealth = FMath::Clamp(
		OldHealth + FMath::Max(0.f, NewMaxHealth - OldMaxHealth), 0.f, NewMaxHealth);
	// Increasing MaxMana is capacity growth, not immediate current-mana recovery.
	const float NewMana = FMath::Clamp(OldMana, 0.f, NewMaxMana);
	AbilitySystem.SetNumericAttributeBase(UAuraAttributeSet::GetHealthAttribute(), NewHealth);
	AbilitySystem.SetNumericAttributeBase(UAuraAttributeSet::GetManaAttribute(), NewMana);
}

void UAuraEquipmentComponent::MarkEquipmentChanged()
{
	if (GetOwner()) GetOwner()->ForceNetUpdate();
	OnEquipmentChanged.Broadcast();
}

void UAuraEquipmentComponent::NotifyOwner(
	const FString& Message, const FLinearColor& Color)
{
	if (!Message.IsEmpty()) ClientEquipmentMessage(Message, Color);
}
