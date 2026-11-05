// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Character/AuraCharacter.h"
#include "Character/AuraEnemy.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Level/AuraArchmage.h"
#include "Level/AuraCampVendor.h"
#include "Level/AuraDungeonCheckpoint.h"
#include "Level/AuraLootChest.h"
#include "Level/AuraLoreRecord.h"
#include "Level/AuraRitualSite.h"
#include "Level/AuraTreasureChest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/AuraPlayerState.h"
#include "TimerManager.h"
#include "UI/AuraHUD.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraInteractableGlow, Log, All);

void AAuraPlayerController::ClearInteractableHighlights()
{
	for (const TPair<TWeakObjectPtr<UMeshComponent>, TWeakObjectPtr<UMaterialInterface>>& Entry
		: InteractableOriginalOverlays)
	{
		if (UMeshComponent* Mesh = Entry.Key.Get();
			Mesh && Mesh->GetOverlayMaterial() == InteractableGlowMaterial)
		{
			Mesh->SetOverlayMaterial(Entry.Value.Get());
		}
	}
	InteractableOriginalOverlays.Empty();
}

void AAuraPlayerController::ClearEnemyHoverHighlight()
{
	for (const auto& Entry : EnemyHoverOriginalOverlays)
	{
		if (UMeshComponent* Mesh = Entry.Key.Get();
			Mesh && Mesh->GetOverlayMaterial() == EnemyHoverOutlineMaterial)
		{
			Mesh->SetOverlayMaterial(Entry.Value.Get());
		}
	}
	EnemyHoverOriginalOverlays.Empty();
	HoveredFireBoltEnemy.Reset();
}

void AAuraPlayerController::UpdateEnemyHoverHighlight()
{
	const AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	const AAuraCharacter* AuraPawn = IsLocalController()
		? Cast<AAuraCharacter>(GetPawn()) : nullptr;
	const bool bCanAim = InteractableGlowMaterial && AuraPawn && !AuraPawn->IsDead()
		&& !bDemoCompleted && !bInventoryOpen && !bArchmageMenuOpen
		&& !bCampMenuOpen && !bLanMenuOpen && !bIsRunUpgradeSelectionActive
		&& !bIsArcaneShardsTargeting && !bIsShockTargeting
		&& (!AuraHUD || !AuraHUD->IsLoreRecordVisible());
	AAuraEnemy* Enemy = bCanAim ? GetFireBoltCursorTarget() : nullptr;
	if (Enemy == HoveredFireBoltEnemy.Get())
	{
		if (!Enemy && EnemyHoverOriginalOverlays.IsEmpty()) return;
		if (const UMeshComponent* MainMesh = Enemy->GetMesh();
			MainMesh && MainMesh->GetOverlayMaterial() == EnemyHoverOutlineMaterial)
			return;
	}
	ClearEnemyHoverHighlight();
	if (!Enemy) return;
	if (!EnemyHoverOutlineMaterial)
	{
		EnemyHoverOutlineMaterial = UMaterialInstanceDynamic::Create(
			InteractableGlowMaterial, this);
		if (!EnemyHoverOutlineMaterial) return;
		EnemyHoverOutlineMaterial->SetVectorParameterValue(
			TEXT("OutlineColor"), FLinearColor(1.f, 0.025f, 0.015f, 1.f));
		EnemyHoverOutlineMaterial->SetScalarParameterValue(TEXT("PulseAmount"), 0.05f);
		EnemyHoverOutlineMaterial->SetScalarParameterValue(TEXT("PulseFloor"), 0.96f);
	}
	for (UMeshComponent* Mesh : { static_cast<UMeshComponent*>(Enemy->GetMesh()),
		static_cast<UMeshComponent*>(Enemy->GetWeaponMesh()) })
	{
		if (!Mesh || !Mesh->IsRegistered() || !Mesh->IsVisible()
			|| Mesh->GetNumMaterials() == 0) continue;
		EnemyHoverOriginalOverlays.Add(Mesh, Mesh->GetOverlayMaterial());
		Mesh->SetOverlayMaterial(EnemyHoverOutlineMaterial);
	}
	HoveredFireBoltEnemy = Enemy;
}

