// Copyright yosep

#include "Loot/AuraLootSubsystem.h"

#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Data/AuraCombatBalanceData.h"
#include "Data/AuraItemDefinition.h"
#include "Game/AuraGameState.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Items/AuraWorldItem.h"
#include "Player/AuraPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraLoot, Log, All);

TArray<FAuraItemInstance> UAuraLootSubsystem::RollLoot(
	const EAuraLootProfileType ProfileType, const int32 PlayerLevel,
	const int32 Seed) const
{
	TArray<FAuraItemInstance> Result;
	const FAuraLootProfile& Profile = UAuraLootProfileData::Get().GetProfile(ProfileType);
	if (Profile.GuaranteedRolls <= 0 && Profile.BonusRolls <= 0
		&& Profile.GuaranteedArcaneOre <= 0 && Profile.BonusArcaneOreChance <= 0.f)
	{
		return Result;
	}

	const int32 EffectiveSeed = Seed != 0 ? Seed : FMath::Max(1, FMath::Rand());
	FRandomStream Stream(EffectiveSeed);
	int32 RollCount = FMath::Max(0, Profile.GuaranteedRolls);
	for (int32 Index = 0; Index < FMath::Max(0, Profile.BonusRolls); ++Index)
	{
		if (Stream.FRand() <= FMath::Clamp(Profile.BonusRollChance, 0.f, 1.f)) ++RollCount;
	}
	for (int32 Index = 0; Index < RollCount; ++Index)
	{
		FAuraItemInstance Item = RollSingleItem(Profile, PlayerLevel, Stream);
		if (Item.IsValid()) Result.Add(MoveTemp(Item));
	}
	const AAuraGameState* Quest = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	const bool bAbyss = Quest && Quest->IsAbyssTrialActive();
	const float OreMultiplier = bAbyss
		? UAuraCombatBalanceData::Get().AbyssTrial.OreMultiplier : 1.f;
	int32 OreCount = Profile.GuaranteedArcaneOre
		+ (Profile.BonusArcaneOreChance > 0.f
			&& Stream.FRand() < FMath::Min(1.f,
				Profile.BonusArcaneOreChance * OreMultiplier) ? 1 : 0);
	if (bAbyss)
	{
		const float ExtraChance = FMath::Clamp(OreMultiplier - 1.f, 0.f, 1.f);
		for (int32 Index = 0; Index < Profile.GuaranteedArcaneOre; ++Index)
		{
			if (Stream.FRand() < ExtraChance) ++OreCount;
		}
	}
	if (OreCount > 0)
	{
		if (UAuraItemDefinition* Ore = UAuraItemCatalog::Get().FindItem(
			TEXT("Material.ArcaneOreShard")))
		{
			FAuraItemInstance Material;
			Material.Definition = Ore;
			Material.Quantity = OreCount;
			Material.Rarity = Ore->DefaultRarity;
			Material.EnsureIdentity();
			Result.Add(MoveTemp(Material));
		}
	}
	return Result;
}

EAuraItemRarity UAuraLootSubsystem::RollRarity(
	const FAuraLootProfile& Profile, FRandomStream& Stream) const
{
	float TotalWeight = 0.f;
	for (const FAuraLootRarityWeight& Entry : Profile.RarityWeights)
	{
		TotalWeight += FMath::Max(0.f, Entry.Weight);
	}
	if (TotalWeight <= 0.f) return EAuraItemRarity::Common;

	float Roll = Stream.FRandRange(0.f, TotalWeight);
	for (const FAuraLootRarityWeight& Entry : Profile.RarityWeights)
	{
		Roll -= FMath::Max(0.f, Entry.Weight);
		if (Roll <= 0.f) return Entry.Rarity;
	}
	return Profile.RarityWeights.Last().Rarity;
}

