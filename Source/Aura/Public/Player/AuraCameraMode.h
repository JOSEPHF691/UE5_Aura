// Copyright yosep

#pragma once

#include "CoreMinimal.h"
#include "AuraCameraMode.generated.h"

UENUM(BlueprintType)
enum class EAuraCameraMode : uint8
{
	TopDown UMETA(DisplayName = "God View")
};

USTRUCT(BlueprintType)
struct FAuraCameraModeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float TargetArmLength = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float FieldOfView = 90.f;
	/** Frames terrain ahead of the player along the current camera yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float LookAheadDistance = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector SocketOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator RelativeRotation = FRotator(-55.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bUsePawnControlRotation = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnableCameraLag = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bDoCollisionTest = false;
};

USTRUCT(BlueprintType)
struct FAuraAimResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bBlockingHit = false;
	UPROPERTY(BlueprintReadOnly) FVector TraceStart = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector ImpactNormal = FVector::UpVector;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> HitActor = nullptr;
};