void AAuraPlayerController::UpdateInteractableHighlights()
{
	if (!GetWorld())
	{
		ClearInteractableHighlights();
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextInteractableHighlightRefreshTime) return;
	NextInteractableHighlightRefreshTime = Now + 0.08f;

	const AAuraCharacter* AuraPawn = IsLocalController()
		? Cast<AAuraCharacter>(GetPawn()) : nullptr;
	const AAuraHUD* AuraHUD = Cast<AAuraHUD>(GetHUD());
	if (!InteractableGlowMaterial || !AuraPawn || AuraPawn->IsDead() || bDemoCompleted
		|| bInventoryOpen || bArchmageMenuOpen || bCampMenuOpen || bLanMenuOpen
		|| bIsRunUpgradeSelectionActive || (AuraHUD && AuraHUD->IsLoreRecordVisible()))
	{
		ClearInteractableHighlights();
		return;
	}

	const FVector PlayerLocation = AuraPawn->GetActorLocation();
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	TSet<TWeakObjectPtr<UMeshComponent>> DesiredMeshes;
	const auto AddActorMeshes = [&DesiredMeshes](AActor* Actor)
	{
		TArray<UMeshComponent*> Meshes;
		Actor->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->IsRegistered() && Mesh->IsVisible()
				&& Mesh->GetNumMaterials() > 0)
			{
				DesiredMeshes.Add(Mesh);
			}
		}
	};

	FString FailureReason;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		if (It->CanServe(AuraPawn, FailureReason)) AddActorMeshes(*It);
	}
	for (TActorIterator<AAuraCampVendor> It(GetWorld()); It; ++It)
	{
		if (It->CanServe(AuraPawn, FailureReason)) AddActorMeshes(*It);
	}
	for (TActorIterator<AAuraRitualSite> It(GetWorld()); It; ++It)
	{
		if (It->IsReadyForInteraction() && It->CanInteract(AuraPawn, FailureReason))
			AddActorMeshes(*It);
	}
	for (TActorIterator<AAuraLoreRecord> It(GetWorld()); It; ++It)
	{
		if (It->IsInRange(PlayerLocation)) AddActorMeshes(*It);
	}
	if (AuraPlayerState)
	{
		constexpr float ChestInteractionDistanceSq = 320.f * 320.f;
		for (TActorIterator<AAuraTreasureChest> It(GetWorld()); It; ++It)
		{
			if (!It->IsGuardLocked()
				&& !AuraPlayerState->HasClaimedTreasure(It->GetTreasureId())
				&& FVector::DistSquared(PlayerLocation, It->GetActorLocation())
					<= ChestInteractionDistanceSq)
			{
				AddActorMeshes(*It);
			}
		}
		for (TActorIterator<AAuraLootChest> It(GetWorld()); It; ++It)
		{
			if (!It->IsGuardLocked() && !It->HasBeenClaimedBy(AuraPlayerState)
				&& FVector::DistSquared(PlayerLocation, It->GetActorLocation())
					<= ChestInteractionDistanceSq)
			{
				AddActorMeshes(*It);
			}
		}
	}
	// Checkpoints activate on overlap rather than F. A short approach band gives
	// their stone pillar a readable cue before the automatic activation fires.
	for (TActorIterator<AAuraDungeonCheckpoint> It(GetWorld()); It; ++It)
	{
		if (!It->IsActivated()
			&& FVector::DistSquared(PlayerLocation,
				It->GetActorLocation() + FVector(0.f, 0.f, 92.f)) <= FMath::Square(280.f))
		{
			AddActorMeshes(*It);
		}
	}

	for (auto It = InteractableOriginalOverlays.CreateIterator(); It; ++It)
	{
		UMeshComponent* Mesh = It.Key().Get();
		if (!Mesh || !DesiredMeshes.Contains(It.Key()))
		{
			// Do not overwrite a newer overlay installed by another system.
			if (Mesh && Mesh->GetOverlayMaterial() == InteractableGlowMaterial)
				Mesh->SetOverlayMaterial(It.Value().Get());
			It.RemoveCurrent();
		}
	}
	for (const TWeakObjectPtr<UMeshComponent>& WeakMesh : DesiredMeshes)
	{
		UMeshComponent* Mesh = WeakMesh.Get();
		if (!Mesh || InteractableOriginalOverlays.Contains(WeakMesh)) continue;
		UMaterialInterface* OriginalOverlay = Mesh->GetOverlayMaterial();
		Mesh->SetOverlayMaterial(InteractableGlowMaterial);
		InteractableOriginalOverlays.Add(WeakMesh, OriginalOverlay);
	}
}

