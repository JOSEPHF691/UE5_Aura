// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Character/AuraCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/AuraCameraMode.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraCameraWallSmoke, Log, All);

void AAuraPlayerController::AuraTestCameraWallFade()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!IsLocalController() || !GetWorld() || !AuraPawn || !CameraWallFadeMaterial)
	{
		UE_LOG(LogAuraCameraWallSmoke, Error,
			TEXT("AURA_CAMERA_WALL Passed=false Setup=invalid pawn or material"));
		return;
	}

	UStaticMeshComponent* Wall = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		UStaticMeshComponent* Candidate = It->GetStaticMeshComponent();
		if (!Candidate || !Candidate->GetStaticMesh()
			|| Candidate->GetStaticMesh()->GetFName() != FName(TEXT("SM_Wall_4x2"))) continue;
		const float Distance = FVector::DistSquared2D(Candidate->GetComponentLocation(),
			AuraPawn->GetActorLocation());
		if (Distance >= NearestDistance) continue;
		NearestDistance = Distance;
		Wall = Candidate;
	}
	if (!Wall)
	{
		UE_LOG(LogAuraCameraWallSmoke, Error,
			TEXT("AURA_CAMERA_WALL Passed=false Setup=missing wall"));
		return;
	}

	const FBox MeshBounds = Wall->GetStaticMesh()->GetBoundingBox();
	const EAxis::Type NormalAxis = MeshBounds.GetExtent().X < MeshBounds.GetExtent().Y
		? EAxis::X : EAxis::Y;
	const FVector Normal = Wall->GetComponentTransform().GetUnitAxis(NormalAxis);
	const float FloorZ = Wall->Bounds.Origin.Z - Wall->Bounds.BoxExtent.Z;
	const float PawnZ = FloorZ + AuraPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector InitialLocation = AuraPawn->GetActorLocation();
	const FRotator InitialControlRotation = GetControlRotation();
	UMaterialInterface* OriginalMaterial = Wall->GetMaterial(0);
	const ECollisionEnabled::Type InitialCollision = Wall->GetCollisionEnabled();
	const bool bInitialVisibility = Wall->IsVisible();
	const auto PlaceBesideWall = [AuraPawn, Wall, Normal, PawnZ](const float Side)
	{
		FVector Location = Wall->Bounds.Origin + Normal * (Side * 160.f);
		Location.Z = PawnZ;
		AuraPawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	};
	SetControlRotation(FRotator(-62.f, Normal.Rotation().Yaw, 0.f));
	AuraPawn->SetCameraMode(EAuraCameraMode::TopDown, true);
	PlaceBesideWall(1.f);
	if (FParse::Param(FCommandLine::Get(), TEXT("AuraCameraWallCapture")))
	{
		const FString ScreenshotDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
		IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
		FTimerHandle CaptureTimer;
		GetWorldTimerManager().SetTimer(CaptureTimer, [ScreenshotDirectory]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(
				ScreenshotDirectory, TEXT("AuraCameraWallFade.png")), false, false);
		}, 0.7f, false);
	}

	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakPawn(AuraPawn);
	const TWeakObjectPtr<UStaticMeshComponent> WeakWall(Wall);
	FTimerHandle FadeCheck;
	GetWorldTimerManager().SetTimer(FadeCheck,
		[WeakController, WeakPawn, WeakWall, PlaceBesideWall, OriginalMaterial,
			InitialCollision, bInitialVisibility, InitialLocation, InitialControlRotation]()
		{
			AAuraPlayerController* Controller = WeakController.Get();
			UStaticMeshComponent* CurrentWall = WeakWall.Get();
			if (!Controller || !CurrentWall || !WeakPawn.IsValid()) return;
			UMaterialInstanceDynamic* Fade = Cast<UMaterialInstanceDynamic>(
				CurrentWall->GetMaterial(0));
			const float Opacity = Fade
				? Fade->K2_GetScalarParameterValue(TEXT("OcclusionOpacity")) : -1.f;
			const bool bFadedNotHidden = Controller->CameraFadedWalls.Contains(CurrentWall)
				&& Fade && CurrentWall->IsVisible()
				&& Opacity >= 0.35f && Opacity < 0.75f
				&& CurrentWall->GetCollisionEnabled() == InitialCollision;
			PlaceBesideWall(-1.f);
			FTimerHandle RestoreCheck;
			Controller->GetWorldTimerManager().SetTimer(RestoreCheck,
				[WeakController, WeakPawn, WeakWall, OriginalMaterial,
					InitialCollision, bInitialVisibility, InitialLocation,
					InitialControlRotation, bFadedNotHidden, Opacity]()
				{
					AAuraPlayerController* CurrentController = WeakController.Get();
					AAuraCharacter* CurrentPawn = WeakPawn.Get();
					UStaticMeshComponent* WallAfter = WeakWall.Get();
					if (!CurrentController || !CurrentPawn || !WallAfter) return;
					const bool bRestored = !CurrentController->CameraFadedWalls.Contains(WallAfter)
						&& WallAfter->GetMaterial(0) == OriginalMaterial
						&& WallAfter->IsVisible() == bInitialVisibility
						&& WallAfter->GetCollisionEnabled() == InitialCollision;
					CurrentPawn->SetActorLocation(InitialLocation, false, nullptr,
						ETeleportType::TeleportPhysics);
					CurrentController->SetControlRotation(InitialControlRotation);
					const bool bPassed = bFadedNotHidden && bRestored;
					UE_LOG(LogAuraCameraWallSmoke, Display,
						TEXT("AURA_CAMERA_WALL Passed=%s FadedNotHidden=%s Restored=%s Opacity=%.3f"),
						bPassed ? TEXT("true") : TEXT("false"),
						bFadedNotHidden ? TEXT("true") : TEXT("false"),
						bRestored ? TEXT("true") : TEXT("false"), Opacity);
				}, 1.f, false);
		}, 1.f, false);
}
