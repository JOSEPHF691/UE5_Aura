// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraDungeonGate.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Replicated encounter gate that raises out of the path when opened. */
UCLASS()
class AURA_API AAuraDungeonGate : public AActor
{
	GENERATED_BODY()

public:
	AAuraDungeonGate();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	void SetGateOpen(bool bNewOpen, bool bInstant = false);

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsOpen() const { return bOpen; }
	UStaticMeshComponent* GetGateVisual() const { return GateMesh; }
	/** Local camera occlusion is composed with the opening fade, not visibility-raced. */
	void SetCameraOccluded(bool bOccluded);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Open();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> GateMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UBoxComponent> Blocker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	bool bStartsOpen = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FVector GateMeshScale = FVector(0.25f, 2.15f, 2.2f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FVector BlockerExtent = FVector(115.f, 860.f, 225.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FVector OpenOffset = FVector(0.f, 0.f, 520.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon", meta = (ClampMin = "0.1"))
	float OpenInterpSpeed = 2.8f;

	/** Masked copy of the dungeon stone material; GateOpacity drives temporal dithering. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aura|Dungeon|Visual")
	TObjectPtr<UMaterialInterface> FadeMaterial;

private:
	void RefreshGeometry(bool bSnapToState);
	void RefreshCollision();

	UPROPERTY(ReplicatedUsing = OnRep_Open)
	bool bOpen = false;

	FVector ClosedVisualLocation = FVector::ZeroVector;
	float CurrentOpenAlpha = 0.f;
	bool bCameraOccluded = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FadeMaterialInstance;
};
