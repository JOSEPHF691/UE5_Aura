// Copyright yosep

#include "Coop/AuraReviveComponent.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Character/AuraCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Game/AuraGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Player/AuraPlayerState.h"
#include "Player/AuraPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogAuraRescue, Log, All);

UAuraReviveComponent::UAuraReviveComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UAuraReviveComponent::BeginPlay()
{
	Super::BeginPlay();
	LastSafeTransform = GetOwner()->GetActorTransform();
	AAuraCharacter* Character = GetCharacter();
	SetComponentTickEnabled(Character && GetOwner()->HasAuthority() && GetNetMode() != NM_Standalone);
	if (Character && GetOwner()->HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
			ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::OnAbilityActivated);
	}
}

void UAuraReviveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner()->HasAuthority()) CancelAllRescueLinks();
	if (AAuraCharacter* Character = GetCharacter())
		if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
			ASC->AbilityActivatedCallbacks.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

void UAuraReviveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAuraReviveComponent, bWaitingForRescue);
	DOREPLIFETIME(UAuraReviveComponent, RescueTransform);
	DOREPLIFETIME(UAuraReviveComponent, Channel);
	DOREPLIFETIME(UAuraReviveComponent, RescueTarget);
	DOREPLIFETIME(UAuraReviveComponent, ProtectionEndServerTime);
}

AAuraCharacter* UAuraReviveComponent::GetCharacter() const
{
	return Cast<AAuraCharacter>(GetOwner());
}

float UAuraReviveComponent::ServerTime() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

bool UAuraReviveComponent::IsProtected() const
{
	return ProtectionEndServerTime > ServerTime();
}

float UAuraReviveComponent::GetRescueProgress() const
{
	return bWaitingForRescue && Channel.Rescuer
		? FMath::Clamp((ServerTime() - Channel.StartServerTime) / RescueDuration, 0.f, 1.f) : 0.f;
}

bool UAuraReviveComponent::CanRescue(const AAuraCharacter* Target) const
{
	const AAuraCharacter* Character = GetCharacter();
	const UAuraReviveComponent* Other = IsValid(Target) ? Target->GetReviveComponent() : nullptr;
	const AAuraGameState* State = GetWorld() ? GetWorld()->GetGameState<AAuraGameState>() : nullptr;
	if (GetNetMode() == NM_Standalone || !Character || Character->IsDead()
		|| !Character->GetPlayerState() || !Other || Target == Character
		|| !Target->GetPlayerState() || !Target->IsDead() || !Other->bWaitingForRescue
		|| (Other->Channel.Rescuer && Other->Channel.Rescuer != Character)
		|| (State && (State->IsTeamWipePending() || State->IsRunCompleted()))) return false;
	const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!ASC || ASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(TEXT("State.Stunned")))) return false;

	const FVector Source = Character->GetActorLocation();
	const FVector Destination = Other->RescueTransform.GetLocation();
	if (FVector::DistSquared2D(Source, Destination) > FMath::Square(RescueRadius)
		|| FMath::Abs(Source.Z - Destination.Z) > 100.f) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AuraRescueSight), false, Character);
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Source, Destination,
		ECC_Visibility, Params);
}

void UAuraReviveComponent::EnterDownedState()
{
	if (!GetOwner()->HasAuthority() || GetNetMode() == NM_Standalone) return;
	CancelAllRescueLinks();
	EndProtection();
	bWaitingForRescue = true;
	RescueTransform = GetOwner()->GetActorTransform();
	GetOwner()->ForceNetUpdate();
}

void UAuraReviveComponent::ResetAfterRevive(const float ProtectionDuration)
{
	if (!GetOwner()->HasAuthority()) return;
	CancelAllRescueLinks();
	bWaitingForRescue = false;
	Channel = FAuraRescueChannel();
	LastSafeTransform = GetOwner()->GetActorTransform();
	ProtectionEndServerTime = ProtectionDuration > 0.f ? ServerTime() + ProtectionDuration : 0.f;
	GetOwner()->ForceNetUpdate();
}

