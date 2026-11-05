// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraDungeonCheckpoint.generated.h"

class AAuraCharacter;
class UPointLightComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UPrimitiveComponent;
struct FHitResult;

/** One-shot dungeon checkpoint that restores vitals and persists a respawn transform across map reloads. */
UCLASS()
class AURA_API AAuraDungeonCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	AAuraDungeonCheckpoint();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Aura|Dungeon")
	void ActivateCheckpoint(AAuraCharacter* AuraCharacter);

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	bool IsActivated() const { return bActivated; }

	UFUNCTION(BlueprintPure, Category = "Aura|Dungeon")
	FName GetCheckpointId() const { return CheckpointId; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnCheckpointOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Activated();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<USphereComponent> ActivationSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UStaticMeshComponent> CheckpointMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UPointLightComponent> CheckpointLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	TObjectPtr<UTextRenderComponent> CheckpointLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FName CheckpointId = TEXT("DungeonMidpoint");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aura|Dungeon")
	FVector RespawnOffset = FVector(260.f, 0.f, 96.f);

private:
	void RefreshVisuals();

	UPROPERTY(ReplicatedUsing = OnRep_Activated)
	bool bActivated = false;
};
