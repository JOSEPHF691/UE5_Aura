// Copyright yosep

#include "Items/AuraItemTypes.h"

#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"

namespace
{
	FGameplayTag ItemStatTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
	}

	void AddModifier(TArray<FAuraItemStatModifier>& Modifiers,
		const FGameplayTag& Tag, const float Magnitude)
	{
		if (!Tag.IsValid() || FMath::IsNearlyZero(Magnitude)) return;
		if (FAuraItemStatModifier* Existing = Modifiers.FindByPredicate(
			[&Tag](const FAuraItemStatModifier& Modifier)
			{
				return Modifier.StatTag == Tag;
			}))
		{
			Existing->Magnitude += Magnitude;
			return;
		}
		FAuraItemStatModifier& Added = Modifiers.AddDefaulted_GetRef();
		Added.StatTag = Tag;
		Added.Magnitude = Magnitude;
	}

	TArray<FGameplayTag> GetAffixPool(const EAuraEquipmentSlot Slot)
	{
		switch (Slot)
		{
		case EAuraEquipmentSlot::Weapon:
			return { ItemStatTag(TEXT("Item.Stat.Intelligence")),
				ItemStatTag(TEXT("Item.Stat.MaxMana")),
				ItemStatTag(TEXT("Item.Stat.Strength")),
				ItemStatTag(TEXT("Item.Stat.Armor")) };
		case EAuraEquipmentSlot::Armor:
			return { ItemStatTag(TEXT("Item.Stat.MaxHealth")),
				ItemStatTag(TEXT("Item.Stat.Armor")),
				ItemStatTag(TEXT("Item.Stat.Strength")),
				ItemStatTag(TEXT("Item.Stat.MoveSpeed")) };
		case EAuraEquipmentSlot::Head:
			return { ItemStatTag(TEXT("Item.Stat.MaxMana")),
				ItemStatTag(TEXT("Item.Stat.Intelligence")),
				ItemStatTag(TEXT("Item.Stat.MaxHealth")),
				ItemStatTag(TEXT("Item.Stat.Armor")) };
		case EAuraEquipmentSlot::Shoes:
			return { ItemStatTag(TEXT("Item.Stat.MoveSpeed")),
				ItemStatTag(TEXT("Item.Stat.Armor")),
				ItemStatTag(TEXT("Item.Stat.MaxHealth")),
				ItemStatTag(TEXT("Item.Stat.MaxMana")) };
		default:
			return {};
		}
	}

	float GetAffixMagnitude(const FGameplayTag& Tag, const int32 ItemLevel,
		FRandomStream& Stream, const float SignatureMultiplier)
	{
		const float Level = static_cast<float>(FMath::Clamp(ItemLevel, 1, 100));
		float BaseMagnitude = 0.f;
		if (Tag == ItemStatTag(TEXT("Item.Stat.MaxHealth"))) BaseMagnitude = 5.f + 2.f * Level;
		else if (Tag == ItemStatTag(TEXT("Item.Stat.MaxMana"))) BaseMagnitude = 2.f + Level;
		else if (Tag == ItemStatTag(TEXT("Item.Stat.Strength"))
			|| Tag == ItemStatTag(TEXT("Item.Stat.Intelligence")))
		{
			BaseMagnitude = 0.8f + 0.35f * Level;
		}
		else if (Tag == ItemStatTag(TEXT("Item.Stat.Armor"))) BaseMagnitude = 0.7f + 0.3f * Level;
		else if (Tag == ItemStatTag(TEXT("Item.Stat.MoveSpeed"))) BaseMagnitude = 8.f + 2.f * Level;
		const float RolledMagnitude = BaseMagnitude
			* Stream.FRandRange(0.9f, 1.1f) * SignatureMultiplier;
		return FMath::RoundToFloat(RolledMagnitude * 10.f) / 10.f;
	}
}

bool FAuraItemInstance::IsValid() const
{
	return Definition != nullptr && Quantity > 0;
}

bool FAuraItemInstance::CanStackWith(const FAuraItemInstance& Other) const
{
	// Stackable effects and material rules come from the shared definition.
	// Equipment-only metadata can differ in legacy saves or between drop sources.
	// Instance GUIDs identify stacks for requests; they do not define compatibility.
	return Definition && Definition == Other.Definition
		&& Definition->ItemType != EAuraItemType::Equipment
		&& Definition->GetMaxStackSize() > 1;
}