bool UAuraReviveComponent::TryBeginRescue(AAuraCharacter* Target, const int32 RequestId)
{
	if (!GetOwner()->HasAuthority() || RequestId <= 0 || RequestId <= LastRequestId) return false;
	LastRequestId = RequestId;
	if (IsRescuing() || !CanRescue(Target)) return false;
	AAuraCharacter* Character = GetCharacter();
	UAuraReviveComponent* Other = Target->GetReviveComponent();
	if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent()) ASC->CancelAllAbilities();
	EndProtection();
	RescueTarget = Target;
	ActiveRequestId = RequestId;
	RescueStartLocation = Character->GetActorLocation();
	Other->Channel.Rescuer = Character;
	Other->Channel.RequestId = RequestId;
	Other->Channel.StartServerTime = ServerTime();
	Character->ConsumeMovementInputVector();
	Character->GetCharacterMovement()->StopMovementImmediately();
	Character->GetCharacterMovement()->DisableMovement();
	Character->ForceNetUpdate();
	Target->ForceNetUpdate();
	UE_LOG(LogAuraRescue, Display, TEXT("Rescue started. Rescuer=%s Target=%s Request=%d Duration=%.1f"),
		*Character->GetName(), *Target->GetName(), RequestId, RescueDuration);
	return true;
}

void UAuraReviveComponent::CancelRescue(const int32 RequestId)
{
	if (!GetOwner()->HasAuthority()) return;
	// Do not let an old release packet cancel a newer attempt. Remember a release
	// even if its matching begin was rejected, so it cannot be replayed later.
	if (RequestId != MAX_int32)
	{
		LastRequestId = FMath::Max(LastRequestId, RequestId);
		if (RequestId != ActiveRequestId) return;
	}
	ClearActiveRescue();
}

void UAuraReviveComponent::ClearActiveRescue()
{
	AAuraCharacter* Character = GetCharacter();
	if (IsValid(RescueTarget))
	{
		UAuraReviveComponent* Other = RescueTarget->GetReviveComponent();
		if (Other && Other->Channel.Rescuer == Character)
		{
			Other->Channel = FAuraRescueChannel();
			RescueTarget->ForceNetUpdate();
		}
	}
	const bool bWasRescuing = RescueTarget != nullptr;
	if (bWasRescuing && Character)
		if (AAuraPlayerController* Controller = Cast<AAuraPlayerController>(Character->GetController()))
			Controller->ClientRescueChannelEnded(ActiveRequestId);
	RescueTarget = nullptr;
	ActiveRequestId = 0;
	if (bWasRescuing && Character)
	{
		if (!Character->IsDead()) Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Character->ForceNetUpdate();
	}
}

void UAuraReviveComponent::CancelAllRescueLinks()
{
	ClearActiveRescue();
	if (IsValid(Channel.Rescuer))
	{
		if (UAuraReviveComponent* Other = Channel.Rescuer->GetReviveComponent())
			Other->ClearActiveRescue();
	}
	Channel = FAuraRescueChannel();
}

void UAuraReviveComponent::EndProtection()
{
	if (!GetOwner()->HasAuthority() || ProtectionEndServerTime == 0.f) return;
	ProtectionEndServerTime = 0.f;
	GetOwner()->ForceNetUpdate();
}

void UAuraReviveComponent::OnAbilityActivated(UGameplayAbility* Ability)
{
	EndProtection();
}

void UAuraReviveComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AAuraCharacter* Character = GetCharacter();
	if (!Character || !Character->HasAuthority()) return;
	if (!Character->IsDead() && Character->GetCharacterMovement()->IsMovingOnGround()
		&& Character->GetCharacterMovement()->CurrentFloor.IsWalkableFloor())
		LastSafeTransform = Character->GetActorTransform();
	if (!RescueTarget) return;
	UAuraReviveComponent* Other = IsValid(RescueTarget) ? RescueTarget->GetReviveComponent() : nullptr;
	if (!Other || !CanRescue(RescueTarget) || Other->Channel.Rescuer != Character
		|| Other->Channel.RequestId != ActiveRequestId
		|| FVector::DistSquared(Character->GetActorLocation(), RescueStartLocation) > FMath::Square(25.f))
	{
		CancelRescue();
		return;
	}
	if (ServerTime() - Other->Channel.StartServerTime < RescueDuration) return;
	AAuraCharacter* Target = RescueTarget;
	const FTransform Destination = Other->RescueTransform;
	CancelRescue();
	Target->ReviveAtTransform(Destination, ReviveVitalFraction, GetUpProtectionDuration);
	if (AAuraPlayerState* State = Target->GetPlayerState<AAuraPlayerState>()) State->SetPlayerDead(false);
	UE_LOG(LogAuraRescue, Display, TEXT("Rescue completed. Rescuer=%s Target=%s Vitals=20%%"),
		*Character->GetName(), *Target->GetName());
}