FAuraItemInstance UAuraLootSubsystem::RollSingleItem(
	const FAuraLootProfile& Profile, const int32 PlayerLevel,
	FRandomStream& Stream) const
{
	const UAuraItemCatalog& Catalog = UAuraItemCatalog::Get();
	FAuraItemInstance Result;
	const bool bEquipment = !Profile.EquipmentItemIds.IsEmpty()
		&& Stream.FRand() <= FMath::Clamp(Profile.EquipmentChance, 0.f, 1.f);
	if (bEquipment)
	{
		const int32 DefinitionIndex = Stream.RandRange(
			0, Profile.EquipmentItemIds.Num() - 1);
		Result.Definition = Catalog.FindItem(Profile.EquipmentItemIds[DefinitionIndex]);
		Result.Quantity = 1;
		Result.Rarity = RollRarity(Profile, Stream);
		const int32 MinOffset = FMath::Min(
			Profile.MinItemLevelOffset, Profile.MaxItemLevelOffset);
		const int32 MaxOffset = FMath::Max(
			Profile.MinItemLevelOffset, Profile.MaxItemLevelOffset);
		Result.ItemLevel = FMath::Clamp(PlayerLevel
			+ Stream.RandRange(MinOffset, MaxOffset), 1, 100);
		Result.EnsureIdentity();
		AuraItems::RollEquipmentModifiers(
			Result, Stream.RandRange(1, MAX_int32));
	}
	else
	{
		const FName ConsumableId = Stream.FRand()
			<= FMath::Clamp(Profile.HealthPotionWeight, 0.f, 1.f)
			? FName(TEXT("Consumable.HealthPotion"))
			: FName(TEXT("Consumable.ManaPotion"));
		Result.Definition = Catalog.FindItem(ConsumableId);
		Result.Quantity = 1;
		Result.ItemLevel = 1;
		Result.Rarity = EAuraItemRarity::Common;
		Result.EnsureIdentity();
	}
	return Result;
}

EAuraLootProfileType UAuraLootSubsystem::ResolveEnemyProfile(
	const AAuraEnemy& Enemy) const
{
	if (Enemy.GetLootProfile() != EAuraLootProfileType::Auto)
	{
		return Enemy.GetLootProfile();
	}
	if (Enemy.IsBossEnemy()) return EAuraLootProfileType::Boss;
	if (Enemy.IsRangedCombatant()) return EAuraLootProfileType::EliteEnemy;
	return EAuraLootProfileType::CommonEnemy;
}

int32 UAuraLootSubsystem::SpawnPersonalLootForEnemy(AAuraEnemy& Enemy)
{
	UWorld* World = GetWorld();
	if (!World || !Enemy.HasAuthority()) return 0;
	const EAuraLootProfileType ProfileType = ResolveEnemyProfile(Enemy);
	if (ProfileType == EAuraLootProfileType::None) return 0;
	const UAuraLootProfileData& LootData = UAuraLootProfileData::Get();
	const float EligibilityRadiusSq = FMath::Square(LootData.EligibilityRadius);
	const AGameStateBase* GameState = World->GetGameState();
	if (!GameState) return 0;

	int32 TotalSpawned = 0;
	int32 EligiblePlayers = 0;
	const int32 BaseSeed = FMath::Max(1, FMath::Rand());
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		const AAuraCharacter* PlayerCharacter = PlayerState
			? Cast<AAuraCharacter>(PlayerState->GetPawn()) : nullptr;
		if (!PlayerState || !PlayerCharacter
			|| FVector::DistSquared(PlayerCharacter->GetActorLocation(),
				Enemy.GetActorLocation()) > EligibilityRadiusSq)
		{
			continue;
		}
		++EligiblePlayers;
		const int32 PlayerSeed = static_cast<int32>(HashCombine(
			static_cast<uint32>(BaseSeed), GetTypeHash(PlayerState->GetPlayerId())));
		TotalSpawned += SpawnPersonalLootForPlayer(*PlayerState, ProfileType,
			Enemy.GetActorLocation() + FVector(0.f, 0.f, 35.f),
			EAuraItemSourceType::EnemyDrop,
			Enemy.GetClass()->GetFName(), PlayerSeed);
	}
	UE_LOG(LogAuraLoot, Display,
		TEXT("Enemy personal loot resolved. Enemy=%s Profile=%d EligiblePlayers=%d Spawned=%d"),
		*Enemy.GetName(), static_cast<int32>(ProfileType), EligiblePlayers, TotalSpawned);
	return TotalSpawned;
}