void AAuraPlayerController::AuraTestInteractableGlow()
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!IsLocalController() || !GetWorld() || !AuraPawn || !InteractableGlowMaterial)
	{
		UE_LOG(LogAuraInteractableGlow, Error,
			TEXT("AURA_INTERACTABLE_GLOW Passed=false Setup=invalid pawn or material"));
		return;
	}
	const FVector InitialLocation = AuraPawn->GetActorLocation();
	const FRotator InitialControlRotation = GetControlRotation();
	const float HalfHeight = AuraPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const auto HasGoldRim = [this](const AActor* Actor)
	{
		if (!Actor) return false;
		TArray<UMeshComponent*> Meshes;
		Actor->GetComponents<UMeshComponent>(Meshes);
		for (const UMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->IsVisible()
				&& Mesh->GetOverlayMaterial() == InteractableGlowMaterial) return true;
		}
		return false;
	};
	const auto PlaceAndRefresh = [this, AuraPawn, HalfHeight](const AActor* Actor)
	{
		const FVector Location = Actor->GetActorLocation() + FVector(0.f, 120.f, HalfHeight);
		AuraPawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		NextInteractableHighlightRefreshTime = 0.f;
		UpdateInteractableHighlights();
	};

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAuraLootChest* TestChest = GetWorld()->SpawnActor<AAuraLootChest>(
		InitialLocation + FVector(200.f, 0.f, -HalfHeight), FRotator::ZeroRotator,
		SpawnParameters);
	if (!TestChest)
	{
		UE_LOG(LogAuraInteractableGlow, Error,
			TEXT("AURA_INTERACTABLE_GLOW Passed=false Setup=chest spawn failed"));
		return;
	}
	PlaceAndRefresh(TestChest);
	const bool bChestNearby = HasGoldRim(TestChest);
	TestChest->SetGuardLocked(true, 1);
	NextInteractableHighlightRefreshTime = 0.f;
	UpdateInteractableHighlights();
	const bool bChestLockedDark = !HasGoldRim(TestChest);
	TestChest->SetGuardLocked(false, 0);
	NextInteractableHighlightRefreshTime = 0.f;
	UpdateInteractableHighlights();
	const bool bChestUnlocked = HasGoldRim(TestChest);
	AuraPawn->SetActorLocation(InitialLocation + FVector(5000.f, 0.f, 500.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	NextInteractableHighlightRefreshTime = 0.f;
	UpdateInteractableHighlights();
	const bool bChestLeftRange = !HasGoldRim(TestChest);
	AAuraTreasureChest* TestTreasure = GetWorld()->SpawnActor<AAuraTreasureChest>(
		InitialLocation + FVector(-200.f, 0.f, -HalfHeight), FRotator::ZeroRotator,
		SpawnParameters);
	bool bTreasureNearby = false;
	bool bTreasureLockedDark = false;
	if (TestTreasure)
	{
		PlaceAndRefresh(TestTreasure);
		bTreasureNearby = HasGoldRim(TestTreasure);
		TestTreasure->SetGuardLocked(true, 1);
		NextInteractableHighlightRefreshTime = 0.f;
		UpdateInteractableHighlights();
		bTreasureLockedDark = !HasGoldRim(TestTreasure);
		TestTreasure->Destroy();
		NextInteractableHighlightRefreshTime = 0.f;
		UpdateInteractableHighlights();
	}

	bool bLoreNearby = false;
	for (TActorIterator<AAuraLoreRecord> It(GetWorld()); It; ++It)
	{
		PlaceAndRefresh(*It);
		bLoreNearby = HasGoldRim(*It);
		break;
	}
	bool bMageNearby = false;
	AAuraArchmage* CaptureMage = nullptr;
	for (TActorIterator<AAuraArchmage> It(GetWorld()); It; ++It)
	{
		PlaceAndRefresh(*It);
		bMageNearby = HasGoldRim(*It);
		CaptureMage = *It;
		break;
	}
	bool bVendorNearby = false;
	for (TActorIterator<AAuraCampVendor> It(GetWorld()); It; ++It)
	{
		PlaceAndRefresh(*It);
		bVendorNearby = HasGoldRim(*It);
		break;
	}
	bool bLockedRitualDark = true;
	for (TActorIterator<AAuraRitualSite> It(GetWorld()); It; ++It)
	{
		if (It->IsReadyForInteraction()) continue;
		PlaceAndRefresh(*It);
		bLockedRitualDark = !HasGoldRim(*It);
		break;
	}
	bool bCheckpointChecked = false;
	bool bCheckpointNearby = false;
	for (TActorIterator<AAuraDungeonCheckpoint> It(GetWorld()); It; ++It)
	{
		if (It->IsActivated()) continue;
		const FVector Location = It->GetActorLocation() + FVector(255.f, 0.f, HalfHeight);
		AuraPawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		NextInteractableHighlightRefreshTime = 0.f;
		UpdateInteractableHighlights();
		bCheckpointChecked = true;
		bCheckpointNearby = HasGoldRim(*It);
		break;
	}
	const bool bPassed = bChestNearby && bChestLockedDark && bChestUnlocked
		&& bChestLeftRange && bTreasureNearby && bTreasureLockedDark
		&& bLoreNearby && bMageNearby && bVendorNearby && bLockedRitualDark
		&& (!bCheckpointChecked || bCheckpointNearby);
	const auto LogResult = [bPassed, bChestNearby, bChestLockedDark,
		bChestUnlocked, bChestLeftRange, bTreasureNearby, bTreasureLockedDark,
		bLoreNearby, bMageNearby, bVendorNearby, bLockedRitualDark,
		bCheckpointChecked, bCheckpointNearby](const bool bCaptureSaved)
	{
		UE_LOG(LogAuraInteractableGlow, Display,
			TEXT("AURA_INTERACTABLE_GLOW Passed=%s Chest=%s LockedDark=%s Unlocked=%s LeftRange=%s Treasure=%s TreasureLockedDark=%s Lore=%s Mage=%s Vendor=%s RitualLockedDark=%s CheckpointChecked=%s Checkpoint=%s Capture=%s"),
			(bPassed && bCaptureSaved) ? TEXT("true") : TEXT("false"),
			bChestNearby ? TEXT("true") : TEXT("false"),
			bChestLockedDark ? TEXT("true") : TEXT("false"),
			bChestUnlocked ? TEXT("true") : TEXT("false"),
			bChestLeftRange ? TEXT("true") : TEXT("false"),
			bTreasureNearby ? TEXT("true") : TEXT("false"),
			bTreasureLockedDark ? TEXT("true") : TEXT("false"),
			bLoreNearby ? TEXT("true") : TEXT("false"),
			bMageNearby ? TEXT("true") : TEXT("false"),
			bVendorNearby ? TEXT("true") : TEXT("false"),
			bLockedRitualDark ? TEXT("true") : TEXT("false"),
			bCheckpointChecked ? TEXT("true") : TEXT("false"),
			bCheckpointNearby ? TEXT("true") : TEXT("false"),
			bCaptureSaved ? TEXT("true") : TEXT("false"));
	};
	AuraPawn->SetActorLocation(InitialLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SetControlRotation(InitialControlRotation);
	NextInteractableHighlightRefreshTime = 0.f;
	UpdateInteractableHighlights();
	if (!FParse::Param(FCommandLine::Get(), TEXT("AuraInteractableGlowCapture")))
	{
		TestChest->Destroy();
		NextInteractableHighlightRefreshTime = 0.f;
		UpdateInteractableHighlights();
		LogResult(true);
		return;
	}

	const bool bCaptureNpc = FParse::Param(
		FCommandLine::Get(), TEXT("AuraInteractableGlowCaptureNpc"));
	AActor* CaptureTarget = bCaptureNpc && CaptureMage
		? static_cast<AActor*>(CaptureMage) : static_cast<AActor*>(TestChest);
	if (CaptureTarget != TestChest) PlaceAndRefresh(CaptureTarget);
	const FString ScreenshotDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Screenshots/WindowsEditor"));
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
	const FString ScreenshotPath = FPaths::Combine(
		ScreenshotDirectory, bCaptureNpc
			? TEXT("AuraInteractableGoldNpc.png")
			: TEXT("AuraInteractableGoldChest.png"));
	const FVector CameraLocation = CaptureTarget->GetActorLocation()
		+ (bCaptureNpc ? FVector(0.f, 0.f, 340.f) : FVector(0.f, -300.f, 220.f));
	const FVector CameraFocus = CaptureTarget->GetActorLocation()
		+ FVector(0.f, 0.f, bCaptureNpc ? 115.f : 48.f);
	ACameraActor* DetailCamera = GetWorld()->SpawnActor<ACameraActor>(
		CameraLocation, (CameraFocus - CameraLocation).Rotation(), SpawnParameters);
	const TWeakObjectPtr<AActor> OriginalViewTarget(GetViewTarget());
	if (DetailCamera)
	{
		DetailCamera->GetCameraComponent()->SetFieldOfView(55.f);
		SetViewTarget(DetailCamera);
	}
	FTimerHandle CaptureTimer;
	GetWorldTimerManager().SetTimer(CaptureTimer, [ScreenshotPath]()
	{
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
	}, 0.6f, false);
	const TWeakObjectPtr<AAuraPlayerController> WeakController(this);
	const TWeakObjectPtr<AAuraCharacter> WeakPawn(AuraPawn);
	const TWeakObjectPtr<AAuraLootChest> WeakChest(TestChest);
	const TWeakObjectPtr<ACameraActor> WeakDetailCamera(DetailCamera);
	FTimerHandle FinishTimer;
	GetWorldTimerManager().SetTimer(FinishTimer,
		[WeakController, WeakPawn, WeakChest, WeakDetailCamera, OriginalViewTarget,
			InitialLocation, InitialControlRotation, ScreenshotPath, LogResult]()
	{
		if (AAuraPlayerController* Controller = WeakController.Get())
		{
			if (AActor* ViewTarget = OriginalViewTarget.Get())
				Controller->SetViewTarget(ViewTarget);
			if (AAuraCharacter* CurrentPawn = WeakPawn.Get())
				CurrentPawn->SetActorLocation(InitialLocation, false, nullptr,
					ETeleportType::TeleportPhysics);
			Controller->SetControlRotation(InitialControlRotation);
			if (ACameraActor* Camera = WeakDetailCamera.Get()) Camera->Destroy();
			if (AAuraLootChest* Chest = WeakChest.Get()) Chest->Destroy();
			Controller->NextInteractableHighlightRefreshTime = 0.f;
			Controller->UpdateInteractableHighlights();
		}
		LogResult(IFileManager::Get().FileExists(*ScreenshotPath));
	}, 1.1f, false);
}
