// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/AuraItemTypes.h"
#include "AuraWorldItem.generated.h"

class AAuraCharacter;
class APlayerState;
class UBillboardComponent;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class USoundBase;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAuraItemDefinition;

/** Replicated item lying in the world. Server resolves a pickup exactly once. */
UCLASS()
class AURA_API AAuraWorldItem : public AActor
{
	GENERATED_BODY()

public:
	AAuraWorldItem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeItem(const FAuraItemInstance& InItem, APlayerState* InReservedPlayer,
		float ProtectionDuration, bool bInExclusiveOwner, float LifetimeSeconds = 0.f);
	int32 TryPickupBy(AAuraCharacter* Character);

	const FAuraItemInstance& GetItemInstance() const { return ItemInstance; }
	APlayerState* GetReservedPlayer() const { return ReservedPlayer; }
	bool IsExclusiveOwnerDrop() const { return bExclusiveOwner; }
	bool IsVisibleToLocalPlayer() const;
	bool RequiresManualPickup() const;
	/** Runtime QA helpers measure the rendered equipment, not just the actor root. */
	bool HasCompactGroundedEquipmentVisual() const;
	float GetEquipmentVisualLongestDimension() const;
	float GetEquipmentVisualFloorOffset() const;
	bool IsPickupEligibleFor(const APlayerState* PlayerState) const;
	void SetLocallyHighlighted(bool bHighlighted);
	bool IsLocallyHighlighted() const { return bLocallyHighlighted; }

	/** Projects runtime drops onto WorldStatic so the floor rune never floats at capsule height. */
	static FVector FindGroundedSpawnLocation(
		UWorld* World, const FVector& DesiredLocation, const AActor* IgnoredActor = nullptr);

protected:
	UFUNCTION()
	void OnPickupSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_ItemState();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayPickupSound();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<USphereComponent> PickupSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<USceneComponent> VisualRoot;

	/** Persistent floor rune so a drop remains readable even when its mesh is small. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UStaticMeshComponent> GroundMarker;

	/** Rarity-coloured vertical beacon readable from every gameplay camera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UStaticMeshComponent> LootBeam;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UStaticMeshComponent> LootBeamCross;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UPointLightComponent> ItemLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UTextRenderComponent> ItemLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UBillboardComponent> ItemIcon;

	/** Editor-placement fallback. Runtime drops call InitializeItem instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	TObjectPtr<UAuraItemDefinition> PlacedDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|World Item", meta = (ClampMin = "1"))
	int32 PlacedQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|World Item")
	EAuraItemRarity PlacedRarity = EAuraItemRarity::Common;

private:
	bool CanBePickedBy(const APlayerState* PlayerState) const;
	bool ShouldShowForLocalPlayer() const;
	void RefreshVisuals();

	UPROPERTY(ReplicatedUsing = OnRep_ItemState)
	FAuraItemInstance ItemInstance;

	UPROPERTY(ReplicatedUsing = OnRep_ItemState)
	TObjectPtr<APlayerState> ReservedPlayer = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_ItemState)
	bool bExclusiveOwner = false;

	float ReservationEndTime = 0.f;
	float AnimationTime = 0.f;
	float VisualBaseHeight = 55.f;
	float VisualBobAmplitude = 5.f;
	bool bPickupInProgress = false;
	bool bLocallyHighlighted = false;

	UPROPERTY()
	TObjectPtr<UStaticMesh> FallbackMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> RedLiquidMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BlueLiquidMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> LootGroundMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> LootBeamMaterial;

	UPROPERTY()
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY()
	TObjectPtr<USoundBase> SpawnSound;
};
