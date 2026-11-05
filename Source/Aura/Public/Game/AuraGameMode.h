// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AuraGameMode.generated.h"

class AAuraCharacter;

/** Authoritative co-op lifecycle: spawn, death, and checkpoint respawn. */
UCLASS()
class AURA_API AAuraGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAuraGameMode();

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	void HandlePlayerDeath(AAuraCharacter* DefeatedCharacter);
	void RequestPlayerRespawn(AController* Controller);
	void CheckForTeamWipe();
	bool IsTeamWipePending() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Coop")
	float GetRespawnDelay() const { return RespawnDelay; }

private:
	void FinishTeamWipe();
	FTimerHandle TeamWipeTimer;
	bool bResettingTeam = false;
	bool TryResolveActiveRoomTransform(AController* Controller, FTransform& OutTransform) const;
	FTransform ResolveRespawnTransform(AController* Controller);
	void RespawnPlayerAtCheckpoint(TWeakObjectPtr<AController> Controller);

	UPROPERTY(EditDefaultsOnly, Category = "Aura|Coop", meta = (ClampMin = "0.0"))
	float RespawnDelay = 4.f;

	TMap<TWeakObjectPtr<AController>, FTimerHandle> PendingRespawns;
};