int32 UAuraLootSubsystem::SpawnPersonalLootForPlayer(
	APlayerState& PlayerState, const EAuraLootProfileType ProfileType,
	const FVector& Origin, const EAuraItemSourceType SourceType,
	const FName SourceId, const int32 Seed)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return 0;
	const AAuraCharacter* Character = Cast<AAuraCharacter>(PlayerState.GetPawn());
	const int32 PlayerLevel = Character ? Character->GetCharacterLevel() : 1;
	const int32 EffectiveSeed = Seed != 0 ? Seed : FMath::Max(1, FMath::Rand());
	TArray<FAuraItemInstance> Items = RollLoot(ProfileType, PlayerLevel, EffectiveSeed);
	const FAuraLootProfile& Profile = UAuraLootProfileData::Get().GetProfile(ProfileType);
	FRandomStream ScatterStream(HashCombine(
		static_cast<uint32>(EffectiveSeed), 0x9e3779b9u));
	const int32 Spawned = SpawnRolledItems(PlayerState, Items, Origin, Profile,
		SourceType, SourceId, ScatterStream);
	if (Spawned > 0)
	{
		if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(
			PlayerState.GetPlayerController()))
		{
			Controller->ShowCoopMessage(
				FString::Printf(TEXT("个人战利品  x%d"), Spawned),
				FLinearColor(1.f, 0.68f, 0.16f), 1.8f);
		}
	}
	return Spawned;
}

int32 UAuraLootSubsystem::SpawnRolledItems(APlayerState& PlayerState,
	const TArray<FAuraItemInstance>& Items, const FVector& Origin,
	const FAuraLootProfile& Profile, const EAuraItemSourceType SourceType,
	const FName SourceId, FRandomStream& Stream)
{
	int32 Spawned = 0;
	for (const FAuraItemInstance& RolledItem : Items)
	{
		if (GetActivePersonalDropCount(&PlayerState)
			>= UAuraLootProfileData::Get().MaxActivePersonalDropsPerPlayer)
		{
			UE_LOG(LogAuraLoot, Warning,
				TEXT("Personal loot cap reached. Player=%s Cap=%d Skipped=%s"),
				*GetNameSafe(&PlayerState),
				UAuraLootProfileData::Get().MaxActivePersonalDropsPerPlayer,
				RolledItem.Definition
					? *RolledItem.Definition->ItemId.ToString() : TEXT("Invalid"));
			break;
		}
		const float Angle = Stream.FRandRange(0.f, 2.f * PI);
		const float Radius = FMath::Sqrt(Stream.FRand())
			* FMath::Max(0.f, Profile.ScatterRadius);
		const FVector Location = Origin + FVector(
			FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		FAuraItemInstance Item = RolledItem;
		Item.SourceType = SourceType;
		Item.SourceId = SourceId;
		if (SpawnSpecificPersonalItem(
			PlayerState, Item, Location, Profile.LifetimeSeconds))
		{
			++Spawned;
		}
	}
	return Spawned;
}

AAuraWorldItem* UAuraLootSubsystem::SpawnSpecificPersonalItem(
	APlayerState& PlayerState, const FAuraItemInstance& Item,
	const FVector& Location, const float LifetimeSeconds)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !Item.IsValid()) return nullptr;
	APawn* Pawn = PlayerState.GetPawn();
	const FVector GroundedLocation = AAuraWorldItem::FindGroundedSpawnLocation(
		World, Location, Pawn);
	const FTransform SpawnTransform(FRotator::ZeroRotator, GroundedLocation);
	AAuraWorldItem* WorldItem = World->SpawnActorDeferred<AAuraWorldItem>(
		AAuraWorldItem::StaticClass(), SpawnTransform,
		Pawn, Pawn,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!WorldItem) return nullptr;
	WorldItem->InitializeItem(Item, &PlayerState, 0.f, true, LifetimeSeconds);
	WorldItem->FinishSpawning(SpawnTransform);
	UE_LOG(LogAuraLoot, Display,
		TEXT("Personal loot spawned. Actor=%s Player=%s Item=%s Rarity=%s Level=%d Seed=%d Source=%s Lifetime=%.1f"),
		*GetNameSafe(WorldItem), *GetNameSafe(&PlayerState),
		Item.Definition ? *Item.Definition->ItemId.ToString() : TEXT("None"),
		AuraItems::GetRarityName(Item.Rarity), Item.ItemLevel, Item.RollSeed,
		*Item.SourceId.ToString(), LifetimeSeconds);
	return WorldItem;
}

int32 UAuraLootSubsystem::GetActivePersonalDropCount(
	const APlayerState* PlayerState) const
{
	if (!GetWorld() || !PlayerState) return 0;
	int32 Count = 0;
	for (TActorIterator<AAuraWorldItem> It(GetWorld()); It; ++It)
	{
		if (It->IsExclusiveOwnerDrop() && It->GetReservedPlayer() == PlayerState) ++Count;
	}
	return Count;
}
