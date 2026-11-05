// Copyright yosep

#include "Player/AuraPlayerController.h"

#include "Coop/AuraReviveComponent.h"
#include "Game/AuraGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "UI/AuraHUD.h"

AAuraCharacter* AAuraPlayerController::FindRescueCandidate() const
{
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!AuraPawn || AuraPawn->IsDead() || !State || GetNetMode() == NM_Standalone) return nullptr;
	AAuraCharacter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const APlayerState* TeamMember : State->PlayerArray)
	{
		AAuraCharacter* Other = TeamMember ? Cast<AAuraCharacter>(TeamMember->GetPawn()) : nullptr;
		if (!AuraPawn->GetReviveComponent()->CanRescue(Other)) continue;
		const float Distance = FVector::DistSquared(AuraPawn->GetActorLocation(), Other->GetActorLocation());
		if (Distance < BestDistance) { Best = Other; BestDistance = Distance; }
	}
	return Best;
}

void AAuraPlayerController::CancelCoopRescue()
{
	if (!bRescueInputHeld) return;
	bRescueInputHeld = false;
	ServerCancelRescue(NextRescueRequestId);
}

bool AAuraPlayerController::HandleCoopRescueInput(const FInputKeyEventArgs& Params)
{
	if (!IsLocalController() || GetNetMode() == NM_Standalone) return false;
	// Releases must bypass every menu/input early-return, including a menu opened
	// during the pending round-trip to the server.
	if (Params.Key == EKeys::F && Params.Event == IE_Released && bRescueInputHeld)
	{
		CancelCoopRescue();
		Super::InputKey(Params);
		return true;
	}
	const AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (AuraPawn && AuraPawn->IsDead())
	{
		// Still forward releases so Enhanced Input cannot retain a held movement key.
		if (Params.Event == IE_Released) Super::InputKey(Params);
		return true;
	}
	if (Params.Event != IE_Pressed || bDemoCompleted) return false;
	if (bRescueInputHeld && Params.Key != EKeys::F
		&& Params.Key != EKeys::RightMouseButton)
	{
		CancelCoopRescue();
		// A movement key also reaches Enhanced Input; other commands cancel only.
		const bool bMovement = Params.Key == EKeys::W || Params.Key == EKeys::A
			|| Params.Key == EKeys::S || Params.Key == EKeys::D;
		return !bMovement;
	}
	if (Params.Key != EKeys::F || bInventoryOpen || bCampMenuOpen || bArchmageMenuOpen
		|| bIsRunUpgradeSelectionActive) return false;
	if (bRescueInputHeld) return true;
	AAuraCharacter* Target = FindRescueCandidate();
	if (!Target) return false;
	if (AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD())) HUD->HideLoreRecord();
	if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
	if (bIsShockTargeting) CancelShockTargeting();
	bRescueInputHeld = true;
	++NextRescueRequestId;
	Super::InputKey(Params);
	ServerBeginRescue(Target, NextRescueRequestId);
	return true;
}

void AAuraPlayerController::ServerBeginRescue_Implementation(AAuraCharacter* Target, const int32 RequestId)
{
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	const bool bAccepted = AuraPawn && !bDemoCompleted
		&& AuraPawn->GetReviveComponent()->TryBeginRescue(Target, RequestId);
	ClientRescueRequestResult(RequestId, bAccepted);
}

void AAuraPlayerController::ServerCancelRescue_Implementation(const int32 RequestId)
{
	if (AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn()))
		AuraPawn->GetReviveComponent()->CancelRescue(RequestId);
}

void AAuraPlayerController::ClientRescueRequestResult_Implementation(const int32 RequestId, const bool bAccepted)
{
	if (RequestId != NextRescueRequestId || bAccepted) return;
	bRescueInputHeld = false;
	if (AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD()))
		HUD->ShowStatusMessage(TEXT("无法救援：请靠近队友并保持通路畅通"), FLinearColor(1.f, 0.65f, 0.2f));
}

void AAuraPlayerController::ClientRescueChannelEnded_Implementation(const int32 RequestId)
{
	if (RequestId == NextRescueRequestId) bRescueInputHeld = false;
}

void AAuraPlayerController::UpdateCoopRescue()
{
	if (!IsLocalController() || GetNetMode() == NM_Standalone) return;
	AAuraCharacter* AuraPawn = Cast<AAuraCharacter>(GetPawn());
	if (!AuraPawn) return;
	if (bRescueInputHeld && (!IsInputKeyDown(EKeys::F) || AuraPawn->IsDead())) CancelCoopRescue();
	const bool bShouldSpectate = AuraPawn->IsDead() && !bDemoCompleted;
	if (bShouldSpectate && !bCoopSpectating)
	{
		bCoopSpectating = true;
		if (bIsArcaneShardsTargeting) CancelArcaneShardsTargeting();
		if (bIsShockTargeting) CancelShockTargeting();
		if (bCampMenuOpen) CloseCampMenu();
		if (bArchmageMenuOpen) CloseArchmageMenu();
		bInventoryOpen = false;
		bLanMenuOpen = false;
		ResetInventoryDrag();
		CloseInventoryContextMenu();
		ClearNearbyEquipment();
		ClearEnemyHoverHighlight();
		ClearInteractableHighlights();
		if (AAuraHUD* HUD = Cast<AAuraHUD>(GetHUD())) HUD->HideLoreRecord();
		FlushPressedKeys();
		ApplyCursorMode(GetCameraMode());
	}
	if (!bShouldSpectate)
	{
		if (bCoopSpectating)
		{
			bCoopSpectating = false;
			SpectatedTeammate.Reset();
			if (!bDemoCompleted)
			{
				SetViewTargetWithBlend(AuraPawn, 0.35f);
				FlushPressedKeys();
				ApplyCursorMode(GetCameraMode());
			}
		}
		return;
	}
	AAuraCharacter* Target = SpectatedTeammate.Get();
	if (IsValid(Target) && !Target->IsDead()) return;
	Target = nullptr;
	if (const AGameStateBase* State = GetWorld()->GetGameState())
	{
		for (const APlayerState* TeamMember : State->PlayerArray)
		{
			AAuraCharacter* Other = TeamMember ? Cast<AAuraCharacter>(TeamMember->GetPawn()) : nullptr;
			if (Other && Other != AuraPawn && !Other->IsDead()) { Target = Other; break; }
		}
	}
	SpectatedTeammate = Target;
	if (Target) SetViewTargetWithBlend(Target, 0.45f);
}
