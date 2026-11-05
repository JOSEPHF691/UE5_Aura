// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Data/AuraLootProfileData.h"
#include "GameFramework/Actor.h"
#include "AuraLootChest.generated.h"

class AAuraCharacter;
class APlayerState;
class UPointLightComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UPrimitiveComponent;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnAuraLootChestClaimed, AAuraLootChest*, Chest, APlayerState*, ClaimingPlayer);

/**
 * A room reward chest that grants item loot only. Every PlayerState may claim
 * it once, while the lid and guard lock remain shared replicated world state.
 */
UCLASS()
class AURA_API AAuraLootChest : public AActor
{
	GENERATED_BODY()

public:
	AAuraLootChest();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	void SetGuardLocked(bool bLocked, int32 RemainingGuards = 0);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	bool ClaimForCharacter(AAuraCharacter* AuraCharacter);

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsOpened() const { return bOpened; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsGuardLocked() const { return bGuardLocked; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	int32 GetRemainingGuardCount() const { return RemainingGuardCount; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	FName GetLootChestId() const { return LootChestId.IsNone() ? GetFName() : LootChestId; }

	bool HasBeenClaimedBy(const APlayerState* PlayerState) const;

	UPROPERTY(BlueprintAssignable, Category = "Aura|Dungeon")
	FOnAuraLootChestClaimed OnLootChestClaimed;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRewardTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_ChestState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> ChestBase;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USceneComponent> LidPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> ChestLid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> ChestBandLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> ChestBandRight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> ChestLock;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> LootBeacon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USphereComponent> RewardTrigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UPointLightComponent> RewardLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UTextRenderComponent> RewardLabel;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon")
	FName LootChestId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Loot")
	EAuraLootProfileType LootProfile = EAuraLootProfileType::Treasure;

private:
	void RefreshGeometry();
	void RefreshChestState(bool bSnapAnimation);

	UPROPERTY(ReplicatedUsing = OnRep_ChestState)
	bool bOpened = false;

	UPROPERTY(ReplicatedUsing = OnRep_ChestState)
	bool bGuardLocked = false;

	UPROPERTY(ReplicatedUsing = OnRep_ChestState)
	int32 RemainingGuardCount = 0;

	float CurrentOpenAlpha = 0.f;
	float AnimationTime = 0.f;
};
