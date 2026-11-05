// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraLoreRecord.generated.h"

class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** A readable, per-player story clue. It never changes the shared quest state. */
UCLASS()
class AURA_API AAuraLoreRecord : public AActor
{
	GENERATED_BODY()

public:
	AAuraLoreRecord();
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	FName GetRecordId() const { return RecordId; }
	const FString& GetRecordTitle() const { return RecordTitle; }
	const FString& GetLineOne() const { return LineOne; }
	const FString& GetLineTwo() const { return LineTwo; }
	const FString& GetLineThree() const { return LineThree; }
	const FLinearColor& GetAccentColor() const { return AccentColor; }
	float GetInteractionRadius() const { return InteractionRadius; }
	bool IsInRange(const FVector& PlayerLocation) const;

private:
	void RefreshVisuals();

	UPROPERTY(EditInstanceOnly, Category = "Aura|Lore")
	FName RecordId = NAME_None;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Lore")
	FString RecordTitle = TEXT("守印残卷");
	UPROPERTY(EditInstanceOnly, Category = "Aura|Lore")
	FString LineOne;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Lore")
	FString LineTwo;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Lore")
	FString LineThree;
	UPROPERTY(EditInstanceOnly, Category = "Aura|Lore")
	FLinearColor AccentColor = FLinearColor(0.32f, 0.7f, 1.f);
	UPROPERTY(EditAnywhere, Category = "Aura|Lore", meta = (ClampMin = "100.0"))
	float InteractionRadius = 280.f;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MarkerMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> InteractionLabel;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> MarkerLight;
};
