// Copyright yosep

#include "Animation/AuraAnimInstance.h"

#include "Animation/AnimationPoseData.h"
#include "Animation/BlendSpace.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AuraAnimInstance)

FAuraAnimInstanceProxy::FAuraAnimInstanceProxy()
{
}

FAuraAnimInstanceProxy::FAuraAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimSingleNodeInstanceProxy(InAnimInstance)
{
}

void FAuraAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimSingleNodeInstanceProxy::Initialize(InAnimInstance);
	RegisterSlotNodeWithAnimInstance(TEXT("DefaultSlot"));
	SlotNodeWeight = 0.f;
	SourceWeight = 1.f;
	TotalNodeWeight = 0.f;
}

void FAuraAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	UAuraAnimInstance* AuraAnimInstance = Cast<UAuraAnimInstance>(InAnimInstance);
	if (!AuraAnimInstance) return;

	ACharacter* Character = Cast<ACharacter>(AuraAnimInstance->TryGetPawnOwner());
	AuraAnimInstance->GroundSpeed = Character ? Character->GetVelocity().Size2D() : 0.f;
	AuraAnimInstance->bIsInAir = Character && Character->GetCharacterMovement()->IsFalling();
	SetBlendSpacePosition(FVector(FMath::Clamp(AuraAnimInstance->GroundSpeed, 0.f, 500.f), 0.f, 0.f));
}

void FAuraAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	FAnimSingleNodeInstanceProxy::UpdateAnimationNode(InContext);
	GetSlotWeight(TEXT("DefaultSlot"), SlotNodeWeight, SourceWeight, TotalNodeWeight);
	UpdateSlotNodeWeight(TEXT("DefaultSlot"), SlotNodeWeight, InContext.GetFinalBlendWeight());
}

bool FAuraAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (SlotNodeWeight <= ZERO_ANIMWEIGHT_THRESH)
	{
		return FAnimSingleNodeInstanceProxy::Evaluate(Output);
	}

	FPoseContext SourceContext(Output);
	FAnimSingleNodeInstanceProxy::Evaluate(SourceContext);
	const FAnimationPoseData SourcePoseData(SourceContext);
	FAnimationPoseData OutputPoseData(Output);
	SlotEvaluatePose(TEXT("DefaultSlot"), SourcePoseData, SourceWeight,
		OutputPoseData, SlotNodeWeight, TotalNodeWeight);
	return true;
}

void UAuraAnimInstance::SetMovementBlendSpace(UBlendSpace* InBlendSpace)
{
	MovementBlendSpace = InBlendSpace;
	SetAnimationAsset(InBlendSpace, true, 1.f);
}

float UAuraAnimInstance::GetDefaultSlotWeight() const
{
	float SlotWeight = 0.f;
	float SourceWeight = 0.f;
	float TotalWeight = 0.f;
	GetProxyOnGameThread<FAuraAnimInstanceProxy>().GetSlotWeight(
		TEXT("DefaultSlot"), SlotWeight, SourceWeight, TotalWeight);
	return SlotWeight;
}

void UAuraAnimInstance::Montage_Advance(float DeltaTime)
{
	// The single-node implementation writes montage time into the BlendSpace
	// clock. Keep the locomotion asset advancing independently of the slot.
	UAnimInstance::Montage_Advance(DeltaTime);
}

FAnimInstanceProxy* UAuraAnimInstance::CreateAnimInstanceProxy()
{
	return new FAuraAnimInstanceProxy(this);
}

void UAuraAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FAuraAnimInstanceProxy*>(InProxy);
}
