// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraArchmage.generated.h"

class AAuraCharacter;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UPointLightComponent;

/** Safe-camp M12 service. The actor is only a spatial authority anchor; growth lives on PlayerState. */
UCLASS()
class AURA_API AAuraArchmage : public AActor
{
	GENERATED_BODY()

public:
	AAuraArchmage();
	bool CanServe(const AAuraCharacter* Character, FString& OutFailureReason) const;
	float GetInteractionRadius() const { return InteractionRadius; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> MageMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ArcaneStaff;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> InteractionLabel;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> ArcaneLight;
	UPROPERTY(EditAnywhere, Category = "Aura|Interaction", meta = (ClampMin = "100.0"))
	float InteractionRadius = 350.f;
};
