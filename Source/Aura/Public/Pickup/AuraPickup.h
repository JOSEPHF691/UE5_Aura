// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraPickup.generated.h"

class UPointLightComponent;
class USceneComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInterface;
class USoundBase;
class UPrimitiveComponent;
struct FHitResult;

UENUM(BlueprintType)
enum class EAuraPickupType : uint8
{
	Health UMETA(DisplayName = "Health"),
	Mana UMETA(DisplayName = "Mana")
};

/** A collision pickup that restores a GAS vital attribute and can be reset by a checkpoint. */
UCLASS()
class AURA_API AAuraPickup : public AActor
{
	GENERATED_BODY()

public:
	AAuraPickup();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Aura|Pickup")
	EAuraPickupType GetPickupType() const { return PickupType; }

	UFUNCTION(BlueprintPure, Category = "Aura|Pickup")
	bool IsAvailable() const { return bAvailable; }

	UFUNCTION(BlueprintPure, Category = "Aura|Pickup")
	float GetRestoreAmount() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Pickup")
	float GetRespawnDelay() const;

	UFUNCTION(BlueprintPure, Category = "Aura|Pickup")
	bool RespawnsAutomatically() const { return bRespawnsAutomatically; }

	/** Called by the future checkpoint/encounter reset flow. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Pickup")
	void ResetForCheckpoint();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnPickupSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Available();

	UFUNCTION()
	void OnRep_PickupType();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayPickupSound(bool bRespawning);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	TObjectPtr<USphereComponent> PickupSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	TObjectPtr<UPointLightComponent> PickupLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	TObjectPtr<UTextRenderComponent> PickupLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_PickupType, Category = "Aura|Pickup")
	EAuraPickupType PickupType = EAuraPickupType::Health;

	/** Disabled for dungeon progression so players cannot camp beside infinite supplies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Pickup")
	bool bRespawnsAutomatically = false;

private:
	void RefreshVisuals();
	void ApplyAvailabilityState();
	void SetAvailable(bool bNewAvailable);
	void Respawn();

	UPROPERTY()
	TObjectPtr<UStaticMesh> HealthMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ManaMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> RedLiquidMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BlueLiquidMaterial;

	UPROPERTY()
	TObjectPtr<USoundBase> ConsumeSound;

	UPROPERTY()
	TObjectPtr<USoundBase> RespawnSound;

	UPROPERTY(ReplicatedUsing = OnRep_Available)
	bool bAvailable = true;

	float VisualBaseHeight = 58.f;
	float AnimationTime = 0.f;
	FTimerHandle RespawnTimer;
};
