// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuraCampVendor.generated.h"

class AAuraCharacter;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UPointLightComponent;

UENUM(BlueprintType)
enum class EAuraCampService : uint8
{
	Merchant UMETA(DisplayName = "Merchant"),
	Blacksmith UMETA(DisplayName = "Blacksmith")
};

/** Spatial service authority and visible camp dressing; inventory/economy stay on PlayerState. */
UCLASS()
class AURA_API AAuraCampVendor : public AActor
{
	GENERATED_BODY()

public:
	AAuraCampVendor();
	virtual void OnConstruction(const FTransform& Transform) override;
	bool CanServe(const AAuraCharacter* Character, FString& OutFailureReason) const;
	EAuraCampService GetServiceType() const { return ServiceType; }
	float GetInteractionRadius() const { return InteractionRadius; }

private:
	UPROPERTY(EditAnywhere, Category = "Aura|Camp")
	EAuraCampService ServiceType = EAuraCampService::Merchant;
	UPROPERTY(EditAnywhere, Category = "Aura|Camp", meta = (ClampMin = "100.0"))
	float InteractionRadius = 235.f;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> VendorMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WorkSurface;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DisplayProp;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> InteractionLabel;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> ServiceLight;
};