void FAuraItemInstance::EnsureIdentity()
{
	if (!InstanceId.IsValid()) InstanceId = FGuid::NewGuid();
	Quantity = FMath::Max(1, Quantity);
	ItemLevel = FMath::Max(1, ItemLevel);
	EnhancementLevel = FMath::Clamp(EnhancementLevel, 0, 3);
}

const TCHAR* AuraItems::GetRarityName(const EAuraItemRarity Rarity)
{
	switch (Rarity)
	{
	case EAuraItemRarity::Quality: return TEXT("优质");
	case EAuraItemRarity::Fine: return TEXT("精良");
	case EAuraItemRarity::Epic: return TEXT("史诗");
	case EAuraItemRarity::Legendary: return TEXT("传说");
	default: return TEXT("普通");
	}
}

FLinearColor AuraItems::GetRarityColor(const EAuraItemRarity Rarity)
{
	switch (Rarity)
	{
	case EAuraItemRarity::Quality: return FLinearColor(0.22f, 0.88f, 0.28f, 1.f);
	case EAuraItemRarity::Fine: return FLinearColor(0.18f, 0.48f, 1.f, 1.f);
	case EAuraItemRarity::Epic: return FLinearColor(0.68f, 0.27f, 1.f, 1.f);
	case EAuraItemRarity::Legendary: return FLinearColor(1.f, 0.52f, 0.08f, 1.f);
	default: return FLinearColor(0.88f, 0.9f, 0.94f, 1.f);
	}
}

const TCHAR* AuraItems::GetEquipmentSlotName(const EAuraEquipmentSlot Slot)
{
	switch (Slot)
	{
	case EAuraEquipmentSlot::Weapon: return TEXT("武器");
	case EAuraEquipmentSlot::Armor: return TEXT("防具");
	case EAuraEquipmentSlot::Head: return TEXT("头饰");
	case EAuraEquipmentSlot::Shoes: return TEXT("鞋子");
	default: return TEXT("无");
	}
}

const TCHAR* AuraItems::GetStatName(const FGameplayTag& StatTag)
{
	if (StatTag == ItemStatTag(TEXT("Item.Stat.MaxHealth"))) return TEXT("生命上限");
	if (StatTag == ItemStatTag(TEXT("Item.Stat.MaxMana"))) return TEXT("法力上限");
	if (StatTag == ItemStatTag(TEXT("Item.Stat.Strength"))) return TEXT("力量");
	if (StatTag == ItemStatTag(TEXT("Item.Stat.Intelligence"))) return TEXT("智力");
	if (StatTag == ItemStatTag(TEXT("Item.Stat.Armor"))) return TEXT("护甲");
	if (StatTag == ItemStatTag(TEXT("Item.Stat.MoveSpeed"))) return TEXT("移速");
	return TEXT("未知属性");
}

const TCHAR* AuraItems::GetSourceName(const EAuraItemSourceType SourceType)
{
	switch (SourceType)
	{
	case EAuraItemSourceType::EnemyDrop: return TEXT("怪物掉落");
	case EAuraItemSourceType::Treasure: return TEXT("宝箱");
	case EAuraItemSourceType::PlayerDrop: return TEXT("玩家丢弃");
	case EAuraItemSourceType::Debug: return TEXT("测试物品");
	case EAuraItemSourceType::Merchant: return TEXT("商人");
	case EAuraItemSourceType::Crafted: return TEXT("锻造大师");
	default: return TEXT("未知来源");
	}
}

float AuraItems::GetRarityMultiplier(const EAuraItemRarity Rarity)
{
	switch (Rarity)
	{
	case EAuraItemRarity::Quality: return 1.08f;
	case EAuraItemRarity::Fine: return 1.18f;
	case EAuraItemRarity::Epic: return 1.32f;
	case EAuraItemRarity::Legendary: return 1.5f;
	default: return 1.f;
	}
}

int32 AuraItems::GetRarityAffixCount(const EAuraItemRarity Rarity)
{
	switch (Rarity)
	{
	case EAuraItemRarity::Quality: return 1;
	case EAuraItemRarity::Fine: return 2;
	case EAuraItemRarity::Epic: return 3;
	case EAuraItemRarity::Legendary: return 4;
	default: return 0;
	}
}

