// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "Data/AuraLootProfileData.h"
#include "GameFramework/Actor.h"
#include "AuraTreasureChest.generated.h"

class AAuraTreasureChest;
class AAuraCharacter;
class USceneComponent;
class APlayerState;
class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UPrimitiveComponent;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnAuraTreasureClaimed, AAuraTreasureChest*, Chest, APlayerState*, ClaimingPlayer);

/**
 * Guarded reward chest. Players approach it and press F after the room is
 * cleared to open the lid and receive personal equipment/consumable drops.
 */
UCLASS()
class AURA_API AAuraTreasureChest : public AActor
{
	GENERATED_BODY()

public:
	AAuraTreasureChest();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsOpened() const { return bOpened; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsGuardLocked() const { return bGuardLocked; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	int32 GetRemainingGuardCount() const { return RemainingGuardCount; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	void SetGuardLocked(bool bLocked, int32 RemainingGuards = 0);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	bool ClaimForCharacter(AAuraCharacter* AuraCharacter);

	FName GetTreasureId() const { return TreasureId.IsNone() ? GetFName() : TreasureId; }

	UPROPERTY(BlueprintAssignable, Category = "Aura|Dungeon")
	FOnAuraTreasureClaimed OnTreasureClaimed;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRewardTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Opened();

	UFUNCTION()
	void OnRep_GuardState();

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
	TObjectPtr<UStaticMeshComponent> RewardBeacon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USphereComponent> RewardTrigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UPointLightComponent> RewardLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UTextRenderComponent> RewardLabel;

	/** Stable per-run identity used to prevent reclaiming the cache after checkpoint reload. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Aura|Dungeon")
	FName TreasureId = NAME_None;

	/** Legacy serialized switch. Chests now always grant item rolls, never run boons. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon|Loot")
	bool bGrantPersonalItemLoot = true;

private:
	void RefreshGeometry();
	void RefreshOpenState(bool bSnapAnimation);
	bool OpenChest(AAuraCharacter* AuraCharacter);

	UPROPERTY(ReplicatedUsing = OnRep_Opened)
	bool bOpened = false;

	UPROPERTY(ReplicatedUsing = OnRep_GuardState)
	bool bGuardLocked = false;

	UPROPERTY(ReplicatedUsing = OnRep_GuardState)
	int32 RemainingGuardCount = 0;

	float CurrentOpenAlpha = 0.f;
	float AnimationTime = 0.f;
};
