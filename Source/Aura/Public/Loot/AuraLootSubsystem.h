// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Data/AuraLootProfileData.h"
#include "Items/AuraItemTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "AuraLootSubsystem.generated.h"

class AAuraEnemy;
class AAuraWorldItem;
class APlayerState;

/** Server-only personal loot generation and world-drop spawning. */
UCLASS()
class AURA_API UAuraLootSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	TArray<FAuraItemInstance> RollLoot(
		EAuraLootProfileType ProfileType, int32 PlayerLevel, int32 Seed = 0) const;

	int32 SpawnPersonalLootForEnemy(AAuraEnemy& Enemy);
	int32 SpawnPersonalLootForPlayer(APlayerState& PlayerState,
		EAuraLootProfileType ProfileType, const FVector& Origin,
		EAuraItemSourceType SourceType, FName SourceId, int32 Seed = 0);

	AAuraWorldItem* SpawnSpecificPersonalItem(APlayerState& PlayerState,
		const FAuraItemInstance& Item, const FVector& Location,
		float LifetimeSeconds = 120.f);

	int32 GetActivePersonalDropCount(const APlayerState* PlayerState) const;
	EAuraLootProfileType ResolveEnemyProfile(const AAuraEnemy& Enemy) const;

private:
	EAuraItemRarity RollRarity(
		const FAuraLootProfile& Profile, FRandomStream& Stream) const;
	FAuraItemInstance RollSingleItem(
		const FAuraLootProfile& Profile, int32 PlayerLevel, FRandomStream& Stream) const;
	int32 SpawnRolledItems(APlayerState& PlayerState,
		const TArray<FAuraItemInstance>& Items, const FVector& Origin,
		const FAuraLootProfile& Profile, EAuraItemSourceType SourceType,
		FName SourceId, FRandomStream& Stream);
};