float AuraItems::GetItemLevelMultiplier(const int32 ItemLevel)
{
	return 1.f + 0.05f * static_cast<float>(FMath::Clamp(ItemLevel, 1, 100) - 1);
}

void AuraItems::RollEquipmentModifiers(FAuraItemInstance& Item, const int32 Seed)
{
	Item.EnsureIdentity();
	Item.RolledModifiers.Reset();
	Item.RollSeed = 0;
	if (!Item.Definition || Item.Definition->ItemType != EAuraItemType::Equipment
		|| Item.Definition->EquipmentSlot == EAuraEquipmentSlot::None)
	{
		return;
	}

	const int32 AffixCount = GetRarityAffixCount(Item.Rarity);
	if (AffixCount <= 0) return;
	const uint32 GuidHash = GetTypeHash(Item.InstanceId);
	Item.RollSeed = Seed != 0 ? Seed : static_cast<int32>(HashCombine(
		GuidHash, HashCombine(GetTypeHash(Item.ItemLevel), GetTypeHash(Item.Rarity))));
	if (Item.RollSeed == 0) Item.RollSeed = 1;
	FRandomStream Stream(Item.RollSeed);
	TArray<FGameplayTag> Pool = GetAffixPool(Item.Definition->EquipmentSlot);

	// Legendary gear receives its slot-defining stat first as the fourth,
	// stronger signature affix. The remaining affixes still use the same
	// no-duplicate whitelist as every other rarity.
	if (Item.Rarity == EAuraItemRarity::Legendary && !Pool.IsEmpty())
	{
		const FGameplayTag SignatureTag = Pool[0];
		FAuraItemStatModifier& Signature = Item.RolledModifiers.AddDefaulted_GetRef();
		Signature.StatTag = SignatureTag;
		Signature.Magnitude = GetAffixMagnitude(SignatureTag, Item.ItemLevel, Stream, 1.35f);
		Pool.RemoveAt(0);
	}

	while (Item.RolledModifiers.Num() < AffixCount && !Pool.IsEmpty())
	{
		const int32 PickIndex = Stream.RandRange(0, Pool.Num() - 1);
		const FGameplayTag PickedTag = Pool[PickIndex];
		Pool.RemoveAtSwap(PickIndex);
		FAuraItemStatModifier& Modifier = Item.RolledModifiers.AddDefaulted_GetRef();
		Modifier.StatTag = PickedTag;
		Modifier.Magnitude = GetAffixMagnitude(PickedTag, Item.ItemLevel, Stream, 1.f);
	}
}

TArray<FAuraItemStatModifier> AuraItems::BuildFinalStatModifiers(
	const FAuraItemInstance& Item)
{
	TArray<FAuraItemStatModifier> Result;
	if (!Item.Definition || Item.Definition->ItemType != EAuraItemType::Equipment) return Result;
	const float ForgeBonus = Item.Definition->EquipmentSlot == EAuraEquipmentSlot::Weapon
		? UAuraCombatBalanceData::Get().CampEconomy.WeaponBaseStatBonusPerLevel
		: UAuraCombatBalanceData::Get().CampEconomy.ArmorBaseStatBonusPerLevel;
	const float ForgeMultiplier = 1.f + ForgeBonus
		* FMath::Clamp(Item.EnhancementLevel, 0, 3);
	const float BaseMultiplier = GetRarityMultiplier(Item.Rarity)
		* GetItemLevelMultiplier(Item.ItemLevel) * ForgeMultiplier;
	for (const FAuraItemStatModifier& Modifier : Item.Definition->BaseStatModifiers)
	{
		AddModifier(Result, Modifier.StatTag, Modifier.Magnitude * BaseMultiplier);
	}
	for (const FAuraItemStatModifier& Modifier : Item.RolledModifiers)
	{
		AddModifier(Result, Modifier.StatTag, Modifier.Magnitude);
	}
	for (FAuraItemStatModifier& Modifier : Result)
	{
		Modifier.Magnitude = FMath::RoundToFloat(Modifier.Magnitude * 10.f) / 10.f;
	}
	return Result;
}

float AuraItems::GetStatMagnitude(
	const TArray<FAuraItemStatModifier>& Modifiers, const FGameplayTag& StatTag)
{
	float Result = 0.f;
	for (const FAuraItemStatModifier& Modifier : Modifiers)
	{
		if (Modifier.StatTag == StatTag) Result += Modifier.Magnitude;
	}
	return Result;
}
